#!/bin/bash

# ================= 配置区域 =================
# 1. DSM 程序的路径 (确保所有机器上这个路径都存在且一致！)
APP_PATH="/home/heqizheng/Desktop/dsm_code"  

# 2. Leader (Master) 的 IP 和端口
LEADER_IP="192.29.109.58"  # <--- 请改成你 Master 的真实 IP (eth1)
LEADER_PORT="9999"         # <--- DSM 程序监听的端口

# 3. 节点列表 (格式: 用户名@IP)
# 包括 Master 自己，也包括 Worker
NODES=(
    "heqizheng@192.29.109.58"   # Master
    "heqizheng@10.112.100.112"  # Worker (你刚才连通的那个 IP)
)
# ===========================================

echo "? 开始启动分布式共享内存集群..."

# 遍历所有节点
for NODE in "${NODES[@]}"; do
    echo "----------------------------------------"
    echo "正在启动节点: $NODE"
    
    # 核心 SSH 远程执行命令：
    # 1. export 环境变量 (注入 IP 和 端口)
    # 2. nohup 后台运行 (防止 SSH 断开程序就挂了)
    # 3. 输出日志到 /tmp/dsm_node.log
    ssh $NODE "export DSM_LEADER_IP=$LEADER_IP; \
               export DSM_LEADER_PORT=$LEADER_PORT; \
               nohup $APP_PATH > /tmp/dsm_node.log 2>&1 &"
               
    if [ $? -eq 0 ]; then
        echo "? [成功] 启动命令已发送"
    else
        echo "? [失败] 无法连接或启动"
    fi
done

echo "----------------------------------------"
echo "? 集群启动完毕！"
echo "请登录各节点查看日志: tail -f /tmp/dsm_node.log"