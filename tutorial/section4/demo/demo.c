#include <stdio.h>
#include <thrd_ndl/thrd_ndl.h>

static mutex_t mutex;
static cond_t  cond;

static int curr_turn = 0;

static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    mutex_lock(&mutex);

    while (curr_turn != 0)
      cond_wait(&cond, &mutex);

    printf("A: %d\n", i);
    curr_turn = 1;

    cond_signal(&cond);
    mutex_unlock(&mutex);
  }
}

static void func_b(void) {
  for (int i = 0; i < 3; ++i) {
    mutex_lock(&mutex);

    while (curr_turn != 1)
      cond_wait(&cond, &mutex);

    printf("B: %d\n", i);
    curr_turn = 0;

    cond_signal(&cond);
    mutex_unlock(&mutex);
  }
}

int main(void) {
  if (thrd_init() != THRD_SUCCESS)
    return 1;

  mutex_init(&mutex);
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
