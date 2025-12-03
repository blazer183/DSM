#include <iostream>
#include <mutex>
#include <cstring>
#include <sys/mman.h> // ������������� mprotect
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

    // ��������ҳ��ı��������ַ
    uintptr_t page_addr_int = (uintptr_t)state.shared_mem_base + (body.page_index * DSM_PAGE_SIZE);
    void* page_ptr = (void*)page_addr_int;

    // 1. ���ȷ������Ȩ
    auto* record = ::PageTable->Find(page_addr_int);
    
    // ��ȡ��ǰ Owner (-1 ��ʾδ���䣬Ĭ�Ϲ� Node 0)
    int current_owner = record ? record->owner_id : -1;
    // �����¼�����ڣ������� Manager (Node 0)����Ĭ�Ͼ����ҵ�
    if (current_owner == -1 && dsm_getnodeid() == 0) {
        current_owner = 0;
    }

    // ============================================================
    // Case A: ���� Owner (����������)
    // ============================================================
    if (current_owner == state.my_node_id) {
        std::cout << "[DSM] Serving Page " << body.page_index << " to Node " << head.src_node_id << std::endl;

        // ���ؼ����衿OS ��������
        // �����ǰҳ���� PROT_NONE (������Ϊ���˱������������Լ�Ҳûд��)��
        // ���������ʱ������Ȩ�ޣ����� rio_writen ��ȡ�ڴ�ʱ Server �������
        mprotect(page_ptr, DSM_PAGE_SIZE, PROT_READ);

        // 1. ����ͷ����ʹ�� PAGE_REP����� unused=1 (��������)
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP;
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = DSM_PAGE_SIZE; // �̶� 4096
        rep_head.unused = 1; // <--- 1 ��ʾ������ Data

        // 2. ����ͷ��
        rio_writen(sock, &rep_head, sizeof(rep_head));

        // 3. �������� (4KB)
        rio_writen(sock, page_ptr, DSM_PAGE_SIZE);

        // 4. �ƽ�����Ȩ (����Э�飬���ͼ��ƽ�)
        // ���±���ҳ��
        if (record) {
            record->owner_id = head.src_node_id;
        } else {
            PageRecord new_rec; 
            new_rec.owner_id = head.src_node_id;
            state.page_table.Insert(page_addr_int, new_rec);
        }

        // ���ؼ����衿���ñ��ز��ɷ���
        // ��Ȼ���ݸ��˱��ˣ��ұ��ؾ�ʧЧ��(Invalidate)����ֹ������һ��
        mprotect(page_ptr, DSM_PAGE_SIZE, PROT_NONE);

        std::cout << "  -> Data sent. Ownership transferred." << std::endl;
    } 
    // ============================================================
    // Case B: �Ҳ��� Owner -> �ض��� (Redirect)
    // ============================================================
    else if (current_owner != -1 && current_owner != ::NodeId) {
        // ����鲻����¼����Ҳ����Manager���Ǵ����Ӧ����Manager������Hash�������ProbOwner
        
        std::cout << "[DSM] Redirect Page " << body.page_index << ": Me(" << ::NodeId 
                  << ") -> RealOwner(" << current_owner << ")" << std::endl;

        // 1. ����ͷ����PAGE_REP����� unused=0 (�����ض���)
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP; 
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_page_rep_t);
        rep_head.unused = 0; // <--- 0 ��ʾ������ Redirect Info

        // 2. ���� Body
        payload_page_rep_t rep_body;
        rep_body.page_index = body.page_index;
        rep_body.requester_id = current_owner; // �����ֶδ� RealOwner ID

        // 3. ����
        
        send(sock, (const char*)&rep_head, sizeof(rep_head), 0);
        send(sock, (const char*)&rep_body, sizeof(rep_body), 0);
    }
    // ============================================================
    // Case C: pageδ���䣬������node 0 -> Ĭ������Owner
    // ============================================================
    else if (current_owner == -1 && ::NodeId == 0) {
        std::cout << "[DSM] Serving Page " << body.page_index << " to Node " << head.src_node_id << " (Default Owner)" << std::endl;

        // ���ؼ����衿OS ��������
        mprotect(page_ptr, DSM_PAGE_SIZE, PROT_READ);

        // 1. ����ͷ����ʹ�� PAGE_REP����� unused=1 (��������)
        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP;
        rep_head.src_node_id = dsm_getnodeid() ;
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = DSM_PAGE_SIZE; // �̶� 4096
        rep_head.unused = 1; // <--- 1 ��ʾ������ Data

        // 2. ����ͷ��
        send(sock, &rep_head, sizeof(rep_head), 0);

        // 3. �������� (4KB)
        send(sock, page_ptr, DSM_PAGE_SIZE, 0);

        // 4. �ƽ�����Ȩ (����Э�飬���ͼ��ƽ�)
        // ���±���ҳ��
        if (record) {
            record->owner_id = head.src_node_id;
        } else {
            PageRecord new_rec; 
            new_rec.owner_id = head.src_node_id;
            state.page_table.Insert(page_addr_int, new_rec);
        }

        // ���ؼ����衿���ñ��ز��ɷ���
        mprotect(page_ptr, DSM_PAGE_SIZE, PROT_NONE);

        std::cout << "  -> Data sent. Ownership transferred." << std::endl;

    }

    ::PageTable->LockRelease();
}


