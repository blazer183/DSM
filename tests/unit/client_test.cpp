// tests/client_test.cpp
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>
#include "net/dsm_protocol.h" // 引用你的协议头文件

int main() {
    // 1. 创建 Socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080); // 连接 Server 的 8080 端口
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "[Client] Connection failed. Is server running?" << std::endl;
        return -1;
    }
    std::cout << "[Client] Connected to DSM Server." << std::endl;

    // 2. 构造请求：我是 Node 2，我想申请 99 号锁
    payload_lock_req_t req;
    req.lock_id = 99;

    dsm_header_t head;
    head.type = DSM_MSG_LOCK_ACQ;
    head.src_node_id = 2; 
    head.seq_num = 1;
    head.payload_len = sizeof(req);
    head.unused = 0;

    // 3. 发送请求
    write(sock, &head, sizeof(head)); // 发包头
    write(sock, &req, sizeof(req));   // 发包体
    std::cout << "[Client] Sent Lock Request for LockID 99" << std::endl;

    // 4. 接收响应
    dsm_header_t rep_head;
    int n = read(sock, &rep_head, sizeof(rep_head));
    
    if (n > 0 && rep_head.type == DSM_MSG_LOCK_REP) {
        payload_lock_rep_t rep_body;
        read(sock, &rep_body, sizeof(rep_body));
        
        std::cout << "--------------------------------" << std::endl;
        std::cout << "[Client] Received Response!" << std::endl;
        std::cout << "  Status: " << (rep_head.unused == 1 ? "GRANTED" : "REJECTED") << std::endl;
        std::cout << "  LockID: " << rep_body.lock_id << std::endl;
        std::cout << "  Owner : Node " << rep_body.realowner << std::endl;
        std::cout << "--------------------------------" << std::endl;
    } else {
        std::cout << "[Client] Failed to receive valid response" << std::endl;
    }

    close(sock);
    return 0;
}