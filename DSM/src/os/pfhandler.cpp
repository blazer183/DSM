#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>

#include "dsm.h"
#include "net/protocol.h"
#include "os/socket_table.h"
#include "os/page_table.h"

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
STATIC void pull_remote_page(int VPN);

STATIC void segv_handler(int signo, siginfo_t* info, void* uctx)
{
    (void)signo;
    (void)uctx;
    
    // Get the faulting address
    std::cout << "[System information] Page Fault Happened!" << std::endl;
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
    uintptr_t page_base = static_cast<uintptr_t>(VPN) << 12;
    
    // Check if this page needs to be pulled from remote
    if (InvalidPages != nullptr && InvalidPages[VPN - SAB_VPNumber] == 0) {
        pull_remote_page(VPN);
    } else {
        mprotect((void*)page_base, g_page_sz, PROT_READ | PROT_WRITE);
    }
    
    // Mark the page as modified (invalid for other nodes)
    InvalidPages[VPN - SAB_VPNumber] = 1;
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
    // Calculate probable owner using hash (VPN % ProcNum)
    // This is the manager for this page - we need to update this node after getting the page
    int manager_id = VPN % ProcNum;
    int probowner = manager_id;
    
    // Calculate page base address
    uintptr_t page_base = static_cast<uintptr_t>(VPN) << 12;
    
    // Retry loop for following redirects to real owner
    while (true) {
        // Get socket to probable owner
        std::string target_ip = GetPodIp(probowner);
        int target_port = GetPodPort(probowner);
        int sock = getsocket(target_ip, target_port);
        
        if (sock < 0) {
            std::cerr << "[pull_remote_page] Failed to connect to node " << probowner 
                      << " at " << target_ip << ":" << target_port << std::endl;
            return;
        }
        
        // Get sequence number for this connection
        uint32_t seq_num = 1;
        if (SocketTable != nullptr) {
            SocketTable->GlobalMutexLock();
            SocketRecord* record = SocketTable->Find(probowner);
            if (record != nullptr) {
                seq_num = record->allocate_seq();
            }
            SocketTable->GlobalMutexUnlock();
        }
        
        // Build and send PAGE_REQ message
        dsm_header_t req_header = {
            DSM_MSG_PAGE_REQ,
            0,                              // unused
            htons(static_cast<uint16_t>(PodId)),  // src_node_id
            htonl(seq_num),                 // seq_num
            htonl(sizeof(payload_page_req_t))  // payload_len
        };
        
        payload_page_req_t req_payload = {
            htonl(static_cast<uint32_t>(VPN))  // page_index
        };
        
        // Send header and payload
        if (::send(sock, &req_header, sizeof(req_header), 0) != sizeof(req_header)) {
            std::cerr << "[pull_remote_page] Failed to send PAGE_REQ header" << std::endl;
            return;
        }
        
        if (::send(sock, &req_payload, sizeof(req_payload), 0) != sizeof(req_payload)) {
            std::cerr << "[pull_remote_page] Failed to send PAGE_REQ payload" << std::endl;
            return;
        }
        
        std::cout << "[System information] Succeed in sending PAGE_REQ for VPN=" << VPN 
                  << " to node " << probowner << std::endl;
        
        // Receive response
        rio_t rio;
        rio_readinit(&rio, sock);
        
        dsm_header_t rep_header;
        if (rio_readn(&rio, &rep_header, sizeof(rep_header)) != sizeof(rep_header)) {
            std::cerr << "[pull_remote_page] Failed to receive PAGE_REP header" << std::endl;
            return;
        }
        
        // Check response type
        if (rep_header.type != DSM_MSG_PAGE_REP) {
            std::cerr << "[pull_remote_page] Unexpected response type: " << static_cast<int>(rep_header.type) << std::endl;
            return;
        }
        
        // Read real_owner_id first
        uint16_t real_owner_id;
        if (rio_readn(&rio, &real_owner_id, sizeof(real_owner_id)) != sizeof(real_owner_id)) {
            std::cerr << "[pull_remote_page] Failed to read real_owner_id" << std::endl;
            return;
        }
        real_owner_id = ntohs(real_owner_id);
        
        // Check unused flag to determine if this is a redirect or data response
        if (rep_header.unused == 0) {
            // Redirect to real owner - continue the loop
            std::cout << "[System information] Redirecting to real owner: node " << real_owner_id << std::endl;
            probowner = real_owner_id;
            continue;
        }
        
        // unused == 1: We received page data - break out of the loop
        // Read page data
        char page_buffer[DSM_PAGE_SIZE];
        if (rio_readn(&rio, page_buffer, DSM_PAGE_SIZE) != DSM_PAGE_SIZE) {
            std::cerr << "[pull_remote_page] Failed to read page data" << std::endl;
            return;
        }
        
        // Make the page writable before copying data
        if (mprotect((void*)page_base, g_page_sz, PROT_READ | PROT_WRITE) != 0) {
            std::cerr << "[pull_remote_page] mprotect failed" << std::endl;
            return;
        }
        
        // Copy page data to memory (equivalent to DMA)
        std::memcpy((void*)page_base, page_buffer, DSM_PAGE_SIZE);

        // Update local PageTable: set this node as the owner of the page
        if (PageTable != nullptr) {
            PageTable->GlobalMutexLock();
            PageRecord* page_rec = PageTable->Find(VPN);
            if (page_rec != nullptr) {
                page_rec->owner_id = PodId;  // Set ourselves as the new owner
            } else {
                // If record doesn't exist, create it
                PageRecord new_record;
                new_record.owner_id = PodId;
                PageTable->Insert(VPN, new_record);
            }
            PageTable->GlobalMutexUnlock();
        }
        
        // Send OWNER_UPDATE to the manager (the original probable owner, not the redirected one)
        std::string manager_ip = GetPodIp(manager_id);
        int manager_port = GetPodPort(manager_id);
        int manager_sock = getsocket(manager_ip, manager_port);
        
        if (manager_sock < 0) {
            std::cerr << "[pull_remote_page] Failed to connect to manager " << manager_id << std::endl;
            // Page data is already loaded, so we can continue
            std::cout << "[System information] Page loaded successfully (OWNER_UPDATE skipped)" << std::endl;
            return;
        }
        
        // Get sequence number for manager connection
        uint32_t update_seq = 1;
        if (SocketTable != nullptr) {
            SocketTable->GlobalMutexLock();
            SocketRecord* record = SocketTable->Find(manager_id);
            if (record != nullptr) {
                update_seq = record->allocate_seq();
            }
            SocketTable->GlobalMutexUnlock();
        }
        
        // Build and send OWNER_UPDATE message
        dsm_header_t update_header = {
            DSM_MSG_OWNER_UPDATE,
            0,                              // unused
            htons(static_cast<uint16_t>(PodId)),  // src_node_id
            htonl(update_seq),              // seq_num
            htonl(sizeof(payload_owner_update_t))  // payload_len
        };
        
        payload_owner_update_t update_payload = {
            htonl(static_cast<uint32_t>(VPN)),    // resource_id (page number)
            htons(static_cast<uint16_t>(PodId))   // new_owner_id (we are the new owner)
        };
        
        // Send OWNER_UPDATE header and payload
        if (::send(manager_sock, &update_header, sizeof(update_header), 0) != sizeof(update_header)) {
            std::cerr << "[pull_remote_page] Failed to send OWNER_UPDATE header" << std::endl;
            return;
        }
        
        if (::send(manager_sock, &update_payload, sizeof(update_payload), 0) != sizeof(update_payload)) {
            std::cerr << "[pull_remote_page] Failed to send OWNER_UPDATE payload" << std::endl;
            return;
        }
        
        // Wait for ACK
        rio_t ack_rio;
        rio_readinit(&ack_rio, manager_sock);
        
        dsm_header_t ack_header;
        if (rio_readn(&ack_rio, &ack_header, sizeof(ack_header)) != sizeof(ack_header)) {
            std::cerr << "[pull_remote_page] Failed to receive ACK for OWNER_UPDATE" << std::endl;
            return;
        }
        
        if (ack_header.type != DSM_MSG_ACK) {
            std::cerr << "[pull_remote_page] Unexpected ACK type: " << static_cast<int>(ack_header.type) << std::endl;
            return;
        }
        
        std::cout << "[System information] Page " << VPN << " loaded successfully, ownership updated" << std::endl;
        return;
    }
}