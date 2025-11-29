// src/concurrent/dsm_state.hpp
#ifndef DSM_STATE_HPP
#define DSM_STATE_HPP

#include <mutex>
#include <memory>
#include "os/page_table.h"
#include "os/locktable.h"  // 注意文件名大小写要和实际文件一致
#include "os/bindtable.h"

// 全局 DSM 状态管理类
struct DSMState {
    // 互斥锁：保护以下所有表的操作
    std::mutex state_mutex;

    // 三大核心表
    PageTable page_table;
    LockTable lock_table;
    BindTable bind_table;

    // 自身节点信息
    int my_node_id = -1;
    void* shared_mem_base = nullptr;

    // 单例模式获取
    static DSMState& GetInstance() {
        static DSMState instance;
        return instance;
    }

private:
    DSMState() : page_table(1024) {} // 预分配容量
};

#endif // DSM_STATE_HPP