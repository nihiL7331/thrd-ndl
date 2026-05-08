#pragma once

#include <stdint.h>
#include <stddef.h>

uint64_t get_os_time(void);
void* os_alloc(size_t size); // OS call for memory page
void os_free(void* ptr, size_t size); // OS call for free
int protect_page(void* ptr, size_t size);
size_t page_size(void); // memory page size

// round up to a multiple of `page_size()`
static inline size_t align_to_page(size_t size) {
  size_t p_size = page_size();
  return (size + (p_size - 1)) & ~(p_size - 1);
}
