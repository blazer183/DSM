#ifndef OS_LOCK_TABLE_H
#define OS_LOCK_TABLE_H

#include <cstddef>
#include <cstdint>
#include <limits>

#include "os/table_base.hpp"

struct LockRecord {
    LockRecord() noexcept : owner_id(-1), locked(false) {}
    explicit LockRecord(int owner) noexcept : owner_id(owner), locked(true) {}

    int owner_id;
    bool locked;
};

class LockTable final : public TableBase<int, LockRecord> {
public:
    using Base = TableBase<int, LockRecord>;

    explicit LockTable(std::size_t capacity = 0)
        : Base(capacity == 0 ? std::numeric_limits<std::size_t>::max() : capacity)
    {
    }

    using Base::Clear;
    using Base::Find;
    using Base::Insert;
    using Base::Remove;
    using Base::Size;
    using Base::Update;
    using Base::LockAcquire;
    using Base::LockRelease;
};


#endif /* OS_LOCK_TABLE_H */
