#ifndef BTATOMIC_H
#define BTATOMIC_H

#include <stdbool.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
typedef volatile int atomic_t;

/*
 * atomic_get - get the value of the atomic variable
 * @a: pointer of type atomic_t
 */
static inline int atomic_get(const atomic_t * a) { return *a; }

/*
 * atomic_set - set the value of the atomic variable
 * @a: pointer of type atomic_t
 * @v: new value
 */
static inline void atomic_set(atomic_t * a,int v) { *a=v; }

/*
 * atomic_inc - Atomically do: !++*a
 * @a: pointer of type atomic_t
 */
static inline bool atomic_inc(atomic_t * a) {
	unsigned char retval;
	
	asm volatile(
		"lock; incl %0; setz %1"
		:"+m"(*a)
		,"=qm"(retval)
	);
	
	return retval;
}

/*
 * atomic_dec_c - Atomically do: !*a--
 * @a: pointer of type atomic_t
 */
static inline bool atomic_dec_c(atomic_t * a) {
	unsigned char retval;
	
	asm volatile(
		"lock; subl $1,%0; setc %1"
		:"+m"(*a)
		,"=qm"(retval)
	);
	
	return retval;
}

/*
 * atomic_dec_z - Atomically do: !--*a
 * @a: pointer of type atomic_t
 */
static inline bool atomic_dec_z(atomic_t * a) {
	unsigned char retval;
	
	asm volatile(
		"lock; decl %0; setz %1"
		:"+m"(*a)
		,"=qm"(retval)
	);
	
	return retval;
}

/*
 * atomic_dec - Atomically do: --*a (without returning a truth value)
 * @a: pointer of type atomic_t
 */
static inline void atomic_dec(atomic_t * a) {
	asm volatile(
		"lock; decl %0"
		:"+m"(*a)
	);
}

static inline int atomic_cmpxchg(atomic_t * a,int ov,int nv) {
	int retval;
	asm volatile (
		"lock; cmpxchgl %1,%2"
		:"=a"(retval)
		:"r"(nv)
		,"m"(*a)
		,"0"(ov)
		:"memory"
	);
	return retval;
}

#endif // #ifdef ATOMIC_H
