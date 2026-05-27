#ifdef __APPLE__
  #define SYM_SWITCH _thrd_switch
  #define SYM_TRAMP  _thrd_tramp
#else
  #define SYM_SWITCH thrd_switch
  #define SYM_TRAMP  thrd_tramp
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

  .global SYM_TRAMP

SYM_TRAMP:
  br x19
