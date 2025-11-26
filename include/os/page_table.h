#ifndef OS_PAGE_TABLE_H
#define OS_PAGE_TABLE_H

#include <cstddef>
#include <cstdint>

#if defined(__cplusplus)
#include "os/table_base.hpp"

class PageTableEntry {
public:
	PageTableEntry() noexcept;
	PageTableEntry(void *frame, int owner_id, int protection_flags, bool dirty = false) noexcept;

	void *Frame() const noexcept;
	void SetFrame(void *frame) noexcept;

	int OwnerId() const noexcept;
	void SetOwnerId(int owner_id) noexcept;

	int Protection() const noexcept;
	void SetProtection(int protection_flags) noexcept;

	bool Dirty() const noexcept;
	void MarkDirty(bool dirty) noexcept;

private:
	void *frame_;
	int owner_id_;
	int protection_flags_;
	bool dirty_;
};

class PageTable final : public TableBase<std::uintptr_t, PageTableEntry> {
public:
	explicit PageTable(std::size_t capacity);
	~PageTable();

	PageTable(const PageTable &) = delete;
	PageTable &operator=(const PageTable &) = delete;
	PageTable(PageTable &&) noexcept;
	PageTable &operator=(PageTable &&) noexcept;

	using TableBase::Capacity;
	using TableBase::Clear;
	using TableBase::Find;
	using TableBase::Insert;
	using TableBase::Remove;
	using TableBase::Size;
	using TableBase::Update;

	bool PromoteOwner(std::uintptr_t page_no, int new_owner_id);
	bool SetProtection(std::uintptr_t page_no, int protection_flags);
};

#else

typedef struct PageTableEntry PageTableEntry;
typedef struct PageTable PageTable;

#endif /* defined(__cplusplus) */

#endif /* OS_PAGE_TABLE_H */
