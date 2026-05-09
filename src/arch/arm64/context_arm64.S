  .text
  .align 4
  .global thrd_ndl_switch
  .global _thrd_ndl_switch /* macos compiler prefixes symbols with underscore */

thrd_ndl_switch:
_thrd_ndl_switch:
  /* push callee-saved registers onto the current stack */
  /* pre decrement sp by 16 */
  stp x19, x20, [sp, #-16]!
  stp x21, x22, [sp, #-16]!
  stp x23, x24, [sp, #-16]!
  stp x25, x26, [sp, #-16]!
  stp x27, x28, [sp, #-16]!
  stp x29, x30, [sp, #-16]!

  /* save the current stack pointer into old_tcb */
  /* old_tcb->rsp is at offset 0 */
  /* move to tmp register first */
  mov x9, sp
  str x9, [x0]

  /* load the new stack pointer */
  ldr x9, [x1]
  mov sp, x9

  /* pop the registers (reverse order to push) */
  /* post increment sp by 16 */
  ldp x29, x30, [sp], #16
  ldp x27, x28, [sp], #16
  ldp x25, x26, [sp], #16
  ldp x23, x24, [sp], #16
  ldp x21, x22, [sp], #16
  ldp x19, x20, [sp], #16

  /* jump to new thread */
  ret
