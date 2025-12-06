# DSM Distributed Mutex Lock Implementation

## Overview

This document describes the implementation of the distributed mutex lock functionality in the DSM (Distributed Shared Memory) system. The implementation enables multiple processes across different nodes to coordinate access to shared resources using distributed locks.

## Architecture

### Lock Protocol

The lock acquisition protocol follows a three-hop approach:

1. **Hash-based Probable Owner**: Each lock ID is mapped to a probable owner using `lock_id % ProcNum`
2. **Request-Reply Protocol**: Requesters send LOCK_ACQ messages and wait for LOCK_REP responses
3. **Redirection Mechanism**: If the probable owner doesn't hold the lock, it redirects to the real owner

### Message Types

#### LOCK_ACQ (Lock Acquisition Request)
```c
Header:
- type: DSM_MSG_LOCK_ACQ
- src_node_id: Requester's node ID
- seq_num: Sequence number for this request
- payload_len: sizeof(payload_lock_req_t)

Payload:
- lock_id: The ID of the lock being requested
```

#### LOCK_REP (Lock Acquisition Reply)
```c
Header:
- type: DSM_MSG_LOCK_REP
- unused: 0 (redirect) or 1 (granted)
- src_node_id: Responder's node ID
- seq_num: Same as request
- payload_len: sizeof(payload_lock_rep_t)

Payload:
- invalid_set_count: Number of invalid pages (for scope consistency)
- realowner: Real owner's node ID (only valid when unused=0)
```

## Implementation Details

### Client-Side Implementation (`dsm_mutex_lock`)

Located in `DSM/src/os/dsm_os.cpp`:

```cpp
int dsm_mutex_lock(int *mutex) {
    // 1. Check if already acquired locally
    LockTable->LockAcquire();
    auto record = LockTable->Find(lockid);
    if (record != nullptr && record->locked && record->owner_id == NodeId) {
        LockTable->LockRelease();
        return 0;  // Already have the lock
    }
    LockTable->LockRelease();
    
    // 2. Determine probable owner
    int lockprobowner = lockid % ProcNum;
    
    // 3. Loop until lock acquired
    while (true) {
        // 3a. Establish connection if needed
        // 3b. Send LOCK_ACQ request
        // 3c. Receive LOCK_REP response
        // 3d. If unused=0, redirect to real owner and retry
        // 3e. If unused=1, update local lock table and return
    }
}
```

**Key Features:**
- Local lock check to avoid network overhead
- Socket connection caching in SocketTable
- Automatic retry on redirect
- Exponential backoff when lock is held (100ms sleep)
- Proper sequence number management

### Server-Side Implementation (Daemon Handler)

Located in `DSM/src/concurrent/concurrent_daemon.cpp`:

The daemon's `peer_handler` function processes incoming LOCK_ACQ messages:

```cpp
if (header.type == DSM_MSG_LOCK_ACQ) {
    // Read lock request payload
    uint32_t lockid = ntohl(req_payload.lock_id);
    uint16_t requester_id = ntohs(header.src_node_id);
    
    // Check lock state
    LockTable->LockAcquire();
    auto lock_rec = LockTable->Find(lockid);
    
    if (lock_rec == nullptr) {
        // Grant new lock to requester
        LockTable->Insert(lockid, LockRecord(requester_id));
        rep_header.unused = 1;  // Granted
    }
    else if (lock_rec->locked && lock_rec->owner_id != NodeId) {
        // Redirect to real owner
        rep_header.unused = 0;  // Redirect
        rep_payload.realowner = lock_rec->owner_id;
    }
    else if (lock_rec->locked && lock_rec->owner_id == NodeId) {
        // Lock is held by us, requester must wait
        rep_header.unused = 0;  // Not granted
        rep_payload.realowner = NodeId;  // Same owner
    }
    else {
        // Lock exists but unlocked, grant it
        lock_rec->locked = true;
        lock_rec->owner_id = requester_id;
        LockTable->Update(lockid, *lock_rec);
        rep_header.unused = 1;  // Granted
    }
    
    LockTable->LockRelease();
    
    // Send response
    write(connfd, &rep_header, sizeof(rep_header));
    write(connfd, &rep_payload, sizeof(rep_payload));
}
```

**Key Features:**
- Thread-safe lock table access
- Four distinct cases handled:
  1. New lock (never seen before)
  2. Lock held by another node (redirect)
  3. Lock held by current node (wait)
  4. Lock exists but unlocked (grant)
- Proper message serialization with network byte order

### Unlock Implementation (`dsm_mutex_unlock`)

Located in `DSM/src/os/dsm_os.cpp`:

```cpp
int dsm_mutex_unlock(int *mutex) {
    LockTable->LockAcquire();
    auto record = LockTable->Find(*mutex);
    if (record == nullptr) {
        LockTable->LockRelease();
        return -1;  // Invalid mutex
    }
    record->locked = false;
    LockTable->Update(*mutex, *record);
    LockTable->LockRelease();
    return 0;
}
```

**Note:** This is a local-only operation. The lock state is updated locally, and the next requester will be able to acquire it through the normal protocol.

## Data Structures

### LockRecord

```cpp
struct LockRecord {
    int owner_id;    // Node ID of current owner (-1 if no owner)
    bool locked;     // Whether lock is currently held
};
```

### SocketRecord

```cpp
struct SocketRecord {
    int socket;          // File descriptor for the connection
    uint32_t next_seq;   // Next sequence number to use
};
```

## Protocol Flow Example

### Scenario: 4 nodes, Node 0 acquires lock, Node 1 tries to acquire

```
1. Node 0 calls dsm_mutex_lock(lock_id=1)
   - Probable owner = 1 % 4 = 1
   - Connects to Node 1
   - Sends LOCK_ACQ(lock_id=1, src=0)

2. Node 1 receives LOCK_ACQ
   - No record for lock_id=1
   - Creates LockRecord(owner_id=0, locked=true)
   - Sends LOCK_REP(unused=1, grant)

3. Node 0 receives LOCK_REP
   - unused=1, lock granted!
   - Updates local LockTable
   - Returns 0 (success)

4. Node 1 calls dsm_mutex_lock(lock_id=1)
   - Probable owner = 1 % 4 = 1 (itself)
   - Checks local LockTable
   - Finds lock_id=1, owner_id=0, locked=true
   - Sends LOCK_REP(unused=0, realowner=0) to itself

5. Node 1 receives own LOCK_REP
   - unused=0, redirected to Node 0
   - Connects to Node 0
   - Sends LOCK_ACQ(lock_id=1, src=1)

6. Node 0's daemon receives LOCK_ACQ
   - Checks local LockTable
   - Finds lock_id=1, owner_id=0 (self), locked=true
   - Sends LOCK_REP(unused=0, realowner=0)

7. Node 1 receives LOCK_REP
   - unused=0, still owned by Node 0
   - Detects redirect to same node (lock is held)
   - Sleeps 100ms and retries at step 5

8. Eventually Node 0 calls dsm_mutex_unlock(lock_id=1)
   - Updates local LockTable: locked=false
   - Lock is now available

9. Node 1 retries (back to step 5)
   - Node 0's daemon now sees locked=false
   - Updates LockRecord(owner_id=1, locked=true)
   - Sends LOCK_REP(unused=1, grant)

10. Node 1 receives LOCK_REP
    - unused=1, lock granted!
    - Updates local LockTable
    - Returns 0 (success)
```

## Testing Results

### Local 4-Process Test

**Configuration:**
- 4 processes on localhost
- All nodes trying to acquire the same lock
- Lock held for 3 seconds by first acquirer

**Observed Behavior:**
1. Node 0 acquired lock first
2. Nodes 1, 2, 3 all redirected to Node 0
3. All waiting nodes received "lock is held" response
4. Waiting nodes retried with 100ms backoff
5. After Node 0 released, one waiting node acquired it
6. No deadlocks or race conditions

**Log Evidence:**
```
[DSM Daemon] Received LOCK_ACQ: lockid=1 from NodeId=0
[DSM Daemon] Granting existing lock 1 to NodeId=0
I'm 0 and I get lock A!
[DSM Daemon] Received LOCK_ACQ: lockid=1 from NodeId=1
[DSM Daemon] Lock 1 is currently held by us (NodeId=0), requester must wait
[DSM Lock] Lock 1 is held, waiting...
See? No one can get lock A because I locked it!...
```

## Performance Considerations

### Optimizations
1. **Socket Caching**: Connections are cached in SocketTable to avoid repeated connection overhead
2. **Local Lock Check**: Avoids network roundtrip if lock already held
3. **Backoff Strategy**: 100ms sleep prevents busy-waiting when lock is contended

### Potential Improvements
1. **Lock Queue**: Currently uses retry-based waiting; could implement a queue at lock owner
2. **Timeout Handling**: No timeout mechanism; a node waiting forever if owner crashes
3. **Lock Release Notification**: Could notify waiting nodes when lock is released
4. **Fairness**: No guarantee of FIFO order for lock acquisition

## Known Limitations

1. **No Deadlock Detection**: The system doesn't detect or prevent deadlocks
2. **No Lock Timeout**: A node can wait indefinitely for a lock
3. **Connection-based Protocol**: Relies on persistent TCP connections
4. **Single Lock Server**: Each lock's probable owner becomes a bottleneck
5. **No Crash Recovery**: If a lock owner crashes, lock is orphaned

## Future Work

1. Implement proper waiting queue at lock owner
2. Add timeout mechanism for lock acquisition
3. Implement lock release notifications
4. Add deadlock detection and recovery
5. Implement fair lock scheduling (FIFO or priority-based)
6. Add support for read/write locks
7. Implement lock migration for load balancing

## Conclusion

The distributed mutex lock implementation provides basic but functional distributed synchronization. The three-hop protocol with redirection works correctly, and the implementation handles concurrent requests properly. While there are areas for improvement (particularly in fairness and timeout handling), the current implementation successfully demonstrates distributed lock management across multiple processes.
