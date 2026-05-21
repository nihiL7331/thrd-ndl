#include <stdio.h>
#include <thrd_ndl/thrd_ndl.h>

static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    printf("A: %d\n", i);
    thrd_sleep(200);
  }
}

static void func_b(void) {
  for (int i = 0; i < 6; ++i) {
    printf("B: %d\n", i);
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

  thrd_join(thrd_a);
  thrd_join(thrd_b);

  printf("done\n");
}
