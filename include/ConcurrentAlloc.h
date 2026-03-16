#pragma once

#include "ObjectPool.h"
#include "PageCache.h"
#include "ThreadCache.h"

class ThreadCache;
class PageCache;
// class ObjectPool;

// 即tcmalloc，线程调用该函数申请空间
void* ConcurrentAlloc(size_t size)
{
    if (size <= MAX_BYTES)
    {
        /*
        由于pTLSThreadCache是TLS的，每个线程都会有一个，且相互独立，
        所以不存在竞态条件，只需要判断一次就可以直接new，没有线程安全问题
        */
        if (pTLSThreadCache == nullptr)
        {
            // 此时相当于每个线程都有了ThreadCache对象
            // pTLSThreadCache = new ThreadCache;
            // 引入定长内存池ObjectPool，替换掉new操作
            static ObjectPool<ThreadCache> objPoll;  // 静态的，一直存在
            // 需要上锁，不能多线程可能访问到nullptr
            objPoll.getPoolMtx().lock();
            pTLSThreadCache = objPoll.New();
            objPoll.getPoolMtx().unlock();
        }

        // return pTLSThreadCache;
        return pTLSThreadCache->Allocate(size);
    }
    else
    {
        // 申请空间超过最大空间256KB，则直接向pc或者os要
        size_t alignSize = sizeClass::RoundUp(size);  // 对齐
        size_t k = alignSize >> PAGE_SHIFT;           // 需要多少页

        // 对pc的span进行操作
        PageCache::GetInstance()->getPageMtx().lock();  // 上锁
        Span* span = PageCache::GetInstance()->NewSpan(k);
        PageCache::GetInstance()->getPageMtx().unlock();  // 解锁
        span->_objSize = alignSize;

        // 通过pageID的偏移获取地址
        void* ptr = (void*)(span->_pageID << PAGE_SHIFT);
        return ptr;
    }
}

// 线程调用该函数回收空间
void ConcurrentFree(void* ptr)
{
    assert(ptr != nullptr);

    Span* span = PageCache::GetInstance()->MapObj2Span(ptr);
    size_t size = span->_objSize;

    if (size <= MAX_BYTES)
    {
        pTLSThreadCache->Deallocate(ptr, size);
    }
    else
    {
        // 释放空间大于256KB，直接还给pc
        PageCache::GetInstance()->getPageMtx().lock();
        PageCache::GetInstance()->ReleaseSpanToPageCache(span);
        PageCache::GetInstance()->getPageMtx().unlock();
    }
}