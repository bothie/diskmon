#ifndef FUTEX_H
#define FUTEX_H

#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>

static inline int sys_futex(int * addr1,int op,int val1,const struct timespec * timeout,int * addr2,int val2) {
	return syscall(SYS_futex,addr1,op,val1,timeout,addr2,val2);
}

/*
 * Do the following atomically:
 * if (*uaddr == val) {
 * 	wait for any other thread or process to call futex_wake(uaddr,>=1).
 * }
 */
static inline int futex_wait(volatile int * addr,int cmp) {
	return sys_futex((int*)addr,FUTEX_WAIT,cmp,NULL,NULL,0);
}
static inline int futex_wait_timeout(volatile int * addr,int cmp,const struct timespec * timeout) {
	return sys_futex((int*)addr,FUTEX_WAIT,cmp,timeout,NULL,0);
}

/*
 * Wake at most val threads/processes waiting inside futex_wait on the same uaddr.
 */
static inline int futex_wake(volatile int * addr,int wake_max) {
	return sys_futex((int*)addr,FUTEX_WAKE,wake_max,NULL,NULL,0);
}

/*
 * No longer present starting with Linux 2.6.26
 * 
 * Associate a file descriptor (FIXME: Which?) with a futex (?uaddr?). If 
 * another thread/process calls futex_wake, the process (?which?) will 
 * receive the signal number that was passed in val (?of THIS call or of the 
 * call to futex_wake?). The calling process must close the returned file 
 * descriptor after use.
 * 
 * This operation was removed from Linux 2.6.26 onwards because it was 
 * inherently racy.
 */
static inline int futex_fd(volatile int * addr,int val) {
	return sys_futex((int*)addr,FUTEX_FD,val,NULL,NULL,0);
}

/*
 * Since Linux 2.5.70
 * 
 * Wakes up at most val threads/processes like futex_wake would do, but 
 * requeue any other waiting thread/process to the new futex at address 
 * new_addr.
 */
static inline int futex_requeue(volatile int * addr,int wake_max,volatile int * new_addr) {
	return sys_futex((int*)addr,FUTEX_REQUEUE,wake_max,NULL,(int*)new_addr,0);
}

/*
 * Since Linux 2.6.7
 * 
 * There was a race in the intended use of futex_requeue. To fix that race, 
 * this version was added. It fails if *addr != cmp with error EAGAIN. If 
 * *addr == cmp, it behaves exactly as futex_requeue.
 */
static inline int futex_cmp_requeue(volatile int * addr,int cmp,int wake_max,volatile int * new_addr) {
	return sys_futex((int*)addr,FUTEX_CMP_REQUEUE,wake_max,NULL,(int*)new_addr,cmp);
}

#endif // #ifndef FUTEX_H
