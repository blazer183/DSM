#include <iostream>
#include <mutex>
#include <cstring>
#include <sys/mman.h> // 必须包含，用于 mprotect
#include <unistd.h>

#include "concurrent/concurrent_core.h"
#include "net/protocol.h"

// 辅助宏：页对齐
#define PAGE_ALIGN_DOWN(addr) ((void*)((uintptr_t)(addr) & ~(DSM_PAGE_SIZE - 1)))

// ============================================================================
// 1. 处理页面请求 (Server/Owner 侧)
// ============================================================================
void process_page_req(int sock, const dsm_header_t& head, const payload_page_req_t& body) {
    auto& state = DSMState::GetInstance();
    std::lock_guard<std::mutex> lock(state.state_mutex);

    // 计算请求页面的本地虚拟地址
    uintptr_t page_addr_int = (uintptr_t)state.shared_mem_base + (body.page_index * DSM_PAGE_SIZE);
    void* page_ptr = (void*)page_addr_int;

    // 1. 查表确认所有权
    auto* record = state.page_table.Find(page_addr_int);
    
    // 获取当前 Owner (-1 表示未分配，默认归 Node 1/Manager)
    int current_owner = record ? record->owner_id : -1;
    // 如果记录不存在，且我是 Manager (Node 1)，那默认就是我的
    if (current_owner == -1 && state.my_node_id == 1) {
        current_owner = 1;
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
        rep_head.src_node_id = state.my_node_id;
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
    else {
        // 如果查不到记录且我也不是Manager，那大概率应该找Manager，或者Hash计算出的ProbOwner
        if (current_owner == -1) current_owner = 1; 

        std::cout << "[DSM] Redirect Page " << body.page_index << ": Me(" << state.my_node_id 
                  << ") -> RealOwner(" << current_owner << ")" << std::endl;

        // 1. 构造头部：PAGE_REP，标记 unused=0 (代表重定向)
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP; 
        rep_head.src_node_id = state.my_node_id;
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_page_rep_t);
        rep_head.unused = 0; // <--- 0 表示负载是 Redirect Info

        // 2. 构造 Body
        payload_page_rep_t rep_body;
        rep_body.page_index = body.page_index;
        rep_body.requester_id = current_owner; // 复用字段存 RealOwner ID

        // 3. 发送
        rio_writen(sock, &rep_head, sizeof(rep_head));
        rio_writen(sock, &rep_body, sizeof(rep_body));
    }
}


// ============================================================================
// 2. 处理页面回复 (Client 侧，由 Daemon 线程调用)
// ============================================================================
void process_page_rep(int sock, const dsm_header_t& head, const char* raw_payload) {
    auto& state = DSMState::GetInstance();
    
    // 加锁，因为要修改内存和 notify
    std::unique_lock<std::mutex> lock(state.state_mutex);

    // 假设：我们在 DSMState 里增加了一个字段 `pending_page_index` 
    // 用来记录当前线程正在等哪一页 (因为协议报文头里没带页号，如果是Data包的话)
    // 如果没有这个字段，我们不知道收到的 4KB 往哪里写。
    // 这里假定 payload_page_req_t (Client发起请求时) 设置了这个值。
    
    // 为了代码能跑，这里假设你会在 dsm_state.hpp 里加一个 `volatile uint32_t pending_page_index;`
    uint32_t page_idx = state.pending_page_index; 
    uintptr_t page_addr_int = (uintptr_t)state.shared_mem_base + (page_idx * DSM_PAGE_SIZE);
    void* page_ptr = (void*)page_addr_int;

    // ============================================================
    // Case A: 收到数据 (unused == 1)
    // ============================================================
    if (head.unused == 1) {
        std::cout << "[Client] Received Page Data for Index " << page_idx << std::endl;

        // 1. 开启写权限 (以便写入收到的数据)
        mprotect(page_ptr, DSM_PAGE_SIZE, PROT_READ | PROT_WRITE);

        // 2. 写入数据
        memcpy(page_ptr, raw_payload, DSM_PAGE_SIZE);

        // 3. 更新页表：我是 Owner 了
        PageRecord rec;
        rec.owner_id = state.my_node_id;
        if (!state.page_table.Update(page_addr_int, rec)) {
            state.page_table.Insert(page_addr_int, rec);
        }

        // 4. 设置状态：成功
        state.page_req_finished = true;  // 请求结束
        state.data_received = true;      // 拿到了数据 (不用重试)
    }
    // ============================================================
    // Case B: 收到重定向 (unused == 0)
    // ============================================================
    else {
        auto* body = (const payload_page_rep_t*)raw_payload;
        int real_owner = body->requester_id; // 这里存的是 Owner ID

        std::cout << "[Client] Received Redirect for Index " << page_idx 
                  << " -> Go to Node " << real_owner << std::endl;

        // 1. 更新页表：记录真正的 Owner
        PageRecord rec;
        rec.owner_id = real_owner;
        if (!state.page_table.Update(page_addr_int, rec)) {
            state.page_table.Insert(page_addr_int, rec);
        }

        // 2. 设置状态：未完成 (需要主线程醒来后重试)
        state.page_req_finished = true; // 当前这一次请求交互算结束了
        state.data_received = false;    // 但是没拿到数据，需要 Retry
    }

    // 唤醒 dsm_request_page 里的 wait
    state.page_cond.notify_all();
}