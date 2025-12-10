#include <iostream>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "concurrent/concurrent_core.h"
#include "net/protocol.h"
#include "dsm.h"
#include "os/table_base.hpp"
#include "os/lock_table.h"

extern class LockTable* LockTable;

void process_lock_acq(int sock, const dsm_header_t& head, const payload_lock_req_t& body) {
    int lock_id = body.lock_id;
    int requester = head.src_node_id;

    ::LockTable->GlobalMutexLock();
    auto* record = ::LockTable->Find(lock_id);
    uint32_t count = record->invalid_set_count;
    // 计算变长数据的字节大小
    uint32_t list_bytes = count * sizeof(uint32_t);

    ::LockTable->LocalMutexLock(lock_id);
    ::LockTable->GlobalMutexUnlock();
     
    dsm_header_t rep_head = {0};
    rep_head.type = DSM_MSG_LOCK_REP;
    rep_head.src_node_id = dsm_getnodeid();
    rep_head.seq_num = head.seq_num;
    rep_head.payload_len = sizeof(payload_lock_rep_t) + list_bytes;
    rep_head.unused = 1; 
  
    payload_lock_rep_t rep_body_fixed;
    rep_body_fixed.invalid_set_count = count;
        
    ::LockTable->LocalMutexUnlock(lock_id);
        
    send(sock, (const char*)&rep_head, sizeof(rep_head), 0);
    send(sock, (const char*)&rep_body_fixed, sizeof(rep_body_fixed), 0);
    if(count > 0) {
        send(sock, (const char*)record->invalid_page_list.data(), list_bytes, 0);
    }

    return ;

}


void process_lock_rls(int sock, const dsm_header_t& head, const payload_lock_rls_t& body) {
    

    // 1. 构造回复 (ACK) 只回复头
    dsm_header_t ack_head = {0};
    ack_head.type = DSM_MSG_ACK;
    ack_head.src_node_id = dsm_getnodeid();
    ack_head.seq_num = head.seq_num;
    ack_head.payload_len = 0;
    ack_head.unused = 0;//没用到 

    // 3. 发送回复 只回复头
    send(sock, (const char*)&ack_head, sizeof(ack_head), 0);

}