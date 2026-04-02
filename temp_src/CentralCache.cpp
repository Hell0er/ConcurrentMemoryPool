#include "CentralCache.h"

#include "PageCache.h"

CentralCache CentralCache::_sInst;  // CentralCache的饿汉对象

Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
    // 先在cc中找一下有没有管理空间非空的span
    Span* it = list.Begin();
    while (it != list.End())
    {
        if (it->_freeList != nullptr)
        {
            return it;
        }
        it = it->_next;
    }

    // 解掉桶锁，让其它向该cc桶进行操作的线程能够拿到锁
    list.GetMtx().unlock();

    // 如果没有管理空间非空的span
    // 将size转成匹配的页数，以提供pc一个合适的span
    size_t k = sizeClass::NumMovePage(size);

    // 解决NewSpan()递归死锁：
    // 加锁
    PageCache::GetInstance()->getPageMtx().lock();
    // 调用NewSpan获取一个全新的span
    Span* span = PageCache::GetInstance()->NewSpan(k);
    span->_objSize = size;  // 记录span被拆分成块的大小
    span->_isUse = true;  // cc获取了pc中的span，置为正在使用
    // 解锁
    PageCache::GetInstance()->getPageMtx().unlock();

    char* start = (char*)(span->_pageID << PAGE_SHIFT);
    char* end = (char*)(start + (span->_n << PAGE_SHIFT));

    // 开始划分span管理的空间
    span->_freeList = start;  // 管理的空间放在span->_freeList中
    void* tail = start;
    start += size;

    // 连接各个块
    while (start < end)
    {
        ObjNext(tail) = start;
        tail = start;
        start += size;
    }
    ObjNext(tail) = nullptr;  // 最后一个位置置空

    // 在span挂好前把锁加回去
    list.GetMtx().lock();
    // 切好span后将span挂到cc对应下标的桶中
    list.PushFront(span);

    return span;
}

// cc从自己的_spanLists中为tc提供tc所需要的块空间
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum,
                                   size_t size)
{
    // 获取到size对应于哪个SpanList
    size_t index = sizeClass::Index(size);

    // 由于cc是共有的，可能出现多个线程同时访问同一个SpanList的情况，需要上锁
    _spanLists[index].GetMtx().lock();

    // 获取一个管理非空空间的span
    Span* span = GetOneSpan(_spanLists[index], size);
    assert(span != nullptr);
    assert(span->_freeList != nullptr);

    start = end = span->_freeList;
    --batchNum;
    size_t actualNum = 1;
    while (batchNum-- && ObjNext(end) != nullptr)
    {
        end = ObjNext(end);
        ++actualNum;
    }
    // 将[start,end]提供给tc后，_freeList指向end+1
    span->_freeList = ObjNext(end);
    span->use_count += actualNum;  // cc的该span为tc分配了多少小块

    // end的下一个节点指向nullptr
    ObjNext(end) = nullptr;

    _spanLists[index].GetMtx().unlock();

    return actualNum;
}

void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
    // 先获取size对应的桶下标
    size_t index = sizeClass::Index(size);
    _spanLists[index].GetMtx().lock();
    // 对回收空间逐块操作
    while (start)
    {
        void* next = ObjNext(start);
        Span* span = PageCache::GetInstance()->MapObj2Span(start);
        ObjNext(start) = span->_freeList;
        span->_freeList = start;
        --span->use_count;  // 减去回收的块

        // 该span的块空间（占若干页）全部回收，将这个span还给pc
        if (span->use_count == 0)
        {
            // 将该span从cc的桶中删掉
            _spanLists[index].Erase(span);
            span->_freeList = nullptr;
            span->_next = span->_prev = nullptr;

            _spanLists[index].GetMtx().unlock();
            PageCache::GetInstance()->getPageMtx().lock();
            PageCache::GetInstance()->ReleaseSpanToPageCache(span);
            PageCache::GetInstance()->getPageMtx().unlock();
            _spanLists[index].GetMtx().lock();
        }

        start = next;
    }
    _spanLists[index].GetMtx().unlock();
}