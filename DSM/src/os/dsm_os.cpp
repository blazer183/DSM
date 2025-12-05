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
std::vector<std::string> WorkerNodeIps;  // ʹ��vector�洢IP�б�

STATIC std::string LeaderNodeIp;
STATIC int LeaderNodePort = 0;
STATIC int LeaderNodeSocket = -1;
STATIC int LockNum = 0;
STATIC void * SharedAddrCurrentLoc = nullptr;   //��ǰ����������λ��


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
    SharedPages = static_cast<size_t>(dsm_memsize) / PAGESIZE;
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
    // ���乲���ڴ�����
    if (SharedAddrBase != nullptr && SharedPages != 0){
        const size_t total_size = (size_t)dsm_memsize;
        void* mapped_addr = ::mmap(
            SharedAddrBase,                    // �̶���ʼ��ַ
            total_size,                        // �����С
            PROT_NONE,                        // ��ʼ��Ȩ�ޣ�����ȱҳ�쳣
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,  // ˽������ӳ�䣬�̶���ַ
            -1,                               // ���ļ�������
            0                                 // ƫ����Ϊ0
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


    // ��ʼ��ҳ����������Bind��
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

    // ����Ƿ����г����ӣ������ã�������
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
        LeaderNodeSocket = leader_sock;  // ���������Ա��������
    }

    
    dsm_header_t req = {
        DSM_MSG_JOIN_REQ,                    
        0,                       // unused
        htons(NodeId),           // src_node_id: ʹ��Ԥ�����ID 
        htonl(1),                // seq_num: 1 
        0                       // payload length
    };
    ::send(leader_sock, &req, sizeof(req), 0);

    // �ȴ�Leaderȷ�����н��̾�����Ļظ�
    rio_t rio;
    rio_readinit(&rio, leader_sock);
    
    dsm_header_t ack_header;
    rio_readn(&rio, &ack_header, sizeof(ack_header));
    
    return true;
}

int dsm_init(int dsm_memsize)       //argc argv���ڴ���
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

int dsm_finalize(void);

int dsm_mutex_init(){
    if (LockTable == nullptr)
        return -1;

    LockTable->LockAcquire();
    LockTable->Insert(++LockNum, LockRecord());
    LockTable->LockRelease();
    return 0;
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
    if (mutex == nullptr || SocketTable == nullptr)
        return -1;

    const int lockid = *mutex;
    const int lockprobowner = lockid % ProcNum;

    bool has_connection = false;
    SocketTable->LockAcquire();
    has_connection = (SocketTable->Find(lockprobowner) != nullptr);
    SocketTable->LockRelease();

    if (!has_connection) {
        //��������
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
    // TODO: Send lock request message
    // Check if lock is already acquired locally
    // If not, retrieve real owner ID and send request
    // Meanwhile send message to probowner to get real owner info
    // Wait for ACK and return accordingly
    return 0; // Placeholder - function needs full implementation
}

int dsm_mutex_unlock(int *mutex){
    if (LockTable == nullptr || mutex == nullptr)
        return -1;

    LockTable->LockAcquire();
    auto record = LockTable->Find(*mutex);
    if(record == nullptr){
        LockTable->LockRelease();
        std::cerr << "[dsm_mutex_unlock] Invalid mutex ID: " << *mutex << std::endl;
        return -1; // �������� 
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
