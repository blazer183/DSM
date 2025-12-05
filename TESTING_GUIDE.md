# Multi-VM Testing Guide

This guide provides step-by-step instructions for testing the DSM system on multiple virtual machines.

## Prerequisites

### Required Setup
- 2-4 Linux VMs (Ubuntu 20.04 or later recommended)
- Network connectivity between all VMs
- SSH access to all VMs
- GCC with C++17 support on all VMs

### Network Configuration
All VMs should be able to reach each other. You can verify this with:
```bash
# On each VM, test connectivity to others
ping <other-vm-ip>
```

## Step 1: VM Preparation

### On Each VM:

1. **Install required packages:**
```bash
sudo apt update
sudo apt install -y openssh-server g++ make git dos2unix
```

2. **Configure SSH server:**
```bash
# Remove old SSH keys
sudo rm /etc/ssh/ssh_host_*
sudo ssh-keygen -A

# Edit SSH config if needed
sudo nano /etc/ssh/sshd_config

# Restart SSH service
sudo service ssh restart
sudo service ssh status

# Get VM IP address
ip addr
```

3. **Note the IP address** of each VM. You'll need these later.

## Step 2: Setup SSH Passwordless Login

### On the Master Node (Leader):

1. **Clone the repository:**
```bash
cd $HOME
git clone <repository-url> dsm
cd dsm
```

2. **Run SSH initialization script:**
```bash
cd DSM/scripts
./init_ssh.sh
```

This script will:
- Generate SSH keys if they don't exist
- Copy public key to all worker nodes
- You'll need to enter the password for each worker node once

**Troubleshooting SSH setup:**
- If key generation fails, manually run: `ssh-keygen -t rsa -b 4096`
- If copying fails, manually copy: `ssh-copy-id <user>@<worker-ip>`
- If permission errors occur: `chmod 700 ~/.ssh && chmod 600 ~/.ssh/id_rsa`
- Verify key was copied: `ssh <user>@<worker-ip> 'ls -la ~/.ssh/authorized_keys'`

3. **Test SSH connectivity:**
```bash
# Should login without password (replace <user> with your actual username, e.g., 'ubuntu', 'user', 'root')
# <worker-vm-ip> is the IP address you noted in Step 1
ssh <user>@<worker-vm-ip> 'echo "Connection successful"'

# Example:
# ssh ubuntu@192.168.1.101 'echo "Connection successful"'
```

**Note:** The username should match the user account on the worker VM. Common usernames:
- Ubuntu VMs: `ubuntu`
- Debian VMs: `debian` 
- Generic Linux: `user` or the name you chose during VM setup
- Root access: `root` (not recommended for security)

To find your username on the worker VM, login directly and run: `whoami`

## Step 3: Configure Launcher Script

Edit `DSM/scripts/launcher.sh` and update the following variables:

```bash
# Line 21: Set leader's actual IP address
LEADER_IP="<your-leader-vm-ip>"  # e.g., "192.168.1.100"

# Line 28: Set master node
MASTER_NODE="<user>@<leader-vm-ip>"  # e.g., "ubuntu@192.168.1.100"

# Lines 31-34: Set worker nodes (one per line)
WORKER_NODES=(
    "<user>@<worker1-vm-ip>"  # e.g., "ubuntu@192.168.1.101"
    "<user>@<worker2-vm-ip>"  # e.g., "ubuntu@192.168.1.102"
    # Add more workers as needed
)
```

**Important Notes:**
- `LEADER_IP` should be the real IP address of the master VM
- Do NOT use `127.0.0.1` for multi-VM setup
- Ensure all IPs are reachable from all VMs

## Step 4: Deploy and Run

### On the Master Node:

1. **Make sure you're in the correct directory:**
```bash
cd $HOME/dsm/DSM/scripts
```

2. **Convert line endings (if needed):**
```bash
dos2unix launcher.sh init_ssh.sh
chmod +x launcher.sh init_ssh.sh
```

3. **Run the launcher:**
```bash
# For 4 total processes (1 leader + 3 workers)
./launcher.sh 4

# For 3 total processes (1 leader + 2 workers)
./launcher.sh 3

# For 2 total processes (1 leader + 1 worker)
./launcher.sh 2
```

The launcher will:
1. Build the executable
2. Distribute it to all nodes
3. Kill any old processes
4. Start the cluster with proper synchronization

## Step 5: Verify Results

### Check Logs

The launcher will display commands to check logs. You can also manually check:

```bash
# Leader log
ssh <master-node> 'cat /tmp/dsm_leader.log'

# Worker logs (for each worker)
ssh <worker-node> 'cat /tmp/dsm_pod_1.log'
ssh <worker-node> 'cat /tmp/dsm_pod_2.log'
ssh <worker-node> 'cat /tmp/dsm_pod_3.log'
```

### Expected Output

Each log should show:
```
========== DSM client test ==========
[DSM Info] DSM_LEADER_IP: <leader-ip>
[DSM Info] DSM_LEADER_PORT: 9999
[DSM Info] DSM_TOTAL_PROCESSES: <N>
[DSM Info] DSM_NODE_ID: <0-N>
[DSM Daemon] Listening on port <9999+NodeID>...
SUCCESS: dsm_init() completed successfully!
...
SUCCESS: All tests passed! DSM system running normally
```

**Leader-specific output:**
```
[DSM Daemon] Received JOIN_REQ: NodeId=<X>
[DSM Daemon] Currently connected: X / N
[DSM Daemon] All processes ready, broadcasting JOIN_ACK...
[DSM Daemon] Barrier synchronization complete!
```

## Step 6: Take Screenshots

For documentation, capture:

1. **Terminal output** showing successful deployment
2. **Leader log** showing all JOIN_REQ received and ACK broadcast
3. **Worker logs** showing successful synchronization
4. **Network diagram** (optional) showing VM topology

Example commands for screenshots:
```bash
# Get all logs at once
echo "=== Leader ===" && ssh <master> 'cat /tmp/dsm_leader.log'
echo "=== Worker 1 ===" && ssh <worker1> 'cat /tmp/dsm_pod_1.log'
echo "=== Worker 2 ===" && ssh <worker2> 'cat /tmp/dsm_pod_2.log'
```

## Troubleshooting

### Build Fails
```bash
# Check GCC version (needs C++17 support)
g++ --version  # Should be 7.0 or higher

# Check if source files exist
cd $HOME/dsm
ls -la DSM/src/os/dsm_os.cpp DSM/src/concurrent/concurrent_daemon.cpp
```

### SSH Connection Failed
```bash
# Test SSH manually
ssh -v <user>@<vm-ip>

# Check SSH service on target
ssh <user>@<vm-ip> 'sudo service ssh status'

# Verify firewall allows SSH (port 22)
sudo ufw status
```

### Port Binding Error
```bash
# Check if ports are already in use
netstat -tlnp | grep 9999

# Kill old processes
pkill -9 -x dsm_app

# Wait a moment for ports to be released
sleep 2
```

### Processes Don't Synchronize
- Verify `DSM_TOTAL_PROCESSES` matches actual number of started processes
- Check that LEADER_IP is the correct IP (not 127.0.0.1)
- Ensure firewall allows traffic on ports 9999+
- Check worker count and IP list are correct

### Connection Timeout
```bash
# Check network connectivity
ping <leader-ip>
telnet <leader-ip> 9999

# Check if leader is listening
ssh <leader> 'netstat -tlnp | grep 9999'

# Verify firewall rules
sudo ufw allow 9999:10010/tcp
```

## Cleanup

To stop all processes:
```bash
# On master node
ssh <master> 'pkill -9 -x dsm_app'

# On each worker
ssh <worker> 'pkill -9 -x dsm_app'

# Or use the kill command provided by launcher
```

## Example VM Setup

### Recommended Configuration:

**VM 1 (Leader):**
- IP: 192.168.1.100
- Role: Master/Leader (NodeID=0)
- Listening on: Port 9999

**VM 2 (Worker):**
- IP: 192.168.1.101
- Role: Worker
- Processes: NodeID=1,2 (ports 10000,10001)

**VM 3 (Worker):**
- IP: 192.168.1.102
- Role: Worker
- Processes: NodeID=3 (port 10002)

Total: 4 processes distributed across 3 VMs

## Success Criteria

✅ Compilation completes without errors
✅ Executable is distributed to all VMs
✅ All processes start and listen on correct ports
✅ Leader receives JOIN_REQ from all processes
✅ All processes receive JOIN_ACK
✅ Barrier synchronization completes
✅ All tests pass on all nodes
✅ Logs show "SUCCESS: All tests passed!"

## Additional Notes

- The system uses TCP sockets for communication
- Port assignment: Leader uses 9999, workers use 9999+NodeID
- Each process has its own listener daemon
- Barrier synchronization is required for initialization
- The test program validates basic DSM functionality

For more information, see:
- [README.md](../README.md) - Project overview
- [DSM/docs/ARCHITECTURE.md](DSM/docs/ARCHITECTURE.md) - System architecture
- [DSM/docs/README.md](DSM/docs/README.md) - Development guide
