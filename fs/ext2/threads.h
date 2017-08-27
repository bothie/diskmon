#ifndef THREADS_H
#define THREADS_H

#include "conf.h"

#include "btthread.h"

#if ALLOW_CONCURRENT_TABLE_READER

#include <btlock.h>

#define TRE_WAIT() if (sc->allow_concurrent_table_reader) do { \
	btlock_wait(sc->table_reader_empty); \
} while (0)

#define TRE_WAKE() if (sc->allow_concurrent_table_reader) do { \
	btlock_wake(sc->table_reader_empty); \
} while (0)

#define TRF_WAIT() if (sc->allow_concurrent_table_reader) do { \
	btlock_wait(sc->table_reader_full); \
} while (0)

#define TRF_WAKE() if (sc->allow_concurrent_table_reader) do { \
	btlock_wake(sc->table_reader_full); \
} while (0)

THREAD_RETURN_TYPE ext2_read_tables(THREAD_ARGUMENT_TYPE arg);

#else // #if ALLOW_CONCURRENT_TABLE_READER

#define TRE_WAIT() do {} while (0)
#define TRE_WAKE() do {} while (0)
#define TRF_WAIT() do {} while (0)
#define TRF_WAKE() do {} while (0)

#endif // #if ALLOW_CONCURRENT_TABLE_READER, else

struct thread {
#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
	struct thread * volatile next;
	struct thread * volatile prev;
	struct btlock_lock * lock;
	struct thread_ctx * thread_ctx;
#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
	struct inode_scan_context * isc;
};

#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION

extern struct thread * thread_head;
extern struct thread * thread_tail;

#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION

extern volatile unsigned live_child;

extern unsigned hard_child_limit;

#endif // #ifndef THREADS_H
