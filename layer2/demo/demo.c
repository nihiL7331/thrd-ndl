#include <thrd_ndl/thrd_ndl.h>
#include "scheduler.h"
#include <stdio.h>

static int completed = 0;

static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    printf("A: %d\n", i);
    thrd_yield();
  }
  completed++;

  while (1)
    thrd_yield(); // can't return yet - no 'thrd_exit'
}

static void func_b(void) {
  for (int i = 0; i < 3; ++i) {
    printf("B: %d\n", i);
    thrd_yield();
  }
  completed++;

  while(1)
    thrd_yield();
}

int main(void) {
  if (thrd_init() != THRD_SUCCESS)
    return 1;

  thrd_t thrd_a = tcb_init(func_a);
  thrd_t thrd_b = tcb_init(func_b);
  if (thrd_a == NULL || thrd_b == NULL)
    return 1;

  thrd_register(thrd_a);
  thrd_register(thrd_b);

  while (completed < 2)
    thrd_yield();

  printf("done\n");
}
