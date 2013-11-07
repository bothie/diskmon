#ifndef ISC_H
#define ISC_H

#include "bdev.h"

#include <vasiar.h>

struct inode_scan_context {
	struct scan_context * sc;
	unsigned long inode_num;
	struct inode * inode;
	bool keep_inode;
	char * type;
	unsigned type_bit;
	bool is_dir;
	bool has_io_errors;
	block_t schedule_first_cluster;
	unsigned long schedule_num_clusters;
	unsigned long schedule_containing;
	
	unsigned long illegal_ind_clusters[4];
	unsigned long read_error_ind_blocks[4];
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

struct inode_owning_map_entry {
	u32 parent;
	bool virtual;
	char name[1];
};
typedef VASIAR(struct inode_owning_map_entry *) vasiar_struct_inode_owning_map_entry_vasiar;

struct inode_owning_map_entry * mk_iome_sl(unsigned long inode_num,const char * filename,size_t sl,bool virtual);

struct inode_owning_map_entry * mk_iome(unsigned long inode_num,const char * filename,bool virtual);

#endif // #ifndef ISC_H
