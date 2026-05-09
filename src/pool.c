#include "pool.h"
#include "platform.h"
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

static inline size_t align_up(size_t size, size_t align);

int pool_new(pool_t* pool, size_t chunk_size, size_t chunk_align, size_t chunk_cnt) {
  if (pool == NULL || chunk_size == 0 || chunk_align == 0 || chunk_cnt == 0)
    return POOL_ARG;

  pool->chunk_align = chunk_align;

  size_t min_chunk_size = chunk_size;
  if (sizeof(void*) > min_chunk_size)
    min_chunk_size = sizeof(void*);

  pool->chunk_size = align_up(min_chunk_size, pool->chunk_align);
  if (pool->chunk_size > SIZE_MAX / chunk_cnt)
    return POOL_SMALL;

  pool->total_size = pool->chunk_size * chunk_cnt;

  pool->start_ptr = os_alloc(pool->total_size);
  if (pool->start_ptr == NULL)
    return POOL_OOM;

  pool->lock = (atomic_flag)ATOMIC_FLAG_INIT;

  pool_clear(pool);

  return POOL_SUCCESS;
}

int pool_destroy(pool_t* pool) {
  if (pool == NULL)
    return POOL_ARG;

  if (pool->start_ptr == NULL)
    return POOL_UNINIT;

  os_free(pool->start_ptr, pool->total_size);
  pool->start_ptr = NULL;
  pool->free_hd = NULL;

  return POOL_SUCCESS;
}

void* pool_alloc(pool_t* pool, size_t size, size_t align) {
  if (pool == NULL)
    return NULL;

  if (align == 0 || (align & (align - 1)) != 0)
    return NULL;

  if (size == 0 || size > pool->chunk_size || align > pool->chunk_align)
    return NULL;

  while (atomic_flag_test_and_set_explicit(&pool->lock, memory_order_acquire));

  if (pool->free_hd == NULL) {
    atomic_flag_clear_explicit(&pool->lock, memory_order_release);
    return NULL;
  }

  void* ret_head = pool->free_hd;
  pool->free_hd = *(void**)pool->free_hd;

  atomic_flag_clear_explicit(&pool->lock, memory_order_release);

  return ret_head;
}

int pool_free(pool_t* pool, void* ptr) {
  if (pool == NULL || ptr == NULL)
    return POOL_ARG;

  if (pool->start_ptr == NULL)
    return POOL_UNINIT;

  while (atomic_flag_test_and_set_explicit(&pool->lock, memory_order_acquire));

  *((void**)ptr) = pool->free_hd;
  pool->free_hd = ptr;

  atomic_flag_clear_explicit(&pool->lock, memory_order_release);

  return POOL_SUCCESS;
}

int pool_clear(pool_t* pool) {
  if (pool == NULL)
    return POOL_ARG;

  if (pool->start_ptr == NULL)
    return POOL_UNINIT;

  while (atomic_flag_test_and_set_explicit(&pool->lock, memory_order_acquire));

  uint8_t* raw_mem = (uint8_t*)pool->start_ptr;
  size_t num_chunks = pool->total_size / pool->chunk_size;

  for (size_t i = 0; i < num_chunks - 1; ++i) {
    void** curr_chunk = (void**)(raw_mem + i * pool->chunk_size);
    void*  next_chunk = raw_mem + (i + 1) * pool->chunk_size;
    *curr_chunk = next_chunk;
  }

  void** last_chunk = (void**)(raw_mem + (num_chunks - 1) * pool->chunk_size);
  *last_chunk = NULL;
  pool->free_hd = pool->start_ptr;

  atomic_flag_clear_explicit(&pool->lock, memory_order_release);
  
  return POOL_SUCCESS;
}

static inline size_t align_up(size_t size, size_t align) {
  return (size + (align - 1)) & ~(align - 1);
}
