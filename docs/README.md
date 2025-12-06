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

作用：不同操作系统换行符转换，这里经常出现有文件但显示required file not found 的情况

## 5.创建软链接

```
ln -s "/mnt/c/Users/20399/Desktop/DSM_test_generation" dsm
```

## 6.git小连招

```
git init
git remote add origin http://......git

git fetch
git switch 分支名

git pull	//只拉取本地分支的库并合并
git reset --hard HEAD	//重置仓库指令

git push origin --delete <分支名>	//删除分支
git branch	//查看本地分支 -a 查看远端+本地分支

git config --global credential.helper 'cache --timeout=36000'	//缓存10小时密码
```

适当听从建议：![image-20251205113815429](C:\Users\20399\AppData\Roaming\Typora\typora-user-images\image-20251205113815429.png)

## 7.vscode速览代码

F12跳转定义，Alt+<-是返回上一光标
