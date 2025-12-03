#include <iostream>
#include <mutex>
#include <cstring>
#include <unistd.h>

// 引入头文件
#include "concurrent/concurrent_core.h"
#include "net/protocol.h"

// 标志位约定 (对应 dsm_header_t.unused)
#define FLAG_UPDATE_PAGE 0x00
#define FLAG_UPDATE_LOCK 0x01

// ============================================================================
// 1. 处理所有权更新 (Manager 侧)
// 逻辑：收到 Worker 的通知，更新 Directory (页表/锁表) 中的 Owner 字段
// ============================================================================
void process_owner_update(int sock, const dsm_header_t& head, const payload_owner_update_t& body) {
    auto& state = DSMState::GetInstance();
    std::lock_guard<std::mutex> lock(state.state_mutex);

    // 只有 Manager 维护全局目录，通常只有 Manager 会收到这个消息
    // 但为了健壮性，我们可以处理所有节点的更新（如果做备份的话）

    uint32_t res_id = body.resource_id;
    int new_owner = body.new_owner_id;

    // 根据头部 unused 字段判断是 页 还是 锁
    // 约定：0 = Page, 1 = Lock
    if (head.unused == FLAG_UPDATE_LOCK) {
        // --- 更新锁表 ---
        LockRecord rec(new_owner); // 使用构造函数初始化 owner
        // 这里不确定是否 locked，通常 update 发过来意味着有人持有了
        rec.locked = true; 

        // 尝试更新，如果不存在则插入
        if (!state.lock_table.Update(res_id, rec)) {
            state.lock_table.Insert(res_id, rec);
        }
        std::cout << "[Directory] Lock " << res_id << " moved to Node " << new_owner << std::endl;

    } else {
        // --- 更新页表 ---
        // 注意：页表的 Key 是虚拟地址，不是 page_index
        // 我们需要把 page_index 转回地址
        uintptr_t page_addr = (uintptr_t)state.shared_mem_base + (res_id * DSM_PAGE_SIZE);

        PageRecord rec;
        rec.owner_id = new_owner;

        if (!state.page_table.Update(page_addr, rec)) {
            state.page_table.Insert(page_addr, rec);
        }
        std::cout << "[Directory] Page " << res_id << " moved to Node " << new_owner << std::endl;
    }
}

