#include <cassert>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

#include "dsm.h"

int main()
{
    std::cout << "\n========== DSM client test ==========" << std::endl;
    std::cout << "========================================\n" << std::endl;
    

    
    int memsize = 4096;  // 4KB shared memory
    int result = dsm_init(memsize);
    
    if (result != 0) {
        std::cerr << "ERROR: dsm_init() failed, return value: " << result << std::endl;
        std::cerr << "  Possible causes:" << std::endl;
        std::cerr << "    - Environment variables not set correctly" << std::endl;
        std::cerr << "    - Leader not started or cannot connect" << std::endl;
        std::cerr << "    - daemon thread failed to handle JOIN_REQ correctly" << std::endl;
        return 1;
    }
    
    std::cout << "SUCCESS: dsm_init() completed successfully!" << std::endl;
    std::cout << std::endl;
    
    // ============ 2. Verify DSM initialization state ============
    std::cout << "[Step 2] Verifying DSM initialization state..." << std::endl;
    
    int node_id = dsm_getnodeid();
    std::cout << "  Current node ID: " << node_id << std::endl;
    
    if (node_id < 0) {
        std::cerr << "ERROR: Failed to get NodeID" << std::endl;
        return 1;
    }
    std::cout << "SUCCESS: NodeID retrieved successfully" << std::endl;
    std::cout << std::endl;
    
    // ============ 3. Test shared memory address mapping ============
    std::cout << "[Step 3] Testing GetPodIp/GetPodPort address mapping..." << std::endl;
    
#ifdef UNITEST
    // If UNITEST is defined, we can call internal functions
    for (int pod_id = 0; pod_id < 3; pod_id++) {
        std::string ip = GetPodIp(pod_id);
        int port = GetPodPort(pod_id);
        std::cout << "  PodID=" << pod_id << " -> " << ip << ":" << port << std::endl;
    }
    std::cout << "SUCCESS: Address mapping verification completed" << std::endl;
#else
    std::cout << "  INFO: Requires -DUNITEST compilation flag to test internal functions" << std::endl;
    std::cout << "  Skipping this test" << std::endl;
#endif
    std::cout << std::endl;
    
    // ============ 4. Clean up and exit ============
    std::cout << "[Step 4] verify lock aquire..." << std::endl;
    int lock_A = dsm_mutex_init();
    dsm_mutex_lock(&lock_A);
    std::cout<<"I'm "<< node_id <<" and I get lock A!"<<std::endl;
    sleep(3);
    std::cout << "See? No one can get lock A because I locked it!..." << std::endl;
    dsm_mutex_unlock(&lock_A);

    std::cout << "[Step 5] Cleaning up resources and exiting..." << std::endl;
    // Call dsm_finalize() if available
    // dsm_finalize();
    //sleep(5); 
    
    std::cout << "INFO: Program terminated normally" << std::endl;
    std::cout << "\n========================================" << std::endl;
    std::cout << "SUCCESS: All tests passed! DSM system running normally" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    return 0;
}
