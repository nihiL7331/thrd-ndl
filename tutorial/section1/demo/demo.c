#include <thrd_ndl/thrd_ndl.h>
#include <stdio.h>

static thrd_t main_thrd, thrd_a, thrd_b;

static void func_a(void) {
  for (int i = 0; i < 3; ++i) {
    printf("A: %d\n", i);
    thrd_switch(thrd_a, thrd_b);
  }
  thrd_switch(thrd_a, main_thrd); // hand back when done
}

static void func_b(void) {
  for (int i = 0; i < 3; ++i) {
    printf("B: %d\n", i);
    thrd_switch(thrd_b, thrd_a);
  }
  thrd_switch(thrd_b, main_thrd);
}

int main(void) {
  main_thrd = tcb_init(NULL); // placeholder for "where are we now"
  thrd_a    = tcb_init(func_a);
  thrd_b    = tcb_init(func_b);

  thrd_switch(main_thrd, thrd_a); // kickstart the threads
  printf("done\n");
}
