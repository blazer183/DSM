# Distributed Shared Memory

本项目是DSM的一次小小的探索，由djx与zrz开发



本次实验心得：

## 1.static and debug

宏定义static关键字实现单元测试时的debug

```
g++ -D<宏(UNITEST)> src/os/A.cpp tests/unit/test_A.cpp -Iinclude -o test_A
```

```
#ifdef UNITEST
#define STATIC
#else
#define STATICI static
#endif
```

## 2.extern C

extern C在系统头文件里均有定义，只需要在自定义的纯C头文件里定义

## 3.ssh免密登录

1. 设置脚本

init_ssh.sh只需要运行一次永久生效，launcher.sh每个项目都需要发起一次

2. 命令行设置

```
//登录wsl后
sudo apt install openssh-server -y
//安装
sudo rm /etc/ssh/ssh_host_*
sudo ssh-keygen -A
//移除原先的密钥 配置新密钥
sudo nano /etc/ssh/sshd_config
//查询配置文件，把文件对应的端口添加入站规则，默认22
sudo service ssh restart
sudo service ssh status
//启动ssh并验证状态
ip addr
//查询本次IP


//客户端：
./init_ssh.sh
输入密码

```

## 4. dos2unix

```
sudo apt install dos2unix
dos2unix A.sh
```

作用：不同操作系统换行符转换