/*
 * btfileio_read_entire_file.c. Part of the bothie-utils.
 *
 * Copyright (C) 2006-2015 Bodo Thiesen <bothie@gmx.de>
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

#include "btfileio.h"

#include <errno.h>
#include <unistd.h>

#define BLOCKSIZE 65536

/*
 * Bodo: FIXME: Use mmap() if available!
 */
unsigned char * read_entire_file(int fd, size_t * size) {
	unsigned char * content = NULL;
	unsigned char * tmp;
	size_t i = 0;
	ssize_t r = -1;
	
/*	eprintf("O_NDELAY is %s\n\n",fcntl(0,F_GETFL)&O_NDELAY?"set":"cleared"); */
/*	fcntl(0,F_SETFL,fcntl(0,F_GETFL)&(-1-O_NDELAY)); */
	
	do {
		if (r) {
			content = realloc(tmp = content, i + BLOCKSIZE);
			if (!content) {if (tmp) free(tmp); return NULL;} /* eexit("Konnte Speicherblock nicht vergrößern: "); */
		}
		errno=0;
		r=read(fd,content+i,BLOCKSIZE);
		/* eprintf("read(0,source+i,BLOCKSIZE=%i)=%i (errno=%i): ",BLOCKSIZE,r,errno); */
		/* perror(""); */
		if (r>0) i+=r;
	} while (r || errno==EAGAIN);
	if (errno && errno!=EAGAIN) {free(content); return NULL;}
	content = realloc(tmp = content, (*size = i) + 1);
	if (!content) {free(tmp); return NULL;} /* eexit("Konnte Speicherblock nicht verkleinern: "); */
	content[i]=0;
	return content;
}
