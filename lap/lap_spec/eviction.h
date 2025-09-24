#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "timer_tools.h"

#define CACHELINE     64
#define PROBE_STRIDE  512       // probe stride (bytes)
#define PROBE_COUNT   256       // probe lines => total 128 KiB
#define POOL_BYTES    (8u<<20)  // candidate pool size (8 MiB)


typedef struct {
    volatile uint8_t **ptrs;
    int count;
    uint8_t *buf_base;          // 원본 버퍼 포인터 (free용)
} pool_t;

typedef struct {
    volatile uint8_t **es; // 결과 포인터들
    int n;                 // 개수(보통 ways 근처)
} evset_t;



 // ---- 간단 난수(LFSR) ----
static inline uint64_t xorshift64(uint64_t *x){
    uint64_t v=*x; v^=v<<13; v^=v>>7; v^=v<<17; *x=v; return v;
}

// ---- 후보/에빅션셋 순회(Prime) ----
static inline void touch_ptrs(volatile uint8_t **list, int n){
    for (int i=0;i<n;i++){ 
        volatile uint8_t x = *list[i]; 
        (void)x; 
    }
}

// ---- 에빅션셋으로 대상 한 라인 밀어내기 ----
static inline void evict_with_evset(volatile uint8_t *t, evset_t *E){
    (void)t;
    touch_ptrs(E->es, E->n);
    __asm__ __volatile__("dmb ish" ::: "memory");
}

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

// ---- 에빅션 테스트: t를 캐시에 올린 뒤 S를 터치 → t 재측정 ----
static int evicts(volatile uint8_t *t, volatile uint8_t **S, int n, double thr_ns){
    // 타깃을 확실히 L1에 올림
    for(int r=0;r<4;r++){ volatile uint8_t x=*t; (void)x; }
    __asm__ __volatile__("dmb ish" ::: "memory");

    // 후보 셋을 터치 (prime)
    touch_ptrs(S, n);
    __asm__ __volatile__("dmb ish" ::: "memory");

    // 타깃 재측정
    uint64_t lat = measure_load_ns(t);
    return (lat > (uint64_t)thr_ns);
    
}


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