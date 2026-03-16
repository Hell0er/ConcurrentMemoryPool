#pragma once

#include "Common.h"

class FreeList;
class ThreadCache
{
   public:
    void* Allocate(size_t size);  // 线程申请size大小的空间

    void Deallocate(void* obj, size_t size);  // 回收线程中大小为size的空间

    // ThreadCache中空间不够时，向CentralCache申请空间
    void* FetchFromCentralCache(size_t index, size_t alignSize);

    // tc向cc归还空间，自由链表list归还size大小的小块
    void ListTooLong(FreeList& list, size_t size);

   private:
    FreeList _freeLists[FREE_LIST_NUM];  // hash，表示每个桶一个自由链表
};

static __thread ThreadCache* pTLSThreadCache = nullptr;