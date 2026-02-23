#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef BASE_ARRAY

typedef struct {
    void   *elements;
    size_t  len;
    size_t  head;
    size_t  element_size;
} dyn_array;

dyn_array dyn_array_create(size_t element_size){
    
    return (dyn_array){
        .elements = NULL,
        .element_size = element_size,
        .head = 1,
        .len  = 0
    };
}

int dyn_array_push(dyn_array *array, const void *element){
    if (!array) return -1;

    if (array->len >= (array->head - 1)){
        void *n = realloc(array->elements, array->head * 2 * array->element_size);
        if (!n) return -1;
        array->elements = n;
        array->head *= 2;
    }

    memcpy(
        ((char*)array->elements) + array->len * array->element_size, 
        element, 
        array->element_size
    );

    array->len++;
    return 0;
}

size_t dyn_array_index(dyn_array *array, const void *element){
    if (!array) return SIZE_MAX;
    for (size_t i = 0; i < array->len; i++){
        if (memcmp(element, ((char*)array->elements) + i * array->element_size, array->element_size) != 0) 
            continue;
        return i;
    }

    return SIZE_MAX;
}

int dyn_array_remove(dyn_array *array, size_t index){
    if (!array) return -1;
    if (array->len <= index) return -1;

    if (index < array->len - 1){
        memmove(
            ((char*)array->elements) + index * array->element_size,
            ((char*)array->elements) + (index + 1) * array->element_size,
            (array->len - index - 1) * array->element_size
        );
    }
    array->len--;
    return 0;
}

int dyn_array_count(dyn_array *array, const void *element){
    if (!array) return 0;

    int count = 0;
    for (size_t i = 0; i < array->len; i++){
        if (memcmp(element, 
            ((char*)array->elements) + i * array->element_size, 
            array->element_size
        ) == 0)
            count++;
    }

    return count;
}

void *dyn_array_at(dyn_array *array, size_t index){
    if (!array || array->len <= index) return NULL;

    return ((char*)array->elements) + index * array->element_size;
}

void dyn_array_setself(dyn_array *array){
    if (!array) return;

    for (size_t i = 0; i < array->len;){
        if (dyn_array_count(array, dyn_array_at(array, i)) > 1){
            dyn_array_remove(array, i);
            continue;
        }

        i++;
    }
}

int dyn_array_insert(dyn_array *array, size_t index, const void *element) {
    if (!array || !element || index > array->len) return -1;

    if (array->len >= (array->head - 1)) {
        size_t new_capacity = (array->head == 0) ? 2 : array->head * 2;
        void *n = realloc(array->elements, new_capacity * array->element_size);
        if (!n) return -1;
        
        array->elements = n;
        array->head = new_capacity;
    }

    if (index < array->len) {
        memmove(
            ((char*)array->elements) + (index + 1) * array->element_size,
            ((char*)array->elements) + index * array->element_size,
            (array->len - index) * array->element_size
        );
    }

    memcpy(
        ((char*)array->elements) + index * array->element_size,
        element,
        array->element_size
    );

    array->len++;
    return 0;
}

typedef int (*dyn_array_cmp_t)(const void *a, const void *b);

void dyn_array_sort(dyn_array *array, dyn_array_cmp_t cmp){
    if (!array || !cmp || array->len <= 1) return;
    
    qsort(array->elements, array->len, array->element_size, 
          (int (*)(const void*, const void*))cmp);
}

void dyn_array_end(dyn_array *array){
    if (!array) return;
    if (array->elements) free(array->elements);
    array->elements = NULL;
    array->len = 0;
    array->head = 0;
    array->element_size = 0;
}

#endif
#define BASE_ARRAY