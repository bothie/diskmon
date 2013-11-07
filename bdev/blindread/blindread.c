/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "common.h"
#include "bdev.h"

#include <assert.h>
#include <btmacros.h>
#include <fcntl.h>
#include <mprintf.h>
#include <parseutil.h>
#include <stdbool.h>
#include <stddbg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DRIVER_NAME "blindread"

/*
 * Public interface
 */

// nothing

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
static struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	ignore(bdev_driver);
	ignore(name);
	block_t addr=0;
	block_t size=0;
	struct bdev * bdev;
	{
		char * dev_name;
		bool have_size=false;
		const char * cp;
		
		{
			cp=args;
			unsigned long long _size;
			if (':'==parse_unsigned_long_long(&cp,&_size)) {
				args=cp+1;
				have_size=true;
				parse_skip(&cp,':');
			}
			size=_size;
		}
		
		{
			cp=args;
			char c=parse_string(&cp,"@",NULL,NULL,&dev_name);
			eprintf("dev_name=»%s«\n",dev_name);
//			cp="test@123";
//			c=parse_string(&cp,"@",NULL,NULL,&dev_name);
//			eprintf("dev_name=»%s«\n",dev_name);
			if (c=='@') {
				unsigned long long _addr;
				c=parse_unsigned_long_long(&cp,&_addr);
				addr=_addr;
			}
			if (c) {
				ERRORF("Couldn't parse argument »%s«, must be of form [size:]devname[@offset], c='%c' (%u), cp=%p, args=%p\n",args,c,(unsigned)(unsigned char)c,cp,args);
				free(dev_name);
				return NULL;
			}
		}
		bdev=bdev_lookup_bdev(dev_name);
		if (!bdev) {
			ERRORF("Couldn't lookup %s\n",dev_name);
			free(dev_name);
			return NULL;
		}
		block_t real_size=bdev_get_size(bdev);
		if (real_size<addr) {
			ERRORF(
				"Size of device %s is %llu which is smaller than the requested start offset %llu\n"
				,dev_name
				,(unsigned long long)real_size
				,(unsigned long long)addr
			);
			free(dev_name);
			return NULL;
		}
		if (have_size) {
			size+=addr;
			if (size>real_size) {
				ERRORF(
					"It was requested to read all blocks until block %llu but there are only %llu blocks available on device %s\n"
					,(unsigned long long)size
					,(unsigned long long)real_size
					,dev_name
				);
				free(dev_name);
				return NULL;
			}
		} else {
			size=real_size;
		}
		if (addr==real_size) {
			NOTIFYF("We have nothing to do on devive with argument %s\n",args);
			free(dev_name);
			return NULL;
		}
		free(dev_name);
	}
	block_t chunk_size=128;
	u8 * buffer=NULL;
	
	bttime_t start_time=bttime();
	bttime_t prev_time=start_time-1000000000LLU;
	bttime_t curr_time;
	char * t;
	int format_size=strlen(t=mprintf("%llu",(unsigned long long)size));
	free(t);
	block_t errors=0;
	while (addr<size) {
		u8 * memory=NULL;
		// block_t cs=chunk_size;
		if (size-addr<chunk_size) {
			chunk_size=size-addr;
		}
		if (bdev_get_mmapro_function(bdev)) {
			memory=bdev_mmapro(bdev,addr,chunk_size);
			if (memory) {
				unsigned i,j=chunk_size*bdev_get_block_size(bdev);
				u8 sum=0;
				for (i=0;i<j;++i) {
					sum+=memory[i];
				}
				// siginfo->si_addr
				// siginfo->si_error
				// #include <siginfo.h>
				// sigaction
				// http://landru.uwaterloo.ca/cgi-bin/man.cgi?section=5&topic=siginfo
			}
		}
		if (!memory) {
			if (!buffer) {
				buffer=malloc(chunk_size*bdev_get_block_size(bdev));
			}
			block_t r = bdev_read(bdev, addr, chunk_size, buffer, "blindread");
			if (r!=(block_t)-1) {
				/* Uncomment only for debugging!
#warning WRITE CODE IN blindread IS ACTIVATED. DO NOT USE THIS IN PRODUCTIVE ENVIRONMENTS.
				block_t w=bdev_write(bdev,addr,r,buffer);
				if (w!=r) {
					NOTIFYF("%s: Read = %lli, Write = %lli"
						,bdev_get_name(bdev)
						,r
						,w
					);
				}
				*/
/*
//				if (r!=chunk_size) {
					ERRORF(
						"%s: Read successfully %llu (0x%llx) blocks at %llu (0x%llx)."
						,bdev_get_name(bdev)
						,r,r
						,addr,addr
					);
//				}
*/
				addr+=r;
				if (r!=chunk_size) r=-1;
			}
			
			if (r!=chunk_size) {
				ERRORF(
					"%s: Read error at %llu (0x%llx) while reading."
					,bdev_get_name(bdev)
					,(unsigned long long)addr,(unsigned long long)addr
				);
				++addr;
				++errors;
			}
			// addr+=chunk_size;
		}
		
		curr_time=bttime();
		if (addr==size || prev_time+333333333UL<curr_time) {
			prev_time=curr_time;
			char * t1;
			char * t2;
			bprintf(
				"%s: %*llu/%*llu (%s/~%s)\r"
				,bdev_get_name(bdev)
				,format_size
				,(unsigned long long)addr
				,format_size
				,(unsigned long long)size
				,t1=mkhrtime((curr_time-start_time)/1000000000)
				,t2=mkhrtime((((curr_time-start_time)/1000000*size)/addr)/1000)
			);
			free(t1);
			free(t2);
		}
	}
	eprintf("\n");
	if (errors) {
		NOTIFYF("Blindread completed for %s with %llu read errors :-(",bdev_get_name(bdev),(unsigned long long)errors);
	} else {
		NOTIFYF("Blindread completed for %s without any errors ;-)",bdev_get_name(bdev));
	}
	free(buffer);
	return (struct bdev *)0xdeadbeef;
}
