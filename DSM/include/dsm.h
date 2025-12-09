#ifndef DSM_H
#define DSM_H

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>


#define PAGESIZE 4096


struct PageTable;
struct LockTable;
struct BindTable;
struct SocketTable;



extern struct PageTable *PageTable;         // 
extern struct LockTable *LockTable;         // 
extern struct BindTable *BindTable;         // 
extern struct SocketTable *SocketTable;     // 

extern size_t SharedPages;                  //
extern int PodId;                           // 
extern void *SharedAddrBase;                // 
extern int ProcNum;                         // 
extern int WorkerNodeNum;                   // 
extern std::vector<std::string> WorkerNodeIps;  // 



int dsm_init(int dsm_memsize);  

int dsm_finalize(void);
int dsm_getpodid(void);

int dsm_mutex_init();
int dsm_mutex_destroy(int *mutex);
int dsm_mutex_lock(int *mutex);
int dsm_mutex_unlock(int *mutex);
void* dsm_malloc(const char *name, int * num);

bool dsm_barrier(void);




std::string GetPodIp(int pod_id);      //
int GetPodPort(int pod_id);            // 

#endif /* DSM_H */
