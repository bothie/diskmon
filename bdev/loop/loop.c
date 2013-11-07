/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#define _GNU_SOURCE

#include "common.h"
#include "bdev.h"
#include "btthread.h"

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

/*
#ifndef BLKSSZGET
#define BLKSSZGET  _IO(0x12,104)
#endif // #ifndef BLKSSZGET
*/

#define DRIVER_NAME "loop"

/*
 * Public interface
 */

// No special public functions defined

/*
 * Indirect public interface
 */
static block_t loop_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data, const char * reason);
static block_t loop_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data);

static bool loop_destroy(struct bdev * bdev);

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

struct bdev_private {
	int fd;
	struct btlock_lock * lock;
//	struct bdev * bdev;
};

static bool loop_destroy_private(struct bdev_private * private);

// <filename>
//
// name will be reowned by init, args will be freed by the caller.
// static 
struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	struct bdev_private * private = NULL;
	
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
	
	struct bdev * bdev=bdev_register_bdev(
		bdev_driver,
		name,
		size,
		block_size,
		loop_destroy,
		private
	);
	if (likely(bdev)) {
		bdev_set_read_function(bdev,loop_read);
		bdev_set_write_function(bdev,loop_write);
		return bdev;
	}

err:
	if (private) {
		loop_destroy_private(private);
	}
	free(name);
	free(filename);
	return NULL;
}

static bool loop_destroy_private(struct bdev_private * private) {
	btlock_lock_free(private->lock);
	if (private->fd>=0) {
		close(private->fd);
	}
	free(private);
	return true;
}

static bool loop_destroy(struct bdev * bdev) {
	return loop_destroy_private(bdev_get_private(bdev));
}

static block_t loop_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data, const char * reason) {
	struct bdev_private * private = bdev_get_private(bdev);
	
	ignore(reason);
	
	/*
	struct timespec time;
	time.tv_sec=0;
	time.tv_nsec=10*1000*1000;
	if (nanosleep(&time,NULL)) {
		eprintf("nanosleep: %s\n",strerror(errno));
	}
	*/
	
	if ((first+num)>bdev_get_size(bdev)) {
		WARNINGF("Attempt to access %s beyond end of device.",bdev_get_name(bdev));
		errno=EINVAL;
		return 0;
	}
	
	btlock_lock(private->lock);
	
//	eprintf("Thread %i: got lock of device %s (%i) for reading\n",gettid(),bdev_get_name(bdev),private->fd);
	if (first*bdev_get_block_size(bdev)!=lseek(private->fd,first*bdev_get_block_size(bdev),SEEK_SET)) {
//		eprintf("Thread %i: releasing lock of device %s (%i) after error\n",gettid(),bdev_get_name(bdev),private->fd);
		btlock_unlock(private->lock);
		return 0;
	}
	errno=0;
	off_t r=read(private->fd,data,num*bdev_get_block_size(bdev));
//	eprintf("Thread %i: releasing lock of device %s (%i) after success\n",gettid(),bdev_get_name(bdev),private->fd);
	int e = errno;
	
	btlock_unlock(private->lock);
	if (0)
	bprintf(
		"%s->loop::read(%llu,%llu)=%lli\n"
		,bdev_get_name(bdev)
		,(unsigned long long)first
		,(unsigned long long)num
		,(signed  long long)((r>=0)?r/bdev_get_block_size(bdev):r)
	);
	if (r<=0) {
		NOTIFYF("%s->loop::read(%llu,%llu)=%i: %s"
			,bdev_get_name(bdev)
			,(unsigned long long)first
			,(unsigned long long)num
			,r?-1:0
			,strerror(e)
		);
		if (!(errno = e)) {
			errno=EIO;
		}
		return 0;
	}
	r/=bdev_get_block_size(bdev);
	
	return r;
}

static block_t loop_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data) {
	struct bdev_private * private = bdev_get_private(bdev);
	
	if ((first+num)>bdev_get_size(bdev)) {
		WARNINGF("Attempt to access %s beyond end of device.",bdev_get_name(bdev));
		errno=EINVAL;
		return 0;
	}
	
	btlock_lock(private->lock);
	
//	eprintf("Thread %i: got lock of device %s (%i) for writing\n",gettid(),bdev_get_name(bdev),private->fd);
	if (first*bdev_get_block_size(bdev)!=lseek(private->fd,first*bdev_get_block_size(bdev),SEEK_SET)) {
//		eprintf("Thread %i: releasing lock of device %s (%i) after error\n",gettid(),bdev_get_name(bdev),private->fd);
		btlock_unlock(private->lock);
		return 0;
	}
	errno=0;
//	w=num*bdev_get_block_size(bdev);
	off_t w=write(private->fd,data,num*bdev_get_block_size(bdev));
//	eprintf("Thread %i: releasing lock of device %s (%i) after success\n",gettid(),bdev_get_name(bdev),private->fd);
	int e = errno;
	
	btlock_unlock(private->lock);
	if (0)
	bprintf(
		"%s->loop::write(%llu,%llu)=%lli\n"
		,bdev_get_name(bdev)
		,(unsigned long long)first
		,(unsigned long long)num
		,(signed  long long)((w>=0)?w/bdev_get_block_size(bdev):w)
	);
	if (w<=0) {
		NOTIFYF("%s->loop::write(%llu,%llu)=%i: %s"
			,bdev_get_name(bdev)
			,(unsigned long long)first
			,(unsigned long long)num
			,w?-1:0
			,strerror(e)
		);
		if (!(errno = e)) {
			errno=EIO;
		}
		return 0;
	}
	w/=bdev_get_block_size(bdev);
	
	return w;
}
