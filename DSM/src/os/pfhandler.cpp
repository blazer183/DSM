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

STATIC size_t g_region_pages;   // number of pages in the managed region
STATIC size_t g_page_sz;        // system page size
STATIC void* g_region;          // start address of the managed memory region
STATIC struct sigaction g_prev_sa;  // previous SIGSEGV handler

STATIC void segv_handler(int signo, siginfo_t* info, void* uctx)
{

/*
pseudocode：
if(指针不在共享区内){
    跳转系统缺页调用
}else if(InvalidPages[this page] == 1){
    InvalidPages[this page] = 1;  //标记已经修改了
    mprotect回复页权限，直接返回
}else{
    pull_remote_page(page_base);
}


*/
    
}

void install_handler(void* base_addr, size_t num_pages)
{
    g_page_sz = static_cast<size_t>(sysconf(_SC_PAGESIZE)); // get system page size
    g_region_pages = num_pages;
    if(base_addr == nullptr) {
        base_addr = (void *)0x4000000000; // default base address
    }

    g_region = mmap(base_addr, num_pages * g_page_sz, PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (g_region == MAP_FAILED) {
        perror("mmap");
        std::exit(1);
    }

    // Set up alternate signal stack

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