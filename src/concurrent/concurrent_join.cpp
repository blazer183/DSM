// src/concurrent/dsm_join.cpp

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
           // 下一个分配的节点ID

/**
 * 处理新节点的加入请求
 * 逻辑：
 * 1. 检查权限
 * 2. 回复 ACK 只回复头
 */
void process_join_req(int sock, const dsm_header_t& head) {

    if(dsm_getnodeid() != 0) {
        return;
    }

    // 1. 构造回复 (ACK) 只回复头
    dsm_header_t ack_head = {0};
    ack_head.type = DSM_MSG_JOIN_ACK;
    ack_head.src_node_id = 0;
    ack_head.seq_num = head.seq_num;
    ack_head.payload_len = 0;
    ack_head.unused = 0;//没用到 

    // 3. 发送回复 只回复头
    send(sock, (const char*)&ack_head, sizeof(ack_head), 0);
    
}

