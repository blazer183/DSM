#!/bin/bash
# Local test script for DSM cluster simulation on a single machine
# This script runs multiple DSM processes locally to test page fault handling
# and inter-process paging

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  DSM Local Cluster Test${NC}"
echo -e "${GREEN}========================================${NC}"

# Configuration
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR="$(dirname "$SCRIPT_DIR")"
PROJECT_ROOT="$(dirname "$SOURCE_DIR")"

# Number of processes (can be overridden by first argument)
TOTAL_PROCESSES=${1:-2}

echo -e "${YELLOW}Configuration:${NC}"
echo "  Source directory: $SOURCE_DIR"
echo "  Project root: $PROJECT_ROOT"
echo "  Total processes: $TOTAL_PROCESSES"

# Build the project
echo -e "\n${YELLOW}[1/4] Building project...${NC}"
cd "$PROJECT_ROOT"

g++ -std=c++17 -pthread -DUNITEST \
    -I"DSM/include" \
    test_dsm.cpp \
    "DSM/src/os/dsm_os.cpp" \
    "DSM/src/os/dsm_os_cond.cpp" \
    "DSM/src/os/pfhandler.cpp" \
    "DSM/src/concurrent/concurrent_daemon.cpp" \
    "DSM/src/network/connection.cpp" \
    -o dsm_app \
    -lpthread

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi
echo -e "${GREEN}Build successful!${NC}"

# Prepare data files
echo -e "\n${YELLOW}[2/4] Preparing data files...${NC}"
mkdir -p "$HOME/dsm"
cp -f "$PROJECT_ROOT/Ar" "$HOME/dsm/"
cp -f "$PROJECT_ROOT/sum" "$HOME/dsm/"
echo "Data files copied to $HOME/dsm/"
echo "Content of Ar.txt: $(cat "$PROJECT_ROOT/Ar.txt")"

# Clean up old processes
echo -e "\n${YELLOW}[3/4] Cleaning up old processes...${NC}"
pkill -9 -x dsm_app 2>/dev/null || true
sleep 1

# Start cluster
echo -e "\n${YELLOW}[4/4] Starting DSM cluster with $TOTAL_PROCESSES processes...${NC}"

# Common environment
export DSM_LEADER_IP=127.0.0.1
export DSM_LEADER_PORT=10000
export DSM_TOTAL_PROCESSES=$TOTAL_PROCESSES
export DSM_WORKER_COUNT=$((TOTAL_PROCESSES - 1))

# Build worker IPs list (all localhost for local testing)
WORKER_IPS=""
for ((i=1; i<TOTAL_PROCESSES; i++)); do
    if [ -z "$WORKER_IPS" ]; then
        WORKER_IPS="127.0.0.1"
    else
        WORKER_IPS="${WORKER_IPS},127.0.0.1"
    fi
done
export DSM_WORKER_IPS="$WORKER_IPS"

# Create log directory
LOG_DIR="/tmp/dsm_logs"
mkdir -p "$LOG_DIR"

# Start Pod 0 (leader)
echo "Starting Pod 0 (Leader) on port 10000..."
DSM_POD_ID=0 timeout 30 ./dsm_app > "$LOG_DIR/pod_0.log" 2>&1 &
POD_PIDS[0]=$!

# Wait for leader to start
sleep 2

# Start worker pods
for ((i=1; i<TOTAL_PROCESSES; i++)); do
    PORT=$((10000 + i))
    echo "Starting Pod $i (Worker) on port $PORT..."
    DSM_POD_ID=$i timeout 30 ./dsm_app > "$LOG_DIR/pod_$i.log" 2>&1 &
    POD_PIDS[$i]=$!
    sleep 0.5
done

# Wait for all processes to complete
echo -e "\n${YELLOW}Waiting for processes to complete...${NC}"
wait

echo -e "\n${GREEN}========================================${NC}"
echo -e "${GREEN}  Test Results${NC}"
echo -e "${GREEN}========================================${NC}"

# Display logs
for ((i=0; i<TOTAL_PROCESSES; i++)); do
    echo -e "\n${YELLOW}=== Pod $i Log ===${NC}"
    cat "$LOG_DIR/pod_$i.log"
done

echo -e "\n${GREEN}========================================${NC}"
echo -e "${GREEN}  Test Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo "Log files are saved in: $LOG_DIR"
