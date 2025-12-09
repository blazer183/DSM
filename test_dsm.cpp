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
    
    int memsize = 100;  
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
    int elen ;
    int *Ar = (int *) dsm_malloc("$HOME/dsm/Ar", &elen);     //分配
    elen = 9;
    std::cout << "[System information] Ar: " << Ar << std::endl;
    int *sum = (int *) dsm_malloc("$HOME/dsm/sum", nullptr);
    std::cout << "[System information] sum: " << sum << std::endl;
    std::cout << "[System information] Pod " << myrank << " allocated and bound memory." << std::endl;

    int lock_A = dsm_mutex_init();
    int S= 0;

    dsm_mutex_lock(&lock_A);
    for(int i = 0; i < elen; i+=ProcNum)
        S += Ar[i];
    std::cout << "[System information] Ar: " << Ar[0] << std::endl;
    dsm_mutex_unlock(&lock_A);
    std:: cout << "local sum is: "<< S << std::endl;

    dsm_mutex_lock(&lock_A);
    (*sum) = (*sum) + S;
    dsm_mutex_unlock(&lock_A);

    dsm_barrier();
    
    if(myrank == 0){
        std::cout << "The sum of array Ar is " << (*sum) << std::endl;
    }
    std::cout << "[System information] Program terminated normally" << std::endl;

    dsm_finalize();
    return 0;
}
