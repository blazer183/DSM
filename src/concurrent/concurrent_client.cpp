#include <iostream>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <vector>

#include "net/protocol.h"
#include "concurrent/concurrent_core.h" // 包含 peer_handler 声明


// ---------------------------------------------------------
// 辅助函数：连接节点
// ---------------------------------------------------------
int connect_to_node(const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port); 
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

// ---------------------------------------------------------
// 辅助函数：初始化本地共享内存
// ---------------------------------------------------------
void init_local_memory(DSMState& state, size_t size) {
    state.total_mem_size = size;
    // 使用 mmap 或者 malloc
    state.shared_mem_base = malloc(size);
    if (!state.shared_mem_base) {
        perror("[Fatal] Memory allocation failed");
        exit(1);
    }
    memset(state.shared_mem_base, 0, size); 
    std::cout << "[System] Shared Memory Initialized: " << size << " bytes." << std::endl;
}

// 需要引用 dsm_daemon.cpp 里的 peer_handler
// 这里的 peer_handler 是个死循环，用来收数据的
extern void peer_handler(int connfd);

// [新增] 获取长连接 (核心复用逻辑)
// 如果连接已存在，直接返回；否则建立连接、缓存它、并启动后台接收线程
int get_stable_connection(int target_node_id) {
    auto& state = DSMState::GetInstance();

    // 1. 先加锁查缓存
    {
        std::lock_guard<std::mutex> lk(state.net_mutex);
        if (target_node_id < state.node_sockets.size()) {
            int existing_sock = state.node_sockets[target_node_id];
            if (existing_sock != -1) {
                return existing_sock; // >>> 命中缓存，直接复用 <<<
            }
        }
    }

    // 2. 缓存没命中，建立新连接
    // 假设 IP 都是本地，端口是 8000 + ID
    char ip_buf[32] = "127.0.0.1"; 
    int port = 8000 + target_node_id;

    // 特殊情况：如果是连 Manager (ID=1)
    if (target_node_id == 1) {
        // 假设 Manager 端口固定或者已存
        port = 8080; // 这里需要根据实际情况写，或者用 saved_manager_port
    }

    int new_sock = connect_to_node(ip_buf, port);
    if (new_sock < 0) {
        std::cerr << "[Network] Connect to Node " << target_node_id << " failed!" << std::endl;
        return -1;
    }

    std::cout << "[Network] Established NEW connection to Node " << target_node_id << std::endl;

    // 3. 【关键】启动后台接收线程
    // 既然是长连接，必须有一个线程专门守着它收数据
    std::thread(peer_handler, new_sock).detach();

    // 4. 存入缓存
    {
        std::lock_guard<std::mutex> lk(state.net_mutex);
        // 扩容检查
        if (target_node_id >= state.node_sockets.size()) {
            state.node_sockets.resize(target_node_id + 16, -1);
        }
        state.node_sockets[target_node_id] = new_sock;
    }

    return new_sock;
}

// ==========================================================
// 1. 系统初始化与启动 (Bootstrap)
// ==========================================================
void concurrent_system_init(int my_port, const char* manager_ip, bool is_manager) {
    auto& state = DSMState::GetInstance();
    
    // --- 启动监听 Daemon (接受别的节点连我) ---
    // 在新线程启动，否则会阻塞主线程
    std::thread([my_port](){ 
        dsm_start_daemon(my_port); 
    }).detach();

    // 给 Daemon 一点启动时间
    usleep(100000); 

    // ------------------------------------------
    // Case A: 我是 Manager
    // ------------------------------------------
    if (is_manager) {
        state.my_node_id = 1;
        state.node_count = 1; // 初始只有自己
        std::cout << "[Bootstrap] Starting as Manager (ID=1)..." << std::endl;
        
        // 分配默认大小内存 (例如 4MB)
        init_local_memory(state, 1024 * DSM_PAGE_SIZE); 
    }
    // ------------------------------------------
    // Case B: 我是 Client (Worker)
    // ------------------------------------------
    else {
        std::cout << "[Bootstrap] Connecting to Manager at " << manager_ip << "..." << std::endl;
        
        // 1. 连接 Manager
        // 假设 Manager 监听端口固定为 8080，或者通过参数传递
        int sock = connect_to_node(manager_ip, 8080); 
        if (sock < 0) {
            std::cerr << "[Fatal] Cannot connect to Manager!" << std::endl;
            exit(1);
        }

        // 【修改点】: 既然连上了，就要把它存入长连接缓存！
        // 这样后续 get_stable_connection(1) 就能直接复用了
        {
            std::lock_guard<std::mutex> lk(state.net_mutex);
            if (state.node_sockets.size() < 2) state.node_sockets.resize(100, -1);
            state.node_sockets[1] = sock;
        }

        // 保存 Manager 连接
        state.manager_sock = sock;

        // 【关键】必须立刻启动一个线程来在这个 socket 上收 Manager 的回复
        // 这样当 JOIN_ACK 回来时，dsm_daemon 中的 process_join_ack 才能被触发
        std::thread(peer_handler, sock).detach();

        // 2. 发送 JOIN 请求
        dsm_header_t req_head = {0};
        req_head.type = DSM_MSG_JOIN_REQ;
        req_head.src_node_id = 0; // 0 表示未知
        req_head.payload_len = sizeof(payload_join_req_t);
        
        payload_join_req_t req_body;
        req_body.listen_port = my_port;

        rio_writen(sock, &req_head, sizeof(req_head));
        rio_writen(sock, &req_body, sizeof(req_body));

        // 3. 阻塞等待 JOIN_ACK
        std::cout << "[Bootstrap] Waiting for Join ACK..." << std::endl;
        {
            std::unique_lock<std::mutex> lock(state.state_mutex);
            state.join_cond.wait(lock, [&state]{ return state.join_finished; });
        }

        // 4. 被唤醒了！初始化内存
        std::cout << "[Bootstrap] Join Success! My ID: " << state.my_node_id << std::endl;
        init_local_memory(state, state.total_mem_size);
    }
}


// ==========================================================
// 2. 缺页请求逻辑 (核心)
// ==========================================================
void dsm_request_page(uint32_t page_index) {
    auto& state = DSMState::GetInstance();
    
    // 循环直到拿到数据 (处理重定向)
    while (true) {
        // 1. 确定目标节点 (ProbOwner)
        // 策略：先查页表，如果页表没记录，则 Hash 计算 (page % node_count + 1)
        // 注意：Manager ID 是 1
        int target_node_id = -1;
        uintptr_t page_addr = (uintptr_t)state.shared_mem_base + (page_index * DSM_PAGE_SIZE);
        auto* rec = state.page_table.Find(page_addr);

        if (rec) {
            target_node_id = rec->owner_id;
        } else {
            // Hash 算法：简单的取模
            // 假设 ID 从 1 开始，node_count 是总数
            target_node_id = (page_index % state.node_count) + 1;
        }

        // 如果目标就是我自己，说明逻辑出错了 (我有所有权却缺页?)
        // 或者是因为我刚刚把页给了别人，本地页表还没更新
        if (target_node_id == state.my_node_id) {
            target_node_id = 1; // 兜底找 Manager
        }

        std::cout << "[Client] Requesting Page " << page_index << " from Node " << target_node_id << "..." << std::endl;

        // 2. [修改点] 获取连接 (使用长连接缓存)
        int target_sock = get_stable_connection(target_node_id);

        // 如果连不上 ProbOwner，尝试连 Manager

        if (target_sock < 0) {
            std::cerr << "[Error] Cannot connect to Node " << target_node_id << ", retrying Manager..." << std::endl;
            // 连不上就找 Manager 哭诉
            target_node_id = 1;
            target_sock = state.manager_sock;
        }

        // 3. 准备状态位
        {
            std::lock_guard<std::mutex> lk(state.state_mutex);
            state.page_req_finished = false;
            state.data_received = false;
            state.pending_page_index = page_index; // 告诉 Daemon 我们在等这页
        }

        // 4. 发送请求
        dsm_header_t req_head = {0};
        req_head.type = DSM_MSG_PAGE_REQ;
        req_head.src_node_id = state.my_node_id;
        req_head.payload_len = sizeof(payload_page_req_t);
        
        payload_page_req_t req_body;
        req_body.page_index = page_index;

        rio_writen(target_sock, &req_head, sizeof(req_head));
        rio_writen(target_sock, &req_body, sizeof(req_body));

        // 5. 阻塞等待
        {
            std::unique_lock<std::mutex> lock(state.state_mutex);
            // 等待 Daemon 收到 REP 并 notify
            state.page_cond.wait(lock, [&state]{ return state.page_req_finished; });
        }

        // 6. 检查结果
        if (state.data_received) {
            std::cout << "[Client] Page " << page_index << " acquired successfully." << std::endl;
            
            // [新增] 成功拿到数据后，发送 OwnerUpdate 通知 Manager
            // 除非数据本来就是 Manager 给的，且 Manager 知道我们要拿走 (通常协议里 Manager 给数据就默认改了 Owner)
            // 但如果是从别的 Worker 拿的 (三跳)，最好发一个 UPDATE
            if (target_node_id != 1) { 
                int mgr_sock = get_stable_connection(1);
                if (mgr_sock > 0) {
                    dsm_header_t upd_head = {0};
                    upd_head.type = DSM_MSG_OWNER_UPDATE;
                    upd_head.src_node_id = state.my_node_id;
                    upd_head.unused = 0; // Page
                    upd_head.payload_len = sizeof(payload_owner_update_t);
                    
                    payload_owner_update_t upd_body;
                    upd_body.resource_id = page_index;
                    upd_body.new_owner_id = state.my_node_id;
                    
                    rio_writen(mgr_sock, &upd_head, sizeof(upd_head));
                    rio_writen(mgr_sock, &upd_body, sizeof(upd_body));
                }
            }
            break; 
        } else {
            std::cout << "[Client] Redirected. Retrying..." << std::endl;
            // 状态已被 process_page_rep 更新，下一次循环 state.page_table.Find 会找到新 Owner
        }
    }
}

// ==========================================================
// 3. 锁请求逻辑
// ==========================================================
void dsm_request_lock(uint32_t lock_id) {
    auto& state = DSMState::GetInstance();
    
    // 逻辑和 Page 类似：发给 Manager -> Wait -> 如果 Redir 则发给 RealOwner -> Wait
    // 简化版：我们假设锁全归 Manager 管路由
    
    std::cout << "[Client] Requesting Lock " << lock_id << "..." << std::endl;

    {
        std::lock_guard<std::mutex> lk(state.state_mutex);
        state.lock_granted = false;
    }

    // 1. [修改点] 获取 Manager 连接 (长连接)
    int mgr_sock = get_stable_connection(1);
    if (mgr_sock < 0) {
        std::cerr << "[Client] Failed to connect to Manager for Lock!" << std::endl;
        return;
    }

    // 1. 发给 Manager
    dsm_header_t req_head = {0};
    req_head.type = DSM_MSG_LOCK_ACQ;
    req_head.src_node_id = state.my_node_id;
    req_head.payload_len = sizeof(payload_lock_req_t);
    
    payload_lock_req_t req_body;
    req_body.lock_id = lock_id;

    rio_writen(state.manager_sock, &req_head, sizeof(req_head));
    rio_writen(state.manager_sock, &req_body, sizeof(req_body));

    // 2. 等待
    {
        std::unique_lock<std::mutex> lock(state.state_mutex);
        // 这里需要更复杂的判断：如果是排队，可能要等很久；如果是 Redirect，需要 retry
        // 简单起见，假设收到 Grant 才醒
        state.lock_cond.wait(lock, [&state]{ return state.lock_granted; });
    }
    
    std::cout << "[Client] Lock " << lock_id << " acquired." << std::endl;
}