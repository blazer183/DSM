/*1. ��������������������δ����ͷ�ļ���tagΪDSM_H�ģ��������������ͷ�ļ�����def��Χ��*/
#ifndef DSM_H
#define DSM_H

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>



/* ����������������������������������������������������Ҫ����������������������������������������������������������

���������ݽṹ����������
1.�꣺ȫ��д
2.ȫ�ֱ���/ȫ�����ݽṹ������ĸ��д������Сд���������ֱ������ ���磺SharedPages
3.�ṹ���ڲ�������ȫСд������������»������� ���磺lock_id

4.�ֲ�����/�ֲ����ݽṹ������Ҫ��

*/


//2.�곣��������

#define PAGESIZE 4096


//3.ȫ������������

struct PageTable;
struct LockTable;
struct BindTable;
struct SocketTable;


//4.ȫ�ֱ�������

extern struct PageTable *PageTable;         // ҳ��
extern struct LockTable *LockTable;         // ����
extern struct BindTable *BindTable;         // Bind��
extern struct SocketTable *SocketTable;     // Socket��

extern size_t SharedPages;                  // �������м�ҳ
extern int NodeId;                          // ��ȺID
extern void *SharedAddrBase;                // ��������ʼ��ַ
extern int ProcNum;                         // ��������
extern int WorkerNodeNum;                   // Worker�ڵ�����
extern std::vector<std::string> WorkerNodeIps;  // Worker�ڵ�IP�б�

extern int next_node_id;              // ��һ������Ľڵ�ID



int dsm_init(int dsm_memsize);  
int dsm_finalize(void);
int dsm_getnodeid(void);

int dsm_mutex_init();
int dsm_mutex_destroy(int *mutex);
int dsm_mutex_lock(int *mutex);
int dsm_mutex_unlock(int *mutex);

void dsm_bind(void *addr, const char *name,size_t element_size);
bool dsm_barrier(void);
void *dsm_malloc(size_t size);



std::string GetPodIp(int pod_id);      // ����PodID����IP
int GetPodPort(int pod_id);            // ����PodID���Ҷ˿�

#endif /* DSM_H */
