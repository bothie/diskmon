#define _GNU_SOURCE

#include "common.h"
#include "bdev.h"

#include <assert.h>
#include <btlock.h>
#include <btmacros.h>
#include <btstr.h>
#include <bttime.h>
#include <fcntl.h>
#include <errno.h>
#include <mprintf.h>
#include <stdbool.h>
#include <stddbg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * On my system, both files, linux/fs.h and sys/mount.h define BLKBSZGET and 
 * BLKSSZGET. If you get errors about this symbol being undefined, try 
 * uncommenting the include directive for the other file before doing 
 * anything else.
 */
// #include <linux/fs.h>
#include <sys/mount.h>

#define DRIVER_NAME "loop"

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
static block_t loop_read(void * _private,block_t first,block_t num,unsigned char * data);
static block_t loop_write(void * _private,block_t first,block_t num,const unsigned char * data);

static bool loop_destroy(void * _private);

static struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,const char * args);

static bool initialized;

BDEV_INIT {
	if (!bdev_register_driver(DRIVER_NAME,&bdev_init)) {
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

struct loop_dev {
	int fd;
	struct btlock_lock * lock;
	struct bdev * bdev;
};

// <filename>
//
// name will be reowned by init, args will be freed by the caller.
// static 
struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	struct loop_dev * private=NULL;
	
	char * filename=mstrcpy(args);	
	if (!filename) {
		ERRORF("loop_init: malloc: %s",strerror(errno));
		goto err;
	}
	
	private=zmalloc(sizeof(*private));
	if (!private) {
		ERRORF("loop_init: malloc: %s",strerror(errno));
		goto err;
	}
	private->lock=btlock_lock_mk();
	if (!private->lock) {
		ERRORF("loop_init: lock_mk: %s",strerror(errno));
		goto err;
	}
	
	// First try the whole argument as one file name.
	// Thus if you have a file /image and a file "/image --read-write", 
	// you will not be able to open /image in read-write mode as 
	// "/image --read-write" will be tried first and opened (read-only).
	// In general, this shouldn't be a problem.
	private->fd=open(filename,O_RDONLY);
	if (private->fd<0) {
		size_t sl=strlen(filename);
		const char * rw=" --read-write";
		size_t sl_rw=strlen(rw);
		bool ok=false;
		if (sl>sl_rw && !memcmp(filename+sl-sl_rw,rw,sl_rw)) {
			filename[sl-sl_rw]=0;
			private->fd=open(filename,O_RDWR);
			if (private->fd>=0) ok=true;
		}
		if (!ok) {
			ERRORF("loop_init: Couldn't open %s: %s.",filename,strerror(errno));
			goto err;
		}
	}
	
	unsigned block_size;
	{
		bool ioctlok;
		{
			size_t bs;
			ioctlok=!ioctl(private->fd,BLKSSZGET,&bs);
			block_size=bs;
		}
		if (!ioctlok) {
			if (errno==ENOTTY) {
				block_size=512;
				WARNINGF("loop_init: Backing device is not a block device, emulating block size %u",block_size);
			} else {
				ERRORF("loop_init: Couldn't determine block size: %s",strerror(errno));
				goto err;
			}
		}
	}
	block_t size=lseek(private->fd,0,SEEK_END);
	if (size%block_size) {
		ERRORF(
			"loop_init: Size of file %s is %llu which is not a multiple of %u bytes (block size).\n"
			,filename
			,(unsigned long long)size
			,block_size
		);
		goto err;
	}
	size/=block_size;
	
	free(filename);
	
	private->bdev=bdev_register_bdev(
		bdev_driver,
		name,
		size,
		block_size,
		loop_destroy,
		private
	);
	if (likely(private->bdev)) {
		bdev_set_read_function(private->bdev,loop_read);
		bdev_set_write_function(private->bdev,loop_write);
		return private->bdev;
	}

err:
	if (private) {
		loop_destroy(private);
	}
	free(name);
	free(filename);
	return NULL;
}

static bool loop_destroy(void * _private) {
//	raid6_sync(dev);
	struct loop_dev * private=(struct loop_dev *)_private;
	btlock_lock_free(private->lock);
	if (private->fd>=0) {
		close(private->fd);
	}
	free(private);
	return true;
}

static block_t loop_read(void * _private,block_t first,block_t num,unsigned char * data) {
	struct loop_dev * private=(struct loop_dev *)_private;
	if ((first+num)>bdev_get_size(private->bdev)) {
		WARNINGF("Attempt to access %s beyond end of device.",bdev_get_name(private->bdev));
		errno=EINVAL;
		return -1;
	}
	
	btlock_lock(private->lock);
	
//	eprintf("Thread %i: got lock of device %s (%i) for reading\n",gettid(),bdev_get_name(private->bdev),private->fd);
	if (first*bdev_get_block_size(private->bdev)!=lseek(private->fd,first*bdev_get_block_size(private->bdev),SEEK_SET)) {
//		eprintf("Thread %i: releasing lock of device %s (%i) after error\n",gettid(),bdev_get_name(private->bdev),private->fd);
		btlock_unlock(private->lock);
		return -1;
	}
	errno=0;
	off_t r=read(private->fd,data,num*bdev_get_block_size(private->bdev));
//	eprintf("Thread %i: releasing lock of device %s (%i) after success\n",gettid(),bdev_get_name(private->bdev),private->fd);
	btlock_unlock(private->lock);
	bprintf(
		"%s->loop::read(%llu,%llu)=%lli\n"
		,bdev_get_name(private->bdev)
		,(unsigned long long)first
		,(unsigned long long)num
		,(signed  long long)((r>=0)?r/bdev_get_block_size(private->bdev):r)
	);
	if (r<=0) {
		NOTIFYF("%s->loop::read(%llu,%llu)=%i: %s"
			,bdev_get_name(private->bdev)
			,(unsigned long long)first
			,(unsigned long long)num
			,r?-1:0
			,strerror(errno)
		);
		if (!errno) {
			errno=EIO;
		}
		return -1;
	}
	r/=bdev_get_block_size(private->bdev);
	
	return r;
}

static block_t loop_write(void * _private,block_t first,block_t num,const unsigned char * data) {
	struct loop_dev * private=(struct loop_dev *)_private;
	if ((first+num)>bdev_get_size(private->bdev)) {
		WARNINGF("Attempt to access %s beyond end of device.",bdev_get_name(private->bdev));
		errno=EINVAL;
		return -1;
	}
	
	btlock_lock(private->lock);
	
//	eprintf("Thread %i: got lock of device %s (%i) for writing\n",gettid(),bdev_get_name(private->bdev),private->fd);
	if (first*bdev_get_block_size(private->bdev)!=lseek(private->fd,first*bdev_get_block_size(private->bdev),SEEK_SET)) {
//		eprintf("Thread %i: releasing lock of device %s (%i) after error\n",gettid(),bdev_get_name(private->bdev),private->fd);
		btlock_unlock(private->lock);
		return -1;
	}
	errno=0;
//	w=num*bdev_get_block_size(private->bdev);
	off_t w=write(private->fd,data,num*bdev_get_block_size(private->bdev));
//	eprintf("Thread %i: releasing lock of device %s (%i) after success\n",gettid(),bdev_get_name(private->bdev),private->fd);
	btlock_unlock(private->lock);
	bprintf(
		"%s->loop::write(%llu,%llu)=%lli\n"
		,bdev_get_name(private->bdev)
		,(unsigned long long)first
		,(unsigned long long)num
		,(signed  long long)((w>=0)?w/bdev_get_block_size(private->bdev):w)
	);
	if (w<=0) {
		NOTIFYF("%s->loop::write(%llu,%llu)=%i: %s"
			,bdev_get_name(private->bdev)
			,(unsigned long long)first
			,(unsigned long long)num
			,w?-1:0
			,strerror(errno)
		);
		if (!errno) {
			errno=EIO;
		}
		return -1;
	}
	w/=bdev_get_block_size(private->bdev);
	
	return w;
}
