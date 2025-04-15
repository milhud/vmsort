#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "vmsort_bm.h"

#define DEV "vmsort"
#define WIN (256UL << 20) // 256 MiB window
struct vmsort_iter { u64 ptr; u32 cap; u32 out; };
#define IOCTL _IOWR('v', 1, struct vmsort_iter)

static struct vmsort_bm bm;
static unsigned long base;

static vm_fault_t fault(struct vm_fault *vmf)
{
	u16 k = (vmf->address - base) >> PAGE_SHIFT;
	vmsort_bm_set(&bm, k);
	get_page(ZERO_PAGE(0));
	vmf->page = ZERO_PAGE(0);
	return VM_FAULT_NOPAGE;
}

static const struct vm_operations_struct ops = {
	.fault = fault,
};

static int mmap_f(struct file *f, struct vm_area_struct *v)
{
	if (v->vm_end - v->vm_start != WIN) return -EINVAL;
	v->vm_ops = &ops;
	base = v->vm_start;
	return 0;
}

static long ioctl_f(struct file *f, unsigned int c, unsigned long a)
{
	if (c != IOCTL) return -ENOTTY;
	struct vmsort_iter it;
	if (copy_from_user(&it, (void __user *)a, sizeof(it))) return -EFAULT;
	u16 *tmp = kmalloc_array(it.cap, sizeof(u16), GFP_KERNEL);
	if (!tmp) return -ENOMEM;
	u32 n = 0; u16 k;
	while (n < it.cap && !vmsort_bm_next(&bm, &k)) tmp[n++] = k;
	if (copy_to_user((void __user *)it.ptr, tmp, n * sizeof(u16))) n = 0;
	kfree(tmp); it.out = n;
	copy_to_user((void __user *)a, &it, sizeof(it));
	return 0;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.mmap = mmap_f,
	.unlocked_ioctl = ioctl_f,
};

static int __init init(void) {
	int m = register_chrdev(0, DEV, &fops);
	if (m < 0) return m;
	pr_info("vmsort: registered /dev/%s (major %d)\n", DEV, m);
	return 0;
}

static void __exit exit_(void) {
	unregister_chrdev(0, DEV);
}

MODULE_LICENSE("GPL");
module_init(init);
module_exit(exit_);
