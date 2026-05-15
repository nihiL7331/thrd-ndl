#include <stdio.h>
#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <assert.h>

volatile int keep_run = 1;
volatile long long thrd_a_ticks = 0;
volatile long long thrd_b_ticks = 0;

void task_a(void) {
  while (keep_run)
    thrd_a_ticks++;
}

void task_b(void) {
  while (keep_run)
    thrd_b_ticks++;
}

int main(void) {
  assert(thrd_init() == THRD_SUCCESS && "failed to main initialize");

  thrd_t thrd_a, thrd_b;

  int err1 = thrd_create(&thrd_a, task_a);
  int err2 = thrd_create(&thrd_b, task_b);
  assert(err1 == THRD_SUCCESS && "failed to create thread A");
  assert(err2 == THRD_SUCCESS && "failed to create thread B");

  thrd_sleep(1000);

  keep_run = 0;

  thrd_join(thrd_a);
  thrd_join(thrd_b);

  assert(thrd_a_ticks > 0 && "thread A was completely starved");
  assert(thrd_b_ticks > 0 && "thread B was completely starved");

  double ratio = (double)thrd_a_ticks / (double)thrd_b_ticks;
  printf("%.2f\n", ratio);
  assert(ratio > 0.5 && ratio < 2.0 && "preemption fair check failed");
}
