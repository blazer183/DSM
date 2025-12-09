#ifndef OS_BIND_TABLE_H
#define OS_BIND_TABLE_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "os/table_base.hpp"

// BindRecord 用于记录文件绑定信息
struct BindRecord {
    std::string filepath;        // 绑定的文件路径
    int fd { -1 };               // 文件描述符
    size_t file_size { 0 };      // 文件大小
    int start_page { -1 };       // 起始页号
    int page_count { 0 };        // 占用页数
};

class BindTable final : public TableBase<std::string, BindRecord> {
public:
    using Base = TableBase<std::string, BindRecord>;

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
    using Base::GlobalMutexLock;
    using Base::GlobalMutexUnlock;
};

#endif /* OS_BIND_TABLE_H */
