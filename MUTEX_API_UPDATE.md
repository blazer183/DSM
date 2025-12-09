# 互斥锁 API 更新总结

## 更新概览

根据头文件的更新,将所有源文件中的旧锁API调用更新为新的显式命名API。

## API 变更映射

### 全局锁 (Table-level 锁)
- `LockAcquire()` → `GlobalMutexLock()`
- `LockRelease()` → `GlobalMutexUnlock()`

### 局部锁 (Record-level 锁)
- `LockPage(key)` → `LocalMutexLock(key)`
- `UnlockPage(key)` → `LocalMutexUnlock(key)`

## 文件修改详情

### 1. concurrent_daemon.cpp

#### 修改的函数调用
- **handle_lock_acquire()**: 
  - `LockTable->LockAcquire()` → `LockTable->GlobalMutexLock()`
  - `LockTable->LockRelease()` → `LockTable->GlobalMutexUnlock()`
  - 涉及 6 处修改

- **handle_owner_update()**: 
  - `PageTable->LockPage(page_addr)` → `PageTable->LocalMutexLock(page_addr)`
  - `PageTable->UnlockPage(page_addr)` → `PageTable->LocalMutexUnlock(page_addr)`
  - `PageTable->LockAcquire()` → `PageTable->GlobalMutexLock()`
  - `PageTable->LockRelease()` → `PageTable->GlobalMutexUnlock()`
  - 涉及 4 处修改

- **handle_lock_release()**: 
  - `LockTable->LockAcquire()` → `LockTable->GlobalMutexLock()`
  - `LockTable->LockRelease()` → `LockTable->GlobalMutexUnlock()`
  - 涉及 2 处修改

- **handle_page_require()**: 
  - `PageTable->LockPage(page_index)` → `PageTable->LocalMutexLock(page_index)`
  - `PageTable->UnlockPage(page_addr)` → `PageTable->LocalMutexUnlock(page_addr)` (多处)
  - `PageTable->LockAcquire()` → `PageTable->GlobalMutexLock()`
  - `PageTable->LockRelease()` → `PageTable->GlobalMutexUnlock()`
  - `BindTable->LockAcquire()` → `BindTable->GlobalMutexLock()`
  - `BindTable->LockRelease()` → `BindTable->GlobalMutexUnlock()`
  - 涉及 15+ 处修改

#### 修改统计
- GlobalMutexLock/Unlock: 约 14 处
- LocalMutexLock/Unlock: 约 13 处
- **总计**: 约 27 处修改

### 2. dsm_os.cpp

#### 修改的函数
- **dsm_mutex_lock()**: 
  - `SocketTable->LockAcquire()` → `SocketTable->GlobalMutexLock()`
  - `SocketTable->LockRelease()` → `SocketTable->GlobalMutexUnlock()`

- **dsm_mutex_unlock()**: 
  - `SocketTable->LockAcquire()` → `SocketTable->GlobalMutexLock()`
  - `SocketTable->LockRelease()` → `SocketTable->GlobalMutexUnlock()`

#### 修改统计
- GlobalMutexLock/Unlock: 4 处
- **总计**: 4 处修改

### 3. pfhandler.cpp

#### 修改的函数
- **handle_pagefault()**: 
  - `SocketTable->LockAcquire()` → `SocketTable->GlobalMutexLock()`
  - `SocketTable->LockRelease()` → `SocketTable->GlobalMutexUnlock()`
  - 在两个不同位置各修改一次

#### 修改统计
- GlobalMutexLock/Unlock: 4 处
- **总计**: 4 处修改

## 修改原则

### 1. 全局锁使用场景
用于保护整个表结构的操作:
- 插入/删除/查找表项
- 遍历表结构
- 修改表元数据

```cpp
// 旧代码
LockTable->LockAcquire();
LockRecord* record = LockTable->Find(lock_id);
LockTable->LockRelease();

// 新代码
LockTable->GlobalMutexLock();
LockRecord* record = LockTable->Find(lock_id);
LockTable->GlobalMutexUnlock();
```

### 2. 局部锁使用场景
用于保护单个记录的操作:
- 锁定特定页面进行读写
- 独占访问某个页面记录
- 修改单个记录内容

```cpp
// 旧代码
if (!PageTable->LockPage(page_addr)) {
    return false;
}
// ... 操作页面 ...
PageTable->UnlockPage(page_addr);

// 新代码
if (!PageTable->LocalMutexLock(page_addr)) {
    return false;
}
// ... 操作页面 ...
PageTable->LocalMutexUnlock(page_addr);
```

### 3. 嵌套锁策略
- 先获取全局锁查找记录
- 释放全局锁
- 再获取局部锁操作记录

```cpp
// 正确的嵌套模式
PageTable->GlobalMutexLock();
PageRecord* record = PageTable->Find(page_index);
PageTable->GlobalMutexUnlock();

if (record != nullptr) {
    PageTable->LocalMutexLock(page_index);
    // ... 操作 record ...
    PageTable->LocalMutexUnlock(page_index);
}
```

## 验证检查

### 自动验证
使用 grep 搜索确认没有遗留的旧API调用:
```bash
grep -r "LockAcquire\|LockRelease\|LockPage\|UnlockPage" DSM/src/
```
结果: **无匹配** ✓

### 受影响的表类型
- ✓ PageTable
- ✓ LockTable
- ✓ SocketTable
- ✓ BindTable

### 受影响的文件
- ✓ concurrent_daemon.cpp (27 处)
- ✓ dsm_os.cpp (4 处)
- ✓ pfhandler.cpp (4 处)
- ✓ dsm_os_cond.cpp (0 处,无需修改)

## 总修改统计

- **文件数**: 3
- **总修改数**: 35 处
- **GlobalMutexLock/Unlock**: 约 22 处
- **LocalMutexLock/Unlock**: 约 13 处

## 优势

1. **语义清晰**: 通过函数名立即了解是全局锁还是局部锁
2. **可维护性**: 更容易理解代码意图和锁的范围
3. **调试友好**: 在调试时能快速识别锁类型
4. **一致性**: 所有表使用相同的锁API命名规范

## 注意事项

1. **Windows 编译**: 代码中包含 Linux 特定头文件 (`sys/socket.h`, `arpa/inet.h` 等),需要在 Linux 环境下编译
2. **线程安全**: 确保在并发环境下正确使用全局锁和局部锁
3. **死锁预防**: 避免不当的嵌套锁顺序
4. **错误处理**: 所有锁操作都检查返回值

## 后续建议

1. 添加锁超时机制防止死锁
2. 考虑使用 RAII 封装锁管理
3. 添加锁性能监控和统计
4. 编写单元测试验证锁的正确性
