#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/bitmap.h>

#define DEV "vmsort"
#define PHYS_MEM_SIZE (16UL << 20)  // 16 MB physical memory buffer

// Bitmap structure for tracking accessed memory locations
struct vmsort_bitmap {
    DECLARE_BITMAP(bits, 65536);  // Bitmap for 2^16 possible values
    u16 iter_pos;                 // Current iteration position
};

// Device-specific data structure
struct vmsort_dev {
    struct vmsort_bitmap bitmap;
    void *virt_addr;              // Kernel virtual address of our buffer
    phys_addr_t phys_addr;        // Physical address of our buffer
    size_t mem_size;              // Size of the allocated memory
    struct mutex lock;            // Synchronization lock
};

// IOCTL commands
#define VMSORT_CMD_SET_VALUE _IOW('v', 1, u16)
#define VMSORT_CMD_GET_NEXT  _IOR('v', 2, u16)
#define VMSORT_CMD_RESET     _IO('v', 3)
#define VMSORT_CMD_GET_INFO  _IOR('v', 4, struct vmsort_info)

// Info structure returned to userspace
struct vmsort_info {
    u64 phys_addr;
    u64 mem_size;
};

static struct vmsort_dev *vmsort_device;

static int vmsort_open(struct inode *inode, struct file *file)
{
    file->private_data = vmsort_device;
    return 0;
}

static int vmsort_release(struct inode *inode, struct file *file)
{
    return 0;
}

static void vmsort_set_value(struct vmsort_dev *dev, u16 value)
{
    if (value < 65536) {
        mutex_lock(&dev->lock);
        set_bit(value, dev->bitmap.bits);
        mutex_unlock(&dev->lock);
    }
}

static bool vmsort_get_next(struct vmsort_dev *dev, u16 *value)
{
    bool found = false;
    
    mutex_lock(&dev->lock);
    while (dev->bitmap.iter_pos < 65536) {
        if (test_bit(dev->bitmap.iter_pos, dev->bitmap.bits)) {
            *value = dev->bitmap.iter_pos++;
            found = true;
            break;
        }
        dev->bitmap.iter_pos++;
    }
    mutex_unlock(&dev->lock);
    
    return found;
}

static void vmsort_reset(struct vmsort_dev *dev)
{
    mutex_lock(&dev->lock);
    bitmap_zero(dev->bitmap.bits, 65536);
    dev->bitmap.iter_pos = 0;
    mutex_unlock(&dev->lock);
}

static long vmsort_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct vmsort_dev *dev = file->private_data;
    u16 value;
    struct vmsort_info info;
    
    switch (cmd) {
    case VMSORT_CMD_SET_VALUE:
        if (copy_from_user(&value, (void __user *)arg, sizeof(value)))
            return -EFAULT;
        vmsort_set_value(dev, value);
        return 0;
        
    case VMSORT_CMD_GET_NEXT:
        if (!vmsort_get_next(dev, &value))
            return -ENODATA;  // No more data
        if (copy_to_user((void __user *)arg, &value, sizeof(value)))
            return -EFAULT;
        return 0;
        
    case VMSORT_CMD_RESET:
        vmsort_reset(dev);
        return 0;
        
    case VMSORT_CMD_GET_INFO:
        info.phys_addr = dev->phys_addr;
        info.mem_size = dev->mem_size;
        if (copy_to_user((void __user *)arg, &info, sizeof(info)))
            return -EFAULT;
        return 0;
        
    default:
        return -ENOTTY;  // Invalid command
    }
}

static const struct file_operations vmsort_fops = {
    .owner = THIS_MODULE,
    .open = vmsort_open,
    .release = vmsort_release,
    .unlocked_ioctl = vmsort_ioctl,
};

static int __init vmsort_init(void)
{
    int major;
    struct vmsort_dev *dev;
    
    // Allocate device structure
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;
    
    // Initialize mutex
    mutex_init(&dev->lock);
    
    // Allocate contiguous physical memory
    dev->mem_size = PHYS_MEM_SIZE;
    dev->virt_addr = kmalloc(dev->mem_size, GFP_KERNEL | GFP_DMA);
    if (!dev->virt_addr) {
        kfree(dev);
        return -ENOMEM;
    }
    
    // Get physical address
    dev->phys_addr = virt_to_phys(dev->virt_addr);
    
    // Initialize the bitmap
    bitmap_zero(dev->bitmap.bits, 65536);
    dev->bitmap.iter_pos = 0;
    
    // Store the device globally
    vmsort_device = dev;
    
    // Register character device
    major = register_chrdev(0, DEV, &vmsort_fops);
    if (major < 0) {
        kfree(dev->virt_addr);
        kfree(dev);
        return major;
    }
    
    pr_info("vmsort: registered /dev/%s (major %d)\n", DEV, major);
    pr_info("vmsort: physical memory at 0x%llx, size: %zu bytes\n", 
           (unsigned long long)dev->phys_addr, dev->mem_size);
    
    return 0;
}

static void __exit vmsort_exit(void)
{
    if (vmsort_device) {
        if (vmsort_device->virt_addr)
            kfree(vmsort_device->virt_addr);
        kfree(vmsort_device);
    }
    unregister_chrdev(0, DEV);
    pr_info("vmsort: unregistered\n");
}

MODULE_LICENSE("GPL");
module_init(vmsort_init);
module_exit(vmsort_exit);
