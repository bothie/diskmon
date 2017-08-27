/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "com_cache.h"

#include "btthread.h"
#include "sc.h"
#include "threads.h"

#include <btlock.h>
#include <bttypes.h>
#include <string.h>
#include <zlib.h>

#if ALLOW_COM_CACHE_THREAD

#define CL_LOCK(sc) if (sc->allow_com_cache_thread) do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Locking cluster allocation map\n",__LINE__,gettid()); \
	} \
	btlock_lock(sc->cam_lock); \
} while (0)

#define CL_UNLOCK(sc) if (sc->allow_com_cache_thread) do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Unlocking cluster allocation map\n",__LINE__,gettid()); \
	} \
	btlock_unlock(sc->cam_lock); \
} while (0)

#define CCT_WAIT() if (sc->allow_com_cache_thread) do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: cc thread - going to sleep\n",__LINE__,gettid()); \
	} \
	btlock_wait(sc->com_cache_thread_lock); \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: cc thread - resuming operation\n",__LINE__,gettid()); \
	} \
} while (0)

#define CCT_WAKE() if (sc->allow_com_cache_thread) do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Waking up cc thread\n",__LINE__,gettid()); \
	} \
	btlock_wake(sc->com_cache_thread_lock); \
} while (0)

#define CC_LOCK(x) if (sc->allow_com_cache_thread) do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Locking com_cache directory\n",__LINE__,gettid()); \
	} \
	btlock_lock((x).debug_lock); \
} while (0)

#define CC_UNLOCK(x) if (sc->allow_com_cache_thread) do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Unlocking com_cache directory\n",__LINE__,gettid()); \
	} \
	btlock_unlock((x).debug_lock); \
} while (0)

#define CCE_LOCK(y) if (sc->allow_com_cache_thread) do { \
	struct com_cache_entry * x=(y); \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Locking com_cache[%u]\n",__LINE__,gettid(),(unsigned)x->index); \
	} \
	btlock_lock(x->debug_lock); \
} while (0)

#define CCE_UNLOCK(y) if (sc->allow_com_cache_thread) do { \
	struct com_cache_entry * x=(y); \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Unlocking com_cache[%u]\n",__LINE__,gettid(),(unsigned)x->index); \
	} \
	btlock_unlock(x->debug_lock); \
} while (0)

#define CCE_LOCK_FREE(y) if (sc->allow_com_cache_thread) do { \
	struct com_cache_entry * x=(y); \
	/* \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Freeing lock for com_cache[%u] which is using pipe fd %4i->%4i\n",__LINE__,gettid(),x->index,x->debug_lock->fd_writer_side,x->debug_lock->fd_reader_side); \
	} \
	*/ \
	btlock_lock_free(x->debug_lock); \
} while (0)

#define CCE_LOCK_MK(y) if (sc->allow_com_cache_thread) do { \
	struct com_cache_entry * x=(y); \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Allocating lock for com_cache[%u]\n",__LINE__,gettid(),(unsigned)x->index); \
	} \
	do { \
		x->debug_lock=btlock_lock_mk(); \
	} while (!x->debug_lock); \
	/* \
	if (CC_DEBUG_LOCKS) { \
		if (x->debug_lock) { \
			eprintf("%4i in thread %5i: Allocated lock using pipe fd %4i->%4i\n",__LINE__,gettid(),x->debug_lock->fd_writer_side,x->debug_lock->fd_reader_side); \
		} else { \
			eprintf("%4i in thread %5i: Allocation failed\n",__LINE__,gettid()); \
		} \
	} \
	*/ \
} while (0)

#else // #if ALLOW_COM_CACHE_THREAD

#define CL_LOCK(sc) do {} while (0)
#define CL_UNLOCK(sc) do {} while (0)
#define CCT_WAIT() do {} while (0)
#define CCT_WAKE() do {} while (0)
#define CC_LOCK(x) do {} while (0)
#define CC_UNLOCK(x) do {} while (0)
#define CCE_LOCK(y) do {} while (0)
#define CCE_UNLOCK(y) do {} while (0)
#define CCE_LOCK_FREE(y) do {} while (0)
#define CCE_LOCK_MK(y) do {} while (0)

#endif // #if ALLOW_COM_CACHE_THREAD, else

volatile bool exit_request_com_cache_thread=false;

/*
 * CCE_LOCK(walk) must be held
 */
void comc_move_front(struct scan_context * sc, struct com_cache_entry * walk, bool new) {
	if (!sc->allow_com_cache_thread) {
		exit_request_com_cache_thread = true;
		com_cache_work(sc);
	}
	
	if (new) {
		CC_LOCK(sc->comc);
	} else {
		if (!walk->prev) {
			return;
		}
		
		CC_LOCK(sc->comc);
		
		walk->prev->next = walk->next;
		
		if (walk->next) {
			walk->next->prev = walk->prev;
		} else {
			sc->comc.tail = walk->prev;
		}
	}
	
	if (likely(sc->comc.head)) {
		sc->comc.head->prev = walk;
	} else {
		sc->comc.tail = walk;
	}
	
	walk->next = sc->comc.head;
	sc->comc.head = walk;
	walk->prev = NULL;
	
	CC_UNLOCK(sc->comc);
}

void comc_get(struct scan_context * sc, struct com_cache_entry * walk) {
	// CCE_LOCK(walk); <--- was already locked by get_com_cache_entry
	
	// BEGIN READING
	walk->entry=malloc(8*sc->comc.clusters_per_entry);
#if COMC_IN_MEMORY
	if (!sc->comc.compr[walk->index].ptr) {
#else // #if COMC_IN_MEMORY
	char * tmp;
	int fd=open(tmp=mprintf("/ramfs/comc/%04x",(unsigned)walk->index),O_RDONLY);
	if (fd<0) {
		if (errno!=ENOENT) {
			eprintf("cluster-owner-map-cache-thread: OOPS: open(»%s«): %s\n",tmp,strerror(errno));
			exit(2);
		}
		free(tmp);
#endif // #if COMC_IN_MEMORY, else
		memset(walk->entry,0,8*sc->comc.clusters_per_entry);
		// eprintf("Init %5i (num_in_memory=%u)\n",walk->index,sc->comc.num_in_memory);
	} else {
		// eprintf("Read %5i (num_in_memory=%u)\n",walk->index,sc->comc.num_in_memory);
#if COMC_IN_MEMORY
		Bytef * cbuffer=(Bytef *)sc->comc.compr[walk->index].ptr;
		uLongf  dbuflen=sc->comc.compr[walk->index].len;
#else // #if COMC_IN_MEMORY
		if (CC_DEBUG_LOCKS) eprintf("%4i in thread %5i: Reading data by using fd %i\n",__LINE__,gettid(),fd);
		uLongf dbuflen=read(fd,cbuffer,cbuflen);
		if (close(fd)) {
			eprintf("cluster-owner-map-cache-thread: OOPS: close(»%s«): %s\n",tmp,strerror(errno));
			exit(2);
		}
		free(tmp);
#endif // #if COMC_IN_MEMORY, else
		uLongf x=8*sc->comc.clusters_per_entry;
		if (Z_OK!=uncompress((Bytef*)walk->entry,&x,(Bytef*)cbuffer,dbuflen)) {
			eprintf("cluster-owner-map-cache-thread: OOPS: uncompress failed\n");
			exit(2);
		}
		if (x!=8*sc->comc.clusters_per_entry) {
			eprintf("cluster-owner-map-cache-thread: OOPS: uncompress failed\n");
			exit(2);
		}
	}
	
	walk->dirty=false;
	// END READING
	
	comc_move_front(sc, walk, true);
}

bool com_cache_work(struct scan_context * sc) {
	bool nothing_done = true;
	
	CC_LOCK(sc->comc);
#if USE_COMC_THREAD_TO_READ
	if (sc->comc.read_head) {
		nothing_done = false;
		
		struct com_cache_entry * walk=sc->comc.read_head;
		sc->comc.read_head=sc->comc.read_head->next;
		if (!sc->comc.read_head) {
			sc->comc.read_tail=NULL;
		}
		++sc->comc.num_in_memory;
		CC_UNLOCK(sc->comc);
		
		comc_get(sc, walk);
		
		CCE_UNLOCK(walk);
		
		CC_LOCK(sc->comc);
	}
#endif // #if USE_COMC_THREAD_TO_READ
	if (nothing_done || likely(sc->comc.num_in_memory>=MAX_COMC_IN_MEMORY)) {
		if (likely(sc->comc.num_in_memory>MAX_COMC_DIRTY)) {
			size_t num=sc->comc.num_in_memory-MAX_COMC_DIRTY;
			struct com_cache_entry * walk=sc->comc.tail;
			do {
				if (!walk->num_in_use
				&&  walk->dirty) {
					++walk->num_in_use;
					CC_UNLOCK(sc->comc);
					
					CCE_LOCK(walk);
					
					// eprintf("Sync %5i (num_in_memory=%u)\n",walk->index,sc->comc.num_in_memory);
					
					// BEGIN WRITING
					uLongf dbuflen=sc->comc.cbuflen;
					int z_rv = compress2((Bytef*)sc->comc.cbuffer,&dbuflen,(Bytef*)walk->entry,8*sc->comc.clusters_per_entry,Z_BEST_SPEED);
					if (Z_OK != z_rv) {
						eprintf("cluster-owner-map-cache-thread: OOPS: compress2 failed: %s\n", zError(z_rv));
						exit(2);
					}
#if COMC_IN_MEMORY
					free(sc->comc.compr[walk->index].ptr);
					memcpy(sc->comc.compr[walk->index].ptr=malloc(dbuflen),sc->comc.cbuffer,sc->comc.compr[walk->index].len=dbuflen);
#else // #if COMC_IN_MEMORY
					char * tmp;
					int fd=open(tmp=mprintf("/ramfs/comc/%04x",(unsigned)walk->index),O_CREAT|O_TRUNC|O_WRONLY,0666);
					if (fd<0) {
						eprintf("cluster-owner-map-cache-thread: OOPS: open(»%s«): %s\n",tmp,strerror(errno));
						exit(2);
					} else {
						if (CC_DEBUG_LOCKS) eprintf("%4i in thread %5i: Writing data by using fd %i\n",__LINE__,gettid(),fd);
					}
					if ((ssize_t)dbuflen!=write(fd,sc->comc.cbuffer,dbuflen)) {
						eprintf("cluster-owner-map-cache-thread: OOPS: write(»%s«): %s\n",tmp,strerror(errno));
						exit(2);
					}
					if (close(fd)) {
						eprintf("cluster-owner-map-cache-thread: OOPS: close(»%s«): %s\n",tmp,strerror(errno));
						exit(2);
					}
					free(tmp);
#endif // #if COMC_IN_MEMORY, else
					walk->dirty=false;
					// END WRITING
					
					CCE_UNLOCK(walk);
					
					CC_LOCK(sc->comc);
					--walk->num_in_use;
					nothing_done=false;
					break;
				}
				walk=walk->prev;
				assert(walk);
			} while (--num);
		}
	}
	if (likely(sc->comc.num_in_memory>=MAX_COMC_IN_MEMORY)) {
		struct com_cache_entry * walk=sc->comc.tail;
		while (walk && (walk->num_in_use || walk->dirty)) {
			walk=walk->prev;
		}
		if (walk) {
			--sc->comc.num_in_memory;
//				eprintf("Drop %5i (num_in_memory=%u)\n",walk->index,sc->comc.num_in_memory);
			if (walk->next) {
				walk->next->prev=walk->prev;
			} else {
				sc->comc.tail=walk->prev;
			}
			if (walk->prev) {
				walk->prev->next=walk->next;
			} else {
				sc->comc.head=walk->next;
			}
			free(walk->entry);
			sc->comc.ccgroup[walk->index]=NULL;
			CCE_LOCK_FREE(walk);
			free(walk);
		}
	}
	CC_UNLOCK(sc->comc);
	
	return !nothing_done;
}

#if ALLOW_COM_CACHE_THREAD
THREAD_RETURN_TYPE com_cache_thread(void * arg) {
	struct scan_context * sc=(struct scan_context *)arg;
	
	eprintf("Executing com_cache_thread via thread %i\n",gettid());
	
	do { // while (!exit_request_com_cache_thread)
		if (!com_cache_work(sc) && !exit_request_com_cache_thread) {
			CCT_WAIT();
		}
	} while (!exit_request_com_cache_thread);
	exit_request_com_cache_thread=false;
	GENERAL_BARRIER();
	TRF_WAKE();
	
	eprintf("Returning from com_cache_thread (exiting cluster-owner-map-cache-thread)\n");
	
	THREAD_RETURN();
}
#endif // #if ALLOW_COM_CACHE_THREAD

struct com_cache_entry * get_com_cache_entry(struct scan_context * sc, size_t ccg, u64 cluster) {
/*
	CC_LOCK(sc->comc);
	if (!sc->comc.ccgroup[ccg]) {
		sc->comc.ccgroup[ccg]=malloc(sizeof(*sc->comc.ccgroup[ccg]));
		sc->comc.ccgroup[ccg]->index=ccg;
		CCE_LOCK_MK(sc->comc.ccgroup[ccg]);
		sc->comc.ccgroup[ccg]->entry=NULL;
		sc->comc.ccgroup[ccg]->num_in_use=1;
		sc->comc.ccgroup[ccg]->
		sc->comc.compr[ccg].ptr
	}
	CCE_LOCK(sc->comc.ccgroup[ccg]);
	CC_UNLOCK(sc->comc);
	if (!sc->comc.ccgroup[ccg]->entry) {
		sc->comc.ccgroup[ccg]->next=NULL;
		sc->comc.ccgroup[ccg]->prev=sc->comc.read_tail;
		if (!sc->comc.read_tail) {
			sc->comc.read_head=sc->comc.ccgroup[ccg];
		} else {
			sc->comc.read_tail->next=sc->comc.ccgroup[ccg];
		}
		sc->comc.read_tail=sc->comc.ccgroup[ccg];
		// A little bit strange. We lock this here and don't unlock it again. That 
		// will be done by the reader thread. This allows us to wait for it by a 
		// simple double locking.
		CCT_WAKE(); // Make sure, we won't wait senselessly for the com_cache_thread.
		CCE_LOCK(sc->comc.ccgroup[ccg]);
	}
	++sc->comc.ccgroup[ccg]->num_in_use;
	*/
	CC_LOCK(sc->comc);
	if (ccg>=sc->comc.size) {
		eprintf(
			"ccg=%u, sc->comc.size=%u (cluster=%u)\n"
			,(unsigned)ccg
			,(unsigned)sc->comc.size
			,(unsigned)cluster
		);
		ccg=*((size_t*)0);
	}
	assert(ccg<sc->comc.size);
	struct com_cache_entry * walk = sc->comc.ccgroup[ccg];
	
	if (!walk) {
		walk = sc->comc.ccgroup[ccg] = malloc(sizeof(*sc->comc.ccgroup[ccg]));
		
		walk->index=ccg;
		CCE_LOCK_MK(walk);
		walk->num_in_use=1;
		walk->next=NULL;
		walk->prev=sc->comc.read_tail;
		CCE_LOCK(walk);
		// A little bit strange. We lock CCE here and don't unlock it again. That 
		// will be done by the reader thread. This allows us to wait for it by a 
		// simple double locking.
#if USE_COMC_THREAD_TO_READ
		if (!sc->comc.read_tail) {
			sc->comc.read_head=walk;
		} else {
			sc->comc.read_tail->next=walk;
		}
		sc->comc.read_tail=walk;
		CCT_WAKE(); // Make sure, we won't wait senselessly for the com_cache_thread.
		CC_UNLOCK(sc->comc);
		CCE_LOCK(walk);
#else // #if USE_COMC_THREAD_TO_READ
		++sc->comc.num_in_memory;
		CC_UNLOCK(sc->comc);
		
		comc_get(sc, walk);
#endif // #if USE_COMC_THREAD_TO_READ, else
		CCT_WAKE(); // Make sure the com cache thread gets woken up even is nothing really *needs* it, so it can do it's clean up job
	} else {
		++walk->num_in_use;
		CC_UNLOCK(sc->comc);
		CCE_LOCK(walk);
	}
	comc_move_front(sc, walk, false);
	return walk;
}

void release(struct scan_context * sc, struct com_cache_entry * ccge) {
#if !ALLOW_COM_CACHE_THREAD
	ignore(sc); // prevent warning: unused parameter 'sc'
#endif // #if !ALLOW_COM_CACHE_THREAD
	if (1 == ccge->num_in_use) {
		comc_move_front(sc, ccge, false);
	}
	CCE_UNLOCK(ccge);
	CC_LOCK(sc->comc);
	--ccge->num_in_use;
	CC_UNLOCK(sc->comc);
}

void shutdown_com_cache_thread(struct scan_context * sc) {
#if !ALLOW_COM_CACHE_THREAD
	ignore(sc); // prevent warning: unused parameter 'sc'
#endif // #if !ALLOW_COM_CACHE_THREAD
	
	TRF_WAIT(); // We misuse the table reader full lock to wait for the com cache thread to exit. However, TRF is known to be in the "waked" state right now, so wait for it once to reset it to default state.
	exit_request_com_cache_thread=true;
	GENERAL_BARRIER();
	CCT_WAKE(); // Make sure, we won't wait senselessly for the com_cache_thread.
	TRF_WAIT(); // And wait for that thread to exit
	GENERAL_BARRIER();
	assert(!exit_request_com_cache_thread);
}

void com_cache_init(struct scan_context * sc, u64 num_clusters) {
	sc->comc.debug_lock=btlock_lock_mk();
	
	// u32 i=(sc->num_groups*sc->clusters_per_group)/65536;
	u32 i = num_clusters / 65536;
	sc->comc.clusters_per_entry=1;
	while (i) {
		i>>=1;
		sc->comc.clusters_per_entry<<=1;
	}
	sc->comc.size = (num_clusters + sc->comc.clusters_per_entry - 1) / sc->comc.clusters_per_entry;
	sc->comc.ccgroup=malloc(sc->comc.size*sizeof(*sc->comc.ccgroup));
#if COMC_IN_MEMORY
	sc->comc.compr=malloc(sc->comc.size*sizeof(*sc->comc.compr));
#endif // #if COMC_IN_MEMORY
	for (i=0;i<sc->comc.size;++i) {
		sc->comc.ccgroup[i]=NULL;
#if COMC_IN_MEMORY
		sc->comc.compr[i].ptr=NULL;
		sc->comc.compr[i].len=0;
#endif // #if COMC_IN_MEMORY
	}
	sc->comc.head=NULL;
	sc->comc.tail=NULL;
	sc->comc.num_in_memory=0;
#if USE_COMC_THREAD_TO_READ
	sc->comc.read_head=NULL;
#endif // #if USE_COMC_THREAD_TO_READ
	sc->comc.read_tail=NULL;
	
	sc->comc.cbuflen = compressBound(8 * sc->comc.clusters_per_entry);
	sc->comc.cbuffer = malloc(sc->comc.cbuflen);
}

void com_cache_cleanup(struct scan_context * sc) {
	free(sc->comc.cbuffer);
	if (sc->comc.ccgroup) {
		for (u32 i=0;i<sc->comc.size;++i) {
			if (sc->comc.ccgroup[i]) {
				CCE_LOCK_FREE(sc->comc.ccgroup[i]);
				free(sc->comc.ccgroup[i]->entry);
				free(sc->comc.ccgroup[i]);
			}
		}
	}
#if COMC_IN_MEMORY
	if (sc->comc.compr) {
		for (u32 i=0;i<sc->comc.size;++i) {
			free(sc->comc.compr[i].ptr);
		}
	}
	free(sc->comc.compr);
#endif // #if COMC_IN_MEMORY
	free(sc->comc.ccgroup);
	btlock_lock_free(sc->comc.debug_lock);
}
