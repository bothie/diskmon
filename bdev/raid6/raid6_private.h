#include "raid6.h"

#define NEED_LONG_LONG

#include <bttypes.h>

/*
 * Shamelessly stolen from mdadm:
 *
 * The version-1 superblock :
 * All numeric fields are little-endian.
 *
 * total size: 256 bytes plus 2 per device.
 *  1K allows 384 devices.
 */
struct softraid_superblock_1 {
	// constant array information - 128 bytes
	u32 magic;             // MD_SB_MAGIC: 0xa92b4efc - little endian
	u32 major_version;     // 1 (constant) */
	u32 feature_map;       // 0 for now
	u32 pad0;              // 0 (unused, always set to 0 when writing)
	
	u8  set_uuid[16];      // UUID of this (FIXME: ARRAY or COMPONENT?)
	u8  set_name[32];      // set and interpreted by user-space
	
	u64 ctime;             // creation time (low 40 bits are seconds, upper 24 are microseconds or 0) */
	u32 level;             // 0,1,4,5,6,0xfffffffc (multipath) or 0xffffffff (linear)
	u32 layout;            // only for raid5 currently (FIXME: RAID 6?)
	u64 size;              // Used size of component devices in blocks
	
	u32 chunksize;         // in blocks
	u32 raid_disks;        // Number of disks (data AND parity)
	s32 bitmap_offset;     // relative block number of bitmap after super block.
	
	/*
	 * The field bitmap_offset is SIGNED, so the bitmap may lay BEFORE the superblock.
	 *
	 * This field is only valid, if feature flag MD_FEATURE_BITMAP_OFFSET is set.
	 */
	
	/*
	 * This fields are only valid, if feature flag MD_FEATURE_RESHAPE_ACTIVE is set.
	 */
	struct {
		u32 new_level;        // new level we are reshaping to
		u64 reshape_position; // next address in array-space for reshape
		u32 delta_disks;      // change in number of raid_disks
		u32 new_layout;       // new layout
		u32 new_chunk;        // new chunk size (bytes)
		u8  pad1[128-124];    // 0 (unused, always set to 0 when writing)
	} reshape;
	
	/*
	 * Information about this device, which are not generally subject to 
	 * change. (64 bytes)
	 */
	u64 data_offset;        // first data block (in most cases just 0)
	u64 data_size;          // num blocks in this device used for data
	u64 super_offset;       // block number of this super block
	u64 recovery_offset;    // blocks before this offset (relative to data_offset) have been recovered already */
	u32 dev_number;         // permanent identifier of this device (this is NOT the role in the raid)
	u32 cnt_corrected_read; // Number of read errors already corrected by re-writing the data
	u8  device_uuid[16];    // user-space setable, ignored by kernel */
	u8  devflags;           // per-device flags. Only one defined ...
	u8  pad2[64-57];        // 0 (unused, always set to 0 when writing)
	
	// array state information - 64 bytes
	u64 utime;              // update time (like ctime)
	u64 events;             // incremented each time when superblock updated
	u64 resync_offset;      // data before this offset (relative to data_offset) are known to be in sync
	u32 chksum;             // checksum upto dev_roles[max_dev].
	/*
	 * While calculating chksum, it is assumed to be zero.
	 *
	 * The checksumming function is: hi32(sum64("32-bit-words"))+lo32(sum64("32-bit-words"))
	 */
	u32 max_dev;            // size of dev_roles array to consider
	u8  pad3[64-32];        // 0 (unused, always set to 0 when writing)
	
	/*
	 * device state information. Indexed by dev_number.
	 * 2 bytes per device
	 * Note there are no per-device state flags. State information is rolled
	 * into the 'roles' value. If a device is spare or faulty, then it doesn't
	 * have a meaningful role.
	 */
	u16 dev_roles[0];	/* role in array, or 0xffff for a spare, or 0xfffe for faulty */
};

#define MD_MAGIC 0xa92b4efc

// devflags:
#define MD_DF_WRITEMOSTLY 1

// feature_map:
#define MD_FM_BITMAP_OFFSET 1 // We have a write intent bitmap
#define MD_FM_RECOVERY_OFFSET 2 // recovery_offset is present and must be honoured
#define MD_FM_RESHAPE_ACTIVE 4 // We are currently reshaping

#define	MD_FEATURE_ALL (8-1)

struct softraid_superblock_0_device_descriptor {
	u32 number;    // 0 Device number in the entire set
	u32 major;     // 1 Device major number
	u32 minor;     // 2 Device minor number
	u32 raid_disk; // 3 The role of the device in the raid set
	u32 state;     // 4 Operational state
	u32 reserved[32-5];
};

/*
 * Device "operational" state bits
 */
#define MD_DISK_FAULTY_SHIFT 0 // disk is faulty / operational
#define MD_DISK_ACTIVE_SHIFT 1 // disk is running or spare disk
#define MD_DISK_SYNC_SHIFT 2 // disk is in sync with the raid set
#define MD_DISK_REMOVED_SHIFT 3 // disk is in sync with the raid set

#define MD_DISK_WRITEMOSTLY_SHIFT 9 // disk is "write-mostly" is RAID1 config. Read requests will only be sent here if needed

#define MD_DISK_FAULTY      (1U<<MD_DISK_FAULTY_SHIFT)
#define MD_DISK_ACTIVE      (1U<<MD_DISK_ACTIVE_SHIFT)
#define MD_DISK_SYNC        (1U<<MD_DISK_SYNC_SHIFT)
#define MD_DISK_REMOVED     (1U<<MD_DISK_REMOVED_SHIFT)

#define MD_DISK_FAULTY_REMOVED (MD_DISK_FAULTY|MD_DISK_REMOVED)

#define MD_DISK_WRITEMOSTLY (1U<<MD_DISK_WRITEMOSTLY_SHIFT) // disk is "write-mostly" is RAID1 config. Read requests will only be sent here if needed

/*
 * Superblock state bits
 */
#define MD_SB_CLEAN 0
#define MD_SB_ERRORS 1

#define MD_SB_BITMAP_PRESENT 8 // bitmap may be present nearby

#define MD_SB_DISKS 27
struct softraid_superblock_0 {
	/*
	 * Constant generic information
	 */
	u32 magic;            //  0 MD identifier
	u32 major_version;    //  1 =0 major version to which the set conforms
	u32 minor_version;    //  2 =90 minor version ...
	u32 patch_version;    //  3 =0 patchlevel version ...
	u32 gvalid_words;     //  4 =? Number of used words in this section
	u32 uuid0;            //  5 UUID part 1 of 4
	u32 ctime;            //  6 Creation time
	u32 sb0_level;            //  7 Raid personality (0,1,4,5,6)
	u32 sb0_size;             //  8 Used size of the disks in KB!!! (so it's size/2, if size is blocks)
	u32 sb0_nr_disks;         //  9 total disks in the raid set (including spares and non-removed failed disks)
	u32 sb0_raid_disks;       // 10 disks in a fully functional raid set
	u32 md_minor;         // 11 preferred MD minor device number
	u32 not_persistent;   // 12 does it have a persistent superblock <--- was hat der on-disk zu suchen?
	u32 uuid1;            // 13 UUID part 2 of 4
	u32 uuid2;            // 14 UUID part 3 of 4
	u32 uuid3;            // 15 UUID part 4 of 4
	u32 gstate_creserved[32-16];
	
	/*
	 * Generic state information
	 */
	u32 utime;            //  0 Superblock update time
	u32 state;            //  1 State bits (clean, ...)
	u32 active_disks;     //  2 Number of currently active disks
	u32 working_disks;    //  3 Number of working disks
	u32 failed_disks;     //  4 Number of failed disks
	u32 spare_disks;      //  5 Number of spare disks
	u32 chksum;           //  6 checksum of the whole superblock
	u64 events;           //  7 superblock update count
	u64 cp_events;        //  9 checkpoint update count
	u32 recovery_cp;      // 11 recovery checkpoint block number
	// There are only valid for minor_version > 90
	u64 reshape_position; // 12 next address in array-space for reshape
	u32 new_level;        // 14 new level we are reshaping to
	u32 delta_disks;      // 15 change in number of raid_disks
	u32 new_layout;       // 16 new layout
	u32 new_chunk;        // 17 new chunk size (bytes)
	u32 gstate_sreserved[32-18];
	
	/*
	 * Personality information
	 */
	u32 sb0_layout;           //  0 the array's physical layout
	u32 sb0_chunk_size;       //  1 chunk size in bytes
	u32 root_pv;          //  2 LV root PV
	u32 root_block;       //  3 LV root block
	u32 pstate_reserved[64-4];
	
	/*
	 * Disks information
	 */
	struct softraid_superblock_0_device_descriptor disks[MD_SB_DISKS];
	
	/*
	 * Reserved
	 */
//	u32 reserved[MD_SB_RESERVED_WORDS];
	
	/*
	 * Active descriptor
	 */
	struct softraid_superblock_0_device_descriptor this_disk;
};

// #define MD_SB_GENERIC_CONSTANT_WORDS	32
// #define MD_SB_GENERIC_STATE_WORDS	32
// #define MD_SB_PERSONALITY_WORDS		64
// #define MD_SB_DESCRIPTOR_WORDS		32


/*
 * RAID superblock.
 *
 * The RAID superblock maintains some statistics on each RAID configuration.
 * Each real device in the RAID set contains it near the end of the device.
 * Some of the ideas are copied from the ext2fs implementation.
 *
 * We currently use 4096 bytes as follows:
 *
 *	word offset	function
 *
 *	   0  -    31	Constant generic RAID device information.
 *        32  -    63   Generic state information.
 *	  64  -   127	Personality specific information.
 *	 128  -   511	12 32-words descriptors of the disks in the raid set.
 *	 512  -   911	Reserved.
 *	 912  -  1023	Disk specific descriptor.
 */

/*
 * If x is the real device size in bytes, we return an apparent size of:
 *
 *	y = (x & ~(MD_RESERVED_BYTES - 1)) - MD_RESERVED_BYTES
 *
 * and place the 4kB superblock at offset y.
 */
