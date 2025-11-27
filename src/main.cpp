#include <iostream>
#include <string>

#include "os/page_table.h"
#include "os/lock_table.h"
#include "os/bind_table.h"

int main()
{
	std::cout << "[DSM Test] TableBase derived structures demo" << std::endl;

	PageTable page_table(8);
	page_table.Insert(0x1000, PageRecord(0));
	page_table.PromoteOwner(0x1000, 1);
	page_table.Invalidate(0x1000);
	if (auto *record = page_table.Find(0x1000)) {
		std::cout << "Page 0x1000 owner=" << record->owner_id
				  << " valid=" << std::boolalpha << record->valid << std::endl;
	}

	LockTable lock_table;
	const int lock_id = 7;
	if (lock_table.TryAcquire(lock_id, 42)) {
		std::cout << "Lock " << lock_id << " acquired by 42" << std::endl;
	}
	std::cout << "Release result=" << lock_table.Release(lock_id, 42) << std::endl;

	BindTable bind_table;
	char region;
	auto key = reinterpret_cast<std::uintptr_t>(&region);
	bind_table.Insert(key, BindRecord(&region, "segment.bin"));
	if (auto *bind = bind_table.Find(key)) {
		std::cout << "Binding for addr=" << bind->base << " file=" << bind->file << std::endl;
	}

	if (auto *by_name = bind_table.FindByFile("segment.bin")) {
		std::cout << "Found by file: addr=" << by_name->base << std::endl;
	}

	std::cout << "All tests complete." << std::endl;
	return 0;
}
