#ifndef OS_SOCKET_TABLE_H
#define OS_SOCKET_TABLE_H

#include <cstddef>
#include <cstdint>
#include <limits>

#include "os/table_base.hpp"

struct SocketRecord {
   int socket { -1 };
   uint32_t next_seq { 1 };  // per-connection seq counter

   uint32_t allocate_seq() noexcept {
      uint32_t current = next_seq;
      next_seq = (next_seq == std::numeric_limits<uint32_t>::max()) ? 1 : next_seq + 1;
      return current;
   }
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

   // Ϊָ���ڵ������һ�� seq �ţ�����¼�����ڷ��� 0
   uint32_t NextSeq(int node_id) {
        LockAcquire();
        auto *record = Find(node_id);
        uint32_t seq = record ? record->allocate_seq() : 0;
        LockRelease();
        return seq;
   }
};

#endif /* OS_SOCKET_TABLE_H */
