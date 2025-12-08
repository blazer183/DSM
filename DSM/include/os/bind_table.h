#ifndef OS_BIND_TABLE_H
#define OS_BIND_TABLE_H

#include <cstddef>
#include <limits>
#include <string>
#include <utility>

#include "os/table_base.hpp"

struct BindRecord {
    BindRecord() = default;
    BindRecord(void *addr, std::string file_path)
        : base(addr), file(std::move(file_path)) {}
    BindRecord(void *addr, size_t elem_size, std::string file_path)
        : base(addr), element_size(elem_size), file(std::move(file_path)) {}

    void *base {nullptr};
    size_t element_size {0};
    std::string file;
};

class BindTable final : public TableBase<void *, BindRecord> {
public:
    using Base = TableBase<void *, BindRecord>;

    explicit BindTable(std::size_t capacity = 0)
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


#endif /* OS_BIND_TABLE_H */
