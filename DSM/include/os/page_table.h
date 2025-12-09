#ifndef OS_PAGE_TABLE_H
#define OS_PAGE_TABLE_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <pthread.h>

#include "os/table_base.hpp"

struct PageRecord {
    int owner_id { -1 };                  // 当前页的真正拥有者
    pthread_mutex_t mutex;                // 用于互斥访问该页
    std::string filepath;                 // 保存该页绑定的文件路径
    int offset { 0 };                     // 保存页的偏移页数，真实偏移量为 offset * PAGESIZE
    int fd { -1 };                        // 文件描述符，保持打开

    PageRecord() noexcept {
        ::pthread_mutex_init(&mutex, nullptr);
    }

    ~PageRecord() noexcept {
        ::pthread_mutex_destroy(&mutex);
    }

    PageRecord(const PageRecord &other)
        : owner_id(other.owner_id),
          filepath(other.filepath),
          offset(other.offset),
          fd(other.fd)
    {
        ::pthread_mutex_init(&mutex, nullptr);
    }

    PageRecord &operator=(const PageRecord &other) {
        if (this != &other) {
            ::pthread_mutex_destroy(&mutex);
            ::pthread_mutex_init(&mutex, nullptr);
            owner_id = other.owner_id;
            filepath = other.filepath;
            offset = other.offset;
            fd = other.fd;
        }
        return *this;
    }

    PageRecord(PageRecord &&other) noexcept
        : owner_id(other.owner_id),
          filepath(std::move(other.filepath)),
          offset(other.offset),
          fd(other.fd)
    {
        ::pthread_mutex_init(&mutex, nullptr);
    }

    PageRecord &operator=(PageRecord &&other) noexcept {
        if (this != &other) {
            ::pthread_mutex_destroy(&mutex);
            ::pthread_mutex_init(&mutex, nullptr);
            owner_id = other.owner_id;
            filepath = std::move(other.filepath);
            offset = other.offset;
            fd = other.fd;
        }
        return *this;
    }
};

//键int不是虚拟地址 是虚拟页号
class PageTable final : public TableBase<int, PageRecord> {
public:
    using Base = TableBase<int, PageRecord>;

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
    using Base::GlobalMutexLock;    
    using Base::GlobalMutexUnlock;

    bool LocalMutexLock(int page_index) noexcept {
        // 获取 page_index 对应的键的对应结构体的局部锁
        auto *record = Find(page_index);
        if (record == nullptr) {
            return false;
        }
        int rc = ::pthread_mutex_lock(&record->mutex);
        return rc == 0;
    }

    bool LocalMutexUnlock(int page_index) noexcept {
        // 释放 page_index 对应的键的对应结构体的局部锁
        auto *record = Find(page_index);
        if (record == nullptr) {
            return false;
        }
        int rc = ::pthread_mutex_unlock(&record->mutex);
        return rc == 0;
    }
};


#endif /* OS_PAGE_TABLE_H */
