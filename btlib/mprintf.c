/*
 * mprintf.c. Part of the bothie-utils.
 *
 * Copyright (C) 2002-2015 Bodo Thiesen <bothie@gmx.de>
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif // #ifndef _GNU_SOURCE

#include "mprintf.h"
#include "bttypes.h"

#include <stdlib.h>
#include <stdio.h>

#ifdef WIN30
int vsnprintf(char * buffer,int size,const char * fmt,va_list ap) {
	return sprintf(buffer,fmt,ap);
}
#endif /* #ifdef WIN30 */

#ifdef HAVE_VA_COPY
#define ap ap_dup
#else // #ifdef HAVE_VA_COPY
#define ap ap_arg
#endif // #ifdef HAVE_VA_COPY, else

char * vmprintf_sl(size_t * sl,const char * fmt,va_list ap_arg) {
#ifdef WIN30
	int size=32767,nchars; /* Start with a big string ... and hope */
#else
	int size=127,nchars;
#endif
	char * buffer=NULL;
	char * oldbuffer;
	
#ifdef HAVE_VA_COPY
	va_list ap_dup;
#endif // #ifdef HAVE_VA_COPY
	
	for (;;) {
		buffer = (char*)realloc(oldbuffer = buffer, size + 1);
		if (!buffer) {
			if (oldbuffer) free(oldbuffer);
			return NULL;
		}
		
#ifdef HAVE_VA_COPY
		va_copy(ap_dup,ap_arg);
#endif // #ifdef HAVE_VA_COPY
		if (size==(nchars=vsnprintf(buffer,size+1,fmt,ap))) {
#ifdef HAVE_VA_COPY
			va_end(ap_dup);
#endif // #ifdef HAVE_VA_COPY
			*sl=size;
			return buffer;
		}
#ifdef HAVE_VA_COPY
		va_end(ap_dup);
#endif // #ifdef HAVE_VA_COPY
		
		if (nchars>-1) {
			size=nchars;    /* precisely what is needed */
		} else {
			size=size*2+1;  /* twice the old size */
		}
	}
}

char * mprintf_sl(size_t * sl,const char * fmt,...) {
	char * retval;
	va_list ap;
	
	va_start(ap,fmt);
	retval=vmprintf_sl(sl,fmt,ap);
	va_end(ap);
	
	return retval;
}

char * mprintf(const char * fmt,...) {
	char * retval;
	va_list ap;
	size_t sl;
	
	va_start(ap,fmt);
	retval=vmprintf_sl(&sl,fmt,ap);
	va_end(ap);
	return retval;
}
