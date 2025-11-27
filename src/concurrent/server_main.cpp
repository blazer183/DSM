// src/concurrent/server_main.cpp
#include <iostream>
#include <vector>
#include "dsm_state.hpp"

// 声明外部函数
void dsm_start_daemon(int port);

int main() {
    // 1. 初始化模拟的共享内存
    auto& state = DSMState::GetInstance();
    state.my_node_id = 1; // 假设我是 Node 1
    
    // 分配 1MB 内存模拟 DSM 共享区
    state.shared_mem_base = malloc(1024 * 1024); 
    if (!state.shared_mem_base) {
        std::cerr << "Memory allocation failed" << std::endl;
        return -1;
    }

    // 2. 启动服务 (监听 8080)
    std::cout << "Starting DSM Server Node 1..." << std::endl;
    dsm_start_daemon(8080);

    // 释放内存(实际永远跑不到这)
    free(state.shared_mem_base);
    return 0;
}