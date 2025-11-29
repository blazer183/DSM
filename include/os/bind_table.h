#ifndef OS_BIND_TABLE_H
#define OS_BIND_TABLE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "os/table_base.hpp"

struct BindRecord {
    BindRecord() = default;
    BindRecord(void *addr, std::string file_path)
        : base(addr), file(std::move(file_path)) {}

    void *base {nullptr};
    std::string file;
};

class BindTable final : public TableBase<std::uintptr_t, BindRecord> {
public:
    BindTable() = default;

    using TableBase::Clear;
    using TableBase::Find;
    using TableBase::Insert;
    using TableBase::Remove;
    using TableBase::Size;
    using TableBase::Update;

    /* Linear search across bindings to find match by file path. */
    const BindRecord *FindByFile(const std::string &file_path) const
    {
        for (const auto &kv : this->entries_) {
            if (kv.second.file == file_path)
                return &kv.second;
        }
        return nullptr;
    }
};


#endif /* OS_BIND_TABLE_H */
