#ifndef OS_LOCK_TABLE_H
#define OS_LOCK_TABLE_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <pthread.h>

#include "os/table_base.hpp"

// LockRecord 以 pthread 互斥锁为核心，辅以失效集合统计
struct LockRecord {
    pthread_mutex_t lock_id;  // 与 map 的 key 保持一致的逻辑锁（按需绑定）
    uint32_t invalid_set_count { 0 };
    std::vector<int> invalid_page_list;

    int owner_id { -1 };     // 兼容旧实现：记录当前持有者
    bool locked { false };

    LockRecord() noexcept { ::pthread_mutex_init(&lock_id, nullptr); }
    explicit LockRecord(int owner) noexcept
        : invalid_set_count(0), invalid_page_list(), owner_id(owner), locked(true)
    {
        ::pthread_mutex_init(&lock_id, nullptr);
    }

    ~LockRecord() noexcept { ::pthread_mutex_destroy(&lock_id); }

    LockRecord(const LockRecord &other)
        : invalid_set_count(other.invalid_set_count),
          invalid_page_list(other.invalid_page_list),
          owner_id(other.owner_id),
          locked(other.locked)
    {
        ::pthread_mutex_init(&lock_id, nullptr);
    }

    LockRecord &operator=(const LockRecord &other) {
        if (this != &other) {
            ::pthread_mutex_destroy(&lock_id);
            ::pthread_mutex_init(&lock_id, nullptr);
            invalid_set_count = other.invalid_set_count;
            invalid_page_list = other.invalid_page_list;
            owner_id = other.owner_id;
            locked = other.locked;
        }
        return *this;
    }

    LockRecord(LockRecord &&other) noexcept
        : invalid_set_count(other.invalid_set_count),
          invalid_page_list(std::move(other.invalid_page_list)),
          owner_id(other.owner_id),
          locked(other.locked)
    {
        ::pthread_mutex_init(&lock_id, nullptr);
    }

    LockRecord &operator=(LockRecord &&other) noexcept {
        if (this != &other) {
            ::pthread_mutex_destroy(&lock_id);
            ::pthread_mutex_init(&lock_id, nullptr);
            invalid_set_count = other.invalid_set_count;
            invalid_page_list = std::move(other.invalid_page_list);
            owner_id = other.owner_id;
            locked = other.locked;
        }
        return *this;
    }
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
    using Base::LockAcquire;
    using Base::LockRelease;
    using Base::Remove;
    using Base::Size;
    using Base::Update;

    // 表级锁的封装：直接复用 Base::LockAcquire/LockRelease

    // 对指定 lock_id 的互斥进行加锁，必要时创建记录
    bool Lock(int lock_id) noexcept {
        LockAcquire();
        auto *record = Find(lock_id);
        if (record == nullptr) {
            LockRecord new_record;
            if (!Insert(lock_id, new_record)) {
                LockRelease();
                return false;
            }
            record = Find(lock_id);
            if (record == nullptr) {
                LockRelease();
                return false;
            }
        }
        int rc = ::pthread_mutex_lock(&record->lock_id);
        LockRelease();
        return rc == 0;
    }

    bool Unlock(int lock_id) noexcept {
        LockAcquire();
        auto *record = Find(lock_id);
        if (record == nullptr) {
            LockRelease();
            return false;
        }
        int rc = ::pthread_mutex_unlock(&record->lock_id);
        LockRelease();
        return rc == 0;
    }
};


#endif /* OS_LOCK_TABLE_H */
