#include <bits/pthreadtypes.h>
#include <pthread.h>
#include "dyn_array.h"

#ifndef BASE_PROT_ARRAY

typedef struct {
    dyn_array       array;
    pthread_mutex_t mtx;
} prot_array;

prot_array prot_array_create(size_t element_size){
    prot_array arr;
    arr.array = dyn_array_create(element_size);
    
    pthread_mutexattr_t attrs;
    pthread_mutexattr_init(&attrs);
    pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&arr.mtx, &attrs);
    pthread_mutexattr_destroy(&attrs);

    return arr;
}

void prot_array_lock(prot_array *array){
    pthread_mutex_lock(&array->mtx);
}

void prot_array_unlock(prot_array *array){
    pthread_mutex_unlock(&array->mtx);
}

int prot_array_push(prot_array *array, const void *element){
    pthread_mutex_lock(&array->mtx);
    int r = dyn_array_push(&array->array, element);
    pthread_mutex_unlock(&array->mtx);
    return r;
}

size_t prot_array_index(prot_array *array, const void *element){
    pthread_mutex_lock(&array->mtx);
    size_t r = dyn_array_index(&array->array, element);
    pthread_mutex_unlock(&array->mtx);
    return r;
}

void *prot_array_at(prot_array *array, size_t index){
    pthread_mutex_lock(&array->mtx);
    void *r = dyn_array_at(&array->array, index);
    pthread_mutex_unlock(&array->mtx);
    return r;
}

int prot_array_remove(prot_array *array, size_t index){
    pthread_mutex_lock(&array->mtx);
    int r = dyn_array_remove(&array->array, index);
    pthread_mutex_unlock(&array->mtx);
    return r;
}

void prot_array_end(prot_array *array){
    pthread_mutex_destroy(&array->mtx);
    return dyn_array_end(&array->array);
}


#endif
#define BASE_PROT_ARRAY