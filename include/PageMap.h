#include <cstring>

#include "Common.h"
/*
Three-level radix tree for 36-bit page IDs
(48-bit virtual address, 4KB pages)
*/
template <int BITS>
class TCMalloc_PageMap3
{
   private:
    // Split 36 bits into three 12-bit levels
    static const int L0_BITS = 12;
    static const int L1_BITS = 12;
    static const int L2_BITS = BITS - L0_BITS - L1_BITS;  // should be 12

    static_assert(
        BITS == 36,
        "PageMap3 is designed for 36-bit page IDs (48-bit VA, 4KB pages)");

    static const int L0_LENGTH = 1 << L0_BITS;  // 4096
    static const int L1_LENGTH = 1 << L1_BITS;  // 4096
    static const int L2_LENGTH = 1 << L2_BITS;  // 4096

    // Leaf node: holds actual values (e.g., Span*)
    struct Leaf
    {
        void* values[L2_LENGTH];
    };

    // Mid-level node: points to leaves
    struct Mid
    {
        Leaf* ptrs[L1_LENGTH];
    };

    // Root: points to mid-level nodes
    Mid* root_[L0_LENGTH];

   public:
    typedef uintptr_t Number;

    explicit TCMalloc_PageMap3()
    {
        memset(root_, 0, sizeof(root_));
        // Optional: pre-allocate if you want (usually not needed for sparse
        // usage) PreallocateMoreMemory();
    }

    void* get(Number k) const
    {
        if ((k >> BITS) != 0)
        {
            return nullptr;  // Out of range
        }

        const Number i0 = (k >> (L1_BITS + L2_BITS)) & (L0_LENGTH - 1);
        const Number i1 = (k >> L2_BITS) & (L1_LENGTH - 1);
        const Number i2 = k & (L2_LENGTH - 1);

        if (root_[i0] == nullptr) return nullptr;
        if (root_[i0]->ptrs[i1] == nullptr) return nullptr;
        return root_[i0]->ptrs[i1]->values[i2];
    }

    void set(Number k, void* v)
    {
        const Number i0 = (k >> (L1_BITS + L2_BITS)) & (L0_LENGTH - 1);
        const Number i1 = (k >> L2_BITS) & (L1_LENGTH - 1);
        const Number i2 = k & (L2_LENGTH - 1);

        assert(i0 < L0_LENGTH);
        assert(root_[i0] != nullptr && root_[i0]->ptrs[i1] != nullptr);

        root_[i0]->ptrs[i1]->values[i2] = v;
    }

    // Ensure the path for [start, start + n) is allocated
    bool Ensure(Number start, size_t n)
    {
        for (Number key = start; key <= start + n - 1;)
        {
            const Number i0 = (key >> (L1_BITS + L2_BITS)) & (L0_LENGTH - 1);
            const Number i1 = (key >> L2_BITS) & (L1_LENGTH - 1);

            if (i0 >= L0_LENGTH) return false;

            // Allocate L0 -> Mid
            if (root_[i0] == nullptr)
            {
                static ObjectPool<Mid> midPool;
                Mid* mid = (Mid*)midPool.New();
                memset(mid, 0, sizeof(*mid));
                root_[i0] = mid;
            }

            // Allocate Mid -> Leaf
            if (root_[i0]->ptrs[i1] == nullptr)
            {
                static ObjectPool<Leaf> leafPool;
                Leaf* leaf = (Leaf*)leafPool.New();
                memset(leaf, 0, sizeof(*leaf));
                root_[i0]->ptrs[i1] = leaf;
            }

            // Move to next L1 block boundary
            key = ((key >> L2_BITS) + 1) << L2_BITS;
        }
        return true;
    }

    // Optional: pre-allocate entire space (not recommended for large address
    // spaces)
    void PreallocateMoreMemory() { Ensure(0, 1ULL << BITS); }
};