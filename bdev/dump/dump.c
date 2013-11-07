/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "common.h"
#include "bdev.h"

#include <assert.h>
#include <btmacros.h>
#include <errno.h>
#include <fcntl.h>
#include <mprintf.h>
#include <parseutil.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DRIVER_NAME "dump"

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

block_t my_bdev_read(struct bdev * dev, block_t first, block_t num, unsigned char * data, const char * reason) {
	return bdev_read(dev, first, num, data, reason);
}

block_t my_bdev_write(struct bdev * dev, block_t first, block_t num, unsigned char * data, const char * reason) {
	ignore(reason); // TODO
	return bdev_write(dev,first,num,data);
}

ssize_t my_posix_read(int fd,void * buf,size_t count) {
	return read(fd,buf,count);
}

ssize_t my_posix_write(int fd,void * buf,size_t count) {
	return write(fd,buf,count);
}

/*
 * Implementation - everything non-public (except for init of course).
 */
static struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	ignore(bdev_driver);
	ignore(name);
	block_t addr=0;
	block_t size=0;
	
	block_t (*bdev_rw)(struct bdev * dev, block_t first, block_t num, unsigned char * data, const char * reason) = NULL;
	ssize_t (*posix_rw)(int fd,void * buf,size_t count)=NULL;
	bool dumping_to_file;
	int fd=-1;
	
	struct bdev * bdev;
	{
		char * dev_name;
		bool have_size=false;
		const char * cp;
		
		{
			cp=args;
			unsigned long long _size;
			if (':'==parse_unsigned_long_long(&cp,&_size)) {
				++cp;
				have_size=true;
			}
			size=_size;
			
			char c=parse_string(&cp,"@",NULL,NULL,&dev_name);
			if (c=='@') {
				unsigned long long _addr;
				c=parse_unsigned_long_long(&cp,&_addr);
				addr=_addr;
			}
			if (c=='<' || c=='>') {
				++cp; // skip '<' or '>'
				int posix_flags;
				switch (c) {
					case '>': { bdev_rw=my_bdev_read; posix_rw=my_posix_write; dumping_to_file=true; posix_flags=O_WRONLY|O_CREAT|O_TRUNC; break; }
					case '<': { bdev_rw=my_bdev_write; posix_rw=my_posix_read; dumping_to_file=false; posix_flags=O_RDONLY; break; }
					default: assert(never_reached); raise(SIGKILL);
				}
				fd=open(cp,posix_flags,0600);
				if (fd<0) {
					ERRORF("Couldn't open file %s: %s",cp,strerror(errno));
					free(dev_name);
					return NULL;
				}
				c=0;
			} else {
				c=1;
			}
			if (c) {
				ERRORF("Couldn't parse argument »%s«, must be of form [size:]devname[@offset]{>|<}filename\n",args);
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
	int format_size;
	{
		size_t sl;
		char * t = mprintf_sl(&sl, "%llu", (unsigned long long)size);
		format_size = sl;
		free(t);
	}
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
			ssize_t l=chunk_size*bdev_get_block_size(bdev);
			if (!buffer) {
				buffer=malloc(l);
			}
			if (!dumping_to_file) {
				ssize_t r=posix_rw(fd,buffer,l);
				if (r%bdev_get_block_size(bdev)==0) {
					chunk_size=r/bdev_get_block_size(bdev);
					if (!chunk_size) break;
				} else {
					ERRORF(
						"%s: Couldn't read from file\n"
						,bdev_get_name(bdev)
					);
					break;
				}
			}
			/*
			eprintf(
				"bdev_rw[%s](bdev,%llu,%llu,%p)\n"
				,(bdev_rw==my_bdev_read)?"read":((bdev_rw==my_bdev_write)?"write":"???")
				,(unsigned long long)addr
				,(unsigned long long)chunk_size
				,(void *)buffer
			);
			*/
			block_t r = bdev_rw(bdev, addr, chunk_size, buffer, "dump");
			if (dumping_to_file) {
				if (r!=chunk_size) {
					ERRORF(
						"%s: Couldn't read from block device for writing to file"
						,bdev_get_name(bdev)
					);
					break;
				}
				ssize_t w=posix_rw(fd,buffer,r*bdev_get_block_size(bdev));
				if (w!=l) {
					ERRORF(
						"%s: Couldn't write to file\n"
						,bdev_get_name(bdev)
					);
					break;
				}
			}
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
			eprintf(
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
	close(fd);
	return (struct bdev *)0xdeadbeef;
}
