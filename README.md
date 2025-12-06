# DSM Test Generation

This repository contains the Distributed Shared Memory (DSM) middleware testing framework.

## Task Overview

The goal is to test the DSM system by deploying `test_dsm.cpp` to multiple virtual machines and verifying that:
1. The compilation completes successfully
2. The launcher script distributes the executable to all nodes
3. All processes can synchronize via the barrier mechanism
4. Logs are correctly returned from all nodes

## Quick Start

### Prerequisites

- Multiple Linux VMs (Ubuntu/Debian recommended) with WSL or native Linux
- SSH access configured between VMs (passwordless login)
- GCC with C++17 support
- Network connectivity between all VMs

### Local Testing (Single Machine)

To quickly test the system on a single machine with multiple processes:

```bash
cd /home/runner/work/DSM_test_generation/DSM_test_generation

# Build the application
g++ -std=c++17 -pthread -DUNITEST -I"DSM/include" test_dsm.cpp \
    "DSM/src/os/dsm_os.cpp" "DSM/src/os/pfhandler.cpp" \
    "DSM/src/concurrent/concurrent_daemon.cpp" \
    "DSM/src/network/connection.cpp" -o dsm_app -lpthread

# Set environment variables
export DSM_LEADER_IP="127.0.0.1"
export DSM_LEADER_PORT="9999"
export DSM_TOTAL_PROCESSES=2
export DSM_WORKER_COUNT=1
export DSM_WORKER_IPS="127.0.0.1"

# Start leader (NodeID=0)
export DSM_NODE_ID=0
./dsm_app &

# Start worker (NodeID=1)
export DSM_NODE_ID=1
./dsm_app &

# Wait and check results
wait
```

### Multi-VM Testing

1. **Prepare VMs:**
   - Create multiple VMs (e.g., 2-4 VMs)
   - Note each VM's IP address
   - Configure SSH passwordless login using `DSM/scripts/init_ssh.sh`

2. **Update launcher.sh:**
   Edit `DSM/scripts/launcher.sh` to set your VM IPs:
   ```bash
   LEADER_IP="<your-leader-vm-ip>"
   MASTER_NODE="<user>@<leader-vm-ip>"
   WORKER_NODES=(
       "<user>@<worker1-vm-ip>"
       "<user>@<worker2-vm-ip>"
   )
   ```

3. **Run the launcher:**
   ```bash
   cd DSM/scripts
   ./launcher.sh 4  # 4 total processes (1 leader + 3 workers)
   ```

4. **Check logs:**
   ```bash
   # Leader log
   ssh <user>@<leader-vm-ip> 'cat /tmp/dsm_leader.log'
   
   # Worker logs
   ssh <user>@<worker-vm-ip> 'cat /tmp/dsm_pod_1.log'
   ssh <user>@<worker-vm-ip> 'cat /tmp/dsm_pod_2.log'
   ```

## Project Structure

```
DSM/
├── config/           # Cluster configuration
├── docs/            # Documentation (ARCHITECTURE.md, README.md)
├── include/         # Header files
│   ├── concurrent/  # Listener thread headers
│   ├── net/        # Network protocol definitions
│   └── os/         # OS-level abstractions
├── scripts/        # Deployment scripts
│   ├── init_ssh.sh    # SSH setup (one-time)
│   └── launcher.sh    # Main deployment script
├── src/            # Source code
│   ├── concurrent/    # Listener thread implementation
│   ├── network/       # RIO network functions
│   └── os/           # DSM user interface
└── tests/          # Unit and integration tests
```

## Key Features Implemented

### Compilation Fixes
- ✅ Fixed missing `PageSize` constant (changed to `DSM_PAGE_SIZE`)
- ✅ Added missing `payload_join_req_t` structure definition
- ✅ Fixed missing `join_mutex` global variable
- ✅ Added missing return statements in `FetchGlobalData()` and `dsm_mutex_lock()`
- ✅ Corrected build command to include all necessary source files

### Listener Thread Implementation
- ✅ Complete JOIN_REQ/JOIN_ACK message handling
- ✅ Proper mutex protection for concurrent access
- ✅ Barrier synchronization ensures all processes join before proceeding
- ✅ Thread-safe message broadcasting to all connected nodes

### Distributed Mutex Lock Implementation
- ✅ Complete implementation of `dsm_mutex_lock()` functionality
- ✅ Lock acquisition protocol with probable owner hashing
- ✅ Redirect mechanism when lock is held by another node
- ✅ Retry logic with backoff for waiting on held locks
- ✅ LOCK_ACQ/LOCK_REP message handling in daemon thread
- ✅ Proper lock state management in LockTable
- ✅ Support for multiple concurrent lock requests

## Testing Results

### Local Test (Single Machine with 4 Processes)

Successfully tested with 4 processes on localhost to simulate a multi-node cluster:

**Test Command:**
```bash
export DSM_LEADER_IP="127.0.0.1"
export DSM_LEADER_PORT="9999"
export DSM_TOTAL_PROCESSES=4
export DSM_WORKER_COUNT=1
export DSM_WORKER_IPS="127.0.0.1"

# Start 1 leader + 3 worker processes
```

**Results:**

**Leader Process (NodeID=0, Port 9999):**
- ✅ Listening on port 9999
- ✅ Received JOIN_REQ from all 4 processes (including itself)
- ✅ Successfully broadcast JOIN_ACK to all processes
- ✅ Acquired distributed lock successfully
- ✅ Held lock while other nodes waited
- ✅ All tests passed

```
[DSM Daemon] Received JOIN_REQ: NodeId=0
[DSM Daemon] Currently connected: 1 / 4
[DSM Daemon] Received JOIN_REQ: NodeId=1
[DSM Daemon] Currently connected: 2 / 4
[DSM Daemon] Received JOIN_REQ: NodeId=2
[DSM Daemon] Currently connected: 3 / 4
[DSM Daemon] Received JOIN_REQ: NodeId=3
[DSM Daemon] Currently connected: 4 / 4
[DSM Daemon] All processes ready, broadcasting JOIN_ACK...
[DSM Daemon] Sent JOIN_ACK to fd=4
[DSM Daemon] Sent JOIN_ACK to fd=6
[DSM Daemon] Sent JOIN_ACK to fd=7
[DSM Daemon] Sent JOIN_ACK to fd=8
[DSM Daemon] Barrier synchronization complete!
SUCCESS: dsm_init() completed successfully!
[Step 4] verify lock aquire...
I'm 0 and I get lock A!
[DSM Daemon] Lock 1 is currently held by us (NodeId=0), requester must wait
See? No one can get lock A because I locked it!...
SUCCESS: All tests passed! DSM system running normally
```

**Worker Processes (NodeID=1,2,3 on Ports 10000,10001,10002):**
- ✅ Each worker listening on its assigned port
- ✅ Successfully connected to leader
- ✅ Received JOIN_ACK and synchronized
- ✅ Correctly waited when trying to acquire held lock
- ✅ Lock contention handled properly

**Lock Test Summary:**
- ✅ Node 0 acquired lock first and held it for 3 seconds
- ✅ Nodes 1, 2, and 3 all tried to acquire the same lock
- ✅ All requests were correctly redirected to Node 0 (the lock holder)
- ✅ All waiting nodes received "lock is held, must wait" response
- ✅ Retry mechanism with 100ms backoff working correctly
- ✅ No deadlocks or race conditions observed

**Summary:**
- ✅ All 4 processes started successfully
- ✅ Barrier synchronization worked correctly
- ✅ All processes completed initialization
- ✅ Distributed mutex lock working correctly
- ✅ Lock contention handled properly with waiting/retry mechanism
- ✅ Address mapping verified for all PodIDs
- ✅ Clean program termination

### Multi-VM Test Results

The system is now ready for multi-VM testing. To perform multi-VM tests:

1. Set up 2-4 VMs with SSH access
2. Update `DSM/scripts/launcher.sh` with your VM IPs
3. Run `./launcher.sh N` where N is the total number of processes
4. Verify logs from each node

*Screenshots of multi-VM deployment will be added here after testing on actual virtual machines.*

**Expected behavior for multi-VM deployment:**
- Leader node distributes executable to all worker nodes
- Each node starts its listener daemon
- All processes connect to leader (NodeID=0)
- Barrier synchronization ensures all processes join before proceeding
- Each node logs successful initialization

## Troubleshooting

### Build Errors
- Ensure GCC supports C++17: `g++ --version`
- Check all source files are present in `DSM/src/`
- Verify include paths are correct

### Connection Issues
- Verify firewall allows traffic on ports 9999+
- Check SSH connectivity: `ssh <user>@<vm-ip> 'echo test'`
- Ensure all VMs can reach each other's IPs

### Synchronization Issues
- Check that `DSM_TOTAL_PROCESSES` matches actual number of processes
- Verify `DSM_WORKER_COUNT` is correct
- Review logs for connection errors

## Documentation

For detailed information, please refer to:
- [Architecture Overview](DSM/docs/ARCHITECTURE.md) - System design and message protocol
- [DSM Usage Guide](DSM/docs/README.md) - Build instructions and SSH setup

## License

This is a research project for exploring distributed shared memory concepts.
