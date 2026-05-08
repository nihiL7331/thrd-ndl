#pragma once

#include <stddef.h>

// OS call for memory page
void* os_alloc(size_t size);

// OS call for free
void os_free(void* ptr, size_t size);

// memory page size
const size_t page_size();

int protect_page(void* ptr, size_t size);

const size_t align_to_page(size_t size);
