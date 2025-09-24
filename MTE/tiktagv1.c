#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

__attribute__((always_inline)) void check(unsigned long targetAddr) {
    volatile uint8_t temp;
    temp ^= *(volatile uint8_t*)(targetAddr);
    __asm__ __volatile__("dmb ish" ::: "memory");
}

__attribute__((always_inline)) void test() {
    
}



int main(void) {

    int probe[256 * 512];
    uint64_t timings[256];
    unsigned long testPtr;
    unsigned long targetPtr;

    char* public  = malloc(sizeof(char) * 20);
    char* secret = malloc(sizeof(char) * 10);

    for(size_t targetIndex = 0; targetIndex < 19; targetIndex++){
        // branch misprediction
        if(public + targetIndex < strlen(public)){ 
            check(targetPtr);
            test();
        }
    }
}