#include <iostream>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
// 引入头文件
#include "concurrent/concurrent_core.h"
#include "net/protocol.h"
#include "dsm.h"
#include "os/table_base.hpp"

// ============================================================================
// 1. 处理锁请求 (proowner侧)
// 逻辑：收到 ACQ -> 查表 -> 是我的就给(Grant) -> 不是我的就重定向(Redirect)
// ============================================================================
void process_lock_acq(int sock, const dsm_header_t& head, const payload_lock_req_t& body) {
    int lock_id = body.lock_id;
    int requester = head.src_node_id;

    ::LockTable->LockAcquire();
    
    // 1. 查表看锁现在的状态
    auto* record = ::LockTable->Find(lock_id);
    
    // 获取当前 Owner 
    // -1 表示未分配，默认归 Node 0
    // 如果我是 Manager 且记录不存在，那默认我是 Owner
    int current_owner = record ? record->owner_id : -1;
    if (current_owner == -1 && ::dsm_getnodeid() == 0) {
        current_owner = 0;
    }

    // ============================================================
    // Case A: 我是 Owner (锁的Token在我这)
    // ============================================================
    if (current_owner == state.my_node_id) {      
        std::cout << "[DSM] Granting Lock " << lock_id << " to Node " << requester << std::endl;

        // 1. 构造回复：unused=1 (Success/Grant)
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_LOCK_REP;
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_lock_rep_t);
        rep_head.unused = 1; // <--- 1 表示授权成功

        // 2. 构造 Payload
        payload_lock_rep_t rep_body;
        rep_body.lock_id = lock_id;
        rep_body.realowner = requester; // 确认你是新主人
        rep_body.invalid_set_count = 0; // Scope Consistency 暂时留空

        // 3. 发送
        send(sock, (const char*)&rep_head, sizeof(rep_head), 0);
        send(sock, (const char*)&rep_body, sizeof(rep_body), 0);

        // 4. 更新本地表：移交所有权
        // 我不再拥有这把锁，新主人是 requester
        if (record) {
            record->owner_id = requester;
            record->locked = false; // 既然给出去了，我本地肯定没锁住了
        } else {
            // 如果表里没记录，插入一条，记录新主人
            ::LockTable->Insert(lock_id, LockRecord(requester));
        }
        std::cout << "  -> Lock Ownership transferred." << std::endl;
    }
    // ============================================================
    // Case B: 我不是 Owner -> 重定向 (Redirect)
    // ============================================================
    else {
        if (current_owner == -1) current_owner = 0; // 默认 Owner 是 Node 0

        std::cout << "[DSM] Redirect Lock " << lock_id << ": Me(" << ::NodeId 
                  << ") -> RealOwner(" << current_owner << ")" << std::endl;

        // 1. 构造回复：unused=0 (Redirect)
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_LOCK_REP;
        rep_head.src_node_id = dsm_getnodeid;
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_lock_rep_t);
        rep_head.unused = 0; // <--- 0 表示重定向

        // 2. 构造 Payload (告诉Client谁才是真Owner)
        payload_lock_rep_t rep_body;
        rep_body.lock_id = lock_id;
        rep_body.realowner = current_owner; // 把真 Owner ID发回去
        rep_body.invalid_set_count = 0;

        // 3. 发送
        send(sock, (const char*)&rep_head, sizeof(rep_head), 0);
        send(sock, (const char*)&rep_body, sizeof(rep_body), 0);
    }
    
    ::LockTable->LockRelease();
}


