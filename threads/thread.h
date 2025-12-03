#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* Maximum number of nested priority donations */
#define MAX_DONATION_DEPTH 8

/* A kernel thread or user process. */
struct thread
  {
    /* Owned by thread.c. */

    /* Alarm clock: tick at which this thread should wake up
       if it is currently sleeping. */
    int64_t wakeup_tick;

    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */

    /* Scheduling priority (top of donation stack). */
    int priority;

    /* Base priority before any donations (stored at priorities[0]). */
    int base_priority;

    /* Priority donation stack for nested donations */
    int priorities[MAX_DONATION_DEPTH];

    /* Number of active donations (stack height) */
    int donation_no;

    /* Current stack size (initially 1 for base priority) */
    int size;

    /* List element for all threads list. */
    struct list_elem allelem;

    /* Shared: List element used for ready_list, semaphore waiters, condvar waiters, or sleep_list */
    struct list_elem elem;

    /* Lock this thread is waiting for (renamed to waiting_for) */
    struct lock *waiting_for;

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

/* Alarm clock helpers */
void thread_sleep_until (int64_t wake_tick);
void thread_wake_sleeping (int64_t now);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

/* Priority and scheduler interface. */
int  thread_get_priority (void);
void thread_set_priority (int);

/* Other scheduler functions */
int  thread_get_nice (void);
void thread_set_nice (int);
int  thread_get_recent_cpu (void);
int  thread_get_load_avg (void);

/* Priority comparator for lists (ready_list, waiters, etc.). */
bool thread_priority_greater (const struct list_elem *a,
                              const struct list_elem *b,
                              void *aux);

#endif /* threads/thread.h */
