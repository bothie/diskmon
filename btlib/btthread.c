#include "btthread.h"

#ifdef BTCLONE

int btclone(void * * stack_memory,int (*thread_main)(void * arg),size_t stack_size,int flags,void * arg) {
#if HAVE_LINUX_CLONE
	*stack_memory=malloc(stack_size);
	void * stack_argument;
	if (unlikely(!*stack_memory)) {
		return -1;
	}
	
	/*
	 * nice try, but doesn't work on amd64 with register argument passing ...
	 *
	 * So, just break the one platform that has stack growth in the "wrong" direction.
	 */
/*
	if ((char *)&stack_memory>(char *)&stack_argument) {
*/
		stack_argument=(char *)*stack_memory+stack_size;
/*
	} else {
		stack_argument=*stack_memory;
		assert(0);
	}
*/
	
	int retval=clone(thread_main,stack_argument,flags,arg);
	
	if (likely(retval>=0)) {
		return retval;
	}
	
	free(*stack_memory);
	*stack_memory=NULL;
	return retval;
#else // #if HAVE_LINUX_CLONE
	ignore(stack_memory);
	ignore(thread_main);
	ignore(stack_size);
	ignore(flags);
	ignore(arg);
	errno=ENOSYS;
	return -1;
#endif // #if HAVE_LINUX_CLONE, else
}

#endif // #ifdef BTCLONE
