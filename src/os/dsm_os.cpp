#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/mman.h>

#include "dsm.h"
#include "net/protocol.h"
#include "os/bind_table.h"
#include "os/lock_table.h"
#include "os/page_table.h"
#include "os/pfhandler.h"

// 来自其他编译单元的函数声明
extern void dsm_start_daemon(int port);


// DSM 对外可见的全局变量实际定义
struct PageTable *PageTable = nullptr;
struct LockTable *LockTable = nullptr;
struct BindTable *BindTable = nullptr;

size_t SharedPages = 0;
int NodeId = -1;
void *SharedAddrBase = nullptr;
int ProcNum = 0;

std::string LeaderIp;
int LeaderPort = 0;
int LeaderSocket = -1;



namespace {

constexpr int kDefaultListenPort = 9999;

bool LaunchListenerThread()
{
    try {
        std::thread listener([]() {
            dsm_start_daemon(kDefaultListenPort);
        });
        listener.detach();
        return true;
    } catch (const std::system_error &err) {
        std::cerr << "[dsm] failed to launch listener thread: " << err.what() << std::endl;
        return false;
    }
}

bool JoinClusterAndFetchMetadata(int dsm_memsize)    
{
    // 从环境变量中获取主节点IP，端口和总进程数
    const char* env_ip = std::getenv("DSM_LEADER_IP");
    if (env_ip != nullptr) {
        LeaderIp = env_ip;
    } else {
        std::cerr << "[DSM Warning] DSM_LEADER_IP not set! exit! " << std::endl;
        exit(1);
    }
    const char* env_port = std::getenv("DSM_LEADER_PORT");
    if (env_port != nullptr) {
        LeaderPort = std::atoi(env_port);
    } else {
        std::cerr << "[DSM Warning] DSM_LEADER_PORT not set! exit! " << std::endl;
        exit(1);
    }
    const char* env_total = std::getenv("DSM_TOTAL_PROCESSES");
    if (env_total != nullptr) {
        ProcNum = std::atoi(env_total);
        std::cout << "[DSM Info] Total processes in cluster: " << ProcNum << std::endl;
    } else {
        std::cerr << "[DSM Warning] DSM_TOTAL_PROCESSES not set! Using default value 1" << std::endl;
        ProcNum = 1;
    }

    SharedPages = static_cast<size_t>(dsm_memsize) / PAGESIZE;
    SharedAddrBase = reinterpret_cast<void *>(0x4000000000ULL);  //固定起始地址

    // 建立TCP连接到Leader
    int leader_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (leader_sock < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LeaderPort);
    ::inet_pton(AF_INET, LeaderIp.c_str(), &addr.sin_addr);
    
    if (::connect(leader_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(leader_sock);
        return false;
    }

    // 发送0x01报文: 头部(0x01, 0, -1, 1, sizeof(body)) + 端口号  
    payload_join_req_t body = {htons(kDefaultListenPort)}; 
    dsm_header_t req = {
        0x01,               // type
        0,                  // unused
        htons(0xFFFF),      // src_node_id: -1 
        htonl(1),           // seq_num: 1 
        htonl(sizeof(body)) // port number payload length
    };

    
    ::send(leader_sock, &req, sizeof(req), 0);
    ::send(leader_sock, &body, sizeof(body), 0);

    // 等待并解析回复: 使用rio确保完整读取
    rio_t rio;
    rio_readinit(&rio, leader_sock);
    
    dsm_header_t ack_header;
    rio_readn(&rio, &ack_header, sizeof(ack_header));
    
    payload_join_ack_t ack_body;
    rio_readn(&rio, &ack_body, sizeof(ack_body));
    
    NodeId = ack_body.assigned_node_id;        // ID从payload中获取
    SharedAddrBase = (void*)0x4000000000ULL;   // 使用默认基址

    LeaderSocket = leader_sock;


    const bool ok = (NodeId >= 0) && (SharedAddrBase != nullptr) && (SharedPages > 0);
    if (!ok) {
        std::cerr << "[dsm] missing cluster metadata (node=" << NodeId
                  << ", base=" << SharedAddrBase << ", pages=" << SharedPages << ")" << std::endl;
    }
    return ok;
}

bool InitializeDsmTables()
{
    if (PageTable == nullptr)
        PageTable = new (::std::nothrow) ::PageTable();
    if (LockTable == nullptr)
        LockTable = new (::std::nothrow) ::LockTable();
    if (BindTable == nullptr)
        BindTable = new (::std::nothrow) ::BindTable();
    
    PageTable.
    
    const bool ok = (PageTable != nullptr) && (LockTable != nullptr) && (BindTable != nullptr);
    if (!ok)
        std::cerr << "[dsm] failed to allocate metadata tables" << std::endl;
    return ok;
}

bool SetupSharedRegionAndBarrier()
{
    if (SharedAddrBase == nullptr || SharedPages == 0) {
        std::cerr << "[dsm] invalid shared region parameters" << std::endl;
        return false;
    }

    const size_t total_size = SharedPages * PAGESIZE;
    
    // 使用mmap在指定地址分配共享内存
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
    
    std::cout << "[dsm] shared region reserved: " << SharedAddrBase 
              << " size " << total_size << " bytes (" << SharedPages << " pages) - PROT_NONE" << std::endl;
    //todo: barrier初始化 
    

    return true;
}

} // namespace

int dsm_init(int argc, char *argv[], int dsm_memsize)       //argc argv用于传递
{
    if (!LaunchListenerThread())
       return -1;   
    if (!JoinClusterAndFetchMetadata(dsm_memsize))
        return -1;
    if (!InitializeDsmTables())
        return -1;
    if (!SetupSharedRegionAndBarrier())
        return -1;
    // if (SharedAddrBase != nullptr && SharedPages != 0)
    //     install_handler(SharedAddrBase, SharedPages);
    return 0;
}
