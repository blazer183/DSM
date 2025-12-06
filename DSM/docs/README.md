github copilot: 你好！这是你本次任务的任务清单

正如你所看见的，本次仓库为我的dsm中间件源代码，我需要你实现单元测试功能。也是软件工程必要一环

/docs/AICHITECTURE.md 系统介绍了目录架构，请你先阅读它，并了解报文规范。

你的任务：实现DSM/src/os/dsm_os.cpp中的锁请求功能，伪代码我已经为你实现了 

任务完成条件：在wsl环境下启动DSM/scripts/launcher.sh，分发test_dsm.cpp到多台虚拟机，能够正确返回测试日志，成功启动。 你在测试时需要修改launcher.sh里的IP成你的虚拟机IP，你需要建立多台虚拟机完成测试，测试截图放在README最后。你要解决编译时错误，修改部分源代码和补全监听线程部分。祝你好运！

Tips: 这里你对locktable的理解有偏差：locktable里写的是谁是这个锁的owner没问题，但是每当某个进程想获取锁，它不会查本地的locktable，而是先确定这个锁表的拥有者是谁，也就是probowner是谁，我是用哈希实现，即probowner = lock_id % ProcNum 从他那里查询realowner,获取realowner_id后向realowner发送消息，等到realowner回复后，再次向probowner发送消息更新锁表，同时根据realowner发送的invalide_set_count修改自己的页权限（mprotect修改）

Tips: 监听线程的JOIN报文部分你不可以通过joined_fds.size()判断是否全部进程都加入连接，因为这里使用的是TCP长连接，你需要维护一个计数，并用一把锁保护它。每次在join时递增，如果不到ProcNum就返回，如果相等就进行广播。广播时再对joined_fds调用。

Tips: 监听线程的LOCK_AQUIRE报文部分你需要处理多个进程争用锁的情况。你需要把“传递realowner_id-等待-收到owner_update报文”当成一个整体，只有这一轮结束才能把更改后的realowner返回给请求者；对于realowner的线程，需要先查看锁表的is_locked是否为真，只有为假时才可以传递锁权限。

最后，good luck to you!
