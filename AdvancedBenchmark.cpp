#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "ConcurrentAlloc.h"

// 使用 std::atomic<bool> 替代普通 bool（更安全）
std::atomic<bool> g_stop{false};

// 移除默认参数，显式提供两个参数
void Worker(size_t nallocs, size_t max_size)
{
    std::vector<void*> ptrs;
    ptrs.reserve(1000);

    // 使用 static 避免每个线程重复构造（可选优化）
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(1, max_size);

    while (!g_stop.load(std::memory_order_relaxed) && nallocs-- > 0)
    {
        size_t sz = dis(gen);
        void* p = ConcurrentAlloc(sz);
        if (p == nullptr)
        {
            std::cerr << "Allocation failed!\n";
            return;
        }
        ptrs.push_back(p);

        // 随机释放一部分（模拟真实使用）
        if (ptrs.size() > 1000 || (dis(gen) % 100) < 30)
        {
            for (void* p : ptrs) ConcurrentFree(p);
            ptrs.clear();
        }
    }

    // 清理剩余
    for (void* p : ptrs) ConcurrentFree(p);
}

void StressTest(size_t nthreads, size_t total_allocs_per_thread = 100000)
{
    std::cout << "\n=== Testing with " << nthreads << " threads ===\n";

    // 重置停止标志
    g_stop.store(false, std::memory_order_relaxed);

    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    try
    {
        for (size_t i = 0; i < nthreads; ++i)
        {
            threads.emplace_back(Worker, total_allocs_per_thread, 8192ULL);
        }
        for (auto& t : threads)
        {
            t.join();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
        exit(1);
    }

    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                  .count();

    double total_ops = static_cast<double>(nthreads) * total_allocs_per_thread;
    double throughput = total_ops / (ms / 1000.0) / 1e6;  // Mops/sec

    std::cout << "✅ Success! Threads=" << nthreads
              << ", Total ops=" << static_cast<size_t>(total_ops)
              << ", Time=" << ms << " ms"
              << ", Throughput=" << throughput << " Mops/sec\n";
}

int main()
{
    // 从小到大测试
    std::vector<size_t> thread_counts = {1,  2,   4,   8,   16,  32,
                                         64, 128, 256, 512, 1024};

    for (size_t n : thread_counts)
    {
        try
        {
            StressTest(n, 50000);  // 每个线程做 5 万次 alloc/free
        }
        catch (...)
        {
            std::cout << "💥 Crashed at " << n << " threads!\n";
            break;
        }
    }
    return 0;
}