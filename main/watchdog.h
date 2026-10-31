/*
 * Task watchdog: a hung task restarts the unit instead of leaving it dead.
 *
 * Only the tasks that subscribe are watched, so long running flash or network
 * operations in other tasks cannot trip it.  A timeout triggers a panic
 * (backtrace in the log) and a restart - the reset reason is then
 * "task watchdog" in the log of the next boot.
 */

#pragma once

/** Reconfigure the task watchdog to 60 s, panic instead of hanging.  Call once, early. */
void watchdog_init(void);

/** Watch the calling task.  It must then call watchdog_feed() at least every 60 s. */
void watchdog_subscribe(void);

/** Stop watching the calling task (before it deletes itself). */
void watchdog_unsubscribe(void);

/** Report that the calling task is alive. */
void watchdog_feed(void);
