#pragma once

#include "net/protocol.h"

// 建立连接的辅助函数
int connect_to_node(int target_node_id);

// ==========================================
// 测试用：发送请求函数
// ==========================================

// 发送 Join 请求
void request_join(int target_node);

// 发送 Page 请求 (模拟缺页)
void request_page(int target_node, int page_index);

// 发送 Lock 请求
void request_lock(int target_node, int lock_id);

// 【新增】发送 Lock Release 请求
void request_lock_release(int target_node, int lock_id);

// 发送 Owner Update 请求
void send_owner_update(int target_node, uint32_t page_index, int new_owner);

// 【新增】并发压力测试函数
// target: 目标节点
// page_idx: 请求的页号
// count: 请求次数
void run_stress_test(int target, int page_idx, int count);