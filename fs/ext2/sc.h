#ifndef SC_H
#define SC_H

#include "conf.h"

#include "com_cache.h"
#include "isc.h"

#include <btatomic.h>

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
	
	// struct read_tables_list * list_head;
	// struct read_tables_list * list_tail;
	
	const char * name;
	
	u8 * calculated_inode_allocation_map;
	u8 * cluster_allocation_map;
	u8 * calculated_cluster_allocation_map;
	u8 * inode_allocation_map;
	
	vasiar_struct_inode_owning_map_entry_vasiar * inode_owning_map;
	struct btlock_lock * inode_owning_map_lock;
	struct problem_context * * inode_problem_context;
	struct inode_owning_map_entry * * inode_parent;
	unsigned long * loop_protect;
	unsigned char * inode_type;
	
	block_t cluster_size_Blocks;
//	block_t cs;
	block_t group_size_Blocks;
	block_t size_of_gdt_Blocks;
	
#define block_size_Bytes block_size
	unsigned block_size;
//	unsigned cam_iam_it_Blocks;
//	unsigned cam_iam_it_Clusters;
	unsigned des_per_groupcluster_size_Bytes;
//	unsigned long group;
	unsigned long num_groups;
	
//	unsigned long long num_clusters;
	
	size_t cluster_size_Bytes;
	size_t num_clusterpointers_per_block;
	size_t num_clusterpointers_per_cluster;
	unsigned num_clusterpointers_per_cluster_shift;
	
	u32 clusters_per_group;
	
	bool surface_scan_used;
	bool surface_scan_full;
	// bool do_surface_scan; use surface_scan_used or surface_scan_full instead
	
	bool dump_files;
	bool show_cam_diffs;
	
	/*
	 * This variable get's updated in the reader thread to the group 
	 * currently being read in and therefore MUST NOT get cached by the 
	 * parent thread by optimizing read operations away.
	 *
	 * The reader thread is finished as soon as group == num_groups
	 *
	 * Please note that group is being updated without any locking at all.
	 */
	volatile unsigned long table_reader_group;
	atomic_t prereaded;
	struct btlock_lock * table_reader_full;
	struct btlock_lock * table_reader_empty;
	
	/*
	 * Tells, wether multithreading is actually being used.
	 * 
	 * If threading support is deactivated, this is always false. This
	 * way, code doesn't always have to use constructs like
	 *
	 * #ifndef THREADS
	 * 	if (!sc->threads) {
	 * #endif // #ifndef THREADS
	 * 		so stuff without multithreading
	 * #ifndef THREADS
	 * 	if (!sc->threads) {
	 * #endif // #ifndef THREADS
	 */
	bool threads;
	bool allow_concurrent_chk_block_function;
	bool allow_concurrent_table_reader;
	
	/*
	 * Tells, wether the reader is running in the background or not. If 
	 * it is NOT, it MUST process each group's memcpy()s before reading 
	 * the next group, as it will most probably run out of memory if it 
	 * doesn't.
	 */
	bool background;
	
#ifdef THREADS
	/*
	 * A little bit eye-candy: As soon as the main loop has finished, we 
	 * set waiting4threads to true, which causes threads being busy in the 
	 * cluster allocation check loop to notify their finish. If not set, 
	 * they just die off silently.
	 */
	volatile bool waiting4threads;
#endif // #ifdef THREADS

	/*
	 * Some means to configure automatic runs. Currently I only provide a 
	 * means to omit uncritical messages. But later on, this may be 
	 * extended up to supporting a fully automatic run.
	 */
	bool warn_ctime;
	bool warn_mtime;
	bool warn_atime;
	bool warn_dtime_zero;
	bool warn_dtime_nonzero;
	
	bool warn_expect_zero_inodes;
};

#endif // #ifndef SC_H
