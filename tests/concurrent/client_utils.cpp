#include "client_utils.h"
#include "mock_env.h" // 获取 MockGetPodIp/Port

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
#include <iomanip> // for hex dump
#include <thread> // for sleep
#include <chrono>

// 静态序列号，每次发包递增，方便观察日志
static uint32_t g_seq_num = 0;

// ==========================================
// 辅助函数：建立 TCP 连接
// ==========================================
int connect_to_node(int target_node_id) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("[TestClient] Socket creation failed");
        return -1;
    }

    std::string ip = GetPodIp(target_node_id);
    int port = GetPodPort(target_node_id);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "[TestClient] Invalid address: " << ip << std::endl;
        close(sockfd);
        return -1;
    }

    // std::cout << "[TestClient] Connecting to Node " << target_node_id 
    //           << " (" << ip << ":" << port << ")..." << std::endl;

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("[TestClient] Connection failed"); // 对方没启动或者端口不对
        close(sockfd);
        return -1;
    }

    return sockfd;
}

// ==========================================
// 1. Join 请求测试
// ==========================================
void request_join(int target_node) {
    int sock = connect_to_node(target_node);
    if (sock < 0) return;

    // 1. 发送请求头 (JoinReq 通常没有 body 或者 body 为空)
    dsm_header_t head = {0};
    head.type = DSM_MSG_JOIN_REQ;
    head.src_node_id = dsm_getnodeid();
    head.seq_num = ++g_seq_num;
    head.payload_len = 0; // 假设有 body
    head.unused = 0;

    send(sock, &head, sizeof(head), 0);


    std::cout << "[TestClient] Sent JOIN_REQ to Node " << target_node << std::endl;

    // 2. 接收回复 (Header only based on your concurrent_join.cpp)
    dsm_header_t ack_head;
    ssize_t n = recv(sock, &ack_head, sizeof(ack_head), MSG_WAITALL);
    
    if (n == sizeof(ack_head) && ack_head.type == DSM_MSG_ACK) {
        std::cout << "[TestClient] SUCCESS: Received JOIN_ACK from Node " 
                  << ack_head.src_node_id << std::endl;
    } else {
        std::cerr << "[TestClient] FAILED: Invalid Join response. Type=" 
                  << std::hex << (int)ack_head.type << std::dec << std::endl;
    }

    close(sock);
}

// ==========================================
// 2. Page 请求测试 (最关键)
// ==========================================
void request_page(int target_node, int page_index) {
    int sock = connect_to_node(target_node);
    if (sock < 0) return;

    std::cout << "[TestClient] Requesting Page " << page_index << " from Node " << target_node << "..." << std::endl;

    // 1. 发送请求
    dsm_header_t head = {0};
    head.type = DSM_MSG_PAGE_REQ;
    head.src_node_id = dsm_getnodeid();
    head.seq_num = ++g_seq_num;
    head.payload_len = sizeof(payload_page_req_t);

    payload_page_req_t req_body;
    req_body.page_index = page_index;

    send(sock, &head, sizeof(head), 0);
    send(sock, &req_body, sizeof(req_body), 0);

    // 2. 接收回复头
    dsm_header_t rep_head;
    if (recv(sock, &rep_head, sizeof(rep_head), MSG_WAITALL) != sizeof(rep_head)) {
        std::cerr << "[TestClient] Failed to receive header." << std::endl;
        close(sock);
        return;
    }

    if (rep_head.type != DSM_MSG_PAGE_REP) {
        std::cerr << "[TestClient] Error: Expected PAGE_REP, got 0x" 
                  << std::hex << (int)rep_head.type << std::dec << std::endl;
        close(sock);
        return;
    }

    // 3. 接收回复体 (注意：你的代码里把 pagedata 放进了 rep_body 结构体)
    // 所以这是一个大约 4098 字节的大包
    payload_page_rep_t rep_body;
    
    // 确保接收完整的大包
    size_t total_len = sizeof(payload_page_rep_t);
    ssize_t n = recv(sock, &rep_body, total_len, MSG_WAITALL);

    if (n != total_len) {
        std::cerr << "[TestClient] Failed to receive body. Got " << n << " bytes, expected " << total_len << std::endl;
        close(sock);
        return;
    }

    // 4. 打印结果
    //std::cout << "[TestClient] SUCCESS: Received Page " << page_index << " from Node " << rep_head.src_node_id << "\n";
    //std::cout << "    -> Real Owner ID: " << rep_body.real_owner_id << "\n";

    // =================================================================
    // 【核心修改】处理重定向逻辑
    // =================================================================
    if (rep_head.unused == 0) { 
        // 没数据，检查是不是重定向
        int real_owner = rep_body.real_owner_id;
        
        std::cout << "[TestClient] Node " << target_node << " says: I don't have it. Go ask Node " << real_owner << ".\n";
        
        // 关闭与当前节点的连接
        close(sock);

        // 如果 owner 有效，且不是我自己，且不是我刚才问的那个人（防止死循环）
        if (real_owner != -1 && real_owner != dsm_getnodeid() && real_owner != target_node) {
            std::cout << "[TestClient] >>> Redirecting request to Node " << real_owner << " >>>" << std::endl;
            
            // 【递归调用】去找真正的 Owner 要数据！
            request_page(real_owner, page_index);
            return; // 结束当前函数，后面的逻辑交给递归的那次去处理
        } else {
            std::cout << "[TestClient] Failed: Page not found or circular redirection." << std::endl;
            return;
        }
    }
    
    

    if (rep_head.unused == 1) { // unused=1 代表携带了有效数据

        // 打印前 32 个字节的内容预览
        std::cout << "    -> Content Preview: ";
        for (int i = 0; i < 32; i++) {
            char c = rep_body.pagedata[i];
            // 只打印可打印字符，否则打点
            if (isprint(c)) std::cout << c;
            else std::cout << ".";
        }
        std::cout << "..." << std::endl;
        
        // 只有当我不是 Node 0，且我拿到了数据，我才需要通知 Node 0 更新 Owner
        if (dsm_getnodeid() != 0) {
            std::cout << "[TestClient] Data received. Sending OWNER_UPDATE to Manager (Node 0)..." << std::endl;

            // 1. 连接 Node 0 (Manager)
            // 注意：如果 target_node 本身就是 0，我们可以复用 sock？
            // 不行，因为 Node 0 那边的线程正卡在 recv 等着呢，复用 sock 正好！
            
            // 【关键分支】
            
                // 场景：我直接找 Node 0 要的数据。Node 0 的 Case C 正在这个 socket 上等我回复。
                // 所以直接在这个 sock 上发 Update！
                
                dsm_header_t update_head = {0};
                update_head.type = DSM_MSG_OWNER_UPDATE;
                update_head.src_node_id = dsm_getnodeid();
                update_head.payload_len = sizeof(payload_owner_update_t);
                
                payload_owner_update_t update_body;
                update_body.resource_id = page_index;
                update_body.new_owner_id = dsm_getnodeid();

                send(sock, &update_head, sizeof(update_head), 0);
                send(sock, &update_body, sizeof(update_body), 0);
                
                std::cout << "[TestClient] Update sent to Node 0 via existing connection." << std::endl;
            

            // 2. 【新增】更新本地 PageTable 和 内存
            // ---------------------------------------------------------
            ::PageTable->GlobalMutexLock();
            
            // 写入共享内存 (模拟缺页处理完成)
            uintptr_t addr_base = (uintptr_t)SharedAddrBase + (page_index * DSM_PAGE_SIZE);
            memcpy((void*)addr_base, rep_body.pagedata, DSM_PAGE_SIZE);

            // 更新/插入页表记录
            auto* record = ::PageTable->Find(page_index);
            if (record == nullptr) {
                PageRecord new_rec;
                new_rec.owner_id = dsm_getnodeid(); // 我是 Owner
                // filepath/fd 不需要填，因为这是内存页
                ::PageTable->Insert(page_index, new_rec);
            } else {
                ::PageTable->LocalMutexLock(page_index);
                ::PageTable->GlobalMutexUnlock(); // 拿着局部锁
                
                record->owner_id = dsm_getnodeid();
                
                ::PageTable->LocalMutexUnlock(page_index);
                // 注意：如果上面是 Insert，这里不需要 unlock，因为 Insert 内部处理了
                // 为了简化逻辑，这里写得稍微冗余一点，主要是确保 owner_id 变了
            }
            
            // 如果走的是 Insert 分支，别忘了释放全局锁
            if (record == nullptr) {
                ::PageTable->GlobalMutexUnlock();
            }
            // ---------------------------------------------------------
            
            std::cout << "[TestClient] Local PageTable updated. I am now the Owner of Page " << page_index << std::endl;


        }

    }



    close(sock);
}

// ==========================================
// 3. Lock 请求测试
// ==========================================
void request_lock(int target_node, int lock_id) {
    int sock = connect_to_node(target_node);
    if (sock < 0) return;

    std::cout << "[TestClient] Requesting Lock " << lock_id << " from Node " << target_node << std::endl;

    // 发送
    dsm_header_t head = {0};
    head.type = DSM_MSG_LOCK_ACQ;
    head.src_node_id = dsm_getnodeid();
    head.seq_num = ++g_seq_num;
    head.payload_len = sizeof(payload_lock_req_t);

    payload_lock_req_t req_body;
    req_body.lock_id = lock_id;

    send(sock, &head, sizeof(head), 0);
    send(sock, &req_body, sizeof(req_body), 0);

    // 接收
    dsm_header_t rep_head;
    payload_lock_rep_t rep_body;

    recv(sock, &rep_head, sizeof(rep_head), MSG_WAITALL);
    recv(sock, &rep_body, sizeof(rep_body), MSG_WAITALL);

    if (rep_head.type == DSM_MSG_LOCK_REP) {
        std::cout << "[TestClient] SUCCESS: Acquired Lock " << lock_id << "\n";
        std::cout << "    -> Invalid Set Count: " << rep_body.invalid_set_count << std::endl;
    } else {
        std::cout << "[TestClient] FAILED to acquire lock." << std::endl;
    }

    close(sock);
}

// 【新增】实现释放锁的测试函数
void request_lock_release(int target_node, int lock_id) {
    int sock = connect_to_node(target_node);
    if (sock < 0) return;

    std::cout << "[TestClient] Releasing Lock " << lock_id << " to Node " << target_node << std::endl;

    // 1. 发送请求
    dsm_header_t head = {0};
    head.type = DSM_MSG_LOCK_RLS; // 确保定义了这个类型
    head.src_node_id = dsm_getnodeid();
    head.seq_num = ++g_seq_num;
    head.payload_len = sizeof(payload_lock_req_t);

    payload_lock_req_t req_body;
    req_body.lock_id = lock_id;

    send(sock, &head, sizeof(head), 0);
    send(sock, &req_body, sizeof(req_body), 0);

    // 2. 接收 ACK (只有头，没有体)
    dsm_header_t ack_head;
    ssize_t n = recv(sock, &ack_head, sizeof(ack_head), MSG_WAITALL);

    if (n == sizeof(ack_head) && ack_head.type == DSM_MSG_ACK) {
        std::cout << "[TestClient] SUCCESS: Received ACK for Lock Release." << std::endl;
    } else {
        std::cout << "[TestClient] FAILED: Did not receive expected ACK." << std::endl;
    }

    close(sock);
}

// ==========================================
// 4. Owner Update 测试
// ==========================================
void send_owner_update(int target_node, uint32_t page_index, int new_owner) {
    int sock = connect_to_node(target_node);
    if (sock < 0) return;

    std::cout << "[TestClient] Updating Owner of Page " << page_index 
              << " on Node " << target_node << " to " << new_owner << std::endl;

    // Update 请求通常没有 Rep 回包 (One-way notification)，或者是异步的
    // 根据你的 concurrent_misc.cpp，函数最后只是 return，没有 send 回包
    // 所以我们只管发，不等待 recv
    
    dsm_header_t head = {0};
    head.type = DSM_MSG_OWNER_UPDATE;
    head.src_node_id = dsm_getnodeid();
    head.seq_num = ++g_seq_num;
    head.payload_len = sizeof(payload_owner_update_t);

    payload_owner_update_t body;
    body.resource_id = page_index;
    body.new_owner_id = new_owner;

    send(sock, &head, sizeof(head), 0);
    send(sock, &body, sizeof(body), 0);

    std::cout << "[TestClient] Update sent. (No ACK expected)" << std::endl;
    close(sock);
}

// 【新增】批量请求实现
void run_stress_test(int target, int page_idx, int count) {
    std::cout << "[TestClient] Starting STRESS TEST: " << count 
              << " requests to Node " << target << " for Page " << page_idx << "..." << std::endl;

    int success_cnt = 0;
    
    for (int i = 0; i < count; i++) {
        // 复用你已经写好的 request_page 函数
        // 注意：request_page 里有打印日志，刷屏会很快，这正好模拟 IO 压力
        request_page(target, page_idx);
        
        // 可选：极短延时，防止瞬间耗尽本地临时端口（Ephemeral Ports）
        // 如果 count 很大（比如 > 5000），建议打开
        // std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "[TestClient] Stress Test DONE. Check logs above." << std::endl;
}