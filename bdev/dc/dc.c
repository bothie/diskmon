/*
 * dc = disc compare - in Anlehnung an dd für disk dump.
 * 
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "common.h"
#include "bdev.h"

#include <assert.h>
#include <btmacros.h>
#include <btmath.h>
#include <btstr.h>
#include <bttime.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <mprintf.h>
#include <parseutil.h>
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
	bool report_all_read_errors;
	bool report_first_read_error;
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
 * size may be -1 (for backwards compatibility) or empty (argument starts
 * with colon or with a device name itself) for full device(s)
 */
static struct bdev * bdev_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	ignore(bdev_driver);
	ignore(name);
	
	bool dump_differences=true; // false;
	
	struct bdev * retval=NULL;
	const char * p=args;
	
	block_t chunk_size=128;
	size_t block_size=0;
	block_t max_blocks=0;
	
	errno = 0;
	bool have_size = false;
	block_t size = 0; // warning: 'size' could be used uninitialized in this function. (it's protected by have_size)
	
	if (!strstartcmp(p, "-1:")) {
		p += 3;
	} else if (!strstartcmp(p, ":")) {
		++p;
	} else {
		unsigned long long s;
		volatile block_t _size;
		if (':' == parse_unsigned_long_long(&p, &s)) {
			have_size = true;
			size = _size = s;
			assert((unsigned long long)_size == s);
			parse_skip(&p, ':');
		} else {
			p = args;
		}
	}
	
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
			++p;
		}
		struct dc * dv;
		{
			const char * s=p;
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
			dv=&VANEW(device);
			dv->ignore=false;
			dv->report_all_read_errors = false;
			dv->report_first_read_error = true;
			dv->name=name;
			dv->buffer=NULL;
			dv->bdev=bdev_lookup_bdev(name);
			if (!dv->bdev) {
				ERRORF("Couldn't lookup device %s.",name);
				eprintf("vs=%u,p=%s\ns=%s\n",(unsigned)VASIZE(device),p,s);
				goto err;
			}
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
		if (!parse_skip(&p,'@')) {
			dv->offset=0;
		} else {
			errno=0;
			unsigned long o;
			if (!parse_unsigned_long(&p,&o) && pwerrno) {
				ERRORF(
					"Couldn't parse offset ([%s]) for %ith device (%s): %s."
					, p
					, VASIZE(device)
					, name
					, pwerror(pwerrno)
				);
				goto err;
			}
			dv->offset = o;
		}
		dv->num_blocks = bdev_get_size(dv->bdev);
		if (dv->num_blocks < dv->offset) {
			dv->num_blocks = 0;
		} else {
			dv->num_blocks -= dv->offset;
		}
		if (dv->num_blocks > max_blocks) {
			max_blocks = dv->num_blocks;
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
	
	if (have_size && size < max_blocks) {
		max_blocks = size;
	}
	
	block_t addr = 0;
	
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
			if (unlikely(addr >= d->num_blocks)) {
				if (unlikely(addr == d->num_blocks)) {
					eprintf(
						"dc: EOF on %s reached @ block %llu (0x%llx) after %llu (0x%llx) blocks.\n"
						, d->name
						, (unsigned long long)(d->offset + addr)
						, (unsigned long long)(d->offset + addr)
						, (unsigned long long)(addr)
						, (unsigned long long)(addr)
					);
					if (duplicate_errors_on_stdout) printf(
						"dc: EOF on %s reached @ block %llu (0x%llx) after %llu (0x%llx) blocks.\n"
						, d->name
						, (unsigned long long)(d->offset + addr)
						, (unsigned long long)(d->offset + addr)
						, (unsigned long long)(addr)
						, (unsigned long long)(addr)
					);
					PRINT_PROGRESS_BAR(addr);
				}
				d->ignore = true;
				continue;
			}
			if (addr+cs>d->num_blocks) {
				cs=d->num_blocks-addr;
			}
		}
		
		for (i=0;i<VASIZE(device);++i) {
			struct dc * d=&VAACCESS(device,i);
			if (unlikely(addr>=d->num_blocks)) {
				continue;
			}
retry:
			ignore(i);
			block_t r = bdev_read(d->bdev, d->offset + addr, cs, d->buffer, "dc");
			if (r!=cs) {
				if (d->report_all_read_errors || d->report_first_read_error) {
					eprintf("Read problems ...\r");
				}
				if (cs!=1) {
					cs>>=1;
					goto retry;
				}
				memset(d->buffer, 0, block_size);
				if (d->report_all_read_errors || d->report_first_read_error) {
					eprintf(
						"dc: %sead error on device %s @ block %llu (0x%llx) after %llu (0x%llx) blocks.\n"
						, d->report_first_read_error?"First r":"R"
						, d->name
						, (unsigned long long)(d->offset + addr)
						, (unsigned long long)(d->offset + addr)
						, (unsigned long long)(addr)
						, (unsigned long long)(addr)
					);
					if (duplicate_errors_on_stdout) printf(
						"dc: %sead error on device %s @ block %llu (0x%llx) after %llu (0x%llx) blocks.\n"
						, d->report_first_read_error?"First r":"R"
						, d->name
						, (unsigned long long)(d->offset + addr)
						, (unsigned long long)(d->offset + addr)
						, (unsigned long long)(addr)
						, (unsigned long long)(addr)
					);
					d->report_first_read_error = false;
				}
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
				
				eprintf(
					"dc: Data mismatch(es) @ in logical block range %llu..%llu:"
					, (unsigned long long)(addr)
					, (unsigned long long)(addr+cs-1)
				);
				if (duplicate_errors_on_stdout) printf(
					"dc: Data mismatch(es) @ in logical block range %llu..%llu:"
					, (unsigned long long)(addr)
					, (unsigned long long)(addr+cs-1)
				);
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
					eprintf(
						" %s@%llu"
						, di->name
						, (unsigned long long)(di->offset + addr)
					);
					if (duplicate_errors_on_stdout) printf(
						" %s@%llu"
						, di->name
						, (unsigned long long)(di->offset + addr)
					);
					for (j=i+1;j<VASIZE(device);++j) {
						struct dc * dj=&VAACCESS(device,j);
						if (dj->ignore || dj->printed) {
							continue;
						}
						if (!memcmp(di->buffer+mem_offset,dj->buffer+mem_offset,cs*block_size)) {
							eprintf(
								"==%s@%llu"
								, dj->name
								, (unsigned long long)(dj->offset + addr)
							);
							if (duplicate_errors_on_stdout) printf(
								"==%s@%llu"
								, dj->name
								, (unsigned long long)(dj->offset + addr)
							);
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
