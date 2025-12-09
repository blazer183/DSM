#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <type_traits>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <vector>
#include <sstream>

#include "dsm.h"
#include "net/protocol.h"
#include "os/lock_table.h"
#include "os/page_table.h"
#include "os/socket_table.h"
#include "os/pfhandler.h"

extern void dsm_start_daemon(int port);

// 外部引用全局变量
extern struct PageTable *PageTable;
extern struct LockTable *LockTable;
extern struct BindTable *BindTable;
extern struct SocketTable *SocketTable;

extern size_t SharedPages;
extern int PodId;
extern void *SharedAddrBase;
extern void *SharedAddrCurrentLoc ;
extern int ProcNum;
extern int WorkerNodeNum;
extern std::vector<std::string> WorkerNodeIps;
extern int* InvalidPages;

extern int SAB_VPNumber ;           //共享区起始虚拟页号
extern int SAC_VPNumber ;           //共享区下一次分配的空间的虚拟页号



// 外部引用来自 dsm_os.cpp 的函数
extern std::string GetPodIp(int pod_id);
extern int GetPodPort(int pod_id);

// Helper function to get or create a socket connection to a remote pod
// Returns existing socket if already connected, creates new connection otherwise
int getsocket(const std::string& ip, int port) {
    if (SocketTable == nullptr) {
        std::cerr << "[getsocket] SocketTable not initialized" << std::endl;
        return -1;
    }

    // Check if socket already exists for this target
    int target_node = -1;
    for (int i = 0; i < ProcNum; i++) {
        if (GetPodIp(i) == ip && GetPodPort(i) == port) {
            target_node = i;
            break;
        }
    }

    if (target_node >= 0) {
        SocketTable->GlobalMutexLock();
        SocketRecord* record = SocketTable->Find(target_node);
        if (record != nullptr && record->socket >= 0) {
            int existing_sock = record->socket;
            SocketTable->GlobalMutexUnlock();
            return existing_sock;
        }
        SocketTable->GlobalMutexUnlock();
    }

    // Create new socket connection
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "[getsocket] Failed to create socket: " << std::strerror(errno) << std::endl;
        return -1;
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "[getsocket] Invalid address: " << ip << std::endl;
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[getsocket] Failed to connect to " << ip << ":" << port 
                  << " - " << std::strerror(errno) << std::endl;
        close(sockfd);
        return -1;
    }

    // Store socket in SocketTable if we identified the target node
    if (target_node >= 0) {
        SocketTable->GlobalMutexLock();
        SocketRecord new_record;
        new_record.socket = sockfd;
        new_record.next_seq = 1;
        SocketTable->Insert(target_node, new_record);
        SocketTable->GlobalMutexUnlock();
    }

    return sockfd;
}

template<typename T>
bool GetEnvVar(const char* name, T& value, const T& default_val, bool required = true) {
    const char* env_val = std::getenv(name);
    if (env_val != nullptr) {
        if constexpr (std::is_same_v<T, int>) {
            value = std::atoi(env_val);
        } else if constexpr (std::is_same_v<T, std::string>) {
            value = env_val;
        }
        std::cout << "[DSM Info] " << name << ": " << value << std::endl;
        return true;
    } else {
        if (required) {
            std::cerr << "[DSM Warning] " << name << " not set! exit!" << std::endl;
            return false;
        } else {
            value = default_val;
            std::cerr << "[DSM Warning] " << name << " not set! Using default: " << default_val << std::endl;
            return true;
        }
    }
}

bool LaunchListenerThread(int Port)
{
   try {
      std::thread listener([Port]() {
         dsm_start_daemon(Port);
      });
      listener.detach();
      sleep(1);
      return true;
   } catch (const std::system_error &err) {
      std::cerr << "[dsm] failed to launch listener thread: " << err.what() << std::endl;
      return false;
   }
}

bool FetchGlobalData(int dsm_pagenum, std::string& LeaderNodeIp, int& LeaderNodePort)
{
    // dsm_pagenum is the memory size in bytes, calculate the number of pages
    // Use ceiling division to ensure we have enough pages
    SharedPages = dsm_pagenum ;
    SharedAddrBase = reinterpret_cast<void *>(0x4000000000ULL); 
    SharedAddrCurrentLoc = SharedAddrBase;  // Initialize current location
    
    // Calculate virtual page number for shared address base
    // SAB_VPNumber is the virtual page number of SharedAddrBase
    SAB_VPNumber = static_cast<int>(reinterpret_cast<uintptr_t>(SharedAddrBase) / PAGESIZE);
    // SAC_VPNumber starts at the same page number as SAB
    SAC_VPNumber = SAB_VPNumber;
    
    if (!GetEnvVar("DSM_LEADER_IP", LeaderNodeIp, std::string(""), true)) exit(1);
    if (!GetEnvVar("DSM_LEADER_PORT", LeaderNodePort, 0, true)) exit(1);
    if (!GetEnvVar("DSM_TOTAL_PROCESSES", ProcNum, 1, false)) exit(1);
    if (!GetEnvVar("DSM_POD_ID", PodId, -1, false)) exit(1);
    if (!GetEnvVar("DSM_WORKER_COUNT", WorkerNodeNum, 0, false)) exit(1);
    std::string worker_ips_str;
    if (!GetEnvVar("DSM_WORKER_IPS", worker_ips_str, std::string(""), false)) exit(1);
    WorkerNodeIps.clear();
    if (!worker_ips_str.empty()) {
        std::stringstream ss(worker_ips_str);
        std::string ip;
        while (std::getline(ss, ip, ',')) {
            WorkerNodeIps.push_back(ip);
        }
        std::cout << "[DSM Info] Parsed " << WorkerNodeIps.size() << " worker IPs" << std::endl;
    }
    const bool ok = (PodId >= 0) && (SharedAddrBase != nullptr) && (SharedPages > 0);
    if (!ok) {
        std::cerr << "[dsm] invalid shared region parameters (PodID=" << PodId
                  << ", base=" << SharedAddrBase << ", pages=" << SharedPages << ")" << std::endl;
        return false;
    }
    return true;
}

bool InitDataStructs(int dsm_pagenum)
{   
   // Initialize shared memory region
   // Note: dsm_pagenum is the memory size in bytes, we already calculated SharedPages
   if (SharedAddrBase != nullptr && SharedPages != 0){
      const size_t total_size = SharedPages * PAGESIZE;  // Use SharedPages calculated from dsm_pagenum
      void* mapped_addr = ::mmap(
         SharedAddrBase,                    // Desired start address
         total_size,                        // Size of the mapping
         PROT_NONE,                        // Initial protection: no access, to trigger page faults
         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,  // Private anonymous mapping, fixed address
         -1,                               // No file descriptor
         0                                 // Offset 0
      );
      if (mapped_addr == MAP_FAILED) {
         std::cerr << "[dsm] mmap failed at address " << SharedAddrBase 
                  << " size " << total_size << ": " << std::strerror(errno) << std::endl;
         return false;
      }


      InvalidPages = new int[SharedPages];
      std::memset(InvalidPages, 0, sizeof(int) * SharedPages);
      install_handler(SharedAddrBase, SharedPages);
   }else {
      std::cerr << "[dsm] invalid shared region parameters (base=" << SharedAddrBase 
               << ", pages=" << SharedPages << ")" << std::endl;
      return false;
   }

   // Initialize page, lock, bind, and socket tables
   if (PageTable == nullptr)
      PageTable = new (::std::nothrow) class PageTable();
   
   // Initialize page table entries for all shared pages
   // SAB_VPNumber is the base virtual page number of SharedAddrBase
   if (PageTable != nullptr) {
      PageTable->GlobalMutexLock();
      for (size_t i = 0; i < SharedPages; i++) {
         PageTable->Insert(SAB_VPNumber + static_cast<int>(i), PageRecord());
      }
      PageTable->GlobalMutexUnlock();
   }
   
   if (LockTable == nullptr)
      LockTable = new (::std::nothrow) class LockTable();
   if (SocketTable == nullptr)
      SocketTable = new (::std::nothrow) class SocketTable();
   const bool ok = (PageTable != nullptr) && (LockTable != nullptr) && (SocketTable != nullptr);
   if (!ok){
      std::cerr << "[dsm] failed to allocate metadata tables" << std::endl;
      return false;
   }
   return true;
}
