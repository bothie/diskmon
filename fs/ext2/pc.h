#ifndef PC_H
#define PC_H

#include "isc.h"
#include "sc.h"

#include <string.h>

// PM = problem mask

#define PM_IT_EIO (1LLU << 0)
// 	Error while reading the inode table.
// 	No further information about this inode are available.

#define PM_FILE_DIR_EIO (1LLU << 1)
//	Error while reading the contents of this inode (which is a directory,
//	a file or a slow symbolic link)
//	Not all directory entries may have been processed because of this problem

#define PM_DIR_NO_VALID_DOT_DIR (1LLU << 2)
//	There was no "." entry in this directory

#define PM_DIR_NO_PARENT_DIR (1LLU << 3)
//	There was no ".." entry in this directory

/*
#define PM_DIR_PARENT_DIR_INVALID (1LLU << 4)
*/
//	This directory in fact HAS a ".." entry, but that is not pointing to 
//	the parent directory.

#define PM_INODE_WRONG_NUM_BLOCKS (1LLU << 5)
//	The number of blocks entry in this inode is wrong.

/*
#define PM_INODE_MULTIPLY_CLAIMED_CLUSTER (1LLU << 6)
*/
//	One or more clusters which are referenced by this inode are referenced 
//	by another inode (or by the same inode at a different offset), too.

#define PM_IND_EIO (1LLU << 7)
//	Error while reading the contents of this inode (which is a directory)
//	Not all directory entries may have been processed because of this problem

#define PM_ZERO_DTIME (1LLU << 8)
//	Deleted inode has zero dtime

#define PM_WRONG_LINK_COUNT (1LLU << 9)
//	Link count is wrong

#define PM_DIRECTORY_LOOP (1LLU << 10)
//	Directory loop, this directory is part of the actual loop

#define PM_DIRECTORY_HAS_INVALID_DOT_ENTRY (1LLU << 11)
//	A "." entry pointing to a different inode

#define PM_DIRECTORY_HAS_MULTIPLE_VALID_DOT_ENTRIES (1LLU << 12)
//	More than one valid "." entry was found. Additional illegal "." 
//	entries will not be considered for this problem context. I.e.: 
//	Having two illegal "." entries and ONE legal leads to 
//	PM_DIRECTORY_HAS_MULTIPLE_VALID_DOT_ENTRIES remaining cleared.

#define PM_DIRECTORY_PARENT_DOESNT_POINT_TO_US (1LLU << 13)
//	This directory contains a ".." entry, but the target directroy 
//	doesn't point back to this directory

#define PM_DIRECTORY_HAS_MULTIPLE_PARENTS (1LLU << 14)
//	More than one non-"." and non-".." entry points to this directory

#define PM_DIR_DOUBLE_DOT_ENTRY_TO_NON_DIR (1LLU << 15)
//	A ".." entry points to an inode which is not a directory.

#define PM_INODE_IS_DIR (1LLU << 16)
//	This inode, which could not be read due to an i/o error (PM_IT_EIO) is 
//	a directory, because there are directory entries pointing to this inode

#define PM_PARENT_MISSING_ENTRY (1LLU << 17)
//	This directory doen't point to the list of directories, which in turn 
//	point back to this directory with their '..' entries. This will only 
//	be set automatically, if not all directory clusters of this directory
//	could be read (or if even the inode itself could not be read).

#define PM_FREE (1LLU << 18)
//	This inode is PM_IT_EIO but follows an inode of the same group, which 
//	was never in-use. So this inode should have never been in-use either, 
//	because the ext2 driver normally uses the first free inode of a group.
//	If problem_mask & PM_FREE is true, then problem_mask == (PM_IT_EIO | PM_FREE)
//	should be true either.

#define PM_IT_WDN (1LLU << 19)
#define PM_FILE_DIR_WDN (1LLU << 20)
#define PM_IND_WDN (1LLU << 21)

/*
#define PM_INODE_UNREACHABLE (2 * PM_INODE_UNREACHABLE)
//	Starting at the root, there is no path with which this directory may 
//	be reached.

#define PM_DIR_DOT_DIR_INVALID (2 * PM_DIR_DOT_DIR_INVALID)
//	This directory in fact HAS a "." entry, but that is not pointing to 
//	the directory itself.
*/

/*
struct pc_multiply_claimed_cluster {
	VASIAR(unsigned long) inodes;
*/

struct indlevel_block_eio_wdn {
	block_t absolute_block;
	block_t last_relative_block;
	block_t frst_relative_block;
	unsigned level;
	bool wdn;
	bool eio;
};

struct block_eio_wdn {
	block_t absolute_block;
	block_t relative_block;
	bool wdn;
	bool eio;
};

struct problem_context {
	unsigned long problem_mask;
	
	struct inode * inode;
	
	// Only set if bit PM_DIR_DOT_DIR_INVALID is set.
	/*
	unsigned long wrong_backlink_inode;
	*/
	
	// Only set on directories
	unsigned long parent_inode;
	
	// Only set on directories if bit PM_DIR_PARENT_DIR_INVALID is set.
	unsigned long wrong_parent_inode;
	
	// These arrays contain BLOCK (not cluster) numbers.
	// ind_eio also contains the indirect cluster/extent level
	VASIAR(struct block_eio_wdn) file_dir_eio_wdn;
	VASIAR(struct indlevel_block_eio_wdn) ind_eio_wdn;
	
	// Only valid if bit PM_INODE_WRONG_NUM_BLOCKS is set.
	unsigned long num_blocks;
	
	// Only valid if bit PM_WRONG_LINK_COUNT is set.
	unsigned link_count;
	
	// Only valid if bit PM_DIRECTORY_LOOP is set.
	// loop_group is the number of one of the directory inodes which are 
	// part of the actual loop. E.g. if directory a(4) points to b(9) and 
	// b points to c(42) and c points back to a, then loop_group is 4, 9 
	// or 42. Which of this is not specified, however, it's the same for 
	// every member of the loop, i.e. pc(4)->loop_group == pc(9)->loop_group
	// && pc(9)->loop_group == pc(42)->loop_group
	unsigned long loop_group;
	
	// Only valid if bit PM_DIRECTORY_HAS_INVALID_DOT_ENTRY is set.
	VASIAR(unsigned long) invalid_dot_entry_target;
	
	// Only valid if bit PM_DIRECTORY_PARENT_DOESNT_POINT_TO_US is set.
	unsigned long missing_entry_to_us_parent;
	
	// Only valid if bit PM_DIRECTORY_HAS_MULTIPLE_PARENTS is set.
	vasiar_struct_inode_owning_map_entry_vasiar parents;

	// Only valid if bit PM_DIR_DOUBLE_DOT_ENTRY_TO_NON_DIR is set.
	VASIAR(unsigned long) bad_double_entry_dir;
	
	// Only valid if bit PM_PARENT_MISSING_ENTRY is set.
	VASIAR(unsigned long) missing_entry;
};

static inline struct problem_context * get_problem_context(struct scan_context * sc
	, unsigned long inode
) {
	return sc->inode_problem_context[inode - 1];
}

void pc_free(struct problem_context * pc);

struct problem_context * add_problem_context(struct scan_context * sc
	, unsigned long inode
	, unsigned long pm_bit
);

static inline struct problem_context * add_problem_reading_inode_table_eio(struct scan_context * sc
	, unsigned long inode
) {
	struct problem_context * pc = add_problem_context(sc, inode, PM_IT_EIO);
	return pc;
}

static inline struct problem_context * add_problem_context_isc(struct inode_scan_context * isc
	, unsigned long pm_bit
) {
	struct problem_context * pc = add_problem_context(isc->sc, isc->inode_num, pm_bit);
	if (!pc->inode && isc->inode) {
		if (isc->keep_inode) {
			struct inode * inode_copy = malloc(128);
			assert(inode_copy);
			memcpy(inode_copy, isc->inode, 128);
			pc->inode = inode_copy;
		} else {
			pc->inode = isc->inode;
			isc->keep_inode = true;
		}
	}
	return pc;
}

static inline struct problem_context * add_problem_wrong_num_blocks(struct inode_scan_context * isc
	, unsigned long num_blocks
) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_INODE_WRONG_NUM_BLOCKS);
	pc->num_blocks = num_blocks;
	return pc;
}

static inline struct problem_context * add_problem_reading_dir_eio(struct inode_scan_context * isc
	, block_t absolute_block
	, block_t relative_block
) {
	assert(isc->is_dir);
	struct problem_context * pc = add_problem_context_isc(isc, PM_FILE_DIR_EIO);
	struct block_eio_wdn b;
	b.absolute_block = absolute_block;
	b.relative_block = relative_block;
	b.eio = true;
	b.wdn = !b.eio;
	VANEW(pc->file_dir_eio_wdn) = b;
	return pc;
}

static inline struct problem_context * add_problem_reading_dir_wdn(struct inode_scan_context * isc
	, block_t absolute_block
	, block_t relative_block
) {
	assert(isc->is_dir);
	struct problem_context * pc = add_problem_context_isc(isc, PM_FILE_DIR_WDN);
	struct block_eio_wdn b;
	b.absolute_block = absolute_block;
	b.relative_block = relative_block;
	b.eio = false;
	b.wdn = !b.eio;
	VANEW(pc->file_dir_eio_wdn) = b;
	return pc;
}

static inline struct problem_context * add_problem_reading_file_eio(struct inode_scan_context * isc
	, block_t absolute_block
	, block_t relative_block
) {
	assert(!isc->is_dir);
	struct problem_context * pc = add_problem_context_isc(isc, PM_FILE_DIR_EIO);
	struct block_eio_wdn b;
	b.absolute_block = absolute_block;
	b.relative_block = relative_block;
	b.eio = true;
	b.wdn = !b.eio;
	VANEW(pc->file_dir_eio_wdn) = b;
	return pc;
}

static inline struct problem_context * add_problem_reading_file_wdn(struct inode_scan_context * isc
	, block_t absolute_block
	, block_t relative_block
) {
	assert(!isc->is_dir);
	struct problem_context * pc = add_problem_context_isc(isc, PM_FILE_DIR_WDN);
	struct block_eio_wdn b;
	b.absolute_block = absolute_block;
	b.relative_block = relative_block;
	b.eio = false;
	b.wdn = !b.eio;
	VANEW(pc->file_dir_eio_wdn) = b;
	return pc;
}

static inline struct problem_context * add_problem_reading_indirect_cluster_eio(struct inode_scan_context * isc
	, unsigned level
	, block_t absolute_block
	, block_t frst_relative_block
	, block_t last_relative_block
) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_IND_EIO);
	struct indlevel_block_eio_wdn lb;
	lb.level = level;
	lb.absolute_block = absolute_block;
	lb.frst_relative_block = frst_relative_block;
	lb.last_relative_block = last_relative_block;
	lb.eio = true;
	lb.wdn = !lb.eio;
	VANEW(pc->ind_eio_wdn) = lb;
	return pc;
}

static inline struct problem_context * add_problem_reading_indirect_cluster_wdn(struct inode_scan_context * isc
	, unsigned level
	, block_t absolute_block
	, block_t frst_relative_block
	, block_t last_relative_block
) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_IND_WDN);
	struct indlevel_block_eio_wdn lb;
	lb.level = level;
	lb.absolute_block = absolute_block;
	lb.frst_relative_block = frst_relative_block;
	lb.last_relative_block = last_relative_block;
	lb.eio = false;
	lb.wdn = !lb.eio;
	VANEW(pc->ind_eio_wdn) = lb;
	return pc;
}

static inline struct problem_context * add_problem_zero_dtime(struct inode_scan_context * isc) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_ZERO_DTIME);
	return pc;
}

static inline struct problem_context * add_problem_wrong_link_count(struct inode_scan_context * isc
	, unsigned link_count
) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_WRONG_LINK_COUNT);
	pc->link_count = link_count;
	return pc;
}

static inline struct problem_context * add_problem_directory_loop(struct inode_scan_context * isc
	, unsigned long group
) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_DIRECTORY_LOOP);
	pc->loop_group = group;
	return pc;
}

static inline struct problem_context * add_problem_invalid_dot_entry(struct inode_scan_context * isc
	, unsigned long inode
) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_DIRECTORY_HAS_INVALID_DOT_ENTRY);
	VANEW(pc->invalid_dot_entry_target) = inode;
	return pc;
}

static inline struct problem_context * add_problem_duplicated_dot_entry(struct inode_scan_context * isc) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_DIRECTORY_HAS_MULTIPLE_VALID_DOT_ENTRIES);
	return pc;
}

static inline struct problem_context * add_problem_orphan_double_dot_entry(struct inode_scan_context * isc
	, unsigned long parent
) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_DIRECTORY_PARENT_DOESNT_POINT_TO_US);
	pc->missing_entry_to_us_parent = parent;
	return pc;
}

static inline struct problem_context * add_problem_duplicated_forward_entry(struct inode_scan_context * isc
	, struct inode_owning_map_entry * parent_ome
) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_DIRECTORY_HAS_MULTIPLE_PARENTS);
	VANEW(pc->parents) = parent_ome;
	return pc;
}

static inline struct problem_context * add_problem_has_no_valid_dot_entry(struct inode_scan_context * isc) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_DIR_NO_VALID_DOT_DIR);
	return pc;
}

static inline struct problem_context * add_problem_has_no_parent(struct inode_scan_context * isc) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_DIR_NO_PARENT_DIR);
	return pc;
}

static inline struct problem_context * add_problem_double_dot_entry_to_non_dir(struct inode_scan_context * isc
	, unsigned long parent
) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_DIR_DOUBLE_DOT_ENTRY_TO_NON_DIR);
	VANEW(pc->bad_double_entry_dir) = parent;
	return pc;
}

static inline struct problem_context * add_problem_eio_inode_is_dir(struct inode_scan_context * isc) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_INODE_IS_DIR);
	return pc;
}

static inline struct problem_context * add_problem_parent_directory_is_missing_entry_to_dir(struct inode_scan_context * isc
	, unsigned long entry
) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_PARENT_MISSING_ENTRY);
	VANEW(pc->missing_entry) = entry;
	return pc;
}

static inline struct problem_context * add_problem_free(struct inode_scan_context * isc) {
	struct problem_context * pc = add_problem_context_isc(isc, PM_FREE);
	return pc;
}

#endif // #ifndef PC_H
