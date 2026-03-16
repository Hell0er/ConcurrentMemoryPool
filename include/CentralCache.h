#pragma once

#include "Common.h"

class SpanList;
class CentralCache
{
   public:
    // 单例接口
    static CentralCache* GetInstance() { return &_sInst; }

    // 获取一个管理空间不为空的span
    Span* GetOneSpan(SpanList& list, size_t size);
    /*
    start和end表示cc提供的空间的起始与结尾，
    batchNum表示tc需要多少块size大小的空间，
    size表示tc需要的单块空间的大小，
    返回cc实际提供的空间大小
    */
    size_t FetchRangeObj(void*& start, void*& end, size_t batchNum,
                         size_t size);

    void ReleaseListToSpans(void* start, size_t size);

   private:
    CentralCache() {}
    CentralCache(const CentralCache& copy) = delete;
    CentralCache& operator=(const CentralCache& copy) = delete;

    static CentralCache _sInst;  // 饿汉模式创建CentralCache单例

    SpanList _spanLists[FREE_LIST_NUM];  // hash，表示每个桶一个Span链表
};