#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <vector>

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
    // 1. 同步阶段（init的内部实现也调用了barrier函数）
    DSM_MSG_JOIN_REQ      = 0x01,  // 同步请求
    

    // 2. 页面请求流程 (三跳协议)
    DSM_MSG_PAGE_REQ      = 0x10,  // A向B发送页面请求
    DSM_MSG_PAGE_REP      = 0x11,  // 注意：B不会判断自己是prob owner还是real owner,只是判断自己与pagetable里对应页的owner是否一致，
                                   // 一致就发送页面，否则返回页的owner的ID给A，如果页owner是-1，则向0号进程调数据
    
    // 3. 锁请求流程
    DSM_MSG_LOCK_ACQ      = 0x20,  // A向B发送锁请求
    DSM_MSG_LOCK_REP      = 0x21,  // B向A返回锁请求，一并返回的还有无效页号
    DSM_MSG_LOCK_RLS      = 0X22,   // A向B发送锁释放，返回无效页号的list，B会将该list存储在锁表里

    // 4. 维护与确认
    DSM_MSG_OWNER_UPDATE  = 0x30,  // 告知Manager页表所有权已变更

    DSM_MSG_ACK           = 0xFF   // 通用确认：同步确认，lock release确认，页表更新确认
} dsm_msg_type_t;

// ================= 通用协议头 (12字节) =================
typedef struct {
    uint8_t  type;           // dsm_msg_type_t
    uint8_t  unused;         // 保留位 （消息复用时的区分）
    uint16_t src_node_id;    // 发送方ID (-1/255 表示未知)
    uint32_t seq_num;        // 序列号 (处理乱序/丢包)
    uint32_t payload_len;    // 后续负载长度 (不含包头)
} __attribute__((packed)) dsm_header_t;

// [DSM_MSG_PAGE_REQ] Requestor -> Manager
typedef struct {
    uint32_t page_index;        // 请求的全局页号
} __attribute__((packed)) payload_page_req_t;

// [DSM_MSG_PAGE_REP] Manager -> Requestor
typedef struct {
    uint16_t real_owner_id;
    char pagedata[DSM_PAGE_SIZE];
} __attribute__((packed)) payload_page_rep_t;

// [DSM_MSG_LOCK_ACQ]  Requestor -> Manager
typedef struct {
    uint32_t lock_id;           // 锁 ID
} __attribute__((packed)) payload_lock_req_t;

// [DSM_MSG_LOCK_REP] Manager -> Requestor (授予锁)
typedef struct {
    uint32_t invalid_set_count; 
    //跟上失效的数组
    uint32_t invalid_page_list[0]; // 无效页号数组（动态大小）
    
} __attribute__((packed)) payload_lock_rep_t;

// [DSM_MSG_LOCK_RLS] LockOwner -> Manager (释放锁)
typedef struct {
    uint32_t invalid_set_count; 
    uint32_t lock_id;
    //跟上失效的数组
    uint32_t invalid_page_list[0]; // 无效页号数组（动态大小）
} __attribute__((packed)) payload_lock_rls_t;

// [DSM_MSG_OWNER_UPDATE] RealOwner -> Manager
typedef struct {
    uint32_t resource_id;    // 页号
    uint16_t new_owner_id;   // 页面最新副本在哪里
} __attribute__((packed)) payload_owner_update_t;




#if defined(__cplusplus)
}
#endif

#endif /* NET_PROTOCOL_H */
