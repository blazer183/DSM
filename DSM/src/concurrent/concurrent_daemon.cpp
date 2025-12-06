// src/concurrent/concurrent_daemon.cpp
// Simplified implementation of Barrier synchronization and JOIN_REQ/ACK handling

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

// Global state for JOIN barrier synchronization

std::mutex join_mutex;                  // Protects shared state
std::vector<int> joined_fds;           // Connected client sockets (bidirectional channels)
bool barrier_ready = false;            // Whether all processes have joined


// Handle client connection processing thread
// Parameter connfd: the connected socket returned by accept(), the only communication channel with the client
// Note: The listen_port in the payload is the client's daemon listening port, not used for connection
void peer_handler(int connfd) {
    rio_t rp;
    rio_readinit(&rp, connfd);

    // Process messages in a loop for this connection
    while (true) {
        dsm_header_t header;

        // Read message header
        ssize_t n = rio_readn(&rp, &header, sizeof(dsm_header_t));
        if (n != sizeof(dsm_header_t)) {
            if (n == 0) {
                // Connection closed cleanly
                std::cout << "[DSM Daemon] Connection closed by peer" << std::endl;
            } else {
                std::cerr << "[DSM Daemon] Failed to read header, closing connection" << std::endl;
            }
            close(connfd);
            return;
        }

        // Handle different message types
        if (header.type == DSM_MSG_JOIN_REQ) {
            std::cout << "[DSM Daemon] Received JOIN_REQ: NodeId=" << ntohs(header.src_node_id) << std::endl; 

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
                ack_header.src_node_id = htons(NodeId);  // Let client know who sent this
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
        } 
        else if (header.type == DSM_MSG_LOCK_ACQ) {
            // Read lock request payload
            payload_lock_req_t req_payload;
            if (rio_readn(&rp, &req_payload, sizeof(req_payload)) != sizeof(req_payload)) {
                std::cerr << "[DSM Daemon] Failed to read LOCK_ACQ payload" << std::endl;
                close(connfd);
                return;
            }

            uint32_t lockid = ntohl(req_payload.lock_id);
            uint16_t requester_id = ntohs(header.src_node_id);
            
            std::cout << "[DSM Daemon] Received LOCK_ACQ: lockid=" << lockid 
                      << " from NodeId=" << requester_id << std::endl;

            // Check if we own this lock
            LockTable->LockAcquire();
            auto lock_rec = LockTable->Find(lockid);
            
            dsm_header_t rep_header;
            rep_header.type = DSM_MSG_LOCK_REP;
            rep_header.src_node_id = htons(NodeId);
            rep_header.seq_num = header.seq_num;
            rep_header.payload_len = htonl(sizeof(payload_lock_rep_t));

            payload_lock_rep_t rep_payload;
            rep_payload.invalid_set_count = 0;
            
            if (lock_rec == nullptr) {
                // We don't have this lock, create it and give it to requester
                LockTable->Insert(lockid, LockRecord(requester_id));
                rep_header.unused = 1;  // We are granting the lock
                rep_payload.realowner = htonl(static_cast<uint32_t>(-1));
                
                std::cout << "[DSM Daemon] Granting new lock " << lockid 
                          << " to NodeId=" << requester_id << std::endl;
            } 
            else if (lock_rec->locked && lock_rec->owner_id != NodeId) {
                // Lock is held by someone else (not us), redirect to real owner
                rep_header.unused = 0;  // Redirect
                rep_payload.realowner = htonl(lock_rec->owner_id);
                
                std::cout << "[DSM Daemon] Redirecting lock request for " << lockid 
                          << " to real owner NodeId=" << lock_rec->owner_id << std::endl;
            }
            else if (lock_rec->locked && lock_rec->owner_id == NodeId) {
                // We own this lock and it's still locked - requester must wait
                // For simplicity, just keep the requester blocked until lock is released
                // In a real implementation, we would queue this request
                // For now, tell them to try again (redirect to ourselves)
                rep_header.unused = 0;  // Still locked, cannot grant
                rep_payload.realowner = htonl(NodeId);
                
                std::cout << "[DSM Daemon] Lock " << lockid 
                          << " is currently held by us (NodeId=" << NodeId 
                          << "), requester must wait" << std::endl;
            }
            else {
                // Lock exists but not locked, grant it to requester
                lock_rec->locked = true;
                lock_rec->owner_id = requester_id;
                LockTable->Update(lockid, *lock_rec);
                rep_header.unused = 1;  // We are granting the lock
                rep_payload.realowner = htonl(static_cast<uint32_t>(-1));
                
                std::cout << "[DSM Daemon] Granting existing lock " << lockid 
                          << " to NodeId=" << requester_id << std::endl;
            }
            
            LockTable->LockRelease();

            // Send response
            if (write(connfd, &rep_header, sizeof(rep_header)) != sizeof(rep_header)) {
                std::cerr << "[DSM Daemon] Failed to send LOCK_REP header" << std::endl;
                close(connfd);
                return;
            }
            if (write(connfd, &rep_payload, sizeof(rep_payload)) != sizeof(rep_payload)) {
                std::cerr << "[DSM Daemon] Failed to send LOCK_REP payload" << std::endl;
                close(connfd);
                return;
            }
        } 
        else {
            std::cerr << "[DSM Daemon] Received unknown message type: 0x" 
                      << std::hex << (int)header.type << std::dec << std::endl;
            close(connfd);
            return;
        }
    }
}

// Daemon listener thread
void dsm_start_daemon(int port) {
    int listenfd, connfd;
    struct sockaddr_in clientaddr;
    socklen_t clientlen;
    struct sockaddr_in serveraddr;

    // 1. Create listening socket
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
