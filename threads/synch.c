/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#define DONATION_MAX_DEPTH 8   /* Max depth for nested donation chain */

/* Initializes semaphore SEMA to VALUE. */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);
  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level = intr_disable ();

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  while (sema->value == 0) 
    {
      list_insert_ordered (&sema->waiters, 
                           &thread_current ()->elem,
                           thread_priority_greater,
                           NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Try down. */
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

/* Up or "V" operation. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level = intr_disable ();

  ASSERT (sema != NULL);

  if (!list_empty (&sema->waiters))
    {
      list_sort (&sema->waiters, thread_priority_greater, NULL);
      struct list_elem *e = list_pop_front (&sema->waiters);
      struct thread *t = list_entry (e, struct thread, elem);
      thread_unblock (t);
    }

  sema->value++;

  if (!intr_context ())
    thread_yield ();

  intr_set_level (old_level);
}

/* Returns highest waiter thread for a semaphore. */
static struct thread *
semaphore_max_waiter (struct semaphore *sema)
{
  if (list_empty (&sema->waiters))
    return NULL;

  struct list_elem *e = list_front (&sema->waiters);
  return list_entry (e, struct thread, elem);
}

/* Comparator for condition-variable waiter list. */
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

/* Initializes a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
  list_init (&lock->elem);
}

/* Correct priority donation chain (recursive up to 8). */
static void
donate_priority_chain (struct thread *donor, struct lock *lock)
{
  int depth = 0;

  while (lock != NULL && lock->holder != NULL && depth < DONATION_MAX_DEPTH)
    {
      struct thread *holder = lock->holder;

      if (holder->priority >= donor->priority)
        break;

      holder->priority = donor->priority;

      lock = holder->waiting_lock;
      depth++;
    }
}

/* Acquire lock with donation. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread *cur = thread_current();

  if (lock->holder != NULL)
    {
      cur->waiting_lock = lock;
      donate_priority_chain (cur, lock);
    }

  sema_down (&lock->semaphore);

  cur->waiting_lock = NULL;
  lock->holder = cur;
  list_push_back (&cur->held_locks, &lock->elem);

  thread_update_priority (cur);
}

/* Try acquire. */
bool
lock_try_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  bool success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Correct lock release: NO interrupts disabled, NO page faults. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct thread *cur = thread_current();

  list_remove (&lock->elem);
  lock->holder = NULL;

  thread_update_priority (cur);

  sema_up (&lock->semaphore);

  thread_yield();
}

/* Does current thread hold lock? */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);
  return lock->holder == thread_current ();
}

/* Condition variable init. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);
  list_init (&cond->waiters);
}

/* cond_wait: releases lock, waits, reacquires */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);
  list_insert_ordered (&cond->waiters, &waiter.elem,
                       cond_sema_priority_greater, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* cond_signal: wake one */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (!intr_context ());

  if (!list_empty (&cond->waiters))
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* cond_broadcast: wake all */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

