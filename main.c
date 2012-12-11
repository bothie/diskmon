#include "bdev.h"
#include "common.h"

#include <bterror.h>
#include <btstr.h>
#include <bttime.h>
#include <dlfcn.h>
#include <errno.h>
#include <mprintf.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc,char * argv[]) {
	setlinebuf(stdout);
	for (int a=1;a<argc;++a) {
		char * driver_name;
		char * name;
		char * p;
		char * start;
		
		p=argv[a];
		
//		printf("Processing \"%s\"\n",p);
		start=p;
		while (*p && *p!=':') ++p;
		if (!*p) {
			eprintf("Invalid argument: %s\n",argv[a]);
			exit(2);
		}
		driver_name=malloc(p-argv[a]+1);
		memcpy(driver_name,start,p-start);
		driver_name[p-start]=0;
		++p;
//		eprintf("Need driver %s\n",driver_name);
//		printf("\tdriver=\"%s\"\n",driver);
		struct bdev_driver * bdev_driver=bdev_lookup_driver(driver_name);
//		printf("\t\tinit=%p\n",init);
		if (!bdev_driver) {
//			eprintf("Have to load driver %s\n",driver_name);
			char * filename=mprintf("%s.so",driver_name);
			if (!filename) {
				eprintf(
					"mprintf(\"%%s.so\",\"%s\") failed: %s\n"
					,driver_name
					,strerror(errno)
				);
				exit(2);
			}
//			printf("\t\t\tsoname=\"%s\"\n",filename);
			void * handle=dlopen(filename,RTLD_NOW|RTLD_GLOBAL);
//			printf("\t\t\tdlopen-handle=%p\n",handle);
			if (!handle) {
				eprintf(
					"dlopen failed: %s\n"
					,dlerror()
				);
				exit(2);
			}
			bdev_driver=bdev_lookup_driver(driver_name);
//			printf("\t\t\tinit=%p\n",init);
			if (!bdev_driver) {
				eprintf(
					"Loaded %s.so successfully, but it didn't register itself to the dev framework.\n"
					,filename
				);
				exit(2);
			}
			free(filename);
//			printf("\t\t\tloop_init=%p\n",dlsym(handle,"loop_init"));
			// ggf handle merken.
//		} else {
//			eprintf("Driver %s already present\n",driver_name);
		}
		start=p;
		while (*p && *p!='=') ++p;
		if (!*p) {
			eprintf("Invalid syntax in argument: %s\n",argv[a]);
			eprintf("Correct syntax is: driver:device_name=arguments\n");
			exit(2);
		}
		name=malloc(p-argv[a]+1);
		memcpy(name,start,p-start);
		name[p-start]=0;
		++p;
//		printf("\t\tname=\"%s\"\n",name);
		if (bdev_lookup_bdev(name)) {
			eprintf(
				"Device with name %s already registered"
				,name
			);
			exit(2);
		}
//		printf("\t\targ=\"%s\"\n",p);
//		printf("\t\tinit=%p(name=\"%s\",p=\"%s\")\n",init,name,p);
		char * name_copy=mstrcpy(name);
		if (!bdev_driver->init(bdev_driver,name,p)) {
			eprintf(
				"Couldn't register device %s of type %s with args %s\n"
				,name_copy
				,driver_name
				,p
			);
			exit(2);
		}
		free(name_copy);
		free(driver_name);
	}
	
	char * filename="ext2.so";
	void * ext2_driver=dlopen(filename,RTLD_NOW|RTLD_GLOBAL);
	if (!ext2_driver) {
		eprintf("Couldn't load %s",filename);
		exit(2);
	}
	
	bool (*ext2_fsck)(char * name);
	ext2_fsck=(bool (*)(char *))dlsym(ext2_driver,"ext2_fsck");
	if (!ext2_fsck) {
		eprintf("Couldn't lookup %s","ext2_fsck");
		exit(2);
	}
	
//	ext2_fsck("platz/home");
//	ext2_fsck("root");
	ext2_fsck("aesraid5/home");
	
	exit(0);
	
/*
	unsigned char * buffer=malloc(16*512);
	struct dev * s1r7=dev_lookup("s1r7");
	s1r7->read(s1r7,0,16,buffer);
	eprintf("Writing 16*512 bytes to stdout = ");
	eprintf("%i\n",write(1,buffer,16*512));
*/
	
	// Interactive mode?
	
#define CONST_64 2048

#define BDEV_LOOKUP(var,bdev) do { \
	var=bdev_lookup_bdev(bdev); \
	if (!var) { \
		eprintf("Couldn't lookup %s\n",bdev); \
		exit(2); \
	} \
} while (0)
	
/*
	struct dev * dev1; DEV_LOOKUP(dev1,"platz/home");
	struct dev * dev2; DEV_LOOKUP(dev2,"home");
	
	unsigned char * buffer1=malloc(CONST_64*512);
	unsigned char * buffer2=malloc(CONST_64*512);
	
	off_t start_pos1=2*0;
	off_t start_pos2=2*0;
	off_t num_blocks=2*1465156603LLU;
*/
	
	struct bdev * bdev1; BDEV_LOOKUP(bdev1,"e6");
	struct bdev * bdev2; BDEV_LOOKUP(bdev2,"e6");
	
	unsigned char * buffer1=malloc(CONST_64*512);
	unsigned char * buffer2=malloc(CONST_64*512);
	
	off_t start_pos1=2*104857536LLU;
	off_t start_pos2=2*0;
	off_t num_blocks=2*104857536LLU;
	
	off_t pos1=start_pos1;
	off_t pos2=start_pos2;
	off_t num=num_blocks;
	
	{ // First check the last part of the range
		block_t r;
		if (CONST_64!=(r=bdev_read(bdev1,pos1+num_blocks-CONST_64,CONST_64,buffer1))) {
			eprintf("bdev1->read failed (r=%llu)\n",(unsigned long long)r);
		}
		if (CONST_64!=(r=bdev_read(bdev2,pos2+num_blocks-CONST_64,CONST_64,buffer2))) {
			eprintf("bdev2->read failed (r=%llu)\n",(unsigned long long)r);
		}
		if (memcmp(buffer1,buffer2,CONST_64*512)) {
			eprintf(
				"Data mismatch @ bdev1:%llu vs bdev2:%llu [end of devices]\n"
				,(unsigned long long)(pos1+num_blocks-CONST_64)
				,(unsigned long long)(pos2+num_blocks-CONST_64)
			);
		}
	}
	
	bttime_t start_time=bttime();
	bttime_t prev_time=start_time-1000000000LLU;
	bttime_t curr_time;
	char * t;
	int format_size=strlen(t=mprintf("%llu",(unsigned long long)num_blocks));
	free(t);
	while (num) {
		block_t n=CONST_64;
		if (unlikely(n>num)) n=num;
		bdev_read(bdev1,pos1,n,buffer1);
		bdev_read(bdev2,pos2,n,buffer2);
		if (memcmp(buffer1,buffer2,n*512)) {
			eprintf(
				"Data mismatch @ bdev1:%llu vs bdev2:%llu\n"
				,(unsigned long long)pos1
				,(unsigned long long)pos2
			);
		}
		pos1+=n;
		pos2+=n;
		num-=n;
		
		curr_time=bttime();
		if (!num || prev_time+333333333UL<curr_time) {
			prev_time=curr_time;
			char * t1;
			char * t2;
			fprintf(stderr
				,"%*llu/%*llu (%s/~%s)\r"
				,format_size
				,(unsigned long long)(pos1-start_pos1)
				,format_size
				,(unsigned long long)(num_blocks)
				,t1=mkhrtime((curr_time-start_time)/1000000000)
				,t2=mkhrtime(((curr_time-start_time)/1000000*(num_blocks)/(pos1-start_pos1))/1000)
			);
			free(t1);
			free(t2);
		}
	}
//	104857536-209715071 == 000000000-104857535
	
/*
	struct dev * e6=dev_lookup("e6");
	struct dev * e7=dev_lookup("e7");
	
#define CONST_384 384
#define CONST_64  (64)
	
	unsigned char * buffer_e6=malloc(2*CONST_64*512);
	unsigned char * buffer_e7=malloc(2*CONST_64*512);
	
	off_t e6_pos_start=0;
	off_t e7_pos_start=314572415;
	off_t num_start=104857536;
	
	off_t e6_pos=e6_pos_start;
	off_t e7_pos=e7_pos_start;
	off_t num=num_start;
	
	{ // First check the last part of the range
		e6->read(e6,2*(e6_pos+num-2*CONST_64)+CONST_384,2*CONST_64,buffer_e6);
		e7->read(e7,2*(e7_pos+num-2*CONST_64)+CONST_384,2*CONST_64,buffer_e7);
		if (memcmp(buffer_e6,buffer_e7,2*CONST_64*512)) {
			eprintf("Data mismatch @ e6:%llu vs e7:%llu\n",e6_pos+num-CONST_64,e7_pos+num-CONST_64);
		}
	}
	
	bttime_t start_time=bttime();
	bttime_t prev_time=start_time;
	bttime_t curr_time;
	char * t;
	int format_size=strlen(t=mprintf("%llu",num_start));
	free(t);
	while (num) {
		e6->read(e6,2*(e6_pos)+CONST_384,2*CONST_64,buffer_e6);
		e7->read(e7,2*(e7_pos)+CONST_384,2*CONST_64,buffer_e7);
		num-=CONST_64;
		if (memcmp(buffer_e6,buffer_e7,2*CONST_64*512)) {
			eprintf("Data mismatch @ e6:%llu vs e7:%llu\n",e6_pos,e7_pos);
		}
		e6_pos+=CONST_64;
		e7_pos+=CONST_64;
		
		curr_time=bttime();
		if (!num || prev_time+333333333UL<curr_time) {
			prev_time=curr_time;
			char * t1;
			char * t2;
			fprintf(stderr
				,"%*llu/%*llu (%s/~%s)\r"
				,format_size
				,num_start-num
				,format_size
				,num_start
				,t1=mkhrtime((curr_time-start_time)/1000000000)
				,t2=mkhrtime(((curr_time-start_time)/1000000*(num_start)/(num_start-num))/1000)
			);
			free(t1);
			free(t2);
		}
	}
*/
}
