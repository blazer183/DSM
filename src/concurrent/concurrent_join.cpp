// src/concurrent/dsm_join.cpp

#include <iostream>
#include <mutex>
#include <cstring>
#include <unistd.h>

// 引入头文件
#include "concurrent/concurrent_core.h"
#include "net/protocol.h"

/**
 * [Manager端运行] 处理新节点的加入请求
 * 逻辑：
 * 1. 检查权限 (只有 Manager 能处理)
 * 2. 分配全局唯一 ID
 * 3. 回复 ACK (包含分配的ID、当前节点总数、共享内存大小)
 */
void process_join_req(int sock, const dsm_header_t& head, const payload_join_req_t& body) {
    auto& state = DSMState::GetInstance();
    std::lock_guard<std::mutex> lock(state.state_mutex);

    // 只有 Manager (我们约定 ID=1 是 Manager) 才有资格分配 ID
    if (state.my_node_id != 1) {
        std::cerr << "[Error] I am not Manager (ID=" << state.my_node_id 
                  << "), but received JOIN_REQ from Socket " << sock << "!" << std::endl;
        return;
    }

    // 1. 分配新 ID
    int assigned_id = state.next_node_id++;
    // 更新 Manager 自己的节点计数
    state.node_count = state.next_node_id - 1;

    std::cout << "[Manager] New Node Joining... "
              << "ClientPort: " << body.listen_port 
              << " -> Assigned ID: " << assigned_id 
              << " (Total Nodes: " << state.node_count << ")" << std::endl;

    // TODO: 如果需要，这里应该把 sock 和 assigned_id 的映射关系存到 state.node_sockets 里
    // state.node_sockets[assigned_id] = sock; 

    // 2. 构造回复 (ACK)
    dsm_header_t ack_head = {0};
    ack_head.type = DSM_MSG_JOIN_ACK;
    ack_head.src_node_id = state.my_node_id;
    ack_head.seq_num = head.seq_num;
    ack_head.payload_len = sizeof(payload_join_ack_t);
    ack_head.unused = 1; // Success

    payload_join_ack_t ack_body;
    ack_body.assigned_node_id = assigned_id;
    ack_body.node_count = state.node_count;     // 把当前最新的节点总数告诉新人
    ack_body.dsm_mem_size = state.total_mem_size; // 告诉新人内存有多大

    // 3. 发送回复
    // 注意：rio_writen 需要你自己确保已包含对应头文件，且 sock 有效
    rio_writen(sock, &ack_head, sizeof(ack_head));
    rio_writen(sock, &ack_body, sizeof(ack_body));
}

/**
 * [Client端运行] 处理 Manager 的加入确认
 * 逻辑：
 * 1. 接收分配的 ID
 * 2. 更新本地状态 (内存大小、节点总数)
 * 3. 唤醒正在阻塞等待的初始化线程 (concurrent_system_init)
 */
void process_join_ack(int sock, const dsm_header_t& head, const payload_join_ack_t& body) {
    auto& state = DSMState::GetInstance();
    
    // 加锁，因为我们即将修改全局状态，并且操作条件变量
    std::unique_lock<std::mutex> lock(state.state_mutex);

    std::cout << "[Client] Received JOIN_ACK from Manager (ID " << head.src_node_id << ")" << std::endl;

    // 1. 更新身份信息
    state.my_node_id = body.assigned_node_id;
    state.node_count = body.node_count;       // 重要：用于后续 Hash 路由计算
    state.total_mem_size = body.dsm_mem_size; // 记录共享内存大小

    std::cout << "  -> My Assigned ID: " << state.my_node_id << std::endl;
    std::cout << "  -> Cluster Size: " << state.node_count << std::endl;
    std::cout << "  -> Shared Mem Size: " << state.total_mem_size << " bytes" << std::endl;

    // 2. 设置标志位
    state.join_finished = true;

    // 3. 唤醒主线程
    // 对应 dsm_state.hpp 中的 std::condition_variable join_cond;
    state.join_cond.notify_all();
    
    // lock 自动释放
}