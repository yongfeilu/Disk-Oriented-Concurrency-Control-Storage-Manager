//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// extendible_hash_table.cpp
//
// Identification: src/container/hash/extendible_hash_table.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "common/exception.h"
#include "common/logger.h"
#include "common/rid.h"
#include "container/hash/extendible_hash_table.h"

namespace bustub {

template <typename KeyType, typename ValueType, typename KeyComparator>
HASH_TABLE_TYPE::ExtendibleHashTable(const std::string &name, BufferPoolManager *buffer_pool_manager,
                                     const KeyComparator &comparator, HashFunction<KeyType> hash_fn)
    : buffer_pool_manager_(buffer_pool_manager), comparator_(comparator), hash_fn_(std::move(hash_fn)) {
  //  implement me!
  directory_page_id_ = INVALID_PAGE_ID;
}

/*****************************************************************************
 * HELPERS
 *****************************************************************************/
/**
 * Hash - simple helper to downcast MurmurHash's 64-bit hash to 32-bit
 * for extendible hashing.
 *
 * @param key the key to hash
 * @return the downcasted 32-bit hash
 */
template <typename KeyType, typename ValueType, typename KeyComparator>
auto HASH_TABLE_TYPE::Hash(KeyType key) -> uint32_t {
  return static_cast<uint32_t>(hash_fn_.GetHash(key));
}

template <typename KeyType, typename ValueType, typename KeyComparator>
inline auto HASH_TABLE_TYPE::KeyToDirectoryIndex(KeyType key, HashTableDirectoryPage *dir_page) -> uint32_t {
  return Hash(key) & dir_page->GetGlobalDepthMask();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
inline auto HASH_TABLE_TYPE::KeyToPageId(KeyType key, HashTableDirectoryPage *dir_page) -> uint32_t {
  return dir_page->GetBucketPageId(KeyToDirectoryIndex(key, dir_page));
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto HASH_TABLE_TYPE::FetchDirectoryPage() -> HashTableDirectoryPage * {
  HashTableDirectoryPage *ret;
  // ?????????????????????????????????
  // avoid concurrency to create repeated directory page.
  init_lock_.lock();
  if (directory_page_id_ == INVALID_PAGE_ID) {
    // ?????????DirectoryPage
    page_id_t new_page_id_dir;
    Page *page = buffer_pool_manager_->NewPage(&new_page_id_dir);
    assert(page != nullptr);
    ret = reinterpret_cast<HashTableDirectoryPage *>(page->GetData());
    directory_page_id_ = new_page_id_dir;
    ret->SetPageId(directory_page_id_);
    // ?????????????????????directory???????????????bucket
    page_id_t new_page_id_buc;
    page = nullptr;
    page = buffer_pool_manager_->NewPage(&new_page_id_buc);
    assert(page != nullptr);
    ret->SetBucketPageId(0, new_page_id_buc);
    // ??????Unpin???????????????
    assert(buffer_pool_manager_->UnpinPage(new_page_id_dir, true));
    assert(buffer_pool_manager_->UnpinPage(new_page_id_buc, true));
  }
  init_lock_.unlock();

  // ???buffer???????????????
  assert(directory_page_id_ != INVALID_PAGE_ID);
  Page *page = buffer_pool_manager_->FetchPage(directory_page_id_);
  assert(page != nullptr);
  ret = reinterpret_cast<HashTableDirectoryPage *>(page->GetData());
  return ret;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
Page *HASH_TABLE_TYPE::FetchBucketPage(page_id_t bucket_page_id) {
  return AssertPage(buffer_pool_manager_->FetchPage(bucket_page_id));
}

template <typename KeyType, typename ValueType, typename KeyComparator>
HASH_TABLE_BUCKET_TYPE *HASH_TABLE_TYPE::RetrieveBucket(Page *page) {
  return reinterpret_cast<HASH_TABLE_BUCKET_TYPE *>(page->GetData());
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
auto HASH_TABLE_TYPE::GetValue(Transaction *transaction, const KeyType &key, std::vector<ValueType> *result) -> bool {
  table_latch_.RLock();
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();
  page_id_t bucket_page_id = KeyToPageId(key, dir_page);
  Page *bucket_page = FetchBucketPage(bucket_page_id);

  // ????????????
  bucket_page->RLatch();
  HASH_TABLE_BUCKET_TYPE *bucket = RetrieveBucket(bucket_page);
  bool ret = bucket->GetValue(key, comparator_, result);
  bucket_page->RUnlatch();

  // ??????Unpin
  assert(buffer_pool_manager_->UnpinPage(bucket_page_id, false));
  assert(buffer_pool_manager_->UnpinPage(dir_page->GetPageId(), false));

  table_latch_.RUnlock();
  return ret;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
auto HASH_TABLE_TYPE::Insert(Transaction *transaction, const KeyType &key, const ValueType &value) -> bool {
  table_latch_.RLock();
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();
  page_id_t bucket_page_id = KeyToPageId(key, dir_page);
  Page *page = FetchBucketPage(bucket_page_id);

  page->WLatch();

  HASH_TABLE_BUCKET_TYPE *bucket = RetrieveBucket(page);
  // ??????bucket???????????????????????????
  if (!bucket->IsFull()) {
    bool ret = bucket->Insert(key, value, comparator_);
    page->WUnlatch();
    assert(buffer_pool_manager_->UnpinPage(bucket_page_id, true));
    assert(buffer_pool_manager_->UnpinPage(dir_page->GetPageId(), false));
    return ret;
  }
  page->WUnlatch();
  // ???????????????
  assert(buffer_pool_manager_->UnpinPage(bucket_page_id, false));
  assert(buffer_pool_manager_->UnpinPage(dir_page->GetPageId(), false));
  table_latch_.RUnlock();
  return SplitInsert(transaction, key, value);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto HASH_TABLE_TYPE::SplitInsert(Transaction *transaction, const KeyType &key, const ValueType &value) -> bool {
  table_latch_.WLock();
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();
  int64_t split_bucket_index = KeyToDirectoryIndex(key, dir_page);
  uint32_t split_bucket_depth = dir_page->GetLocalDepth(split_bucket_index);

  // ???????????????????????????
  if (split_bucket_depth >= MAX_BUCKET_DEPTH) {
    assert(buffer_pool_manager_->UnpinPage(dir_page->GetPageId(), false));
    table_latch_.WUnlock();
    return false;
  }

  // ??????Directory??????????????????
  if (split_bucket_depth == dir_page->GetGlobalDepth()) {
    dir_page->IncrGlobalDepth();
  }

  // ??????local depth
  dir_page->IncrLocalDepth(split_bucket_index);

  // ????????????bucket??????????????????????????????????????????????????????
  page_id_t split_bucket_page_id = KeyToPageId(key, dir_page);
  Page *split_page = FetchBucketPage(split_bucket_page_id);

  split_page->WLatch();
  HASH_TABLE_BUCKET_TYPE *split_bucket = RetrieveBucket(split_page);
  uint32_t origin_array_size = split_bucket->NumReadable();
  MappingType *origin_array = split_bucket->GetArrayCopy();
  split_bucket->Reset();

  // ????????????image bucket??????????????????image bucket
  page_id_t image_bucket_page_id;
  Page *image_bucket_page = buffer_pool_manager_->NewPage(&image_bucket_page_id);
  assert(image_bucket_page != nullptr);
  HASH_TABLE_BUCKET_TYPE *image_bucket = RetrieveBucket(image_bucket_page);
  uint32_t split_image_bucket_index = dir_page->GetLocalHighBit(split_bucket_index);
  dir_page->SetLocalDepth(split_image_bucket_index, dir_page->GetLocalDepth(split_bucket_index));
  dir_page->SetBucketPageId(split_image_bucket_index, image_bucket_page_id);

  // ?????????????????????bucket??????????????????local depth???page
  uint32_t diff = 1 << dir_page->GetLocalDepth(split_bucket_index);
  for (uint32_t i = split_bucket_index; i >= 0; i -= diff) {
    dir_page->SetBucketPageId(i, split_bucket_page_id);
    dir_page->SetLocalDepth(i, dir_page->GetLocalDepth(split_bucket_index));
    if (i < diff) {
      // avoid negative
      break;
    }
  }
  for (uint32_t i = split_bucket_index; i < dir_page->Size(); i += diff) {
    dir_page->SetBucketPageId(i, split_bucket_page_id);
    dir_page->SetLocalDepth(i, dir_page->GetLocalDepth(split_bucket_index));
  }
  for (uint32_t i = split_image_bucket_index; i >= 0; i -= diff) {
    dir_page->SetBucketPageId(i, image_bucket_page_id);
    dir_page->SetLocalDepth(i, dir_page->GetLocalDepth(split_bucket_index));
    if (i < diff) {
      // avoid negative
      break;
    }
  }
  for (uint32_t i = split_image_bucket_index; i < dir_page->Size(); i += diff) {
    dir_page->SetBucketPageId(i, image_bucket_page_id);
    dir_page->SetLocalDepth(i, dir_page->GetLocalDepth(split_bucket_index));
  }

  // ??????????????????
  uint32_t mask = dir_page->GetLocalDepthMask(split_bucket_index);
  for (uint32_t i = 0; i < origin_array_size; i++) {
    MappingType tmp = origin_array[i];
    uint32_t target_bucket_index = Hash(tmp.first) & mask;
    page_id_t target_bucket_index_page = dir_page->GetBucketPageId(target_bucket_index);
    assert(target_bucket_index_page == split_bucket_page_id || target_bucket_index_page == image_bucket_page_id);
    // ????????????????????????hash????????????????????????bucket
    if (target_bucket_index_page == split_bucket_page_id) {
      assert(split_bucket->Insert(tmp.first, tmp.second, comparator_));
    } else {
      assert(image_bucket->Insert(tmp.first, tmp.second, comparator_));
    }
  }
  delete[] origin_array;
  split_page->WUnlatch();

  // Unpin
  assert(buffer_pool_manager_->UnpinPage(split_bucket_page_id, true));
  assert(buffer_pool_manager_->UnpinPage(image_bucket_page_id, true));
  assert(buffer_pool_manager_->UnpinPage(dir_page->GetPageId(), true));
  table_latch_.WUnlock();
  // ????????????????????????
  return Insert(transaction, key, value);
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
auto HASH_TABLE_TYPE::Remove(Transaction *transaction, const KeyType &key, const ValueType &value) -> bool {
  table_latch_.RLock();
  // ?????????bucket
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();
  page_id_t bucket_page_id = KeyToPageId(key, dir_page);
  uint32_t bucket_index = KeyToDirectoryIndex(key, dir_page);
  Page *page = FetchBucketPage(bucket_page_id);
  page->WLatch();
  HASH_TABLE_BUCKET_TYPE *bucket = RetrieveBucket(page);

  // ??????Key-value
  bool ret = bucket->Remove(key, value, comparator_);

  // ???????????????
  if (bucket->IsEmpty()) {
    page->WUnlatch();
    assert(buffer_pool_manager_->UnpinPage(bucket_page_id, true));
    assert(buffer_pool_manager_->UnpinPage(dir_page->GetPageId(), false));
    table_latch_.RUnlock();
    Merge(transaction, bucket_index);
    return ret;
  }

  page->WUnlatch();
  // Unpin
  assert(buffer_pool_manager_->UnpinPage(bucket_page_id, true));
  assert(buffer_pool_manager_->UnpinPage(dir_page->GetPageId(), false));
  table_latch_.RUnlock();
  return ret;
}

/*****************************************************************************
 * MERGE
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
void HASH_TABLE_TYPE::Merge(Transaction *transaction, uint32_t target_bucket_index) {
  table_latch_.WLock();
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();

  if (target_bucket_index >= dir_page->Size()) {
    // used to check to auto merge
    table_latch_.WUnlock();
    return;
  }

  page_id_t target_bucket_page_id = dir_page->GetBucketPageId(target_bucket_index);
  uint32_t image_bucket_index = dir_page->GetLocalHighBit(target_bucket_index);

  // local depth???0?????????????????????????????????
  uint32_t local_depth = dir_page->GetLocalDepth(target_bucket_index);
  if (local_depth == 0) {
    assert(buffer_pool_manager_->UnpinPage(dir_page->GetPageId(), false));
    table_latch_.WUnlock();
    return;
  }

  // ?????????bucket??????split image???????????????????????????
  if (local_depth != dir_page->GetLocalDepth(image_bucket_index)) {
    assert(buffer_pool_manager_->UnpinPage(dir_page->GetPageId(), false));
    table_latch_.WUnlock();
    return;
  }

  // ??????target bucket????????????????????????
  Page *target_page = FetchBucketPage(target_bucket_page_id);
  target_page->RLatch();
  HASH_TABLE_BUCKET_TYPE *target_bucket = RetrieveBucket(target_page);
  if (!target_bucket->IsEmpty()) {
    target_page->RUnlatch();
    assert(buffer_pool_manager_->UnpinPage(target_bucket_page_id, false));
    assert(buffer_pool_manager_->UnpinPage(dir_page->GetPageId(), false));
    table_latch_.WUnlock();
    return;
  }

  // ??????target bucket????????????bucket????????????
  target_page->RUnlatch();
  assert(buffer_pool_manager_->UnpinPage(target_bucket_page_id, false));
  assert(buffer_pool_manager_->DeletePage(target_bucket_page_id));

  // ??????target bucket???page???split image???page????????????target???split
  page_id_t image_bucket_page_id = dir_page->GetBucketPageId(image_bucket_index);
  dir_page->SetBucketPageId(target_bucket_index, image_bucket_page_id);
  dir_page->DecrLocalDepth(target_bucket_index);
  dir_page->DecrLocalDepth(image_bucket_index);
  assert(dir_page->GetLocalDepth(target_bucket_index) == dir_page->GetLocalDepth(image_bucket_index));

  // ????????????directory??????????????????target bucket page???bucket??????????????????split image bucket???page
  for (uint32_t i = 0; i < dir_page->Size(); i++) {
    if (dir_page->GetBucketPageId(i) == target_bucket_page_id || dir_page->GetBucketPageId(i) == image_bucket_page_id) {
      dir_page->SetBucketPageId(i, image_bucket_page_id);
      dir_page->SetLocalDepth(i, dir_page->GetLocalDepth(target_bucket_index));
    }
  }

  // ????????????Directory
  // ???????????????????????????????????????
  while (dir_page->CanShrink()) {
    dir_page->DecrGlobalDepth();
  }

  assert(buffer_pool_manager_->UnpinPage(dir_page->GetPageId(), true));
  table_latch_.WUnlock();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
Page *ExtendibleHashTable<KeyType, ValueType, KeyComparator>::AssertPage(Page *page) {
  assert(page != nullptr);
  return page;
}

/*****************************************************************************
 * GETGLOBALDEPTH - DO NOT TOUCH
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
auto HASH_TABLE_TYPE::GetGlobalDepth() -> uint32_t {
  table_latch_.RLock();
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();
  uint32_t global_depth = dir_page->GetGlobalDepth();
  assert(buffer_pool_manager_->UnpinPage(directory_page_id_, false, nullptr));
  table_latch_.RUnlock();
  return global_depth;
}

/*****************************************************************************
 * VERIFY INTEGRITY - DO NOT TOUCH
 *****************************************************************************/
template <typename KeyType, typename ValueType, typename KeyComparator>
void HASH_TABLE_TYPE::VerifyIntegrity() {
  table_latch_.RLock();
  HashTableDirectoryPage *dir_page = FetchDirectoryPage();
  dir_page->VerifyIntegrity();
  assert(buffer_pool_manager_->UnpinPage(directory_page_id_, false, nullptr));
  table_latch_.RUnlock();
}

/*****************************************************************************
 * TEMPLATE DEFINITIONS - DO NOT TOUCH
 *****************************************************************************/
template class ExtendibleHashTable<int, int, IntComparator>;

template class ExtendibleHashTable<GenericKey<4>, RID, GenericComparator<4>>;
template class ExtendibleHashTable<GenericKey<8>, RID, GenericComparator<8>>;
template class ExtendibleHashTable<GenericKey<16>, RID, GenericComparator<16>>;
template class ExtendibleHashTable<GenericKey<32>, RID, GenericComparator<32>>;
template class ExtendibleHashTable<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
