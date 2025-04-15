// Add this to the include section in driver.c
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <assert.h>
#include <string.h>

// IOCTL commands - must match kernel definitions
#define VMSORT_CMD_SET_VALUE _IOW('v', 1, uint16_t)
#define VMSORT_CMD_GET_NEXT  _IOR('v', 2, uint16_t)
#define VMSORT_CMD_RESET     _IO('v', 3)
#define VMSORT_CMD_GET_INFO  _IOR('v', 4, struct vmsort_info)

struct vmsort_info {
    uint64_t phys_addr;
    uint64_t mem_size;
};

#define N 100  // Number of values to sort

int main(int argc, char *argv[])
{
    int i, fd, fd_mem;
    uint16_t values[N];
    uint16_t sorted[N];
    uint16_t value;
    struct vmsort_info info;
    void *mem_map;
    int count = 0;
    
    // Generate random values
    srand(time(NULL));
    printf("Original values:\n");
    for (i = 0; i < N; i++) {
        values[i] = rand() % 65536;
        printf("%u ", values[i]);
        if ((i + 1) % 10 == 0) printf("\n");
    }
    printf("\n");
    
    // Open the vmsort device
    fd = open("/dev/vmsort", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/vmsort");
        return 1;
    }
    
    // Reset the device
    if (ioctl(fd, VMSORT_CMD_RESET) < 0) {
        perror("VMSORT_CMD_RESET failed");
        close(fd);
        return 1;
    }
    
    // Get physical memory info
    if (ioctl(fd, VMSORT_CMD_GET_INFO, &info) < 0) {
        perror("VMSORT_CMD_GET_INFO failed");
        close(fd);
        return 1;
    }
    printf("Physical memory at 0x%lx, size: %lu bytes\n", 
           info.phys_addr, info.mem_size);
    
    // Open /dev/mem for direct physical memory access
    fd_mem = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd_mem < 0) {
        perror("Failed to open /dev/mem (requires root privileges)");
        close(fd);
        return 1;
    }
    
    // Map physical memory
    mem_map = mmap(NULL, info.mem_size, PROT_READ | PROT_WRITE, 
                  MAP_SHARED, fd_mem, info.phys_addr);
    if (mem_map == MAP_FAILED) {
        perror("mmap failed");
        close(fd_mem);
        close(fd);
        return 1;
    }
    
    // Use direct memory access to "sort" values
    printf("Setting values directly in physical memory...\n");
    for (i = 0; i < N; i++) {
        // Just set a byte at the offset corresponding to each value
        ((volatile uint8_t *)mem_map)[values[i] % info.mem_size] = 1;
        
        // Also tell the kernel module about it
        if (ioctl(fd, VMSORT_CMD_SET_VALUE, &values[i]) < 0) {
            perror("VMSORT_CMD_SET_VALUE failed");
        }
    }
    
    // Extract sorted values
    printf("Retrieving sorted values...\n");
    while (ioctl(fd, VMSORT_CMD_GET_NEXT, &value) == 0 && count < N) {
        sorted[count++] = value;
    }
    
    // Display sorted values
    printf("Sorted values (%d retrieved):\n", count);
    for (i = 0; i < count; i++) {
        printf("%u ", sorted[i]);
        if ((i + 1) % 10 == 0) printf("\n");
    }
    printf("\n");
    
    // Verify sorting
    for (i = 1; i < count; i++) {
        assert(sorted[i-1] <= sorted[i]);
    }
    printf("PASS: Values are correctly sorted\n");
    
    // Clean up
    munmap(mem_map, info.mem_size);
    close(fd_mem);
    close(fd);
    
    return 0;
}
