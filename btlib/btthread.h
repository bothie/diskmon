#ifndef BTTHREAD_H
#define BTTHREAD_H

#ifndef THREADS
#	include "btthread_nothreads.h"

#else // #ifndef THREADS
#	ifdef BTCLONE
#		include "btthread_btclone.h"
#	else // #ifdef BTCLONE
#		include "btthread_pthread.h"
#	endif // #ifdef BTCLONE, else

#include <stdbool.h>

// #include <stdlib.h>

struct thread_ctx * create_thread(size_t min_stack_size, THREAD_RETURN_TYPE (*thread_function)(THREAD_ARGUMENT_TYPE), THREAD_ARGUMENT_TYPE arg);
void cleanup_thread(struct thread_ctx * thread_ctx);
bool terminated_thread(struct thread_ctx * thread_ctx);

#endif // #ifndef THREADS, else

#endif // #ifndef BTTHREAD_H
