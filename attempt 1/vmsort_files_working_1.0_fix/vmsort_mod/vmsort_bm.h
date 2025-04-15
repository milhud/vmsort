#ifndef VMSORT_BM_H_
#define VMSORT_BM_H_

#include <linux/bitmap.h>
#include <linux/types.h>      /* for uintptr_t */

/* ------------------------------------------------------------------ */
/* 1 024‑bit summary bitmap lives in one global array (16 × 64 bits).  */
/* This avoids changing the size/layout of struct vmsort_bm.          */
/* ------------------------------------------------------------------ */
static unsigned long vmsort_l1_global[16];

struct vmsort_bm {
        DECLARE_BITMAP(l0, 65536);   /* 2^16 bits  */
        u64 l1;                      /* holds POINTER to summary bitmap */
        /* iteration state */
        u16 iter_w;
        u16 iter_b;
};

/* ------------------------------------------------------------------ */
/* Initialise both bitmaps and stash the pointer in l1                */
/* ------------------------------------------------------------------ */
static inline void vmsort_bm_init(struct vmsort_bm *bm)
{
    bitmap_zero(bm->l0, 65536);
    bitmap_zero(vmsort_l1_global, 1024);
    bm->l1      = (u64)(uintptr_t)vmsort_l1_global;
    bm->iter_w  = 0;
    bm->iter_b  = 0;
}

/* ------------------------------------------------------------------ */
/* Set presence bit; also set summary bit safely (0‑1023 fits array)   */
/* ------------------------------------------------------------------ */
static inline void vmsort_bm_set(struct vmsort_bm *bm, u16 k)
{
    if (k >= 65536) return;

    u16 w = k >> 6;                 /* 0‑1023  */
    u16 b = k & 63;                 /* bit in that word */

    if (!test_and_set_bit(b, bm->l0 + w))
        set_bit(w, (unsigned long *)(uintptr_t)bm->l1);  /* summary */
}

/* ------------------------------------------------------------------ */
/* Iterate in sorted order — now skips empty words correctly          */
/* ------------------------------------------------------------------ */
static inline bool vmsort_bm_next(struct vmsort_bm *bm, u16 *out)
{
    for (; bm->iter_w < 1024; ++bm->iter_w) {
        if (!test_bit(bm->iter_w,
                      (unsigned long *)(uintptr_t)bm->l1))
            continue;

        while (bm->iter_b < 64) {
            if (test_bit(bm->iter_b, bm->l0 + bm->iter_w)) {
                *out = (bm->iter_w << 6) | bm->iter_b++;
                return false;
            }
            ++bm->iter_b;
        }
        bm->iter_b = 0;
    }
    return true;                     /* no more keys */
}

/* ------------------------------------------------------------------ */
static inline void vmsort_bm_reset_iter(struct vmsort_bm *bm)
{
    bm->iter_w = 0;
    bm->iter_b = 0;
}

#endif /* VMSORT_BM_H_ */

