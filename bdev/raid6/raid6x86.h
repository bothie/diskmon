/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#ifndef RAID6X86_H
#define RAID6X86_H

#if defined(__i386__) || defined(__x86_64__)

typedef struct {
	unsigned int fsave[27];
} raid6_mmx_save_t __attribute__((aligned(16)));

static inline void raid6_before_mmx(raid6_mmx_save_t * s) {
	asm volatile("fsave %0 ; fwait" : "=m" (s->fsave[0]));
}

static inline void raid6_after_mmx(raid6_mmx_save_t * s) {
	asm volatile("frstor %0" : : "m" (s->fsave[0]));
}

#endif // #if defined(__i386__) || defined(__x86_64__)
#endif // #ifndef RAID6X86_H
