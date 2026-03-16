#pragma once

#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

class PageCache
{
   public:
    // 单例接口
    static PageCache* GetInstance() { return &_sInst; }

    // pc从_spanLists中拿出一个k页的span
    Span* NewSpan(size_t k);

    std::mutex& getPageMtx() { return _pageMtx; }

    // 将页ID映射为相应Span，每个Span至少管理一页
    Span* MapObj2Span(void* obj);

    // 回收cc还回来的span
    void ReleaseSpanToPageCache(Span* span);

   private:
    PageCache() {}
    PageCache(const PageCache& pc) = delete;
    PageCache& operator=(const PageCache& pc) = delete;

    static PageCache _sInst;

    SpanList _spanLists[PAGE_NUM];  // pc中的哈希
    std::mutex _pageMtx;            // pc整体的锁

    // map映射，用来快速通过页号找到对应的span
    // std::unordered_map<PageID, Span*> _id2SpanMap;
    TCMalloc_PageMap3<48 - PAGE_SHIFT> _id2SpanMap;

    // span对象的定长内存池
    ObjectPool<Span> _spanPool;
};