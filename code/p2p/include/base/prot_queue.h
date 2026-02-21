#include "prot_array.h"

#ifndef BASE_PROT_QUEUE

typedef struct {
    prot_array arr;
} prot_queue;

prot_queue prot_queue_create(size_t element_size){
    return (prot_queue){
        .arr = prot_array_create(element_size)
    };
}

void prot_queue_lock(prot_queue *queue){
    prot_array_lock(&queue->arr);
}

void prot_queue_unlock(prot_queue *queue){
    prot_array_unlock(&queue->arr);
}

int prot_queue_push(prot_queue *q, const void *element){
    return prot_array_push(&q->arr, element);
}

int prot_queue_pop(prot_queue *q, void **elem){
    pthread_mutex_lock(&q->arr.mtx); 
    
    if (q->arr.array.len == 0) {
        pthread_mutex_unlock(&q->arr.mtx);
        return -1;
    }
    
    memcpy(elem, 
           ((char*)q->arr.array.elements), 
           q->arr.array.element_size
    );
    
    dyn_array_remove(&q->arr.array, 0);
    
    pthread_mutex_unlock(&q->arr.mtx);
    return 0;
}

void* prot_queue_peek(prot_queue *q) {
    return prot_array_at(&q->arr, 0);
}

void prot_queue_end(prot_queue *q){
    prot_array_end(&q->arr);
}

#endif
#define BASE_PROT_QUEUE