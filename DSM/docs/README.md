github copilot: 你好！这是你本次任务的任务清单

正如你所看见的，本次仓库为我的dsm中间件源代码，我需要你实现单元测试功能。也是软件工程必要一环

/docs/AICHITECTURE.md 系统介绍了目录架构，请你先阅读它，并了解报文规范。

你的任务：

实现DSM/src/os/dsm_os_cond.cpp中InitDataStructs() FetchGlobalData()中的伪代码；

实现DSM/src/os/dsm_os.cpp中dsm_malloc(),dsm_mutex_lock(), dsm_mutex_init() dsm_mutex_finalize()中的伪代码；

任务完成条件：在wsl环境下启动DSM/scripts/launcher.sh，分发test_dsm.cpp到多台虚拟机，能够正确返回测试日志，成功启动。 你在测试时需要修改launcher.sh里的IP成你的虚拟机IP，你需要建立多台虚拟机完成测试，测试截图放在README最后。你要解决编译时错误，修改部分源代码和补全监听线程部分。祝你好运！

Tips: 每一个进程给自己的监听进程发送消息也是通过socket编程实现的而不是进程间通信！！这样方便管理。你也已经看到我在GetPodIp时把自己的IP设置为127.0.0.1了

最后，good luck to you!
