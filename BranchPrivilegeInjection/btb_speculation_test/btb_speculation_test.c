#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <x86intrin.h> /* for rdtsc, rdtscp, clflush */

#define CACHE_THRESHHOLD 80
// gcc -g -O0  -fno-pie -no-pie -o btb_speculation_test btb_speculation_test.c 

#define my_snip(name,code)                      \
    asm(".pushsection .text\n"                  \
        ".balign 0x1000\n"                    \
        ".global " #name "_start\n"             \
        ".type " #name "_start, @function\n"    \
        #name "_start:\n"                       \
        code                                    \
        "\n" #name "_end:\n"                    \
        ".popsection\n");

my_snip(br_src_call,
    "nop\n"
    "lfence\n"
    "leaq 1536(%rdi), %rbx\n" // rbx = &probe[3*256]
    "call *%rsi\n"
    "ret\n"
    )

my_snip(sig_gadget,
    "movzbl (%rbx), %eax\n"  // movzbl (suffix l = 32 bit)을 사용하였기 때문에 eax로 load 해야 함.
    "ret\n")

unsigned long br_src_addr;
unsigned long sig_gadget_addr;

extern unsigned char br_src_call_start[];
extern unsigned char br_src_call_end[];

extern unsigned char sig_gadget_start[];
extern unsigned char sig_gadget_end[];

int result[256];


unsigned long map_code(unsigned long addr, void* code_addr, size_t code_size){
    size_t page_sz = (size_t)getpagesize();
    size_t offset = addr & 0xfff;
    unsigned long base = addr & ~0xfff;

    size_t map_len = (offset + code_size + page_sz - 1) & ~(page_sz - 1);

    void* page = mmap((void*)base, map_len,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE,
                      -1, 0);

    if(page == MAP_FAILED){
        perror("MMAP");
        return (unsigned long)-1;
    }

    memcpy((char*)page + offset, code_addr, code_size);
    return (unsigned long)((char*)page + offset);
}

void flush_array(uint8_t* probe){
    for(int i = 0; i < 256; i++)
        _mm_clflush(&probe[i *512]);
}

void call_gadget(uint8_t *probe){
    asm volatile(
        "call *%%rax\n"
        :
        : "a"(br_src_addr) , "S" (sig_gadget_addr), "D" (probe)
    );
}

int find_cached_index(int* arr){
    int max_score = 0;
    int index = -1;
    for(int i = 0; i <256; i++){
        if(arr[i] > max_score){
            max_score = arr[i];
            index = i;
        }
    }
    return index;
}

uint64_t check_latency(uint8_t* probe,int index){
    
    unsigned int junk;
    volatile uint8_t *addr;
    register uint64_t t1, t2;

    addr = &probe[index * 512];

    t1 = __rdtscp(&junk);
    junk = *addr;
    t2 = __rdtscp(&junk);

    return t2 - t1;
}

void reload(uint8_t* probe){
    
    

    for(int i = 0; i <256; i++){
        int mix_i;
        mix_i = ((i * 167) + 13) & 255;

        if(check_latency(probe,mix_i) < CACHE_THRESHHOLD)
            result[mix_i]++;
    }

    
}

void run(uint8_t* probe){
    probe[0] = 5;
    for(int tries = 0; tries < 5000; tries++){
        flush_array(probe);

        call_gadget(probe);
        
        reload(probe);
    }
    printf("cached index is %d\n",find_cached_index(result));
}

int main(void){
    size_t br_src_code_tmpl_size = (size_t)(br_src_call_end - br_src_call_start);
    size_t sig_gadget_tmpl_size = (size_t)(sig_gadget_end - sig_gadget_start);

    uint8_t probe[256 *512];

    srandom(getpid());

    do{
        br_src_addr = ((unsigned long)random() << 16) ^ random();
        br_src_addr = map_code(br_src_addr, br_src_call_start, br_src_code_tmpl_size);
    } while (br_src_addr == (unsigned long)-1);

    do{
        sig_gadget_addr = ((unsigned long)random() << 16) ^ random();
        sig_gadget_addr = map_code(sig_gadget_addr, sig_gadget_start, sig_gadget_tmpl_size);
    } while (sig_gadget_addr == (unsigned long)-1);

    run(probe);

    return 0;
}

