#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <stddef.h>
#include <sys/syscall.h>
#if 0 // not needed; breaks some NDK setups
#include <asm/unistd.h>
#endif
#include <assert.h>
//#include <arm_neon.h>
#ifndef CACHELINE
#define CACHELINE 64
#endif
#if defined(__arm__) && !defined(__aarch64__)
void clearcache(char* begin, char *end)
{
  const int syscall = 0xf0002; /* __ARM_NR_cacheflush */
  __asm __volatile (
		    "mov	 r0, %0\n"			
		    "mov	 r1, %1\n"
		    "mov	 r7, %2\n"
		    "mov     r2, #0x0\n"
		    "svc     0x00000000\n"
		    :
		    :	"r" (begin), "r" (end), "r" (syscall)
		    :	"r0", "r1", "r7"
		    );
}
#endif
// ARMv8 Generic Timer (virtual count). EL0에서 접근 가능해야 합니다.
// 기기 정책에 따라 막혀 있을 수 있어, 실패 시 clock_gettime 대체 제공.
static inline __attribute__((always_inline)) uint64_t rdtsc_cntvct(void) {
    uint64_t v;
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb");
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
    __asm__ __volatile__("isb");
    return v;
}

static inline __attribute__((always_inline)) uint64_t rdtsc_fallback_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline __attribute__((always_inline)) uint64_t rdtsc(void) {
    // Use CLOCK_MONOTONIC_RAW for portability on Android/aarch64
    return rdtsc_fallback_ns();
}




static void pinCore(int cpu){
    long ncpu = sysconf(_SC_NPROCESSORS_CONF);
    if (ncpu <= 0 || cpu < 0 || cpu >= ncpu) return;
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
    (void)sched_setaffinity(0, sizeof(set), &set);
}

// Simple cache eviction: walk a 2MiB buffer by cacheline stride
static inline void cacheEvictGlobal(void)
{
    static uint8_t evict_buf[128 * 1024 * 1024];
    volatile uint8_t sink = 0;
    for(int i = 0; i <16; i++){
        for (size_t i = 0; i < sizeof(evict_buf); i += CACHELINE) {
            sink ^= evict_buf[i];
        }
    }
    __asm__ __volatile__("dmb ish" ::: "memory");
}

//static unsigned char buf[1000];

//const char *public_data = (const char *)buf;   //
int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("start\n");
    pinCore(0);
    int score[1024];
    memset(score, 0, sizeof(score));
    volatile uint8_t temp = 0;
    int mix_i;
    int public_data[256];
    for(int i = 0; i < 256; i++) public_data[i] = i;
    uint64_t t0,t1;
    static uint8_t probe[1000 * 1024] __attribute__((aligned(CACHELINE)));
    
    t0 = rdtsc();
    temp ^= *(volatile uint8_t *)(probe +  public_data[0] * 1024);
    t1 = rdtsc();
    printf("%ld\n",t1 - t0);

    t0 = rdtsc();
    temp ^= *(volatile uint8_t *)(probe +  public_data[0] *1024);
    t1 = rdtsc();
    printf("%ld\n",t1 - t0);
    t0 = rdtsc();
    temp ^= *(volatile uint8_t *)(probe +  public_data[1] * 1024);
    t1 = rdtsc();
    printf("%ld\n",t1 - t0);
    
    /*
    **  왜 1,2,3번에서, 3번만 오래걸리고 1번과 2번은 왜 차이가 별로 없는지 모르겠음
    for (int i = 0; i < 101; ++i) public_data[i] = (unsigned char)i;
    public_data[999] = 999;
    
    

    t0 = rdtsc();
    temp ^= *(volatile uint8_t *)(probe +  public_data[101] * 1024);
    t1 = rdtsc();

    printf("%ld\n",t1 - t0);

    temp ^= *(volatile uint8_t *)(probe +  public_data[0] * 1024);
    t0 = rdtsc();
    temp ^= *(volatile uint8_t *)(probe +  public_data[0] * 1024);
    t1 = rdtsc();

    printf("%ld\n",t1 - t0);

    t0 = rdtsc();
    temp ^= *(volatile uint8_t *)(probe +  public_data[999] * 1024);
    t1 = rdtsc();

    printf("%ld\n",t1 - t0);
    */
    /*
    for(int tries = 0; tries < 1; tries++){
        cacheEvictGlobal();

        temp ^= *(volatile uint8_t *)(probe + 100 * 1024);

        for(int i = 0; i < 1024; i ++){
            mix_i = ((i * 167) + 13) & 255;
            t0 = rdtsc();
            temp ^= *(volatile uint8_t *)(probe + mix_i * 2048);
            t1 = rdtsc();
            printf("%d access time : t1 - t0 = %ld\n",mix_i, t1 - t0);
            if(t1 - t0 < 90) score[mix_i]++;
        }
    }
    temp ^= *(volatile uint8_t *)(probe + 100 * 1024);
    t0 = rdtsc();
    temp ^= *(volatile uint8_t *)(probe + 100 * 1024);
    t1 = rdtsc();
    printf("100 access time : t1 - t0 = %ld\n", t1 - t0);
    if(t1 - t0 < 90) score[100]++;
    
    
    int max = 0;
    int maxScore = 0;

    for(int i = 0; i < 1024; i++){
        if(score[i] > maxScore){
            max = i;
            maxScore = score[i];
        }
    }
    
    printf("accessed value is %d\n",max);
    */
}
