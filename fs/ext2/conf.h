/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#ifndef CONF_H
#define CONF_H

/*
 * Is support for multithreading included?
 */
#if THREADS

/*
 * Debug locking by tracing any call to lock and unlock
 */
#define CC_DEBUG_LOCKS 0

/*
 * Multithreading support doesn't mean, you have to multithreading.
 * Shall multithreading be on by default?
 */
#define USE_THREADS_DEFAULT 1

/*
 * Allow concurrent chk_block function?
 * 
 * If true, chk_block functions will be processed by concurrent worker 
 * threads. In current implementation however, those threads will process 
 * one inode scan context, and once it's done, just take the next and go
 * on - this way not creating and quitting one thread for each inode scan
 * context.
 */
#define ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION 1

/*
 * If support for concurrent chk_block function is compiled in, shall it be 
 * used by default?
 */
#define USE_CONCURRENT_CHK_BLOCK_FUNCTION_DEFAULT true

/*
 * Allow concurrent group table reader?
 * 
 * If true, the group table reader will be run in a concurrent thread.
 * If false, one table is read at a time and it's inodes are processed then 
 * before reading the next table. Good for memory constrained systems.
 */
#define ALLOW_CONCURRENT_TABLE_READER 0

/*
 * If support for concurrent group table reader is compiled in, shall it be 
 * used by default?
 */
#define USE_CONCURRENT_TABLE_READER_DEFAULT true

/*
 * Allow concurrent com cache thread?
 * 
 * If true, an additional thread will to in background compression of dirty 
 * com cache entries.
 */
#define ALLOW_COM_CACHE_THREAD 0

/*
 * If support for concurrent com cache thread is compiled in, shall it be
 * used by default?
 */
#define USE_COM_CACHE_THREAD_DEFAULT true

/*
 * If true, use the com cache thread to decompress needed com cache entries.
 * If false, whoever needs the entry decompresses it himself.
 */
#define USE_COMC_THREAD_TO_READ 0

/*
 * 295 threads (plus two or three or so) is a limit of valgrind
 */

#define INIT_HARD_CHILD_LIMIT 294
#define MAX_INODE_TABLE_PREREADED 512
#define PREREADED_NOTIFYTIME 10
#define INODE_TABLE_ROUNDROUBIN 512

#if MAX_INODE_TABLE_PREREADED > INODE_TABLE_ROUNDROUBIN
#error MAX_INODE_TABLE_PREREADED must not be greater than INODE_TABLE_ROUNDROUBIN
#endif

#endif // #if THREADS

/*
 * Keep cluster owning map cache in memory. It will be compressed in blocks tho.
 */
#define COMC_IN_MEMORY 1

/*
 *
 */
// #define PRINT_MARK_SB0GAP 1

/*
 *
 */
// #define PRINT_MARK_SB 1

/*
 *
 */
// #define PRINT_MARK_GDT 1

/*
 *
 */
// #define PRINT_MARK_CAM 1

/*
 *
 */
// #define PRINT_MARK_IAM 1

/*
 *
 */
// #define PRINT_MARK_IT 1

/*
 * Switch on all of PRINT_MARK_SB0GAP, PRINT_MARK_SB, PRINT_MARK_GDT, 
 * PRINT_MARK_CAM, PRINT_MARK_IAM and PRINT_MARK_IT.
 * 
 * However, if you explicitly switch off any of them using
 * #define ... 0
 * they remain switched off.
 *
 * Not setting PRINT_MARK_ALL makes the 6 mentioned above to be switched
 * off, again, switching on explicitly cause them to remain switched on.
 */
#define PRINT_MARK_ALL 0

/*
 * With threads, you should let enough room for clean comc entries, so reader 
 * threads don't have to wait for dirty comc entries to get written out.
 */
// #define MAX_COMC_IN_MEMORY 448
// #define MAX_COMC_DIRTY 384
// #define MAX_COMC_IN_MEMORY 128
// #define MAX_COMC_DIRTY 96
// #define MAX_COMC_IN_MEMORY 512
// #define MAX_COMC_DIRTY 448
// #define MAX_COMC_IN_MEMORY 32
// #define MAX_COMC_DIRTY 24

#define MAX_COMC_IN_MEMORY 512
#define MAX_COMC_DIRTY 510

#if PRINT_MARK_ALL
#	ifndef PRINT_MARK_SB0GAP
#		define PRINT_MARK_SB0GAP 1
#	endif // #ifndef PRINT_MARK_SB0GAP
#	ifndef PRINT_MARK_SB
#		define PRINT_MARK_SB 1
#	endif // #ifndef PRINT_MARK_SB
#	ifndef PRINT_MARK_GDT
#		define PRINT_MARK_GDT 1
#	endif // #ifndef PRINT_MARK_GDT
#	ifndef PRINT_MARK_CAM
#		define PRINT_MARK_CAM 1
#	endif // #ifndef PRINT_MARK_CAM
#	ifndef PRINT_MARK_IAM
#		define PRINT_MARK_IAM 1
#	endif // #ifndef PRINT_MARK_IAM
#	ifndef PRINT_MARK_IT
#		define PRINT_MARK_IT 1
#	endif // #ifndef PRINT_MARK_IT
#else // #if PRINT_MARK_ALL
#	ifndef PRINT_MARK_SB0GAP
#		define PRINT_MARK_SB0GAP 0
#	endif // #ifndef PRINT_MARK_SB0GAP
#	ifndef PRINT_MARK_SB 
#		define PRINT_MARK_SB 0 
#	endif // #ifndef PRINT_MARK_SB 
#	ifndef PRINT_MARK_GDT
#		define PRINT_MARK_GDT 0
#	endif // #ifndef PRINT_MARK_GDT
#	ifndef PRINT_MARK_CAM
#		define PRINT_MARK_CAM 0
#	endif // #ifndef PRINT_MARK_CAM
#	ifndef PRINT_MARK_IAM
#		define PRINT_MARK_IAM 0
#	endif // #ifndef PRINT_MARK_IAM
#	ifndef PRINT_MARK_IT
#		define PRINT_MARK_IT 0
#	endif // #ifndef PRINT_MARK_IT
#endif // #if PRINT_MARK_ALL, else

#endif // #ifndef CONF_H
