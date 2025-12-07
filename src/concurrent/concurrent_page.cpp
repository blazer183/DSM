#include <iostream>
#include <mutex>
#include <cstring>
#include <sys/mman.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "concurrent/concurrent_core.h"
#include "net/protocol.h"
#include "dsm.h"
#include "os/table_base.hpp"
#include "os/page_table.h"
#include "os/bind_table.h"

#define PAGE_ALIGN_DOWN(addr) ((void*)((uintptr_t)(addr) & ~(DSM_PAGE_SIZE - 1)))

// 辅助函数：从磁盘加载页
// 返回值：true 成功，false 失败
bool DSM_LoadFromDisk(uint32_t page_index, void* dest_buffer) {
    // 1. 计算缺页的绝对虚拟地址
    // 注意：state.shared_mem_base 需要是全局可访问的，或者传进来
    uintptr_t target_addr_val = (uintptr_t)state.shared_mem_base + (page_index * DSM_PAGE_SIZE);
    void* target_addr = (void*)target_addr_val;

    std::string file_path;
    uintptr_t file_base_addr = 0;

    // 2. 查 BindTable (临界区)
    // 必须加锁，防止别人正在 Bind/Unbind
    ::BindTable->LockAcquire();
    
    // 【注意】这里假设 BindTable 里存的 Key 正好就是 target_addr
    // 如果你们 BindTable 只存了文件头地址，这里需要遍历查找，效率较低
    auto* record = ::BindTable->Find(target_addr);

    if (record == nullptr) {
        ::BindTable->LockRelease();
        std::cerr << "[DSM] Error: No bind record found for page addr " << target_addr << std::endl;
        // 如果找不到绑定记录，说明这块内存没有映射文件，或者是匿名页
        // 这里可以直接 memset 0，或者返回错误
        memset(dest_buffer, 0, DSM_PAGE_SIZE);
        return true; // 视作匿名页处理，返回成功但全是0
        // return false; // 或者视作错误
    }

    // 拷贝我们需要的数据，尽快释放锁
    file_path = record->file;
    file_base_addr = (uintptr_t)record->base;

    ::BindTable->LockRelease();

    // 3. 计算文件偏移量 (Offset)
    // 比如：文件映射在 0x1000，现在缺页 0x2000，则偏移量 = 0x2000 - 0x1000 = 4096
    off_t offset = target_addr_val - file_base_addr;

    if (offset < 0) {
        std::cerr << "[DSM] Error: Invalid offset calculation." << std::endl;
        return false;
    }

    // 4. 执行磁盘 IO (耗时操作，必须在锁外进行)
    // O_RDONLY: 只读模式
    int fd = open(file_path.c_str(), O_RDONLY);
    if (fd < 0) {
        perror("[DSM] Open file failed");
        return false;
    }

    // pread: 原子操作，从指定 offset 读取，不改变文件指针位置，适合多线程
    ssize_t bytes_read = pread(fd, dest_buffer, DSM_PAGE_SIZE, offset);

    close(fd);

    if (bytes_read < 0) {
        perror("[DSM] Read file failed");
        return false;
    }
    
    // 如果文件不够长（比如最后一页不满 4KB），剩下的部分补 0
    if (bytes_read < DSM_PAGE_SIZE) {
        memset((char*)dest_buffer + bytes_read, 0, DSM_PAGE_SIZE - bytes_read);
    }

    std::cout << "[DSM] Loaded page " << page_index 
              << " from file " << file_path 
              << " at offset " << offset << std::endl;

    return true;
}

void process_page_req(int sock, const dsm_header_t& head, const payload_page_req_t& body) {
    ::PageTable->LockAcquire();

    uintptr_t page_addr_int = (uintptr_t)state.shared_mem_base + (body.page_index * DSM_PAGE_SIZE);
    void* page_ptr = (void*)page_addr_int;

    auto* record = ::PageTable->Find(page_addr_int);
    
    int current_owner = record ? record->owner_id : -1;

    // Case A: owner_id == PodId
      if (current_owner == dsm_getnodeid()) {
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP;
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_page_rep_t); 
        rep_head.unused = 1; //
        
        payload_page_rep_t rep_body;
        rep_body.real_owner_id = current_owner;
        memcpy(rep_body.pagedata, page_ptr, DSM_PAGE_SIZE);

        send(sock, (const char*)&rep_head, sizeof(rep_head));
        send(sock, (const char*)&rep_body, sizeof(rep_body));
    } 
    // Case B: owner_id != PodId
    else if ( current_owner != dsm_getnodeid()) {
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP; 
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_page_rep_t);
        rep_head.unused = 0; 

        payload_page_rep_t rep_body;
        rep_body.real_owner_id = current_owner;
        rep_body.page_data = 0x0; // 

        send(sock, (const char*)&rep_head, sizeof(rep_head), 0);
        send(sock, (const char*)&rep_body, sizeof(rep_body), 0);
    }
    // Case C: owner_id = -1 && PodId = 0
    else if (current_owner == -1 && dsm_getnodeid() == 0) {
        DSM_LoadFromDisk(body.page_index, page_ptr);

        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP;
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_page_rep_t); 
        rep_head.unused = 1; //
        
        payload_page_rep_t rep_body;
        rep_body.real_owner_id = current_owner;
        memcpy(rep_body.pagedata, page_ptr, DSM_PAGE_SIZE);

        send(sock, (const char*)&rep_head, sizeof(rep_head));
        send(sock, (const char*)&rep_body, sizeof(rep_body));
    }
    // Case D: owner_id = -1 && PodId != 0
    else if (current_owner == -1 && dsm_getnodeid() != 0) {

        DSM_LoadFromDisk(body.page_index, page_ptr);
      
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP;
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_page_rep_t); 
        rep_head.unused = 1; //
        
        payload_page_rep_t rep_body;
        rep_body.real_owner_id = current_owner;
        memcpy(rep_body.pagedata, page_ptr, DSM_PAGE_SIZE);

        send(sock, (const char*)&rep_head, sizeof(rep_head));
        send(sock, (const char*)&rep_body, sizeof(rep_body));
    }

    ::PageTable->LockRelease();
}


