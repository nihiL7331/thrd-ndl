#ifndef THRD_NDL_H
#define THRD_NDL_H

// return codes
#define THRD_SUCCESS 0
#define THRD_EINVAL  1
#define THRD_ENOMEM  2

typedef void* thrd_t;

thrd_t tcb_init(void (*entry)(void));

int    thrd_init(void);
void   thrd_yield(void);

#endif // THRD_NDL_H
