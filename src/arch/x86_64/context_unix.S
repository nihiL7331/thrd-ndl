  .text
  .align 16
  .global thrd_switch
  .type thrd_switch, @function

 /* void thrd_switch(tcb_t* old_tcb, tcb_t* new_tcb)
  * 
  * this function is responsible for storing the callee-saved
  * registers data in the tcb_t (old_tcb) struct, as well as the stack pointer.
  * 
  * it then loads the stack pointer from 'new_tcb',
  * pops the registers from `new_tcb` back onto the cpu,
  * and call 'ret' to start the thread
  */

thrd_switch:
  /* push callee-saved registers onto the current stack */
  pushq %rbx
  pushq %rbp
  pushq %r12
  pushq %r13
  pushq %r14
  pushq %r15

  /* save the current stack pointer into old_tcb */
  /* old_tcb->rsp is at offset 0 */
  movq %rsp, (%rdi)

  /* load the new stack pointer */
  movq (%rsi), %rsp

  /* pop the registers (reverse order to push) */
  popq %r15
  popq %r14
  popq %r13
  popq %r12
  popq %rbp
  popq %rbx

  /* jump to new thread */
  ret

.size thrd_switch, . - thrd_switch

#if defined(__linux__) && defined(__ELF__)
  .section .note.GNU-stack,"",%progbits
#endif
