#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#define DONATION_MAX_DEPTH 8	/* Max depth for nested donation chain */

/* Initializes semaphore SEMA to VALUE. */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level = intr_disable ();

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  while (sema->value == 0) 
  {
    list_insert_ordered (&sema->waiters, &thread_current ()->elem,
                         compare_priority, 0);
    thread_block ();
  }
  sema->value--;
  intr_set_level (old_level);
}

/* Try to down semaphore without sleeping. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
  {
    sema->value--;
    success = true; 
  }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level = intr_disable ();

  ASSERT (sema != NULL);

  if (!list_empty (&sema->waiters)) 
  {
    list_sort (&sema->waiters, compare_priority, 0);
    struct list_elem *e = list_pop_front (&sema->waiters);
    struct thread *t = list_entry (e, struct thread, elem);
    thread_unblock (t);
  }
  sema->value++;

  /* Preempt if a higher priority thread was unblocked and not in interrupt */
  if (!intr_context ())
    thread_yield ();

  intr_set_level (old_level);
}

/* Return the highest-priority thread waiting on SEMA, or NULL if none. */
static struct thread *
semaphore_max_waiter (struct semaphore *sema)
{
  if (list_empty (&sema->waiters))
    return NULL;

  struct list_elem *e = list_front (&sema->waiters);
  return list_entry (e, struct thread, elem);
}

static bool
cond_sema_priority_greater (const struct list_elem *a,
                            const struct list_elem *b,
                            void *aux UNUSED)
{
  const struct semaphore_elem *sa = list_entry (a, struct semaphore_elem, elem);
  const struct semaphore_elem *sb = list_entry (b, struct semaphore_elem, elem);
  struct thread *ta = semaphore_max_waiter (&sa->semaphore);
  struct thread *tb = semaphore_max_waiter (&sb->semaphore);
  return ta->priority > tb->priority;
}

/* Helper to remove a notification from donation stack and update priority */
void
search_array(struct thread *cur, int elem)
{
  int found = 0;
  for (int i = 0; i < (cur->size) - 1; i++)
  {
    if (cur->priorities[i] == elem)
    {
      found = 1;
    }
    if (found == 1)
    {
      cur->priorities[i] = cur->priorities[i + 1];
    }
  }
  cur->size -= 1;
}

/* Sorts ready_list by priority */
void sort_ready_list(void)
{
  list_sort(&ready_list, compare_priority, 0);
}

/* Initializes LOCK. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
  lock->is_donated = false;
}

/* Acquires LOCK, applying priority donation if necessary. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread *cur = thread_current ();

  if (lock->holder != NULL)
  {
    cur->waiting_for= lock;

    if (lock->holder->priority < cur->priority)
    {
      struct thread *temp = cur;
      int depth = 0;
      while(temp->waiting_for != NULL && depth < DONATION_MAX_DEPTH)
      {
        struct lock *cur_lock = temp->waiting_for;
        struct thread *holder = cur_lock->holder;
        if (holder->priority >= temp->priority)
          break;

        // Push donation priority to holder's stack
        holder->priorities[holder->size++] = temp->priority;
        holder->priority = temp->priority;

        if (holder->status == THREAD_READY)
          break;

        temp = holder;
        depth++;
      }
      if(!lock->is_donated)
      {
        lock->is_donated = true;
        lock->holder->donation_no += 1;
      }
      sort_ready_list();
    }
  }

  sema_down (&lock->semaphore);

  lock->holder = cur;
  lock->holder->waiting_for = NULL;
}

/* Tries to acquire LOCK without sleeping. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK and adjusts priority donation stack. */
void
lock_release (struct lock *lock) 
{ 
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct semaphore *lock_sema = &lock->semaphore;

  list_sort(&lock_sema->waiters, compare_priority, 0);

  if (lock->is_donated)
  {
    thread_current()->donation_no -= 1;

    if (!list_empty(&lock_sema->waiters))
    {
      int elem = list_entry (list_front (&lock_sema->waiters), struct thread, elem)->priority;
      search_array(thread_current(), elem);
    }

    thread_current()->priority = thread_current()->priorities[(thread_current()->size) - 1];
    lock->is_donated = false;
  }

  if (thread_current()->donation_no == 0)
  {
    thread_current()->size = 1;
    thread_current()->priority = thread_current()->priorities[0];
  }

  lock->holder = NULL;
  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK. */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* Initializes condition variable COND. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Waits on condition variable COND, releasing LOCK. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  list_insert_ordered (&cond->waiters, &waiter.elem, cond_sema_priority_greater, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* Signals one thread waiting on condition variable COND. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  list_sort(&cond->waiters, cond_sema_priority_greater, 0);
  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Broadcasts to all threads waiting on COND. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

/* Comparator for priorities used in ready_list and semaphore waiters */
bool compare_priority(struct list_elem *l1, struct list_elem *l2, void *aux UNUSED)
{
  struct thread *t1 = list_entry(l1, struct thread, elem);
  struct thread *t2 = list_entry(l2, struct thread, elem);
  return t1->priority > t2->priority;
}
