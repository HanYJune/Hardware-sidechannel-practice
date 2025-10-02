#define _GNU_SOURCE
#include <sched.h>
#include <stdint.h>
#include <x86intrin.h>

#define my_snip(name,code)      \
    asm(".pushsection .text\n"  \
        ".balign 0x1000\n"      \
        ".global " #name "_start\n" \
        ".type " #name "_start, @function\n" \
        #name "_start:\n"                   \
        code                                \
        "\n" #name "_end:\n"                \
        ".popsection\n");



#define LINE_SIZE 64
#define PAGE_SIZE 4096

static const size_t TRASH_SZ = (64UL<<20);
static const size_t PAGES = 8192;
static const int pattern[] = {9,15,4,1};
static const int patternLength = sizeof(pattern) / sizeof(pattern[0]);


static void pinToCore(int core) {
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
    sched_setaffinity(0, sizeof(set), &set);
}

__attribute__((noinline))
static void trainOnePage(uint8_t *base){
    
    
    for (int i=0; i<patternLength; i++){
        size_t off = (size_t)pattern[i] * LINE_SIZE;
        load(base,i);
    }

  
}

static void trash_llc(uint8_t *trash, size_t sz){
    volatile uint8_t acc = 0;
    for(size_t i=0;i<sz;i+=LINE_SIZE) acc ^= trash[i];
    (void)acc;
}

__attribute__((noinline))
static void load(uint8_t *base, int i){
    volatile uint8_t sum;
    size_t offset = (size_t)pattern[i] * LINE_SIZE;
    sum ^= base[offset];
}

int main(void){
    pinToCore(0);

    uint8_t *buf;
    size_t bufSize = PAGES * PAGE_SIZE;
    if(posix_memalign((void**)&buf, PAGE_SIZE, bufSize)){
        perror("posix_memalign");
        return 1;
    }

    memset(buf, 0, bufSize);

    uint8_t** pages = (uint8_t**)malloc(PAGES * sizeof(uint8_t*));
    if(!pages){ perror("malloc pages"); return 1;}

    for(size_t p = 0 ; p < PAGES; p++){
        pages[p] = buf + p * PAGE_SIZE;
    }

    //train
    for(size_t p = 0; p < PAGES - 1; p++){
        trainOnePage(pages[p]);
    }
    
    uint8_t *trash;
    if (posix_memalign((void**)&trash, 128, TRASH_SZ)) {
        perror("posix_memalign(trash)"); return 1;
    }
    memset(trash, 1, TRASH_SZ);

    trash_llc(trash, TRASH_SZ);

    volatile uint8_t sink = 0;
    int testPage = PAGES - 1;

    uint64_t t1 = __rdtscp(&sink);
    load(pages[testPage],0);
    uint64_t t2 = __rdtscp(&sink);
    printf("first pattern access time : %ld\n", t2 - t1);

    uint8_t offset;
    for(int i = 1; i < patternLength; i++){\
        t1 = __rdtscp(&sink);
        offset = pattern[i] * LINE_SIZE;
        sink ^= pages[testPage][offset];
        t2 = __rdtscp(&sink);
        printf("pattern access time : %ld\n", t2 - t1);
    }


    t1 = __rdtscp(&sink);
    offset = 60 * LINE_SIZE;
    sink ^= pages[testPage][offset];
    t2 = __rdtscp(&sink);
    printf("not pattern access time : %ld\n", t2 - t1);

    t1 = __rdtscp(&sink);
    offset = 3 * LINE_SIZE;
    sink ^= pages[testPage][offset];
    t2 = __rdtscp(&sink);
    printf("not pattern access time : %ld\n", t2 - t1);
}