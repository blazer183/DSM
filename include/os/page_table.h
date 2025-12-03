#ifndef OS_PAGE_TABLE_H
#define OS_PAGE_TABLE_H

#include <cstddef>
#include <cstdint>
#include <limits>

#include "os/table_base.hpp"

struct PageRecord {
	int owner_id { -1 };
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

    
};


#endif /* OS_PAGE_TABLE_H */
