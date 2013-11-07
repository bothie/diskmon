#define _GNU_SOURCE

#include "btthread.h"

#include <btmacros.h>
#include <pthread.h>

struct thread_ctx * create_thread(size_t min_stack_size, THREAD_RETURN_TYPE (*thread_function)(THREAD_ARGUMENT_TYPE), THREAD_ARGUMENT_TYPE arg) {
	ignore(min_stack_size);
	
	RETVAL(struct thread_ctx);
	
	if (pthread_create(
		&retval->thread,
		NULL,
		thread_function,
		arg
	)) {
		free(retval);
		retval = NULL;
	}
	
	return retval;
}

bool terminated_thread(struct thread_ctx * thread_ctx) {
	return !pthread_tryjoin_np(thread_ctx->thread, NULL);
}

void cleanup_thread(struct thread_ctx * thread_ctx) {
	free(thread_ctx);
}
