// src/concurrent/concurrent_daemon.cpp
// �򻯰汾����ʵ�� Barrier ͬ������� JOIN_REQ/ACK ����

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

// ȫ��״̬��JOIN �����ռ�

std::mutex join_mutex;                  // ��������
std::vector<int> joined_fds;        // �����ӵĿͻ��� socket (����˫��ͨ��)
bool barrier_ready = false;         // �Ƿ����н����Ѿ���


// Handle client connection processing thread
// Parameter connfd: the connected socket returned by accept(), the only communication channel with the client
// Note: The listen_port in the payload is the client's daemon listening port, not used for connection
void peer_handler(int connfd) {
    rio_t rp;
    rio_readinit(&rp, connfd);

    dsm_header_t header;

    // Read message header
    ssize_t n = rio_readn(&rp, &header, sizeof(dsm_header_t));
    if (n != sizeof(dsm_header_t)) {
        std::cerr << "[DSM Daemon] Failed to read header, closing connection" << std::endl;
        close(connfd);
        return;
    }

    // Verify message type
    if (header.type != DSM_MSG_JOIN_REQ) {
        std::cerr << "[DSM Daemon] Received non-JOIN_REQ message: 0x" 
                  << std::hex << (int)header.type << std::dec << std::endl;
        close(connfd);
        return;
    }

    std::cout << "[DSM Daemon] Received JOIN_REQ: NodeId=" << header.src_node_id << std::endl; 

    // Add connection to list (protected by mutex)
    bool should_broadcast = false;
    {
        std::lock_guard<std::mutex> lock(join_mutex);
        
        joined_fds.push_back(connfd);
        
        std::cout << "[DSM Daemon] Currently connected: " << joined_fds.size() 
                  << " / " << ProcNum << std::endl;

        // Check if all processes have connected
        // Note: ProcNum is the total number of processes including leader
        // Each process (including leader) will call dsm_barrier() which sends JOIN_REQ
        if (joined_fds.size() == static_cast<size_t>(ProcNum) && !barrier_ready) {
            barrier_ready = true;
            should_broadcast = true;
        }
    }

    // If all processes have joined, broadcast ACK to all
    if (should_broadcast) {
        std::cout << "[DSM Daemon] All processes ready, broadcasting JOIN_ACK..." << std::endl;

        dsm_header_t ack_header;
        ack_header.type = DSM_MSG_JOIN_ACK;
        ack_header.payload_len = 0;  // No payload
        ack_header.src_node_id = NodeId;  // Let client know who sent this
        ack_header.seq_num = 0;
        ack_header.unused = 0;

        std::lock_guard<std::mutex> lock(join_mutex);
        
        // Send ACK to all processes
        for (size_t i = 0; i < joined_fds.size(); i++) {
            int fd = joined_fds[i];
            ssize_t nw = write(fd, &ack_header, sizeof(dsm_header_t));
            if (nw != sizeof(dsm_header_t)) {
                std::cerr << "[DSM Daemon] Failed to send ACK to fd=" << fd << std::endl;
            } else {
                std::cout << "[DSM Daemon] Sent JOIN_ACK to fd=" << fd << std::endl;
            }
        }

        std::cout << "[DSM Daemon] Barrier synchronization complete!" << std::endl;
    }

    // Keep connection open, can be used for later communication
    // For follow-up messages like PAGE_REQ/LOCK_REQ
}

// �������߳�
void dsm_start_daemon(int port) {
    int listenfd, connfd;
    struct sockaddr_in clientaddr;
    socklen_t clientlen;
    struct sockaddr_in serveraddr;

    // 1. �������� socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("[DSM Daemon] socket creation failed");
        return;
    }

    // 2. �����˿ڸ��ã����� TIME_WAIT ״̬���°�ʧ�ܣ�
    int optval = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval, sizeof(int)) < 0) {
        perror("[DSM Daemon] setsockopt failed");
        close(listenfd);
        return;
    }

    // 3. �󶨵�ָ���˿ڣ�9999 + NodeID��
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);  // ������������
    serveraddr.sin_port = htons((unsigned short)port);


    if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("[DSM Daemon] bind failed");
        close(listenfd);
        return;
    }

    // 4. ��ʼ����
    if (listen(listenfd, 1024) < 0) {
        perror("[DSM Daemon] listen failed");
        close(listenfd);
        return;
    }

    std::cout << "[DSM Daemon] Listening on port " << port << "..." << std::endl;

    // 5. ѭ����������
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (connfd < 0) {
            continue;  // accept �������ź��жϣ���������
        }

        // 6. Ϊÿ���������������̴߳���
        //    connfd ��һ��������˫��ͨ��ͨ����
        //    - �߳̿��Դ��� read() ������Ϣ
        //    - �߳̿������� write() ���ͻظ�
        std::thread t(peer_handler, connfd);
        t.detach();  // �����̣߳������Լ�����
    }
}
