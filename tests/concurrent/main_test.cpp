#include <iostream>
#include <thread>
#include <chrono>
#include <string>

#include "mock_env.h"
#include "client_utils.h"

// 引入你的 daemon 启动函数声明
extern void dsm_start_daemon(int port);

void print_help() {
    std::cout << "Commands:\n"
              << "  join <target_id>       : Send Join Request\n"
              << "  page <target_id> <idx> : Request Page\n"
              << "  lock <target_id> <id>  : Request Lock\n"
              << "  owner <target_id> <idx> <owner> : Update Owner\n"
              << "  help                   : Show this message\n"
              << "  bench <target> <page> <count> : Run stress test\n" // 新增说明
              << "  exit                   : Quit\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./dsm_test <node_id>" << std::endl;
        return 1;
    }

    int my_id = std::stoi(argv[1]);
    std::cout << ">>> Starting DSM Test Node [" << my_id << "] <<<" << std::endl;

    // 1. 初始化模拟环境 (全局变量、预置数据)
    mock_init(my_id);

    // 2. 启动后台监听线程 (Server)
    int my_port = GetPodPort(my_id);
    std::thread server_thread(dsm_start_daemon, my_port);
    server_thread.detach(); // 让它在后台跑

    // 给一点时间让 Server 启动
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 3. 命令行交互循环 (Client)
    print_help();
    
    std::string cmd;
    while (true) {
        std::cout << "Node[" << my_id << "]> ";
        std::cin >> cmd;

        if (cmd == "exit") break;
        else if (cmd == "help") print_help();
        
        else if (cmd == "join") {
            int target; std::cin >> target;
            request_join(target);
        }
        else if (cmd == "page") {
            int target, idx; std::cin >> target >> idx;
            request_page(target, idx);
        }
        else if (cmd == "lock") {
            int target, id; std::cin >> target >> id;
            request_lock(target, id);
        }
        else if (cmd == "owner") {
            int target, idx, owner; std::cin >> target >> idx >> owner;
            send_owner_update(target, idx, owner);
        }
        else if (cmd == "bench") {
            int target, page, count;
            std::cin >> target >> page >> count;
            run_stress_test(target, page, count);
        }
        else {
            std::cout << "Unknown command." << std::endl;
        }
    }

    // 清理
    mock_cleanup();
    return 0;
}