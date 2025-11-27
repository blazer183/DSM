#ifndef OS_LOCK_TABLE_H
#define OS_LOCK_TABLE_H

#include <cstddef>
#include <cstdint>

#if defined(__cplusplus)
#include "os/table_base.hpp"

struct LockRecord {
    LockRecord() noexcept : owner_id(-1), locked(false) {}
    explicit LockRecord(int owner) noexcept : owner_id(owner), locked(true) {}

    int owner_id;
    bool locked;
};

class LockTable final : public TableBase<int, LockRecord> {
public:
    LockTable() = default;

    using TableBase::Clear;
    using TableBase::Find;
    using TableBase::Insert;
    using TableBase::Remove;
    using TableBase::Size;
    using TableBase::Update;

    bool TryAcquire(int lock_id, int requester)
    {
        auto *record = Find(lock_id);
        if (record == nullptr) {
            return Insert(lock_id, LockRecord(requester));
        }
        if (!record->locked || record->owner_id == requester) {
            record->owner_id = requester;
            record->locked = true;
            return true;
        }
        return false;
    }

    bool Release(int lock_id, int requester)
    {
        auto *record = Find(lock_id);
        if (record == nullptr || record->owner_id != requester)
            return false;
        record->locked = false;
        return true;
    }
};

#else

typedef struct LockTable LockTable;
typedef struct LockRecord LockRecord;

#endif /* defined(__cplusplus) */

#endif /* OS_LOCK_TABLE_H */
