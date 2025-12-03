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
int next_node_id = 1;           // 下一个分配的节点ID

/**
 * [node0端运行] 处理新节点的加入请求
 * 逻辑：
 * 1. 检查权限
 * 2. 分配全局唯一 ID
 * 3. 回复 ACK (包含分配的ID、当前节点总数、共享内存大小)
 */
void process_join_req(int sock, const dsm_header_t& head, const payload_join_req_t& body) {
  

    // 我们约定 ID=0 才有资格分配 ID
    if ( dsm_getnodeid() != 0) {
        std::cerr << "[Error] I am not Manager (ID=" << dsm_getnodeid() 
                  << "), but received JOIN_REQ from Socket " << sock << "!" << std::endl;
        return;
    }

    // 1. 分配新 ID
    int assigned_id = next_node_id;
    next_node_id++;
    

    std::cout << "New Node Joining... "
              << "ClientPort: " << body.listen_port 
              << " -> Assigned ID: " << assigned_id 
              << " (Total Nodes: " << WorkerNodeNum << ")" << std::endl;

    // TODO: 如果需要，这里应该把 sock 和 assigned_id 的映射关系存到 state.node_sockets 里
    // state.node_sockets[assigned_id] = sock; 

    // 2. 构造回复 (ACK)
    dsm_header_t ack_head = {0};
    ack_head.type = DSM_MSG_JOIN_ACK;
    ack_head.src_node_id = dsm_getnodeid();
    ack_head.seq_num = head.seq_num;
    ack_head.payload_len = sizeof(payload_join_ack_t);
    ack_head.unused = 1; // Success

    payload_join_ack_t ack_body;
    ack_body.assigned_node_id = assigned_id;
    ack_body.node_count = WorkerNodeNum;     // 把当前最新的节点总数告诉新人
    

    // 3. 发送回复
    
    send(sock, (const char*)&ack_head, sizeof(ack_head), 0);
    send(sock, (const char*)&ack_body, sizeof(ack_body), 0);
}

