/*
 * stddbg.c. Part of the bothie-utils.
 *
 * Copyright (C) 2002-2004 Bodo Thiesen <bothie@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "stddbg.h"

FILE * stddbg=NULL;

/*
 * This function should be called as first function call from main()
 */
void init_stddbg() {
	static bool initialized=false;
	
	if (initialized) {
		return;
	}
	initialized=true;
	
/*
	ch=dup(STDDBG_FILENO);
	if (ch==-1)
		fprintf(stderr,"dup(%i) failed: %s\n",strerror(errno));
		exit(1);
	else {
		fprintf(stderr,"dup(%i) succeed.\n");
		close(ch);
		fcntl(STDDBG_FILENO,F_SETFL,(fcntl(STDDBG_FILENO,F_GETFL)|O_NDELAY));
	}
*/
	
	stddbg=fdopen(STDDBG_FILENO,"a+");
	if (!stddbg) {
		int errno1=errno;
		
		stddbg=fdopen(STDDBG_FILENO,"a");
		if (!stddbg) {
			int errno2=errno;
			
			stddbg=fopen("/dev/null","a+");
			if (!stddbg) {
				int errno3=errno;
				
				eprintf("fdopen(%i,\"a+\") failed: %s\n",STDDBG_FILENO,strerror(errno1));
				eprintf("fdopen(%i,\"a\") failed: %s\n",STDDBG_FILENO,strerror(errno2));
				eprintf("fdopen(\"/dev/null\",\"a+\") failed: %s\n",strerror(errno3));
				exit(1);
			}
		}
	}
	
	setvbuf(stddbg,NULL,_IONBF,0);
}

int bprintf(const char * format,...) {
	int RC;
	va_list ap;
	
	if (!stddbg) init_stddbg();
	
	va_start(ap,format);
	RC=vfprintf(stddbg,format,ap);
	va_end(ap);
	
	return RC;
}

int vbprintf(const char * format,va_list ap) {
	return vfprintf(stddbg,format,ap);
}

static int level;
static char level_character;
static FILE * level_file=NULL;

static inline void init_level_file() {
	if (!level_file) level_file=stderr;
}

char set_level_character(char new_level_character) {
	char RC;
	
	RC=level_character;
	level_character=new_level_character;
	
	return RC;
}

FILE * set_level_file(FILE * new_level_file) {
	FILE * RC;
	
	init_level_file();
	
	RC=new_level_file;
	new_level_file=new_level_file;
	
	return RC;
}

int lprintf(int deltalevel,const char * format,...) {
	int RC;
	
	va_list ap;
	va_start(ap,format);
	RC=vlprintf(deltalevel,format,ap);
	va_end(ap);
	
	return RC;
}

int vlprintf(int deltalevel,const char * format,va_list ap) {
	int RC;
	int l;
	
	init_level_file();
	
	if (deltalevel<0) --level;
	
	for (l=0;l<level;++l) fputc('\t',level_file);
	
	RC=vfprintf(level_file,format,ap);
	
	if (deltalevel>0) ++level;
	
	return RC;
}

int nlprintf(const char * format,...) {
	int RC;
	va_list ap;
	
	va_start(ap,format);
	RC=vnlprintf(format,ap);
	va_end(ap);
	
	return RC;
}

int vnlprintf(const char * format,va_list ap) {
	int RC;
	
	init_level_file();
	
	RC=vfprintf(level_file,format,ap);
	
	return RC;
}
