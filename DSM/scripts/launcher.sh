#!/bin/bash

show_santi_ui() {
    clear
    
    # === 5级 红色渐变 ===
    local C1='\033[38;5;196m' 
    local C2='\033[38;5;160m' 
    local C3='\033[38;5;124m' 
    local C4='\033[38;5;88m'  
    local C5='\033[38;5;52m'  
    
    local YELLOW='\033[38;5;220m' 
    local GREY='\033[1;30m'      
    local RESET='\033[0m'
    
    # ==========================================
    # 0. 布局参数 (宽体版)
    # ==========================================
    # 字母宽 40，间隔 6
    # 总宽度 = 132
    
    local term_width=$(tput cols)
    local content_width=132
    local padding_len=$(( (term_width - content_width) / 2 ))
    
    if [ $padding_len -lt 0 ]; then padding_len=0; fi
    local PADDING=$(printf '%*s' "$padding_len" "")
    
    print_line() { echo -e "${PADDING}$1"; }

    # ==========================================
    # 1. 几何构造 (Width 40)
    # ==========================================
    
    local FULL=$(printf '█%.0s' {1..40})     
    local SP=$(printf ' %.0s' {1..6})        

    # --- D ---
    local D_WALL=$(printf '█%.0s' {1..12})
    local D_HOLE=$(printf ' %.0s' {1..16})   
    
    # D 圆角 (切掉右侧 8 格)
    local D_ROUND_SOLID=$(printf '█%.0s' {1..32})
    local D_ROUND_EMPTY=$(printf ' %.0s' {1..8})
    local D_ROUND_BAR="${D_ROUND_SOLID}${D_ROUND_EMPTY}"

    # --- S ---
    local S_VOID=$(printf ' %.0s' {1..28})   

    # --- M ---
    local M_LEG=$(printf '█%.0s' {1..10})    
    local M_CTR=$(printf '█%.0s' {1..12})    
    local M_SLIT=$(printf ' %.0s' {1..4})    
    local M_ARCH=$(printf ' %.0s' {1..20})

    # === 2. 星空 ===
    echo ""
    echo -e "${PADDING}${GREY}      .        +          .        *       .          +         .   .        +          .        *       .          +         .    ${RESET}"
    echo -e "${PADDING}${GREY}      .            *       .          +         .   +          .        *       .          +         .   .        +          .     ${RESET}"
    echo ""

    # === 3. 堆叠 (高度微调) ===
    # 设定为 7 (总行数 35)，介于之前的 50 和 25 之间
    local HEIGHT=7

    # --- Layer 1 ---
    local ROW_L1="${C1}${D_ROUND_BAR}${SP}${FULL}${SP}${FULL}${RESET}"
    for i in $(seq 1 $HEIGHT); do print_line "$ROW_L1"; done

    # --- Layer 2 ---
    local D_L2="${D_WALL}${D_HOLE}${D_WALL}"
    local S_L2="${D_WALL}${S_VOID}"
    local M_L2="${M_LEG}${M_SLIT}${M_CTR}${M_SLIT}${M_LEG}"
    local ROW_L2="${C2}${D_L2}${SP}${S_L2}${SP}${M_L2}${RESET}"
    for i in $(seq 1 $HEIGHT); do print_line "$ROW_L2"; done

    # --- Layer 3 ---
    local D_L3="${D_WALL}${D_HOLE}${D_WALL}"
    local S_L3="${FULL}"
    local M_L3="${M_LEG}${M_SLIT}${M_CTR}${M_SLIT}${M_LEG}"
    local ROW_L3="${C3}${D_L3}${SP}${S_L3}${SP}${M_L3}${RESET}"
    for i in $(seq 1 $HEIGHT); do print_line "$ROW_L3"; done

    # --- Layer 4 ---
    local D_L4="${D_WALL}${D_HOLE}${D_WALL}"
    local S_L4="${S_VOID}${D_WALL}"
    local M_L4="${M_LEG}${M_ARCH}${M_LEG}"
    local ROW_L4="${C4}${D_L4}${SP}${S_L4}${SP}${M_L4}${RESET}"
    for i in $(seq 1 $HEIGHT); do print_line "$ROW_L4"; done

    # --- Layer 5 ---
    local D_L5="${D_ROUND_BAR}"
    local S_L5="${FULL}"
    local M_L5="${M_LEG}${M_ARCH}${M_LEG}"
    local ROW_L5="${C5}${D_L5}${SP}${S_L5}${SP}${M_L5}${RESET}"
    for i in $(seq 1 $HEIGHT); do print_line "$ROW_L5"; done

    # === 4. 装饰 ===
    local REF_STR="|||       |||       |||"
    local REF_PAD=$(printf '%*s' 52 "")
    echo -e "${PADDING}${GREY}${REF_PAD}${REF_STR}${RESET}"
    echo -e "${PADDING}${GREY}${REF_PAD}:::       :::       :::${RESET}"

    # === 5. 地平线 ===
    local horizon=$(printf '%*s' "$term_width" | tr ' ' '_')
    echo -e "${YELLOW}${horizon}${RESET}"
    
    # === 6. 状态栏 ===
    local status_text="Version: 0.0.1  DEVELOPER: Runze Zhang, Jiaxin Dong"
    local status_len=${#status_text}
    local status_pad=$(( (term_width - status_len) / 2 ))
    printf "%${status_pad}s" ""
    echo -e "${GREY}${status_text}${RESET}"
    echo ""
    
    sleep 0.1
}

# 运行
show_santi_ui

# Set UTF-8 encoding to prevent garbled output
export LC_ALL=C.UTF-8
export LANG=C.UTF-8
export LANGUAGE=en_US:en

# ================= 1. Global configuration =================

# --- Project path ---
SOURCE_DIR="$HOME/dsm"        # Your source root directory
#BUILD_CMD="make -j4" # Your build command
BUILD_CMD='g++ -std=c++17 -pthread -DUNITEST -I"DSM/include" Dijkstra.cpp "DSM/src/os/dsm_os.cpp" "DSM/src/os/dsm_os_cond.cpp" "DSM/src/os/pfhandler.cpp" "DSM/src/concurrent/concurrent_daemon.cpp" "DSM/src/network/connection.cpp" -o dsm_app -lpthread'
EXE_NAME="dsm_app"                      # The name of the compiled executable

# --- Deployment target path (uniform across all machines) ---
# The program will be copied to this folder for execution
REMOTE_DIR="$HOME/Desktop/dsm_bin"

# --- DSM runtime parameters ---
LEADER_IP="10.29.109.58"   # Master real internal IP (eth1)
LEADER_PORT="10000"          # DSM listening port
TOTAL_PROCESSES=${1:-4}     # Total number of processes, default is 4, user can specify via the first argument
WORKER_PROCESSES=$((TOTAL_PROCESSES - 1))  # Number of worker processes = total - 1 leader                  

# --- Machine list (username@IP) ---
# Master: used for local deployment and startup
MASTER_NODE="heqizheng@10.29.109.58"

# Workers: used for remote distribution and startup
WORKER_NODES=(
    "heqizheng@10.112.100.112"
    #"heqizheng@10.29.32.115"
)

# All nodes collection (used for stopping and starting)
ALL_NODES=("$MASTER_NODE" "${WORKER_NODES[@]}")

# ================= 2. Build phase =================
echo -e "\n> [1/4] Building..."
cd $SOURCE_DIR

# Execute build
eval $BUILD_CMD

if [ $? -ne 0 ]; then
    echo "> Build failed! Please check for code errors."
    exit 1
fi
echo "> Build succeeded!"

# ================= 3. Kill old processes =================
echo -e "\n> [2/4] Cleaning up old processes..."

for NODE in "${ALL_NODES[@]}"; do
    # Use pkill to kill processes named dsm_app, ignore errors (if not running)
    ssh $NODE "pkill -9 -x $EXE_NAME" > /dev/null 2>&1
    echo " -> Cleaned up $NODE"
done

sleep 1

# ================= 4. Deployment phase =================
echo -e "\n> [3/4] Deploying program..."

# 3.1 Deploy to Master local machine
echo " -> Deploying to local machine (Master)..."
mkdir -p $REMOTE_DIR
cp $SOURCE_DIR/$EXE_NAME $REMOTE_DIR/
chmod +x $REMOTE_DIR/$EXE_NAME

# 3.2 Deploy to Workers
for WORKER in "${WORKER_NODES[@]}"; do
    echo " -> Deploying to Worker: $WORKER..."
    
    # Remote create directory + copy files + set permissions
    ssh $WORKER "mkdir -p $REMOTE_DIR"
    scp $SOURCE_DIR/$EXE_NAME $WORKER:$REMOTE_DIR/
    ssh $WORKER "chmod +x $REMOTE_DIR/$EXE_NAME"
    
    if [ $? -eq 0 ]; then
        echo "    > $WORKER Deployment succeeded"
    else
        echo "    > $WORKER Deployment failed (check SSH connection)"
        exit 1
    fi
done


# ================= 5. Run cluster =================
echo -e "\n> [4/4] Starting cluster ($TOTAL_PROCESSES processes)..."
echo "    -> Leader node: 1 process"
echo "    -> Worker nodes: $WORKER_PROCESSES processes (Round Robin distribution)"

# Calculate number of Worker nodes
WORKER_COUNT=${#WORKER_NODES[@]}

# Build Worker IP list (excluding Leader IP, separated by commas)
WORKER_IPS=""
for WORKER in "${WORKER_NODES[@]}"; do
    WORKER_IP=${WORKER##*@}  # Extract the part after @ (the IP)
    if [ -z "$WORKER_IPS" ]; then
        WORKER_IPS="$WORKER_IP"
    else
        WORKER_IPS="${WORKER_IPS},${WORKER_IP}"
    fi
done

# Start Leader first (PodID=0, Port=10000)
echo " -> Starting Leader ($MASTER_NODE) [PodID=0, Port=10000]..."
ssh $MASTER_NODE "export DSM_LEADER_IP=127.0.0.1; \
                  export DSM_LEADER_PORT=$LEADER_PORT; \
                  export DSM_TOTAL_PROCESSES=$TOTAL_PROCESSES; \
                  export DSM_WORKER_COUNT=$WORKER_COUNT; \
                  export DSM_WORKER_IPS='$WORKER_IPS'; \
                  export DSM_POD_ID=0; \
                  nohup $REMOTE_DIR/$EXE_NAME > /tmp/dsm_leader.log 2>&1 &"

# Wait 2 seconds to ensure Leader is fully started and listening


# Round Robin distribute Worker processes to each Worker node
if [ $WORKER_COUNT -eq 0 ]; then
    echo " -> Warning: No Worker nodes configured, all processes run on Leader node"
else
    echo " -> Starting Round Robin distribution of $WORKER_PROCESSES Worker processes to $WORKER_COUNT nodes..."
    
    for ((i=1; i<=WORKER_PROCESSES; i++)); do
        # Determine Worker node based on PodID algorithm: PodID % WORKER_COUNT
        POD_ID=$i
        NODE_INDEX=$((POD_ID % WORKER_COUNT))
        WORKER_NODE=${WORKER_NODES[$NODE_INDEX]}
        LISTEN_PORT=$((LEADER_PORT + POD_ID))
        
        echo "   -> Starting process #$i on $WORKER_NODE [PodID=$POD_ID, Port=$LISTEN_PORT]..."
    ssh $WORKER_NODE "export DSM_LEADER_IP=$LEADER_IP; \
              export DSM_LEADER_PORT=$LEADER_PORT; \
              export DSM_TOTAL_PROCESSES=$TOTAL_PROCESSES; \
              export DSM_WORKER_COUNT=$WORKER_COUNT; \
              export DSM_WORKER_IPS='$WORKER_IPS'; \
              export DSM_POD_ID=$POD_ID; \
              nohup $REMOTE_DIR/$EXE_NAME > /tmp/dsm_pod_$POD_ID.log 2>&1 &"
        
        # Slight delay to avoid simultaneous connections to Leader causing contention
        sleep 0.2
    done
fi

echo -e "\n> =========================================="
echo " DSM cluster has been fully started! ($TOTAL_PROCESSES Pods)"
echo " Pod distribution strategy:"
echo "   >> Leader Pod: $MASTER_NODE (PodID=0, Port=10000)"
echo "   >> Worker Pods: $WORKER_PROCESSES Pods distributed Round Robin"
echo "   >> Port mapping: Pod#N listens on port $LEADER_PORT+N"
echo " Cluster network:"
echo "   >> Leader IP: $LEADER_IP (PodID=0 fixed)"
echo "   >> Worker IPs: $WORKER_IPS (count: $WORKER_COUNT)"
echo "   >> Address mapping: PodID#N >> worker_ips[N%$WORKER_COUNT]:$(($LEADER_PORT+N))"
echo ""
echo " Log monitoring commands:"
echo " Leader log: ssh $MASTER_NODE 'cat /tmp/dsm_leader.log'"
if [ $WORKER_COUNT -gt 0 ]; then
    echo " Worker logs:"
    for ((i=1; i<=WORKER_PROCESSES; i++)); do
        POD_ID=$i
        NODE_INDEX=$((POD_ID % WORKER_COUNT))
        WORKER_NODE=${WORKER_NODES[$NODE_INDEX]}
        LISTEN_PORT=$((LEADER_PORT + POD_ID))
        echo "   Pod#$POD_ID: ssh $WORKER_NODE 'cat /tmp/dsm_pod_$POD_ID.log'"
    done
fi
echo ""
echo " Stop cluster: pkill -9 -x $EXE_NAME (execute on each node)"
echo " =========================================="