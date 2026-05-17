#ifndef THRD_NDL_H
#define THRD_NDL_H

#include <stdnoreturn.h> // include this for 'noreturn'

// return codes
#define THRD_SUCCESS 0
#define THRD_EINVAL  1
#define THRD_ENOMEM  2
#define THRD_EUNINIT 3

typedef void* thrd_t;

int           thrd_init(void);
void          thrd_yield(void);
int           thrd_create(thrd_t* out_thread, void (*entry)(void));
noreturn void thrd_exit(void);

#endif // THRD_NDL_H
