/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "common.h"
#include "bdev.h"

#include <btstr.h>

#define DRIVER_NAME "dos"

/*
 * Public interface
 */

/*
 * Indirect public interface (via struct bdev)
 */

struct dos_bdev;

static bool dos_destroy(struct bdev * bdev);

static block_t dos_read(struct bdev * bdev,block_t first,block_t num,unsigned char * data,const char * reason);
static block_t dos_write(struct bdev * bdev,block_t first,block_t num,const unsigned char * data);
static bool dos_destroy(struct bdev * bdev);
static block_t dos_short_read(struct bdev * bdev,block_t first,block_t num,u8 * data,u8 * error_map,const char * reason);

static struct bdev * dos_init(struct bdev_driver * bdev_driver,char * name,const char * args);

static bool initialized;

BDEV_INIT {
	if (!bdev_register_driver(DRIVER_NAME,&dos_init)) {
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

struct chs {
	u8 head;
	u8 sector; // Bits 0..5: sector, bits 6 and 7 represent bit 8 and 9 of cylinder;
	u8 cylinder; // This are the bits 0..7 of the cylinder value, bits 8 and 9 are stored in bits 6 and 7 of sector
} __attribute__((__packed__));

struct partition_entry {
	u8  boot_indicator;
	struct chs chs_offset;
	u8  system_id;
	// 0x05, 0x0f: extended partition
	// 0x12: EISA partition or OEM partition
	// 0xde: Dell OEM partition
	// 0xee: GPT partition
	// 0xef: EFI system parition
	// 0xfe: IBM OEM partition
	struct chs chs_end;
	u32 lba_offset;
	u32 lba_length;
} __attribute__((__packed__));

struct mbr {
	u8 bootloader[512-2-4*16];
	struct partition_entry partition[4];
	u16 signature;
} __attribute__((__packed__));

struct bdev_private {
	struct bdev * bdev;
	char * name;
	block_t offset;
	block_t length;
};

// <dev1>
static struct bdev * dos_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	struct bdev * bdev=bdev_lookup_bdev(args);
	if (!bdev) {
		ERRORF("Couldn't lookup device (%s).",args);
		free(name);
		return NULL;
	}
	if (bdev_get_block_size(bdev)!=512) {
		ERROR("DOS partition tables can only be stored on disks with block size == 512 byte.");
		free(name);
		return NULL;
	}
	
	eprintf("sizeof(struct mbr)=%u\n",(unsigned)sizeof(struct mbr));
	assert(sizeof(struct mbr)==512);
	struct bdev_private * p[4];
	int error=false;
	{
		struct mbr * mbr=malloc(512);;
		if (1!=bdev_read(bdev,0,1,(unsigned char *)mbr,"read mbr")) {
			ERROR("Couldn't read partition table.");
			free(mbr);
			free(name);
			return NULL;
		}
		
		for (int i=0;i<4;++i) {
			eprintf(
				"[%i]: 0x%08lx..0x%08lx (0x%08lx)\n"
				,i
				,(unsigned long)mbr->partition[i].lba_offset
				,(unsigned long)mbr->partition[i].lba_offset+mbr->partition[i].lba_length-1
				,(unsigned long)mbr->partition[i].lba_length
			);
			if (mbr->partition[i].lba_offset
			&&  mbr->partition[i].lba_length) {
				p[i]=malloc(sizeof(*p[i]));
				if (!p[i]) {
					error=true;
					p[i]=NULL;
					ERROR("Error while allocating memory for the partition table entries");
				} else {
					p[i]->name=NULL;
					p[i]->offset=mbr->partition[i].lba_offset;
					p[i]->length=mbr->partition[i].lba_length;
				}
			} else {
				p[i]=NULL;
			}
		}
		free(mbr);
	}
	struct bdev * b[4];
	for (int i=0;i<4;++i) {
		if (p[i]) {
			p[i]->name=mprintf("%s%i",name,i+1);
			p[i]->bdev=bdev;
			b[i]=bdev_register_bdev(
				bdev_driver,
				mstrcpy(p[i]->name),
				p[i]->length,
				512,
				dos_destroy,
				p[i]
			);
			if (!b[i]) {
				error=true;
			} else {
				p[i]=NULL;
				bdev_set_read_function(b[i],dos_read);
				bdev_set_write_function(b[i],dos_write);
				bdev_set_short_read_function(b[i],dos_short_read);
			}
		} else {
			b[i]=NULL;
		}
	}
	if (!error) {
		return (struct bdev *)0xdeadbeef;
	}
	for (int i=0;i<4;++i) {
		if (p[i]) {
			free(p[i]);
		}
		if (b[i]) {
			bdev_destroy(b[i]);
		}
	}
	return NULL;
}

bool dos_destroy(struct bdev * bdev) {
	struct bdev_private * private = bdev_get_private(bdev);
	
	free(private->name);
	free(private);
	
	return true;
}

static inline block_t dos_readwrite(bool do_write, struct bdev_private * private, block_t first, block_t num, u8 * data, u8 * error_map, const char * reason) {
	if (first+num>private->length || first+num<first) {
		ERRORF("%s: Attempt to access beyond end of partition.",private->name);
		assert_never_reached();
		if (first<private->length) {
			num=private->length-first;
		} else {
			return 0;
		}
	}
	if (error_map) {
		assert(!do_write);
		return bdev_short_read(
			private->bdev,
			private->offset+first,
			num,
			data,
			error_map
			,reason
		);
	} else {
		if (do_write) {
			return bdev_write(
				private->bdev,
				private->offset+first,
				num,
				data
			);
		} else {
			return bdev_read(
				private->bdev,
				private->offset+first,
				num,
				data,
				reason
			);
		}
	}
}

block_t dos_read(struct bdev * bdev, block_t first, block_t num, u8 * data, const char * reason) {
	return dos_readwrite(false, bdev_get_private(bdev), first, num, data, NULL, reason);
}

block_t dos_short_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const char * reason) {
	return dos_readwrite(false, bdev_get_private(bdev), first, num, data, error_map, reason);
}

block_t dos_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data) {
	return dos_readwrite(true, bdev_get_private(bdev), first, num, (unsigned char *)data, NULL, "TODO: reason");
}
