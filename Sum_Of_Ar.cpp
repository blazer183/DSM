#include <cassert>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

#include "dsm.h"
#include "os/page_table.h"

extern int SAB_VPNumber;  // Base virtual page number of shared region
extern size_t SharedPages; // Number of shared pages

void cluster_config(){
    // Print PageTable entries
    std::cout << "\n========== PageTable Contents ==========" << std::endl;
    
    if (PageTable == nullptr) {
        std::cerr << "[cluster_config] ERROR: PageTable is not initialized!" << std::endl;
        return;
    }
    
    // Lock the global mutex to safely access the table
    PageTable->GlobalMutexLock();
    
    size_t table_size = PageTable->Size();
    std::cout << "[cluster_config] Total entries in PageTable: " << table_size << std::endl;
    std::cout << "[cluster_config] Shared region base VPN: " << SAB_VPNumber 
              << " (0x" << std::hex << SAB_VPNumber << std::dec << ")" << std::endl;
    std::cout << "[cluster_config] Shared pages allocated: " << SharedPages << std::endl;
    
    if (table_size == 0) {
        std::cout << "[cluster_config] PageTable is empty (no pages allocated yet)" << std::endl;
    } else {
        std::cout << "[cluster_config] Listing all page entries:" << std::endl;
        std::cout << "-------------------------------------------" << std::endl;
        std::cout << "VPN\t\tOwner\tFilepath\tOffset\tFD" << std::endl;
        std::cout << "-------------------------------------------" << std::endl;
        
        // Iterate through the actual VPN range used by the shared region
        int vpn_start = SAB_VPNumber;
        int vpn_end = SAB_VPNumber + static_cast<int>(SharedPages);
        int found_count = 0;
        
        for (int vpn = vpn_start; vpn < vpn_end && found_count < static_cast<int>(table_size); vpn++) {
            PageRecord* record = PageTable->Find(vpn);
            if (record != nullptr) {
                found_count++;
                std::cout << vpn << " (0x" << std::hex << vpn << std::dec << ")\t" 
                         << record->owner_id << "\t";
                
                if (record->filepath.empty()) {
                    std::cout << "(none)";
                } else {
                    std::cout << record->filepath;
                }
                
                std::cout << "\t" << record->offset 
                         << "\t" << record->fd << std::endl;
            }
        }
        
        if (found_count < static_cast<int>(table_size)) {
            std::cout << "[cluster_config] Note: Found " << found_count << " out of " 
                     << table_size << " entries" << std::endl;
        }
    }
    
    std::cout << "-------------------------------------------" << std::endl;
    
    PageTable->GlobalMutexUnlock();
    std::cout << "========================================\n" << std::endl;
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

    //cluster_config();
    
    int myrank = dsm_getpodid();
    int elen ;
    int *Ar = (int *) dsm_malloc("$HOME/dsm/Ar", &elen);     //分配
    elen = 9;
    //std::cout << "[System information] Ar: " << Ar << std::endl;
    int *sum = (int *) dsm_malloc("$HOME/dsm/sum", nullptr);
    //std::cout << "[System information] sum: " << sum << std::endl;
    std::cout << "[System information] Pod " << myrank << " allocated and bound memory." << std::endl;

    int lock_A = dsm_mutex_init();
    int S= 0;
    //cluster_config();

    dsm_mutex_lock(&lock_A);
    for(int i = PodId; i < elen; i+=ProcNum){
        S += Ar[i];
    }
    std:: cout << "Local sum is : " << S << std::endl;
    dsm_mutex_unlock(&lock_A);

    dsm_barrier();

    dsm_mutex_lock(&lock_A);
    (*sum) = (*sum) + S;
    dsm_mutex_unlock(&lock_A);


    dsm_barrier();
    if(myrank == 0){
        std :: cout << "Global sum is :" << (*sum) << std::endl;
    }
    
    dsm_finalize();
    std::cout << "[System information] Program terminated normally" << std::endl;
    return 0;
}
