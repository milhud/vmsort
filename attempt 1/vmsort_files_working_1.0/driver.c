#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/ioctl.h>

#define TOTAL_WIN  (256UL << 20)   /* module's fixed window size */
#define N          100             /* we'll touch only 100 pages  */
#define STRIDE     4096            /* 4 KiB per key               */

struct vmsort_iter { uint64_t ptr; uint32_t cap; uint32_t out; };
#define VMSORT_IOCTL _IOWR('v', 1, struct vmsort_iter)

int main(void)
{
    int fd = open("/dev/vmsort", O_RDWR);
    if (fd < 0) { perror("open /dev/vmsort"); return 1; }
    
    void *base = mmap(NULL, TOTAL_WIN, PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) { perror("mmap"); return 1; }
    
    /* touch only the first N pages */
    for (uint32_t i = 0; i < N; ++i)
        ((volatile char *)base)[i * STRIDE] = 1;
    
    uint16_t out[N];
    struct vmsort_iter it = { .ptr = (uint64_t)out, .cap = N };
    
    if (ioctl(fd, VMSORT_IOCTL, &it)) { perror("ioctl"); return 1; }
    
    printf("Retrieved %u keys\n", it.out);
    for (uint32_t i = 0; i < it.out; ++i) {
        printf("%u ", out[i]);
        if ((i+1) % 10 == 0) printf("\n");
    }
    printf("\n");
    
    for (uint32_t i = 1; i < it.out; ++i)
        assert(out[i-1] <= out[i]);
    
    printf("PASS: %u keys sorted\n", it.out);
    
    munmap(base, TOTAL_WIN);
    close(fd);
    return 0;
}
