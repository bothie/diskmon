#ifdef BTCLONE

#include <linux/sched.h>
extern int clone(int (*)(void *),void *,int,void *,...);
#define THREAD_RETURN_TYPE int
#define THREAD_RETURN() do { return 0; } while (0)

int btclone(void * * stack_memory,int (*thread_main)(void * arg),size_t stack_size,int flags,void * arg);

#else // #ifdef BTCLONE

#include <pthread.h>
#define THREAD_RETURN_TYPE void * 
#define THREAD_RETURN() do { return NULL; } while (0)

#endif // #ifdef BTCLONE, else

#define COMPILER_BARRIER() __asm__ __volatile__ ("" ::: "memory")

#define GENERAL_BARRIER() do { \
	COMPILER_BARRIER(); \
	__sync_synchronize(); \
} while(0)

#include <sys/syscall.h>
#include <unistd.h>

static inline int gettid() {
	return syscall(SYS_gettid);
}
