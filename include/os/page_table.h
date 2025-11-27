#ifndef OS_PAGE_TABLE_H
#define OS_PAGE_TABLE_H

#include <cstddef>
#include <cstdint>
#include <limits>

#if defined(__cplusplus)
#include "os/table_base.hpp"

struct PageRecord {
    PageRecord() noexcept : owner_id(-1), valid(true) {}
    explicit PageRecord(int owner) noexcept : owner_id(owner), valid(true) {}

    int owner_id;
    bool valid;
};

class PageTable final : public TableBase<std::uintptr_t, PageRecord> {
public:
    explicit PageTable(std::size_t capacity = 0)
        : TableBase<std::uintptr_t, PageRecord>(capacity == 0 ? std::numeric_limits<std::size_t>::max() : capacity) {}

    using TableBase::Clear;
    using TableBase::Find;
    using TableBase::Insert;
    using TableBase::Remove;
    using TableBase::Size;
    using TableBase::Update;

    bool PromoteOwner(std::uintptr_t page_no, int new_owner)
    {
        if (auto *record = Find(page_no)) {
            record->owner_id = new_owner;
            record->valid = true;
            return true;
        }
        return false;
    }

    bool Invalidate(std::uintptr_t page_no)
    {
        if (auto *record = Find(page_no)) {
            record->valid = false;
            return true;
        }
        return false;
    }
};

#else

typedef struct PageTable PageTable;

typedef struct PageRecord PageRecord;

#endif /* defined(__cplusplus) */

#endif /* OS_PAGE_TABLE_H */
