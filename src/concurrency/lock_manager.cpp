//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lock_manager.cpp
//
// Identification: src/concurrency/lock_manager.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"

#include <utility>
#include <vector>

namespace bustub {

auto LockManager::LockShared(Transaction *txn, const RID &rid) -> bool {

  if (CheckAbort(txn)) {
    return false;
  }

  if (txn->GetIsolationLevel() == IsolationLevel::READ_UNCOMMITTED) {
    // read uncommitted don't need LockShared.
    txn->SetState(TransactionState::ABORTED);
    return false;
  }

  // cannot request lock on shrinking stage
  if (txn->GetState() != TransactionState::GROWING) {
    txn->SetState(TransactionState::ABORTED);
    throw TransactionAbortException(txn->GetTransactionId(), AbortReason::LOCK_ON_SHRINKING);
  }

  // if rid already shared locked by the txn
  if (txn->IsSharedLocked(rid)) {
    return true;
  }

  // send request for lock on rid
  std::unique_lock<std::mutex> guard(latch_);
  LockRequestQueue *lock_queue = &lock_table_[rid];
  LockRequest lock_request = LockRequest(txn->GetTransactionId(), LockMode::SHARED);
  lock_queue->request_queue_.emplace_back(lock_request);
  txn->GetSharedLockSet()->emplace(rid);

  // wait until lock granted for all requests in txn's request queue
  while (NeedWait(txn, lock_queue)) {
    lock_queue->cv_.wait(guard);
    LOG_DEBUG("%d: Awake and check itself.", txn->GetTransactionId());
    if (CheckAbort(txn)) {
      return false;
    }
  }

  for (auto &iter : lock_queue->request_queue_) {
    if (iter.txn_id_ == txn->GetTransactionId()) {
      iter.granted_ = true;
    }
  }
  txn->SetState(TransactionState::GROWING);
  return true;

}

auto LockManager::LockExclusive(Transaction *txn, const RID &rid) -> bool {

  if (CheckAbort(txn)) {
    return false;
  }

  if (txn->GetState() != TransactionState::GROWING) {
    txn->SetState(TransactionState::ABORTED);
    throw TransactionAbortException(txn->GetTransactionId(), AbortReason::LOCK_ON_SHRINKING);
  }

  if (txn->IsExclusiveLocked(rid)) {
    return true;
  }

  std::unique_lock<std::mutex> guard(latch_);
  LockRequestQueue *lock_queue = &lock_table_[rid];
  LockRequest lock_request = LockRequest(txn->GetTransactionId(), LockMode::EXCLUSIVE);
  lock_queue->request_queue_.emplace_back(lock_request);
  txn->GetExclusiveLockSet()->emplace(rid);

  while (NeedWait(txn, lock_queue)) {
    LOG_DEBUG("%d: Wait for exclusive lock", txn->GetTransactionId());
    lock_queue->cv_.wait(guard);
    LOG_DEBUG("%d: Awake and check itself.", txn->GetTransactionId());
    if (CheckAbort(txn)) {
      return false;
    }
  }

  // grant exclusive locks for all txn's requests
  LOG_DEBUG("%d: Get exclusive lock", txn->GetTransactionId());
  for (auto &iter : lock_queue->request_queue_) {
    if (iter.txn_id_ == txn->GetTransactionId()) {
      iter.granted_ = true;
    }
  }
  
  txn->SetState(TransactionState::GROWING);
  return true;
}

auto LockManager::LockUpgrade(Transaction *txn, const RID &rid) -> bool {

  if (CheckAbort(txn)) {
    return false;
  }

  if (txn->GetState() != TransactionState::GROWING) {
    txn->SetState(TransactionState::ABORTED);
    throw TransactionAbortException(txn->GetTransactionId(), AbortReason::UPGRADE_CONFLICT);
  }

  if (txn->IsExclusiveLocked(rid)) {
    return true;
  }

  std::unique_lock<std::mutex> guard(latch_);

  LockRequestQueue *lock_queue = &lock_table_[rid];

  while (NeedWaitUpdate(txn, lock_queue)) {
    lock_queue->cv_.wait(guard);
    if (CheckAbort(txn)) {
      return false;
    }
  }
  
  for (auto iter : lock_queue->request_queue_) {
    if (iter.txn_id_ == txn->GetTransactionId()) {
      iter.granted_ = true;
      iter.lock_mode_ = LockMode::EXCLUSIVE;
      txn->SetState(TransactionState::GROWING);
      txn->GetSharedLockSet()->erase(rid);
      txn->GetExclusiveLockSet()->emplace(rid);
      break;
    }
  }
  return true;
}

auto LockManager::Unlock(Transaction *txn, const RID &rid) -> bool {

  LOG_DEBUG("%d: Unlock", txn->GetTransactionId());
  if (!txn->IsSharedLocked(rid) && !txn->IsExclusiveLocked(rid)) {
    return false;
  }

  
  std::unique_lock<std::mutex> guard(latch_);
  LockRequestQueue &lock_queue = lock_table_[rid];
  // if txn is unpgrading lock
  if (lock_queue.upgrading_ == txn->GetTransactionId()) {
    lock_queue.upgrading_ = INVALID_TXN_ID;
  }
  // try to find txn's request to lock rid
  bool found = false;
  for (auto iter = lock_queue.request_queue_.begin(); iter != lock_queue.request_queue_.end(); iter++) {
    if (iter->txn_id_ == txn->GetTransactionId()) {
      found = true;
      lock_queue.request_queue_.erase(iter);

      lock_queue.cv_.notify_all();
      break;
    }
  }

  if (!found) {
    return false;
  }

  if (txn->GetState() == TransactionState::GROWING && txn->GetIsolationLevel() == IsolationLevel::REPEATABLE_READ) {
    txn->SetState(TransactionState::SHRINKING);
  }

  txn->GetSharedLockSet()->erase(rid);
  txn->GetExclusiveLockSet()->erase(rid);
  return true;
}


bool LockManager::NeedWait(Transaction *txn, LockRequestQueue *lock_queue) {

  auto self = lock_queue->request_queue_.back();

  auto first_iter = lock_queue->request_queue_.begin();
  if (self.lock_mode_ == LockMode::SHARED) {
    if (first_iter->txn_id_ == txn->GetTransactionId() || first_iter->lock_mode_ == LockMode::SHARED) {
      return false;
    }
  } else {
    if (first_iter->txn_id_ == txn->GetTransactionId()) {
      return false;
    }
  }

  // need wait, try to prevent it.
  bool need_wait = false;
  bool has_aborted = false;

  for (auto iter = first_iter; iter->txn_id_ != txn->GetTransactionId(); iter++) {
    // wound-wait
    if (iter->txn_id_ > txn->GetTransactionId()) {
      bool situation1 = self.lock_mode_ == LockMode::SHARED && iter->lock_mode_ == LockMode::EXCLUSIVE;
      bool situation2 = self.lock_mode_ == LockMode::EXCLUSIVE;
      if (situation1 || situation2) {
        // abort younger, older is not allowed to wait
        Transaction *younger_txn = TransactionManager::GetTransaction(iter->txn_id_);
        if (younger_txn->GetState() != TransactionState::ABORTED) {
          LOG_DEBUG("%d: Abort %d", txn->GetTransactionId(), iter->txn_id_);
          younger_txn->SetState(TransactionState::ABORTED);
          has_aborted = true;
        }
      }
      continue;
    }

    // let younger wait
    if (self.lock_mode_ == LockMode::EXCLUSIVE) {
      need_wait = true;
    }

    if (iter->lock_mode_ == LockMode::EXCLUSIVE) {
      need_wait = true;
    }
  }

  if (has_aborted) {
    lock_queue->cv_.notify_all();
  }

  return need_wait;
}
  bool LockManager::CheckAbort(Transaction *txn) { return txn->GetState() == TransactionState::ABORTED; }

  bool LockManager::NeedWaitUpdate(Transaction *txn, LockRequestQueue *lock_queue) {

    bool need_wait = false;
    bool has_aborted = false;

    for (auto iter = lock_queue->request_queue_.begin(); iter->txn_id_ != txn->GetTransactionId(); iter++) {
      if (iter->txn_id_ > txn->GetTransactionId()) {
        LOG_DEBUG("%d: Abort %d", txn->GetTransactionId(), iter->txn_id_);
        Transaction *younger_txn = TransactionManager::GetTransaction(iter->txn_id_);
        if (younger_txn->GetState() != TransactionState::ABORTED) {
          younger_txn->SetState(TransactionState::ABORTED);
          has_aborted = true;
        }
        continue;
      }

      // curr txn is younger, need to wait for older txn
      need_wait = true;
    }

    if (has_aborted) {
      lock_queue->cv_.notify_all();
    }

    return need_wait;
  }
}  // namespace bustub
