cmake_minimum_required(VERSION 3.20)

project(thrd_ndl VERSION 0.0.1 LANGUAGES C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 17)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set(ARCH_SRC src/arch/x86_64/context.S)
else()
  message(FATAL_ERROR "arch '${CMAKE_SYSTEM_PROCESSOR}' not supported")
endif()

set(SOURCE_FILES
  ${ARCH_SRC}
  src/tcb.c
  src/platform.c
  src/scheduler.c
  src/mutex.c
)

add_library(thrd_ndl STATIC ${SOURCE_FILES})

target_include_directories(thrd_ndl
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>

  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(thrd_ndl PRIVATE
  $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
  $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror>
)

enable_testing()
add_subdirectory(tests)
