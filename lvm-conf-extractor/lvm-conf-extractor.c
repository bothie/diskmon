/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include <bterror.h>
#include <errno.h>
#include <fcntl.h>
#include <mprintf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

int main(int argc,char * argv []) {
	unsigned char buffer[512];
	
	if (argc!=3) {
		printf(
			"Syntax: lvm-conf-extracter <pv-device> <conf-writeout-basename>\n"
		);
		exit(1);
	}
	int fd=open(argv[1],O_RDONLY);
	if (fd<0) {
		eprintf(
			"open(\"%s\",O_RDONLY): %s\n"
			,argv[1]
			,strerror(errno)
		);
		exit(2);
	}
	
	LSEEK(argv[1],fd,0x218);
	READ(argv[1],fd,buffer,8);
	buffer[8]=0;
	if (strcmp((char *)buffer,"LVM2 001")) {
		eprintf(
			"Unexpected data [0x218]\n"
		);
		exit(2);
	}
	unsigned char uuid[39];
	READ(argv[1],fd,uuid,32);
	uuid[38]=0;
	int r=32;
	int w=38;
	int i;
	for (i=6;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	for (i=4;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	for (i=4;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	for (i=4;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	for (i=4;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	for (i=4;i--;) uuid[--w]=uuid[--r]; uuid[--w]='-';
	if (r!=w || r!=6) {
		eprintf("uuid-fehler\n");
		exit(2);
	}
	printf("Lese PV %s\n",uuid);
	READ(argv[1],fd,buffer,4); // Unbekannte Bedeutung
	// READ(argv[1],fd,num_conf_list_blocks,4); <--- stimmt nicht!
	
	LSEEK(argv[1],fd,0x1005);
	READ(argv[1],fd,buffer,4);
	buffer[4]=0;
	if (strcmp((char *)buffer,"LVM2")) {
		eprintf(
			"Unexpected data [0x1005]\n"
		);
		exit(2);
	}
	
	// off_t next_conf_pointer=0x1200;
	// LSEEK(argv[1],fd,next_conf_pointer);
	LSEEK(argv[1],fd,0x1200);
	int num_conf_writeouts=0;
	int full_bytes;
	do {
		char * confname=NULL;
		full_bytes=0;
		int bytes;
		int conf_fd=0;
		do {
			READ(argv[1],fd,buffer,0x200);
			for (bytes=0;bytes<0x200 && buffer[bytes];++bytes) ;
			if (bytes) {
				if (!full_bytes) {
					confname=mprintf(
						argv[2]
						,num_conf_writeouts
					);
					if (!confname) {
						eprintf(
							"mprintf: %s\n"
							,strerror(errno)
						);
						exit(2);
					}
					conf_fd=open(confname,O_WRONLY|O_TRUNC|O_CREAT,0666);
					if (conf_fd<0) {
						eprintf(
							"open(\"%s\",O_WRONLY|O_TRUNC|O_CREAT,0666): %s\n"
							,confname
							,strerror(errno)
						);
						exit(2);
					}
				}
				WRITE(confname,conf_fd,buffer,bytes);
				full_bytes+=bytes;
			}
		} while (bytes==0x200);
		if (full_bytes) {
			close(conf_fd);
			printf("Created file %s with size %i\n",confname,full_bytes);
			free(confname);
			++num_conf_writeouts;
		}
	} while (full_bytes);
	exit(0);
}
