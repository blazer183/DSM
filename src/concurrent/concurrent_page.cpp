#include <iostream>
#include <mutex>
#include <cstring>
#include <sys/mman.h> // �������������? mprotect
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "concurrent/concurrent_core.h"
#include "net/protocol.h"
#include "dsm.h"
#include "os/table_base.hpp"
// �����꣺ҳ����
#define PAGE_ALIGN_DOWN(addr) ((void*)((uintptr_t)(addr) & ~(DSM_PAGE_SIZE - 1)))

void process_page_req(int sock, const dsm_header_t& head, const payload_page_req_t& body) {
    ::PageTable->LockAcquire();

    uintptr_t page_addr_int = (uintptr_t)state.shared_mem_base + (body.page_index * DSM_PAGE_SIZE);
    void* page_ptr = (void*)page_addr_int;

    auto* record = ::PageTable->Find(page_addr_int);
    
    int current_owner = record ? record->owner_id : -1;
    if (current_owner == -1 && dsm_getnodeid() == 0) {
        current_owner = 0;
    }

    // Case A: owner_id == PodId
      if (current_owner == dsm_getnodeid()) {
        std::cout << "[DSM] Serving Page " << body.page_index << " to Node " << head.src_node_id << std::endl;
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP;
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = DSM_PAGE_SIZE; 
        rep_head.unused = 1; // 

        send(sock, (const char*)&rep_head, sizeof(rep_head));
        send(sock, (const char*)page_ptr, DSM_PAGE_SIZE);
    } 
    // Case B: owner_id != PodId
    else if ( current_owner != dsm_getnodeid()) {
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP; 
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_page_rep_t);
        rep_head.unused = 0; 

        payload_page_rep_t rep_body;
        rep_body.page_index = body.page_index;
        rep_body.requester_id = current_owner; 

        send(sock, (const char*)&rep_head, sizeof(rep_head), 0);
        send(sock, (const char*)&rep_body, sizeof(rep_body), 0);
    }
    // Case C: owner_id = -1 && PodId = 0
    else if (current_owner == -1 && dsm_getnodeid() == 0) {
      
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP;
        rep_head.src_node_id = dsm_getnodeid() ;
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = DSM_PAGE_SIZE; // �̶� 4096
        rep_head.unused = 1; 

        send(sock, &rep_head, sizeof(rep_head), 0);
        send(sock, page_ptr, DSM_PAGE_SIZE, 0);
    }
    // Case D: owner_id = -1 && PodId != 0
    else if (current_owner == -1 && dsm_getnodeid() != 0) {
      
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP;
        rep_head.src_node_id = dsm_getnodeid() ;
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = DSM_PAGE_SIZE; // �̶� 4096
        rep_head.unused = 1; 

        send(sock, &rep_head, sizeof(rep_head), 0);
        send(sock, page_ptr, DSM_PAGE_SIZE, 0);
    }

    ::PageTable->LockRelease();
}


