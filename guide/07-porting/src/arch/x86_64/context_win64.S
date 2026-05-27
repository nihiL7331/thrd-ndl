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
