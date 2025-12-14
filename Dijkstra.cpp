/*
 * Dijkstra's Single-Source Shortest Path Algorithm - DSM Version
 * 
 * 基于分布式共享内存(DSM)实现的并行Dijkstra算法
 * 改写自MPI版本
*/

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <climits>
#include <cstring>
#include <unistd.h>

#include "dsm.h"

#define MAXINT INT_MAX

/**
 * 在DSM版本中，我们需要用共享内存来实现MPI_Allreduce的功能
 * 使用一个共享的gminpair数组来收集所有进程的局部最小值，
 * 然后通过barrier同步后，每个进程都能看到全局最小值
 */

void SingleSource_DSM(int n, int source, int *wgt, int *lengths, int lock_gmin) {
    int i, j;
    int nlocal;           // 本地存储的顶点数量
    int *marker;          // 标记数组: 0表示已找到最短路径，1表示未找到
    int firstvtx;         // 本地存储的第一个顶点索引
    int lastvtx;          // 本地存储的最后一个顶点索引
    int u, udist;         // 当前选中的顶点及其距离
    int lminpair[2];      // 本地最小值对: [距离, 顶点索引]
    
    int npes = ProcNum;   // 总进程数
    int myrank = PodId;   // 当前进程ID
    
    // 计算每个进程负责的顶点数量和范围
    nlocal = n / npes;
    firstvtx = myrank * nlocal;
    lastvtx = firstvtx + nlocal - 1;
    
    // 处理不能整除的情况：最后一个进程处理剩余顶点
    if (myrank == npes - 1) {
        lastvtx = n - 1;
        nlocal = lastvtx - firstvtx + 1;
    }
    
    std::cout << "[Pod " << myrank << "] Processing vertices " << firstvtx 
              << " to " << lastvtx << " (nlocal=" << nlocal << ")" << std::endl;
    
    // Step 0: 初始化 lengths 数组（从邻接矩阵的source行获取初始距离）
    // lengths[j] 存储从 source 到 firstvtx+j 的当前最短距离
    for (j = 0; j < nlocal; j++) {
        lengths[j] = wgt[source * n + (firstvtx + j)];
    }
    
    // 分配并初始化 marker 数组
    // marker[j] = 1 表示顶点 firstvtx+j 的最短路径尚未确定
    marker = (int *)malloc(nlocal * sizeof(int));
    for (j = 0; j < nlocal; j++) {
        marker[j] = 1;
    }
    
    // 如果源顶点在本进程负责的范围内，标记它为已处理
    if (source >= firstvtx && source <= lastvtx) {
        marker[source - firstvtx] = 0;
    }
    
    // 分配共享内存用于全局归约操作
    // gminpair[i*2] = 进程i的最小距离
    // gminpair[i*2+1] = 进程i的最小距离对应的顶点
    int *gminpair = (int *)dsm_malloc("$HOME/dsm/gminpair", nullptr);
    
    // 初始化互斥锁，用于保护共享内存的写操作
   
    
    dsm_barrier();  // 确保所有进程都完成初始化
    
    // 主循环：需要找到 n-1 个顶点的最短路径
    for (i = 1; i < n; i++) {
        // Step 1: 找到本地未处理顶点中距离最小的
        lminpair[0] = MAXINT;  // 最小距离
        lminpair[1] = -1;      // 对应顶点
        
        for (j = 0; j < nlocal; j++) {
            if (marker[j] && lengths[j] < lminpair[0]) {
                lminpair[0] = lengths[j];
                lminpair[1] = firstvtx + j;
            }
        }
        
        // Step 2: 使用DSM实现MPI_Allreduce的功能
        // 每个进程将自己的局部最小值写入共享内存
        dsm_mutex_lock(&lock_gmin);
        gminpair[myrank * 2] = lminpair[0];
        gminpair[myrank * 2 + 1] = lminpair[1];
        dsm_mutex_unlock(&lock_gmin);
        
        dsm_barrier();  // 等待所有进程写入完成
        
        // 每个进程读取所有进程的值，找出全局最小值
        int gmin_dist = MAXINT;
        int gmin_vtx = -1;
        
        dsm_mutex_lock(&lock_gmin);
        for (int p = 0; p < npes; p++) {
            int dist = gminpair[p * 2];
            int vtx = gminpair[p * 2 + 1];
            if (dist < gmin_dist) {
                gmin_dist = dist;
                gmin_vtx = vtx;
            }
        }
        dsm_mutex_unlock(&lock_gmin);
        
        udist = gmin_dist;
        u = gmin_vtx;
        
        if (u == -1) {
            // 所有剩余顶点都不可达
            break;
        }
        
        // 存储全局最小距离的进程将该顶点标记为已处理
        if (u >= firstvtx && u <= lastvtx) {
            marker[u - firstvtx] = 0;
        }
        
        dsm_barrier();  // 确保所有进程都获取了相同的 u 和 udist
        
        // Step 3: 更新距离（松弛操作）
        for (j = 0; j < nlocal; j++) {
            if (marker[j]) {
                int new_dist = udist + wgt[u * n + (firstvtx + j)];
                if (new_dist < lengths[j] && wgt[u * n + (firstvtx + j)] != MAXINT) {
                    lengths[j] = new_dist;
                }
            }
        }
        
        dsm_barrier();  // 确保所有进程完成更新后再进行下一轮
    }
    
    free(marker);
    
    std::cout << "[Pod " << myrank << "] Dijkstra completed." << std::endl;
}

int main() {
    std::cout << "========== DSM: Dijkstra's Shortest Path Algorithm ==========" << std::endl;
    
    // 初始化DSM，分配足够的共享内存页
    int memsize = 200;  // 根据图的大小调整
    int result = dsm_init(memsize);
    if (result != 0) {
        std::cerr << "[Error] dsm_init() failed, return value: " << result << std::endl;
        return 1;
    }
    std::cout << "[Info] DSM initialized successfully." << std::endl;
    
    
    dsm_barrier();
    
    int myrank = dsm_getpodid();
    int npes = ProcNum;
    int lock_gmin = dsm_mutex_init();
    
    // ============ 图参数配置 ============
    // 根据 wgt.txt 数据文件：6x6 邻接矩阵，6个顶点
    const int N = 6;  // 顶点数量（必须与 wgt.txt 中的数据一致）
    
    // 从共享内存加载邻接矩阵（权重矩阵）
    // wgt[i*n + j] 表示从顶点i到顶点j的边权重，999999表示无边
    int *wgt = (int *)dsm_malloc("$HOME/dsm/wgt", nullptr);
    
    int n = N;  // 使用明确的顶点数
    
    if (myrank == 0) {
        std::cout << "[Info] Graph size: " << n << " vertices" << std::endl;
        std::cout << "[Info] Number of processes: " << npes << std::endl;
    }
    
    // 每个进程负责的顶点数量
    int nlocal = n / npes;
    if (myrank == npes - 1) {
        nlocal = n - myrank * (n / npes);
    }
    
    // 分配本地 lengths 数组（存储从源点到本地顶点的最短距离）
    int *lengths = (int *)malloc(nlocal * sizeof(int));
    
    // 设置源顶点（起点）
    int source = 0;
    
    dsm_barrier();
    
    // 执行Dijkstra算法
    SingleSource_DSM(n, source, wgt, lengths, lock_gmin);
    
    dsm_barrier();
    
    // 输出结果

    int firstvtx = myrank * (n / npes);
    std::cout << "[Pod " << myrank << "] Shortest distances from vertex " << source << ":" << std::endl;
    for (int j = 0; j < nlocal; j++) {
        int vtx = firstvtx + j;
        if (lengths[j] == MAXINT) {
            std::cout << "To vertex " << vtx << ": INF (unreachable)" << std::endl;
        } else {
            std::cout << "  To vertex " << vtx << ": " << lengths[j] << std::endl;
        }
    }

    
    
    free(lengths);
    
    dsm_barrier();
    
    dsm_finalize();
    std::cout << "[Info] Program terminated normally." << std::endl;
    
    return 0;
}
