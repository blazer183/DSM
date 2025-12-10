#pragma once

#include <string>
#include <vector>
#include "dsm.h"
#include "os/page_table.h"
#include "os/lock_table.h"
#include "os/table_base.hpp"// #include "os/bind_table.h" // 如果需要

// ==========================================
// 1. 全局变量声明 (模拟 dsm.c 中的定义)
// ==========================================
extern void* SharedAddrBase;
extern class PageTable* PageTable;
extern class LockTable* LockTable;
// extern BindTable* BindTable; 

// ==========================================
// 2. 模拟辅助函数声明
// ==========================================

// 初始化模拟环境：分配内存、创建表、预置数据(Page0绑定文件)
void mock_init(int node_id); 

// 销毁环境
void mock_cleanup();

// 模拟获取当前节点ID (从命令行参数读入)
void set_dsm_nodeid(int id);
// int dsm_getnodeid(); // 这个函数通常在 dsm.h 里声明了，我们去 cpp 里实现它

// 模拟获取 IP 和 端口
// 逻辑：所有节点 IP 都是 127.0.0.1，端口是 8000 + node_id
std::string GetPodIp(int node_id);
int GetPodPort(int node_id);

// 此时需要用宏或者链接技巧，让你的源码调用 GetPodIp 时实际调用 MockGetPodIp
// 或者直接在 mock_env.cpp 里实现 GetPodIp