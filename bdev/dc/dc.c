/*
 * In anlehnung an dd für disk dump:
 *
 * dc = disc compare
 */

#include "common.h"
#include "bdev.h"

#include <assert.h>
#include <btmacros.h>
#include <btmath.h>
#include <bttime.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <mprintf.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vasiar.h>

#define DRIVER_NAME "dc"

/*
 * Public interface
 */

// nothing

/*
 * Indirect public interface (via struct dev)
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

struct dc {
	char * name;
	struct bdev * bdev;
	block_t offset;
	block_t num_blocks;
	u8 * buffer;
	bool ignore;
	bool printed;
	bool dump;
};

VASIAR(struct dc) device;

/*
 * Implementation - everything non-public (except for init of course).
 *
 * args:
 *
 * size:dev1[@offset1],dev2[@offset2][,dev3[@offset3][...]]
 *
 * size may be -1 for full device
 */
static struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	ignore(bdev_driver);
	ignore(name);
	
	bool dump_differences=true; // false;
	
	struct bdev * retval=NULL;
	const char * s=args;
	const char * p=args;
	
	block_t chunk_size=128;
	size_t block_size=0;
	block_t max_blocks=0;
//	block_t max_blocks2=0;
	
	errno=0;
	block_t size=strtoll(s,&p,0);
//	eprintf("bdev_init: size=%llu, s=\"%s\", p=\"%s\"\n",(unsigned long long)size,s,p);
	if (*p!=':'
	||  ((size!=(block_t)-1) && (size>=LLONG_MAX || (size<0)))) {
		ERROR("Couldn't parse size argument. Syntax is: \"size:dev1[@offset1],dev2[@offset2][,dev3[@offset3][...]]\".");
		return NULL;
	}
	s=++p;
	if (!*p) {
		ERROR("No devices named in arguments.");
		goto err;
	}
	do {
		/*
		 * Starting with the second loop, we must skip the ',' 
		 * character. We do this in the first loop as well, to ease 
		 * generation of the argument line by scripts.
		 */
		if (*p==',') {
			s=++p;
		}
		while (*p && *p!='@' && *p!=',') ++p;
		if (!(p-s)) {
			ERROR("Couldn't parse arguments (empty device name).");
			goto err;
		}
		
		char * name=malloc(p-s+1);
		if (!name) {
			ERROR("Couldn't allocate memory for device name.");
			goto err;
		}
		memcpy(name,s,p-s);
		name[p-s]=0;
		struct dc * dv=&VANEW(device);
		dv->ignore=false;
		dv->name=name;
/*
		printf("VASIZE(device)=%i:\n",VASIZE(device));
		for (size_t i=0;i<VASIZE(device);++i) {
			printf("\tdevice[%i].name=\"%s\"\n",i,VAACCESS(device,i).name);
		}
*/
		dv->buffer=NULL;
		dv->bdev=bdev_lookup_bdev(name);
		if (!dv->bdev) {
			ERRORF("Couldn't lookup device %s.",name);
			eprintf("vs=%u,p=%s\ns=%s\n",(unsigned)VASIZE(device),p,s);
			goto err;
		}
		if (VASIZE(device)==1) {
			block_size=bdev_get_block_size(dv->bdev);
		} else {
			if (block_size!=bdev_get_block_size(dv->bdev)) {
				ERRORF(
					"Block sizes of devices %s (%u) and %s (%u) differ."
					,bdev_get_name(dv->bdev)
					,bdev_get_block_size(dv->bdev)
					,bdev_get_name(VAACCESS(device,0).bdev)
					,bdev_get_block_size(VAACCESS(device,0).bdev)
				);
				goto err;
			}
		}
		dv->buffer=malloc(chunk_size*block_size);
		if (!dv->buffer) {
			ERROR("Couldn't allocate memory for data buffer.");
			goto err;
		}
		if (*p!='@') {
			dv->offset=0;
		} else {
			s=p+1;
			errno=0;
			dv->offset=strtoull(s,&p,0);
			if (dv->offset>=LLONG_MAX || dv->offset<0) {
				ERRORF(
					"Couldn't parse offset for %ith device (%s)."
					,VASIZE(device)
					,name
				);
				goto err;
			}
		}
		dv->num_blocks=bdev_get_size(dv->bdev)-dv->offset;
		block_t nb=dv->num_blocks;
		if (nb>max_blocks) {
//			max_blocks2=max_blocks;
			max_blocks=nb;
		}
		if (size==(block_t)-1) {
			size=nb;
		} else {
			if (nb>size) nb=size;
		}
	} while (*p==',');
	
	if (*p) {
		ERRORF(
			"Couldn't parse arguments: Garbage found: %s."
			,p
		);
		goto err;
	}
	
	if (VASIZE(device)<2) {
		ERROR("At least two devices must be given.");
		goto err;
	}
	
	block_t addr=1;
	
	bool duplicate_errors_on_stdout=false;
	{
		struct stat stat_stdout;
		struct stat stat_stderr;
		if (fstat(fileno(stderr),&stat_stderr)
		||  fstat(fileno(stdout),&stat_stdout)
		||  stat_stdout.st_dev!=stat_stderr.st_dev
		||  stat_stdout.st_ino!=stat_stderr.st_ino
		||  stat_stdout.st_rdev!=stat_stderr.st_rdev) {
			duplicate_errors_on_stdout=true;
		}
	}
	OPEN_PROGRESS_BAR_LOOP(0,addr,max_blocks)
		block_t cs=btrand(chunk_size)+1;
		size_t i,j;
		
		for (i=0;i<VASIZE(device);++i) {
			struct dc * d=&VAACCESS(device,i);
			if (unlikely(addr>=d->num_blocks)) {
				if (unlikely(addr==d->num_blocks)) {
					eprintf(
						"dc: EOF on %s reached @ block %llu (0x%llx) after %llu (0x%llx) blocks.\n"
						,d->name
						,d->offset+addr
						,d->offset+addr
						,addr
						,addr
					);
					if (duplicate_errors_on_stdout) printf(
						"dc: EOF on %s reached @ block %llu (0x%llx) after %llu (0x%llx) blocks.\n"
						,d->name
						,d->offset+addr
						,d->offset+addr
						,addr
						,addr
					);
					PRINT_PROGRESS_BAR(addr);
				}
				d->ignore=true;
				continue;
			}
			if (addr+cs>d->num_blocks) {
				cs=d->num_blocks-addr;
			}
		}
		
		for (i=0;i<VASIZE(device);++i) {
			struct dc * d=&VAACCESS(device,i);
			if (unlikely(addr>=d->num_blocks)) {
//				eprintf("Skipping %i. device.\n",i+1);
				continue;
			}
retry:
			ignore(i);
			block_t r=bdev_read(d->bdev,d->offset+addr,cs,d->buffer);
			d->ignore=false;
			if (r!=cs) {
				eprintf("Read problems ...\r");
				if (cs!=1) {
					cs>>=1;
					goto retry;
				}
				eprintf(
					"dc: Read error on device %s @ block %llu (0x%llx) after %llu (0x%llx) blocks.\n"
					,d->name
					,d->offset+addr
					,d->offset+addr
					,addr
					,addr
				);
				if (duplicate_errors_on_stdout) printf(
					"dc: Read error on device %s @ block %llu (0x%llx) after %llu (0x%llx) blocks.\n"
					,d->name
					,d->offset+addr
					,d->offset+addr
					,addr
					,addr
				);
				d->ignore=true;
				PRINT_PROGRESS_BAR(addr);
			}
		}
		
		struct dc * di=NULL;
		for (i=0;i<VASIZE(device);++i) {
			di=&VAACCESS(device,i);
			if (!di->ignore) {
				break;
			}
		}
		if (i==VASIZE(device)) {
			PRINT_PROGRESS_BAR(addr);
			eprintf("dc: EOF reached on all devices\n");
			if (duplicate_errors_on_stdout) printf("dc: EOF reached on all devices\n");
			break;
		}
		
		block_t loaded_cs=cs;
		size_t mem_offset=0;
		
		while (loaded_cs) {
/*
			printf(
				"addr=%llu, mem_offset=%u, cs=%u, loaded_cs=%u, i=%u\n"
				,(unsigned long long)addr
				,(unsigned)mem_offset
				,(unsigned)cs
				,(unsigned)loaded_cs
				,(unsigned)i
			);
*/
			bool equal=true;
			for (j=i+1;j<VASIZE(device);++j) {
				struct dc * dj=&VAACCESS(device,j);
				if (dj->ignore) {
					continue;
				}
				unsigned char * memi=di->buffer+mem_offset;
				unsigned char * memj=dj->buffer+mem_offset;
				size_t memc=cs*block_size;
				int memcmp_result=memcmp(memi,memj,memc);
/*
				printf(
					"\tmemcmp(%p+%u=%p,%p+%u=%p,%u)=%i\n"
					,di->buffer,mem_offset,memi
					,dj->buffer,mem_offset,memj
					,memc,memcmp_result
				);
*/
				if (memcmp_result) {
					equal=false;
					break;
				}
			}
			
			if (!equal) {
				if (cs>1) {
					cs>>=1;
					continue;
				}
				
				eprintf("dc: Data mismatch(es) @ in logical block range %llu..%llu:",addr,addr+cs-1);
				if (duplicate_errors_on_stdout) printf("dc: Data mismatch(es) @ in logical block range %llu..%llu:",addr,addr+cs-1);
				bool first=true;
				
				for (j=i+1;j<VASIZE(device);++j) {
					struct dc * dj=&VAACCESS(device,j);
					dj->printed=false;
					dj->dump=false;
				}
				int saved_i=i;
				for (;;) {
					di->dump=true;
					if (first) {
						first=false;
					} else {
						eprintf(" !=");
						if (duplicate_errors_on_stdout) printf(" !=");
					}
					eprintf(" %s@%llu",di->name,di->offset+addr);
					if (duplicate_errors_on_stdout) printf(" %s@%llu",di->name,di->offset+addr);
					for (j=i+1;j<VASIZE(device);++j) {
						struct dc * dj=&VAACCESS(device,j);
						if (dj->ignore || dj->printed) {
							continue;
						}
						if (!memcmp(di->buffer+mem_offset,dj->buffer+mem_offset,cs*block_size)) {
							eprintf("==%s@%llu",dj->name,dj->offset+addr);
							if (duplicate_errors_on_stdout) printf("==%s@%llu",dj->name,dj->offset+addr);
							dj->printed=true;
						}
					}
					for (i++;i<VASIZE(device);++i) {
						di=&VAACCESS(device,i);
						if (!di->ignore && !di->printed) {
							break;
						}
					}
					if (i==VASIZE(device)) {
						break;
					}
				}
				eprintf("\n");
				if (duplicate_errors_on_stdout) printf("\n");
				if (dump_differences) {
					// for (j=saved_i;j<VASIZE(device);++j) {
					for (j=0;j<VASIZE(device);++j) {
						struct dc * dj=&VAACCESS(device,j);
						if (!dj->ignore && dj->dump) {
							printf(
								"\nDump of block %llu of device %s:\n\n"
								,(unsigned long long)dj->offset+addr
								,dj->name
							);
							for (int y=0;y<512;y+=16) {
								printf("%03x:",y);
								for (int x=0;x<16;++x) {
									printf(" %02x",dj->buffer[mem_offset+y+x]);
								}
								printf("  ");
								for (int x=0;x<16;++x) {
									printf("%c",dj->buffer[mem_offset+y+x]>=32?dj->buffer[mem_offset+y+x]:'?');
								}
								putchar('\n');
							}
						}
					}
				}
				
/*
				for (size_t i=0;i<VASIZE(device);++i) {
					printf("%s:\n",di->name);
					for (int b=0;b<cs;++b) {
						printf("\t%3i:",b);
						for (int j=0;j<16;++j) {
							printf(" %02x",(unsigned)di->buffer[mem_offset+b*block_size+j]);
						}
						putchar('\n');
					}
				}
*/
				
/*
				for (i=0;i<VASIZE(device);++i) {
					VAACCESS(device,i).ignore=false;
				}
*/
				i=saved_i;
				di=&VAACCESS(device,i);
/*
			} else {
				for (j=i;j<VASIZE(device);++j) {
					struct dc * dj=&VAACCESS(device,j);
					if (!dj->ignore) {
						printf(
							"\nDump of block %llu of device %s:\n\n"
							,(unsigned long long)dj->offset+addr
							,dj->name
						);
						for (int y=0;y<512;y+=16) {
							printf("%03x:",y);
							for (int x=0;x<16;++x) {
								printf(" %02x",dj->buffer[mem_offset+y+x]);
							}
							printf("  ");
							for (int x=0;x<16;++x) {
								printf("%c",dj->buffer[mem_offset+y+x]>=32?dj->buffer[mem_offset+y+x]:'?');
							}
							putchar('\n');
						}
					}
				}
*/
			}
			
			addr+=cs;
			mem_offset+=cs*block_size;
			loaded_cs-=cs;
			block_t new_cs;
			for (new_cs=1;new_cs<=loaded_cs;new_cs<<=1) ;
			cs=new_cs>>1;
		}
		
		PRINT_PROGRESS_BAR(addr);
	CLOSE_PROGRESS_BAR_LOOP()
	
	eprintf("dc completed ;-)\n");
	retval=(struct bdev *)0xdeadbeef;

err:
	{
		size_t vs=VASIZE(device);
		while (vs--) {
			struct dc * d=&VAACCESS(device,vs);
			free(d->name);
			free(d->buffer);
		}
	}
	VAFREE(device);
	return retval;
}
