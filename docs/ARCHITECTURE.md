文件夹 PATH 列表
卷序列号为 7A53-DBC3
C:.
├─config    //集群描述，包括IP，角色，节点ID等
├─docs      //新人上手必看，包含项目简介，快速上手，使用说明
├─include   //头文件，展开后供各个模块复用
│  ├─net    //报文规范，流式读取函数的声明
│  └─os     //定义页表结构和宏
├─scripts
├─src
│  ├─concurrent //服务端模型
│  ├─network    //报文规范，流式读取函数封装
│  ├─os         //客户端模型
│  └─main.c     //测试程序入口
└─tests
    ├─integration   //集成测试
    └─unit          //单元测试
  

主要分为src文件夹里的三个模块，其中concurrent与os通过network连接。