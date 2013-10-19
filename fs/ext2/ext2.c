// -> define globally (or not) #define BTCLONE // broken on amd64?

#define __athlon__

#include <btatomic.h>
#define _GNU_SOURCE

#include "ext2.h"

#include "common.h"
#include "bdev.h"

#include <btendian.h>

#include <assert.h>
#include <btlock.h>
#include <btthread.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <mprintf.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <zlib.h>

/*
-1UL-(5UL*sc->table_reader_group) CAM[x]
-2UL-(5UL*sc->table_reader_group) IAM[x]
-3UL-(5UL*sc->table_reader_group) IT[x]
-4UL-(5UL*sc->table_reader_group) SB[x]
-5UL-(5UL*sc->table_reader_group) GDT[x]

num_inodes+5*num_groups
2^32

8192 -> 2048
2048*2048*2048
8*1024^3=8GB
-> pro Inode mehr als 2^32 cluster!

epi=entries per inode (z.B. 256 für 1024 cluster size)
0..11               -> Block im Inode
12                  -> Dies ist sind (oder ein über diesen sind indizierter Cluster)
13                  -> Dies ist dind
14                  -> Dies ist tind
13*epi^1+[0..epi)   -> Dies ist sind in dind(13) (oder ein über diesen sind indizierter Cluster)
14*epi^1+[0..epi)   -> Dies ist dind in tind(14)
14*epi^2+[0..epi^2) -> Dies ist sind in dind(tind(14)) (oder ein über diesen sind indizierter Cluster)
0x80000001          -> Dies ist ein Cluster Allocation Map Cluster, inode nummer gibt Gruppe an
0x80000002          -> Dies ist ein Inode Allocation Map Cluster, inode nummer gibt Gruppe an
0x80000003-0x8....X -> Dies ist ein Inode Table Cluster, inode nummer gibt Gruppe an, dieser Wert -0x80000003 ist die relative Cluster nummer.
0x8000000X+1        -> Dies ist ein Super block, inode nummer gibt Gruppe an, X=Anzahl Inode Table Clusters+2
0x8000000X+2-0x8..Y -> Dies ist eine Gruppenbeschreibung, inode nummer gibt Gruppe an, Y=Anzahl Cluster für Gruppenbeschreibung+1+X
*/

#define COMC_IN_MEMORY

/*
#define PRINT_MARK_IT 1
#define PRINT_MARK_IAM 1
#define PRINT_MARK_CAM 1
*/

#ifdef PRINT_MARK_ALL
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
#else
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
#endif // #ifdef PRINT_MARK_ALL

/*
 * 295 threads (plus two or three or so) is a limit of valgrind
 * Each thread needs 2 fds (for it's thread structure lock), and maybe 2 fds of the com_cache's lock this thread is wating for.
 * The soft limit is hard limit minus 50 
 * up to 448 com_cache entries make 896 fds for their locks.
 * Sum is ~2000 which is way too much.
 */

/*
unsigned hard_child_limit=294;
unsigned soft_child_limit=244;

#define MAX_COMC_IN_MEMORY 448
#define MAX_COMC_DIRTY 384
*/

unsigned hard_child_limit=294;
unsigned soft_child_limit=244;

// #define MAX_COMC_IN_MEMORY 128
// #define MAX_COMC_DIRTY 96
#define MAX_COMC_IN_MEMORY 512
#define MAX_COMC_DIRTY 448

/*
unsigned hard_child_limit=15;
unsigned soft_child_limit=12;

#define MAX_COMC_IN_MEMORY 32
#define MAX_COMC_DIRTY 24
*/

#define MAX_INODE_TABLE_PREREADED 512
#define PREREADED_NOTIFYTIME 10

// #define INODE_TABLE_ROUNDROUBIN 0xffffffffLU
#define INODE_TABLE_ROUNDROUBIN 512LU

volatile unsigned live_child;

#define DRIVER_NAME "ext2"

#define le32(x) x=le2host32(x)
#define le16(x) x=le2host16(x)

void endian_swap_sb(struct super_block * sb) {
	le32(sb->num_inodes);
	le32(sb->num_clusters);
	le32(sb->num_reserved_clusters);
	le32(sb->num_free_clusers);
	le32(sb->num_free_inodes);
	le32(sb->first_data_cluster);
	le32(sb->log_cluster_size);
	le32(sb->log_frag_size);
	le32(sb->clusters_per_group);
	le32(sb->frags_per_group);
	le32(sb->inodes_per_group);
	le32(sb->mtime);
	le32(sb->wtime);
	le16(sb->mount_count);
	le16(sb->max_mount_count);
	le16(sb->magic);
	le16(sb->state);
	le16(sb->errors);
	le16(sb->minor_rev_level);
	le32(sb->lastcheck_time);
	le32(sb->checkinterval);
	le32(sb->creator_os);
	le32(sb->rev_level);
	le16(sb->reserved_access_uid);
	le16(sb->reserved_access_gid);
	le32(sb->first_inode);
	le16(sb->inode_size);
	le16(sb->my_group);
	le32(sb->rw_compat);
	le32(sb->in_compat);
	le32(sb->ro_compat);
	le32(sb->e2compr_algorithm_usage_bitmap);
	le16(sb->onlineresize2fs_reserved_gdt_blocks);
	le32(sb->journal_inode);
	le32(sb->journal_dev);
	le32(sb->last_orphanded_inode);
	for (int i=0;i<4;++i) {
		le32(sb->hash_seed[i]);
	}
	le32(sb->default_mount_opts);
	le32(sb->first_meta_bg);
}

void gdt_disk2memory_v1(struct group_desciptor_in_memory *gdt, struct group_desciptor_v1 *gdtv1, unsigned num_groups) {
	for (unsigned group = 0; group < num_groups; ++group) {
		gdt[group].cluster_allocation_map = le2host32(gdtv1[group].cluster_allocation_map);
		gdt[group].inode_allocation_map = le2host32(gdtv1[group].inode_allocation_map);
		gdt[group].inode_table = le2host32(gdtv1[group].inode_table);
		gdt[group].num_free_clusters = le2host16(gdtv1[group].num_free_clusters);
		gdt[group].num_free_inodes = le2host16(gdtv1[group].num_free_inodes);
		gdt[group].num_directories = le2host16(gdtv1[group].num_directories);
		gdt[group].flags = le2host16(gdtv1[group].flags);
		gdt[group].snapshot_exclude_bitmap = le2host32(gdtv1[group].snapshot_exclude_bitmap);
		gdt[group].cluster_allocation_map_csum = le2host16(gdtv1[group].cluster_allocation_map_csum);
		gdt[group].inode_allocation_map_csum = le2host16(gdtv1[group].inode_allocation_map_csum);
		gdt[group].num_virgin_inodes = le2host16(gdtv1[group].num_virgin_inodes);
		gdt[group].csum = le2host16(gdtv1[group].csum);
	}
}

void gdt_disk2memory_v2(struct group_desciptor_in_memory *gdt, struct group_desciptor_v2 *gdtv2, unsigned num_groups) {
	for (unsigned group = 0; group < num_groups; ++group) {
		gdt[group].cluster_allocation_map = le2host32(gdtv2[group].cluster_allocation_map_lo) | ((u64)le2host32(gdtv2[group].cluster_allocation_map_hi) << 32);
		gdt[group].inode_allocation_map = le2host32(gdtv2[group].inode_allocation_map_lo) | ((u64)le2host32(gdtv2[group].inode_allocation_map_hi) << 32);
		gdt[group].inode_table = le2host32(gdtv2[group].inode_table_lo) | ((u64)le2host32(gdtv2[group].inode_table_hi) << 32);
		gdt[group].num_free_clusters = le2host16(gdtv2[group].num_free_clusters_lo) | ((u32)le2host16(gdtv2[group].num_free_clusters_hi) << 16);
		gdt[group].num_free_inodes = le2host16(gdtv2[group].num_free_inodes_lo) | ((u32)le2host16(gdtv2[group].num_free_inodes_hi) << 16);
		gdt[group].num_directories = le2host16(gdtv2[group].num_directories_lo) | ((u32)le2host16(gdtv2[group].num_directories_hi) << 16);
		gdt[group].snapshot_exclude_bitmap = le2host32(gdtv2[group].snapshot_exclude_bitmap_lo) | ((u64)le2host32(gdtv2[group].snapshot_exclude_bitmap_hi) << 32);
		gdt[group].cluster_allocation_map_csum = le2host16(gdtv2[group].cluster_allocation_map_csum_lo) | ((u32)le2host16(gdtv2[group].cluster_allocation_map_csum_hi) << 16);
		gdt[group].inode_allocation_map_csum = le2host16(gdtv2[group].inode_allocation_map_csum_lo) | ((u32)le2host16(gdtv2[group].inode_allocation_map_csum_hi) << 16);
		gdt[group].num_virgin_inodes = le2host16(gdtv2[group].num_virgin_inodes_lo) | ((u32)le2host16(gdtv2[group].num_virgin_inodes_hi) << 16);
		gdt[group].flags = le2host16(gdtv2[group].flags);
		gdt[group].reserved = le2host32(gdtv2[group].reserved);
		gdt[group].csum = le2host16(gdtv2[group].csum);
	}
}

void endian_swap_extent_header(struct extent_header * eh) {
	le16(eh->magic);
	le16(eh->num_entries);
	le16(eh->max_entries);
	le16(eh->depth);
	le32(eh->generation);
}

void endian_swap_extent_descriptor(struct extent_descriptor * ed) {
	le32(ed->file_cluster);
	le16(ed->len);
	le16(ed->disk_cluster_hi);
	le32(ed->disk_cluster_lo);
}

void endian_swap_extent_index(struct extent_index * ei) {
	le32(ei->file_cluster);
	le32(ei->leaf_lo);
	le16(ei->leaf_hi);
	le16(ei->unused);
}

void endian_swap_extent_tail(struct extent_tail * et) {
	le32(et->checksum);
}

bool endian_swap_extent_block(char * eb, size_t size) {
	struct extent_header * eh = (struct extent_header *)eb;
	
	eb += 12;
	
	endian_swap_extent_header(eh);
	
	/*
	if (eh->magic != EXTENT_HEADER_MAGIC) {
		// endian_swap_extent_header(eh); // swap back
		return false;
	}
	*/
	
	u16 num = eh->num_entries;
	if (num > size / 12 - 1) {
		// endian_swap_extent_header(eh); // swap back
		/*
		return false;
		*/
		num = size / 12 - 1;
	}
	
	if (eh->depth) {
		while (num--) {
			struct extent_index * ei = (struct extent_index *)eb;
			eb += 12;
			endian_swap_extent_index(ei);
		}
	} else {
		while (num--) {
			struct extent_descriptor * ed = (struct extent_descriptor *)eb;
			eb += 12;
			endian_swap_extent_descriptor(ed);
		}
	}
	
	if (size != 60) {
		struct extent_tail * et = (struct extent_tail *)eb;
		endian_swap_extent_tail(et);
	}
	
	return true;
}

void endian_swap_inode(struct inode * inode) {
	le16(inode->mode);
	le16(inode->uid);
	le32(inode->size);
	le32(inode->atime);
	le32(inode->ctime);
	le32(inode->mtime);
	le32(inode->dtime);
	le16(inode->gid);
	le16(inode->links_count);
	le32(inode->num_blocks);
	le32(inode->flags);
	le32(inode->translator);
	if (!(inode->flags & INOF_EXTENTS)) {
		for (int i=0;i<NUM_CLUSTER_POINTERS;++i) {
			le32(inode->cluster[i]);
		}
	} else {
		endian_swap_extent_block((char*)inode->cluster, sizeof(inode->cluster));
	}
	le32(inode->generation);
	le32(inode->file_acl);
	le32(inode->dir_acl);
	le32(inode->faddr);
	le16(inode->mode_high);
	le16(inode->uid_high);
	le16(inode->gid_high);
	le32(inode->author);
}

void write_super_blocks(struct super_block * sb) {
	endian_swap_sb(sb);
	// FIXME: Implement this ...
	endian_swap_sb(sb);
}

/*
struct read_tables_list {
	u8 * block_buffer;
	struct read_tables_list * next;
};
*/

struct inode_scan_context {
	struct scan_context * sc;
	unsigned long inode_num;
	struct inode * inode;
	char * type;
	unsigned type_bit;
	bool is_dir;
	block_t schedule_first_cluster;
	unsigned long schedule_num_clusters;
	
	unsigned long illegal_ind_clusters[4];
	unsigned long used_clusters;
	unsigned long maybe_holes;
	unsigned long holes;
};

struct dirent {
	u32 inode;
	char * filename;
	u8 filetype; // We have to check that, so remember it ...
};

struct dir {
	unsigned num_entries;
	struct dirent * entry;
};

struct dirent_node {
	struct dirent_node * next;
	struct dirent dirent;
};

struct dirent_list {
	struct dirent_node * frst;
	struct dirent_node * last;
};

// #include <atomic_ops.h>

/*
struct btlock_lock {
	AO_t locked;
	int fd_writer_side;
	int fd_reader_side;
};
*/

#define CC_DEBUG_LOCKS 0

#define CL_LOCK(sc) do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Locking cluster allocation map\n",__LINE__,gettid()); \
	} \
	btlock_lock(sc->cam_lock); \
} while (0)

#define CL_UNLOCK(sc) do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Unlocking cluster allocation map\n",__LINE__,gettid()); \
	} \
	btlock_unlock(sc->cam_lock); \
} while (0)

#define CCT_WAIT() do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: cc thread - going to sleep\n",__LINE__,gettid()); \
	} \
	btlock_wait(sc->com_cache_thread_lock); \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: cc thread - resuming operation\n",__LINE__,gettid()); \
	} \
} while (0)

#define CCT_WAKE() do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Waking up cc thread\n",__LINE__,gettid()); \
	} \
	btlock_wake(sc->com_cache_thread_lock); \
} while (0)

#define CC_LOCK(x) do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Locking com_cache directory\n",__LINE__,gettid()); \
	} \
	btlock_lock((x).debug_lock); \
} while (0)

#define CC_UNLOCK(x) do { \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Unlocking com_cache directory\n",__LINE__,gettid()); \
	} \
	btlock_unlock((x).debug_lock); \
} while (0)

#define CCE_LOCK(y) do { \
	struct com_cache_entry * x=(y); \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Locking com_cache[%u]\n",__LINE__,gettid(),(unsigned)x->index); \
	} \
	btlock_lock(x->debug_lock); \
} while (0)

#define CCE_UNLOCK(y) do { \
	struct com_cache_entry * x=(y); \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Unlocking com_cache[%u]\n",__LINE__,gettid(),(unsigned)x->index); \
	} \
	btlock_unlock(x->debug_lock); \
} while (0)

#define CCE_LOCK_FREE(y) do { \
	struct com_cache_entry * x=(y); \
	/* \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Freeing lock for com_cache[%u] which is using pipe fd %4i->%4i\n",__LINE__,gettid(),x->index,x->debug_lock->fd_writer_side,x->debug_lock->fd_reader_side); \
	} \
	*/ \
	btlock_lock_free(x->debug_lock); \
} while (0)

#define CCE_LOCK_MK(y) do { \
	struct com_cache_entry * x=(y); \
	if (CC_DEBUG_LOCKS) { \
		eprintf("%4i in thread %5i: Allocating lock for com_cache[%u]\n",__LINE__,gettid(),(unsigned)x->index); \
	} \
	x->debug_lock=btlock_lock_mk(); \
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

#define TRE_WAIT() do { \
	btlock_wait(sc->table_reader_empty); \
} while (0)

#define TRE_WAKE() do { \
	btlock_wake(sc->table_reader_empty); \
} while (0)

#define TRF_WAIT() do { \
	btlock_wait(sc->table_reader_full); \
} while (0)

#define TRF_WAKE() do { \
	btlock_wake(sc->table_reader_full); \
} while (0)

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
	struct com_cache_entry * read_head;
	struct com_cache_entry * read_tail;
	// 2^32*4/65536=256KB per cache entry
#ifdef COMC_IN_MEMORY
	struct {
		u8 * ptr;
		size_t len;
	} * compr;
#endif // #ifdef COMC_IN_MEMORY, else
};

struct scan_context {
	struct bdev * bdev;
	struct group_desciptor_in_memory * gdt;
	struct group_desciptor_v1 * gdtv1;
	struct group_desciptor_v2 * gdtv2;
	struct inode * inode_table;
	struct btlock_lock * cam_lock;
	struct progress_bar * progress_bar;
	struct super_block * sb;
	struct dir * * dir;
	
	struct com_cache comc;
	struct btlock_lock * com_cache_thread_lock;
	
	u16 * inode_link_count;
	char * * inode_type_str;
	
	int cluster_offset;
	
//	struct read_tables_list * list_head;
//	struct read_tables_list * list_tail;
	
	const char * name;
	
	u8 * calculated_inode_allocation_map;
	u8 * cluster_allocation_map;
	u8 * calculated_cluster_allocation_map;
	u8 * inode_allocation_map;
	
	u16 * calculated_link_count_array;
	
	block_t cluster_size_Blocks;
//	block_t cs;
	block_t group_size_Blocks;
	block_t size_of_gdt_Blocks;
	
	unsigned block_size;
//	unsigned cam_iam_it_Blocks;
//	unsigned cam_iam_it_Clusters;
	unsigned des_per_groupcluster_size_Bytes;
//	unsigned group;
	unsigned num_groups;
	
//	unsigned long num_clusters;
	
	size_t cluster_size_Bytes;
	size_t num_clusterpointers;
	unsigned num_clusterpointers_shift;
	
	u32 clusters_per_group;
	
	/*
	 * This variable get's updated in the reader thread to the group 
	 * currently being read in and therefore MUST NOT get cached by the 
	 * parent thread by optimizing read operations away.
	 *
	 * The reader thread is finished as soon as group == num_groups
	 *
	 * Please note that group is being updated without any locking at all.
	 */
	volatile unsigned table_reader_group;
	atomic_t prereaded;
	struct btlock_lock * table_reader_full;
	struct btlock_lock * table_reader_empty;
	
	/*
	 * Tells, wether the reader is running in the background or not. It 
	 * if is NOT, it MUST process each group's memcpy()s before reading 
	 * the next group, as it will most probably run out of memory if it 
	 * doesn't.
	 */
	bool background;
	
	/*
	 * A little bit eye-candy: As soon as the main loop has finished, we 
	 * set waiting4threads to true, which causes threads being busy in the 
	 * cluster allocation check loop to notify their finish. If not set, 
	 * they just die off silently.
	 */
	volatile bool waiting4threads;

	/*
	 * Some means to configure automatic runs. Currently I only provide a 
	 * means to omit uncritival messages. But later on, this may be 
	 * extended up to supporting a fully automatic run.
	 */
	bool warn_ctime;
	bool warn_mtime;
	bool warn_atime;
	bool warn_dtime_zero;
	bool warn_dtime_nonzero;
};

bool read_super(struct scan_context * sc,struct super_block * sb,unsigned group,block_t offset,bool probing) {
	if (2!=bdev_read(sc->bdev,offset,2,(void *)sb)) {
		ERRORF("%s: Couldn't read blocks %llu and %llu while trying to read super block of group %u.",sc->name,(unsigned long long)offset,(unsigned long long)(offset+1),group);
		return false;
	} else {
		endian_swap_sb(sb);
		
		if (sb->magic==MAGIC) {
			u16 g = group > 65535 ? 65535 : group;
			if (sb->my_group != g) {
				if (g != group) {
					ERRORF("%s: Cluster group %u contains a super block backup claiming to belonging to cluster group %u (should be 65535).",sc->name,(unsigned)group,sb->my_group);
				} else {
					ERRORF("%s: Cluster group %u contains a super block backup claiming to belonging to cluster group %u.",sc->name,(unsigned)group,sb->my_group);
				}
				return false;
			}
			if (probing) {
				NOTIFYF("%s: Found ext2 super block in block %llu of device.",sc->name,(unsigned long long)offset);
			}
			return true;
		}
		
		if (!probing) {
			ERRORF(
				"%s: Didn't find expected ext2 super block backup in block %llu."
				,sc->name
				,(unsigned long long)offset
			);
		}
		return false;
	}
	assert(never_reached);
}

static inline int test_root(unsigned a,unsigned b) {
	unsigned num=b;
	
	while (a>num) {
		num*=b;
	}
	
	return num==a;
}

static bool has_sparsed_super_block(unsigned group) {
	if (group<=1) return 1;
	if (!(group&1)) return 0;
	return (test_root(group,7)
	||      test_root(group,5)
	||      test_root(group,3));
}

bool group_has_super_block(const struct super_block * sb,unsigned group) {
	if (sb->ro_compat&RO_COMPAT_SPARSE_SUPER) {
		return has_sparsed_super_block(group);
	}
	return true;
}

void process_table_for_group(unsigned long group,struct scan_context * sc) {
/*	{
		struct read_tables_list * lh=sc->list_head;
		sc->list_head=lh->next;
		free(lh);
//		eprintf("process_table_for_group(%u): free(lh=%p), list_head:=%p\n",group,lh,cmd_struct->list_head);
	} */
	
/*
	memcpy(
		sc->cluster_allocation_map+sc->cluster_size_Bytes*group,
		sc->list_head->block_buffer,
		sc->cluster_size_Bytes
	);
	memcpy(
		sc->inode_allocation_map+sc->sb->inodes_per_group/8*group,
		sc->list_head->block_buffer+sc->cluster_size_Bytes,
		sc->sb->inodes_per_group/8
	);
	struct inode * inode2=(struct inode *)(sc->list_head->block_buffer+2*sc->cluster_size_Bytes);
*/
	
	struct inode * inode=sc->inode_table+sc->sb->inodes_per_group*(group%INODE_TABLE_ROUNDROUBIN);
	
/*
	memcpy(
		inode,
		inode2,
		sc->sb->inodes_per_group*sizeof(*inode)
	);
	
	free(sc->list_head->block_buffer);
*/
	
	for (unsigned i = 0; i < (sc->sb->inodes_per_group - sc->gdt[group].num_virgin_inodes); ++i) {
		endian_swap_inode(inode+i);
	}
	
//	eprintf("process_table_for_group(%u): free(list_head->bb=%p)\n",group,cmd_struct->list_head->block_buffer);
}

bool read_table_for_one_group(struct scan_context * sc) {
	// struct read_tables_list * list=sc->list_tail;
	
	unsigned counter;
	
	counter=0;
	while (atomic_get(&sc->prereaded)==MAX_INODE_TABLE_PREREADED) {
		if (++counter==PREREADED_NOTIFYTIME) {
//			NOTIFY("read_table_for_one_group: Going to sleep to let the foreground process catch up ...");
		}
		TRF_WAIT();
	}
	
/*	counter=0;
	while (!(list->next=malloc(sizeof(*list->next)))) {
		if (!sc->background) {
			ERRORF("malloc error in ext2_read_tables: %s.",strerror(errno));
			abort();
		}
		if (++counter==60) {
			ERRORF("malloc error in ext2_read_tables, gave up after 60 seconds: %s.",strerror(errno));
			abort();
		}
		sleep(1);
	}
//	eprintf("read_table_for_one_group(%u): %p->next:=%p\n",cmd_struct->group,list,list->next);
	sc->list_tail=list=list->next;
	counter=0;
	while (!(list->block_buffer=malloc(sc->cam_iam_it_Clusters*sc->cluster_size_Bytes))) {
		if (!sc->background) {
			ERRORF("malloc error in ext2_read_tables: %s.",strerror(errno));
			abort();
		}
		if (++counter==60) {
			ERRORF("malloc error in ext2_read_tables, gave up after 60 seconds: %s.",strerror(errno));
			abort();
		}
		sleep(1);
	}
//	eprintf("read_table_for_one_group(%u): %p->bb=%p\n",cmd_struct->group,list,list->block_buffer);
*/
	
//	block_t bo=sc->gdt[sc->table_reader_group].cluster_allocation_map*sc->cluster_size_Blocks;
//	block_t nb=sc->cam_iam_it_Blocks;
	
//	eprintf("reading %llu blocks @ offset %llu\n",(unsigned long long)nb,(unsigned long long)bo);
	
	u8 *    cb_b=sc->cluster_allocation_map+sc->cluster_size_Bytes*sc->table_reader_group;
	u8 *    ib_b=sc->inode_allocation_map+sc->sb->inodes_per_group/8*sc->table_reader_group;
	u8 *    it_b=(u8*)(sc->inode_table+sc->sb->inodes_per_group*(sc->table_reader_group%INODE_TABLE_ROUNDROUBIN));
	
	/*
	eprintf(
		"read_table_for_one_group: sc->table_reader_group=%u (0x%0x), sc->inode_table=%p => it_b=%p\n"
		,sc->table_reader_group
		,sc->table_reader_group
		,sc->inode_table
		,it_b
	);
	*/
	
	size_t  cb_n=sc->cluster_size_Bytes;
	size_t  ib_n=sc->sb->inodes_per_group/8;
	size_t  it_n=(sc->sb->inodes_per_group - sc->gdt[sc->table_reader_group].num_virgin_inodes) * sizeof(*sc->inode_table);
	
	block_t cb_a=sc->cluster_size_Blocks*sc->gdt[sc->table_reader_group].cluster_allocation_map;
	block_t ib_a=sc->cluster_size_Blocks*sc->gdt[sc->table_reader_group].inode_allocation_map;
	block_t it_a=sc->cluster_size_Blocks*sc->gdt[sc->table_reader_group].inode_table;
	
	block_t cb_s=sc->cluster_size_Blocks;
	block_t ib_s=sc->cluster_size_Blocks;
	block_t it_s=sc->cluster_size_Blocks*((it_n+sc->cluster_size_Bytes-1)/sc->cluster_size_Bytes);
	
	if (sc->gdt[sc->table_reader_group].flags & BG_INODE_ALLOCATION_MAP_UNINIT || cb_s != bdev_read(sc->bdev, cb_a, cb_s, cb_b)) {
		/*
		 * This error is in no way critical as we don't trust that 
		 * data anyways, we do this reading only to CHECK wether the 
		 * data are correct and inform the user if it wasn't.
		 */
		if (! (sc->gdt[sc->table_reader_group].flags & BG_INODE_ALLOCATION_MAP_UNINIT)) {
			NOTIFYF("%s: Error reading cluster allocation map for group %u (ignored, will be recalculated anyways ...)",sc->name,sc->table_reader_group);
		}
		// For VALGRIND:
		memset(cb_b,0,cb_n);
	}
	
	/*
	 * The in-memory copy of the inode allocation map only is big enough 
	 * for the actual needed bits. As reading can be done in chunks of 
	 * full block only, we may get problems in the last group. So we 
	 * mis-use the memory for the inode table here as target buffer for 
	 * the inode bitmap and then copy over the data to the right location.
	 */
	if (sc->gdt[sc->table_reader_group].flags & BG_CLUSTER_ALLOCATION_MAP_UNINIT || ib_s != bdev_read(sc->bdev, ib_a, ib_s, it_b)) {
		/*
		 * Again: Just ignore errors in reading of bitmap blocks.
		 */
		if (! (sc->gdt[sc->table_reader_group].flags & BG_CLUSTER_ALLOCATION_MAP_UNINIT)) {
			NOTIFYF("%s: Error reading inode allocation map for group %u (ignored, will be recalculated anyways ...)",sc->name,sc->table_reader_group);
		}
		// For VALGRIND:
		memset(ib_b,0,ib_n);
	} else {
		memcpy(ib_b,it_b,ib_n);
	}
	
#if 0
	/*
	 * Finally read the actual inode table.
	 *
	 * FIXME: There should be better error handling:
	 *  - try to read each block in turn, if bulk reading fails.
	 *  - each block which can't be read should be zeroed out.
	 */
	if (it_s!=bdev_read(sc->bdev,it_a,it_s,it_b)) {
		ERRORF("%s: Error reading inode table for group %u",sc->name,sc->table_reader_group);
		/*
		 * FIXME: Here goes the error handling ...
		 */
		memset(it_b,0,it_n);
	}
#endif // #if 0
	
	if (sc->gdt[sc->table_reader_group].flags & BG_INODE_TABLE_INITIALIZED) {
		/*
		 * The following implementation solves the problem stated in the 
		 * FIXME above, however it's a very poor solution and should be 
		 * replaced by something more sane. However, at least it works ...
		 */
		bool have_zero_errors=false;
		bool have_soft_errors=false;
		bool have_hard_errors=false;
		unsigned bs=bdev_get_block_size(sc->bdev);
		u8 error_bitmap[bs];
		bool first=true;
		for (block_t i=0;i<it_s;++i) {
			block_t r=bdev_read(sc->bdev,it_a,1,it_b);
//			eprintf("%s->read(%llu,1,%p)=%lli\n",bdev_get_name(sc->bdev),(unsigned long long)it_a,it_b,(int long long)r);
			if (1!=r) {
				have_zero_errors=true;
				r=bdev_short_read(sc->bdev,it_a,1,it_b,error_bitmap);
//				eprintf("%s->short_read(%llu,1,%p,%p)=%lli\n",bdev_get_name(sc->bdev),(unsigned long long)it_a,it_b,error_bitmap,(int long long)r);
				if (1!=r) {
					have_hard_errors=true;
					have_soft_errors=true;
					memset(it_b,0,bs);
				} else {
//					struct inode * it=(struct inode *)error_bitmap;
					int ni=bs/sizeof(struct inode);
					if (first) {
						first=false;
						// eprintf("Errors in fields: ");
					}
					for (int i=0;i<ni;++i) {
						/*
						eprintf("[it[i].links_count=%u]",it[i].links_count);
						if (it[i].mode&07777) eprintf("M");
						if (it[i].uid) eprintf("u");
						if (it[i].atime) eprintf("a");
						if (it[i].ctime) eprintf("c");
						if (it[i].mtime) eprintf("m");
						if (it[i].dtime) eprintf("d");
						if (it[i].gid) eprintf("g");
						if (it[i].links_count) eprintf("l");
						if (it[i].num_blocks) eprintf("b");
						if (it[i].flags) eprintf("f");
						if (it[i].translator) eprintf("t");
						if (it[i].generation) eprintf("e");
						if (it[i].mode_high) eprintf("H");
						if (it[i].uid_high) eprintf("U");
						if (it[i].gid_high) eprintf("G");
						if (it[i].author) eprintf("A");
						if (it[i].file_acl) eprintf("L");
						if (it[i].faddr) eprintf("D");
						if (it[i].frag) eprintf("R");
						if (it[i].fsize) eprintf("z");
						*/
						
						// it[i].mode&=~07777;
						// it[i].uid=0;
						// it[i].atime=0;
						// it[i].ctime=0;
						// it[i].mtime=0;
						// it[i].dtime=0;
						// it[i].gid=0;
						// it[i].links_count=0;
						// it[i].num_blocks=0;
						// it[i].flags=0;
						// it[i].translator=0;
						// it[i].generation=0;
						// it[i].mode_high=0;
						// it[i].uid_high=0;
						// it[i].gid_high=0;
						// it[i].author=0;
						// *** If file system doesn't use ACLs:
						// it[i].file_acl=0;
						// *** If file system doesn't use fragments:
						// it[i].faddr=0;
						// it[i].frag=0;
						// it[i].fsize=0;
					}
					for (unsigned i=0;i<bs;++i) {
						if (error_bitmap[i]) {
							have_soft_errors=true;
							memset(it_b,0,bs);
							break;
						}
					}
				}
			}
			it_a++;
			it_b+=bs;
		}
		if (!first) {
//			eprintf("\n");
		}
		if (have_zero_errors) {
			if (have_soft_errors) {
				if (have_hard_errors) {
					eprintf("\033[31m");
					ERRORF("%s: While reading inode table for group %u: Couldn't read all blocks of inode table :(",sc->name,sc->table_reader_group);
					eprintf("\033[0m");
				} else {
					eprintf("\033[31m");
					ERRORF("%s: While reading inode table for group %u: Couldn't read all blocks savely :(",sc->name,sc->table_reader_group);
					eprintf("\033[0m");
				}
			} else {
				NOTIFYF("%s: While reading inode table for group %u: Unproblematic read errors.",sc->name,sc->table_reader_group);
			}
		}
	}
	
	atomic_inc(&sc->prereaded);
	
	return true;
}

/*
 * CCE_LOCK(walk) must be held
 */
void comc_move_front(struct scan_context * sc, struct com_cache_entry * walk, bool new) {
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
	walk->entry=malloc(4*sc->comc.clusters_per_entry);
#ifdef COMC_IN_MEMORY
	if (!sc->comc.compr[walk->index].ptr) {
#else // #ifdef COMC_IN_MEMORY
	char * tmp;
	int fd=open(tmp=mprintf("/ramfs/comc/%04x",(unsigned)walk->index),O_RDONLY);
	if (fd<0) {
		if (errno!=ENOENT) {
			eprintf("cluster-owner-map-cache-thread: OOPS: open(»%s«): %s\n",tmp,strerror(errno));
			exit(2);
		}
		free(tmp);
#endif // #ifdef COMC_IN_MEMORY, else
		memset(walk->entry,0,4*sc->comc.clusters_per_entry);
//		eprintf("Init %5i (num_in_memory=%u)\n",walk->index,sc->comc.num_in_memory);
	} else {
//		eprintf("Read %5i (num_in_memory=%u)\n",walk->index,sc->comc.num_in_memory);
#ifdef COMC_IN_MEMORY
		Bytef * cbuffer=(Bytef *)sc->comc.compr[walk->index].ptr;
		uLongf  dbuflen=sc->comc.compr[walk->index].len;
#else // #ifdef COMC_IN_MEMORY
		if (CC_DEBUG_LOCKS) eprintf("%4i in thread %5i: Reading data by using fd %i\n",__LINE__,gettid(),fd);
		uLongf dbuflen=read(fd,cbuffer,cbuflen);
		if (close(fd)) {
			eprintf("cluster-owner-map-cache-thread: OOPS: close(»%s«): %s\n",tmp,strerror(errno));
			exit(2);
		}
		free(tmp);
#endif // #ifdef COMC_IN_MEMORY, else
		uLongf x=4*sc->comc.clusters_per_entry;
		if (Z_OK!=uncompress((Bytef*)walk->entry,&x,(Bytef*)cbuffer,dbuflen)) {
			eprintf("cluster-owner-map-cache-thread: OOPS: uncompress failed\n");
			exit(2);
		}
		if (x!=4*sc->comc.clusters_per_entry) {
			eprintf("cluster-owner-map-cache-thread: OOPS: uncompress failed\n");
			exit(2);
		}
	}
	
	walk->dirty=false;
	// END READING
	
	comc_move_front(sc, walk, true);
}

volatile bool exit_request_com_cache_thread=false;
// btlock_t wait_for_com_cache_thread;

THREAD_RETURN_TYPE com_cache_thread(void * arg) {
	struct scan_context * sc=(struct scan_context *)arg;
	
	eprintf("Executing com_cache_thread via thread %i\n",gettid());
	
	size_t cbuflen=compressBound(4*sc->comc.clusters_per_entry);
	Bytef * cbuffer=malloc(cbuflen);
	
	while (!exit_request_com_cache_thread) {
		bool nothing_done=true;
		CC_LOCK(sc->comc);
		if (sc->comc.read_head) {
			nothing_done=false;
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
						
//						eprintf("Sync %5i (num_in_memory=%u)\n",walk->index,sc->comc.num_in_memory);
						
						// BEGIN WRITING
						uLongf dbuflen=cbuflen;
						if (Z_OK!=compress2((Bytef*)cbuffer,&dbuflen,(Bytef*)walk->entry,4*sc->comc.clusters_per_entry,1)) {
							eprintf("cluster-owner-map-cache-thread: OOPS: compress2 failed\n");
							exit(2);
						}
#ifdef COMC_IN_MEMORY
						free(sc->comc.compr[walk->index].ptr);
						memcpy(sc->comc.compr[walk->index].ptr=malloc(dbuflen),cbuffer,sc->comc.compr[walk->index].len=dbuflen);
#else // #ifdef COMC_IN_MEMORY
						char * tmp;
						int fd=open(tmp=mprintf("/ramfs/comc/%04x",(unsigned)walk->index),O_CREAT|O_TRUNC|O_WRONLY,0666);
						if (fd<0) {
							eprintf("cluster-owner-map-cache-thread: OOPS: open(»%s«): %s\n",tmp,strerror(errno));
							exit(2);
						} else {
							if (CC_DEBUG_LOCKS) eprintf("%4i in thread %5i: Writing data by using fd %i\n",__LINE__,gettid(),fd);
						}
						if ((ssize_t)dbuflen!=write(fd,cbuffer,dbuflen)) {
							eprintf("cluster-owner-map-cache-thread: OOPS: write(»%s«): %s\n",tmp,strerror(errno));
							exit(2);
						}
						if (close(fd)) {
							eprintf("cluster-owner-map-cache-thread: OOPS: close(»%s«): %s\n",tmp,strerror(errno));
							exit(2);
						}
						free(tmp);
#endif // #ifdef COMC_IN_MEMORY, else
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
		if (nothing_done) {
			CCT_WAIT();
		}
	}
	free(cbuffer);
	exit_request_com_cache_thread=false;
	GENERAL_BARRIER();
	CCT_WAKE();
	
	eprintf("Returning from com_cache_thread (exiting cluster-owner-map-cache-thread)\n");
	
	return 0;
}

struct com_cache_entry * get_com_cache_entry(struct scan_context * sc,size_t ccg,u32 cluster) {
/*
	CC_LOCK(sc->comc);
	if (!sc->comc.ccgroup[ccg]) {
		sc->comc.ccgroup[ccg]=malloc(sizeof(*sc->comc.ccgroup[ccg]));
		sc->comc.ccgroup[ccg]->index=ccg;
		do {
			CCE_LOCK_MK(sc->comc.ccgroup[ccg]);
		} while (!sc->comc.ccgroup[ccg]->debug_lock);
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
		do {
			CCE_LOCK_MK(walk);
		} while (!walk->debug_lock);
		walk->num_in_use=1;
		walk->next=NULL;
		walk->prev=sc->comc.read_tail;
		CCE_LOCK(walk);
#ifdef USE_COMC_THREAD_TO_READ
		if (!sc->comc.read_tail) {
			sc->comc.read_head=walk;
		} else {
			sc->comc.read_tail->next=walk;
		}
		sc->comc.read_tail=walk;
		// A little bit strange. We lock this here and don't unlock it again. That 
		// will be done by the reader thread. This allows us to wait for it by a 
		// simple double locking.
		CCT_WAKE(); // Make sure, we won't wait senselessly for the com_cache_thread.
		CC_UNLOCK(sc->comc);
		CCE_LOCK(walk);
#else // #ifdef USE_COMC_THREAD_TO_READ
		++sc->comc.num_in_memory;
		CC_UNLOCK(sc->comc);
		
		comc_get(sc, walk);
#endif // #ifdef USE_COMC_THREAD_TO_READ, else
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
	if (1 == ccge->num_in_use) {
		comc_move_front(sc, ccge, false);
	}
	CCE_UNLOCK(ccge);
	CC_LOCK(sc->comc);
	--ccge->num_in_use;
	CC_UNLOCK(sc->comc);
}

/*
 * This is proven to be correct for inode allocation map, 
 * but MAY be incorrect for block allocation map.
 */
#define bit2byte(o) (o>>3)
#define bit2mask(o) (1<<(o&7))

static inline void set_bit(u8 * bitmap,u32 o) {
	bitmap[bit2byte(o)]|=bit2mask(o);
}

static inline bool get_bit(u8 * bitmap,u32 o) {
	return bitmap[bit2byte(o)]&bit2mask(o);
}

#define get_calculated_inode_allocation_map_bit(sc,inode_num) get_bit(sc->calculated_inode_allocation_map,inode_num-1)
#define set_calculated_inode_allocation_map_bit(sc,inode_num) set_bit(sc->calculated_inode_allocation_map,inode_num-1)

#define get_calculated_cluster_allocation_map_bit(sc,cluster_num) get_bit(sc->calculated_cluster_allocation_map,cluster_num-sc->cluster_offset)
#define set_calculated_cluster_allocation_map_bit(sc,cluster_num) set_bit(sc->calculated_cluster_allocation_map,cluster_num-sc->cluster_offset)

#define get_cluster_allocation_map_bit(sc,cluster_num) get_bit(sc->calculated_cluster_allocation_map,cluster_num-sc->cluster_offset)

//		NOTIFYF("%s: Cluster %llu used multiple times.",name,(unsigned long long)cluster);

#define break_on_cluster(cluster) (false \
/*	||  cluster==      627 \
	||  cluster==    18703 \
	||  cluster==    18704 \
	||  cluster==  6488092 \
	||  cluster==  6488093 \
	||  cluster==  8331277 \
	||  cluster==  8331278 \
	||  cluster== 11157515 \
	||  cluster== 11157516 \
	||  cluster== 11157519 \
	||  cluster== 11157520 \
	||  cluster== 11976715 \
	||  cluster== 11976716 \
	||  cluster== 12025867 \
	||  cluster== 12025868 \
	||  cluster== 12042251 \
	||  cluster== 12042252 \
	||  cluster== 12058635 */ \
)

#define cluster_to_cci(cluster) (((cluster)-sc->cluster_offset)%sc->comc.clusters_per_entry)
#define cluster_to_ccg(cluster) (((cluster)-sc->cluster_offset)/sc->comc.clusters_per_entry)

u32 set_owner(struct scan_context * sc,u32 cluster,u32 inode) {
	// size_t index=cluster%sc->comc.clusters_per_entry;
	// struct com_cache_entry * ccge=get_com_cache_entry(sc,cluster/sc->comc.clusters_per_entry);
	
	size_t index=cluster_to_cci(cluster);
	struct com_cache_entry * ccge=get_com_cache_entry(sc,cluster_to_ccg(cluster),cluster);
	
	u32 retval=ccge->entry[index];
	if (break_on_cluster(cluster)) { \
		NOTIFYF("%s: set_owner(cluster=%lu,inode=%li)=%li",sc->name,(unsigned long)cluster,(long)inode,(long)retval);
	}
	ccge->entry[index]=inode;
	ccge->dirty=true;
	release(sc,ccge);
	return retval;
}

u32 get_owner(struct scan_context * sc,u32 cluster) {
	size_t index=cluster_to_cci(cluster);
	struct com_cache_entry * ccge=get_com_cache_entry(sc,cluster_to_ccg(cluster),cluster);
	
	u32 retval=ccge->entry[index];
	release(sc,ccge);
	if (break_on_cluster(cluster)) {
		NOTIFYF("%s: get_owner(cluster=%lu)=%li",sc->name,(unsigned long)cluster,(long)retval);
	}
	return retval;
}

/*
	CL_LOCK(sc); \
	get_calculated_cluster_allocation_map_bit(sc,__cluster)) { \
		set_calculated_cluster_allocation_map_bit(sc,__cluster); \
	CL_UNLOCK(sc); \
	
	eprintf( \
		"%4i in thread %5i: cluster[%llu]=%lu\n" \
		,__LINE__ \
		,gettid() \
		,(unsigned long long)__cluster \
		,(unsigned long)__inode \
	); \
*/

#define MARK_CLUSTER_IN_USE_BY(sc,_cluster,_inode) do { \
	u64 __cluster=(_cluster); \
	u32 __inode=(_inode); \
	u32 old_owner=set_owner(sc,__cluster,__inode); \
	if (old_owner) { \
		NOTIFYF( \
			"%s: Cluster %llu used multiple times, by %lu and %lu." \
			,sc->name \
			,(unsigned long long)__cluster \
			,(unsigned long)old_owner \
			,(unsigned long)__inode \
		); \
	} \
} while (0)

u32 set_owners(struct scan_context * sc,block_t * cp,size_t * num,u32 inode) {
	size_t index=cluster_to_cci(*cp);
	struct com_cache_entry * ccge=get_com_cache_entry(sc,cluster_to_ccg(*cp),*cp);
	
	u32 retval=0;
	while (likely(!retval)
	&&     likely(index<sc->comc.clusters_per_entry)
	&&     likely(*num)) {
		retval=ccge->entry[index];
		if (break_on_cluster(*cp)) {
			NOTIFYF("%s: set_owner(cluster=%lu,inode=%li)=%li",sc->name,(unsigned long)*cp,(long)inode,(long)retval);
		}
		ccge->entry[index]=inode;
		++index;
		++*cp;
		--*num;
	}
	ccge->dirty=true;
	release(sc,ccge);
	return retval;
}

/*
		eprintf("Calling set_owners(sc,&%llu,&%lu,inode=%li\n",(unsigned long long)c,(unsigned long)num,(long)inode); \
		eprintf("Returned from set_owners, num=%li now\n",(long)num); \
*/

#define MARK_CLUSTERS_IN_USE_BY(sc,_c,_num,_inode) do { \
	block_t c=(_c); \
	size_t num=(_num); \
	u32 inode=(_inode); \
	while (num) { \
		u32 old_owner=set_owners(sc,&c,&num,inode); \
		if (old_owner) { \
			NOTIFYF( \
				"%s: Cluster %llu used multiple times, by %lu and %lu." \
				,sc->name \
				,(unsigned long long)c-1 \
				,(unsigned long)old_owner \
				,(unsigned long)inode \
			); \
		} \
	} \
} while (0)

THREAD_RETURN_TYPE ext2_read_tables(void * arg) {
	struct scan_context * sc=(struct scan_context *)arg;
	
	eprintf("Executing read_tables via thread %i\n",gettid());
	
	OPEN_PROGRESS_BAR_LOOP(0,sc->table_reader_group,sc->num_groups)
	
		read_table_for_one_group(sc);
		
		++sc->table_reader_group;
		
		TRE_WAKE();
		
		PRINT_PROGRESS_BAR(sc->table_reader_group);
	CLOSE_PROGRESS_BAR_LOOP()
	
	if (sc->background) {
		NOTIFYF("%s: Background reader is successfully quitting",sc->name);
		--live_child;
	}
	
	eprintf("Returning from ext2_read_tables thread (exiting)\n");
	
	THREAD_RETURN();
}

void process_dir_cluster(struct inode_scan_context * isc,unsigned long cluster) {
	u8 buffer[isc->sc->cluster_size_Bytes];
	void * ptr=(void *)buffer;
	
	if (isc->sc->cluster_size_Blocks!=bdev_read(
		isc->sc->bdev,cluster*isc->sc->cluster_size_Blocks,isc->sc->cluster_size_Blocks,ptr)
	) {
		ERRORF("%s: Inode %lu [%s]: Error while reading directory cluster %lu",isc->sc->name,isc->inode_num,isc->type,cluster);
		return;
	}
	
	/*
	 * Structure of a record:
	 *
	 * u32 inode;
	 * u16 reclen;
	 * u8  filename_len;
	 * u8  filetype;
	 * u8  filename[(filename_len+3)&-4];
	 * 
	 * The filename is NOT null-terminated.
	 * If inode is zero, the entry doesn't describe any object (maybe it's 
	 * left over after a delete) and can just be ignored (or used to store 
	 * a new file name). The last entry uses the entire space of it's 
	 * containing cluster so filling it up. The next entry starts in the 
	 * next cluster.
	 */
	
	unsigned long offset=0;
	
	while (offset<isc->sc->cluster_size_Bytes) {
		u32 inode=0
			+((u32)buffer[offset+0]    )
			+((u32)buffer[offset+1]<< 8)
			+((u32)buffer[offset+2]<<16)
			+((u32)buffer[offset+3]<<24)
		;
		u16 reclen=0
			+((u32)buffer[offset+4]    )
			+((u32)buffer[offset+5]<< 8)
		;
		if (!inode) {
//			NOTIFYF("%s: Inode %lu [%s]: Cluster %lu: Free directory record of length %u bytes.",isc->sc->name,isc->inode_num,isc->type,cluster,reclen);
			if (reclen < 8) {
				ERRORF("%s: Inode %lu [%s]: reclen is to small. reclen=%u - skipping rest of cluster.",isc->sc->name,isc->inode_num,isc->type,(unsigned)reclen);
				offset = isc->sc->cluster_size_Bytes; // Skip rest of cluster to prevent endless loop
			} else {
				offset+=reclen;
			}
			continue;
		}
		
		u8 filename_len=0
			+((u32)buffer[offset+6]    )
		;
		u8 filetype=0
			+((u32)buffer[offset+7]    )
		;
		char * filename=malloc(filename_len+1);
		assert(filename);
		for (unsigned i=0;i<filename_len;++i) {
			filename[i]=buffer[offset+8+i];
		}
		filename[filename_len]=0;
		offset+=reclen;
//		NOTIFYF("%s: Inode %lu [%s]: Cluster %lu: Directory entry pointing to inode %lu: »%s«.",isc->sc->name,isc->inode_num,isc->type,cluster,inode,filename);
		
		if (inode<=isc->sc->sb->num_inodes) {
//			++isc->sc->link_count[inode-1];
			++isc->sc->calculated_link_count_array[inode-1];
		} else {
			ERRORF("%s: Inode %lu [%s]: Entry \"%s\" points to illegal inode %lu.",isc->sc->name,isc->inode_num,isc->type,filename,(unsigned long)inode);
		}
		
		/*
		NOTIFYF(
			"%s: Inode %lu [%s]: Entry to inode %lu, reclen=%u, filetype=%u, filename_len=%u, filename=\"%s\""
			,isc->sc->name,isc->inode_num,isc->type
			,(unsigned long)inode,(unsigned)reclen,(unsigned)filetype,(unsigned)filename_len,filename
		);
		*/
		
		if (filename_len+8>reclen) {
			ERRORF("%s: Inode %lu [%s]: reclen is to small for filename. reclen=%u, decoded filename of size %u: \"%s\" - skipping rest of cluster.",isc->sc->name,isc->inode_num,isc->type,(unsigned)reclen,(unsigned)filename_len,filename);
			offset = isc->sc->cluster_size_Bytes; // Skip rest of cluster to prevent endless loop
		}
		
		struct dirent_list * dl;
		struct dirent_node * dn;
		
		dn=malloc(sizeof(*dn));
		assert(dn);
		if (unlikely(!isc->sc->dir[isc->inode_num-1])) {
			dl=(struct dirent_list *)(isc->sc->dir[isc->inode_num-1]=malloc(sizeof(struct dirent_list)));
			assert(isc->sc->dir[isc->inode_num-1]);
			
			dl->frst=dl->last=dn;
		} else {
			dl=(struct dirent_list *)isc->sc->dir[isc->inode_num-1];
			dl->last->next=dn;
		}
		dl->last=dn;
		dn->next=NULL;
		dn->dirent.inode=inode;
		dn->dirent.filetype=filetype;
		dn->dirent.filename=filename;
	}
}

static inline void isc_schedule_cluster_init(struct inode_scan_context * isc) {
	isc->schedule_first_cluster=0;
	isc->schedule_num_clusters=0;
}

static inline void isc_schedule_cluster_flush(struct inode_scan_context * isc) {
	if (isc->schedule_num_clusters) {
		MARK_CLUSTERS_IN_USE_BY(isc->sc,isc->schedule_first_cluster,isc->schedule_num_clusters,isc->inode_num);
	}
}

static inline void isc_schedule_cluster_add(struct inode_scan_context * isc,unsigned long cluster) {
	if (likely((isc->schedule_first_cluster+isc->schedule_num_clusters)==cluster)) {
		++isc->schedule_num_clusters;
		return;
	}
	isc_schedule_cluster_flush(isc);
	isc->schedule_first_cluster=cluster;
	isc->schedule_num_clusters=1;
//	MARK_CLUSTER_IN_USE_BY(isc->sc,cluster,isc->inode_num);
}

void chk_block(struct inode_scan_context * isc,int level,unsigned long cluster) {
//	eprintf("chk_block(level=%i,cluster=%lu)\n",level,cluster);
	if (break_on_cluster(cluster)) {
		NOTIFYF(
			"%s: Inode %lu [%s]: chk_block on cluster %lu (for inode %lu)."
			,isc->sc->name,isc->inode_num,isc->type
			,cluster,(unsigned long)isc->inode_num
		);
	}
	
	if (unlikely(!cluster)) {
		unsigned long h=1<<(level*isc->sc->num_clusterpointers_shift);
		
		isc->maybe_holes+=h;
		
		return;
	}
	
	isc->holes+=isc->maybe_holes;
	isc->maybe_holes=0;
	
	if (unlikely(cluster>=isc->sc->sb->num_clusters)) {
		ERRORF(
			"%s: Inode %lu [%s]: Illegal cluster %llu"
			,isc->sc->name,isc->inode_num,isc->type
			,(unsigned long long)cluster
		);
		++isc->illegal_ind_clusters[level];
		return;
	}
	
	isc_schedule_cluster_add(isc,cluster);
	
	if (isc->is_dir && !level) {
		process_dir_cluster(isc,cluster);
	}
	
	++isc->used_clusters;
	
	if (likely(!level)) return;
	
	u32 pointer[isc->sc->num_clusterpointers];
	void * ptr=(void *)pointer;
	
	if (isc->sc->cluster_size_Blocks!=bdev_read(isc->sc->bdev,cluster*isc->sc->cluster_size_Blocks,isc->sc->cluster_size_Blocks,ptr)) {
		ERRORF("%s: Inode %lu [%s]: Error while reading indirect cluster %lu",isc->sc->name,isc->inode_num,isc->type,cluster);
	} else {
		for (unsigned i=0;i<isc->sc->num_clusterpointers;++i) {
			chk_block(isc,level-1,pointer[i]);
		}
	}
}

u32 chk_extent_block(struct inode_scan_context * isc, u32 file_cluster, char * eb, size_t size, unsigned depth) {
	bool in_inode = size == 60;
	
	if (!in_inode) {
		++isc->used_clusters;
	}
	
	struct extent_header * eh;
	struct extent_tail * et;
	
	eh = (struct extent_header *)eb; eb += 12; size -= 12;
	
	if (eh->magic != EXTENT_HEADER_MAGIC) {
		ERRORF("%s: Inode %lu [%s]: %s extent header has wrong magic value (0x%04x, shoud be 0x%04x).",isc->sc->name,isc->inode_num,isc->type,in_inode?"In-inode":"External",(unsigned)eh->magic,EXTENT_HEADER_MAGIC);
	}
	
	unsigned num = eh->num_entries;
	unsigned max = eh->max_entries;
	
	if (num > max) {
		ERRORF("%s: Inode %lu [%s]: %s extent header denotes a num entries value (%u) > max entries value (%u).",isc->sc->name,isc->inode_num,isc->type,in_inode?"In-inode":"External",num,max);
		num = max;
	}
	
	if (max > (in_inode ? 4 : isc->sc->cluster_size_Bytes / 12 - 1)) {
		ERRORF("%s: Inode %lu [%s]: %s extent header denotes a max entries value > 4: %u.",isc->sc->name,isc->inode_num,isc->type,in_inode?"In-inode":"External",max);
		max = 4;
		
		if (num > max) {
			ERRORF("%s: Inode %lu [%s]: %s extent header denotes a num entries value (%u) > max entries value (%u).",isc->sc->name,isc->inode_num,isc->type,in_inode?"In-inode":"External",num,max);
			num = max;
		}
	}
	
	if (!in_inode && eh->depth != depth) {
		ERRORF("%s: Inode %lu [%s]: External extent header's depth field is %u, should be %u,",isc->sc->name,isc->inode_num,isc->type,eh->depth,depth);
	}
	
	char * block_buffer = malloc(isc->sc->cluster_size_Bytes);
	if (!block_buffer) {
		ERRORF("%s: Inode %lu [%s]: While processing %s extent block: Cannot recurse extent structure: No memory left.",isc->sc->name,isc->inode_num,isc->type,in_inode?"in-inode":"external");
		return file_cluster;
	}
	
	if (eh->depth) {
		struct extent_index * ei = (struct extent_index *)eb;
		
		for (; num--; ++ei) {
			if (ei->file_cluster != file_cluster) {
				if (ei->file_cluster < file_cluster) {
					ERRORF("%s: Inode %lu [%s]: While processing %s extent index: Oops, unsorted extent information detected ...",isc->sc->name,isc->inode_num,isc->type,in_inode?"in-inode":"external");
				/*
				} else {
					NOTIFYF("%s: Inode %lu [%s]: While processing %s extent index: File hole detected.",isc->sc->name,isc->inode_num,isc->type,in_inode?"in-inode":"external");
				*/
				}
			}
			
			block_t leaf = isc->sc->cluster_size_Blocks * ((block_t)ei->leaf_lo | ((block_t)ei->leaf_hi << 32));
			
			if (isc->sc->cluster_size_Blocks != bdev_read(isc->sc->bdev,leaf,isc->sc->cluster_size_Blocks,(unsigned char *)block_buffer)) {
				ERRORF("%s: Inode %lu [%s]: While processing %s extent index: Read error while trying to read an extent cluster.",isc->sc->name,isc->inode_num,isc->type,in_inode?"in-inode":"external");
				// TODO: Read and process as much as possible block-wise
				continue;
			}
			
			endian_swap_extent_block(block_buffer, isc->sc->cluster_size_Blocks);
			
			file_cluster = chk_extent_block(isc, ei->file_cluster, block_buffer, isc->sc->cluster_size_Bytes, eh->depth - 1);
		}
	} else {
		struct extent_descriptor * ed = (struct extent_descriptor *)eb;
		
		for (; num--; ++ed) {
			if (ed->file_cluster != file_cluster) {
				if (ed->file_cluster < file_cluster) {
					ERRORF("%s: Inode %lu [%s]: While processing %s extent descriptor: Oops, unsorted extent information detected ...",isc->sc->name,isc->inode_num,isc->type,in_inode?"in-inode":"external");
				/*
				} else {
					NOTIFYF("%s: Inode %lu [%s]: While processing %s extent descriptor: File hole detected.",isc->sc->name,isc->inode_num,isc->type,in_inode?"in-inode":"external");
				*/
				}
			}
			
			if (!ed->len) {
				ERRORF("%s: Inode %lu [%s]: In %s extend descriptor: Bogus length field (0).",isc->sc->name,isc->inode_num,isc->type,in_inode?"in-inode":"external");
			}
			
			unsigned real_length = (ed->len <= 0x8000) ? ed->len : (ed->len & 0x7fff);
			file_cluster = ed->file_cluster + real_length;
			
			block_t disc_cluster = (block_t)ed->disk_cluster_lo | ((block_t)ed->disk_cluster_hi << 16);
			for (unsigned num = real_length; num--; ) {
				chk_block(isc,0,disc_cluster++);
			}
		}
	}
	free(block_buffer);
	
	// TODO: eh->generation ???
	
	eb += 12 * (size / 12);
	et = (struct extent_tail *)eb;
	
	/*
	if (size % 12) {
		// TODO: Verify checksum
	}
	*/
	
	return file_cluster;
}

struct thread {
	struct thread * volatile next;
	struct thread * volatile prev;
	struct btlock_lock * lock;
#ifdef BTCLONE
	void * stack_memory;
	pid_t tid;
#else // #ifdef BTCLONE
	pthread_t thread;
#endif // #ifdef BTCLONE, else
	void * arg;
};

struct thread * thread_head=NULL;
struct thread * thread_tail=NULL;

// bool do_chk_block_function(struct inode_scan_context * isc) {
THREAD_RETURN_TYPE chk_block_function(void * arg) {
	struct thread * t=(struct thread *)arg;
	
	if (t->lock) {
		btlock_lock(t->lock);
		btlock_unlock(t->lock);
	}
	
	struct inode_scan_context * isc=(struct inode_scan_context *)t->arg;
	
	/*
	if (t->lock) {
		NOTIFYF(
			"%s: Inode %lu [%s]: Background job started."
			,isc->sc->name
			,isc->inode_num
			,isc->type
		);
	}
	*/
	
	isc_schedule_cluster_init(isc);
	
	if (!(isc->inode->flags & INOF_EXTENTS)) {
		for (unsigned z=0;z<NUM_ZIND;++z) {
			chk_block(isc,0,isc->inode->cluster[z]);
		}
		
		chk_block(isc,1,isc->inode->cluster[FIRST_SIND]);
		/*
		if (inode->cluster[FIRST_DIND] || inode->cluster[FIRST_TIND]) {
			PRINT_PROGRESS_BAR(inode_num);
		}
		*/
		chk_block(isc,2,isc->inode->cluster[FIRST_DIND]);
		/*
		if (inode->cluster[FIRST_DIND] || inode->cluster[FIRST_TIND]) {
			PRINT_PROGRESS_BAR(inode_num);
		}
		*/
		chk_block(isc,3,isc->inode->cluster[FIRST_TIND]);
	} else {
		chk_extent_block(isc, 0, (char *)isc->inode->cluster, 60, 0);
	}
	
	isc_schedule_cluster_flush(isc);
	
	unsigned long illegal_clusters=0;
	for (int i=0;i<4;++i) {
		illegal_clusters    += isc->illegal_ind_clusters[i];
	}
	
	if (isc->is_dir) {
		unsigned ino=isc->inode_num-1;
		
		// Every directory points back to itself, so increment link count
//		++isc->sc->calculated_link_count_array[ino];
		// -> will be done upon reading the '.' entry, which is already done in process_dir_cluster
		
		if (unlikely(!isc->sc->dir[ino])) {
			/*
			 * We report missing . and .. entries only, if the 
			 * directory doesn't contain illegal clusters.
			 *
			 * TODO: Same shall apply, if at least one cluster 
			 * couldn't be read.
			 */
			if (unlikely(!illegal_clusters)) {
				ERRORF("%s: Inode %lu [%s]: Empty directory is missing it's . and .. entry.",isc->sc->name,isc->inode_num,isc->type);
			}
			goto skip_dir;
		}
		
		
		
		struct dir * d=malloc(sizeof(*d));
		assert(d);
		
		d->num_entries=0;
		
		struct dirent_list * dl=(struct dirent_list *)isc->sc->dir[isc->inode_num-1];
		struct dirent_node * dn=dl->frst;
		size_t filenames_storage=0;
		
		while (dn) {
			++d->num_entries;
			filenames_storage+=1+strlen(dn->dirent.filename);
			dn=dn->next;
		}
		
		d->entry=malloc(sizeof(*d->entry)*d->num_entries+filenames_storage);
		assert(d->entry);
		filenames_storage=sizeof(*d->entry)*d->num_entries;
		
		dn=dl->frst;
		for (unsigned i=0;i<d->num_entries;++i) {
			size_t sl=strlen(dn->dirent.filename);
			char * dst=filenames_storage+(char *)d->entry;
			memcpy(dst,dn->dirent.filename,sl+1);
			free(dn->dirent.filename);
			dn->dirent.filename=dst;
			filenames_storage+=1+sl;
			
			memcpy(
				(char *)&d->entry[i],
				(char *)&dn->dirent,
				sizeof(dn->dirent)
			);
			
			dn=dn->next;
			free(dl->frst);
			dl->frst=dn;
			
			// Increment link count for the pointed to file
//			++isc->sc->calculated_link_count_array[d->entry[i].inode-1]; <--- already done in process_dir_cluster
			
			if (strchr(dst,'/')) {
				ERRORF(
					"%s: Inode %lu [%s]: Filename \"%s\" of entry %u contains a slash."
					,isc->sc->name,isc->inode_num,isc->type
					,dst
					,i+1
				);
			}
			// Can't check d->entry[entry].filetype right now.
		}
		free(dl);
		isc->sc->dir[isc->inode_num-1]=d;
	}
	
skip_dir:
	if (illegal_clusters) {
		ERRORF(
			"%s: Inode %lu [%s] contains %lu illegal clusters "
			"(%lu zind, %lu sind, %lu dind, %lu tind)."
			,isc->sc->name,isc->inode_num,isc->type
			,illegal_clusters
			,isc->illegal_ind_clusters[0],isc->illegal_ind_clusters[1]
			,isc->illegal_ind_clusters[2],isc->illegal_ind_clusters[3]
		);
//		isc->ok=false;
	}
	
	if (isc->inode->size < isc->used_clusters + isc->holes && !(isc->inode->flags & INOF_EOFBLOCKS)) {
		ERRORF("%s: Inode %lu [%s]: size < position addressed by highest allocated cluster",isc->sc->name,isc->inode_num,isc->type);
//		isc->ok=false;
	}
	
	if (isc->inode->num_blocks%isc->sc->cluster_size_Blocks) {
		ERRORF("%s: Inode %lu [%s]: num_blocks is not a multiple of the cluster size",isc->sc->name,isc->inode_num,isc->type);
//		isc->ok=false;
	}
	
	unsigned long used_blocks=isc->used_clusters*isc->sc->cluster_size_Blocks;
	if (isc->inode->num_blocks!=used_blocks) {
		ERRORF("%s: Inode %lu [%s]: num_blocks is %lu, counted %lu",isc->sc->name,isc->inode_num,isc->type,(unsigned long)isc->inode->num_blocks,used_blocks);
//		isc->ok=false;
	}
	
	/*
	bool retval=isc->sc->waiting4threads;
	
	return retval;
}

	do_chk_block_function(isc);
	*/
	
	if (isc->sc->waiting4threads) {
		if (t->lock) {
			NOTIFYF(
				"%s: Inode %lu [%s]: Background job completed."
				,isc->sc->name
				,isc->inode_num
				,isc->type
			);
		}
	}
	
	free(isc->inode);
	free(isc);
	
	THREAD_RETURN();
}

void cluster_scan_inode(struct inode_scan_context * isc) {
	u64 size=isc->inode->size;
	
	if (isc->type_bit&FTB_FILE) {
		size|=(u64)isc->inode->size_high<<32;
	}
	
	if (!isc->type_bit) {
		free(isc);
		return;
	}
	
	for (int i=0;i<4;++i) isc->illegal_ind_clusters[i]=0;
	isc->used_clusters=0;
	isc->maybe_holes=0;
	isc->holes=0;
	
	struct thread * t=malloc(sizeof(*t));
	if (!t) {
		eprintf("cluster_scan_inode: OOPS: malloc(t) failed: %s\n",strerror(errno));
		exit(2);
	}
	
	t->lock=btlock_lock_mk();
	if (!t->lock) {
		eprintf("cluster_scan_inode: OOPS: btlock_lock_mk failed: %s\n",strerror(errno));
		exit(2);
	}
	
	// First we lock ourself, so we don't get hurt in the very last moment ...
	btlock_lock(t->lock);
	
	t->arg=isc;
	
	bool try_clone;
	
	{
		struct inode * i=malloc(sizeof(*i));
		*i=*isc->inode;
		isc->inode=i;
	}
	
	if ((live_child<hard_child_limit && isc->inode->cluster[FIRST_TIND])
	||  (live_child<soft_child_limit && isc->inode->cluster[FIRST_DIND])) {
		try_clone=true;
		/*
		struct inode * i=malloc(sizeof(*i));
		if (unlikely(!i)) {
			try_clone=false;
		} else {
			*i=*isc->inode;
			isc->inode=i;
		}
		*/
	} else {
		try_clone=false;
	}
	
#ifdef BTCLONE
	if (!try_clone || (t->tid=btclone(
		&t->stack_memory,
		chk_block_function,
		65536,
		CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD,
		(void *)t
	))<=0) {
#else // #ifdef BTCLONE
	if (!try_clone || pthread_create(
		&t->thread,
		NULL,
		chk_block_function,
		(void *)t
	)) {
#endif // #ifdef BTCLONE, else
		// if (errno!=ENOSYS) eprintf("btclone failed: %m\n");
		btlock_unlock(t->lock);
		btlock_lock_free(t->lock);
		t->lock=NULL;
		chk_block_function(t);
		/*
		if (unlikely(try_clone)) {
			free(isc->inode);
			isc->inode=NULL;
		}
		*/
		free(t);
	} else {
		++live_child;
		/*
		 * Now, we link us to the thread list. We assure, that the last element 
		 * remains the last and the first remains the first. We will be the second 
		 * last element after this operation.
		 */
		
		btlock_lock(thread_tail->lock); // thread_tail doesn't change, so this is safe.
		btlock_lock(thread_tail->prev->lock);
		
		/*
		 * Now, we are fine, we can just link us as we do with any doubly linked list.
		 */
		t->prev=thread_tail->prev;
		t->next=thread_tail;
		t->prev->next=t;
		t->next->prev=t;
		
		if (isc->sc->waiting4threads) NOTIFYF("%s: Inode %lu [%s]: Processing inode's cluster allocation check in background",isc->sc->name,isc->inode_num,isc->type);
		
		/*
		 * Unlocking is straight forward: Just do it ;)
		 */
		btlock_unlock(t->lock);
		btlock_unlock(t->next->lock);
		btlock_unlock(t->prev->lock);
		
		{
			struct thread * walk=thread_head->next;
			while (walk->next) {
#ifdef BTCLONE
				if (kill(walk->tid,0)) {
					free(walk->stack_memory);
#else // #ifdef BTCLONE
				if (!pthread_tryjoin_np(walk->thread,NULL)) {
#endif // #ifdef BTCLONE, else
					walk->next->prev=walk->prev;
					walk->prev->next=walk->next;
					btlock_lock_free(walk->lock);
					struct thread * w=walk;
					walk=walk->prev;
					free(w);
					--live_child;
				}
				walk=walk->next;
			}
		}
	}
}

void do_typecheck_inode(struct scan_context * sc,u32 inode_num,struct inode * * _inode,char * * _type_str,unsigned * _type_bit) {
	struct inode * inode;
	char * type_str;
	unsigned type_bit;
	
	{
		u32 i=inode_num-1;
		inode=sc->inode_table+i%sc->sb->inodes_per_group+sc->sb->inodes_per_group*((i/sc->sb->inodes_per_group)%INODE_TABLE_ROUNDROUBIN);
	}
	
	if (!inode->links_count) {
		type_bit=0;
		if (inode->mode) {
			type_str="DELE";
			if (!inode->dtime) {
				if (sc->warn_dtime_zero) {
					NOTIFYF(
						"%s: Inode %lu [%s] (deleted) has zero dtime <auto-fixed>"
						,sc->name,(unsigned long)inode_num,type_str
					);
				}
				inode->dtime=sc->sb->wtime;
			}
		} else {
			type_str="FREE";
		}
	} else {
		switch (MASK_FT(inode)) {
			case MF_BDEV: type_str="BDEV"; type_bit=FTB_BDEV; break;
			case MF_CDEV: type_str="CDEV"; type_bit=FTB_CDEV; break;
			case MF_DIRE: type_str="DIRE"; type_bit=FTB_DIRE; break;
			case MF_FIFO: type_str="FIFO"; type_bit=FTB_FIFO; break;
			case MF_SLNK: type_str="SLNK"; type_bit=FTB_SLNK; break;
			case MF_FILE: type_str="FILE"; type_bit=FTB_FILE; break;
			case MF_SOCK: type_str="SOCK"; type_bit=FTB_SOCK; break;
			default     : type_str="ILLT"; type_bit=       0; break;
		}
	}
	
	if (_inode) *_inode=inode;
	if (_type_str) *_type_str=type_str;
	if (_type_bit) *_type_bit=type_bit;
}

void typecheck_inode(struct inode_scan_context * isc) {
	do_typecheck_inode(isc->sc,isc->inode_num,&isc->inode,&isc->type,&isc->type_bit);
}

void check_inode(struct scan_context * sc,unsigned long inode_num,bool prefetching) {
	{
		unsigned long i = inode_num - 1;
		unsigned long g = i / sc->sb->inodes_per_group;
		i %= sc->sb->inodes_per_group;
		
		if (i >= sc->sb->inodes_per_group - sc->gdt[g].num_virgin_inodes) {
			return;
		}
	}
	
	struct inode_scan_context * isc=malloc(sizeof(*isc));
	assert(isc);
	isc->sc=sc;
	isc->inode_num=inode_num;
	
	if (prefetching) {
		isc->sc->table_reader_group=(isc->inode_num-1)/isc->sc->sb->inodes_per_group;
		
		read_table_for_one_group(isc->sc);
		
		process_table_for_group(sc->table_reader_group, sc);
	}
	
	typecheck_inode(isc);
	
	sc->inode_link_count[inode_num-1]=isc->inode->links_count;
	sc->inode_type_str[inode_num-1]=isc->type;
	
	isc->is_dir=isc->type_bit&FTB_DIRE;
	
	bool ok=true;
	
	assert(!get_calculated_inode_allocation_map_bit(isc->sc,isc->inode_num));
	
	if (!isc->inode->links_count) {
		free(isc);
		return;
	}
	
	/*
	if (get_calculated_inode_allocation_map_bit(isc->sc,isc->inode_num)) {
		free(isc);
		return;
	}
	*/
	
	u8 a[4];
	for (size_t i=0;i<4;++i) {
		a[i]=isc->sc->calculated_inode_allocation_map[(((isc->inode_num-1)>>5)<<2)+i];
	}
	
	set_calculated_inode_allocation_map_bit(isc->sc,isc->inode_num);
	
	u8 b[4];
	for (size_t i=0;i<4;++i) {
		b[i]=isc->sc->calculated_inode_allocation_map[(((isc->inode_num-1)>>5)<<2)+i];
	}
	
	/*
	NOTIFYF("%s: Inode %lu [%s]: Marked inode %lu (with bit offset %lu) as in use, bitmap pattern at offset 0x%lx was %02x %02x %02x %02x, is now %02x %02x %02x %02x"
		,isc->sc->name,isc->inode_num,isc->type
		,(unsigned long)isc->inode_num
		,(unsigned long)(isc->inode_num-1)
		,(unsigned long)(((isc->inode_num-1)>>5)<<2)
		,(unsigned)a[0],(unsigned)a[1],(unsigned)a[2],(unsigned)a[3]
		,(unsigned)b[0],(unsigned)b[1],(unsigned)b[2],(unsigned)b[3]
	);
	*/
	
	assert(get_calculated_inode_allocation_map_bit(isc->sc,isc->inode_num));
	
	if (!isc->type_bit) {
		ERRORF("%s: Inode %lu [%s] has illegal type number %u set",isc->sc->name,isc->inode_num,isc->type,(unsigned)MASK_FT(isc->inode));
		ok=false;
		free(isc);
		return;
	}
	
	if (isc->inode->dtime) {
		if (isc->sc->warn_dtime_nonzero) NOTIFYF("%s: Inode %lu [%s] (in-use) has dtime set",isc->sc->name,isc->inode_num,isc->type);
		isc->inode->dtime=0;
	}
	
	if (isc->inode->ctime>isc->sc->sb->wtime) {
		if (isc->sc->warn_ctime) NOTIFYF("%s: Inode %lu [%s] has ctime (=%lu) in future relative to last write time (=%lu)",isc->sc->name,isc->inode_num,isc->type,(unsigned long)isc->inode->ctime,(unsigned long)isc->sc->sb->wtime);
		isc->inode->ctime=isc->sc->sb->wtime;
		ok=false;
	}
	
	if (isc->inode->mtime>isc->sc->sb->wtime) {
		if (isc->sc->warn_mtime) NOTIFYF("%s: Inode %lu [%s] has mtime (=%lu) in future relative to last write time (=%lu)",isc->sc->name,isc->inode_num,isc->type,(unsigned long)isc->inode->mtime,(unsigned long)isc->sc->sb->wtime);
		isc->inode->mtime=isc->sc->sb->wtime;
		ok=false;
	}
	
	if (isc->inode->atime>isc->sc->sb->wtime) {
		if (isc->sc->warn_atime) NOTIFYF("%s: Inode %lu [%s] has atime (=%lu) in future relative to last write time (=%lu)",isc->sc->name,isc->inode_num,isc->type,(unsigned long)isc->inode->atime,(unsigned long)isc->sc->sb->wtime);
		isc->inode->atime=isc->sc->sb->mtime;
		ok=false;
	}
	
	if (isc->type_bit&~(FTB_DIRE|FTB_FILE)) {
		if (isc->inode->flags&INOF_SECURE_REMOVE) {
			ERRORF("%s: Inode %lu [%s] has SECURE_REMOVE flag set.",isc->sc->name,isc->inode_num,isc->type);
			ok=false;
		}
		if (isc->inode->flags&INOF_IMMUTABLE) {
			ERRORF("%s: Inode %lu [%s] has IMMUTABLE flag set.",isc->sc->name,isc->inode_num,isc->type);
			ok=false;
		}
		if (isc->inode->flags&INOF_APPEND_ONLY) {
			ERRORF("%s: Inode %lu [%s] has APPEND_ONLY flag set.",isc->sc->name,isc->inode_num,isc->type);
			ok=false;
		}
	}
	
	// We ignore INOF_UNRM completely
	
	if (isc->inode->flags&INOF_E2COMPR_FLAGS) {
		if (!(isc->sc->sb->in_compat&IN_COMPAT_COMPRESSION)) {
			ERRORF("%s: Inode %lu [%s] has at least one compression flag set but filesystem has the feature bit not set",isc->sc->name,isc->inode_num,isc->type);
			// 1.) Set feature flag
			// 2.) Reset compression flags (w/ot decompression)
			// 3.) Reset compression flags (with decompression)
			// 4.) Clear inode's in-use flag
			// 5.) Ignore (This will have to set a flag which causes 
			//     solution 1 to no longer work (i.e. the user has 
			//     to restart checking)
			ok=false;
		} else {
			if (isc->type_bit&~(FTB_FILE|FTB_DIRE)) {
				ERRORF("%s: Inode %lu [%s] has at lease one compression flag set",isc->sc->name,isc->inode_num,isc->type);
				ok=false;
			} else {
				if (isc->type_bit&FTB_DIRE
				&&  (isc->inode->flags&INOF_E2COMPR_FLAGS)!=INOF_E2COMPR_COMPR) {
					ERRORF("%s: Inode %lu [%s] has at lease one compression flag (expcept for COMPR) set",isc->sc->name,isc->inode_num,isc->type);
					ok=false;
				}
				
				if (isc->type_bit&FTB_FILE) {
					if (isc->inode->flags&INOF_E2COMPR_COMPR) {
						NOTIFYF("%s: Inode %lu [%s] has flag E2COMPR_COMPR set.",isc->sc->name,isc->inode_num,isc->type);
					}
					
					if (isc->inode->flags&INOF_E2COMPR_DIRTY) {
						NOTIFYF("%s: Inode %lu [%s] has flag E2COMPR_DIRTY set.",isc->sc->name,isc->inode_num,isc->type);
					}
					
					if (isc->inode->flags&INOF_E2COMPR_HASCOMPRBLK) {
						NOTIFYF("%s: Inode %lu [%s] has flag E2COMPR_HASCOMPRBLK set, can't check validity of data yet.",isc->sc->name,isc->inode_num,isc->type);
					}
					
					if (isc->inode->flags&INOF_E2COMPR_NOCOMPR) {
						NOTIFYF("%s: Inode %lu [%s] has flag E2COMPR_NOCOMPR set.",isc->sc->name,isc->inode_num,isc->type);
					}
					
					if (isc->inode->flags&INOF_E2COMPR_ERROR) {
						NOTIFYF("%s: Inode %lu [%s] has flag E2COMPR_ERROR set.",isc->sc->name,isc->inode_num,isc->type);
					}
				}
			}
		}
	}
	
	if (isc->type_bit&~(FTB_DIRE|FTB_FILE|FTB_SLNK)) {
		if (isc->inode->flags&INOF_SYNC) {
			ERRORF("%s: Inode %lu [%s] has SYNC flag set.",isc->sc->name,isc->inode_num,isc->type);
			ok=false;
		}
	}
	
	// We ignore INOF_NODUMP (this inode should not be backed up) completely
	// We ignore INOF_NOATIME (do not update atime on access) completely
	
	if (isc->type_bit&~FTB_DIRE) {
		if (isc->inode->flags&INOF_INDEX) {
			ERRORF("%s: Inode %lu [%s] has INDEX flag set (which does make sense for directories only)",isc->sc->name,isc->inode_num,isc->type);
			ok=false;
		}
		if (isc->inode->flags&INOF_DIRSYNC) {
			ERRORF("%s: Inode %lu [%s] has DIRSYNC flag set (which does make sense for directories only)",isc->sc->name,isc->inode_num,isc->type);
			ok=false;
		}
	}
	
	if (isc->inode->flags&INOF_IMAGIC
	&&  !(isc->sc->sb->rw_compat&RW_COMPAT_IMAGIC_INODES)) {
		ERRORF("%s: Inode %lu [%s] has flag IMAGIC set.",isc->sc->name,isc->inode_num,isc->type);
		/*
		 * 1.) Allow feature:
		 *
		 *	sb->rw_compat|=RW_COMPAT_IMAGIC_INODES;
		 *
		 * 2.) Cleare use of feature
		 *
		 *	inode->flags&=~INOF_IMAGIC;
		 */
		ok=false;
	}
	
	if (isc->inode->flags&INOF_JOURNAL_DATA
	&&  !(isc->sc->sb->rw_compat&RW_COMPAT_HAS_JOURNAL)) {
		ERRORF("%s: Inode %lu [%s] has flag JOURNAL_DATA set.",isc->sc->name,isc->inode_num,isc->type);
		/*
		 * 1.) Allow feature
		 *
		 *	sb->rw_compat|=RW_COMPAT_HAS_JOURNAL;
		 *
		 * 2.) Cleare use of feature
		 *
		 *	inode->flags&=~INOF_JOURNAL_DATA;
		 */
		ok=false;
	}
	
	if (isc->inode->flags&INOF_NOTAIL) {
		NOTIFYF("%s: Inode %lu [%s] has flag NOTAIL set (whatever that means).",isc->sc->name,isc->inode_num,isc->type);
	}
	
	if (!!(isc->inode->flags&INOF_TOPDIR) != (isc->inode_num==ROOT_INO)) {
		if (isc->inode_num==ROOT_INO) {
			NOTIFYF("%s: Inode %lu [%s] (root) has flag TOPDIR not set.",isc->sc->name,isc->inode_num,isc->type);
		} else {
			NOTIFYF("%s: Inode %lu [%s] (not root) has flag TOPDIR set.",isc->sc->name,isc->inode_num,isc->type);
		}
		isc->inode->flags^=INOF_TOPDIR;
	}
	
	if (isc->inode->flags&INOF_HUGE_FILE) {
		NOTIFYF("%s: Inode %lu [%s] has unsupported flag HUGE_FILE set.",isc->sc->name,isc->inode_num,isc->type);
	}
	
	// Proper handling of INOF_EXTENTS is established
	
	if (isc->inode->flags&INOF_RESERVED_00100000) {
		NOTIFYF("%s: Inode %lu [%s] has unsupported flag RESERVED_00100000 set.",isc->sc->name,isc->inode_num,isc->type);
	}
	
	if (isc->inode->flags&INOF_EA_INODE) {
		NOTIFYF("%s: Inode %lu [%s] has unsupported flag EA_INODE set.",isc->sc->name,isc->inode_num,isc->type);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_00800000) {
		NOTIFYF("%s: Inode %lu [%s] has flag UNKNOWN_00800000 set.",isc->sc->name,isc->inode_num,isc->type);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_01000000) {
		NOTIFYF("%s: Inode %lu [%s] has flag UNKNOWN_01000000 set.",isc->sc->name,isc->inode_num,isc->type);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_02000000) {
		NOTIFYF("%s: Inode %lu [%s] has flag UNKNOWN_02000000 set.",isc->sc->name,isc->inode_num,isc->type);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_04000000) {
		NOTIFYF("%s: Inode %lu [%s] has flag UNKNOWN_04000000 set.",isc->sc->name,isc->inode_num,isc->type);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_08000000) {
		NOTIFYF("%s: Inode %lu [%s] has flag UNKNOWN_08000000 set.",isc->sc->name,isc->inode_num,isc->type);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_10000000) {
		NOTIFYF("%s: Inode %lu [%s] has flag UNKNOWN_10000000 set.",isc->sc->name,isc->inode_num,isc->type);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_20000000) {
		NOTIFYF("%s: Inode %lu [%s] has flag UNKNOWN_20000000 set.",isc->sc->name,isc->inode_num,isc->type);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_40000000) {
		NOTIFYF("%s: Inode %lu [%s] has flag UNKNOWN_40000000 set.",isc->sc->name,isc->inode_num,isc->type);
	}
	
	// We ignore INOF_TOOLCHAIN completely
	
	/*
	if (isc->inode->flags&INOF_UNKNOWN_FLAGS) {
		// We report this problem, but don't consider doing anything about it.
		NOTIFYF("%s: Inode %lu [%s] has unknown flag bits set: %lu.",isc->sc->name,isc->inode_num,isc->type,(unsigned long)isc->inode->flags&INOF_UNKNOWN_FLAGS);
	}
	*/
	
	if (isc->inode->file_acl) {
		NOTIFYF("%s: Inode %lu [%s]: Don't know how to handle file_acl=%lu.",isc->sc->name,isc->inode_num,isc->type,(unsigned long)isc->inode->file_acl);
	}
	
	if (isc->inode->faddr) {
		NOTIFYF("%s: Inode %lu [%s]: Don't know how to handle faddr=%lu.",isc->sc->name,isc->inode_num,isc->type,(unsigned long)isc->inode->faddr);
	}
	if (isc->inode->frag) {
		NOTIFYF("%s: Inode %lu [%s]: Don't know how to handle frag=%u.",isc->sc->name,isc->inode_num,isc->type,(unsigned)isc->inode->frag);
	}
	if (isc->inode->fsize) {
		NOTIFYF("%s: Inode %lu [%s]: Don't know how to handle fsize=%u.",isc->sc->name,isc->inode_num,isc->type,(unsigned)isc->inode->fsize);
	}
	
	if (isc->type_bit&~(FTB_DIRE|FTB_FILE|FTB_SLNK)) {
		if (isc->type_bit&~FTB_SLNK) {
			if (isc->inode->size) {
				NOTIFYF("%s: Inode %lu [%s] has non-zero size.",isc->sc->name,isc->inode_num,isc->type);
				isc->inode->size=0;
			}
		}
		
		if (isc->inode->size_high) {
			NOTIFYF("%s: Inode %lu [%s] has non-zero size_high.",isc->sc->name,isc->inode_num,isc->type);
			isc->inode->size_high=0;
		}
	}
	
	if (isc->type_bit&FTB_SLNK
	&&  isc->inode->size<sizeof(isc->inode->cluster)) {
		if (!isc->inode->size) {
			ERRORF("%s: Inode %lu [%s] has size == 0",isc->sc->name,isc->inode_num,isc->type);
			ok=false;
		} else {
			size_t len=strnlen((char *)isc->inode->cluster,sizeof(isc->inode->cluster));
			if (len!=isc->inode->size) {
				ERRORF("%s: Inode %lu [%s] (fast symbolic link) has size %u but %lu was recored.",isc->sc->name,isc->inode_num,isc->type,(unsigned)len,(unsigned long)isc->inode->size);
				ok=false;
			}
		}
	}
	
	if (isc->type_bit&FTB_DIRE) {
		if (isc->inode->dir_acl) {
			NOTIFYF("%s: Inode %lu [%s]: Don't know how to handle dir_acl=%lu.",isc->sc->name,isc->inode_num,isc->type,(unsigned long)isc->inode->dir_acl);
		}
	}
	
	if (isc->type_bit&~(FTB_DIRE|FTB_FILE|FTB_SLNK)
	||  ((isc->type_bit&FTB_SLNK) && (isc->inode->size<MAX_SIZE_FAST_SYMBOLIC_LINK))) {
		/*
		if (isc->type_bit&~FTB_SLNK) {
			for (int c=1;c<NUM_CLUSTER_POINTERS;++c) {
				if (isc->inode->cluster[c]) {
					NOTIFYF(
						"%s: Inode %lu [%s] has cluster[%i]=0x%08lx."
						,isc->sc->name
						,isc->inode_num
						,isc->type
						,c
						,(unsigned long)isc->inode->cluster[c]
					);
				}
			}
		}
		*/
		
		free(isc);
		return;
	}
	
	/*
	 * Here we have only FILES, DIRECTORIES 
	 * and non-fast SYMBOLIC LINKS left.
	 */
	
	cluster_scan_inode(isc);
}
/*
	TODO: Recheck this table:
	
	                   // FILE DIRE SLNK FIFO SOCK CDEV BDEV lc=0
	u16 mode&0x0fff;   // mode mode mode mode mode mode mode ---- -> immer ok
	u16 uid;           // uid  uid  uid  uid  uid  uid  uid  ---- -> immer ok
	u16 gid;           // gid  gid  gid  gid  gid  gid  gid  ---- -> immer ok
	u16 uid_high;      // uid  uid  uid  uid  uid  uid  uid  ---- -> immer ok
	u16 gid_high;      // gid  gid  gid  gid  gid  gid  gid  ---- -> immer ok
	u32 translator;    // ???? ???? ???? ???? ???? ???? ???? ---- -> immer ok (HURD) in_compat
	u16 mode_high;     // mode mode mode mode mode mode mode ---- -> immer ok (HURD) ro_compat
	u32 author;        // autr autr autr autr autr autr autr ---- -> immer ok (HURD) rw_compat
	u32 generation;    // ???? ???? ???? ???? ???? ???? ???? ---- -> immer ok (for NFS [i.e. unused], it's NFS'es proplem, how to handle that)
	
	u16 mode>>12;      // 0x8  0x4  0xa  0x1  0xe  0x2  0x6  ---- -> checked
	
	u32 dtime;         //  0    0    0    0    0    0    0   dtme -> checked -> dtime<=sb->wtime
	
	u32 atime;         // atme atme atme atme atme atme atme ---- -> checked -> atime>=mtime
	u32 ctime;         // ctme ctme ctme ctme ctme ctme ctme ---- -> checked -> ctime<=sb->wtime
	u32 mtime;         // mtme mtme ???? ???? ???? ???? ???? ---- -> checked -> mtime>=ctime
	
	u32 flags;         // flgs flgs flgs flgs flgs flgs flgs ---- -> checked -> INOF_UNKNOWN_FLAGS
	
	u32 file_acl;      // facl ???? ???? ???? ???? ???? ???? ---- -> checked -> immer ok, wenn 0 
	u32 faddr;         //  0    0    0    0    0    0    0   ---- -> checked -> immer ok, wenn 0 
	u8  frag;          //  0    0    0    0    0    0    0   ---- -> checked -> immer ok, wenn 0 
	u8  fsize;         //  0    0    0    0    0    0    0   ---- -> checked -> immer ok, wenn 0 
	u32 size;          //                 0    0    0    0   ---- -> immer ok, wenn 0 
	u32 size_high;     //            0    0    0    0    0   ---- -> immer ok, wenn 0 
	u32 size_high;     //      dacl                          ----
	u32 size;          // size size size                     ----
	u32 size_high;     // size                               ----

	u32 num_clusters;  // ncob ncob ncob ncob ncob ncob ncob ---- -> zusammen mit cluster prüfen
	u32 cluster[0];    // c[0] c[0] c[0]  0    0    0    0   ----
	u32 cluster[1];    // c[1] c[1]  0    0    0    0    0   ----
	u32 cluster[2];    // c[2] c[2]  0    0    0    0    0   ----
	u32 cluster[3];    // c[3] c[3]  0    0    0    0    0   ----
	u32 cluster[4+];   // c[n] c[n]  0    0    0    0    0   ----
	
	u16 links_count;   // lcnt lcnt lcnt lcnt lcnt lcnt lcnt  0   -> später prüfen
*/

#define MALLOCBYTES(var,size) do { \
	if (!(var=malloc(size))) { \
		ERRORF("malloc returned %p while trying to allocate %llu bytes in %s:%s:%i: %s",(void*)var,(unsigned long long)size,__FILE__,__FUNCTION__,__LINE__,strerror(errno)); \
		goto cleanup; \
	} \
} while(0)

#define MALLOCARRAY(var,num) MALLOCBYTES(var,sizeof(*var)*num)

#define MALLOC1(var) MALLOCARRAY(var,1)

void ext2_dump_file(struct scan_context * sc,const char * path,const char * filename);
void ext2_redump_file(struct scan_context * sc,const char * path,const char * filename);

static inline unsigned long long u64_to_ull(u64 * v) { return *v; }
static inline unsigned long u32_to_ul(u32 * v) { return *v; }
static inline unsigned u16_to_u(u16 * v) { return *v; }
static inline unsigned u08_to_u(u8  * v) { return *v; }

bool fsck(const char * _name) {
	struct scan_context * sc = NULL;
	struct super_block * sb2 = NULL;
	struct group_desciptor_v1 * gdt2v1 = NULL;
	struct group_desciptor_v2 * gdt2v2 = NULL;
	struct group_desciptor_in_memory * gdt2im = NULL;
	
	bool retval=false;
	
	MALLOC1(sc);
	
	/*
	 * The following three errors are VERY common to not cleanly 
	 * unmounted file systems. So, per default, ignore them.
	 */
	sc->warn_ctime=false;
	sc->warn_mtime=false;
	sc->warn_atime=false;
	
	/*
	 * The original ext2 fsck reports this too, but as we can't really do 
	 * anything else than just fix it (and there is effectively only one 
	 * way to do so) the user may with to just not be notified about it 
	 * at all.
	 */
	sc->warn_dtime_zero=true;
	sc->warn_dtime_nonzero=true;
	
	/*
	 * We initialize all pointers to NULL, to keep cleaning up simple: 
	 * Just free everything. (ISO C specifies, that free(NULL) just 
	 * doesn't do anything, so we don't even need to test for NULL.)
	 *
	 * Pointers which aren't malloc()ed doesn't need to be initialized 
	 * here as they doesn't need free()ing as well.
	 */
	sc->gdt=NULL;
	sc->gdtv1=NULL;
	sc->gdtv2=NULL;
	sc->inode_table=NULL;
	sc->cam_lock=NULL;
	sc->progress_bar=NULL;
	sc->sb=NULL;
	sc->dir=NULL;
//	sc->link_count=NULL;
//	sc->list_head=NULL;
//	sc->list_tail=NULL;
	sc->calculated_inode_allocation_map=NULL;
	sc->cluster_allocation_map=NULL;
	sc->inode_allocation_map=NULL;
	sc->comc.ccgroup=NULL;
#ifdef COMC_IN_MEMORY
	sc->comc.compr=NULL;
#endif // #ifdef COMC_IN_MEMORY
	
#ifdef BTCLONE
	void * com_cache_thread_stack_memory=NULL;
	void * ext2_read_tables_stack_memory=NULL;
#endif // #ifdef BTCLONE
	
	sc->com_cache_thread_lock=NULL;
	sc->table_reader_full=NULL;
	sc->table_reader_empty=NULL;
	
	sc->bdev=bdev_lookup_bdev(sc->name=_name);
	if (!sc->bdev) {
		ERRORF("Couldn't lookup device %s.",sc->name);
		free(sc);
		return false;
	}
	
	eprintf("Pass 1a: Find and read and check the master super block ...\n");
	
	sc->block_size=bdev_get_block_size(sc->bdev);
	
	MALLOC1(sc->sb);
	
//	sc->cs=2;
	if (read_super(sc,sc->sb,0,2,true)) {
		goto found_superblock;
	}
	
	/*
	sc->cs=0;
	// First try to read super block from block 0
//	eprintf("read_super(%u)\n",sc->cs);
	if (read_super(sc,sc->sb,0,sc->cs,true)) {
		goto found_superblock;
	}
	
	// Now, try all cluster sizes we support.
	for (sc->cs=1;sc->cs<64;sc->cs<<=1) {
//		eprintf("read_super(%u)\n",sc->cs);
		if (read_super(sc,sc->sb,0,sc->cs,true)) {
			goto found_superblock;
		}
	}
	*/
	
	ERRORF("Couldn't find ext2 superblock on %s.",sc->name);
	goto cleanup;

found_superblock:
	sc->sb->my_group=0;
	
	sc->cluster_size_Blocks=2<<(block_t)sc->sb->log_cluster_size;
	sc->cluster_size_Bytes=512*sc->cluster_size_Blocks;
	sc->num_groups=((sc->sb->num_clusters-(sc->sb->first_data_cluster+1))/sc->sb->clusters_per_group)+1;
	
	NOTIFYF("%s: File system has %u cluster groups with a cluster size of %u bytes.",sc->name,sc->num_groups,(unsigned)sc->cluster_size_Bytes);
	
	eprintf("Pass 1b: Read and check group descriptor table ...\n");
	
	switch (sc->sb->group_descriptor_table_entry_size) {
		case 32: {
			eprintf("Detected old 32 byte group descriptor table format with group_descriptor_table_entry_size == 32.\n");
		case_32: ;
			sc->size_of_gdt_Blocks=(sc->num_groups*sizeof(*sc->gdtv1)+sc->block_size-1)/sc->block_size;
			MALLOCBYTES(sc->gdtv1,sc->size_of_gdt_Blocks*sc->block_size);
			break;
		}
		
		case 64: {
			eprintf("Detected new 64 byte group descriptor table format with group_descriptor_table_entry_size == 64.\n");
		case_64: ;
			sc->size_of_gdt_Blocks=(sc->num_groups*sizeof(*sc->gdtv2)+sc->block_size-1)/sc->block_size;
			MALLOCBYTES(sc->gdtv2,sc->size_of_gdt_Blocks*sc->block_size);
			break;
		}
		
		case 0: {
			if (sc->sb->in_compat & IN_COMPAT_64BIT) {
				eprintf("Detected new 64 byte group descriptor table format, but group_descriptor_table_entry_size == 0!\n");
				goto case_64;
			} else {
				eprintf("Detected old 32 byte group descriptor table format with group_descriptor_table_entry_size == 0!\n");
				goto case_32;
			}
			/* fall through */
		}
		
		default: {
			eprintf("Unknown group descriptor entry size %u (supported are 32 and 64)\n",sc->sb->group_descriptor_table_entry_size);
			goto cleanup;
		}
	}
	
	MALLOCBYTES(sc->gdt,sc->num_groups * sizeof(*sc->gdt));
	
	sc->group_size_Blocks=(block_t)sc->sb->clusters_per_group*(block_t)sc->cluster_size_Blocks;
	eprintf(
		"sc->sb->clusters_per_group=%llu, sc->cluster_size_Blocks=%llu -> sc->group_size_Blocks=%llu\n"
		,(unsigned long long)sc->sb->clusters_per_group
		,(unsigned long long)sc->cluster_size_Blocks
		,(unsigned long long)sc->group_size_Blocks
	);
	
	bool have_gdt=false;
	bool gdt_from_group_0 = false;
	
	for (unsigned pass=0;pass<3;++pass) {
		block_t sb_offset;
		
		for (unsigned group=0;group<sc->num_groups;++group) {
		// unsigned group=-1; OPEN_PROGRESS_BAR_LOOP(1,group,sc->num_groups) ++group;
//			eprintf("%u ",group);
			
			if (!pass == !group_has_super_block(sc->sb,group)) {
				continue;
			}
			
			/*
			if (!group) {
				sb_offset=(sc->cs/sc->cluster_size_Blocks+1)*sc->cluster_size_Blocks;
			} else {
				sb_offset=(block_t)group*sc->group_size_Blocks+sc->cluster_size_Blocks;
			}
			*/
			sb_offset=(
				1+(
					(
						2+(block_t)group*sc->group_size_Blocks
					)/sc->cluster_size_Blocks
				)
			)*sc->cluster_size_Blocks;
			
			if (sc->size_of_gdt_Blocks==bdev_read(sc->bdev,sb_offset,sc->size_of_gdt_Blocks,sc->gdtv1?(void*)sc->gdtv1:(void*)sc->gdtv2)) {
				have_gdt=true;
				gdt_from_group_0 = !group;
				break;
			}
		// CLOSE_PROGRESS_BAR_LOOP()
		}
		
		if (have_gdt) {
			break;
		}
	}
	
	if (!have_gdt) {
		ERRORF("%s: Couldn't read group descriptor table: %s.",sc->name,strerror(errno));
		return 2;
	}
	
	if (sc->gdtv1) {
		gdt_disk2memory_v1(sc->gdt,sc->gdtv1,sc->num_groups);
	} else {
		gdt_disk2memory_v2(sc->gdt,sc->gdtv2,sc->num_groups);
	}
	
	if (!gdt_from_group_0) {
		// The group descriptor table backups only contain bogus flags. So, we have to set the
		// BG_INODE_TABLE_INITIALIZED flags in order to make sure, the table reader actually
		// reads the inode table and allocation bitmaps. However, that also means, we may end
		// up reading uninitialized data and hence find tons of errors that aren't really there.
		for (unsigned g = 0; g < sc->num_groups; ++g) {
			sc->gdt[g].flags |= BG_INODE_TABLE_INITIALIZED;
			sc->gdt[g].flags &= ~BG_INODE_ALLOCATION_MAP_UNINIT;
			sc->gdt[g].flags &= ~BG_CLUSTER_ALLOCATION_MAP_UNINIT;
		}
	}
	
	if (0 /* !checksum_ok(...) */) {
		for (unsigned g = 0; g < sc->num_groups; ++g) {
			sc->gdt[g].num_virgin_inodes = 0;
		}
	}
	
	eprintf("%s: Cluster groups with copies of super block and group descriptor table:",sc->name);
	
	{
		for (unsigned group=1;group<sc->num_groups;++group) {
			if (group_has_super_block(sc->sb,group)) {
				eprintf(" %llu",(unsigned long long)group);
			}
		}
	}
	eprintf("\n");
	
	eprintf("Pass 2: Comparing the super block and group descriptor table backup copies with the master ones ...\n");
	
	eprintf("size_of_gdt_Bytes=%llu\n",(unsigned long long)sc->num_groups*sizeof(*sc->gdt));
	
	{
		MALLOC1(sb2);
		
		if (sc->gdtv1) {
			MALLOCBYTES(gdt2v1,sc->size_of_gdt_Blocks*sc->block_size);
			eprintf("malloc(gdt2v1)=%p\n",(void*)gdt2v1);
		} else {
			MALLOCBYTES(gdt2v2,sc->size_of_gdt_Blocks*sc->block_size);
			eprintf("malloc(gdt2v2)=%p\n",(void*)gdt2v2);
		}
		MALLOCBYTES(gdt2im,sc->num_groups * sizeof(*gdt2im));
		
		/*
		int fd;
		fd=open("/ramfs/gdt.0.img",O_WRONLY|O_CREAT|O_TRUNC,0666);
		write(fd,sc->gdt,sc->num_groups*sizeof(*sc->gdt));
		close(fd);
		*/
		for (unsigned long group=1;group<sc->num_groups;++group) {
			if (group_has_super_block(sc->sb,group)) {
				block_t sb_offset;
				
				switch (sc->cluster_size_Blocks) {
					case 2: {
						sc->cluster_offset=1;
						break;
					}
					
					case 4: case 8: case 16: {
						sc->cluster_offset=0;
						break;
					}
					
					default: {
						assert(never_reached);
						exit(2);
					}
				}
				sb_offset=sc->cluster_size_Blocks*sc->cluster_offset+(block_t)group*sc->group_size_Blocks;
				if (!sb_offset) sb_offset=2;
				
				if (!read_super(sc,sb2,group,sb_offset,false)) {
					// read_super printed a suitable error message already, so skip it here.
					continue;
				}
				
				sb2->my_group=0;
				
				sb2->num_free_clusers=sc->sb->num_free_clusers;
				sb2->num_free_inodes=sc->sb->num_free_inodes;
				sb2->mtime=sc->sb->mtime;
				sb2->wtime=sc->sb->wtime;
				sb2->state=sc->sb->state;
				sb2->lastcheck_time=sc->sb->lastcheck_time;
				
				/*
				 * If the file system contains a journal and is 
				 * mounted, the following three additional 
				 * entries differ between the main copy of the 
				 * super block and it's backup copies.
				 */
				sb2->mount_count=sc->sb->mount_count;
				sb2->in_compat|=(sc->sb->in_compat&IN_COMPAT_RECOVER);
				sb2->ro_compat|=(sc->sb->ro_compat&RO_COMPAT_LARGE_FILE);
				sb2->last_orphanded_inode|=sc->sb->last_orphanded_inode;
				
#define CMP64(v) if (sb2->v!=sc->sb->v) eprintf("%s in backup super block is %llu, should be %llu\n",#v,u64_to_ull(&sb2->v),u64_to_ull(&sc->sb->v));
#define CMP32(v) if (sb2->v!=sc->sb->v) eprintf("%s in backup super block is %lu, should be %lu\n",#v,u32_to_ul(&sb2->v),u32_to_ul(&sc->sb->v));
#define CMP16(v) if (sb2->v!=sc->sb->v) eprintf("%s in backup super block is %u, should be %u\n",#v,u16_to_u(&sb2->v),u16_to_u(&sc->sb->v));
#define CMP08(v) if (sb2->v!=sc->sb->v) eprintf("%s in backup super block is %u, should be %u\n",#v,u08_to_u(&sb2->v),u08_to_u(&sc->sb->v));
#define CMP32n(v,n) do { for (int i=0;i<n;++i) { if (sb2->v[i]!=sc->sb->v[i]) eprintf("%s[%i] in backup super block is %lu, should be %lu\n",#v,i,u32_to_ul(sb2->v+i),u32_to_ul(sc->sb->v+i)); } } while (0)
#define CMP16n(v,n) do { for (int i=0;i<n;++i) { if (sb2->v[i]!=sc->sb->v[i]) eprintf("%s[%i] in backup super block is %u, should be %u\n",#v,i,u16_to_u(sb2->v+i),u16_to_u(sc->sb->v+i)); } } while (0)
#define CMP08n(v,n) do { for (int i=0;i<n;++i) { if (sb2->v[i]!=sc->sb->v[i]) eprintf("%s[%i] in backup super block is %u, should be %u\n",#v,i,u08_to_u(sb2->v+i),u08_to_u(sc->sb->v+i)); } } while (0)
#define CMPCn(v,n) do { \
	bool differs=false; \
	 \
	for (int i=0;i<n;++i) { \
		if (sb2->v[i]!=sc->sb->v[i]) { \
			eprintf( \
				"%s[%i] in backup super block is '%c' (%u), should be '%c' (%u)\n" \
				,#v \
				,i \
				,sb2->v[i] \
				,sb2->v[i] \
				,sc->sb->v[i] \
				,sc->sb->v[i] \
			); \
			differs=true; \
		} \
	} \
	 \
	if (differs && strcmp(sb2->v,sc->sb->v)) { \
		eprintf( \
			"%s in backup super block is \"%s\", should be \"%s\"\n" \
			,#v \
			,sb2->v \
			,sc->sb->v \
		); \
	} \
} while (0)
				
				CMP32(num_inodes);
				CMP32(num_clusters);
				CMP32(num_reserved_clusters);
				CMP32(num_free_clusers);
				CMP32(num_free_inodes);
				CMP32(first_data_cluster);
				CMP32(log_cluster_size);
				CMP32(log_frag_size);
				CMP32(clusters_per_group);
				CMP32(frags_per_group);
				CMP32(inodes_per_group);
				CMP32(mtime);
				CMP32(wtime);
				CMP16(mount_count);
				CMP16(max_mount_count);
				CMP16(magic);
				CMP16(state);
				CMP16(errors);
				CMP16(minor_rev_level);
				CMP32(lastcheck_time);
				CMP32(checkinterval);
				CMP32(creator_os);
				CMP32(rev_level);
				CMP16(reserved_access_uid);
				CMP16(reserved_access_gid);
				CMP32(first_inode);
				CMP16(inode_size);
				CMP16(my_group);
				CMP32(rw_compat);
				CMP32(in_compat);
				CMP32(ro_compat);
				CMP08n(uuid,16);
				CMPCn(volume_name,16);
				CMPCn(last_mount_point,64);
				CMP32(e2compr_algorithm_usage_bitmap);
				CMP08(num_blocks_to_prealloc);
				CMP08(num_blocks_to_prealloc4dirs);
				CMP16(onlineresize2fs_reserved_gdt_blocks);
				CMP08n(journal_uuid,16);
				CMP32(journal_inode);
				CMP32(journal_dev);
				CMP32(last_orphanded_inode);
				CMP32n(hash_seed,4);
				CMP08(def_hash_version);
				CMP08(jnl_backup_type);
				CMP16(group_descriptor_table_entry_size);
				CMP32(default_mount_opts);
				CMP32(first_meta_bg);
				CMP32(mkfs_time);
				CMP32n(jnl_blocks,17);
				CMP32(blocks_count_hi);
				CMP32(r_blocks_count_hi);
				CMP32(free_blocks_count_hi);
				CMP16(min_extra_isize);
				CMP16(want_extra_isize);
				CMP32(flags);
				CMP16(raid_stride);
				CMP16(mmp_update_interval);
				CMP64(mmp_block);
				CMP32(raid_stripe_width);
				CMP08(log_groups_per_flex);
				CMP08(checksum_type);
				CMP16(reserved_pad);
				CMP64(kbytes_written);
				CMP32(snapshot_inum);
				CMP32(snapshot_id);
				CMP64(snapshot_r_blocks_count);
				CMP32(snapshot_list);
				CMP32(error_count);
				CMP32(first_error_time);
				CMP32(first_error_ino);
				CMP64(first_error_block);
				CMP08n(first_error_func,32);
				CMP32(first_error_line);
				CMP32(last_error_time);
				CMP32(last_error_ino);
				CMP32(last_error_line);
				CMP64(last_error_block);
				CMP08n(last_error_func,32);
				
				CMP08n(mount_opts,64);
				CMP32(usr_quota_inum);
				CMP32(grp_quota_inum);
				CMP32(overhead_clusters);
				CMP32n(reserved,108);
				CMP32(checksum);
#undef CMP64
#undef CMP32
#undef CMP16
#undef CMP08
#undef CMP32n
#undef CMP16n
#undef CMP08n
#undef CMPCn
				
				if (memcmp(sc->sb,sb2,sc->block_size)) {
					NOTIFYF(
						"%s: Super blocks of groups 0 and %lu differ"
						,sc->name
						,group
					);
				}
				// assert(!memcmp(sc->sb,sb2,sc->block_size));
				
#define CMP64(v) if (gdt2im[g].v != sc->gdt[g].v) { diff = true; eprintf("%s in backup group descriptor table %lu at index %lu is %llu, should be %llu\n",#v,group,g,u64_to_ull(&gdt2im[g].v),u64_to_ull(&sc->gdt[g].v)); }
#define CMP32(v) if (gdt2im[g].v != sc->gdt[g].v) { diff = true; eprintf("%s in backup group descriptor table %lu at index %lu is %lu, should be %lu\n",#v,group,g,u32_to_ul(&gdt2im[g].v),u32_to_ul(&sc->gdt[g].v)); }
#define CMP16(v) if (gdt2im[g].v != sc->gdt[g].v) { diff = true; eprintf("%s in backup group descriptor table %lu at index %lu is %u, should be %u\n",#v,group,g,u16_to_u(&gdt2im[g].v),u16_to_u(&sc->gdt[g].v)); }
				
				if (1) { // Deactivate for VALGRIND if you like
					if (sc->size_of_gdt_Blocks!=bdev_read(sc->bdev,(sb_offset/sc->cluster_size_Blocks+1)*sc->cluster_size_Blocks,sc->size_of_gdt_Blocks,sc->gdtv1?(void*)gdt2v1:(void*)gdt2v2)) {
						eprintf("\033[31m");
						ERRORF("%s: Couldn't read group descriptor table of group %lu: %s.",sc->name,group,strerror(errno));
						eprintf("\033[0m");
					} else {
						if (sc->gdtv1) {
							gdt_disk2memory_v1(gdt2im,gdt2v1,sc->num_groups);
						} else {
							gdt_disk2memory_v2(gdt2im,gdt2v2,sc->num_groups);
						}
						bool diff = false;
						for (unsigned long g=0;g<sc->num_groups;++g) {
							CMP64(cluster_allocation_map);
							CMP64(inode_allocation_map);
							CMP64(inode_table);
							CMP64(snapshot_exclude_bitmap);
							// CMP32(num_free_clusters);
							// CMP32(num_free_inodes);
							// CMP32(num_directories);
							CMP32(cluster_allocation_map_csum);
							CMP32(inode_allocation_map_csum);
							// CMP32(num_virgin_inodes);
							CMP32(reserved);
							// CMP16(csum);
							// FIXME: Check csum based on actual group descriptor data
							// The group descriptor table backups only contain bogus flags. So, ignore this field completely here.
							// CMP16(flags);
						}
#undef CMP64
#undef CMP32
#undef CMP16
						
						/*
						char * tmp;
						fd=open(tmp=mprintf("/ramfs/%s.gdt.%03u.img",sc->name,group),O_WRONLY|O_CREAT|O_TRUNC,0666);
						write(fd,gdt2,sc->num_groups*sizeof(*sc->gdt));
						close(fd);
						free(tmp);
						*/
						
						if (diff) {
							ERRORF("%s: Group descriptor tables of groups 0 and %lu differ",sc->name,group);
						}
					}
				}
			}
		}
		free(gdt2v1); gdt2v1=NULL;
		free(gdt2v2); gdt2v2=NULL;
		free(gdt2im); gdt2im=NULL;
		free(sb2); sb2=NULL;
		
#if 0
		for (unsigned group=0;group<sc->num_groups;++group) {
			assert(sc->gdt[group].inode_allocation_map  ==sc->gdt[group].cluster_allocation_map+1
			&&     sc->gdt[group].inode_allocation_map+1==sc->gdt[group].inode_table);
			/*
			 * If this assertion fails it doesn't mean the filesystem 
			 * having errors, it just means, that our optimiziation, 
			 * reading cluster allocation map, inode allocation map and 
			 * inode table in one rush would fail. We don't handle that 
			 * case currently (however, implementing that is not really 
			 * a big problem).
			 */
		}
#endif // #if 0
	}
	
	{
		for (unsigned long g = 0; g < sc->num_groups; ++g) {
			if (! (sc->sb->ro_compat & (RO_COMPAT_METADATA_CSUM | RO_COMPAT_GDT_CSUM))) {
				if (sc->gdt[g].flags & BG_INODE_ALLOCATION_MAP_UNINIT) {
					NOTIFYF("%s: Group %lu has flag BG_INODE_ALLOCATION_MAP_UNINIT set, but file system has neither RO_COMPAT_METADATA_CSUM nor RO_COMPAT_GDT_CSUM set.",sc->name,g);
					// Only save thing to do is to clear the uninit flag and write correct inode allocation map
				}
				if (sc->gdt[g].flags & BG_CLUSTER_ALLOCATION_MAP_UNINIT) {
					NOTIFYF("%s: Group %lu has flag BG_CLUSTER_ALLOCATION_MAP_UNINIT set, but file system has neither RO_COMPAT_METADATA_CSUM nor RO_COMPAT_GDT_CSUM set.",sc->name,g);
					// Only save thing to do is to clear the uninit flag and write correct cluster allocation map
				}
			}
			/*
			 * BG_INODE_TABLE_INITIALIZED: No isolated plausibility check possible:
			 * If checksums are in use, this flag may be set or unset with the appropriate meaning
			 * Without checksums, this flag may be set or unset, both meaning it's initialized.
			 * So: all four combinations of "has checksum" and "is zeroed" are valid and meaningful.
			 */
			if (sc->gdt[g].flags & BG_UNKNOWN_0008) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_0008 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_0010) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_0010 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_0020) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_0020 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_0040) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_0040 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_0080) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_0080 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_0100) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_0100 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_0200) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_0200 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_0400) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_0400 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_0800) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_0800 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_1000) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_1000 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_2000) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_2000 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_4000) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_4000 set.",sc->name,g);
			}
			if (sc->gdt[g].flags & BG_UNKNOWN_8000) {
				NOTIFYF("%s: Group %lu has flag UNKNOWN_8000 set.",sc->name,g);
			}
		}
	}
	
recheck_compat_flags:
	if (sc->sb->log_frag_size!=sc->sb->log_cluster_size) {
		NOTIFYF("%s: Fragment size (%u) differs from cluster size (%u)."
			,sc->name
			,1024<<sc->sb->log_frag_size
			,1024<<sc->sb->log_cluster_size
		);
	}
	
	if (sc->sb->clusters_per_group!=8*sc->cluster_size_Bytes) {
		if (sc->sb->clusters_per_group>8*sc->cluster_size_Bytes) {
			ERRORF(
				"%s: num clusters per group (%lu) > num bits per cluster (%lu)."
				,sc->name
				,(unsigned long)sc->sb->clusters_per_group
				,(unsigned long)8*(unsigned long)sc->cluster_size_Bytes
			);
			return false;
		}
		NOTIFYF(
			"%s: num clusters per group (%lu) < num bits per cluster (%lu)."
			,sc->name
			,(unsigned long)sc->sb->clusters_per_group
			,(unsigned long)8*(unsigned long)sc->cluster_size_Bytes
		);
	}
	
	if (sc->sb->num_clusters!=bdev_get_size(sc->bdev)/sc->cluster_size_Blocks) {
		if (sc->sb->num_clusters>bdev_get_size(sc->bdev)/sc->cluster_size_Blocks) {
			ERRORF(
				"%s: num_clusters is %lu, should be %llu"
				,sc->name
				,(unsigned long)sc->sb->num_clusters
				,(unsigned long long)bdev_get_size(sc->bdev)/sc->cluster_size_Blocks
			);
			return false;
		}
		NOTIFYF(
			"%s: num_clusters is %lu, should be %llu (this is ok, if you extended the underlaying device after creation of the file system)"
			,sc->name
			,(unsigned long)sc->sb->num_clusters
			,(unsigned long long)bdev_get_size(sc->bdev)/sc->cluster_size_Blocks
		);
	} else {
		NOTIFYF(
			"%s: num_clusters=%lu"
			,sc->name
			,(unsigned long)sc->sb->num_clusters
		);
	}
	
	if (sc->sb->rev_level>=1) {
		if (sc->sb->inode_size!=128) {
			if (sc->sb->inode_size<128) {
				ERRORF(
					"%s: Inode size (%lu) is too small."
					,sc->name
					,(unsigned long)sc->sb->inode_size
				);
				return false;
			}
			if (sc->sb->inode_size%128) {
				NOTIFYF(
					"%s: Inode size (%lu) is not 128 and not even dividable by 128. This MAY cause troubles, but let's see ..."
					,sc->name
					,(unsigned long)sc->sb->inode_size
				);
			} else {
				NOTIFYF(
					"%s: Inode size (%lu) is not 128. This MAY cause troubles, but let's see ..."
					,sc->name
					,(unsigned long)sc->sb->inode_size
				);
			}
		}
		
		if (sc->sb->first_inode>=sc->sb->num_inodes) {
			if (sc->sb->first_inode>sc->sb->num_inodes) {
				ERRORF(
					"%s: The first usable inode is outside the inode address space."
					,sc->name
				);
				return false;
			}
			NOTIFYF(
				"%s: All inodes of this file system are reserved ..."
				,sc->name
			);
		}
	} else {
		sc->sb->inode_size=128;
		sc->sb->first_inode=11;
		sc->sb->rw_compat=sc->sb->ro_compat=sc->sb->in_compat=0;
	}
	
	if (sc->sb->inodes_per_group/(sc->cluster_size_Bytes/sc->sb->inode_size)
	>   sc->sb->clusters_per_group-2) {
		ERRORF(
			"%s: inodes_per_group (%lu) is too big, theoretically maximal number is %lu."
			,sc->name
			,(unsigned long)sc->sb->inodes_per_group
			,(unsigned long)(sc->sb->clusters_per_group-2)*(unsigned long)(sc->cluster_size_Bytes/sc->sb->inode_size)
		);
		return false;
	}
	
	if (sc->sb->inodes_per_group%(sc->cluster_size_Bytes/sc->sb->inode_size)) {
		NOTIFYF(
			"%s: Num inodes per group causes the last cluster being inefficiently used."
			,sc->name
		);
	}
	
	if (sc->sb->rev_level>1) {
		ERRORF(
			"%s: File system major revision is too high. It is %lu, maximum supported revision is 1."
			,sc->name
			,(unsigned long)sc->sb->rev_level
		);
	}
	
	if (sc->sb->minor_rev_level) {
		WARNINGF(
			"%s: Unknown minor revision level %u."
			,sc->name
			,(unsigned)sc->sb->minor_rev_level
		);
	}
	
/*
	// num_inodes
	// num_reserved_clusters
	// first_data_cluster
	// frags_per_group
	
	// CMP16(state);
	// CMP16(errors);
				CMP32(lastcheck_time);
				CMP32(checkinterval);
				CMP32(creator_os);
				CMP32(e2compr_algorithm_usage_bitmap);
				CMP08(num_blocks_to_prealloc);
				CMP08(num_blocks_to_prealloc4dirs);
				CMP16(onlineresize2fs_reserved_gdt_blocks);
				CMP32(journal_inode);
				CMP32(journal_dev);
				CMP32(last_orphanded_inode);
				CMP08(def_hash_version);
				CMP08(reserved_char_pad);
				CMP16(reserved_word_pad);
				CMP32(default_mount_opts);
				CMP32(first_meta_bg);
				CMP32n(reserved,62);
	// Zum Schluss:
	// 	num_free_clusers
	// 	num_free_inodes
*/
	unsigned flags_tested=0;
#define IF(flags, flag) flags_tested |= flag; if (flags & flag)
	
	IF (sc->sb->rw_compat, RW_COMPAT_DIR_PREALLOC) {
		NOTIFYF("%s: This file system has the RW compatible feature flag DIR_PREALLOC set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->rw_compat, RW_COMPAT_IMAGIC_INODES) {
		NOTIFYF("%s: This file system has the RW compatible feature flag IMAGIC_INODES set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->rw_compat, RW_COMPAT_HAS_JOURNAL) {
		NOTIFYF("%s: This file system has the RW compatible feature flag HAS_JOURNAL set.",sc->name);
		if (sc->sb->journal_inode) {
			INFOF("%s: Journal inode is %lu",sc->name,(unsigned long)sc->sb->journal_inode);
		} else {
			INFOF("%s: Journal inode is not known in super block (will have to search \"/.journal\" later).",sc->name);
		}
	}
	
	IF (sc->sb->rw_compat, RW_COMPAT_EXT_ATTR) {
		NOTIFYF("%s: This file system has the RW compatible feature flag EXT_ATTR set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->rw_compat, RW_COMPAT_RESIZE_INODE) {
		NOTIFYF("%s: This file system has the RW compatible feature flag RESIZE_INODE set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->rw_compat, RW_COMPAT_DIR_INDEX) {
		NOTIFYF("%s: This file system has the RW compatible feature flag DIR_INDEX set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->rw_compat, RW_COMPAT_UNKNOWN_0040) NOTIFYF("%s: This file system has the RW compatible feature flag UNKNOWN_0040 set.",sc->name);
	IF (sc->sb->rw_compat, RW_COMPAT_UNKNOWN_0080) NOTIFYF("%s: This file system has the RW compatible feature flag UNKNOWN_0080 set.",sc->name);
	IF (sc->sb->rw_compat, RW_COMPAT_UNKNOWN_0100) NOTIFYF("%s: This file system has the RW compatible feature flag UNKNOWN_0100 set.",sc->name);
	IF (sc->sb->rw_compat, RW_COMPAT_UNKNOWN_0200) NOTIFYF("%s: This file system has the RW compatible feature flag UNKNOWN_0200 set.",sc->name);
	IF (sc->sb->rw_compat, RW_COMPAT_UNKNOWN_0400) NOTIFYF("%s: This file system has the RW compatible feature flag UNKNOWN_0400 set.",sc->name);
	IF (sc->sb->rw_compat, RW_COMPAT_UNKNOWN_0800) NOTIFYF("%s: This file system has the RW compatible feature flag UNKNOWN_0800 set.",sc->name);
	IF (sc->sb->rw_compat, RW_COMPAT_UNKNOWN_1000) NOTIFYF("%s: This file system has the RW compatible feature flag UNKNOWN_1000 set.",sc->name);
	IF (sc->sb->rw_compat, RW_COMPAT_UNKNOWN_2000) NOTIFYF("%s: This file system has the RW compatible feature flag UNKNOWN_2000 set.",sc->name);
	IF (sc->sb->rw_compat, RW_COMPAT_UNKNOWN_4000) NOTIFYF("%s: This file system has the RW compatible feature flag UNKNOWN_4000 set.",sc->name);
	IF (sc->sb->rw_compat, RW_COMPAT_UNKNOWN_8000) NOTIFYF("%s: This file system has the RW compatible feature flag UNKNOWN_8000 set.",sc->name);
	
	assert(flags_tested == 0xffff);
	flags_tested = 0;
	
	IF (sc->sb->ro_compat, RO_COMPAT_SPARSE_SUPER) {
		NOTIFYF("%s: This file system has the RO compatible feature flag SPARSE_SUPER set.",sc->name);
	}
	
	IF (sc->sb->ro_compat, RO_COMPAT_LARGE_FILE) {
		NOTIFYF("%s: This file system has the RO compatible feature flag LARGE_FILE set.",sc->name);
	}
	
	IF (sc->sb->ro_compat, RO_COMPAT_BTREE_DIR) {
		NOTIFYF("%s: This file system has the RO compatible feature flag BTREE_DIR set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->ro_compat, RO_COMPAT_HUGE_FILE) {
		NOTIFYF("%s: This file system has the RO compatible feature flag HUGE_FILE set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->ro_compat, RO_COMPAT_GDT_CSUM) {
		NOTIFYF("%s: This file system has the RO compatible feature flag GDT_CSUM set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->ro_compat, RO_COMPAT_DIR_NLINK) {
		NOTIFYF("%s: This file system has the RO compatible feature flag DIR_NLINK set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->ro_compat, RO_COMPAT_EXTRA_ISIZE) {
		NOTIFYF("%s: This file system has the RO compatible feature flag EXTRA_ISIZE set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->ro_compat, RO_COMPAT_RESERVED_0080) {
		NOTIFYF("%s: This file system has the RO compatible feature flag RESERVED_0080 set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->ro_compat, RO_COMPAT_QUOTA) {
		NOTIFYF("%s: This file system has the RO compatible feature flag QUOTA set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->ro_compat, RO_COMPAT_BIGALLOC) {
		NOTIFYF("%s: This file system has the RO compatible feature flag BIGALLOC set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->ro_compat, RO_COMPAT_METADATA_CSUM) {
		NOTIFYF("%s: This file system has the RO compatible feature flag METADATA_CSUM set which is currently not supported.",sc->name);
	}
	
	if (sc->sb->ro_compat & RO_COMPAT_GDT_CSUM 
	&&  sc->sb->ro_compat & RO_COMPAT_METADATA_CSUM) {
		ERRORF("%s: This file system has set both, GDT_CSUM and METADATA_CSUM set.",sc->name);
	}
	
	IF (sc->sb->ro_compat, RO_COMPAT_UNKNOWN_0800) { NOTIFYF("%s: This file system has the RO compatible feature flag UNKNOWN_0800 set.",sc->name); }
	IF (sc->sb->ro_compat, RO_COMPAT_UNKNOWN_1000) { NOTIFYF("%s: This file system has the RO compatible feature flag UNKNOWN_1000 set.",sc->name); }
	IF (sc->sb->ro_compat, RO_COMPAT_UNKNOWN_2000) { NOTIFYF("%s: This file system has the RO compatible feature flag UNKNOWN_2000 set.",sc->name); }
	IF (sc->sb->ro_compat, RO_COMPAT_UNKNOWN_4000) { NOTIFYF("%s: This file system has the RO compatible feature flag UNKNOWN_4000 set.",sc->name); }
	IF (sc->sb->ro_compat, RO_COMPAT_UNKNOWN_8000) { NOTIFYF("%s: This file system has the RO compatible feature flag UNKNOWN_8000 set.",sc->name); }
	
	assert(flags_tested == 0xffff);
	flags_tested = 0;
	
	IF (sc->sb->in_compat, IN_COMPAT_COMPRESSION) {
		NOTIFYF("%s: This file system has the IN compatible feature flag COMPRESSION set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_FILETYPE) {
		NOTIFYF("%s: This file system has the IN compatible feature flag FILETYPE set.",sc->name);
	}
		
	IF (sc->sb->in_compat, IN_COMPAT_RECOVER) {
		if (sc->sb->rw_compat&RW_COMPAT_HAS_JOURNAL) {
			ERRORF("%s: This file system contains a journal AND NEEDS RECOVERY which is currently not supported. I'm going to ignore the journal and remove the 'needs recovery flag' after having the file system's errors fixed.",sc->name);
			sc->sb->ro_compat &= ~IN_COMPAT_RECOVER;
		} else {
			eprintf("%s: This file system is marked to not contain a journal, but needing recovery.",sc->name);
			eprintf("%s: <j> Fix by marking this file system to contain a journal.",sc->name);
			eprintf("%s: <r> Fix by marking this file system to not needing recovery.",sc->name);
			eprintf("%s: <q> Don't fix and quit now.",sc->name);
			int answer;
			do {
				answer=fgetc(stderr);
				answer=tolower(answer);
			} while (answer!='j' && answer!='r' && answer!='q');
			switch (answer) {
				case 'j':
					printf("This file system is marked to not contain a journal, but needing recovery. Fixed by marking file system containing a journal.\n"); fflush(stdout);
					sc->sb->rw_compat |= RW_COMPAT_HAS_JOURNAL;
					write_super_blocks(sc->sb);
					goto recheck_compat_flags;
				case 'r':
					printf("This file system is marked to not contain a journal, but needing recovery. Fixed by marking file system not needing recovery.\n"); fflush(stdout);
					sc->sb->ro_compat &= ~IN_COMPAT_RECOVER;
					break;
				case 'q':
					printf("This file system is marked to not contain a journal, but needing recovery. Not fixed.\n"); fflush(stdout);
					return false;
				default:
					assert(never_reached);
			}
		}
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_JOURNAL_DEV) {
		NOTIFYF("%s: This file system has the IN compatible feature flag JOURNAL_DEV set (external journal) which is currently not supported.",sc->name);
		sc->sb->ro_compat &= ~IN_COMPAT_RECOVER;
		sc->sb->rw_compat &= ~RW_COMPAT_HAS_JOURNAL;
		goto recheck_compat_flags;
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_META_BG) {
		NOTIFYF("%s: This file system has the IN compatible feature flag META_BG set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_RESERVED_0080) {
		NOTIFYF("%s: This file system has the IN compatible feature flag RESERVED_0080 set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_EXTENTS) {
		NOTIFYF("%s: This file system has the IN compatible feature flag EXTENTS set.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_64BIT) {
		NOTIFYF("%s: This file system has the IN compatible feature flag 64BIT set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_MMP) {
		NOTIFYF("%s: This file system has the IN compatible feature flag MMP set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_FLEX_BG) {
		NOTIFYF("%s: This file system has the IN compatible feature flag FLEX_BG set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_EA_INODE) {
		NOTIFYF("%s: This file system has the IN compatible feature flag EA_INODE set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_RESERVED_0800) {
		NOTIFYF("%s: This file system has the IN compatible feature flag RESERVED_0800 set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_DIRDATA) {
		NOTIFYF("%s: This file system has the IN compatible feature flag DIRDATA set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_BG_USE_META_CSUM) {
		NOTIFYF("%s: This file system has the IN compatible feature flag BG_USE_META_CSUM set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_LARGEDIR) {
		NOTIFYF("%s: This file system has the IN compatible feature flag LARGEDIR set which is currently not supported.",sc->name);
	}
	
	IF (sc->sb->in_compat, IN_COMPAT_INLINEDATA) {
		NOTIFYF("%s: This file system has the IN compatible feature flag INLINEDATA set which is currently not supported.",sc->name);
	}
	
	assert(flags_tested == 0xffff);
#undef IF
	
	if (sc->sb->wtime>(u32)time(NULL)) {
		NOTIFYF("%s: This file system has wtime in future relative to system time",sc->name);
		sc->sb->wtime=time(NULL);
	}
	
	if (sc->sb->mtime>(u32)time(NULL)) {
		NOTIFYF("%s: This file system has mtime in future relative to system time",sc->name);
		sc->sb->mtime=time(NULL);
	}
	
	if (sc->sb->wtime<sc->sb->mtime) {
		NOTIFYF("%s: This file system has wtime in past relative to mtime",sc->name);
		sc->sb->wtime=sc->sb->mtime;
	}
	
	eprintf("num_inodes=%lu\n",(long unsigned)sc->sb->num_inodes);
	
	assert(sc->sb->num_inodes==sc->sb->inodes_per_group*sc->num_groups);
	
	eprintf("Pass 3: Checking inode table and directory structure ...\n");
	
	assert(!(sc->sb->inodes_per_group&7)); // Should be removed.
	assert(!(sc->sb->clusters_per_group&7));
	assert(sc->sb->clusters_per_group/8==sc->cluster_size_Bytes);
	
	size_t cam_size_Bytes=(size_t)sc->cluster_size_Bytes*(size_t)sc->num_groups;
	size_t iam_size_Bytes=(sc->sb->num_inodes+7)/8;
	
	MALLOCBYTES(sc->cluster_allocation_map,cam_size_Bytes);
	MALLOCBYTES(sc->inode_allocation_map,iam_size_Bytes);
	off_t inode_table_len;
	{
		int fd=open("/ramfs/inode_table",O_RDWR|O_CREAT|O_TRUNC,0600);
		if (fd<0) {
			eprintf("main-thread: OOPS: Couldn't open/create file /ramfs/inode_table: %s",strerror(errno));
			exit(2);
		}
		inode_table_len=sizeof(*sc->inode_table);
		assert(sc->sb->num_inodes==(sc->sb->num_inodes/sc->sb->inodes_per_group)*sc->sb->inodes_per_group);
		if ((sc->sb->num_inodes/sc->sb->inodes_per_group)>INODE_TABLE_ROUNDROUBIN) {
			inode_table_len*=sc->sb->inodes_per_group*INODE_TABLE_ROUNDROUBIN;
		} else {
			inode_table_len*=sc->sb->num_inodes;
		}
		--inode_table_len;
		if (inode_table_len!=lseek(fd,inode_table_len,SEEK_SET)) {
			eprintf("main-thread: OOPS: Couldn't seek in file /ramfs/inode_table: %s",strerror(errno));
			exit(2);
		}
		if (1!=write(fd,&inode_table_len,1)) {
			eprintf("main-thread: OOPS: Couldn't write one byte to file /ramfs/inode_table: %s",strerror(errno));
			exit(2);
		}
		++inode_table_len;
//		sc->inode_table=mmap(NULL,inode_table_len,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
		sc->inode_table=mmap(NULL,inode_table_len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
		if (sc->inode_table==MAP_FAILED) {
			eprintf("main-thread: OOPS: Couldn't mmap file /ramfs/inode_table: %s",strerror(errno));
			exit(2);
		}
	}
	MALLOCARRAY(sc->inode_link_count,sc->sb->num_inodes);
	MALLOCARRAY(sc->inode_type_str,sc->sb->num_inodes);
	MALLOCARRAY(sc->calculated_link_count_array,sc->sb->num_inodes);
	MALLOCBYTES(sc->calculated_inode_allocation_map,iam_size_Bytes);
	
	for (unsigned ino=0;ino<sc->sb->num_inodes;++ino) {
		sc->calculated_link_count_array[ino]=0;
	}
	
/*
	sc->cam_iam_it_Blocks=sc->cluster_size_Blocks*(
		2+(sc->sb->inodes_per_group*sizeof(*sc->inode_table)+sc->cluster_size_Bytes-1)/sc->cluster_size_Bytes
	);
*/
	
	sc->num_clusterpointers=sc->cluster_size_Bytes/4;
	sc->num_clusterpointers_shift=8+sc->sb->log_cluster_size;
//	sc->cam_iam_it_Clusters=sc->cam_iam_it_Blocks/sc->cluster_size_Blocks;
	
//	MALLOC1(sc->list_head);
//	sc->list_tail=sc->list_head;
	
	eprintf("Executing ext2_fsck via thread %i\n",gettid());
	
	memset(sc->calculated_inode_allocation_map,0,iam_size_Bytes);
	
	MALLOC1(thread_head);
	MALLOC1(thread_tail);
	thread_head->lock=btlock_lock_mk();
	thread_tail->lock=btlock_lock_mk();
	assert(thread_head->lock && thread_tail->lock);
//	thread_head->tid=0;
//	thread_tail->tid=0;
	thread_head->next=thread_tail; thread_head->prev=NULL;
	thread_tail->prev=thread_head; thread_tail->next=NULL;
	
	/*
	 * We have to initialize both arrays, dir and link_count to zero. But 
	 * link_count is just a scalar, while dir is a pointer array. So we 
	 * alloc link_count before dir, as failing to alloc dir won't cause 
	 * troubles if link_count is not initialized yet. The other way round 
	 * would need taking special care in the clean up code.
	 */
//	MALLOCARRAY(sc->link_count,sc->sb->num_inodes);
	MALLOCARRAY(sc->dir,sc->sb->num_inodes);
	for (unsigned i=0;i<sc->sb->num_inodes;++i) {
		sc->dir[i]=NULL;
//		sc->link_count[i]=0;
	}
	
	sc->comc.debug_lock=btlock_lock_mk();
	// u32 i=(sc->num_groups*sc->clusters_per_group)/65536;
	u32 i=sc->sb->num_clusters/65536;
	sc->comc.clusters_per_entry=1;
	while (i) {
		i>>=1;
		sc->comc.clusters_per_entry<<=1;
	}
	sc->comc.size=(sc->sb->num_clusters+sc->comc.clusters_per_entry-1)/sc->comc.clusters_per_entry;
	sc->comc.ccgroup=malloc(sc->comc.size*sizeof(*sc->comc.ccgroup));
#ifdef COMC_IN_MEMORY
	sc->comc.compr=malloc(sc->comc.size*sizeof(*sc->comc.compr));
#endif // #ifdef COMC_IN_MEMORY
	eprintf("sc->comc.ccgroup=malloc: %p..%p\n",(void*)sc->comc.ccgroup,((char*)sc->comc.ccgroup)+sc->comc.size*sizeof(*sc->comc.ccgroup)-1);
	for (i=0;i<sc->comc.size;++i) {
		sc->comc.ccgroup[i]=NULL;
#ifdef COMC_IN_MEMORY
		sc->comc.compr[i].ptr=NULL;
		sc->comc.compr[i].len=0;
#endif // #ifdef COMC_IN_MEMORY
	}
	eprintf("sc->comc.ccgroup was initialized\n");
	sc->comc.head=NULL;
	sc->comc.tail=NULL;
	sc->comc.num_in_memory=0;
	sc->comc.read_head=NULL;
	sc->comc.read_tail=NULL;
	
	/*
	 * We mis-use this variable here to mark, that the new cluster scan 
	 * threads should be reported to the user. Normally they are just 
	 * silently created.
	 */
	sc->waiting4threads=true;
	
	sc->cam_lock=btlock_lock_mk();
	assert(sc->cam_lock);
	
	sc->background=false;
	atomic_set(&sc->prereaded,0);
	
#if HAVE_LINUX_CLONE
	/*
	 * This starts the cluster allocation check for very big inodes 
	 * immediatelly in the beginning of the file system check. If the 
	 * CLONE feature is not present on the current platform, it doesn't 
	 * make sense to do this in the very beginning, so we do it only if 
	 * the CLONE feature is present. If it is not, this inodes will be 
	 * reqularly processed with all the others as they are at turn.
	 */
	/*
	check_inode(sc,385,true);
	check_inode(sc,389,true);
	check_inode(sc,4238220,true);
	check_inode(sc,4238222,true);
	check_inode(sc,4238223,true);
	*/
	// check_inode(sc,6145418,true);
#endif // #if HAVE_LINUX_CLONE
	
	/*
	 * 858993459 is (2^32)/5.
	 * Each group has five different types of maintainance cluster types:
	 * 
	 * super block (not in each group present)
	 * group descriptor table (exactly present in the same groups in which a super block is present, too)
	 * cluster allocation map (present in each group)
	 * inode allocation map (present in each group)
	 * inode table (present in each group)
	 * 
	 * Each of this get a different number, which will be added to -5*group number. That value is then 
	 * used as if it would be an inode number.
	 * 
	 * This if clause is used to determine wether an overflow occurs, so we can notify the user about the 
	 * fact, that inode numbers may be ambiguous.
	 */
	if (sc->num_groups>858993459
	||  5*sc->num_groups+sc->sb->num_inodes<sc->sb->num_inodes) {
		WARNINGF("%s: 5*num_groups + num_inodes is greater than 2^32. This is not a problem but it makes our internally used inode numbers ambiguous and hence may produce bogus messages.\n",sc->name);
	}
	
	sc->waiting4threads=false;
	sc->background=true;
	sc->table_reader_group=0;
	sc->com_cache_thread_lock=btlock_lock_mk();
	sc->table_reader_full=btlock_lock_mk();
	sc->table_reader_empty=btlock_lock_mk();
	
	if (0) {
		atomic_t x;
#define INI() do { atomic_set(&x,-1); eprintf("atomic_set(&x,-1), atomic_get()=%i\n",atomic_get(&x)); } while (0)
#define INC() do { eprintf("atomic_inc()=%s, ",atomic_inc(&x)?"true ":"false"); eprintf("atomic_get()=%i\n",atomic_get(&x)); } while (0)
#define DEC() do { eprintf("atomic_dec()=%s, ",atomic_dec(&x)?"true ":"false"); eprintf("atomic_get()=%i\n",atomic_get(&x)); } while (0)
#define CMPXCHG(ov,nv) do { eprintf("atomic_cmpxchg(%i,%i)=%i\n",ov,nv,atomic_cmpxchg(&x,ov,nv)); } while (0)
		INI();
		CMPXCHG( 1, 0);
		CMPXCHG( 0, 2);
		CMPXCHG(-1, 0);
		CMPXCHG(-1, 1);
		CMPXCHG( 0, 1);
		CMPXCHG( 2, 3);
		CMPXCHG( 1,-1);
		INC();
		/*
		DEC();
		INC();
		INC();
		INC();
		DEC();
		DEC();
		*/
#undef INI
#undef INC
#undef DEC
	}
	
	{
#ifdef BTCLONE
		if (btclone(
			&com_cache_thread_stack_memory,
			com_cache_thread,
			65536,
			CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD,
			(void *)sc
		)<=0) {
#else // #ifdef BTCLONE
		pthread_t thread;
		if (pthread_create(
			&thread,
			NULL,
			com_cache_thread,
			(void *)sc
		)) {
#endif // #ifdef BTCLONE, else
			eprintf("main-thread: OOPS: clone failed, we can't proceed!");
			exit(2);
		}
	}
	
	{
#ifdef BTCLONE
		if (btclone(
			&ext2_read_tables_stack_memory,
			ext2_read_tables,
			65536,
			CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD,
			(void *)sc
		)<=0) {
#else // #ifdef BTCLONE
		pthread_t thread;
		if (pthread_create(
			&thread,
			NULL,
			ext2_read_tables,
			(void *)sc
		)) {
#endif // #ifdef BTCLONE, else
			if (errno!=ENOSYS) eprintf("clone failed, now first we'll read in the tables and then process them (instead of doing both at the same time)\n");
			sc->background=false;
			ext2_read_tables(sc);
		} else {
			++live_child;
		}
	}
	
	struct progress_bar * live_child_progress_bar=progress_bar_mk(2,hard_child_limit);
	assert(live_child_progress_bar);
	
	{
		u32 size_of_gdt_Clusters=(sc->size_of_gdt_Blocks+sc->cluster_size_Blocks-1)/sc->cluster_size_Blocks;
		unsigned long group=0;
		unsigned long inode_num=1;
		
		unsigned num_clusters_per_group_for_inode_table = sc->sb->inodes_per_group / (sc->cluster_size_Bytes / sizeof(struct inode));
		
		OPEN_PROGRESS_BAR_LOOP(1,inode_num,sc->sb->num_inodes)
			
			print_stalled_progress_bar(live_child_progress_bar,live_child);
			
			while (sc->table_reader_group==group) {
				PROGRESS_STALLED(inode_num);
				TRE_WAIT();
			}
			
			{
				process_table_for_group(group, sc);
				
				unsigned long c;
				
				c = sc->gdt[group].cluster_allocation_map;
				if (PRINT_MARK_CAM) {
					eprintf(
						"Marking cluster %lu in use (cluster allocation bitmap in group %lu)\n"
						,c
						,group
					);
				}
				MARK_CLUSTER_IN_USE_BY(sc, c, -1UL - (5UL * group));
				
				c = sc->gdt[group].inode_allocation_map;
				if (PRINT_MARK_IAM) {
					eprintf(
						"Marking cluster %lu in use (inode allocation bitmap in group %lu)\n"
						,c
						,group
					);
				}
				MARK_CLUSTER_IN_USE_BY(sc, c, -2UL - (5UL * group));
				
				if (PRINT_MARK_IT) {
					eprintf(
						"Marking clusters %lu .. %lu in use (inode table in group %lu)\n"
						,(unsigned long)sc->gdt[group].inode_table
						,(unsigned long)(sc->gdt[group].inode_table + num_clusters_per_group_for_inode_table)
						,group
					);
				}
				MARK_CLUSTERS_IN_USE_BY(sc, sc->gdt[group].inode_table, num_clusters_per_group_for_inode_table, -3UL - (5UL * group));
			}
			
//			eprintf("Parent's current group: %u\r",group);
			
//			if (sc->background) process_table_for_group(group,sc);
			
			if (group_has_super_block(sc->sb,group)) {
				block_t sb_offset=2+group*sc->group_size_Blocks;
				u32 cluster=sb_offset/sc->cluster_size_Blocks;
				if (!group) {
					/*
					 * The super blocks start at offset 1024 
					 * in each cluster group. The block before 
					 * is free to use for data. Exception in 
					 * the first cluster group, the blocks 
					 * before are reserved. So, mark all 
					 * clusters before the super block cluster 
					 * in group 0 as being used.
					 */
					for (u32 c=1;c<cluster;++c) {
						if (PRINT_MARK_SB0GAP) {
							eprintf(
								"Marking cluster %lu in use (gap before super block in group 0)\n"
								,(unsigned long)c
							);
						}
						MARK_CLUSTER_IN_USE_BY(sc,c,-4); // Same ID as the super block of group 0 itself
					}
				}
				
				if (cluster || !sc->cluster_offset) {
					// Mark cluster containing super block as in-use
					if (PRINT_MARK_SB) {
						eprintf(
							"Marking cluster %lu in use (super block of group %lu)\n"
							,(unsigned long)cluster
							,group
						);
					}
					MARK_CLUSTER_IN_USE_BY(sc,cluster,-4UL-(5UL*group));
				}
				
				++cluster;
				if (PRINT_MARK_GDT) {
					eprintf(
						"Marking clusters %lu .. %lu in use (group descriptor table in group %lu)\n"
						,(unsigned long)cluster
						,(unsigned long)(cluster + size_of_gdt_Clusters - 1)
						,group
					);
				}
				// Mark all clusters containing the group descriptor table as in-use
				MARK_CLUSTERS_IN_USE_BY(sc,cluster,size_of_gdt_Clusters,-5UL-(5UL*group));
			}
			
			{
				struct thread * walk=thread_head->next;
				while (walk->next) {
#ifdef BTCLONE
					if (kill(walk->tid,0)) {
						free(walk->stack_memory);
#else // #ifdef BTCLONE
					if (!pthread_tryjoin_np(walk->thread,NULL)) {
#endif // #ifdef BTCLONE, else
						walk->next->prev=walk->prev;
						walk->prev->next=walk->next;
						btlock_lock_free(walk->lock);
						struct thread * w=walk;
						walk=walk->prev;
						free(w);
						--live_child;
					}
					walk=walk->next;
				}
			}
			
			if (sc->gdt[group].flags & BG_INODE_TABLE_INITIALIZED) {
				for (unsigned ino=0;ino<sc->sb->inodes_per_group;++ino,++inode_num) {
					PRINT_PROGRESS_BAR(inode_num);
					
					check_inode(sc,inode_num,false);
				}
			}
			
			++group;
			
			atomic_dec(&sc->prereaded);
			
			TRF_WAKE();
		
		CLOSE_PROGRESS_BAR_LOOP()
	}
	
	/*
	 * Now let's mark all reserved inodes as in-use regardless wether they 
	 * really are in-use.
	 */
	for (unsigned ino=1;ino<sc->sb->first_inode;++ino) {
		set_calculated_inode_allocation_map_bit(sc,ino);
	}
	
	eprintf("Waiting for unfinished background jobs to finish ...\n");
	
	sc->waiting4threads=true;
	
	while (thread_head->next!=thread_tail) {
		struct thread * walk=thread_head->next;
		while (walk->next) {
#ifdef BTCLONE
			if (kill(walk->tid,0)) {
				free(walk->stack_memory);
#else // #ifdef BTCLONE
			if (!pthread_tryjoin_np(walk->thread,NULL)) {
#endif // #ifdef BTCLONE, else
				walk->next->prev=walk->prev;
				walk->prev->next=walk->next;
				btlock_lock_free(walk->lock);
				struct thread * w=walk;
				walk=walk->prev;
				free(w);
				--live_child;
			}
			walk=walk->next;
		}
		print_stalled_progress_bar(live_child_progress_bar,live_child);
		sleep(1);
	}
	
	progress_bar_free(live_child_progress_bar);
	
	assert(!live_child);
	
	int fd;
	gzFile gzf;
	
#define OPEN(filename,flags,mode) do { \
	fd=open(filename,flags,mode); \
	if (fd<0) { \
		eprintf( \
			"open(\"%s\",%u,%o): %s\n" \
			,filename \
			,flags \
			,mode \
			,strerror(errno) \
		); \
		goto cleanup; \
	} \
} while (0)
	
#define GZOPEN(filename,mode) do { \
	gzf=gzopen(filename,mode); \
	if (!gzf) { \
		eprintf( \
			"gzopen(»%s«,»%s«): %s\n" \
			,filename \
			,mode \
			,strerror(errno) \
		); \
		goto cleanup; \
	} \
} while (0)
	
	for (u32 inode_num=1;inode_num<=sc->sb->num_inodes;++inode_num) {
//		struct inode * inode;
//		char * type;
		
//		do_typecheck_inode(sc,inode_num,&inode,&type,NULL);
		if (sc->inode_link_count[inode_num-1]!=sc->calculated_link_count_array[inode_num-1]) {
			ERRORF(
				"%s: Inode %lu [%s]: Link count is %u, should be %u."
				,sc->name,(unsigned long)inode_num,sc->inode_type_str[inode_num-1]
				,(unsigned)sc->inode_link_count[inode_num-1]
				,(unsigned)sc->calculated_link_count_array[inode_num-1]
			);
		}
	}
	
	eprintf("Started building calculated_cluster_allocation_map from cluster_owning_map\n");
	MALLOCBYTES(sc->calculated_cluster_allocation_map,cam_size_Bytes);
	memset(sc->calculated_cluster_allocation_map,0,cam_size_Bytes);
	
	GZOPEN("/ramfs/cluster_owner_map.calculated.gz","wb1");
	{
		u32 ccg=0;
		if (unlikely(sc->sb->num_clusters<sc->comc.clusters_per_entry)) {
			sc->comc.clusters_per_entry=sc->sb->num_clusters;
		}
		u32 last_ccg=(sc->sb->num_clusters-1)/sc->comc.clusters_per_entry;
		u32 nc=sc->comc.clusters_per_entry;
		u32 cluster;
		
		cluster = sc->cluster_offset;
		
		OPEN_PROGRESS_BAR_LOOP(1, cluster, sc->sb->num_clusters)
		
			struct com_cache_entry * ccge=get_com_cache_entry(sc,ccg,cluster);
			if (unlikely(ccg==last_ccg)) {
				nc=sc->sb->num_clusters-cluster;
			}
			gzwrite(gzf,ccge->entry,4*nc);
			for (size_t c=0;c<nc;++c) {
				if (ccge->entry[c]) {
					set_calculated_cluster_allocation_map_bit(sc,cluster+c);
				}
			}
			release(sc,ccge);
			
			cluster+=nc;
			++ccg;
		
		CLOSE_PROGRESS_BAR_LOOP()

		assert(cluster==sc->sb->num_clusters);
		for (;cluster<=sc->num_groups*8*sc->cluster_size_Bytes-1+sc->cluster_offset;++cluster) {
			set_calculated_cluster_allocation_map_bit(sc,cluster);
		}
		/*
		u32 num_clusters=sc->sb->num_clusters;
		u32 ccg=0;
		struct com_cache_entry * ccge;
		while (num_clusters>sc->comc.clusters_per_entry) {
			ccge=get_com_cache_entry(sc,ccg);
			gzwrite(gzf,ccge->entry,4*sc->comc.clusters_per_entry);
			release(sc,ccge);
			++ccg;
			num_clusters-=sc->comc.clusters_per_entry;
		}
		ccge=get_com_cache_entry(sc,ccg);
		gzwrite(gzf,ccge->entry,4*num_clusters);
		release(sc,ccge);
		*/
	}
	gzclose(gzf);
	exit_request_com_cache_thread=true;
	GENERAL_BARRIER();
	CCT_WAKE(); // Make sure, we won't wait senselessly for the com_cache_thread.
	CCT_WAIT(); // Wait for that thread to exit
	GENERAL_BARRIER();
	assert(!exit_request_com_cache_thread);
	
	/*
	for (u32 cluster=0;cluster<sc->sb->num_clusters;++cluster) {
		if (get_owner(sc,cluster)) {
			set_calculated_cluster_allocation_map_bit(sc,cluster);
		}
	}
	*/
	eprintf("Finisched building calculated_cluster_allocation_map from cluster_owning_map\n");
	
	if (memcmp(sc->cluster_allocation_map,sc->calculated_cluster_allocation_map,(size_t)sc->cluster_size_Bytes*(size_t)sc->num_groups)) {
		eprintf("%s: cluster allocation map differs:",sc->name);
		bool valid_problem_range=false;
		bool really_allocated=true;
		u32 first_cluster=0;
		for (u32 cluster=1;cluster<sc->sb->num_clusters;++cluster) {
			bool on_disk_flag=get_cluster_allocation_map_bit(sc,cluster);
			bool calculated_flag=get_calculated_cluster_allocation_map_bit(sc,cluster);
			bool print=false;
			bool new_range=false;
			
			if (likely(on_disk_flag==calculated_flag)) {
				if (unlikely(valid_problem_range)) {
					print=true;
				}
			} else {
				if (likely(!valid_problem_range) || unlikely(really_allocated!=calculated_flag)) {
					new_range=true;
					if (likely(valid_problem_range)) {
						print=true;
					}
				}
			}
			if (unlikely(print)) {
				valid_problem_range=false;
				eprintf(" %c",really_allocated?'+':'-');
				if (unlikely(first_cluster==cluster-1)) {
					eprintf("%lu",(unsigned long)first_cluster);
				} else {
					eprintf("(%lu..%lu)",(unsigned long)first_cluster,(unsigned long)cluster-1);
				}
			}
			if (new_range) {
				valid_problem_range=true;
				really_allocated=calculated_flag;
				first_cluster=cluster;
			}
		}
		eprintf("\n");
	}
	
	{
		int w;
		
		OPEN("/ramfs/cluster_allocation_map.calculated",O_WRONLY|O_CREAT|O_TRUNC,0666);
		w=write(fd,sc->calculated_cluster_allocation_map,cam_size_Bytes);
		close(fd);
		free(sc->calculated_cluster_allocation_map);
		
		/*
		for (unsigned i=0;i<(size_t)sc->cluster_size_Bytes*(size_t)sc->num_groups;++i) {
			if (sc->calculated_cluster_collision_map[i]) {
				ERRORF("%s: There are doubly used clustes.",sc->name);
				i=-2;
			}
		}
		*/
		
		if (memcmp(sc->inode_allocation_map,sc->calculated_inode_allocation_map,(sc->sb->num_inodes+7)/8)) {
			ERRORF("%s: inode allocation map differs!",sc->name);
		}
		
		OPEN("/ramfs/cluster_allocation_map.read",O_WRONLY|O_CREAT|O_TRUNC,0666);
		w=write(fd,sc->cluster_allocation_map,cam_size_Bytes);
		close(fd);
		
		OPEN("/ramfs/inode_allocation_map.read",O_WRONLY|O_CREAT|O_TRUNC,0666);
		w=write(fd,sc->inode_allocation_map,iam_size_Bytes);
		close(fd);
		
		OPEN("/ramfs/inode_allocation_map.calculated",O_WRONLY|O_CREAT|O_TRUNC,0666);
		w=write(fd,sc->calculated_inode_allocation_map,iam_size_Bytes);
		close(fd);
	}
	
	retval=true;

cleanup:
	if (thread_head) {
		btlock_lock_free(thread_head->lock);
		free(thread_head);
	}
	if (thread_tail) {
		btlock_lock_free(thread_tail->lock);
		free(thread_tail);
	}
	if (sc) {
		if (sc->dir) {
			for (unsigned i=0;i<sc->sb->num_inodes;++i) {
				if (sc->dir[i]) {
					free(sc->dir[i]->entry);
					free(sc->dir[i]);
				}
			}
		}
		if (sc->com_cache_thread_lock) btlock_lock_free(sc->com_cache_thread_lock);
		if (sc->table_reader_full) btlock_lock_free(sc->table_reader_full);
		if (sc->table_reader_empty) btlock_lock_free(sc->table_reader_empty);
		free(sc->inode_link_count);
		free(sc->inode_type_str);
		free(sc->gdt);
		free(sc->gdtv1);
		free(sc->gdtv2);
//		free(sc->inode_table);
		munmap(sc->inode_table,inode_table_len);
		free(sc->cam_lock);
		free(sc->progress_bar);
		free(sc->sb);
		free(sc->dir);
//		free(sc->link_count);
//		free(sc->list_head);
//		assert(sc->list_tail==sc->list_head);
		free(sc->calculated_inode_allocation_map);
		free(sc->cluster_allocation_map);
		free(sc->inode_allocation_map);
		free(sc->calculated_link_count_array);
		if (sc->comc.ccgroup) {
			for (i=0;i<sc->comc.size;++i) {
				if (sc->comc.ccgroup[i]) {
					CCE_LOCK_FREE(sc->comc.ccgroup[i]);
					free(sc->comc.ccgroup[i]->entry);
					free(sc->comc.ccgroup[i]);
				}
			}
		}
#ifdef COMC_IN_MEMORY
		if (sc->comc.compr) {
			for (i=0;i<sc->comc.size;++i) {
				free(sc->comc.compr[i].ptr);
			}
		}
		eprintf("free(sc->comc.compr (%p));\n",(void*)sc->comc.compr);
		free(sc->comc.compr);
#endif // #ifdef COMC_IN_MEMORY
		eprintf("free(sc->comc.ccgroup (%p));\n",(void*)sc->comc.ccgroup);
		free(sc->comc.ccgroup);
		btlock_lock_free(sc->comc.debug_lock);
		free(sc);
		free(sb2);
		free(gdt2v1);
		free(gdt2v2);
		free(gdt2im);
#ifdef BTCLONE
		free(com_cache_thread_stack_memory);
		free(ext2_read_tables_stack_memory);
#endif // #ifdef BTCLONE
	}
	return retval;
}

struct fs_inode {
	struct scan_context * fs; // struct fs * fs
	u32 number;
	struct inode data;
	u32 nind_number[4];
	u8 * nind_buffer[4];
};

struct fs_inode * ext2_read_inode(struct scan_context * sc,u32 ino) {
	struct fs_inode * retval=malloc(sizeof(*retval));
	u32 bs=bdev_get_block_size(sc->bdev);
	u8 block_buffer[bs];
	u32 ino_group=(ino-1)/sc->sb->inodes_per_group;
	u32 ino_block=(ino-1)%sc->sb->inodes_per_group*sizeof(retval->data)/bs;
	u32 ino_offset=(ino-1)%sc->sb->inodes_per_group*sizeof(retval->data)%bs;
	
	if (!retval) return NULL;
	
	if (1!=bdev_read(sc->bdev,sc->cluster_size_Blocks*sc->gdt[ino_group].inode_table+ino_block,1,block_buffer)) {
		free(retval);
		return NULL;
	}
	
	memcpy(&retval->data,block_buffer+ino_offset,sizeof(retval->data));
	retval->number=ino;
	retval->fs=sc;
	
	retval->nind_number[0]=0xdeadbeef;
	retval->nind_buffer[0]=(u8*)0xdeadbeef;
	
	for (int i=1;i<4;++i) {
		retval->nind_number[i]=0;
		retval->nind_buffer[i]=malloc(sc->cluster_size_Bytes);
	}
	/*
	eprintf("clusters[]: {");
	for (int i=0;i<15;++i) {
		if (i) eprintf(",");
		eprintf(" %lu",retval->data.cluster[i]);
	}
	eprintf(" }\n");
	*/
	return retval;
}

void ext2_put_inode(struct fs_inode * inode) {
	for (int i=1;i<4;++i) {
		free(inode->nind_buffer[i]);
	}
	free(inode);
}

size_t ext2_read_clusters(struct scan_context * sc,u32 absolute_cluster,size_t num,u8 * buffer) {
	unsigned cs=sc->cluster_size_Blocks;
	return bdev_read(sc->bdev,cs*absolute_cluster,cs*num,buffer)/cs;
}

bool ext2_read_cluster(struct scan_context * sc,u32 absolute_cluster,u8 * buffer) {
	return ext2_read_clusters(sc,absolute_cluster,1,buffer);
}

size_t ext2_write_clusters(struct scan_context * sc,u32 absolute_cluster,size_t num,const u8 * buffer) {
	unsigned cs=sc->cluster_size_Blocks;
	return bdev_write(sc->bdev,cs*absolute_cluster,cs*num,buffer)/cs;
}

bool ext2_write_cluster(struct scan_context * sc,u32 absolute_cluster,const u8 * buffer) {
	return ext2_write_clusters(sc,absolute_cluster,1,buffer);
}

u32 ext2_inode_translate_cluster(struct fs_inode * inode,u32 relative_cluster) {
	u32 absolute_cluster=0xdeadbeef,*pointers;
	u32 num[4]={
		NUM_ZIND<<(0*inode->fs->num_clusterpointers_shift),
		NUM_SIND<<(1*inode->fs->num_clusterpointers_shift),
		NUM_DIND<<(2*inode->fs->num_clusterpointers_shift),
		NUM_TIND<<(3*inode->fs->num_clusterpointers_shift),
	};
	u32 ptr[4]={
		FIRST_ZIND,
		FIRST_SIND,
		FIRST_DIND,
		FIRST_TIND,
	};
	u32 shift[4]={
		0*inode->fs->num_clusterpointers_shift,
		1*inode->fs->num_clusterpointers_shift,
		2*inode->fs->num_clusterpointers_shift,
		3*inode->fs->num_clusterpointers_shift,
	};
	int level;
	
	for (level=0;level<4;++level) {
		if (relative_cluster<num[level]) {
//			eprintf("ext2_inode_read_relative_cluster: level=%i\n",level);
//			eprintf("idx=%u\n",(unsigned)(ptr[level]+(relative_cluster>>shift[level])));
			absolute_cluster=inode->data.cluster[ptr[level]+(relative_cluster>>shift[level])];
//			eprintf("absolute_cluster=%lu\n",(unsigned long)absolute_cluster);
			while (level) {
				if (inode->nind_number[level]!=absolute_cluster) {
					if (likely(absolute_cluster)) {
						if (unlikely(!ext2_read_cluster(inode->fs,absolute_cluster,inode->nind_buffer[level]))) {
							return -1;
						}
					} else {
						return 0;
					}
					inode->nind_number[level]=absolute_cluster;
				}
				pointers=(u32*)inode->nind_buffer[level];
				u32 idx=relative_cluster>>shift[--level];
				absolute_cluster=pointers[idx];
				relative_cluster-=idx<<shift[level];
			}
			break;
		}
		relative_cluster-=num[level];
	}
	if (level==4) return 0;
	return absolute_cluster;
}

void ext2_inode_write_relative_cluster(struct fs_inode * inode,u32 relative_cluster,const u8 * buffer) {
	u32 absolute_cluster=ext2_inode_translate_cluster(inode,relative_cluster);
	if (absolute_cluster==(u32)-1) {
		// error
		return;
	}
	if (!absolute_cluster) {
		// can't fill holes atm
		return;
	} else {
		if (!ext2_write_cluster(inode->fs,absolute_cluster,buffer)) {
			// error
			return;
		}
	}
	// ok ;-)
	return;
}

u8 * ext2_inode_read_relative_cluster(struct fs_inode * inode,u32 relative_cluster) {
	u8 * retval=malloc(inode->fs->cluster_size_Bytes);
	if (!retval) return NULL;
	u32 absolute_cluster=ext2_inode_translate_cluster(inode,relative_cluster);
	if (absolute_cluster==(u32)-1) {
		return NULL;
	}
	if (!absolute_cluster) {
		memset(retval,0,inode->fs->cluster_size_Bytes);
	} else {
		if (!ext2_read_cluster(inode->fs,absolute_cluster,retval)) {
			free(retval);
			return NULL;
		}
	}
	return retval;
	
	/*
	if (relative_cluster<NUM_ZIND) {
		absolute_cluster=inode->data.isc->inode->cluster[relative_cluster];
	} else {
		relative_cluster-=NUM_ZIND;
		if (relative_cluster<isc->sc->num_clusterpointers) {
			absolute_cluster=inode->data.isc->inode->cluster[FIRST_SIND];
		} else {
			relative_cluster-=isc->sc->num_clusterpointers;
			if (relative_cluster<isc->sc->num_clusterpointers*isc->sc->num_clusterpointers) {
				absolute_cluster=inode->data.isc->inode->cluster[FIRST_DIND];
			} else {
				relative_cluster-=isc->sc->num_clusterpointers*isc->sc->num_clusterpointers;
				if (relative_cluster<isc->sc->num_clusterpointers*isc->sc->num_clusterpointers*isc->sc->num_clusterpointers) {
					absolute_cluster=inode->data.isc->inode->cluster[FIRST_TIND];
				} else {
					absolute_cluster=0;
				}
				if (nind_number[3]!=absolute_cluster) {
					nind_number[3]=absolute_cluster;
					if (likely(absolute_cluster)) {
						free(nind_number[3]);
						nind_buffer[3]=ext2_read_cluster(absolute_cluster);
					} else {
						memset(nind_buffer[3],0,sc->cluster_size_Bytes);
					}
				}
				pointers=nind_buffer[3];
				absolute_cluster=pointers[relative_cluster/isc->sc->num_clusterpointers*isc->sc->num_clusterpointers];
			}
			if (nind_number[2]!=absolute_cluster) {
				nind_number[2]=absolute_cluster;
				if (likely(absolute_cluster)) {
					free(nind_number[2]);
					nind_buffer[2]=ext2_read_cluster(absolute_cluster);
				} else {
					memset(nind_buffer[2],0,sc->cluster_size_Bytes);
				}
			}
			pointers=nind_buffer[2];
			absolute_cluster=pointers[relative_cluster/isc->sc->num_clusterpointers];
		}
		if (nind_number[1]!=absolute_cluster) {
			nind_number[1]=absolute_cluster;
			if (likely(absolute_cluster)) {
				free(nind_number[1]);
				nind_buffer[1]=ext2_read_cluster(absolute_cluster);
			} else {
				memset(nind_buffer[1],0,sc->cluster_size_Bytes);
			}
		}
		pointers=nind_buffer[1];
		absolute_cluster=pointers[relative_cluster];
	}
	if (nind_number[0]!=absolute_cluster) {
		nind_number[0]=absolute_cluster;
		if (likely(absolute_cluster)) {
			free(nind_buffer[0]);
			nind_buffer[0]=ext2_read_cluster(absolute_cluster);
		} else {
			if (unlikely(!nind_buffer[0])) {
				nind_buffer[0]=malloc(sc->cluster_size_Bytes);
			}
			memset(nind_buffer[0],0,sc->cluster_size_Bytes);
		}
	}
	return nind_buffer[0];
	*/
}

u32 ext2_lookup_relative_path(struct scan_context * sc,u32 cwd,const char * path) {
	u8 * buffer;
	size_t clen;
	
recheck_path:
	clen=0;
	while (path[clen] && path[clen]!='/') ++clen;
	if (!clen) {
		if (!*path) {
			return cwd;
		}
		++path;
		goto recheck_path;
	}
//	eprintf("ext2_lookup_relative_path: Searching for entry »%.*s«\n",clen,path);
	struct fs_inode * inode=ext2_read_inode(sc,cwd);
	/*
	u32 num_clusters=inode->data.size/sc->cluster_size_Bytes;
	eprintf("inode->data.size=%lu\n",(unsigned long)inode->data.size);
	eprintf("num_clusters=%lu\n",(unsigned long)num_clusters);
	*/
	u32 num_clusters=NUM_ZIND
		+(1<<(1*inode->fs->num_clusterpointers_shift))
		+(1<<(2*inode->fs->num_clusterpointers_shift))
		+(1<<(3*inode->fs->num_clusterpointers_shift))
	;
	for (u32 cluster=0;cluster<num_clusters;++cluster) {
//		eprintf(">>> ext2_inode_read_relative_cluster(inode,%lu)\n",(unsigned long)cluster);
		if (unlikely(!(buffer=ext2_inode_read_relative_cluster(inode,cluster)))) {
//			eprintf("Couldn't read relative cluster %lu: %s\n",(unsigned long)cluster,strerror(errno));
		} else {
//			eprintf("<<< ext2_inode_read_relative_cluster(inode,%lu)\n",(unsigned long)cluster);
			/*
			 * Structure of a record:
			 *
			 * u32 inode;
			 * u16 reclen;
			 * u8  filename_len;
			 * u8  filetype;
			 * u8  filename[(filename_len+3)&-4];
			 * 
			 * The filename is NOT null-terminated.
			 * If inode is zero, the entry doesn't describe any object (maybe it's 
			 * left over after a delete) and can just be ignored (or used to store 
			 * a new file name). The last entry uses the entire space of it's 
			 * containing cluster so filling it up. The next entry starts in the 
			 * next cluster.
			 */
			
			unsigned long offset=0;
			
			while (offset<inode->fs->cluster_size_Bytes) {
				u32 ino=0
					+((u32)buffer[offset+0]    )
					+((u32)buffer[offset+1]<< 8)
					+((u32)buffer[offset+2]<<16)
					+((u32)buffer[offset+3]<<24)
				;
				u16 reclen=0
					+((u32)buffer[offset+4]    )
					+((u32)buffer[offset+5]<< 8)
				;
				if (!reclen) {
					break;
				}
				if (!ino) {
					offset+=reclen;
					continue;
				}
				
				u8 filename_len=0
					+((u32)buffer[offset+6]    )
				;
				/*
				u8 filetype=0
					+((u32)buffer[offset+7]    )
				;
				*/
//				eprintf("\tConsidering entry (%10lu) »%.*s« [%u]\n",(unsigned long)ino,filename_len,buffer+offset+8,(unsigned)reclen);
				if (clen==filename_len
				&&  !memcmp(buffer+offset+8,path,filename_len)) {
					eprintf("\t\tFOUND »%s« ;)\n",path);
					free(buffer);
					if (ino>inode->fs->sb->num_inodes) {
						ERRORF(
							"%s: Can't open object with name »%.*s«: Illegal inode %lu."
							,inode->fs->name
							,filename_len
							,path
							,(unsigned long)ino
						);
						ext2_put_inode(inode);
						return 0;
					}
					ext2_put_inode(inode);
					return ext2_lookup_relative_path(sc,ino,path+clen+1);
				}
				offset+=reclen;
			}
		}
	}
	errno=ENOENT;
	return 0;
}

u32 ext2_lookup_absolute_path(struct scan_context * sc,const char * path) {
	if (path[0]!='/') {
		errno=EINVAL;
		return 0;
	}
	return ext2_lookup_relative_path(sc,2,path+1);
}

void ext2_dump_file(struct scan_context * sc,const char * path,const char * filename) {
	u32 ino=ext2_lookup_absolute_path(sc,path);
	if (!ino) {
		ERRORF("%s: Couldn't locate inode via path »%s«: %s.",sc->name,path,strerror(errno));
	} else {
		int fd=open(filename,O_WRONLY|O_CREAT|O_TRUNC,0666);
		if (fd<0) {
			ERRORF("%s: Couldn't open file »%s«: %s.",sc->name,filename,strerror(errno));
			return;
		}
		struct fs_inode * inode=ext2_read_inode(sc,ino);
		u32 num_clusters=(inode->data.size+sc->cluster_size_Bytes-1)/sc->cluster_size_Bytes;
		for (u32 cluster=0;cluster<num_clusters;++cluster) {
			u8 * buffer;
			buffer=ext2_inode_read_relative_cluster(inode,cluster);
			int w=write(fd,buffer,sc->cluster_size_Bytes);
			if (w!=(int)sc->cluster_size_Bytes) {
				ERRORF("%s: Couldn't write to file »%s«: %s.",sc->name,filename,strerror(errno));
				break;
			}
			free(buffer);
		}
		close(fd);
		ext2_put_inode(inode);
	}
}

void ext2_redump_file(struct scan_context * sc,const char * path,const char * filename) {
	u32 ino=ext2_lookup_absolute_path(sc,path);
	if (!ino) {
		ERRORF("%s: Couldn't locate inode via path »%s«: %s.",sc->name,path,strerror(errno));
	} else {
		int fd=open(filename,O_RDONLY);
		if (fd<0) {
			ERRORF("%s: Couldn't open file »%s«: %s.",sc->name,filename,strerror(errno));
			return;
		}
		struct fs_inode * inode=ext2_read_inode(sc,ino);
		u32 num_clusters=(inode->data.size+sc->cluster_size_Bytes-1)/sc->cluster_size_Bytes;
		u8 buffer[sc->cluster_size_Bytes];
		for (u32 cluster=0;cluster<num_clusters;++cluster) {
			int r=read(fd,buffer,sc->cluster_size_Bytes);
			if (r!=(int)sc->cluster_size_Bytes) {
				ERRORF("%s: Couldn't read from file »%s«: %s.",sc->name,filename,strerror(errno));
				break;
			}
			ext2_inode_write_relative_cluster(inode,cluster,buffer);
		}
		close(fd);
		ext2_put_inode(inode);
	}
}

/*
 * RESIZE_INODE: Just check, if it exists, then ignore it
 * DIR_INDEX: Unknown how to handle (we ignore it currently, which is not good, but our only chance).
 * HAS_JOURNAL: Just check, if it exists, then ignore it.
 * SPARSE_SUPER: Not every cluster group contains a copy of the super block (and the group descritor table)
 * LARGE_FILE: size_48 may contain the upper 16 bit of the 48 bit size field. Without this feature, that value has to be zero.
 * FILETYPE: A directory entry contains the file type.
 */

struct fs_driver {
	bool (*fsck)(const char * name);
	struct fs * (*mount)(struct fs_driver * driver,const char * device,const char * args);
};

struct fs {
	struct fs_driver * driver;
	struct fs_file_handle * root;
};

struct fs_file {
	struct fs * fs_inode;
};

struct fs_file_handle {
	struct fs_file * file;
	u64 offset;
	bool may_read;
	bool may_write;
	u64 (*read)(struct fs_file_handle * handle,u8 * buffer,u64 num_bytes); // returns 0 on error.
	u64 (*write)(struct fs_file_handle * handle,const u8 * buffer,u64 num_bytes); // returns 0 on error.
	int (*close)(struct fs_file_handle * handle); // returns 0 on success, errno on failure
};

struct fs_dir {
	struct inode * inode;
};

struct fs_dir_handle {
	struct fs_dir * dir;
};

/*
 * This function creates the neccessary in-memory data structures to actually 
 * access files and directories on this file system. It's name is mount like 
 * the usual UNIX mount command. However there are some differences to that 
 * command:
 *
 * 1. This function MUST NEVER write to the file system. The file system may 
 *    provide a separate function to enable write access to an already mounted 
 *    file system. That function may then do any needed journal recovery.
 * 2. In contrary to the UNIX mount command, this function does NOT link the 
 *    file system to a directory in an already mounted file system. In 
 *    notation of this frame work each file system has it's independent name 
 *    space. So each file name begins with that what is given to this function 
 *    as argument name. So, if you use "mount_point" as argument for name, a 
 *    file in the root of that file system is called "mount_point/file". 
 *    However, the prefix of the file names are first mached against all 
 *    mounted file systems, and the longest matching one is then taken to 
 *    access the correct file system. So, giving "" as argument name to the 
 *    mount call for the root file system (which is perfectly ok) and "/usr" 
 *    as the argument name to the mount call for the user file system 
 *    effectively yields mostly the same results as the UNIX concept of file 
 *    system linking. However, there may be some flaws e.g. in conjunction 
 *    with symbolic links. Or we may not access "/usr" and then chdir ".." to 
 *    reach "/". We have to see ...
 */
// static
struct fs * ext2_mount(struct fs_driver * driver,char * name,char * args) {
	ignore(driver);
	ignore(name);
	ignore(args);
//	struct fs_dir * root_dir=...;
	
//	return fs_register_fs(driver,name,root_dir,umount);
	return NULL;
}

#if 0

Verzeichnisse durchsuchen:
	* lineares durchscannen (z.B. für ls, cp -r etc.)
		-> struct fs_dir_scan_ctx * dir_mk_scan_ctx(struct fs_dir * dir)
		-> char * dir_read(struct fs_dir_scan_ctx * ctx)
	* Nach Dateinamen suchen (open etc)
		-> struct fs_dir_scan_ctx * dir_find(struct fs_dir * dir,const char * name)
	-> void fs_dir_destroy_scan_ctx(struct fs_dir_scan_ctx * ctx)

struct fs {
	
};

static struct fs_dir * get_root_dir(struct fs_driver * driver,struct fs * fs) {
	
}

struct fs_dir * fs_open_dir(struct fs_dir * _parent,const char * name) {
	struct ext2_dir * retval;
	
	retval=new_fs_dir(sizeof(*retval));
	if (!retval) return NULL;
	
	retval->
	
	return (struct fs_dir *)retval;
}

static block_t ext2_read(struct dev * dev,block_t first,block_t num,unsigned char * data);
static void loop_destroy(struct dev * dev);
static block_t loop_write(struct dev * dev,block_t first,block_t num,const unsigned char * data);

static struct fs * fs_init(struct fs_driver * fs,char * name,char * args);
#endif

/*
journal_superblock_t journal_super_block;
ext3_inode           journal_inode;         ext2_super_block.s_journal_inum
int                  journal_block_size_;   journal_super_block.s_blocksize
int                  journal_maxlen_;       journal_super_block.s_maxlen
int                  journal_first_;        journal_super_block.s_first
int                  journal_sequence_;     journal_super_block.s_sequence
int                  journal_start_;        journal_super_block.s_start
uint32_t             wrapped_journal_sequence = 0;

if (super_block.s_journal_dev) {
	// Journal device
} else {
	// Journal inode
}
journal_super_block.s_header.h_magic==JFS_MAGIC_NUMBER

#define JFS_MAGIC_NUMBER 0xc03b3998U // The first 4 bytes of /dev/random!

#define JFS_BT_DESCRIPTOR    1
#define JFS_BT_COMMIT        2
#define JFS_BT_SUPERBLOCK_V1 3
#define JFS_BT_SUPERBLOCK_V2 4
#define JFS_BT_REVOKE        5

struct ext2_journal_descriptor_header { // journal_header_s == journal_header_t
	u32 magic;
	u32 blocktype; // JFS_BT_...
	u32 sequence;
};

#define JFS_BTF_ESCAPE    1 // on-disk block is escaped
#define JFS_BTF_SAME_UUID 2 // block has same uuid as previous
#define JFS_BTF_DELETED   4 // block deleted by this transaction
#define JFS_BTF_LAST_TAG  8 // last tag in this descriptor block

struct journal_block_tag_s { // journal_block_tag_s == journal_block_tag_t
	u32 blocknr;
	u32 flags; // JFS_BTF_...
};

struct journal_revoke_header_s { // journal_revoke_header_s == journal_revoke_header_t
	struct ext2_journal_descriptor_header header;
	s32                                   count; // Number of bytes used in this block.
};

struct journal_superblock_s {
/-* 0x000 *-/	struct ext2_journal_descriptor_header header;
		// Static information describing the journal
/-* 0x00c *-/	u32 cluster_size; // Journal device cluster size (in bytes)
/-* 0x010 *-/	u32 max_len; // Total clusters in journal (file)
/-* 0x014 *-/	u32 first; // First block of log information
		// Dynamic information describing the current state of the log
/-* 0x018 *-/	u32 sequence; // first commit ID expected in log
/-* 0x01c *-/	u32 start; // cluster number of start of log
/-* 0x020 *-/	u32 errno; // Error value, as set by journal_abort()
		// Remaining fields are only valid in a version-2 superblock
/-* 0x024 *-/	u32 feature_rwcompat; // rw compatible feature set
/-* 0x028 *-/	u32 feature_incompat; // incompatible feature set
/-* 0x02c *-/	u32 feature_rocompat; // ro compatible feature set
/-* 0x030 *-/	u8  uuid[16]; // 128-bit uuid for journal
/-* 0x040 *-/	u32 nr_users; // Number of file systems sharing this log
/-* 0x044 *-/	u32 dynsuper; // Blocknr of dynamic superblock copy
/-* 0x048 *-/	u32 max_transaction_j_size; // Limit of journal blocks per transaction
/-* 0x04c *-/	u32 max_transaction_d_size; // Limit of data blocks per transaction
/-* 0x050 *-/	u32 padding[44]; // Unused
/-* 0x100 *-/	u8  users[16*48]; // UUID of every FS sharing this log (zero for internal journals)
/-* 0x400 *-/
};

--- 0x00000
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=4 (JFS_BT_SUPERBLOCK_V2)
header.sequence=0
cluster_size=2048
max_len=2048
first=1
sequence=0x0008E1F5
start=0
errno=0
feature_rwcompat=0
feature_incompat=1 (JFS_FEATURE_INCOMPAT_REVOKE)
feature_rocompat=0
uuid[]={ 0xDA,0xBD,0x0F,0xA1, 0x50,0x0C,0x47,0xF2, 0xB8,0xF7,0x0A,0x33, 0x23,0x00,0x12,0x28 }
nr_users=1
dynsuper=0
max_transaction_j_size=0
max_transaction_d_size=0
padding[]={ 0 }
users[]={ 0 }
--- 0x00800
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=1 (JFS_BT_DESCRIPTOR)
header.sequence=0x0008E1E4
x0c=0x000004BE
x10=0
x14=0
x18=0
x1c=0
x20=0
x24=0x2055C002
x28=0x0000000A
--- 0x01000
[data]
--- 0x01800
[data]
--- 0x02000
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=2 (JFS_BT_COMMIT)
header.sequence=0x0008E1E4
--- 0x02800
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=1 (JFS_BT_DESCRIPTOR)
header.sequence=0x0008E1E5
x0c=0x2055C002
x10=8
--- 0x03000
[data]
--- 0x03800
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=2 (JFS_BT_COMMIT)
header.sequence=0x0008E1E5
--- 0x04000
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=1 (JFS_BT_DESCRIPTOR)
header.sequence=0x0008E1E6
x0c=0x2055C002
x10=8
--- 0x04800
[data]
--- 0x05000
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=2 (JFS_BT_COMMIT)
header.sequence=0x0008E1E6
--- 0x05800
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=1 (JFS_BT_DESCRIPTOR)
header.sequence=0x0008E1E7
x0c=0x20560002
x10=0
x14=0
x18=0
x1c=0
x20=0
x24=0x2BAA4000
x28=0x00000002
x2c=0x000002BB
x30=0x00000002
x34=0x2B15AE78
x38=0x0000000A
--- 0x06000
[data]
--- 0x06800
[data] (?inode bitmap?)
--- 0x07000
[data] (?group descriptor table?)
--- 0x07800
[data] (?indirect block?)
--- 0x08000
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=2 (JFS_BT_COMMIT)
header.sequence=0x0008E1E7
--- 0x08800
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=1 (JFS_BT_DESCRIPTOR)
header.sequence=0x0008E1E8
x0c=0x2055C002
x10=0
x14=0
x18=0
x1c=0
x20=0
x24=0x20560002
x28=0x0000000A
--- 0x09000
[data]
--- 0x09800
[data]
--- 0x0a000
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=2 (JFS_BT_COMMIT)
header.sequence=0x0008E1E8
--- 0x0a800
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=2 (JFS_BT_COMMIT)
header.sequence=0x0008E1E9
--- 0x0b000
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=1 (JFS_BT_DESCRIPTOR)
header.sequence=0x0008E1EA
x0c=0x2055C002
x10=8
--- 0x0b800
[data]
--- 0x0c000
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=2 (JFS_BT_COMMIT)
header.sequence=0x0008E1EA
--- 0x0c800
header.magic=0xc03b3998 (JFS_MAGIC_NUMBER)
header.blocktype=1 (JFS_BT_DESCRIPTOR)
header.sequence=0x0008E1EB
37C54001
00000000
00000000
00000000
00000000
00000000
0000037D
00000002
37C54002
00000002
2055C002
00000002
2056000A
00000002
20560002
00000002
37C54000
0000000A
--- 0x0d000
[data] (?cluster bitmap?)
--- 0x0d800
[data] (?group descriptor table?)
--- 0x0e000
[data]
--- 0x0e800
[data]
--- 0x0f000
[data] (directory contents)
--- 0x0f800
[data]
--- 0x10000
[data]
--- 0x10800



#define fv (j)->j_format_version
#define jsb (j)->j_superblock
#define JFS_HAS_RWCOMPAT_FEATURE(j,mask) (fv>=2 && (jsb->s_feature_rwcompat & cpu_to_be32((mask))))

#define JFS_FEATURE_INCOMPAT_REVOKE 0x00000001

#define JFS_KNOWN_RW_COMPAT_FEATURES 0
#define JFS_KNOWN_RO_COMPAT_FEATURES 0
#define JFS_KNOWN_IN_COMPAT_FEATURES JFS_FEATURE_INCOMPAT_REVOKE
*/
