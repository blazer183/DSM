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
#include <sys/stat.h>
#include <fcntl.h>
#include <vector>
#include <sstream>

#include "dsm.h"
#include "net/protocol.h"
#include "os/bind_table.h"
#include "os/lock_table.h"
#include "os/page_table.h"
#include "os/socket_table.h"
#include "os/pfhandler.h"

// 声明来自 dsm_os_cond.cpp 的辅助函数
extern int getsocket(const std::string& ip, int port);
extern bool LaunchListenerThread(int Port);
extern bool FetchGlobalData(int dsm_pagenum, std::string& LeaderNodeIp, int& LeaderNodePort);
extern bool InitDataStructs(int dsm_pagenum);

// 全局变量定义

struct PageTable *PageTable = nullptr;
struct LockTable *LockTable = nullptr;
struct BindTable *BindTable = nullptr;
struct SocketTable *SocketTable = nullptr;

size_t SharedPages = 0;
int PodId = -1;
void *SharedAddrBase = nullptr;
void * SharedAddrCurrentLoc = nullptr;   // 下一次分配空间（bind）的基址
int SAB_VPNumber = 0;           //共享区起始虚拟页号
int SAC_VPNumber = 0;           //共享区下一次分配的空间的虚拟页号



int ProcNum = 0;
int WorkerNodeNum = 0;
std::vector<std::string> WorkerNodeIps;  // worker IP list
int* InvalidPages = nullptr;            //0: valid, 1: invalid


std::string LeaderNodeIp;
int LeaderNodePort = 0;
int LeaderNodeSocket = -1;
int LockNum = 0;


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

bool dsm_barrier()
{
    if (SharedAddrBase != nullptr && SharedPages > 0) {
        size_t total_size = SharedPages * PAGESIZE;
        if (mprotect(SharedAddrBase, total_size, PROT_NONE) == -1) {
            std::cerr << "[dsm_mutex_lock] mprotect failed: " << std::strerror(errno) << std::endl;
        }else{
            std::cerr << "[dsm_mutex_lock] mprotect succeed: " << std::endl;
        }
    }

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

int dsm_init(int dsm_pagenum)       
{
    if (!FetchGlobalData(dsm_pagenum, LeaderNodeIp, LeaderNodePort))
        return -1;
    if (!LaunchListenerThread(LeaderNodePort+PodId))
        return -2;
    if (!InitDataStructs(dsm_pagenum))
        return -3;
    return 0;
}

int dsm_getpodid(void){
    return PodId;
}

int dsm_finalize(void){
    // Synchronize all processes before cleanup
    dsm_barrier();
    
    // Note: In a real implementation, we would kill all snooping threads here
    // For now, we just return success since threads are detached
    return 0;
}

int dsm_mutex_init(){
    // Initialize a new lock in the LockTable
    LockTable->GlobalMutexLock();
    LockNum++;
    
    // Check if this lock already exists (another process may have created it earlier)
    if (LockTable->Find(LockNum) == nullptr) {
        LockTable->Insert(LockNum, LockRecord());
    }
    LockTable->GlobalMutexUnlock();
    
    return LockNum;
}

int dsm_mutex_destroy(int *mutex){
    return 0;
}

int dsm_mutex_lock(int *mutex){

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
    SocketTable->GlobalMutexLock();
    SocketRecord* record = SocketTable->Find(lockprobowner);
    if (record != nullptr) {
        seq_num = record->allocate_seq();
    }
    SocketTable->GlobalMutexUnlock();

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
                
                // Mark pages as needing to be pulled (invalidated by previous owner)
                // InvalidPages[i] = 0 means we need to pull from remote
                for (uint32_t i = 0; i < invalid_count; i++) {
                    uint32_t page_idx = ntohl(invalid_pages[i]);
                    if (page_idx < (uint32_t)SharedPages) {
                        InvalidPages[page_idx] = 0;
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
    SocketTable->GlobalMutexLock();
    SocketRecord* record = SocketTable->Find(lockprobowner);
    if (record != nullptr) {
        seq_num = record->allocate_seq();
    }
    SocketTable->GlobalMutexUnlock();

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
    uint32_t payload_len = sizeof(payload_lock_rls_t) + invalid_count * sizeof(uint32_t);

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

    // Send payload structure (invalid_set_count and lock_id)
    payload_lock_rls_t rls_payload = {
        htonl(invalid_count),      // invalid_set_count
        htonl(lockid)              // lock_id
    };
    
    if (::send(sock, &rls_payload, sizeof(rls_payload), 0) != sizeof(rls_payload)) {
        std::cerr << "[dsm_mutex_unlock] Failed to send LOCK_RLS payload" << std::endl;
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

//集成dsm_bind与dsm_malloc,
//输入参数：文件路径，未获得的文件元素个数（比如数组大小）
//返回参数：数组的起始地址
//目前共享区里的共享数据不支持字符串，对于数字默认int类型，所以返回的元素个数也是sizeof(int)为最小单位
void* dsm_malloc(const char *name, int * num){
    // For non-leader processes, return the current allocation address
    // Note: In a distributed system, this assumes the allocation order is deterministic
    // and all processes call dsm_malloc in the same order with the same arguments
    if(PodId != 0) {
        // Non-Pod 0 processes: just return the current location and advance it
        // We need to calculate how much to advance based on file size
        // For now, advance by one page as a simple approximation
        void* result = SharedAddrCurrentLoc;
        SharedAddrCurrentLoc = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(SharedAddrCurrentLoc) + PAGESIZE
        );
        SAC_VPNumber += 1;
        return result;
    }
    
    // Expand environment variables in the path (e.g., $HOME)
    std::string filepath(name);
    size_t pos = filepath.find("$HOME");
    if (pos != std::string::npos) {
        const char* home = std::getenv("HOME");
        if (home != nullptr) {
            filepath.replace(pos, 5, home);
        }
    }
    
    // Open the file
    int fd = open(filepath.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "[dsm_malloc] Failed to open file: " << filepath 
                  << " - " << std::strerror(errno) << std::endl;
        return nullptr;
    }
    
    // Get file size using stat
    struct stat st;
    if (fstat(fd, &st) < 0) {
        std::cerr << "[dsm_malloc] Failed to stat file: " << filepath 
                  << " - " << std::strerror(errno) << std::endl;
        close(fd);
        return nullptr;
    }
    
    size_t filesize = st.st_size;
    
    // Set the number of elements (int type) if num pointer is provided
    if (num != nullptr) {
        *num = static_cast<int>(filesize / sizeof(int));
    }
    
    // Calculate the number of pages required (ceiling division)
    int page_required = static_cast<int>((filesize + PAGESIZE - 1) / PAGESIZE);
    if (page_required == 0) page_required = 1;  // At least one page
    
    int pagebasenumber = SAC_VPNumber;
    
    // Update page table entries for this file binding
    PageTable->GlobalMutexLock();
    for (int i = 0; i < page_required; i++) {
        PageRecord* record = PageTable->Find(pagebasenumber + i);
        if (record != nullptr) {
            record->filepath = filepath;
            record->fd = fd;
            record->offset = i;
            record->owner_id = -1;  
        } else {
            std::cout<<"error: no pagetable found!" << std::endl;
        }
    }
    PageTable->GlobalMutexUnlock();
    
    // Save the start address before updating
    void* result = SharedAddrCurrentLoc;
    
    // Update allocation pointers
    SharedAddrCurrentLoc = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(SharedAddrCurrentLoc) + static_cast<size_t>(page_required) * PAGESIZE
    );
    SAC_VPNumber += page_required;
    
    return result;
}

