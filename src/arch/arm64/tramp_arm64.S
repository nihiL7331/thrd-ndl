#ifdef __APPLE__
  #define SYM_TRAMP _thrd_tramp
#else
  #define SYM_TRAMP thrd_tramp
#endif

  .text
  .align 4
  .global SYM_TRAMP

SYM_TRAMP:
  br x19
