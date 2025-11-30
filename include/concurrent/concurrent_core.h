#ifndef DSM_STATE_HPP
#define DSM_STATE_HPP

#include <mutex>
#include <memory>
#include <iostream>
#include <condition_variable> 
#include <vector>
#include "os/page_table.h"
#include "os/lock_table.h"  // 注意文件名大小写要和实际文件一致
#include "os/bind_table.h"
#include "os/table_base.hpp"
#include "net/protocol.h" 

// 全局 DSM 状态管理类
struct DSMState {
    // 互斥锁：保护以下所有表的操作
    std::mutex state_mutex;

    // [新增] 同步机制：用于 Client 线程阻塞等待 Daemon 收到回复
    std::condition_variable page_cond;      // 等待页面数据回来
    std::condition_variable lock_cond;      // 等待锁申请成功
    std::condition_variable join_cond;      // 等待加入集群成功

    // [新增] 状态标志位：配合条件变量使用，防止虚假唤醒
    volatile bool page_req_finished = false; // 页面请求是否完成
    volatile bool lock_granted = false;      // 锁是否拿到
    volatile bool join_finished = false;     // 加入是否成功
    // [新增] 记录当前 Client 正在请求哪个页面
    volatile uint32_t pending_page_index = 0; 

    // [新增] 标记这次交互是否拿到了数据 (true=拿到数据, false=被重定向需重试)
    volatile bool data_received = false;

    // 三大核心表
    PageTable page_table;
    LockTable lock_table;
    BindTable bind_table;

    // 自身节点信息
    int my_node_id = -1;             // 我的 ID
    int node_count = 1;              // [新增] 集群总节点数 (用于 Hash 计算 ProbOwner)
    void* shared_mem_base = nullptr; // 共享内存基址
    uint64_t total_mem_size = 0;     // 共享内存总大小

    int next_node_id = 2;            // Manager 计数器 (仅Manager用)
    
    // [新增] 保存 Manager 的连接 Socket，方便 Client 发送请求
    // (如果是 Hash 算法，可能需要维护一个 vector<int> sock_map 存所有节点的连接)
    // [新增] 网络锁：专门保护 node_sockets
    std::mutex net_mutex;
    int manager_sock = -1; 
    std::vector<int> node_sockets;   // 建议：缓存到其他节点的 Socket，避免每次重连

    // 单例模式获取
    static DSMState& GetInstance() {
        static DSMState instance;
        return instance;
    }

private:
    DSMState() : page_table(1024) {
        // 预分配 100 个节点的位置，初始值为 -1 (Invalid Socket)
        // 这样可以直接用 node_sockets[id] 访问而不会越界
        node_sockets.resize(100, -1); 
    } 
};

// =========================================================================
// 1. 消息处理函数 (Passive Handlers)
// 每一个 process_ 函数对应 dsm_msg_type_t 中的一个枚举值
// 只要收到该类型的包，就在监听线程的 switch case 中调用这些函数
// =========================================================================

// [0x01] DSM_MSG_JOIN_REQ
// 接收者：Manager (Leader)
// 作用：记录新节点，分配ID，准备回复 ACK
void process_join_req(int sock, const dsm_header_t& head, const payload_join_req_t& body);

// [0x02] DSM_MSG_JOIN_ACK
// 接收者：Client (New Node)
// 作用：新人收到 Leader 的确认，初始化本地内存基址、ID
void process_join_ack(int sock, const dsm_header_t& head, const payload_join_ack_t& body);

// [0x10] DSM_MSG_PAGE_REQ
// 接收者：Manager 或 Owner
// 作用：
// 1. 如果我是 Owner：直接发回 DSM_MSG_PAGE_REP (带数据, unused=1)
// 2. 如果我不是：发回 DSM_MSG_PAGE_REP (带重定向ID, unused=0)
void process_page_req(int sock, const dsm_header_t& head, const payload_page_req_t& body);

// [0x11] DSM_MSG_PAGE_REP
// 接收者：Requestor (发起请求的 A)
// 逻辑：这是 "三跳/重定向" 协议的回调核心。
// 需要判断 head.unused 字段：
// - Case A (unused == 1): 负载是数据。将 raw_payload (4KB) 写入共享内存，更新页表，唤醒等待线程。
// - Case B (unused == 0): 负载是信息。解析出 real_owner_id，更新本地页表，然后向 real_owner 发起新的 REQ。
// 注意：参数 raw_payload 指向 buffer 起始位置，函数内部根据情况强转类型。
void process_page_rep(int sock, const dsm_header_t& head, const char* raw_payload);

// [0x20] DSM_MSG_LOCK_ACQ
// 接收者：Manager
// 作用：查 LockTable，如果空闲则授予 (发LOCK_REP)，如果占用则加入队列
void process_lock_acq(int sock, const dsm_header_t& head, const payload_lock_req_t& body);

// [0x21] DSM_MSG_LOCK_REP
// 接收者：Requestor
// 作用：收到锁授予通知 -> 处理 invalid_set -> 唤醒等待锁的线程
void process_lock_rep(int sock, const dsm_header_t& head, const payload_lock_rep_t& body);

// [0x30] DSM_MSG_OWNER_UPDATE
// 接收者：Manager
// 作用：收到 RealOwner 的通知，更新 Directory 中的 owner_id
void process_owner_update(int sock, const dsm_header_t& head, const payload_owner_update_t& body);

// [0xFF] DSM_MSG_ACK
// 接收者：通用
// 作用：处理通用的确认回执（例如 Barrier 同步完成确认等）
void process_ack(int sock, const dsm_header_t& head, const payload_ack_t& body);


// =========================================================================
// 2. 监听服务入口 (Daemon)
// =========================================================================
void dsm_start_daemon(int port);
void peer_handler(int connfd); 
// =========================================================================
// 3. 客户端主动请求函数 (Active Requests) - [这是你这次必须要加的]
// 这些函数运行在 应用程序线程 或 信号处理函数 中
// =========================================================================

/**
 * @brief 系统初始化
 * 连接 Manager，发送 Join 请求，阻塞等待 Join ACK
 */
void concurrent_system_init(int my_port, const char* manager_ip, bool is_manager);

/**
 * @brief 缺页请求入口 (Client核心逻辑)
 * 1. 计算 ProbOwner = page_index % state.node_count
 * 2. 如果 ProbOwner == Me，说明我是管理者但没数据(PageFault)，直接找所有者或者报错
 * 3. 否则，向 ProbOwner 发送 DSM_MSG_PAGE_REQ
 * 4. 阻塞等待 condition_variable (直到 process_page_rep 唤醒)
 */
void dsm_request_page(uint32_t page_index);

/**
 * @brief 申请锁
 * 1. 构造 DSM_MSG_LOCK_ACQ
 * 2. 发给 Manager
 * 3. 阻塞等待 condition_variable (直到 process_lock_rep 唤醒)
 */
void dsm_request_lock(uint32_t lock_id);

#endif // DSM_STATE_HPP
