#include <iostream>
#include <mutex>
#include <cstring>
#include <sys/mman.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <fcntl.h>      // for open
#include <sys/stat.h>   // for file modes
#include <cstdint>      // for uintptr_t
#include <netinet/in.h>
#include <arpa/inet.h>

#include "concurrent/concurrent_core.h"
#include "net/protocol.h"
#include "dsm.h"
#include "os/table_base.hpp"
#include "os/page_table.h"


extern void *SharedAddrBase;  
extern class PageTable* PageTable;


bool DSM_LoadFromDisk(int page_index, void* dest_buffer) {
    
    std::string file_path;
    int page_offset_index = 0;
           
    auto* record = ::PageTable->Find(page_index);

    // 提取文件信息
    file_path = record->filepath;
    page_offset_index = record->offset; // 这是“第几页”

    

    off_t byte_offset = (off_t)page_offset_index * DSM_PAGE_SIZE;

    ssize_t bytes_read = pread(record->fd, dest_buffer, DSM_PAGE_SIZE, byte_offset);

    // 如果读到的字节数少于 4096 (到达 EOF)，剩下的补 0
    if (bytes_read < DSM_PAGE_SIZE) {
        memset((char*)dest_buffer + bytes_read, 0, DSM_PAGE_SIZE - bytes_read);
    }

    return true;
}

void process_page_req(int sock, const dsm_header_t& head, const payload_page_req_t& body) {
    ::PageTable->GlobalMutexLock();

    auto* record = ::PageTable->Find(body.page_index);

    ::PageTable->LocalMutexLock(body.page_index);
    ::PageTable->GlobalMutexUnlock();
    
    int current_owner = record ? record->owner_id : -1;

    

    // 计算物理地址用于发送数据
    uintptr_t page_addr_int = (uintptr_t)SharedAddrBase + (body.page_index * DSM_PAGE_SIZE);
    void* page_ptr = (void*)page_addr_int;

    // Case A: owner_id == PodId
      if (current_owner == dsm_getnodeid()) {
        

        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP;
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_page_rep_t); 
        rep_head.unused = 1; //
        
        payload_page_rep_t rep_body;
        rep_body.real_owner_id = current_owner;//垃圾信息
        memcpy(rep_body.pagedata, page_ptr, DSM_PAGE_SIZE);

       // 【新增日志】
        std::cout << "[DSM] Node " << dsm_getnodeid() << " (Owner) serving Page " 
                  << body.page_index << " to Node " << head.src_node_id << std::endl;
        send(sock, (const char*)&rep_head, sizeof(rep_head), 0);
        send(sock, (const char*)&rep_body, sizeof(rep_body), 0);

        dsm_header_t update_head;
        payload_owner_update_t update_body;
                
        // 这里会卡住，直到 Node 1 处理完并回信
        // 如果 Node 2 此时来请求，会在 LocalMutexLock 那里被卡住，非常完美
        ssize_t n = recv(sock, &update_head, sizeof(update_head), MSG_WAITALL);
                
        if (n > 0 && update_head.type == DSM_MSG_OWNER_UPDATE) {
            recv(sock, &update_body, sizeof(update_body), MSG_WAITALL);

            if (record) {
                record->owner_id = update_body.new_owner_id;
            }       
            
                    
            std::cout << "[DSM] Ownership transferred to Node " << record->owner_id << std::endl;
        } else {
            std::cerr << "[DSM] Warning: Did not receive Owner Update from Node " << head.src_node_id << std::endl;
        }

        ::PageTable->LocalMutexUnlock(body.page_index);

        return;
    } 
    // Case B: owner_id != PodId
    else if ( current_owner != -1 && current_owner != dsm_getnodeid()) {
        

        dsm_header_t rep_head = {0};
        rep_head.type = DSM_MSG_PAGE_REP; 
        rep_head.src_node_id = dsm_getnodeid();
        rep_head.seq_num = head.seq_num;
        rep_head.payload_len = sizeof(payload_page_rep_t);
        rep_head.unused = 0; 

        payload_page_rep_t rep_body;
        rep_body.real_owner_id = current_owner;
        memset(rep_body.pagedata, 0, DSM_PAGE_SIZE); //
        
        // 【新增日志】
        std::cout << "[DSM] Node " << dsm_getnodeid() << " Redirecting Page " 
                  << body.page_index << " request from Node " << head.src_node_id 
                  << " to Owner " << current_owner << std::endl;

        send(sock, (const char*)&rep_head, sizeof(rep_head), 0);
        send(sock, (const char*)&rep_body, sizeof(rep_body), 0);
                
        ::PageTable->LocalMutexUnlock(body.page_index);
        return;
    }
    // Case C: owner_id = -1 && PodId = 0
    else if (current_owner == -1 && dsm_getnodeid() == 0) {
       
        bool load_success = DSM_LoadFromDisk(body.page_index, page_ptr);

        if (load_success) {
            
            if (record) record->owner_id = 0; 
            current_owner = 0; // 修正局部变量
            

            dsm_header_t rep_head = {0};
            rep_head.type = DSM_MSG_PAGE_REP;
            rep_head.src_node_id = dsm_getnodeid();
            rep_head.seq_num = head.seq_num;
            rep_head.payload_len = sizeof(payload_page_rep_t); 
            rep_head.unused = 1; // 
            
            payload_page_rep_t rep_body;
            rep_body.real_owner_id = 0; // 【重要】告诉对方，现在 Owner 是 0 (我)
            memcpy(rep_body.pagedata, page_ptr, DSM_PAGE_SIZE);
            // 【新增】加上这行日志
            std::cout << "[DSM] Node 0 serving Page " << body.page_index << " request." << std::endl;

            send(sock, (const char*)&rep_head, sizeof(rep_head), 0);
            send(sock, (const char*)&rep_body, sizeof(rep_body), 0);

            if(dsm_getnodeid() == head.src_node_id) {
                // 如果请求者正好是 Node 0 自己，就不必等 Update 了
                ::PageTable->LocalMutexUnlock(body.page_index);
                return;
            }

            // 阻塞接收 Node 1 发回来的 Update 包
                dsm_header_t update_head;
                payload_owner_update_t update_body;
                
                // 这里会卡住，直到 Node 1 处理完并回信
                // 如果 Node 2 此时来请求，会在 LocalMutexLock 那里被卡住，非常完美
                ssize_t n = recv(sock, &update_head, sizeof(update_head), MSG_WAITALL);
                
                if (n > 0 && update_head.type == DSM_MSG_OWNER_UPDATE) {
                    recv(sock, &update_body, sizeof(update_body), MSG_WAITALL);
                    
                    if (record) {
                record->owner_id = update_body.new_owner_id;
            }
                    std::cout << "[DSM] Ownership transferred to Node " << record->owner_id << std::endl;
                } else {
                    std::cerr << "[DSM] Warning: Did not receive Owner Update from Node " << head.src_node_id << std::endl;
                }


            ::PageTable->LocalMutexUnlock(body.page_index);
                        
        } else {
            std::cerr << "[DSM] Disk load failed for page " << body.page_index << std::endl;
            ::PageTable->LocalMutexUnlock(body.page_index);
        }
        return;
    }
    // Case D: owner_id = -1 && PodId != 0
    else if (current_owner == -1 && dsm_getnodeid() != 0) {
        

        // 【绝对不能调 DSM_LoadFromDisk，因为我没有文件】
        // 1. 建立临时连接连 Node 0
        int temp_sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in node0_addr;
        node0_addr.sin_family = AF_INET;
        node0_addr.sin_port = htons(GetPodPort(0)); // 获取 Node 0 端口
        inet_pton(AF_INET, GetPodIp(0).c_str(), &node0_addr.sin_addr); // 获取 Node 0 IP

        if (connect(temp_sock, (struct sockaddr*)&node0_addr, sizeof(node0_addr)) == 0) {
            // 2. 伪装成 Client 向 Node 0 发请求
            dsm_header_t req_head = {0};
            req_head.type = DSM_MSG_PAGE_REQ;
            req_head.src_node_id = dsm_getnodeid(); // 告诉 Node 0 是我(小弟)要的
            req_head.payload_len = sizeof(payload_page_req_t);
            
            payload_page_req_t req_body_out;
            req_body_out.page_index = body.page_index;
            
            send(temp_sock, &req_head, sizeof(req_head), 0);
            send(temp_sock, &req_body_out, sizeof(req_body_out), 0);

            // 3. 接收 Node 0 的回包 (它会读磁盘并返回数据)
            dsm_header_t rep_head_in;
            payload_page_rep_t rep_body_in; 
            
            // 接收头
            recv(temp_sock, &rep_head_in, sizeof(rep_head_in), MSG_WAITALL);
            // 接收体 (包含 4096 数据的结构体)
            recv(temp_sock, &rep_body_in, sizeof(rep_body_in), MSG_WAITALL);

            // 4. 把 Node 0 给我的数据，写入我的内存 (page_ptr)
            if (rep_head_in.unused == 1) {
                // 将数据落盘到我的共享内存
                memcpy(page_ptr, rep_body_in.pagedata, DSM_PAGE_SIZE);
                dsm_header_t update_head = {0};
                update_head.type = DSM_MSG_OWNER_UPDATE; // 确保 protocol.h 有这个
                update_head.src_node_id = dsm_getnodeid();
                update_head.payload_len = sizeof(payload_owner_update_t);
                
                payload_owner_update_t update_body;
                update_body.resource_id = body.page_index;
                update_body.new_owner_id = dsm_getnodeid(); // 声明：现在归我了
                
                send(temp_sock, &update_head, sizeof(update_head), 0);
                send(temp_sock, &update_body, sizeof(update_body), 0);
                
            }
            close(temp_sock);

            
           auto* record = ::PageTable->Find(body.page_index);

            // 2. 只有当记录存在时，才去更新 Owner
            if (record != nullptr) {
                
                
                // 只有 record 有效才能写！
                record->owner_id = dsm_getnodeid(); 
                
                ::PageTable->LocalMutexUnlock(body.page_index);
            } else {
                // 如果表里没这条记录（Node 1 只是临时转发，没注册这一页），直接放锁
                ::PageTable->LocalMutexUnlock(body.page_index);
            }
            rep_head_in.src_node_id = dsm_getnodeid();
            send(sock, &rep_head_in, sizeof(rep_head_in), 0);
            send(sock, &rep_body_in, sizeof(rep_body_in), 0);
        } else {
            std::cerr << "[DSM] Failed to connect to Node 0" << std::endl;
        }

        return;
    }

}


