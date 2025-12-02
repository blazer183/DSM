// src/concurrent/concurrent_daemon.cpp
// 简化版本：仅实现 Barrier 同步所需的 JOIN_REQ/ACK 处理

#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

#include "concurrent/concurrent_core.h"
#include "net/protocol.h"
#include "dsm.h"

// 全局状态：JOIN 请求收集
namespace {
    std::mutex join_mutex;
    std::vector<int> joined_fds;        // 已连接的客户端 socket (可以双向通信)
    bool barrier_ready = false;         // 是否所有进程已就绪
}

// 单个客户端连接处理线程
// 参数 connfd: accept() 返回的已连接 socket，这是与客户端通信的唯一通道
// 注意：payload 中的 listen_port 是客户端的 daemon 监听端口，不是用来连接的
void peer_handler(int connfd) {
    rio_t rp;
    rio_readinit(&rp, connfd);

    dsm_header_t header;
    payload_join_req_t join_payload;

    // 读取消息头
    ssize_t n = rio_readn(&rp, &header, sizeof(dsm_header_t));
    if (n != sizeof(dsm_header_t)) {
        std::cerr << "[DSM Daemon] 读取头部失败，关闭连接" << std::endl;
        close(connfd);
        return;
    }

    // 验证消息类型
    if (header.type != DSM_MSG_JOIN_REQ) {
        std::cerr << "[DSM Daemon] 收到非 JOIN_REQ 消息: 0x" 
                  << std::hex << (int)header.type << std::dec << std::endl;
        close(connfd);
        return;
    }

    // 读取 Payload
    if (header.payload_len != sizeof(payload_join_req_t)) {
        std::cerr << "[DSM Daemon] JOIN_REQ payload 长度错误" << std::endl;
        close(connfd);
        return;
    }

    n = rio_readn(&rp, &join_payload, sizeof(payload_join_req_t));
    if (n != sizeof(payload_join_req_t)) {
        std::cerr << "[DSM Daemon] 读取 JOIN_REQ payload 失败" << std::endl;
        close(connfd);
        return;
    }

    std::cout << "[DSM Daemon] 收到 JOIN_REQ: NodeId=" << header.src_node_id 
              << ", DaemonPort=" << join_payload.listen_port 
              << " (注意：ACK 通过当前 socket 发送，不连接此端口)" << std::endl;

    // 加入已连接列表
    bool should_broadcast = false;
    {
        std::lock_guard<std::mutex> lock(join_mutex);
        joined_fds.push_back(connfd);  // 保存 socket，这是与客户端通信的唯一通道
        
        std::cout << "[DSM Daemon] 当前已连接: " << joined_fds.size() 
                  << " / " << ProcNum << std::endl;

        // 检查是否所有进程都已连接（使用全局变量 ProcNum）
        if (joined_fds.size() == static_cast<size_t>(ProcNum) && !barrier_ready) {
            barrier_ready = true;
            should_broadcast = true;
        }
    }

    // 如果所有进程就绪，批量发送 ACK
    if (should_broadcast) {
        std::cout << "[DSM Daemon] 所有进程已就绪，批量发送 JOIN_ACK..." << std::endl;

        dsm_header_t ack_header;
        ack_header.type = DSM_MSG_JOIN_ACK;
        ack_header.payload_len = 0;  // 无 payload
        ack_header.src_node_id = NodeId;  // 告诉客户端这是谁发的
        ack_header.seq_num = 0;

        std::lock_guard<std::mutex> lock(join_mutex);
        for (int fd : joined_fds) {
            // 关键：直接通过 accept() 得到的 socket 发送
            // 客户端的 barrier() 函数在 rio_readn() 处阻塞等待这个 ACK
            ssize_t nw = write(fd, &ack_header, sizeof(dsm_header_t));
            if (nw != sizeof(dsm_header_t)) {
                std::cerr << "[DSM Daemon] 发送 ACK 到 fd=" << fd << " 失败" << std::endl;
            } else {
                std::cout << "[DSM Daemon] 已发送 JOIN_ACK 到 fd=" << fd << std::endl;
            }
        }

        std::cout << "[DSM Daemon] ? Barrier 同步完成！" << std::endl;
    }

    // 保持连接打开，由客户端决定何时关闭
    // 或者用于后续通信（如 PAGE_REQ/LOCK_REQ）
}

// 主监听线程
void dsm_start_daemon(int port) {
    int listenfd, connfd;
    struct sockaddr_in clientaddr;
    socklen_t clientlen;
    struct sockaddr_in serveraddr;

    // 1. 创建监听 socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("[DSM Daemon] socket 创建失败");
        return;
    }

    // 2. 允许端口复用（避免 TIME_WAIT 状态导致绑定失败）
    int optval = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval, sizeof(int)) < 0) {
        perror("[DSM Daemon] setsockopt 失败");
        close(listenfd);
        return;
    }

    // 3. 绑定到指定端口（9999 + NodeID）
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);  // 监听所有网卡
    serveraddr.sin_port = htons((unsigned short)port);

    if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("[DSM Daemon] bind 失败");
        close(listenfd);
        return;
    }

    // 4. 开始监听
    if (listen(listenfd, 1024) < 0) {
        perror("[DSM Daemon] listen 失败");
        close(listenfd);
        return;
    }

    std::cout << "[DSM Daemon] 开始监听端口 " << port << "..." << std::endl;

    // 5. 循环接受连接
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (connfd < 0) {
            continue;  // accept 可能因信号中断，继续即可
        }

        // 6. 为每个连接启动独立线程处理
        //    connfd 是一个完整的双向通信通道：
        //    - 线程可以从它 read() 接收消息
        //    - 线程可以向它 write() 发送回复
        std::thread t(peer_handler, connfd);
        t.detach();  // 分离线程，让它自己运行
    }
}
