#define _GNU_SOURCE
#include <stdio.h>
#include <sys/mman.h>
#include <x86intrin.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

const size_t MEM_SIZE  = (64 * 1024 * 1024);
const size_t PAGE_SZ = 4096;
const int CONST = 0xA;

static inline uint64_t get_cycles(void){
    unsigned aux;
    uint64_t t = __rdtscp(&aux);
    
    return t;
}
static inline void mfence_all(void){
    _mm_mfence(); _mm_lfence(); _mm_sfence();
}

static inline void lfence(void){
    __asm__ __volatile__(
        "lfence" : : : "memory"
    );
}

static inline __attribute__((always_inline)) uint64_t rdtsc(){
    lfence();
    return __rdtsc();
}

static inline __attribute__((always_inline)) void flushBuffer(uint8_t *buf){

    size_t line = 64;

    for(size_t i = 0; i < MEM_SIZE; i+= line)
        _mm_clflush(buf +i);
    
    mfence_all();
}

void init_arr(int* arr){
    for(int i = 0; i < 10000; i++)
        arr[i] = rand() % 10000;
}

void shuffle_offsets(int *buf, size_t n){
    for(size_t i = n - 1; i > 0; i--){
        size_t j = (size_t) (rand() % (int)(i + 1));
        int tmp = buf[i];
        buf[i] = buf[j];
        buf[j] = tmp;
    }
}
void init_offsets(int* offsets, int n,size_t buf_size){
    for(size_t i = 0; i < n; i++)
        offsets[i] = (int)(rand() % (int)buf_size);
}

void init_random_value(uint8_t* mem, size_t mem_size){
    for(size_t i = 0; i < mem_size; i++)
        *(mem + i) = rand() % 256;
}

int main(int argc, char *argv[]){
    int junk = 0;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    srand(time(NULL));

    if(sched_setaffinity(0,sizeof(set), &set) != 0){
        perror("sched_setaffinity");
        return EXIT_FAILURE;
    }


    const int ITERS = atoi(argv[1]);

    uint8_t *mem = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(mem,CONST,MEM_SIZE);
    init_random_value(mem,MEM_SIZE);

    int *offsets = (int*)malloc(sizeof(int)*ITERS);
    init_offsets(offsets,ITERS,sizeof(offsets));

    flushBuffer(mem);

    uint64_t t0,t1;
    t0 = get_cycles();
    lfence();
    for(int i = 0; i < ITERS; i++)
        junk = junk + mem[offsets[i]];
    lfence();
    t1 = get_cycles();

    uint64_t time = t1 - t0;

    printf("time = %ld", time);
    
    return 0;
}