#include "dyn_array.h"

#ifndef BASE_QUEUE

typedef struct {
    dyn_array arr;
} dyn_queue;

dyn_queue dyn_queue_create(size_t element_size){
    return (dyn_queue){
        .arr = dyn_array_create(element_size)
    };
}

int dyn_queue_push(dyn_queue *q, const void *element){
    return dyn_array_push(&q->arr, element);
}

int dyn_queue_pop(dyn_queue *q, void **elem){
    if (q->arr.len == 0) {
        return -1;
    }
    
    memcpy(elem, 
           ((char*)q->arr.elements), 
           q->arr.element_size
    );
    
    dyn_array_remove(&q->arr, 0);
    return 0;
}

void* dyn_queue_peek(dyn_queue *q) {
    return dyn_array_at(&q->arr, 0);
}

void dyn_queue_end(dyn_queue *q){
    dyn_array_end(&q->arr);
}

#endif
#define BASE_QUEUE