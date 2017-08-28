/*
 * btlock.h. Part of the bothie-utils.
 *
 * Copyright (C) 2008-2015 Bodo Thiesen <bothie@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef BTLOCK_H
#define BTLOCK_H

struct btlock_lock;

struct btlock_lock * btlock_lock_mk();
void btlock_lock_free(struct btlock_lock * lock);

void btlock_lock(struct btlock_lock * lock);
void btlock_unlock(struct btlock_lock * lock);

/*
 * @function wait
 * 
 * @desc
 * Causes the calling thread to wait until another thread calls wake with the 
 * same argument.
 * Any wake calle after the previous call to wait will make the next wait to 
 * not wait at all to prevent race-conditions which make a thread wait forever.
 * So, wait MAY return immediatelly, but IF it waits, it's guaranteed that - 
 * provided that the thread is written properly - no other thread is waiting for 
 * this thread to do some work any longer.
 * It is ok to have more than one thread calling wait on the same argument, 
 * however only one thread will be woken up at any call of wake, so if you call 
 * wake by two threads this may cause only ONE or BOTH threads to wake up 
 * (depends on scheduling and stuff - here you have a race-condition again, so 
 * take care of that.)
 * Because of other possible races, wait is not guaranteed to wait in any 
 * single case in which it COULD wait, however it will generally do it's job.
 * However your thread must count on the chance to be woken up without any work 
 * to be done.
 * 
 * @example:
 * int worker_thread(struct * lock) {
 * 	do {
 * 		// so some work
 *		unlock(some_lock); // This actually wakes up other_thread
 * 		wait(lock_for_worker_thread);
 * 	} while (running);
 * }
 * 
 * int other_thread(struct * lock_for_worker_thread) {
 * 	// schedule some work to be done by worker_thread
 * 	lock(some_lock); // this lock shall be unlocked by the worker_thread upon completion of the request
 * 	wake(lock_for_worker_thread);
 * 	lock(some_lock); // This will block until worker_thread unlocked this lock
 * 	// use the result created by the worker thread.
 * }
 * 
 * @see wake
 */
void btlock_wait(struct btlock_lock * lock);

/*
 * @function wake
 * 
 * @desc
 * Wakes a thread which has called wait on the same arg before or will make 
 * sure that the next thread, which calls wait with the same arg will return 
 * immediatelly.
 * 
 * @example
 * See function wait for an example
 * 
 * @see wait
 */
void btlock_wake(struct btlock_lock * lock);

// bool try_lock(struct btlock_lock * lock);

#endif // #ifndef BTLOCK_H
