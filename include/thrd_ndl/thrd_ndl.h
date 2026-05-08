#ifndef THRD_NDL_H
#define THRD_NDL_H

#include <stdnoreturn.h>

noreturn void thrd_exit(void);

void thrd_yield(void);

void thrd_init(void);

int thrd_create(void (*func)(void));

#endif
