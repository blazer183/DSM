// tests/test_update.cpp

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
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) return -1;
    return sock;
}

int main() {
    std::cout << "========== TEST: Owner Update Flow ==========" << std::endl;
    int sock = connect_server();
    if (sock < 0) { std::cerr << "Connect failed" << std::endl; return 1; }

    uint32_t test_page_id = 999;
    uint16_t new_owner_id = 5; // 我们要篡改的目标 Owner

    // -------------------------------------------------------
    // 步骤 1: 发送 UPDATE 消息 (修改 Manager 的目录表)
    // -------------------------------------------------------
    std::cout << "[Step 1] Sending UPDATE: Page " << test_page_id << " -> Node " << new_owner_id << std::endl;

    dsm_header_t upd_head = {0};
    upd_head.type = DSM_MSG_OWNER_UPDATE;
    upd_head.src_node_id = 2; // 模拟我是 Node 2
    upd_head.payload_len = sizeof(payload_owner_update_t);
    upd_head.unused = 0; // 0 表示 Page Update (根据 dsm_misc.cpp 的约定)

    payload_owner_update_t upd_body;
    upd_body.resource_id = test_page_id;
    upd_body.new_owner_id = new_owner_id;

    write(sock, &upd_head, sizeof(upd_head));
    write(sock, &upd_body, sizeof(upd_body));

    // 给 Manager 一点点时间处理 (微秒级即可)
    usleep(10000); 

    // -------------------------------------------------------
    // 步骤 2: 发送 REQ 消息 (验证 Manager 是否更新了记录)
    // -------------------------------------------------------
    std::cout << "[Step 2] Sending PAGE_REQ for Page " << test_page_id << "..." << std::endl;

    dsm_header_t req_head = {0};
    req_head.type = DSM_MSG_PAGE_REQ;
    req_head.src_node_id = 2; 
    req_head.payload_len = sizeof(payload_page_req_t);
    
    payload_page_req_t req_body;
    req_body.page_index = test_page_id;

    write(sock, &req_head, sizeof(req_head));
    write(sock, &req_body, sizeof(req_body));

    // -------------------------------------------------------
    // 步骤 3: 检查回复
    // -------------------------------------------------------
    dsm_header_t rep_head;
    read(sock, &rep_head, sizeof(rep_head));

    if (rep_head.type == DSM_MSG_PAGE_REP) {
        // 我们期望的是重定向 (Unused=0)
        if (rep_head.unused == 0) {
            std::cout << "[PASS] Received Redirect (unused=0)" << std::endl;
            
            payload_page_rep_t rep_body;
            read(sock, &rep_body, sizeof(rep_body));
            
            std::cout << "  -> Redirected to Node: " << rep_body.requester_id << std::endl;

            if (rep_body.requester_id == new_owner_id) {
                std::cout << "[PASS] Target matches the updated owner (" << new_owner_id << ")!" << std::endl;
            } else {
                std::cerr << "[FAIL] Target mismatch! Expected " << new_owner_id 
                          << ", got " << rep_body.requester_id << std::endl;
            }

        } else {
            // 如果 unused=1，说明 Manager 还是把数据发过来了，说明它认为自己还是 Owner
            std::cerr << "[FAIL] Manager sent DATA (unused=1). Update failed!" << std::endl;
        }
    } else {
        std::cerr << "[FAIL] Unexpected Msg Type: " << (int)rep_head.type << std::endl;
    }

    close(sock);
    return 0;
}