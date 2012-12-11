#include <bttypes.h>

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
	// 0 means 128 byte inodes and 10 reserved inodes (1..10)
	// 1 means dynamic inode size (see field inode_size) and an arbitrary number of reserved inodes (see first_inode).
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
/*0FD*/	u8   reserved_char_pad;
/*0FE*/	u16  reserved_word_pad;
/*100*/	u32  default_mount_opts;
/*104*/	u32  first_meta_bg;                       // First metablock block group
/*108*/	u32  reserved[62];                        // Padding to the end of the block
/*200*/	// End of Super Block
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
#define RW_COMPAT_UNKNOWN       0xFFC0

#define RO_COMPAT_SPARSE_SUPER  0x0001
#define RO_COMPAT_LARGE_FILE    0x0002
#define RO_COMPAT_BTREE_DIR     0x0004
#define RO_COMPAT_UNKNOWN       0xFFF8

#define IN_COMPAT_COMPRESSION   0x0001 // There MAY be compressed files on the file system
#define IN_COMPAT_FILETYPE      0x0002 // Store file type in directory entry
#define IN_COMPAT_RECOVER       0x0004 // Needs recovery
#define IN_COMPAT_JOURNAL_DEV   0x0008 // Journal device
#define IN_COMPAT_META_BG       0x0010 // ???
#define IN_COMPAT_UNKNOWN       0xFFE0

#define	RW_COMPAT_SUPPORTED (RW_COMPAT_EXT_ATTR)
#define	IN_COMPAT_SUPPORTED (IN_COMPAT_FILETYPE|IN_COMPAT_RECOVER|IN_COMPAT_META_BG)
#define	RO_COMPAT_SUPPORTED (RO_COMPAT_SPARSE_SUPER|RO_COMPAT_LARGE_FILE|RO_COMPAT_BTREE_DIR)

#define	RW_COMPAT_UNSUPPORTED ~(RW_COMPAT_EXT_ATTR)
#define	IN_COMPAT_UNSUPPORTED ~(IN_COMPAT_FILETYPE|IN_COMPAT_RECOVER|IN_COMPAT_META_BG)
#define	RO_COMPAT_UNSUPPORTED ~(RO_COMPAT_SPARSE_SUPER|RO_COMPAT_LARGE_FILE|RO_COMPAT_BTREE_DIR)

/*
 * On-Disk structure of a blocks group descriptor
 */
struct group_desciptor {
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
/*012*/	u16 pad;
/*014*/	u32 reserved[3];
/*020*/	// End of Structure (32 bytes)
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
	 * Of course, this file SHOULD be num_clusters, but somebody[TM] 
	 * defined it differently. So, this field really contains the number 
	 * of hardware blocks.
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
#define	INOF_TOOLCHAIN           0x80000000 // This bit is reserved for the ext2 library. File system drivers should just ignore it and not modify it.

#define INOF_E2COMPR_FLAGS (INOF_E2COMPR_DIRTY|INOF_E2COMPR_COMPR|INOF_E2COMPR_HASCOMPRBLK|INOF_E2COMPR_NOCOMPR|INOF_E2COMPR_ERROR)

#define	INOF_USER_VISIBLE        0x0003DFFF // User visible flags
#define	INOF_USER_MODIFIABLE     0x000380FF // User modifiable flags
#define INOF_UNKNOWN_FLAGS       0x7FFC0000 // No unknown flag may be set. This mask here is ease checking this requirement.

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

#define	FTB_FILE  1
#define	FTB_DIRE  2
#define	FTB_CDEV  4
#define	FTB_BDEV  8
#define	FTB_FIFO 16
#define	FTB_SOCK 32
#define	FTB_SLNK 64

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

/* 0x020 */	u32                                   errno; // Error value, as set by journal_abort()
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
