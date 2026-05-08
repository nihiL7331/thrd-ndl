#pragma once

#include <stddef.h>

#if defined(_WIN32)
  #define NOMINMAX
  #include <windows.h>
  #include <memoryapi.h>
#else
  #include <sys/mman.h>
  #include <unistd.h>
#endif

// OS call for memory page
void* os_alloc(size_t size);

// OS call for free
void os_free(void* ptr, size_t size);

int protect_page(void* ptr, size_t size);

// memory page size
static inline size_t page_size(void) {
#if defined(_WIN32)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return (size_t)sysInfo.dwPageSize;
#else
    return (size_t)sysconf(_SC_PAGESIZE);
#endif
}

// round up to a multiple of `page_size()`
static inline size_t align_to_page(size_t size) {
  size_t p_size = page_size();
  return (size + (p_size - 1)) & ~(p_size - 1);
}
