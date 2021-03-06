/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#ifndef RAID6_H
#define RAID6_H

#include <stdbool.h>
#include <stdlib.h> // For size_t

#define NEED_LONG_LONG

#include <bttypes.h>

extern bool raid6_verify_parity;

struct raid6_calls {
	void (*gen_syndrome)(unsigned ndisks,size_t bytes,u8 * * ptrs);
	void (*recover)(unsigned ndisks,unsigned nfailed,unsigned * failmap,size_t bytes,u8 * * ptrs);
};

/*
 * Galois field tables
 * (defined in raid6tables.c)
 */
extern const u8 raid6_gfmul[256][256] __attribute__((aligned(256)));
extern const u8 raid6_gfexp[256]      __attribute__((aligned(256)));
extern const u8 raid6_gfinv[256]      __attribute__((aligned(256)));
extern const u8 raid6_gfexi[256]      __attribute__((aligned(256)));

#endif // #ifndef RAID6_H
