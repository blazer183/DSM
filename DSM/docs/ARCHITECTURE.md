本DSM项目划分为三个部分：concurrent（监听线程部分）net(网络流读取和通信报文规范部分) os（类操作系统部分）

解释一下os: DSM的初衷是让程序员将并发编程无痛迁移到多机上，os部分为程序员提供了并发编程部分系统调用的假象，故此称为os部分



文件夹 PATH 列表
卷序列号为 7A53-DBC3
C:.
├─config    	//集群配置
├─docs      	//新人上手必看，包含项目简介，快速上手，使用说明
├─include   	//头文件，展开后供各个模块复用
│  ├─concurrent    //监听线程部分
│  ├─net    	//报文规范，流式读取函数的声明
│  └─os     	//定义全局数据结构
├─scripts	 //初始化脚本，init_ssh用于配置节点间的ssh登录，launch.sh用于主节点编译后分发可执行程序，分发逻辑为主节点1个进程，从节点进程RR算法分配
├─src			//源代码实现
│  ├─concurrent    //监听线程实现
│  ├─network         //RIO读取函数封装
│  ├─os    		//dsm用户接口的实现
└─tests
    ├─integration     //集成测试
    └─unit          	//单元测试




# 报文通信规范：

报文头：

```
typedef struct {
    uint8_t  type;           // dsm_msg_type_t
    uint8_t  unused;         // 保留位 (页/锁判断；realowner/data判断）
    uint16_t src_node_id;    // 发送方ID (-1/255 表示未知)
    uint32_t seq_num;        // 序列号 (处理乱序/丢包)
    uint32_t payload_len;    // 后续负载长度 (不含包头)
} __attribute__((packed)) dsm_header_t;
```

## seq_num 管理规范

- 每个 TCP 连接维护独立的 32 位递增计数器，从 **1** 开始；每发送一条 DSM 消息就把当前序列号写入 `seq_num` 并自增一次。
- 发送端同时建立 `seq_num -> 上下文` 的映射（比如等待的线程或回调），只有在收到对端响应或超时后才移除。
- 接收端在回复报文中原样携带同一个 `seq_num`
- 即使当前实现是阻塞发送，也保留这一机制，为后续并发请求、重试、乱序处理预留扩展空间。

# Tips: 看管理规范第三条：接收端在回复报文中原样携带同一个 `seq_num`！！！！以下回复报文中的seq_num都等于发送的请求的seq_num，不是全局变量！！！

## 情景1：全局同步 （加入集群也是通过barrier实现的）

发送方：计算进程调用dsm_barrier()时

发送消息：

```
struct {
	DSM_MSG_JOIN_REQ
	0/1		
	PodId	//区分发送方进程的ID
	seq_num		//从1开始计数
	0		//无负载
}
```

回复方：PodId = 0的监听线程 （注意，主节点/0号节点只有一个进程，节点ID就是进程ID）

回复消息：

```
struct {
	DSM_MSG_ACK	//注意：共用一个ACK
	0/1
	0		//ID = 0的主进程
	seq_num		//1开始计数
	0		//无负载
}
```

## 情景2：缺页处理

发送方：计算进程缺页处理函数，发送probowner=hash(page_index) 

发送消息：

```
struct {
	DSM_MSG_PAGE_REQ
	0/1		//都行
	PodId
	seq_num
	sizeof(payload)
}

payload: 
struct {
	page_index	//uint32_t类型，标识虚拟页号
}
```

回复方：监听进程，**<u>首先需要处理多个进程争用同一页的问题，必须顺序访问页</u>**（感觉这一步很难处理，初步版本是参考锁争用的实现，也就是在页表的**每一项**集成一个锁，想访问某页需要先获取该页对应的锁）；获得锁后，查owner_id

# Tips: 这里的PageTable和LockTable都包含两种锁：一种是保护整个表的（全局锁），一种是保护其中某一个原子的（局部锁）。调用原则：要想访问表先调用全局锁；如果需要获取局部锁，应先释放全局锁，再申请局部锁；成功申请局部锁后，应再次申请全局锁，实现对表的增删查改，并在释放局部锁前释放全局锁。

### 情况1：owner_id = -1 && PodId != 0 

即首次缺页，还没有数据，向PodId = 0的监听进程请求调页

这里很特殊 是监听线程-监听线程 无法复用已有的socket ，需要新创建一个socket（记得在完成后销毁线程）

发送消息同上

### 情况2：owner_id = -1 && PodId = 0 

这需要监听进程查bind表将文件载入内存并传送

返回消息同下

### 情况3：owner_id == PodId 

说明自己是拥有者 向请求者发送页面

```
head:
struct {
	DSM_MSG_PAGE_REP
	1		//重要！！1表示“真”，说明自己是拥有者，发送页面
	PodId
	seq_num
	sizeof(payload)
}

payload:
// [DSM_MSG_PAGE_REP] Manager -> Requestor
typedef struct {
    uint16_t real_owner_id;	//垃圾信息
    char pagedata[DSM_PAGE_SIZE]
} __attribute__((packed)) payload_page_rep_t;
```

### 情况4：owner_id != PodId

 则返回PodId给请求者

```
head:
struct {
	DSM_MSG_PAGE_REP
	0		//重要！！0表示“假”，返回真拥有者ID
	PodId
	seq_num
	sizeof(payload)
}

payload: 
// [DSM_MSG_PAGE_REP] Manager -> Requestor
typedef struct {
    uint16_t real_owner_id;
    char pagedata[DSM_PAGE_SIZE]//垃圾信息
} __attribute__((packed)) payload_page_rep_t;
```

## 情景3：锁请求

发送方：计算进程的dsm_mutex_lock函数，发送给probowner = hash(lock_id)

发送消息：

```
struct {
	DSM_MSG_LOCK_ACQ
	0/1
	PodId
	seq_num
	sizeof(payload)
}

payload:
struct {
	lock_id	//uint32_t类型 锁ID
}
```

回复方：监听进程，首先每个监听线程都要去获取该锁对应的锁（第一个锁是全局锁，第二个锁是为了保证对全局锁的顺序访问）

回复信息：

```
struct {
	DSM_MSG_LOCK_REP 
	1				//！！！
	PodId
	seq_num
	sizeof(payload)
}

payload 
// [DSM_MSG_LOCK_REP] Manager -> Requestor (授予锁)
typedef struct {
    uint32_t invalid_set_count; 
    vector invalid_page_list;
} __attribute__((packed)) payload_lock_rep_t;
```

## 情景4：锁释放

// [DSM_MSG_LOCK_RLS] LockOwner -> Manager (释放锁)

发送：

​	报文头：

```
typedef struct {
    DSM_MSG_LOCK_RLS           
    0/1         
    PodId    
    seq_num        
    sizeof(负载)    
} __attribute__((packed)) dsm_header_t;
```

​	负载：

```
// [DSM_MSG_LOCK_RLS] LockOwner -> Manager (释放锁)
typedef struct {
    uint32_t invalid_set_count; // Scope Consistency: 需要失效的页数量
    vector invalid_page_list;
} __attribute__((packed)) payload_lock_rls_t;
```

回复：ACK

## 情景5：页表更新

// [DSM_MSG_OWNER_UPDATE] RealOwner -> Manager

发送：

头：

```
typedef struct {
    uint8_t  type;           // dsm_msg_type_t
    uint8_t  unused;         // 保留位 （消息复用时的区分）
    uint16_t src_node_id;    // 发送方ID (-1/255 表示未知)
    uint32_t seq_num;        // 序列号 (处理乱序/丢包)
    uint32_t payload_len;    // 后续负载长度 (不含包头)
} __attribute__((packed)) dsm_header_t;
```

负载：

```
// [DSM_MSG_OWNER_UPDATE] RealOwner -> Manager
typedef struct {
    uint32_t resource_id;    // 页号
    uint16_t new_owner_id;   // 页面最新副本在哪里
} __attribute__((packed)) payload_owner_update_t;
```

