#ifndef THRD_NDL_H
#define THRD_NDL_H

#include <stdnoreturn.h> // include this for 'noreturn'

// return codes
#define THRD_SUCCESS 0
#define THRD_EINVAL  1
#define THRD_ENOMEM  2
#define THRD_EUNINIT 3
#define THRD_EBUSY   4

typedef void* thrd_t;

typedef struct {
  thrd_t owner;         // 'NULL' when no owner, otherwise the holding thread
  thrd_t wait_queue_hd; // head of threads parked on this mutex
  thrd_t wait_queue_tl; // tail of threads parked on this mutex
} mutex_t;

typedef struct {
  thrd_t wait_queue_hd; // head of threads parked on this condition
  thrd_t wait_queue_tl; // tail of threads parked on this condition
} cond_t;

int           thrd_init(void);
void          thrd_yield(void);
int           thrd_create(thrd_t* out_thrd, void (*entry)(void));
noreturn void thrd_exit(void);
int           thrd_join(thrd_t thrd);

int mutex_init(mutex_t* mutex);
int mutex_trylock(mutex_t* mutex);
int mutex_lock(mutex_t* mutex);
int mutex_unlock(mutex_t* mutex);

int cond_init(cond_t* cond);
int cond_wait(cond_t* cond, mutex_t* mutex);
int cond_signal(cond_t* cond);
int cond_bcast(cond_t* cond);

#endif // THRD_NDL_H
