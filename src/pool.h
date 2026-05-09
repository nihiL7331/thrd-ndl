#pragma once

#include <stddef.h>
#include <stdatomic.h>

typedef struct {
  size_t chunk_size;
  size_t chunk_align;
  size_t total_size;
  void*  start_ptr;
  void*  free_hd;
  atomic_flag lock;
} pool_t;

// return codes for allocator
#define POOL_SUCCESS 0
#define POOL_OOM 1
#define POOL_ARG 2
#define POOL_SMALL 3
#define POOL_UNINIT 4

int pool_new(pool_t* pool, size_t chunk_size, size_t chunk_align, size_t chunk_cnt);
int pool_destroy(pool_t* pool);
void* pool_alloc(pool_t* pool, size_t size, size_t align);
int pool_free(pool_t* pool, void* ptr);
int pool_clear(pool_t* pool);
