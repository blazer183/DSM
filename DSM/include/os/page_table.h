#ifndef OS_PAGE_TABLE_H
#define OS_PAGE_TABLE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <pthread.h>

#include "os/table_base.hpp"

struct PageRecord {
    int owner_id { -1 };
    int lock_id { 0 };              // 自增锁号，不必与页号一致
    pthread_mutex_t mutex;          // 用于顺序访问的互斥锁

    PageRecord() noexcept
        : owner_id(-1), lock_id(NextLockId())
    {
        ::pthread_mutex_init(&mutex, nullptr);
    }

    ~PageRecord() noexcept {
        ::pthread_mutex_destroy(&mutex);
    }

    PageRecord(const PageRecord &other)
        : owner_id(other.owner_id), lock_id(other.lock_id)
    {
        ::pthread_mutex_init(&mutex, nullptr);
    }

    PageRecord &operator=(const PageRecord &other) {
        if (this != &other) {
            ::pthread_mutex_destroy(&mutex);
            ::pthread_mutex_init(&mutex, nullptr);
            owner_id = other.owner_id;
            lock_id = other.lock_id;
        }
        return *this;
    }

    PageRecord(PageRecord &&other) noexcept
        : owner_id(other.owner_id), lock_id(other.lock_id)
    {
        ::pthread_mutex_init(&mutex, nullptr);
    }

    PageRecord &operator=(PageRecord &&other) noexcept {
        if (this != &other) {
            ::pthread_mutex_destroy(&mutex);
            ::pthread_mutex_init(&mutex, nullptr);
            owner_id = other.owner_id;
            lock_id = other.lock_id;
        }
        return *this;
    }

    int Lock() noexcept { return ::pthread_mutex_lock(&mutex); }
    int Unlock() noexcept { return ::pthread_mutex_unlock(&mutex); }

private:
    static int NextLockId() noexcept {
        static std::atomic<int> counter { 0 };
        return ++counter;
    }
};

class PageTable final : public TableBase<std::uintptr_t, PageRecord> {
public:
    using Base = TableBase<std::uintptr_t, PageRecord>;

    explicit PageTable(std::size_t capacity = 0)
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

    bool LockPage(std::uintptr_t page_addr) noexcept {
        LockAcquire();
        auto *record = Find(page_addr);
        if (record == nullptr) {
            PageRecord new_record;
            if (!Insert(page_addr, new_record)) {
                LockRelease();
                return false;
            }
            record = Find(page_addr);
            if (record == nullptr) {
                LockRelease();
                return false;
            }
        }
        int rc = record->Lock();
        LockRelease();
        return rc == 0;
    }

    bool UnlockPage(std::uintptr_t page_addr) noexcept {
        LockAcquire();
        auto *record = Find(page_addr);
        if (record == nullptr) {
            LockRelease();
            return false;
        }
        int rc = record->Unlock();
        LockRelease();
        return rc == 0;
    }

    int GetLockId(std::uintptr_t page_addr) noexcept {
        LockAcquire();
        auto *record = Find(page_addr);
        int id = record ? record->lock_id : -1;
        LockRelease();
        return id;
    }
};


#endif /* OS_PAGE_TABLE_H */
