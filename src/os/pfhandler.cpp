#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

#ifdef UNITEST
#define STATIC 
#else
#define STATIC static
#endif

STATIC size_t g_region_pages;   // 管理区域的页面数
STATIC size_t g_page_sz;        // 系统页面大小
STATIC void* g_region;          // 受管理的内存区域起始地址
STATIC struct sigaction g_prev_sa;  // 先前的 SIGSEGV 处理器

STATIC void segv_handler(int signo, siginfo_t* info, void* uctx)
{
    (void)uctx;
    const uintptr_t fault = reinterpret_cast<uintptr_t>(info->si_addr);
    const uintptr_t base  = reinterpret_cast<uintptr_t>(g_region);
    const uintptr_t limit = base + g_region_pages * g_page_sz;

    if (fault < base || fault >= limit) {
        sigaction(SIGSEGV, &g_prev_sa, nullptr);
        raise(SIGSEGV);
        return;
    }

    const uintptr_t page_base = fault & ~(g_page_sz - 1);   //页基址
    //报错声明
    std::cerr << "[handler] fault @ " << info->si_addr << " -> page "
              << reinterpret_cast<void*>(page_base) << '\n';

    // TODO: 拉取远端页、刷新版本号等
    //pull_remote_page(page_base);

    // 恢复页面权限为读写
    if (mprotect(reinterpret_cast<void*>(page_base), g_page_sz,
                 PROT_READ | PROT_WRITE) == -1) {
        perror("mprotect");
        _exit(1);
    }

    std::memset(reinterpret_cast<void*>(page_base), 0x42, g_page_sz);
}

void install_handler(void* base_addr, size_t num_pages)
{
    g_page_sz = static_cast<size_t>(sysconf(_SC_PAGESIZE)); // 获取系统页面大小
    g_region_pages = num_pages;
    if(base_addr == nullptr) {
        base_addr = (void *)0x4000000000; // 默认基址
    }

    g_region = mmap(base_addr, num_pages * g_page_sz, PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (g_region == MAP_FAILED) {
        perror("mmap");
        std::exit(1);
    }

    // 设置信号专用栈

    stack_t alt{};
    alt.ss_sp = std::malloc(SIGSTKSZ);
    alt.ss_size = SIGSTKSZ;
    sigaltstack(&alt, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = segv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGSEGV, &sa, &g_prev_sa) == -1) {
        perror("sigaction");
        std::exit(1);
    }
}

void pull_remote_page(uintptr_t page_base){

}