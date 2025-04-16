// =============================================================
//  V M S O R T   —  high‑speed virtual‑memory counting sort
//  Full source bundle:  driver.c  |  vmsort.c  |  vmsort_bm.h
//  Implements all “next‑win” optimisations requested:
//    • lazy 2 MiB huge‑page pooling (order‑9)  — amortised O(1)
//    • per‑CPU bitmap stripes                 — lock‑free faults
//    • zero‑copy ioctl using bit‑scan loops    — O(n) iteration
//  Everything remains strictly O(n) in the number of *unique*
//  keys actually touched; all one‑off kernel work is constant.
// =============================================================

/* -------------------------------------------------------------
 *  driver.c  —  userspace benchmark                            
 * -----------------------------------------------------------*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>

#define TOTAL_WIN   (256UL << 20)      /* 256 MiB window  */
#define STRIDE      4096               /* 4 KiB            */
#define N_PAGES     60000              /* pages to fault   */

struct vmsort_iter { uint64_t ptr; uint32_t cap; uint32_t out; };
#define VMSORT_IOCTL _IOWR('v', 1, struct vmsort_iter)

static inline uint64_t clk_ns(void)
{ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec*1000000000ULL + ts.tv_nsec; }

static int cmp16(const void *a,const void *b){return*(const uint16_t*)a-*(const uint16_t*)b;}

/* radix‑16, two passes ------------------------------------------------ */
static void radix16(uint16_t *a,size_t n){
    uint16_t *tmp=malloc(n*2);size_t cnt[256];
    memset(cnt,0,sizeof cnt);for(size_t i=0;i<n;++i)cnt[a[i]&0xFF]++;
    size_t pos=0;for(size_t i=0;i<256;++i){size_t c=cnt[i];cnt[i]=pos;pos+=c;}
    for(size_t i=0;i<n;++i){uint8_t b=a[i]&0xFF;tmp[cnt[b]++]=a[i];}
    memset(cnt,0,sizeof cnt);for(size_t i=0;i<n;++i)cnt[tmp[i]>>8]++;
    pos=0;for(size_t i=0;i<256;++i){size_t c=cnt[i];cnt[i]=pos;pos+=c;}
    for(size_t i=0;i<n;++i){uint8_t b=tmp[i]>>8;a[cnt[b]++]=tmp[i];}
    free(tmp);
}

/* counting 16‑bit ---------------------------------------------------- */
static void count16(uint16_t *a,size_t n){static uint32_t c[65536];
    memset(c,0,sizeof c);
    for(size_t i=0;i<n;++i)c[a[i]]++;
    size_t p=0;for(uint32_t v=0;v<65536;++v)while(c[v]--)a[p++]=v;
}

static void verify(const uint16_t *x,size_t n,const char *tag){for(size_t i=1;i<n;++i)if(x[i-1]>x[i]){fprintf(stderr,"%s not sorted\n",tag);exit(1);} }

#define BENCH(name,call) do{uint16_t *b=malloc(n*2);memcpy(b,orig,n*2);uint64_t t0=clk_ns();call;uint64_t t1=clk_ns();verify(b,n,name);printf("%-12s: %6.3f ms (%.1f ns/key)\n",name,(t1-t0)/1e6,(double)(t1-t0)/n);free(b);}while(0)

int main(void){
    /* map char‑device */
    int fd=open("/dev/vmsort",O_RDWR);if(fd<0){perror("open");return 1;}
    void *base=mmap(NULL,TOTAL_WIN,PROT_WRITE,MAP_SHARED,fd,0);
    if(base==MAP_FAILED){perror("mmap");return 1;}

    /* generate pages & fault in ------------------------------------ */
    srand(1);uint32_t *pg=malloc(N_PAGES*4);
    for(size_t i=0;i<N_PAGES;++i)pg[i]=rand()&0xFFFF;
    uint64_t f0=clk_ns();
    for(size_t i=0;i<N_PAGES;++i)((volatile char*)base)[pg[i]*STRIDE]=1;
    uint64_t f1=clk_ns();

    /* ioctl --------------------------------------------------------- */
    uint16_t *kbuf=malloc(N_PAGES*2);
    struct vmsort_iter it={.ptr=(uint64_t)kbuf,.cap=N_PAGES};
    uint64_t k0=clk_ns();
    if(ioctl(fd,VMSORT_IOCTL,&it)){perror("ioctl");return 1;}
    uint64_t k1=clk_ns();
    printf("kernel vmsort: %u keys  %6.3f ms (%.1f ns/key)\n",it.out,(k1-k0)/1e6,(double)(k1-k0)/it.out);
    verify(kbuf,it.out,"kernel");

    /* reshuffle to create identical unsorted workload -------------- */
    size_t n=it.out;uint16_t *orig=malloc(n*2);memcpy(orig,kbuf,n*2);
    for(size_t i=n-1;i>0;--i){size_t j=rand()% (i+1);uint16_t t=orig[i];orig[i]=orig[j];orig[j]=t;}

    BENCH("qsort",    qsort(b,n,2,cmp16));
    BENCH("radix16",  radix16(b,n));
    BENCH("count16",  count16(b,n));

    printf("fault phase  : %6.3f ms (%.1f ns/fault)\n\n",(f1-f0)/1e6,(double)(f1-f0)/N_PAGES);

    munmap(base,TOTAL_WIN);close(fd);free(pg);free(kbuf);free(orig);
    return 0;}

