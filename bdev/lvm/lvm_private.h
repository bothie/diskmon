#include "uuid.h"

#define PV_MAGIC "LABELONE"
#define PV_MAGIC_LEN 8
#define PV_TYPE "LVM2 001"
#define PV_TYPE_LEN 8
#define VG_MAGIC "\040\114\126\115\062\040\170\133\065\101\045\162\060\116\052\076"
#define VG_MAGIC_LEN 16
#define VG_VERSION 1

// #define MDA_HEADER_SIZE 512

// #define LABEL_SIZE SECTOR_SIZE  /* Think very carefully before changing this */
// #define LABEL_SCAN_SECTORS 4L
// #define LABEL_SCAN_SIZE (LABEL_SCAN_SECTORS << SECTOR_SHIFT)
// #define MDA_SIZE_MIN (8 * (unsigned) lvm_getpagesize())

struct pv_disk_locn {
	uint64_t offset; // in bytes
	uint64_t size; // in bytes
} __attribute__ ((packed));

struct pv_superblock {
	// in LVM sources this is called "struct label_header"
/*000*/	int8_t magic[PV_MAGIC_LEN]; // == "LABELONE"
/*008*/	uint64_t this_block_number; // block number of this block m.p. 1
/*010*/	uint32_t crc; // CRC of the range &offset to end of block
/*014*/	uint32_t offset; // Offset of PV metadata
/*018*/	int8_t type[PV_TYPE_LEN]; // == "LVM2 001"
	// in LVM sources this is called "struct pv_header"
/*020*/	int8_t pv_uuid[UUID_LEN];
/*040*/	uint64_t device_size; // Only relevant if PV is VGless. Size is in BYTES!
/*048*/	struct pv_disk_locn pv_area; // We can't handle more than one pv_area currently.
/*058*/	struct pv_disk_locn null1;
/*068*/	struct pv_disk_locn vg_metadata_area; // We can't handle more than one md area as well
/*078*/	struct pv_disk_locn null2;
/*088*/	// Padding ...
} __attribute__ ((packed));

struct vg_locn {
	// struct raw_locn
	uint64_t offset; // in bytes
	uint64_t size; // in bytes
	uint32_t checksum;
	uint32_t filler;
} __attribute__ ((packed));

struct vg_superblock {
/*000*/	uint32_t checksum; // Checksum of rest of vg_superblock
/*004*/	int8_t magic[VG_MAGIC_LEN]; // = FMTT_MAGIC
/*014*/	uint32_t version; // = FMTT_VERSION
/*018*/	uint64_t start; // Start of vg_metadata_area (this structure) in bytes
/*020*/	uint64_t size; // Size of vg_metadata_area (this structure) in bytes
/*028*/	struct vg_locn vg_metadata_area; // Relative pointer to the currently valid metadata in the ringbuffer
/*040*/	// Padding ...
} __attribute__ ((packed));
