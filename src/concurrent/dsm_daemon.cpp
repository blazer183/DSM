// src/concurrent/dsm_daemon.cpp

#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 

// 引入你的头文件
#include "net/protocol.h"
#include "net/dsm_protocol.h"
#include "dsm_state.hpp"

// ============================================
// 业务逻辑处理函数 (私有)
// ============================================

// 处理页面请求: Requestor -> Manager
// ---------------------------------------------------------
// 修正后的 process_page_req (适配队友的新 struct PageRecord)
// ---------------------------------------------------------
void process_page_req(int sock, const dsm_header_t& head, const payload_page_req_t& body) {
    auto& state = DSMState::GetInstance();
    std::lock_guard<std::mutex> lock(state.state_mutex);

    uintptr_t page_addr = (uintptr_t)state.shared_mem_base + (body.page_index * DSM_PAGE_SIZE);
    
    // 1. 查表
    auto* record = state.page_table.Find(page_addr);
    
    // 2. 直接访问 owner_id (因为它是 struct)
    int current_owner = record ? record->owner_id : -1; 

    if (current_owner == state.my_node_id || current_owner == -1) {
        // Case A: 我是 Owner
        // TODO: 发送 DSM_MSG_PAGE_DATA (目前先略过数据发送)
        
        // 更新所有权
        if (record) {
            record->owner_id = head.src_node_id;
        } else {
            // 注意：PageRecord 是 struct，用大括号 {} 初始化
            PageRecord new_rec; 
            new_rec.owner_id = head.src_node_id;
            state.page_table.Insert(page_addr, new_rec);
        }
        std::cout << "[DSM] Page " << body.page_index << " transfer to Node " << head.src_node_id << std::endl;
    } else {
        // Case B: 转发
        std::cout << "[DSM] Forward Page Req to RealOwner: " << current_owner << std::endl;
    }
}

// ---------------------------------------------------------
// 修正后的 process_lock_acq (适配队友的新 struct LockRecord)
// ---------------------------------------------------------
void process_lock_acq(int sock, const dsm_header_t& head, const payload_lock_req_t& body) {
    auto& state = DSMState::GetInstance();
    std::lock_guard<std::mutex> lock(state.state_mutex);

    int lock_id = body.lock_id;
    int requester = head.src_node_id;

    // 1. 尝试获取锁 (LockTable 类自带的方法)
    bool success = state.lock_table.TryAcquire(lock_id, requester);

    // 2. 准备回包
    dsm_header_t reply_head;
    reply_head.type = DSM_MSG_LOCK_REP;
    reply_head.src_node_id = state.my_node_id;
    reply_head.seq_num = head.seq_num;
    
    payload_lock_rep_t reply_body;
    reply_body.lock_id = lock_id;
    reply_body.realowner = requester; 
    reply_body.invalid_set_count = 0;

    if (success) {
        reply_head.payload_len = sizeof(reply_body);
        reply_head.unused = 1; // 成功
        rio_writen(sock, &reply_head, sizeof(reply_head));
        rio_writen(sock, &reply_body, sizeof(reply_body));
        std::cout << "[DSM] Lock " << lock_id << " GRANTED to Node " << requester << std::endl;
    } else {
        // 失败
        reply_head.payload_len = 0; // 失败包不带body，或者带空body
        reply_head.unused = 0;      // 失败
        rio_writen(sock, &reply_head, sizeof(reply_head));
        
        // 获取当前锁的持有者，用于打印日志
        auto* rec = state.lock_table.Find(lock_id);
        // 直接访问 owner_id
        int holder = rec ? rec->owner_id : -1;
        
        std::cout << "[DSM] Lock " << lock_id << " BUSY (Held by " << holder << ")" << std::endl;
    }
}

// ============================================
// 网络线程循环
// ============================================

// 单个对等节点(Peer)的处理线程
void peer_handler(int connfd) {
    rio_t rp;
    rio_readinit(&rp, connfd);

    dsm_header_t header;
    std::vector<char> buffer; // 动态缓冲区

    while (true) {
        // 1. 读取头部 (Blocking, robust)
        ssize_t n = rio_readn(&rp, &header, sizeof(dsm_header_t));
        if (n != sizeof(dsm_header_t)) {
            // 连接断开或错误
            close(connfd);
            return;
        }

        // 2. 准备缓冲区读取 Payload
        if (header.payload_len > 0) {
            if (header.payload_len > 10 * 1024 * 1024) { // 安全检查：限制最大10MB
                std::cerr << "[DSM] Payload too big!" << std::endl;
                break;
            }
            buffer.resize(header.payload_len);
            n = rio_readn(&rp, buffer.data(), header.payload_len);
            if (n != header.payload_len) break;
        }

        // 3. 消息分发 (Switch)
        switch (header.type) {
            case DSM_MSG_PAGE_REQ:
                process_page_req(connfd, header, *(payload_page_req_t*)buffer.data());
                break;

            case DSM_MSG_LOCK_ACQ:
                process_lock_acq(connfd, header, *(payload_lock_req_t*)buffer.data());
                break;
            
            // TODO: 处理 JOIN, ACK, OWNER_UPDATE 等
            default:
                std::cout << "[DSM] Unknown Msg Type: " << (int)header.type << std::endl;
        }
    }
}

// 主监听函数 (需要在 main 或初始化中启动)
void dsm_start_daemon(int port) {
    int listenfd, connfd;
    struct sockaddr_in clientaddr;
    socklen_t clientlen;
    struct sockaddr_in serveraddr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    
    // 【新增】允许端口复用，防止重启Server时报 "Address already in use"
    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)port);

    if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("bind failed"); // 打印错误信息
        return;
    }
    
    if (listen(listenfd, 5) < 0) {
        perror("listen failed");
        return;
    }

    std::cout << "[DSM] Daemon listening on port " << port << std::endl;

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (connfd < 0) continue;
        
        std::cout << "[DSM] New connection accepted" << std::endl;
        std::thread t(peer_handler, connfd);
        t.detach();
    }
}