#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "net/protocol.h"

int connect_server() { 
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    return sock;
}

int main() {
    std::cout << "========== TEST: Lock Flow ==========" << std::endl;
    int sock = connect_server();

    // 1. 构造 LOCK_ACQ
    dsm_header_t req_head = {0};
    req_head.type = DSM_MSG_LOCK_ACQ;
    req_head.src_node_id = 3; // 假装我是 ID 3
    req_head.payload_len = sizeof(payload_lock_req_t);
    
    payload_lock_req_t req_body;
    req_body.lock_id = 100; // 申请 100 号锁

    write(sock, &req_head, sizeof(req_head));
    write(sock, &req_body, sizeof(req_body));

    // 2. 接收响应
    dsm_header_t rep_head;
    read(sock, &rep_head, sizeof(rep_head));

    if (rep_head.type == DSM_MSG_LOCK_REP) {
        std::cout << "[PASS] Received LOCK_REP" << std::endl;
        
        if (rep_head.unused == 1) {
            std::cout << "[PASS] Lock Granted (unused=1)" << std::endl;
        } else {
            std::cout << "[INFO] Lock Redirect (unused=0)" << std::endl;
        }
        
        payload_lock_rep_t rep_body;
        read(sock, &rep_body, sizeof(rep_body));
        std::cout << "  -> Lock ID confirmed: " << rep_body.lock_id << std::endl;

    } else {
        std::cerr << "[FAIL] Expected LOCK_REP, got " << (int)rep_head.type << std::endl;
    }

    close(sock);
    return 0;
}