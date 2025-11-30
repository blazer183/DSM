本项目是DSM的一次小小的探索，由djx与zrz开发



本次实验心得：

1. 宏定义static关键字实现单元测试时的debug

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

2. extern C在系统头文件里均有定义，只需要在自定义的纯C头文件里定义
3. 