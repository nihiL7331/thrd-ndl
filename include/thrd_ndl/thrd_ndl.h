#ifndef THRD_NDL_H
#define THRD_NDL_H

#include <stdint.h>

// return codes
#define THRD_SUCCESS     0
#define THRD_ENOMEM      1
#define THRD_EBUSY       2
#define THRD_EINVAL      3
#define THRD_EPOOL_SMALL 4
#define THRD_EUNINIT     5

typedef void* thrd_t;

typedef struct {
  int is_locked;
  void* wait_queue_hd;
  void* wait_queue_tl;
} mutex_t;

typedef struct {
  void* block_queue_hd;
  void* block_queue_tl;
} cond_t;

void thrd_exit(void);
void thrd_yield(void);
void thrd_init(void);
int  thrd_create(thrd_t* out_thread, void (*func)(void));
void thrd_join(thrd_t thread);
void thrd_sleep(uint64_t time_ms);
void thrd_dump(void);

void mutex_lock(mutex_t* mutex);
void mutex_unlock(mutex_t* mutex);
int  mutex_trylock(mutex_t* mutex);

void cond_wait(cond_t* cond, mutex_t* mutex);
void cond_signal(cond_t* cond);
void cond_bcast(cond_t* cond);

#endif
