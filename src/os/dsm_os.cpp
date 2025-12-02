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

size_t SharedPages = 0;
int NodeId = -1;
void *SharedAddrBase = nullptr;
int ProcNum = 0;
int WorkerNodeNum = 0;
std::vector<std::string> WorkerNodeIps;  // 使用vector存储IP列表

STATIC std::string LeaderNodeIp;
STATIC int LeaderNodePort = 0;
STATIC int LeaderNodeSocket = -1;
constexpr int kDefaultListenPort = 9999;



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
        return kDefaultListenPort + pod_id;
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
        return true;
    } catch (const std::system_error &err) {
        std::cerr << "[dsm] failed to launch listener thread: " << err.what() << std::endl;
        return false;
    }
}

bool FetchGlobalData(int dsm_memsize)    
{
    // 统一从环境变量获取所有配置参数
    if (!GetEnvVar("DSM_LEADER_IP", LeaderNodeIp, std::string(""), true)) exit(1);
    if (!GetEnvVar("DSM_LEADER_PORT", LeaderNodePort, 0, true)) exit(1);
    if (!GetEnvVar("DSM_TOTAL_PROCESSES", ProcNum, 1, false)) exit(1);
    if (!GetEnvVar("DSM_NODE_ID", NodeId, -1, false)) exit(1);

    
    // 获取Worker拓扑信息 (用于地址映射算法)
    if (!GetEnvVar("DSM_WORKER_COUNT", WorkerNodeNum, 0, false)) exit(1);
    std::string worker_ips_str;
    if (!GetEnvVar("DSM_WORKER_IPS", worker_ips_str, std::string(""), false)) exit(1);

    
    // 解析逗号分隔的IP列表到vector
    WorkerNodeIps.clear();
    if (!worker_ips_str.empty()) {
        std::stringstream ss(worker_ips_str);
        std::string ip;
        while (std::getline(ss, ip, ',')) {
            WorkerNodeIps.push_back(ip);
        }
        std::cout << "[DSM Info] Parsed " << WorkerNodeIps.size() << " worker IPs" << std::endl;
    }


    //初始化全局变量
    SharedPages = static_cast<size_t>(dsm_memsize) / PAGESIZE;
    SharedAddrBase = reinterpret_cast<void *>(0x4000000000ULL);  


    // 判断当前进程是否为Leader (NodeId == 0)
    if (NodeId == 0) {
        LeaderNodeSocket = -1;
        std::cout << "[DSM Info] I am Leader (PodID=0), starting listener on port " << LeaderNodePort << std::endl;
        return LaunchListenerThread(LeaderNodePort);
    }


    const bool ok = (NodeId >= 0) && (SharedAddrBase != nullptr) && (SharedPages > 0);
    if (!ok) {
        std::cerr << "[dsm] invalid shared region parameters (PodID=" << NodeId
                  << ", base=" << SharedAddrBase << ", pages=" << SharedPages << ")" << std::endl;
        return false;
    }
    
    std::cout << "[DSM Info] I am Worker (PodID=" << NodeId << "), cluster ready, starting listener on port " << (kDefaultListenPort+NodeId) << std::endl;
    return LaunchListenerThread(kDefaultListenPort+NodeId);
}

bool InitDataStructs(int dsm_memsize)
{   
    // 分配共享内存区域
    if (SharedAddrBase != nullptr && SharedPages != 0){
        const size_t total_size = (size_t)dsm_memsize;
        
        void* mapped_addr = ::mmap(
            SharedAddrBase,                    // 固定起始地址
            total_size,                        // 分配大小
            PROT_NONE,                        // 初始无权限，触发缺页异常
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,  // 私有匿名映射，固定地址
            -1,                               // 无文件描述符
            0                                 // 偏移量为0
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



    // 初始化页表、锁表、Bind表
    if (PageTable == nullptr)
        PageTable = new (::std::nothrow) ::PageTable();
    if (LockTable == nullptr)
        LockTable = new (::std::nothrow) ::LockTable();
    if (BindTable == nullptr)
        BindTable = new (::std::nothrow) ::BindTable();
    const bool ok = (PageTable != nullptr) && (LockTable != nullptr) && (BindTable != nullptr);
    if (!ok){
        std::cerr << "[dsm] failed to allocate metadata tables" << std::endl;
        return false;
    }

    return true;
}

bool barrier()
{
    // Leader 本身也是计算进程，需要参与 barrier 同步
    // 如果是 Leader，使用本地地址（127.0.0.1），然后走同一套通信逻辑
    std::string target_ip = LeaderNodeIp;
    if (NodeId == 0) {
        target_ip = "127.0.0.1";  // Leader 给自己发消息
    }

    // 检查是否已有长连接，有则复用，无则建立
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
        LeaderNodeSocket = leader_sock;  // 保存连接以便后续复用
    }

    // 发送0x01报文: 告知Leader我已就绪，监听端口为9999+NodeId
    payload_join_req_t body = {htons(kDefaultListenPort + NodeId)}; 
    dsm_header_t req = {
        DSM_MSG_JOIN_REQ,                    
        0,                       // unused
        htons(NodeId),           // src_node_id: 使用预分配的ID 
        htonl(1),                // seq_num: 1 
        htonl(sizeof(body))      // payload length
    };

    ::send(leader_sock, &req, sizeof(req), 0);
    ::send(leader_sock, &body, sizeof(body), 0);

    // 等待Leader确认所有进程就绪后的回复
    rio_t rio;
    rio_readinit(&rio, leader_sock);
    
    dsm_header_t ack_header;
    rio_readn(&rio, &ack_header, sizeof(ack_header));
    // Leader回复无payload，只要收到ACK就表示集群已就绪

    return true;
}


int dsm_init(int dsm_memsize)       //argc argv用于传递
{
    if (!FetchGlobalData(dsm_memsize))
        return -1;
    if (!InitDataStructs(dsm_memsize))
        return -1;
    if(!barrier())
        return -1;
    return 0;
}
