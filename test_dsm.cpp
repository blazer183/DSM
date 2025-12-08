#include <cassert>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

#include "dsm.h"

void cluster_config(){
    std::cout << "[System information] Cluster configuration:" << std::endl;
    for (int pod_id = 0; pod_id < ProcNum; pod_id++) {
        std::string ip = GetPodIp(pod_id);
        int port = GetPodPort(pod_id);
        std::cout << "  PodID=" << pod_id << " -> " << ip << ":" << port << std::endl;
    }
}

int main(){
    std::cout << "========== DSM: Sum of array test ==========" << std::endl;
    
    int memsize = PAGESIZE + sizeof(int);  
    int result = dsm_init(memsize);   
    if (result != 0) {
        std::cerr << "[System information] ERROR: dsm_init() failed, return value: " << result << std::endl;
        return 1;
    }else{
        std::cout << "[System information] SUCCESS: dsm_init() completed successfully!" << std::endl;
    }

    dsm_barrier();

    cluster_config();
    
    int myrank = dsm_getpodid();
    int elen = PAGESIZE / sizeof(int);
    int *Ar = (int *) dsm_malloc(elen * sizeof(int));     //分配
    int *sum = (int *)dsm_malloc(sizeof(int));
    dsm_bind(Ar, "$HOME/dsm/array", sizeof(int));                         //绑定数据源
    dsm_bind(sum, "$HOME/dsm/sum", sizeof(int));

    int lock_A = dsm_mutex_init();

    dsm_mutex_lock(&lock_A);
    for(int i = 0; i < elen; i+=ProcNum)
        (*sum) = (*sum) + Ar[i];
    dsm_mutex_unlock(&lock_A);

    dsm_barrier();
    
    if(myrank == 0){
        std::cout << "The sum of array Ar is " << (*sum) << std::endl;
    }
    std::cout << "[System information] Program terminated normally" << std::endl;

    dsm_finalize();
    return 0;
}
