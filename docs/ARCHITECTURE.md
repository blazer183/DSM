文件夹 PATH 列表
卷序列号为 7A53-DBC3
C:.
├─config    //集群描述，包括IP，角色，节点ID等
├─docs      //新人上手必看，包含项目简介，快速上手，使用说明
├─include   //头文件，展开后供各个模块复用
│  ├─net    //报文规范，流式读取函数的声明
│  └─os     //定义页表结构和宏
├─scripts
├─src
│  ├─concurrent //服务端模型
│  ├─network    //报文规范，流式读取函数封装
│  ├─os         //客户端模型
│  └─main.c     //测试程序入口
└─tests
    ├─integration   //集成测试
    └─unit          //单元测试


主要分为src文件夹里的三个模块，其中concurrent与os通过network连接。

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
- 接收端在回复报文中原样携带同一个 `seq_num`，或在 `DSM_MSG_ACK` 的 `payload_ack_t.target_seq` 中填入；这样发送端才能准确匹配“哪条请求已经完成”。
- 即使当前实现是阻塞发送，也保留这一机制，为后续并发请求、重试、乱序处理预留扩展空间。

## 情景1：加入集群的同步，全局同步

> 复用消息类型 DSM_MSG_JOIN_REQ

发送方：计算进程调用dsm_barrier()时

发送消息：

```
struct {
	DSM_MSG_JOIN_REQ
	0/1		//都行，反正用不到
	PodId	//区分发送方进程的ID
	seq_num		//从1开始计数
	0		//无负载
}
```

回复方：PodId = 0的监听线程 （注意，主节点/0号节点只有一个进程，节点ID就是进程ID）

回复消息：

```
struct {
	DSM_MSG_JOIN_ACK
	0/1		//unused
	0		//ID = 0的主进程
	seq_num		//1开始计数
	0		//无负载
}
```

## 情景2：缺页处理

发送方：计算进程缺页处理函数，发送给哈希后的probowner=hash(page_index)

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

回复方：probowner的监听进程，**<u>首先需要处理多个进程争用同一页的问题，必须顺序访问页</u>**（感觉这一步很难处理）；在顺序访问基础上查自己的page table对应表项里的owner_id

### 情况1：owner_id = -1 && PodId != 0 

即首次缺页，还没有数据，向PodId = 0的监听进程请求调页

这里很特殊 是监听线程-监听线程 无法复用已有的socket 

发送消息同上

### 情况2：owner_id = -1 && PodId = 0 

这需要监听进程查bind表将文件载入内存并传送

返回消息同下

### 情况3：owner_id == PodId 

说明自己是拥有者 向请求者发送页面

```
struct {
	DSM_MSG_PAGE_REP
	1		//重要！！1表示“真”，说明自己是拥有者，发送页面
	PodId
	seq_num
	sizeof(payload)
}

payload: 4k Bytes
```

### 情况4：owner_id != PodId

 则返回PodId给请求者

```
struct {
	DSM_MSG_PAGE_REP
	0		//重要！！0表示“假”，返回真拥有者ID
	PodId
	seq_num
	sizeof(payload)
}
payload: 
struct {
	real_owner_id //uint_16t类型 真owner的ID
}
```

## 情景3：锁请求

发送方：计算进程的dsm_mutex_lock函数，发送给probowner = hash(lock_id)或realowner = 从probowner那里收到的ID

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

回复方：监听进程，查表看锁是否在自己这里 

### 情况1：在自己这里

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
struct {
	invalid_set_count;	//如果不是0，则还有后续负载
	-1					//realowner字段 			
}

长度为invalid_set_count大小的数组
```

### 情况2： 不在自己这里

回复信息：

```
struct {
	同上，只不过改成0
}

struct {
	0
	realowner
}
```

## 情景4：表更新

### 情况1：页表更新：标志位=1

### 情况2：锁表更新 标志位0

