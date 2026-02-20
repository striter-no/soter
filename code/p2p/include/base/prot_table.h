#include <pthread.h>
#include "dyn_table.h"

#ifndef PROT_BASE_TABLE

typedef struct {
    pthread_mutex_t mtx;
    dyn_table       table;
} prot_table;

prot_table prot_table_create(size_t key_size, size_t val_size, dyn_own_t flags){
    prot_table table;
    table.table = dyn_table_create(key_size, val_size, flags);
    
    pthread_mutexattr_t attrs;
    pthread_mutexattr_init(&attrs);
    pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&table.mtx, &attrs);
    pthread_mutexattr_destroy(&attrs);

    return table;
}

void prot_table_lock(prot_table *table){
    pthread_mutex_lock(&table->mtx);
}

void prot_table_unlock(prot_table *table){
    pthread_mutex_unlock(&table->mtx);
}

int prot_table_set(prot_table *table, const void *key, const void *val){
    pthread_mutex_lock(&table->mtx);
    int r = dyn_table_set(&table->table, key, val);
    pthread_mutex_unlock(&table->mtx);
    return r;
}

void* prot_table_get(prot_table *table, const void *key){
    pthread_mutex_lock(&table->mtx);
    void *r = dyn_table_get(&table->table, key);
    pthread_mutex_unlock(&table->mtx);
    return r;
}

int prot_table_remove(prot_table *table, const void *key){
    pthread_mutex_lock(&table->mtx);
    int r = dyn_table_remove(&table->table, key);
    pthread_mutex_unlock(&table->mtx);
    return r;
}

void prot_table_end(prot_table *table){
    pthread_mutex_lock(&table->mtx);
    dyn_table_end(&table->table);
    pthread_mutex_unlock(&table->mtx);
    pthread_mutex_destroy(&table->mtx);
}


#endif
#define PROT_BASE_TABLE