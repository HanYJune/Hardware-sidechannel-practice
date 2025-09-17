#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

//#include "stats.h"
#include "btb_delay.h"

#define MAX_OPS 1024
#define NUM_ROUNDS 100000

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)



// typedef code_snip_t code_snip_t;
// 주어진 어셈블리 코드 조각을 개별 심볼로 주어진 인자 .text 섹션에 박아 넣음
// .text 영역에 넣는다
// 4KB 정렬을 강제한다 -> BTB aliasing에 유리하도록 ??
// 외부에서 참조 가능한 시작 라벨을 만든다
// 시작 라벨을 함수 타입으로 표시한다
// 코드 조각의 시작과 끝 라벨을 만든다
// #name_start -> foo_start (인자가 foo일 경우)
/*
    사용법 : 선언 my_snip(foo, "nop\nret\n")
    C에서 참조 : 
        - extern char foo_start[], foo_end[];
        - 길이 : size_t len = (size_t)(foo_end - foo_start)
        - 함수처럼 호출 : typedef void(*fn_t)(void);
                        ((fn_t)foo_start)());

    기본 문법 
        size_t : 객체의 크기/인덱스를 표현하기 위한 표준 정수 타입. 64비트 unsigned integer
        unsigned long : 
*/
#define my_snip(name,code)                      \
    asm(".pushsection .text\n"                  \  
        ".balign 0x1000           \n"           \
        ".global " #name "_start\n"             \  
        ".type " #name "_start, @function\n"    \
        #name "_start:\n"                       \
        code                                    \
        "\n" #name "_end:\n"                    \
        ".popsection\n");                        


// 점프할 코드 타겟 할당
unsigned long map_code(unsigned long addr, void* code_templ, size_t code_size){
     // MAP_FIXED_NOREPLACE 플래그는 해당 위치에 강제로 할당하고 이미 있는 자리면 실패를 반환
    // page alignment
    size_t pagesz = (size_t)getpagesize();
    size_t off = addr & 0xfff;
    unsigned long base = addr & ~0xfff;
    // 할당할 메모리 영역 크기는 [base ~ (base + offset + code_size의 페이지 사이즈로 반올림)]영역임
    size_t map_len = (off + code_size + pagesz - 1) & ~(pagesz - 1);
    void* page = mmap((void*)base,map_len, PROT_READ | PROT_WRITE | PROT_EXEC,MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE,-1,0);

    if(page == MAP_FAILED){
        perror("MMAP");
        return -1;
    }

    // char*로 캐스팅해야 오프셋을 더할 수 있음. void* 타입에 산술연산은 표준이 아님.
    memcpy((char *)page + off,code_templ,code_size);

    return page + off;
}


// 점프 테이블 0번째 점프 -> src, 1번째 점프 -> src, 2번째 점프 -> dst(종료)
unsigned long jmp_table[3];

// my_snip으로 생성된 심볼들을 참조
extern unsigned char train_src_call_start[];
extern unsigned char train_src_call_end[];

extern unsigned char train_dst_call_start[];
extern unsigned char train_dst_call_end[];

/*
    arg : jmp_table
    first branch = jmp_table[0] (train_dst_call)
    second branch = jmp_table[1] = jmp_table[0] + 8 (train_dst_call)
    last(finish) = jmp_table[2] = train_dst_call
*/
my_snip(train_src_call,
    ".rept " STR(MAX_OPS) "\n"
    "nop\n"
    ".endr\n"
    "lfence\n"

    "addq $8, %rsi\n"
    "call *(%rsi)\n"
    "ret\n"
);

my_snip(train_dst_call,
    " ret\n"
);

void run_one_nop(int num_nops, unsigned long src_addr){
    
    unsigned long jmp_offset = MAX_OPS - num_nops;
    //printf("jump to %p", (void*)(src_addr + jmp_offset));
    void* jump_target = (void*)(src_addr + jmp_offset);
    jmp_table[0] += jmp_offset;
    jmp_table[1] += jmp_offset;
    // *%rax = rax 레지스터에 있는 값으로 점프 vs *(%rax) rax 레지스터에 있는 값이 가리키는 곳에서 8바이트를 읽고 그곳으로 점프 [rax]
    // "a"(x) : x를 rax에 넣어라
    // "S"(x) : x를 rsi에 넣어라
    // % 하나는 이스케이프임. 즉 %%rax -> %rax
    asm volatile(
    "call *%%rax\n"
    :
    : "a"(src_addr), "S"((void*)jmp_table - 8));

}

void run_all_num_of_ops(unsigned long src_addr){
    for (int num_nops = 1; num_nops < MAX_OPS; num_nops++){
        run_one_nop(num_nops,src_addr);
    }

}


void set_jump_table(unsigned long src_addr, unsigned long dst_addr){
    jmp_table[0] = src_addr;
    jmp_table[1] = src_addr;
    jmp_table[2] = dst_addr;
}

int main(int argc, char *argv[]){

    unsigned long src_addr;
    unsigned long dst_addr;

    int max_num_ops = atoi(argv[1]);

    size_t src_snip_size = (size_t)(train_src_call_end - train_src_call_start);
    size_t dst_snip_size = (size_t)(train_dst_call_end - train_dst_call_start);
  
    srandom(getpid());
   
    for(int run = 0; run < 1; run++){
        
        do{
            src_addr = ((unsigned long)random() << 16) ^ random();
            src_addr = map_code(src_addr,train_src_call_start,src_snip_size);
        } while(src_addr == -1);
        
        do{
            dst_addr = ((unsigned long)random() << 16) ^ random();
            dst_addr = map_code(dst_addr,train_dst_call_start,dst_snip_size);
        } while(dst_addr == -1);

        
        set_jump_table(src_addr, dst_addr);

        run_all_num_of_ops(src_addr);
        //run_one_nop(max_num_ops,src_addr);
       
        clear(src_addr, src_snip_size, dst_addr, dst_snip_size);

        
    }
 
    return 0;
}

void clear(unsigned long src_addr, size_t src_snip_size, unsigned long dst_addr, size_t dst_snip_size) {
    size_t pagesz = (size_t)getpagesize();

    unsigned long src_base = src_addr & ~0xffful;
    size_t src_off = (size_t)(src_addr & 0xffful);
    size_t src_map_len = (src_off + src_snip_size + pagesz - 1) & ~(pagesz - 1);

    unsigned long dst_base = dst_addr & ~0xffful;
    size_t dst_off = (size_t)(dst_addr & 0xffful);
    size_t dst_map_len = (dst_off + dst_snip_size + pagesz - 1) & ~(pagesz - 1);

    munmap((void*)src_base, src_map_len);
    munmap((void*)dst_base, dst_map_len);
}

