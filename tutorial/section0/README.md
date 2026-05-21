# Build setup

This project will use CMake as a build tool.
Below is a `CMakeLists.txt` file that lists files we'll use during the next section.
`cmake -B build` will work after you've written all of them.
```cmake
cmake_minimum_required(VERSION 3.20)
project(thrd_ndl_tutorial LANGUAGES C ASM)

set(CMAKE_C_STANDARD 17)

add_executable(demo
  demo/demo.c
  src/tcb.c
  src/arch/x86_64/context_unix.S
)

target_include_directories(demo PRIVATE include src)
```
We will update this file as each section progresses.

**[next: Context switching |>](../section1/README.md)**
