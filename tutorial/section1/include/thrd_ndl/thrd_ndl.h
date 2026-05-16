#ifndef THRD_NDL_H
#define THRD_NDL_H

typedef void* thrd_t;

thrd_t      tcb_init(void (*entry)(void));
extern void thrd_switch(thrd_t old_tcb, thrd_t new_tcb);

#endif // THRD_NDL_H
