#ifndef OS_TABLE_BASE_HPP
#define OS_TABLE_BASE_HPP

#include <cstddef>
#include <limits>
#include <unordered_map>
#include <string>
#include <pthread.h>

template <typename Key, typename Record, typename Map = std::unordered_map<Key, Record>>
class TableBase {
public:
    using key_type = Key;
    using record_type = Record;
    using map_type = Map;

    explicit TableBase(std::size_t capacity = std::numeric_limits<std::size_t>::max()) noexcept
        : capacity_(capacity)
    {
        ::pthread_mutex_init(&mutex_, nullptr);
    }

    virtual ~TableBase() {
        ::pthread_mutex_destroy(&mutex_);
    }

    TableBase(const TableBase &) = delete;
    TableBase &operator=(const TableBase &) = delete;
    TableBase(TableBase &&) noexcept = delete;
    TableBase &operator=(TableBase &&) noexcept = delete;

    bool Insert(const key_type &key, const record_type &record) //Ôö
    {
        if (entries_.find(key) != entries_.end())
            return false;
        if (!HasCapacity())
            return false;
        entries_.emplace(key, record);
        return true;
    }

    bool Update(const key_type &key, const record_type &record) //¸Ä
    {
        auto it = entries_.find(key);
        if (it == entries_.end())
            return false;
        it->second = record;
        return true;
    }

    bool Remove(const key_type &key)                            //É¾
    {
        return entries_.erase(key) > 0;
    }

    record_type *Find(const key_type &key)                      //²é    
    {
        auto it = entries_.find(key);
        return (it == entries_.end()) ? nullptr : &it->second;
    }

    const record_type *Find(const key_type &key) const
    {
        auto it = entries_.find(key);
        return (it == entries_.end()) ? nullptr : &it->second;
    }

    std::size_t Size() const noexcept { return entries_.size(); }
    std::size_t Capacity() const noexcept { return capacity_; }

    void Clear() noexcept { entries_.clear(); }

    int LockAcquire() noexcept { return ::pthread_mutex_lock(&mutex_); }
    int LockRelease() noexcept { return ::pthread_mutex_unlock(&mutex_); }

protected:
    bool HasCapacity() const noexcept { return entries_.size() < capacity_; }

    map_type entries_;
    std::size_t capacity_;
    pthread_mutex_t mutex_;
};

std::string GetPodIp(int pod_id);
int GetPodPort(int pod_id);

#endif /* OS_TABLE_BASE_HPP */
