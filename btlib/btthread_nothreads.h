#ifndef BTTHREAD_NOTHREADS_H
#define BTTHREAD_NOTHREADS_H

#define COMPILER_BARRIER() do {} while(0)
#define GENERAL_BARRIER() do {} while(0)

static inline int gettid() {
	return getpid();
}

#endif // #ifndef BTTHREAD_NOTHREADS_H
