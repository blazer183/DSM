#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

#include "dsm.h"
#include "net/protocol.h"

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
}else{
    if(InvalidPages[this page] == 0){
        pull_remote_page(page_base);
    }
    InvalidPages[this page] = 1;  //标记已经修改了
    mprotect恢复页权限，直接返回
} 
*/
    
}

void install_handler(void* base_addr, size_t num_pages)
{
    g_page_sz =  PAGESIZE; // get system page size
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
/*
pseudocode:
page_index = page_base对应的页号
probowner = page_index % ProcNum
A:
    send to probowner:
    struct {
        DSM_MSG_PAGE_REQ
        0/1		
        PodId
        seq_num
        sizeof(payload)
    }

    payload: 
    struct {
        page_index	//uint32_t类型，标识虚拟页号
    }

    rcv:
    if(unused == 0){
        probowner = real_owner_id
        goto A;
    }else{
        load pagedata to mem(DMA)
        //在DMA的同时，CPU进行以下活动overlap：
        probowner = page_index%ProcNum
        send:
        struct {
            DSM_MSG_OWNER_UPDATE
            0/1
            PodId
            seq_num
            sizeof(payload)
        }
        
        payload:
        // [DSM_MSG_OWNER_UPDATE] RealOwner -> Manager
        typedef struct {
            page_index;    // 页号
            PodId;   // 页面最新副本在哪里
        } __attribute__((packed)) payload_owner_update_t;
        
        rvc: ACK
        while(DMA is not complete) {}

        return ;
    }


*/

}