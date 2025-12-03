#include <iostream>
#include <mutex>
#include <cstring>
#include <sys/mman.h> // 必须包含，用于 mprotect
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "concurrent/concurrent_core.h"
#include "net/protocol.h"
#include "dsm.h"
#include "os/table_base.hpp"
// 辅助宏：页对齐
#define PAGE_ALIGN_DOWN(addr) ((void*)((uintptr_t)(addr) & ~(DSM_PAGE_SIZE - 1)))

void process_page_req(int sock, const dsm_header_t& head, const payload_page_req_t& body) {
    ::PageTable->LockAcquire();

    // 计算请求页面的本地虚拟地址
    uintptr_t page_addr_int = (uintptr_t)state.shared_mem_base + (body.page_index * DSM_PAGE_SIZE);
    void* page_ptr = (void*)page_addr_int;

    // 1. 查表确认所有权
    auto* record = ::PageTable->Find(page_addr_int);
    
    // 获取当前 Owner (-1 表示未分配，默认归 Node 0)
    int current_owner = record ? record->owner_id : -1;
    // 如果记录不存在，且我是 Manager (Node 0)，那默认就是我的
    if (current_owner == -1 && dsm_getnodeid() == 0) {
        current_owner = 0;
    }

    // ============================================================
    // Case A: 我是 Owner (数据在我这)
    // ============================================================
    if (current_owner == state.my_node_id) {
        std::cout << "[DSM] Serving Page " << body.page_index << " to Node " << head.src_node_id << std::endl;

        // 【关键步骤】OS 保护处理
        // 如果当前页面是 PROT_NONE (可能因为给了别人锁，或者自己也没写过)，
        // 这里必须临时开启读权限，否则 rio_writen 读取内存时 Server 会崩溃。
        mprotect(page_ptr, DSM_PAGE_SIZE, PROT_READ);

        // 1. 构造头部：使用 PAGE_REP，标记 unused=1 (代表数据)
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP;
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = DSM_PAGE_SIZE; // 固定 4096
        rep_head.unused = 1; // <--- 1 表示负载是 Data

        // 2. 发送头部
        rio_writen(sock, &rep_head, sizeof(rep_head));

        // 3. 发送数据 (4KB)
        rio_writen(sock, page_ptr, DSM_PAGE_SIZE);

        // 4. 移交所有权 (根据协议，发送即移交)
        // 更新本地页表
        if (record) {
            record->owner_id = head.src_node_id;
        } else {
            PageRecord new_rec; 
            new_rec.owner_id = head.src_node_id;
            state.page_table.Insert(page_addr_int, new_rec);
        }

        // 【关键步骤】设置本地不可访问
        // 既然数据给了别人，我本地就失效了(Invalidate)，防止产生不一致
        mprotect(page_ptr, DSM_PAGE_SIZE, PROT_NONE);

        std::cout << "  -> Data sent. Ownership transferred." << std::endl;
    } 
    // ============================================================
    // Case B: 我不是 Owner -> 重定向 (Redirect)
    // ============================================================
    else if (current_owner != -1 && current_owner != ::NodeId) {
        // 如果查不到记录且我也不是Manager，那大概率应该找Manager，或者Hash计算出的ProbOwner
        
        std::cout << "[DSM] Redirect Page " << body.page_index << ": Me(" << ::NodeId 
                  << ") -> RealOwner(" << current_owner << ")" << std::endl;

        // 1. 构造头部：PAGE_REP，标记 unused=0 (代表重定向)
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP; 
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_page_rep_t);
        rep_head.unused = 0; // <--- 0 表示负载是 Redirect Info

        // 2. 构造 Body
        payload_page_rep_t rep_body;
        rep_body.page_index = body.page_index;
        rep_body.requester_id = current_owner; // 复用字段存 RealOwner ID

        // 3. 发送
        
        send(sock, (const char*)&rep_head, sizeof(rep_head), 0);
        send(sock, (const char*)&rep_body, sizeof(rep_body), 0);
    }
    // ============================================================
    // Case C: page未分配，但我是node 0 -> 默认我是Owner
    // ============================================================
    else if (current_owner == -1 && ::NodeId == 0) {
        std::cout << "[DSM] Serving Page " << body.page_index << " to Node " << head.src_node_id << " (Default Owner)" << std::endl;

        // 【关键步骤】OS 保护处理
        mprotect(page_ptr, DSM_PAGE_SIZE, PROT_READ);

        // 1. 构造头部：使用 PAGE_REP，标记 unused=1 (代表数据)
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP;
        rep_head.src_node_id = dsm_getnodeid() ;
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = DSM_PAGE_SIZE; // 固定 4096
        rep_head.unused = 1; // <--- 1 表示负载是 Data

        // 2. 发送头部
        send(sock, &rep_head, sizeof(rep_head), 0);

        // 3. 发送数据 (4KB)
        send(sock, page_ptr, DSM_PAGE_SIZE, 0);

        // 4. 移交所有权 (根据协议，发送即移交)
        // 更新本地页表
        if (record) {
            record->owner_id = head.src_node_id;
        } else {
            PageRecord new_rec; 
            new_rec.owner_id = head.src_node_id;
            state.page_table.Insert(page_addr_int, new_rec);
        }

        // 【关键步骤】设置本地不可访问
        mprotect(page_ptr, DSM_PAGE_SIZE, PROT_NONE);

        std::cout << "  -> Data sent. Ownership transferred." << std::endl;

    }

    ::PageTable->LockRelease();
}


