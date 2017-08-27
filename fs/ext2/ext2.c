/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#define DRIVER_NAME "ext2"

#include "ext2.h"

#include "conf.h"

#include "bitmap.h"
#include "com_cache.h"
#include "endian.h"
#include "fsck.h"
#include "pc.h"
#include "sc.h"
#include "threads.h"

#include "common.h"

#include <assert.h>
#include <btatomic.h>
#include <btendian.h>
#include <btlock.h>
#include <btstr.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <mprintf.h>
#include <stddbg.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>
#include <zlib.h>

#if THREADS
#include <btthread.h>
/*
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
*/
#endif // #if THREADS

#ifdef HAVE_VALGRIND
#include <valgrind/valgrind.h>
#endif // #ifdef HAVE_VALGRIND

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

void write_super_blocks(struct super_block * sb) {
	endian_swap_sb(sb);
	// FIXME: Implement this ...
	endian_swap_sb(sb);
}

bool read_super(struct scan_context * sc, struct super_block * sb, unsigned long group, block_t offset, bool probing) {
	char * tmp = mprintf("ext2 cluster %llu sb[%lu]", (unsigned long long)offset, (unsigned long)group);
	if (2 != bdev_read(sc->bdev, offset, 2, (void *)sb, tmp)) {
		free(tmp);
		ERRORF(
			"%s: Couldn't read blocks %llu and %llu while trying to read super block of group %lu."
			, sc->name
			, (unsigned long long)offset
			, (unsigned long long)(offset + 1)
			, (unsigned long)group
		);
		return false;
	} else {
		free(tmp);
		endian_swap_sb(sb);
		
		if (sb->magic==MAGIC) {
			u16 g = group > 65535 ? 65535 : group;
			if (sb->my_group != g) {
				ERRORF(
					"%s: Cluster group %lu contains a super block backup claiming to belong to cluster group %u (should be %u)."
					, sc->name
					, group
					, sb->my_group
					, g
				);
				return false;
			}
			if (probing) {
				NOTIFYF(
					"%s: Found ext2 super block in block %llu of device."
					, sc->name
					, (unsigned long long)offset
				);
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

bool read_table_for_one_group(struct scan_context * sc, bool prefetching) {
	u8 *    it_b;
	
#if ALLOW_CONCURRENT_TABLE_READER
	if (sc->allow_concurrent_table_reader) {
		unsigned counter = 0;
		
		while (atomic_get(&sc->prereaded) == MAX_INODE_TABLE_PREREADED) {
			if (++counter == PREREADED_NOTIFYTIME) {
				// NOTIFY("read_table_for_one_group: Going to sleep to let the foreground process catch up ...");
			}
			TRF_WAIT();
		}
		it_b = (u8*)(sc->inode_table + sc->sb->inodes_per_group * (sc->table_reader_group % INODE_TABLE_ROUNDROUBIN));
	} else {
#endif // #if ALLOW_CONCURRENT_TABLE_READER
		it_b = (u8*)sc->inode_table;
#if ALLOW_CONCURRENT_TABLE_READER
	}
#endif // #if ALLOW_CONCURRENT_TABLE_READER
	
	u8 *    cb_b=sc->cluster_allocation_map+sc->cluster_size_Bytes*sc->table_reader_group;
	u8 *    ib_b=sc->inode_allocation_map+sc->sb->inodes_per_group/8*sc->table_reader_group;
	
	size_t  cb_n=sc->cluster_size_Bytes;
	size_t  ib_n=sc->sb->inodes_per_group/8;
	size_t  it_n=(sc->sb->inodes_per_group - sc->gdt[sc->table_reader_group].num_virgin_inodes) * sizeof(*sc->inode_table);
	
	block_t cb_a=sc->cluster_size_Blocks*sc->gdt[sc->table_reader_group].cluster_allocation_map;
	block_t ib_a=sc->cluster_size_Blocks*sc->gdt[sc->table_reader_group].inode_allocation_map;
	block_t it_a=sc->cluster_size_Blocks*sc->gdt[sc->table_reader_group].inode_table;
	
	block_t cb_s=sc->cluster_size_Blocks;
	block_t ib_s=sc->cluster_size_Blocks;
	block_t it_s=sc->cluster_size_Blocks*((it_n+sc->cluster_size_Bytes-1)/sc->cluster_size_Bytes);
	
	char * tmp = mprintf(
		"ext2 cluster %llu blocks %llu..%llu cluster_allocation_map of group %lu"
		, (unsigned long long)sc->gdt[sc->table_reader_group].cluster_allocation_map
		, (unsigned long long)cb_a
		, (unsigned long long)(cb_a + sc->cluster_size_Blocks - 1)
		, sc->table_reader_group
	);
	if (sc->gdt[sc->table_reader_group].flags & BG_INODE_ALLOCATION_MAP_UNINIT || cb_s != bdev_read(sc->bdev, cb_a, cb_s, cb_b, tmp)) {
		/*
		 * This error is in no way critical as we don't trust that 
		 * data anyways, we do this reading only to CHECK wether the 
		 * data are correct and inform the user if it wasn't.
		 */
		if (! (sc->gdt[sc->table_reader_group].flags & BG_INODE_ALLOCATION_MAP_UNINIT)) {
			NOTIFYF(
				"%s: Error reading cluster allocation map of group %lu (ignored, will be recalculated anyways ...)"
				, sc->name
				, sc->table_reader_group
			);
		}
		// For VALGRIND:
		memset(cb_b,0,cb_n);
	}
	free(tmp);
	
	/*
	 * The in-memory copy of the inode allocation map only is big enough 
	 * for the actual needed bits. As reading can be done in chunks of 
	 * full block only, we may get problems in the last group. So we 
	 * mis-use the memory for the inode table here as target buffer for 
	 * the inode bitmap and then copy over the data to the right location.
	 */
	tmp = mprintf(
		"ext2 cluster %llu blocks %llu..%llu inode_allocation_map of group %lu"
		, (unsigned long long)sc->gdt[sc->table_reader_group].inode_allocation_map
		, (unsigned long long)ib_a
		, (unsigned long long)(ib_a + sc->cluster_size_Blocks - 1)
		, sc->table_reader_group
	);
	if (sc->gdt[sc->table_reader_group].flags & BG_CLUSTER_ALLOCATION_MAP_UNINIT || ib_s != bdev_read(sc->bdev, ib_a, ib_s, it_b, tmp)) {
		/*
		 * Again: Just ignore errors in reading of bitmap blocks.
		 */
		if (! (sc->gdt[sc->table_reader_group].flags & BG_CLUSTER_ALLOCATION_MAP_UNINIT)) {
			NOTIFYF(
				"%s: Error reading inode allocation map of group %lu (ignored, will be recalculated anyways ...)"
				, sc->name
				, sc->table_reader_group
			);
		}
		// For VALGRIND:
		memset(ib_b,0,ib_n);
	} else {
		memcpy(ib_b,it_b,ib_n);
	}
	free(tmp);
	
	if (sc->gdt[sc->table_reader_group].flags & BG_INODE_TABLE_INITIALIZED) {
		/*
		 * The following implementation solves the problem stated in the 
		 * FIXME above, however it's a very poor solution and should be 
		 * replaced by something more sane. However, at least it works ...
		 */
		u8 ignore_map[sc->block_size_Bytes];
		{
			memset(ignore_map, 0, sc->block_size_Bytes);
			struct inode * it=(struct inode *)ignore_map;
			int ni=sc->block_size_Bytes/sizeof(struct inode);
			for (int i=0;i<ni;++i) {
				#define FIELD_TEST_MASK(fieldname, fieldmask, fieldflag) do { it[i].fieldname = fieldmask; } while (0)
				#define FIELD_TEST(fieldname, fieldflag) do { it[i].fieldname = -1; } while (0)
				
				FIELD_TEST_MASK(mode, 07777, 'M');
				FIELD_TEST(uid, 'u');
				FIELD_TEST(atime, 'a');
				FIELD_TEST(ctime, 'c');
				FIELD_TEST(mtime, 'm');
				FIELD_TEST(dtime, 'd');
				FIELD_TEST(gid, 'g');
				FIELD_TEST(links_count, 'l');
				FIELD_TEST(num_blocks, 'b');
				FIELD_TEST(flags, 'f');
				FIELD_TEST(translator, 't');
				FIELD_TEST(generation, 'e');
				FIELD_TEST(mode_high, 'H');
				FIELD_TEST(uid_high, 'U');
				FIELD_TEST(gid_high, 'G');
				FIELD_TEST(author, 'A');
				FIELD_TEST(file_acl, 'L'); // If file system doesn't use ACLs
				FIELD_TEST(faddr, 'D'); // If file system doesn't use fragments
				FIELD_TEST(frag, 'R');
				FIELD_TEST(fsize, 'z');
				
				#undef FIELD_TEST_MASK
				#undef FIELD_TEST
			}
		}
		
		bool have_soft_errors=false;
		bool have_hard_errors=false;
		unsigned bs=sc->block_size;
		u8 error_bitmap[bs];
		bool expect_zeros = false;
		char * msg = NULL;
		assert(bs == 512); // This code currently only works for bs == 512.
		for (block_t i=0;i<it_s;++i) {
			tmp = mprintf(
				"ext2 cluster %llu block %llu inode_table of group %lu for %s %lu..%lu"
				, (unsigned long long)(sc->gdt[sc->table_reader_group].inode_table + it_a / sc->cluster_size_Bytes)
				, (unsigned long long)it_a
				, sc->table_reader_group
				, expect_zeros ? "zero inodes" : "potentially used inodes"
				, (unsigned long)0 // TODO: first inode in block
				, (unsigned long)0 // TODO: last inode in block
			);
			block_t r = bdev_disaster_read(sc->bdev, it_a, 1, it_b, error_bitmap, ignore_map, tmp);
			free(tmp);
			bool create_problem_context = false;
			if (1!=r) {
				have_hard_errors = true;
				memset(it_b, 0, bs);
				create_problem_context = true;
			} else {
				size_t j = expect_zeros ? 0 : 128 * 3;
				while (j < 512) {
					if (it_b[j]) {
						break;
					}
					++j;
				}
				if (expect_zeros) {
					if (j != 512) {
						if (sc->warn_expect_zero_inodes) {
							NOTIFYF(
								"group(%lu): expect_zeros is true, but read block (inodes %lu .. %lu) isn't composed of only zeros!\n"
								, (unsigned long)sc->table_reader_group
								, (unsigned long)i * 4 + 1
								, ((unsigned long)i + 1) * 4
							);
						}
					}
				} else {
					if ((sc->table_reader_group
					||   i >= ((block_t)sc->sb->first_inode - 1) / 4)
					&&  j==512) {
						expect_zeros=true;
					}
				}
				
				struct inode * it=(struct inode *)error_bitmap;
				int ni=bs/sizeof(struct inode);
				for (int j = 0; j < ni; ++j) {
					#define FIELD_TEST_MASK(fieldname, fieldmask, fieldflag) do { \
						if (it[j].fieldname & fieldmask) { \
							if (!msg) { msg = mstrcpy("Errors in fields: "); } \
							char * tmp = msg; msg = mprintf("%s%c", tmp, fieldflag); free(tmp); \
							it[j].fieldname &= ~fieldmask; \
						} \
					} while (0)
					#define FIELD_TEST(fieldname, fieldflag) do { \
						if (it[j].fieldname) { \
							if (!msg) { msg = mstrcpy("Errors in fields: "); } \
							char * tmp = msg; msg = mprintf("%s%c", tmp, fieldflag); free(tmp); \
							it[j].fieldname = 0; \
						} \
					} while (0)
					
					FIELD_TEST_MASK(mode, 07777, 'M');
					FIELD_TEST(uid, 'u');
					FIELD_TEST(atime, 'a');
					FIELD_TEST(ctime, 'c');
					FIELD_TEST(mtime, 'm');
					FIELD_TEST(dtime, 'd');
					FIELD_TEST(gid, 'g');
					FIELD_TEST(links_count, 'l');
					FIELD_TEST(num_blocks, 'b');
					FIELD_TEST(flags, 'f');
					FIELD_TEST(translator, 't');
					FIELD_TEST(generation, 'e');
					FIELD_TEST(mode_high, 'H');
					FIELD_TEST(uid_high, 'U');
					FIELD_TEST(gid_high, 'G');
					FIELD_TEST(author, 'A');
					FIELD_TEST(file_acl, 'L'); // If file system doesn't use ACLs
					FIELD_TEST(faddr, 'D'); // If file system doesn't use fragments
					FIELD_TEST(frag, 'R');
					FIELD_TEST(fsize, 'z');
				}
				
				for (unsigned j = 0; j < bs; ++j) {
					if (error_bitmap[j]) {
						have_soft_errors = true;
						create_problem_context = true;
						break;
					}
				}
			}
			
			if (create_problem_context) {
				unsigned long g = sc->table_reader_group;
				unsigned long ipg = sc->sb->inodes_per_group;
				unsigned long fig = 1 + g * ipg; // first inode in grouo
				unsigned long ipb = bs / sizeof(*sc->inode_table); // inodes per block
				unsigned long fiib = fig + i * ipb; // first inode in block
				unsigned long fiinb = fiib + ipb; // first inode in next block
				
				for (unsigned long inode = fiib; inode < fiinb; ++inode) {
					add_problem_reading_inode_table_eio(sc, inode);
				}
			}
			
			it_a++;
			it_b += bs;
		}
		if (msg) {
			free(msg);
			NOTIFYF(
				"%s: While reading inode table of group %lu: Received unproblematic notifications on wrong data."
				, sc->name
				, sc->table_reader_group
			);
		}
		if (have_soft_errors) {
			ERRORF(
				"%s: \033[31mWhile reading inode table of group %lu: Received problematic notifications on wrong data. :(\033[0m"
				, sc->name
				, sc->table_reader_group
			);
		}
		if (have_hard_errors) {
			ERRORF(
				"%s: \033[31mWhile reading inode table of group %lu: Couldn't read all blocks of inode table :(\033[0m"
				, sc->name
				, sc->table_reader_group
			);
		}
	}
	
	{
		struct inode * inode = sc->inode_table;
		
#if ALLOW_CONCURRENT_TABLE_READER
		if (sc->allow_concurrent_table_reader) {
			inode += sc->sb->inodes_per_group * (sc->table_reader_group % INODE_TABLE_ROUNDROUBIN);
		}
#endif // #if ALLOW_CONCURRENT_TABLE_READER
		
		for (unsigned i = 0; i < (sc->sb->inodes_per_group - sc->gdt[sc->table_reader_group].num_virgin_inodes); ++i) {
			endian_swap_inode(inode + i);
		}
	}
	
	atomic_inc(&sc->prereaded);
	
	if (!prefetching) {
		unsigned long long c;
		
		c = sc->gdt[sc->table_reader_group].cluster_allocation_map;
		if (PRINT_MARK_CAM) {
			eprintf(
				"Marking cluster %llu in use (cluster allocation bitmap in group %lu)\n"
				, c
				, sc->table_reader_group
			);
		}
		MARK_CLUSTER_IN_USE_BY(sc, c, -1UL - (5UL * sc->table_reader_group), 0);
		
		c = sc->gdt[sc->table_reader_group].inode_allocation_map;
		if (PRINT_MARK_IAM) {
			eprintf(
				"Marking cluster %llu in use (inode allocation bitmap in group %lu)\n"
				, c
				, sc->table_reader_group
			);
		}
		MARK_CLUSTER_IN_USE_BY(sc, c, -2UL - (5UL * sc->table_reader_group), 0);
		
		unsigned num_clusters_per_group_for_inode_table =
			sc->sb->inodes_per_group / (sc->cluster_size_Bytes / sizeof(struct inode))
		;
		
		if (PRINT_MARK_IT) {
			eprintf(
				"Marking clusters %llu .. %llu in use (inode table in group %lu)\n"
				, (unsigned long long) sc->gdt[sc->table_reader_group].inode_table
				, (unsigned long long)(sc->gdt[sc->table_reader_group].inode_table + num_clusters_per_group_for_inode_table)
				, sc->table_reader_group
			);
		}
		MARK_CLUSTERS_IN_USE_BY(sc
			, sc->gdt[sc->table_reader_group].inode_table
			, num_clusters_per_group_for_inode_table
			, -3UL - (5UL * sc->table_reader_group)
			, 0
		);
		
		++sc->table_reader_group;
	}
	
	return true;
}

static inline bool read_cluster_block_isc(struct inode_scan_context * isc
	, block_t absolute_block
	, block_t relative_block
	, u8 * buffer
	, u8 * errmap
	, struct problem_context * (*eio_function)(struct inode_scan_context * isc, block_t absolute_block, block_t relative_block)
	, struct problem_context * (*wdn_function)(struct inode_scan_context * isc, block_t absolute_block, block_t relative_block)
	, const char * reason
) {
	block_t r = bdev_short_read(isc->sc->bdev, absolute_block, 1, buffer, errmap, reason);
	if (likely(r)) {
		for (unsigned i = 0; i < isc->sc->block_size; ++i) {
			if (unlikely(errmap[i])) {
				wdn_function(isc, absolute_block, relative_block);
				break;
			}
		}
		return true;
	} else {
		eio_function(isc, absolute_block, relative_block);
		isc->has_io_errors = true;
		++isc->read_error_ind_blocks[0];
		return false;
	}
}

static inline void read_cluster_isc(struct inode_scan_context * isc
	, block_t absolute_cluster
	, block_t relative_cluster
	, u8 * buffer
	, u8 * error_map
	, bool * valid
	, struct problem_context * (*eio_function)(struct inode_scan_context * isc, block_t absolute_block, block_t relative_block)
	, struct problem_context * (*wdn_function)(struct inode_scan_context * isc, block_t absolute_block, block_t relative_block)
	, const char * reason_fmt
) {
	block_t absolute_offset = absolute_cluster * isc->sc->cluster_size_Blocks;
	block_t relative_offset = relative_cluster * isc->sc->cluster_size_Blocks;
	block_t num = isc->sc->cluster_size_Blocks;
	size_t relative = 0;
	u8 * buffer_ptr = buffer;
	u8 * errorm_ptr = error_map;
	bool errors = false;
	while (num) {
		char * tmp = mprintf(reason_fmt, absolute_offset);
		block_t r = bdev_short_read(isc->sc->bdev, absolute_offset, 1, buffer_ptr, errorm_ptr, tmp);
		free(tmp);
		if (likely(r)) {
			valid[relative] = true;
			for (unsigned i = 0; i < isc->sc->block_size; ++i) {
				if (unlikely(errorm_ptr[i])) {
					wdn_function(isc, absolute_offset, relative_offset);
					break;
				}
			}
		} else {
			valid[relative] = false;
			eio_function(isc, absolute_offset, relative_offset);
			errors = true;
		}
		++relative;
		++absolute_offset;
		++relative_offset;
		--num;
		buffer_ptr += isc->sc->block_size_Bytes;
		errorm_ptr += isc->sc->block_size_Bytes;
	}
	if (errors) {
		isc->has_io_errors = true;
		++isc->read_error_ind_blocks[0];
	}
}

static inline void read_ind_cluster_isc(struct inode_scan_context * isc
	, block_t absolute_cluster
	, block_t relative_cluster
	, u8 * buffer
	, u8 * error_map
	, bool * valid
	, struct problem_context * (*eio_function)(struct inode_scan_context * isc, unsigned level, block_t absolute_block, block_t frst_relative_block, block_t last_relative_block)
	, struct problem_context * (*wdn_function)(struct inode_scan_context * isc, unsigned level, block_t absolute_block, block_t frst_relative_block, block_t last_relative_block)
	, const char * reason_fmt
) {
	block_t absolute_offset = absolute_cluster * isc->sc->cluster_size_Blocks;
	unsigned level = 0; // TODO: Argument
	block_t relative_offset_increment = 1; // TODO: Argument
	block_t frst_relative_block = relative_cluster * isc->sc->cluster_size_Blocks; // TODO: Argument
	block_t num = isc->sc->cluster_size_Blocks;
	size_t relative = 0;
	u8 * buffer_ptr = buffer;
	u8 * errorm_ptr = error_map;
	bool errors = false;
	while (num) {
		char * tmp = mprintf(reason_fmt, absolute_offset);
		block_t r = bdev_short_read(isc->sc->bdev, absolute_offset, 1, buffer_ptr, errorm_ptr, tmp);
		free(tmp);
		if (likely(r)) {
			valid[relative] = true;
			for (unsigned i = 0; i < isc->sc->block_size; ++i) {
				if (unlikely(errorm_ptr[i])) {
					wdn_function(isc, level, absolute_offset, frst_relative_block, frst_relative_block + relative_offset_increment - 1);
					break;
				}
			}
		} else {
			valid[relative] = false;
			eio_function(isc, level, absolute_offset, frst_relative_block, frst_relative_block + relative_offset_increment - 1);
			errors = true;
		}
		++relative;
		++absolute_offset;
		frst_relative_block += relative_offset_increment;
		--num;
		buffer_ptr += isc->sc->block_size_Bytes;
		errorm_ptr += isc->sc->block_size_Bytes;
	}
	if (errors) {
		isc->has_io_errors = true;
		++isc->read_error_ind_blocks[0];
	}
}

/*
 * Read the next *needed* directory cluster blocks. If a record skips an
 * entire block, it is not "needed" and hence won't be read.
 * If sc->surface_scan_full is true, nothing will be skipped but read
 * anyways in order to be able to report problems.
 */
#define READ_NEXT_BLOCK_FOR(num_bytes) do { \
	if (offset + num_bytes > max_valid_offset) { \
		if (offset > max_valid_offset) { \
			while (offset > max_valid_offset) { \
				max_valid_offset += isc->sc->block_size_Bytes; \
				++block_offset; \
			} \
			if (offset < max_valid_offset) { \
				max_valid_offset -= isc->sc->block_size_Bytes; \
				--block_offset; \
			} \
		} \
		while (offset + num_bytes > max_valid_offset) { \
			max_valid_offset += isc->sc->block_size_Bytes; \
			if (offset < max_valid_offset \
			||  isc->sc->surface_scan_full) { \
				char * tmp = mprintf("ext2 dir-content cluster %llu block %llu", (unsigned long long)absolute_cluster, (unsigned long long)(offset + block_offset)); \
				if (!read_cluster_block_isc(isc \
					, absolute_block + block_offset \
					, relative_block + block_offset \
					, buffer + block_offset * isc->sc->block_size_Bytes \
					, errmap + block_offset * isc->sc->block_size_Bytes \
					, add_problem_reading_dir_eio \
					, add_problem_reading_dir_wdn \
					, tmp \
				)) { \
					free(tmp); \
					return; \
				} \
				free(tmp); \
			} \
			++block_offset; \
		} \
	} \
} while (0) \

void process_dir_cluster(struct inode_scan_context * isc, block_t absolute_cluster) {
	block_t relative_block = 0; // TODO: Make this an argument
	block_t absolute_block = absolute_cluster * isc->sc->cluster_size_Blocks;
	
	u8 buffer[isc->sc->cluster_size_Bytes];
	u8 errmap[isc->sc->cluster_size_Bytes];
	
	assert(isc->is_dir);
	
	unsigned long max_valid_offset = 0;
	
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
	unsigned block_offset = 0;
	
	while (offset<isc->sc->cluster_size_Bytes) {
		READ_NEXT_BLOCK_FOR(8);
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
		if (!reclen) {
			EXT2_ERRORF(isc,
				"In directory cluster %llu at offset %lu: Zero record length."
				, (unsigned long long)absolute_cluster
				, offset
			);
			// TODO: Try to figure out the record length
			return;
		}
		if (!inode) {
			/*
			EXT2_NOTIFYF(isc,
				"Cluster %lu: Free directory record of length %u bytes."
				, absolute_cluster
				, reclen
			);
			*/
			if (reclen < 8) {
				EXT2_ERRORF(isc,
					"reclen is to small. reclen=%u - skipping ahead 8 bytes."
					, (unsigned)reclen
				);
				offset += 8;
			} else {
				offset += reclen;
			}
			continue;
		}
		
		u8 filename_len=0
			+((u32)buffer[offset+6]    )
		;
		u8 filetype=0
			+((u32)buffer[offset+7]    )
		;
		READ_NEXT_BLOCK_FOR(8+filename_len);
		char * filename=malloc(filename_len+1);
		assert(filename);
		memcpy(filename, buffer + offset + 8, filename_len);
		filename[filename_len]=0;
		/*
		EXT2_NOTIFYF(isc,
			"Cluster %lu: Directory entry pointing to inode %lu: »%s«."
			, absolute_cluster
			, inode
			, filename
		);
		*/
		
		if (inode<=isc->sc->sb->num_inodes) {
			struct inode_owning_map_entry * iome = mk_iome(isc->inode_num, filename, false);
			btlock_lock(isc->sc->inode_owning_map_lock);
			VANEW(isc->sc->inode_owning_map[inode - 1]) = iome;
			btlock_unlock(isc->sc->inode_owning_map_lock);
		} else {
			EXT2_ERRORF(isc,
				"In cluster %llu at offset %lu: "
				"Entry \"%s\" points to illegal inode %lu."
				, (unsigned long long)absolute_cluster
				, offset
				, filename
				, (unsigned long)inode
			);
		}
		
		/*
		EXT2_NOTIFYF(isc,
			"Entry to inode %lu, reclen=%u, filetype=%u, filename_len=%u, filename=\"%s\""
			,(unsigned long)inode,(unsigned)reclen,(unsigned)filetype,(unsigned)filename_len,filename
		);
		*/
		
		if (filename_len+8>reclen) {
			EXT2_ERRORF(isc,
				"In cluster %llu at offset %lu: "
				"reclen is to small for filename. reclen=%u, decoded filename of size %u: \"%s\"."
				, (unsigned long long)absolute_cluster
				, offset
				, (unsigned)reclen
				, (unsigned)filename_len
				, filename
			);
		}
		
		offset+=reclen;
		
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
		MARK_CLUSTERS_IN_USE_BY(isc->sc
			, isc->schedule_first_cluster
			, isc->schedule_num_clusters
			, isc->inode_num
			, isc->schedule_containing
		);
	}
}

static inline void isc_schedule_cluster_add(struct inode_scan_context * isc
	, block_t cluster
	, block_t containing
) {
	if (likely((block_t)(isc->schedule_first_cluster + isc->schedule_num_clusters) == cluster)
	&&  likely((block_t)isc->schedule_containing == containing)) {
		++isc->schedule_num_clusters;
		return;
	}
	isc_schedule_cluster_flush(isc);
	isc->schedule_first_cluster=cluster;
	isc->schedule_num_clusters=1;
	isc->schedule_containing = containing;
//	MARK_CLUSTER_IN_USE_BY(isc->sc,cluster,isc->inode_num);
}

void chk_block(struct inode_scan_context * isc
	, int level
	, unsigned long cluster
	, unsigned long containing
) {
//	eprintf("chk_block(level=%i,cluster=%lu)\n",level,cluster);
	
	if (unlikely(!cluster)) {
		unsigned long h = 1 << (level * isc->sc->num_clusterpointers_per_cluster_shift);
		
		isc->maybe_holes+=h;
		
		return;
	}
	
	isc->holes+=isc->maybe_holes;
	isc->maybe_holes=0;
	
	if (unlikely(cluster>=isc->sc->sb->num_clusters)) {
		// TODO: Problem context!!!
		EXT2_ERRORF(isc,
			"In inode table or indirect cluster %llu: Illegal cluster %llu"
			, (unsigned long long)containing
			, (unsigned long long)cluster
		);
		++isc->illegal_ind_clusters[level];
		return;
	}
	
	isc_schedule_cluster_add(isc, cluster, containing);
	
	++isc->used_clusters;
	
	if (likely(!level)) {
		if (isc->is_dir) {
			process_dir_cluster(isc,cluster);
		} else if (isc->sc->surface_scan_used) {
			bool valid[isc->sc->cluster_size_Blocks];
			u8 buffer[isc->sc->cluster_size_Bytes];
			u8 errmap[isc->sc->cluster_size_Bytes];
			/*
			u8 ignmap[isc->sc->cluster_size_Bytes];
			bool ignore = false
			|| isc->inode_num == BAD_INO
			|| isc->inode_num == RESIZE_INO
			|| isc->inode_num == JOURNAL_INO
			|| false;
			memset(ignmap, ignore ? 0xff : 0x00, isc->sc->cluster_size_Bytes);
			*/
			
			read_cluster_isc(isc
				, cluster
				, 0 // TODO: relative cluster
				, buffer
				, errmap
				/*
				, ignmap
				*/
				, valid
				, add_problem_reading_file_eio
				, add_problem_reading_file_wdn
				, "file-content block %%llu"
			);
		}
		
		return;
	}
	
	u32 pointer[isc->sc->num_clusterpointers_per_cluster];
	u32 err_ptr[isc->sc->num_clusterpointers_per_cluster];
	u8 * ptr_pointer = (u8 *)pointer;
	u8 * ptr_err_ptr = (u8 *)err_ptr;
	bool valid[isc->sc->cluster_size_Blocks];
	
	char * tmp = mprintf("ext2 %s-%cindirect block %%llu", isc->is_dir ? "dir" : "file", level > 1 ? (level == 2 ? 'd' : 't') : 's');
	read_ind_cluster_isc(isc
		, cluster
		, 0 // TODO: relative cluster
		, ptr_pointer
		, ptr_err_ptr
		, valid
		, add_problem_reading_indirect_cluster_eio
		, add_problem_reading_indirect_cluster_wdn
		, tmp
	);
	free(tmp);
	
	unsigned index = 0;
	for (unsigned block = 0; block < isc->sc->cluster_size_Blocks; ++block) {
		if (!valid[block]) {
			index += isc->sc->num_clusterpointers_per_block;
		} else {
			for (unsigned i = 0; i < isc->sc->num_clusterpointers_per_block; ++i) {
				u32 pointer_steps;
				switch (level) {
					case 1 /* sind */: pointer_steps =                                                      1                                             ; break;
					case 2 /* dind */: pointer_steps =      (1 + isc->sc->num_clusterpointers_per_cluster * 1)                                            ; break;
					case 3 /* tind */: pointer_steps = (1 + (1 + isc->sc->num_clusterpointers_per_cluster * 1) * isc->sc->num_clusterpointers_per_cluster); break;
					default: assert_never_reached();
				}
				u32 expected_pointer1 = cluster + 1 + index * pointer_steps;
				u32 expected_pointer2 = index ? pointer[index - 1] + pointer_steps : expected_pointer1;
				u32 expected_pointer3 = index < (isc->sc->num_clusterpointers_per_cluster - 1) ? pointer[index + 1] - pointer_steps : expected_pointer1;
				if (true
				&&  err_ptr[index]
				&&  pointer[index]
				&&  pointer[index] != expected_pointer1
				&&  pointer[index] != expected_pointer2
				&&  pointer[index] != expected_pointer3
				&&  true) {
					const char * what_ind[4] = {
						"ERROR!",
						"single",
						"double",
						"trible",
					};
					// TODO: Problem context
					EXT2_NOTIFYF(isc,
						"Wrong data notification received while reading %s indirect cluster %lu. Possibly wrong cluster pointer: %lu."
						, what_ind[level]
						, (unsigned long)cluster
						, (unsigned long)pointer[index]
					);
				}
				
				chk_block(isc, level - 1, pointer[index++], cluster);
			}
		}
	}
}

// TODO: Problem contexts!
u32 chk_extent_block(struct inode_scan_context * isc, u32 file_cluster, char * eb, size_t size, unsigned depth, block_t containing_cluster) {
	bool in_inode = size == 60;
	
	if (!in_inode) {
		++isc->used_clusters;
	}
	
	struct extent_header * eh;
	
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
			
			if (isc->sc->cluster_size_Blocks != bdev_read(isc->sc->bdev,leaf,isc->sc->cluster_size_Blocks,(unsigned char *)block_buffer, "ext2 extent index")) {
				ERRORF("%s: Inode %lu [%s]: While processing %s extent index: Read error while trying to read an extent cluster.",isc->sc->name,isc->inode_num,isc->type,in_inode?"in-inode":"external");
				// TODO: Read and process as much as possible block-wise
				continue;
			}
			
			endian_swap_extent_block(block_buffer, isc->sc->cluster_size_Blocks);
			
			file_cluster = chk_extent_block(isc, ei->file_cluster, block_buffer, isc->sc->cluster_size_Bytes, eh->depth - 1, leaf);
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
				chk_block(isc, 0, disc_cluster++, containing_cluster);
			}
		}
	}
	free(block_buffer);
	
	// TODO: eh->generation ???
	
	eb += 12 * (size / 12);
	
	/*
	struct extent_tail * et = (struct extent_tail *)eb;
	
	if (size % 12) {
		// TODO: Verify checksum
	}
	*/
	
	return file_cluster;
}

#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
typedef VASIAR(struct thread *) vasiar_struct_thread_pointer;
vasiar_struct_thread_pointer ind[5];
struct btlock_lock * ind_lock;
/*
[0] //                                                  files without single indirect clusters
[1] // directories without single indirect clusters AND files without double indirect clusters
[2] // directories without double indirect clusters AND files without trible indirect clusters
[3] // directories without trible indirect clusters AND files with    trible indirect clusters
[4] // directories with    trible indirect clusters
*/

struct thread * find_next_job() {
	btlock_lock(ind_lock);
	for (size_t level = 5; level--; ) {
		if (VASIZE(ind[level])) {
			// TODO: Replace this by a kind of FIFO
			struct thread * retval = VAACCESS(ind[level], 0);
			for (size_t i = 1; i < VASIZE(ind[level]); ++i) {
				VAACCESS(ind[level], i - 1) = VAACCESS(ind[level], i);
			}
			VATRUNCATE(ind[level], VASIZE(ind[level]) - 1);
			btlock_unlock(ind_lock);
			return retval;
		}
	}
	btlock_unlock(ind_lock);
	return NULL;
}
#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION

void do_chk_block(struct inode_scan_context * isc, bool free_isc) {
	struct scan_context * sc = isc->sc;
	
	/*
	if (t->lock) {
		EXT2_NOTIFYF(isc, "Background job started.");
	}
	*/
	
	isc_schedule_cluster_init(isc);
	
	{
		unsigned long inodes_cluster = get_inodes_cluster(sc, isc->inode_num);
		if (!(isc->inode->flags & INOF_EXTENTS)) {
			for (unsigned z = 0; z < NUM_ZIND; ++z) {
				chk_block(isc, 0, isc->inode->cluster[z], inodes_cluster);
			}
			
			chk_block(isc, 1, isc->inode->cluster[FIRST_SIND], inodes_cluster);
			/*
			if (inode->cluster[FIRST_DIND] || inode->cluster[FIRST_TIND]) {
				PRINT_PROGRESS_BAR(inode_num);
			}
			*/
			chk_block(isc, 2, isc->inode->cluster[FIRST_DIND], inodes_cluster);
			/*
			if (inode->cluster[FIRST_DIND] || inode->cluster[FIRST_TIND]) {
				PRINT_PROGRESS_BAR(inode_num);
			}
			*/
			chk_block(isc, 3, isc->inode->cluster[FIRST_TIND], inodes_cluster);
		} else {
			chk_extent_block(isc, 0, (char *)isc->inode->cluster, 60, 0, inodes_cluster);
		}
	}
	
	isc_schedule_cluster_flush(isc);
	
	unsigned long illegal_clusters=0;
	unsigned long read_error_blocks = 0;
	for (int i=0;i<4;++i) {
		illegal_clusters  += isc->illegal_ind_clusters[i];
		read_error_blocks += isc->read_error_ind_blocks[i];
	}
	
	if (isc->is_dir) {
		unsigned ino=isc->inode_num-1;
		
		if (unlikely(!sc->dir[ino])) {
			/*
			 * We report missing . and .. entries only, if the 
			 * directory doesn't contain illegal clusters and we 
			 * could successfully read all clusters.
			 */
			if (unlikely(!illegal_clusters && !read_error_blocks)) {
				EXT2_ERROR(isc,
					"Empty directory is missing it's . and .. entry."
				);
			}
			goto skip_dir;
		}
		
		struct dir * d=malloc(sizeof(*d));
		assert(d);
		
		d->num_entries=0;
		
		struct dirent_list * dl=(struct dirent_list *)sc->dir[isc->inode_num-1];
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
			
			if (strchr(dst,'/')) {
				EXT2_ERRORF(isc,
					"Filename \"%s\" of entry %u contains a slash."
					,dst
					,i+1
				);
			}
			// Can't check d->entry[entry].filetype right now.
		}
		free(dl);
		sc->dir[isc->inode_num-1]=d;
	}
	
skip_dir:
	if (illegal_clusters) {
		EXT2_ERRORF(isc,
			"Contains %lu illegal cluster(s) "
			"(%lu zind, %lu sind, %lu dind, %lu tind)."
			, illegal_clusters
			, isc->illegal_ind_clusters[0],isc->illegal_ind_clusters[1]
			, isc->illegal_ind_clusters[2],isc->illegal_ind_clusters[3]
		);
	}
	
	if (read_error_blocks) {
		EXT2_ERRORF(isc,
			"Couldn't read %lu block(s) "
			"(%lu zind, %lu sind, %lu dind, %lu tind)."
			, read_error_blocks
			, isc->read_error_ind_blocks[0]
			, isc->read_error_ind_blocks[1]
			, isc->read_error_ind_blocks[2]
			, isc->read_error_ind_blocks[3]
		);
	}
	
	if (isc->inode->size < isc->used_clusters + isc->holes && !(isc->inode->flags & INOF_EOFBLOCKS)) {
		EXT2_ERROR(isc,
			"size < position addressed by highest allocated cluster"
		);
	}
	
	if (isc->inode->num_blocks % sc->cluster_size_Blocks) {
		EXT2_ERROR(isc,
			"Num_blocks is not a multiple of the cluster size"
		);
	}
	
	unsigned long used_blocks = isc->used_clusters * sc->cluster_size_Blocks;
	if (isc->inode->num_blocks!=used_blocks) {
		add_problem_wrong_num_blocks(isc, used_blocks);
	}
	
	if (!isc->keep_inode) free(isc->inode);
	if (free_isc) free(isc);
}

#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
THREAD_RETURN_TYPE chk_block_function(THREAD_ARGUMENT_TYPE arg) {
	struct thread * t=(struct thread *)arg;
	
	struct scan_context * sc = t->isc->sc;
	
	if (sc->allow_concurrent_chk_block_function) {
		if (t->lock) {
			btlock_lock(t->lock);
			btlock_unlock(t->lock);
		}
	}
start_work: ;
	
	do_chk_block(t->isc, false);
	
	if (sc->waiting4threads) {
		if (t->lock) {
			EXT2_NOTIFY(t->isc,
				"Background job completed."
			);
		}
	}
	
	free(t->isc);
	
	struct thread * t2 = find_next_job();
	if (t2) {
		t->isc = t2->isc;
		btlock_unlock(t2->lock);
		btlock_lock_free(t2->lock);
		free(t2);
		goto start_work;
	}
	
	THREAD_RETURN();
}
#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION

void try_clone_failed(struct thread * t) {
#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
	if (t->isc->sc->allow_concurrent_chk_block_function) {
		btlock_unlock(t->lock);
		btlock_lock_free(t->lock);
		t->lock = NULL;
	}
#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
	do_chk_block(t->isc, true);
	free(t);
}

#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
void register_job(struct thread * t) {
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
	t->prev = thread_tail->prev;
	t->next = thread_tail;
	t->prev->next = t;
	t->next->prev = t;
	
	{
		struct inode_scan_context * isc = t->isc; // (struct inode_scan_context *)t->arg;
		if (isc->sc->waiting4threads) {
			EXT2_NOTIFY(isc,
				"Processing inode's cluster allocation check in background"
			);
		}
	}
	
	/*
	 * Unlocking is straight forward: Just do it ;)
	 */
	btlock_unlock(t->next->lock);
	btlock_unlock(t->prev->lock);
}

void try_clone_succeeded(struct thread * t) {
	setpriority(PRIO_PROCESS, 0, 20);
	++live_child;
	register_job(t);
	
	btlock_unlock(t->lock);
}

void do_try_clone(struct thread * t) {
	t->thread_ctx = create_thread(65536, chk_block_function, (void *)t);
	if (!t->thread_ctx) {
		eprintf("clone failed\n");
		try_clone_failed(t);
	} else {
		try_clone_succeeded(t);
	}
}

void try_clone() {
	{
		struct thread * walk = thread_head->next;
		while (walk->next) {
			if (terminated_thread(walk->thread_ctx)) {
				cleanup_thread(walk->thread_ctx);
				walk->next->prev = walk->prev;
				walk->prev->next = walk->next;
				btlock_lock_free(walk->lock);
				struct thread * w = walk;
				walk = walk->prev;
				free(w);
				--live_child;
			}
			walk = walk->next;
		}
	}
	
	while (live_child < hard_child_limit) {
		struct thread * t = find_next_job();
		if (!t) break;
		do_try_clone(t);
	}
	
	bprintf(
		"%u %u %u %u %u\r"
		, (unsigned)VASIZE(ind[4])
		, (unsigned)VASIZE(ind[3])
		, (unsigned)VASIZE(ind[2])
		, (unsigned)VASIZE(ind[1])
		, (unsigned)VASIZE(ind[0])
	);
}
#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION

void cluster_scan_inode(struct inode_scan_context * isc) {
	u64 size=isc->inode->size;
	
	if (isc->type_bit&FTB_FILE) {
		size|=(u64)isc->inode->size_high<<32;
	}
	
	if (!isc->type_bit) {
		if (!isc->keep_inode) free(isc->inode);
		free(isc);
		return;
	}
	
	for (int i=0;i<4;++i) {
		isc->illegal_ind_clusters[i] = 0;
		isc->read_error_ind_blocks[i] = 0;
	}
	isc->used_clusters=0;
	isc->maybe_holes=0;
	isc->holes=0;
	
	struct thread * t=malloc(sizeof(*t));
	if (!t) {
		eprintf("cluster_scan_inode: OOPS: malloc(t) failed: %s\n",strerror(errno));
		exit(2);
	}
	
	t->isc = isc;
	
#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
	if (isc->sc->allow_concurrent_chk_block_function) {
		t->lock = btlock_lock_mk();
		if (!t->lock) {
			free(t);
			eprintf("cluster_scan_inode: OOPS: btlock_lock_mk failed: %s\n", strerror(errno));
			exit(2);
		}
		
		// First we lock the new thread, so we don't get hurt in the very last moment ...
		btlock_lock(t->lock); // BTW: The thread has not been started yet ;)
		
		size_t level = 0;
		if ((isc->type_bit & FTB_DIRE                                   ) || isc->inode->cluster[FIRST_SIND]) {
			level = 1;
		}
		if ((isc->type_bit & FTB_DIRE && isc->inode->cluster[FIRST_SIND]) || isc->inode->cluster[FIRST_DIND]) {
			level = 2;
		}
		if ((isc->type_bit & FTB_DIRE && isc->inode->cluster[FIRST_DIND]) || isc->inode->cluster[FIRST_TIND]) {
			level = 3;
		}
		if ((isc->type_bit & FTB_DIRE && isc->inode->cluster[FIRST_TIND])) {
			level = 4;
		}
		if (!level
#ifdef HAVE_VALGRIND
		||  (level == 1 && RUNNING_ON_VALGRIND)
#endif // #ifdef HAVE_VALGRIND
		||  false) {
			try_clone_failed(t);
		} else {
			btlock_lock(ind_lock);
			VANEW(ind[level]) = t;
			btlock_unlock(ind_lock);
			try_clone();
		}
	} else {
#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
		try_clone_failed(t);
#if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
	}
#endif // #if ALLOW_CONCURRENT_CHK_BLOCK_FUNCTION
}

void typecheck_inode(struct inode_scan_context * isc, struct inode * inode) {
	struct problem_context * pc = get_problem_context(isc->sc, isc->inode_num);
	
	if (pc
	&&  pc->problem_mask & PM_IT_EIO) {
		isc->type_bit = 0;
		isc->type = "EIO!";
		isc->is_dir = pc->problem_mask & PM_INODE_IS_DIR;
		goto out;
	}
	
	isc->inode = inode;
	isc->keep_inode = true;
	
	if (isc->inode) {
		unsigned ft;
		if (!isc->inode->links_count) {
			u8 * data = (u8 *)isc->inode;
			isc->type_bit = 0;
			isc->type = "FREE";
			ft = FT_FREE;
			for (size_t i = 0; i < 128; ++i) {
				if (data[i]) {
					isc->type = "DELE";
					ft = FT_DELE;
					if (!isc->inode->dtime) {
						add_problem_zero_dtime(isc);
					}
					break;
				}
			}
		} else {
			switch (MASK_FT(isc->inode)) {
				case MF_BDEV: isc->type = "BDEV"; isc->type_bit = FTB_BDEV; ft = FT_BDEV; break;
				case MF_CDEV: isc->type = "CDEV"; isc->type_bit = FTB_CDEV; ft = FT_CDEV; break;
				case MF_DIRE: isc->type = "DIRE"; isc->type_bit = FTB_DIRE; ft = FT_DIRE; break;
				case MF_FIFO: isc->type = "FIFO"; isc->type_bit = FTB_FIFO; ft = FT_FIFO; break;
				case MF_SLNK: isc->type = "SLNK"; isc->type_bit = FTB_SLNK; ft = FT_SLNK; break;
				case MF_FILE: isc->type = "FILE"; isc->type_bit = FTB_FILE; ft = FT_FILE; break;
				case MF_SOCK: isc->type = "SOCK"; isc->type_bit = FTB_SOCK; ft = FT_SOCK; break;
				default     : isc->type = "ILLT"; isc->type_bit =        0; ft = FT_ILLT; break;
			}
		}
		isc->sc->inode_type[isc->inode_num - 1] = ft;
		
		isc->is_dir = isc->type_bit & FTB_DIRE;
	} else {
		isc->is_dir = false;
		switch (isc->sc->inode_type[isc->inode_num - 1]) {
			case FT_FILE: isc->type = "FILE"; isc->type_bit = FTB_FILE; break;
			case FT_DIRE: isc->type = "DIRE"; isc->type_bit = FTB_DIRE; isc->is_dir = true; break;
			case FT_CDEV: isc->type = "CDEV"; isc->type_bit = FTB_CDEV; break;
			case FT_BDEV: isc->type = "BDEV"; isc->type_bit = FTB_BDEV; break;
			case FT_FIFO: isc->type = "FIFO"; isc->type_bit = FTB_FIFO; break;
			case FT_SOCK: isc->type = "SOCK"; isc->type_bit = FTB_SOCK; break;
			case FT_SLNK: isc->type = "SLNK"; isc->type_bit = FTB_SLNK; break;
			case FT_EIO : isc->type = "EIO!"; isc->type_bit =        0; break;
			case FT_DELE: isc->type = "DELE"; isc->type_bit =        0; break;
			case FT_FREE: isc->type = "FREE"; isc->type_bit =        0; break;
			case FT_ILLT: isc->type = "ILLT"; isc->type_bit =        0; break;
			default: assert_never_reached();
		}
		if (pc
		&&  pc->problem_mask & PM_IT_EIO
		&&  pc->problem_mask & PM_INODE_IS_DIR) {
			isc->is_dir = true;
		}
	}
	
out: ;
}

/*
 * If we start an inode check before it is regularily in turn, we set 
 * prefetching=true to prevent read_table_for_one_group from marking the 
 * clusters ocupied by the group's special clusters marked. That will be 
 * done in the regular run later.
 */
bool check_inode(struct scan_context * sc, unsigned long inode_num, bool prefetching, bool expected_to_be_free) {
	if (prefetching) {
		sc->table_reader_group = (inode_num - 1) / sc->sb->inodes_per_group;
		
		read_table_for_one_group(sc, prefetching);
	}
	
	struct inode_scan_context * isc=malloc(sizeof(*isc));
	assert(isc);
	isc->sc=sc;
	isc->inode_num=inode_num;
	isc->inode = NULL;
	
	struct problem_context * pc = get_problem_context(sc, inode_num);
	if (pc && pc->problem_mask & PM_IT_EIO) {
		if (expected_to_be_free) {
			add_problem_free(isc);
		}
		free(isc);
		return expected_to_be_free;
	}
	
	{
		unsigned long i = inode_num - 1;
		unsigned long g = i / sc->sb->inodes_per_group;
		i %= sc->sb->inodes_per_group;
		
		if (i >= sc->sb->inodes_per_group - sc->gdt[g].num_virgin_inodes) {
			bool retval = isc->sc->inode_type[isc->inode_num - 1] == FT_FREE;
			free(isc);
			return retval;
		}
		
		struct inode * inode = isc->sc->inode_table + i;
#if ALLOW_CONCURRENT_TABLE_READER
		if (sc->allow_concurrent_table_reader) {
			inode += isc->sc->sb->inodes_per_group * (g % INODE_TABLE_ROUNDROUBIN);
		}
#endif // #if ALLOW_CONCURRENT_TABLE_READER
		
		struct inode * inode_copy=malloc(128);
		assert(inode_copy);
		memcpy(inode_copy,inode,128);
		
		typecheck_inode(isc, inode_copy);
		isc->keep_inode=false;
	}
	
	sc->inode_link_count[inode_num-1]=isc->inode->links_count;
	sc->inode_type_str[inode_num-1]=isc->type;
	
	isc->has_io_errors=false;
	
	bool ok = true;
	
	assert(!get_calculated_inode_allocation_map_bit(isc->sc,isc->inode_num));
	
	if (!isc->inode->links_count) {
		bool retval = isc->sc->inode_type[isc->inode_num - 1] == FT_FREE;
		if (!isc->keep_inode) free(isc->inode);
		free(isc);
		return retval;
	}
	
	/*
	if (get_calculated_inode_allocation_map_bit(isc->sc,isc->inode_num)) {
		if (!isc->keep_inode) free(isc->inode);
		free(isc);
		return false;
	}
	*/
	
	/*
	u8 a[4];
	for (size_t i=0;i<4;++i) {
		a[i]=isc->sc->calculated_inode_allocation_map[(((isc->inode_num-1)>>5)<<2)+i];
	}
	*/
	
	set_calculated_inode_allocation_map_bit(isc->sc,isc->inode_num);
	
	/*
	u8 b[4];
	for (size_t i=0;i<4;++i) {
		b[i]=isc->sc->calculated_inode_allocation_map[(((isc->inode_num-1)>>5)<<2)+i];
	}
	
	EXT2_NOTIFYF(isc,
		"Marked inode %lu (with bit offset %lu) as in use, bitmap pattern at offset 0x%lx was %02x %02x %02x %02x, is now %02x %02x %02x %02x"
		,(unsigned long)isc->inode_num
		,(unsigned long)(isc->inode_num-1)
		,(unsigned long)(((isc->inode_num-1)>>5)<<2)
		,(unsigned)a[0],(unsigned)a[1],(unsigned)a[2],(unsigned)a[3]
		,(unsigned)b[0],(unsigned)b[1],(unsigned)b[2],(unsigned)b[3]
	);
	*/
	
	assert(get_calculated_inode_allocation_map_bit(isc->sc,isc->inode_num));
	
	if (!isc->type_bit) {
		EXT2_ERRORF(isc,
			"Has illegal type number %u set"
			, (unsigned)MASK_FT(isc->inode)
		);
		ok=false;
		if (!isc->keep_inode) free(isc->inode);
		free(isc);
		return false;
	}
	
	if (isc->inode->dtime) {
		if (isc->sc->warn_dtime_nonzero) {
			EXT2_NOTIFY(isc,
				"(in-use) has dtime set"
			);
		}
		isc->inode->dtime=0;
	}
	
	if (isc->inode->ctime>isc->sc->sb->wtime) {
		if (isc->sc->warn_ctime) {
			EXT2_NOTIFYF(isc,
				"Has ctime (=%lu) in future relative to last write time (=%lu)"
				, (unsigned long)isc->inode->ctime
				, (unsigned long)isc->sc->sb->wtime
			);
		}
		isc->inode->ctime=isc->sc->sb->wtime;
		ok=false;
	}
	
	if (isc->inode->mtime>isc->sc->sb->wtime) {
		if (isc->sc->warn_mtime) {
			EXT2_NOTIFYF(isc,
				"Has mtime (=%lu) in future relative to last write time (=%lu)"
				, (unsigned long)isc->inode->mtime
				, (unsigned long)isc->sc->sb->wtime
			);
		}
		isc->inode->mtime=isc->sc->sb->wtime;
		ok=false;
	}
	
	if (isc->inode->atime>isc->sc->sb->wtime) {
		if (isc->sc->warn_atime) {
			EXT2_NOTIFYF(isc,
				"Has atime (=%lu) in future relative to last write time (=%lu)"
				, (unsigned long)isc->inode->atime
				, (unsigned long)isc->sc->sb->wtime
			);
		}
		isc->inode->atime=isc->sc->sb->mtime;
		ok=false;
	}
	
	if (isc->type_bit&~FTB_DIRE) {
		if (isc->inode->flags&INOF_INDEX) {
			EXT2_ERROR(isc,
				"Has INDEX flag set (which does make sense for directories only)"
			);
			ok=false;
		}
		if (isc->inode->flags&INOF_DIRSYNC) {
			EXT2_ERROR(isc,
				"Has DIRSYNC flag set (which does make sense for directories only)"
			);
			ok=false;
		}
	}
	
	if (isc->type_bit&~(FTB_DIRE|FTB_FILE)) {
		if (isc->inode->flags&INOF_IMMUTABLE) {
			EXT2_ERROR(isc,
				"Has IMMUTABLE flag set."
			);
			ok=false;
		}
		if (isc->inode->flags&INOF_APPEND_ONLY) {
			EXT2_ERROR(isc,
				"Has APPEND_ONLY flag set."
			);
			ok=false;
		}
		if (isc->inode->flags&INOF_SECURE_REMOVE) {
			EXT2_ERROR(isc,
				"Has SECURE_REMOVE flag set."
			);
			ok=false;
		}
	}
	
	if (isc->type_bit & ~(FTB_DIRE | FTB_FILE | FTB_SLNK)) {
		if (isc->inode->flags & INOF_SYNC) {
			EXT2_ERROR(isc,
				"Has SYNC flag set."
			);
			ok = false;
		}
	}
	
	if (isc->inode->flags&INOF_E2COMPR_FLAGS) {
		if (!(isc->sc->sb->in_compat&IN_COMPAT_COMPRESSION)) {
			EXT2_ERROR(isc,
				"Has at least one compression flag set but filesystem has the feature bit not set"
			);
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
				EXT2_ERROR(isc,
					"Has at least one compression flag set"
				);
				ok=false;
			} else {
				if (isc->type_bit&FTB_DIRE
				&&  (isc->inode->flags&INOF_E2COMPR_FLAGS)!=INOF_E2COMPR_COMPR) {
					EXT2_ERROR(isc,
						"Has at least one compression flag (expcept for COMPR) set"
					);
					ok=false;
				}
				
				if (isc->type_bit&FTB_FILE) {
					if (isc->inode->flags&INOF_E2COMPR_COMPR) {
						EXT2_NOTIFY(isc,
							"Has flag E2COMPR_COMPR set."
						);
					}
					
					if (isc->inode->flags&INOF_E2COMPR_DIRTY) {
						EXT2_NOTIFY(isc,
							"Has flag E2COMPR_DIRTY set."
						);
					}
					
					if (isc->inode->flags&INOF_E2COMPR_HASCOMPRBLK) {
						EXT2_NOTIFY(isc,
							"Has flag E2COMPR_HASCOMPRBLK set, can't check validity of data yet."
						);
					}
					
					if (isc->inode->flags&INOF_E2COMPR_NOCOMPR) {
						EXT2_NOTIFY(isc,
							"Has flag E2COMPR_NOCOMPR set."
						);
					}
					
					if (isc->inode->flags&INOF_E2COMPR_ERROR) {
						EXT2_NOTIFY(isc,
							"Has flag E2COMPR_ERROR set."
						);
					}
				}
			}
		}
	}
	
	if (isc->inode->flags&INOF_IMAGIC
	&&  !(isc->sc->sb->rw_compat&RW_COMPAT_IMAGIC_INODES)) {
		EXT2_ERROR(isc,
			"Has flag IMAGIC set."
		);
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
		EXT2_ERROR(isc,
			"Has flag JOURNAL_DATA set."
		);
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
		EXT2_NOTIFY(isc,
			"Has flag NOTAIL set (whatever that means)."
		);
	}
	
	if (!!(isc->inode->flags&INOF_TOPDIR) != (isc->inode_num==ROOT_INO)) {
		if (isc->inode_num==ROOT_INO) {
			EXT2_NOTIFY(isc,
				"(root) has flag TOPDIR not set."
			);
		} else {
			EXT2_NOTIFY(isc,
				"(not root) has flag TOPDIR set."
			);
		}
		isc->inode->flags^=INOF_TOPDIR;
	}
	
	if (isc->inode->flags&INOF_HUGE_FILE) {
		EXT2_NOTIFY(isc,
			"Has unsupported flag HUGE_FILE set."
		);
	}
	
	// Proper handling of INOF_EXTENTS is established
	
	if (isc->inode->flags&INOF_RESERVED_00100000) {
		EXT2_NOTIFY(isc,
			"Has unsupported flag RESERVED_00100000 set."
		);
	}
	
	if (isc->inode->flags&INOF_EA_INODE) {
		EXT2_NOTIFY(isc,
			"Has unsupported flag EA_INODE set."
		);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_00800000) {
		EXT2_NOTIFY(isc,
			"Has flag UNKNOWN_00800000 set."
		);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_01000000) {
		EXT2_NOTIFY(isc,
			"Has flag UNKNOWN_01000000 set."
		);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_02000000) {
		EXT2_NOTIFY(isc,
			"Has flag UNKNOWN_02000000 set."
		);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_04000000) {
		EXT2_NOTIFY(isc,
			"Has flag UNKNOWN_04000000 set."
		);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_08000000) {
		EXT2_NOTIFY(isc,
			"Has flag UNKNOWN_08000000 set."
		);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_10000000) {
		EXT2_NOTIFY(isc,
			"Has flag UNKNOWN_10000000 set."
		);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_20000000) {
		EXT2_NOTIFY(isc,
			"Has flag UNKNOWN_20000000 set."
		);
	}
	
	if (isc->inode->flags&INOF_UNKNOWN_40000000) {
		EXT2_NOTIFY(isc,
			"Has flag UNKNOWN_40000000 set."
		);
	}
	
	// We ignore INOF_UNRM and INOF_TOOLCHAIN completely
	
	// We ignore INOF_NODUMP (this inode should not be backed up) completely
	// We ignore INOF_NOATIME (do not update atime on access) completely
	
	if (isc->inode->file_acl) {
		EXT2_NOTIFYF(isc,
			"Don't know how to handle file_acl=%lu."
			, (unsigned long)isc->inode->file_acl
		);
	}
	
	if (isc->inode->faddr) {
		EXT2_NOTIFYF(isc,
			"Don't know how to handle faddr=%lu."
			, (unsigned long)isc->inode->faddr
		);
	}
	if (isc->inode->frag) {
		EXT2_NOTIFYF(isc,
			"Don't know how to handle frag=%u."
			, (unsigned)isc->inode->frag
		);
	}
	if (isc->inode->fsize) {
		EXT2_NOTIFYF(isc,
			"Don't know how to handle fsize=%u."
			, (unsigned)isc->inode->fsize
		);
	}
	
	if (isc->type_bit&~(FTB_DIRE|FTB_FILE|FTB_SLNK)) {
		if (isc->type_bit&~FTB_SLNK) {
			if (isc->inode->size) {
				EXT2_NOTIFY(isc,
					"Has non-zero size."
				);
				isc->inode->size=0;
			}
		}
		
		if (isc->inode->size_high) {
			EXT2_NOTIFY(isc,
				"Has non-zero size_high."
			);
			isc->inode->size_high=0;
		}
	}
	
	if (isc->type_bit&FTB_SLNK
	&&  isc->inode->size<sizeof(isc->inode->cluster)) {
		if (!isc->inode->size) {
			EXT2_ERROR(isc,
				"Has size == 0"
			);
			ok=false;
		} else {
			size_t len=strnlen((char *)isc->inode->cluster,sizeof(isc->inode->cluster));
			if (len!=isc->inode->size) {
				EXT2_ERRORF(isc,
					"(fast symbolic link) has size %u but %lu was recored."
					, (unsigned)len
					, (unsigned long)isc->inode->size
				);
				ok=false;
			}
		}
	}
	
	if (isc->type_bit&FTB_DIRE) {
		if (isc->inode->dir_acl) {
			EXT2_NOTIFYF(isc,
				"Don't know how to handle dir_acl=%lu."
				, (unsigned long)isc->inode->dir_acl
			);
		}
	}
	
	if (isc->type_bit&~(FTB_DIRE|FTB_FILE|FTB_SLNK)
	||  ((isc->type_bit&FTB_SLNK) && (isc->inode->size<MAX_SIZE_FAST_SYMBOLIC_LINK))) {
		/*
		if (isc->type_bit&~FTB_SLNK) {
			for (int c=1;c<NUM_CLUSTER_POINTERS;++c) {
				if (isc->inode->cluster[c]) {
					EXT2_NOTIFYF(isc,
						"Has cluster[%i]=0x%08lx."
						,c
						,(unsigned long)isc->inode->cluster[c]
					);
				}
			}
		}
		*/
		
		if (!isc->keep_inode) free(isc->inode);
		free(isc);
		return false;
	}
	
	/*
	 * Here we have only FILES, DIRECTORIES 
	 * and non-fast SYMBOLIC LINKS left.
	 */
	
	cluster_scan_inode(isc);
	
	ignore(ok);
	
	return false;
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

struct fs_inode {
	struct scan_context * fs; // struct fs * fs
	u32 number;
	struct inode data;
	u32 nind_number[4];
	u8 * nind_buffer[4];
};

struct fs_inode * ext2_get_inode(struct scan_context * sc, u32 ino) {
	struct fs_inode * retval=malloc(sizeof(*retval));
	if (!retval) return NULL;
	
	u32 bs=sc->block_size;
	u8 block_buffer[bs];
	u32 ino_group=(ino-1)/sc->sb->inodes_per_group;
	u32 ino_block=(ino-1)%sc->sb->inodes_per_group*sizeof(retval->data)/bs;
	u32 ino_offset=(ino-1)%sc->sb->inodes_per_group*sizeof(retval->data)%bs;
	
	char * tmp = mprintf("ext2_get_inode[%lu]", (unsigned long)ino);
	if (1 != bdev_read(sc->bdev, sc->cluster_size_Blocks * sc->gdt[ino_group].inode_table + ino_block, 1, block_buffer, tmp)) {
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
	return retval;
}

void ext2_put_inode(struct fs_inode * inode) {
	for (int i=1;i<4;++i) {
		free(inode->nind_buffer[i]);
	}
	free(inode);
}

size_t ext2_read_clusters(struct scan_context * fs, u32 absolute_cluster, size_t num, u8 * buffer, const char * reason) {
	unsigned cs = fs->cluster_size_Blocks;
	
	return bdev_read(fs->bdev, cs * absolute_cluster, cs * num, buffer, reason) / cs;
}

bool ext2_read_cluster(struct scan_context * fs, u32 absolute_cluster, u8 * buffer, const char * reason) {
	return ext2_read_clusters(fs, absolute_cluster, 1, buffer, reason);
}

size_t ext2_write_clusters(struct scan_context * fs, u32 absolute_cluster, size_t num, const u8 * buffer) {
	unsigned cs = fs->cluster_size_Blocks;
	
	return bdev_write(fs->bdev, cs * absolute_cluster, cs * num,buffer) / cs;
}

bool ext2_write_cluster(struct scan_context * fs, u32 absolute_cluster, const u8 * buffer) {
	return ext2_write_clusters(fs, absolute_cluster, 1, buffer);
}

u32 ext2_inode_translate_cluster(struct fs_inode * inode, u32 relative_cluster, char * bdev_msg) {
	u32 absolute_cluster = 0, *pointers;
	u32 num[4]={
		NUM_ZIND << (0 * inode->fs->num_clusterpointers_per_cluster_shift),
		NUM_SIND << (1 * inode->fs->num_clusterpointers_per_cluster_shift),
		NUM_DIND << (2 * inode->fs->num_clusterpointers_per_cluster_shift),
		NUM_TIND << (3 * inode->fs->num_clusterpointers_per_cluster_shift),
	};
	u32 ptr[4]={
		FIRST_ZIND,
		FIRST_SIND,
		FIRST_DIND,
		FIRST_TIND,
	};
	u32 shift[4]={
		0 * inode->fs->num_clusterpointers_per_cluster_shift,
		1 * inode->fs->num_clusterpointers_per_cluster_shift,
		2 * inode->fs->num_clusterpointers_per_cluster_shift,
		3 * inode->fs->num_clusterpointers_per_cluster_shift,
	};
	int level;
	
	size_t bs = bdev_get_block_size(inode->fs->bdev);
	for (level=0;level<4;++level) {
		if (relative_cluster<num[level]) {
			absolute_cluster=inode->data.cluster[ptr[level]+(relative_cluster>>shift[level])];
			assert(!level || !(relative_cluster >> shift[level]));
			while (level) {
				if (unlikely(!absolute_cluster)) {
					return 0;
				}
				if (unlikely(absolute_cluster == (u32)-1)) {
					return -1;
				}
				if (inode->nind_number[level] != absolute_cluster) {
					for (unsigned block = 0; block < inode->fs->cluster_size_Blocks; ++block) {
						if (!bdev_read(
							inode->fs->bdev,
							absolute_cluster * inode->fs->cluster_size_Blocks + block,
							1,
							inode->nind_buffer[level] + block * bs,
							bdev_msg
						)) {
							memset(inode->nind_buffer[level] + block * bs, 255, bs);
						}
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
	return absolute_cluster;
}

void ext2_inode_write_relative_cluster(struct fs_inode * inode,u32 relative_cluster,const u8 * buffer) {
	u32 absolute_cluster = ext2_inode_translate_cluster(inode, relative_cluster, "TODO: reason (ext2_inode_write_relative_cluster)");
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

u8 * ext2_inode_read_relative_cluster(struct fs_inode * inode, u32 relative_cluster) {
	u8 * retval=malloc(inode->fs->cluster_size_Bytes);
	if (!retval) return NULL;
	u32 absolute_cluster = ext2_inode_translate_cluster(inode, relative_cluster, "TODO: reason (ext2_inode_read_relative_cluster)");
	if (absolute_cluster==(u32)-1) {
		return NULL;
	}
	if (!absolute_cluster) {
		memset(retval,0,inode->fs->cluster_size_Bytes);
	} else {
		char * tmp = mprintf("ext2_inode_read_relative_cluster(%lu,%llu)", (unsigned long)inode->number, (unsigned long long)relative_cluster);
		if (!ext2_read_cluster(inode->fs, absolute_cluster, retval, tmp)) {
			free(retval);
			retval = NULL;
		}
		free(tmp);
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
	struct fs_inode * inode = ext2_get_inode(sc, cwd);
	if (!inode) {
		ERRORF("%s: Couldn't ext2_get_inode »%s« in %lu.", sc->name, path, (unsigned long)cwd);
		return 0;
	}
	
	/*
	u32 num_clusters=inode->data.size/sc->cluster_size_Bytes;
	eprintf("inode->data.size=%lu\n",(unsigned long)inode->data.size);
	eprintf("num_clusters=%lu\n",(unsigned long)num_clusters);
	*/
	u32 num_clusters=NUM_ZIND
		+(1<<(1*inode->fs->num_clusterpointers_per_cluster_shift))
		+(1<<(2*inode->fs->num_clusterpointers_per_cluster_shift))
		+(1<<(3*inode->fs->num_clusterpointers_per_cluster_shift))
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

bool ext2_dump_file_by_inode(struct scan_context * fs, u32 ino, const char * filename, const char * filename_msg) {
	int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		ERRORF("%s: Couldn't open file »%s«: %s.", fs->name, filename, strerror(errno));
		return false;
	}
	struct fs_inode * inode = ext2_get_inode(fs, ino);
	if (!inode) {
		ERRORF("%s: Couldn't ext2_get_inode %lu in ext2_dump_file_by_inode »%s«.", fs->name, (unsigned long)ino, filename);
		close(fd);
		return false;
	}
	
	size_t bs = bdev_get_block_size(fs->bdev);
	u8 buffer[bs];
	bool have_errors = false;
	char * bdev_msg = NULL;
	
	if (!inode->data.size) {
		goto skip_reading_loop;
	}
	
	u64 size = inode->data.size + ((u64)inode->data.size_high << 32);
	
	size_t cs = fs->cluster_size_Blocks;
	
	size_t last_num_bytes = size % bs;
	if (!last_num_bytes) last_num_bytes = bs;
	size = 1 + (size - last_num_bytes) / bs;
	
	size_t last_num_blocks = size % cs;
	if (!last_num_blocks) last_num_blocks = cs;
	size = 1 + (size - last_num_blocks) / cs;
	
	bdev_msg = mprintf("ext2_dump_file_by_inode(%lu, \"%s\")", (unsigned long)ino, filename_msg);
	
	u32 num_clusters = size;
	
	for (u32 cluster = 0; cluster < num_clusters; ++cluster) {
		char * bdev_msg_for_translate = mprintf("ext2_inode_translate_cluster(%lu, \"%s\", %llu)", (unsigned long)inode, filename_msg, (unsigned long long)cluster);
		u32 absolute_cluster = ext2_inode_translate_cluster(inode, cluster, bdev_msg_for_translate);
		free(bdev_msg_for_translate);
		if (absolute_cluster && absolute_cluster != (u32)-1) {
			block_t num_blocks;
			if (cluster + 1 == num_clusters) {
				num_blocks = last_num_blocks;
			} else {
				num_blocks = cs;
			}
			for (block_t relative_block = 0; relative_block < num_blocks; ++relative_block) {
				u32 lba = cs * absolute_cluster + relative_block;
				block_t r = bdev_read(fs->bdev, lba, 1, buffer, bdev_msg);
				size_t num_bytes;
				if (cluster + 1 == (u32)num_clusters
				&&  relative_block + 1 == num_blocks) {
					num_bytes = last_num_bytes;
				} else {
					num_bytes = bs;
				}
				if (1 != r) {
					have_errors = true;
					snprintf(
						(char*)buffer,
						bs,
						"\n"
						"************************************************************************\n"
						"*                                                                      *\n"
						"* * * * * * * * * * * *  PHYSICAL DISK-IO ERROR  * * * * * * * * * * * *\n"
						"*  *   *   *   *   *   *                        *   *   *   *   *   *  *\n"
						"* * * * * * * * * * * * * *  LBA %10llu"  "  * * * * * * * * * * * * * *\n"
						"*                                                                      *\n"
						"************************************************************************\n"
						, (unsigned long long)(lba + 63)
					);
				}
				size_t w = write(fd, buffer, num_bytes);
				if (w != num_bytes) {
					ERRORF("Write error while writing to file %s: %s.", filename, strerror(errno));
					have_errors = true;
					goto skip_reading_loop;
				}
			}
		} else {
			if (absolute_cluster) {
				have_errors = true;
			}
			off_t l = lseek(fd, fs->cluster_size_Bytes, SEEK_CUR);
			assert(l != (off_t)-1);
		}
	}
	
skip_reading_loop: ;
	uid_t uid = inode->data.uid + ((u32)inode->data.uid_high << 16);
	gid_t gid = inode->data.gid + ((u32)inode->data.gid_high << 16);
	mode_t mode = inode->data.mode + ((u32)inode->data.mode_high << 16);
	struct utimbuf time;
	time.actime = inode->data.atime;
	time.modtime = inode->data.mtime;
	int rv = fchown(fd, uid, gid);
	ignore(rv);
	fchmod(fd, mode);
	close(fd);
	utime(filename, &time);
	// file flags
	// acl
	ext2_put_inode(inode);
	free(bdev_msg);
	return !have_errors;
}

void ext2_dump_file_by_path(struct scan_context * sc, const char * path, const char * filename) {
	u32 ino=ext2_lookup_absolute_path(sc,path);
	if (!ino) {
		ERRORF("%s: Couldn't locate inode via path »%s«: %s.",sc->name,path,strerror(errno));
	} else {
		ext2_dump_file_by_inode(sc, ino, filename, path);
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
		struct fs_inode * inode=ext2_get_inode(sc,ino);
		if (!inode) {
			ERRORF("%s: Couldn't ext2_get_inode »%s«.", sc->name, path);
			close(fd);
			return;
		}
		u32 num_clusters=(inode->data.size+sc->cluster_size_Bytes-1)/sc->cluster_size_Bytes;
		u8 buffer[sc->cluster_size_Bytes];
		for (u32 cluster=0;cluster<num_clusters;++cluster) {
			if (sc->cluster_size_Bytes != (size_t)read(fd, buffer, sc->cluster_size_Bytes)) {
				eprintf("EOF while reading from file\n");
				abort();
			}
			/*
			int r=read(fd,buffer,sc->cluster_size_Bytes);
			if (r!=(int)sc->cluster_size_Bytes) {
				ERRORF("%s: Couldn't read from file »%s«: %s.",sc->name,filename,strerror(errno));
				break;
			}
			*/
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
