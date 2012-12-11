#define _GNU_SOURCE

#include "ext2.h"

#include "common.h"
#include "bdev.h"

#include <btendian.h>

#include <assert.h>
#include <btlock.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <mprintf.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/sched.h>
extern int clone(int (*)(void *),void *,int,void *,...);

static inline int gettid() {
	return syscall(SYS_gettid);
}

int btclone(void * * stack_memory,int (*thread_main)(void * arg),size_t stack_size,int flags,void * arg) {
#if HAVE_LINUX_CLONE
	*stack_memory=malloc(stack_size);
	void * stack_argument;
	if (unlikely(!*stack_memory)) {
		return -1;
	}
	
	if ((char *)&stack_memory>(char *)&stack_argument) {
		stack_argument=(char *)*stack_memory+stack_size;
	} else {
		stack_argument=*stack_memory;
	}
	
	int retval=clone(thread_main,stack_argument,flags,arg);
	
	if (likely(retval>=0)) {
		return retval;
	}
	
	free(*stack_memory);
	*stack_memory=NULL;
	return retval;
#else // #if HAVE_LINUX_CLONE
	ignore(stack_memory);
	ignore(thread_main);
	ignore(stack_size);
	ignore(flags);
	ignore(arg);
	errno=ENOSYS;
	return -1;
#endif // #if HAVE_LINUX_CLONE, else
}

volatile unsigned live_child;
unsigned hard_child_limit=295;
unsigned soft_child_limit=245;

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

void endian_swap_gdt(struct group_desciptor * gdt,unsigned num_groups) {
	for (unsigned group=0;group<num_groups;++group) {
		le32(gdt[group].cluster_allocation_map);
		le32(gdt[group].inode_allocation_map);
		le32(gdt[group].inode_table);
		le16(gdt[group].num_free_clusters);
		le16(gdt[group].num_free_inodes);
		le16(gdt[group].num_directories);
	}
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
	for (int i=0;i<NUM_CLUSTER_POINTERS;++i) {
		le32(inode->cluster[i]);
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

#define MAX_PREREADED 512
#define PREREADED_NOTIFYTIME 10

struct scan_context {
	struct bdev * bdev;
	struct group_desciptor * gdt;
	struct inode * inode_table;
	struct btlock_lock * cam_lock;
	struct progress_bar * progress_bar;
	struct super_block * sb;
	struct dir * * dir;
	
	u16 * link_count;
	
//	struct read_tables_list * list_head;
//	struct read_tables_list * list_tail;
	
	const char * name;
	
	u8 * calculated_cluster_allocation_map;
	u8 * calculated_cluster_collision_map;
	u8 * calculated_inode_allocation_map;
	u8 * cluster_allocation_map;
	u8 * inode_allocation_map;
	
	u16 * calculated_link_count_array;
	
	block_t cluster_size_Blocks;
	block_t cs;
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
	volatile unsigned prereaded;
	
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
	if (1!=bdev_read(sc->bdev,offset,1,(void *)sb)) {
		ERRORF("%s: Couldn't read block %llu while trying to read super block of group %u.",sc->name,(unsigned long long)offset,group);
		return false;
	} else {
		endian_swap_sb(sb);
		
		if (sb->magic==MAGIC) {
			if (sb->my_group!=group) {
				ERRORF("%s: Cluster group %u contains a super block backup claiming belonging to cluster group %u.",sc->name,(unsigned)group,sb->my_group);
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

void process_table_for_group(unsigned group,struct scan_context * sc) {
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
	
	struct inode * inode=sc->inode_table+sc->sb->inodes_per_group*group;
	
/*
	memcpy(
		inode,
		inode2,
		sc->sb->inodes_per_group*sizeof(*inode)
	);
	
	free(sc->list_head->block_buffer);
*/
	
	for (unsigned i=0;i<sc->sb->inodes_per_group;++i) {
		endian_swap_inode(inode+i);
	}
	
	assert(sc->prereaded--);
//	eprintf("process_table_for_group(%u): free(list_head->bb=%p)\n",group,cmd_struct->list_head->block_buffer);
}

bool read_table_for_one_group(struct scan_context * sc) {
	// struct read_tables_list * list=sc->list_tail;
	
	unsigned counter;
	
	counter=0;
	while (sc->prereaded==MAX_PREREADED) {
		if (++counter==PREREADED_NOTIFYTIME) {
//			NOTIFY("read_table_for_one_group: Going to sleep to let the foreground process catch up ...");
		}
		sleep(1);
	}
	
	++sc->prereaded;
	
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
	u8 *    it_b=(u8*)(sc->inode_table+sc->sb->inodes_per_group*sc->table_reader_group);
	
	size_t  cb_n=sc->cluster_size_Bytes;
	size_t  ib_n=sc->sb->inodes_per_group/8;
	size_t  it_n=sc->sb->inodes_per_group*sizeof(*sc->inode_table);
	
	block_t cb_a=sc->cluster_size_Blocks*sc->gdt[sc->table_reader_group].cluster_allocation_map;
	block_t ib_a=sc->cluster_size_Blocks*sc->gdt[sc->table_reader_group].inode_allocation_map;
	block_t it_a=sc->cluster_size_Blocks*sc->gdt[sc->table_reader_group].inode_table;
	
	block_t cb_s=sc->cluster_size_Blocks;
	block_t ib_s=sc->cluster_size_Blocks;
	block_t it_s=sc->cluster_size_Blocks*((it_n+sc->cluster_size_Bytes-1)/sc->cluster_size_Bytes);
	
	if (cb_s!=bdev_read(sc->bdev,cb_a,cb_s,cb_b)) {
		/*
		 * This error is in no way critical as we don't trust that 
		 * data anyways, we do this reading only to CHECK wether the 
		 * data are correct and inform the user if it wasn't.
		 */
		NOTIFYF("%s: Error reading cluster allocation map for group %u (ignored, will be recalculated anyways ...)",sc->name,sc->table_reader_group);
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
	if (ib_s!=bdev_read(sc->bdev,ib_a,ib_s,it_b)) {
		/*
		 * Again: Just ignore errors in reading of bitmap blocks.
		 */
		NOTIFYF("%s: Error reading inode allocation map for group %u (ignored, will be recalculated anyways ...)",sc->name,sc->table_reader_group);
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
	 *  - try to reach each block if bulk reading fails.
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
//		eprintf("%s->read(%llu,1,%p)=%lli\n",bdev_get_name(sc->bdev),(unsigned long long)it_a,it_b,(int long long)r);
		if (1!=r) {
			have_zero_errors=true;
			r=bdev_short_read(sc->bdev,it_a,1,it_b,error_bitmap);
//			eprintf("%s->short_read(%llu,1,%p,%p)=%lli\n",bdev_get_name(sc->bdev),(unsigned long long)it_a,it_b,error_bitmap,(int long long)r);
			if (1!=r) {
				have_hard_errors=true;
				have_soft_errors=true;
				memset(it_b,0,bs);
			} else {
				// struct inode * it=(struct inode *)error_bitmap;
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
		// eprintf("\n");
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
	
	if (!sc->background) {
		process_table_for_group(sc->table_reader_group,sc);
	}
	
	return true;
}

int read_tables(void * arg) {
	struct scan_context * sc=(struct scan_context *)arg;
	
	eprintf("Executing read_tables via thread %i\n",gettid());
	
	OPEN_PROGRESS_BAR_LOOP(0,sc->table_reader_group,sc->num_groups)
	
		read_table_for_one_group(sc);
		
		++sc->table_reader_group;
		
		PRINT_PROGRESS_BAR(sc->table_reader_group);
	CLOSE_PROGRESS_BAR_LOOP()

	if (sc->background) {
		NOTIFYF("%s: Background reader is successfully quitting",sc->name);
		--live_child;
	}
	return 0;
}

int ext2_read_tables(void * arg) {
	return read_tables(arg);
}

#define xset_bit(bitmap,bitoff) do { \
	u8 * b=(bitmap); \
	u32 o=(bitoff); \
	b[o>>3]|=1<<(o&7); \
} while (0)

static inline bool xget_bit(u8 * bitmap,u32 bitoff) {
	return !!(bitmap[bitoff>>3]&(1<<(bitoff&7)));
}

//		NOTIFYF("%s: Cluster %llu used multiple times.",name,(unsigned long long)cluster);

#define MARK_CLUSTER_IN_USE(sc,cluster) do { \
	u32 c=(cluster); \
	btlock_lock(sc->cam_lock); \
	if (xget_bit(sc->calculated_cluster_allocation_map,c)) { \
		xset_bit(sc->calculated_cluster_collision_map,c); \
	} else { \
		xset_bit(sc->calculated_cluster_allocation_map,c); \
	} \
	btlock_unlock(sc->cam_lock); \
} while (0)

void process_dir_cluster(struct inode_scan_context * isc,unsigned long cluster) {
	u8 buffer[isc->sc->cluster_size_Bytes];
	void * ptr=(void *)buffer;
	
	if (isc->sc->cluster_size_Blocks!=bdev_read(
		isc->sc->bdev,cluster*isc->sc->cluster_size_Blocks,isc->sc->cluster_size_Blocks,ptr)
	) {
		ERRORF("%s: Inode %lu [%s]: Error while reading directory block %lu",isc->sc->name,isc->inode_num,isc->type,cluster);
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
			offset+=reclen;
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
		
		if (inode<=isc->sc->sb->num_inodes) {
			++isc->sc->link_count[inode-1];
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
			ERRORF("%s: Inode %lu [%s]: reclen is to small for filename. reclen=%u, decoded filename of size %u: \"%s\".",isc->sc->name,isc->inode_num,isc->type,(unsigned)reclen,(unsigned)filename_len,filename);
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

#define CHK_BLOCK(level,cluster) chk_block(isc,level,cluster)
void chk_block(struct inode_scan_context * isc,int level,unsigned long cluster) {
//	eprintf("chk_block(level=%i,cluster=%lu)\n",level,cluster);
	
	if (likely(!cluster)) {
		unsigned long h=1;
		
		while (level--) h*=isc->sc->num_clusterpointers;
		
		isc->maybe_holes+=h;
		
		return;
	}
	
	MARK_CLUSTER_IN_USE(isc->sc,cluster);
	
	isc->holes+=isc->maybe_holes;
	isc->maybe_holes=0;
	
	if (unlikely(cluster>=isc->sc->sb->num_clusters)) {
		++isc->illegal_ind_clusters[level];
		return;
	}
	
	if (isc->is_dir && !level) {
		process_dir_cluster(isc,cluster);
	}

	++isc->used_clusters;
	
	if (likely(!level)) return;
	
	u32 pointer[isc->sc->num_clusterpointers];
	void * ptr=(void *)pointer;
	
	if (isc->sc->cluster_size_Blocks!=bdev_read(isc->sc->bdev,cluster*isc->sc->cluster_size_Blocks,isc->sc->cluster_size_Blocks,ptr)) {
		ERRORF("%s: Inode %lu [%s]: Error while reading indirect block %lu",isc->sc->name,isc->inode_num,isc->type,cluster);
	} else {
		for (unsigned i=0;i<isc->sc->num_clusterpointers;++i) {
			chk_block(isc,level-1,pointer[i]);
		}
	}
}

struct thread {
	struct thread * volatile next;
	struct thread * volatile prev;
	void * stack_memory;
	struct btlock_lock * lock;
	pid_t tid;
};

struct thread * thread_head=NULL;
struct thread * thread_tail=NULL;

bool do_chk_block_function(struct inode_scan_context * isc) {
	for (unsigned z=0;z<NUM_ZIND;++z) {
		CHK_BLOCK(0,isc->inode->cluster[z]);
	}
	
	CHK_BLOCK(1,isc->inode->cluster[FIRST_SIND]);
/*
	if (inode->cluster[FIRST_DIND] || inode->cluster[FIRST_TIND]) {
		PRINT_PROGRESS_BAR(inode_num);
	}
*/
	CHK_BLOCK(2,isc->inode->cluster[FIRST_DIND]);
/*
	if (inode->cluster[FIRST_DIND] || inode->cluster[FIRST_TIND]) {
		PRINT_PROGRESS_BAR(inode_num);
	}
*/
	CHK_BLOCK(3,isc->inode->cluster[FIRST_TIND]);
	
	unsigned long illegal_blocks=0;
	for (int i=0;i<4;++i) illegal_blocks+=isc->illegal_ind_clusters[i];
	
	if (isc->is_dir) {
		unsigned ino=isc->inode_num-1;
		
		// Every directory points back to itself, so increment link count
		++isc->sc->calculated_link_count_array[ino];
		
		if (unlikely(!isc->sc->dir[ino])) {
			/*
			 * We report missing . and .. entries only, if the 
			 * directory doesn't contain illegal clusters.
			 *
			 * TODO: Same shall apply, if at least one cluster 
			 * couldn't be read.
			 */
			if (unlikely(!illegal_blocks)) {
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
			++isc->sc->calculated_link_count_array[d->entry[i].inode-1];
			
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
	if (illegal_blocks) {
		ERRORF(
			"%s: Inode %lu [%s] contains %lu illegal block "
			"(%lu zind, %lu sind, %lu dind, %lu tind)."
			,isc->sc->name,isc->inode_num,isc->type
			,illegal_blocks
			,isc->illegal_ind_clusters[0],isc->illegal_ind_clusters[1]
			,isc->illegal_ind_clusters[2],isc->illegal_ind_clusters[3]
		);
//		isc->ok=false;
	}
	
	if (isc->inode->size<isc->used_clusters+isc->holes) {
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
	
	bool retval=isc->sc->waiting4threads;
	
	free(isc);
	
	return retval;
}

int chk_block_function(void * arg) {
	struct inode_scan_context * isc=(struct inode_scan_context *)arg;
	
	/*
	 * isc will be freed by do_chk_block_function, so we must save all 
	 * values, which are needed after the call to it.
	 */
	
	const char * name=isc->sc->name;
	unsigned long inode_num=isc->inode_num;
	char * type=isc->type;
	
	if (do_chk_block_function(isc)) NOTIFYF("%s: Inode %lu [%s]: Background job completed.",name,inode_num,type);
	
	return 0;
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
	
	if ((live_child<hard_child_limit && isc->inode->cluster[FIRST_TIND])
	||  (live_child<soft_child_limit && isc->inode->cluster[FIRST_DIND])) {
		struct thread * t=malloc(sizeof(*t));
		if (!t) {
			// eprintf("malloc(t) failed: %m\n");
			goto inode_scan_clone_failed;
		}
		
		t->lock=btlock_lock_mk();
		if (!t->lock) {
			// eprintf("lock_mk failed: %m\n");
			free(t);
			goto inode_scan_clone_failed;
		}
		
		if ((t->tid=btclone(
			&t->stack_memory,
			chk_block_function,
			65536,
			CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD,
			(void *)isc
		))<=0) {
			// if (errno!=ENOSYS) eprintf("btclone failed: %m\n");
			btlock_lock_free(t->lock);
			free(t);
			
inode_scan_clone_failed:
			do_chk_block_function(isc);
//			NOTIFYF("%s: Inode %lu [%s]: Failed to clone and file is VERY big, skipping processing cluster scan to speed up debugging",name,inode_num,type);
		} else {
			++live_child;
			/*
			 * Now, we link us to the thread list. We assure, that the last element 
			 * remains the last and the first remains the first. We will be the second 
			 * last element after this operation.
			 */
			
			// First we lock ourself, so we don't get hurt in the very last moment ...
			btlock_lock(t->lock);
			
#if 0
			/*
			 * Now, we lock thread_tail. It's a little bit more complicated than just 
			 * lock(thread_tail->lock). Think about it, before you change the code ;)
			 *
			 * !!! BEWARE OF RACES !!!
			 */
			for (;;) {
				struct thread * tt=thread_tail;
				lock(tt->lock);
				if (!tt->next) break;
				unlock(tt->lock);
			}
#else
			btlock_lock(thread_tail->lock); // thread_tail doesn't change, so this is safe.
#endif
			// Now, we got thread_tail, now lock it's precessor.
			btlock_lock(thread_tail->prev->lock);
			
			/*
			 * Now, we are fine, we can just link us as we do with any doubly linked list.
			 */
			t->prev=thread_tail->prev;
			t->next=thread_tail;
			t->prev->next=t;
			t->next->prev=t;
			
			/*
			 * Unlocking is straight forward: Just do it ;)
			 */
			btlock_unlock(t->lock);
			btlock_unlock(t->next->lock);
			btlock_unlock(t->prev->lock);
			
			{
				struct thread * walk=thread_head->next;
				while (walk->tid) {
					if (kill(walk->tid,0)) {
						walk->next->prev=walk->prev;
						walk->prev->next=walk->next;
						free(walk->stack_memory);
						btlock_lock_free(walk->lock);
						struct thread * w=walk;
						walk=walk->prev;
						free(w);
						--live_child;
					}
					walk=walk->next;
				}
			}
	
			/*
			pid_t tid;
			
			while ((tid=waitpid(-1,NULL,WNOHANG|__WCLONE))>0) {
				struct thread * walk=thread_head;
				while (walk && walk->tid!=tid) {
					walk=walk->next;
				}
				if (walk->tid==tid) {
					walk->next->prev=walk->prev;
					walk->prev->next=walk->next;
					free(walk->stack_memory);
					lock_free(walk->lock);
					free(walk);
				}
			}
			*/
			
			if (isc->sc->waiting4threads) NOTIFYF("%s: Inode %lu [%s]: Processing inode's cluster allocation check in background",isc->sc->name,isc->inode_num,isc->type);
		}
	} else {
		do_chk_block_function(isc);
	}
}

void typecheck_inode(struct inode_scan_context * isc) {
	if (!isc->inode->links_count) {
		isc->type_bit=0;
		if (isc->inode->mode) {
			isc->type="DELE";
			if (!isc->inode->dtime) {
				if (isc->sc->warn_dtime_zero) NOTIFYF("%s: Inode %lu [%s] (deleted) has zero dtime",isc->sc->name,isc->inode_num,isc->type);
				isc->inode->dtime=isc->sc->sb->wtime;
			}
		} else {
			isc->type="FREE";
		}
	} else {
		switch (MASK_FT(isc->inode)) {
			case MF_BDEV: isc->type="BDEV"; isc->type_bit=FTB_BDEV; break;
			case MF_CDEV: isc->type="CDEV"; isc->type_bit=FTB_CDEV; break;
			case MF_DIRE: isc->type="DIRE"; isc->type_bit=FTB_DIRE; break;
			case MF_FIFO: isc->type="FIFO"; isc->type_bit=FTB_FIFO; break;
			case MF_SLNK: isc->type="SLNK"; isc->type_bit=FTB_SLNK; break;
			case MF_FILE: isc->type="FILE"; isc->type_bit=FTB_FILE; break;
			case MF_SOCK: isc->type="SOCK"; isc->type_bit=FTB_SOCK; break;
			default: isc->type="ILLT"; isc->type_bit=0; break;
		}
	}
}

void check_inode(struct scan_context * sc,unsigned long inode_num,bool prefetching) {
	struct inode_scan_context * isc=malloc(sizeof(*isc));
	assert(isc);
	isc->inode=(isc->sc=sc)->inode_table+((isc->inode_num=inode_num)-1);
	
	if (prefetching) {
		isc->sc->table_reader_group=(isc->inode_num-1)/isc->sc->sb->inodes_per_group;
		
		read_table_for_one_group(isc->sc);
	}
	
	typecheck_inode(isc);
	
	isc->is_dir=isc->type_bit&FTB_DIRE;
	
	bool ok=true;
	
	if (!isc->inode->links_count) {
		free(isc);
		return;
	}
	
	if (xget_bit(isc->sc->calculated_inode_allocation_map,isc->inode_num-1)) {
		free(isc);
		return;
	}
	
	xset_bit(isc->sc->calculated_inode_allocation_map,isc->inode_num-1);
	
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
	
	if (isc->type_bit&~(FTB_DIRE|FTB_FILE)) {
		if (isc->inode->flags&INOF_IMMUTABLE) {
			ERRORF("%s: Inode %lu [%s] has IMMUTABLE flag set.",isc->sc->name,isc->inode_num,isc->type);
			ok=false;
		}
		if (isc->inode->flags&INOF_APPEND_ONLY) {
			ERRORF("%s: Inode %lu [%s] has APPEND_ONLY flag set.",isc->sc->name,isc->inode_num,isc->type);
			ok=false;
		}
		if (isc->inode->flags&INOF_SECURE_REMOVE) {
			ERRORF("%s: Inode %lu [%s] has SECURE_REMOVE flag set.",isc->sc->name,isc->inode_num,isc->type);
			ok=false;
		}
	}
	
	if (isc->type_bit&~(FTB_DIRE|FTB_FILE|FTB_SLNK)) {
		if (isc->inode->flags&INOF_SYNC) {
			ERRORF("%s: Inode %lu [%s] has SYNC flag set.",isc->sc->name,isc->inode_num,isc->type);
			ok=false;
		}
	}
	
	if (isc->inode->flags&INOF_E2COMPR_FLAGS) {
		if (!(isc->sc->sb->in_compat&IN_COMPAT_COMPRESSION)) {
			ERRORF("%s: Inode %lu [%s] has at lease one compression flag set but filesystem has the feature bit not set",isc->sc->name,isc->inode_num,isc->type);
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
					if (isc->inode->flags&INOF_E2COMPR_DIRTY) {
						NOTIFYF("%s: Inode %lu [%s] has flag E2COMPR_DIRTY set.",isc->sc->name,isc->inode_num,isc->type);
					}
					
					if (isc->inode->flags&INOF_E2COMPR_COMPR) {
						NOTIFYF("%s: Inode %lu [%s] has flag E2COMPR_COMPR set.",isc->sc->name,isc->inode_num,isc->type);
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
	
	// We ignore INOF_UNRM and INOF_TOOLCHAIN completely
	
	if (isc->inode->flags&INOF_UNKNOWN_FLAGS) {
		// We report this problem, but don't consider doing anything about it.
		NOTIFYF("%s: Inode %lu [%s] has unknown flag bits set: %lu.",isc->sc->name,isc->inode_num,isc->type,(unsigned long)isc->inode->flags&INOF_UNKNOWN_FLAGS);
	}
	
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
	||  ((isc->type_bit&FTB_SLNK) && (isc->inode->size<64))) {
		if (isc->type_bit&~FTB_SLNK) {
			for (int c=1;c<NUM_CLUSTER_POINTERS;++c) {
				if (isc->inode->cluster[c]) {
					NOTIFYF("%s: Inode %lu [%s] has cluster[%i] set.",isc->sc->name,isc->inode_num,isc->type,c);
				}
			}
		}
		
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

bool ext2_fsck(const char * _name) {
	struct scan_context * sc;
	
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
	sc->inode_table=NULL;
	sc->cam_lock=NULL;
	sc->progress_bar=NULL;
	sc->sb=NULL;
	sc->dir=NULL;
	sc->link_count=NULL;
//	sc->list_head=NULL;
//	sc->list_tail=NULL;
	sc->calculated_cluster_allocation_map=NULL;
	sc->calculated_cluster_collision_map=NULL;
	sc->calculated_inode_allocation_map=NULL;
	sc->cluster_allocation_map=NULL;
	sc->inode_allocation_map=NULL;
	
	sc->bdev=bdev_lookup_bdev(sc->name=_name);
	if (!sc->bdev) {
		ERRORF("Couldn't lookup device %s.",sc->name);
		return false;
	}
	
	eprintf("Pass 1a: Find and read and check the master super block ...\n");
	
	sc->block_size=bdev_get_block_size(sc->bdev);
	
	MALLOC1(sc->sb);
	
	sc->cs=0;
	// First try to read super block from block 0
	if (read_super(sc,sc->sb,0,sc->cs,true)) {
		goto found_superblock;
	}
	
	// Now, try all cluster sizes we support.
	for (sc->cs=1;sc->cs<64;sc->cs<<=1) {
		if (read_super(sc,sc->sb,0,sc->cs,true)) {
			goto found_superblock;
		}
	}
	
	ERRORF("Couldn't find ext2 superblock on %s.",sc->name);
	goto cleanup;

found_superblock:
	sc->sb->my_group=0;
	
	sc->cluster_size_Blocks=2<<(block_t)sc->sb->log_cluster_size;
	sc->cluster_size_Bytes=512*sc->cluster_size_Blocks;
	sc->num_groups=((sc->sb->num_clusters-(sc->sb->first_data_cluster+1))/sc->sb->clusters_per_group)+1;
	
	NOTIFYF("%s: File system has %u cluster groups with a cluster size of %u.",sc->name,sc->num_groups,(unsigned)sc->cluster_size_Bytes);
	
	eprintf("Pass 1b: Read and check group descriptor table ...\n");
	
	sc->size_of_gdt_Blocks=(sc->num_groups*sizeof(*sc->gdt)+sc->block_size-1)/sc->block_size;
	
	MALLOCBYTES(sc->gdt,sc->size_of_gdt_Blocks*sc->block_size);
	
	sc->group_size_Blocks=(block_t)sc->sb->clusters_per_group*(block_t)sc->cluster_size_Blocks;
	
	bool have_gdt=false;
	for (unsigned pass=0;pass<3;++pass) {
		block_t sb_offset;
		
		for (unsigned group=0;group<sc->num_groups;++group) {
			if (!pass == !group_has_super_block(sc->sb,group)) {
				continue;
			}
			
			if (!group) {
				sb_offset=(sc->cs/sc->cluster_size_Blocks+1)*sc->cluster_size_Blocks;
			} else {
				sb_offset=(block_t)group*sc->group_size_Blocks+sc->cluster_size_Blocks;
			}
			
			if (sc->size_of_gdt_Blocks==bdev_read(sc->bdev,sb_offset,sc->size_of_gdt_Blocks,(void*)sc->gdt)) {
				have_gdt=true;
				break;
			}
		}
		if (have_gdt) {
			break;
		}
	}
	
	if (!have_gdt) {
		ERRORF("%s: Couldn't read group descriptor table: %s.",sc->name,strerror(errno));
		return 2;
	}
	
	endian_swap_gdt(sc->gdt,sc->num_groups);
	
#define NUM_ZIND 12 // zero    indirect -> direct
	
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
	
	/*
	 * We swap here the gdt again, so we don't need to swap all backup 
	 * versions while we read them. Howewer, we have to reswap all over 
	 * again after the loop, of course.
	 */
	endian_swap_gdt(sc->gdt,sc->num_groups);
	
	{
		struct super_block * sb2;
		MALLOC1(sb2);
		struct group_desciptor * gdt2;
		MALLOCBYTES(gdt2,sc->size_of_gdt_Blocks*sc->block_size);
		/*
		int fd;
		fd=open("/ramfs/gdt.0.img",O_WRONLY|O_CREAT|O_TRUNC,0666);
		write(fd,sc->gdt,sc->num_groups*sizeof(*sc->gdt));
		close(fd);
		*/
		for (unsigned group=1;group<sc->num_groups;++group) {
			if (group_has_super_block(sc->sb,group)) {
				block_t sb_offset=(block_t)group*sc->group_size_Blocks;
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
				
#define CMP32(v) if (sb2->v!=sc->sb->v) eprintf("%s in backup copy is %lu, should be %lu\n",#v,(unsigned long)sb2->v,(unsigned long)sc->sb->v);
#define CMP16(v) if (sb2->v!=sc->sb->v) eprintf("%s in backup copy is %lu, should be %lu\n",#v,(unsigned long)sb2->v,(unsigned long)sc->sb->v);
#define CMP08(v) if (sb2->v!=sc->sb->v) eprintf("%s in backup copy is %lu, should be %lu\n",#v,(unsigned long)sb2->v,(unsigned long)sc->sb->v);
#define CMP32n(v,n) do { for (int i=0;i<n;++i) { CMP32(v[i]); } } while (0)
#define CMP16n(v,n) do { for (int i=0;i<n;++i) { CMP16(v[i]); } } while (0)
#define CMP08n(v,n) do { for (int i=0;i<n;++i) { CMP08(v[i]); } } while (0)
				
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
				CMP08n(volume_name,16);
				CMP08n(last_mount_point,64);
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
				CMP08(reserved_char_pad);
				CMP16(reserved_word_pad);
				CMP32(default_mount_opts);
				CMP32(first_meta_bg);
				CMP32n(reserved,62);
				
				assert(!memcmp(sc->sb,sb2,sc->block_size));
				
				if (1) { // Deactivate for VALGRIND if you like
					if (sc->size_of_gdt_Blocks!=bdev_read(sc->bdev,(sb_offset/sc->cluster_size_Blocks+1)*sc->cluster_size_Blocks,sc->size_of_gdt_Blocks,(void*)gdt2)) {
						eprintf("\033[31m");
						ERRORF("%s: Couldn't read group descriptor table of group %u: %s.",sc->name,group,strerror(errno));
						eprintf("\033[0m");
					} else {
						/*
						char * tmp;
						fd=open(tmp=mprintf("/ramfs/%s.gdt.%03u.img",sc->name,group),O_WRONLY|O_CREAT|O_TRUNC,0666);
						write(fd,gdt2,sc->num_groups*sizeof(*sc->gdt));
						close(fd);
						free(tmp);
						*/
						
						for (unsigned g=0;g<sc->num_groups;++g) {
							gdt2[g].num_free_clusters=sc->gdt[g].num_free_clusters;
							gdt2[g].num_free_inodes=sc->gdt[g].num_free_inodes;
							gdt2[g].num_directories=sc->gdt[g].num_directories;
						}
						if (memcmp(sc->gdt,gdt2,sc->num_groups*sizeof(*sc->gdt))) {
							ERRORF("%s: Group descriptor tables of groups 0 and %u differ",sc->name,group);
						}
					}
				}
			}
		}
		free(gdt2);
		free(sb2);
		
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
	
	endian_swap_gdt(sc->gdt,sc->num_groups);
	
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
		
		if (sc->sb->rw_compat&RW_COMPAT_UNKNOWN
		||  sc->sb->ro_compat&RO_COMPAT_UNKNOWN
		||  sc->sb->in_compat&IN_COMPAT_UNKNOWN) {
			ERRORF("%s: This file system uses unknown features.",sc->name);
			return false;
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
	
	if (sc->sb->rw_compat&RW_COMPAT_IMAGIC_INODES) {
		ERRORF("%s: This file system has the RW compatible feature flag IMAGIC_INODES set which I don't know how to handle.",sc->name);
		return false;
	}
	if (sc->sb->rw_compat&RW_COMPAT_EXT_ATTR) {
		ERRORF("%s: This file system has the RW compatible feature flag EXT_ATTR set which I don't know how to handle.",sc->name);
		return false;
	}
	
	if (sc->sb->ro_compat&RO_COMPAT_BTREE_DIR) {
		ERRORF("%s: This file system has the RO compatible feature flag BTREE_DIR set which I don't know how to handle.",sc->name);
		return false;
	}
	
	if (sc->sb->in_compat&IN_COMPAT_COMPRESSION) {
		ERRORF("%s: This file system has the IN compatible feature flag COMPRESSION set which I don't know how to handle.",sc->name);
		return false;
	}
	if (sc->sb->in_compat&IN_COMPAT_RECOVER) {
		if (sc->sb->rw_compat&RW_COMPAT_HAS_JOURNAL) {
			ERRORF("%s: This file system contains a journal AND NEEDS RECOVERY, which I don't know how to handle. I'm going to ignore the journal and remove the 'needs recovery flag' after having the file system's error fixed.",sc->name);
			sc->sb->ro_compat&=~IN_COMPAT_RECOVER;
		} else {
			eprintf("%s: This file system is marked to not contain a journal, but needing recovery.",sc->name);
			eprintf("%s: <j> Fix by making this file system to contain a journal.",sc->name);
			eprintf("%s: <r> Fix by making this file system not needing recovery.",sc->name);
			eprintf("%s: <n> Don't fix and quit now.",sc->name);
			int answer;
			do {
				answer=fgetc(stderr);
				answer=tolower(answer);
			} while (answer!='j' && answer!='r' && answer!='n');
			switch (answer) {
				case 'j':
					printf("%s: This file system is marked to not contain a journal, but needing recovery. Fixed by marking file system containing a journal.",sc->name);
					sc->sb->rw_compat&=~RW_COMPAT_HAS_JOURNAL;
					write_super_blocks(sc->sb);
					goto recheck_compat_flags;
				case 'r':
					printf("%s: This file system is marked to not contain a journal, but needing recovery. Fixed by marking file system not needing recovery.",sc->name);
					sc->sb->ro_compat&=~IN_COMPAT_RECOVER;
					break;
				case 'n':
					printf("%s: This file system is marked to not contain a journal, but needing recovery. Not fixed.",sc->name);
					return false;
				default:
					assert(never_reached);
			}
		}
	}
	if (sc->sb->in_compat&IN_COMPAT_JOURNAL_DEV) {
		ERRORF("%s: This file system has the IN compatible feature flag JOURNAL_DEV set (this is not the HAS_JOURNAL flag neither the needs RECOVERy flag) which I don't know how to handle.",sc->name);
		return false;
	}
	if (sc->sb->in_compat&IN_COMPAT_META_BG) {
		ERRORF("%s: This file system has the IN compatible feature flag META_BG set which I don't know how to handle.",sc->name);
		return false;
	}
	
	if (sc->sb->rw_compat&RW_COMPAT_RESIZE_INODE) {
		NOTIFYF("%s: This file system has the RW compatible feature flag RESIZE_INODE set.",sc->name);
	}
	if (sc->sb->rw_compat&RW_COMPAT_DIR_INDEX) {
		NOTIFYF("%s: This file system has the RW compatible feature flag DIR_INDEX set.",sc->name);
	}
	if (sc->sb->rw_compat&RW_COMPAT_HAS_JOURNAL) {
		NOTIFYF("%s: This file system has the RW compatible feature flag HAS_JOURNAL set.",sc->name);
	}
	
	if (sc->sb->ro_compat&RO_COMPAT_SPARSE_SUPER) {
		NOTIFYF("%s: This file system has the RO compatible feature flag SPARSE_SUPER set.",sc->name);
	}
	if (sc->sb->ro_compat&RO_COMPAT_LARGE_FILE) {
		NOTIFYF("%s: This file system has the RO compatible feature flag LARGE_FILE set.",sc->name);
	}
	
	if (sc->sb->in_compat&IN_COMPAT_FILETYPE) {
		NOTIFYF("%s: This file system has the IN compatible feature flag FILETYPE set.",sc->name);
	}
	
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
	MALLOCARRAY(sc->inode_table,sc->sb->num_inodes); // FIXME: BUG: Just for VALGRIND's extreme memory comsumption
	
	MALLOCARRAY(sc->calculated_link_count_array,sc->sb->num_inodes);
	
	MALLOCBYTES(sc->calculated_cluster_allocation_map,cam_size_Bytes);
	MALLOCBYTES(sc->calculated_cluster_collision_map,cam_size_Bytes);
	MALLOCBYTES(sc->calculated_inode_allocation_map,iam_size_Bytes);
	
	for (unsigned ino=0;ino<sc->sb->num_inodes;++ino) {
		sc->calculated_link_count_array[ino]=0;
	}
	
/*
	sc->cam_iam_it_Blocks=sc->cluster_size_Blocks*(
		2+(sc->sb->inodes_per_group*sizeof(*sc->inode_table)+sc->cluster_size_Bytes-1)/sc->cluster_size_Bytes
	);
*/
	
	void * stack_memory;
	
	sc->num_clusterpointers=sc->cluster_size_Bytes/4;
//	sc->cam_iam_it_Clusters=sc->cam_iam_it_Blocks/sc->cluster_size_Blocks;
	
//	MALLOC1(sc->list_head);
//	sc->list_tail=sc->list_head;
	
	eprintf("Executing ext2_fsck via thread %i\n",gettid());
	
	memset(sc->calculated_cluster_allocation_map,0,cam_size_Bytes);
	memset(sc->calculated_cluster_collision_map,0,cam_size_Bytes);
	memset(sc->calculated_inode_allocation_map,0,iam_size_Bytes);
	
	MALLOC1(thread_head);
	MALLOC1(thread_tail);
	thread_head->lock=btlock_lock_mk();
	thread_tail->lock=btlock_lock_mk();
	assert(thread_head->lock && thread_tail->lock);
	thread_head->tid=0;
	thread_tail->tid=0;
	thread_head->next=thread_tail; thread_head->prev=NULL;
	thread_tail->prev=thread_head; thread_tail->next=NULL;
	
	/*
	 * We have to initialize both arrays, dir and link_count to zero. But 
	 * link_count is just a scalar, while dir is a pointer array. So we 
	 * alloc link_count before dir, as failing to alloc dir won't cause 
	 * troubles if link_count is not initialized yet. The other way round 
	 * would need taking special care in the clean up code.
	 */
	MALLOCARRAY(sc->link_count,sc->sb->num_inodes);
	MALLOCARRAY(sc->dir,sc->sb->num_inodes);
	for (unsigned i=0;i<sc->sb->num_inodes;++i) {
		sc->dir[i]=NULL;
		sc->link_count[i]=0;
	}
	
	/*
	 * We mis-use this variable here to mark, that the new cluster scan 
	 * threads should be reported to the user. Normally they are just 
	 * silently created.
	 */
	sc->waiting4threads=true;
	
	sc->cam_lock=btlock_lock_mk();
	assert(sc->cam_lock);
	
	sc->background=false;
	sc->prereaded=0;
	
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
#endif // #if HAVE_LINUX_CLONE
	
	sc->waiting4threads=false;
	sc->background=true;
	sc->table_reader_group=0;
	
	if (btclone(
		&stack_memory,
		ext2_read_tables,
		65536,
		CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD,
		(void *)sc
	)<=0) {
		if (errno!=ENOSYS) eprintf("clone failed, now first we'll read in the tables and then process them (instead of doing both at the same time)\n");
		sc->background=false;
		ext2_read_tables(sc);
	} else {
		++live_child;
	}
	
	/*
	 * First, we mark all blocks uses by the cluster/inode allocation maps 
	 * and inode table as in-use.
	 */
	{
		unsigned num_clusters_per_group_for_inode_table=sc->sb->inodes_per_group/(sc->cluster_size_Bytes/sizeof(struct inode));
		for (unsigned group=0;group<sc->num_groups;++group) {
			MARK_CLUSTER_IN_USE(sc,sc->gdt[group].cluster_allocation_map);
			MARK_CLUSTER_IN_USE(sc,sc->gdt[group].inode_allocation_map);
			for (unsigned i=0;i<num_clusters_per_group_for_inode_table;++i) {
				MARK_CLUSTER_IN_USE(sc,sc->gdt[group].inode_table+i);
			}
		}
	}
	
	struct progress_bar * live_child_progress_bar=progress_bar_mk(2,hard_child_limit);
	assert(live_child_progress_bar);
	
	{
		u32 size_of_gdt_Clusters=sc->size_of_gdt_Blocks/sc->cluster_size_Blocks;
		unsigned group=0;
		unsigned long inode_num=1;
		
		OPEN_PROGRESS_BAR_LOOP(1,inode_num,sc->sb->num_inodes)
			
			print_stalled_progress_bar(live_child_progress_bar,live_child);
			
			while (sc->table_reader_group==group) {
				PROGRESS_STALLED(inode_num);
				sleep(1);
			}
			
//			eprintf("Parent's current group: %u\r",group);
			
			if (sc->background) process_table_for_group(group,sc);
			
			if (group_has_super_block(sc->sb,group)) {
				u32 cluster=group*(sc->group_size_Blocks/sc->cluster_size_Blocks);
				if (!group && sc->cluster_size_Bytes<2048) {
					/*
					 * In the first cluster group, the 
					 * super block may start at byte offset 
					 * 1024 instead of 0. If The cluster 
					 * size is >=2048, that's no problem, as 
					 * the entire super block still resides 
					 * in cluster 0. On file systems with a 
					 * cluster size of 1024 however, the 
					 * super block uses cluster 1. So, 
					 * cluster 0 is unused (and all other 
					 * structures move one cluster to 
					 * higher addresses). So, mark cluster 
					 * 0 as being used and increment cluster 
					 * number for super block.
					 */
					MARK_CLUSTER_IN_USE(sc,cluster++);
				}
				
				// Mark cluster containing super block as in-use
				MARK_CLUSTER_IN_USE(sc,cluster);
				
				// Mark all clusters containing the group descriptor table as in-use
				for (u32 i=0;i<size_of_gdt_Clusters;++i) {
					MARK_CLUSTER_IN_USE(sc,++cluster);
				}
			}
			
			{
				struct thread * walk=thread_head->next;
				while (walk->tid) {
					if (kill(walk->tid,0)) {
						walk->next->prev=walk->prev;
						walk->prev->next=walk->next;
						free(walk->stack_memory);
						btlock_lock_free(walk->lock);
						struct thread * w=walk;
						walk=walk->prev;
						free(w);
						--live_child;
					}
					walk=walk->next;
				}
			}
			
			for (unsigned ino=0;ino<sc->sb->inodes_per_group;++ino,++inode_num) {
				PRINT_PROGRESS_BAR(inode_num);
				
				check_inode(sc,inode_num,false);
			}
			++group;
			
		CLOSE_PROGRESS_BAR_LOOP()
		
		progress_bar_free(live_child_progress_bar);
	}
	
	/*
	 * Now let's mark all reserved inodes as in-use regardless wether they 
	 * really are in-use.
	 */
	for (unsigned ino=1;ino<sc->sb->first_inode;++ino) {
		xset_bit(sc->calculated_inode_allocation_map,ino-1);
	}
	
	eprintf("Waiting for unfinished background jobs to finish ...\n");
	
	sc->waiting4threads=true;
	
	while (thread_head->next!=thread_tail) {
		struct thread * walk=thread_head->next;
		while (walk->tid) {
			if (kill(walk->tid,0)) {
				walk->next->prev=walk->prev;
				walk->prev->next=walk->next;
				free(walk->stack_memory);
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
	
	assert(!live_child);
	
	if (memcmp(sc->cluster_allocation_map,sc->calculated_cluster_allocation_map,(size_t)sc->cluster_size_Bytes*(size_t)sc->num_groups)) {
		ERRORF("%s: cluster allocation map differs!",sc->name);
	}
	
	for (unsigned i=0;i<(size_t)sc->cluster_size_Bytes*(size_t)sc->num_groups;++i) {
		if (sc->calculated_cluster_collision_map[i]) {
			ERRORF("%s: There are doubly used clustes.",sc->name);
			i=-2;
		}
	}
	
	if (memcmp(sc->inode_allocation_map,sc->calculated_inode_allocation_map,(sc->sb->num_inodes+7)/8)) {
		ERRORF("%s: inode allocation map differs!",sc->name);
	}
	
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
	
	int fd;
	{
		int w;
		
		OPEN("/ramfs/cluster_allocation_map.read",O_WRONLY|O_CREAT|O_TRUNC,0666);
		w=write(fd,sc->cluster_allocation_map,cam_size_Bytes);
		close(fd);
		
		OPEN("/ramfs/cluster_allocation_map.calculated",O_WRONLY|O_CREAT|O_TRUNC,0666);
		w=write(fd,sc->calculated_cluster_allocation_map,cam_size_Bytes);
		close(fd);
		
		OPEN("/ramfs/cluster_allocation_map.collisions",O_WRONLY|O_CREAT|O_TRUNC,0666);
		w=write(fd,sc->calculated_cluster_collision_map,cam_size_Bytes);
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
		free(sc->gdt);
		free(sc->inode_table);
		free(sc->cam_lock);
		free(sc->progress_bar);
		free(sc->sb);
		free(sc->dir);
		free(sc->link_count);
//		free(sc->list_head);
//		assert(sc->list_tail==sc->list_head);
		free(sc->calculated_cluster_allocation_map);
		free(sc->calculated_cluster_collision_map);
		free(sc->calculated_inode_allocation_map);
		free(sc->cluster_allocation_map);
		free(sc->inode_allocation_map);
		free(sc);
	}
	return retval;
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

struct fs_inode {
	struct fs * fs;
	u32 number;
	struct inode * data;
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
#if 0
static struct fs * mount(struct fs_driver * driver,char * name,char * args) {
	ignore(driver);
	struct fs_dir * root_dir=...;
	
	return fs_register_fs(driver,name,root_dir,umount);
}
#endif // #if 0

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

#if 0
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
#endif // #if 0
