#pragma once

#include <stddef.h>
#include <stdint.h> // include for 'uint64_t'

void*  os_alloc(size_t size);
void   os_free(void* ptr, size_t size);

int    protect_page(void* ptr, size_t size);
size_t page_size(void);

void* os_alloc_stack(size_t usable_size);
void  os_free_stack(void* base_ptr, size_t usable_size);

uint64_t get_os_time(void);
void     os_sleep_ms(uint64_t time_ms);

void preempt_disable(void);
void preempt_enable(void);
void timer_init(void);
