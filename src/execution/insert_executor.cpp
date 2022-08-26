//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "execution/executors/insert_executor.h"

namespace bustub {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      catalog_(exec_ctx->GetCatalog()),
      table_info_(catalog_->GetTable(plan->TableOid())),
      table_heap_(table_info_->table_.get()) {}

void InsertExecutor::Init() {
    if (!plan_->IsRawInsert()) {
        child_executor_->Init();
    } else {
        iter_ = plan_->RawValues().begin();
    }
}

auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
    std::vector<Tuple> tuples;

    // check raw insert
    if (!plan_->IsRawInsert()) {
        // execute child executor first
        if (!child_executor_->Next(tuple, rid)) {
            return false;
        }
    } else {
        if (iter_ == plan_->RawValues().end()) {
            return false;
        }
        *tuple = Tuple(*iter_, &table_info_->schema_);
        iter_++;
    }

    // insert tuple
    if (!table_heap_->InsertTuple(*tuple, rid, exec_ctx_->GetTransaction())) {
        LOG_DEBUG("INSERT FAIL");
        return false;
    }

    // update index
    Transaction *txn = GetExecutorContext()->GetTransaction();
    LockManager *lock_mgr = GetExecutorContext()->GetLockManager();
    // get write lock
    if (txn->IsSharedLocked(*rid)) {
        if (!lock_mgr->LockUpgrade(txn, *rid)) {
            throw TransactionAbortException(txn->GetTransactionId(), AbortReason::DEADLOCK);
        }
    } else {
        if (!lock_mgr->LockExclusive(txn, *rid)) {
            throw TransactionAbortException(txn->GetTransactionId(), AbortReason::DEADLOCK);
        }
    }

    // update index
    for (const auto &index : catalog_->GetTableIndexes(table_info_->name_)) {
        index->index_->InsertEntry(
            tuple->KeyFromTuple(table_info_->schema_, *index->index_->GetKeySchema(), index->index_->GetKeyAttrs()), *rid,
            exec_ctx_->GetTransaction());
    }

    // unlock write lock
    if (txn->GetIsolationLevel() != IsolationLevel::REPEATABLE_READ) {
        if (!lock_mgr->Unlock(txn, *rid)) {
            throw TransactionAbortException(txn->GetTransactionId(), AbortReason::DEADLOCK);
        }
    }

    return Next(tuple, rid);

}

}  // namespace bustub
