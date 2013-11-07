#include "fsck.h"

#include "conf.h"

#include "ext2.h"
#include "bitmap.h"
#include "endian.h"
#include "pc.h"
#include "sc.h"
#include "threads.h"

#include "common.h"
#include "bdev.h"

#include <assert.h>
#include <btatomic.h>
#include <btendian.h>
#include <btlock.h>
#include <btthread.h>
#include <btsf.h>
#include <btstr.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <mprintf.h>
#include <stdbool.h>
#include <stddbg.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#ifdef THREADS
#include <signal.h>
#include <sys/types.h>
#endif // #ifdef THREADS

#define LIST_VASIAR(vasiar,list,FMT,castfunction) do { \
	struct string_factory * sf=sf_new(); \
	for (size_t i=0;i<VASIZE(vasiar);++i) { \
		sf_printf( \
			sf, \
			" "FMT \
			,castfunction(VAACCESS(vasiar,i)) \
		); \
	} \
	list=sf_c_str(sf); \
} while(0)

static inline unsigned long ul2ul(unsigned long x) { return x; }
static inline unsigned long long block_t2ull(block_t x) { return x; }

#define LIST_VASIAR_UNSIGNED_LONG(vasiar,list) LIST_VASIAR(vasiar,list,"%lu",ul2ul)
#define LIST_VASIAR_BLOCK_T(vasiar,list) LIST_VASIAR(vasiar,list,"%llu",block_t2ull)

#define LIST_VASIAR_IND_BLOCK_T(vasiar,list) do { \
	struct string_factory * sf=sf_new(); \
	for (size_t i=0;i<VASIZE(vasiar);++i) { \
		sf_printf( \
			sf, \
			" %llu" \
			,block_t2ull(VAACCESS(vasiar,i).absolute_block) \
		); \
	} \
	list=sf_c_str(sf); \
} while (0)

#define MALLOCBYTES(var,size) do { \
	if (!(var=malloc(size))) { \
		ERRORF("malloc returned %p while trying to allocate %llu bytes in %s:%s:%i: %s",(void*)var,(unsigned long long)size,__FILE__,__FUNCTION__,__LINE__,strerror(errno)); \
		goto cleanup; \
	} \
} while(0)

#define MALLOCARRAY(var,num) MALLOCBYTES(var,sizeof(*var)*num)

#define MALLOC1(var) MALLOCARRAY(var,1)

static inline unsigned long long u64_to_ull(u64 * v) { return *v; }
static inline unsigned long u32_to_ul(u32 * v) { return *v; }
static inline unsigned u16_to_u(u16 * v) { return *v; }
static inline unsigned u08_to_u(u8  * v) { return *v; }

bool fsck(const char * _name) {
	init_stddbg();
	
	struct scan_context * sc;
	struct super_block * sb2 = NULL;
	struct group_desciptor_v1 * gdt2v1 = NULL;
	struct group_desciptor_v2 * gdt2v2 = NULL;
	struct group_desciptor_in_memory * gdt2im = NULL;
	
	bool retval=false;
	
	MALLOC1(sc);
	
	sc->threads = USE_THREADS_DEFAULT;
	sc->allow_concurrent_chk_block_function = ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION;
	/*
	sc->allow_concurrent_table_reader = ALLOW_CONCURRENT_TABLE_READER;
	*/
	sc->allow_concurrent_table_reader = false;
	
	/*
	 * Dump files without errors to rescued/ok and those with errors
	 * to rescued/errors in error reporting pass.
	 */
	sc->dump_files = false;
	
	/*
	 * surface_scan_used: Additionally to performing the normal file 
	 * system check, read each file's data block(s).
	 * Normally, only the file system structures (including indirect 
	 * clusters) will be read to perform the integrity check.
	 */
	sc->surface_scan_used=false;
	
	/*
	 * Perform an additional surface scan of unused blocks. The unused 
	 * blocks of directory clusters will be checked in the normal inode 
	 * scan pass, the free clusters will be read in an additional pass.
	 * TODO: Acutally DO this additional pass.
	 */
	sc->surface_scan_full=false;
	
	/*
	 * Upon completion of the scan, we compare the calculated cluster
	 * allocation map with the one stored on disk. It they differ, the
	 * user is notified. Setting this to true also lists the actual
	 * differences (in constrast of just telling that there are
	 * differences with no further details, which will be done, if
	 * this is false)
	 */
	sc->show_cam_diffs = false;
	
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
	
	sc->warn_expect_zero_inodes = false;
	
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
//	sc->list_head=NULL;
//	sc->list_tail=NULL;
	sc->calculated_inode_allocation_map=NULL;
	sc->cluster_allocation_map=NULL;
	sc->inode_allocation_map=NULL;
	sc->comc.ccgroup=NULL;
#ifdef COMC_IN_MEMORY
	sc->comc.compr=NULL;
#endif // #ifdef COMC_IN_MEMORY
	sc->inode_problem_context = NULL;
	
	struct thread_ctx * com_cache_thread_ctx = NULL;
	struct thread_ctx * ext2_read_tables_thread_ctx = NULL;
	
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
	
	sc->block_size_Bytes = bdev_get_block_size(sc->bdev);
	
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
	
	NOTIFYF("%s: File system has %lu cluster groups with a cluster size of %u bytes.", sc->name, (unsigned long)sc->num_groups, (unsigned)sc->cluster_size_Bytes);
	
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
			
			char * tmp = mprintf("ext2 gdt[%u]", group);
			if (sc->size_of_gdt_Blocks == bdev_read(sc->bdev, sb_offset, sc->size_of_gdt_Blocks, sc->gdtv1 ? (u8 *)sc->gdtv1 : (u8 *)sc->gdtv2, tmp)) {
				free(tmp);
				have_gdt=true;
				gdt_from_group_0 = !group;
				break;
			}
			free(tmp);
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
		gdt_disk2memory_v1(sc->gdt, sc->gdtv1, sc->num_groups);
	} else {
		gdt_disk2memory_v2(sc->gdt, sc->gdtv2, sc->num_groups);
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
			MALLOCBYTES(gdt2v1,sc->size_of_gdt_Blocks*sc->block_size_Bytes);
		} else {
			MALLOCBYTES(gdt2v2,sc->size_of_gdt_Blocks*sc->block_size_Bytes);
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
				
				if (memcmp(sc->sb,sb2,sc->block_size_Bytes)) {
					NOTIFYF(
						"%s: Super blocks of groups 0 and %lu differ"
						,sc->name
						,group
					);
				}
				// assert(!memcmp(sc->sb,sb2,sc->block_size_Bytes));
				
#define CMP64(v) if (gdt2im[g].v != sc->gdt[g].v) { diff = true; eprintf("%s in backup group descriptor table %lu at index %lu is %llu, should be %llu\n",#v,group,g,u64_to_ull(&gdt2im[g].v),u64_to_ull(&sc->gdt[g].v)); }
#define CMP32(v) if (gdt2im[g].v != sc->gdt[g].v) { diff = true; eprintf("%s in backup group descriptor table %lu at index %lu is %lu, should be %lu\n",#v,group,g,u32_to_ul(&gdt2im[g].v),u32_to_ul(&sc->gdt[g].v)); }
#define CMP16(v) if (gdt2im[g].v != sc->gdt[g].v) { diff = true; eprintf("%s in backup group descriptor table %lu at index %lu is %u, should be %u\n",#v,group,g,u16_to_u(&gdt2im[g].v),u16_to_u(&sc->gdt[g].v)); }
				
				if (1) { // Deactivate for VALGRIND if you like
					char * tmp = mprintf("ext2 gdt[%lu]", group);
					if (sc->size_of_gdt_Blocks != bdev_read(sc->bdev, (sb_offset / sc->cluster_size_Blocks + 1) * sc->cluster_size_Blocks, sc->size_of_gdt_Blocks, sc->gdtv1 ? (u8 *)gdt2v1 : (u8 *)gdt2v2, tmp)) {
						ERRORF(
							"%s: \033[31mCouldn't read group descriptor table of group %lu: %s.\033[0m"
							, sc->name
							, group
							, strerror(errno)
						);
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
					free(tmp);
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
		/*
		sc->sb->rw_compat &= ~RW_COMPAT_HAS_JOURNAL;
		sc->sb->in_compat &= ~IN_COMPAT_JOURNAL_DEV;
		// FIXME: Mark that we may not write back super block
		*/
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
	
	sc->inode_owning_map_lock=btlock_lock_mk();
	
	MALLOCBYTES(sc->cluster_allocation_map,cam_size_Bytes);
	MALLOCBYTES(sc->inode_allocation_map,iam_size_Bytes);
	off_t inode_table_len; // Will be needed for unmapping
	int inode_table_fd=open("/ramfs/inode_table",O_RDWR|O_CREAT|O_TRUNC,0600);
	{
		if (inode_table_fd<0) {
			eprintf("main-thread: OOPS: Couldn't open/create file /ramfs/inode_table: %s",strerror(errno));
			exit(2);
		}
		inode_table_len=sizeof(*sc->inode_table);
		assert(sc->sb->num_inodes==(sc->sb->num_inodes/sc->sb->inodes_per_group)*sc->sb->inodes_per_group);
#ifdef THREADS
		if (sc->threads) {
			if ((sc->sb->num_inodes/sc->sb->inodes_per_group)>INODE_TABLE_ROUNDROUBIN) {
				inode_table_len*=sc->sb->inodes_per_group*INODE_TABLE_ROUNDROUBIN;
			} else {
				inode_table_len*=sc->sb->num_inodes;
			}
		} else {
#endif // #ifdef THREADS
			inode_table_len*=sc->sb->inodes_per_group;
#ifdef THREADS
		}
#endif // #ifdef THREADS
		--inode_table_len;
		if (inode_table_len!=lseek(inode_table_fd,inode_table_len,SEEK_SET)) {
			eprintf("main-thread: OOPS: Couldn't seek in file /ramfs/inode_table: %s",strerror(errno));
			exit(2);
		}
		if (1!=write(inode_table_fd,&inode_table_len,1)) {
			eprintf("main-thread: OOPS: Couldn't write one byte to file /ramfs/inode_table: %s",strerror(errno));
			exit(2);
		}
		++inode_table_len;
		sc->inode_table=mmap(NULL,inode_table_len,PROT_READ|PROT_WRITE,MAP_SHARED,inode_table_fd,0);
		if (sc->inode_table==MAP_FAILED) {
			eprintf("main-thread: OOPS: Couldn't mmap file /ramfs/inode_table: %s",strerror(errno));
			exit(2);
		}
		/*
		 * If I understood the POSIX man page for close correctly, 
		 * closing a memory mapped file doesn't guarantee the file to 
		 * remain valid and mapped, if the file got (or gets?) 
		 * unlinked and is not open any longer.
		 * So, if the user removes the file prior to this call to 
		 * close, the mapping may be destroyed. So we can't close the 
		 * file here. We will do it in the clean up phase.
		 */
	}
	
	MALLOCARRAY(sc->inode_link_count,sc->sb->num_inodes); // 2*
	MALLOCARRAY(sc->inode_type_str,sc->sb->num_inodes); // 4*
	MALLOCARRAY(sc->inode_owning_map,sc->sb->num_inodes); // 12*
	MALLOCARRAY(sc->inode_problem_context,sc->sb->num_inodes); // 4*
	for (u32 ino=0;ino<sc->sb->num_inodes;++ino) {
		VAINIT(sc->inode_owning_map[ino]);
		sc->inode_problem_context[ino]=NULL;
	}
	MALLOCARRAY(sc->inode_parent,sc->sb->num_inodes); // 4*
	MALLOCARRAY(sc->loop_protect,sc->sb->num_inodes); // 4*
	MALLOCARRAY(sc->inode_type,sc->sb->num_inodes); // 1*
	MALLOCBYTES(sc->calculated_inode_allocation_map,iam_size_Bytes);
	
/*
	sc->cam_iam_it_Blocks=sc->cluster_size_Blocks*(
		2+(sc->sb->inodes_per_group*sizeof(*sc->inode_table)+sc->cluster_size_Bytes-1)/sc->cluster_size_Bytes
	);
*/
	
	sc->num_clusterpointers_per_cluster=sc->cluster_size_Bytes/4;
	sc->num_clusterpointers_per_block=sc->block_size_Bytes/4;
	sc->num_clusterpointers_per_cluster_shift=8+sc->sb->log_cluster_size;
//	sc->cam_iam_it_Clusters=sc->cam_iam_it_Blocks/sc->cluster_size_Blocks;
	
//	MALLOC1(sc->list_head);
//	sc->list_tail=sc->list_head;
	
	memset(sc->calculated_inode_allocation_map,0,iam_size_Bytes);
	
	MALLOCARRAY(sc->dir,sc->sb->num_inodes);
	for (unsigned i=0;i<sc->sb->num_inodes;++i) {
		sc->dir[i]=NULL;
	}
	
	com_cache_init(sc,sc->sb->num_clusters);
	
	sc->cam_lock=btlock_lock_mk();
	assert(sc->cam_lock);
	
	atomic_set(&sc->prereaded,0);
	
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
	
#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
	thread_head->lock=btlock_lock_mk();
	thread_tail->lock=btlock_lock_mk();
	assert(thread_head->lock && thread_tail->lock);
//	thread_head->tid=0;
//	thread_tail->tid=0;
	thread_head->next=thread_tail; thread_head->prev=NULL;
	thread_tail->prev=thread_head; thread_tail->next=NULL;
#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
	
#ifdef THREADS
	ind_lock=btlock_lock_mk();
	MALLOC1(thread_head);
	MALLOC1(thread_tail);
	
	/*
	 * We mis-use this variable here to mark, that the new cluster scan 
	 * threads should be reported to the user. Normally they are just 
	 * silently created.
	 */
	sc->waiting4threads=true;
	sc->background=false;
	
	/*
	 * This starts the cluster allocation check for very big inodes 
	 * immediatelly in the beginning of the file system check. If the 
	 * CLONE feature is not present on the current platform, it doesn't 
	 * make sense to do this in the very beginning, so we do it only if 
	 * the CLONE feature is present. If it is not, this inodes will be 
	 * reqularly processed with all the others as they are at turn.
	 */
	/*
	if (sc->threads) {
		check_inode(sc,385,true);
		check_inode(sc,389,true);
		check_inode(sc,4238220,true);
		check_inode(sc,4238222,true);
		check_inode(sc,4238223,true);
		check_inode(sc,6145418,true);
	}
	*/
	sc->waiting4threads=false;
	sc->background=sc->threads;
	
	if (sc->threads) {
		eprintf("Executing ext2_fsck main thread via thread %i\n", gettid());
	}
#else // #ifndef THREADS
	sc->threads = false;
	sc->background = false;
#endif // #ifndef THREADS, else
	
	sc->table_reader_group=0;
	sc->com_cache_thread_lock=btlock_lock_mk();
	sc->table_reader_full=btlock_lock_mk();
	sc->table_reader_empty=btlock_lock_mk();
	
#ifdef THREADS
	if (sc->threads) {
		com_cache_thread_ctx = create_thread(65536, com_cache_thread, (THREAD_ARGUMENT_TYPE)sc);
		if (!com_cache_thread_ctx) {
			NOTIFY("clone for com_cache_thread failed, switching multithreading off.\n");
			sc->threads=false;
			// FIXME: If we started some background threads earlier, wait for them before
			// setting sc->threads to false
		}
	}
#endif // #ifdef THREADS
	
#ifdef ALLOW_CONCURRENT_TABLE_READER
#ifdef THREADS
	if (sc->background) {
		ext2_read_tables_thread_ctx = create_thread(65536, ext2_read_tables, (THREAD_ARGUMENT_TYPE)sc);
		if (!ext2_read_tables_thread_ctx) {
			NOTIFY("clone for ext2_read_tables thread failed (so we have to read the tables in the main thread).");
#endif // #ifdef THREADS
#endif // #ifdef ALLOW_CONCURRENT_TABLE_READER
			sc->background=false;
#ifdef ALLOW_CONCURRENT_TABLE_READER
#ifdef THREADS
		} else {
			++live_child;
		}
	}
#endif // #ifdef THREADS
#endif // #ifdef ALLOW_CONCURRENT_TABLE_READER
	
#ifdef THREADS
	struct progress_bar * live_child_progress_bar=progress_bar_mk(2,hard_child_limit);
	assert(live_child_progress_bar);
#endif // #ifdef THREADS
	
	{
		u32 size_of_gdt_Clusters=(sc->size_of_gdt_Blocks+sc->cluster_size_Blocks-1)/sc->cluster_size_Blocks;
		unsigned long group=0;
		unsigned long inode_num=1;
		
		/*
		unsigned num_clusters_per_group_for_inode_table = sc->sb->inodes_per_group / (sc->cluster_size_Bytes / sizeof(struct inode));
		*/
		
		OPEN_PROGRESS_BAR_LOOP(1,inode_num,sc->sb->num_inodes)
			
#ifdef THREADS
			if (sc->threads) {
				print_stalled_progress_bar(live_child_progress_bar,live_child);
			}
			
#ifdef ALLOW_CONCURRENT_TABLE_READER
			if (sc->background) {
				while (sc->table_reader_group==group) {
					PROGRESS_STALLED(inode_num);
					try_clone();
					print_stalled_progress_bar(live_child_progress_bar,live_child);
					TRE_WAIT();
				}
			} else {
#endif // #ifdef ALLOW_CONCURRENT_TABLE_READER
#endif // #ifdef THREADS
				read_table_for_one_group(sc, false);
#ifdef THREADS
#ifdef ALLOW_CONCURRENT_TABLE_READER
			}
#endif // #ifdef ALLOW_CONCURRENT_TABLE_READER
#endif // #ifdef THREADS
			
			assert(sc->table_reader_group>group);
			
			// eprintf("Parent's current group: %u\r",group);
			
			if (group_has_super_block(sc->sb,group)) {
				block_t sb_offset=2+group*sc->group_size_Blocks;
				u64 cluster=sb_offset/sc->cluster_size_Blocks;
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
					for (u64 c=1;c<cluster;++c) {
						if (PRINT_MARK_SB0GAP) {
							eprintf(
								"Marking cluster %llu in use (gap before super block in group 0)\n"
								, (unsigned long long)c
							);
						}
						MARK_CLUSTER_IN_USE_BY(sc, c, -4, 0); // Same ID as the super block of group 0 itself
					}
				}
				
				if (cluster || !sc->cluster_offset) {
					// Mark cluster containing super block as in-use
					if (PRINT_MARK_SB) {
						eprintf(
							"Marking cluster %llu in use (super block of group %lu)\n"
							, (unsigned long long)cluster
							, group
						);
					}
					MARK_CLUSTER_IN_USE_BY(sc, cluster, -4UL - (5UL * group), 0);
				}
				
				++cluster;
				if (PRINT_MARK_GDT) {
					eprintf(
						"Marking clusters %llu .. %llu in use (group descriptor table in group %lu)\n"
						, (unsigned long long)cluster
						, (unsigned long long)(cluster + size_of_gdt_Clusters - 1)
						, group
					);
				}
				// Mark all clusters containing the group descriptor table as in-use
				MARK_CLUSTERS_IN_USE_BY(sc, cluster, size_of_gdt_Clusters, -5UL - (5UL * group), 0);
			}
			
#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
			if (sc->threads) {
				struct thread * walk=thread_head->next;
				while (walk->next) {
					if (terminated_thread(walk->thread_ctx)) {
						cleanup_thread(walk->thread_ctx);
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
#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
			
			if (sc->gdt[group].flags & BG_INODE_TABLE_INITIALIZED
			||  !(sc->sb->ro_compat & (RO_COMPAT_METADATA_CSUM | RO_COMPAT_GDT_CSUM))) {
				bool have_free = false;
				for (unsigned ino=0;ino<sc->sb->inodes_per_group;++ino,++inode_num) {
					PRINT_PROGRESS_BAR(inode_num);
					
					bool is_free = check_inode(sc, inode_num, false, have_free);
					if (have_free) {
						if (!is_free) {
							if (sc->warn_expect_zero_inodes) {
								ERRORF(
									"Inode %lu is in-use but one or more former inode(s) from this group is/are never-used"
									, inode_num
								);
							}
							have_free = false;
						}
					} else {
						if (is_free && inode_num >= sc->sb->first_inode) {
							have_free = true;
						}
					}
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
	
#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
	if (sc->threads) {
		eprintf("Waiting for unfinished background jobs to finish ...\n");
		
		sc->waiting4threads=true;
		
		/*
		while (try_clone()) {
			print_stalled_progress_bar(live_child_progress_bar, live_child);
			sleep(1);
		}
		*/
		
		while (thread_head->next!=thread_tail) {
			struct thread * walk=thread_head->next;
			while (walk->next) {
				if (terminated_thread(walk->thread_ctx)) {
					cleanup_thread(walk->thread_ctx);
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
	}
#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
	
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
	
	unsigned long inode_num;
	
	eprintf("Pass 4a: Determining parent directories (forward) ...\n");
	inode_num=0;
	OPEN_PROGRESS_BAR_LOOP(0,inode_num,sc->sb->num_inodes)
		PRINT_PROGRESS_BAR(inode_num);
		++inode_num;
		
		vasiar_struct_inode_owning_map_entry_vasiar * ome=sc->inode_owning_map+inode_num-1;
		sc->inode_parent[inode_num-1]=NULL;
		for (size_t i=0;i<VASIZE(*ome);++i) {
			if (VAACCESS(*ome,i)->virtual
			||  !strcmp(VAACCESS(*ome,i)->name,".")
			||  !strcmp(VAACCESS(*ome,i)->name,"..")) {
				continue;
			}
			if (sc->inode_parent[inode_num-1]) {
				// add_problem_duplicated_forward_entry will be done in pass 4c
			}
			sc->inode_parent[inode_num-1]=VAACCESS(*ome,i);
		}
	CLOSE_PROGRESS_BAR_LOOP()
	
	eprintf("Pass 4b: Determining parent directories (backward) ...\n");
	inode_num=0;
	OPEN_PROGRESS_BAR_LOOP(0,inode_num,sc->sb->num_inodes)
		PRINT_PROGRESS_BAR(inode_num);
		++inode_num;
		
		vasiar_struct_inode_owning_map_entry_vasiar * ome=sc->inode_owning_map+inode_num-1;
		for (size_t i=0;i<VASIZE(*ome);++i) {
			if (VAACCESS(*ome,i)->virtual
			||  strcmp(VAACCESS(*ome,i)->name,"..")) {
				continue;
			}
			unsigned long parent_inode_num=VAACCESS(*ome,i)->parent;
			if (!sc->inode_parent[parent_inode_num-1]) {
				vasiar_struct_inode_owning_map_entry_vasiar * parent_ome;
				parent_ome=sc->inode_owning_map+parent_inode_num-1;
				
				char * tmp=mprintf("#%lu",parent_inode_num);
				struct inode_owning_map_entry * iome=mk_iome(inode_num,tmp,true);
				free(tmp);
				sc->inode_parent[parent_inode_num-1]=VANEW(*parent_ome)=iome;
			}
		}
	CLOSE_PROGRESS_BAR_LOOP()
	
	eprintf("Pass 4c: Checking directory connectivity ...\n");
	inode_num=0;
	OPEN_PROGRESS_BAR_LOOP(0,inode_num,sc->sb->num_inodes)
		PRINT_PROGRESS_BAR(inode_num);
		++inode_num;
		struct inode_scan_context isc_data;
		struct inode_scan_context * isc=&isc_data;
		isc->sc=sc;
		isc->inode_num=inode_num;
		typecheck_inode(isc,NULL);
		
		sc->loop_protect[inode_num-1]=0;
		
		struct problem_context * pc=get_problem_context(sc,inode_num);
		
		if (pc && pc->problem_mask&PM_IT_EIO) {
			sc->inode_link_count[inode_num-1]=VASIZE(sc->inode_owning_map[inode_num-1]);
		} else {
			if (sc->inode_link_count[inode_num-1]!=VASIZE(sc->inode_owning_map[inode_num-1])) {
				add_problem_wrong_link_count(isc,VASIZE(sc->inode_owning_map[inode_num-1]));
			}
		}
		
		vasiar_struct_inode_owning_map_entry_vasiar * ome=sc->inode_owning_map+inode_num-1;
		
		if (isc->is_dir) {
			bool has_dot_entry=false;
			bool has_multiple_dot=false;
			
			bool has_multiple_parent=false;
			bool has_parent_entry=false;
			
			size_t parent_i=0; // warning: 'parent' may be used uninitialized in this function
			
			for (size_t i=0;i<VASIZE(*ome);++i) {
				if (VAACCESS(*ome,i)->virtual) {
					continue;
				}
				if (!strcmp(VAACCESS(*ome,i)->name,".")) {
					if (VAACCESS(*ome,i)->parent!=inode_num) {
						struct inode_scan_context parent_isc_data;
						struct inode_scan_context * parent_isc=&parent_isc_data;
						parent_isc->sc=sc;
						parent_isc->inode_num=VAACCESS(*ome,i)->parent;
						typecheck_inode(parent_isc,NULL);
						add_problem_invalid_dot_entry(isc,inode_num);
					} else {
						if (has_dot_entry && !has_multiple_dot) {
							PRINT_PROGRESS_BAR(inode_num);
							goto report_multiple_dot_entries;
						}
						report_multiple_dot_entries_back: ;
						if (has_dot_entry) {
							report_multiple_dot_entries: ;
							add_problem_duplicated_dot_entry(isc);
							if (!has_multiple_dot) {
								has_multiple_dot=true;
								PRINT_PROGRESS_BAR(inode_num);
								goto report_multiple_dot_entries_back;
							}
						} else {
							has_dot_entry=true;
						}
					}
					continue;
				}
				
				if (!strcmp(VAACCESS(*ome,i)->name,"..")) {
					vasiar_struct_inode_owning_map_entry_vasiar * parent_ome;
					bool ok=false;
					unsigned long parent_inode_num=VAACCESS(*ome,i)->parent;
					struct inode_scan_context parent_isc_data;
					struct inode_scan_context * parent_isc=&parent_isc_data;
					parent_isc->sc=sc;
					parent_isc->inode_num=parent_inode_num;
					typecheck_inode(parent_isc,NULL);
					
					struct problem_context * parent_pc=get_problem_context(sc,parent_inode_num);
					if (parent_pc && parent_pc->problem_mask&PM_IT_EIO && !parent_isc->is_dir) {
						add_problem_eio_inode_is_dir(parent_isc);
						parent_isc->is_dir=true;
					}
					if (!parent_isc->is_dir) {
						EXT2_ERROR(parent_isc,"Inode was expected to be a directory.");
					}
					assert((parent_pc && parent_pc->problem_mask&PM_IT_EIO) || parent_isc->is_dir);
					parent_ome=sc->inode_owning_map+parent_inode_num-1;
					for (size_t j=0;j<VASIZE(*parent_ome);++j) {
						if (strcmp(VAACCESS(*parent_ome,j)->name,".")
						&&  strcmp(VAACCESS(*parent_ome,j)->name,"..")) {
							if (VAACCESS(*parent_ome,j)->parent==inode_num) {
								if (!VAACCESS(*parent_ome,j)->virtual) {
									ok=true;
								}
								break;
							}
						}
					}
					if (!ok) {
						if (pc && pc->problem_mask&(PM_IT_EIO|PM_FILE_DIR_EIO)) {
							add_problem_parent_directory_is_missing_entry_to_dir(isc,parent_inode_num);
						} else {
							add_problem_orphan_double_dot_entry(parent_isc,inode_num);
						}
					}
					continue;
				}
				
				if (has_parent_entry && !has_multiple_parent) {
					goto double_report_parent;
				}
				double_report_parent_back:
				parent_i=i;
				if (has_parent_entry) {
					double_report_parent:
					add_problem_duplicated_forward_entry(isc,VAACCESS(*ome,parent_i));
					if (!has_multiple_parent) {
						has_multiple_parent=true;
						goto double_report_parent_back;
					}
				} else {
					has_parent_entry=true;
				}
			}
			if (!has_dot_entry) {
				add_problem_has_no_valid_dot_entry(isc);
			}
			if (!has_parent_entry) {
				add_problem_has_no_parent(isc);
			}
		} else { // if (isc->is_dir)
			for (size_t i=0;i<VASIZE(*ome);++i) {
				if (VAACCESS(*ome,i)->virtual) {
					continue;
				}
				if (!strcmp(VAACCESS(*ome,i)->name,".")) {
					struct inode_scan_context parent_isc_data;
					struct inode_scan_context * parent_isc=&parent_isc_data;
					parent_isc->sc=sc;
					parent_isc->inode_num=VAACCESS(*ome,i)->parent;
					typecheck_inode(parent_isc,NULL);
					add_problem_invalid_dot_entry(isc,inode_num);
					continue;
				}
				if (!strcmp(VAACCESS(*ome,i)->name,"..")) {
					add_problem_double_dot_entry_to_non_dir(isc,VAACCESS(*ome,i)->parent);
					continue;
				}
			}
		} // if (isc->is_dir), else
	CLOSE_PROGRESS_BAR_LOOP()
	
	eprintf("Pass 4d: Performing orphan and loop analysis (for directories) ...\n");
	inode_num=2;
	sc->loop_protect[inode_num-1]=inode_num;
	inode_num=0;
	OPEN_PROGRESS_BAR_LOOP(0,inode_num,sc->sb->num_inodes)
		PRINT_PROGRESS_BAR(inode_num);
		++inode_num;
		struct inode_scan_context isc_data;
		struct inode_scan_context * isc=&isc_data;
		isc->sc=sc;
		isc->inode_num=inode_num;
		typecheck_inode(isc,NULL);
		
		if (!isc->is_dir) {
			continue;
		}
		
		u32 scan=inode_num;
		u32 group=0;
		bool is_loop=false;
		while (scan!=2) {
			if (!sc->inode_parent[scan-1]) {
				// Orphan directory
				// Problem has already been added in the directory scan already
				group=scan;
				break;
			}
			if (sc->loop_protect[scan-1]==inode_num) {
				is_loop=true;
				group=scan;
				break;
			}
			if (sc->loop_protect[scan-1]) {
				group=sc->loop_protect[scan-1];
				break;
			}
			sc->loop_protect[scan-1]=inode_num;
			scan=sc->inode_parent[scan-1]->parent;
		}
		if (scan==2) {
			group=2;
		}
		assert(group);
		scan=inode_num;
		if (group!=inode_num) {
			while (sc->loop_protect[scan-1]==inode_num) {
				sc->loop_protect[scan-1]=group;
				scan=sc->inode_parent[scan-1]->parent;
			}
			assert(!sc->loop_protect[scan-1] || sc->loop_protect[scan-1]==group);
			sc->loop_protect[scan-1]=group;
		}
		assert(sc->loop_protect[scan-1]==group || !sc->loop_protect[scan-1]);
		sc->loop_protect[scan-1]=group;
		if (is_loop) {
			scan=group;
			do {
				struct inode_scan_context scan_isc_data;
				struct inode_scan_context * scan_isc=&scan_isc_data;
				scan_isc->sc=sc;
				scan_isc->inode_num=inode_num;
				typecheck_inode(scan_isc,NULL);
				
				add_problem_directory_loop(scan_isc,group);
				scan=sc->inode_parent[scan-1]->parent;
			} while (scan!=group);
		}
	CLOSE_PROGRESS_BAR_LOOP()
	
	eprintf("Pass 4e: Performing orphan and loop analysis (for non-directories) ...\n");
	inode_num=0;
	OPEN_PROGRESS_BAR_LOOP(0,inode_num,sc->sb->num_inodes)
		PRINT_PROGRESS_BAR(inode_num);
		++inode_num;
		struct inode_scan_context isc_data;
		struct inode_scan_context * isc=&isc_data;
		isc->sc=sc;
		isc->inode_num=inode_num;
		typecheck_inode(isc,NULL);
		
		if (isc->is_dir) {
			continue;
		}
		
		sc->loop_protect[inode_num-1]=inode_num;
		vasiar_struct_inode_owning_map_entry_vasiar * ome=sc->inode_owning_map+inode_num-1;
		sc->inode_parent[inode_num-1]=NULL;
		for (size_t i=0;i<VASIZE(*ome);++i) {
			if (VAACCESS(*ome,i)->virtual
			||  !strcmp(VAACCESS(*ome,i)->name,".")
			||  !strcmp(VAACCESS(*ome,i)->name,"..")) {
				continue;
			}
			unsigned long parent_inode_num=VAACCESS(*ome,i)->parent;
			unsigned long group_inode_num=sc->loop_protect[parent_inode_num-1];
			sc->loop_protect[inode_num-1]=group_inode_num;
			sc->inode_parent[inode_num-1]=VAACCESS(*ome,i);
			if (group_inode_num==2) {
				break;
			}
		}
	CLOSE_PROGRESS_BAR_LOOP()
	
	/*
	 * FIXME: Read all inodes with problem contexts, which have not been 
	 * saved in the problem_context already.
	 */
	
	eprintf("Pass 5: Reporting errors ...\n");
	/*
	 * All errors are known, report them now.
	 */
	sc->inode_parent[1]=NULL;
	inode_num=0;
	OPEN_PROGRESS_BAR_LOOP(0,inode_num,sc->sb->num_inodes)
		PRINT_PROGRESS_BAR(inode_num);
		++inode_num;
		// ERRORF("%s: Inode %lu [%s]: No directory points to this directory.",sc->name,inode_num,isc->type);
		// goto ask_for_connect_to_lost_and_found;
		// EXT2_ERRORF(isc,"Directory has more than one '.' directory entry. (This one is stored in inode %lu)",dot_parent);
		// EXT2_ERRORF(isc,"Directory has more than one parent directries. (This one is '%s' in inode %lu).",VAACCESS(*ome,parent_i)->name,VAACCESS(*ome,parent_i)->parent);
		
		struct inode_scan_context isc_data;
		struct inode_scan_context * isc=&isc_data;
		isc->sc=sc;
		isc->inode_num=inode_num;
		typecheck_inode(isc,NULL);
		
		vasiar_struct_inode_owning_map_entry_vasiar * ome=sc->inode_owning_map+inode_num-1;
		
		char * filename=NULL;
		if (!VASIZE(*ome)) {
			filename=mprintf("@UNLINKED@/#%lu",inode_num);
			if (sc->inode_link_count[inode_num-1]) {
				EXT2_ERRORF_DIR(
					isc,filename,"%s","No directory entry points here."
				);
			}
//				ask_for_connect_to_lost_and_found:
//				ASK("I,N=Ignore this error, C,R,Y=connect to lost+found (Recover), K=Kill inode","(IN)(CRY)K");
		} else {
			// FIXME: Determine parent directory
			// is_dir{parent_i=...} else {parent_i=0;}
			unsigned loops=0;
			char * root;
			unsigned long root_inode_num=sc->loop_protect[inode_num-1];
			if (root_inode_num==2) {
				root=mstrcpy("@ROOT@");
			} else {
				unsigned long scan=root_inode_num;
				if (!sc->inode_parent[scan-1]) {
					root=mprintf("@ORPHAN@(%lu)",root_inode_num);
				} else {
					do {
						++loops;
						scan=sc->inode_parent[scan-1]->parent;
					} while (scan!=root_inode_num);
					root=mprintf("@LOOP@(%u)",loops);
				}
			}
			
			if (sc->inode_parent[inode_num-1]) {
				filename=mstrcpy(sc->inode_parent[inode_num-1]->name);
				unsigned long parent=sc->inode_parent[inode_num-1]->parent;
				struct inode_scan_context parent_isc_data;
				struct inode_scan_context * parent_isc=&parent_isc_data;
				/*
				vasiar_struct_inode_owning_map_entry_vasiar * parent_ome;
				*/
				struct problem_context * parent_pc;
				
				while (parent && sc->inode_parent[parent-1]) {
					parent_isc->sc=sc;
					parent_isc->inode_num=parent;
					typecheck_inode(parent_isc,NULL);
					/*
					parent_ome=sc->inode_owning_map+parent-1;
					*/
					parent_pc=get_problem_context(sc,parent);
					
					if (parent_pc && parent_pc->problem_mask&PM_DIRECTORY_LOOP) {
						if (!loops--) {
							break;
						}
					}
					char * tmp=filename;
					filename=mprintf("%s/%s",sc->inode_parent[parent-1]->name,tmp);
					free(tmp);
					parent=sc->inode_parent[parent-1]->parent;
				}
			} else {
				filename=mprintf("#%lu",inode_num);
			}
			char * tmp=filename;
			filename=mprintf("%s/%s",root,tmp);
			free(tmp);
			free(root);
		} // if (!VASIZE(*ome)), else
		
		struct problem_context * pc=get_problem_context(sc,inode_num);
		/*
		if ((!pc
		||   pc->problem_mask==(pc->problem_mask&(PM_INODE_WRONG_NUM_BLOCKS|PM_WRONG_LINK_COUNT)))
		&&  isc->type_bit==FTB_FILE) {
		*/
		if (sc->dump_files && isc->type_bit==FTB_FILE) {
			char * tmp;
			if (ext2_dump_file_by_inode(sc,inode_num,"rescue.tmp",filename)) {
				tmp=mprintf("rescued/ok/%s",filename);
			} else {
				tmp=mprintf("rescued/errors/%s",filename);
			}
			int i=0;
			size_t sl=strlen(tmp);
			size_t j=sl;
			for (;;) {
				while (j-- && tmp[j]!='/') ;
				assert(++j);
				tmp[--j]=0;
				++i;
				if (!mkdir(tmp,0666) || errno==EEXIST) {
					break;
				}
			}
			while (i--) {
				tmp[j]='/';
				while (tmp[++j]) ;
				if (j!=sl) {
					mkdir(tmp,0666);
				}
			}
			rename("rescue.tmp",tmp);
			free(tmp);
		}
		if (pc) {
			char * msg=mprintf(
				"%s: Inode %lu (cluster %lu) [%s] (%s)"
				,sc->name
				,inode_num
				,get_inodes_cluster(sc,inode_num)
				,isc->type
				,filename
			);
			free(filename);
				/*
				 * in-use, never-used
						struct problem_context * pc=get_problem_context(sc,inode_num);
						if (!pc
						||  pc->problem_mask!=(PM_IT_EIO|PM_FREE)) {
						}
				*/
			if (pc->problem_mask&PM_IT_EIO) {
				u32 group=(inode_num-1)/sc->sb->inodes_per_group;
				u32 relative_inode_num=(inode_num-1)%sc->sb->inodes_per_group;
				u32 relative_byte_offset=relative_inode_num*sizeof(*sc->inode_table);
				u64 group_relative_block_offset=sc->gdt[group].inode_table*sc->cluster_size_Blocks;
				u32 relative_block_offset=relative_byte_offset/sc->block_size_Bytes;
				ERRORF(
					"%s: Couldn't read this inode%s: Disk I/O error. Missing blocks are: %llu."
					,msg
					,pc->problem_mask==(PM_IT_EIO|PM_FREE)?", which was never-used anyways":""
					, (unsigned long long)(group_relative_block_offset+relative_block_offset)
				);
				if (pc->problem_mask&PM_FREE
				&&  pc->problem_mask!=(PM_IT_EIO|PM_FREE)) {
					ERRORF(
						 "%s: Inode was expected to be never-used, but something points here"
						 ,msg
					);
				}
			} else { // if (pc->problem_mask&PM_IT_EIO)
				if (pc->problem_mask&PM_DIRECTORY_LOOP) {
					ERRORF(
						"%s: This inode is part of a directory loop. Loop group: %lu."
						,msg
						,pc->loop_group
					);
				}
				if (pc->problem_mask&PM_DIRECTORY_HAS_INVALID_DOT_ENTRY) {
					char * list;
					LIST_VASIAR_UNSIGNED_LONG(pc->invalid_dot_entry_target,list);
					ERRORF(
						"%s: This directory contains %lu invalid '.' entr%s, pointing to inodes%s."
						,msg
						,VASIZE(pc->invalid_dot_entry_target)
						,(VASIZE(pc->invalid_dot_entry_target)==1)?"y":"ies"
						,list
					);
					free(list);
				}
				if (pc->problem_mask&PM_DIR_NO_VALID_DOT_DIR) {
					ERRORF(
						"%s: This directory is missing it's '.' entry."
						,msg
					);
				}
				if (pc->problem_mask&PM_DIR_NO_PARENT_DIR) {
					ERRORF(
						"%s: This directory is missing it's '..' entry."
						,msg
					);
				}
				if (pc->problem_mask&PM_DIRECTORY_HAS_MULTIPLE_VALID_DOT_ENTRIES) {
					ERRORF(
						"%s: This directory has more than one valid '.' entry."
						,msg
					);
				}
				if (pc->problem_mask&PM_DIRECTORY_PARENT_DOESNT_POINT_TO_US) {
					ERRORF(
						"%s: This directories '..' entry points to inode %lu which in turn is missing any entry to this directory."
						,msg
						,pc->missing_entry_to_us_parent
					);
				}
				if (pc->problem_mask&PM_DIR_DOUBLE_DOT_ENTRY_TO_NON_DIR) {
					char * list;
					LIST_VASIAR_UNSIGNED_LONG(pc->bad_double_entry_dir,list);
					ERRORF(
						"%s: %lu director%s pointing to this inode. The offending director%s%s."
						,msg
						,VASIZE(pc->bad_double_entry_dir)
						,(VASIZE(pc->bad_double_entry_dir)==1)?"y has a '..' entry":"ies have '..' enties"
						,(VASIZE(pc->bad_double_entry_dir)==1)?"y is:":"ies are:"
						,list
					);
					free(list);
				}
				if (pc->problem_mask&PM_DIRECTORY_HAS_MULTIPLE_PARENTS) {
					char * list=mstrcpy("");
					for (size_t i=0;i<VASIZE(pc->parents);++i) {
						char * tmp=mprintf(
							"%s (%s<-%lu)"
							,list
							,VAACCESS(pc->parents,i)->name
							,(unsigned long)VAACCESS(pc->parents,i)->parent
						);
						free(list);
						list=tmp;
					}
					ERRORF(
						"%s: This directory is a sub-directory of more than one directory. The offending directory entries are:%s."
						,msg
						,list
					);
					free(list);
				}
				if (pc->problem_mask&PM_IND_EIO
				||  pc->problem_mask&PM_IND_WDN) {
					/*
					const char * what_ind[4] = {
						"ERROR!",
						"single",
						"double",
						"trible",
					};
					*/
					
					char * list;
					LIST_VASIAR_IND_BLOCK_T(pc->ind_eio_wdn,list);
					// TODO: Respect difference between eio and wdn
					ERRORF(
						"%s: Couldn't read all indirect / extent-directory and -index clusters: Disk I/O error. Missing blocks are:%s"
						,msg
						,list
					);
					free(list);
				} else {
					if (pc->problem_mask&PM_INODE_WRONG_NUM_BLOCKS) {
						ERRORF(
							"%s: num_blocks is %lu, counted %lu"
							,msg
							, (unsigned long)pc->inode->num_blocks
							,pc->num_blocks
						);
					}
				}
				if (pc->problem_mask&PM_ZERO_DTIME) {
					if (sc->warn_dtime_zero) {
						NOTIFYF(
							"%s: Deleted inode has zero dtime."
							,msg
						);
					}
					// inode->dtime = sc->sb->wtime;
				}
				if (pc->problem_mask&PM_WRONG_LINK_COUNT
				&&  VASIZE(*ome)) {
					/*
					 * This error will not be shown, if there 
					 * is no directory entry pointing to this 
					 * object at all.
					 */
					ERRORF(
						"%s: link_count is %u, counted %u."
						, msg
						, (unsigned)sc->inode_link_count[inode_num-1]
						, pc->link_count
					);
				}
				if (pc->problem_mask&PM_FILE_DIR_EIO
				||  pc->problem_mask&PM_FILE_DIR_WDN) {
					char * list;
					LIST_VASIAR_IND_BLOCK_T(pc->file_dir_eio_wdn, list);
					// TODO: Respect difference between eio and wdn
					// ERRORF("%s: Inode %lu [%s]: While reading directory cluster %lu: Received wrong data notification.", isc->sc->name, isc->inode_num, isc->type, cluster);
					ERRORF(
						"%s: Couldn't read all %s clusters: Disk I/O error. Missing blocks are:%s"
						,msg
						,isc->is_dir?"directory":"file"
						,list
					);
					free(list);
				}
			} // if (pc->problem_mask&PM_IT_EIO), else
			free(msg);
		} // if (!pc), else
	CLOSE_PROGRESS_BAR_LOOP()
	
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
#ifdef THREADS
	if (sc->threads) {
		shutdown_com_cache_thread(sc);
	}
#endif // #ifdef THREADS
	
	/*
	for (u32 cluster=0;cluster<sc->sb->num_clusters;++cluster) {
		if (get_owner_inode(sc,cluster)) {
			set_calculated_cluster_allocation_map_bit(sc,cluster);
		}
	}
	*/
	eprintf("Finisched building calculated_cluster_allocation_map from cluster_owning_map\n");
	
	if (memcmp(sc->cluster_allocation_map, sc->calculated_cluster_allocation_map, (size_t)sc->cluster_size_Bytes * (size_t)sc->num_groups)) {
		eprintf("%s: cluster allocation map differs%c", sc->name, sc->show_cam_diffs ? ':' : '!');
		if (sc->show_cam_diffs) {
			bool valid_problem_range = false;
			bool really_allocated = true;
			u32 first_cluster = 0;
			for (u32 cluster = 1; cluster < sc->sb->num_clusters; ++cluster) {
				bool on_disk_flag = get_cluster_allocation_map_bit(sc, cluster);
				bool calculated_flag = get_calculated_cluster_allocation_map_bit(sc, cluster);
				bool new_range = false;
				bool print = false;
				
				if (likely(on_disk_flag == calculated_flag)) {
					if (unlikely(valid_problem_range)) {
						print = true;
					}
				} else {
					if (likely(!valid_problem_range) || unlikely(really_allocated != calculated_flag)) {
						new_range = true;
						if (likely(valid_problem_range)) {
							print = true;
						}
					}
				}
				if (unlikely(print)) {
					valid_problem_range = false;
					eprintf(" %c", really_allocated ? '+' : '-');
					if (unlikely(first_cluster == cluster-1)) {
						eprintf("%lu", (unsigned long)first_cluster);
					} else {
						eprintf("(%lu..%lu)", (unsigned long)first_cluster, (unsigned long)cluster - 1);
					}
				}
				if (new_range) {
					valid_problem_range = true;
					really_allocated = calculated_flag;
					first_cluster = cluster;
				}
			}
		}
		eprintf("\n");
	}
	
	{
		ssize_t w;
		
		OPEN("/ramfs/cluster_allocation_map.calculated",O_WRONLY|O_CREAT|O_TRUNC,0666);
		w=write(fd,sc->calculated_cluster_allocation_map,cam_size_Bytes);
		ignore(w);
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
		ignore(w);
		close(fd);
		
		OPEN("/ramfs/inode_allocation_map.read",O_WRONLY|O_CREAT|O_TRUNC,0666);
		w=write(fd,sc->inode_allocation_map,iam_size_Bytes);
		ignore(w);
		close(fd);
		
		OPEN("/ramfs/inode_allocation_map.calculated",O_WRONLY|O_CREAT|O_TRUNC,0666);
		w=write(fd,sc->calculated_inode_allocation_map,iam_size_Bytes);
		ignore(w);
		close(fd);
	}
	
	retval=true;
	eprintf("Pass 6: Cleaning up ...\n");

cleanup:
#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
	if (thread_head) {
		btlock_lock_free(thread_head->lock);
		free(thread_head);
	}
	if (thread_tail) {
		btlock_lock_free(thread_tail->lock);
		free(thread_tail);
	}
#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
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
		TRE_WAKE(); TRE_WAIT(); // Reset state to default
		if (sc->table_reader_empty) btlock_lock_free(sc->table_reader_empty);
		free(sc->inode_link_count);
		free(sc->inode_type_str);
		free(sc->gdt);
		free(sc->gdtv1);
		free(sc->gdtv2);
//		free(sc->inode_table);
		munmap(sc->inode_table,inode_table_len);
		close(inode_table_fd);
		free(sc->cam_lock);
		free(sc->progress_bar);
		free(sc->sb);
		free(sc->dir);
//		free(sc->list_head);
//		assert(sc->list_tail==sc->list_head);
		free(sc->calculated_inode_allocation_map);
		free(sc->cluster_allocation_map);
		free(sc->inode_allocation_map);
		free(sc->inode_parent);
		free(sc->loop_protect);
		free(sc->inode_type);
		if (sc->inode_owning_map || sc->inode_problem_context) {
			inode_num=0;
			OPEN_PROGRESS_BAR_LOOP(0,inode_num,sc->sb->num_inodes)
				PRINT_PROGRESS_BAR(inode_num);
				++inode_num;
				if (sc->inode_owning_map) {
					vasiar_struct_inode_owning_map_entry_vasiar * ome=sc->inode_owning_map+inode_num-1;
					if (ome) {
						for (size_t i=0;i<VASIZE(*ome);++i) {
							free(VAACCESS(*ome,i));
						}
						VAFREE(*ome);
					}
				}
				if (sc->inode_problem_context) {
					struct problem_context * pc=get_problem_context(sc,inode_num);
					if (pc) {
						pc_free(pc);
					}
				}
			CLOSE_PROGRESS_BAR_LOOP()
			free(sc->inode_owning_map);
			free(sc->inode_problem_context);
		}
		btlock_lock_free(sc->inode_owning_map_lock);
		com_cache_cleanup(sc);
		free(sc->sb);
		free(sc);
		free(sb2);
		free(gdt2v1);
		free(gdt2v2);
		free(gdt2im);
		if (com_cache_thread_ctx) cleanup_thread(com_cache_thread_ctx);
		if (ext2_read_tables_thread_ctx) cleanup_thread(ext2_read_tables_thread_ctx);
	}
	return retval;
}
