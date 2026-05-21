#pragma once

#include <stddef.h>

typedef struct {
  size_t chunk_size;
  size_t total_size;
  void*  start_ptr;
  void*  free_hd;
} pool_t;

int   pool_new(pool_t* pool, size_t chunk_size, size_t chunk_align, size_t chunk_cnt);
int   pool_destroy(pool_t* pool);
void* pool_alloc(pool_t* pool);
int   pool_free(pool_t* pool, void* ptr);
int   pool_clear(pool_t* pool);
