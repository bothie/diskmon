#ifndef BTTHREAD_PTHREAD_H
#define BTTHREAD_PTHREAD_H

#include <pthread.h>

#define THREAD_ARGUMENT_TYPE void *
#define THREAD_RETURN_TYPE void * 
#define THREAD_RETURN() do { return NULL; } while (0)

static inline int gettid() {
	return pthread_self();
}

#define COMPILER_BARRIER() __asm__ __volatile__ ("" ::: "memory")

#define GENERAL_BARRIER() do { \
	COMPILER_BARRIER(); \
	__sync_synchronize(); \
} while(0)

struct thread_ctx {
	pthread_t thread;
};

#endif // #ifndef BTTHREAD_PTHREAD_H
