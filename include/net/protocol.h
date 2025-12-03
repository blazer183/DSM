#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#if defined(__cplusplus)
extern "C" {
#endif


#define DSM_PAGE_SIZE 4096
//������������Ҫ�������� ���ļ��еĺ�����ȫ����caspp page 630-631

/* Constants ------------------------------------------------------------- */
#define RIO_BUFSIZE 8192  /* rio��������С */
/* Enumerations ---------------------------------------------------------- */

/* Structures ------------------------------------------------------------ */
typedef struct {
    int rio_fd;                /* ������ */
    int rio_cnt;               /* δ���ֽ��� */
    char *rio_bufptr;          /* ��һ��δ���ֽڵ�ָ�� */
    char rio_buf[RIO_BUFSIZE]; /* �ڲ������� */
} rio_t;
/* Helpers --------------------------------------------------------------- */

/* API ------------------------------------------------------------------- */
void rio_readinit(rio_t *rp, int fd);
ssize_t rio_readline(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t rio_readn(rio_t *rp, void *usrbuf, size_t n);



//���Ĺ淶


// ================= ��Ϣ����ö�� =================
typedef enum {
    // 1. ��ʼ���׶�
    DSM_MSG_JOIN_REQ      = 0x01,  // �½ڵ�������룬��Ҫͳ�ƽڵ�����������ID��ȷ����������ַ
    DSM_MSG_JOIN_ACK      = 0x02,  // Leader����������Ϣ(ID, ��ַ)��Ҳ���Ǻ������ذ���ID�ͻ�ַ����ø��ص�ǰ8�ֽ���ID����8�ֽ��ǻ�ַ��Ĭ�Ϸ���0x4000000000

    // 2. ҳ���������� (����Э��)
    DSM_MSG_PAGE_REQ      = 0x10,  // A��B����ҳ������
    DSM_MSG_PAGE_REP      = 0x11,  // ע�⣺B�����ж��Լ���prob owner����real owner,ֻ���ж��Լ���pagetable���Ӧҳ��owner�Ƿ�һ�£�
                                   // һ�¾ͷ���ҳ�棬���򷵻�ҳ��owner��ID��A�����ҳowner��-1������0�Ž��̵�����
       
    // 3. ����������
    DSM_MSG_LOCK_ACQ      = 0x20,  // ͬ��
    DSM_MSG_LOCK_REP      = 0x21,  // ͬ��
    
    // 4. ά����ȷ��
    DSM_MSG_OWNER_UPDATE  = 0x30,  // ��֪Manager����Ȩ�ѱ����ͨ������λ����ҳ/��
    DSM_MSG_ACK           = 0xFF   // ͨ��ȷ�� (����ͬ��)
} dsm_msg_type_t;

// ================= ͨ��Э��ͷ (12�ֽ�) =================
typedef struct {
    uint8_t  type;           // dsm_msg_type_t
    uint8_t  unused;         // ����λ (ҳ/���жϣ�realowner/data�жϣ�
    uint16_t src_node_id;    // ���ͷ�ID (-1/255 ��ʾδ֪)
    uint32_t seq_num;        // ���к� (��������/����)
    uint32_t payload_len;    // �������س��� (������ͷ)
} __attribute__((packed)) dsm_header_t;


// [DSM_MSG_JOIN_REQ]
typedef struct {
    uint16_t listen_port;    // ���� Leader �����ĸ��˿ڼ���
} __attribute__((packed)) payload_join_req_t;

// [DSM_MSG_JOIN_ACK]
typedef struct {
    uint16_t assigned_node_id;  // ��������˵� ID
    uint16_t node_count;        // �ܽڵ��� (���� Hash ����)
    uint64_t dsm_mem_size;      // �����ڴ��ܴ�С
} __attribute__((packed)) payload_join_ack_t;


// ---------------- B. ҳ�����ģ�� ----------------

// [DSM_MSG_PAGE_REQ] Requestor -> Manager
typedef struct {
    uint32_t page_index;        // �����ȫ��ҳ��
} __attribute__((packed)) payload_page_req_t;

// [DSM_MSG_PAGE_REP] Manager -> RealOwner
// ���壺"Node X ��Ҫ page_index�����������⣬�㷢����"
typedef struct {
    uint32_t page_index;        // ҳ��
    uint16_t requester_id;      // ������Ҫ���ݵĽڵ�ID (RealOwnerҪ�����ݷ�����)
} __attribute__((packed)) payload_page_rep_t;

// [DSM_MSG_PAGE_DATA] RealOwner -> Requestor
// �����ͨ������Ҫ struct ���壬ֱ�� Header + 4096�ֽ�����
// �����ҪԪ���ݣ������� unused �ֶ�


// ---------------- C. ������ģ�� ----------------

// [DSM_MSG_LOCK_ACQ] / [DSM_MSG_LOCK_REL] Requestor -> Manager
typedef struct {
    uint32_t lock_id;           // �� ID
} __attribute__((packed)) payload_lock_req_t;

// [DSM_MSG_LOCK_REP] Manager -> Requestor (������)
typedef struct {
    uint32_t lock_id;
    uint32_t invalid_set_count; // Scope Consistency: ��ҪʧЧ��ҳ����
    // �����ź�������� invalid_set �б�
    uint32_t realowner;

} __attribute__((packed)) payload_lock_rep_t;


// ---------------- D. ά����ȷ�� ----------------

// [DSM_MSG_OWNER_UPDATE] RealOwner -> Manager
typedef struct {
    uint32_t resource_id;    // ҳ�Ż���ID
    uint16_t new_owner_id;   // ��Դ���ڹ�˭��
} __attribute__((packed)) payload_owner_update_t;

// [DSM_MSG_ACK]
typedef struct {
    uint32_t target_seq;     // ȷ���Ƕ��ĸ�����Ļ�Ӧ
    uint8_t  status;         // 0=OK, 1=Fail
} __attribute__((packed)) payload_ack_t;


#if defined(__cplusplus)
}
#endif

#endif /* NET_PROTOCOL_H */
