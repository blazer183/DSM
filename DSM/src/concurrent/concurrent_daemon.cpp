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
    // Extract source node ID from header
    uint16_t src_node = ntohs(header.src_node_id);
    
    std::cout << "[DSM Daemon] Received JOIN_REQ: NodeId=" << src_node << std::endl;

    // Acquire the join mutex to protect shared state
    join_mutex.lock();
    
    // Check if this fd is already in the joined list
    bool already_joined = false;
    for (int fd : joined_fds) {
        if (fd == connfd) {
            already_joined = true;
            break;
        }
    }
    
    // Add to joined_fds if not already present
    if (!already_joined) {
        joined_fds.push_back(connfd);
    }
    
    joined_count++;
    
    std::cout << "[DSM Daemon] Currently connected: " << joined_count << " / " << ProcNum << std::endl;

    // Check if all processes have joined
    if (joined_count < ProcNum) {
        join_mutex.unlock();
        return true;  // Keep connection open and continue processing
    }
    
    // All processes have joined, broadcast JOIN_ACK to all
    std::cout << "[DSM Daemon] All processes ready, broadcasting JOIN_ACK..." << std::endl;
    
    joined_count = 0;  // Reset for potential future barriers
    
    for (int fd : joined_fds) {
        dsm_header_t ack = {
            DSM_MSG_ACK,
            0,
            htons(PodId),
            htonl(1),
            0
        };
        
        ssize_t sent = ::send(fd, &ack, sizeof(ack), 0);
        if (sent == sizeof(ack)) {
            std::cout << "[DSM Daemon] Sent JOIN_ACK to fd=" << fd << std::endl;
        } else {
            std::cerr << "[DSM Daemon] Failed to send JOIN_ACK to fd=" << fd << std::endl;
        }
    }
    
    std::cout << "[DSM Daemon] Barrier synchronization complete!" << std::endl;
    
    join_mutex.unlock();
    
    return true;  // Keep connection open for future messages
}

static bool handle_lock_acquire(int connfd, const dsm_header_t &header, rio_t &rp) {
    // Read the payload to get lock_id
    uint32_t payload_len = ntohl(header.payload_len);
    if (payload_len < sizeof(payload_lock_req_t)) {
        std::cerr << "[DSM Daemon] Invalid LOCK_ACQ payload length" << std::endl;
        return false;
    }

    payload_lock_req_t req_payload;
    if (rio_readn(&rp, &req_payload, sizeof(req_payload)) != sizeof(req_payload)) {
        std::cerr << "[DSM Daemon] Failed to read LOCK_ACQ payload" << std::endl;
        return false;
    }

    uint32_t lock_id = ntohl(req_payload.lock_id);
    uint16_t requester_id = ntohs(header.src_node_id);
    uint32_t seq_num = ntohl(header.seq_num);

    std::cout << "[DSM Daemon] Received LOCK_ACQ for lock " << lock_id 
              << " from NodeId=" << requester_id << std::endl;

    // Try to acquire the lock
    // First, acquire the specific lock's mutex
    bool lock_acquired = LockTable->Lock(lock_id);
    
    if (!lock_acquired) {
        std::cerr << "[DSM Daemon] Failed to acquire lock table entry for lock " << lock_id << std::endl;
        return false;
    }

    // Now acquire the global lock to read/update lock state
    LockTable->LockAcquire();
    
    LockRecord* record = LockTable->Find(lock_id);
    if (record == nullptr) {
        // Create new lock record
        LockRecord new_record;
        new_record.owner_id = requester_id;
        new_record.locked = true;
        LockTable->Insert(lock_id, new_record);
        record = LockTable->Find(lock_id);
    }

    // Check if lock is already held
    if (record->locked && record->owner_id != requester_id) {
        // Lock is held by another node
        std::cout << "[DSM Daemon] Lock " << lock_id << " is currently held by NodeId=" 
                  << record->owner_id << ", requester must wait" << std::endl;
        
        LockTable->LockRelease();
        
        // Send LOCK_REP with unused=0 to indicate lock is held
        dsm_header_t rep_header = {
            DSM_MSG_LOCK_REP,
            0,  // unused=0: lock is held by another
            htons(PodId),
            htonl(seq_num),
            htonl(sizeof(uint32_t))  // Just send invalid_set_count = 0
        };
        
        ::send(connfd, &rep_header, sizeof(rep_header), 0);
        
        uint32_t zero = 0;
        ::send(connfd, &zero, sizeof(zero), 0);
        
        // Don't unlock the lock_id mutex - keep it locked until lock is released
        // Return true to keep processing other messages
        return true;
    }

    // Grant the lock
    record->owner_id = requester_id;
    record->locked = true;
    
    // Prepare invalid page list from the record
    uint32_t invalid_count = record->invalid_page_list.size();
    uint32_t payload_len_rep = sizeof(uint32_t) + invalid_count * sizeof(uint32_t);

    std::cout << "[DSM Daemon] Granting lock " << lock_id << " to NodeId=" << requester_id 
              << " with " << invalid_count << " invalid pages" << std::endl;

    LockTable->LockRelease();

    // Send LOCK_REP with unused=1 to indicate lock is granted
    dsm_header_t rep_header = {
        DSM_MSG_LOCK_REP,
        1,  // unused=1: lock is granted
        htons(PodId),
        htonl(seq_num),
        htonl(payload_len_rep)
    };

    if (::send(connfd, &rep_header, sizeof(rep_header), 0) != sizeof(rep_header)) {
        std::cerr << "[DSM Daemon] Failed to send LOCK_REP header" << std::endl;
        LockTable->Unlock(lock_id);
        return false;
    }

    // Send invalid_set_count
    uint32_t invalid_count_net = htonl(invalid_count);
    if (::send(connfd, &invalid_count_net, sizeof(invalid_count_net), 0) != sizeof(invalid_count_net)) {
        std::cerr << "[DSM Daemon] Failed to send invalid_set_count" << std::endl;
        LockTable->Unlock(lock_id);
        return false;
    }

    // Send invalid page list
    LockTable->LockAcquire();
    record = LockTable->Find(lock_id);
    if (record != nullptr) {
        for (int page_idx : record->invalid_page_list) {
            uint32_t page_idx_net = htonl(page_idx);
            if (::send(connfd, &page_idx_net, sizeof(page_idx_net), 0) != sizeof(page_idx_net)) {
                std::cerr << "[DSM Daemon] Failed to send invalid page index" << std::endl;
                LockTable->LockRelease();
                LockTable->Unlock(lock_id);
                return false;
            }
        }
    }
    LockTable->LockRelease();

    // Now wait for LOCK_RLS message on this connection
    dsm_header_t rls_header;
    if (rio_readn(&rp, &rls_header, sizeof(rls_header)) != sizeof(rls_header)) {
        std::cerr << "[DSM Daemon] Failed to read LOCK_RLS header" << std::endl;
        LockTable->Unlock(lock_id);
        return false;
    }

    if (rls_header.type != DSM_MSG_LOCK_RLS) {
        std::cerr << "[DSM Daemon] Expected LOCK_RLS but got type " << (int)rls_header.type << std::endl;
        LockTable->Unlock(lock_id);
        return false;
    }

    // Read the release payload
    uint32_t rls_payload_len = ntohl(rls_header.payload_len);
    if (rls_payload_len >= sizeof(uint32_t)) {
        uint32_t rls_invalid_count;
        if (rio_readn(&rp, &rls_invalid_count, sizeof(rls_invalid_count)) != sizeof(rls_invalid_count)) {
            std::cerr << "[DSM Daemon] Failed to read LOCK_RLS invalid_set_count" << std::endl;
            LockTable->Unlock(lock_id);
            return false;
        }
        
        rls_invalid_count = ntohl(rls_invalid_count);
        
        std::cout << "[DSM Daemon] Received LOCK_RLS for lock " << lock_id 
                  << " with " << rls_invalid_count << " invalid pages" << std::endl;

        // Read invalid page list and update lock record
        std::vector<int> new_invalid_pages;
        for (uint32_t i = 0; i < rls_invalid_count; i++) {
            uint32_t page_idx;
            if (rio_readn(&rp, &page_idx, sizeof(page_idx)) != sizeof(page_idx)) {
                std::cerr << "[DSM Daemon] Failed to read invalid page index" << std::endl;
                break;
            }
            new_invalid_pages.push_back(ntohl(page_idx));
        }

        // Update lock record with new invalid pages
        LockTable->LockAcquire();
        record = LockTable->Find(lock_id);
        if (record != nullptr) {
            record->invalid_page_list = new_invalid_pages;
            record->invalid_set_count = rls_invalid_count;
            record->locked = false;
            record->owner_id = -1;
        }
        LockTable->LockRelease();
    }

    // Send ACK for the release
    dsm_header_t ack = {
        DSM_MSG_ACK,
        0,
        htons(PodId),
        htonl(ntohl(rls_header.seq_num)),
        0
    };
    
    ::send(connfd, &ack, sizeof(ack), 0);

    // Unlock the lock_id mutex
    LockTable->Unlock(lock_id);

    std::cout << "[DSM Daemon] Lock " << lock_id << " released" << std::endl;

    return true;
}

static bool handle_owner_update(int connfd, const dsm_header_t &header, rio_t &rp) {
    // Read the payload
    uint32_t payload_len = ntohl(header.payload_len);
    if (payload_len < sizeof(payload_owner_update_t)) {
        std::cerr << "[DSM Daemon] Invalid OWNER_UPDATE payload length" << std::endl;
        return false;
    }

    payload_owner_update_t update_payload;
    if (rio_readn(&rp, &update_payload, sizeof(update_payload)) != sizeof(update_payload)) {
        std::cerr << "[DSM Daemon] Failed to read OWNER_UPDATE payload" << std::endl;
        return false;
    }

    uint32_t resource_id = ntohl(update_payload.resource_id);
    uint16_t new_owner_id = ntohs(update_payload.new_owner_id);
    uint16_t src_node = ntohs(header.src_node_id);
    uint32_t seq_num = ntohl(header.seq_num);

    std::cout << "[DSM Daemon] Received OWNER_UPDATE: page " << resource_id 
              << " new owner=" << new_owner_id << " from NodeId=" << src_node << std::endl;

    // Update PageTable
    PageTable->LockAcquire();
    
    // For now, we'll just acknowledge the update
    // In a full implementation, we would update the PageTable entry for this page
    // PageTable->Update(resource_id, new_page_record);
    
    PageTable->LockRelease();

    // Send ACK
    dsm_header_t ack = {
        DSM_MSG_ACK,
        0,
        htons(PodId),
        htonl(seq_num),
        0
    };
    
    if (::send(connfd, &ack, sizeof(ack), 0) != sizeof(ack)) {
        std::cerr << "[DSM Daemon] Failed to send ACK for OWNER_UPDATE" << std::endl;
        return false;
    }

    std::cout << "[DSM Daemon] Sent ACK for OWNER_UPDATE" << std::endl;

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

    // 2. Set socket options to reuse address
    int optval = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval, sizeof(int)) < 0) {
        perror("[DSM Daemon] setsockopt failed");
        close(listenfd);
        return;
    }

    // 3. Bind socket to specified port
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);  // Listen on all interfaces
    serveraddr.sin_port = htons((unsigned short)port);


    if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("[DSM Daemon] bind failed");
        close(listenfd);
        return;
    }

    // 4. Start listening
    if (listen(listenfd, 1024) < 0) {
        perror("[DSM Daemon] listen failed");
        close(listenfd);
        return;
    }

    std::cout << "[DSM Daemon] Listening on port " << port << "..." << std::endl;

    // 5. Accept incoming connections in a loop
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (connfd < 0) {
            continue;  // accept failed, retry
        }

        // 6. Create a new thread for each accepted connection
        //    connfd is a bidirectional communication channel
        //    - The thread uses read() to receive messages
        //    - The thread uses write() to send messages
        std::thread t(peer_handler, connfd);
        t.detach();  // Detach the thread to allow independent execution
    }
}
