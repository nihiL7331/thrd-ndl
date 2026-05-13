#include "heap.h"
#include <assert.h>
#include <stddef.h>
#include <thrd_ndl/thrd_ndl.h>

#define PARENT(i)  (((i) - 1) / 2)
#define CHILD_L(i) (((i) * 2) + 1)
#define CHILD_R(i) (((i) * 2) + 2)

static inline void sift_up(heap_t* heap, size_t idx);
static inline void sift_down(heap_t* heap, size_t idx);

int heap_new(heap_t* heap, void** storage, size_t cap, int (*cmp_fn)(const void*, const void*)) {
  if (cap == 0 || heap == NULL || storage == NULL || cmp_fn == NULL)
    return THRD_EINVAL;

  heap->data = storage;
  heap->count = 0;
  heap->cap = cap;
  heap->cmp = cmp_fn;

  return THRD_SUCCESS;
}

int heap_push(heap_t* heap, void* obj) {
  if (heap == NULL || obj == NULL)
    return THRD_EINVAL;

  if (heap->count == heap->cap)
    return THRD_ENOMEM;

  heap->data[heap->count++] = obj;
  sift_up(heap, heap->count - 1);

  return THRD_SUCCESS;
}

void* heap_peek(const heap_t* heap) {
  if (heap == NULL || heap->count == 0)
    return NULL;

  return heap->data[0];
}

static inline void heap_swap(heap_t* heap, size_t idx_a, size_t idx_b) {
  void* tmp = heap->data[idx_a];
  heap->data[idx_a] = heap->data[idx_b];
  heap->data[idx_b] = tmp;
}

static inline void sift_up(heap_t* heap, size_t idx) {
  while (idx > 0) {
    size_t par_idx = PARENT(idx);

    if (heap->cmp(heap->data[idx], heap->data[par_idx]) >= 0)
      break;

    heap_swap(heap, idx, par_idx);

    idx = par_idx;
  }
}

static inline void sift_down(heap_t* heap, size_t idx) {
  while (1) {
    size_t l_idx = CHILD_L(idx);
    size_t r_idx = CHILD_R(idx);
    size_t min_idx = idx;

    if (l_idx < heap->count && heap->cmp(heap->data[l_idx], heap->data[min_idx]) < 0)
      min_idx = l_idx;
    
    if (r_idx < heap->count && heap->cmp(heap->data[r_idx], heap->data[min_idx]) < 0)
      min_idx = r_idx;

    // if both indices are either out of range, or are bigger than data[idx],
    // then break
    if (min_idx == idx)
      break;

    // else swap and continue from the smallest index
    heap_swap(heap, idx, min_idx);
    idx = min_idx;
  }
}
