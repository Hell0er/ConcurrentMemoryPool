#pragma once

#include <sys/mman.h>

#include <cassert>
#include <iostream>
#include <mutex>
#include <thread>

using std::cout;
using std::endl;

typedef size_t PageID;

static const size_t FREE_LIST_NUM = 208;  // 哈希表中自由链表的数目
static const size_t MAX_BYTES = 256 * 1024;  // tc单次申请的最大字节数
static const size_t PAGE_NUM = 129;  // span的最大管理页数（下标从1开始）
static const size_t PAGE_SHIFT = 12;  // 一页占多少位（12位，4KB）

static void*& ObjNext(void* obj)  // 获取obj的头
{
    return *(void**)obj;
}

class FreeList
{
   public:
    size_t Size()  // 返回自由链表中存放的块的数目
    {
        return _size;
    }

    bool Empty()  // 判断哈希桶是否为空
    {
        return _freelist == nullptr;
    }

    void Push(void* obj)  // 回收空间
    {
        assert(obj != nullptr);

        ObjNext(obj) = _freelist;
        _freelist = obj;

        ++_size;  // 加上回收的一块
    }

    void PushRange(void* start, void* end, size_t size)
    {
        ObjNext(end) = _freelist;
        _freelist = start;

        _size += size;  // 加上回收的若干块
    }

    void* Pop()  // 提供空间
    {
        assert(_freelist != nullptr);

        void* obj = _freelist;
        _freelist = ObjNext(obj);

        --_size;  // 减去提供的一块

        return obj;
    }

    // 删掉桶中的前n个块，并返回删除的空间
    void PopRange(void*& start, void*& end, size_t n)
    {
        // 删除块数n不超过size
        assert(n <= _size);

        start = end = _freelist;
        for (size_t i = 0; i < n - 1; ++i)
        {
            end = ObjNext(end);
        }

        _freelist = ObjNext(end);
        ObjNext(end) = nullptr;
        _size -= n;
    }

    size_t& MaxSize() { return _maxSize; }

   private:
    void* _freelist = nullptr;  // 自由链表
    // 当前自由链表还能向CentralCache申请的块空间的最大数目
    size_t _maxSize = 1;
    // 当前自由链表中有多少块空间
    size_t _size = 0;
};

class sizeClass
{
   public:
    static size_t _RounUp(size_t size, size_t alignNum)
    {
        size_t res = 0;
        if (size % alignNum)
        {
            res = (size / alignNum + 1) * alignNum;
        }
        else
        {
            res = size;
        }
        return res;
    }

    static size_t RoundUp(size_t size)  // 计算对齐后的字节数
    {
        if (size <= 128)  // [1, 128] 8B * 16
        {
            return _RounUp(size, 8);
        }
        else if (size <= 1024)  // [128+1, 1024] 16B * 56
        {
            return _RounUp(size, 16);
        }
        else if (size <= 8 * 1024)  // [1024+1, 8*1024] 128B * 56
        {
            return _RounUp(size, 128);
        }
        else if (size <= 64 * 1024)  // [8*1024+1, 64*1024] 1024B * 56
        {
            return _RounUp(size, 1024);
        }
        else if (size <= 256 * 1024)  // [64*1024+1, 256*1024] 8*1024B * 24
        {
            return _RounUp(size, 8 * 1024);
        }
        else
        {
            // 单次申请空间到达了超过了256KB，直接按页来读取
            return _RounUp(size, 1 << PAGE_SHIFT);
            // assert(false);
            // return -1;
        }
    }

    // 求size对应在哈希表中的下标
    static inline size_t _Index(size_t size, size_t align_shift)
    {
        return ((size + (1 << align_shift) - 1) >> align_shift) - 1;
    }

    // 计算映射的哪一个自由链表桶
    static inline size_t Index(size_t size)
    {
        assert(size <= MAX_BYTES);

        // 每个区间有多少个链
        static int group_array[4] = {16, 56, 56, 56};
        if (size <= 128)
        {  // [1,128] 8B -->8B就是2^3B，对应二进制位为3位
           // 3是指对齐数的二进制位位数，这里8B就是2^3B，所以就是3
            return _Index(size, 3);
        }
        else if (size <= 1024)
        {  // [128+1,1024] 16B -->4位
            // 这里_Index计算的是当前size所在区域的第几个下标，所以Index的返回值需要加上前面所有区域的哈希桶的个数
            return _Index(size - 128, 4) + group_array[0];
        }
        else if (size <= 8 * 1024)
        {  // [1024+1,8*1024] 128B -->7位
            return _Index(size - 1024, 7) + group_array[1] + group_array[0];
        }
        else if (size <= 64 * 1024)
        {  // [8*1024+1,64*1024] 1024B -->10位
            return _Index(size - 8 * 1024, 10) + group_array[2] +
                   group_array[1] + group_array[0];
        }
        else if (size <= 256 * 1024)
        {  // [64*1024+1,256*1024] 8 * 1024B  -->13位
            return _Index(size - 64 * 1024, 13) + group_array[3] +
                   group_array[2] + group_array[1] + group_array[0];
        }
        else
        {
            assert(false);
        }
        return -1;
    }

    static size_t NumMoveSize(size_t size)
    {
        assert(size > 0);
        // 简单控制
        int num = MAX_BYTES / size;
        if (num > 512)
        {
            num = 512;
        }
        if (num < 2)
        {
            num = 2;
        }
        return num;
    }

    // 块页匹配
    static size_t NumMovePage(size_t size)
    {
        // 获取tc向cc申请size大小的块时的单次最大申请块数
        size_t num = NumMoveSize(size);

        // 单次最大申请空间的大小
        size_t npage = num * size;

        // PAGE_SHIFT表示一页占用多少位，得到单次申请最大空间占多少页
        npage >>= PAGE_SHIFT;

        if (npage == 0)
        {
            npage = 1;
        }

        return npage;
    }
};

struct Span  // 以页为基本单位的结构体
{
   public:
    PageID _pageID = 0;  // 首页页号
    size_t _n;           // 该Span管理的页数

    void* _freeList = nullptr;  // span下的小块空间的头节点
    size_t use_count = 0;  // 当前span分配出去的小块空间的数目
    size_t _objSize = 0;   // span所管理页的小块的大小

    Span* _prev = nullptr;
    Span* _next = nullptr;

    bool _isUse = false;  // 判断当前span在cc中还是pc中
};

class SpanList
{
   public:
    Span* Begin() { return _head->_next; }
    Span* End() { return _head; }

    SpanList()  // 构造函数中创建哨兵位头节点
    {
        _head = new Span;

        // 双向链表
        _head->_next = _head;
        _head->_prev = _head;
    }

    bool Empty() { return _head == _head->_next; }

    void PushFront(Span* span) { Insert(Begin(), span); }

    Span* PopFront()
    {
        Span* front = _head->_next;
        Erase(front);
        return front;
    }

    void Insert(Span* pos, Span* ptr)  // 在pos前面插入ptr
    {
        assert(pos != nullptr);
        assert(ptr != nullptr);

        Span* prev = pos->_prev;
        prev->_next = ptr;
        ptr->_prev = prev;
        pos->_prev = ptr;
        ptr->_next = pos;
    }

    void Erase(Span* pos)
    {
        assert(pos != nullptr);
        assert(pos != _head);

        Span* prev = pos->_prev;
        prev->_next = pos->_next;
        pos->_next->_prev = prev;
    }

    std::mutex& GetMtx() { return _mtx; }

   private:
    Span* _head = nullptr;
    std::mutex _mtx;  // CentralList中的每个SpanList都有一个桶锁
};

inline static void* SystemAlloc(size_t kpage)
{
    void* ptr = mmap(NULL, kpage << PAGE_SHIFT, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == nullptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

inline static void SystemFree(void* ptr, size_t kpage)
{
    if (ptr == nullptr)
    {
        return;
    }

    size_t bytes = kpage << PAGE_SHIFT;
    munmap(ptr, bytes);
}