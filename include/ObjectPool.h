#pragma once

#include <iostream>
#include <mutex>

#include "Common.h"

using std::cout;
using std::endl;

template <class T>
class ObjectPool
{
   private:
    char* _memory = nullptr;  // 指向内存块的ptr
    size_t _remainBytes = 0;  // 大块内存在切分过程中的剩余字节数
    void* _freelist = nullptr;  // 自由链表，连接还回来的空闲空间
    std::mutex _poolMtx;        // 防止ThreadCache申请到nullptr
   public:
    T* New()
    {
        T* obj = nullptr;  // 返回的空间

        if (_freelist != nullptr)  // 自由链表中有空闲空间
        {
            void* next = *(void**)_freelist;
            obj = (T*)_freelist;
            _freelist = next;
        }
        else  // 自由链表中没有空闲空间
        {
            // if (_memory == nullptr)  // _memory 没有空闲空间了
            if (_remainBytes < sizeof(T))  // 包含了剩余空间为0的情况
            {
                _remainBytes = 128 * 1024;
                // _memory = (char*)malloc(_remainBytes);  // 申请128K的空间
                _memory = (char*)SystemAlloc(_remainBytes >> PAGE_SHIFT);
                if (_memory == nullptr)  // 失败
                {
                    throw std::bad_alloc();
                }
            }

            obj = (T*)_memory;
            // 判断T的大小，小于一个指针就给一个指针的大小，大于则直接给T的大小
            size_t objSize =
                sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
            _memory += objSize;
            _remainBytes -= objSize;
        }

        new (obj) T;  // 构造

        return obj;
    }

    void Delete(T* obj)
    {
        obj->~T();  // 析构

        *(void**)obj = _freelist;
        _freelist = obj;
    }

    std::mutex& getPoolMtx() { return _poolMtx; }
};