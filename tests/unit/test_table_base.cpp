#include <cstdint>
#include <iostream>
#include <cstdint>
#include <iostream>
#include <string>
#include "os/page_table.h"
#include "os/bind_table.h"
#include "os/lock_table.h"

int main()
{
	std::cout << "[DSM Test] TableBase derived structures demo" << std::endl;

	// ---- Page table demo -------------------------------------------------
	PageTable page_table(8);    //最大容量8
	page_table.Insert(0x1000, PageRecord{0});   //这里是无名对象，用于传递值
	page_table.Update(0x1000, PageRecord{1});
	if (auto *record = page_table.Find(0x1000)) {
		std::cout << "Page 0x1000 owner=" << record->owner_id << std::endl;
	}

	std::cout << "InvalidateRange placeholder result="
		  << std::boolalpha << page_table.InvalidateRange(nullptr, 0) << std::endl;

	// ---- Lock table demo -------------------------------------------------
	LockTable lock_table;
	const int lock_id = 7;
	if (lock_table.TryAcquire(lock_id, 42)) {
		std::cout << "Lock " << lock_id << " acquired by 42" << std::endl;
	}
	std::cout << "Release result=" << lock_table.Release(lock_id, 42) << std::endl;

	// ---- Bind table demo -------------------------------------------------
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
