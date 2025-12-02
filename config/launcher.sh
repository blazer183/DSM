#!/bin/bash

# Set UTF-8 encoding to prevent garbled output
export LC_ALL=C.UTF-8
export LANG=C.UTF-8
export LANGUAGE=en_US:en

# ================= 1. Global configuration =================

# --- Project path ---
SOURCE_DIR="$HOME/dsm"        # Your source root directory
#BUILD_CMD="make -j4" # Your build command
BUILD_CMD='g++ -std=c++17 -pthread -DUNITEST -I"DSM/include" test_dsm.cpp "DSM/src/os/dsm_os.cpp" "DSM/src/os/client_end.cpp" "DSM/src/os/segv_handler.cpp" "DSM/src/concurrent/concurrent_daemon.cpp"  "DSM/src/network/connection.cpp" -o dsm_app -lpthread'
EXE_NAME="dsm_app"                      # The name of the compiled executable

# --- Deployment target path (uniform across all machines) ---
# The program will be copied to this folder for execution
REMOTE_DIR="$HOME/Desktop/dsm_bin"

# --- DSM runtime parameters ---
LEADER_IP="10.29.109.58"   # Master real internal IP (eth1)
LEADER_PORT="9999"          # DSM listening port
TOTAL_PROCESSES=${1:-4}     # Total number of processes, default is 4, user can specify via the first argument
WORKER_PROCESSES=$((TOTAL_PROCESSES - 1))  # Number of worker processes = total - 1 leader                  

# --- Machine list (username@IP) ---
# Master: used for local deployment and startup
MASTER_NODE="heqizheng@10.29.109.58"

# Workers: used for remote distribution and startup
WORKER_NODES=(
    "heqizheng@10.112.100.112"
    "heqizheng@10.29.32.115"
)

# All nodes collection (used for stopping and starting)
ALL_NODES=("$MASTER_NODE" "${WORKER_NODES[@]}")

# ================= 2. Build phase =================
echo -e "\n? [1/4] Building..."
cd $SOURCE_DIR

# Execute build
eval $BUILD_CMD

if [ $? -ne 0 ]; then
    echo "? Build failed! Please check for code errors."
    exit 1
fi
echo "? Build succeeded!"

# ================= 3. Deployment phase =================
echo -e "\n? [2/4] Deploying program..."

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
        echo "    ? $WORKER Deployment succeeded"
    else
        echo "    ? $WORKER Deployment failed (check SSH connection)"
        exit 1
    fi
done

# ================= 4. Kill old processes =================
echo -e "\n? [3/4] Cleaning up old processes..."

for NODE in "${ALL_NODES[@]}"; do
    # Use pkill to kill processes named dsm_app, ignore errors (if not running)
    ssh $NODE "pkill -9 -x $EXE_NAME" > /dev/null 2>&1
    echo " -> Cleaned up $NODE"
done

# ================= 5. Run cluster =================
echo -e "\n? [4/4] Starting cluster ($TOTAL_PROCESSES processes)..."
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

# Start Leader first (PodID=0, Port=9999)
echo " -> Starting Leader ($MASTER_NODE) [PodID=0, Port=9999]..."
ssh $MASTER_NODE "export DSM_LEADER_IP=127.0.0.1; \
                  export DSM_LEADER_PORT=$LEADER_PORT; \
                  export DSM_TOTAL_PROCESSES=$TOTAL_PROCESSES; \
                  export DSM_WORKER_COUNT=$WORKER_COUNT; \
                  export DSM_WORKER_IPS='$WORKER_IPS'; \
                  export DSM_NODE_ID=0; \
                  nohup $REMOTE_DIR/$EXE_NAME > /tmp/dsm_leader.log 2>&1 &"

# Wait 2 seconds to ensure Leader is fully started and listening
sleep 2

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
                          export DSM_NODE_ID=$POD_ID; \
                          nohup $REMOTE_DIR/$EXE_NAME > /tmp/dsm_pod_$POD_ID.log 2>&1 &"
        
        # Slight delay to avoid simultaneous connections to Leader causing contention
        sleep 0.2
    done
fi

echo -e "\n? =========================================="
echo " DSM cluster has been fully started! ($TOTAL_PROCESSES Pods)"
echo " Pod distribution strategy:"
echo "   ¡ú Leader Pod: $MASTER_NODE (PodID=0, Port=9999)"
echo "   ¡ú Worker Pods: $WORKER_PROCESSES Pods distributed Round Robin"
echo "   ¡ú Port mapping: Pod#N listens on port $LEADER_PORT+N"
echo " Cluster network:"
echo "   ¡ú Leader IP: $LEADER_IP (PodID=0 fixed)"
echo "   ¡ú Worker IPs: $WORKER_IPS (count: $WORKER_COUNT)"
echo "   ¡ú Address mapping: PodID#N ¡ú worker_ips[N%$WORKER_COUNT]:$(($LEADER_PORT+N))"
echo ""
echo " Log monitoring commands:"
echo " Leader log: ssh $MASTER_NODE 'tail -f /tmp/dsm_leader.log'"
if [ $WORKER_COUNT -gt 0 ]; then
    echo " Worker logs: ssh ${WORKER_NODES[0]} 'tail -f /tmp/dsm_pod_*.log'"
    echo " (or specify a specific Pod: dsm_pod_1.log, dsm_pod_2.log, ...)"
fi
echo ""
echo " Stop cluster: pkill -9 -x $EXE_NAME (execute on each node)"
echo " =========================================="