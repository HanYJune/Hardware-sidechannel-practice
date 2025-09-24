#define _GNU_SOURCE
#include <stdio.h>
#include <inttypes.h>
#include <ptrauth.h>
/*
    clang -O2 -o pacman pacman.c \
  -target aarch64-linux-gnu \
  -march=armv8.3-a \
  -mbranch-protection=pac-ret \
  -fptrauth-intrinsics
*/


void target(void) {
    puts("target(): entered");
}

int main(void) {
    void (*raw_f)(void) = target;
    uintptr_t raw = (uintptr_t)raw_f;
    printf("raw ptr   = 0x%016" PRIxPTR "\n", raw);

    // 1) 포인터에 PAC(서명) 붙이기 (저장/전달용)
    void *signed_ptr = ptrauth_sign_unauthenticated((void*)raw_f,
                                                    ptrauth_key_function_pointer, 0);
    printf("signed ptr= %p (0x%016" PRIxPTR ")\n", signed_ptr, (uintptr_t)signed_ptr);

    // 2) 분기(호출) 직전에 인증 혹은 strip 수행 (반드시 해야 함)
    // 방법 A: strip (인증 검증 후 PAC 제거)
    void *stripped = ptrauth_strip(signed_ptr, ptrauth_key_function_pointer);
    printf("stripped  = %p (0x%016" PRIxPTR ")\n", stripped, (uintptr_t)stripped);

    // 안전하게 간접 호출
    void (*fcall)(void) = (void(*)(void))stripped;
    fcall();


    return 0;
}
