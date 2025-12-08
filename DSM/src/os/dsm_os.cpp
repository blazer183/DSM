#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <type_traits>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <vector>
#include <sstream>

#include "dsm.h"
#include "net/protocol.h"
#include "os/bind_table.h"
#include "os/lock_table.h"
#include "os/page_table.h"
#include "os/socket_table.h"
#include "os/pfhandler.h"

#ifdef UNITEST
#define STATIC 
#else
#define STATIC static
#endif

extern void dsm_start_daemon(int port);

struct PageTable *PageTable = nullptr;
struct LockTable *LockTable = nullptr;
struct BindTable *BindTable = nullptr;
struct SocketTable *SocketTable = nullptr;

size_t SharedPages = 0;
int PodId = -1;
void *SharedAddrBase = nullptr;
int ProcNum = 0;
int WorkerNodeNum = 0;
std::vector<std::string> WorkerNodeIps;  // worker IP list
int* InvalidPages = nullptr;            //0: valid, 1: invalid
int* ValidPages = nullptr;              //0: not valid, 1: valid
int* DirtyPages = nullptr;              //0: clean, 1: dirty

STATIC std::string LeaderNodeIp;
STATIC int LeaderNodePort = 0;
STATIC int LeaderNodeSocket = -1;
STATIC int LockNum = 0;
STATIC void * SharedAddrCurrentLoc = nullptr;   // current location in shared memory


std::string GetPodIp(int pod_id) {
    if (pod_id == 0) {
        // Leader Pod
        return LeaderNodeIp;
    } else if (pod_id == PodId) {
        return "127.0.0.1";
    }else if (pod_id > 0 && WorkerNodeNum > 0) {
        // Worker Pod: pod_id % WorkerNodeNum
        int worker_index = pod_id % WorkerNodeNum;
        if (worker_index < static_cast<int>(WorkerNodeIps.size())) {
            return WorkerNodeIps[worker_index];
        }
    }
    std::cerr << "[DSM Warning] Invalid PodID " << pod_id << " or empty Worker IP list" << std::endl;
    return "";
}

int GetPodPort(int pod_id) {
    if (pod_id == 0) {
        return LeaderNodePort;
    } else if (pod_id > 0) {
        return LeaderNodePort + pod_id;
    }
    std::cerr << "[DSM Warning] Invalid PodID " << pod_id << std::endl;
    return -1;
}

// Helper function to get or create a socket connection to a remote pod
// Returns existing socket if already connected, creates new connection otherwise
int getsocket(const std::string& ip, int port) {
    if (SocketTable == nullptr) {
        std::cerr << "[getsocket] SocketTable not initialized" << std::endl;
        return -1;
    }

    // Check if socket already exists for this target
    int target_node = -1;
    for (int i = 0; i < ProcNum; i++) {
        if (GetPodIp(i) == ip && GetPodPort(i) == port) {
            target_node = i;
            break;
        }
    }

    if (target_node >= 0) {
        SocketTable->LockAcquire();
        SocketRecord* record = SocketTable->Find(target_node);
        if (record != nullptr && record->socket >= 0) {
            int existing_sock = record->socket;
            SocketTable->LockRelease();
            return existing_sock;
        }
        SocketTable->LockRelease();
    }

    // Create new socket connection
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "[getsocket] Failed to create socket: " << std::strerror(errno) << std::endl;
        return -1;
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "[getsocket] Invalid address: " << ip << std::endl;
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[getsocket] Failed to connect to " << ip << ":" << port 
                  << " - " << std::strerror(errno) << std::endl;
        close(sockfd);
        return -1;
    }

    // Store socket in SocketTable if we identified the target node
    if (target_node >= 0) {
        SocketTable->LockAcquire();
        SocketRecord new_record;
        new_record.socket = sockfd;
        new_record.next_seq = 1;
        SocketTable->Insert(target_node, new_record);
        SocketTable->LockRelease();
    }

    return sockfd;
}

template<typename T>
bool GetEnvVar(const char* name, T& value, const T& default_val, bool required = true) {
    const char* env_val = std::getenv(name);
    if (env_val != nullptr) {
        if constexpr (std::is_same_v<T, int>) {
            value = std::atoi(env_val);
        } else if constexpr (std::is_same_v<T, std::string>) {
            value = env_val;
        }
        std::cout << "[DSM Info] " << name << ": " << value << std::endl;
        return true;
    } else {
        if (required) {
            std::cerr << "[DSM Warning] " << name << " not set! exit!" << std::endl;
            return false;
        } else {
            value = default_val;
            std::cerr << "[DSM Warning] " << name << " not set! Using default: " << default_val << std::endl;
            return true;
        }
    }
}

bool LaunchListenerThread(int Port)
{
    try {
        std::thread listener([Port]() {
            dsm_start_daemon(Port);
        });
        listener.detach();
        sleep(1);
        return true;
    } catch (const std::system_error &err) {
        std::cerr << "[dsm] failed to launch listener thread: " << err.what() << std::endl;
        return false;
    }
}

bool FetchGlobalData(int dsm_memsize)    
{
    SharedPages = static_cast<size_t>(dsm_memsize) / DSM_PAGE_SIZE;
    SharedAddrBase = reinterpret_cast<void *>(0x4000000000ULL); 
    SharedAddrCurrentLoc = SharedAddrBase;
    if (!GetEnvVar("DSM_LEADER_IP", LeaderNodeIp, std::string(""), true)) exit(1);
    if (!GetEnvVar("DSM_LEADER_PORT", LeaderNodePort, 0, true)) exit(1);
    if (!GetEnvVar("DSM_TOTAL_PROCESSES", ProcNum, 1, false)) exit(1);
    if (!GetEnvVar("DSM_POD_ID", PodId, -1, false)) exit(1);
    if (!GetEnvVar("DSM_WORKER_COUNT", WorkerNodeNum, 0, false)) exit(1);
    std::string worker_ips_str;
    if (!GetEnvVar("DSM_WORKER_IPS", worker_ips_str, std::string(""), false)) exit(1);
    WorkerNodeIps.clear();
    if (!worker_ips_str.empty()) {
        std::stringstream ss(worker_ips_str);
        std::string ip;
        while (std::getline(ss, ip, ',')) {
            WorkerNodeIps.push_back(ip);
        }
        std::cout << "[DSM Info] Parsed " << WorkerNodeIps.size() << " worker IPs" << std::endl;
    }
    const bool ok = (PodId >= 0) && (SharedAddrBase != nullptr) && (SharedPages > 0);
    if (!ok) {
        std::cerr << "[dsm] invalid shared region parameters (PodID=" << PodId
                  << ", base=" << SharedAddrBase << ", pages=" << SharedPages << ")" << std::endl;
        return false;
    }
    return true;
}

bool InitDataStructs(int dsm_memsize)
{   
    // Initialize shared memory region
    if (SharedAddrBase != nullptr && SharedPages != 0){
        const size_t total_size = (size_t)dsm_memsize;
        void* mapped_addr = ::mmap(
            SharedAddrBase,                    // Desired start address
            total_size,                        // Size of the mapping
            PROT_NONE,                        // Initial protection: no access, to trigger page faults
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,  // Private anonymous mapping, fixed address
            -1,                               // No file descriptor
            0                                 // Offset 0
        );
        if (mapped_addr == MAP_FAILED) {
            std::cerr << "[dsm] mmap failed at address " << SharedAddrBase 
                    << " size " << total_size << ": " << std::strerror(errno) << std::endl;
            return false;
        }
        ValidPages = new int[SharedPages];
        std::memset(ValidPages, 0, sizeof(int) * SharedPages);
        DirtyPages = new int[SharedPages];
        std::memset(DirtyPages, 0, sizeof(int) * SharedPages);
        InvalidPages = new int[SharedPages];
        std::memset(InvalidPages, 0, sizeof(int) * SharedPages);
        install_handler(SharedAddrBase, SharedPages);
    }else {
        std::cerr << "[dsm] invalid shared region parameters (base=" << SharedAddrBase 
                  << ", pages=" << SharedPages << ")" << std::endl;
        return false;
    }


    // Initialize page, lock, bind, and socket tables
    if (PageTable == nullptr)
        PageTable = new (::std::nothrow) class PageTable();
    if (LockTable == nullptr)
        LockTable = new (::std::nothrow) class LockTable();
    if (BindTable == nullptr)
        BindTable = new (::std::nothrow) class BindTable();
    if (SocketTable == nullptr)
        SocketTable = new (::std::nothrow) class SocketTable();
    const bool ok = (PageTable != nullptr) && (LockTable != nullptr)
                    && (BindTable != nullptr) && (SocketTable != nullptr);
    if (!ok){
        std::cerr << "[dsm] failed to allocate metadata tables" << std::endl;
        return false;
    }
    return true;
}

bool dsm_barrier()
{
    std::string target_ip = LeaderNodeIp;

    // Connect to leader node for synchronization
    int leader_sock = getsocket(LeaderNodeIp, LeaderNodePort);
    if (leader_sock < 0) {
        std::cerr << "[dsm_barrier] failed to connect to leader at "
                  << LeaderNodeIp << ":" << LeaderNodePort << std::endl;
        return false;
    }
    dsm_header_t req = {
        DSM_MSG_JOIN_REQ,                    
        0,                       // unused
    htons(PodId),            // src_node_id: source pod ID
        htonl(1),                // seq_num: 1 
        0                       // payload length
    };
    ::send(leader_sock, &req, sizeof(req), 0);

    // Wait for acknowledgment from leader node
    rio_t rio;
    rio_readinit(&rio, leader_sock);
    
    dsm_header_t ack_header;
    rio_readn(&rio, &ack_header, sizeof(ack_header));
    
    return true;
}

int dsm_init(int dsm_memsize)       
{
    if (!FetchGlobalData(dsm_memsize))
        return -1;
    if(!LaunchListenerThread(LeaderNodePort+PodId))
        return -2;
    if (!InitDataStructs(dsm_memsize))
        return -3;
    return 0;
}

int dsm_getpodid(void){
    return PodId;
}

int dsm_finalize(void){
    return 1;
}

int dsm_mutex_init(){
    if (LockTable == nullptr)
        return -1;

    LockTable->LockAcquire();
    //检查是否有该锁，因为有可能别的进程先进行到这一步，然后请求锁，把锁请求走了。
    //监听线程行为是只要有锁请求就先创建锁再授予，所以有可能已经有锁了，但是监听线程创建的
    LockTable->Insert(++LockNum, LockRecord());
    LockTable->LockRelease();
    return LockNum;
}

int dsm_mutex_destroy(int *mutex){
    if (LockTable == nullptr || mutex == nullptr)
        return -1;

    LockTable->LockAcquire();
    LockTable->Remove(*mutex);
    LockTable->LockRelease();
    return 0;
}

int dsm_mutex_lock(int *mutex){
    if (mutex == nullptr || SocketTable == nullptr || LockTable == nullptr)
        return -1;

    // Use mprotect to set all shared pages as invalid (PROT_NONE)
    // This ensures that any access to shared memory will trigger a page fault
    if (SharedAddrBase != nullptr && SharedPages > 0) {
        if (mprotect(SharedAddrBase, SharedPages * DSM_PAGE_SIZE, PROT_NONE) == -1) {
            std::cerr << "[dsm_mutex_lock] mprotect failed: " << std::strerror(errno) << std::endl;
            // Continue anyway, as this is not critical for basic functionality
        }
    }

    const int lockid = *mutex;
    int lockprobowner = lockid % ProcNum;
    // Get or create socket connection to the probable owner
    std::string target_ip = GetPodIp(lockprobowner);
    int target_port = GetPodPort(lockprobowner);
    
    int sock = getsocket(target_ip, target_port);
    if (sock < 0) {
        std::cerr << "[dsm_mutex_lock] Failed to connect to lock manager at " 
                  << target_ip << ":" << target_port << std::endl;
        return -1;
    }

    // Get sequence number for this connection
    uint32_t seq_num = 1;
    SocketTable->LockAcquire();
    SocketRecord* record = SocketTable->Find(lockprobowner);
    if (record != nullptr) {
        seq_num = record->allocate_seq();
    }
    SocketTable->LockRelease();

    // Build and send LOCK_ACQ message
    dsm_header_t req_header = {
        DSM_MSG_LOCK_ACQ,
        0,                          // unused
        htons(PodId),              // src_node_id
        htonl(seq_num),            // seq_num
        htonl(sizeof(payload_lock_req_t))  // payload_len
    };

    payload_lock_req_t req_payload = {
        htonl(lockid)              // lock_id
    };

    // Send header and payload
    if (::send(sock, &req_header, sizeof(req_header), 0) != sizeof(req_header)) {
        std::cerr << "[dsm_mutex_lock] Failed to send request header" << std::endl;
        return -1;
    }
    
    if (::send(sock, &req_payload, sizeof(req_payload), 0) != sizeof(req_payload)) {
        std::cerr << "[dsm_mutex_lock] Failed to send request payload" << std::endl;
        return -1;
    }

    // Receive response
    rio_t rio;
    rio_readinit(&rio, sock);

    dsm_header_t rep_header;
    if (rio_readn(&rio, &rep_header, sizeof(rep_header)) != sizeof(rep_header)) {
        std::cerr << "[dsm_mutex_lock] Failed to receive response header" << std::endl;
        return -1;
    }

    // Check if this is a LOCK_REP or if we need to wait/retry
    if (rep_header.type == DSM_MSG_LOCK_REP ) {
        // Lock acquired successfully, read payload
        uint32_t payload_len = ntohl(rep_header.payload_len);
        
        if (payload_len >= sizeof(uint32_t)) {
            payload_lock_rep_t rep_payload;
            if (rio_readn(&rio, &rep_payload, sizeof(uint32_t)) != sizeof(uint32_t)) {
                std::cerr << "[dsm_mutex_lock] Failed to read invalid_set_count" << std::endl;
                return -1;
            }
            
            uint32_t invalid_count = ntohl(rep_payload.invalid_set_count);
            
            // Read invalid page list if any
            if (invalid_count > 0 && InvalidPages != nullptr) {
                std::vector<uint32_t> invalid_pages(invalid_count);
                size_t bytes_to_read = invalid_count * sizeof(uint32_t);
                if (rio_readn(&rio, invalid_pages.data(), bytes_to_read) != (ssize_t)bytes_to_read) {
                    std::cerr << "[dsm_mutex_lock] Failed to read invalid page list" << std::endl;
                    return -1;
                }
                
                // Mark pages as invalid
                for (uint32_t i = 0; i < invalid_count; i++) {
                    uint32_t page_idx = ntohl(invalid_pages[i]);
                    if (page_idx < (uint32_t)SharedPages) {
                        InvalidPages[page_idx] = 1;
                    }
                }
            }
        }
        
        return 0;  // Success
    } 

    std::cerr << "[dsm_mutex_lock] Unexpected response type: " << (int)rep_header.type << std::endl;
    return -1;
}    

int dsm_mutex_unlock(int *mutex){   
    if (mutex == nullptr || SocketTable == nullptr || LockTable == nullptr)
        return -1;

    const int lockid = *mutex;
    int lockprobowner = lockid % ProcNum;

    // Get socket connection to the probable owner
    std::string target_ip = GetPodIp(lockprobowner);
    int target_port = GetPodPort(lockprobowner);
    
    int sock = getsocket(target_ip, target_port);
    if (sock < 0) {
        std::cerr << "[dsm_mutex_unlock] Failed to connect to lock manager" << std::endl;
        return -1;
    }

    // Get sequence number
    uint32_t seq_num = 1;
    SocketTable->LockAcquire();
    SocketRecord* record = SocketTable->Find(lockprobowner);
    if (record != nullptr) {
        seq_num = record->allocate_seq();
    }
    SocketTable->LockRelease();

    // Collect invalid pages from InvalidPages array
    std::vector<uint32_t> invalid_pages;
    if (InvalidPages != nullptr) {
        for (size_t i = 0; i < SharedPages; i++) {
            if (InvalidPages[i] == 1) {
                invalid_pages.push_back((uint32_t)i);  // Explicit cast
                InvalidPages[i] = 0;  // Reset after collecting
            }
        }
    }

    uint32_t invalid_count = invalid_pages.size();
    uint32_t payload_len = sizeof(uint32_t) + invalid_count * sizeof(uint32_t);

    // Build and send LOCK_RLS message
    dsm_header_t req_header = {
        DSM_MSG_LOCK_RLS,
        0,                          // unused
        htons(PodId),              // src_node_id
        htonl(seq_num),            // seq_num
        htonl(payload_len)         // payload_len
    };

    // Send header
    if (::send(sock, &req_header, sizeof(req_header), 0) != sizeof(req_header)) {
        std::cerr << "[dsm_mutex_unlock] Failed to send release header" << std::endl;
        return -1;
    }

    // Send invalid_set_count
    uint32_t invalid_count_net = htonl(invalid_count);
    if (::send(sock, &invalid_count_net, sizeof(invalid_count_net), 0) != sizeof(invalid_count_net)) {
        std::cerr << "[dsm_mutex_unlock] Failed to send invalid_set_count" << std::endl;
        return -1;
    }

    // Send invalid page list
    if (invalid_count > 0) {
        for (uint32_t page_idx : invalid_pages) {
            uint32_t page_idx_net = htonl(page_idx);
            if (::send(sock, &page_idx_net, sizeof(page_idx_net), 0) != sizeof(page_idx_net)) {
                std::cerr << "[dsm_mutex_unlock] Failed to send invalid page index" << std::endl;
                return -1;
            }
        }
    }

    // Wait for ACK
    rio_t rio;
    rio_readinit(&rio, sock);

    dsm_header_t ack_header;
    if (rio_readn(&rio, &ack_header, sizeof(ack_header)) != sizeof(ack_header)) {
        std::cerr << "[dsm_mutex_unlock] Failed to receive ACK" << std::endl;
        return -1;
    }

    if (ack_header.type != DSM_MSG_ACK) {
        std::cerr << "[dsm_mutex_unlock] Unexpected response type: " << (int)ack_header.type << std::endl;
        return -1;
    }

    return 0;
}

void dsm_bind(void *addr, const char *name, size_t element_size){
    if (BindTable == nullptr || addr == nullptr || name == nullptr)
        return;

    BindTable->LockAcquire();
    BindTable->Insert(addr, BindRecord{addr, element_size, std::string(name)});
    BindTable->LockRelease();
}

void *dsm_malloc(size_t size){
    if ((char*)SharedAddrCurrentLoc + size > (char*)SharedAddrBase + (SharedPages * DSM_PAGE_SIZE)) {
        return nullptr;
    }

    void* allocated_ptr = SharedAddrCurrentLoc;

    SharedAddrCurrentLoc = (char*)SharedAddrCurrentLoc + size;

    return allocated_ptr;
}
