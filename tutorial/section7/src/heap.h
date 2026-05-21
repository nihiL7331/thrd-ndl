#pragma once

#include <stddef.h> // include for 'size_t'

typedef struct {
  void** data;
  size_t count;
  size_t cap;
  // comparator returns:
  // < 0 if a should return first,
  // > 0 if b should return first,
  // 0 if equal.
  int (*cmp)(const void* a, const void* b);
} heap_t;

int   heap_new(heap_t* heap, void** storage, size_t cap, int (*cmp_fn)(const void*, const void*));
int   heap_push(heap_t* heap, void* obj);
void* heap_peek(const heap_t* heap);
void* heap_pop(heap_t* heap);
