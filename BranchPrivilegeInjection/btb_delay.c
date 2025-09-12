#define _GNU_SOURCE
#include <x86intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>

static inline __attribute__((always_inline)) void nop(){
    __asm__ __volatile__(
        "nop"
    );
}

static inline void lfence(void){
    __asm__ __volatile__(
        "lfence" : : : "memory"
    );
}

static inline void delay_nop(unsigned d){ 
    for(unsigned i = 0; i < d; i++) 
        nop();
}

__attribute__((noinline)) void target_A(void) { nop();}
__attribute__((noinline)) void target_B(void) { nop();}
__attribute__((noinline)) void target_C(void) { nop();}
__attribute__((noinline)) void target_D(void) { nop();}

typedef void (*target_function)(void);
target_function TARGETS[] = {target_A, target_B, target_C, target_D};

__attribute__((noinline))
void source_1(target_function f, unsigned d){
    volatile target_function vf = f;
    delay_nop(d);        
    vf();            
    delay_nop(d);        
    vf();            
    __asm__ __volatile__("" ::: "memory"); // TCO 방지
}

typedef void (*src_t)(target_function, unsigned);

__attribute__((noinline)) void source_2(target_function f, unsigned d){ source_1(f,d); }
__attribute__((noinline)) void source_3(target_function f, unsigned d){ source_1(f,d); }
__attribute__((noinline)) void source_4(target_function f, unsigned d){ source_1(f,d); }
static src_t SOURCES[] = { source_1, source_2, source_3, source_4 };

static inline void pin_cpu0(void){
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(0,&s);

    if(sched_setaffinity(0,sizeof(s),&s) != 0)
        perror("sched affinity");
}

int main(int argc, char* argv[]){
    pin_cpu0();
    int d = atoi(argv[1]);
    int ITERS = 100;

    srand((unsigned)time(NULL));

    for(int i = 0; i < ITERS; i++){
        //do_B1_B2_indirect_call(TARGETS[rand() % 4],d);
        src_t s = SOURCES[rand() % 4];
        target_function  t = TARGETS[rand() % 4];
        s(t, d);

    }
    
    return 0;
}