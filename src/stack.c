#include "stack.h"

#if defined(_WIN32)
  #define NOMINMAX
  #include <windows.h>
  #include <memoryapi.h>
#else
  #include <sys/mman.h>
  #include <unistd.h>
#endif

// OS call for memory page
void* os_alloc(size_t size) {
#if defined(_WIN32)
  void* ptr = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  return ptr;
#else
  void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  return (ptr == MAP_FAILED) ? NULL : ptr;
#endif
}

// OS call for free
void os_free(void* ptr, size_t size) {
  if (ptr == NULL) 
    return;

#if defined(_WIN32)
  (void)size;
  VirtualFree(ptr, 0, MEM_RELEASE);
#else
  munmap(ptr, size);
#endif
}

// memory page size
const size_t page_size(void) {
#if defined(_WIN32)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return (size_t)sysInfo.dwPageSize;
#else
    return (size_t)sysconf(_SC_PAGESIZE);
#endif
}

int protect_page(void* ptr, size_t size) {
#if defined (_WIN32)
  return (int)VirtualProtect(ptr, size, PAGE_NOACCESS);
#else
  return mprotect(ptr, size, PROT_NONE);
#endif
}
