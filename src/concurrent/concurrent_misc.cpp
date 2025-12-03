#include <iostream>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h> // ������������� mprotect
#include <sys/types.h>
#include <sys/socket.h>
// ����ͷ�ļ�
#include "concurrent/concurrent_core.h"
#include "net/protocol.h"
#include "dsm.h"
#include "os/table_base.hpp"
// ��־λԼ�� (��Ӧ dsm_header_t.unused)
#define FLAG_UPDATE_PAGE 0x00
#define FLAG_UPDATE_LOCK 0x01

// ============================================================================
// 1. ��������Ȩ����
// �߼����յ� Worker ��֪ͨ������ Directory (ҳ��/����) �е� Owner �ֶ�
// ============================================================================
void process_owner_update(int sock, const dsm_header_t& head, const payload_owner_update_t& body) {
    

    uint32_t res_id = body.resource_id;
    int new_owner = body.new_owner_id;

    // ����ͷ�� unused �ֶ��ж��� ҳ ���� ��
    // Լ����0 = Page, 1 = Lock
    if (head.unused == FLAG_UPDATE_LOCK) {
        // --- �������� ---
        //����ס����
        ::LockTable->LockAcquire();
        LockRecord rec(new_owner); // ʹ�ù��캯����ʼ�� owner
        // ���ﲻȷ���Ƿ� locked��ͨ�� update ��������ζ�����˳�����
        rec.locked = true; 

        // ���Ը��£���������������
        if (!state.lock_table.Update(res_id, rec)) {
            state.lock_table.Insert(res_id, rec);
        }
        std::cout << "[Directory] Lock " << res_id << " moved to Node " << new_owner << std::endl;
        //��������
        ::LockTable->LockRelease();
    } else {
        // --- ����ҳ�� ---
        // ע�⣺ҳ���� Key �������ַ������ page_index
        // ������Ҫ�� page_index ת�ص�ַ
        uintptr_t page_addr = (uintptr_t)state.shared_mem_base + (res_id * DSM_PAGE_SIZE);
        //��סҳ��
        ::PageTable->LockAcquire();
        PageRecord rec;
        rec.owner_id = new_owner;

        if (!state.page_table.Update(page_addr, rec)) {
            state.page_table.Insert(page_addr, rec);
        }
        std::cout << "[Directory] Page " << res_id << " moved to Node " << new_owner << std::endl;
        //����ҳ��
        ::PageTable->LockRelease();
    }
}

