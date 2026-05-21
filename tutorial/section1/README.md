# Context switching

## What's in the CPU state?

By the end of this section, we want a function `thrd_switch(old, new)` that saves the current thread's CPU state and resumes another thread that was previously saved.
You may wonder: what does the CPU state actually contain?

The CPU has a small amount of working memory - called registers - and a much larger working area, the stack. Together, they hold everything about "where this thread is right now".
Together, they store current values, call history, and where to go next in code.

To pause a thread, we save all of that somewhere. To resume it, we put it back.

That was a high-level view of the CPU state. Concretely, what it actually consists of:
* Callee-saved registers - these are values that the caller of the function is relying on us not to modify. On `x86_64` *Linux*, these are: `%rbx`, `%rbp` and `%r12-%r15`. On *Windows* there are more - `%rsi` and `%rdi` also need to be stored. We must preserve these across a switch, so we save them.
* The stack pointer - it points to the top of the current call stack. Everything below it is the thread's history. We also save that value, to later restore the entire stack.
* The instruction pointer - it stores the address of the next instruction to run. We will not save it directly - `ret` and the stack will handle it for us (more on that later).

## Saving and resuming

We need to store the callee-saved registers somewhere. 
To do that, we will use the thread's own stack, with the `push` instruction.
We also need to save the stack pointer (which, notably, now points past the saved registers).
Unlike the callee-saved registers, we'll store it in the thread's control block, or TCB for short, which we'll define later.

To understand how the instruction pointer is stored, we need to understand what `call` and `ret` actually do.
When a caller does `call thrd_switch`, the CPU `push`es the address of the instruction **after** the call onto the stack, then `jump`s into `thrd_switch`.
So by the time `thrd_switch` starts executing, its return address is already on the stack.
`ret` is a mirror image of the `call` instruction.
It `pop`s the top of the stack and `jump`s there. 
It doesn't care what's there or how it got there.
We use that behavior to store and retrieve the instruction pointer.
When we save `%rsp`, the return address goes with it, since it's a part of the stack.
When `ret` later executes on this thread's restored stack, it pops that address and jumps to it.
The instruction pointer is preserved even though we didn't touch it.

Now that the thread's state is stored, we need to do the opposite to resume the new thread.
First, we load the stack pointer from the struct using the `mov` instruction.
Now we `pop` the callee-saved registers off the stack.
The procedure finishes with `ret`, handling the instruction pointer.

The above works for any thread that has been suspended by `thrd_switch` before. 
A brand-new thread has never been called this way, so its stack starts empty.
This case will be handled when we set up new threads later, in this section.

## The TCB and the switcher

We have been referring to 'the thread's struct' throughout, so let's define it now.
We'll call the struct *TCB* - Thread Control Block.
It's a name for a data structure used in OS kernels that we'll borrow for our implementation.
It will contain thread-specific information needed to manage the thread - the information that we've defined above.
It will be stored in `src/tcb.h`, which will be accompanied by `src/tcb.c` in a moment.
```c
#pragma once

typedef struct tcb {
  void* rsp; // the stack pointer
} tcb_t;
```
We'll expand on the TCB struct later.
We also need to get the thread's stack.
A keen eye might notice it's not in the TCB struct.
For now, we'll allocate each thread's stack as a static array. 
Section [Thread lifecycle](../tutorial/section3/README.md) introduces a proper stack allocator.

But now, let's transfer the earned knowledge about the context switch to some assembly code.
We'll create a `src/arch/x86_64/context_unix.S` file and create the following procedure.
```gas
thrd_switch:
  // push callee-saved registers onto the current stack
  push %rbx
  push %rbp
  push %r12
  push %r13
  push %r14
  push %r15

  // save the current stack pointer into old_tcb
  // old_tcb->rsp is at offset 0
  mov %rsp, (%rdi)

  // load the new stack pointer
  mov (%rsi), %rsp

  // pop the registers (reverse order to push)
  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %rbp
  pop %rbx

  // jump to new thread (instruction pointer is handled implicitly!)
  ret
```
The version in [src/arch/x86_64/context_unix.S](src/arch/x86_64/context_unix.S) adds a few ELF directives.
They're standard boilerplate, unrelated to the context switching itself.

## Public API

The code maps perfectly to the logic we've gone over before.
It's a good moment to expose a public API that will be called by the end user.
Create a `include/thrd_ndl/thrd_ndl.h` file (or whatever name suits you best).
For now, we'll keep it simple:
```c
#ifndef THRD_NDL_H
#define THRD_NDL_H

typedef void* thrd_t;

thrd_t      tcb_init(void (*entry)(void));
extern void thrd_switch(thrd_t old_tcb, thrd_t new_tcb);

#endif // THRD_NDL_H
```
`thrd_switch` is really an internal primitive.
Starting from the section [Cooperative scheduling](../section2/README.md), this will be abstracted away via `thrd_yield`.
For now, we'll invoke it manually.

## Thread initialization

Similarly, `tcb_init` will become an internal primitive starting from the section [Thread lifecycle](../section3/README.md).
Now we need to handle the thread initialization, so that `thrd_switch` works properly.
Create the file `tcb.c`.
It will handle the initialization of the TCB.

`tcb_init` does three things:
* allocates the TCB,
* allocates the stack,
* writes the initial stack frame that makes the first `ret` jump to the entry function.
```c
#include "tcb.h"
#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <stdint.h>

#define THRD_STACK_SIZE 16384
#define CALLEE_REG_CNT 6

thrd_t tcb_init(void (*entry)(void)) {
  tcb_t* tcb = (tcb_t*)malloc(sizeof(tcb_t));
  if (tcb == NULL)
    return NULL;

  uint8_t* stack = (uint8_t*)malloc(THRD_STACK_SIZE);
  if (stack == NULL) {
    free(tcb);
    return NULL;
  }
  // stack pointer is at the top of the stack
  uint64_t* sp  = (uint64_t*)(stack + THRD_STACK_SIZE);

  *(--sp) = 0;               // padding for ABI alignment
  *(--sp) = (uint64_t)entry; // fake return address for 'ret'
  sp -= CALLEE_REG_CNT;      // space for callee-saved registers

  tcb->rsp = sp;
  return tcb;
}
```
The extra 8-byte slot at the top is alignment padding.
The *System V ABI* requires `%rsp` to be 8 mod 16 at function entry, but with only the return address and six callee-saved registers, the math comes out 16-aligned instead.
The dummy slot shifts everything by one word so the entry function gets a properly aligned stack.
<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/fake_stack_dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/fake_stack_light.svg">
    <img alt="fake stack" src="../../docs/assets/fake_stack_dark.svg">
  </picture>

  <p><em>The fake initial stack frame written by <code>tcb_init</code>. The first <code>ret</code> after switching to this thread pops the entry point address and jumps there.</em></p>

</div>

For now we'll not have an option to free the `stack`, but it'll be handled later.

## Putting it together

Now, with the code finished, we're ready to put it all together. 
The following demo creates two threads that take turns printing a counter, switching to each other manually.
```c
#include <thrd_ndl/thrd_ndl.h>
#include <stdio.h>

static thrd_t main_thrd, thrd_a, thrd_b;

static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    printf("A: %d\n", i);
    thrd_switch(thrd_a, thrd_b);
  }
  thrd_switch(thrd_a, main_thrd); // hand back when done
}

static void func_b(void) {
  for (int i = 0; i < 3; ++i) {
    printf("B: %d\n", i);
    thrd_switch(thrd_b, thrd_a);
  }
  thrd_switch(thrd_b, main_thrd);
}

int main(void) {
  main_thrd = tcb_init(NULL); // placeholder for "where are we now"
  thrd_a    = tcb_init(func_a);
  thrd_b    = tcb_init(func_b);

  thrd_switch(main_thrd, thrd_a); // kickstart the threads
  printf("done\n");
}
```

To compile and run this demo, use:
```bash
cmake -B build && cmake --build build && ./build/demo
```

The expected output is:
```
A: 0
B: 0
A: 1
B: 1
A: 2
B: 2
done
```

After the loop, each thread explicitly switches back to `main`.
Without this, control would fall off the end of the function and segfault, because we don't have a way to clean up a finished thread implicitly yet.

Manual `thrd_switch` calls are the rawest possible form of cooperative scheduling.
The next section will introduce `thrd_yield`, which delegates the decision of who runs next to the scheduler.

**[<| prev: Build setup](../section0/README.md)** | **[next: Cooperative scheduling |>](tutorial/section2/README.md)**
