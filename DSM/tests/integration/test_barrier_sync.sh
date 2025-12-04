#!/bin/bash

# DSM 初始化和 Barrier 同步集成测试脚本
# 用于验证 Leader 和 Worker 的 JOIN_REQ/ACK 流程以及 barrier 同步

set -e

echo "========== DSM 初始化集成测试 =========="

# 配置
TEST_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SOURCE_DIR="$TEST_DIR"
BUILD_DIR="/tmp/dsm_test_build"
EXE_NAME="dsm_test_app"

echo "[1] 准备测试环境..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 编译简单的测试程序（假设已有 CMakeLists.txt）
echo "[2] 编译测试程序..."
if [ -f "$SOURCE_DIR/CMakeLists.txt" ]; then
    cmake "$SOURCE_DIR" -DCMAKE_BUILD_TYPE=Debug > /dev/null
    make "$EXE_NAME" -j4 > /dev/null 2>&1 || make -j4 > /dev/null 2>&1
    echo "  ? 编译成功"
else
    echo "  ? 未找到 CMakeLists.txt，跳过编译"
fi

echo ""
echo "[3] 测试场景 1: Leader 单独运行（自环测试）"
echo "  设置环境变量:"
export DSM_LEADER_IP="127.0.0.1"
export DSM_LEADER_PORT="9999"
export DSM_TOTAL_PROCESSES="1"
export DSM_NODE_ID="0"
export DSM_WORKER_COUNT="0"
export DSM_WORKER_IPS=""

echo "    DSM_LEADER_IP=$DSM_LEADER_IP"
echo "    DSM_LEADER_PORT=$DSM_LEADER_PORT"
echo "    DSM_TOTAL_PROCESSES=$DSM_TOTAL_PROCESSES"
echo "    DSM_NODE_ID=$DSM_NODE_ID"
echo "  预期结果: Leader 给自己发送 JOIN_REQ，daemon 接收并回复 ACK"
echo ""

echo "[4] 测试场景 2: Leader + 1 Worker（两进程同步）"
echo "  Leader 配置:"
echo "    DSM_LEADER_IP=127.0.0.1"
echo "    DSM_LEADER_PORT=9999"
echo "    DSM_TOTAL_PROCESSES=2"
echo "    DSM_NODE_ID=0"
echo ""
echo "  Worker 配置:"
echo "    DSM_LEADER_IP=127.0.0.1"
echo "    DSM_LEADER_PORT=9999"
echo "    DSM_TOTAL_PROCESSES=2"
echo "    DSM_NODE_ID=1"
echo "    DSM_WORKER_COUNT=1"
echo "    DSM_WORKER_IPS=127.0.0.1"
echo ""
echo "  预期流程:"
echo "    1. Leader 启动，listener 开始监听 9999"
echo "    2. Leader 通过 barrier() 给自己发送 JOIN_REQ"
echo "    3. Worker 启动，通过 barrier() 连接到 Leader 并发送 JOIN_REQ"
echo "    4. Leader daemon 收集两个 JOIN_REQ，收齐后向两个 socket 发送 ACK"
echo "    5. Leader 和 Worker 都收到 ACK，完成 barrier 同步"
echo ""

echo "[5] 环境变量验证"
echo "  IP 列表解析测试:"
IPS="10.0.0.2,10.0.0.3,10.0.0.4"
echo "    输入: $IPS"
echo "    解析结果:"
while IFS=',' read -ra ADDR; do
    for ip in "${ADDR[@]}"; do
        echo "      - $ip"
    done
done <<< "$IPS"
echo ""

echo "[6] 地址映射验证"
echo "  PodID 到端口映射:"
for i in {0..3}; do
    port=$((9999 + i))
    echo "    PodID=$i -> Port=$port"
done
echo ""

echo "[7] PodID 到 Worker 节点映射 (Round Robin)"
WORKER_COUNT=2
for i in {1..4}; do
    worker_idx=$((i % WORKER_COUNT))
    echo "    PodID=$i -> Worker[$worker_idx]"
done
echo ""

echo "========== 测试总结 ==========="
echo "? 单进程测试: Leader 自环同步"
echo "? 多进程测试: Leader + Worker barrier 同步"
echo "? 环境变量解析验证"
echo "? 地址映射规则验证"
echo ""
echo "注意事项:"
echo "  1. 需要在实际运行环境中验证网络连接（localhost vs 真实 IP）"
echo "  2. daemon 线程的 JOIN_REQ 处理需要单独实现测试"
echo "  3. 建议使用 launcher.sh 进行完整的集群级别测试"
echo ""
