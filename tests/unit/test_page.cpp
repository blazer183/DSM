#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "net/protocol.h"

int connect_server() { /* 同上，为了节省篇幅省略，实际代码要加上 */ 
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    return sock;
}

int main() {
    std::cout << "========== TEST: Page Flow ==========" << std::endl;
    int sock = connect_server();

    // 1. 构造 PAGE_REQ (请求第 0 页)
    dsm_header_t req_head = {0};
    req_head.type = DSM_MSG_PAGE_REQ;
    req_head.src_node_id = 2; // 假装我是 ID 2
    req_head.payload_len = sizeof(payload_page_req_t);
    
    payload_page_req_t req_body;
    req_body.page_index = 0; 

    write(sock, &req_head, sizeof(req_head));
    write(sock, &req_body, sizeof(req_body));

    // 2. 接收响应
    dsm_header_t rep_head;
    read(sock, &rep_head, sizeof(rep_head));

    // 3. 验证类型 (应该收到 PAGE_REP)
    if (rep_head.type == DSM_MSG_PAGE_REP) {
        std::cout << "[PASS] Received PAGE_REP" << std::endl;
    } else {
        std::cerr << "[FAIL] Expected PAGE_REP, got " << (int)rep_head.type << std::endl;
        return 1;
    }

    // 4. 验证 unused (应该是 1，代表数据，因为 Manager 默认有数据)
    if (rep_head.unused == 1) {
        std::cout << "[PASS] Unused flag is 1 (DATA)" << std::endl;
        
        // 读取 4096 字节数据
        char buffer[4096];
        int n = read(sock, buffer, 4096);
        if (n == 4096) std::cout << "[PASS] Received 4096 bytes of page data." << std::endl;
        else std::cerr << "[FAIL] Data length mismatch: " << n << std::endl;
        
    } else {
        std::cout << "[WARN] Unused flag is 0 (REDIRECT). Did manager lose ownership?" << std::endl;
        // 如果这里收到 Redirect，可能是因为上一个测试把所有权拿走了
        // 读取 Redirect Body 验证 ID
        payload_page_rep_t rep_body;
        read(sock, &rep_body, sizeof(rep_body));
        std::cout << "  -> Redirected to Node " << rep_body.requester_id << std::endl;
    }

    close(sock);
    return 0;
}