## ConcurrentMemoryPool
c++实现高并发内存池（linux版）

# 内存池结构
主要包含三层缓存结构:
- ThreadCache
- CentralCache
- PageCache

# 优化
使用三层基数树优化锁相关的性能瓶颈

# Linux版本与Windows版本的区别
1. Linux申请和释放空间使用函数不同，参考include/Common.h
2. 本次开发使用的linux系统页面大小为4kb，不是8kb，因此static const size_t PAGE_SHIFT = 12，2^12 = 4KB
3. Linux下地址空间为64位
4. Linux64为地址空间下，下基数树必须使用三层，参考代码(include/PageMap.h)
