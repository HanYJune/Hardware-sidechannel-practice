// build_evsets_probe_and_node.c  (Android NDK / aarch64)
// - pool P를 한 번 만들고 재사용
// - probe[256*512] 각 라인과 Node* lastNode의 라인에 대해 eviction set 생성/캐싱
// - 이후엔 캐싱된 evset으로 즉시 flush 가능
//
// 빌드:
// $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang \
//   --target=aarch64-linux-android31 -D__ANDROID_API__=31 -O2 -fPIE -pie \
//   build_evsets_probe_and_node.c -o build_evsets_probe_and_node
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>

#define CACHELINE     64
#define PROBE_STRIDE  1024
#define PROBE_COUNT   256
#define POOL_BYTES    (8u<<20)  // 8 MiB 후보 풀 (필요시 조절)

typedef struct Node {
    struct Node* next;
    uint64_t     val;
    // 실제 프로젝트의 Node 정의를 사용하세요
} Node;

// ---------- 공통 유틸 ----------
static inline uint64_t now_ns(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}
static inline uint64_t xorshift64(uint64_t *x){
    uint64_t v=*x; v^=v<<13; v^=v>>7; v^=v<<17; *x=v; return v;
}
static inline void dmb_ish(void){ __asm__ __volatile__("dmb ish" ::: "memory"); }
static inline uint64_t measure_load_ns(volatile uint8_t *p){
    uint64_t t0=now_ns(); volatile uint8_t v=*p; (void)v; return now_ns()-t0;
}
static inline void touch_ptrs(volatile uint8_t **list, int n){
    for (int i=0;i<n;i++){ volatile uint8_t x = *list[i]; (void)x; }
}

// ---------- 임계값 캘리브레이션 ----------
static double calib_threshold_ns(void){
    // L1 중앙값
    uint8_t *p = (uint8_t*)aligned_alloc(CACHELINE, CACHELINE*2);
    if (!p) { perror("aligned_alloc(l1)"); exit(1); }
    memset(p, 0xAA, CACHELINE*2);
    volatile uint8_t *q = p;
    for(int i=0;i<32;i++){ volatile uint8_t x=*q; (void)x; }
    double l1med=0.0; {
        const int N=101; double a[N];
        for(int i=0;i<N;i++) a[i]=(double)measure_load_ns(q);
        for(int i=0;i<N-1;i++) for(int j=i+1;j<N;j++) if(a[j]<a[i]){double t=a[i];a[i]=a[j];a[j]=t;}
        l1med = a[N/2];
    }
    free(p);

    // DRAM-ish 중앙값
    size_t bigsz = 32u<<20;
    uint8_t *big = (uint8_t*)aligned_alloc(CACHELINE, bigsz);
    if (!big) { perror("aligned_alloc(big)"); exit(1); }
    memset(big, 0xBB, bigsz);
    uint64_t seed = 0x9e3779b97f4a7c15ull;
    double drammed=0.0; {
        const int N=101; double a[N];
        for(int i=0;i<N;i++){
            size_t off = (size_t)(xorshift64(&seed) % (bigsz - CACHELINE)) & ~(size_t)(CACHELINE-1);
            a[i] = (double)measure_load_ns(big + off);
        }
        for(int i=0;i<N-1;i++) for(int j=i+1;j<N;j++) if(a[j]<a[i]){double t=a[i];a[i]=a[j];a[j]=t;}
        drammed = a[N/2];
    }
    free(big);

    double thr = (l1med*2.0 + drammed)/3.0;
    if (thr < l1med + 5.0) thr = l1med + 5.0;
    return thr;
}

// ---------- evset 탐색에 필요한 타입 ----------
typedef struct {
    volatile uint8_t **ptrs;
    int count;
    uint8_t *buf_base;          // 원본 풀 버퍼 (free용)
} pool_t;

typedef struct {
    volatile uint8_t **es;
    int n;
} evset_t;

typedef struct {
    uintptr_t line_addr; // 타깃 라인 시작 주소 (64B 정렬)
    evset_t   set;       // 찾은 에빅션셋
} evcache_entry;

// ---------- 후보 풀 ----------
static pool_t make_pool(size_t bytes, int shuffle){
    uint8_t *buf = (uint8_t*)aligned_alloc(CACHELINE, bytes);
    if (!buf) { perror("aligned_alloc(pool)"); exit(1); }
    memset(buf, 0, bytes);
    int max = (int)(bytes / CACHELINE);
    volatile uint8_t **ptrs = (volatile uint8_t**)malloc(sizeof(*ptrs) * max);
    if (!ptrs) { perror("malloc(ptrs)"); exit(1); }
    for (int i=0;i<max;i++) ptrs[i] = buf + (size_t)i * CACHELINE;

    if (shuffle){
        uint64_t seed=0x243f6a8885a308d3ull;
        for (int i=max-1;i>0;i--){
            int j = (int)(xorshift64(&seed) % (uint64_t)(i+1));
            volatile uint8_t *tmp = ptrs[i]; ptrs[i]=ptrs[j]; ptrs[j]=tmp;
        }
    }
    pool_t P = { ptrs, max, buf };
    return P;
}

// ---------- 판정: S가 t를 evict하는가 ----------
static int evicts(volatile uint8_t *t, volatile uint8_t **S, int n, double thr_ns){
    for(int r=0;r<4;r++){ volatile uint8_t x=*t; (void)x; } // 타깃을 확실히 올림
    dmb_ish();
    touch_ptrs(S, n);
    dmb_ish();
    uint64_t lat = measure_load_ns(t);
    return (lat > (uint64_t)thr_ns);
}

// ---------- evset 찾기(증가식) + 축소(greedy) ----------
static evset_t find_evset(volatile uint8_t *t, pool_t *P, double thr_ns, int max_probe){
    volatile uint8_t **cand = (volatile uint8_t**)malloc(sizeof(*cand) * P->count);
    if (!cand) { perror("malloc(cand)"); exit(1); }
    int cn = 0, pi = 0;
    for (; pi < P->count && cn < max_probe; ++pi){
        // 같은 64B 라인은 제외(드물지만 안전)
        if ( ((uintptr_t)P->ptrs[pi] >> 6) == ((uintptr_t)t >> 6) ) continue;
        cand[cn++] = P->ptrs[pi];
        if (cn >= 4 && evicts(t, cand, cn, thr_ns)) break;
    }
    if (pi == P->count && !evicts(t, cand, cn, thr_ns)) {
        evset_t r = { NULL, 0 }; free(cand); return r;
    }
    // 축소
    int i = 0;
    while (i < cn){
        volatile uint8_t *save = cand[i];
        cand[i] = cand[cn-1];
        int ok = evicts(t, cand, cn-1, thr_ns);
        if (ok){ cn -= 1; } else { cand[i] = save; i += 1; }
    }
    volatile uint8_t **es = (volatile uint8_t**)malloc(sizeof(*es)*cn);
    if (!es) { perror("malloc(es)"); exit(1); }
    for (int k=0;k<cn;k++) es[k]=cand[k];
    free(cand);
    evset_t r = { es, cn };
    return r;
}

// ---------- flush with cached evset ----------
static inline void flush_with_evset(const evset_t *E){
    if (!E || E->n == 0) return;
    touch_ptrs(E->es, E->n);
    dmb_ish();
}

// ---------- 타깃 라인 주소 계산 ----------
static inline uintptr_t line_addr_of_ptr(const void* p){
    return (uintptr_t)p & ~(uintptr_t)(CACHELINE-1);
}
static inline volatile uint8_t* ptr_from_line(uintptr_t line){
    return (volatile uint8_t*)line;
}

// ---------- probe & node용 evset 빌드 ----------
static void build_evsets_for_probe(uint8_t *probe, pool_t *P, double thr,
                                   evcache_entry *out, int *ok_cnt){
    int success = 0;
    for (int i=0;i<PROBE_COUNT;i++){
        uintptr_t line = line_addr_of_ptr(probe + (size_t)i*PROBE_STRIDE);
        volatile uint8_t *t = ptr_from_line(line);
        evset_t E = find_evset(t, P, thr, /*max_probe*/4096);
        out[i].line_addr = line;
        out[i].set = E;
        if (E.n) success++;
        if ((i % 16) == 0) fprintf(stderr, "[probe] built %d/%d (ok=%d)\n", i, PROBE_COUNT, success);
    }
    *ok_cnt = success;
}
static int build_evset_for_node(Node *lastNode, pool_t *P, double thr,
                                evcache_entry *out, int use_val_field){
    // lastNode가 올라간 라인(또는 val 필드 라인)
    uintptr_t target_line = line_addr_of_ptr(use_val_field ? (void*)&lastNode->val : (void*)lastNode);
    volatile uint8_t *t = ptr_from_line(target_line);
    evset_t E = find_evset(t, P, thr, /*max_probe*/4096);
    out->line_addr = target_line;
    out->set = E;
    return E.n > 0;
}

// ---------- 시그널 핸들러 ----------
static void sigh(int s){
    fprintf(stderr, "[!] caught signal %d\n", s); fflush(NULL); _Exit(128+s);
}

// ---------- 데모 main ----------
int main(int argc, char**argv){
    // 로그 버퍼링 off + 시그널 핸들러
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    signal(SIGILL, sigh); signal(SIGSEGV, sigh);

    // (선택) 코어 고정
    if (argc>1){ int cpu=atoi(argv[1]); cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu,&set); sched_setaffinity(0,sizeof(set),&set); }

    fprintf(stderr, "[start] pid=%d\n", getpid());
    double thr = calib_threshold_ns();
    fprintf(stderr, "[calib] thr=%.1f ns\n", thr);

    // 후보 풀: 한 번만 만들고 모든 타깃에 재사용
    pool_t P = make_pool(POOL_BYTES, 1);
    fprintf(stderr, "[pool] %u KiB, candidates=%d\n", (unsigned)(POOL_BYTES>>10), P.count);

    // probe 배열
    size_t PROBE_BYTES = (size_t)PROBE_COUNT * PROBE_STRIDE;
    uint8_t *probe = (uint8_t*)aligned_alloc(CACHELINE, PROBE_BYTES);
    if (!probe) { perror("aligned_alloc(probe)"); return 1; }
    memset(probe, 0xCC, PROBE_BYTES);
    for (int i=0;i<PROBE_COUNT;i++){ volatile uint8_t x = probe[i*PROBE_STRIDE]; (void)x; }
    fprintf(stderr, "[probe] warmed (%zu bytes)\n", PROBE_BYTES);

    // Node* lastNode (데모용으로 하나 할당; 실제 환경에선 외부의 lastNode 사용)
    size_t allocsz = (sizeof(Node) + (CACHELINE - 1)) & ~(CACHELINE - 1);
    Node *lastNode = (Node*)aligned_alloc(CACHELINE, allocsz);
    if (!lastNode) { perror("aligned_alloc(Node)"); return 1; }
    memset(lastNode, 0, sizeof(Node));
    lastNode->val = 0xDEADBEEF;

    // 1) probe 전 라인용 evset 빌드 & 캐시
    evcache_entry probe_ev[PROBE_COUNT];
    int ok_probe = 0;
    build_evsets_for_probe(probe, &P, thr, probe_ev, &ok_probe);
    fprintf(stderr, "[probe] evset ready: %d/%d ok\n", ok_probe, PROBE_COUNT);

    // 2) Node(lastNode) 라인용 evset 빌드 & 캐시 (구조체 시작 라인 기준; val 필드를 겨냥하려면 인자 1)
    evcache_entry node_ev = {0};
    int ok_node = build_evset_for_node(lastNode, &P, thr, &node_ev, /*use_val_field=*/0);
    fprintf(stderr, "[node] evset %s (line=%p)\n", ok_node?"OK":"FAIL", (void*)node_ev.line_addr);

    // === 캐시된 evset으로 즉시 flush 예시 ===
    // 임의 몇 개 라인 pick → 워밍 → evset flush → 1st-touch 측정
    uint64_t seed=0x3141592653589793ull;
    for (int trial=0; trial<4; ++trial){
        int i = (int)(xorshift64(&seed) % PROBE_COUNT);
        volatile uint8_t *t = probe + (size_t)i*PROBE_STRIDE;
        // 워밍
        for(int r=0;r<4;r++){ volatile uint8_t x=*t; (void)x; }
        // flush (캐시된 evset 사용)
        flush_with_evset(&probe_ev[i].set);
        // 측정
        uint64_t lat = measure_load_ns(t);
        printf("[test] probe[%3d] 1st-touch = %3llu ns  %s\n",
               i, (unsigned long long)lat, (lat > (uint64_t)thr ? "MISS~" : "HIT?"));
    }
    if (ok_node){
        volatile uint8_t *tn = ptr_from_line(node_ev.line_addr);
        for(int r=0;r<4;r++){ volatile uint8_t x=*tn; (void)x; }
        flush_with_evset(&node_ev.set);
        uint64_t lat = measure_load_ns(tn);
        printf("[test] node(line) 1st-touch = %3llu ns  %s\n",
               (unsigned long long)lat, (lat > (uint64_t)thr ? "MISS~" : "HIT?"));
    }

    // 리소스 정리(계속 재사용하려면 생략)
    for (int i=0;i<PROBE_COUNT;i++) free((void*)probe_ev[i].set.es);
    if (ok_node) free((void*)node_ev.set.es);
    free(probe);
    free(lastNode);
    free(P.buf_base);
    free(P.ptrs);
    return 0;
}