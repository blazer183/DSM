#!/bin/bash

# ================= 配置区域 =================
# 你的用户名
USER_NAME="heqizheng"

# Worker 机器的 IP 列表 (空格分隔) 最重要！！！
WORKER_IPS=("10.112.100.112")
# ===========================================

# 1. 检查是否已有密钥，没有则自动生成
if [ ! -f ~/.ssh/id_ed25519.pub ]; then
    echo "[Master] 未检测到密钥，正在生成..."
    # -N "" 表示空密码，-f 指定路径
    ssh-keygen -t ed25519 -N "" -f ~/.ssh/id_ed25519
else
    echo "[Master] 密钥已存在，跳过生成。"
fi

# 2. 循环分发给所有 Worker
echo "=== 开始分发公钥 ==="
echo "注意：接下来你需要为每台机器输入一次密码（这是最后一次！）"

for IP in "${WORKER_IPS[@]}"; do
    echo "----------------------------------------"
    echo "正在处理节点: $IP"
    
    # 自动将公钥追加到远程机器的 authorized_keys
    # -o StrictHostKeyChecking=no 防止第一次连接弹出的 'yes/no' 确认
    ssh-copy-id -i ~/.ssh/id_ed25519.pub -o StrictHostKeyChecking=no "$USER_NAME@$IP"
    
    if [ $? -eq 0 ]; then
        echo "? [成功] $IP 已配置免密登录"
    else
        echo "? [失败] 无法连接到 $IP，请检查网络或密码"
    fi
done

echo "========================================"
echo "所有节点处理完毕，请运行 'ssh 用户名@IP' 测试。"
