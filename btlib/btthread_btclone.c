#include "btthread.h"

#include <btmacros.h>
// #include <sys/types.h>
// #include <signal.h>

int btclone(void * * stack_memory,int (*thread_main)(void * arg),size_t stack_size,int flags,void * arg) {
	*stack_memory=malloc(stack_size);
	void * stack_argument;
	if (unlikely(!*stack_memory)) {
		return -1;
	}
	
	if (stack_grows_down(&stack_argument)) {
		stack_argument=(char *)*stack_memory+stack_size;
	} else {
		stack_argument=*stack_memory;
	}
	
	int retval=clone(thread_main,stack_argument,flags,arg);
	
	if (likely(retval>=0)) {
		return retval;
	}
	
	free(*stack_memory);
	*stack_memory=NULL;
	return retval;
}

struct thread_ctx * create_thread(size_t min_stack_size, THREAD_RETURN_TYPE (*thread_function)(THREAD_ARGUMENT_TYPE), THREAD_ARGUMENT_TYPE arg) {
	RETVAL(struct thread_ctx);
	
	retval->tid = btclone(
		&retval->stack_memory,
		thread_function,
		min_stack_size,
		CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD,
		arg
	);
	if (retval->tid <= 0) {
		free(retval);
		retval = NULL;
	}
	
	return retval;
}

bool terminated_thread(struct thread_ctx * thread_ctx) {
	return tgkill(getpid(), thread_ctx->tid, 0);
}

void cleanup_thread(struct thread_ctx * thread_ctx) {
	free(thread_ctx->stack_memory);
	free(thread_ctx);
}
