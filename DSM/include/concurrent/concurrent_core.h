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

// [0x01] DSM_MSG_JOIN_REQ
// 接收者：Manager (Leader)
// 作用：记录新节点，分配ID，准备回复 ACK
void process_join_req(int sock, const dsm_header_t& head, const payload_join_req_t& body);

// [0x10] DSM_MSG_PAGE_REQ
// 接收者：Manager 或 Owner
// 作用：
// 1. 如果我是 Owner：直接发回 DSM_MSG_PAGE_REP (带数据, unused=1)
// 2. 如果我不是：发回 DSM_MSG_PAGE_REP (带重定向ID, unused=0)
void process_page_req(int sock, const dsm_header_t& head, const payload_page_req_t& body);

// [0x20] DSM_MSG_LOCK_ACQ
// 接收者：Manager
// 作用：查 LockTable，如果空闲则授予 (发LOCK_REP)，如果占用则加入队列
void process_lock_acq(int sock, const dsm_header_t& head, const payload_lock_req_t& body);

// [0x30] DSM_MSG_OWNER_UPDATE
// 接收者：Manager
// 作用：收到 RealOwner 的通知，更新 Directory 中的 owner_id
void process_owner_update(int sock, const dsm_header_t& head, const payload_owner_update_t& body);

// =========================================================================
// 2. 监听服务入口 (Daemon)
// =========================================================================
void dsm_start_daemon(int port);
void peer_handler(int connfd); 


#endif // DSM_STATE_HPP