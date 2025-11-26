#ifndef OS_LOCK_TABLE_H
#define OS_LOCK_TABLE_H

#include <cstddef>
#include <cstdint>

#if defined(__cplusplus)
#include "os/table_base.hpp"

class LockRecord {
public:
    LockRecord() noexcept;
    explicit LockRecord(int owner_id) noexcept;

    int OwnerId() const noexcept;
    void SetOwnerId(int owner_id) noexcept;

    bool IsLocked() const noexcept;
    void SetLocked(bool locked) noexcept;

private:
    int owner_id_;
    bool locked_;
};

class LockTable final : public TableBase<int, LockRecord> {
public:
    LockTable();
    ~LockTable();

    LockTable(const LockTable &) = delete;
    LockTable &operator=(const LockTable &) = delete;
    LockTable(LockTable &&) noexcept;
    LockTable &operator=(LockTable &&) noexcept;

    using TableBase::Clear;
    using TableBase::Find;
    using TableBase::Insert;
    using TableBase::Remove;
    using TableBase::Size;
    using TableBase::Update;

    bool TryAcquire(int lock_id, int requester_id);
    bool Release(int lock_id, int requester_id);
};

#else

typedef struct LockRecord LockRecord;
typedef struct LockTable LockTable;

#endif /* defined(__cplusplus) */

#endif /* OS_LOCK_TABLE_H */
