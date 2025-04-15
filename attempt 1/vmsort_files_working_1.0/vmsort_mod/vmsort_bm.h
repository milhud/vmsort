#ifndef VMSORT_BM_H_
#define VMSORT_BM_H_
#include <linux/bitmap.h>
struct vmsort_bm {
        DECLARE_BITMAP(l0, 65536); // 2^16 bits
        u64 l1;                    // one summary word
        // State for iteration
        u16 iter_w;
        u16 iter_b;
};

static inline void vmsort_bm_init(struct vmsort_bm *bm)
{
    bitmap_zero(bm->l0, 65536);
    bm->l1 = 0;
    bm->iter_w = 0;
    bm->iter_b = 0;
}

static inline void vmsort_bm_set(struct vmsort_bm *bm, u16 k)
{
    if (k >= 65536) return;
    u16 w = k >> 6, b = k & 63;
    if (!test_and_set_bit(b, bm->l0 + w))
        set_bit(w, (unsigned long *)&bm->l1);
}

static inline bool vmsort_bm_next(struct vmsort_bm *bm, u16 *out)
{
    // Use struct members instead of static variables
    for (; bm->iter_w < 1024; ++bm->iter_w) {
        if (!test_bit(bm->iter_w, (unsigned long *)&bm->l1)) continue;
        while (bm->iter_b < 64) {
            if (test_bit(bm->iter_b, bm->l0 + bm->iter_w)) {
                *out = (bm->iter_w << 6) | bm->iter_b++;
                return false;
            }
            ++bm->iter_b;
        }
        bm->iter_b = 0;
    }
    return true;
}

static inline void vmsort_bm_reset_iter(struct vmsort_bm *bm)
{
    bm->iter_w = 0;
    bm->iter_b = 0;
}

#endif /* VMSORT_BM_H_ */
