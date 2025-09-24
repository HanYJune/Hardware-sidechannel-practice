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
#define PROBE_STRIDE  4096       // probe stride (bytes)
#define PROBE_COUNT   256       // probe lines => total 128 KiB
#define POOL_BYTES    (8u<<20)  // candidate pool size (8 MiB)

// ---- 타이밍 유틸 ----
static inline uint64_t now_ns(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}

// ---- 간단 난수(LFSR) ----
static inline uint64_t xorshift64(uint64_t *x){
    uint64_t v=*x; v^=v<<13; v^=v>>7; v^=v<<17; *x=v; return v;
}

// ---- 메모리 배리어 ----
static inline void dmb_ish(void){ __asm__ __volatile__("dmb ish" ::: "memory"); }

// ---- 한 주소 1회 접근 지연 측정 ----
static inline uint64_t measure_load_ns(volatile uint8_t *p){
    uint64_t t0=now_ns();
    volatile uint8_t v = *p; (void)v;
    uint64_t t1=now_ns();
    return t1 - t0;
}

// ---- 후보/에빅션셋 순회(Prime) ----
static inline void touch_ptrs(volatile uint8_t **list, int n){
    for (int i=0;i<n;i++){ volatile uint8_t x = *list[i]; (void)x; }
}

// ---- 임계값(THR) 캘리브레이션: L1 히트 vs 큰 버퍼 랜덤 접근 ----
static double calib_threshold_ns(void){
    // L1 히트 샘플
    uint8_t *p = (uint8_t*)aligned_alloc(CACHELINE, CACHELINE*2);
    if (!p) { perror("aligned_alloc(l1)"); exit(1); }
    memset(p, 0xAA, CACHELINE*2);
    volatile uint8_t *q = p;
    for(int i=0;i<32;i++){ volatile uint8_t x=*q; (void)x; } // 워밍
    double l1med=0.0; {
        const int N=101; double a[N];
        for(int i=0;i<N;i++) a[i]=(double)measure_load_ns(q);
        // 간단 정렬 후 중앙값
        for(int i=0;i<N-1;i++) for(int j=i+1;j<N;j++) if(a[j]<a[i]){double t=a[i];a[i]=a[j];a[j]=t;}
        l1med = a[N/2];
    }
    free(p);

    // DRAM-ish 샘플
    size_t bigsz = 32u<<20; // 32 MiB
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

    // 보수적 임계값
    double thr = (l1med*2.0 + drammed)/3.0;
    if (thr < l1med + 5.0) thr = l1med + 5.0;
    return thr;
}

// ---- 에빅션 테스트: t를 캐시에 올린 뒤 S를 터치 → t 재측정 ----
static int evicts(volatile uint8_t *t, volatile uint8_t **S, int n, double thr_ns){
    // 타깃을 확실히 L1에 올림
    for(int r=0;r<4;r++){ volatile uint8_t x=*t; (void)x; }
    dmb_ish();

    // 후보 셋을 터치 (prime)
    touch_ptrs(S, n);
    dmb_ish();

    // 타깃 재측정
    uint64_t lat = measure_load_ns(t);
    return (lat > (uint64_t)thr_ns);
}

// ---- 후보 풀 생성: 큰 버퍼를 라인 단위로 분해해 후보 포인터 리스트 구성 ----
typedef struct {
    volatile uint8_t **ptrs;
    int count;
    uint8_t *buf_base;          // 원본 버퍼 포인터 (free용)
} pool_t;

static pool_t make_pool(size_t bytes, int shuffle){
    // aligned_alloc: size는 alignment의 배수여야 함 -> bytes는 64의 배수 (OK)
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

// ---- 에빅션셋 탐색(Find) + 축소(Reduce) (Greedy) ----
typedef struct {
    volatile uint8_t **es; // 결과 포인터들
    int n;                 // 개수(보통 ways 근처)
} evset_t;

// target t와 동일 set을 만드는 에빅션셋 찾기
static evset_t find_evset(volatile uint8_t *t, pool_t *P, double thr_ns, int max_probe){
    // 1) 증가식으로 후보를 모아 evict 달성
    volatile uint8_t **cand = (volatile uint8_t**)malloc(sizeof(*cand) * P->count);
    if (!cand) { perror("malloc(cand)"); exit(1); }
    int cn = 0, pi = 0;
    for (; pi < P->count && cn < max_probe; ++pi){
        cand[cn++] = P->ptrs[pi];

        // 충분히 모였는지 종종 체크
        if (cn >= 4){
            if (evicts(t, cand, cn, thr_ns)) break;
        }
    }

    // 실패 시 빈 셋 반환
    if (pi == P->count && !evicts(t, cand, cn, thr_ns)) {
        evset_t r = { NULL, 0 }; free(cand); return r;
    }

    // 2) Greedy 축소: 한 개씩 빼 봐서 여전히 evict되면 제거
    int i = 0;
    while (i < cn){
        volatile uint8_t *save = cand[i];
        // cand_without_i 만들기 (in-place swap)
        cand[i] = cand[cn-1];
        int ok = evicts(t, cand, cn-1, thr_ns);
        if (ok){
            // i번째는 불필요 → 버림 (cn-1 유지), 다음 i는 같은 인덱스로 계속
            cn -= 1;
        }else{
            // 필요했음 → 원상복구하고 i++
            cand[i] = save;
            i += 1;
        }
    }

    // 결과 복사
    volatile uint8_t **es = (volatile uint8_t**)malloc(sizeof(*es)*cn);
    if (!es) { perror("malloc(es)"); exit(1); }
    for (int k=0;k<cn;k++) es[k]=cand[k];
    free(cand);
    evset_t r = { es, cn };
    return r;
}

// ---- 에빅션셋으로 대상 한 라인 밀어내기 ----
static inline void evict_with_evset(volatile uint8_t *t, evset_t *E){
    (void)t;
    touch_ptrs(E->es, E->n);
    dmb_ish();
}

// ---- 상위: probe[256*512]의 각 타깃을 에빅션셋으로 flush ----
void flush_probe_with_evsets(uint8_t *probe_base){
    fprintf(stderr, "[flush] calib...\n");
    // 임계값 추정
    double thr = calib_threshold_ns();
    fprintf(stderr, "[flush] thr=%.1f ns\n", thr);

    // 후보 풀
    pool_t P = make_pool(POOL_BYTES, 1);
    fprintf(stderr, "[flush] pool=%u KiB, candidates=%d\n", (unsigned)(POOL_BYTES>>10), P.count);

    // 각 타깃에 대해 에빅션셋 찾고 즉시 flush
    const int MAX_PROBE = 8192; // 후보 최대 사용 제한(안전장치)
    for (int i=0;i<PROBE_COUNT;i++){
        if ((i % 16) == 0) { fprintf(stderr, "[flush] target %d/%d\n", i, PROBE_COUNT); }
        volatile uint8_t *t = probe_base + (size_t)i * PROBE_STRIDE;

        evset_t E = find_evset(t, &P, thr, MAX_PROBE);
        if (E.n == 0){
            // 실패 시: 전역 스윕(폴백)으로라도 밀어내기
            touch_ptrs(P.ptrs, P.count);
            dmb_ish();
        }else{
            evict_with_evset(t, &E);
            free((void*)E.es);
        }
    }

    // 풀 해제
    free(P.buf_base);   // ★ 셔플과 무관하게 원본 버퍼만 free
    free(P.ptrs);
}

// ---- 시그널 핸들러: 크래시 시 즉시 로그 ----
static void sigh(int s){
    fprintf(stderr, "[!] caught signal %d\n", s);
    fflush(NULL);
    _Exit(128+s);
}

// ---- 데모용 main: probe를 만들고 flush 수행 ----
int main(int argc,char**argv){
    // 출력 무버퍼 + 시그널 핸들러
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    signal(SIGILL, sigh); signal(SIGSEGV, sigh);

    // 코어 고정(선택)
    if (argc>1){
        int cpu=atoi(argv[1]); cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu,&set);
        sched_setaffinity(0,sizeof(set),&set);
    }

    fprintf(stderr, "[start] pid=%d\n", getpid());

    // probe 배열 생성
    size_t PROBE_BYTES = (size_t)PROBE_COUNT * PROBE_STRIDE;
    uint8_t *probe = (uint8_t*)aligned_alloc(CACHELINE, PROBE_BYTES);

    uint64_t seed=0x3141592653589793ull;
    if (!probe) { perror("aligned_alloc(probe)"); return 1; }
    memset(probe, 0xCC, PROBE_BYTES);
    fprintf(stderr, "[probe] %zu bytes @ %p\n", PROBE_BYTES, (void*)probe);

    // 먼저 전 라인을 L1에 올리기(검증 가시성)
    for (int i=0;i<PROBE_COUNT;i++){ volatile uint8_t x = probe[i*PROBE_STRIDE]; (void)x; }
    fprintf(stderr, "[probe] warmed\n");

    double thr = calib_threshold_ns();

    int access_index[256];
    for(int k = 0; k < 256; k ++) access_index[k] = (int)(xorshift64(&seed) % PROBE_COUNT);

    printf("###### cache miss ######\n");
    for(int k=0;k<10;k++){
        int i = access_index[k];
        volatile uint8_t *p = probe + (size_t)i*PROBE_STRIDE;
        uint64_t lat = measure_load_ns(p);
        printf("probe[%3d] 1st-touch = %3llu ns  %s\n", i, (unsigned long long)lat,
               (lat > (uint64_t)thr ? "MISS" : "HIT"));
    }
    printf("###### cache hit ######\n");
    for(int k=0;k<10;k++){
        int i = access_index[k];
        volatile uint8_t *p = probe + (size_t)i*PROBE_STRIDE;
        uint64_t lat = measure_load_ns(p);
        printf("probe[%3d] 1st-touch = %3llu ns  %s\n", i, (unsigned long long)lat,
               (lat > (uint64_t)thr ? "MISS" : "HIT"));
    }
    // Flush (에빅션셋 방식)
    uint64_t t0=now_ns();
    flush_probe_with_evsets(probe);
    uint64_t t1=now_ns();
    //printf("Eviction-set based flush done in %.3f ms\n", (t1-t0)/1e6);

    
    
    
    // 간단 검증: 임의 몇 라인의 1st-touch 지연 출력
    printf("###### RESULT ######\n");
   

    for(int k=0;k<10;k++){
        int i = access_index[k];
        volatile uint8_t *p = probe + (size_t)i*PROBE_STRIDE;
        uint64_t lat = measure_load_ns(p);
        printf("probe[%3d] 1st-touch = %3llu ns  %s\n", i, (unsigned long long)lat,
               (lat > (uint64_t)thr ? "MISS" : "HIT"));
    }
    return 0;
}