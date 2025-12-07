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
int joined_count = 0;                  // Counter for joined processes (protected by join_mutex)


// Forward declarations for message handlers
static bool handle_join_request(int connfd, const dsm_header_t &header);
static bool handle_lock_acquire(int connfd, const dsm_header_t &header, rio_t &rp);
static bool handle_owner_update(int connfd, const dsm_header_t &header, rio_t &rp);
static bool handle_unknown_message(int connfd, const dsm_header_t &header);


static bool handle_join_request(int connfd, const dsm_header_t &header) {
    (void)connfd; // Unused, kept for consistent signature

    std::cout << "[DSM Daemon] Received JOIN_REQ: NodeId=" << ntohs(header.src_node_id) << std::endl;

    bool should_broadcast = false;
    {
        std::lock_guard<std::mutex> lock(join_mutex);

        joined_fds.push_back(connfd);
        joined_count++;

        std::cout << "[DSM Daemon] Currently connected: " << joined_count
                  << " / " << ProcNum << std::endl;

        if (joined_count == ProcNum && !barrier_ready) {
            barrier_ready = true;
            should_broadcast = true;
        }
    }

    if (should_broadcast) {
        std::cout << "[DSM Daemon] All processes ready, broadcasting JOIN_ACK..." << std::endl;

        dsm_header_t ack_header{};
        ack_header.type = DSM_MSG_JOIN_ACK;
        ack_header.payload_len = 0;
        ack_header.src_node_id = htons(NodeId);
        ack_header.seq_num = 0;
        ack_header.unused = 0;

        std::lock_guard<std::mutex> lock(join_mutex);
        for (int fd : joined_fds) {
            ssize_t nw = write(fd, &ack_header, sizeof(dsm_header_t));
            if (nw != sizeof(dsm_header_t)) {
                std::cerr << "[DSM Daemon] Failed to send JOIN_ACK to fd=" << fd << std::endl;
            } else {
                std::cout << "[DSM Daemon] Sent JOIN_ACK to fd=" << fd << std::endl;
            }
        }

        std::cout << "[DSM Daemon] Barrier synchronization complete!" << std::endl;
    }

    return true;
}

static bool handle_lock_acquire(int connfd, const dsm_header_t &header, rio_t &rp) {
    /*
    这里把网络分布式锁调用转化为局部的线程锁调用
    LockTable.Lock(lock_id);    //获取原子锁
    LockTable.LockAquire(); //获取全局锁
    send:
        struct {
        DSM_MSG_LOCK_REP 
        1				//！！！
        PodId
        seq_num
        sizeof(payload)
    }

    payload 
    // [DSM_MSG_LOCK_REP] Manager -> Requestor (授予锁)
    typedef struct {
        uint32_t invalid_set_count; 
        vector invalid_page_list;
    } __attribute__((packed)) payload_lock_rep_t;

    等待锁释放消息
    rcv: LOCK RLS
    rcv: payload

    LockTable.Update(lock_id); //更新invalid page list
    LockTable.LockRelease(); //释放全局锁
    LockTable.Unlock(lock_id); //释放原子锁
    return ;

    */
}

static bool handle_owner_update(int connfd, const dsm_header_t &header, rio_t &rp) {
    payload_owner_update_t update_payload{};
    if (rio_readn(&rp, &update_payload, sizeof(update_payload)) != sizeof(update_payload)) {
        std::cerr << "[DSM Daemon] Failed to read OWNER_UPDATE payload" << std::endl;
        close(connfd);
        return false;
    }

    uint32_t resource_id = ntohl(update_payload.resource_id);
    uint16_t new_owner = ntohs(update_payload.new_owner_id);
    uint16_t requester_id = ntohs(header.src_node_id);

    if (header.unused == 0) {
        std::cout << "[DSM Daemon] Received OWNER_UPDATE for lock " << resource_id
                  << " from NodeId=" << requester_id << ", new owner=" << new_owner << std::endl;

        LockTable->LockAcquire();
        auto lock_rec = LockTable->Find(resource_id);
        if (lock_rec != nullptr) {
            lock_rec->owner_id = new_owner;
            lock_rec->locked = true;
            LockTable->Update(resource_id, *lock_rec);
        } else {
            LockTable->Insert(resource_id, LockRecord(new_owner));
        }
        LockTable->LockRelease();

        std::cout << "[DSM Daemon] Updated lock " << resource_id
                  << " owner to NodeId=" << new_owner << std::endl;
    } else {
        std::cout << "[DSM Daemon] Received OWNER_UPDATE for page " << resource_id
                  << " from NodeId=" << requester_id << ", new owner=" << new_owner << std::endl;

        PageTable->LockAcquire();
        auto page_rec = PageTable->Find(resource_id);
        if (page_rec != nullptr) {
            page_rec->owner_id = new_owner;
            PageTable->Update(resource_id, *page_rec);
        }
        PageTable->LockRelease();

        std::cout << "[DSM Daemon] Updated page " << resource_id
                  << " owner to NodeId=" << new_owner << std::endl;
    }

    dsm_header_t ack_header{};
    ack_header.type = DSM_MSG_ACK;
    ack_header.unused = 0;
    ack_header.src_node_id = htons(NodeId);
    ack_header.seq_num = header.seq_num;
    ack_header.payload_len = htonl(sizeof(payload_ack_t));

    payload_ack_t ack_payload{};
    ack_payload.status = 0;

    if (write(connfd, &ack_header, sizeof(ack_header)) != sizeof(ack_header)) {
        std::cerr << "[DSM Daemon] Failed to send ACK header" << std::endl;
        close(connfd);
        return false;
    }
    if (write(connfd, &ack_payload, sizeof(ack_payload)) != sizeof(ack_payload)) {
        std::cerr << "[DSM Daemon] Failed to send ACK payload" << std::endl;
        close(connfd);
        return false;
    }

    return true;
}

static bool handle_unknown_message(int connfd, const dsm_header_t &header) {
    std::cerr << "[DSM Daemon] Received unknown message type: 0x"
              << std::hex << static_cast<int>(header.type) << std::dec << std::endl;
    close(connfd);
    return false;
}



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

        bool keep_processing = true;
        switch (header.type) {
            case DSM_MSG_JOIN_REQ:
                keep_processing = handle_join_request(connfd, header);
                break;
            case DSM_MSG_LOCK_ACQ:
                keep_processing = handle_lock_acquire(connfd, header, rp);
                break;
            case DSM_MSG_OWNER_UPDATE:
                keep_processing = handle_owner_update(connfd, header, rp);
                break;
            default:
                keep_processing = handle_unknown_message(connfd, header);
                break;
        }

        if (!keep_processing) {
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
