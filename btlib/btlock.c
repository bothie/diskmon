/*
 * btlock.c. Part of the bothie-utils.
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

#include "btlock.h"

#include "bterror.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef ATOMIC_OPS_IMPLEMENTATION

#include <atomic_ops.h>

struct btlock_lock {
	AO_t locked;
	int fd_writer_side;
	int fd_reader_side;
};

struct btlock_lock * btlock_lock_mk() {
	struct btlock_lock * retval;
	
	retval=malloc(sizeof(*retval));
	if (!retval) return NULL;
	
	int pipe_fds[2];
	
	if (pipe(pipe_fds)) {
		free(retval);
		return NULL;
	}
	
	AO_store_full(&retval->locked,0);
	retval->fd_writer_side=pipe_fds[1];
	retval->fd_reader_side=pipe_fds[0];
	
//	eprintf("lock_mk: Using FDs %i and %i\n",retval->fd_reader_side,retval->fd_writer_side);
	
	return retval;
}

void btlock_lock_free(struct btlock_lock * lock) {
	if (unlikely(AO_load_full(&lock->locked))) {
		eprintf("lock_free: Destroying a lock which is still in-use.");
		abort(); // FIXME: Make sure, all threads get stopped
	}
	
//	eprintf("lock_free: Freeing FDs %i and %i\n",lock->fd_reader_side,lock->fd_writer_side);
	
	close(lock->fd_writer_side);
	close(lock->fd_reader_side);
	free(lock);
}

void btlock_lock(struct btlock_lock * lock) {
	if (unlikely(AO_fetch_and_add1_full(&lock->locked))) {
		int r;
		do {
			char c;
			// select!
			r=read(lock->fd_reader_side,&c,1);
			if (r==-1 && errno==EAGAIN) r=0;
		} while (!r);
		if (r==-1) {
			eprintf("lock: While trying to read from pipe: %s\n",strerror(errno));
			abort(); // FIXME: Make sure, all threads get stopped
		}
	}
}

unsigned btlock_unlock(struct btlock_lock * lock) {
	unsigned retval=AO_fetch_and_sub1_full(&lock->locked)-1;
	if (likely(!retval)) {
		return retval;
	}
	
	char c=0;
	int w=write(lock->fd_writer_side,&c,1);
	if (w!=1) {
		eprintf("lock: While trying to write to pipe: %s\n",strerror(errno));
		abort(); // FIXME: Make sure, all threads get stopped
	}
	return retval;
}

void btlock_wait(struct btlock_lock * lock) {
	AO_store_full(&lock->locked,0);
	int r;
	do {
		char c;
		r=read(lock->fd_reader_side,&c,1);
		if (r==-1 && errno==EAGAIN) r=0;
	} while (!r);
	if (r==-1) {
		eprintf("wait: While trying to read from pipe: %s\n",strerror(errno));
		abort(); // FIXME: Make sure, all threads get stopped
	}
	AO_store_full(&lock->locked,0);
}

void btlock_wake(struct btlock_lock * lock) {
	if (AO_fetch_and_add1_full(&lock->locked)) {
		return;
	}
	char c=0;
	int w=write(lock->fd_writer_side,&c,1);
	if (w!=1) {
		eprintf("wake: While trying to write to pipe: %s\n",strerror(errno));
		abort(); // FIXME: Make sure, all threads get stopped
	}
}

#else // #ifdef ATOMIC_OPS_IMPLEMENTATION

#include "btatomic.h"
#include "futex.h"

#define TRACE 0

static inline int gettid() {
	return syscall(SYS_gettid);
}

struct btlock_lock {
	atomic_t locked;
	atomic_t num_waiters;
	int tid;
};

struct btlock_lock * btlock_lock_mk() {
	struct btlock_lock * retval;
	
	retval=malloc(sizeof(*retval));
	if (!retval) return NULL;
	
	atomic_set(&retval->locked,-1);
	retval->tid=0;
	retval->num_waiters=0;
	
	return retval;
}

void btlock_lock_free(struct btlock_lock * lock) {
	if (unlikely(-1!=atomic_get(&lock->locked))) {
		eprintf("lock_free: Destroying a lock which is still in-use (i.e. locked or being waited on).");
		abort(); // FIXME: Make sure, all threads get stopped
	}
	
	free(lock);
}

void btlock_lock(struct btlock_lock * lock) {
	if (lock->tid==gettid()) {
		if (TRACE) eprintf("btll in thread %5i: btlock_lock on %p: WARNING: This lock is already held by this thread, locking again WILL dead-lock if there is no other thread which will unlock it.\n",gettid(),(void*)lock);
	}
	
	if (TRACE) eprintf("btll in thread %5i: btlock_lock on %p: Trying to get lock\n",gettid(),(void*)lock);
	atomic_inc(&lock->num_waiters);
	for (;;) {
		if (likely(atomic_cmpxchg(&lock->locked,-1,0)==-1)) {
			atomic_dec_z(&lock->num_waiters);
			if (TRACE) eprintf("btll in thread %5i: btlock_lock on %p: Got lock\n",gettid(),(void*)lock);
			lock->tid=gettid();
			return;
		}
		if (TRACE) eprintf("btll in thread %5i: btlock_lock on %p: Waiting for lock to get released\n",gettid(),(void*)lock);
		futex_wait(&lock->locked,0);
	}
}

void btlock_unlock(struct btlock_lock * lock) {
	int tid=lock->tid;
	lock->tid=0;
	if (tid!=gettid()) {
		if (TRACE) eprintf("btll in thread %5i: btlock_unlock on %p: WARNING: This was locked by thread %5i (it will be unlocked as ordered now)\n",gettid(),(void*)lock,tid);
	}
	assert(0==atomic_cmpxchg(&lock->locked,0,-1));
	if (likely(atomic_get(&lock->num_waiters)==0)) {
		if (TRACE) eprintf("btll in thread %5i: btlock_unlock on %p: There was no one waiting for a lock\n",gettid(),(void*)lock);
		return;
	}
	int retval=futex_wake(&lock->locked,1);
	if (TRACE) eprintf("btll in thread %5i: btlock_unlock on %p: Woke %i waiters for this lock\n",gettid(),(void*)lock,retval);
}

/*
void old_and_broken_btlock_lock(struct btlock_lock * lock) {
	// True if -1 -> 0
	if (likely(atomic_inc(&lock->locked))) {
		if (TRACE) eprintf("btll in thread %5i: btlock_lock on %p: Got lock without waiting\n",gettid(),(void*)lock);
		lock->tid=gettid();
		return;
	}
	if (TRACE) eprintf("btll in thread %5i: btlock_lock on %p: Spin-waiting for lock, which is currently being held by thread %i ...\n",gettid(),(void*)lock,lock->tid);
	int val;
	for (;;) {
		int retval=futex_wait(&lock->locked,val=atomic_get(&lock->locked));
		char * r;
		switch (retval) {
			case 0: r="0"; break;
			case EINTR: r="EINTR"; break;
			case ETIMEDOUT: r="ETIMEDOUT"; break;
			case EAGAIN: r="EAGAIN"; break;
			default: r="???"; break;
		}
		if (TRACE) eprintf("btll in thread %5i: btlock_lock on %p: futex_wait()=%s\n",gettid(),(void*)lock,r);
		if (!retval) {
			break;
		}
	}
	if (TRACE) eprintf("btll in thread %5i: btlock_lock on %p: Got lock with value %i after spin-waiting for it.\n",gettid(),(void*)lock,val);
	lock->tid=gettid();
}

void old_and_broken_btlock_unlock(struct btlock_lock * lock) {
	int tid=lock->tid;
	lock->tid=0;
	if (tid!=gettid()) {
		if (TRACE) eprintf("btll in thread %5i: btlock_unlock on %p: WARNING: This was locked by thread %5i (it will be unlocked as ordered now)\n",gettid(),(void*)lock,tid);
	}
	if (likely(atomic_dec_c(&lock->locked))) {
		if (TRACE) eprintf("btll in thread %5i: btlock_unlock on %p: There was no one waiting for a lock\n",gettid(),(void*)lock);
		return;
	}
	int retval;
	do {
		retval=futex_wake(&lock->locked,1);
		if (!retval) {
			// if (TRACE) 
			eprintf("\033[31mbtll in thread %5i: btlock_unlock on %p: Woke %i waiters for this lock\033[0m\n",gettid(),(void*)lock,retval);
			sleep(1);
		} else {
			if (TRACE) eprintf("btll in thread %5i: btlock_unlock on %p: Woke %i waiters for this lock\n",gettid(),(void*)lock,retval);
		}
	} while (!retval);
}
*/

void btlock_wait(struct btlock_lock * lock) {
	if (TRACE) eprintf("btll in thread %5i: btlock_wait on %p: Issuing futex_wait if value is not -1 (value is %i currently)\n",gettid(),(void*)lock,atomic_get(&lock->locked));
	int retval=futex_wait(&lock->locked,-1);
	char * r;
	switch (retval) {
		case 0: r="0"; break;
		case EINTR: r="EINTR"; break;
		case ETIMEDOUT: r="ETIMEDOUT"; break;
		case EAGAIN: r="EAGAIN"; break;
		default: r="???"; break;
	}
	if (TRACE) eprintf("btll in thread %5i: btlock_wait on %p: futex_wait()=%s\n",gettid(),(void*)lock,r);
	if (TRACE) eprintf("btll in thread %5i: btlock_wait on %p: futex_wait returned, setting value to -1 now (before returning)\n",gettid(),(void*)lock);
	atomic_set(&lock->locked,-1);
}

void btlock_wake(struct btlock_lock * lock) {
	if (likely(atomic_inc(&lock->locked))) {
		int retval=futex_wake(&lock->locked,1);
		if (TRACE) eprintf("btll in thread %5i: btlock_wake on %p: Woke %i waiters for this waitqueue\n",gettid(),(void*)lock,retval);
		return;
	}
	if (TRACE) eprintf("btll in thread %5i: btlock_wake on %p: Waking up is already scheduled -> nothing to do\n",gettid(),(void*)lock);
}

#endif // #ifdef ATOMIC_OPS_IMPLEMENTATION, else
