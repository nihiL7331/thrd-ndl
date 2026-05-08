#ifndef THRD_NDL_H
#define THRD_NDL_H

typedef void* thrd_t;

void thrd_exit(void);

void thrd_yield(void);

void thrd_init(void);

int thrd_create(thrd_t* out_thread, void (*func)(void));

void thrd_join(thrd_t thread);

#endif
