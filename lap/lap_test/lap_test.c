#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
// export NDK="/home/hyj/android-ndk-r27c"

// "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"   --target=aarch64-linux-android21 -fPIE -pie -O2   -o lap_test lap_test.c

static int use_fallback_timer = 0;

// ARMv8 Generic Timer (virtual count). EL0에서 접근 가능해야 합니다.
// 기기 정책에 따라 막혀 있을 수 있어, 실패 시 clock_gettime 대체 제공.
static inline __attribute__((always_inline)) uint64_t rdtsc_cntvct(void) {
    uint64_t v;
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb");
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    asm volatile("isb");
    return v;
}

static inline __attribute__((always_inline)) uint64_t rdtsc_fallback_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline __attribute__((always_inline)) uint64_t rdtsc(void) {
    if (!use_fallback_timer) {
        // 간단한 프로빙: 첫 호출에서 cntvct 읽기 시도
        uint64_t v = rdtsc_cntvct();
        if (v == 0) { // 의미 없는 값이면 폴백 사용
            use_fallback_timer = 1;
            return rdtsc_fallback_ns();
        }
        return v;
    } else {
        return rdtsc_fallback_ns();
    }
}

static int compare_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}


static double median(uint64_t arr[], int n) {
    qsort(arr, n, sizeof(uint64_t), compare_u64);
    if ((n & 1) == 0) return (double)(arr[n/2 - 1] + arr[n/2]) / 2.0;
    return (double)arr[n/2];
}

int bindToCpu(int cpu_id) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu_id, &set);
    return sched_setaffinity(0, sizeof(set), &set); // 현재 스레드
}

void* allocate_memory(int sizeOfPage, int numOfPage){
    void* buffer = mmap(NULL, (size_t)numOfPage * (size_t)sizeOfPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buffer == MAP_FAILED) {
        perror("MMAP");
    }
    memset(buffer, 0x0, (size_t)numOfPage * (size_t)sizeOfPage);

    return buffer;
}

static inline __attribute__((always_inline))
void dryRun(int* arr, int iteration){
    int junk = 0;
    
    for(int i = 0; i < iteration; i++)
        junk = arr[junk];
}

static inline __attribute__((always_inline))
double measureRunningTime(int* arr, int numOfIteration, int numOfRep, uint64_t* timings){
    uint64_t t0, t1;
    int junk;

    for(int rep; rep < numOfRep; rep++){
        junk = 0;
        t0 = rdtsc();
        for(int iter; iter < numOfIteration; iter++){
            junk = arr[junk];
        }
        t1 = rdtsc();
        timings[rep] = (t1 - t0);
    }
    return median(timings,numOfRep);
}

static inline __attribute__((always_inline))
void shuffleValues(int* arr, int numOfPages, long sizeOfPage){
    int lengthOfArray = (int)(numOfPages * sizeOfPage / (long)sizeof(int));

    for(int i = 0; i < lengthOfArray; i++)
        arr[i] = rand() % (lengthOfArray + 1);
}

static inline __attribute__((always_inline))
void strideValues(int* arr, int numOfPages, long sizeOfPage, int stride){
    int lengthOfArray = (int)(numOfPages * sizeOfPage / (long)sizeof(int));
    int index = 0;
    
    while(index + stride < lengthOfArray){
        arr[index] = index + stride;
        index += stride;
    }
}
int main(int argc, char* argv[]){
    int coreId = 0;

    if(bindToCpu(coreId) != 0){
        fprintf(stderr, "Warning: failed to set CPU affinity to core %d (continue anyway)\n", coreId);
    }

    if(argc != 2){
        printf("Usage: %s numOfIters\n", argv[0]);
        return EXIT_FAILURE;
    }

    const int numOfIters = atoi(argv[1]);
    const long sizeOfPage = sysconf(_SC_PAGESIZE); // 보통 4096
    const int numOfRep = 100;
    const int stride = 8;
    const int numOfPagesOfBuffer = 1 + (numOfIters * stride * sizeof(int)) / sizeOfPage;
    int* array = (int*)allocate_memory(sizeOfPage,numOfPagesOfBuffer);

    uint64_t timings[numOfRep];
    uint64_t resultTime;

    printf("Iters = %d, PageSz = %ld\n", numOfIters, sizeOfPage);

    srand((unsigned int)time(NULL));

    shuffleValues(array,numOfPagesOfBuffer,sizeOfPage);
    dryRun(array,numOfIters);
    resultTime = measureRunningTime(array,numOfIters,numOfRep,timings);

    printf("Loop Random Addr + Random Value: %.2f\n", resultTime);
    

    strideValues(array,numOfPagesOfBuffer,sizeOfPage,stride);
    dryRun(array,numOfIters);
    resultTime = measureRunningTime(array,numOfIters,numOfRep,timings);

    printf("Loop stride addr + stride value: %.2f\n", resultTime);
}