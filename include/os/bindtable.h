#ifndef OS_BIND_TABLE_H
#define OS_BIND_TABLE_H

#include <cstddef>
#include <cstdint>

#if defined(__cplusplus)
#include <string>

#include "os/table_base.hpp"

class BindRecord {
public:
    BindRecord() = default;
    BindRecord(void *base_addr, std::size_t length, std::string name);

    void *BaseAddress() const noexcept;
    void SetBaseAddress(void *addr) noexcept;

    std::size_t Length() const noexcept;
    void SetLength(std::size_t length) noexcept;

    const std::string &Name() const noexcept;
    void SetName(std::string name);

private:
    void *base_addr_ {nullptr};
    std::size_t length_ {0};
    std::string name_;
};

class BindTable final : public TableBase<std::uintptr_t, BindRecord> {
public:
    BindTable();
    ~BindTable();

    BindTable(const BindTable &) = delete;
    BindTable &operator=(const BindTable &) = delete;
    BindTable(BindTable &&) noexcept;
    BindTable &operator=(BindTable &&) noexcept;

    using TableBase::Clear;
    using TableBase::Find;
    using TableBase::Insert;
    using TableBase::Remove;
    using TableBase::Size;
    using TableBase::Update;

    const BindRecord *FindByName(const std::string &name) const;
};

#else

typedef struct BindRecord BindRecord;
typedef struct BindTable BindTable;

#endif /* defined(__cplusplus) */

#endif /* OS_BIND_TABLE_H */
