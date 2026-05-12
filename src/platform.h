#pragma once

#include <stdint.h>
#include <stddef.h>

uint64_t get_os_time(void);
void os_sleep_ms(uint64_t time_ms);

void* os_alloc(size_t size); // OS call for memory page
void os_free(void* ptr, size_t size); // OS call for free
int protect_page(void* ptr, size_t size);
size_t page_size(void); // memory page size

void preempt_disable(void);
void preempt_enable(void);
void timer_init(void);

// round up to a multiple of `page_size()`
static inline size_t align_to_page(size_t size) {
  size_t p_size = page_size();
  return (size + (p_size - 1)) & ~(p_size - 1);
}
