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



std::mutex join_mutex;                  // Protects shared state
std::vector<int> joined_fds;           // Connected client sockets (bidirectional channels)
bool barrier_ready = false;            // Whether all processes have joined
int joined_count = 0;                  // Counter for joined processes (protected by join_mutex)


void process_join_req(int sock, const dsm_header_t &head) {
    // Extract source node ID from header
    uint16_t src_node = ntohs(head.src_node_id);
    
    std::cout << "[DSM Daemon] Received JOIN_REQ: NodeId=" << src_node << std::endl;

    // Acquire the join mutex to protect shared state
    join_mutex.lock();
    
    // Check if this fd is already in the joined list
    bool already_joined = false;
    for (int fd : joined_fds) {
        if (fd == sock) {
            already_joined = true;
            break;
        }
    }
    
    // Add to joined_fds if not already present
    if (!already_joined) {
        joined_fds.push_back(sock);
    }
    
    joined_count++;
    
    std::cout << "[DSM Daemon] Currently connected: socket number " << sock << std::endl;

    // Check if all processes have joined
    if (joined_count < ProcNum) {
        join_mutex.unlock();
        return;  // Keep connection open and continue processing
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
}

void process_lock_acq(int sock, const dsm_header_t &head, rio_t &rp) {
    /*pseudo code:
    //请你查看以下代码是否按照以下原则进行：
    1.对大表的增删查改必须获取全局锁
    2.一旦查到某个数据存在,想要读/修改某个值,必须释放全局锁,获取局部锁
    所以锁的调用链是这样:
    获取全局锁-查表某个原子是否存在-释放全局锁-获取局部锁-等待对应进程再发送消息(释放锁)并更新-释放局部锁
    */



    // Read the payload to get lock_id
    uint32_t payload_len = ntohl(head.payload_len);
    if (payload_len < sizeof(payload_lock_req_t)) {
        std::cerr << "[DSM Daemon] Invalid LOCK_ACQ payload length" << std::endl;
        return;
    }

    payload_lock_req_t req_payload;
    if (rio_readn(&rp, &req_payload, sizeof(req_payload)) != sizeof(req_payload)) {
        std::cerr << "[DSM Daemon] Failed to read LOCK_ACQ payload" << std::endl;
        return;
    }

    uint32_t lock_id = ntohl(req_payload.lock_id);
    uint16_t requester_id = ntohs(head.src_node_id);
    uint32_t seq_num = ntohl(head.seq_num);

    std::cout << "[DSM Daemon] Received LOCK_ACQ for lock " << lock_id 
              << " from NodeId=" << requester_id << std::endl;

    // Prepare or create lock record under table mutex
    LockTable->GlobalMutexLock();
    LockRecord* record = LockTable->Find(lock_id);
    if (record == nullptr) {
        LockRecord new_record;
        LockTable->Insert(lock_id, new_record);
        record = LockTable->Find(lock_id);
    }
    LockTable->GlobalMutexUnlock();

    // Block until we acquire the pthread mutex for this lock
    if (!LockTable->LocalMutexLock(lock_id)) {
        std::cerr << "[DSM Daemon] LocalMutexLock failed for lock " << lock_id << std::endl;
        return;
    }

    
    // Prepare invalid page list from the record
    uint32_t invalid_count = record->invalid_page_list.size();
    uint32_t payload_len_rep = sizeof(uint32_t) + invalid_count * sizeof(uint32_t);

    std::cout << "[DSM Daemon] Granting lock " << lock_id << " to NodeId=" << requester_id 
              << " with " << invalid_count << " invalid pages" << std::endl;

    // Copy invalid page list before releasing table lock
    std::vector<int> invalid_pages = record->invalid_page_list;

    // Send LOCK_REP with unused=1 to indicate lock is granted
    dsm_header_t rep_header = {
        DSM_MSG_LOCK_REP,
        1,  // unused=1: lock is granted
        htons(PodId),
        htonl(seq_num),
        htonl(payload_len_rep)
    };

    if (::send(sock, &rep_header, sizeof(rep_header), 0) != sizeof(rep_header)) {
        std::cerr << "[DSM Daemon] Failed to send LOCK_REP header" << std::endl;
        // Unlock the mutex before returning
        LockTable->LocalMutexUnlock(lock_id);
        return;
    }

    // Send invalid_set_count
    uint32_t invalid_count_net = htonl(invalid_count);
    if (::send(sock, &invalid_count_net, sizeof(invalid_count_net), 0) != sizeof(invalid_count_net)) {
        std::cerr << "[DSM Daemon] Failed to send invalid_set_count" << std::endl;
        // Unlock the mutex before returning
        LockTable->LocalMutexUnlock(lock_id);
        return;
    }

    // Send invalid page list
    for (int page_idx : invalid_pages) {
        uint32_t page_idx_net = htonl(page_idx);
        if (::send(sock, &page_idx_net, sizeof(page_idx_net), 0) != sizeof(page_idx_net)) {
            std::cerr << "[DSM Daemon] Failed to send invalid page index" << std::endl;
            // Unlock the mutex before returning
            LockTable->LocalMutexUnlock(lock_id);
            return;
        }
    }

    std::cout << "[DSM Daemon] Lock " << lock_id << " granted and held by NodeId=" << requester_id << std::endl;
}

void process_lock_rls(int sock, const dsm_header_t &head, rio_t &rp) {
    // Read the release payload
    uint32_t payload_len = ntohl(head.payload_len);
    if (payload_len < sizeof(payload_lock_rls_t)) {
        std::cerr << "[DSM Daemon] Invalid LOCK_RLS payload length" << std::endl;
        return;
    }

    // Read the payload structure (invalid_set_count and lock_id)
    payload_lock_rls_t rls_payload;
    if (rio_readn(&rp, &rls_payload, sizeof(rls_payload)) != sizeof(rls_payload)) {
        std::cerr << "[DSM Daemon] Failed to read LOCK_RLS payload" << std::endl;
        return;
    }
    
    uint32_t rls_invalid_count = ntohl(rls_payload.invalid_set_count);
    uint32_t lock_id = ntohl(rls_payload.lock_id);
    uint16_t src_node = ntohs(head.src_node_id);
    uint32_t seq_num = ntohl(head.seq_num);
    
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

    
        
    LockRecord* record = LockTable->Find(lock_id);
    if (record != nullptr) {
        // Update invalid page list
        record->invalid_page_list = new_invalid_pages;
        record->invalid_set_count = rls_invalid_count;
        
        std::cout << "[DSM Daemon] Released lock " << lock_id << std::endl;
    }
    
    // 3. Release the local lock (acquired in handle_lock_acquire)
    LockTable->LocalMutexUnlock(lock_id);
    

    // Send ACK for the release
    dsm_header_t ack = {
        DSM_MSG_ACK,
        0,
        htons(PodId),
        htonl(seq_num),
        0
    };
    
    if (::send(sock, &ack, sizeof(ack), 0) != sizeof(ack)) {
        std::cerr << "[DSM Daemon] Failed to send ACK for LOCK_RLS" << std::endl;
        return;
    }

    std::cout << "[DSM Daemon] Sent ACK for LOCK_RLS" << std::endl;
}

static bool handle_unknown_message(int connfd, const dsm_header_t &header) {
    std::cerr << "[DSM Daemon] Received unknown message type: 0x"
              << std::hex << static_cast<int>(header.type) << std::dec << std::endl;
    close(connfd);
    return false;
}

void process_page_req(int sock, const dsm_header_t &head, rio_t &rp) {
    // Read payload to get VPN
    uint32_t payload_len = ntohl(head.payload_len);
    if (payload_len < sizeof(payload_page_req_t)) {
        std::cerr << "[DSM Daemon] Invalid PAGE_REQ payload length" << std::endl;
        return;
    }
    
    payload_page_req_t req_payload;
    if (rio_readn(&rp, &req_payload, sizeof(req_payload)) != sizeof(req_payload)) {
        std::cerr << "[DSM Daemon] Failed to read PAGE_REQ payload" << std::endl;
        return;
    }
    
    uint32_t VPN = ntohl(req_payload.page_index);
    uint16_t requester_id = ntohs(head.src_node_id);
    uint32_t seq_num = ntohl(head.seq_num);
    
    std::cout << "[DSM Daemon] Received PAGE_REQ for page " << VPN 
              << " from NodeId=" << requester_id << std::endl;
    
    // Follow the locking principle:
    // 1. Acquire global lock to access the table
    PageTable->GlobalMutexLock();
    PageRecord* record = PageTable->Find(VPN);
    if (record == nullptr) {
        std::cerr << "[DSM Daemon] Error: page " << VPN << " beyond shared space" << std::endl;
        PageTable->GlobalMutexUnlock();
        return;
    }
    int owner_id = record->owner_id;
    // 2. Release global lock
   

    // 3. Acquire local lock to ensure sequential access
    if (!PageTable->LocalMutexLock(VPN)) {
        std::cerr << "[DSM Daemon] Failed to lock page " << VPN << std::endl;
        return;
    }
    PageTable->GlobalMutexUnlock();
    
    
    // Case 1: We are the real owner (owner_id == PodId)
    if (owner_id == PodId) {
        std::cout << "[DSM Daemon] We are the owner of page " << VPN << ", sending page data" << std::endl;
        
        // Prepare a buffer for page data
        char page_buffer[PAGESIZE];
        std::memset(page_buffer, 0, PAGESIZE);
        uintptr_t page_add = static_cast<uintptr_t>(VPN) << 12;
        std::memcpy(page_buffer, reinterpret_cast<void*>(page_add), PAGESIZE);
        
        // Send page data
        dsm_header_t rep_header = {
            DSM_MSG_PAGE_REP,
            1,  // unused=1: we have the page data
            htons(PodId),
            htonl(seq_num),
            htonl(sizeof(uint16_t) + DSM_PAGE_SIZE)
        };
        
        if (::send(sock, &rep_header, sizeof(rep_header), 0) != sizeof(rep_header)) {
            std::cerr << "[DSM Daemon] Failed to send PAGE_REP header" << std::endl;
            PageTable->LocalMutexUnlock(VPN);
            return;
        }
        
        // Send real_owner_id (ourselves)
        uint16_t real_owner_net = htons(PodId);
        if (::send(sock, &real_owner_net, sizeof(real_owner_net), 0) != sizeof(real_owner_net)) {
            std::cerr << "[DSM Daemon] Failed to send real_owner_id" << std::endl;
            PageTable->LocalMutexUnlock(VPN);
            return;
        }
        
        // Send page data from buffer
        if (::send(sock, page_buffer, DSM_PAGE_SIZE, 0) != DSM_PAGE_SIZE) {
            std::cerr << "[DSM Daemon] Failed to send page data" << std::endl;
            PageTable->LocalMutexUnlock(VPN);
            return;
        }
        
        PageTable->LocalMutexUnlock(VPN);
        return;
    }
    // Case 2: First access and we are not Pod 0 (owner_id == -1 && PodId != 0)
    else if (owner_id == -1 && PodId != 0) {
        std::cout << "[DSM Daemon] First access, requesting page from Pod 0" << std::endl;
        
        // Forward request to Pod 0
        std::string pod0_ip = GetPodIp(0);
        int pod0_port = GetPodPort(0);
        
        int pod0_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (pod0_sock < 0) {
            std::cerr << "[DSM Daemon] Failed to create socket for Pod 0" << std::endl;
            PageTable->LocalMutexUnlock(VPN);
            return;
        }
        
        struct sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(pod0_port);
        inet_pton(AF_INET, pod0_ip.c_str(), &server_addr.sin_addr);
        
        if (connect(pod0_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "[DSM Daemon] Failed to connect to Pod 0" << std::endl;
            close(pod0_sock);
            PageTable->LocalMutexUnlock(VPN);
            return;
        }
        
        // Send PAGE_REQ to Pod 0
        dsm_header_t fwd_header = {
            DSM_MSG_PAGE_REQ,
            0,
            htons(PodId),
            htonl(1),
            htonl(sizeof(payload_page_req_t))
        };
        
        ::send(pod0_sock, &fwd_header, sizeof(fwd_header), 0);
        ::send(pod0_sock, &req_payload, sizeof(req_payload), 0);
        
        // Receive response from Pod 0
        rio_t pod0_rio;
        rio_readinit(&pod0_rio, pod0_sock);
        
        dsm_header_t pod0_rep;
        if (rio_readn(&pod0_rio, &pod0_rep, sizeof(pod0_rep)) != sizeof(pod0_rep)) {
            std::cerr << "[DSM Daemon] Failed to receive response from Pod 0" << std::endl;
            close(pod0_sock);
            PageTable->LocalMutexUnlock(VPN);
            return;
        }
        
        // Read page data from Pod 0
        uint16_t real_owner_id;
        rio_readn(&pod0_rio, &real_owner_id, sizeof(real_owner_id));
        
        char page_buffer[DSM_PAGE_SIZE];
        if (rio_readn(&pod0_rio, page_buffer, DSM_PAGE_SIZE) != DSM_PAGE_SIZE) {
            std::cerr << "[DSM Daemon] Failed to read page data from Pod 0" << std::endl;
            close(pod0_sock);
            PageTable->LocalMutexUnlock(VPN);
            return;
        }
        
        close(pod0_sock);
        
        // Forward page data to requester
        dsm_header_t rep_header = {
            DSM_MSG_PAGE_REP,
            1,  // unused=1: we have the page data
            htons(PodId),
            htonl(seq_num),
            htonl(sizeof(uint16_t) + DSM_PAGE_SIZE)
        };
        
        ::send(sock, &rep_header, sizeof(rep_header), 0);
        ::send(sock, &real_owner_id, sizeof(real_owner_id), 0);
        ::send(sock, page_buffer, DSM_PAGE_SIZE, 0);
        
        PageTable->LocalMutexUnlock(VPN);
        return;
    }
    // Case 3: First access and we are Pod 0 (owner_id == -1 && PodId == 0)
    else if (owner_id == -1 && PodId == 0) {
        std::cout << "[DSM Daemon] First access on Pod 0, loading from file" << std::endl;
        
        // Prepare page buffer
        char page_buffer[DSM_PAGE_SIZE];
        std::memset(page_buffer, 0, DSM_PAGE_SIZE);
        
        // Try to read from file if this page is bound to a file
        PageTable->GlobalMutexLock();
        PageRecord* rec = PageTable->Find(VPN);
        if (rec != nullptr && rec->fd >= 0 && !rec->filepath.empty()) {
            // This page is bound to a file, read data from file
            // rec->offset is the page number, need to multiply by page size
            off_t file_offset = static_cast<off_t>(rec->offset) * DSM_PAGE_SIZE;
            std::cout << "[DSM Daemon] Reading from file: " << rec->filepath 
                     << " at page " << rec->offset << " (byte offset " << file_offset << ")" << std::endl;
            
            if (lseek(rec->fd, file_offset, SEEK_SET) >= 0) {
                ssize_t bytes_read = read(rec->fd, page_buffer, DSM_PAGE_SIZE);
                if (bytes_read < 0) {
                    std::cerr << "[DSM Daemon] Failed to read file data for page " << VPN << std::endl;
                } else {
                    std::cout << "[DSM Daemon] Successfully read " << bytes_read 
                             << " bytes from file" << std::endl;
                }
                // If less than page size, rest is already zero-filled
            } else {
                std::cerr << "[DSM Daemon] Failed to seek in file for page " << VPN << std::endl;
            }
            
            // Update owner to Pod 0 after loading
            rec->owner_id = 0;
        } else {
            std::cout << "[DSM Daemon] Page not bound to file, sending zero-filled page" << std::endl;
        }
        PageTable->GlobalMutexUnlock();
        
        // Send page data
        dsm_header_t rep_header = {
            DSM_MSG_PAGE_REP,
            1,  // unused=1: we have the page data
            htons(PodId),
            htonl(seq_num),
            htonl(sizeof(uint16_t) + DSM_PAGE_SIZE)
        };
        
        ::send(sock, &rep_header, sizeof(rep_header), 0);
        
        uint16_t real_owner_net = htons(0);
        ::send(sock, &real_owner_net, sizeof(real_owner_net), 0);
        ::send(sock, page_buffer, DSM_PAGE_SIZE, 0);
        
        PageTable->LocalMutexUnlock(VPN);
        return;
    }
    // Case 4: We are not the owner (owner_id != PodId and owner_id != -1)
    else {
        std::cout << "[DSM Daemon] We are not the owner, redirecting to NodeId=" << owner_id << std::endl;
        
        // Return real owner ID to requester
        dsm_header_t rep_header = {
            DSM_MSG_PAGE_REP,
            0,  // unused=0: redirect to real owner
            htons(PodId),
            htonl(seq_num),
            htonl(sizeof(uint16_t))  // Only sending real_owner_id, no page data
        };
        
        if (::send(sock, &rep_header, sizeof(rep_header), 0) != sizeof(rep_header)) {
            std::cerr << "[DSM Daemon] Failed to send PAGE_REP header (redirect)" << std::endl;
            PageTable->LocalMutexUnlock(VPN);
            return;
        }
        
        // Send real_owner_id
        uint16_t real_owner_net = htons(owner_id);
        if (::send(sock, &real_owner_net, sizeof(real_owner_net), 0) != sizeof(real_owner_net)) {
            std::cerr << "[DSM Daemon] Failed to send real_owner_id (redirect)" << std::endl;
            PageTable->LocalMutexUnlock(VPN);
            return;
        }
        
        PageTable->LocalMutexUnlock(VPN);
        return;
    }
}

void process_owner_update(int sock, const dsm_header_t &head, rio_t &rp) {
    // Read payload to get VPN and new_owner_id
    uint32_t payload_len = ntohl(head.payload_len);
    if (payload_len < sizeof(payload_owner_update_t)) {
        std::cerr << "[DSM Daemon] Invalid OWNER_UPDATE payload length" << std::endl;
        return;
    }
    
    payload_owner_update_t update_payload;
    if (rio_readn(&rp, &update_payload, sizeof(update_payload)) != sizeof(update_payload)) {
        std::cerr << "[DSM Daemon] Failed to read OWNER_UPDATE payload" << std::endl;
        return;
    }
    
    uint32_t VPN = ntohl(update_payload.resource_id);
    uint16_t new_owner = ntohs(update_payload.new_owner_id);
    uint32_t seq_num = ntohl(head.seq_num);
    
    std::cout << "[DSM Daemon] Received OWNER_UPDATE for page " << VPN 
              << ", new owner: NodeId=" << new_owner << std::endl;
    
    // Lock the page for exclusive access
    if (!PageTable->LocalMutexLock(VPN)) {
        std::cerr << "[DSM Daemon] Failed to lock page " << VPN << std::endl;
        return;
    }
    
    // Update page table with new owner
    PageTable->GlobalMutexLock();
    
    PageRecord* record = PageTable->Find(VPN);
    if (record == nullptr) {
        // Create new record
        PageRecord new_record;
        new_record.owner_id = new_owner;
        PageTable->Insert(VPN, new_record);
    } else {
        // Update existing record
        record->owner_id = new_owner;
    }
    
    PageTable->GlobalMutexUnlock();
    
    // Unlock the page
    PageTable->LocalMutexUnlock(VPN);
    
    std::cout << "[DSM Daemon] Updated owner of page " << VPN << " to NodeId=" << new_owner << std::endl;
    
    // Send ACK back to sender
    dsm_header_t ack = {
        DSM_MSG_ACK,
        0,
        htons(PodId),
        htonl(seq_num),
        0
    };
    
    if (::send(sock, &ack, sizeof(ack), 0) != sizeof(ack)) {
        std::cerr << "[DSM Daemon] Failed to send ACK for OWNER_UPDATE" << std::endl;
        return;
    }
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
                process_join_req(connfd, header);
                break;
            case DSM_MSG_LOCK_ACQ:
                process_lock_acq(connfd, header, rp);
                break;
            case DSM_MSG_LOCK_RLS:
                process_lock_rls(connfd, header, rp);
                break;
            case DSM_MSG_OWNER_UPDATE:
                process_owner_update(connfd, header, rp);
                break;
            case DSM_MSG_PAGE_REQ:
                process_page_req(connfd, header, rp);
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
