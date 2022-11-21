# tinyasync

仓库地址: https://github.com/lhprojects/tinyasync

fork到我的仓库: https://github.com/rainboyOJ/tinyasync

一个由c++20协程编写的网络io,想学习它的原理

## 文件结构

```plaintext
include
└── tinyasync
    ├── awaiters.h  各种的等待器,实现的协程的暂停
    ├── basics.h    所需的头文件,基础类,工具类的定义
    ├── buffer.h    buffer数组
    ├── dns_resolver.h  hostName 转 ip
    ├── io_context.h    核心,IO中心
    ├── memory_pool.h   内存池,内存分配
    ├── mutex.h         锁,队列锁,无锁队列
    ├── task.h          协程的Return Object 实现
    └── tinyasync.h     包含其它头文件

1 directory, 9 files
```

## 解析

[io_context解析](./io_context.md)


## 原理1: 不需要虚函数

前提：

1. 只需要一个函数进行虚化，所以可以直接定义一个成员变量来存储指针


类的布局图

```
+--------------------+            +--------------------+    
|m_callback          |            |m_callback          |    
|                    | A          |                    | A  
+--------------------+            +--------------------+    
|b_do_call<type C1>()|            |b_do_call<type C1>()|    
|b_do_call<type C3>()|            |b_do_call<type C3>()|    
|b_do_call<type C2>()| B          |b_do_call<type C2>()| B  
|   ......           |            |   ......           |    
+--------------------+            +--------------------+    
|                    |            |                    |    
|on_callback()       | C1         |on_callback()       | C2 
|                    |            |                    |    
+--------------------+            +--------------------+    
```

代码:`./code_by_self/callback.cpp`

可以把所有的`c1,c2,c3`转成


TODO 补充完整

## PostTask

加入任务的队列的有哪些

- post_task 函数

`io_context`里有
`dns_resolve`里
`mutex.h`里有

需要压入任务的
`awaiter.h`
`mutex.h`
`dns_resolve.h`
