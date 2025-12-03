#ifndef OS_SOCKET_TABLE_H
#define OS_SOCKET_TABLE_H

#include <cstddef>
#include <limits>

#include "os/table_base.hpp"

struct SocketRecord {
	int socket { -1 };
};

class SocketTable final : public TableBase<int, SocketRecord> {
public:
   using Base = TableBase<int, SocketRecord>;

   explicit SocketTable(std::size_t capacity = 0)
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

#endif /* OS_SOCKET_TABLE_H */
