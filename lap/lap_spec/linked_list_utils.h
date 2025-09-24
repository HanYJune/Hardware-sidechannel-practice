// linked_list_utils.h (header-only 버전)
#ifndef LINKED_LIST_UTILS_H
#define LINKED_LIST_UTILS_H

#include <stdlib.h>
#include <stdbool.h>

// Ensure Node objects are cacheline-aligned
typedef struct __attribute__((aligned(64))) Node {
    unsigned char* data;
    struct Node* nextNode;
} Node;

static inline Node* createNode(unsigned char *data){
    void* p = NULL;
    if (posix_memalign(&p, 64, sizeof(Node)) != 0 || !p) return NULL;
    Node* newNode = (Node*)p;
    newNode->data = data;
    newNode->nextNode = NULL;
    return newNode;
}

static inline void addNode(Node** head, unsigned char* data){
    Node* newNode = createNode(data);
    if (!newNode) return;
    if (*head == NULL) {                // ★ 버그 수정: head가 아니라 *head 체크
        *head = newNode;
        return;
    }
    Node* temp = *head;
    while (temp->nextNode) temp = temp->nextNode;
    temp->nextNode = newNode;
}

static inline bool isLastNode(int nodeIndex, int length){
    return (nodeIndex == (length - 1));
}

#endif // LINKED_LIST_UTILS_H
