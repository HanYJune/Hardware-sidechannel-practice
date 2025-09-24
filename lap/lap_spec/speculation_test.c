#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <stddef.h>   
#include <stdint.h>  
#include <stdbool.h>
#include <stddef.h>

#include "linked_list_utils.h"
#include "eviction.h"

//#include "timer_tools.h"

const int CACHE_THRESHOLD = 85;
const int MAX_PROBE = 4096;


Node* rootNode = NULL;
Node* lastNode = NULL;

pool_t pool, node_pool;


int bindToCpu(int cpuId){
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpuId,&set);

    return sched_setaffinity(0,sizeof(set),&set);
}

void initLinkedList(void *probe,int length,int stride)
{
    for (int nodeIndex = 0; nodeIndex < length; nodeIndex++)
    {
        unsigned char *ptr;

        ptr = (unsigned char *)probe + nodeIndex * stride;

        if (isLastNode(nodeIndex,length))
        {
            *ptr = 0x20;
            ptr -= 5 * stride;
            addNode(&rootNode, ptr);
        }
        else
        {
            *ptr = 0x10;
            addNode(&rootNode, ptr);
        }
    }

    // Remember the tail node to allow targeted cache flush later
    lastNode = rootNode;
    if (lastNode) {
        while (lastNode->nextNode) lastNode = lastNode->nextNode;
    }
}



int main(int argc, char* argv[]){
    if(bindToCpu(0) != 0){
        fprintf(stderr, "Warning: failed to set CPU affinity; continuing\n");
    }


    if (argc != 3) {
        printf("Usage: %s LL_SIZE STRIDE?????\n", argv[0]);
        return EXIT_FAILURE;
    }

    const int LINKED_LIST_LENGTH = atoi(argv[1]);
    const int STRIDE  = atoi(argv[2]);
    const long pageSize = sysconf(_SC_PAGESIZE);
    const int CACHE_HIT_THRESHOLD = 100; 

    
    srand((unsigned int)time(NULL));

    void* probe = mmap(NULL, pageSize * 256, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,0);
    if (probe == MAP_FAILED) { printf("Failed to allocate cache channel pages\n"); return EXIT_FAILURE; }
    

    memset(probe, 0x10, 256 * pageSize);
    
    initLinkedList(probe,LINKED_LIST_LENGTH,STRIDE);

    pool = make_pool(pageSize * 256, 1);

    evset_t evictionSets;
    measure_load_ns(&lastNode);
    measure_load_ns(probe);
    printf("before node: %lu\n", measure_load_ns((volatile uint8_t*)lastNode));
    printf("before cache flush : %ld\n", measure_load_ns(probe)); 
    printf("cache access time : %lu\n", measure_load_ns((volatile uint8_t*)lastNode));
    // flush
    for(int i = 0; i < 256; i++){
        volatile uint8_t* t = probe + i * pageSize;
        evictionSets = find_evset(t,&pool,CACHE_THRESHOLD,MAX_PROBE);

        if(evictionSets.n == 0){
            touch_ptrs(pool.ptrs,pool.count);
            __asm__ __volatile__("dmb ish" ::: "memory");
        } else {
            evict_with_evset(t,&evictionSets);
            __asm__ __volatile__("dmb ish" ::: "memory");
        }
    }
    
    // Additionally flush the last node object itself (struct Node)
    if (lastNode) {
        volatile uint8_t* tn = (volatile uint8_t*)lastNode;
        evset_t lastNodeEvictionSet = find_evset(tn, &pool, CACHE_HIT_THRESHOLD, MAX_PROBE);

        if (lastNodeEvictionSet.n == 0) {
            touch_ptrs(pool.ptrs, pool.count);
            __asm__ __volatile__("dmb ish" ::: "memory");
        } else {
            evict_with_evset(tn, &lastNodeEvictionSet);
            __asm__ __volatile__("dmb ish" ::: "memory");
        }
    }
    
  
    
   
    printf("after node: %lu\n", measure_load_ns((volatile uint8_t*)lastNode));
    
    printf("after cache flush : %ld\n", measure_load_ns(probe)); 
    printf("after cache flush : %ld\n", measure_load_ns(probe + 1 * pageSize));
    printf("after cache flush : %ld\n", measure_load_ns(probe + 2 * pageSize));
    printf("after cache flush : %ld\n", measure_load_ns(probe+ 3 * pageSize));
    printf("after cache flush : %ld\n", measure_load_ns(probe+ 4 * pageSize));
    
    // traverse
    *(volatile unsigned char *)(lastNode->data + 5 * STRIDE);
    unsigned char* p = (unsigned char*)probe;
    struct Node *head = rootNode;
    uint8_t temp;
    while (head) {
        register unsigned char *int_ptr = (unsigned char *)(head->data);
        register unsigned char lap_load = *int_ptr;
        
        temp ^= *(volatile uint8_t *)(probe + lap_load * pageSize);
        
        head = head->nextNode;
        //printf("%d\n",lap_load);
        //printf("100(architectural) access time = %lu \n", measure_load_ns(probe + 100 * pageSize));
    }
    

    //Reload
    volatile uint8_t junk = 0;
    uint64_t timings[256];
    
    for(int i = 0; i < 256; i++) timings[i] = measure_load_ns(probe + i * pageSize);
    printf("0x10(architectural) access time = %lu \n", timings[0x10]);
    printf("0x20(transient) access time = %lu \n", timings[0x20]);
    
    
    return 0;
}
