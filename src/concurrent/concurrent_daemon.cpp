// src/concurrent/dsm_daemon.cpp

#include <iostream>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <cstring>


#include "concurrent/concurrent_core.h"
#include "net/protocol.h"


// 单个对等节点(Peer)的处理线程
// 负责：死循环读取 Header -> 读取 Payload -> 分发给对应函数
void peer_handler(int connfd) {
    rio_t rp;
    rio_readinit(&rp, connfd);

    dsm_header_t header;
    std::vector<char> buffer; // 动态缓冲区

    while (true) {
        // 1. 读取头部 (Blocking, robust)
        // 如果对方没有发数据，这里会阻塞挂起，不消耗 CPU
        ssize_t n = rio_readn(&rp, &header, sizeof(dsm_header_t));
        if (n != sizeof(dsm_header_t)) {
            // 返回 0 表示对方关闭连接，返回 -1 表示出错
            // std::cout << "[DSM] Peer disconnected (fd: " << connfd << ")" << std::endl;
            close(connfd);
            return;
        }

        // 2. 准备缓冲区读取 Payload
        if (header.payload_len > 0) {
            // 安全检查：限制最大 16MB (容纳超大页或大量 Diff)
            if (header.payload_len > 16 * 1024 * 1024) { 
                std::cerr << "[DSM] Fatal: Payload too big (" << header.payload_len << ")!" << std::endl;
                break;
            }
            
            // 调整 buffer 大小
            if (buffer.size() < header.payload_len) {
                buffer.resize(header.payload_len);
            }
            
            // 读取完整负载
            n = rio_readn(&rp, buffer.data(), header.payload_len);
            if (n != header.payload_len) {
                std::cerr << "[DSM] Read payload failed!" << std::endl;
                break;
            }
        }

        // 3. 消息分发 (Switch)
        // 注意：buffer.data() 返回的是 char*，我们需要根据 type 强转为对应的 struct 指针
        switch (header.type) {
            
            // --- A. 请求类 (别人请求我) ---
            case DSM_MSG_JOIN_REQ:
                process_join_req(connfd, header, *(payload_join_req_t*)buffer.data());
                break;

            case DSM_MSG_PAGE_REQ:
                process_page_req(connfd, header, *(payload_page_req_t*)buffer.data());
                break;

            case DSM_MSG_LOCK_ACQ:
                process_lock_acq(connfd, header, *(payload_lock_req_t*)buffer.data());
                break;
            
            case DSM_MSG_OWNER_UPDATE:
                process_owner_update(connfd, header, *(payload_owner_update_t*)buffer.data());
                break;

            // --- B. 回复类 (我请求后的响应) --- 
            // 【这是你缺失的部分，必须加上！】

            case DSM_MSG_JOIN_ACK:
                // 唤醒 concurrent_system_init
                process_join_ack(connfd, header, *(payload_join_ack_t*)buffer.data());
                break;

            case DSM_MSG_PAGE_REP:
                // 唤醒 dsm_request_page
                // 注意：这里传 buffer.data() (char*) 进去，
                // 因为它可能是 struct (重定向)，也可能是 4096字节数据
                process_page_rep(connfd, header, buffer.data());
                break;

            case DSM_MSG_LOCK_REP:
                // 唤醒 dsm_request_lock
                process_lock_rep(connfd, header, *(payload_lock_rep_t*)buffer.data());
                break;

            case DSM_MSG_ACK:
                process_ack(connfd, header, *(payload_ack_t*)buffer.data());
                break;

            default:
                std::cout << "[DSM] Unknown Msg Type: 0x" << std::hex << (int)header.type << std::dec << std::endl;
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
    if (listenfd < 0) {
        perror("socket failed");
        return;
    }
    
    // 允许端口复用
    int optval = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int)) < 0) {
        perror("setsockopt failed");
        return;
    }

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)port);

    if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("bind failed"); 
        close(listenfd);
        return;
    }
    
    if (listen(listenfd, 1024) < 0) { // 增加 backlog 队列长度
        perror("listen failed");
        close(listenfd);
        return;
    }

    std::cout << "[DSM] Daemon listening on port " << port << "..." << std::endl;

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (connfd < 0) {
            // accept 可能会因为信号中断而失败，continue 即可
            continue; 
        }
        
        // 打印连接信息 (可选)
        // char client_ip[INET_ADDRSTRLEN];
        // inet_ntop(AF_INET, &(clientaddr.sin_addr), client_ip, INET_ADDRSTRLEN);
        // std::cout << "[DSM] Accepted connection from " << client_ip << std::endl;

        // 启动独立线程处理该连接
        std::thread t(peer_handler, connfd);
        t.detach(); // 分离线程，让它自己在后台运行，结束后自动回收
    }
}