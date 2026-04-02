#include "ThreadCache.h"

#include "CentralCache.h"

std::mutex write_mtx;

void* ThreadCache::Allocate(size_t size)
{
    assert(size <= MAX_BYTES);  // tc单次申请不得超过256KB的空间

    size_t alignSize = sizeClass::RoundUp(size);  // size对齐后的字节数
    size_t index =
        sizeClass::Index(size);  // size对应于哈希表中的哪个桶（自由链表）

    // 自由链表不为空，则可以直接从自由链表中获取空间
    if (!_freeLists[index].Empty())
    {
        return _freeLists[index].Pop();
    }
    else  // 否则tc向CentralCache申请空间
    {
        return FetchFromCentralCache(index, alignSize);
    }
}

void ThreadCache::Deallocate(void* obj, size_t size)
{
    assert(obj != nullptr);
    assert(size <= MAX_BYTES);

    size_t index = sizeClass::Index(size);  // size对应于哪个自由链表
    _freeLists[index].Push(obj);            // 用自由链表回收空间

    if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
    {
        ListTooLong(_freeLists[index], size);
    }
}

void* ThreadCache::FetchFromCentralCache(size_t index, size_t alignSize)
{
    // 通过MaxSize和NumMoveSize来控制当前给tc提供多少块alignSize大小的空间
    size_t batchNum = std::min(_freeLists[index].MaxSize(),
                               sizeClass::NumMoveSize(alignSize));

    /*
    MaxSize表示index位置的自由链表单次申请未到上限时，能够申请的最大块空间
    NumMoveSize表示tc单次向cc申请alignSize大小的空间块的最多数目
    二者取小值，得到本次要给tc提供多少块alignSize大小的空间
    */

    // 没有达到上限，则下次申请时可以多申请一块
    if (batchNum == _freeLists[index].MaxSize())
    {
        ++_freeLists[index].MaxSize();  // 慢开始反馈的核心
    }

    void *start = nullptr, *end = nullptr;
    // 取出cc中对应spanList的[start,end]
    // end-start = 8B * (actualNum-1) = 0x8 * (actualNum-1)
    size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(
        start, end, batchNum, alignSize);

    assert(actualNum >= 1);

    // 如果actualNum==1，直接将start地址返回给线程
    if (actualNum == 1)
    {
        assert(start == end);
        return start;
    }
    else  // 否则将[start+1,end]插入到tc的自由链表中，返回start地址给线程
    {
        // 第一块给了线程，所以tc获得的块数目为actualNum-1
        _freeLists[index].PushRange(ObjNext(start), end, actualNum - 1);
        return start;
    }
}

void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
    void* start = nullptr, * end = nullptr;

    // 获取准备归还的list.MaxSize()块空间
    list.PopRange(start, end, list.MaxSize());

    // 向cc归还空间
    CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}