#include <iostream>
#include <unistd.h>
#include "concurrent/concurrent_core.h"

// 只需要引用初始化函数
// 这里的目的是启动一个 ID=1 的 Manager 守护进程
int main(int argc, char** argv) {
    int port = 8080; // 默认监听 8080
    
    std::cout << "[Test Manager] Starting DSM Manager on port " << port << "..." << std::endl;

    // 直接调用你的系统初始化函数
    // 参数: my_port=8080, manager_ip="ignored", is_manager=true
    concurrent_system_init(port, "127.0.0.1", true);

    // 主线程不能退，否则 Daemon 线程也没了
    while(true) {
        sleep(10);
        std::cout << "[Test Manager] Heartbeat... (I am alive)" << std::endl;
    }
    return 0;
}