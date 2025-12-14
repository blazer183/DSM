#include <cassert>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <unistd.h>

#include "dsm.h"


const int M = 4;  // A的行数，C的行数
const int K = 4;  // A的列数，B的行数
const int N = 4;  // B的列数，C的列数


int main() {
    std::cout << "========== DSM: Matrix Multiplication (C = A * B) ==========" << std::endl;
    

    int memsize = 100;  // 根据矩阵大小调整
    int result = dsm_init(memsize);
    if (result != 0) {
        std::cerr << "[Error] dsm_init() failed, return value: " << result << std::endl;
        return 1;
    }
    
    dsm_barrier();
    
    int myrank = dsm_getpodid();
    int npes = ProcNum;
    
    
    // 从共享内存加载矩阵
    // A: M x K 矩阵，存储在文件 $HOME/dsm/A
    // B: K x N 矩阵，存储在文件 $HOME/dsm/B
    // C: M x N 矩阵，存储在文件 $HOME/dsm/C（结果）
    int *A = (int *)dsm_malloc("$HOME/dsm/A", nullptr);
    int *B = (int *)dsm_malloc("$HOME/dsm/B", nullptr);
    int *C = (int *)dsm_malloc("$HOME/dsm/C", nullptr);

    int lock_A = dsm_mutex_init();
    
    
    dsm_barrier();
    //矩阵分块乘法
    //要求：访问共享区必须加锁，因为dsm_mutex_lock才会把页置无效，如果不调用lock就不会触发缺页
    //在main函数中执行，不要使用栈空间
    //建议把数组拷贝到本地就立即释放锁
    
    // 使用malloc分配本地数组（不使用栈空间）
    int *local_A = (int *)malloc(M * K * sizeof(int));
    int *local_B = (int *)malloc(K * N * sizeof(int));
    int *local_C = (int *)malloc(M * N * sizeof(int));
    
    // 计算每个进程负责的行范围
    int rows_per_proc = M / npes;
    int start_row = myrank * rows_per_proc;
    int end_row = (myrank == npes - 1) ? M : start_row + rows_per_proc;
    
    //std::cout << "[Pod " << myrank << "] Processing rows " << start_row << " to " << end_row - 1 << std::endl;
    
    // 加锁后拷贝矩阵A和B到本地，然后立即释放锁
    //dsm_mutex_lock(&lock_A);
    memcpy(local_A, A, M * K * sizeof(int));
    memcpy(local_B, B, K * N * sizeof(int));
    //dsm_mutex_unlock(&lock_A);
    
    // 初始化本地结果矩阵C为0
    memset(local_C, 0, M * N * sizeof(int));
    
    // 计算分配给本进程的行
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < N; j++) {
            int sum = 0;
            for (int k = 0; k < K; k++) {
                sum += local_A[i * K + k] * local_B[k * N + j];
            }
            local_C[i * N + j] = sum;
        }
    }
    
    //std::cout << "[Pod " << myrank << "] Local computation completed." << std::endl;

    std::cout << "\n[Result] Local Matrix C = A * B:" << std::endl;
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            std::cout << local_C[i * N + j] << "\t";
        }
        std::cout << std::endl;
    }
    
    dsm_barrier();
    
    // 加锁后将本进程计算的结果写回共享内存C
    dsm_mutex_lock(&lock_A);
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < N; j++) {
            C[i * N + j] = local_C[i * N + j];
        }
    }
    dsm_mutex_unlock(&lock_A);
    
    dsm_barrier();
    
    // 进程0输出结果矩阵
    if (myrank == 0) {
        dsm_mutex_lock(&lock_A);
        std::cout << "\n[Result] Matrix C = A * B:" << std::endl;
        for (int i = 0; i < M; i++) {
            for (int j = 0; j < N; j++) {
                std::cout << C[i * N + j] << "\t";
            }
            std::cout << std::endl;
        }
        dsm_mutex_unlock(&lock_A);
    }
    
    // 释放本地分配的内存
    free(local_A);
    free(local_B);
    free(local_C);
    
    dsm_finalize();
    std::cout << "[Pod " << myrank << "] Program terminated normally." << std::endl;
    
    return 0;
}
