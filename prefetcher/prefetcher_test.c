#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define CACHELINE 64
#define STRIDE 1151

static inline __attribute__((always_inline)) uint64_t rdtsc_fallback_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline __attribute__((always_inline)) uint64_t rdtsc(void) {
    // Use CLOCK_MONOTONIC_RAW for portability on Android/aarch64
    return rdtsc_fallback_ns();
}

static uint8_t probe[1000 * STRIDE] __attribute__((aligned(CACHELINE)));

int main(void){
    uint64_t t0,t1;
    volatile uint8_t temp = 0;
    int public_data[1000];
    for(int i = 0; i < 1000; i++) public_data[i] = i;

    t0 = rdtsc();
    temp ^= *(volatile uint8_t *)(probe + public_data[999] * STRIDE);
    t1 = rdtsc();
    printf("cache miss : %ld\n", t1 - t0);


    temp ^= *(volatile uint8_t *)(probe);
    /*
    t0 = rdtsc();
    temp ^= *(volatile uint8_t *)(probe + public_data[0] *STRIDE);
    t1 = rdtsc();
    printf("cache hit : %ld\n", t1 - t0);

    */
    t0 = rdtsc();
    temp ^= *(volatile uint8_t *)(probe +  STRIDE);
    t1 = rdtsc();

    printf("%ld\n",t1 - t0);
    /*
    t0 = rdtsc();
    temp ^= *(volatile uint8_t *)(probe + public_data[400] * STRIDE);
    t1 = rdtsc();
    printf("cache miss : %ld\n", t1 - t0);
    */
    return 0;
}