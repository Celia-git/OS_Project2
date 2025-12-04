#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"

/* Thread lifecycle states. */
enum thread_status
  {
    THREAD_RUNNING,     /* Currently executing. */
    THREAD_READY,       /* Ready but not running. */
    THREAD_BLOCKED,     /* Awaiting event. */
    THREAD_DYING        /* Pending destruction. */
  };

/* Thread ID type. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)

/* Priority range. */
#define PRI_MIN 0
#define PRI_DEFAULT 31
#define PRI_MAX 63

/* Kernel thread structure with stack layout.

   Thread occupies bottom of 4kB page, kernel stack grows downward
   from top. Magic value detects stack overflow. */
struct thread
  {
    /* Thread management fields. */
    tid_t tid;                          /* Unique identifier. */
    enum thread_status status;          /* Current state. */
    char name[16];                      /* Debug name. */
    uint8_t *stack;                     /* Stack base pointer. */
    int priority;                       /* Scheduling priority. */
    int priorities[9];                  /* Priority inheritance stack. */
    int size;                           /* Stack size counter. */
    struct list_elem allelem;           /* All-threads list node. */
    int64_t wakeup_time;                /* Sleep wakeup timestamp. */
    int donation_no;                    /* Active donation count. */
    struct lock *waiting_for;           /* Lock being acquired. */
    
    /* Shared list element for ready queue or semaphore waiters. */
    struct list_elem elem;

#ifdef USERPROG
    /* User process page directory. */
    uint32_t *pagedir;
#endif

    /* Stack overflow detection. */
    unsigned magic;
  };

/* MLFQS scheduling flag. */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

bool compare_priority(struct list_elem *e1, struct list_elem *e2, void *aux);
void sort_ready_list(void);
void search_array(struct thread *t, int prio);

#endif /* threads/thread.h */
