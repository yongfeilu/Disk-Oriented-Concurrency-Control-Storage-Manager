//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"

#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, DiskManager *disk_manager,
                                                     LogManager *log_manager)
    : BufferPoolManagerInstance(pool_size, 1, 0, disk_manager, log_manager) {}

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, uint32_t num_instances, uint32_t instance_index,
                                                     DiskManager *disk_manager, LogManager *log_manager)
    : pool_size_(pool_size),
      num_instances_(num_instances),
      instance_index_(instance_index),
      next_page_id_(instance_index),
      disk_manager_(disk_manager),
      log_manager_(log_manager) {
  BUSTUB_ASSERT(num_instances > 0, "If BPI is not part of a pool, then the pool size should just be 1");
  BUSTUB_ASSERT(
      instance_index < num_instances,
      "BPI index cannot be greater than the number of BPIs in the pool. In non-parallel case, index should just be 1.");
  // We allocate a consecutive memory space for the buffer pool.
  pages_ = new Page[pool_size_];
  replacer_ = new LRUReplacer(pool_size);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete replacer_;
}

auto BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) -> bool {
  // Make sure you call DiskManager::WritePage!
  std::scoped_lock lock{latch_};
  // if not valid page id
  if (page_id == INVALID_PAGE_ID) {
    return false;
  }

  auto iter = page_table_.find(page_id);
  // if not in pool
  if (iter == page_table_.end()) {
    return false;
  }

  frame_id_t frame_id = iter->second;
  Page *page = &pages_[frame_id];
  disk_manager_->WritePage(page_id, page->GetData());
  page->is_dirty_ = false;

  return true;
}

void BufferPoolManagerInstance::FlushAllPgsImp() {
  // You can do it!
  std::scoped_lock lock{latch_};
  auto iter = page_table_.begin();
  while (iter != page_table_.end()) {
    page_id_t page_id = iter->first;
    frame_id_t frame_id = iter->second;
    Page *page = &pages_[frame_id];
    // check if page is dirty
    if (page->IsDirty()) {
      disk_manager_->WritePage(page_id, page->GetData());
    }
    iter++;
  }
}

auto BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) -> Page * {
  // 0.   Make sure you call AllocatePage!
  // 1.   If all the pages in the buffer pool are pinned, return nullptr.
  // 2.   Pick a victim page P from either the free list or the replacer. Always pick from the free list first.
  // 3.   Update P's metadata, zero out memory and add P to the page table.
  // 4.   Set the page ID output parameter. Return a pointer to P.

  std::scoped_lock lock{latch_};
  frame_id_t frame_id = -1;
  Page *page = nullptr;

  // get a free frame from the pool to hold the new page
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
    page = &pages_[frame_id];
  } else if (replacer_->Victim(&frame_id)) {
    page = &pages_[frame_id];
    if (page->IsDirty()) {
      disk_manager_->WritePage(page->GetPageId(), page->GetData());
    }
    page_table_.erase(page->GetPageId());
  } else {
    // all pages in the pool are pinned
    return nullptr;
  }

  // construct the new page
  page_id_t new_page_id = AllocatePage();
  page->page_id_ = new_page_id;
  page->pin_count_ = 1;
  page->is_dirty_ = false;
  page->ResetMemory();

  // map the page_id to frame in the bool
  page_table_[new_page_id] = frame_id;
  *page_id = new_page_id;
  replacer_->Pin(frame_id);

  return page;
}

auto BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) -> Page * {
  // 1.     Search the page table for the requested page (P).
  // 1.1    If P exists, pin it and return it immediately.
  // 1.2    If P does not exist, find a replacement page (R) from either the free list or the replacer.
  //        Note that pages are always found from the free list first.
  // 2.     If R is dirty, write it back to the disk.
  // 3.     Delete R from the page table and insert P.
  // 4.     Update P's metadata, read in the page content from disk, and then return a pointer to P.
  std::scoped_lock lock{latch_};
  auto iter = page_table_.find(page_id);
  // if the page is in the pool
  if (iter != page_table_.end()) {
    frame_id_t frame_id = iter->second;
    Page *page = &pages_[frame_id];
    page->pin_count_++;
    replacer_->Pin(frame_id);
    return page;
  }

  // if the page is not in the pool
  // fetch the page
  frame_id_t frame_id = -1;
  Page *page = nullptr;
  // first consider free list for the page
  // if the pool still has free frames for the page
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
    page = &pages_[frame_id];
    // free frames in pool, we can directly
    // read page from disk to the pool

    // if no free frames in the pool,
    // then turn to replacer to find a place for the page
  } else if (replacer_->Victim(&frame_id)) {
    page = &pages_[frame_id];
    // write the page to disk if dirty
    if (page->IsDirty()) {
      disk_manager_->WritePage(page->GetPageId(), page->GetData());
    }
    page_table_.erase(page->GetPageId());
  } else {
    return nullptr;
  }

  // update page metadata, fetch it from disk
  page->page_id_ = page_id;
  page->pin_count_ = 1;
  page->is_dirty_ = false;
  disk_manager_->ReadPage(page_id, page->GetData());
  page_table_[page_id] = frame_id;
  replacer_->Pin(frame_id);

  return page;
}

auto BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) -> bool {
  // 0.   Make sure you call DeallocatePage!
  // 1.   Search the page table for the requested page (P).
  // 1.   If P does not exist, return true.
  // 2.   If P exists, but has a non-zero pin-count, return false. Someone is using the page.
  // 3.   Otherwise, P can be deleted. Remove P from the page table, reset its metadata and return it to the free list.
  std::scoped_lock lock{latch_};
  DeallocatePage(page_id);
  // if not found in the pool
  auto iter = page_table_.find(page_id);
  if (iter == page_table_.end()) {
    // DeallocatePage(page_id);
    return true;
  }

  // find the page
  frame_id_t frame_id = iter->second;
  Page *page = &pages_[frame_id];
  if (page->GetPinCount() > 0) {
    return false;
  }

  // write page (if dirty) to disk before evicting it from pool
  if (page->IsDirty()) {
    disk_manager_->WritePage(page->GetPageId(), page->GetData());
  }

  // the frame is now free, so no available for replacement
  // thus removing it from lru_replacer
  replacer_->Pin(frame_id);
  page_table_.erase(page->page_id_);
  page->is_dirty_ = false;
  page->pin_count_ = 0;
  page->page_id_ = INVALID_PAGE_ID;
  page->ResetMemory();
  free_list_.push_back(frame_id);

  return true;
}

auto BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty) -> bool {
  std::scoped_lock lock{latch_};
  auto iter = page_table_.find(page_id);
  // if not in the buffer poll, just return false
  if (iter == page_table_.end()) {
    return false;
  }

  // if in the bool, find the page
  frame_id_t frame_id = iter->second;
  Page *page = &pages_[frame_id];
  // if it hasn't been pined before, return false
  if (page->GetPinCount() <= 0) {
    return false;
  }

  page->pin_count_--;
  // check in order to avoid:
  // when is_dirty == false, page->is_dirty_  == true
  // then page->is_dirty_ = is_dirty ==> page->is_dirty_ = false
  if (is_dirty) {
    page->is_dirty_ = true;
  }
  if (page->GetPinCount() <= 0) {
    replacer_->Unpin(frame_id);
  }
  return true;
}

auto BufferPoolManagerInstance::AllocatePage() -> page_id_t {
  const page_id_t next_page_id = next_page_id_;
  next_page_id_ += num_instances_;
  ValidatePageId(next_page_id);
  return next_page_id;
}

void BufferPoolManagerInstance::ValidatePageId(const page_id_t page_id) const {
  assert(page_id % num_instances_ == instance_index_);  // allocated pages mod back to this BPI
}

}  // namespace bustub
