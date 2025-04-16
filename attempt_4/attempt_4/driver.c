/* gcc -O2 -std=gnu11 -Wall driver.c -o driver */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <assert.h>

/* ------------ workload & ioctl constants ------------------------- */
#define N_KEYS   50000UL
#define TOTAL_WIN (256UL<<20)
#define STRIDE   4096

struct vmsort_iter { uint64_t ptr; uint32_t cap; uint32_t out; };
#define VMSORT_IOCTL _IOWR('v', 1, struct vmsort_iter)

/* ------------ RNG ------------------------------------------------ */
static inline uint64_t xorshift64(uint64_t *s){
    uint64_t x=*s; x^=x>>12; x^=x<<25; x^=x>>27; *s=x;
    return x*0x2545F4914F6CDD1DULL;
}

/* ------------ radix‑256 ------------------------------------------ */
static void radix256(uint16_t *a,size_t n){
    uint16_t *aux=malloc(n*2); if(!aux){perror("malloc");exit(1);}
    size_t cnt[256];
    for(int pass=0;pass<2;++pass){
        memset(cnt,0,sizeof(cnt));
        int shift=pass?8:0;
        for(size_t i=0;i<n;++i) cnt[(a[i]>>shift)&0xFF]++;
        size_t pos[256]; pos[0]=0;
        for(int i=1;i<256;++i) pos[i]=pos[i-1]+cnt[i-1];
        for(size_t i=0;i<n;++i)
            aux[pos[(a[i]>>shift)&0xFF]++]=a[i];
        memcpy(a,aux,n*2);
    }
    free(aux);
}

/* ------------ bottom‑up mergesort -------------------------------- */
static void mergesort16(uint16_t *a,size_t n){
    uint16_t *buf=malloc(n*2); if(!buf){perror("malloc");exit(1);}
    for(size_t w=1;w<n;w<<=1){
        for(size_t i=0;i<n;i+=2*w){
            size_t l=i, m=i+w, r=i+2*w;
            if(m>n) m=n;
            if(r>n) r=n;

            size_t p=l,q=m,k=l;
            while(p<m&&q<r) buf[k++]=(a[p]<=a[q])?a[p++]:a[q++];
            while(p<m) buf[k++]=a[p++];
            while(q<r) buf[k++]=a[q++];
        }
        uint16_t *tmp=a; a=buf; buf=tmp;
    }
    if(buf!=a) memcpy(buf,a,n*2);
    free(buf);
}

/* ------------ qsort wrapper -------------------------------------- */
static int cmp16(const void* x,const void* y){
    uint16_t a=*(const uint16_t*)x, b=*(const uint16_t*)y;
    return (a>b)-(a<b);
}
static void qsort16(uint16_t *a,size_t n){ qsort(a,n,2,cmp16); }

/* ------------ timing helper -------------------------------------- */
static uint64_t diff_ns(struct timespec s,struct timespec e){
    return (e.tv_sec-s.tv_sec)*1000000000ULL+(e.tv_nsec-s.tv_nsec);
}
static void bench(const char* name,void(*fn)(uint16_t*,size_t),
                  uint16_t* arr,size_t n){
    struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC_RAW,&t0);
    fn(arr,n);
    clock_gettime(CLOCK_MONOTONIC_RAW,&t1);
    uint64_t dt=diff_ns(t0,t1);
    printf("%-10s : %8.2f ms (%6.1f ns/key)\n",
           name, dt/1e6,(double)dt/n);
    for(size_t i=1;i<n;++i) assert(arr[i-1]<=arr[i]);
}

/* ------------ main ----------------------------------------------- */
int main(void){
    /* create unique 16‑bit key set */
    uint16_t *orig=malloc(N_KEYS*2),*qa=malloc(N_KEYS*2),
             *ra=malloc(N_KEYS*2),*ma=malloc(N_KEYS*2);
    if(!orig||!qa||!ra||!ma){perror("malloc");return 1;}

    uint8_t used[65536]={0}; size_t filled=0; uint64_t seed=0xcafebabe;
    while(filled<N_KEYS){
        uint16_t k=xorshift64(&seed)&0xFFFF;
        if(!used[k]){used[k]=1; orig[filled++]=k;}
    }
    memcpy(qa,orig,N_KEYS*2); memcpy(ra,orig,N_KEYS*2); memcpy(ma,orig,N_KEYS*2);

    /* ---- /dev/vmsort ------------------------------------------------*/
    int fd=open("/dev/vmsort",O_RDWR);
    if(fd<0){perror("open /dev/vmsort");return 1;}
    void* base=mmap(NULL,TOTAL_WIN,PROT_WRITE,MAP_SHARED,fd,0);
    if(base==MAP_FAILED){perror("mmap");return 1;}

    struct timespec s0,s1; clock_gettime(CLOCK_MONOTONIC_RAW,&s0);
    for(size_t i=0;i<N_KEYS;++i)
        ((volatile char*)base)[orig[i]*STRIDE]=1;

    uint16_t *out=malloc(N_KEYS*2);
    struct vmsort_iter it={.ptr=(uint64_t)out,.cap=N_KEYS};
    ioctl(fd,VMSORT_IOCTL,&it);
    clock_gettime(CLOCK_MONOTONIC_RAW,&s1);
    uint64_t dt=diff_ns(s0,s1);
    printf("vmsort     : %8.2f ms (%6.1f ns/key, out=%u)\n",
           dt/1e6,(double)dt/N_KEYS,it.out);
    for(size_t i=1;i<it.out;++i) assert(out[i-1]<=out[i]);
    munmap(base,TOTAL_WIN); close(fd);

    /* ---- user‑space sorts ----------------------------------------- */
    bench("qsort",     qsort16,   qa,N_KEYS);
    bench("radix256",  radix256,  ra,N_KEYS);
    bench("mergesort", mergesort16,ma,N_KEYS);

    free(orig);free(qa);free(ra);free(ma);free(out);
    return 0;
}

