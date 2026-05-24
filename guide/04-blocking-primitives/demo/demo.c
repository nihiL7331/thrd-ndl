#include <stdio.h>
#include <thrd_ndl/thrd_ndl.h>

static mtx_t  mtx;
static cond_t cond;

static int curr_turn = 0;

static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    mtx_lock(&mtx);

    while (curr_turn != 0)
      cond_wait(&cond, &mtx);

    printf("A: %d\n", i);
    curr_turn = 1;

    cond_signal(&cond);
    mtx_unlock(&mtx);
  }
}

static void func_b(void) {
  for (int i = 0; i < 3; ++i) {
    mtx_lock(&mtx);

    while (curr_turn != 1)
      cond_wait(&cond, &mtx);

    printf("B: %d\n", i);
    curr_turn = 0;

    cond_signal(&cond);
    mtx_unlock(&mtx);
  }
}

int main(void) {
  if (thrd_init() != THRD_SUCCESS)
    return 1;

  mtx_init(&mtx);
  cond_init(&cond);

  thrd_t thrd_a, thrd_b;
  if (thrd_create(&thrd_a, func_a) != THRD_SUCCESS)
    return 1;
  if (thrd_create(&thrd_b, func_b) != THRD_SUCCESS)
    return 1;

  thrd_join(thrd_a);
  thrd_join(thrd_b);

  printf("done\n");
}
