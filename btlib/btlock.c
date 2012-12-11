#include "btlock.h"

#include "bterror.h"

#include <alsa/iatomic.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct btlock_lock {
	atomic_t locked;
	atomic_t tried;
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
	
	atomic_set(&retval->locked,-1);
	atomic_set(&retval->tried,0);
	retval->fd_writer_side=pipe_fds[1];
	retval->fd_reader_side=pipe_fds[0];
	
//	set_nonblocking(retval->fd_reader_side);
	
//	eprintf("lock_mk: Using FDs %i and %i\n",retval->fd_reader_side,retval->fd_writer_side);
	
	return retval;
}

void btlock_lock(struct btlock_lock * lock) {
	if (unlikely(!atomic_inc_and_test(&lock->locked))) {
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

void btlock_unlock(struct btlock_lock * lock) {
	if (likely(atomic_add_negative(-1,&lock->locked))) {
		return;
	}
	
/*
	if (atomic_read(&retval->tried)) {
		bool zero;
		do {
			zero=atomic_dec_test(&retval->tried);
			if (atomic_add_negative(-1,&lock->locked)) return;
		} (zero);
	}
*/
	
	char c=0;
	int w=write(lock->fd_writer_side,&c,1);
	if (w!=1) {
		eprintf("lock: While trying to write to pipe: %s\n",strerror(errno));
		abort(); // FIXME: Make sure, all threads get stopped
	}
}

void btlock_lock_free(struct btlock_lock * lock) {
	if (unlikely(atomic_read(&lock->locked)!=-1)) {
		eprintf("lock_free: Destroying a lock which is still in-use.");
		abort(); // FIXME: Make sure, all threads get stopped
	}
	
//	eprintf("lock_free: Freeing FDs %i and %i\n",lock->fd_reader_side,lock->fd_writer_side);
	
	close(lock->fd_writer_side);
	close(lock->fd_reader_side);
	free(lock);
}

/*
bool try_lock(struct thread_lock * lock) {
	if (atomic_inc_and_test(&lock->locked)) {
		return true;
	}
	
	atomic_inc(&retval->tried);
	
	
	
	if (atomic_add_negative(-1,&lock->locked)) {
		
	}
	
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
*/
