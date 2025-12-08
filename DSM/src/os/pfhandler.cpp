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
    uintptr_t page_base = fault_addr & ~(g_page_sz - 1);
    size_t page_index = (page_base - region_start) / g_page_sz;
    
    // Check if this page needs to be pulled from remote
    if (InvalidPages != nullptr && InvalidPages[page_index] == 0) {
        // First access or page was invalidated, need to pull from remote
        pull_remote_page(page_base);
    }
    
    // Mark the page as modified (invalid for other nodes)
    if (InvalidPages != nullptr) {
        InvalidPages[page_index] = 1;
    }
    
    // Restore page permissions to allow read/write access
    if (mprotect((void*)page_base, g_page_sz, PROT_READ | PROT_WRITE) == -1) {
        std::cerr << "[segv_handler] mprotect failed for page at " 
                  << std::hex << page_base << std::dec << ": " 
                  << std::strerror(errno) << std::endl;
        return;
    }
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
    // Calculate page index
    uintptr_t region_start = (uintptr_t)g_region;
    size_t page_index = (page_base - region_start) / g_page_sz;
    
    // Calculate probable owner using hash
    int probowner = page_index % ProcNum;
    
    bool page_received = false;
    int retry_count = 0;
    const int max_retries = 10;
    
    while (!page_received && retry_count < max_retries) {
        // Get connection to probable owner
        std::string target_ip = GetPodIp(probowner);
        int target_port = GetPodPort(probowner);
        
        int sock = getsocket(target_ip, target_port);
        
        if (sock < 0) {
            std::cerr << "[pull_remote_page] Failed to connect to " << target_ip << ":" << target_port << std::endl;
            retry_count++;
            usleep(100000);  // Wait 100ms before retry
            continue;
        }
        
        // Get sequence number for this connection
        uint32_t seq_num = 1;
        if (SocketTable != nullptr) {
            SocketTable->LockAcquire();
            SocketRecord* record = SocketTable->Find(probowner);
            if (record != nullptr) {
                seq_num = record->allocate_seq();
            }
            SocketTable->LockRelease();
        }
        
        // Build and send DSM_MSG_PAGE_REQ
        dsm_header_t req_header = {
            DSM_MSG_PAGE_REQ,
            0,                          // unused
            htons(PodId),              // src_node_id
            htonl(seq_num),            // seq_num
            htonl(sizeof(payload_page_req_t))  // payload_len
        };
        
        payload_page_req_t req_payload = {
            htonl(page_index)          // page_index
        };
        
        // Send header and payload
        if (::send(sock, &req_header, sizeof(req_header), 0) != sizeof(req_header)) {
            std::cerr << "[pull_remote_page] Failed to send request header" << std::endl;
            retry_count++;
            continue;
        }
        
        if (::send(sock, &req_payload, sizeof(req_payload), 0) != sizeof(req_payload)) {
            std::cerr << "[pull_remote_page] Failed to send request payload" << std::endl;
            retry_count++;
            continue;
        }
        
        // Receive response
        rio_t rio;
        rio_readinit(&rio, sock);
        
        dsm_header_t rep_header;
        if (rio_readn(&rio, &rep_header, sizeof(rep_header)) != sizeof(rep_header)) {
            std::cerr << "[pull_remote_page] Failed to receive response header" << std::endl;
            retry_count++;
            continue;
        }
        
        if (rep_header.type != DSM_MSG_PAGE_REP) {
            std::cerr << "[pull_remote_page] Unexpected response type: " << (int)rep_header.type << std::endl;
            retry_count++;
            continue;
        }
        
        // Check unused field: 1 = has page data, 0 = redirect to real owner
        if (rep_header.unused == 0) {
            // Redirect: read real_owner_id and retry
            uint16_t real_owner_id;
            if (rio_readn(&rio, &real_owner_id, sizeof(real_owner_id)) != sizeof(real_owner_id)) {
                std::cerr << "[pull_remote_page] Failed to read real_owner_id" << std::endl;
                retry_count++;
                continue;
            }
            
            real_owner_id = ntohs(real_owner_id);
            probowner = real_owner_id;
            retry_count++;
            continue;  // Retry with new owner
        } else {
            // Has page data: read real_owner_id (ignored) and page data
            uint16_t real_owner_id;
            if (rio_readn(&rio, &real_owner_id, sizeof(real_owner_id)) != sizeof(real_owner_id)) {
                std::cerr << "[pull_remote_page] Failed to read real_owner_id field" << std::endl;
                retry_count++;
                continue;
            }
            
            // Read page data directly using dynamic allocation
            char* page_buffer = new (std::nothrow) char[DSM_PAGE_SIZE];
            if (page_buffer == nullptr) {
                std::cerr << "[pull_remote_page] Failed to allocate page buffer" << std::endl;
                retry_count++;
                continue;
            }
            
            if (rio_readn(&rio, page_buffer, DSM_PAGE_SIZE) != DSM_PAGE_SIZE) {
                std::cerr << "[pull_remote_page] Failed to read page data" << std::endl;
                delete[] page_buffer;
                retry_count++;
                continue;
            }
            
            // Copy page data to the memory location (DMA simulation)
            std::memcpy((void*)page_base, page_buffer, DSM_PAGE_SIZE);
            delete[] page_buffer;
            
            // Send OWNER_UPDATE to the manager (probowner based on page_index % ProcNum)
            int manager_id = page_index % ProcNum;
            std::string manager_ip = GetPodIp(manager_id);
            int manager_port = GetPodPort(manager_id);
            
            int mgr_sock = getsocket(manager_ip, manager_port);
            if (mgr_sock >= 0) {
                // Get sequence number for manager connection
                uint32_t mgr_seq = 1;
                if (SocketTable != nullptr) {
                    SocketTable->LockAcquire();
                    SocketRecord* mgr_record = SocketTable->Find(manager_id);
                    if (mgr_record != nullptr) {
                        mgr_seq = mgr_record->allocate_seq();
                    }
                    SocketTable->LockRelease();
                }
                
                // Build OWNER_UPDATE message
                dsm_header_t update_header = {
                    DSM_MSG_OWNER_UPDATE,
                    0,
                    htons(PodId),
                    htonl(mgr_seq),
                    htonl(sizeof(payload_owner_update_t))
                };
                
                payload_owner_update_t update_payload = {
                    htonl(page_index),
                    htons(PodId)
                };
                
                ::send(mgr_sock, &update_header, sizeof(update_header), 0);
                ::send(mgr_sock, &update_payload, sizeof(update_payload), 0);
                
                // Wait for ACK
                rio_t mgr_rio;
                rio_readinit(&mgr_rio, mgr_sock);
                dsm_header_t ack_header;
                rio_readn(&mgr_rio, &ack_header, sizeof(ack_header));
            }
            
            page_received = true;
        }
    }
    
    if (!page_received) {
        std::cerr << "[pull_remote_page] Failed to pull page " << page_index << " after " << max_retries << " retries" << std::endl;
    }
}