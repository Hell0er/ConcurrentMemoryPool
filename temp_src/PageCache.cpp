#include "PageCache.h"

PageCache PageCache::_sInst;  // PageCache的饿汉对象

Span* PageCache::NewSpan(size_t k)
{
    // assert(k > 0 && k < PAGE_NUM);
    assert(k > 0);

    if (k >= PAGE_NUM)
    {
        void* ptr = SystemAlloc(k);  // 直接向os申请

        // Span* span = new Span;
        Span* span = _spanPool.New();  // 用定长内存池开空间
        span->_pageID = ((PageID)ptr >> PAGE_SHIFT);
        span->_n = k;

        // pageID与span映射
        _id2SpanMap[span->_pageID] = span;

        return span;
    }

    // else: k < PAGE_NUM
    // 1
    // k号桶中有span
    if (!_spanLists[k].Empty())
    {
        Span* span = _spanLists[k].PopFront();

        // 记录分配出去的span管理的页号与其地址的映射关系
        for (PageID i = 0; i < span->_n; ++i)
        {
            _id2SpanMap[span->_pageID + i] = span;
        }

        return span;
    }

    // 2
    // k号桶中没有span，但后面的桶中有span
    for (int i = k + 1; i < PAGE_NUM; ++i)
    {
        if (!_spanLists[i].Empty())
        {
            // i号桶中有span，划分该span
            Span* nSpan = _spanLists[i].PopFront();

            // 划分为一个k页和一个i-k页的span
            // Span* kSpan = new Span;
            Span* kSpan = _spanPool.New();
            kSpan->_pageID = nSpan->_pageID;
            kSpan->_n = k;

            nSpan->_pageID += k;
            nSpan->_n -= k;

            _spanLists[nSpan->_n].PushFront(nSpan);

            _id2SpanMap[nSpan->_pageID] = nSpan;
            _id2SpanMap[nSpan->_pageID + nSpan->_n - 1] = nSpan;

            for (PageID i = 0; i < kSpan->_n; ++i)
            {
                _id2SpanMap[kSpan->_pageID + i] = kSpan;
            }

            return kSpan;
        }
    }

    // 3
    // k号桶和后面的桶中都没有span
    // 向系统申请PAGE_NUM-1=128页的内存
    void* ptr = SystemAlloc(PAGE_NUM - 1);

    // Span* bigSpan = new Span;
    Span* bigSpan = _spanPool.New();

    bigSpan->_pageID = ((PageID)ptr) >> PAGE_SHIFT;
    bigSpan->_n = PAGE_NUM - 1;

    _spanLists[PAGE_NUM - 1].PushFront(bigSpan);

    // 此时再次申请就会走2的逻辑了
    return NewSpan(k);
}

Span* PageCache::MapObj2Span(void* obj)
{
    // 通过块地址获取页号
    PageID id = (PageID)obj >> PAGE_SHIFT;
    std::unique_lock<std::mutex> lock(_pageMtx);
    // 通过map找到该页号对应的span，应该是一定能找到的，否则出错
    if (!_id2SpanMap.count(id))
    {
        assert(false);
        return nullptr;
    }
    return _id2SpanMap[id];
}

void PageCache::ReleaseSpanToPageCache(Span* span)
{
    if (span->_n >= PAGE_NUM)
    {
        // 获取想要释放的地址ptr
        void* ptr = (void*)(span->_pageID << PAGE_SHIFT);
        // 从ptr处释放span->_n页
        SystemFree(ptr, span->_n);
        // delete span;
        _spanPool.Delete(span); // 用定长内存池删空间

        return;
    }

    // else: span->_n < PAGE_NUM
    // 向左合并
    while (1)
    {
        // 拿到相邻的span
        PageID lid = span->_pageID - 1;

        // 没有相邻的span，停止合并
        if (!_id2SpanMap[lid])
        {
            break;
        }
        Span* lspan = _id2SpanMap[lid];

        // 相邻的span在cc中，停止合并
        if (lspan->_isUse)
        {
            break;
        }

        // 相邻span与当前span合并后超过128页，停止合并
        if (lspan->_n + span->_n > PAGE_NUM - 1)
        {
            break;
        }

        // 当前span与相邻span合并
        span->_pageID = lspan->_pageID;
        span->_n += lspan->_n;

        _spanLists[lspan->_n].Erase(lspan);  // 将相邻span从桶中删除
        // delete lspan;
        _spanPool.Delete(lspan);
    }

    // 向右合并
    while (1)
    {
        // 拿到相邻的span
        PageID rid = span->_pageID + span->_n;

        // 没有相邻的span，停止合并
        if (!_id2SpanMap[rid])
        {
            break;
        }
        Span* rspan = _id2SpanMap[rid];

        // 相邻的span在cc中，停止合并
        if (rspan->_isUse)
        {
            break;
        }

        // 相邻span与当前span合并后超过128页，停止合并
        if (rspan->_n + span->_n > PAGE_NUM - 1)
        {
            break;
        }

        // 当前span与相邻span合并
        // span->_pageID = rspan->_pageID;
        span->_n += rspan->_n;

        _spanLists[rspan->_n].Erase(rspan);  // 将相邻span从桶中删除
        // delete rspan;
        _spanPool.Delete(rspan);
    }

    // 合并完毕
    _spanLists[span->_n].PushFront(span);
    span->_isUse = false;  // 从cc回收到pc，置为false

    // 映射当前span的边缘页，方便后续继续合并
    _id2SpanMap[span->_pageID] = span;
    _id2SpanMap[span->_pageID + span->_n - 1] = span;
}