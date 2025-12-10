#include "mock_env.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>      // 必须包含这个才能用 open
#include <sys/stat.h>
#include <sys/types.h>

#include "net/protocol.h"   // 包含 DSM_PAGE_SIZE
#include "os/page_table.h"  // 包含 class PageTable 的具体定义，否则无法 new
#include "os/lock_table.h"  // 包含 class LockTable 的具体定义

// ==========================================
// 1. 全局变量定义 (实例化)
// ==========================================
void* SharedAddrBase = nullptr;
class PageTable* PageTable = nullptr;
class LockTable* LockTable = nullptr;

// 存储当前进程模拟的节点 ID
static int g_current_node_id = -1;

void set_dsm_nodeid(int id) {
    g_current_node_id = id;
}

int dsm_getnodeid() {
    return g_current_node_id;
}

// 模拟 IP：全部都是本机
std::string GetPodIp(int node_id) {
    return "127.0.0.1";
}

// 模拟端口：Node 0 -> 8000, Node 1 -> 8001 ...
int GetPodPort(int node_id) {
    return 8000 + node_id;
}

void mock_init(int node_id) {
    // 设置 ID
    set_dsm_nodeid(node_id);

    // 1. 分配共享内存基址 (模拟 100 页大小)
    size_t mem_size = DSM_PAGE_SIZE * 100;
    SharedAddrBase = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, 
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (SharedAddrBase == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }
    // 初始化为全 0
    memset(SharedAddrBase, 0, mem_size);

    // 2. 创建全局表
    ::PageTable = new class PageTable();
    ::LockTable = new class LockTable();

    std::cout << "[MockEnv] Initialized Node " << node_id 
              << ". MemBase: " << SharedAddrBase << std::endl;

    
        std::cout << "[MockEnv] Pre-populating PageTable for Node " << node_id << "..." << std::endl;

        PageRecord rec;
        rec.owner_id = -1; // 初始状态：无人持有(需要读盘)

        for(int i = 0; i < 3; i++){
            rec.filepath = "../test_data/page" + std::to_string(i) + ".txt";
            rec.offset = i;
            rec.owner_id = -1;
            rec.fd = open(rec.filepath.c_str(), O_RDONLY);
        
        if (rec.fd < 0) {
            // 如果打开失败，测试无法进行，直接报错
            perror("[MockEnv] Fatal: Could not open mock file");
            std::cerr << "Path tried: " << rec.filepath << std::endl;
            exit(1); 
        }
            ::PageTable->Insert(i, rec);
            std::cout << "[MockEnv] Page " << i << " bound to '" << rec.filepath << "'" << std::endl;
        }
        
        
       
    
}

void mock_cleanup() {
    if (::PageTable) delete ::PageTable;
    if (::LockTable) delete ::LockTable;
    // munmap 省略，进程退出自动回收
    std::cout << "[MockEnv] Cleanup done." << std::endl;
}