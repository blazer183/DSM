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
        std::memset(DirtyPages, 0, sizeof(int) * SharedPages);
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
    if(!dsm_barrier())
        return -4;
    return 0;
}

int dsm_getpodid(void){
    return PodId;
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
    
    int lockprobowner = lockid % ProcNum;

    /*  
        查询并建立socket，这里建议封装成函数调用，
    比如getsocket(int PodId, int NodePort) 函数内部判断如果已经存在socket就直接返回，如果不存在就建立连接。

        socket = getsocket(lockprobowner, LeaderNodePort);
        send:
        struct {
            DSM_MSG_LOCK_ACQ
            0/1
            PodId
            seq_num
            sizeof(payload)
        }

        payload:
        struct {
            lock_id	//uint32_t类型 锁ID
        }

        recv:
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

        将全部共享区都设置为invalid(可以记录在锁获取期间修改了哪些页)
        将vector invalid page list 全部copy到全局变量 InvalidPages [SharedPages], 用于记录哪些页无效.
        
        return ;

    */
    
}    

int dsm_mutex_unlock(int *mutex){   
    /*
        
        socket = getsocket(probownerid, LeaderNodePort);
        
        send:
        typedef struct {
            DSM_MSG_LOCK_RLS           
            0/1         
            PodId    
            seq_num        
            sizeof(负载)    
        } __attribute__((packed)) dsm_header_t;    

        // [DSM_MSG_LOCK_RLS] LockOwner -> Manager (释放锁)
        typedef struct {
            uint32_t invalid_set_count; // Scope Consistency: 需要失效的页数量
            vector invalid_page_list;   // 把InvalidPages中标记的页号都放到这个list里发送给Manager
        } __attribute__((packed)) payload_lock_rls_t;



    */
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
