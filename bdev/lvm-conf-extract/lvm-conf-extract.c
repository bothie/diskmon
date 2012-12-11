#include "common.h"
#include "bdev.h"

#include <assert.h>
#include <btmacros.h>
#include <errno.h>
#include <fcntl.h>
#include <mprintf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
#define LSEEK(fn,fd,fp) do { \
	if (fp!=lseek(fd,fp,SEEK_SET)) { \
		eprintf( \
			"lseek(\"%s\" via %i,%llu,SEEK_SET): %s\n" \
			,fn,fd,(long long unsigned)fp \
			,strerror(errno) \
		); \
		exit(2); \
	} \
} while (0)

#undef READ
#define READ(fn,fd,dp,nb) do { \
	if (nb!=read(fd,dp,nb)) { \
		eprintf( \
			"read(\"%s\" via %i,%p,%llu): %s\n" \
			,fn,fd,dp,(long long unsigned)nb \
			,strerror(errno) \
		); \
		exit(2); \
	} \
} while (0)
*/

#define WRITE(fn,fd,dp,nb) do { \
	if (nb!=write(fd,dp,nb)) { \
		eprintf( \
			"write(\"%s\" via %i,%p,%llu): %s\n" \
			,fn,fd,dp,(long long unsigned)nb \
			,strerror(errno) \
		); \
		exit(2); \
	} \
} while (0)

#define DRIVER_NAME "lvm-conf-extract"

/*
 * Public interface
 */

// nothing

/*
 * Indirect public interface
 */
static struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,char * args);

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
static struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,char * args) {
	ignore(bdev_driver);
	
	char sbuffer[512];
	unsigned char * ubuffer = (unsigned char *)sbuffer;
//	int argc;
//	char * * argv;
	
/*
	if (!args_split(args,&argc,&argv)) {
		ERRORF("Couldn't split args: %s",strerror(errno));
		return NULL;
	}
	
	if (argc!=2) {
		ERROR(
			"Wrong number of arguments! Syntax for module "
			"lvm-conf-extract: <pv-device> <conf-writeout-basename>\n"
		);
		return NULL;
	}
*/
	
/*
	struct bdev * bdev=bdev_lookup_bdev(argv[0]);
	if (!bdev) {
		ERRORF("Couldn't lookup %s\n",argv[0]);
		return NULL;
	}
*/
	struct bdev * bdev=bdev_lookup_bdev(args);
	if (!bdev) {
		ERRORF("Couldn't lookup %s\n",args);
		return NULL;
	}
	
	bdev_read(bdev,1,1,ubuffer);
	
	char * confname=NULL;
	int conf_fd=0;
	
	confname=mprintf("%s.pv",name);
	if (!confname) {
		ERRORF("mprintf: %s\n",strerror(errno));
		return NULL;
	}
	conf_fd=open(confname,O_WRONLY|O_EXCL|O_CREAT,0666);
	if (conf_fd<0) {
		ERRORF(
			"open(\"%s\",O_WRONLY|O_EXCL|O_CREAT,0666): %s\n"
			,confname
			,strerror(errno)
		);
		return NULL;
	}
	WRITE(confname,conf_fd,ubuffer,512);
	close(conf_fd);
	NOTIFYF("Created file %s with size %i\n",confname,512);
	free(confname);
	
	char uuid[39];
	memcpy(uuid,sbuffer+0x20,32); uuid[38]=0;
	
	sbuffer[0x20]=0;
	if (strcmp(sbuffer+0x18,"LVM2 001")) {
		ERROR("Unexpected data at block 1 offset 0x18 (0x218)\n");
		return NULL;
	}
	
	int r=32;
	int w=38;
	int i;
	for (i=6;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	for (i=4;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	for (i=4;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	for (i=4;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	for (i=4;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	for (i=4;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	
	NOTIFYF("Reading PV %s\n",uuid);
	
	bdev_read(bdev,8,1,ubuffer);
	
	confname=mprintf("%s.vg",name);
	if (!confname) {
		ERRORF("mprintf: %s\n",strerror(errno));
		return NULL;
	}
	conf_fd=open(confname,O_WRONLY|O_EXCL|O_CREAT,0666);
	if (conf_fd<0) {
		ERRORF(
			"open(\"%s\",O_WRONLY|O_EXCL|O_CREAT,0666): %s\n"
			,confname
			,strerror(errno)
		);
		return NULL;
	}
	WRITE(confname,conf_fd,sbuffer,512);
	close(conf_fd);
	NOTIFYF("Created file %s with size %i\n",confname,512);
	free(confname);
	
	sbuffer[9]=0;
	if (strcmp(sbuffer+5,"LVM2")) {
		ERROR("Unexpected data at block 8 offset 0x5 (0x1005)\n");
		return NULL;
	}
	
	block_t block=9;
	
	int num_conf_writeouts=0;
	int full_bytes;
	do {
		full_bytes=0;
		int bytes;
		do {
			bdev_read(bdev,block,1,ubuffer);
			for (bytes=0;bytes<512 && ubuffer[bytes];++bytes) ;
			if (bytes) {
				if (!full_bytes) {
					confname=mprintf("%s.%03i",name,num_conf_writeouts);
					if (!confname) {
						ERRORF("mprintf: %s\n",strerror(errno));
						return NULL;
					}
					conf_fd=open(confname,O_WRONLY|O_EXCL|O_CREAT,0666);
					if (conf_fd<0) {
						ERRORF(
							"open(\"%s\",O_WRONLY|O_EXCL|O_CREAT,0666): %s\n"
							,confname
							,strerror(errno)
						);
						return NULL;
					}
				}
				WRITE(confname,conf_fd,sbuffer,bytes);
				full_bytes+=bytes;
			}
			++block;
		} while (bytes==512);
		if (full_bytes) {
			close(conf_fd);
			NOTIFYF("Created file %s with size %i\n",confname,full_bytes);
			free(confname);
			++num_conf_writeouts;
		}
	} while (full_bytes);
	
	return (struct bdev *)0xdeadbeef;
}
