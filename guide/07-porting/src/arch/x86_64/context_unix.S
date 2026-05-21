  .text
  .align 16
  .global thrd_switch
  .type thrd_switch, @function

thrd_switch:
  /* push callee-saved registers onto the current stack */
  push %rbx
  push %rbp
  push %r12
  push %r13
  push %r14
  push %r15

  /* save the current stack pointer into old_tcb */
  /* old_tcb->rsp is at offset 0 */
  mov %rsp, (%rdi)

  /* load the new stack pointer */
  mov (%rsi), %rsp

  /* pop the registers (reverse order to push) */
  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %rbp
  pop %rbx

  /* jump to new thread (instruction pointer is handled implicitly!) */
  ret

.size thrd_switch, . - thrd_switch

#if defined(__linux__) && defined(__ELF__)
  .section .note.GNU-stack,"",%progbits
#endif
