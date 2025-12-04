#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdarg.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 8254 timer hardware specs in [8254]. */

#if TIMER_FREQ < 19
#error Minimum TIMER_FREQ is 19 for 8254
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ should not exceed 1000
#endif

/* Total timer interrupts since boot. */
static int64_t tick_count;

/* Iterations per interrupt cycle, set by calibration. */
static unsigned iter_per_tick;

/* Sorted list of threads awaiting wakeup by time/priority. */
static struct list wait_queue;

static intr_handler_func irq_handler;
static bool exceeds_tick (unsigned iterations);
static void spin_loop (int64_t iterations);
static void precise_sleep (int64_t numerator, int32_t divisor);
static void precise_wait (int64_t numerator, int32_t divisor);

/* Configures timer for TIMER_FREQ interrupts/sec and registers handler. */
void
timer_init (void)
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, irq_handler, "8254 Timer");
  list_init (&wait_queue);
}

/* Determines iter_per_tick for short delays via calibration. */
void
timer_calibrate (void)
{
  unsigned msb, bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Timer calibration in progress...  ");

  /* Start with largest power-of-two under one tick. */
  iter_per_tick = 1u << 10;
  while (!exceeds_tick (iter_per_tick << 1))
    {
      iter_per_tick <<= 1;
      ASSERT (iter_per_tick != 0);
    }

  /* Fine-tune lower 8 bits. */
  msb = iter_per_tick;
  for (bit = msb >> 1; bit != msb >> 10; bit >>= 1)
    if (!exceeds_tick (iter_per_tick | bit))
      iter_per_tick |= bit;

  printf ("%'"PRIu64" iterations/sec.\n", (uint64_t) iter_per_tick * TIMER_FREQ);
}

/* Gets boot-time tick count. */
int64_t
timer_ticks (void)
{
  enum intr_level prev_level = intr_disable ();
  int64_t count = tick_count;
  intr_set_level (prev_level);
  return count;
}

/* Computes ticks since prior call to timer_ticks(). */
int64_t
timer_elapsed (int64_t start_time)
{
  return timer_ticks () - start_time;
}

/* Blocks for ~TICKS ticks (requires interrupts enabled). */
void
timer_sleep (int64_t ticks)
{
  struct lock mutex;
  lock_init (&mutex);
  lock_acquire (&mutex);

  ASSERT (intr_get_level () == INTR_ON);
  struct thread *current = thread_current ();

  int64_t begin = timer_ticks ();
  current->wake_time = begin + ticks;
  list_insert_ordered (&wait_queue, &current->elem, prioritize_wake, NULL);

  enum intr_level prev_level = intr_disable ();
  thread_block ();
  intr_set_level (prev_level);

  lock_release (&mutex);
}

/* Blocks ~MS milliseconds (interrupts on). */
void
timer_msleep (int64_t ms)
{
  precise_sleep (ms, 1000);
}

/* Blocks ~US microseconds (interrupts on). */
void
timer_usleep (int64_t us)
{
  precise_sleep (us, 1000 * 1000);
}

/* Blocks ~NS nanoseconds (interrupts on). */
void
timer_nsleep (int64_t ns)
{
  precise_sleep (ns, 1000 * 1000 * 1000);
}

/* Spin-waits ~MS milliseconds (interrupts optional). */
void
timer_mdelay (int64_t ms)
{
  precise_wait (ms, 1000);
}

/* Spin-waits ~US microseconds (interrupts optional). */
void
timer_udelay (int64_t us)
{
  precise_wait (us, 1000 * 1000);
}

/* Spin-waits ~NS nanoseconds (interrupts optional). */
void
timer_ndelay (int64_t ns)
{
  precise_wait (ns, 1000 * 1000 * 1000);
}

/* Displays current tick count. */
void
timer_print_stats (void)
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Handles timer interrupts. */
static void
irq_handler (struct intr_frame *args UNUSED)
{
  tick_count++;
  wake_ready_threads ();
  thread_tick ();
}

/* Checks if ITERATIONS exceed one tick. */
static bool
exceeds_tick (unsigned iterations)
{
  /* Sync to next tick. */
  int64_t begin = tick_count;
  while (tick_count == begin)
    barrier ();

  /* Test loop duration. */
  begin = tick_count;
  spin_loop (iterations);

  barrier ();
  return begin != tick_count;
}

/* Simple loop for short delays (not inlined for timing consistency). */
static void NO_INLINE
spin_loop (int64_t iterations)
{
  while (iterations-- > 0)
    barrier ();
}

/* Blocks for NUM/DENOM seconds (interrupts enabled). */
static void
precise_sleep (int64_t num, int32_t denom)
{
  /* Ticks = NUM * TIMER_FREQ / DENOM (floored). */
  int64_t ticks_needed = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks_needed > 0)
    timer_sleep (ticks_needed);
  else
    precise_wait (num, denom);
}

/* Spin-waits NUM/DENOM seconds. */
static void
precise_wait (int64_t num, int32_t denom)
{
  ASSERT (denom % 1000 == 0);
  spin_loop (iter_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
}

/* Orders by wake time, then priority (higher first). */
bool prioritize_wake (struct list_elem *e1, struct list_elem *e2, void *aux UNUSED)
{
  struct thread *th1 = list_entry (e1, struct thread, elem);
  struct thread *th2 = list_entry (e2, struct thread, elem);

  if (th1->wake_time < th2->wake_time)
    return true;
  if (th1->wake_time == th2->wake_time && th1->priority > th2->priority)
    return true;
  return false;
}

/* Unblocks overdue threads from wait queue. */
void wake_ready_threads (void) {
  while (!list_empty (&wait_queue)) {
    struct list_elem *head = list_front (&wait_queue);
    struct thread *lead = list_entry (head, struct thread, elem);

    if (lead->wake_time <= tick_count) {
      list_remove (head);
      thread_unblock (lead);
    } else
      break;
  }
}
