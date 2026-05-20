#pragma once

#include <stddef.h>

void*  os_alloc(size_t size);
void   os_free(void* ptr, size_t size);

int    protect_page(void* ptr, size_t size);
size_t page_size(void);

void* os_alloc_stack(size_t usable_size);
void  os_free_stack(void* base_ptr, size_t usable_size);
