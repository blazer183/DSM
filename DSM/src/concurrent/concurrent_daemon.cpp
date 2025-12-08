#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <map>
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

// Global state for lock tracking
std::mutex lock_tracking_mutex;
std::map<uint16_t, uint32_t> node_to_lock_map;  // Maps node_id to lock_id it currently holds


// Forward declarations for message handlers
static bool handle_join_request(int connfd, const dsm_header_t &header);
static bool handle_lock_acquire(int connfd, const dsm_header_t &header, rio_t &rp);
static bool handle_lock_release(int connfd, const dsm_header_t &header, rio_t &rp);
static bool handle_owner_update(int connfd, const dsm_header_t &header, rio_t &rp);
static bool handle_page_require(int connfd, const dsm_header_t &header, rio_t &rp);
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
    
    std::cout << "[DSM Daemon] Currently connected: socket number " << connfd << std::endl;

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

    // Prepare or create lock record under table mutex
    LockTable->LockAcquire();
    LockRecord* record = LockTable->Find(lock_id);
    if (record == nullptr) {
        LockRecord new_record;
        LockTable->Insert(lock_id, new_record);
        record = LockTable->Find(lock_id);
    }
    LockTable->LockRelease();

    // Block until we acquire the pthread mutex for this lock
    int lock_error = pthread_mutex_lock(&record->lock_id);
    if (lock_error != 0) {
        std::cerr << "[DSM Daemon] pthread_mutex_lock failed for lock " << lock_id
                  << ": " << lock_error << std::endl;
        return false;
    }

    // Update record metadata now that we hold the mutex
    LockTable->LockAcquire();
    record->owner_id = requester_id;
    record->locked = true;
    
    // Track which lock this node holds
    {
        std::lock_guard<std::mutex> guard(lock_tracking_mutex);
        node_to_lock_map[requester_id] = lock_id;
    }
    
    // Prepare invalid page list from the record
    uint32_t invalid_count = record->invalid_page_list.size();
    uint32_t payload_len_rep = sizeof(uint32_t) + invalid_count * sizeof(uint32_t);

    std::cout << "[DSM Daemon] Granting lock " << lock_id << " to NodeId=" << requester_id 
              << " with " << invalid_count << " invalid pages" << std::endl;

    // Copy invalid page list before releasing table lock
    std::vector<int> invalid_pages = record->invalid_page_list;
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
        // Unlock the mutex before returning
        LockTable->LockAcquire();
        LockRecord* rec = LockTable->Find(lock_id);
        if (rec != nullptr) {
            pthread_mutex_unlock(&rec->lock_id);
        }
        LockTable->LockRelease();
        return false;
    }

    // Send invalid_set_count
    uint32_t invalid_count_net = htonl(invalid_count);
    if (::send(connfd, &invalid_count_net, sizeof(invalid_count_net), 0) != sizeof(invalid_count_net)) {
        std::cerr << "[DSM Daemon] Failed to send invalid_set_count" << std::endl;
        // Unlock the mutex before returning
        LockTable->LockAcquire();
        LockRecord* rec = LockTable->Find(lock_id);
        if (rec != nullptr) {
            pthread_mutex_unlock(&rec->lock_id);
        }
        LockTable->LockRelease();
        return false;
    }

    // Send invalid page list
    for (int page_idx : invalid_pages) {
        uint32_t page_idx_net = htonl(page_idx);
        if (::send(connfd, &page_idx_net, sizeof(page_idx_net), 0) != sizeof(page_idx_net)) {
            std::cerr << "[DSM Daemon] Failed to send invalid page index" << std::endl;
            // Unlock the mutex before returning
            LockTable->LockAcquire();
            LockRecord* rec = LockTable->Find(lock_id);
            if (rec != nullptr) {
                pthread_mutex_unlock(&rec->lock_id);
            }
            LockTable->LockRelease();
            return false;
        }
    }

    std::cout << "[DSM Daemon] Lock " << lock_id << " granted and held by NodeId=" << requester_id << std::endl;

    return true;  // Continue processing other messages
}

static bool handle_owner_update(int connfd, const dsm_header_t &header, rio_t &rp) {
    /*
        read information from head/payload
        page_index = payload.page_index
        new_owner = payload.new_owner
        PageTable LockAquire()//全局
        change page record.owner = new_owner
        PageTable LockRelease()//全局
        PageTable unlock(page_index) //局部
    */
}

static bool handle_lock_release(int connfd, const dsm_header_t &header, rio_t &rp) {
    // Read the release payload
    uint32_t payload_len = ntohl(header.payload_len);
    if (payload_len < sizeof(uint32_t)) {
        std::cerr << "[DSM Daemon] Invalid LOCK_RLS payload length" << std::endl;
        return false;
    }

    uint32_t rls_invalid_count;
    if (rio_readn(&rp, &rls_invalid_count, sizeof(rls_invalid_count)) != sizeof(rls_invalid_count)) {
        std::cerr << "[DSM Daemon] Failed to read LOCK_RLS invalid_set_count" << std::endl;
        return false;
    }
    
    rls_invalid_count = ntohl(rls_invalid_count);
    uint16_t src_node = ntohs(header.src_node_id);
    uint32_t seq_num = ntohl(header.seq_num);
    
    // Read invalid page list
    std::vector<int> new_invalid_pages;
    for (uint32_t i = 0; i < rls_invalid_count; i++) {
        uint32_t page_idx;
        if (rio_readn(&rp, &page_idx, sizeof(page_idx)) != sizeof(page_idx)) {
            std::cerr << "[DSM Daemon] Failed to read invalid page index" << std::endl;
            break;
        }
        new_invalid_pages.push_back(ntohl(page_idx));
    }

    std::cout << "[DSM Daemon] Received LOCK_RLS from NodeId=" << src_node 
              << " with " << rls_invalid_count << " invalid pages" << std::endl;

    // Find which lock this node holds using our tracking map
    uint32_t lock_id = 0;
    bool found = false;
    {
        std::lock_guard<std::mutex> guard(lock_tracking_mutex);
        auto it = node_to_lock_map.find(src_node);
        if (it != node_to_lock_map.end()) {
            lock_id = it->second;
            found = true;
            node_to_lock_map.erase(it);  // Remove from tracking
        }
    }

    if (!found) {
        std::cerr << "[DSM Daemon] No lock found for NodeId=" << src_node << std::endl;
        // Still send ACK to avoid blocking the client
    } else {
        // Update lock record and unlock the pthread mutex
        LockTable->LockAcquire();
        
        LockRecord* record = LockTable->Find(lock_id);
        if (record != nullptr) {
            // Update invalid page list
            record->invalid_page_list = new_invalid_pages;
            record->invalid_set_count = rls_invalid_count;
            record->locked = false;
            record->owner_id = -1;
            
            // Unlock the pthread mutex to allow other requests
            pthread_mutex_unlock(&record->lock_id);
            
            std::cout << "[DSM Daemon] Released lock " << lock_id << std::endl;
        }
        
        LockTable->LockRelease();
    }

    // Send ACK for the release
    dsm_header_t ack = {
        DSM_MSG_ACK,
        0,
        htons(PodId),
        htonl(seq_num),
        0
    };
    
    if (::send(connfd, &ack, sizeof(ack), 0) != sizeof(ack)) {
        std::cerr << "[DSM Daemon] Failed to send ACK for LOCK_RLS" << std::endl;
        return false;
    }

    std::cout << "[DSM Daemon] Sent ACK for LOCK_RLS" << std::endl;

    return true;
}

static bool handle_unknown_message(int connfd, const dsm_header_t &header) {
    std::cerr << "[DSM Daemon] Received unknown message type: 0x"
              << std::hex << static_cast<int>(header.type) << std::dec << std::endl;
    close(connfd);
    return false;
}

static bool handle_page_require(int connfd, const dsm_header_t &header, rio_t &rp) {
    /*
        read information from head/payload
        page_indes = payload.page_index

        PageTable lock(page_index) //局部
        //get page record
        if (record.realowner == PodId){
            send page immediately
        }else if(record.realowner == -1 && PodId == 0){
            //first load
            look up bind table and load the coorresponding file
            return page
        }else if(record.realowner == -1 && PodId != 0){
            //not first load
            send owner request to Pod 0
            wait for page data
            return page data
        }else{
            return pagerecord.realowner
            return 
        }
    
    */
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
            case DSM_MSG_LOCK_RLS:
                keep_processing = handle_lock_release(connfd, header, rp);
                break;
            case DSM_MSG_OWNER_UPDATE:
                keep_processing = handle_owner_update(connfd, header, rp);
                break;
            case DSM_MSG_PAGE_REQ:
                keep_processing = handle_page_require(connfd, header, rp);
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

    // 2. Set socket options to reuse address/port so TIME_WAIT sockets don't block restart
    int optval = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                   (const void *)&optval, sizeof(optval)) < 0) {
        perror("[DSM Daemon] setsockopt SO_REUSEADDR failed");
        close(listenfd);
        return;
    }
#ifdef SO_REUSEPORT
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT,
                   (const void *)&optval, sizeof(optval)) < 0) {
        perror("[DSM Daemon] setsockopt SO_REUSEPORT failed");
        close(listenfd);
        return;
    }
#endif

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

        std::thread t(peer_handler, connfd);
        t.detach();  
    }
}
