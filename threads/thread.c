#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

#define THREAD_MAGIC 0xcd6abf4b

/* Queue of ready-to-run threads. */
static struct list run_queue;

/* Complete list of active threads. */
static struct list active_threads;

/* Idle thread reference. */
static struct thread *idle_thr;

/* Main thread reference. */
static struct thread *main_thr;

/* TID allocation lock. */
static struct lock tid_mutex;

/* Kernel thread stack frame layout. */
struct kernel_thread_frame 
  {
    void *ret_addr;             /* Return address. */
    thread_func *target;        /* Thread entry point. */
    void *data;                 /* Passed parameter. */
  };

/* Timing counters. */
static long long idle_time;     /* Idle CPU ticks. */
static long long kernel_time;   /* Kernel thread ticks. */
static long long user_time;     /* User program ticks. */

/* Round-robin scheduling. */
#define TIME_SLICE 4
static unsigned tick_count;

/* MLFQS scheduling mode flag. */
bool thread_mlfqs;

static void kernel_thread_entry (thread_func *, void *data);
static void idle_loop (void *data UNUSED);
static struct thread *current_thread (void);
static struct thread *select_next_thread (void);
static void setup_thread (struct thread *, const char *name, int priority);
static bool validate_thread (struct thread *) UNUSED;
static void *stack_frame (struct thread *, size_t size);
static void scheduler (void);
void thread_schedule_tail (struct thread *prev);
static tid_t generate_tid (void);

/* Converts running code to thread, initializes queues and TID lock. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_mutex);
  list_init (&run_queue);
  list_init (&active_threads);

  main_thr = current_thread ();
  setup_thread (main_thr, "main", PRI_DEFAULT);
  main_thr->status = THREAD_RUNNING;
  main_thr->tid = generate_tid ();
}

/* Enables interrupts and starts idle thread. */
void
thread_start (void) 
{
  struct semaphore idle_ready;
  sema_init (&idle_ready, 0);
  thread_create ("idle", PRI_MIN, idle_loop, &idle_ready);

  intr_enable ();
  sema_down (&idle_ready);
}

/* Timer tick handler - updates stats and enforces preemption. */
void
thread_tick (void) 
{
  struct thread *curr = thread_current ();

  if (curr == idle_thr)
    idle_time++;
#ifdef USERPROG
  else if (curr->pagedir != NULL)
    user_time++;
#endif
  else
    kernel_time++;

  if (++tick_count >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Displays thread timing statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_time, kernel_time, user_time);
}

/* Creates kernel thread with given parameters, adds to run queue. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *data) 
{
  struct thread *new_thr;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t new_tid;

  ASSERT (function != NULL);

  new_thr = palloc_get_page (PAL_ZERO);
  if (new_thr == NULL)
    return TID_ERROR;

  setup_thread (new_thr, name, priority);
  new_tid = new_thr->tid = generate_tid ();

  kf = stack_frame (new_thr, sizeof *kf);
  kf->ret_addr = NULL;
  kf->target = function;
  kf->data = data;

  ef = stack_frame (new_thr, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread_entry;

  sf = stack_frame (new_thr, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  thread_unblock (new_thr);
  thread_yield ();

  return new_tid;
}

/* Blocks current thread until unblocked. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  scheduler ();
}

/* Moves blocked thread T to ready state. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level prev_level;

  ASSERT (validate_thread (t));

  prev_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered (&run_queue, &t->elem, compare_priority, NULL);
  t->status = THREAD_READY;
  intr_set_level (prev_level);
}

/* Returns current thread name. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns current running thread with validation. */
struct thread *
thread_current (void) 
{
  struct thread *t = current_thread ();
  
  ASSERT (validate_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns current thread ID. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Terminates current thread. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  scheduler ();
  NOT_REACHED ();
}

/* Yields CPU voluntarily. */
void
thread_yield (void) 
{
  struct thread *curr = thread_current ();
  enum intr_level prev_level;
  
  ASSERT (!intr_context ());

  prev_level = intr_disable ();
  if (curr != idle_thr)
    list_insert_ordered (&run_queue, &curr->elem, compare_priority, NULL);
  curr->status = THREAD_READY;
  scheduler ();
  intr_set_level (prev_level);
}

/* Applies function to all threads. */
void
thread_foreach (thread_action_func *func, void *data)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&active_threads); e != list_end (&active_threads);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, data);
    }
}

/* Updates current thread priority. */
void
thread_set_priority (int new_priority) 
{
  thread_current()->priorities[0] = new_priority;
  if (thread_current()->size == 1)
  { 
    thread_current ()->priority = new_priority;
    thread_yield ();
  }
}

/* Gets current thread priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets nice value (stub). */
void
thread_set_nice (int nice UNUSED) 
{
}

/* Gets nice value (stub). */
int
thread_get_nice (void) 
{
  return 0;
}

/* Gets load average (stub). */
int
thread_get_load_avg (void) 
{
  return 0;
}

/* Gets recent CPU (stub). */
int
thread_get_recent_cpu (void) 
{
  return 0;
}

/* Idle thread main loop. */
static void
idle_loop (void *idle_ready_)
{
  struct semaphore *idle_ready = idle_ready_;
  idle_thr = thread_current ();
  sema_up (idle_ready);

  for (;;) 
    {
      intr_disable ();
      thread_block ();

      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Kernel thread wrapper. */
static void
kernel_thread_entry (thread_func *function, void *data) 
{
  ASSERT (function != NULL);

  intr_enable ();
  function (data);
  thread_exit ();
}

/* Gets current thread from stack pointer. */
struct thread *
current_thread (void) 
{
  uint32_t *esp;
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Validates thread structure. */
static bool
validate_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Basic thread setup. */
static void
setup_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level prev_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
 
  t->priorities[0] = priority;
  t->donation_no = 0;
  t->size = 1;
  t->magic = THREAD_MAGIC;
  t->waiting_for = NULL;
  prev_level = intr_disable ();
  list_push_back (&active_threads, &t->allelem);
  intr_set_level (prev_level);
}

/* Allocates stack frame. */
static void *
stack_frame (struct thread *t, size_t size) 
{
  ASSERT (validate_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Selects next runnable thread. */
static struct thread *
select_next_thread (void) 
{
  if (list_empty (&run_queue))
    return idle_thr;
  return list_entry (list_pop_front (&run_queue), struct thread, elem);
}

/* Thread switch completion handler. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *curr = current_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  curr->status = THREAD_RUNNING;
  tick_count = 0;

#ifdef USERPROG
  process_activate ();
#endif

  if (prev != NULL && prev->status == THREAD_DYING && prev != main_thr) 
    {
      ASSERT (prev != curr);
      palloc_free_page (prev);
    }
}

/* Core scheduler function. */
static void
scheduler (void) 
{
  struct thread *curr = current_thread ();
  struct thread *next_thr = select_next_thread ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (curr->status != THREAD_RUNNING);
  ASSERT (validate_thread (next_thr));

  if (curr != next_thr)
    prev = switch_threads (curr, next_thr);
  thread_schedule_tail (prev);
}

/* Generates unique TID. */
static tid_t
generate_tid (void) 
{
  static tid_t next_id = 1;
  tid_t id;

  lock_acquire (&tid_mutex);
  id = next_id++;
  lock_release (&tid_mutex);

  return id;
}

uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/* Priority comparison for list ordering. */
bool compare_priority (struct list_elem *e1, struct list_elem *e2, void *aux)
{ 
  struct thread *thr1 = list_entry (e1, struct thread, elem);
  struct thread *thr2 = list_entry (e2, struct thread, elem);
  return thr1->priority > thr2->priority;
}

/* Sorts ready queue by priority. */
void sort_ready_list (void)
{
  list_sort (&run_queue, compare_priority, NULL);
}

/* Removes priority from donation stack. */
void search_array (struct thread *curr_thr, int target_prio)
{ 
  int found = 0;
  for (int i = 0; i < (curr_thr->size) - 1; i++)
  {
    if (curr_thr->priorities[i] == target_prio)
      found = 1;
    if (found == 1)
      curr_thr->priorities[i] = curr_thr->priorities[i + 1];
  }
  curr_thr->size--;
}
