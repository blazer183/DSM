#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#if defined(__cplusplus)
extern "C" {
#endif


#define DSM_PAGE_SIZE 4096
//！！！！！重要！！！！ 本文件中的函数完全来自caspp page 630-631

/* Constants ------------------------------------------------------------- */
#define RIO_BUFSIZE 8192  /* rio缓冲区大小 */
/* Enumerations ---------------------------------------------------------- */

/* Structures ------------------------------------------------------------ */
typedef struct {
    int rio_fd;                /* 描述符 */
    int rio_cnt;               /* 未读字节数 */
    char *rio_bufptr;          /* 下一个未读字节的指针 */
    char rio_buf[RIO_BUFSIZE]; /* 内部缓冲区 */
} rio_t;
/* Helpers --------------------------------------------------------------- */

/* API ------------------------------------------------------------------- */
void rio_readinit(rio_t *rp, int fd);
ssize_t rio_readline(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t rio_readn(rio_t *rp, void *usrbuf, size_t n);



//报文规范


// ================= 消息类型枚举 =================
typedef enum {
    // 1. 初始化阶段
    DSM_MSG_JOIN_REQ      = 0x01,  // 
    DSM_MSG_JOIN_ACK      = 0x02,  // 

    // 2. 页面请求流程 (三跳协议)
    DSM_MSG_PAGE_REQ      = 0x10,  // A向B发送页面请求
    DSM_MSG_PAGE_REP      = 0x11,  // 注意：B不会判断自己是prob owner还是real owner,只是判断自己与pagetable里对应页的owner是否一致，
                                   // 一致就发送页面，否则返回页的owner的ID给A，如果页owner是-1，则向0号进程调数据
       
    // 3. 锁请求流程
    DSM_MSG_LOCK_ACQ      = 0x20,  // 同上
    DSM_MSG_LOCK_REP      = 0x21,  // 同上
    
    // 4. 维护与确认
    DSM_MSG_OWNER_UPDATE  = 0x30,  // 告知Manager所有权已变更，通过保留位区分页/锁
    DSM_MSG_ACK           = 0xFF   // 通用确认 (用于同步)
} dsm_msg_type_t;

// ================= 通用协议头 (12字节) =================
typedef struct {
    uint8_t  type;           // dsm_msg_type_t
    uint8_t  unused;         // 保留位 (页/锁判断；realowner/data判断）
    uint16_t src_node_id;    // 发送方ID (-1/255 表示未知)
    uint32_t seq_num;        // 序列号 (处理乱序/丢包)
    uint32_t payload_len;    // 后续负载长度 (不含包头)
} __attribute__((packed)) dsm_header_t;






// ================= 详细负载定义 (Payloads) =================

// ---------------- A. 初始化模块 ----------------



// [DSM_MSG_JOIN_ACK]
typedef struct {
    uint16_t assigned_node_id;  // 分配给新人的 ID
    uint16_t node_count;        // 总节点数 (用于 Hash 计算)
    uint64_t dsm_mem_size;      // 共享内存总大小
} __attribute__((packed)) payload_join_ack_t;


// ---------------- B. 页面管理模块 ----------------

// [DSM_MSG_PAGE_REQ] Requestor -> Manager
typedef struct {
    uint32_t page_index;        // 请求的全局页号
} __attribute__((packed)) payload_page_req_t;

// [DSM_MSG_PAGE_REP] Manager -> RealOwner
// 含义："Node X 想要 page_index，数据在你这，你发给它"
typedef struct {
    uint32_t page_index;        // 页号
    uint16_t requester_id;      // 真正想要数据的节点ID (RealOwner要把数据发给它)
} __attribute__((packed)) payload_page_rep_t;

// [DSM_MSG_PAGE_DATA] RealOwner -> Requestor
// 这个包通常不需要 struct 定义，直接 Header + 4096字节数据
// 如果需要元数据，可以用 unused 字段


// ---------------- C. 锁管理模块 ----------------

// [DSM_MSG_LOCK_ACQ] / [DSM_MSG_LOCK_REL] Requestor -> Manager
typedef struct {
    uint32_t lock_id;           // 锁 ID
} __attribute__((packed)) payload_lock_req_t;

// [DSM_MSG_LOCK_REP] Manager -> Requestor (授予锁)
typedef struct {
    uint32_t lock_id;
    uint32_t invalid_set_count; // Scope Consistency: 需要失效的页数量
    // 紧接着后面可以是 invalid_set 列表
    uint32_t realowner;

} __attribute__((packed)) payload_lock_rep_t;


// ---------------- D. 维护与确认 ----------------

// [DSM_MSG_OWNER_UPDATE] RealOwner -> Manager
typedef struct {
    uint32_t resource_id;    // 页号或锁ID
    uint16_t new_owner_id;   // 资源现在归谁了
} __attribute__((packed)) payload_owner_update_t;

// [DSM_MSG_ACK]
typedef struct {
    uint32_t target_seq;     // 确认是对哪个请求的回应
    uint8_t  status;         // 0=OK, 1=Fail
} __attribute__((packed)) payload_ack_t;


#if defined(__cplusplus)
}
#endif

#endif /* NET_PROTOCOL_H */
