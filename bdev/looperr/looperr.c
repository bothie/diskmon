/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#define _XOPEN_SOURCE 500
#define _BSD_SOURCE 1
#define _GNU_SOURCE

#include "common.h"
#include "bdev.h"

#include <assert.h>
#include <btlock.h>
#include <btmacros.h>
#include <bttime.h>
#include <fcntl.h>
#include <errno.h>
#include <mprintf.h>
#include <parseutil.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

char parse_c_string(
	const char * * source,
	bool need_quote,
	char * * destination,size_t * destination_length
);

/*
 * On my system, both files, linux/fs.h and sys/mount.h define BLKBSZGET and 
 * BLKSSZGET. If you get errors about this symbol being undefined, try 
 * uncommenting the include directive for the other file before doing 
 * anything else.
 */
// #include <linux/fs.h>
#include <sys/mount.h>

/*
#ifndef BLKSSZGET
#define BLKSSZGET  _IO(0x12,104)
#endif // #ifndef BLKSSZGET
*/

#define DRIVER_NAME "looperr"

/*
 * Public interface
 */

// No special public functions defined

/*
 * Indirect public interface
 */
static block_t looperr_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data, const char * reason);
static block_t looperr_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data);

static bool looperr_destroy(struct bdev * bdev);

static struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,const char * args);

static bool initialized;

BDEV_INIT {
	if (!bdev_register_driver(DRIVER_NAME,&bdev_init)) {
		ERRORF("Couldn't register driver %s",DRIVER_NAME);
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

struct bdev_private {
	int img,map;
	struct btlock_lock * lock;
//	struct bdev * bdev;
	int log;
	unsigned long chunk_size;
};

static bool looperr_destroy_private(struct bdev_private * private);

// <filename>
//
// name will be reowned by init, args will be freed by the caller.
// static 
static struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	struct bdev_private * private=zmalloc(sizeof(*private));
	if (!private) {
		ERRORF("looperr_init: malloc: %s",strerror(errno));
		goto err;
	}
	private->lock=btlock_lock_mk();
	if (!private->lock) {
		ERRORF("looperr_init: lock_mk: %s",strerror(errno));
		goto err;
	}
	private->img=-1;
	private->map=-1;
	private->log=-1;
	private->chunk_size=1;
	unsigned line=-1;
	{
		char * map_filename;
		char * img_filename;
		
		const char * p=args;
		char c;
		
		line=0;
		c=parse_c_string(&p,true,&map_filename,NULL);
		
		if (!parse_skip(&p,' ')) { line=__LINE__; goto err_msg_open; }
		
		c=parse_c_string(&p,true,&img_filename,NULL);
		
		while (parse_skip(&p, ' ')) {
			if (parse_skip_string(&p,"--logfile ")) {
				char * logfilename;
				c=parse_c_string(&p,true,&logfilename,NULL);
				if (!logfilename) {
					ERRORF("looperr_init: parse_c_string couldn't parse logfilename: %s",strerror(errno));
					goto err;
				}
				private->log=open(logfilename,O_WRONLY|O_CREAT|O_TRUNC,0666);
				if (private->log<0) {
					ERRORF("looperr_init: Couldn't open file \"%s\": %s",logfilename,strerror(errno));
					free(logfilename);
					goto err;
				}
				free(logfilename);
				continue;
			}
			if (parse_skip_string(&p,"--chunk-size ")) {
				c=parse_unsigned_long(&p,&private->chunk_size);
				if (!c && pwerrno) {
					ERRORF(
						"looperr_init: parse_unsigned_long couldn't parse chunk size: %s"
						, pwerror(pwerrno)
					);
					goto err;
				}
				continue;
			}
		}
		
		if (*p) { line=__LINE__; goto err_msg_open; }
		
		private->map=open(map_filename,O_RDONLY);
		if (private->map<0) {
			ERRORF("looperr_init: Couldn't open bitmap file %s: %s.",map_filename,strerror(errno));
			goto err;
		}
		private->img=open(img_filename,O_RDONLY);
		if (private->img<0) {
			ERRORF("looperr_init: Couldn't open image file %s: %s.",img_filename,strerror(errno));
			goto err;
		}
		free(map_filename);
		free(img_filename);
	}
	unsigned block_size;
	{
		bool ioctlok;
		{
			size_t bs;
			ioctlok=!ioctl(private->img,BLKSSZGET,&bs);
			block_size=bs;
		}
		if (!ioctlok) {
			if (errno==ENOTTY) {
				block_size=512;
				WARNINGF("looperr_init: Backing device is not a block device, emulating block size %u",block_size);
			} else {
				ERRORF("looperr_init: Couldn't determine block size: %s",strerror(errno));
				goto err;
			}
		}
	}
	block_t size=lseek(private->img,0,SEEK_END);
	if (size%block_size) {
		ERRORF(
			"looperr_init: Size of file %s is %llu which is not a multiple of %u bytes (block size).\n"
			,args
			,(unsigned long long)size
			,block_size
		);
		goto err;
	}
	size/=block_size;
	{
		block_t bmp_size=lseek(private->map,0,SEEK_END);
		if (bmp_size!=(size+7)/8) {
			WARNING(
				"looperr_init: Size of image file and bitmap file don't correspond."
			);
			WARNINGF(
				"looperr_init: Size of image file is %llu blocks"
				, (long long unsigned)size
			);
			WARNINGF(
				"looperr_init: Size of bitmap file is %llu bits"
				, (long long unsigned)(bmp_size * 8)
			);
			if (bmp_size * 8 < size) {
				size = bmp_size * 8;
			}
			WARNINGF(
				"looperr_init: Setting looperr block device to %llu blocks"
				, (long long unsigned)size
			);
		}
	}
	
	struct bdev * bdev=bdev_register_bdev(
		bdev_driver,
		name,
		size,
		block_size,
		looperr_destroy,
		private
	);
	bdev_set_read_function(bdev,looperr_read);
	bdev_set_write_function(bdev,looperr_write);
	
	if (likely(bdev)) {
		return bdev;
	}

	goto err;

err_msg_open:
	assert(line!=(unsigned)-1);
	ERRORF("looperr_init:%u: Couldn't open %s: %s.",line,args,(pwerrno==PW_ERRNO)?strerror(errno):pwerror(pwerrno));
err:
	if (private) {
		looperr_destroy_private(private);
	}
	free(name);
	return NULL;
}

static bool looperr_destroy_private(struct bdev_private * private) {
	btlock_lock_free(private->lock);
	if (private->img>=0) close(private->img);
	if (private->map>=0) close(private->map);
	if (private->log>=0) close(private->log);
	free(private);
	return true;
}

static bool looperr_destroy(struct bdev * bdev) {
	return looperr_destroy_private(bdev_get_private(bdev));
}

static bool looperr_access_one_block(
	struct bdev * bdev
	, block_t block
	, unsigned char * data
	, bool shall_write
	, const char * reason
) {
	struct bdev_private * private = bdev_get_private(bdev);
	
	unsigned bs=bdev_get_block_size(bdev);
	
//	eprintf("looperr_access_one_block: Trying to %s block %llx\n",shall_write?"write":"read",(unsigned long long)block);
	
	if (block >= bdev_get_size(bdev)) {
		WARNINGF("Attempt to access %s beyond end of device.", bdev_get_name(bdev));
		errno=EINVAL;
		return false;
	}
	
	/*
	 * Please keep the lock call and the retval declaration together as 
	 * they are to make sure that no one jumps to "out" prior to lock()ing. 
	 * If somebody does, he should get an error or at least a warning 
	 * bypassing the initialisation of retval which is absolutely intendet 
	 * here.
	 */
	btlock_lock(private->lock);
	bool retval=false;
	
	unsigned char new_m,m;
	if (1 != pread(private->map, &m, 1, block / 8)) {
		ERRORF("looperr: Couldn't pread one byte from the bitmap file: %s", strerror(errno));
		goto out;
	}
	
//	eprintf("read -> m=%02x checking for bit %02x being set\n",(unsigned)m,(unsigned)(1<<(block%8)));
	if (shall_write) {
		new_m=m|(1<<(block%8));
	} else {
		new_m=m; // warning: 'new_m' may be used uninitialized in this function
		if (!(m&(1<<(block%8)))) {
/*
			ERRORF(
				"looperr: According to the bitmap, the block (0x%llx) is not stored in image file. Emulating DISK IO ERROR."
				,(unsigned long long)block
			);
*/
			if (private->log >= 0) {
				dprintf(private->log,
					"%llu %s\n"
					, (unsigned long long)block / private->chunk_size
					, reason
				);
			}
			errno = EIO;
			goto out;
		}
	}
	
	off_t rw = shall_write ? pwrite(private->img, data, bs, block * bs) : pread(private->img, data, bs, block * bs);
	if (rw != bs) {
		if (rw<0) {
			ERRORF("looperr: Couldn't p%s image file: %s"
				, shall_write ? "write to" : "read from"
				, strerror(errno)
			);
		} else {
			ERRORF("looperr: Oops: Couldn't p%s a full block %s the image file (done only %llu "
				, shall_write ? "write" : "read"
				, shall_write ? "to" : "from"
				, (unsigned long long)rw
			);
			ERROR("looperr: Oops: bytes). Did someone mess with the image file while we were ");
			ERROR("looperr: Oops: working on it?");
			errno=EIO;
		}
		goto out;
	}
	if (shall_write) {
		/*
		 * When writing to a "new" block, we have a little problem: If we mark the bitmap, before the block 
		 * has made it to the disk, the data image will be corrupted after a crash, as the bit is telling 
		 * us having a valid block but the block wasn't written yet. So we have to fdatasync the image file 
		 * after writing the block to make sure, the block went to disk before we manipulate the bitmap.
		 */
		if (fdatasync(private->img)) {
			ERRORF("looperr: Oops: Couldn't fdatasync image file: %s",strerror(errno));
			goto out;
		}
		if (new_m!=m) {
			if (1 != pwrite(private->map, &m, 1, block / 8)) {
				ERROR("looperr: Oops: Write to image file succeeded but pwrite on the bitmap file ");
				ERRORF("looperr: Oops: failed: %s",strerror(errno));
				goto out;
			}
			if (fdatasync(private->map)) {
				ERRORF("looperr: Oops: Couldn't fdatasync bitmap file: %s",strerror(errno));
				goto out;
			}
		}
	}
	retval=true;

out:
	btlock_unlock(private->lock);
	return retval;
}

static block_t looperr_access(
	struct bdev * bdev
	, block_t first
	, block_t num
	, unsigned char * data
	, bool shall_write
	, const char * reason
) {
	unsigned bs = bdev_get_block_size(bdev);
	block_t retval;
	
	for (retval=0;retval<num;++retval) {
		if (unlikely(!looperr_access_one_block(bdev, first, data, shall_write, reason))) {
			return retval;
		}
		++first;
		data+=bs;
	}
	return retval;
}

static block_t looperr_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data, const char * reason) {
	return looperr_access(bdev, first, num, data, false, reason);
}

static block_t looperr_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data) {
	/*
	 * Sadly we have to cast away that const qualifiers. But if we don't 
	 * do it here, we'd have to do it in looperr_access or 
	 * looperr_access_one_block anyways, so just do it here.
	 */
	return looperr_access(bdev, first, num, (unsigned char *)data, true, "TODO: reason");
}
