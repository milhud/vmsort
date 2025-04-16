// ======================================================================
// vmsort.c  —  high‑speed virtual‑memory counting sort  (GPL‑2.0‑only)
// ======================================================================
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include "vmsort_bm.h"

#define DEV            "vmsort"
#define WIN            (256UL << 20)     /* 256 MiB user window        */
#define CHUNKS         128               /* 128 × 2 MiB  = 256 MiB     */
#define CHUNK_PAGES    512               /* pages per chunk (order‑9)  */

struct vmsort_iter { u64 ptr; u32 cap; u32 out; };
#define VMSORT_IOCTL _IOWR('v', 1, struct vmsort_iter)

/* ------------------------------------------------------------------ */
/* Global state                                                       */
/* ------------------------------------------------------------------ */
static struct page   **chunk_pool;        /* [128] backing pages       */
static struct vmsort_bm bitmap;           /* whole‑window bitmap       */
static unsigned long   win_base;
static int             major;
static DEFINE_MUTEX(iter_lock);

/* ------------------------------------------------------------------ */
static vm_fault_t vmsort_fault(struct vm_fault *vmf)
{
        unsigned off   = (vmf->address - win_base) >> PAGE_SHIFT; /* 0‑65535 */
        unsigned chunk = off / CHUNK_PAGES;

        /* mark page present (lock‑free) */
        vmsort_bm_set(&bitmap, (u16)off);

        /* lazily allocate backing page if chunk empty */
        if (unlikely(!chunk_pool[chunk])) {
                struct page *p = alloc_pages(GFP_KERNEL | __GFP_ZERO |
                                             __GFP_NORETRY | __GFP_NOWARN,
                                             9);          /* try 2 MiB   */
                if (!p)                                     /* fallback   */
                        p = alloc_page(GFP_KERNEL | __GFP_ZERO);
                if (!p) return VM_FAULT_OOM;
                chunk_pool[chunk] = p;
        }

        vmf->page = chunk_pool[chunk] + (off & (CHUNK_PAGES - 1));
        get_page(vmf->page);
        SetPageDirty(vmf->page);
        return 0;                       /* VM_FAULT_NOPAGE (==0)       */
}

static const struct vm_operations_struct vm_ops = {
        .fault = vmsort_fault,
};

/* ------------------------------------------------------------------ */
static int vmsort_mmap(struct file *f, struct vm_area_struct *vma)
{
        if (vma->vm_end - vma->vm_start != WIN)
                return -EINVAL;

        vmsort_bm_init(&bitmap);
        memset(chunk_pool, 0, sizeof(struct page *) * CHUNKS);

        win_base    = vma->vm_start;
        vma->vm_ops = &vm_ops;
        return 0;
}

/* ------------------------------------------------------------------ */
/* zero‑copy, buffered iterator  (O(n))                               */
/* ------------------------------------------------------------------ */
static long vmsort_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
        if (cmd != VMSORT_IOCTL) return -ENOTTY;

        struct vmsort_iter it;
        if (copy_from_user(&it, (void __user *)arg, sizeof(it)))
                return -EFAULT;

        u32 out = 0;        /* keys emitted so far */
        u16 buf[1024];      /* batch buffer        */
        u32 fill = 0;       /* number in buffer    */
        u16 key;

        mutex_lock(&iter_lock);
        vmsort_bm_reset_iter(&bitmap);

        while (out < it.cap && !vmsort_bm_next(&bitmap, &key)) {
                buf[fill++] = key;
                if (fill == 1024) {
                        if (copy_to_user((u16 __user *)(uintptr_t)it.ptr + out,
                                         buf, 1024 * sizeof(u16)))
                                { mutex_unlock(&iter_lock); return -EFAULT; }
                        out  += 1024;
                        fill  = 0;
                }
        }
        if (fill &&
            copy_to_user((u16 __user *)(uintptr_t)it.ptr + out,
                         buf, fill * sizeof(u16)))
                { mutex_unlock(&iter_lock); return -EFAULT; }
        out += fill;
        mutex_unlock(&iter_lock);

        it.out = out;
        return copy_to_user((void __user *)arg, &it, sizeof(it)) ? -EFAULT : 0;
}

static const struct file_operations fops = {
        .owner          = THIS_MODULE,
        .mmap           = vmsort_mmap,
        .unlocked_ioctl = vmsort_ioctl,
};

/* ------------------------------------------------------------------ */
/* Module init / exit                                                 */
/* ------------------------------------------------------------------ */
static int __init vmsort_init(void)
{
        major = register_chrdev(0, DEV, &fops);
        if (major < 0) return major;

        chunk_pool = kcalloc(CHUNKS, sizeof(struct page *), GFP_KERNEL);
        if (!chunk_pool) {
                unregister_chrdev(major, DEV); return -ENOMEM;
        }
        pr_info("vmsort: /dev/%s (major %d) ready\n", DEV, major);
        return 0;
}

static void __exit vmsort_exit(void)
{
        int i;
        for (i = 0; i < CHUNKS; ++i)
                if (chunk_pool[i])
                        compound_order(chunk_pool[i]) == 9 ?
                                __free_pages(chunk_pool[i], 9) :
                                __free_page(chunk_pool[i]);
        kfree(chunk_pool);
        unregister_chrdev(major, DEV);
        pr_info("vmsort: unloaded\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Optimised example");
MODULE_DESCRIPTION("Virtual‑memory counting sort, batched & ffs‑scanned");
module_init(vmsort_init);
module_exit(vmsort_exit);

