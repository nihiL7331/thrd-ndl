#include <thrd_ndl/thrd_ndl.h>
#include <stdlib.h>
#include <assert.h>

#define TARGET_CNT 10000

volatile int buff_has_data = 0;
volatile int total_consumed = 0;

mutex_t shared_mut = {0};
cond_t not_empty = {0};
cond_t not_full = {0};

void prod_task(void) {
  for (int i = 0; i < TARGET_CNT; ++i) {
    mutex_lock(&shared_mut);

    while (buff_has_data == 1)
      cond_wait(&not_full, &shared_mut);

    buff_has_data = 1;
    cond_signal(&not_empty);
    
    mutex_unlock(&shared_mut);
    thrd_yield();
  }
}

void cons_task(void) {
  for (int i = 0; i < TARGET_CNT; ++i) {
    mutex_lock(&shared_mut);

    while (buff_has_data == 0)
      cond_wait(&not_empty, &shared_mut);

    buff_has_data = 0;
    total_consumed++;
    cond_signal(&not_full);

    mutex_unlock(&shared_mut);
    thrd_yield();
  }
}

int main(void) {
  thrd_init();

  thrd_t prod_thrd, cons_thrd;

  int err1 = thrd_create(&prod_thrd, prod_task);
  int err2 = thrd_create(&cons_thrd, cons_task);

  assert(err1 == THRD_SUCCESS && "failed to create producer");
  assert(err2 == THRD_SUCCESS && "failed to create consumer");

  thrd_join(prod_thrd);
  thrd_join(cons_thrd);

  assert(total_consumed == TARGET_CNT && "consumer failed to process all items");
  assert(buff_has_data == 0 && "buffer not empty at exit");
}
