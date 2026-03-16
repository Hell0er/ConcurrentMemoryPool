# ConcurrentMemoryPool
**本项目为一个C++实现高并发内存池（Linux版）**

在Terminal中输入：`g++ -g -o2 -I include/ src/* Benchmark.cpp -o main -lpthread`，即可编译。

如果不需要调试信息，可以直接：`g++ -I include/ src/* Benchmark.cpp -o main -lpthread`，生成可执行文件`main`后 `./main` 运行，看到测试结果。

### 内存池结构

![structure](images/structure.png)

包含三层缓存结构：

- ThreadCache
- CentralCache
- PageCache

### 优化

![radixtree](images/radixtree.png)

使用三层基数树优化锁相关的性能瓶颈

### 结果图

优化前：

![benchmark1](images/benchmark1.png)

![flamegraph1](images/flamegraph1.png)

优化后：

![benchmark2](images/benchmark2.png)

![flamegraph2](images/flamegraph2.png)

可以看到少了很多unique_lock的开销。

### Linux版本与Windows版本的区别
1. Linux申请和释放空间使用函数不同，参考include/Common.h
2. 本次开发使用的linux系统页面大小为4KB，不是8KB，因此static const size_t PAGE_SHIFT = 12，2<<12 = 4KB

3. Linux下地址空间为64位
4. Linux64为地址空间下，下基数树必须使用三层，参考代码(include/PageMap.h)
