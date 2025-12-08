# DSM Implementation Notes

## Overview
This document describes the implementation of the DSM (Distributed Shared Memory) pseudocode functions as required by `DSM/docs/README.md`.

## Implemented Functions

### 1. segv_handler (DSM/src/os/pfhandler.cpp)

**Purpose:** Handle SIGSEGV signals for page faults in the shared memory region.

**Implementation:**
```cpp
- Check if fault address is within managed shared memory region
- If not in region: Chain to previous SIGSEGV handler (system default)
- If in region:
  - Calculate page index from fault address
  - Check InvalidPages[page_index]
  - If InvalidPages[page_index] == 0: Call pull_remote_page()
  - Set InvalidPages[page_index] = 1 (mark as modified)
  - Call mprotect() to restore page permissions (PROT_READ | PROT_WRITE)
```

**Key Features:**
- Proper handling of non-DSM faults by chaining to previous handler
- Uses bit masking to calculate page-aligned addresses
- Restores write permissions after fetching page data

### 2. pull_remote_page (DSM/src/os/pfhandler.cpp)

**Purpose:** Fetch page data from remote node that owns the page.

**Implementation:**
```cpp
- Calculate page_index from page_base address
- Calculate probable owner: probowner = page_index % ProcNum
- Loop with retry logic (max 10 retries):
  - Connect to probable owner using getsocket()
  - Send DSM_MSG_PAGE_REQ with page_index
  - Receive DSM_MSG_PAGE_REP response
  - Check unused field:
    - If unused == 0: Redirect, update probowner and retry
    - If unused == 1: Page data received
      - Copy page data to memory (DMA simulation)
      - Send DSM_MSG_OWNER_UPDATE to manager
      - Wait for ACK
      - Break retry loop
```

**Key Features:**
- Implements three-hop protocol with owner redirection
- Proper sequence number management per connection
- Concurrent page ownership update while "DMA" is in progress
- Dynamic memory allocation for page buffer (prevents stack overflow)
- Network byte order conversion (htonl/htons)

### 3. handle_page_require (DSM/src/concurrent/concurrent_daemon.cpp)

**Purpose:** Server-side handling of page requests from other nodes.

**Implementation:**
```cpp
- Extract page_index from request payload
- Lock the page for sequential access: PageTable->LockPage(page_addr)
- Get page record from PageTable
- Handle four cases:

  Case 1: owner_id == PodId (we are the owner)
    - Send DSM_MSG_PAGE_REP with unused=1
    - Send real_owner_id (our PodId)
    - Send page data (DSM_PAGE_SIZE bytes)

  Case 2: owner_id == -1 && PodId != 0 (first access, not Pod 0)
    - Create socket to Pod 0
    - Forward PAGE_REQ to Pod 0
    - Receive page data from Pod 0
    - Forward page data to original requester

  Case 3: owner_id == -1 && PodId == 0 (first access, is Pod 0)
    - Look up bind table for file source
    - Load page data (currently zero-filled)
    - Update PageTable to mark self as owner
    - Send page data to requester

  Case 4: owner_id != PodId (redirect to real owner)
    - Send DSM_MSG_PAGE_REP with unused=0
    - Send real_owner_id only (no page data)

- Unlock page: PageTable->UnlockPage(page_addr)
```

**Key Features:**
- Sequential page access using page-level mutexes
- Proper handling of all protocol cases from ARCHITECTURE.md
- Updates ownership when Pod 0 serves initial pages
- Correct payload sizes for redirect vs. data responses

### 4. handle_owner_update (DSM/src/concurrent/concurrent_daemon.cpp)

**Purpose:** Update page ownership information in the PageTable.

**Implementation:**
```cpp
- Extract page_index and new_owner_id from payload
- Calculate page address: page_addr = SharedAddrBase + page_index * PAGESIZE
- Lock the page: PageTable->LockPage(page_addr)
- Acquire global table lock: PageTable->LockAcquire()
- Find or create PageRecord
- Update PageRecord.owner_id = new_owner_id
- Release global table lock: PageTable->LockRelease()
- Unlock the page: PageTable->UnlockPage(page_addr)
- Send ACK with same seq_num
```

**Key Features:**
- Two-level locking (global table + page-level)
- Follows locking protocol from ARCHITECTURE.md Tips section
- Creates PageRecord if doesn't exist
- Proper ACK response with sequence number

### 5. dsm_mutex_lock Enhancement (DSM/src/os/dsm_os.cpp)

**Purpose:** Implement scope consistency by invalidating pages.

**Implementation:**
```cpp
- Note added: Page invalidation should be selective
- Only pages in invalid_page_list from LOCK_REP should be invalidated
- Removed blanket mprotect() call (was causing issues)
- Relies on InvalidPages array for tracking
```

**Rationale:**
- Blanket mprotect(PROT_NONE) on all pages causes excessive page faults
- Scope consistency requires invalidating only pages modified under other locks
- Lock manager returns invalid_page_list for this purpose

## Protocol Implementation

### Message Flow Examples

**Page Request (First Access):**
```
1. Process N accesses page P
2. SIGSEGV → segv_handler → pull_remote_page
3. Calculate probable owner: O = P % ProcNum
4. Send PAGE_REQ to O
5. O has owner_id == -1, forwards to Pod 0
6. Pod 0 loads page, sends back to O
7. O sends page to N
8. N sends OWNER_UPDATE to O
9. N writes page data to memory
```

**Lock Acquisition:**
```
1. Process N calls dsm_mutex_lock(lock_id)
2. Calculate manager: M = lock_id % ProcNum
3. Send LOCK_ACQ to M
4. M's listener thread calls pthread_mutex_lock (blocks if held)
5. When available, M sends LOCK_REP with invalid_page_list
6. N receives LOCK_REP and returns to user code
```

## Technical Considerations

### Signal Handler Safety
The segv_handler performs network I/O and memory allocation, which is not strictly async-signal-safe. This is a known limitation in DSM systems. In production, consider:
- Using a separate page fault handling thread
- Queueing page faults for asynchronous processing
- Pre-allocating buffers to avoid malloc in signal context

### Concurrency
- Each incoming connection gets its own thread (via std::thread in peer_handler)
- Lock acquisition blocks listener threads (pthread_mutex_lock)
- Page-level locking prevents race conditions in PageTable updates
- Socket reuse via SocketTable minimizes connection overhead

### Memory Management
- SharedAddrBase mapped with PROT_NONE initially
- Page permissions restored to PROT_READ|PROT_WRITE after fetch
- InvalidPages array tracks modified pages for scope consistency
- Dynamic allocation used for large buffers (DSM_PAGE_SIZE = 4096)

## Build Instructions

```bash
cd /home/runner/work/DSM_test_generation/DSM_test_generation

# Build command
g++ -std=c++17 -pthread -DUNITEST \
    -I"DSM/include" \
    test_dsm.cpp \
    "DSM/src/os/dsm_os.cpp" \
    "DSM/src/os/pfhandler.cpp" \
    "DSM/src/concurrent/concurrent_daemon.cpp" \
    "DSM/src/network/connection.cpp" \
    -o dsm_app \
    -lpthread
```

## Testing

### Single Machine Test
```bash
# Set environment
export DSM_LEADER_IP="127.0.0.1"
export DSM_LEADER_PORT="9999"
export DSM_TOTAL_PROCESSES=4
export DSM_WORKER_COUNT=1
export DSM_WORKER_IPS="127.0.0.1"

# Start processes
export DSM_POD_ID=0; ./dsm_app &  # Leader
export DSM_POD_ID=1; ./dsm_app &  # Worker 1
export DSM_POD_ID=2; ./dsm_app &  # Worker 2
export DSM_POD_ID=3; ./dsm_app &  # Worker 3
```

### Multi-VM Test
Use `DSM/scripts/launcher.sh` after updating VM IPs:
```bash
cd DSM/scripts
./launcher.sh 4  # 4 total processes
```

## Code Review Fixes Applied

1. ✅ Moved extern declaration to file scope
2. ✅ Fixed payload_len for redirect (sizeof(uint16_t) only)
3. ✅ Changed stack allocation to heap (new/delete)
4. ✅ Added BindRecord constructor for 3 parameters
5. ✅ Proper error handling throughout

## References

- DSM/docs/ARCHITECTURE.md - Protocol specification
- DSM/docs/README.md - Task requirements
- Computer Systems: A Programmer's Perspective (CSAPP) - RIO functions
