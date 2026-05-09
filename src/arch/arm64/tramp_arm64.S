#ifdef __APPLE__
  #define SYM_TRAMP _thrd_tramp
  #define SYM_EXIT _thrd_exit
#else
  #define SYM_TRAMP thrd_tramp
  #define SYM_EXIT thrd_exit
#endif

  .text
  .align 4
  .global SYM_TRAMP

SYM_TRAMP:
  /* this procedure is repsonsible for
     ensuring that 'thrd_exit' is called
     when context switch finishes
     user's function pointer is in x19 */

  /* call the user's function
     'blr' sets x30 to the next line */
  blr x19

  /* when the user's function returns,
     catch it and kill the thread safely */
  bl SYM_EXIT
