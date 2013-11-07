/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#ifndef EXT2_H
#define EXT2_H

#include <bttypes.h>

#include "bdev.h"

#include "sc.h"

struct inode_scan_context;

#define MAX_SIZE_FAST_SYMBOLIC_LINK 60

/*
 * On-Disk structure of the super block
 */
struct super_block {
/*000*/	u32  num_inodes;                          // Inodes count
	u32  num_clusters;                        // Blocks count
	u32  num_reserved_clusters;               // Reserved blocks count
	u32  num_free_clusers;                    // Free blocks count
/*010*/	u32  num_free_inodes;                     // Free inodes count
	u32  first_data_cluster;                  // First Data Block
	u32  log_cluster_size;                    // Block size
	u32  log_frag_size;                       // Fragment size
/*020*/	u32  clusters_per_group;                  // # Blocks per group
	u32  frags_per_group;                     // # Fragments per group
	u32  inodes_per_group;                    // # Inodes per group
	u32  mtime;                               // Last mount time
/*030*/	u32  wtime;                               // Last write time
	u16  mount_count;                         // Mount count
	u16  max_mount_count;                     // Maximal mount count
	u16  magic;                               // Magic signature
	u16  state;                               // File system state
	u16  errors;                              // Behaviour when detecting errors
	u16  minor_rev_level;                     // minor revision level /* ignored by the ext2 driver */
/*040*/	u32  lastcheck_time;                      // time of last check
	u32  checkinterval;                       // max. time between checks
	u32  creator_os;                          // OS
	u32  rev_level;                           // Revision level
	// 0 means 128 byte inodes and 10 reserved inodes (1..10), no compat fields
	// 1 means dynamic inode size (see field inode_size) and an arbitrary number of reserved inodes (see first_inode) and compat fields
/*050*/	u16  reserved_access_uid;                 // Default uid for reserved blocks
	u16  reserved_access_gid;                 // Default gid for reserved blocks
	/*
	 * These fields are for EXT2_DYNAMIC_REV superblocks only.
	 *
	 * Note: the difference between the compatible feature set and
	 * the incompatible feature set is that if there is a bit set
	 * in the incompatible feature set that the kernel doesn't
	 * know about, it should refuse to mount the filesystem.
	 *
	 * e2fsck's requirements are more strict; if it doesn't know
	 * about a feature in either the compatible or incompatible
	 * feature set, it must abort and not try to meddle with
	 * things it doesn't understand...
	 */
	u32  first_inode;                         // First non-reserved inode
	u16  inode_size;                          // size of inode structure
	u16  my_group;                            // cluster group # of this superblock
	u32  rw_compat;                           // compatible feature set
/*060*/	u32  in_compat;                           // incompatible feature set
	u32  ro_compat;                           // readonly-compatible feature set
/*068*/	u8   uuid[16];                            // 128-bit uuid for volume
/*078*/	char volume_name[16];                     // volume name
/*088*/	char last_mount_point[64];                // directory where last mounted
/*0C8*/	u32  e2compr_algorithm_usage_bitmap;      // For compression
	/*
	 * Performance hints.  Directory preallocation should only
	 * happen if the EXT3_FEATURE_COMPAT_DIR_PREALLOC flag is on.
	 */
	u8   num_blocks_to_prealloc;              // Nr of blocks to try to preallocate
	u8   num_blocks_to_prealloc4dirs;         // Nr to preallocate for dirs
	u16  onlineresize2fs_reserved_gdt_blocks; // Per group desc for online growth
	/*
	 * Journaling support valid if EXT3_FEATURE_COMPAT_HAS_JOURNAL set.
	 */

	/*
	 * journal_uuid: uuid of journal superblock
	 *
	 * This is neccessarry, do make sure the given device really 
	 * contains the journal belonging to this file system and not 
	 * anything else (e.g. an ext2 journal to any other ext2 fs). 
	 * Obviously, this is really needed for external journals only.
	 */
/*0D0*/	u8   journal_uuid[16];

	/*
	 * journal_inode: It's possible to add a journal while the file system 
	 * is mounted. Technically this works like this:
	 *
	 * 1. A file - traditionally created in the root of this file system 
	 *    and called '.journal' - will be created and set to the later size 
	 *    of the journal.
	 * 2. An ioctl is done to that file (and hence to the file system 
	 *    driver) telling the driver to make this file the journal. That 
	 *    file's inode number will be recorded in this entry.
	 * 3. At any time when the file system is not mounted, a run of e2fsck 
	 *    moves the 128 byte inode date of that inode to JOURNAL_INO (8), 
	 *    marks the old inode as unused and removes the directory entry to 
	 *    the old journal inode.
	 */
/*0E0*/	u32  journal_inode;                       // inode number of journal file

	/*
	 * journal_dev: device number of journal file
	 *
	 * The problem with this field is that it is out of scope of 
	 * the file system's on disk structures, how the operation 
	 * system handles devices. It's the task of the OS to hand over 
	 * the device containing the journal to the file system driver.
	 *
	 * On the other hand, this field is only meant as a hint for the file 
	 * system driver, which device to open for the journal. The journal 
	 * has to be identified by the field journal_uuid which must be 
	 * identical to the journal's uuid field.
	 */
/*0E4*/	u32  journal_dev;
/*0E8*/	u32  last_orphanded_inode;                // start of list of inodes to delete
/*0EC*/	u32  hash_seed[4];                        // HTREE hash seed
/*0FC*/	u8   def_hash_version;                    // Default hash version to use
/*0FD*/	u8   jnl_backup_type;
/*0FE*/	u16  group_descriptor_table_entry_size;
/*100*/	u32  default_mount_opts;
/*104*/	u32  first_meta_bg;                       // First metablock block group
/*108*/	u32  mkfs_time;
/*10C*/	u32  jnl_blocks[17];
/*150*/	u32  blocks_count_hi;
/*154*/	u32  r_blocks_count_hi;
/*158*/	u32  free_blocks_count_hi;
/*15C*/	u16  min_extra_isize;
/*15E*/	u16  want_extra_isize;
/*160*/	u32  flags;
/*164*/	u16  raid_stride;
/*166*/	u16  mmp_update_interval;
/*168*/	u64  mmp_block;
/*170*/	u32  raid_stripe_width;
/*174*/	u8   log_groups_per_flex;
/*175*/	u8   checksum_type;
/*176*/	u16  reserved_pad;
/*178*/	u64  kbytes_written;
/*180*/	u32  snapshot_inum;
/*184*/	u32  snapshot_id;
/*188*/	u64  snapshot_r_blocks_count;
/*190*/	u32  snapshot_list;
/*194*/	u32  error_count;
/*198*/	u32  first_error_time;
/*19C*/	u32  first_error_ino;
/*1A0*/	u64  first_error_block;
/*1A8*/	u8   first_error_func[32];
/*1C8*/	u32  first_error_line;
/*1CC*/	u32  last_error_time;
/*1D0*/	u32  last_error_ino;
/*1D4*/	u32  last_error_line;
/*1D8*/	u64  last_error_block;
/*1E0*/	u8   last_error_func[32];
/*200*/	u8   mount_opts[64];
/*240*/	u32  usr_quota_inum;
/*244*/	u32  grp_quota_inum;
/*248*/	u32  overhead_clusters;
/*24C*/	u32  reserved[108];
/*3FC*/	u32  checksum;
/*400*/	// End of Super Block
};

#define MAGIC 0xEF53

#define	HAS_RW_COMPAT(sb,mask) (EXT3_SB(sb)->s_es->s_feature_compat    &   cpu_to_le32(mask))
#define	HAS_RO_COMPAT(sb,mask) (EXT3_SB(sb)->s_es->s_feature_ro_compat &   cpu_to_le32(mask))
#define	HAS_IN_COMPAT(sb,mask) (EXT3_SB(sb)->s_es->s_feature_incompat  &   cpu_to_le32(mask))
#define	SET_RW_COMPAT(sb,mask) (EXT3_SB(sb)->s_es->s_feature_compat    |=  cpu_to_le32(mask))
#define	SET_RO_COMPAT(sb,mask) (EXT3_SB(sb)->s_es->s_feature_ro_compat |=  cpu_to_le32(mask))
#define	SET_IN_COMPAT(sb,mask) (EXT3_SB(sb)->s_es->s_feature_incompat  |=  cpu_to_le32(mask))
#define	CLR_RW_COMPAT(sb,mask) (EXT3_SB(sb)->s_es->s_feature_compat    &= ~cpu_to_le32(mask))
#define	CLR_RO_COMPAT(sb,mask) (EXT3_SB(sb)->s_es->s_feature_ro_compat &= ~cpu_to_le32(mask))
#define	CLR_IN_COMPAT(sb,mask) (EXT3_SB(sb)->s_es->s_feature_incompat  &= ~cpu_to_le32(mask))

#define RW_COMPAT_DIR_PREALLOC  0x0001
#define RW_COMPAT_IMAGIC_INODES 0x0002
#define RW_COMPAT_HAS_JOURNAL   0x0004
#define RW_COMPAT_EXT_ATTR      0x0008
#define RW_COMPAT_RESIZE_INODE  0x0010
#define RW_COMPAT_DIR_INDEX     0x0020
#define RW_COMPAT_UNKNOWN_0040  0x0040
#define RW_COMPAT_UNKNOWN_0080  0x0080
#define RW_COMPAT_UNKNOWN_0100  0x0100
#define RW_COMPAT_UNKNOWN_0200  0x0200
#define RW_COMPAT_UNKNOWN_0400  0x0400
#define RW_COMPAT_UNKNOWN_0800  0x0800
#define RW_COMPAT_UNKNOWN_1000  0x1000
#define RW_COMPAT_UNKNOWN_2000  0x2000
#define RW_COMPAT_UNKNOWN_4000  0x4000
#define RW_COMPAT_UNKNOWN_8000  0x8000

#define RO_COMPAT_SPARSE_SUPER  0x0001
#define RO_COMPAT_LARGE_FILE    0x0002
#define RO_COMPAT_BTREE_DIR     0x0004
#define RO_COMPAT_HUGE_FILE     0x0008
#define RO_COMPAT_GDT_CSUM      0x0010
#define RO_COMPAT_DIR_NLINK     0x0020
#define RO_COMPAT_EXTRA_ISIZE   0x0040
#define RO_COMPAT_RESERVED_0080 0x0080
#define RO_COMPAT_QUOTA         0x0100
#define RO_COMPAT_BIGALLOC      0x0200
#define RO_COMPAT_METADATA_CSUM 0x0400
#define RO_COMPAT_UNKNOWN_0800  0x0800
#define RO_COMPAT_UNKNOWN_1000  0x1000
#define RO_COMPAT_UNKNOWN_2000  0x2000
#define RO_COMPAT_UNKNOWN_4000  0x4000
#define RO_COMPAT_UNKNOWN_8000  0x8000

#define IN_COMPAT_COMPRESSION      0x0001 // There MAY be compressed files on the file system
#define IN_COMPAT_FILETYPE         0x0002 // Store file type in directory entry
#define IN_COMPAT_RECOVER          0x0004 // Needs recovery
#define IN_COMPAT_JOURNAL_DEV      0x0008 // Journal device
#define IN_COMPAT_META_BG          0x0010 // ???
#define IN_COMPAT_RESERVED_0080    0x0020 // ??? used in experimental ext4 driver code?
#define IN_COMPAT_EXTENTS          0x0040 // Use extents list instead of indirect clusters
#define IN_COMPAT_64BIT            0x0080 // Besides others, gdt entries are (alt least) 64 bytes instead of 32 bytes long
#define IN_COMPAT_MMP              0x0100 // Multiple mount protection is in place
#define IN_COMPAT_FLEX_BG          0x0200 // RW_COMPAT reature: Drop the requirement, that group meta data must reside in group.
#define IN_COMPAT_EA_INODE         0x0400 // EAs in inode
#define IN_COMPAT_RESERVED_0800    0x0800 // ??? used in experimental ext4 driver code?
#define IN_COMPAT_DIRDATA          0x1000 // ??? (data in dirent - whatever that means)
#define IN_COMPAT_BG_USE_META_CSUM 0x2000 // Calculate checksums for cluster and inode bitmap
#define IN_COMPAT_LARGEDIR         0x4000 // ??? (>2GB or 3-lvl htree - whatever that means exactly)
#define IN_COMPAT_INLINEDATA       0x8000 // ??? (data in inode - whatever that means exactly)

/*
 * METADATA_CSUM also enables group descriptor checksums (GDT_CSUM).  When
 * METADATA_CSUM is set, group descriptor checksums use the same algorithm as
 * all other data structures' checksums.  However, the METADATA_CSUM and
 * GDT_CSUM bits are mutually exclusive.
 */
#define EXT4_FEATURE_RO_COMPAT_METADATA_CSUM 0x0400

#define RW_COMPAT_SUPPORTED (RW_COMPAT_EXT_ATTR)
#define IN_COMPAT_SUPPORTED (IN_COMPAT_FILETYPE|IN_COMPAT_RECOVER|IN_COMPAT_META_BG|IN_COMPAT_64BIT)
#define RO_COMPAT_SUPPORTED (RO_COMPAT_SPARSE_SUPER|RO_COMPAT_LARGE_FILE|RO_COMPAT_BTREE_DIR)

#define RW_COMPAT_UNSUPPORTED ~RW_COMPAT_SUPPORTED
#define IN_COMPAT_UNSUPPORTED ~IN_COMPAT_SUPPORTED
#define RO_COMPAT_UNSUPPORTED ~RO_COMPAT_SUPPORTED

/*
 * On-Disk structure of a blocks group descriptor
 */
struct group_desciptor_v1 {
/*000*/	u32 cluster_allocation_map; // Blocks bitmap block */
/*004*/	u32 inode_allocation_map;   // Inodes bitmap block */
/*008*/	u32 inode_table;            // Inodes table block */
	/*
	 * num_free_clusters
	 *
	 * This field is EVIL. Especially evil is, that this field is 16 bit 
	 * wide! This limits the cluster size to 8192 bytes (8192*8 -> 65536 
	 * clusters). However, one could define, 65535 means "at least 65535 
	 * clusters free", so the free block finder has then to read the 
	 * cluster allocation map and count the free clusters on it's own.
	 * Alternatively, this value could be extended using 8 or 16 bits of 
	 * padding or reserved.
	 */
/*00C*/	u16 num_free_clusters;      // Free blocks count
/*00E*/	u16 num_free_inodes;        // Free inodes count
/*010*/	u16 num_directories;        // Directories count
/*012*/	u16 flags;
/*014*/	u32 snapshot_exclude_bitmap;
/*018*/	u16 cluster_allocation_map_csum;
/*01A*/	u16 inode_allocation_map_csum;
/*01C*/	u16 num_virgin_inodes;
/*01E*/	u16 csum;
/*020*/	// End of Structure (32 bytes)
};

#define BG_INODE_ALLOCATION_MAP_UNINIT   0x0001 /* Inode table/bitmap not in use */
#define BG_CLUSTER_ALLOCATION_MAP_UNINIT 0x0002 /* Block bitmap not in use */
#define BG_INODE_TABLE_INITIALIZED       0x0004 /* On-disk itable initialized to zero */
#define BG_UNKNOWN_0008                  0x0008
#define BG_UNKNOWN_0010                  0x0010
#define BG_UNKNOWN_0020                  0x0020
#define BG_UNKNOWN_0040                  0x0040
#define BG_UNKNOWN_0080                  0x0080
#define BG_UNKNOWN_0100                  0x0100
#define BG_UNKNOWN_0200                  0x0200
#define BG_UNKNOWN_0400                  0x0400
#define BG_UNKNOWN_0800                  0x0800
#define BG_UNKNOWN_1000                  0x1000
#define BG_UNKNOWN_2000                  0x2000
#define BG_UNKNOWN_4000                  0x4000
#define BG_UNKNOWN_8000                  0x8000

struct group_desciptor_v2 {
/*000*/	u32 cluster_allocation_map_lo; // Blocks bitmap block */
/*004*/	u32 inode_allocation_map_lo;   // Inodes bitmap block */
/*008*/	u32 inode_table_lo;            // Inodes table block */
/*00C*/	u16 num_free_clusters_lo;      // Free blocks count
/*00E*/	u16 num_free_inodes_lo;        // Free inodes count
/*010*/	u16 num_directories_lo;        // Directories count
/*012*/	u16 flags;
/*014*/	u32 snapshot_exclude_bitmap_lo;
/*018*/	u16 cluster_allocation_map_csum_lo;
/*01A*/	u16 inode_allocation_map_csum_lo;
/*01C*/	u16 num_virgin_inodes_lo;
/*01E*/	u16 csum;
/*020*/	u32 cluster_allocation_map_hi;
/*024*/	u32 inode_allocation_map_hi;
/*028*/	u32 inode_table_hi;
/*02C*/	u16 num_free_clusters_hi;
/*02E*/	u16 num_free_inodes_hi;
/*030*/	u16 num_directories_hi;
/*032*/	u16 num_virgin_inodes_hi;
/*034*/	u32 snapshot_exclude_bitmap_hi;
/*038*/	u16 cluster_allocation_map_csum_hi;
/*03A*/	u16 inode_allocation_map_csum_hi;
/*03C*/	u32 reserved;
/*040*/	// End of Structure (64 bytes)
};

struct group_desciptor_in_memory {
	u64 cluster_allocation_map;
	u64 inode_allocation_map;
	u64 inode_table;
	u64 snapshot_exclude_bitmap;
	
	u32 num_free_clusters;
	u32 num_free_inodes;
	u32 num_directories;
	u32 cluster_allocation_map_csum;
	u32 inode_allocation_map_csum;
	u32 num_virgin_inodes;
	u32 reserved;
	
	u16 csum;
	u16 flags;
};

#define NUM_ZIND 12 // zero    indirect -> direct
#define NUM_SIND  1 // single  indirect
#define NUM_DIND  1 // double  indirect
#define NUM_TIND  1 // tripple indirect 

#define NUM_CLUSTER_POINTERS 15

#define FIRST_ZIND  0
#define FIRST_SIND 12
#define FIRST_DIND 13
#define FIRST_TIND 14

#define NUM_CLUSTER_POINTERS_USED_BY_SPECIAL_INODES 4

/*
 * On-Disk structure of an inode
 */
struct inode {
	/*
	 * mode: Standard unix access flags
	 *
	 * This field is best thought as being an octal value with the 
	 * following digits representing different access criterias:
	 *
	 * .....u: Flags are valid for accessor with UID == owner's UID
	 * ....g.: Flags are valid for GID == owner's GID && UID != owner's UID
	 * ...o..: Flags are valid for GID != owner's GID && UID != owner's UID
	 *
	 *	With X being a bitmaks of three bits:
	 *
	 *	100 (r) -> permission to open this object for reading (listing 
	 *	           a directory needs this flag as well)
	 *	010 (w) -> permission to open this object for writing 
	 *	           (creating, renaming or removing a file needs this 
	 *	           flag set in every directory affected by that 
	 *	           operation)
	 *	001 (x) -> permission to execute the program (or to ENTER the 
	 *	           directory)
	 *
	 * ..X...: Special type flags to the file:
	 *
	 *	100 (t) -> For executable files: Load the entire executable 
	 *	           image on execution (and don't page it in on request)
	 *	010 (s) -> For executable files: Run the process under the 
	 *	           file's group ID.
	 *	           For directories: Newly created files and directories 
	 *	           will have their owner's GID set to the owner's GID 
	 *	           of this directory. Directories also inherit this 
	 *	           flag (so they behave like this directory).
	 *	001 (s) -> For executable files: Run the process under the 
	 *	           file's user ID.
	 *	           For directories: Newly created files and directories 
	 *	           will have their owner's UID set to the owner's UID 
	 *	           of this directory. Directories also inherit this 
	 *	           flag (so they behave like this directory).
	 */
/*000*/	u16 mode;                          // access flags.
/*002*/	u16 uid;                           // Owner's UID (don't forget uid_high)
/*004*/	u32 size;                          // Size in bytes
	/*
	 * atime: Access time
	 *
	 * This field will be set to current system time every time the file 
	 * CONTENT is being read. Note however, that using mmap does not HAVE 
	 * TO (but MAY) update this field. If the flag INOF_NOATIME is set in 
	 * flags (or the file system was globally mounted with a similar flag) 
	 * atime will NOT be updated (this is to prevent unneccessary write 
	 * operations.
	 */
/*008*/	u32 atime;                         // Access time
	/*
	 * The files ctime and mtime need some explanation:
	 *
	 * mtime refers to modifications to the file CONTENT. This means: 
	 * Writing to a file, creating a directory etc all causes mtime to be 
	 * set to the current system time.
	 *
	 * ctime however (many times misinterpreted as create time) refers to 
	 * modifications to the META DATA of the file. This means: chown, ln 
	 * and so on all set ctime to the current system time. However, 
	 * writing to the file CONTENT modifies mtime, which is META DATA, so 
	 * writing to the file ALSO updates ctime. (Rule: No change of mtime 
	 * without a change to ctime).
	 */
/*00C*/	u32 ctime;                         // Change time
/*010*/	u32 mtime;                         // Modification time
	/*
	 * dtime: Delete time
	 *
	 * If the inode is deleted, this field contains the UNIX time of 
	 * deletion. Note however, that this field will be used for the orphan 
	 * list, if the file system flag IN_COMPAT_RECOVER is set.
	 */
/*014*/	u32 dtime;                         // Deletion time
/*018*/	u16 gid;                           // Owner's GID (don't forget gid_high)
/*01A*/	u16 links_count;                   // Links count (number of directory entries pointing to this entry)
	/*
	 * num_blocks: Num BLOCKS. Really!
	 *
	 * On ext2, clusters are wrongly called "blocks", so creating a 
	 * disambiguity between blocks and clusters. We use consequently the 
	 * term cluster where it belongs, and blocks, where it belongs.
	 * Of course, this field SHOULD be num_clusters, but somebody[TM] 
	 * defined it differently. So, this field really contains the number 
	 * of (hardware) blocks.
	 */
/*01C*/	u32 num_blocks;                    // Number of ALLOCATED clusters.
/*020*/	u32 flags;                         // File flags
/*024*/	u32 translator;                    // Only used on HURD (which will be released next year, btw. ;)
/*028*/	u32 cluster[NUM_CLUSTER_POINTERS]; // Pointers to clusters
#if 0
/*028*/	// u32 block[NUM_ZIND+NUM_SIND+NUM_DIND+NUM_TIND];

/*028*/	u32 zind[NUM_ZIND];
/*058*/	u32 sind[NUM_SIND];
/*05C*/	u32 dind[NUM_DIND];
/*060*/	u32 tind[NUM_TIND];
#endif
/*064*/	u32 generation;                    // File version (for NFS)
/*068*/	u32 file_acl;                      // File ACL
	/*
	 * size_high: High 32 bit word of u64 size field.
	 *
	 * This 32 bits extend the 32 bit value "size" to 64 bit by providing 
	 * bits 32..63 of size. However, on directories, this field holds the 
	 * field dir_acl. (So, obviously directories can't be >4GB).
	 */
/*06C*/	u32 size_high;
	/*
	 * Another strange thing: Fragments. A feature
	 *
	 * a) nobody knows, what it is at all
	 * b) nobody cares about
	 * c) nobody implemented yet at all
	 *
	 * I don't know, why this feature got values in the inode at all ...
	 */
/*070*/	u32 faddr;                         // Fragment address
/*074*/	u8  frag;                          // Fragment number
/*075*/	u8  fsize;                         // Fragment size
/*076*/	u16 mode_high;                     // Only used on HURD (however, maybe it won't be released next year, how knows ...)
/*078*/	u16 uid_high;                      // This field extends the field uid by 16 bit to a 32 bit UID (not used on masix)
/*07A*/	u16 gid_high;                      // This field extends the field gid by 16 bit to a 32 bit GID (not used on masix)
/*07C*/	u32 author;                        // Only used on HURD (eventually it won't be released at all any time)
#if 0
/*080*/	u16 size_48;                       // ???
/*082*/	u16 pad1;
/*084*/	// End of Structure
#endif
};

#define EXTENT_HEADER_MAGIC 0xf30a
struct extent_header {
	u16 magic; // May be needed in future to distinct different extent storage formats
	u16 num_entries; // Number of following extent entries - either all extent_descriptor (depth == 0) or all extent_index (depth > 0).
	u16 max_entries; // Max number of following extent entries - actually this value is used to compute the offset of the checksum field (extent_tail)
	u16 depth; // The depth until we reach extent_descriptor (until then, we have to deal with extent_index structures.
	u32 generation; // generation of the tree - whatever that means
};

/*
 * Assume a file with 3 extents:
 * The first 5 clusters of the file are stored in 4711 to 4715 on disc. The extent for that could be initialized this way:
 * struct extent_descriptor first = { 0, 5, 0, 4711 };
 * The following 12 clusters are stored at clusters 2342 to 2353, so the next extent looks like this:
 * struct extent_descriptor second = { 5, 12, 0, 2342 };
 * The third extent is 3 clusters stored at 123 to 125:
 * struct extent_descriptor third = { 17, 3, 0, 123 };
 */
struct extent_descriptor {
	u32 file_cluster; // The extent covered by this descriptor represents the file data starting at the offset file_cluster in the file
	u16 len; // Number of clusters covered by this descriptor.
	u16 disk_cluster_hi; // The uppermost 16 bits of the 48 bit cluster number of the first block of this extent in the data area
	u32 disk_cluster_lo; // And the lower 32 bits.
};

struct extent_index {
	u32 file_cluster; // The extents covered by this index represents the file data starting at the offset file_cluster in the file
	u32 leaf_lo; // The lower 32 bits of the cluster number where either the next extent index table or the next extent descriptor table can be found.
	u16 leaf_hi; // And the higher 16 bits.
	u16 unused;
};

struct extent_tail {
	u32 checksum; // Stored 
};

// See comment for entry size_high in struct inode
#define dir_acl size_high

// See comment for entry dtime in struct inode
#define orphand_next dtime

/*
 * Special inodes numbers.
 */
#define BAD_INO          1 // Bad blocks inode
#define ROOT_INO         2 // Root inode
#define BOOT_LOADER_INO  5 // Boot loader inode
#define UNDEL_DIR_INO    6 // Undelete directory inode
#define RESIZE_INO       7 // Reserved group descriptors inode
#define JOURNAL_INO      8 // Journal inode

/*
 * First non-reserved inode for old ext2 filesystems
 */
#define PRE_DYNAMIC_FIRST_INODE 11

/*
 * Inode flags
 */
#define	INOF_SECURE_REMOVE       0x00000001 // Secure deletion
#define	INOF_UNRM                0x00000002 // Undelete
#define	INOF_E2COMPR_COMPR       0x00000004 // File should be compressed
#define	INOF_SYNC                0x00000008 // Synchronous updates
#define	INOF_IMMUTABLE           0x00000010 // File can't be modified (regardeless of the values in mode)
#define	INOF_APPEND_ONLY         0x00000020 // writes to file may only append
#define	INOF_NODUMP              0x00000040 // do not dump file
#define	INOF_NOATIME             0x00000080 // do not update atime
#define	INOF_E2COMPR_DIRTY       0x00000100 // The file has dirty blocks?
#define	INOF_E2COMPR_HASCOMPRBLK 0x00000200 // The file has at least one compressed chunk
#define	INOF_E2COMPR_NOCOMPR     0x00000400 // This file may not be compressed
#define	INOF_E2COMPR_ERROR       0x00000800 // This file contains at least one block which can't be decompressed
#define	INOF_INDEX               0x00001000 // hash-indexed directory
#define	INOF_IMAGIC              0x00002000 // AFS directory
#define	INOF_JOURNAL_DATA        0x00004000 // file data should be journaled
#define	INOF_NOTAIL              0x00008000 // file tail should not be merged
#define	INOF_DIRSYNC             0x00010000 // dirsync behaviour (directories only)
#define	INOF_TOPDIR              0x00020000 // Top of directory hierarchies
#define INOF_HUGE_FILE           0x00040000
#define INOF_EXTENTS             0x00080000 // The data associated to this inode is recorded in extents instead of indirect clusters
#define INOF_RESERVED_00100000   0x00100000
#define INOF_EA_INODE            0x00200000 // Inode used for a large extended attribute (whatever that means)
#define INOF_EOFBLOCKS           0x00400000 // This file has blocks allocated past EOF (for what reason?).
#define INOF_UNKNOWN_00800000    0x00800000
#define INOF_UNKNOWN_01000000    0x01000000
#define INOF_UNKNOWN_02000000    0x02000000
#define INOF_UNKNOWN_04000000    0x04000000
#define INOF_UNKNOWN_08000000    0x08000000
#define INOF_UNKNOWN_10000000    0x10000000
#define INOF_UNKNOWN_20000000    0x20000000
#define INOF_UNKNOWN_40000000    0x40000000
#define	INOF_TOOLCHAIN           0x80000000 // This bit is reserved for the ext2 library. File system drivers should just ignore it and not modify it.

#define INOF_E2COMPR_FLAGS (INOF_E2COMPR_DIRTY|INOF_E2COMPR_COMPR|INOF_E2COMPR_HASCOMPRBLK|INOF_E2COMPR_NOCOMPR|INOF_E2COMPR_ERROR)

#define	INOF_USER_VISIBLE        0x0003DFFF // User visible flags
#define	INOF_USER_MODIFIABLE     0x000380FF // User modifiable flags

/*
 * Inode dynamic state flags
 */
#define	INOS_JDATA               0x00000001 // journaled data exists
#define	INOS_NEW                 0x00000002 // inode is newly created
#define	INOS_XATTR               0x00000004 // has in-inode xattrs

#define	FT_UKNW 0
#define	FT_FILE 1
#define	FT_DIRE 2
#define	FT_CDEV 3
#define	FT_BDEV 4
#define	FT_FIFO 5
#define	FT_SOCK 6
#define	FT_SLNK 7
// #define	EXT2_FT_MAX      8
// The following values are used internally only. Currently, ext2 only uses 7 
// types. As long as this doesn't increase to 252 or more, we're fine ;)
#define FT_EIO  252
#define FT_DELE 253
#define FT_FREE 254
#define FT_ILLT 255

#define	FTB_FILE  1
#define	FTB_DIRE  2
#define	FTB_CDEV  4
#define	FTB_BDEV  8
#define	FTB_FIFO 16
#define	FTB_SOCK 32
#define	FTB_SLNK 64

/*
#define FTB_EIO  128
#define FTB_DELE 256
#define FTB_FREE 512
#define FTB_ILLT 1024
*/

/*
 * Hint: Octal values, not hexadecimal ;)
 */
#define	MF_MASK 0170000
#define	MF_BDEV 0060000
#define	MF_CDEV 0020000
#define	MF_DIRE 0040000
#define	MF_FIFO 0010000
#define	MF_SLNK 0120000
#define	MF_FILE 0100000
#define	MF_SOCK 0140000

#define	ISBDEV(inode) (((inode)->mode&MF_MASK)==MF_BDEV)
#define	ISCDEV(inode) (((inode)->mode&MF_MASK)==MF_CDEV)
#define	ISDIRE(inode) (((inode)->mode&MF_MASK)==MF_DIRE)
#define	ISFIFO(inode) (((inode)->mode&MF_MASK)==MF_FIFO)
#define	ISSLNK(inode) (((inode)->mode&MF_MASK)==MF_SLNK)
#define	ISFILE(inode) (((inode)->mode&MF_MASK)==MF_FILE)
#define	ISSOCK(inode) (((inode)->mode&MF_MASK)==MF_SOCK)

#define MASK_FT(inode) ((inode)->mode&MF_MASK)

#define JFS_MAGIC_NUMBER 0xc03b3998U /* The first 4 bytes of /dev/random! */

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
/* 0x000 */	struct ext2_journal_descriptor_header header;
		// Static information describing the journal
/* 0x00c */	u32                                   block_size; // Journal device cluster size
/* 0x010 */	u32                                   max_len; // Total clusters in journal (file)
/* 0x014 */	u32                                   first; // First block of log information
		// Dynamic information describing the current state of the log
/* 0x018 */	u32                                   sequence; // first commit ID expected in log
/* 0x01c */	u32                                   start; // cluster number of start of log

/* 0x020 */	u32                                   error; // Error value, as set by journal_abort()
		// Remaining fields are only valid in a version-2 superblock
/* 0x024 */	u32                                   feature_rwcompat; // rw compatible feature set
/* 0x028 */	u32                                   feature_incompat; // incompatible feature set
/* 0x02c */	u32                                   feature_rocompat; // ro compatible feature set
/* 0x030 */	u8                                    uuid[16]; // 128-bit uuid for journal
/* 0x040 */	u32                                   nr_users; // Number of file systems sharing this log
/* 0x044 */	u32                                   dynsuper; // Blocknr of dynamic superblock copy
/* 0x048 */	u32                                   max_transaction_j_size; // Limit of journal blocks per transaction
/* 0x04c */	u32                                   max_transaction_d_size; // Limit of data blocks per transaction
/* 0x050 */	u32                                   padding[44]; // Unused
/* 0x100 */	u8                                    users[16*48]; // UUID of every FS sharing this log
/* 0x400 */
};

#define fv (j)->j_format_version
#define jsb (j)->j_superblock
#define JFS_HAS_RWCOMPAT_FEATURE(j,mask) (fv>=2 && (jsb->s_feature_rwcompat & cpu_to_be32((mask))))

#define JFS_FEATURE_INCOMPAT_REVOKE 0x00000001

#define JFS_KNOWN_RW_COMPAT_FEATURES 0
#define JFS_KNOWN_RO_COMPAT_FEATURES 0
#define JFS_KNOWN_IN_COMPAT_FEATURES JFS_FEATURE_INCOMPAT_REVOKE

bool read_super(struct scan_context * sc, struct super_block * sb, unsigned long group, block_t offset, bool probing);
bool group_has_super_block(const struct super_block * sb,unsigned group);
void write_super_blocks(struct super_block * sb);
bool read_table_for_one_group(struct scan_context * sc,bool prefetching);
void typecheck_inode(struct inode_scan_context * isc,struct inode * inode);
bool check_inode(struct scan_context * sc,unsigned long inode_num,bool prefetching,bool expected_to_be_free);

static inline unsigned long get_inodes_cluster(struct scan_context * sc,unsigned long inode_num) {
	return
		sc->gdt[(inode_num-1)/sc->sb->inodes_per_group].inode_table
		+((inode_num-1)%sc->sb->inodes_per_group)*128/sc->cluster_size_Bytes
	;
}

#define EXT2_ERRORF_DIR(isc,filename,fmt,...) do { \
	ERRORF( \
		"%s: Inode %lu (cluster %lu) [%s] (%s): "fmt \
		,isc->sc->name \
		,isc->inode_num \
		,get_inodes_cluster(isc->sc,isc->inode_num) \
		,isc->type \
		,filename \
		,__VA_ARGS__ \
	); \
} while (0)
#define EXT2_ERROR_DIR(isc,filename,msg) EXT2_ERRORF_DIR(isc,filename,"%s",msg)

#define EXT2_ERRORF(isc,fmt,...) do { \
	ERRORF( \
		"%s: Inode %lu (cluster %lu) [%s]: "fmt \
		,isc->sc->name \
		,isc->inode_num \
		,get_inodes_cluster(isc->sc,isc->inode_num) \
		,isc->type \
		,__VA_ARGS__ \
	); \
} while (0)
#define EXT2_ERROR(isc,msg) EXT2_ERRORF(isc,"%s",msg)

#define EXT2_NOTIFYF_DIR(isc,filename,fmt,...) do { \
	NOTIFYF( \
		"%s: Inode %lu (cluster %lu) [%s] (%s): "fmt \
		,isc->sc->name \
		,isc->inode_num \
		,get_inodes_cluster(isc->sc,isc->inode_num) \
		,isc->type \
		,filename \
		,__VA_ARGS__ \
	); \
} while (0)
#define EXT2_NOTIFY_DIR(isc,filename,msg) EXT2_NOTIFYF_DIR(isc,filename,"%s",msg)

#define EXT2_NOTIFYF(isc,fmt,...) do { \
	NOTIFYF( \
		"%s: Inode %lu (cluster %lu) [%s]: "fmt \
		,isc->sc->name \
		,isc->inode_num \
		,get_inodes_cluster(isc->sc,isc->inode_num) \
		,isc->type \
		,__VA_ARGS__ \
	); \
} while (0)
#define EXT2_NOTIFY(isc,msg) EXT2_NOTIFYF(isc,"%s",msg)

void ext2_dump_file_by_path(struct scan_context * sc, const char * path, const char * filename);
bool ext2_dump_file_by_inode(struct scan_context * sc, u32 inode_num, const char * path, const char * filename_msg);
void ext2_redump_file(struct scan_context * sc, const char * path, const char * filename);

#endif // #ifndef EXT2_H
