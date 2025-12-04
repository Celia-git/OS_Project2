#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Sets semaphore SEMA to initial VALUE.
   Semaphore supports atomic down(P) and up(V) operations. */
void
sema_init (struct semaphore *sema, unsigned value)
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Performs down(P) on SEMA: waits until value > 0, then decrements.
   Cannot be called from interrupt context. */
void
sema_down (struct semaphore *sema)
{
  enum intr_level prev_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  prev_level = intr_disable ();
  while (sema->value == 0)
  {
    list_insert_ordered (&sema->waiters, &thread_current ()->elem, compare_priority, NULL);
    thread_block ();
  }
  sema->value--;
  intr_set_level (prev_level);
}

/* Non-blocking down(P) attempt on SEMA. Returns true if successful.
   Safe for interrupt handlers. */
bool
sema_try_down (struct semaphore *sema)
{
  enum intr_level prev_level;
  bool result;

  ASSERT (sema != NULL);

  prev_level = intr_disable ();
  if (sema->value > 0)
  {
    sema->value--;
    result = true;
  }
  else
    result = false;
  intr_set_level (prev_level);

  return result;
}

/* Performs up(V) on SEMA: increments value and wakes highest-priority waiter.
   Safe for interrupt handlers. */
void
sema_up (struct semaphore *sema)
{
  enum intr_level prev_level;

  ASSERT (sema != NULL);

  prev_level = intr_disable ();
  if (!list_empty (&sema->waiters))
  {
    list_sort (&sema->waiters, compare_priority, NULL);
    thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
  }
  sema->value++;
  thread_yield ();
  intr_set_level (prev_level);
}

static void sema_test_func (void *sema_ptr);

/* Tests semaphores with ping-pong between two threads. */
void
sema_self_test (void)
{
  struct semaphore sema_pair[2];
  int idx;

  printf ("Semaphore test running...");
  sema_init (&sema_pair[0], 0);
  sema_init (&sema_pair[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_func, &sema_pair);
  for (idx = 0; idx < 10; idx++)
  {
    sema_up (&sema_pair[0]);
    sema_down (&sema_pair[1]);
  }
  printf ("completed.\n");
}

/* Helper thread for semaphore self-test. */
static void
sema_test_func (void *sema_ptr)
{
  struct semaphore *sema = sema_ptr;
  int idx;

  for (idx = 0; idx < 10; idx++)
  {
    sema_down (&sema[0]);
    sema_up (&sema[1]);
  }
}

/* Initializes LOCK as specialized semaphore (value=1, single owner). */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
  lock->is_donated = false;
}

/* Acquires LOCK with priority inheritance if needed.
   Current thread must not already hold it; no interrupt context. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  if (lock->holder != NULL)
  {
    struct thread *curr_thread = thread_current ();
    curr_thread->waiting_for = lock;
    if (lock->holder->priority < curr_thread->priority)
    {
      struct thread *chain = curr_thread;
      while (chain->waiting_for != NULL)
      {
        struct lock *current_lock = chain->waiting_for;
        current_lock->holder->priorities[current_lock->holder->size] = chain->priority;
        current_lock->holder->size++;
        current_lock->holder->priority = chain->priority;
        if (current_lock->holder->status == THREAD_READY)
          break;
        chain = current_lock->holder;
      }
      if (!lock->is_donated)
        lock->holder->donation_no++;
      lock->is_donated = true;
      sort_ready_list ();
    }
  }
  sema_down (&lock->semaphore);
  lock->holder = thread_current ();
  lock->holder->waiting_for = NULL;
}

/* Attempts non-blocking acquire of LOCK. Returns success flag. */
bool
lock_try_acquire (struct lock *lock)
{
  bool result;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  result = sema_try_down (&lock->semaphore);
  if (result)
    lock->holder = thread_current ();
  return result;
}

/* Releases LOCK (must be held by current thread) and restores priorities. */
void
lock_release (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct semaphore *lock_sema = &lock->semaphore;
  list_sort (&lock_sema->waiters, compare_priority, NULL);

  if (lock->is_donated)
  {
    thread_current ()->donation_no--;
    int target_prio = list_entry (list_front (&lock_sema->waiters), struct thread, elem)->priority;
    search_array (thread_current (), target_prio);
    thread_current ()->priority = thread_current ()->priorities[(thread_current ()->size) - 1];
    lock->is_donated = false;
  }
  if (thread_current ()->donation_no == 0)
  {
    thread_current ()->size = 1;
    thread_current ()->priority = thread_current ()->priorities[0];
  }
  lock->holder = NULL;
  sema_up (&lock->semaphore);
}

/* Checks if current thread owns LOCK. */
bool
lock_held_by_current_thread (const struct lock *lock)
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* Semaphore wrapper for condition variable waiters. */
struct semaphore_elem
{
  struct list_elem elem;
  struct semaphore semaphore;
};

/* Initializes condition variable COND. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK, waits on COND, reacquires LOCK (Mesa-style). */
void
cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem semaphore_node;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&semaphore_node.semaphore, 0);
  list_insert_ordered (&cond->waiters, &semaphore_node.elem, compare_priority, NULL);
  lock_release (lock);
  sema_down (&semaphore_node.semaphore);
  lock_acquire (lock);
}

/* Signals highest-priority waiter on COND (LOCK held). */
void
cond_signal (struct condition *cond, struct lock *lock)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  list_sort (&cond->waiters, compare_sema, NULL);
  if (!list_empty (&cond->waiters))
    sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
}

/* Broadcasts to all waiters on COND (LOCK held). */
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

/* Compares semaphores by priority of first waiter. */
bool compare_sema (struct list_elem *e1, struct list_elem *e2, void *aux)
{
  struct semaphore_elem *se1 = list_entry (e1, struct semaphore_elem, elem);
  struct semaphore_elem *se2 = list_entry (e2, struct semaphore_elem, elem);
  struct semaphore *sem1 = &se1->semaphore;
  struct semaphore *sem2 = &se2->semaphore;

  return list_entry (list_front (&sem1->waiters), struct thread, elem)->priority >
         list_entry (list_front (&sem2->waiters), struct thread, elem)->priority;
}
