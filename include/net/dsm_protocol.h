#ifndef DSM_PROTOCOL_H
#define DSM_PROTOCOL_H

#include <stdint.h>

// ================= 常量定义 =================
#define DSM_PAGE_SIZE 4096      // 标准页大小 4KB
#define DSM_HEADER_SIZE 12      // 头部固定长度

// ================= 消息类型 (Message Types) =================
typedef enum {
    // --- 1. 初始化阶段 ---
    DSM_MSG_JOIN_REQ      = 0x01,  // [Join] 新节点申请加入
    DSM_MSG_JOIN_ACK      = 0x02,  // [Join] Leader返回配置(ID, Size)

    // --- 2. 页面请求流程 (三跳协议) ---
    // 流程: Requestor -(REQ)-> Manager -(REP)-> RealOwner -(DATA)-> Requestor
    DSM_MSG_PAGE_REQ      = 0x10,  // Requestor -> Manager: "我要读/写第X页"
    DSM_MSG_PAGE_REP      = 0x11,  // Manager -> RealOwner: "把第X页发给 Requestor"
    DSM_MSG_PAGE_DATA     = 0x12,  // RealOwner -> Requestor: 携带真正的 4KB 数据
       
    // --- 3. 锁请求流程 ---
    DSM_MSG_LOCK_ACQ      = 0x20,  // Requestor -> Manager: 申请锁
    DSM_MSG_LOCK_REP      = 0x21,  // Manager -> Requestor: 授予锁 (可能携带Invalid集合)
    DSM_MSG_LOCK_REL      = 0x22,  // Requestor -> Manager: 释放锁
    
    // --- 4. 维护与确认 ---
    DSM_MSG_OWNER_UPDATE  = 0x30,  // RealOwner -> Manager: "数据我给别人了，更新目录表"
    DSM_MSG_ACK           = 0xFF   // 通用确认 (用于同步)
} dsm_msg_type_t;


// ================= 通用协议头 (12字节) =================
// 所有报文都以此开头
typedef struct {
    uint8_t  type;           // dsm_msg_type_t
    uint8_t  unused;         // 保留/标志位 (0=Fail, 1=Success 等)
    uint16_t src_node_id;    // 发送方ID
    uint32_t seq_num;        // 序列号
    uint32_t payload_len;    // 后续负载长度 (不含这12字节头部)
} __attribute__((packed)) dsm_header_t;


// ================= 详细负载定义 (Payloads) =================

// ---------------- A. 初始化模块 ----------------

// [DSM_MSG_JOIN_REQ]
typedef struct {
    uint16_t listen_port;    // 告诉 Leader 我在哪个端口监听
} __attribute__((packed)) payload_join_req_t;

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

#endif // DSM_PROTOCOL_H