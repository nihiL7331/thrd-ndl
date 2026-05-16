#pragma once

#include <stdint.h>
#include <stddef.h>

uint64_t get_os_time(void);
void     os_sleep_ms(uint64_t time_ms);

void*  os_alloc(size_t size); // OS call for memory page
void   os_free(void* ptr, size_t size); // OS call for free
int    protect_page(void* ptr, size_t size);
size_t page_size(void); // memory page size
void*  os_alloc_stack(size_t usable_size);
void   os_free_stack(void* base_ptr, size_t usable_size);

void preempt_disable(void);
void preempt_enable(void);
void timer_init(void);
