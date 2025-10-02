#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <emmintrin.h>
#include <x86intrin.h> 

void **ptr_array;

// pointer_array[ptr1, ptr2, ptr3, ptr4 .... , secret_value]
// -> cache load : ptr1 값, ptr2 값, ptr3 값 .... secret_value 값이 가리키는 곳을 prefetch : 애초에 그럼 에러남


// slap 이랑 다른거 : pointer_array[ptr1, ptr2, ptr3, ptr4 ... , ptrn]
//    -> 각 ptr이 가리키는 값을 가지고 speculative execution
//      -> 만약 그 execution이 LOAD 라면 

#define CACHELINE 64
#define STRIDE 1023


static uint8_t probe[1000 * STRIDE] __attribute__((aligned(CACHELINE)));

static inline __attribute__((always_inline)) uint8_t probe_load(const uint8_t *addr) {
    uint8_t value;
    asm volatile("movb (%1), %0"
                 : "=q"(value)
                 : "r"(addr)
                 : "memory");
    return value;
}

int main(void){

    
    uint64_t t0,t1;
    volatile uint8_t temp = 0;
    int public_data[1000];
    unsigned long timing[100];
    unsigned int junk = 0;

    for(int i = 0; i < 1000; i++) public_data[i] = i;
   
    
    // load
    // same IP
    temp ^= probe_load(probe);
    
    // different IP
    //temp ^= *(volatile uint8_t*)(probe);
    
    // I/O 지연 -> 프리페처가 멈춤 ??? +프리페처는 파이프라인과 비동기적으로 데이터를 fetching함. 
    //t0 = rdtsc();
    
    //fflush(stdout);
    
    // 최종 결과 : 두 load 사이 cycle이 영향을 끼친다.
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    asm volatile("nop"); asm volatile("nop"); asm volatile("nop");asm volatile("nop");asm volatile("nop");
    


    t0 = __rdtscp(&junk);
    temp ^= probe_load(probe + 500);   // 최대 prefetching byte : 64 * 12 
    //temp ^= *(volatile uint8_t*)(probe +100);
    
    t1 = __rdtscp(&junk);
    printf("cache hit by prefetcher : %ld\n", t1 - t0);

   
    return 0;
}

