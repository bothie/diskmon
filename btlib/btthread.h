#ifndef BTTHREAD_H
#define BTTHREAD_H

#if !THREADS
#	include "btthread_nothreads.h"

#else // #if !THREADS
#	if BTCLONE
#		include "btthread_btclone.h"
#	else // #if BTCLONE
#		include "btthread_pthread.h"
#	endif // #if BTCLONE, else

#include <stdbool.h>

// #include <stdlib.h>

struct thread_ctx * create_thread(size_t min_stack_size, THREAD_RETURN_TYPE (*thread_function)(THREAD_ARGUMENT_TYPE), THREAD_ARGUMENT_TYPE arg);
void cleanup_thread(struct thread_ctx * thread_ctx);
bool terminated_thread(struct thread_ctx * thread_ctx);

#endif // #if !THREADS, else

#endif // #ifndef BTTHREAD_H
