			+--------------------+
			|        CS 140      |
			| PROJECT 1: THREADS |
			|   DESIGN DOCUMENT  |
			+--------------------+
				   
---- GROUP ----

Celia Hough <chough42@tntech.edu>
Michael Clinton <miclinton42@tntech.edu>
Deanna Sola <ddsola43@tntech.edu>
Koby Stovall <kstovall43@tntech.edu>

---- PRELIMINARIES ----

We implemented the Alarm Clock (non-busy sleep), Priority Scheduling, and Priority Donation (including nested donations up to a limited depth). We did not implement the Advanced Scheduler (MLFQS). No external sources beyond Pintos documentation and course materials were used.

			     ALARM CLOCK
			     ===========

---- DATA STRUCTURES ----

A1: New/changed declarations and purpose
----------------------------------------
/* additions to struct thread (threads/thread.h) */
int64_t wake_time;        /* Tick when thread should be awakened. */
struct list_elem sleep_elem; /* List element for sleep_list insertion. */

/* global/static variables in devices/timer.c */
static struct list sleep_list; /* Ordered list of sleeping threads by wake_time. */

/* Explanation (each ≤ 25 words): 
 wake_time: stores absolute tick to wake the thread.
 sleep_elem: allows insertion into sleep_list.
 sleep_list: global ordered list; front has smallest wake_time. */

---- ALGORITHMS ----

A2: timer_sleep() behavior
--------------------------
1. Caller computes wake_time = timer_ticks() + ticks_to_sleep.
2. Disable interrupts to atomically insert into sleep_list.
3. Insert current thread into sleep_list ordered by wake_time (earliest first).
4. Set thread status to BLOCKED and call thread_block().
5. Re-enable interrupts (after block).
6. Timer interrupt increments global ticks; on each tick it checks sleep_list front.
7. For each thread with wake_time <= ticks, pop from sleep_list and thread_unblock() it.

Effect of timer interrupt handler:
- Increments ticks.
- Calls thread_tick() for scheduler tick accounting.
- Wakes threads from sleep_list whose wake_time has arrived by unblocking them.

A3: Minimizing time in interrupt handler
----------------------------------------
- sleep_list is ordered by wake_time; handler inspects only the front element(s).
- Handler wakes only threads whose wake_time <= current ticks (likely few per tick).
- No traversal of entire thread set; only repeated pop_front operations until the front's wake_time > ticks.
- All insertion heavy work (ordered insert) happens in thread context, not interrupt context.
- Interrupt handler keeps operations minimal: increment tick, call thread_tick(), loop only while front ready.

---- SYNCHRONIZATION ----

A4: Race avoidance for concurrent timer_sleep() calls
----------------------------------------------------
- timer_sleep() disables interrupts (intr_disable()) while computing wake_time and inserting into sleep_list; insertion is atomic with respect to timer interrupt.
- sleep_list is the single global data structure; protected via interrupt disable rather than a lock to avoid sleeping while holding lock.
- Because we only disable interrupts for the short critical section (compute + ordered insert), contention window is minimal.

A5: Race avoidance when timer interrupt occurs during timer_sleep()
------------------------------------------------------------------
- timer_sleep() disables interrupts while inserting; timer interrupt cannot preempt during that section.
- After insertion, the thread calls thread_block(). If the timer interrupt fires right after re-enabling interrupts but before thread_block(), the thread may be unblocked immediately; thread_block() then will observe its status as blocked and continue correctly.
- Ordering correctness relies on: insertion into sleep_list happens-before thread_block(); the timer tick handler checks sleep_list entries, so a thread whose wake_time <= ticks will either be woken by the handler (if inserted earlier) or remain blocked until next tick.

---- RATIONALE ----

A6: Design choice and advantages
--------------------------------
- We chose an ordered sleep_list with per-thread wake_time because it minimizes work in the interrupt handler (constant-time check of front) and avoids busy waiting.
- Alternative (non-ordered list + scanning) would make the interrupt handler O(n) per tick; this design keeps interrupt work proportional to threads expiring on that tick, which is typically small.
- Using interrupt-disable for short critical sections avoids complicated locking with sleeping (locks that block cannot be safely held in interrupt handlers).
- Overall, it is simple, efficient, and fits project requirements (no busy-waiting).

			 PRIORITY SCHEDULING
			 ===================

---- DATA STRUCTURES ----

B1: New/changed declarations and purpose
---------------------------------------
/* additions to struct thread (threads/thread.h) */
int base_priority;            /* Original priority set by thread_set_priority(). */
int priority;                 /* Effective priority, including donations. */
struct list donations;        /* List of threads that donated priority to this thread. */
struct lock *waiting_on;      /* Lock the thread is currently waiting to acquire. */
struct list_elem donation_elem; /* For linking this thread in another's donation list. */

/* changes in ready list management (threads/thread.c) */
static struct list ready_list; /* Ready threads inserted sorted by effective priority. */

/* changes in struct lock (threads/synch.h) */
struct thread *holder;        /* Thread currently holding the lock. */

/* Explanation (≤ 25 words each):
 base_priority: preserved original priority for restoration after donations.
 priority: effective priority used for scheduling comparisons.
 donations: tracks donors so we can restore priorities correctly.
 waiting_on: identifies lock that triggered donations.
 ready_list: ready queue sorted by priority.
 lock->holder: identifies lock owner for donation. */

>> B2: Data structure of priority donation (diagram)
---------------------------------------------------
- Each thread keeps a list `donations` of threads that have donated to it.
- A thread that cannot acquire a lock sets `waiting_on` to that lock and donates upward.

Example nested donation scenario (ASCII):
    H (prio 60) waits on lock L1 held by M (prio 30),
    M waits on lock L2 held by L (prio 10).

Before donation:
    H: base 60 waiting_on -> L1
    M: base 30 waiting_on -> L2 holder-> M
    L: base 10 holder of L2

Donation propagation:
    H (60) -> donate to M: M.priority becomes 60; M.donations contains H
    M (now 60) -> donate to L: L.priority becomes 60; L.donations contains M (or propagated)

ASCII diagram:
    H(wait L1) --donates--> M(holder L1, wait L2) --donates--> L(holder L2)
    donations lists:
      M.donations: [H]
      L.donations: [M]  (transitively refl
