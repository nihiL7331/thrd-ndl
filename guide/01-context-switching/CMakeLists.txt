cmake_minimum_required(VERSION 3.20)
project(thrd_ndl_tutorial LANGUAGES C ASM)

set(CMAKE_C_STANDARD 17)

add_executable(demo
  demo/demo.c
  src/tcb.c
  src/arch/x86_64/context_unix.S
)

target_include_directories(demo PRIVATE include src)
