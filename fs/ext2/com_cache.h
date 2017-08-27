/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#ifndef COM_CACHE_H
#define COM_CACHE_H

#include "conf.h"

#include <bttypes.h>

struct scan_context;

struct com_cache_entry {
	struct com_cache_entry * next;
	struct com_cache_entry * prev;
	int num_in_use;
	
	struct btlock_lock * debug_lock;
	u32 * entry;
	size_t index;
	bool dirty;
};

struct com_cache {
	struct btlock_lock * debug_lock;
	u32 size;
	u32 clusters_per_entry;
	unsigned num_in_memory;
	struct com_cache_entry * * ccgroup;
	struct com_cache_entry * head;
	struct com_cache_entry * tail;
#if USE_COMC_THREAD_TO_READ
	struct com_cache_entry * read_head;
#endif // #if USE_COMC_THREAD_TO_READ
	struct com_cache_entry * read_tail;
	// 2^32*4/65536=256KB per cache entry
#if COMC_IN_MEMORY
	struct {
		u8 * ptr;
		size_t len;
	} * compr;
#endif // #if COMC_IN_MEMORY
	size_t cbuflen;
	u8 * cbuffer;
};

extern volatile bool exit_request_com_cache_thread;

struct com_cache_entry * get_com_cache_entry(struct scan_context * sc, size_t ccg, u64 cluster);
void release(struct scan_context * sc, struct com_cache_entry * ccge);
void com_cache_init(struct scan_context * sc, u64 num_clusters);
void com_cache_cleanup(struct scan_context * sc);

bool com_cache_work(struct scan_context * sc);

#if ALLOW_COM_CACHE_THREAD

#include "threads.h"

void shutdown_com_cache_thread(struct scan_context * sc);

THREAD_RETURN_TYPE com_cache_thread(THREAD_ARGUMENT_TYPE arg);

#endif // #if ALLOW_COM_CACHE_THREAD

#endif // #ifndef COM_CACHE_H
