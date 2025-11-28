/*1. 保护与基础引用区：如果未包含头文件（tag为DSM_H的），则包含，整个头文件都在def范围内*/
#ifndef DSM_H
#define DSM_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* ————————————————————！！！！！重要！！！！！————————————————————————

变量与数据结构的声明规则：
1.宏：全大写
2.全局变量/全局数据结构：首字母大写，其余小写，多个单词直接相连 比如：SharedPages
3.结构体内部变量：全小写，多个单词用下划线连接 比如：lock_id

4.局部变量/局部数据结构：不做要求

*/


//2.宏常量声明区

#define PAGESIZE 4096


//3.全局类型声明区

struct PageTable;
struct LockTable;
struct BindTable;

//4.全局变量声明

extern struct PageTable *PageTable;        // 页表句柄
extern struct LockTable *LockTable;        // 锁表句柄
extern struct BindTable *BindTable;        // Bind 表句柄

extern size_t SharedPages;                 // 页表大小

extern int NodeId;            // 集群ID

extern void *SharedAddrBase;     // 共享区起始地址

//5.全局函数声明

int dsm_init(int argc, char *argv[], int dsm_memsize);  
/*创建监听线程，加入集群并获取id，初始化页表，锁表，Bind表，开辟共享区，初始化barrier*/
int dsm_finalize(void);
int dsm_getnodeid(void);

int dsm_mutex_init(dsm_mutex_t *mutex, void *attr);
int dsm_mutex_destroy(dsm_mutex_t *mutex);
int dsm_mutex_lock(dsm_mutex_t *mutex);
int dsm_mutex_unlock(dsm_mutex_t *mutex);

void dsm_bind(void *addr, const char *name, size_t offset, size_t size);
void dsm_barrier(void);
void *dsm_malloc(size_t size);

#if defined(__cplusplus)
}
#endif

#endif /* DSM_H */
