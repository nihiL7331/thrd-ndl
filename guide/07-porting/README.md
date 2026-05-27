### Porting

So far, this tutorial has focused entirely on x86_64 architecture running on Unix-like OSs (Linux/macOS).
In this section we'll make this library truly cross-platform, supporting ARM64 architecture and Windows.

#### Windows preemption

Windows support requires replacing `mmap` with `VirtualAlloc`, which is straightforward.
The real challenge is preemption.
Windows does not have POSIX signals, so there is no `SIGVTALRM`.

To solve this, we spin up a dedicated OS timer thread. 
Every ~7ms, this thread explicitly suspends the main OS thread running our scheduler.
But we can't just call `thrd_yield` from the timer thread, it has to execute on the main thread's stack.

To force the main thread to yield itself, we'll use a simple trick.
While the main thread is suspended, the timer thread modifies the main thread's CPU context to set the [trap flag](https://en.wikipedia.org/wiki/Trap_flag).
The trap flag tells the CPU to execute exactly one instruction and then trigger a hardware breakpoint ([`EXCEPTION_SINGLE_STEP`](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-exception_record)).

We register a [vectored exception handler](https://learn.microsoft.com/en-us/windows/win32/debug/vectored-exception-handling) to catch this breakpoint.
When the handler fires, it is executing on the main thread.
It clears the trap flag and calls `thrd_yield`.

Here's the full implementation for `src/platform.c`:

```c
#include "platform.h"
#include <stdint.h>            // include for 'uint64_t'
#include <thrd_ndl/thrd_ndl.h> // include for 'thrd_yield'

#define PREEMPT_TIMER_INTERVAL 7331
static volatile int preempt_cnt = 0;

static inline size_t align_to_page(size_t size);

#ifdef _WIN32

#define NOMINMAX
#include <windows.h>
#include <memoryapi.h>

#define TRAP_FLAG_MASK 0x100ULL

static HANDLE main_thrd;
static HANDLE timer_thrd;

uint64_t get_os_time(void) {
  static LARGE_INTEGER win_freq = {0};
  if (win_freq.QuadPart == 0)
    QueryPerformanceFrequency(&win_freq);

  LARGE_INTEGER ticks;
  QueryPerformanceCounter(&ticks);

  return (ticks.QuadPart * 1000ULL) / win_freq.QuadPart;
}

void os_sleep_ms(uint64_t time_ms) {
  Sleep((DWORD)time_ms);
}

void* os_alloc(size_t size) {
  return VirtualAlloc(NULL, align_to_page(size), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void os_free(void* ptr, size_t size) {
  (void)size;

  if (ptr == NULL) 
    return;

  VirtualFree(ptr, 0, MEM_RELEASE);
}

int protect_page(void* ptr, size_t size) {
  DWORD old_prot = 0;
  BOOL success = (int)VirtualProtect(ptr, align_to_page(size), PAGE_NOACCESS, &old_prot);
  return success ? 0 : -1;
}

size_t page_size(void) {
  static size_t cached_page_size = 0;

  if (cached_page_size == 0) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    cached_page_size = (size_t)sysInfo.dwPageSize;
  }

  return cached_page_size;
}

LONG WINAPI signal_handler(PEXCEPTION_POINTERS except_info) {
  if (except_info->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
    except_info->ContextRecord->EFlags &= ~0x100ULL;

    if (preempt_cnt == 0)
      thrd_yield();

    return EXCEPTION_CONTINUE_EXECUTION;
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI timer_loop(LPVOID arg) {
  (void)arg;
  while (1) {
    Sleep(PREEMPT_TIMER_INTERVAL / 1000); // milliseconds

    if (preempt_cnt == 0) {
      SuspendThread(main_thrd);

      if (preempt_cnt == 0) {
        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(main_thrd, &ctx)) {
          ctx.EFlags |= TRAP_FLAG_MASK;
          SetThreadContext(main_thrd, &ctx);
        }
      }

      ResumeThread(main_thrd);
    }
  }

  return 0;
}

void timer_init(void) {
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                  GetCurrentProcess(), &main_thrd, 0,
                  FALSE, DUPLICATE_SAME_ACCESS);
                  
  AddVectoredExceptionHandler(1, signal_handler);
  timer_thrd = CreateThread(NULL, 0, timer_loop, NULL, 0, NULL);
}

void preempt_disable(void) {
  preempt_cnt++;
}

void preempt_enable(void) {
  preempt_cnt--;
}

#else

#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

void* os_alloc(size_t size) {
  void* ptr = mmap(NULL, align_to_page(size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  return (ptr == MAP_FAILED) ? NULL : ptr;
}

void os_free(void* ptr, size_t size) {
  if (ptr == NULL) 
    return;

  munmap(ptr, align_to_page(size));
}

int protect_page(void* ptr, size_t size) {
  return mprotect(ptr, align_to_page(size), PROT_NONE);
}

size_t page_size(void) {
  static size_t cached_page_size = 0;
  if (cached_page_size == 0)
    cached_page_size = (size_t)sysconf(_SC_PAGESIZE);

  return cached_page_size;
}

uint64_t get_os_time(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  return (ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL);
}

void os_sleep_ms(uint64_t time_ms) {
  struct timespec ts;
  ts.tv_sec = time_ms / 1000ULL;
  ts.tv_nsec = (time_ms - (ts.tv_sec * 1000ULL)) * 1000000ULL;

  nanosleep(&ts, NULL);
}

static void signal_handler(int num) {
  (void)num;
  thrd_yield();
}

void timer_init(void) {
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = signal_handler;

  if (sigaction(SIGVTALRM, &action, NULL) == -1) {
    perror("sigaction failed");
    _Exit(1);
  }

  struct itimerval timer;
  memset(&timer, 0, sizeof(timer));
  timer.it_value.tv_usec = PREEMPT_TIMER_INTERVAL;
  timer.it_interval.tv_usec = PREEMPT_TIMER_INTERVAL;

  if (setitimer(ITIMER_VIRTUAL, &timer, NULL) == -1) {
    perror("setitimer failed");
    _Exit(1);
  }
}

void preempt_disable(void) {
  if (preempt_cnt++ == 0) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
  }
}

void preempt_enable(void) {
  if (--preempt_cnt == 0) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
  }
}

#endif

void* os_alloc_stack(size_t usable_size) {
  size_t total_size = usable_size + page_size(); // page_size for guard

  void* ptr = os_alloc(total_size);
  if (ptr == NULL)
    return NULL;

  if (protect_page(ptr, page_size()) != 0) {
    os_free(ptr, total_size);
    return NULL;
  }

  return ptr;
}

void os_free_stack(void* base_ptr, size_t usable_size) {
  os_free(base_ptr, usable_size + page_size());
}

static inline size_t align_to_page(size_t size) {
  size_t p_size = page_size();
  return (size + (p_size - 1)) & ~(p_size - 1);
}
```

#### Windows ABI differences

In addition to the timer mechanism, the [Windows x86_64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170) differs from the [System V ABI](https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf) in three crucial ways:
1. Instead of `%rdi` and `%rsi`, Windows passes the first two arguments in `%rcx` and `%rdx`.
2. On Windows, the `%rdi` and `%rsi` registers are also considered callee-saved.
3. While all `%xmm` registers are caller-saved on Linux, on Windows `%xmm6-%xmm15` are callee-saved.

That's why we need to create new assembly file, `src/arch/x86_64/context_win64.S`.

```gas
  .text
  .align 16
  .global thrd_switch

thrd_switch:
  /* push callee-saved registers onto the current stack */
  pushq %rbx
  pushq %rbp
  pushq %rdi
  pushq %rsi
  pushq %r12
  pushq %r13
  pushq %r14
  pushq %r15
  
  /* push callee-saved vector registers */
  subq   $160,   %rsp
  movups %xmm6,  0(%rsp)
  movups %xmm7,  16(%rsp)
  movups %xmm8,  32(%rsp)
  movups %xmm9,  48(%rsp)
  movups %xmm10, 64(%rsp)
  movups %xmm11, 80(%rsp)
  movups %xmm12, 96(%rsp)
  movups %xmm13, 112(%rsp)
  movups %xmm14, 128(%rsp)
  movups %xmm15, 144(%rsp)

  /* save the current stack pointer into old_tcb */
  /* old_tcb->rsp is at offset 0 */
  movq %rsp, (%rcx)

  /* load the new stack pointer */
  movq (%rdx), %rsp

  /* restore vector registers */
  movups 0(%rsp),   %xmm6
  movups 16(%rsp),  %xmm7
  movups 32(%rsp),  %xmm8
  movups 48(%rsp),  %xmm9
  movups 64(%rsp),  %xmm10
  movups 80(%rsp),  %xmm11
  movups 96(%rsp),  %xmm12
  movups 112(%rsp), %xmm13
  movups 128(%rsp), %xmm14
  movups 144(%rsp), %xmm15
  addq   $160,      %rsp

  /* pop the registers (reverse order to push) */
  popq %r15
  popq %r14
  popq %r13
  popq %r12
  popq %rsi
  popq %rdi
  popq %rbp
  popq %rbx

  /* jump to new thread */
  ret
```

We also need to update `CMakeLists.txt` to pick the correct assembly file during compilation.

```cmake
// set ...

if(WIN32)
  set(ARCH_SRC src/arch/x86_64/context_win64.S)
else()
  set(ARCH_SRC src/arch/x86_64/context_unix.S)
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
```

This means our `tcb_init` function needs to allocate space for 18 registers when setting up the initial stack frame.
However, 10 of those registers are _vector registers_, which are twice the size of a generic register we save.
Hence, we need space for 28 64-byte registers.
Replace the old `#define CALLEE_REG_CNT 6` with this:

```c
#ifdef _WIN32
  #define CALLEE_REG_CNT 28
#else
  #define CALLEE_REG_CNT 6
#endif
```

#### Windows TIB

On Windows, the OS tracks the current stack via the [Thread Information Block](https://en.wikipedia.org/wiki/Win32_Thread_Information_Block).
Because here context switch bypasses the OS and doesn't update the TIB's stack bounds, large stack allocations that trigger `__chkstk`, structured exception handling, and some C runtime functions may misbehave.
A complete Windows port requires either manually updating the TIB during `thrd_switch` or utilizing Windows [Fibers](https://learn.microsoft.com/en-us/windows/win32/procthread/fibers), but it's out of scope of this tutorial (for now).

#### ARM64 context switch

To run on ARM processors, the assembly context switch must change.

Instead of pushing `%rbx`, `%rbp`, etc., the ARM64 calling convention requires us to preserve registers `x19-x29`, as well as `d8-d15` (lower 64 bits of the vector registers).
Here's the equivalent `src/arch/arm64/context_arm64.S` file:

```gas
#ifdef __APPLE__
  #define SYM_SWITCH _thrd_switch
#else
  #define SYM_SWITCH thrd_switch
#endif

  .text
  .align 4
  .global SYM_SWITCH

SYM_SWITCH:
  /* push callee-saved registers onto the current stack
     pre decrement sp by 16 */
  stp x19, x20, [sp, #-16]!
  stp x21, x22, [sp, #-16]!
  stp x23, x24, [sp, #-16]!
  stp x25, x26, [sp, #-16]!
  stp x27, x28, [sp, #-16]!
  stp x29, x30, [sp, #-16]!

  /* push float registers */
  stp d8,  d9,  [sp, #-16]!
  stp d10, d11, [sp, #-16]!
  stp d12, d13, [sp, #-16]!
  stp d14, d15, [sp, #-16]!

  /* save the current stack pointer into old_tcb
     old_tcb->rsp is at offset 0
     move to tmp register first */
  mov x9, sp
  str x9, [x0]

  /* load the new stack pointer */
  ldr x9, [x1]
  mov sp, x9

  /* pop float registers in reverse */
  ldp d14, d15, [sp], #16
  ldp d12, d13, [sp], #16
  ldp d10, d11, [sp], #16
  ldp d8,  d9,  [sp], #16

  /* pop the registers (reverse order to push)
     post increment sp by 16 */
  ldp x29, x30, [sp], #16
  ldp x27, x28, [sp], #16
  ldp x25, x26, [sp], #16
  ldp x23, x24, [sp], #16
  ldp x21, x22, [sp], #16
  ldp x19, x20, [sp], #16

  /* jump to new thread */
  ret
```

We also need to update `CALLEE_REG_CNT` for ARM64 in `src/tcb.c`:

```c
#ifdef __aarch64__
  #define CALLEE_REG_CNT 20
#elif defined(_WIN32)
  #define CALLEE_REG_CNT 28
#else
  #define CALLEE_REG_CNT 6
#endif
```

Now, we need to again update `CMakeLists.txt` to pick the correct asm file.
Just replace the previous `if`-statements with this approach:

```cmake
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
```

#### ARM64 `ret` differences and the trampoline

On x86_64, the `ret` instruction pops the target address directly off the top of the stack.
That's why we pushed `tcb_wrap` onto the stack in `tcb_init`.

ARM64 doesn't work this way.
The `ret` instruction doesn't look at the stack at all, instead it jumps to whatever address is stored in the `x30` register.

When the ARM64 `thrd_switch` finishes restoring registers, it loads `x30` from the saved stack frame.
If we want a new thread to start executing, we have to place our starting address into the `x30` slot of our fake stack frame.

But there is a catch.
In ARM64, functions expect their starting context to be perfectly clean, but we are jumping out of a context switch.
To handle this cleanly, we use an assembly **trampoline**.
We store the `tcb_wrap` function in some arbitrary callee-saved register, e.g. `x19`.
We point `x30` to a tiny assembly function called `thrd_tramp`, which will just branch to the address stored in `x19`.

Add this to the `src/arch/arm64/context_arm64.S` file:

```gas
#ifdef __APPLE__
  #define SYM_SWITCH _thrd_switch
  #define SYM_TRAMP  _thrd_tramp
#else
  #define SYM_SWITCH thrd_switch
  #define SYM_TRAMP  thrd_tramp
#endif

// context switch asm...

  .global SYM_TRAMP

SYM_TRAMP:
  br x19
```

To support it across all architectures, we need to update the preprocessor directives in `src/tcb.c` to construct the correct stack frame shape:

```c
#ifdef __aarch64__
  #define CALLEE_REG_CNT 20
  #define X19_REG_POS 18
  #define X30_REG_POS 9
  extern void thrd_tramp(void);
#elif defined(_WIN32)
  #define CALLEE_REG_CNT 8
#else
  #define CALLEE_REG_CNT 6
#endif

tcb_t* tcb_init(void (*entry)(void)) {
  // stack alloc ...

  tcb->user_proc = entry;

  size_t size  = THRD_STACK_SIZE + page_size();
  uint64_t* sp = (uint64_t*)((uint8_t*)tcb->bsp + size);

  *(--sp) = (uint64_t)thrd_exit; // push the cleanup function (align + fail-safe)

#ifdef __aarch64__
  *(--sp) = 0;                  // dummy value for align
#else
  *(--sp) = (uint64_t)tcb_wrap; // jump to the wrapper, not 'entry'
#endif

  sp -= CALLEE_REG_CNT; // space for callee-saved registers
  memset(sp, 0, CALLEE_REG_CNT * sizeof(void*));

#ifdef __aarch64__
  sp[X19_REG_POS] = (uint64_t)tcb_wrap;
  sp[X30_REG_POS] = (uint64_t)thrd_tramp;
#endif

  tcb->rsp   = sp;
  tcb->state = THRD_READY;
  return tcb;
}
```

By storing `tcb_wrap` in `x19` and returning into `thrd_tramp`, we cleanly jump from the assembly domain back into the unified C lifecycle wrapper, abstracting away the architecture differences from the scheduler.

**[<| prev: Preemption](../06-preemption/README.md)**
