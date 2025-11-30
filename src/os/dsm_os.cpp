// 包含 dsm_init , 会调用 client_end 里的 getnodeid(); get_shared_memsize(); get_shared_addrbase();

#include <iostream>
#include <new>
#include <system_error>
#include <thread>

#include "dsm.h"
#include "os/bind_table.h"
#include "os/lock_table.h"
#include "os/page_table.h"
#include "os/pfhandler.h"

// 来自其他编译单元的函数声明
extern void dsm_start_daemon(int port);
extern int getnodeid(void);
extern size_t get_shared_memsize(void);
extern void *get_shared_addrbase(void);

// DSM 对外可见的全局变量实际定义
PageTable *PageTable = nullptr;
LockTable *LockTable = nullptr;
BindTable *BindTable = nullptr;

size_t SharedPages = 0;
int NodeId = -1;
void *SharedAddrBase = nullptr;

namespace {

constexpr int kDefaultListenPort = 9000;

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

bool JoinClusterAndFetchMetadata(int argc, char **argv, int dsm_memsize)
{
    (void)argc;
    (void)argv;
    // todo: fill NodeId, SharedAddrBase
    // SharedPages = dsm_memsize / PAGESIZE  

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

    const bool ok = (PageTable != nullptr) && (LockTable != nullptr) && (BindTable != nullptr);
    if (!ok)
        std::cerr << "[dsm] failed to allocate metadata tables" << std::endl;
    return ok;
}

bool SetupSharedRegionAndBarrier()
{
    if (SharedAddrBase == nullptr || SharedPages == 0)
        return false;

    // TODO: 真正的共享区映射与屏障初始化逻辑在此填充
    
    return true;
}

} // namespace

int dsm_init(int argc, char *argv[], int dsm_memsize)
{
    if (!LaunchListenerThread())
        return -1;

    if (!JoinClusterAndFetchMetadata(argc, argv, dsm_memsize))
        return -1;

    if (!InitializeDsmTables())
        return -1;

    if (!SetupSharedRegionAndBarrier())
        return -1;

    if (SharedAddrBase != nullptr && SharedPages != 0)
        install_handler(SharedAddrBase, SharedPages);

    return 0;
}
