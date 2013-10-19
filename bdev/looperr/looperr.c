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

#define DRIVER_NAME "looperr"

static inline int gettid() {
	return syscall(SYS_gettid);
}

/*
 * Public interface
 */

// No special public functions defined

/*
 * Indirect public interface
 */
static block_t looperr_read(void * _private,block_t first,block_t num,unsigned char * data);
static block_t looperr_write(void * _private,block_t first,block_t num,const unsigned char * data);

static bool looperr_destroy(void * _private);

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

struct looperr_dev {
	int fd;
	int img,map;
	struct btlock_lock * lock;
	struct bdev * bdev;
};

// <filename>
//
// name will be reowned by init, args will be freed by the caller.
// static 
static struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	struct looperr_dev * private=zmalloc(sizeof(*private));
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
	private->fd=open(args,O_RDONLY);
//	private->fd=open(args,O_RDWR);
	unsigned line=-1;
	if (private->fd<0) {
		char * map_filename;
		char * img_filename;
		
		const char * p=args;
		char c;
		
		line=0;
		if (!parse_skip(&p,'"')) { line=__LINE__; goto err_msg_open; }
		
		c=parse_c_string(&p,false,&map_filename,NULL);
		if (c!='"') { line=__LINE__; goto err_msg_open; }
		
		if (!parse_skip(&p,'"')) { line=__LINE__; goto err_msg_open; }
		if (!parse_skip(&p,' ')) { line=__LINE__; goto err_msg_open; }
		if (!parse_skip(&p,'"')) { line=__LINE__; goto err_msg_open; }
		
		c=parse_c_string(&p,false,&img_filename,NULL);
		if (c!='"') { line=__LINE__; goto err_msg_open; }
		
		if (!parse_skip(&p,'"')) { line=__LINE__; goto err_msg_open; }
		
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
			int fd=(private->fd>=0)?private->fd:private->img;
			ioctlok=!ioctl(fd,BLKSSZGET,&bs);
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
	int fd=(private->fd>=0)?private->fd:private->img;
	block_t size=lseek(fd,0,SEEK_END);
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
	if (private->fd<0) {
		block_t bmp_size=lseek(private->map,0,SEEK_END);
		if (bmp_size!=(size+7)/8) {
			ERROR(
				"looperr_init: Size of image file and bitmap file don't correspondent.\n"
			);
			goto err;
		}
	}
	
	private->bdev=bdev_register_bdev(
		bdev_driver,
		name,
		size,
		block_size,
		looperr_destroy,
		private
	);
	bdev_set_read_function(private->bdev,looperr_read);
	bdev_set_write_function(private->bdev,looperr_write);
	
	if (likely(private->bdev)) {
		return private->bdev;
	}

	goto err;

err_msg_open:
	assert(line!=(unsigned)-1);
	ERRORF("looperr_init:%u: Couldn't open %s: %s.",line,args,(pwerrno==PW_ERRNO)?strerror(errno):pwerror(pwerrno));
err:
	if (private) {
		looperr_destroy(private);
	}
	free(name);
	return NULL;
}

static bool looperr_destroy(void * _private) {
//	raid6_sync(dev);
	struct looperr_dev * private=(struct looperr_dev *)_private;
	btlock_lock_free(private->lock);
	if (private->fd>=0) {
		close(private->fd);
	} else {
		if (private->img>=0) close(private->img);
		if (private->map>=0) close(private->map);
	}
	free(private);
	return true;
}

static bool looperr_access_one_block(
	struct looperr_dev * private,
	block_t block,unsigned char * data,
	bool shall_write
) {
	unsigned bs=bdev_get_block_size(private->bdev);
	
//	eprintf("looperr_access_one_block: Trying to %s block %llx\n",shall_write?"write":"read",(unsigned long long)block);
	
	if (block>=bdev_get_size(private->bdev)) {
		WARNINGF("Attempt to access %s beyond end of device.",bdev_get_name(private->bdev));
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
//	eprintf("lseek(map,%llx,SEEK_SET)\n",(unsigned long long)(block/8));
	if (block/8!=lseek(private->map,block/8,SEEK_SET)) {
		ERRORF("looperr: Couldn't seek to the needed location in the bitmap file: %s",strerror(errno));
		goto out;
	}
	if (1!=read(private->map,&m,1)) {
		ERRORF("looperr: Couldn't read one byte from the bitmap file: %s",strerror(errno));
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
			goto out;
		}
	}
	
	if (block*bs!=lseek(private->img,block*bs,SEEK_SET)) {
		ERRORF("looperr: Couldn't seek to the needed location in the image file: %s",strerror(errno));
		goto out;
	}
	errno=0;
	off_t r=shall_write?write(private->img,data,bs):read(private->img,data,bs);
	if (r!=bs) {
		if (r<0) {
			ERRORF("looperr: Couldn't read from image file: %s",strerror(errno));
		} else {
			ERRORF("looperr: Oops: Couldn't read a full block from image file (got only %llu ",(unsigned long long)r);
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
			if (block/8!=lseek(private->map,-1,SEEK_CUR)) {
				ERROR("looperr: Oops: Couldn't seek back to a location in the bitmap file which we ");
				ERRORF("looperr: Oops: previously could successfully seek to: %s",strerror(errno));
				goto out;
			}
			if (1!=write(private->map,&m,1)) {
				ERROR("looperr: Oops: Write to image file succeeded but bitmap setting on bitmap file ");
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
	struct looperr_dev * private,
	block_t first,block_t num,unsigned char * data,
	bool shall_write
) {
	unsigned bs=bdev_get_block_size(private->bdev);
	block_t retval;
	
	for (retval=0;retval<num;++retval) {
		if (unlikely(!looperr_access_one_block(private,first,data,shall_write))) {
			if (unlikely(retval)) {
				return retval;
			}
			return -1;
		}
		++first;
		data+=bs;
	}
	return retval;
}

static block_t looperr_read(void * _private,block_t first,block_t num,unsigned char * data) {
	return looperr_access((struct looperr_dev *)_private,first,num,data,false);
}

static block_t looperr_write(void * _private,block_t first,block_t num,const unsigned char * data) {
	/*
	 * Sadly we have to cast away that const qualifiers. But if we don't 
	 * do it here, we'd have to do it in looperr_access or 
	 * looperr_access_one_block anyways, so just do it here.
	 */
	return looperr_access((struct looperr_dev *)_private,first,num,(unsigned char *)data,true);
}
