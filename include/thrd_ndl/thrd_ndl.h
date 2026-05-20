#ifndef THRD_NDL_H
#define THRD_NDL_H

#include <stdint.h>
#include <stdnoreturn.h>

// return codes
#define THRD_SUCCESS     0
#define THRD_ENOMEM      1
#define THRD_EBUSY       2
#define THRD_EINVAL      3
#define THRD_EPOOL_SMALL 4
#define THRD_EUNINIT     5

typedef void* thrd_t;

typedef struct {
  thrd_t owner;
  thrd_t wait_queue_hd;
  thrd_t wait_queue_tl;
} mutex_t;

typedef struct {
  thrd_t wait_queue_hd;
  thrd_t wait_queue_tl;
} cond_t;

noreturn void thrd_exit(void);
void thrd_yield(void);
int  thrd_init(void);
int  thrd_create(thrd_t* out_thread, void (*func)(void));
int  thrd_join(thrd_t thread);
void thrd_sleep(uint64_t time_ms);
void thrd_dump(void);

int mutex_init(mutex_t* mutex);
int mutex_lock(mutex_t* mutex);
int mutex_unlock(mutex_t* mutex);
int mutex_trylock(mutex_t* mutex);

int cond_init(cond_t* cond);
int cond_wait(cond_t* cond, mutex_t* mutex);
int cond_signal(cond_t* cond);
int cond_bcast(cond_t* cond);

#endif
