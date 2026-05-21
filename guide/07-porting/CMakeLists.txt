cmake_minimum_required(VERSION 3.20)
project(thrd_ndl_tutorial LANGUAGES C ASM)

set(CMAKE_C_STANDARD 17)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  if(WIN32)
    set(ARCH_SRC src/arch/x86_64/context_win64.S)
  else()
    set(ARCH_SRC src/arch/x86_64/context_unix.S)
  endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)")
  set(ARCH_SRC src/arch/arm64/context_arm64.S)
else()
  message(FATAL_ERROR "arch '${CMAKE_SYSTEM_PROCESSOR}' not supported")
endif()

add_executable(demo
  ${ARCH_SRC}
  demo/demo.c
  src/tcb.c
  src/scheduler.c
  src/platform.c
  src/pool.c
  src/mutex.c
  src/cond.c
  src/heap.c
)

target_include_directories(demo PRIVATE include src)
