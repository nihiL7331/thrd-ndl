#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <assert.h>

volatile int shared_cntr = 0;
mutex_t cntr_mutex = {0};

void inc_task(void) {
  for (int i = 0; i < 10000; ++i) {
    mutex_lock(&cntr_mutex);

    int local_val = shared_cntr;

    thrd_yield();

    local_val++;
    shared_cntr = local_val;

    mutex_unlock(&cntr_mutex);

    thrd_yield();
  }

  thrd_exit();
}

int main(void) {
  thrd_init();

  thrd_t t1, t2;

  int err1 = thrd_create(&t1, inc_task);
  int err2 = thrd_create(&t2, inc_task);

  assert(err1 == THRD_SUCCESS && "failed to create thrd 1");
  assert(err2 == THRD_SUCCESS && "failed to create thrd 2");

  thrd_join(t1);
  thrd_join(t2);

  assert(shared_cntr == 20000 && "mutex fail");
}
