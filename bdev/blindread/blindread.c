#include "common.h"
#include "bdev.h"

#include <assert.h>
#include <btmacros.h>
#include <fcntl.h>
#include <mprintf.h>
#include <stdbool.h>
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
	struct bdev * bdev=bdev_lookup_bdev(args);
	if (!bdev) {
		ERRORF("Couldn't lookup %s\n",args);
		return NULL;
	}
	
	block_t chunk_size=128;
	u8 * buffer=malloc(chunk_size*bdev_get_block_size(bdev));
	block_t addr=0;
//	addr=104178592-65536;
	
	bttime_t start_time=bttime();
	bttime_t prev_time=start_time-1000000000LLU;
	bttime_t curr_time;
	char * t;
	int format_size=strlen(t=mprintf("%llu",(unsigned long long)bdev_get_size(bdev)));
	free(t);
	block_t errors=0;
	while (addr<bdev_get_size(bdev)) {
		// block_t cs=chunk_size;
		if (bdev_get_size(bdev)-addr<chunk_size) {
			chunk_size=bdev_get_size(bdev)-addr;
		}
		block_t r=bdev_read(bdev,addr,chunk_size,buffer);
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
//			if (r!=chunk_size) {
				ERRORF(
					"%s: Read successfully %llu (0x%llx) blocks at %llu (0x%llx)."
					,bdev_get_name(bdev)
					,r,r
					,addr,addr
				);
//			}
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
		
		curr_time=bttime();
		if (addr==bdev_get_size(bdev) || prev_time+333333333UL<curr_time) {
			prev_time=curr_time;
			char * t1;
			char * t2;
			eprintf(
				"%s: %*llu/%*llu (%s/~%s)\r"
				,bdev_get_name(bdev)
				,format_size
				,(unsigned long long)addr
				,format_size
				,(unsigned long long)bdev_get_size(bdev)
				,t1=mkhrtime((curr_time-start_time)/1000000000)
				,t2=mkhrtime((((curr_time-start_time)/1000000*bdev_get_size(bdev))/addr)/1000)
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
