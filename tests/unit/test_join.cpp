#include <iostream>
#include <cassert>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "net/protocol.h"

// 简单的辅助函数，用于建立连接
int connect_server() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) return -1;
    return sock;
}

int main() {
    std::cout << "========== TEST: Join Flow ==========" << std::endl;
    
    int sock = connect_server();
    if (sock < 0) {
        std::cerr << "Failed to connect. Is run_manager running?" << std::endl;
        return 1;
    }

    // 1. 构造 JOIN_REQ
    dsm_header_t req_head = {0};
    req_head.type = DSM_MSG_JOIN_REQ;
    req_head.payload_len = sizeof(payload_join_req_t);
    
    payload_join_req_t req_body;
    req_body.listen_port = 9000; // 假装我在 9000 端口

    // 2. 发送
    write(sock, &req_head, sizeof(req_head));
    write(sock, &req_body, sizeof(req_body));

    // 3. 接收响应
    dsm_header_t rep_head;
    read(sock, &rep_head, sizeof(rep_head));

    // 4. 验证 Header
    if (rep_head.type == DSM_MSG_JOIN_ACK) {
        std::cout << "[PASS] Received DSM_MSG_JOIN_ACK" << std::endl;
    } else {
        std::cerr << "[FAIL] Expected JOIN_ACK(0x02), got " << (int)rep_head.type << std::endl;
        return 1;
    }

    // 5. 验证 Body
    payload_join_ack_t rep_body;
    read(sock, &rep_body, sizeof(rep_body));
    
    std::cout << "  -> Assigned ID: " << rep_body.assigned_node_id << std::endl;
    
    // 验证 ID 是否有效 (Manager 是 1，第一个 Client 应该是 2)
    if (rep_body.assigned_node_id >= 2) {
        std::cout << "[PASS] Assigned ID is valid." << std::endl;
    } else {
        std::cerr << "[FAIL] Invalid Assigned ID." << std::endl;
    }

    close(sock);
    return 0;
}