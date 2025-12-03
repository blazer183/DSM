#include <iostream>
#include <unistd.h>
#include "concurrent/concurrent_core.h" // 包含核心 API

int main() {
    std::cout << "========== INTEGRATION TEST: Long Connection ==========" << std::endl;

    // 1. 系统初始化
    // 模拟自己是 Worker (Node 2)，监听端口 9527，Manager 在 127.0.0.1
    // 这一步内部会建立到 Manager 的长连接，并存入 node_sockets[1]
    std::cout << "[Step 1] Initializing System..." << std::endl;
    // 初始化系统，启动 Worker 线程
    
    (9527, "127.0.0.1", false);

    // 给一点时间让后台线程跑起来
    sleep(1);

    // 2. 发起第一次请求 (请求 Page 0)
    // 根据 Hash 算法，或者默认归属，这通常会发给 Manager
    std::cout << "\n[Step 2] First Request (Page 0)..." << std::endl;
    dsm_request_page(0);
    
    std::cout << ">>> Page 0 Acquired! <<<" << std::endl;

    // 3. 发起第二次请求 (请求 Page 1)
    // 【关键验证点】这里应该复用上一步建立的连接，而不是建立新连接
    std::cout << "\n[Step 3] Second Request (Page 1)..." << std::endl;
    dsm_request_page(1);

    std::cout << ">>> Page 1 Acquired! <<<" << std::endl;

    // 4. 发起锁请求
    // 【关键验证点】这里也应该复用连接
    std::cout << "\n[Step 4] Requesting Lock 100..." << std::endl;
    dsm_request_lock(100);
    
    std::cout << ">>> Lock 100 Acquired! <<<" << std::endl;

    std::cout << "\n[SUCCESS] All requests finished. Keeping connection alive..." << std::endl;
    
    // 保持进程不退，观察 Server 端连接状态
    while(1) sleep(10);

    return 0;
}