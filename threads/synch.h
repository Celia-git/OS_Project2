#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H
#include <list.h>
#include <stdbool.h>

/* Semaphore with counter and waiter queue. */
struct semaphore 
  {
    unsigned value;             /* Current count. */
    struct list waiters;        /* Threads awaiting access. */
  };

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Mutual exclusion lock with owner tracking. */
struct lock 
  {
    struct thread *holder;      /* Current owner thread. */
    struct semaphore semaphore; /* Underlying binary semaphore. */
    bool is_donated;            /* Priority donation flag. */
  };

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable with waiter list. */
struct condition 
  {
    struct list waiters;        /* List of semaphore elements. */
  };

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

bool compare_sema(struct list_elem *e1, struct list_elem *e2, void *aux);

/* Prevents compiler reordering across this point. */
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
