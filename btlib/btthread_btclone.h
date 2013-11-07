#ifndef BTTHREAD_BTCLONE_H
#define BTTHREAD_BTCLONE_H

#include <stdbool.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <linux/sched.h>

extern int clone(int (*)(void *),void *,int,void *,...);

#define THREAD_ARGUMENT_TYPE void *
#define THREAD_RETURN_TYPE int
#define THREAD_RETURN() do { return 0; } while (0)

int btclone(void * * stack_memory,THREAD_RETURN_TYPE (*thread_main)(THREAD_ARGUMENT_TYPE arg),size_t stack_size,int flags,void * arg);

static inline int tgkill(int tgid, int tid, int sig) {
	return syscall(SYS_tgkill, tgid, tid, sig);
}

#define COMPILER_BARRIER() __asm__ __volatile__ ("" ::: "memory")

#define GENERAL_BARRIER() do { \
	COMPILER_BARRIER(); \
	__sync_synchronize(); \
} while(0)

struct thread_ctx {
	void * stack_memory;
	pid_t tid;
};

bool stack_grows_down(void * ptr_to_var_on_callers_stack);

static inline int gettid() {
	return syscall(SYS_gettid);
}

#endif // #ifndef BTTHREAD_BTCLONE_H
