!/bin/bash

# ================= 1. 全局配置 (请修改这里) =================

# --- 项目路径 ---
SOURCE_DIR="$HOME/Desktop/dsm"        # 你的源码根目录
#BUILD_CMD="make -j4" # 你的编译命令
BUILD_CMD="g++ -std=c++17 -pthread -DUNITEST -I\"local DSM/include\" test_dsm.cpp \"local DSM/src/os/dsm_os.cpp\" \"local DSM/src/os/client_end.cpp\" \"local DSM/src/os/segv_handler.cpp\" \"local DSM/src/concurrent/concurrent_daemon.cpp\" \"local DSM/src/concurrent/concurrent_client.cpp\" \"local DSM/src/concurrent/concurrent_join.cpp\" \"local DSM/src/concurrent/concurrent_lock.cpp\" \"local DSM/src/concurrent/concurrent_page.cpp\" \"local DSM/src/concurrent/concurrent_misc.cpp\" \"local DSM/src/network/connection.cpp\" -o dsm_app -lpthread\"
EXE_NAME="dsm_app"                      # 编译生成的文件名

# --- 部署目标路径 (所有机器统一) ---
# 程序将被复制到这个文件夹运行
REMOTE_DIR="$HOME/Desktop/dsm_bin"

# --- DSM 运行参数 ---
LEADER_IP="10.29.109.58"   # Master 的真实内网 IP (eth1)
LEADER_PORT="9999"          # DSM 监听端口
TOTAL_PROCESSES=${1:-4}     # 总进程数，默认4个，用户可通过第一个参数指定
WORKER_PROCESSES=$((TOTAL_PROCESSES - 1))  # Worker进程数 = 总数 - 1个Leader

# --- 机器列表 (用户名@IP) ---
# Master: 用于本地部署和启动
MASTER_NODE="heqizheng@10.29.109.58"

# Workers: 用于远程分发和启动
WORKER_NODES=(
    "heqizheng@10.112.100.112"
    
)

# 所有节点合集 (用于停止和启动)
ALL_NODES=("$MASTER_NODE" "${WORKER_NODES[@]}")

# ================= 2. 编译阶段 (Build) =================
echo -e "\n? [1/4] 正在编译..."
cd $SOURCE_DIR

# 执行编译
$BUILD_CMD

if [ $? -ne 0 ]; then
    echo "? 编译失败！请检查代码错误。"
    exit 1
fi
echo "? 编译成功！"

# ================= 3. 分发阶段 (Deploy) =================
echo -e "\n? [2/4] 正在分发程序..."

# 3.1 部署到 Master 本机
echo " -> 部署到本机 (Master)..."
mkdir -p $REMOTE_DIR
cp $SOURCE_DIR/$EXE_NAME $REMOTE_DIR/
chmod +x $REMOTE_DIR/$EXE_NAME

# 3.2 部署到 Workers
for WORKER in "${WORKER_NODES[@]}"; do
    echo " -> 发送到 Worker: $WORKER..."
    
    # 远程创建目录 + 复制文件 + 赋予权限
    ssh $WORKER "mkdir -p $REMOTE_DIR"
    scp $SOURCE_DIR/$EXE_NAME $WORKER:$REMOTE_DIR/
    ssh $WORKER "chmod +x $REMOTE_DIR/$EXE_NAME"
    
    if [ $? -eq 0 ]; then
        echo "    ? $WORKER 分发成功"
    else
        echo "    ? $WORKER 分发失败 (检查 SSH 连接)"
        exit 1
    fi
done

# ================= 4. 清理旧进程 (Kill) =================
echo -e "\n? [3/4] 清理旧进程..."

for NODE in "${ALL_NODES[@]}"; do
    # 使用 pkill 杀掉叫 dsm_app 的进程，忽略报错(如果本来就没运行)
    ssh $NODE "pkill -9 -x $EXE_NAME" > /dev/null 2>&1
    echo " -> 已清理 $NODE"
done

# ================= 5. 启动集群 (Run) =================
echo -e "\n? [4/4] 启动集群 ($TOTAL_PROCESSES 个进程)..."
echo "    -> Leader节点: 1个进程"
echo "    -> Worker节点: $WORKER_PROCESSES 个进程 (Round Robin分发)"

# 构建Worker IP列表 (不包含Leader IP，用逗号分隔)
WORKER_IPS=""
for WORKER in "${WORKER_NODES[@]}"; do
    WORKER_IP=${WORKER##*@}  # 提取@后面的IP部分
    if [ -z "$WORKER_IPS" ]; then
        WORKER_IPS="$WORKER_IP"
    else
        WORKER_IPS="${WORKER_IPS},${WORKER_IP}"
    fi
done

# 先启动 Leader (PodID=0，端口9999)
echo " -> 启动 Leader ($MASTER_NODE) [PodID=0, Port=9999]..."
ssh $MASTER_NODE "export DSM_LEADER_IP=127.0.0.1; \
                  export DSM_LEADER_PORT=$LEADER_PORT; \
                  export DSM_TOTAL_PROCESSES=$TOTAL_PROCESSES; \
                  export DSM_WORKER_COUNT=$WORKER_COUNT; \
                  export DSM_WORKER_IPS='$WORKER_IPS'; \
                  export DSM_NODE_ID=0; \
                  nohup $REMOTE_DIR/$EXE_NAME > /tmp/dsm_leader.log 2>&1 &"

# 等待 2 秒，确保 Leader 完全启动并开始监听
sleep 2

# Round Robin分发Worker进程到各个Worker节点
WORKER_COUNT=${#WORKER_NODES[@]}
if [ $WORKER_COUNT -eq 0 ]; then
    echo " -> 警告: 没有配置Worker节点，所有进程都在Leader节点运行"
else
    echo " -> 开始Round Robin分发 $WORKER_PROCESSES 个Worker进程到 $WORKER_COUNT 个节点..."
    
    for ((i=1; i<=WORKER_PROCESSES; i++)); do
        # 根据PodID算法: PodID % WORKER_COUNT 确定Worker节点
        POD_ID=$i
        NODE_INDEX=$((POD_ID % WORKER_COUNT))
        WORKER_NODE=${WORKER_NODES[$NODE_INDEX]}
        LISTEN_PORT=$((LEADER_PORT + POD_ID))
        
        echo "   -> 启动进程 #$i 在 $WORKER_NODE [PodID=$POD_ID, Port=$LISTEN_PORT]..."
        ssh $WORKER_NODE "export DSM_LEADER_IP=$LEADER_IP; \
                          export DSM_LEADER_PORT=$LEADER_PORT; \
                          export DSM_TOTAL_PROCESSES=$TOTAL_PROCESSES; \
                          export DSM_WORKER_COUNT=$WORKER_COUNT; \
                          export DSM_WORKER_IPS='$WORKER_IPS'; \
                          export DSM_NODE_ID=$POD_ID; \
                          nohup $REMOTE_DIR/$EXE_NAME > /tmp/dsm_pod_$POD_ID.log 2>&1 &"
        
        # 稍微延迟，避免同时连接Leader造成竞争
        sleep 0.2
    done
fi

echo -e "\n? =========================================="
echo "? DSM集群已全部启动完成！($TOTAL_PROCESSES 个Pod)"
echo "? Pod分布策略:"
echo "   → Leader Pod: $MASTER_NODE (PodID=0, Port=9999)"
echo "   → Worker Pods: $WORKER_PROCESSES 个Pod已Round Robin分发"
echo "   → 端口映射: Pod#N 监听端口 $LEADER_PORT+N"
echo "? 集群网络:"
echo "   → Leader IP: $LEADER_IP (PodID=0 固定)"
echo "   → Worker IPs: $WORKER_IPS (数量: $WORKER_COUNT)"
echo "   → 地址映射: PodID#N → worker_ips[N%$WORKER_COUNT]:$(($LEADER_PORT+N))"
echo ""
echo "? 日志监控命令:"
echo "? Leader日志: ssh $MASTER_NODE 'tail -f /tmp/dsm_leader.log'"
if [ $WORKER_COUNT -gt 0 ]; then
    echo "? Worker日志: ssh ${WORKER_NODES[0]} 'tail -f /tmp/dsm_pod_*.log'"
    echo "   (或指定具体Pod: dsm_pod_1.log, dsm_pod_2.log, ...)"
fi
echo ""
echo "? 停止集群: pkill -9 -x $EXE_NAME (在各节点执行)"
echo "? =========================================="