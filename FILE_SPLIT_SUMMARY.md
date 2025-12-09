# DSM 文件拆分总结

## 拆分概览

将 `dsm_os.cpp` 拆分为两个文件:
- **dsm_os.cpp**: 保留所有在 `dsm.h` 中声明的公共 API 函数
- **dsm_os_cond.cpp**: 包含内部辅助函数

## 拆分原则

1. **公共 API (保留在 dsm_os.cpp)**:
   - 所有在 `dsm.h` 中声明的函数
   - 全局变量定义
   - 静态变量定义

2. **辅助函数 (移动到 dsm_os_cond.cpp)**:
   - `GetEnvVar()` - 环境变量读取模板函数
   - `getsocket()` - Socket 连接管理
   - `LaunchListenerThread()` - 守护线程启动
   - `FetchGlobalData()` - 全局数据初始化
   - `InitDataStructs()` - 数据结构初始化

## 文件修改详情

### 1. dsm_os.cpp 修改

#### 删除的函数定义
- `int getsocket(const std::string& ip, int port)`
- `template<typename T> bool GetEnvVar(...)`
- `bool LaunchListenerThread(int Port)`
- `bool FetchGlobalData(int dsm_memsize)`
- `bool InitDataStructs(int dsm_memsize)`

#### 添加的外部声明
```cpp
extern int getsocket(const std::string& ip, int port);
extern bool LaunchListenerThread(int Port);
extern bool FetchGlobalData(int dsm_memsize, std::string& LeaderNodeIp, int& LeaderNodePort);
extern bool InitDataStructs(int dsm_memsize);
```

#### 保留的全局变量
```cpp
struct PageTable *PageTable = nullptr;
struct LockTable *LockTable = nullptr;
struct BindTable *BindTable = nullptr;
struct SocketTable *SocketTable = nullptr;
size_t SharedPages = 0;
int PodId = -1;
void *SharedAddrBase = nullptr;
int ProcNum = 0;
int WorkerNodeNum = 0;
std::vector<std::string> WorkerNodeIps;
int* InvalidPages = nullptr;
int* ValidPages = nullptr;
int* DirtyPages = nullptr;
STATIC std::string LeaderNodeIp;
STATIC int LeaderNodePort = 0;
STATIC int LeaderNodeSocket = -1;
STATIC int LockNum = 0;
STATIC void * SharedAddrCurrentLoc = nullptr;
```

#### 修改的函数调用
```cpp
// 旧版本
if (!FetchGlobalData(dsm_memsize))

// 新版本
if (!FetchGlobalData(dsm_memsize, LeaderNodeIp, LeaderNodePort))
```

#### 添加的初始化
```cpp
int dsm_init(int dsm_memsize)       
{
    if (!FetchGlobalData(dsm_memsize, LeaderNodeIp, LeaderNodePort))
        return -1;
    SharedAddrCurrentLoc = SharedAddrBase;  // 初始化当前位置
    if(!LaunchListenerThread(LeaderNodePort+PodId))
        return -2;
    if (!InitDataStructs(dsm_memsize))
        return -3;
    return 0;
}
```

### 2. dsm_os_cond.cpp (新文件)

#### 包含的头文件
```cpp
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
#include "os/bind_table.h"
#include "os/lock_table.h"
#include "os/page_table.h"
#include "os/socket_table.h"
#include "os/pfhandler.h"
```

#### 外部声明
```cpp
extern void dsm_start_daemon(int port);

// 全局变量
extern struct PageTable *PageTable;
extern struct LockTable *LockTable;
extern struct BindTable *BindTable;
extern struct SocketTable *SocketTable;
extern size_t SharedPages;
extern int PodId;
extern void *SharedAddrBase;
extern int ProcNum;
extern int WorkerNodeNum;
extern std::vector<std::string> WorkerNodeIps;
extern int* InvalidPages;
extern int* ValidPages;
extern int* DirtyPages;

// dsm_os.cpp 中的函数
extern std::string GetPodIp(int pod_id);
extern int GetPodPort(int pod_id);
```

#### 实现的辅助函数
1. **GetEnvVar** - 环境变量读取模板
2. **getsocket** - Socket 连接获取/创建
3. **LaunchListenerThread** - 守护线程启动
4. **FetchGlobalData** - 全局数据初始化(签名更新)
5. **InitDataStructs** - 数据结构初始化

#### 函数签名变化
```cpp
// 旧签名
bool FetchGlobalData(int dsm_memsize)

// 新签名 (通过引用返回)
bool FetchGlobalData(int dsm_memsize, std::string& LeaderNodeIp, int& LeaderNodePort)
```

### 3. launcher.sh 修改

#### 编译命令更新
```bash
# 旧版本
BUILD_CMD='g++ -std=c++17 -pthread -DUNITEST -I"DSM/include" test_dsm.cpp \
  "DSM/src/os/dsm_os.cpp" "DSM/src/os/pfhandler.cpp" \
  "DSM/src/concurrent/concurrent_daemon.cpp" "DSM/src/network/connection.cpp" \
  -o dsm_app -lpthread'

# 新版本 (添加 dsm_os_cond.cpp)
BUILD_CMD='g++ -std=c++17 -pthread -DUNITEST -I"DSM/include" test_dsm.cpp \
  "DSM/src/os/dsm_os.cpp" "DSM/src/os/dsm_os_cond.cpp" \
  "DSM/src/os/pfhandler.cpp" "DSM/src/concurrent/concurrent_daemon.cpp" \
  "DSM/src/network/connection.cpp" -o dsm_app -lpthread'
```

## 设计决策

### 1. 静态变量处理
- `LeaderNodeIp`, `LeaderNodePort`, `SharedAddrCurrentLoc` 等静态变量保留在 `dsm_os.cpp` 中
- 通过引用参数传递给需要修改它们的辅助函数
- 在 `dsm_init` 中直接初始化 `SharedAddrCurrentLoc`

### 2. 全局变量共享
- 所有全局变量定义在 `dsm_os.cpp` 中
- `dsm_os_cond.cpp` 通过 `extern` 声明访问
- 确保单一定义规则 (ODR)

### 3. 函数访问
- 辅助函数通过 `extern` 声明在 `dsm_os.cpp` 中可见
- 公共函数(如 `GetPodIp`, `GetPodPort`) 通过 `extern` 声明在 `dsm_os_cond.cpp` 中可见

### 4. 依赖关系
```
dsm_os.cpp (公共 API)
    ↓ 调用
dsm_os_cond.cpp (辅助函数)
    ↓ 访问
全局变量 (定义在 dsm_os.cpp)
```

## 验证检查清单

- [x] 所有辅助函数已从 dsm_os.cpp 删除
- [x] 辅助函数在 dsm_os_cond.cpp 中实现
- [x] extern 声明添加到 dsm_os.cpp
- [x] 全局变量定义保留在 dsm_os.cpp
- [x] extern 声明添加到 dsm_os_cond.cpp
- [x] 函数签名更新(FetchGlobalData)
- [x] 函数调用更新(dsm_init)
- [x] 编译命令更新(launcher.sh)
- [x] SharedAddrCurrentLoc 初始化位置调整

## 编译和测试

### 编译命令
```bash
cd /path/to/DSM_test_generation
./DSM/scripts/launcher.sh
```

### 预期行为
1. 编译成功,生成 `dsm_app` 可执行文件
2. 运行时正确加载环境变量
3. 辅助函数正确调用
4. 静态变量正确初始化

## 注意事项

1. **Windows 环境**: 当前代码使用 Linux 特定头文件(`arpa/inet.h`, `sys/socket.h` 等),需要在 Linux 环境下编译
2. **线程安全**: 全局变量的访问需要确保线程安全
3. **初始化顺序**: `SharedAddrCurrentLoc` 必须在 `FetchGlobalData` 之后、`InitDataStructs` 之前初始化

## 未来改进建议

1. 考虑将辅助函数放入匿名命名空间或 `detail` 命名空间
2. 使用 RAII 封装资源管理
3. 减少全局变量使用,考虑封装到单例类中
4. 添加单元测试验证文件拆分的正确性
