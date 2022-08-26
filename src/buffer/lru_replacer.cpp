//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_replacer.cpp
// frames and pages (in frames) that can be replaced
// Identification: src/buffer/lru_replacer.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_replacer.h"

namespace bustub {

LRUReplacer::LRUReplacer(size_t num_pages) : max_size_(num_pages) {}

LRUReplacer::~LRUReplacer() = default;

auto LRUReplacer::Victim(frame_id_t *frame_id) -> bool {
  std::scoped_lock lock{mutex_};
  if (lru_list_.empty()) {
    return false;
  }

  // remove the least recently used frame
  // (at the end of list)
  *frame_id = lru_list_.back();
  lru_hash_.erase(lru_list_.back());
  lru_list_.pop_back();
  return true;
}

void LRUReplacer::Pin(frame_id_t frame_id) {
  std::scoped_lock lock{mutex_};
  auto iter = lru_hash_.find(frame_id);
  if (iter == lru_hash_.end()) {
    return;
  }

  lru_list_.erase(iter->second);
  lru_hash_.erase(iter);
}

void LRUReplacer::Unpin(frame_id_t frame_id) {
  std::scoped_lock lock{mutex_};
  // avoid repeatition
  if (lru_hash_.count(frame_id) != 0) {
    return;
  }
  // excceed capacity
  if (lru_list_.size() >= max_size_) {
    return;
  }

  // add to the list front end
  lru_list_.push_front(frame_id);
  lru_hash_[frame_id] = lru_list_.begin();
}

auto LRUReplacer::Size() -> size_t {
  std::scoped_lock lock(mutex_);
  return lru_list_.size();
}

}  // namespace bustub
