#include <stdio.h>
#include <thrd_ndl/thrd_ndl.h>

// the 'greedy' thread
static void func_a(void) {
  printf("starting an infinite loop\n");
  volatile int counter = 0;
  while (1) {
    counter++;
  }
}

// the 'polite' thread
static void func_b(void) {
  for (int i = 0; i < 3; ++i) {
    printf("polite thread running\n");
    thrd_sleep(100);
  }
}

int main(void) {
  if (thrd_init() != THRD_SUCCESS)
    return 1;

  thrd_t thrd_a, thrd_b;
  if (thrd_create(&thrd_a, func_a) != THRD_SUCCESS)
    return 1;
  if (thrd_create(&thrd_b, func_b) != THRD_SUCCESS)
    return 1;

  thrd_join(thrd_b); // only waiting for the 'polite' thread

  printf("done\n");
}
