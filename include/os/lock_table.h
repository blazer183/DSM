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
    pthread_mutex_t mutex;                // 局部锁
    uint32_t invalid_set_count { 0 };     // 失效页计数
    std::vector<int> invalid_page_list;   // 失效页列表

    LockRecord() noexcept {
        ::pthread_mutex_init(&mutex, nullptr);
    }

    ~LockRecord() noexcept {
        ::pthread_mutex_destroy(&mutex);
    }

    LockRecord(const LockRecord &other)
        : invalid_set_count(other.invalid_set_count),
          invalid_page_list(other.invalid_page_list)
    {
        ::pthread_mutex_init(&mutex, nullptr);
    }

    LockRecord &operator=(const LockRecord &other) {
        if (this != &other) {
            ::pthread_mutex_destroy(&mutex);
            ::pthread_mutex_init(&mutex, nullptr);
            invalid_set_count = other.invalid_set_count;
            invalid_page_list = other.invalid_page_list;
        }
        return *this;
    }

    LockRecord(LockRecord &&other) noexcept
        : invalid_set_count(other.invalid_set_count),
          invalid_page_list(std::move(other.invalid_page_list))
    {
        ::pthread_mutex_init(&mutex, nullptr);
    }

    LockRecord &operator=(LockRecord &&other) noexcept {
        if (this != &other) {
            ::pthread_mutex_destroy(&mutex);
            ::pthread_mutex_init(&mutex, nullptr);
            invalid_set_count = other.invalid_set_count;
            invalid_page_list = std::move(other.invalid_page_list);
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
    using Base::Remove;
    using Base::Size;
    using Base::Update;
    using Base::GlobalMutexLock;
    using Base::GlobalMutexUnlock;

    bool LocalMutexLock(int lock_id) noexcept {
        auto *record = Find(lock_id);
        if (record == nullptr) {
            LockRecord new_record;
            if (!Insert(lock_id, new_record)) {
                return false;
            }
            record = Find(lock_id);
            if (record == nullptr) {
                return false;
            }
        }
        int rc = ::pthread_mutex_lock(&record->mutex);
        return rc == 0;
    }

    bool LocalMutexUnlock(int lock_id) noexcept {
        auto *record = Find(lock_id);
        if (record == nullptr) {
            return false;
        }
        int rc = ::pthread_mutex_unlock(&record->mutex);
        return rc == 0;
    }
};


#endif /* OS_LOCK_TABLE_H */
