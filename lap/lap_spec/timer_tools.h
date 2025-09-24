#include <time.h>
#include <stdint.h>

static inline uint64_t now_ns(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline uint64_t measure_load_ns(volatile uint8_t *p){
    uint64_t t0=now_ns();
    volatile uint8_t v = *p; (void)v;
    uint64_t t1=now_ns();
    return t1 - t0;
}