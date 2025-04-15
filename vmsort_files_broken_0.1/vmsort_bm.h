#ifndef _VMSORT_BM_H_
#define _VMSORT_BM_H_

#include <linux/bitmap.h>
struct vmsort_bm {
	DECLARE_BITMAP(l0, 65536); // 2^16 bits
	u64 l1;                    // one summary word
};

static inline void vmsort_bm_set(struct vmsort_bm *bm, u16 k)
{
	u16 w = k >> 6, b = k & 63;
	if (!test_and_set_bit(b, bm->l0 + w))
		set_bit(w, &bm->l1);
}

static inline bool vmsort_bm_next(struct vmsort_bm *bm, u16 *out)
{
	static u16 w = 0, b = 0;
	for (; w < 1024; ++w) {
		if (!test_bit(w, &bm->l1)) continue;
		while (b < 64) {
			if (test_bit(b, bm->l0 + w)) {
				*out = (w << 6) | b++;
				return false;
			}
			++b;
		}
		b = 0;
	}
	return true;
}
#endif
