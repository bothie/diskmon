#define _GNU_SOURCE

#include "common.h"
#include "bdev.h"

#include <bterror.h>
#include <dlfcn.h>
#include <mprintf.h>

/*
#include <assert.h>
#include <btlock.h>
#include <btmacros.h>
#include <btstr.h>
#include <bttime.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <stddbg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
*/

#define DRIVER_NAME "fs"

/*
 * Public interface
 */

// No special public functions defined

/*
 * Indirect public interface
 */
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

// <filename>
//
// name will be reowned by init, args will be freed by the caller.
// static 
struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	ignore(bdev_driver);
	
	char * filename = mprintf("%s.so", name);
	void * fs_driver=dlopen(filename,RTLD_NOW|RTLD_GLOBAL);
	if (!fs_driver) {
		eprintf("Couldn't load %s",filename);
		eprintf("dlerror: %s\n",dlerror());
		free(filename);
		free(name);
		return NULL;
	}
	free(filename);
	
	bool (*fsck)(const char * name);
	fsck=(bool (*)(const char *))dlsym(fs_driver,"fsck");
	if (!fsck) {
		eprintf("Couldn't lookup function %s", "fsck");
		free(name);
		return NULL;
	}
	
	NOTIFYF("Starting %s file system check on device %s.\n", name, args);
	
	free(name);
	
	fsck(args);
	
	// Return a non-NULL pointer to make a check like "if (!init(...)) 
	// exit(2);" fail, but don't give 'em a valid pointer.
	return (struct bdev *)0xdeadbeef;
}
