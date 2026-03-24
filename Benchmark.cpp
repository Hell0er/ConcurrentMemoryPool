/*这里测试的是让多线程申请ntimes*rounds次，比较malloc和刚写完的ConcurrentAlloc的效率*/

/*比较的时候分两种情况，
一种是申请ntimes*rounds次同一个块大小的空间，
一种是申请ntimes*rounds次不同的块大小的空间*/

#include <atomic>
#include <vector>

#include "ConcurrentAlloc.h"

// ntimes 一轮申请和释放内存的次数
// rounds 轮次
// nwors 创建多少个线程
void BenchmarkMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
    std::vector<std::thread> vthread(nworks);

    for (size_t k = 0; k < nworks; ++k)
    {
        vthread[k] = std::thread(
            [&, k]()
            {
                std::vector<void*> v;
                v.reserve(ntimes);

                for (size_t j = 0; j < rounds; ++j)
                {
                    for (size_t i = 0; i < ntimes; i++)
                    {
                        // 每一次申请同一个桶中的块
                        // v.push_back(malloc(16));
                        // 每一次申请不同桶中的块
                        v.push_back(malloc((16 + i) % 8192 + 1));
                    }

                    for (size_t i = 0; i < ntimes; i++)
                    {
                        free(v[i]);
                    }
                    v.clear();
                }
            });
    }

    auto overall_start = std::chrono::high_resolution_clock::now();
    for (auto& t : vthread)
    {
        t.join();
    }
    auto overall_end = std::chrono::high_resolution_clock::now();
    auto total_wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             overall_end - overall_start)
                             .count();

    // 打印总墙上时间
    printf("BenchmarkAlloc: %lu threads, %lu rounds, %lu allocs/round\n",
           nworks, rounds, ntimes);
    printf("Total wall-clock time: %lu ms\n", total_wall_ms);
}

// ntimes 单轮次申请释放次数
// nworks 线程数
// rounds 轮次
void BenchmarkConcurrentMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
    std::vector<std::thread> vthread(nworks);

    for (size_t k = 0; k < nworks; ++k)
    {
        vthread[k] = std::thread(
            [&]()
            {
                std::vector<void*> v;
                v.reserve(ntimes);

                for (size_t j = 0; j < rounds; ++j)
                {
                    for (size_t i = 0; i < ntimes; i++)
                    {
                        // v.push_back(ConcurrentAlloc(16));
                        v.push_back(ConcurrentAlloc((16 + i) % 8192 + 1));
                    }

                    for (size_t i = 0; i < ntimes; i++)
                    {
                        ConcurrentFree(v[i]);
                    }
                    v.clear();
                }
            });
    }

    auto overall_start = std::chrono::high_resolution_clock::now();
    for (auto& t : vthread)
    {
        t.join();
    }
    auto overall_end = std::chrono::high_resolution_clock::now();
    auto total_wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             overall_end - overall_start)
                             .count();

    // 打印总墙上时间
    printf("ConcurrentAlloc: %lu threads, %lu rounds, %lu allocs/round\n",
           nworks, rounds, ntimes);
    printf("Total wall-clock time: %lu ms\n", total_wall_ms);
}

int main()
{
    size_t n = 10000;
    cout << "=========================================" << endl;
    // 这里表示4个线程，每个线程申请10万次，总共申请40万次
    BenchmarkConcurrentMalloc(n, 4, 10);
    cout << endl << endl;

    // 这里表示4个线程，每个线程申请10万次，总共申请40万次
    BenchmarkMalloc(n, 4, 10);
    cout << "=========================================" << endl;

    return 0;
}
