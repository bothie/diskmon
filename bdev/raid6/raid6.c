/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

// #define IGNORE_SUPERBLOCK_DIFFERENCES

#include "raid6_private.h"

#include "common.h"
#include "bdev.h"
#include "raid6.h"

#include <assert.h>
#include <btmacros.h>
#include <btstr.h>
#include <bttime.h>
#include <errno.h>
#include <fcntl.h>
#include <mprintf.h>
#include <parseutil.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DRIVER_NAME "raid6"

/*
 * Public interface
 */
bool raid6_verify_parity = false;
bool raid6_dump_blocks_on_parity_mismatch = false;
bool raid6_notify_read_error = false;

/*
 * Indirect public interface (via struct dev)
 */
static block_t raid6_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data, const char * reason);
static block_t raid6_data_read(struct bdev * bdev, block_t first, block_t num, u8 * data, const char * reason);
static block_t raid6_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data);
static bool raid6_destroy(struct bdev * bdev);

static struct bdev * raid6_init(struct bdev_driver * bdev_driver, char * name, const char * args);

static bool raid6_component_destroy(struct bdev * bdev);
static block_t raid6_component_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data, const char * reason);

static block_t raid6_short_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const char * reason);
static block_t raid6_disaster_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const u8 * ignore_map, const char * reason);
static block_t raid6_do_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const u8 * ignore_map, const char * reason);
static block_t raid6_data_write(struct bdev * bdev, block_t first, block_t num, const u8 * data);

static bool initialized;

BDEV_INIT {
	if (!bdev_register_driver(DRIVER_NAME,&raid6_init)) {
		eprintf("Couldn't register driver %s",DRIVER_NAME);
	} else {
		initialized=true;
	}
}

BDEV_FINI {
	if (initialized) {
		bdev_deregister_driver(DRIVER_NAME);
	}
}

/*
 * Implementation - everything non-public (except for init of course).
 */

struct raid6_disk {
	/*
	 * In raid6_init we get the disks in a random order. The first disk 
	 * given to raid6_init MAY be the first raid disk, but it may be any 
	 * other raid disk as well. So, if we need the first raid disk, we 
	 * look for it's number in disk[0].layoutmapping. So, to access the 
	 * entry for raid disk N, we use the expression 
	 * disk[disk[N].layoutmapping]
	 * (Think about layoutmapping as being a completely separated array 
	 *  and being used like this: disk[layoutmapping[N]].)
	 */
	int layoutmapping;
	struct bdev * bdev;
	block_t sb_offset;
	int sb_version;
	union {
		u8 * u8;
		struct softraid_superblock_0 * v0;
		struct softraid_superblock_1 * v1;
	} sb;
	bool silently_fix_next;
	u64 events;
};

struct raid6_dev {
	struct bdev * this;
	unsigned num_disks;
	unsigned data_disks;
	unsigned parity_disks;
	unsigned layout;
	unsigned level;
	unsigned chunk_size_Bytes;
	struct raid6_disk * disk;
	unsigned chunk_size_Blocks;
	bool can_recover;
	unsigned refcount;
	block_t size;
	u64 max_events;
	bool events_in_sync;
	bool verify_parity;
	bool may_verify_parity;
	u8 * d_space;
	u8 * p_space;
	u8 * v_space;
	u8 * t_space;
};

struct raid6_component_dev {
	struct raid6_dev * full_raid;
	/*
	 * The entry below selects the role of the disk. It's not neccessarry 
	 * for this disk to be active in the array at all (if it is not 
	 * active, all read actions to that disk will be emulated by 
	 * recovering the data from the active disks, if that is not 
	 * possible, EIO will be returned). The entry's name is role instead 
	 * of index or component or something similar to make clear that we 
	 * 
	 * 1. are talking about the disk numbers relative to the raid and not 
	 *    relative to the command line argument.
	 * 2. can't access spare disks and failed disks by this means at all.
	 *
	 * Note however that the parity check will still be done and an error 
	 * will be returned if that check fails.
	 */
	unsigned role;
};

static u32 raid6_calc_chksum_generic(u32 * data,int num_words) {
	u64 sum=0;
	
	while (num_words--) {
		sum+=*data++;
	}
	
	return (sum&0xffffffff) + (sum>>32);
}

static u32 raid6_calc_chksum_v0(struct softraid_superblock_0 * sb) {
	if (sizeof(*sb)!=4096) {
		FATAL("MD superblock's in memory representation is broken. We can't proceed.");
	}
	
	u32 disk_csum=sb->chksum;
	sb->chksum=0;
	
	u32 retval=raid6_calc_chksum_generic((u32*)sb,1024);
	
	sb->chksum=disk_csum;
	
	return retval;
}

static u32 raid6_calc_chksum_v1(struct softraid_superblock_1 * sb) {
	if (sizeof(*sb)!=256
	||  ((unsigned char *)&sb->data_offset-(unsigned char *)sb)!=128
	||  ((unsigned char *)&sb->utime      -(unsigned char *)sb)!=192) {
		FATAL("MD superblock's in memory representation is broken. We can't proceed.");
	}
	
	u32 disk_csum=sb->chksum;
	sb->chksum=0;
	
	if (sb->max_dev&1) {
		sb->dev_roles[sb->max_dev]=0;
	}
	u32 retval=raid6_calc_chksum_generic((u32*)sb,sizeof(*sb)/4+(sb->max_dev+1)/2);
	
	sb->chksum=disk_csum;
	
	return retval;
/*
old, definitively working code:

	if (sizeof(*sb)!=256
	||  ((unsigned char *)&sb->data_offset-(unsigned char *)sb)!=128
	||  ((unsigned char *)&sb->utime      -(unsigned char *)sb)!=192) {
		FATAL("MD superblock's in memory representation is broken. We can't proceed.");
	}
	
	u32 disk_csum=sb->chksum;
	sb->chksum=0;
	u64 sum=0; // warning: 'sum' may be used uninitialized in this function
	
	if (sb->max_dev&1) {
		sb->dev_roles[sb->max_dev]=0;
	}
	unsigned size=sizeof(*sb)/4+sb->max_dev/2;
	u32 * p=(u32*)sb;
	
	while (size--) {
		sum+=*p++;
	}
	sb->chksum=disk_csum;
	
	return (sum&0xffffffff) + (sum>>32);
*/
}

static void print_superblock_v0(struct softraid_superblock_0 * sbv0) {
	printf(
		"uuid=%08lx:%08lx:%08lx:%08lx, version=%lu.%lu.%lu\n"
		"size=%lu, md_minor=%lu, not_persistent=%lu\n"
		"level=%lu, nr_disks=%lu, raid_disks=%lu\n"
		"gvalid_words=%lu\n"
		"---\n"
		"state=%s%s%s\n"
		"active_disks=%lu, working_disks=%lu, failed_disks=%lu, spare_disks=%lu\n"
		"events=%llu, cp_events=%llu, recovery_cp=%lu\n"
		"layout=%lu, chunk_size=%lu\n"
		"root_pv=%lu, root_block=%lu\n"
		"---\n"
		"disk number major minor raid_disk(role) statis\n"
		"THIS %6lu %5lu %5lu %15lu %s%s%s%s%s\n"
		
		,(unsigned long)sbv0->uuid0,(unsigned long)sbv0->uuid1,(unsigned long)sbv0->uuid2,(unsigned long)sbv0->uuid3
		,(unsigned long)sbv0->major_version,(unsigned long)sbv0->minor_version,(unsigned long)sbv0->patch_version
		
		,(unsigned long)sbv0->sb0_size
		,(unsigned long)sbv0->md_minor
		,(unsigned long)sbv0->not_persistent
		
		,(unsigned long)sbv0->sb0_level
		,(unsigned long)sbv0->sb0_nr_disks
		,(unsigned long)sbv0->sb0_raid_disks
		
		,(unsigned long)sbv0->gvalid_words
		
		,sbv0->state&(1<<MD_SB_CLEAN)?"CLEAN ":"DIRTY "
		,sbv0->state&(1<<MD_SB_ERRORS)?"ERRORS ":""
		,sbv0->state&(1<<MD_SB_BITMAP_PRESENT)?"HAS_BITMAP ":""
		,(unsigned long)sbv0->active_disks
		,(unsigned long)sbv0->working_disks
		,(unsigned long)sbv0->failed_disks
		,(unsigned long)sbv0->spare_disks
		,sbv0->events
		,sbv0->cp_events
		,(unsigned long)sbv0->recovery_cp
		,(unsigned long)sbv0->sb0_layout
		,(unsigned long)sbv0->sb0_chunk_size
		,(unsigned long)sbv0->root_pv
		,(unsigned long)sbv0->root_block
		
		,(unsigned long)sbv0->this_disk.number
		,(unsigned long)sbv0->this_disk.major
		,(unsigned long)sbv0->this_disk.minor
		,(unsigned long)sbv0->this_disk.raid_disk
		,sbv0->this_disk.state&(1<<MD_DISK_FAULTY_SHIFT)?"FAULTY ":""
		,sbv0->this_disk.state&(1<<MD_DISK_ACTIVE_SHIFT)?"ACTIVE ":""
		,sbv0->this_disk.state&(1<<MD_DISK_SYNC_SHIFT)?"SYNC ":""
		,sbv0->this_disk.state&(1<<MD_DISK_REMOVED_SHIFT)?"REMOVED ":""
		,sbv0->this_disk.state&(1<<MD_DISK_WRITEMOSTLY_SHIFT)?"WRITEMOSTLY ":""
	);
	
	for (int i=0;i<MD_SB_DISKS;++i) {
		printf(
			"%4i %6lu %5lu %5lu %15lu %s%s%s%s%s\n"
			/*
			 * "%i. disk:\n"
			 * "\tnumber=%lu\n"
			 * "\tmajor=%lu\n"
			 * "\tminor=%lu\n"
			 * "\traid_disk (role)=%lu\n"
			 * "\tstatis=%s%s%s%s%s\n"
			 */
			
			,i
			,(unsigned long)sbv0->disks[i].number
			,(unsigned long)sbv0->disks[i].major
			,(unsigned long)sbv0->disks[i].minor
			,(unsigned long)sbv0->disks[i].raid_disk
			,sbv0->disks[i].state&(1<<MD_DISK_FAULTY_SHIFT)?"FAULTY ":""
			,sbv0->disks[i].state&(1<<MD_DISK_ACTIVE_SHIFT)?"ACTIVE ":""
			,sbv0->disks[i].state&(1<<MD_DISK_SYNC_SHIFT)?"SYNC ":""
			,sbv0->disks[i].state&(1<<MD_DISK_REMOVED_SHIFT)?"REMOVED ":""
			,sbv0->disks[i].state&(1<<MD_DISK_WRITEMOSTLY_SHIFT)?"WRITEMOSTLY ":""
		);
	}
}

static int write_super_version_0(struct raid6_disk * disk) {
	if (disk->sb_version!=0
	||  disk->sb.v0->magic!=MD_MAGIC) {
		ERROR("write_super_version_0: I'm not going to write an invalid super block.");
		return -1;
	}
	if (!disk->sb_offset) {
		ERROR("write_super_version_0: I'm not going to write a super block to block 0 unless you explicitely fore so by setting disk->sb_offset to -1.");
		return -1;
	}
	
	// TODO: Make more consistency checks
	
	if ((u64)disk->sb_offset==(u64)-1) {
		disk->sb_offset=0;
	}
	if ((u64)disk->sb_offset!=(bdev_get_size(disk->bdev)&~(u64)0x7f)-0x80) {
		NOTIFY("disk->sb_offset!=(disk->bdev->size&~((u64)0x7f))-0x80, I'll use disk->sb_offset as requested.");
	}
	
	disk->sb.v0->chksum=0;
	disk->sb.v0->chksum=raid6_calc_chksum_v0(disk->sb.v0);
	
	if (8!=bdev_write(
		disk->bdev,disk->sb_offset,8,disk->sb.u8
	)) {
		ERROR("Couldn't write RAID super block (problem during disk io).");
		return 3;
	}
	
	return 0;
}

/*
 * Tries to read the super block version 0
 *
 * Return:
 * 0=We successfully read and identified a super block version 0
 * 1=We successfully read and identified a super block version 0 but the checksum was wrong
 * 2=We read the block successfully, but it was not a raid super block version 0
 * 3=Something else went wrong (i.e. DISK IO)
 */
static int read_super_version_0(struct raid6_disk * disk,bool probing) {
	disk->sb.v0=malloc(4096);
	disk->sb_version=0;
	disk->sb_offset=(bdev_get_size(disk->bdev)&~((u64)0x7f))-0x80;
	
	bool read_error_warning=false;
	for (block_t i=0;i<8;++i) {
		if (1 != bdev_read(disk->bdev, disk->sb_offset + i, 1, disk->sb.u8 + 512 * i, "raid_sb_v_0")) {
			if (!i || i==7) {
				ERRORF("Couldn't read RAID super block of device %s (problem during disk io).",bdev_get_name(disk->bdev));
				free(disk->sb.v0);
				return 3;
			}
			memset(disk->sb.u8+512*i,0,512);
			read_error_warning=true;
		}
	}
	
	if (read_error_warning) {
		WARNINGF("Couldn't read all disk blocks belonging to the RAID super block of disk %s, zeroing out the failed ones.",bdev_get_name(disk->bdev));
	}
	
	if (disk->sb.v0->magic!=MD_MAGIC) {
		if (!probing) ERRORF("Couldn't find RAID super block on %s.",bdev_get_name(disk->bdev));
/*
		u32 * buffer=malloc(1024LU*1024LU);
		if (2048!=bdev_read(
			disk->dev,disk->dev->size-2048,2048,disk->sb.u8
		)) {
			ERROR("Couldn't read RAID super block.");
			return 3;
		}
		for (int i=2048;i--;) {
			if (*buffer==MD_MAGIC) {
				eprintf(
					"Found raid magic in block %llu (0x%llu) on %s\n"
					,disk->dev->size-i
					,disk->dev->size-i
				);
			}
			buffer+=128;
		}
*/
		free(disk->sb.v0);
		return 2;
	}
	
	u32 csum=disk->sb.v0->chksum;
	disk->sb.v0->chksum=0;
	if (csum!=raid6_calc_chksum_v0(disk->sb.v0)) {
		ERRORF("[2] Checksum of RAID super block of disk %s wrong.",bdev_get_name(disk->bdev));
		free(disk->sb.v0);
		return 1;
	}
	disk->sb.v0->chksum=csum;
	
	bool modified=false;
	
/*
	if (!disk->sb.v0->recovery_cp) {
		disk->sb.v0->cp_events=disk->sb.v0->events;
		disk->sb.v0->recovery_cp=-1;
		modified=true;
	}
	
	if (!(disk->sb.v0->state&(1<<MD_SB_CLEAN))) {
		disk->sb.v0->state|=(1<<MD_SB_CLEAN);
		modified=true;
	}
*/
	
	if (modified) {
		NOTIFY("Writing back modified version of super block.");
		write_super_version_0(disk);
	}
	
	return 0;
}

bool debug_striped_reader=false;
bool debug_data_reader=false;
bool debug_data_writer=false;

// <dev1> <dev2> <dev3> <dev4> [<dev5> [...]]
//
// name will be reowned by init, args will be freed by the caller.
// RAID6{LEFT|RIGHT}[A]SYMMETRIC4096
static struct bdev * raid6_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	struct bdev * retval=NULL;
	struct raid6_dev * private=NULL;
	struct bdev * * components_bdev=NULL;
	struct raid6_component_dev * * components=NULL;
	bool write_super_block=false;
	struct softraid_superblock_0 * v0=NULL;
	
	if (sizeof(struct softraid_superblock_0)!=4096) {
		ERRORF("sizeof(struct softraid_superblock_0)=%u.",(unsigned)sizeof(struct softraid_superblock_0));
		FATAL("MD superblock version 0.90's in memory representation is broken. We can't proceed.");
	}
	
	private=zmalloc(sizeof(*private));
	if (!private) {
		ERROR("Couldn't allocate raid device descriptor.");
		goto err;
	}
	
	private->verify_parity = raid6_verify_parity;
	private->may_verify_parity = private->verify_parity;
	
	const char * p;
	unsigned long param_level;
	unsigned long param_layout;
	unsigned long param_chunk_size;
	unsigned long param_role; // This field is needed when writing raid super blocks if there are missing disks.
	bool param_valid=false;
	
	p=args;
	// RAID6LEFT_SYMMETRIC4096
	if (!parse_skip_string(&p,"RAID")) goto no_params;
	if (!parse_unsigned_long(&p,&param_level)) goto no_params;
	if (!parse_skip_string(&p,"LEFT")
	&&  !parse_skip_string(&p,"RIGHT")) goto no_params;
	param_layout=(p[-2]=='F')?0:1;
	if (!parse_skip(&p,'_')) goto no_params;
	if (!parse_skip(&p,'A')) param_layout|=2;
	if (!parse_skip_string(&p,"SYMMETRIC")) goto no_params;
	if (!parse_unsigned_long(&p,&param_chunk_size)) goto no_params;
eprintf("0");
	write_super_block=parse_skip_string(&p,"WRITE_SB0");
eprintf("1");
	if (*p!=' ') goto no_params;
eprintf("8");
	param_valid=true;
	while (*p && *p==' ') ++p;
	args=p;
eprintf("9, args=\"%s\"\n",args);
	private->level=param_level;
	private->layout=param_layout;
	private->chunk_size_Bytes=param_chunk_size;

no_params:
	private->disk=NULL;
	private->num_disks=0;
	private->data_disks=0;
	
	p=args;
	while (*p) {
		if (*p=='-' && *(p+1)=='-') {
			--private->num_disks;
		}
		while (*p && *p!=' ') ++p;
		++private->num_disks;
		while (*p && *p==' ') ++p;
	}
	
	private->disk=malloc(sizeof(*private->disk)*private->num_disks);
	if (!private->disk) {
		ERROR("Couldn't allocate device list array.");
		goto err;
	}
	private->can_recover=true;

restart:
	p=args;
	param_role=0;
	
	unsigned d;
	for (d=private->num_disks;d;) {
		private->disk[--d].layoutmapping=-1;
	}
	
//	struct softraid_superblock_0 * sb0=NULL; // warning: 'sb0' may be used uninitialized in this function
//	struct softraid_superblock_1 * sb1=NULL; // warning: 'sb1' may be used uninitialized in this function
	unsigned nb=1;
	
	unsigned block_size=0; // warning: 'block_size' may be used uninitialized in this function
	while (*p) {
		// Annahme: nr_disks=active_disks+failed_disks+spare_disks
		// 	Alternativ: working_disks+failed_disks+spare_disks
		unsigned md_disk_faulty=0;
		unsigned md_disk_active=0;
		unsigned md_disk_sync=0;
		unsigned md_disk_removed=0;
		unsigned md_disk_spare=0;
		unsigned md_disk_faulty_removed=0;
		private->disk[d].silently_fix_next=false;
		
parse_disk_device_name: ;
		const char * start=p;
		while (*p && *p!=' ') ++p;
		
		char * disk_device_name=malloc(p-start+1);
		if (!disk_device_name) {
			ERRORF("malloc: %s",strerror(errno));
			goto err;
		}
		memcpy(disk_device_name,start,p-start);
		disk_device_name[p-start]=0;
		while (*p && *p==' ') ++p;
		if (!strcmp(disk_device_name,"--silently-fix-next")) {
			free(disk_device_name);
			private->disk[d].silently_fix_next=true;
			goto parse_disk_device_name;
		}
		if (*disk_device_name=='-' && *(disk_device_name+1)=='-') {
			ERRORF(
				"Unknown argument while parsing command line: %s"
				,disk_device_name
			);
			free(disk_device_name);
			goto err;
		}
//		eprintf("d=%u, args=»%s«, start=»%s«, p=»%s«, disk_device_name=»%s«\n",d,args,start,p,disk_device_name);
		private->disk[d].bdev=bdev_lookup_bdev(disk_device_name);
		if (!private->disk[d].bdev) {
			if (strcmp(disk_device_name,"missing")) {
				ERRORF("Couldn't lookup device %s, threating as missing.",disk_device_name);
			}
			free(disk_device_name);
			++param_role;
			continue;
		}
		
		bool kick_disk=false;
		
		if (write_super_block) {
			if (512!=bdev_get_block_size(private->disk[d].bdev)) {
				ERROR("Can't handle block sizes != 512 currently");
				goto err;
			}
			private->disk[d].sb_offset=(bdev_get_size(private->disk[d].bdev)&~((u64)0x7f))-0x80;
			private->disk[d].sb_version=0;
			if (!d) {
				v0=malloc(4096); memset(v0,0,4096); // SIGSEGV if malloc failed
				private->disk[d].sb.v0=NULL;
			}
			v0->magic=MD_MAGIC;
			v0->major_version=0;
			v0->minor_version=90;
			v0->patch_version=0;
			v0->gvalid_words=0; // Number of used words in this section (why is it 0?)
			v0->sb0_level=private->level;
			v0->sb0_size=private->disk[d].sb_offset/2;
			v0->sb0_nr_disks=private->num_disks; // total disks in the raid set
			v0->sb0_raid_disks=v0->sb0_nr_disks; // disks in a fully functional raid set
			v0->md_minor=0; // Doesn't really matter ...
			v0->not_persistent=0;
			v0->state=(1<<MD_SB_CLEAN);
			v0->active_disks=v0->sb0_nr_disks;
			v0->working_disks=v0->sb0_nr_disks;
			v0->failed_disks=0;
			v0->spare_disks=0;
			// v0->chksum will be initialized by write_super_version_0
			v0->events=2;
			v0->cp_events=2;
			v0->recovery_cp=-1;
			// Only valid for minor_version>90:
			// reshape_position, new_level, delta_disks, new_layout, new_chunk
				v0->reshape_position=0;
				v0->new_level=0;
				v0->delta_disks=0;
				v0->new_layout=0;
				v0->new_chunk=0;
			v0->sb0_layout=private->layout;
			v0->sb0_chunk_size=private->chunk_size_Bytes;
			v0->root_pv=0; // What is this field for at all?
			v0->root_block=0; // What is this field for at all?
			if (!d) {
				v0->ctime=time(NULL);
				int fd=open("/dev/random",O_RDONLY);
				if (fd<0) {
					eprintf("open(\"/dev/random\",O_RDONLY): %s\n",strerror(errno));
					exit(2);
				}
				assert(4==read(fd,&v0->uuid0,4));
				assert(4==read(fd,&v0->uuid1,4));
				assert(4==read(fd,&v0->uuid2,4));
				assert(4==read(fd,&v0->uuid3,4));
				for (unsigned i=0;i<private->num_disks;++i) {
					v0->disks[i].number=i;
					v0->disks[i].major=0;
					v0->disks[i].minor=0;
					v0->disks[i].raid_disk=i;
					v0->disks[i].state=MD_DISK_ACTIVE|MD_DISK_SYNC;
					eprintf("private->disk[d].sb.v0->disks[i].state=%lu\n",(unsigned long)v0->disks[i].state);
				}
			}
			v0->utime=v0->ctime;
			memcpy(
				&v0->this_disk,
				&v0->disks[param_role],
				sizeof(v0->this_disk)
			);
			private->disk[d].sb.v0=v0;
			write_super_version_0(private->disk+d);
			private->disk[d].sb.v0=NULL;
//			free(v0);
		}
		private->disk[d].sb_version=-1;
		if (!d) {
			block_size=bdev_get_block_size(private->disk[d].bdev);
			if (block_size!=512) {
				ERROR("Can't handle block sizes != 512 currently");
				goto err;
			}
			
			if (param_valid) {
				// We don't read any superblocks if we get a 
				// raid defining argument
				private->disk[d].sb_version=-1;
				private->chunk_size_Blocks=private->chunk_size_Bytes/block_size;
				if (private->chunk_size_Bytes%block_size) {
					ERROR("chunk size is not evenly dividable by block size");
					goto err;
				}
				
				private->size=(bdev_get_size(private->disk[d].bdev)&~((u64)0x7f))-0x80;
			} else { // if (param_valid)
				private->disk[d].sb_version=1;
				private->disk[d].sb.v1=malloc(nb*512);
				// Find RAID super block version 1.
				int i;
				for (i=0;i<3;++i) {
					switch (i) {
						/*
						 * First, try at end of device 
						 * (exactly: at least 8K but lesser 
						 * than 12K away from end of device. 
						 * The super block starts on a 4K 
						 * boundary.
						 */
						case 0:
							private->disk[d].sb_offset=(bdev_get_size(private->disk[d].bdev)-16)&~7;
							break;
						
						/*
						 * Second attempt is at start of device
						 */
						case 1:
							private->disk[d].sb_offset=0;
							break;
						
						/*
						 * Last but not least, try at 4K
						 */
						case 2:
							private->disk[d].sb_offset=8;
							break;
					} // switch (i)
					if (1!=bdev_read(
						private->disk[d].bdev,private->disk[d].sb_offset,1,private->disk[d].sb.u8
						, "raid_sb_v_1"
					)) {
						free(private->disk[d].sb.v1);
						ERROR("Couldn't read RAID super block (disk io).");
						goto err;
					}
					if (private->disk[d].sb.v1->magic==MD_MAGIC) {
						break;
					}
				} // for (i=0;i<3;++i)
				if (i<3) {
					unsigned max_dev=private->disk[d].sb.v1->max_dev;
					if (max_dev>128) {
						free(private->disk[d].sb.v1);
						nb=(256+2*max_dev+511)/512;
						private->disk[d].sb.v1=malloc(nb*512);
						if (nb!=bdev_read(
							private->disk[d].bdev,private->disk[d].sb_offset,nb,private->disk[d].sb.u8
							, "raid_sb_v_1"
						)) {
							free(private->disk[d].sb.v1);
							ERROR("Couldn't read RAID super block (disk io).");
							goto err;
						}
					}
					u32 chksum=raid6_calc_chksum_v1(private->disk[d].sb.v1);
					if (chksum!=private->disk[d].sb.v1->chksum) {
						free(private->disk[d].sb.v1);
						ERROR("[3] Checksum of RAID super block wrong.");
						goto err;
					}
					ERROR("Can't handle version 1 superblock yet.");
					goto err;
				} else { // if (i<3)
					// Ok, no version 1 super block, now try to find version 0
					free(private->disk[d].sb.v1);
					private->disk[d].sb.v1=NULL;
					int rv=read_super_version_0(private->disk+d,true);
					if (rv==2) {
						ERRORF("There is no known superblock on this device (%s)\n",disk_device_name);
						goto err;
					}
					if (rv) goto err;
				} // if (i<3), else
				
				private->level=private->disk[d].sb.v0->sb0_level;
				
				if (private->disk[d].sb.v0->major_version!= 0
				||  private->disk[d].sb.v0->minor_version!=90
				||  (private->disk[d].sb.v0->patch_version!= 0
				&&   private->disk[d].sb.v0->patch_version!= 3)) {
					ERROR("Unsupported version number. Expected version was 0.90.0 or 0.90.3.");
print_err:
					print_superblock_v0(private->disk[d].sb.v0);
					goto err;
				}
				
				if ((block_t)private->disk[d].sb.v0->sb0_size * 2 + 128 > bdev_get_size(private->disk[d].bdev)) {
					ERROR("According to super block the device has to be bigger as it actually is.");
					goto print_err;
				}
				
				if (private->disk[d].sb.v0->not_persistent) {
					WARNING("The permanent super block claims that there is no permanent super block. Oops ...");
				}
				
//				printf("private->disk[d].sb.v0->disks[*].state={");
				for (int i=0;i<MD_SB_DISKS;++i) {
					unsigned s=private->disk[d].sb.v0->disks[i].state;
					if (i) {
						if (!private->disk[d].sb.v0->disks[i].raid_disk) {
							continue;
						}
//						putchar(',');
					}
//					printf("%u",s);
					
					if (unlikely(!s)) {
						++md_disk_spare;
					} else {
						if ((s&MD_DISK_FAULTY_REMOVED)==MD_DISK_FAULTY_REMOVED) {
							++md_disk_faulty_removed;
						}
						if (s&MD_DISK_FAULTY ) ++md_disk_faulty ;
						if (s&MD_DISK_ACTIVE ) ++md_disk_active ;
						if (s&MD_DISK_SYNC   ) ++md_disk_sync   ;
						if (s&MD_DISK_REMOVED) ++md_disk_removed;
					}
				}
//				printf("}\n");
				
/*
				printf(
					"md_disk_{faulty=%u, active=%u, sync=%u, removed=%u, spare=%u}\n"
					,md_disk_faulty
					,md_disk_active
					,md_disk_sync
					,md_disk_removed
					,md_disk_spare
				);
*/
				
				// if (private->disk[d].sb.v0->patch_version==0) {
					if (private->disk[d].sb.v0->sb0_nr_disks!=md_disk_active+(md_disk_faulty-md_disk_faulty_removed)+md_disk_spare) {
						ERROR("nr_disks seems to be invalid (it's expected to be the sum of active, non-removed faulty and spare disks.");
						goto print_err;
					}
				// }
				
				if (private->disk[d].sb.v0->active_disks!=md_disk_active) {
					ERROR("active_disks seems to be invalid.");
					goto print_err;
				}
				
				if (private->disk[d].sb.v0->working_disks!=md_disk_active+md_disk_spare) {
					ERROR("working_disks seems to be invalid.");
					goto print_err;
				}
				
				/*
				 * Sanity checking is cool - as long as one really understands, 
				 * what what means. I don't (in this case) and the original soft 
				 * raid programmers forgot to make suitable documentations. So 
				 * I'll stick on just not checking the relationship of this 
				 * values.
				
				if (private->disk[d].sb.v0->failed_disks!=md_disk_faulty-md_disk_faulty_removed) {
					ERRORF("failed_disks seems to be invalid; private->disk[d].sb.v0->failed_disks=%lu, (md_disk_faulty=%u - md_disk_faulty_removed=%u)=%u.",private->disk[d].sb.v0->failed_disks,md_disk_faulty,md_disk_faulty_removed,md_disk_faulty-md_disk_faulty_removed);
					goto print_err;
				}
				*/
				
				if (private->disk[d].sb.v0->spare_disks!=md_disk_spare) {
					ERROR("spare_disks seems to be invalid.");
					goto print_err;
				}
				
				if (private->disk[d].sb.v0->active_disks>private->disk[d].sb.v0->sb0_raid_disks) {
					ERROR("active_disks is greater than raid_disks.");
					goto print_err;
				}
				
				private->chunk_size_Bytes=private->disk[d].sb.v0->sb0_chunk_size;
				private->chunk_size_Blocks=private->chunk_size_Bytes/512;
				
				if (private->disk[d].sb.v0->sb0_chunk_size%block_size
				||  private->disk[d].sb.v0->root_pv
				||  private->disk[d].sb.v0->root_block
// SPARE:			||  private->disk[d].sb.v0->this_disk.number>=private->disk[d].sb.v0->raid_disks
// SPARE:			||  private->disk[d].sb.v0->this_disk.raid_disk>=private->disk[d].sb.v0->raid_disks
				) {
					ERROR("Something in superblock is suspect (or this constellation is not supported yet).");
					print_superblock_v0(private->disk[d].sb.v0);
					goto err;
				}
				
				unsigned state=private->disk[d].sb.v0->state;
				if (!(state&(1<<MD_SB_CLEAN)) || state&(1<<MD_SB_ERRORS)) {
					print_superblock_v0(private->disk[d].sb.v0);
				}
				
				if (private->disk[d].sb.v0->active_disks+private->disk[d].sb.v0->spare_disks!=private->disk[d].sb.v0->working_disks) {
					WARNING("active_disks+spare_disks != working_disks");
					goto print_err;
				}
				
				private->layout=private->disk[d].sb.v0->sb0_layout;
				
				if (private->num_disks!=private->disk[d].sb.v0->sb0_raid_disks) {
					ERROR("Different number of disk in raid and given arguments. You must give exactly the number of disks a non-degraded raid will have. Fill up with the string \"missing\".");
					goto err;
				}
				
				private->size=private->disk[0].sb.v0->sb0_size*2;
				
				private->disk[d].events = private->disk[d].sb.v0->events;
				private->max_events = private->disk[d].sb.v0->events;
				private->events_in_sync = true;
				
				NOTIFYF("gvalid_words=%lu",(unsigned long)private->disk[d].sb.v0->gvalid_words);
			} // if (param_valid), else
			
//			read_super_version_0(private->disk+d,true);
//			NOTIFYF("gvalid_words=%lu",(unsigned long)private->disk[d].sb.v0->gvalid_words);
		} else { // if (!d)
			if (block_size!=bdev_get_block_size(private->disk[d].bdev)) {
				ERROR("Block sizes differ between component devices");
				goto err;
			}
			if (!param_valid) {
				private->disk[d].sb_offset=private->disk[0].sb_offset;
				switch (private->disk[0].sb_version) {
					case 0: {
						int rv=read_super_version_0(private->disk+d,false);
						if (rv) goto err;
						break;
					}
					
					case 1: {
						assert(never_reached);
						private->disk[d].sb_version=private->disk[0].sb_version;
						private->disk[d].sb.v1=malloc(nb*512);
						if (nb!=bdev_read(
							private->disk[d].bdev,private->disk[d].sb_offset,nb,private->disk[d].sb.u8
							, "raid_sb_v_1"
						)) {
							free(private->disk[d].sb.v1);
							ERROR("Couldn't read RAID super block.");
							goto err;
						}
						u32 chksum=raid6_calc_chksum_v1(private->disk[d].sb.v1);
						if (chksum!=private->disk[d].sb.v1->chksum) {
							free(private->disk[d].sb.v1);
							ERROR("[1] Checksum of RAID super block wrong.");
							goto err;
						}
						break;
					}
					
					default: {
						assert(never_reached);
					}
				} // switch (private->disk[0].sb_version)
#ifndef IGNORE_SUPERBLOCK_DIFFERENCES
				NOTIFYF("private->disk[0].sb.v0=%p, private->disk[d=%u].sb.v0=%p",(void*)private->disk[0].sb.v0,d,(void*)private->disk[d].sb.v0);
				u32 sb0_csum=private->disk[0].sb.v0->chksum;
				u32 sbd_csum=private->disk[d].sb.v0->chksum;
				
				private->disk[0].sb.v0->chksum=0;
				private->disk[d].sb.v0->chksum=0;
				
				kick_disk=memcmp(private->disk[d].sb.v0,private->disk[0].sb.v0,512);
				
				private->disk[0].sb.v0->chksum=sb0_csum;
				private->disk[d].sb.v0->chksum=sbd_csum;
				
				if (kick_disk) {
					WARNINGF(
						"Superblocks differ between component "
						"disks %s with event counter %llu and "
						"%s with event counter %llu."
						,bdev_get_name(private->disk[0].bdev)
						,private->disk[0].sb.v0->events
						,bdev_get_name(private->disk[d].bdev)
						,private->disk[d].sb.v0->events
					);
					print_superblock_v0(private->disk[0].sb.v0);
					print_superblock_v0(private->disk[d].sb.v0);
					if (private->max_events != private->disk[d].sb.v0->events) {
						private->events_in_sync = false;
						private->may_verify_parity = true;
						if (private->max_events < private->disk[d].sb.v0->events) {
							private->max_events = private->disk[d].sb.v0->events;
						}
					}
					if (!private->can_recover) {
						/*
						 * If we're not able to recover because of too many failed disk, we don't kick any disk and 
						 * instead let the user access any data, which has not been changed since the first disk fail.
						 */
						kick_disk=false;
					} else {
						if (private->disk[0].sb.v0->events>private->disk[d].sb.v0->events) {
							WARNINGF("Kicking %s out of the raid.",bdev_get_name(private->disk[d].bdev));
						} else {
							unsigned i;
							for (i=d;i--;) {
								WARNINGF("Kicking %s out of the raid.",bdev_get_name(private->disk[i].bdev));
								free(private->disk[i].sb.v0);
							}
							for (i=0;i<private->num_disks;++i) {
								private->disk[i].layoutmapping=-1;
							}
							private->disk[0]=private->disk[d];
							kick_disk=false;
							d=0;
						}
					}
				} // if (kick_disk)
#endif // #ifndef IGNORE_SUPERBLOCK_DIFFERENCES
				if (private->can_recover
				&&  private->disk[d].sb.v0->active_disks<private->disk[d].sb.v0->sb0_raid_disks-private->parity_disks) {
					ERROR("\033[31mThere are too many failed disks. Entering catastrophic mode and restarting device scan.\033[0m");
					private->can_recover=false;
					/*
					 * We don't have enough disks to do any recovery. So we keep all disks as if they 
					 * were ok and let access through only for those stripes which all disks have 
					 * matching parities.
					 */
					free(disk_device_name);
					disk_device_name=NULL;
					do {
						free(private->disk[d].sb.v0); private->disk[d].sb.v0=NULL;
					} while (d--);
					goto restart;
				}
				
				private->disk[d].events = private->disk[d].sb.v0->events;
			} // if (!param_valid)
		} // if (!d), else
		
		switch (private->level) {
			case 4: {
				private->layout=4;
				/* fall-trough */
			}
			
			case 5: {
				private->parity_disks=1;
				break;
			}
			
			case 6: {
				private->parity_disks=2;
				break;
			}
			
			default: {
				ERROR("This code can handle RAID levels 4, 5 and 6 only.");
				goto err;
			}
		}
		
		private->data_disks=private->num_disks-private->parity_disks;
		
		unsigned role;
		if (!param_valid) {
			role=private->disk[d].sb.v0->this_disk.raid_disk;
			
			if (role>=private->num_disks) {
				kick_disk=true;
				NOTIFYF("Disk %s is a spare -> kicking out of the raid.",disk_device_name);
			} else if (!kick_disk && private->disk[role].layoutmapping!=-1) {
				ERRORF("(At least) two component disks claim having the same role (%s is one of them, %s is another one of them).",disk_device_name,bdev_get_name(private->disk[private->disk[role].layoutmapping].bdev));
				goto err;
			}
		} else { // if (!param_valid)
			role=param_role++;
		} // if (!param_valid), else
		
		{
			char * temp[2]={mstrcpy(""),mstrcpy("")};
			for (int tempi=0;tempi<2;++tempi) {
				for (unsigned i=0;i<private->num_disks;++i) {
					char * t;
					if (i) {
						t=temp[tempi];
						temp[tempi]=mprintf("%s, ",t);
						free(t);
					}
					t=temp[tempi];
					temp[tempi]=mprintf("%s%i",t,private->disk[i].layoutmapping);
					free(t);
				}
				if (tempi) {
					break;
				}
				if (!kick_disk) private->disk[role].layoutmapping=d;
			}
			NOTIFYF("%s claims being role %u ( { %s } -> { %s } ).",disk_device_name,role,temp[0],temp[1]);
			free(temp[0]);
			free(temp[1]);
		}
		
		if (kick_disk) {
			NOTIFY("Kicking disk ...");
			switch (private->disk[d].sb_version) {
				case 0: free(private->disk[d].sb.v0); private->disk[d].sb.v0=NULL; break;
				case 1: free(private->disk[d].sb.v1); private->disk[d].sb.v1=NULL; break;
			}
			// FIXME: Additional cleanup neccessary?
			struct raid6_disk tmp;
			memset(&tmp,0,sizeof(tmp));
			tmp.layoutmapping=private->disk[d].layoutmapping;
			// tmp.sb_version=private->disk[d].sb_version;
			// memcpy(&tmp.sb,&private->disk[d].sb,sizeof(private->disk[d].sb));
			private->disk[d]=tmp;
		} else {
			switch (private->level) {
				case 4:
					private->layout=4;
					/* fall through */
				
				case 5:
					break;
				
				case 6:
					break;
				
				default:
					assert(never_reached);
			}
			private->data_disks=private->num_disks-private->parity_disks;
		}
		
		eprintf("private->disk[d=%u].sb_version=%u\n",d,private->disk[d].sb_version);
#ifndef IGNORE_SUPERBLOCK_DIFFERENCES
		if (d) {
#endif // #ifndef IGNORE_SUPERBLOCK_DIFFERENCES
			switch (private->disk[d].sb_version) {
				case 0: free(private->disk[d].sb.v0); private->disk[d].sb.v0=NULL; break;
				case 1: free(private->disk[d].sb.v1); private->disk[d].sb.v1=NULL; break;
			}
#ifndef IGNORE_SUPERBLOCK_DIFFERENCES
		}
#endif // #ifndef IGNORE_SUPERBLOCK_DIFFERENCES
		
		if (!kick_disk) ++d;
		
		while (*p && *p==' ') ++p;
		free(disk_device_name);
		disk_device_name=NULL;
	} // while (*p)
#ifndef IGNORE_SUPERBLOCK_DIFFERENCES
	switch (private->disk[0].sb_version) {
		case 0: free(private->disk[0].sb.v0); private->disk[0].sb.v0=NULL; break;
		case 1: free(private->disk[0].sb.v1); private->disk[0].sb.v1=NULL; break;
	}
#endif // #ifndef IGNORE_SUPERBLOCK_DIFFERENCES
	
/*
	for (unsigned i=0;i<private->num_disks;++i) {
		eprintf("private->disk[%i].layoutmapping=%i\n",i,private->disk[i].layoutmapping);
	}
*/
	
/*
	printf("Layout mapping:");
	for (d=0;d<private->num_disks;++d) {
		printf(" %i",private->disk[d].layoutmapping);
	}
	putchar('\n');
*/
	
	if (!(components_bdev=malloc(sizeof(*components_bdev)*private->num_disks))) {
		goto err_malloc;
	}
	for (unsigned i=0;i<private->num_disks;++i) {
		components_bdev[i]=NULL;
	}
	if (!(components=malloc(sizeof(*components)*private->num_disks))) {
		goto err_malloc;
	}
	for (unsigned i=0;i<private->num_disks;++i) {
		components[i]=NULL;
	}
	for (unsigned i=0;i<private->num_disks;++i) {
		if (!(components[i]=malloc(sizeof(**components)))) {
			goto err_malloc;
		}
	}
	
	private->d_space = malloc(block_size * private->data_disks);
	private->p_space = malloc(block_size * 2);
	private->v_space = private->may_verify_parity ? malloc(block_size * 2) : NULL;
	private->t_space = private->may_verify_parity ? malloc(block_size * 2) : NULL;
	
	if (!(retval=bdev_register_bdev(
		bdev_driver,
		name,
		private->data_disks*private->size,
		block_size,
		raid6_destroy,
		(struct bdev_private *)private
	))) {
		goto err;
	}
	private->this=retval;
	bdev_set_read_function(retval,raid6_read);
//	bdev_set_write_function(retval,raid6_write);
//	bdev_set_read_function(retval,raid6_data_read);
//	bdev_set_write_function(retval,raid6_data_write);
	bdev_set_short_read_function(retval, raid6_short_read);
	bdev_set_disaster_read_function(retval, raid6_disaster_read);
	private->refcount=1;
	for (unsigned i=0;i<private->num_disks;++i) {
		char * tmp;
		
		components[i]->full_raid=private;
		components[i]->role=i;
		if (!(components_bdev[i]=bdev_register_bdev(
			bdev_driver,
			tmp=mprintf("%s/%u",name,i),
			private->size,
			block_size,
			raid6_component_destroy,
			(struct bdev_private *)components[i]
		))) {
			ERRORF("raid6: Couldn't register device %s",tmp);
			free(tmp);
			goto err;
		}
		bdev_set_read_function(components_bdev[i],raid6_component_read);
		/*
		 * Make sure that the cleanup code doesn't free this twice
		 * (once directly and once via raid6_component_destroy
		 */
		components[i]=NULL;
		++private->refcount;
	}
	free(components_bdev);
	free(components);
	
	if (0) {
		unsigned max_num=private->data_disks*3*private->chunk_size_Blocks;
		u8 buffer1[block_size*max_num];
		u8 buffer2[block_size*max_num];
		block_t r = bdev_read(retval, 0, max_num, buffer1, "TODO: reason");
		if (max_num!=r) {
			FATALF("raid456:%i: read(0,max_num=%llu)=%llu failed: %s",__LINE__,(unsigned long long)max_num,(unsigned long long)r,strerror(errno));
		}
		bool dirty=true;
		// for (unsigned start=0;start<max_num;++start) {
			// for (unsigned num=max_num-start;num;--num) {
		// for (unsigned num=max_num;num;--num) {
		for (unsigned num=0;num++<max_num;) {
			for (unsigned start=0;start+num<=max_num;++start) {
				/*
				if (start==2 && num==2) {
					debug_striped_reader=true;
				}
				*/
				eprintf(
					"start=%llu,num=%llu\033[K%c"
					,(unsigned long long)start
					,(unsigned long long)num
					,debug_striped_reader?'\n':'\r'
				);
				if (dirty) {
					memcpy(buffer2,buffer1,sizeof(buffer2));
				}
				dirty=memcmp(buffer2,buffer1,sizeof(buffer2));
				if (num != bdev_read(retval, start, num, buffer2 + block_size * start, "TODO: reason")) {
					FATALF("raid456:%i: read(start=%llu,num=%llu) failed: %s",__LINE__,(unsigned long long)start,(unsigned long long)num,strerror(errno));
				}
				dirty=memcmp(buffer2,buffer1,sizeof(buffer2));
				if (dirty) {
					FATALF("raid456:%i: read(start=%llu,num=%llu) returned different data.",__LINE__,(unsigned long long)start,(unsigned long long)num);
				}
			}
		}
	}
	
	for (unsigned role=0;role<private->num_disks;++role) {
		NOTIFYF(
			"role[%i].layoutmapping=%i"
			,role
			,private->disk[role].layoutmapping
		);
	}
	
	for (unsigned role=0;role<private->num_disks;++role) {
		NOTIFYF(
			"role[%i].bdev->get_name()=%s"
			,role
			,private->disk[role].layoutmapping==-1?"missing":bdev_get_name(private->disk[private->disk[role].layoutmapping].bdev)
		);
	}
	
	NOTIFYF(
		"Initialized RAID%u%s_%sSYMMETRIC%u"
		,private->level
		,(private->layout&1)?"RIGHT":"LEFT"
		,(private->layout&2)?"":"A"
		,private->chunk_size_Bytes
	);
	
	return retval;

err_malloc:
	ERRORF("malloc: %s",strerror(errno));
err:
	if (components) {
		for (unsigned i=0;i<private->num_disks;++i) {
			free(components[i]);
		}
		free(components);
	}
	if (components_bdev) {
		for (unsigned i=0;i<private->num_disks;++i) {
			bdev_destroy(components_bdev[i]);
		}
		free(components_bdev);
	}
	if (retval) {
		bdev_destroy(retval);
	} else if (private) {
		free(private->disk);
		free(private);
		free(name);
	}
	
	return NULL;
}

static bool raid6_destroy_private(struct raid6_dev * private) {
	if (!--private->refcount) {
		free(private->disk);
		free(private);
	}
	return true;
}

static inline struct raid6_dev * get_raid6_dev(struct bdev * bdev) {
	return (struct raid6_dev *)bdev_get_private(bdev);
}

static inline struct raid6_component_dev * get_raid6_component_dev(struct bdev * bdev) {
	return (struct raid6_component_dev *)bdev_get_private(bdev);
}

static bool raid6_destroy(struct bdev * bdev) {
	return raid6_destroy_private(get_raid6_dev(bdev));
}

static bool raid6_component_destroy(struct bdev * bdev) {
	struct raid6_component_dev * private = get_raid6_component_dev(bdev);
	raid6_destroy_private(private->full_raid);
	free(private);
	return true;
}

#define ALGO raid6_mmxx2

extern const struct raid6_calls ALGO;

void hexdump(u8 * data,unsigned num) {
	for (unsigned y=0;y<num;y+=16) {
		eprintf("%03x:",y);
		unsigned maxx=(num-y)>16?16:(num-y);
		unsigned x;
		for (x=0;x<16;++x) {
			if (!(x&3)) {
				eprintf(" ");
				if (x) eprintf("|");
			}
			if (x<maxx) {
				eprintf(" 0x%02x",(unsigned)data[y+x]);
			} else {
				eprintf("   ");
			}
		}
		eprintf("  ");
		for (x=0;x<maxx;++x) {
			if (data[y+x]<32 || data[y+x]>127) {
				eprintf("\033[47;30m?\033[0m");
			} else {
				eprintf("%c",data[y+x]);
			}
		}
		eprintf("\n");
	}
}

#define LAYOUT_LEFT_ASYMMETRIC  0
#define LAYOUT_RIGHT_ASYMMETRIC 1
#define LAYOUT_LEFT_SYMMETRIC   2
#define LAYOUT_RIGHT_SYMMETRIC  3
#define LAYOUT_RAID4            4

// Is right?
#define IS_LAYOUT_R(layout) ((layout<4) && ( (layout&1)))
// Is left?
#define IS_LAYOUT_L(layout) ((layout<4) && (!(layout&1)))
// Is asymmetric?
#define IS_LAYOUT_A(layout) ((layout<4) && (!(layout&2)))
// Is symmetric?
#define IS_LAYOUT_S(layout) ((layout<4) && ( (layout&2)))
// Is static (i.e. RAID4)?
#define IS_LAYOUT_STATIC(layout) (layout==4)

// volatile int debug_trash;

static void raid6_get_role_mapping_for_chunk(
	struct raid6_dev * private,
	block_t chunk,
	unsigned * column2role_mapping,
	unsigned * role2column_mapping
) {
	/*
	INIT	456	didx=(block/8)%d;
	STATIC	4	pidx=(d+p-1)        ;
	LEFT	 56	pidx=(d+p-1)-s%(d+p);
	RIGHT	 56	pidx=        s%(d+p);
	ASYM	 56	q=pidx; for (i=0;i<p;++i) { if (didx>=q) ++didx; q=(q+1)%(d+p); }
	SYM	 56	didx=(pidx+p+didx)%(d+p);
	*/
	
	/*
	            RAID4 R5-LA R5-LS R5-RA R5-RS R6-LA R6-LS R6-RA R6-RS NOSUP
	chunk%5==0: 0123P 0123P 0123P P0123 P0123 Q012P Q012P PQ012 PQ012 012PQ
	chunk%5==1: 0123P 012P3 123P0 0P123 3P012 012PQ 012PQ 0PQ12 2PQ01 012PQ
	chunk%5==2: 0123P 01P23 23P01 01P23 23P01 01PQ2 12PQ0 01PQ2 12PQ0 012PQ
	chunk%5==3: 0123P 0P123 3P012 012P3 123P0 0PQ12 2PQ01 012PQ 012PQ 012PQ
	chunk%5==4: 0123P P0123 P0123 0123P 0123P PQ012 PQ012 Q012P Q012P 012PQ
	*/
	
	// Works at least for R6-LS
	
	unsigned r2c_mapping[private->num_disks];
	unsigned c2r_mapping[private->num_disks];
	unsigned idx;
	bool symmetric;
	
	if (IS_LAYOUT_STATIC(private->layout)) {
		idx=private->data_disks;
		symmetric=true;
//		eprintf("%i: idx=%u\n",__LINE__,idx);
	} else {
		if (IS_LAYOUT_L(private->layout)) {
			idx=private->num_disks-1;
//			eprintf("%i: idx=%u\n",__LINE__,idx);
		} else if (IS_LAYOUT_R(private->layout)) {
			idx=0;
//			eprintf("%i: idx=%u\n",__LINE__,idx);
		} else {
			assert(never_reached);
		}
		
		unsigned pdiff=chunk%private->num_disks;
		if (IS_LAYOUT_L(private->layout)) {
			idx-=pdiff;
//			eprintf("%i: pdiff=%u -> idx=%u\n",__LINE__,pdiff,idx);
		} else if (IS_LAYOUT_R(private->layout)) {
			idx+=pdiff;
//			eprintf("%i: pdiff=%u -> idx=%u\n",__LINE__,pdiff,idx);
		}
		symmetric=IS_LAYOUT_S(private->layout);
	}
	
//	eprintf("symmetric=%s\n",symmetric?"true":"false");
	if (!symmetric) {
		for (unsigned i=0;i<private->data_disks;++i) {
			c2r_mapping[i]=private->num_disks;
		}
	}
	
	for (unsigned i=0;i<private->parity_disks;++i) {
		c2r_mapping[private->data_disks+i]=idx;
		idx=(idx+1)%private->num_disks;
	}
	
	if (symmetric) {
		for (unsigned i=0;i<private->data_disks;++i) {
			c2r_mapping[i]=idx;
			idx=(idx+1)%private->num_disks;
		}
		for (unsigned i=0;i<private->num_disks;++i) {
			r2c_mapping[c2r_mapping[i]]=i;
		}
	} else {
		for (unsigned i=0;i<private->num_disks;++i) {
			r2c_mapping[i]=private->num_disks;
		}
		
		for (unsigned i=0;i<private->num_disks;++i) {
			if (c2r_mapping[i]!=private->num_disks) {
				r2c_mapping[c2r_mapping[i]]=i;
			}
		}
		
		unsigned didx=0;
		for (unsigned i=0;i<private->num_disks;++i) {
			if (r2c_mapping[i]==private->num_disks) {
				r2c_mapping[i]=didx++;
			}
		}
		
		for (unsigned i=0;i<private->num_disks;++i) {
			c2r_mapping[r2c_mapping[i]]=i;
		}
	}
	
	if (column2role_mapping) memcpy(column2role_mapping,c2r_mapping,sizeof(c2r_mapping));
	if (role2column_mapping) memcpy(role2column_mapping,r2c_mapping,sizeof(r2c_mapping));
}

/*
 * raid6_read_stripe
 *
 * Read one stripe of data. A stripe of data in this context is defined as one block per disk of all disks.
 * Addressing is done using chunks and stripes. A chunk is a number of consequitve stripes where the
 * organisation of mapping of data disks D0..Dn and parity disks P0..Pn to real disks is equal. (i.e. a
 * raid4 consists of exactly one chunk). Stripes are then used to address data within such a chunk.
 * 
 * struct raid6_dev * private - the devise from which to read
 * block_t chunk - the chunk to read from
 * block_t stripe - the strip within the chunk to read
 * unsigned char * * d_buffer - pointer where data of data   disks shall be written to.
 * unsigned char * * p_buffer - pointer where data of parity disks shall be written to.
 * unsigned char * * v_buffer - pointer where data of calculated parities (i.e. verify) shall be written to.
 * unsigned char * * t_buffer - pointer where test data shall be written to.
 * unsigned char * * e_buffer - pointer to error map. If NULL, any parity mismatch will result in an IO error.
 * unsigned char * * i_buffer - pointer to ignore map. Any parity mismatch where a bit of i_buffer is set, will be ignored.
 * 
 * d_buffer and p_buffer must be non-NULL with private->data_disks entries in d_buffer and 2 (sic!) entries in p_buffer
 * pointing to block size bytes of DISTINCT(!) buffer space each.
 * If private->verify_parity is set, v_buffer must be non-NULL with 2 entries pointing to block size bytes of DISTINCT(!) buffer
 * space. In raid6, then t_buffer must also be non-NULL with 2 entries pointing to block size bytes of DISTINCT(!) buffer
 * space for the artificial kick feature to repair single parity mismatches.
 * When e_buffer is non-NULL, it must contain private->data_disks entries pointing to block size bytes of DISTINCT(!) buffer
 * space. Any parity mismatch, which couldn't be fixed by artificial kicking a single disk will be recorded here. In this
 * case, the function will successfully return on parity mismatch. Of e_buffer is NULL, any parity mismatch which couldn't
 * be fixed will result in a failure (to be interpreted as IO error).
 * When i_buffer is non-NULL, it must contain private->data_disks entries pointing to block size bytes of DISTINCT(!) buffer
 * space. As an exception, e_buffer[i] may equal to i_buffer[i]. When i_buffer is set, any unfixable parity mismatch will be
 * ignored, if the corresponding bit in i_buffer[i][byte] is set. Since parity mismatches won't report an error, if e_buffer
 * is set, this only affects error reporting using the logging facility (especially whether such error blocks get dumped or
 * not.
 */
static bool raid6_read_stripe(
	struct raid6_dev * private,
	block_t chunk,
	block_t stripe,
	unsigned char * const * d_buffer_ptr,
	unsigned char * const * e_buffer,
	const unsigned char * const * i_buffer
	, const char * reason
) {
	if (debug_striped_reader) eprintf("\t\t\traid6_read_stripe(chunk=%llu,stripe=%llu)\n",(unsigned long long)chunk,(unsigned long long)stripe);
	
	off_t block_size=bdev_get_block_size(private->disk[0].bdev);
	
	unsigned char * p_buffer[2];
	unsigned char * v_buffer[2];
	unsigned char * t_buffer[2];
	
	for (unsigned i = 0; i < 2; ++i) {
		// Parity may be needed for recovery, so set it unconditionally
		p_buffer[i] = private->p_space + i * block_size;
		if (private->may_verify_parity) {
			v_buffer[i] = private->v_space + i * block_size;
			t_buffer[i] = private->t_space + i * block_size;
		} else {
			v_buffer[i] = NULL;
			t_buffer[i] = NULL;
		}
	}
	
	unsigned char * d_buffer[private->data_disks];
	bool need_data[private->data_disks];
	for (unsigned i = 0; i < private->data_disks; ++i) {
		if ((need_data[i] = !!d_buffer_ptr[i])) {
			d_buffer[i] = d_buffer_ptr[i];
		} else {
			d_buffer[i] = private->d_space + i * block_size;
		}
	}
	
	unsigned mapping[private->num_disks];
	
	raid6_get_role_mapping_for_chunk(private,chunk,mapping,NULL);
	
	u8 * dataptrs[private->num_disks];
	unsigned num_failed=0;
	
	/*
	 * We need only a failmap for at most num parity disk disks as more 
	 * failed disks make data nonrecoverably anyways ...
	 */
	unsigned failmap[private->parity_disks];
	for (unsigned i=0;i<private->data_disks;++i) {
		unsigned d = i;
		dataptrs[d] = d_buffer[i];
		if (private->disk[mapping[d]].layoutmapping!=-1) {
			struct bdev * component=private->disk[private->disk[mapping[d]].layoutmapping].bdev;
			
			/*
			 * TODO:
			 * 
			 * Mark, whether this is the actual data requested or if this data was only read
			 * for verify_parity Also, at the same time skip reading data all together, if it
			 * wasn't requested, verify_parity is false and nothing needs reconstructing.
			 */
			char * tmp = mprintf("D%u(chunk[%llu]/stripe[%llu]) for %s"
				, i
				, (unsigned long long)chunk
				, (unsigned long long)stripe
				, reason
			);
			if (1!=bdev_read(
				component,
				stripe+chunk*private->chunk_size_Blocks,
				1,
				dataptrs[d]
				, tmp
			)) {
				free(tmp);
				if (raid6_notify_read_error)
				NOTIFYF("%s: Couldn't read D%u from component disk %s (role %u, column %u) at offset %llu (0x%llx)"
					,bdev_get_name(private->this)
					,i
					,bdev_get_name(component)
					,mapping[d]
					,i
					,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
					,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
				);
				goto data_failed;
			}
			free(tmp);
			/*
			eprintf(
				"Data read from data disk %s:\n"
				,bdev_get_name(component)
			);
			hexdump(dataptrs[i],stripe_size_Bytes);
			*/
		} else {
data_failed:
			if (num_failed==private->parity_disks) {
				ERROR("Too many failed disks.");
				return false;
			}
			failmap[num_failed++]=i;
		}
	}
	for (unsigned i=0;i<private->parity_disks;++i) {
		unsigned d=private->data_disks+i;
		dataptrs[d] = p_buffer[i];
		// debug_trash=mapping[d];
		if (private->disk[mapping[d]].layoutmapping!=-1) {
			struct bdev * component=private->disk[private->disk[mapping[d]].layoutmapping].bdev;
			
			// TODO: As above.
			char * tmp = mprintf("P%u(chunk[%llu]/stripe[%llu]) for %s"
				, i
				, (unsigned long long)chunk
				, (unsigned long long)stripe
				, reason
			);
			
			if (1!=bdev_read(
				component,
				stripe+chunk*private->chunk_size_Blocks,
				1,
				dataptrs[d]
				, tmp
			)) {
				free(tmp);
				if (raid6_notify_read_error)
				NOTIFYF("%s: Couldn't read P%u from component disk %s (role %u, column %u) at offset %llu (0x%llx)"
					,bdev_get_name(private->this)
					,i
					,bdev_get_name(component)
					,mapping[d]
					,d
					,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
					,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
				);
				goto parity_failed;
			}
			free(tmp);
			/*
			eprintf(
				"Data read from parity disk %s:\n"
				,bdev_get_name(component)
			);
			hexdump(dataptrs[d],stripe_size_Bytes);
			*/
		} else {
parity_failed:
			if (num_failed==private->parity_disks) {
				ERROR("Too many failed disks.");
				return false;
			}
			failmap[num_failed++]=d;
		}
	}
	unsigned nd=private->num_disks;
	/*
	 * FIXME: This is an ugly hack to just use the raid 6 recovery code 
	 * for raid 5 recovery. There should be a better interface to allow 
	 * for arbitrary number of parity disks, or we need a wrapper for 
	 * raid 5 which calls the raid 6 function with num discs incremented 
	 * by one.
	 */
	if (private->parity_disks!=2) nd++;
//	eprintf("num_failed=%u\n",num_failed);
	int runs=0;
	bool silently=false;
	
retry_main_recovery:
	assert(++runs<3);
	if (num_failed) {
/*
		NOTIFY("Couldn't read all disks, recovering missing ones.");
*/
//		if (private->can_recover) {
			ALGO.recover(nd,num_failed,failmap,block_size,dataptrs);
/*		} else {
			ERROR("raid6: Couldn't read all disks and can't recover due to catastropic mode.");
			errno=EIO;
			return false;
		} */
	}
	bool do_verify_parity = false;
	if (private->verify_parity) {
		do_verify_parity = true;
	} else if (!private->events_in_sync) {
		bool need_recovery = false;
		for (unsigned i = 0; i < private->data_disks; ++i) {
			if (!need_data[i]) {
				continue;
			}
			int d = private->disk[mapping[i]].layoutmapping;
			if (d == -1) {
				need_recovery = true;
				continue;
			}
			if (private->disk[mapping[d]].events != private->max_events) {
				do_verify_parity = true;
				break;
			}
		}
		if (!do_verify_parity && need_recovery) {
			// FIXME: In RAID6, if exactly ONE disk has events != max_events and exactly another one has failed, we might just assume the one events != max_events being kicked and skip verify_parity
			do_verify_parity = !private->events_in_sync;
			/*
			for (unsigned i = 0; i < private->num_disks; ++i) {
				int d = private->disk[mapping[i]].layoutmapping;
				if (d == -1) {
					continue;
				}
				if (private->disk[mapping[d]].events != private->max_events) {
					do_verify_parity = true;
					break;
				}
			}
			*/
		}
	}
	if (do_verify_parity && num_failed != private->parity_disks) {
/*
		if (num_failed) {
			NOTIFY("Now rechecking parity bits.");
		}
*/
		u8 * testing_dataptrs[nd];
		for (unsigned i = 0; i < private->data_disks; ++i) {
			unsigned d = i;
			testing_dataptrs[d] = dataptrs[i];
		}
		for (unsigned i=0;i<2;++i) {
			unsigned d = private->data_disks + i;
			testing_dataptrs[d] = v_buffer[i];
		}
		ALGO.gen_syndrome(nd, block_size, testing_dataptrs);
		unsigned parity_mismatch=0;
		for (unsigned i=0;i<private->parity_disks;++i) {
			parity_mismatch += !!memcmp(p_buffer[i], v_buffer[i], block_size);
		}
		if (!parity_mismatch) {
			if (num_failed) {
				assert_never_reached(); // If you hit this so some rechecks, that this code is still valid.
/*
				NOTIFY("No parity mismatch found (so, the reconstructed data is valid).");
*/
				/*
				 * We physically couldn't read at least one disk, 
				 * but we could recover it and we know, that the 
				 * recovered data are clean.
				 * So, we write it back to disk.
				 */
				for (unsigned f=0;f<num_failed;++f) {
					unsigned d=failmap[f];
					if (private->disk[mapping[d]].layoutmapping==-1) {
						continue;
					}
					struct bdev * component=private->disk[private->disk[mapping[d]].layoutmapping].bdev;
					unsigned i;
					
					if (d<private->data_disks) {
						i=d;
					} else {
						i=d-private->data_disks;
					}
					
					char * msg;
					
					if (1!=bdev_write(
						component,
						stripe+chunk*private->chunk_size_Blocks,
						1,
						dataptrs[d]
					)) {
						msg="%s: Couldn't write back recovered %c%u to component disk %s (role %u, column %u) at offset %llu (0x%llx)";
						silently=false; // Never be silent on error
					} else {
						msg="%s: Recovered %c%u was written back to component disk %s (role %u, column %u) at offset %llu (0x%llx)";
					}
					
					if (!silently) {
						ERRORF(msg
							,bdev_get_name(private->this)
							,(d<private->data_disks)?'D':'P'
							,i
							,bdev_get_name(component)
							,mapping[d]
							,d
							,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
							,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
						);
					}
				}
			}
		} else { // if (!parity_mismatch)
			/*
			 * Here, we don't know yet wether we are able to reconstruct (and hence can't check for silently_fix_next as well).
			if (num_failed) {
				NOTIFY("Ouch, parities don't match, so we can't trust the reconstructed data to be valid.");
			}
			*/
			/*
			 * We have at least one parity disk which contains 
			 * other parities as the data disks suggest. So we 
			 * try any permutation of additional failed disks, 
			 * re-recover, re-generate syndromes and re-check, 
			 * in the hope that one of those setups reflects a 
			 * consistent image.
			 */
			unsigned num_avail=private->num_disks-num_failed;
			unsigned avail[num_avail];
			for (unsigned i = 0, k = 0; i < num_avail; ++i) {
				for (unsigned j = 0; j < num_failed; ++j) {
					if (k == failmap[j]) {
						if (++k == failmap[j = 0]) ++k;
					}
				}
				avail[i] = k++;
			}
//			eprintf("avai[]={");
//			for (i=0;i<num_avail;++i) {
//				if (i) eprintf(",");
//				eprintf("%u",avail[i]);
//			}
//			eprintf("}\n");
			unsigned art_num_failed=0;
			unsigned art_failmap[private->parity_disks];
			unsigned num_solutions=0;
			char * first_solution_tmp=NULL;
			unsigned first_solution_failmap[private->parity_disks];
			unsigned char first_solution_data[private->parity_disks*block_size];
//			eprintf("%s:%s:%i: silently := false\n",__FILE__,__FUNCTION__,__LINE__);
			while (!first_solution_tmp && ++art_num_failed<(private->parity_disks-num_failed)) {
				assert_never_reached(); // If you hit this so some rechecks, that this code is still valid.
//				eprintf("art_num_failed=%u\n",art_num_failed);
				bool valid_permutation=true;
				art_failmap[0]=0;
				unsigned i = 1;
				unsigned l = 0;
				while (valid_permutation) {
//					eprintf("while (valid_permutation)\n");
regen_permutation:
//					eprintf("i=%u\n",i);
					for (;i<art_num_failed;++i) {
						art_failmap[i]=art_failmap[i-1]+1;
					}
					if (art_failmap[i-1]>=num_avail) {
						if (!valid_permutation) break;
						++art_failmap[l];
						i=l+1;
						if (l) --l; else valid_permutation=false;
//						eprintf("goto regen_permutation\n");
						goto regen_permutation;
					}
					valid_permutation=true;
//					eprintf("valid_permutation:=true\n");
					
					u8 * parity_ptr[private->parity_disks];
					for (i=0;i<private->parity_disks;++i) {
						testing_dataptrs[private->data_disks + i]
						= parity_ptr[i]
						= p_buffer[i];
					}
					
					for (i=0;i<art_num_failed;++i) {
						unsigned d=failmap[num_failed+i]=avail[art_failmap[i]];
						testing_dataptrs[d] = t_buffer[i];
						if (d>=private->data_disks) {
							parity_ptr[d-private->data_disks]
							= t_buffer[i];
						}
					}
					
					ALGO.recover(nd,num_failed+art_num_failed,failmap,block_size,testing_dataptrs);
					for (i=0;i<2;++i) {
						testing_dataptrs[private->data_disks + i] = v_buffer[i];
					}
					ALGO.gen_syndrome(nd, block_size, testing_dataptrs);
					
					for (i=0;i<art_num_failed;++i) {
						testing_dataptrs[failmap[num_failed+i]]=dataptrs[failmap[num_failed+i]];
					}
					
					parity_mismatch=0;
					for (i=0;i<private->parity_disks;++i) {
						parity_mismatch += !!memcmp(parity_ptr[i], v_buffer[i], block_size);
					}
					
					if (!parity_mismatch) {
						if (!num_solutions++) {
							first_solution_tmp=mstrcpy("");
							silently=true;
//							eprintf("%s:%s:%i: silently := true\n",__FILE__,__FUNCTION__,__LINE__);
							for (i=0;i<art_num_failed;++i) {
								char * t=mprintf("%s%scolumn %u (role %u)",first_solution_tmp,i?" ":"",art_failmap[i],mapping[art_failmap[i]]);
								free(first_solution_tmp);
								first_solution_tmp=t;
								if (!private->disk[mapping[art_failmap[i]]].silently_fix_next) {
									eprintf(
										"column %u (role %u) prevents from being silent\n"
										,art_failmap[i]
										,mapping[art_failmap[i]]
									);
//									eprintf("%s:%s:%i: silently := false\n",__FILE__,__FUNCTION__,__LINE__);
									silently=false;
								}
							}
							
							memcpy(
								first_solution_failmap
								,art_failmap
								,sizeof(first_solution_failmap)
							);
							memcpy(
								first_solution_data
								,t_buffer
								,art_num_failed*block_size
							);
						}
					}
					
					for (i=art_num_failed;i--;) {
						if (++art_failmap[i]<num_avail) {
							break;
						}
						if (!i) {
							valid_permutation=false;
						}
					}
//					eprintf("after last loop: i=%i\n",i);
					l=i-1;
					++i;
				}
			}
			if (first_solution_tmp) {
				assert_never_reached(); // If you hit this so some rechecks, that this code is still valid.
//				eprintf("%s:%s:%i: silently == %s\n",__FILE__,__FUNCTION__,__LINE__,silently?"true":"false");
				if (!silently) {
//				if (art_num_failed!=1 
//				||  mapping[avail[first_solution_failmap[0]]]!=4) {
					NOTIFYF(
						"%s: Parity mismatch at stripe %llu of chunk %llu offset %llu [%llu..%llu] fixed by artificially kicking disks %s [art_num_failed=%u]."
						,bdev_get_name(private->this)
						,(unsigned long long)stripe
						,(unsigned long long)chunk
						,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
						,(unsigned long long)(private->data_disks*( chunk   *private->chunk_size_Blocks)  )
						,(unsigned long long)(private->data_disks*((chunk+1)*private->chunk_size_Blocks)-1)
						,first_solution_tmp
						,art_num_failed
					);
				}
				
				/*
				 * We could recover a consistent view by kicking one or more disks but 
				 * yet remaining at least a little bit redundancy.
				 * So, we write it back to disk.
				 */
				for (unsigned f=0;f<art_num_failed;++f) {
					if (first_solution_failmap[f]<private->data_disks) {
						unsigned i=first_solution_failmap[f];
						// dataptrs[i]=d_buffer+i*block_size; 
						assert(private->disk[mapping[i]].layoutmapping!=-1);
						struct bdev * component=private->disk[private->disk[mapping[i]].layoutmapping].bdev;
						
						if (1!=bdev_write(
							component,
							stripe+chunk*private->chunk_size_Blocks,
							1,
							first_solution_data+f*block_size
						)) {
							ERRORF("%s: Couldn't write back corrected D%u to component disk %s (role %u, column %u) at offset %llu (0x%llx)"
								,bdev_get_name(private->this)
								,i
								,bdev_get_name(component)
								,mapping[i]
								,i
								,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
								,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
							);
						}
					} else {
						unsigned d=first_solution_failmap[f];
						unsigned i=d-private->data_disks;
						// dataptrs[d]=p_buffer+i*block_size; // stripe_size_Bytes
						assert(private->disk[mapping[d]].layoutmapping!=-1);
						struct bdev * component=private->disk[private->disk[mapping[d]].layoutmapping].bdev;
						if (1!=bdev_write(
							component,
							stripe+chunk*private->chunk_size_Blocks,
							1,
							first_solution_data+f*block_size
						)) {
							ERRORF("%s: Couldn't write back corrected P%u to component disk %s (role %u, column %u) at offset %llu (0x%llx)"
								,bdev_get_name(private->this)
								,i
								,bdev_get_name(component)
								,mapping[d]
								,d
								,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
								,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
							);
						}
					}
				}
				
				free(first_solution_tmp);
//				NOTIFYF("raid6: num_failed=%u",num_failed);
//				NOTIFYF("raid6: art_num_failed=%u",art_num_failed);
				// while (art_num_failed--) {
				//	failmap[num_failed++]=art_failmap[art_num_failed];
/*
				for (i=0;i<num_failed;++i) {
					DEBUGF("failmap[%u]=%u",i,failmap[i]);
				}
*/
				for (unsigned i=0;i<art_num_failed;++i) {
/*
					DEBUGF("mapping[avail[first_solution_failmap[%u]=%u]=%u]=%u"
						,                                     i
						,              first_solution_failmap[i]
						,        avail[first_solution_failmap[i]]
						,mapping[avail[first_solution_failmap[i]]]
					);
*/
					failmap[num_failed++]=avail[first_solution_failmap[i]];
				}
				goto retry_main_recovery;
			} // if (first_solution_tmp)
			
			/*
			 * recalculate verify buffer
			 *
			 * Right now, I'm not perfectly sure whether this is needed or not.
			 * So, better be save than sorry and regen stuff.
			 * 
			 * TODO: Check this and maybe remove this block again.
			 */
			{
				u8 * testing_dataptrs[nd];
				for (unsigned i = 0; i < private->data_disks; ++i) {
					testing_dataptrs[i] = dataptrs[i];
				}
				for (unsigned i = 0; i < 2; ++i) {
					testing_dataptrs[private->data_disks + i] = v_buffer[i];
				}
				ALGO.gen_syndrome(nd, block_size, testing_dataptrs);
			}
			
			bool report_mismatch = !i_buffer;
			
			if (i_buffer) {
				for (unsigned b = 0; b < block_size; ++b) {
					u8 diff_mask = 0;
					for (unsigned p = 0; p < private->parity_disks; ++p) {
						diff_mask |= p_buffer[p][b] ^ v_buffer[p][b];
					}
					for (unsigned d = 0; d < private->data_disks; ++d) {
						if (i_buffer[d] && (i_buffer[d][b] & diff_mask) != diff_mask) {
							report_mismatch = true;
							goto out_report_test;
						}
					}
				}
out_report_test: ;
			}
			
			if (report_mismatch) {
				NOTIFYF(
					"%s: Parity mismatch at stripe %llu of chunk %llu offset %llu [%llu..%llu] WHICH COULDN'T BE FIXED."
					,bdev_get_name(private->this)
					,(unsigned long long)stripe
					,(unsigned long long)chunk
					,(unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
					,(unsigned long long)(private->data_disks*(stripe+chunk*private->chunk_size_Blocks))
					,(unsigned long long)(private->data_disks*(stripe+chunk*private->chunk_size_Blocks+1)-1)
				);
			}
			
			if (raid6_dump_blocks_on_parity_mismatch && report_mismatch) {
				for (unsigned d = 0, nf = 0; d < private->data_disks + private->parity_disks; ++d) {
					char dp;
					unsigned i;
					u8 * dataptr;
					
					if (d < private->data_disks) {
						dp = 'D';
						i = d;
						dataptr = d_buffer[i];
					} else {
						dp = 'P';
						i = d - private->data_disks;
						dataptr = p_buffer[i];
					}
					
					if (private->disk[mapping[d]].layoutmapping == -1) {
						eprintf(
							"%s: Can't dump data for block %llu on missing disk (role %u, column %u) representing %c%u of stripe %llu of chunk %llu.\n"
							, bdev_get_name(private->this)
							, (unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
							, mapping[d]
							, d
							, dp
							, i
							, (unsigned long long)stripe
							, (unsigned long long)chunk
						);
					} else if (nf < num_failed && failmap[nf] == d) {
						++nf;
						eprintf(
							"%s: Can't dump data for block %llu on disk %s (role %u, column %u) representing %c%u of stripe %llu of chunk %llu after read error.\n"
							, bdev_get_name(private->this)
							, (unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
							, bdev_get_name(private->disk[private->disk[mapping[d]].layoutmapping].bdev)
							, mapping[d]
							, d
							, dp
							, i
							, (unsigned long long)stripe
							, (unsigned long long)chunk
						);
					} else {
						eprintf(
							"%s: Dump of block %llu on disk %s (role %u, column %u) representing %c%u of stripe %llu of chunk %llu (events = %lu, max_events = %lu:\n"
							, bdev_get_name(private->this)
							, (unsigned long long)(stripe+chunk*private->chunk_size_Blocks)
							, bdev_get_name(private->disk[private->disk[mapping[d]].layoutmapping].bdev)
							, mapping[d]
							, d
							, dp
							, i
							, (unsigned long long)stripe
							, (unsigned long long)chunk
							, (unsigned long)private->disk[mapping[d]].events
							, (unsigned long)private->max_events
						);
						unsigned byte = 0;
						size_t sl;
						char * bs_str = mprintf_sl(&sl, "%llx", (unsigned long long)(block_size - 1));
						free(bs_str);
						for (unsigned line = 0; line < block_size; line += 16) {
							eprintf("%0*x:", (int)sl, line);
							
							unsigned saved_byte = byte;
							
							for (unsigned column = 0; column < 16 && byte < block_size; ++column, ++byte) {
								char * extra;
								if (!(column & 3)) {
									if (column) {
										extra = " | ";
									} else {
										extra = "  ";
									}
								} else {
									extra = " ";
								}
								
								enum mismatch_type {
									MATCH = 0,
									IGNORE,
									LOCAL_IGNORE,
									ERROR,
									ASSUME_CORRECT,
								} mt;
								char * mt_color[] = {
									"32",
									"33;1",
									"31;1",
									"31",
									"32;1",
								};
								
								u8 diff_mask = 0;
								for (unsigned p = 0; p < private->parity_disks; ++p) {
									diff_mask |= p_buffer[p][byte] ^ v_buffer[p][byte];
								}
								if (!diff_mask) {
									mt = MATCH;
								} else {
									if (!private->verify_parity && private->disk[mapping[d]].events == private->max_events) {
										mt = ASSUME_CORRECT;
									} else {
										if (!i_buffer) {
											if (d >= private->data_disks || need_data[d]) {
												mt = ERROR;
											} else {
												mt = LOCAL_IGNORE;
											}
										} else {
											mt = IGNORE;
											for (unsigned dd = 0; dd < private->data_disks; ++dd) {
												if (i_buffer[dd] && (i_buffer[dd][byte] & diff_mask) != diff_mask) {
													if (dd == d) {
														mt = ERROR;
														break;
													}
													mt = LOCAL_IGNORE;
												}
											}
										}
									}
								}
								
								eprintf("%s\033[%sm%02x\033[0m", extra, mt_color[mt], (unsigned)dataptr[byte]);
							}
							
							byte = saved_byte;
							
							eprintf("  ");
							
							for (unsigned column = 0; column < 16 && byte < block_size; ++column, ++byte) {
								if (dataptr[byte] >= 32 && dataptr[byte] < 127) {
									eprintf("%c", dataptr[byte]);
								} else {
									eprintf(".");
								}
							}
							
							eprintf("\n");
						}
					}
				}
			}
			
			if (!i_buffer && !e_buffer) {
				errno = EIO;
				return false;
			}
			bool ok = true;
			
			u8 all_diff_mask = 0;
			for (unsigned b = 0; b < block_size; ++b) {
				u8 diff_mask = 0;
				for (unsigned p = 0; p < private->parity_disks; ++p) {
					diff_mask |= p_buffer[p][b] ^ v_buffer[p][b];
				}
				for (unsigned d = 0; d < private->data_disks; ++d) {
					if (i_buffer) {
						if (!i_buffer[d]) {
							continue;
						}
						if ((i_buffer[d][b] & diff_mask) != diff_mask) {
							ok = false;
						}
					} else {
						all_diff_mask |= diff_mask;
					}
					if (e_buffer && e_buffer[d]) {
						e_buffer[d][b] = diff_mask;
					}
				}
			}
			if (!i_buffer) {
				if (all_diff_mask) {
					ok = false;
				}
			}
			
			if (!e_buffer && !ok) {
				errno = EIO;
				return false;
			}
			
			return true;
		} // if (!parity_mismatch), else
	} else { // if (do_verify_parity && num_failed!=private->parity_disks)
		if (e_buffer) {
			for (unsigned i = 0; i < private->data_disks; ++i) {
				if (e_buffer[i]) {
					memset(e_buffer[i], 0, block_size);
				}
			}
		}
	} // if (do_verify_parity && num_failed!=private->parity_disks), else
	return true;
} // static bool raid6_read_stripe()

static block_t raid6_component_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data, const char * reason) {
	ignore(reason);
	assert_never_reached(); // If you hit this so some rechecks, that this code is still valid.
	struct raid6_component_dev * real_private = get_raid6_component_dev(bdev);
	struct raid6_dev * private = real_private->full_raid;
	
	off_t bs=bdev_get_block_size(private->disk[0].bdev);
	block_t cs=private->chunk_size_Blocks;
	u8 d_buffer[private->data_disks * bs];
	u8 p_buffer[                  2 * bs];
	u8 * d_buffer_ptr[private->data_disks];
	u8 * p_buffer_ptr[private->parity_disks];
	
	for (unsigned i = 0; i < private->data_disks; ++i) {
		d_buffer_ptr[i] = d_buffer + i * bs;
	}
	
	for (unsigned i = 0; i < 2; ++i) {
		p_buffer_ptr[i] = p_buffer + i * bs;
	}
	
	unsigned role2column[private->num_disks];
	block_t prev_chunk=first/cs-1;
	block_t retval=0;
	bool saved_verify_parity = private->verify_parity;
	private->verify_parity = false;
	for (;num--;++first,++retval) {
		block_t chunk=first/cs;
		block_t stripe=first%cs;
		unsigned i = role2column[real_private->role];
		bool is_parity = i >= private->data_disks;
		if (is_parity) {
			i -= private->data_disks;
			assert(i < private->parity_disks);
			p_buffer_ptr[i] = data;
		} else {
			d_buffer_ptr[i] = data;
		}
		assert_never_reached(); // If you hit this so some rechecks, that this code is still valid.
		// if (!raid6_read_stripe(private, chunk, stripe, d_buffer_ptr, p_buffer_ptr, NULL, NULL, NULL, NULL, reason)) {
		if (stripe || p_buffer_ptr[0] || d_buffer_ptr[0]) {
			private->verify_parity = saved_verify_parity;
			return retval;
		}
		if (is_parity) {
			p_buffer_ptr[i] = p_buffer + i * bs;
		} else {
			d_buffer_ptr[i] = d_buffer + i * bs;
		}
		if (prev_chunk!=chunk) {
			raid6_get_role_mapping_for_chunk(private,chunk,NULL,role2column);
			prev_chunk=chunk;
		}
		/*
		eprintf("mapping for chunk %llu:",(unsigned long long)chunk);
		for (unsigned i=0;i<private->num_disks;++i) {
			eprintf(" %u",role2column[i]);
		}
		eprintf("\n");
		*/
		data+=bs;
	}
	private->verify_parity = saved_verify_parity;
	return retval;
}

static block_t raid6_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data, const char * reason) {
//	eprintf("raid6_read: ");
	return raid6_do_read(bdev, first, num, data, NULL, NULL, reason);
}

static block_t raid6_short_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const char * reason) {
	return raid6_do_read(bdev, first, num, data, error_map, NULL, reason);
}

static block_t raid6_disaster_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const u8 * ignore_map, const char * reason) {
	return raid6_do_read(bdev, first, num, data, error_map, ignore_map, reason);
}

static block_t raid6_do_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const u8 * ignore_map, const char * reason) {
	struct raid6_dev * private = get_raid6_dev(bdev);
//	eprintf("raid6_short_read(%p,%llu,%llu,%p,%p)\n",(void*)private,(unsigned long long)first,(unsigned long long)num,data,error_map);
	
	unsigned bs=bdev_get_block_size(private->disk[0].bdev);
	
	/*
	 * A raid consisting of:
	 *
	 *  - 3 data disks
	 *  - two parity disks 
	 *  - layout LEFT_SYMMETRIC
	 *  - chunk_size = 8 * block_size_Bytes (8 * 512 = 4096 in most cases)
	 *
	 * Will have the following on-disk structure.
	 *
	 * Chunk 0:    P1   D0   D1   D2   P0
	 * Stripe  0: Q000 D000 D008 D016 P000 \
	 * Stripe  1: Q001 D001 D009 D017 P001  |
	 * Stripe  2: Q002 D002 D010 D018 P002  |
	 * Stripe  3: Q003 D003 D011 D019 P003  \ chunk_size=8*block_size
	 * Stripe  4: Q004 D004 D012 D020 P004  /
	 * Stripe  5: Q005 D005 D013 D021 P005  |
	 * Stripe  6: Q006 D006 D014 D022 P006  |
	 * Stripe  7: Q007 D007 D015 D023 P007 /
	 *            ---- ---- ---- ---- ----
	 * Chunk 1:    D0   D1   D2   P0   P1
	 * Stripe  8: D024 D032 D040 P008 Q008
	 * Stripe  9: D025 D033 D041 P009 Q009
	 * Stripe 10: D026 D034 D042 P010 Q010
	 * Stripe 11: D027 D035 D043 P011 Q011
	 * Stripe 12: D028 D036 D044 P012 Q012
	 * Stripe 13: D029 D037 D045 P013 Q013
	 * Stripe 14: D030 D038 D046 P014 Q014
	 * Stripe 15: D031 D039 D047 P015 Q015
	 *            ---- ---- ---- ---- ----
	 * Chunk 2:    D1   D2   P0   P1   D0
	 * Stripe 16: D056 D064 P016 Q016 D048
	 * Stripe 17: D057 D065 P017 Q017 D049
	 * Stripe 18: D058 D066 P018 Q018 D050
	 * Stripe 19: D059 D067 P019 Q019 D051
	 * Stripe 20: D060 D068 P020 Q020 D052
	 * Stripe 21: D061 D069 P021 Q021 D053
	 * Stripe 22: D062 D070 P022 Q022 D054
	 * Stripe 23: D063 D071 P023 Q023 D055
	 *            ---- ---- ---- ---- ----
	 * Chunk 3:    D2   P0   P1   D0   D1
	 * Stripe 24: D088 P024 Q024 D072 D080
	 *              .    .    .    .    .
	 *              .    .    .    .    .
	 *              .    .    .    .    .
	 * Stripe 31: D095 P031 Q031 D079 D087
	 *            ---- ---- ---- ---- ----
	 * Chunk 4:    P0   P1   D0   D1   D2
	 * Stripe 32: P032 Q032 D096 D104 D112
	 *              .    .    .    .    .
	 *              .    .    .    .    .
	 *              .    .    .    .    .
	 * Stripe 39: P039 Q039 D103 D111 D119
	 *            ==== ==== ==== ==== ====
	 * Chunk 5:    P1   D0   D1   D2   P0
	 * Stripe 40: Q040 D120 D128 D136 P040
	 * ...
	 */
	
	block_t full_chunk_size_Blocks=private->data_disks*private->chunk_size_Blocks;
	
	block_t frst_chunk,frst_full_chunk,frst_block_in_frst_chunk,frst_disk_in_frst_chunk,frst_stripe_in_frst_chunk; // ,frst_chunk_num_stripes;
	block_t last_chunk,last_full_chunk,last_block_in_last_chunk,last_disk_in_last_chunk,last_stripe_in_last_chunk; // ,last_chunk_num_stripes;
	
	/*
	 * First: Figure out which chunks contain the first/last block 
	 * respectively.
	 */
	frst_chunk              =first/full_chunk_size_Blocks;
	frst_block_in_frst_chunk=first%full_chunk_size_Blocks;
	last_chunk              =(first+num-1)/full_chunk_size_Blocks;
	last_block_in_last_chunk=(first+num-1)%full_chunk_size_Blocks;
	
	/*
	 * Now figure out the disk ROLE which contains the first/last block 
	 * respectively.
	 */
	frst_disk_in_frst_chunk=frst_block_in_frst_chunk/private->chunk_size_Blocks;
	last_disk_in_last_chunk=last_block_in_last_chunk/private->chunk_size_Blocks;
	
	/*
	 * Finally figure out the first/last stripe needed. The previous 
	 * values have been just straight forward, this ones are not (think 
	 * about it yourself, it's hard to explain but quite logical).
	 */
	frst_stripe_in_frst_chunk=frst_block_in_frst_chunk%private->chunk_size_Blocks;
	last_stripe_in_last_chunk=last_block_in_last_chunk%private->chunk_size_Blocks;
	
	if (!frst_disk_in_frst_chunk
	&&  !frst_stripe_in_frst_chunk) {
		frst_full_chunk=frst_chunk;
	} else {
		frst_full_chunk=frst_chunk+1;
	}
	
	if (last_disk_in_last_chunk  ==private->data_disks-1
	&&  last_stripe_in_last_chunk==private->chunk_size_Blocks-1) {
		last_full_chunk=last_chunk;
	} else {
		last_full_chunk=last_chunk-1;
	}
	
#if 0
	/*
	 * If we need only blocks from the last disk ROLE in the first chunk, 
	 * we're done after reaching the last stripe.
	 */
		/*
		 * If we pass the chunk boundary, we need all stripes.
		 * If not, we have to check, wether first disk role and last 
		 * disk role are the same. If not, we need the last stripe of 
		 * first disk role and then the first stripe of the next disk 
		 * role -> we need all stripes.
		 */
	/*
	 * Essentially the same for last_stripe.
	 */
#endif // #if 0
	
	u8 * d_buffer_ptr[private->data_disks];
	u8 * e_buffer_ptr[private->data_disks];
	const u8 * i_buffer_ptr[private->data_disks];
	
	block_t frst_stripe,frst_disk;
	unsigned num_disks,num_stripes,decrement_stripe;
	
	if (debug_striped_reader) {
		eprintf("last_stripe_in_last_chunk=%llu\n",(unsigned long long)last_stripe_in_last_chunk);
		eprintf("last_disk_in_last_chunk=%llu\n",(unsigned long long)last_disk_in_last_chunk);
	}
	
	block_t retval=0;
	for (block_t chunk=frst_chunk;chunk<=last_chunk;++chunk) {
		if (debug_striped_reader) eprintf("\tchunk=%llu -> ",(unsigned long long)chunk);
		if (chunk<frst_full_chunk) {
			if (chunk>last_full_chunk) {
				frst_stripe=frst_stripe_in_frst_chunk;
				frst_disk=frst_disk_in_frst_chunk;
				if (frst_disk!=last_disk_in_last_chunk) {
					num_disks=last_disk_in_last_chunk-frst_disk;
					num_stripes=1+last_stripe_in_last_chunk+private->chunk_size_Blocks-frst_stripe;
					if (num_stripes>private->chunk_size_Blocks) {
						if (debug_striped_reader) eprintf("A");
						num_stripes=private->chunk_size_Blocks;
						++num_disks;
					} else {
						if (debug_striped_reader) eprintf("B");
					}
					decrement_stripe=last_stripe_in_last_chunk;
					if (frst_disk+1!=last_disk_in_last_chunk) {
						if (debug_striped_reader) eprintf("C: ");
						num_stripes=8;
					} else {
						if (debug_striped_reader) eprintf("D: ");
					}
				} else {
					if (debug_striped_reader) eprintf("E: ");
					num_disks=1;
					num_stripes=1+last_stripe_in_last_chunk-frst_stripe;
					decrement_stripe=private->chunk_size_Blocks; // num_stripes;
				}
			} else {
				frst_stripe=frst_stripe_in_frst_chunk;
				frst_disk=frst_disk_in_frst_chunk;
				if (frst_disk!=private->data_disks-1) {
					if (debug_striped_reader) eprintf("F: ");
					num_stripes=private->chunk_size_Blocks;
				} else {
					if (debug_striped_reader) eprintf("G: ");
					num_stripes=private->chunk_size_Blocks-frst_stripe;
				}
				num_disks=private->data_disks-frst_disk;
				decrement_stripe=private->chunk_size_Blocks-1;
			}
		} else {
			if (chunk>last_full_chunk) {
				frst_stripe=0;
				frst_disk=0;
				if (last_disk_in_last_chunk) {
					if (debug_striped_reader) eprintf("H: ");
					num_stripes=private->chunk_size_Blocks;
				} else {
					if (debug_striped_reader) eprintf("I: ");
					num_stripes=1+last_stripe_in_last_chunk;
				}
				num_disks=1+last_disk_in_last_chunk;
				decrement_stripe=last_stripe_in_last_chunk;
			} else {
				if (debug_striped_reader) eprintf("J: ");
				frst_stripe=0;
				num_stripes=private->chunk_size_Blocks;
				frst_disk=0;
				num_disks=private->data_disks;
				decrement_stripe=num_stripes; // Effectively deactivating this feature
			}
		}
		if (debug_striped_reader) eprintf("frst_stripe=%llu, num_stripes=%llu, decrement_stripe=%llu, frst_disk=%llu, num_disks=%llu\n",(long long unsigned)frst_stripe,(long long unsigned)num_stripes,(long long unsigned)decrement_stripe,(long long unsigned)frst_disk,(long long unsigned)num_disks);
		block_t stripe=frst_stripe;
		block_t retval_add=0;
		for (block_t s=0;s<num_stripes;++s) {
			if (debug_striped_reader) eprintf("\t\ts=%llu (stripe=%llu)\n",(long long unsigned)s,(long long unsigned)stripe);
			
			unsigned d = 0;
			for (unsigned disk = 0; disk < private->data_disks; ++disk) {
				if (disk < frst_disk || disk >= frst_disk + num_disks) {
					d_buffer_ptr[disk] = NULL;
					e_buffer_ptr[disk] = NULL;
					i_buffer_ptr[disk] = NULL;
				} else {
					if (debug_striped_reader) {
						eprintf("\t\t\td=%u (disk=%u) [src=x+bs*%u, dst=x+bs*%u]\n",d,disk,(unsigned)disk,(unsigned)(retval+s+d*private->chunk_size_Blocks));
					}
					size_t offset = bs * (retval + s + d * private->chunk_size_Blocks);
							d_buffer_ptr[disk] = data       + offset;
					if (error_map)  e_buffer_ptr[disk] = error_map  + offset;
					if (ignore_map) i_buffer_ptr[disk] = ignore_map + offset;
					++retval_add;
					++d;
				}
			}
			
			if (stripe == decrement_stripe) {
				--num_disks;
			}
			
			bool ok = raid6_read_stripe(private
				, chunk
				, stripe
				, d_buffer_ptr
				, error_map ? e_buffer_ptr : NULL
				, ignore_map ? i_buffer_ptr : NULL
				, reason
			);
			
			if (!ok) {
				if (0) eprintf(
					"<< raid6_short_read(%p,%llu,%llu,%p) [FAILED(raid6_read_chunk)]\n"
					,(void*)private
					,(unsigned long long)first
					,(unsigned long long)(num+retval)
					,data
				);
				retval+=s;
				return retval;
			}
			if (++stripe == private->chunk_size_Blocks) {
				stripe = 0;
				++frst_disk;
			}
		}
		retval+=retval_add;
	}
	
#if 0
	for (block_t chunk=frst_chunk;chunk<=last_chunk;++chunk) {
		/*
		block_t frst_stripe,last_stripe;
		if (chunk!=frst_chunk) {
			frst_stripe=0;
		} else {
			frst_stripe=frst_stripe_in_frst_chunk;
		}
		if (chunk!=last_chunk) {
			last_stripe=private->chunk_size_Blocks-1;
		} else {
			last_stripe=last_stripe_in_last_chunk;
		}
		*/
		// unsigned char * data2=data+bs*retval;
		// unsigned char * data3=data+bs*retval+bs*(private->chunk_size_Blocks-frst_stripe_in_frst_chunk);
			/*
			 * Copying over the data is just another time hard 
			 * work as we have to make sure to
			 *
			 * a) don't forget any block
			 * b) copy each block to the correct location
			 */
			if (chunk!=frst_chunk && chunk!=last_chunk) {
				/*
				 * Here, everything is straight forward: We 
				 * need everything. Just make sure it get's 
				 * to the right destinations.
				 *
				 * Helping is that retval is a valid pointer 
				 * to the first destination block in the data 
				 * buffer.
				 */
				for (unsigned disk=0;disk<private->num_disks;++disk) {
					void * dst=data2+(bs*disk*private->chunk_size_Blocks);
					void * src=d_buffer+bs*disk;
					memcpy(dst,src,bs);
					/*
					 * We have private->num_disks blocks more 
					 * than before, but only one consequtive 
					 * block. As soon as we have all stripes of 
					 * this chunk, we can increment retval by 
					 * another (ddisks-1)*chunk_size. But as the 
					 * other cases (first/last chunk) have to 
					 * increment retval, too, we just remember 
					 * here, how much it must be incremented if 
					 * nothing goes wrong.
					 */
					++retval_add;
				}
				/*
				 * However, the first may be added immediatelly ...
				 */
				++retval;
				--retval_add;
			} else {
				/*
				 * The tricky part begins. It's tricky for 
				 * different reasons: We read the first 
				 * stripe and we copy the first blocks to 
				 * the destination area. But we may not be 
				 * allowed to update retval yet ...
				 * And the first chunk may be the last at 
				 * the same time BTW ... ;)
				 */
				if (frst_chunk==last_chunk) {
					
				} else {
					if (chunk==frst_chunk) {
						frst_disk_in_frst_chunk
						frst_stripe_in_frst_chunk
						if (stripe==frst_stripe) {
							
						}
						for (unsigned disk=frst_disk_in_frst_chunk+1;disk<private->num_disks;++disk) {
							void * dst=data2+(bs*(disk*private->chunk_size_Blocks));
							
							void * src=d_buffer+bs*disk;
							memcpy(dst,src,bs);
							++retval_add;
						}
					} else { // chunk==last_chunk
						.
					}
				}
			}
			
			void * dst=data+(bs*retval);
			void * src=d_buffer+(stripe_block*bs);
			d
			
			for (retval=0;num;) {
			
			(retval+first)/private->data_disks;
				ck_in_chunk
					block_t stripe=(retval+first)/private->data_disks;
//				printf("Reading chunk %llu ... ",chunk);
				
				if (!raid6_read_chunk(private,chunk,d_buffer,p_buffer,v_buffer)) {
//					printf("failed\n");
				}
//				printf("done ;-)\r");
				block_t stripe_block=(retval+first)-(chunk*full_chunk_size_Blocks);
				block_t do_copy=full_chunk_size_Blocks-stripe_block;
				if (do_copy>num) {
					do_copy=num;
				}
				void * dst=data+(bs*retval);
				void * src=d_buffer+(stripe_block*bs);
				size_t cnt=do_copy*bs;
				if (0) eprintf("memcpy(%p,%p,%x)\n",dst,src,cnt);
				memcpy(dst,src,cnt);
				retval+=do_copy;
				num-=do_copy;
			}
		}
	}
#endif // #if 0
	
	if (0) eprintf(
		"<< raid6_short_read(%p,%llu,%llu,%p)=%llu\n"
		,(void*)private
		,(unsigned long long)first
		,(unsigned long long)num
		,data
		,(unsigned long long)retval
	);
	
	return retval;
}

static block_t raid6_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data) {
	ignore(bdev);
	ignore(first);
	ignore(num);
	ignore(data);
	return 0;
	/*
	return raid6_data_write(bdev, first, num, data);
	*/
}

/*
 * Simple function meant to ease recovery of a partly broken raid with many 
 * known data by other sources.
 * 
 * This function reads the data colums without verifying the parities. It only 
 * reads parity data to reconstruct data after a disk io error (or if the data 
 * is missing).
 */
static block_t raid6_data_read(struct bdev * bdev, block_t first, block_t num, u8 * data, const char * reason) {
	assert_never_reached(); // If you hit this so some rechecks, that this code is still valid.
	struct raid6_dev * private = get_raid6_dev(bdev);
	
	if (debug_data_reader) eprintf(
		"raid6_data_read(%p,%llu,%llu,%p)\n"
		,(void*)private
		,(unsigned long long)first
		,(unsigned long long)num
		,data
	);
	
	unsigned bs=bdev_get_block_size(private->disk[0].bdev);
	
	block_t full_chunk_size_Blocks=private->data_disks*private->chunk_size_Blocks;
	if (debug_data_reader) eprintf("\tfull_chunk_size_Blocks=%llu\n",(unsigned long long)full_chunk_size_Blocks);
	
	block_t frst_chunk,frst_full_chunk,frst_block_in_frst_chunk,frst_disk_in_frst_chunk,frst_stripe_in_frst_chunk; // ,frst_chunk_num_stripes;
	block_t last_chunk,last_full_chunk,last_block_in_last_chunk,last_disk_in_last_chunk,last_stripe_in_last_chunk; // ,last_chunk_num_stripes;
	
	/*
	 * First: Figure out which chunks contain the first/last block 
	 * respectively.
	 */
	frst_chunk              =first/full_chunk_size_Blocks;
	frst_block_in_frst_chunk=first%full_chunk_size_Blocks;
	last_chunk              =(first+num-1)/full_chunk_size_Blocks;
	last_block_in_last_chunk=(first+num-1)%full_chunk_size_Blocks;

	if (debug_data_reader) eprintf(
		"\tfrst_chunk=%llu\n"
		"\tfrst_block_in_frst_chunk=%llu\n"
		"\tlast_chunk=%llu\n"
		"\tlast_block_in_last_chunk=%llu\n"
		,(unsigned long long)frst_chunk
		,(unsigned long long)frst_block_in_frst_chunk
		,(unsigned long long)last_chunk
		,(unsigned long long)last_block_in_last_chunk
	);
	
	/*
	 * Now figure out the disk ROLE which contains the first/last block 
	 * respectively.
	 */
	frst_disk_in_frst_chunk=frst_block_in_frst_chunk/private->chunk_size_Blocks;
	last_disk_in_last_chunk=last_block_in_last_chunk/private->chunk_size_Blocks;
	
	if (debug_data_reader) eprintf(
		"\tfrst_disk_in_frst_chunk=%llu\n"
		"\tlast_disk_in_last_chunk=%llu\n"
		,(unsigned long long)frst_disk_in_frst_chunk
		,(unsigned long long)last_disk_in_last_chunk
	);
	
	/*
	 * Finally figure out the first/last stripe needed. The previous 
	 * values have been just straight forward, this ones are not (think 
	 * about it yourself, it's hard to explain but quite logical).
	 */
	frst_stripe_in_frst_chunk=frst_block_in_frst_chunk%private->chunk_size_Blocks;
	last_stripe_in_last_chunk=last_block_in_last_chunk%private->chunk_size_Blocks;
	
	if (debug_data_reader) eprintf(
		"\tfrst_stripe_in_frst_chunk=%llu\n"
		"\tlast_stripe_in_last_chunk=%llu\n"
		,(unsigned long long)frst_stripe_in_frst_chunk
		,(unsigned long long)last_stripe_in_last_chunk
	);
	
	if (!frst_disk_in_frst_chunk
	&&  !frst_stripe_in_frst_chunk) {
		frst_full_chunk=frst_chunk;
	} else {
		frst_full_chunk=frst_chunk+1;
	}
	
	if (last_disk_in_last_chunk  ==private->data_disks-1
	&&  last_stripe_in_last_chunk==private->chunk_size_Blocks-1) {
		last_full_chunk=last_chunk;
	} else {
		last_full_chunk=last_chunk-1;
	}
	
	if (debug_data_reader) {
		eprintf(
			"\tfrst_full_chunk=%llu\n"
			,(unsigned long long)frst_full_chunk
		);
		if (last_full_chunk==-1) {
			eprintf(
				"\tlast_full_chunk=-1\n"
			);
		} else {
			eprintf(
				"\tlast_full_chunk=%llu\n"
				,(unsigned long long)last_full_chunk
			);
		}
	}
	
	u8 d_buffer[private->num_disks*bs];
	u8 p_buffer[2*bs];
	u8 v_buffer[private->may_verify_parity ? 2*bs: 1];
	u8 t_buffer[private->may_verify_parity ? 2*bs: 1];
	
	block_t frst_stripe,frst_disk;
	unsigned num_disks,num_stripes,decrement_stripe;
	
	/*
	if (debug_data_reader) eprintf(
		"\t
		=%llu\n"
		"\t
		=%llu\n"
		,(unsigned long long)
		,(unsigned long long)
	);
	*/
	
	block_t retval=0;
	unsigned mapping[private->num_disks];
	for (block_t chunk=frst_chunk;chunk<=last_chunk;++chunk) {
		if (debug_data_reader) eprintf("\tchunk=%llu -> ",(unsigned long long)chunk);
		if (chunk<frst_full_chunk) {
			if (chunk>last_full_chunk) {
				frst_stripe=frst_stripe_in_frst_chunk;
				frst_disk=frst_disk_in_frst_chunk;
				if (frst_disk!=last_disk_in_last_chunk) {
					num_disks=last_disk_in_last_chunk-frst_disk;
					num_stripes=1+last_stripe_in_last_chunk+private->chunk_size_Blocks-frst_stripe;
					if (num_stripes>private->chunk_size_Blocks) {
						if (debug_data_reader) eprintf("A");
						num_stripes=private->chunk_size_Blocks;
						++num_disks;
					} else {
						if (debug_data_reader) eprintf("B");
					}
					decrement_stripe=last_stripe_in_last_chunk;
					if (frst_disk+1!=last_disk_in_last_chunk) {
						if (debug_data_reader) eprintf("C: ");
						num_stripes=8;
					} else {
						if (debug_data_reader) eprintf("D: ");
					}
				} else {
					if (debug_data_reader) eprintf("E: ");
					num_disks=1;
					num_stripes=1+last_stripe_in_last_chunk-frst_stripe;
					decrement_stripe=private->chunk_size_Blocks; // num_stripes;
				}
			} else {
				frst_stripe=frst_stripe_in_frst_chunk;
				frst_disk=frst_disk_in_frst_chunk;
				if (frst_disk!=private->data_disks-1) {
					if (debug_data_reader) eprintf("F: ");
					num_stripes=private->chunk_size_Blocks;
				} else {
					if (debug_data_reader) eprintf("G: ");
					num_stripes=private->chunk_size_Blocks-frst_stripe;
				}
				num_disks=private->data_disks-frst_disk;
				decrement_stripe=private->chunk_size_Blocks-1;
			}
		} else {
			if (chunk>last_full_chunk) {
				frst_stripe=0;
				frst_disk=0;
				if (last_disk_in_last_chunk) {
					if (debug_data_reader) eprintf("H: ");
					num_stripes=private->chunk_size_Blocks;
				} else {
					if (debug_data_reader) eprintf("I: ");
					num_stripes=1+last_stripe_in_last_chunk;
				}
				num_disks=1+last_disk_in_last_chunk;
				decrement_stripe=last_stripe_in_last_chunk;
			} else {
				if (debug_data_reader) eprintf("J: ");
				frst_stripe=0;
				num_stripes=private->chunk_size_Blocks;
				frst_disk=0;
				num_disks=private->data_disks;
				decrement_stripe=num_stripes; // Effectively deactivating this feature
			}
		}
		if (debug_data_reader) eprintf(
			"frst_stripe=%llu, num_stripes=%llu, decrement_stripe=%llu, frst_disk=%llu, num_disks=%llu\n"
			,(long long unsigned)frst_stripe
			,(long long unsigned)num_stripes
			,(long long unsigned)decrement_stripe
			,(long long unsigned)frst_disk
			,(long long unsigned)num_disks
		);
		
		raid6_get_role_mapping_for_chunk(private,chunk,mapping,NULL);
		
		block_t stripe=frst_stripe;
		block_t retval_add=0;
		for (block_t s=0;s<num_stripes;++s) {
			if (debug_data_reader) eprintf("\t\ts=%llu (stripe=%llu)\n",(long long unsigned)s,(long long unsigned)stripe);
			
			unsigned disk=frst_disk;
			for (unsigned d=0;d<num_disks;++d,++disk) {
				block_t r;
				size_t offset=bs*(retval+s+d*private->chunk_size_Blocks);
				if (private->disk[mapping[disk]].layoutmapping==-1) {
					r=0;
				} else {
					struct bdev * component=private->disk[private->disk[mapping[disk]].layoutmapping].bdev;
					block_t block=chunk*private->chunk_size_Blocks+stripe;
					if (debug_data_reader) eprintf(
						"\t\t\t%s->read(1@%llu -> %p)"
						,bdev_get_name(component)
						,(unsigned long long)block
						,data+offset
					);
					r = bdev_read(component, block, 1, data + offset, reason);
				}
				if (r==1) {
					if (debug_data_reader) eprintf("=1\n");
					/*
					if (d==frst_disk) {
						++retval_ok;
					}
					*/
					++retval_add;
				} else {
					if (debug_data_reader && private->disk[mapping[disk]].layoutmapping!=-1) eprintf("=%lli\n",(unsigned long long)r);
					assert_never_reached(); // If you hit this so some rechecks, that this code is still valid.
					// TODO: -> bool ok=raid6_read_stripe(private,chunk,stripe,d_buffer,p_buffer,v_buffer,t_buffer,reason);
					bool ok = d_buffer[0] || p_buffer[0] || v_buffer[0] || t_buffer[0]; // Omit warnings while raid6_read_stripe is commented out
					if (!ok) {
						if (errno==ENOEXEC) errno=EIO;
						if (debug_data_reader) eprintf(
							"<< raid6_data_read(%p,%llu,%llu,%p) [FAILED(raid6_read_stripe)]\n"
							,(void*)private
							,(unsigned long long)first
							,(unsigned long long)(num+retval)
							,data
						);
						retval+=s;
						if (debug_data_reader) eprintf(" -> returning %llu\n",(unsigned long long)retval);
						return retval;
					}
					
//					disk=frst_disk;
					/*
					if (stripe<frst_stripe) {
						++disk;
					}
					*/
//					for (unsigned d=0;d<num_disks;++d,++disk) {
						if (debug_data_reader) eprintf("\t\t\trecovered role %i d=%u (disk=%u) [src=x+bs*%u, dst=x+bs*%u]\n",mapping[disk],d,disk,(unsigned)disk,(unsigned)(retval+s+d*private->chunk_size_Blocks));
//						size_t offset=bs*(retval+s+d*private->chunk_size_Blocks);
						unsigned char * src=d_buffer+bs*disk;
						memcpy(data+offset,src,bs);
						/*
						if (d==frst_disk) {
							++retval_ok;
						}
						*/
						++retval_add;
//					}
					
//					break;
				}
			}
			if (stripe==decrement_stripe) {
				--num_disks;
			}
			if (++stripe==private->chunk_size_Blocks) {
				stripe=0;
				++frst_disk;
			}
		}
		retval+=retval_add;
	}
	
	if (debug_data_reader) eprintf(
		"<< raid6_data_read(%p,%llu,%llu,%p)=%llu\n"
		,(void*)private
		,(unsigned long long)first
		,(unsigned long long)num
		,data
		,(unsigned long long)retval
	);
	
	return retval;
}

/*
 * Simple function meant to ease recovery of a partly broken raid with many 
 * known data by other sources.
 * 
 * This function writes the data colums but doesn't touch the parities in the 
 * process. So it is not senseful for normal disk operation.
 */
static block_t raid6_data_write(struct bdev * bdev, block_t first, block_t num, const u8 * data) {
	struct raid6_dev * private = get_raid6_dev(bdev);
	
	if (debug_data_writer) eprintf(
		"raid6_data_write(%p,%llu,%llu,%p)\n"
		,(void*)private
		,(unsigned long long)first
		,(unsigned long long)num
		,data
	);
	
	unsigned bs=bdev_get_block_size(private->disk[0].bdev);
	
	block_t full_chunk_size_Blocks=private->data_disks*private->chunk_size_Blocks;
	if (debug_data_writer) eprintf("\tfull_chunk_size_Blocks=%llu\n",(unsigned long long)full_chunk_size_Blocks);
	
	block_t frst_chunk,frst_full_chunk,frst_block_in_frst_chunk,frst_disk_in_frst_chunk,frst_stripe_in_frst_chunk; // ,frst_chunk_num_stripes;
	block_t last_chunk,last_full_chunk,last_block_in_last_chunk,last_disk_in_last_chunk,last_stripe_in_last_chunk; // ,last_chunk_num_stripes;
	
	/*
	 * First: Figure out which chunks contain the first/last block 
	 * respectively.
	 */
	frst_chunk              =first/full_chunk_size_Blocks;
	frst_block_in_frst_chunk=first%full_chunk_size_Blocks;
	last_chunk              =(first+num-1)/full_chunk_size_Blocks;
	last_block_in_last_chunk=(first+num-1)%full_chunk_size_Blocks;

	if (debug_data_writer) eprintf(
		"\tfrst_chunk=%llu\n"
		"\tfrst_block_in_frst_chunk=%llu\n"
		"\tlast_chunk=%llu\n"
		"\tlast_block_in_last_chunk=%llu\n"
		,(unsigned long long)frst_chunk
		,(unsigned long long)frst_block_in_frst_chunk
		,(unsigned long long)last_chunk
		,(unsigned long long)last_block_in_last_chunk
	);
	
	/*
	 * Now figure out the disk ROLE which contains the first/last block 
	 * respectively.
	 */
	frst_disk_in_frst_chunk=frst_block_in_frst_chunk/private->chunk_size_Blocks;
	last_disk_in_last_chunk=last_block_in_last_chunk/private->chunk_size_Blocks;
	
	if (debug_data_writer) eprintf(
		"\tfrst_disk_in_frst_chunk=%llu\n"
		"\tlast_disk_in_last_chunk=%llu\n"
		,(unsigned long long)frst_disk_in_frst_chunk
		,(unsigned long long)last_disk_in_last_chunk
	);
	
	/*
	 * Finally figure out the first/last stripe needed. The previous 
	 * values have been just straight forward, this ones are not (think 
	 * about it yourself, it's hard to explain but quite logical).
	 */
	frst_stripe_in_frst_chunk=frst_block_in_frst_chunk%private->chunk_size_Blocks;
	last_stripe_in_last_chunk=last_block_in_last_chunk%private->chunk_size_Blocks;
	
	if (debug_data_writer) eprintf(
		"\tfrst_stripe_in_frst_chunk=%llu\n"
		"\tlast_stripe_in_last_chunk=%llu\n"
		,(unsigned long long)frst_stripe_in_frst_chunk
		,(unsigned long long)last_stripe_in_last_chunk
	);
	
	if (!frst_disk_in_frst_chunk
	&&  !frst_stripe_in_frst_chunk) {
		frst_full_chunk=frst_chunk;
	} else {
		frst_full_chunk=frst_chunk+1;
	}
	
	if (last_disk_in_last_chunk  ==private->data_disks-1
	&&  last_stripe_in_last_chunk==private->chunk_size_Blocks-1) {
		last_full_chunk=last_chunk;
	} else {
		last_full_chunk=last_chunk-1;
	}
	
	if (debug_data_writer) {
		eprintf(
			"\tfrst_full_chunk=%llu\n"
			,(unsigned long long)frst_full_chunk
		);
		if (last_full_chunk==-1) {
			eprintf(
				"\tlast_full_chunk=-1\n"
			);
		} else {
			eprintf(
				"\tlast_full_chunk=%llu\n"
				,(unsigned long long)last_full_chunk
			);
		}
	}
	
	/*
	u8 d_buffer[private->num_disks*bs];
	u8 p_buffer[2*bs];
	u8 v_buffer[private->may_verify_parity ? 2*bs: 1];
	u8 t_buffer[private->may_verify_parity ? 2*bs: 1];
	*/
	
	block_t frst_stripe,frst_disk;
	unsigned num_disks,num_stripes,decrement_stripe;
	
	/*
	if (debug_data_writer) eprintf(
		"\t
		=%llu\n"
		"\t
		=%llu\n"
		,(unsigned long long)
		,(unsigned long long)
	);
	*/
	
	block_t retval=0;
	unsigned mapping[private->num_disks];
	for (block_t chunk=frst_chunk;chunk<=last_chunk;++chunk) {
		if (debug_data_writer) eprintf("\tchunk=%llu -> ",(unsigned long long)chunk);
		if (chunk<frst_full_chunk) {
			if (chunk>last_full_chunk) {
				frst_stripe=frst_stripe_in_frst_chunk;
				frst_disk=frst_disk_in_frst_chunk;
				if (frst_disk!=last_disk_in_last_chunk) {
					num_disks=last_disk_in_last_chunk-frst_disk;
					num_stripes=1+last_stripe_in_last_chunk+private->chunk_size_Blocks-frst_stripe;
					if (num_stripes>private->chunk_size_Blocks) {
						if (debug_data_writer) eprintf("A");
						num_stripes=private->chunk_size_Blocks;
						++num_disks;
					} else {
						if (debug_data_writer) eprintf("B");
					}
					decrement_stripe=last_stripe_in_last_chunk;
					if (frst_disk+1!=last_disk_in_last_chunk) {
						if (debug_data_writer) eprintf("C: ");
						num_stripes=8;
					} else {
						if (debug_data_writer) eprintf("D: ");
					}
				} else {
					if (debug_data_writer) eprintf("E: ");
					num_disks=1;
					num_stripes=1+last_stripe_in_last_chunk-frst_stripe;
					decrement_stripe=private->chunk_size_Blocks; // num_stripes;
				}
			} else {
				frst_stripe=frst_stripe_in_frst_chunk;
				frst_disk=frst_disk_in_frst_chunk;
				if (frst_disk!=private->data_disks-1) {
					if (debug_data_writer) eprintf("F: ");
					num_stripes=private->chunk_size_Blocks;
				} else {
					if (debug_data_writer) eprintf("G: ");
					num_stripes=private->chunk_size_Blocks-frst_stripe;
				}
				num_disks=private->data_disks-frst_disk;
				decrement_stripe=private->chunk_size_Blocks-1;
			}
		} else {
			if (chunk>last_full_chunk) {
				frst_stripe=0;
				frst_disk=0;
				if (last_disk_in_last_chunk) {
					if (debug_data_writer) eprintf("H: ");
					num_stripes=private->chunk_size_Blocks;
				} else {
					if (debug_data_writer) eprintf("I: ");
					num_stripes=1+last_stripe_in_last_chunk;
				}
				num_disks=1+last_disk_in_last_chunk;
				decrement_stripe=last_stripe_in_last_chunk;
			} else {
				if (debug_data_writer) eprintf("J: ");
				frst_stripe=0;
				num_stripes=private->chunk_size_Blocks;
				frst_disk=0;
				num_disks=private->data_disks;
				decrement_stripe=num_stripes; // Effectively deactivating this feature
			}
		}
		if (debug_data_writer) eprintf(
			"frst_stripe=%llu, num_stripes=%llu, decrement_stripe=%llu, frst_disk=%llu, num_disks=%llu\n"
			,(long long unsigned)frst_stripe
			,(long long unsigned)num_stripes
			,(long long unsigned)decrement_stripe
			,(long long unsigned)frst_disk
			,(long long unsigned)num_disks
		);
		
		raid6_get_role_mapping_for_chunk(private,chunk,mapping,NULL);
		
		block_t stripe=frst_stripe;
		block_t retval_add=0;
		for (block_t s=0;s<num_stripes;++s) {
			if (debug_data_writer) eprintf("\t\ts=%llu (stripe=%llu)\n",(long long unsigned)s,(long long unsigned)stripe);
			
			unsigned disk=frst_disk;
			for (unsigned d=0;d<num_disks;++d,++disk) {
				block_t w;
				size_t offset=bs*(retval+s+d*private->chunk_size_Blocks);
				struct bdev * component;
				if (private->disk[mapping[disk]].layoutmapping==-1) {
					component=NULL;
					w=0;
				} else {
					component=private->disk[private->disk[mapping[disk]].layoutmapping].bdev;
					block_t block=chunk*private->chunk_size_Blocks+stripe;
					if (debug_data_writer) eprintf(
						"\t\t\t%s->write(1@%llu -> %p)"
						,bdev_get_name(component)
						,(unsigned long long)block
						,data+offset
					);
					w=bdev_write(component,block,1,data+offset);
				}
				if (w==1) {
					if (debug_data_writer) eprintf("=1\n");
					/*
					if (d==frst_disk) {
						++retval_ok;
					}
					*/
					++retval_add;
				} else {
					if (debug_data_writer && private->disk[mapping[disk]].layoutmapping!=-1) eprintf("=%lli\n",(unsigned long long)w);
					ERRORF("%s: Couldn't data_write on columns %i role %i (disk %s)"
						,bdev_get_name(private->this)
						,disk
						,mapping[disk]
						,bdev_get_name(component)
					);
				}
			}
			if (stripe==decrement_stripe) {
				--num_disks;
			}
			if (++stripe==private->chunk_size_Blocks) {
				stripe=0;
				++frst_disk;
			}
		}
		retval+=retval_add;
	}
	
	if (debug_data_writer) eprintf(
		"<< raid6_data_writer(%p,%llu,%llu,%p)=%llu\n"
		,(void*)private
		,(unsigned long long)first
		,(unsigned long long)num
		,data
		,(unsigned long long)retval
	);
	
	return retval;
}

/*
 * Bogus function only to prevent warnings about static raid6_read, raid6_data_read, raid6_write, raid6_data_write not being used
 */
int foo() {
	void (*p[4])();
	p[0] = (void (*)())raid6_read;
	p[1] = (void (*)())raid6_data_read;
	p[2] = (void (*)())raid6_write;
	p[3] = (void (*)())raid6_data_write;
	unsigned char * cp = (unsigned char *)p;
	int sum = 0;
	for (size_t i = 0; i < sizeof(p); ++i) {
		sum += cp[i];
	}
	return sum;
}
