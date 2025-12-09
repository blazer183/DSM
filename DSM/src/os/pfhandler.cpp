#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "dsm.h"
#include "net/protocol.h"
#include "os/socket_table.h"

#ifdef UNITEST
#define STATIC 
#else
#define STATIC static
#endif

// Forward declarations
extern int* InvalidPages;
extern int getsocket(const std::string& ip, int port);
extern int SAB_VPNumber;  // Base virtual page number of shared region

STATIC size_t g_region_pages;   // number of pages in the managed region
STATIC size_t g_page_sz;        // system page size
STATIC void* g_region;          // start address of the managed memory region
STATIC struct sigaction g_prev_sa;  // previous SIGSEGV handler

// Forward declaration
STATIC void pull_remote_page(uintptr_t page_base);

STATIC void segv_handler(int signo, siginfo_t* info, void* uctx)
{
    (void)signo;
    (void)uctx;
    
    // Get the faulting address
    std::cout << "<Page Fault Happened!>" << std::endl;
    uintptr_t fault_addr = (uintptr_t)info->si_addr;
    uintptr_t region_start = (uintptr_t)g_region;
    uintptr_t region_end = region_start + (g_region_pages * g_page_sz);
    
    // Check if the fault address is within our managed shared region
    if (fault_addr < region_start || fault_addr >= region_end) {
        // Not in shared region, chain to previous handler
        if (g_prev_sa.sa_flags & SA_SIGINFO) {
            if (g_prev_sa.sa_sigaction != nullptr) {
                g_prev_sa.sa_sigaction(signo, info, uctx);
            }
        } else {
            if (g_prev_sa.sa_handler != nullptr && g_prev_sa.sa_handler != SIG_DFL && g_prev_sa.sa_handler != SIG_IGN) {
                g_prev_sa.sa_handler(signo);
            }
        }
        return;
    }
    
    // Calculate page base address and page index
    int VPN = fault_addr >> 12;
    
    // Check if this page needs to be pulled from remote
    if (InvalidPages != nullptr && InvalidPages[VPN - SAB_VPNumber] == 0) {
        pull_remote_page(VPN);
    } else {
        mprotect((void*)page_base, g_page_sz, PROT_READ | PROT_WRITE);
    }
    
    // Mark the page as modified (invalid for other nodes)
    InvalidPages[VPN - SAB_VPNumber] == 1;
}

void install_handler(void* base_addr, size_t num_pages)
{   
    //std::cout << "install handler set!" <<std::endl;
    g_page_sz =  PAGESIZE; // get system page size
    g_region_pages = num_pages;
    if(base_addr == nullptr) {
        base_addr = (void *)0x4000000000; // default base address
    }

    // Store the region address (mmap is already done in InitDataStructs)
    g_region = base_addr;

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

void pull_remote_page(int VPN){
    /*
    pseudocode:
    probowner = VPN % ProcNum

    send to probowner:
    Socket_prob = getsocket(probowner);
    A：
    send to Socket_prob{
        struct {
            DSM_MSG_PAGE_REQ
            0/1		
            PodId
            socketrecord.seq_num
            sizeof(payload)
        }

        payload: 
        struct {
            VPN	
        }
    }
    

    cout<<[System information] succeed in send msg <<endl

    rcv:
    if(unused == 0){
        probowner = real_owner_id
        goto A;
    }else{
        load pagedata to mem(DMA)
        //在DMA的同时，CPU进行以下活动overlap：
        probowner = page_index%ProcNum
        Socket_prob = getsocket(probowner)
        send to Socket_prob{
            struct {
                DSM_MSG_OWNER_UPDATE
                0/1
                PodId
                seq_num
                sizeof(payload)
            }
            
            payload:
            
            typedef struct {
                page_index;    // 页号
                PodId;   // 页面最新副本在哪里
            } payload_owner_update_t;
        }
        
        
        rvc: ACK
        while(DMA is not complete) {}
        cout << [System information] succeed <<
        return ;
    }


}