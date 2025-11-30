#!/bin/bash

# ================= 1. 全局配置 (请修改这里) =================

# --- 项目路径 ---
SOURCE_DIR="/home/heqizheng/dsm"        # 你的源码根目录
#BUILD_CMD="make -j4" # 运行makefile编译
BUILD_CMD="g++ test.cpp -o dsm_app " 
EXE_NAME="dsm_app"                      # 编译生成的文件名

# --- 部署目标路径 (所有机器统一) ---
# 程序将被复制到这个文件夹运行
REMOTE_DIR="/home/heqizheng/Desktop/dsm_bin"

# --- DSM 运行参数 ---
LEADER_IP="10.29.109.58"   # Master 的真实内网 IP (eth1)
LEADER_PORT="9999"          # DSM 监听端口

# --- 机器列表 (用户名@IP) ---
# Master: 用于本地部署和启动
MASTER_NODE="heqizheng@10.29.109.58"

# Workers: 用于远程分发和启动
WORKER_NODES=(
    "heqizheng@10.112.100.112"
    #增加其他节点列表
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
echo -e "\n? [4/4] 启动集群..."

# 先启动 Master (Leader)
echo " -> 启动 Leader ($MASTER_NODE)..."
ssh $MASTER_NODE "export DSM_LEADER_IP=$LEADER_IP; \
                  export DSM_LEADER_PORT=$LEADER_PORT; \
                  nohup $REMOTE_DIR/$EXE_NAME > /tmp/dsm_master.log 2>&1 &"

# 等待 1 秒，确保 Leader 先跑起来监听端口
sleep 1

# 再启动所有 Workers
for WORKER in "${WORKER_NODES[@]}"; do
    echo " -> 启动 Worker ($WORKER)..."
    ssh $WORKER "export DSM_LEADER_IP=$LEADER_IP; \
                 export DSM_LEADER_PORT=$LEADER_PORT; \
                 nohup $REMOTE_DIR/$EXE_NAME > /tmp/dsm_worker.log 2>&1 &"
done

echo -e "\n? ========================================="
echo "? 集群已全部启动！"
echo "? 查看 Master 日志: ssh $MASTER_NODE 'tail -f /tmp/dsm_master.log'"
echo "? 查看 Worker 日志: ssh ${WORKER_NODES[0]} 'tail -f /tmp/dsm_worker.log'"
echo "? ========================================="
