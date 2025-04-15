#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "vmsort_bm.h"
#include <linux/pagemap.h>
#include <linux/gfp.h>
#include <linux/ioctl.h>

#define DEV "vmsort"
#define WIN (256UL << 20) // 256 MiB window
struct vmsort_iter { u64 ptr; u32 cap; u32 out; };
#define VMSORT_IOCTL _IOWR('v', 1, struct vmsort_iter)

static struct vmsort_bm bm;
static unsigned long base;
static DEFINE_MUTEX(vmsort_mutex);  // Add a mutex for synchronization

static vm_fault_t fault(struct vm_fault *vmf)
{
    struct page *p;
    u16 k;
    
    k = (vmf->address - base) >> PAGE_SHIFT;
    
    // Safely record the access in our bitmap
    mutex_lock(&vmsort_mutex);
    vmsort_bm_set(&bm, k);
    mutex_unlock(&vmsort_mutex);
    
    p = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (!p) 
        return VM_FAULT_OOM;
    
    get_page(p);          /* extra pin for the VMA */
    SetPageDirty(p);      /* keep it writable      */
    vmf->page = p;
    
    return 0;  // Changed to 0 from VM_FAULT_NOPAGE for newer kernels
}

static const struct vm_operations_struct ops = {
    .fault = fault,
};

static int mmap_f(struct file *f, struct vm_area_struct *vma)
{
    if (vma->vm_end - vma->vm_start != WIN) 
        return -EINVAL;
    
    mutex_lock(&vmsort_mutex);
    vmsort_bm_init(&bm);  // Initialize the bitmap
    vma->vm_ops = &ops;
    base = vma->vm_start;
    mutex_unlock(&vmsort_mutex);
    
    return 0;
}

static long ioctl_f(struct file *f, unsigned int c, unsigned long a)
{
    struct vmsort_iter it;
    u16 *tmp;
    u32 n = 0;
    u16 k;
    
    if (c != VMSORT_IOCTL) 
        return -ENOTTY;
    
    if (copy_from_user(&it, (void __user *)a, sizeof(it))) 
        return -EFAULT;
    
    tmp = kmalloc_array(it.cap, sizeof(u16), GFP_KERNEL);
    if (!tmp) 
        return -ENOMEM;
    
    mutex_lock(&vmsort_mutex);
    vmsort_bm_reset_iter(&bm);  // Reset iteration state
    while (n < it.cap && !vmsort_bm_next(&bm, &k)) 
        tmp[n++] = k;
    mutex_unlock(&vmsort_mutex);
    
    if (copy_to_user((void __user *)(uintptr_t)it.ptr, tmp, n * sizeof(u16))) 
        n = 0;
    
    kfree(tmp);
    it.out = n;
    
    if (copy_to_user((void __user *)a, &it, sizeof(it)))
        return -EFAULT;
    
    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .mmap = mmap_f,
    .unlocked_ioctl = ioctl_f,
};

static int __init init(void) {
    int m = register_chrdev(0, DEV, &fops);
    if (m < 0) 
        return m;
    
    pr_info("vmsort: registered /dev/%s (major %d)\n", DEV, m);
    return 0;
}

static void __exit exit_(void) {
    unregister_chrdev(0, DEV);
}

MODULE_LICENSE("GPL");
module_init(init);
module_exit(exit_);
