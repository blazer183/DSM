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
int NodeId = -1;
void *SharedAddrBase = nullptr;
int ProcNum = 0;
int WorkerNodeNum = 0;
std::vector<std::string> WorkerNodeIps;  // worker IP list

STATIC std::string LeaderNodeIp;
STATIC int LeaderNodePort = 0;
STATIC int LeaderNodeSocket = -1;
STATIC int LockNum = 0;
STATIC void * SharedAddrCurrentLoc = nullptr;   // current location in shared memory


std::string GetPodIp(int pod_id) {
    if (pod_id == 0) {
        // Leader Pod
        return LeaderNodeIp;
    } else if (pod_id > 0 && WorkerNodeNum > 0) {
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
    if (!GetEnvVar("DSM_NODE_ID", NodeId, -1, false)) exit(1);
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
    const bool ok = (NodeId >= 0) && (SharedAddrBase != nullptr) && (SharedPages > 0);
    if (!ok) {
        std::cerr << "[dsm] invalid shared region parameters (PodID=" << NodeId
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
    int leader_sock = LeaderNodeSocket;
    if (leader_sock == -1) {
        leader_sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (leader_sock < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(LeaderNodePort);
        ::inet_pton(AF_INET, target_ip.c_str(), &addr.sin_addr); 
        if (::connect(leader_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            ::close(leader_sock);
            return false;
        }
        LeaderNodeSocket = leader_sock;  // Cache the leader node socket for future use
    }

    
    dsm_header_t req = {
        DSM_MSG_JOIN_REQ,                    
        0,                       // unused
        htons(NodeId),           // src_node_id: source node ID
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

int dsm_init(int dsm_memsize)       //argc argv processing
{
    if (!FetchGlobalData(dsm_memsize))
        return -1;
    if(!LaunchListenerThread(LeaderNodePort+NodeId))
        return -2;
    if (!InitDataStructs(dsm_memsize))
        return -3;
    if(!dsm_barrier())
        return -4;
    return 0;
}

int dsm_getnodeid(void){
    return NodeId;
}

int dsm_finalize(void){
    return dsm_barrier();
}

int dsm_mutex_init(){
    if (LockTable == nullptr)
        return -1;

    LockTable->LockAcquire();
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

    const int lockid = *mutex;
    
    // Check if lock is already acquired locally
    LockTable->LockAcquire();
    auto record = LockTable->Find(lockid);
    if (record != nullptr && record->locked && record->owner_id == NodeId) {
        // Already acquired by this node
        record->locked = 1;
        LockTable->LockRelease();
        return 0;
    }
    LockTable->LockRelease();
    
    // Determine the probable owner of the lock
    int lockprobowner = lockid % ProcNum;
    
    // Keep trying until we get the lock
    while (true) {
        // Ensure we have a connection to the probable owner
        bool has_connection = false;
        SocketTable->LockAcquire();
        has_connection = (SocketTable->Find(lockprobowner) != nullptr);
        SocketTable->LockRelease();

        if (!has_connection) {
            // Establish connection to lock probowner
            std::string lockprobownerIP = GetPodIp(lockprobowner);
            int lockprotownerPort = GetPodPort(lockprobowner);
            int sock = ::socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) return -1;
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(lockprotownerPort);
            if (::inet_pton(AF_INET, lockprobownerIP.c_str(), &addr.sin_addr) <= 0) {
                ::close(sock);
                return -1;
            }
            if (::connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
                ::close(sock);
                return -1;
            }
            SocketTable->LockAcquire();
            SocketTable->Insert(lockprobowner, SocketRecord{sock});
            SocketTable->LockRelease();
        }
        
        // Get socket and sequence number
        SocketTable->LockAcquire();
        auto socket_record = SocketTable->Find(lockprobowner);
        if (socket_record == nullptr) {
            SocketTable->LockRelease();
            return -1;
        }
        int sock = socket_record->socket;
        uint32_t seq_num = socket_record->allocate_seq();
        SocketTable->LockRelease();
        
        // Send LOCK_ACQ request
        dsm_header_t req_header = {
            DSM_MSG_LOCK_ACQ,
            0,
            static_cast<uint16_t>(htons(NodeId)),
            htonl(seq_num),
            htonl(sizeof(payload_lock_req_t))
        };
        
        payload_lock_req_t req_payload = {
            htonl(lockid)
        };
        
        if (::send(sock, &req_header, sizeof(req_header), 0) != sizeof(req_header)) {
            return -1;
        }
        if (::send(sock, &req_payload, sizeof(req_payload), 0) != sizeof(req_payload)) {
            return -1;
        }
        
        // Receive LOCK_REP response
        rio_t rio;
        rio_readinit(&rio, sock);
        
        dsm_header_t rep_header;
        if (rio_readn(&rio, &rep_header, sizeof(rep_header)) != sizeof(rep_header)) {
            return -1;
        }
        
        // Check if this is the real owner (unused bit = 1) or redirect (unused bit = 0)
        if (rep_header.unused == 0) {
            // Redirect: need to contact the real owner
            payload_lock_rep_t rep_payload;
            if (rio_readn(&rio, &rep_payload, sizeof(rep_payload)) != sizeof(rep_payload)) {
                return -1;
            }
            int new_owner = ntohl(rep_payload.realowner);
            
            // If redirected to the same node, the lock is currently held, wait a bit
            if (new_owner == lockprobowner) {
                std::cout << "[DSM Lock] Lock " << lockid << " is held, waiting..." << std::endl;
                ::usleep(100000);  // Sleep for 100ms before retrying
            }
            
            lockprobowner = new_owner;
            // Loop back to try with the real owner
            continue;
        } else {
            // Successfully acquired the lock
            payload_lock_rep_t rep_payload;
            if (rio_readn(&rio, &rep_payload, sizeof(rep_payload)) != sizeof(rep_payload)) {
                return -1;
            }
            
            uint32_t invalid_set_count = ntohl(rep_payload.invalid_set_count);
            
            // Handle page invalidation if needed
            if (invalid_set_count > 0) {
                std::vector<uint32_t> invalid_pages(invalid_set_count);
                if (rio_readn(&rio, invalid_pages.data(), invalid_set_count * sizeof(uint32_t)) 
                    != static_cast<ssize_t>(invalid_set_count * sizeof(uint32_t))) {
                    return -1;
                }
                
                // Use mprotect to modify page permissions for invalidated pages
                for (uint32_t i = 0; i < invalid_set_count; i++) {
                    uint32_t page_idx = ntohl(invalid_pages[i]);
                    void* page_addr = static_cast<char*>(SharedAddrBase) + page_idx * DSM_PAGE_SIZE;
                    
                    // Remove write permissions (make page read-only or no access)
                    if (::mprotect(page_addr, DSM_PAGE_SIZE, PROT_NONE) != 0) {
                        std::cerr << "[DSM Lock] Failed to mprotect page " << page_idx 
                                  << ": " << std::strerror(errno) << std::endl;
                    }
                }
            }
            
            // Calculate probowner for this lock
            int probowner_id = lockid % ProcNum;
            
            // Only send OWNER_UPDATE if we're not communicating with the probowner
            // (i.e., if lockprobowner != probowner_id, meaning we got redirected to real owner)
            // When we talk to probowner directly and get the lock, we don't need to update
            if (lockprobowner != probowner_id) {
                // We need to inform the probowner about the ownership change
                // Get probowner's socket to send OWNER_UPDATE
                int probowner_sock = -1;
                uint32_t update_seq = 0;
                
                SocketTable->LockAcquire();
                auto probowner_socket_rec = SocketTable->Find(probowner_id);
                if (probowner_socket_rec == nullptr) {
                    SocketTable->LockRelease();
                    // Try to establish connection to probowner
                    std::string probownerIP = GetPodIp(probowner_id);
                    int probownerPort = GetPodPort(probowner_id);
                    int prob_sock = ::socket(AF_INET, SOCK_STREAM, 0);
                    if (prob_sock < 0) return -1;
                    sockaddr_in addr{};
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(probownerPort);
                    if (::inet_pton(AF_INET, probownerIP.c_str(), &addr.sin_addr) <= 0) {
                        ::close(prob_sock);
                        return -1;
                    }
                    if (::connect(prob_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
                        ::close(prob_sock);
                        return -1;
                    }
                    SocketTable->LockAcquire();
                    SocketTable->Insert(probowner_id, SocketRecord{prob_sock});
                    probowner_socket_rec = SocketTable->Find(probowner_id);
                }
                
                probowner_sock = probowner_socket_rec->socket;
                update_seq = probowner_socket_rec->allocate_seq();
                SocketTable->LockRelease();
                
                // Send OWNER_UPDATE message to probowner
                dsm_header_t update_header = {
                    DSM_MSG_OWNER_UPDATE,
                    0,  // unused bit = 0 indicates lock table update (not page table)
                    static_cast<uint16_t>(htons(NodeId)),
                    htonl(update_seq),
                    htonl(sizeof(payload_owner_update_t))
                };
                
                payload_owner_update_t update_payload = {
                    htonl(lockid),
                    htons(NodeId)  // New owner is this node
                };
                
                if (::send(probowner_sock, &update_header, sizeof(update_header), 0) != sizeof(update_header)) {
                    return -1;
                }
                if (::send(probowner_sock, &update_payload, sizeof(update_payload), 0) != sizeof(update_payload)) {
                    return -1;
                }
                
                // Receive ACK
                rio_t ack_rio;
                rio_readinit(&ack_rio, probowner_sock);
                
                dsm_header_t ack_header;
                if (rio_readn(&ack_rio, &ack_header, sizeof(ack_header)) != sizeof(ack_header)) {
                    return -1;
                }
                
                if (ack_header.type == DSM_MSG_ACK) {
                    payload_ack_t ack_payload;
                    if (rio_readn(&ack_rio, &ack_payload, sizeof(ack_payload)) != sizeof(ack_payload)) {
                        return -1;
                    }
                    
                    if (ack_payload.status != 0) {
                        std::cerr << "[DSM Lock] ACK status failed for lock " << lockid << std::endl;
                        return -1;
                    }
                }
            }
            
            // Update local lock table
            LockTable->LockAcquire();
            auto local_rec = LockTable->Find(lockid);
            if (local_rec != nullptr) {
                local_rec->locked = true;
                local_rec->owner_id = NodeId;
                LockTable->Update(lockid, *local_rec);
            } else {
                // Insert new record if not exists
                LockTable->Insert(lockid, LockRecord(NodeId));
            }
            LockTable->LockRelease();
            
            std::cout << "[DSM Lock] Successfully acquired lock " << lockid << std::endl;
            return 0;
        }
    }
    
    return 0;
}

int dsm_mutex_unlock(int *mutex){   //local operation
    if (LockTable == nullptr || mutex == nullptr)
        return -1;

    LockTable->LockAcquire();
    auto record = LockTable->Find(*mutex);
    if(record == nullptr){
        LockTable->LockRelease();
        std::cerr << "[dsm_mutex_unlock] Invalid mutex ID: " << *mutex << std::endl;
        return -1; // Invalid mutex ID
    }
    record->locked = false;
    LockTable->Update(*mutex, *record);
    LockTable->LockRelease();
    return 0;
}

void dsm_bind(void *addr, const char *name){
    if (BindTable == nullptr || addr == nullptr || name == nullptr)
        return;

    BindTable->LockAcquire();
    BindTable->Insert(addr, BindRecord{addr, std::string(name)});
    BindTable->LockRelease();
}

void *dsm_malloc(size_t size){
    if(SharedAddrCurrentLoc + size > SharedAddrBase + SharedPages * DSM_PAGE_SIZE){
        std::cerr << "[dsm_malloc] Out of shared memory!" << std::endl;
        return nullptr;
    }
    SharedAddrCurrentLoc += size;
    return SharedAddrCurrentLoc - size;
}
