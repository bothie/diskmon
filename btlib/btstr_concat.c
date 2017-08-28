/*
 * btstr_concat.c. Part of the bothie-utils.
 *
 * Copyright (C) 2006 Bodo Thiesen <bothie@gmx.de>
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

#include "btstr.h"

#include <string.h>

char * concat(int n, ...) {
	va_list ap;
	int i,string_offset,dest_strlen=1;
	int * sl;
	char * dest_string;
	char * * str;
	
	/*
	 * Cache-Variables: Holds the address of the strings, passed as
	 * arguments after 'int n', and their's sizes.
	 * But first allocate them:
	 */
	sl =(int *)malloc(sizeof(*sl)*n);
	str=(char * *)malloc(sizeof(*str)*n);
	if (expect(!sl || !str,0)) {
		if (sl ) free(sl);
		if (str) free(str);
		
		return NULL;
	}
	
	/*
	 * Walk throw the argument list, and summate the string sizes.
	 * At this time, fill in the determined information in the caches.
	 */
	va_start(ap,n);
	for (i=0;i<n;i++) {
		dest_strlen+=(
			sl[i]=strlen(
				str[i]=va_arg(ap,char*)
			)
		);
	}
	va_end(ap);
	
	/*
	 * Ok, let's allocate the memory for the destination string, ...
	 */
	dest_string=(char*)malloc(dest_strlen);
	if (expect(!dest_string,0)) {
		free(sl);
		free(str);
		
		return NULL;
	}
	
	/*
	 * ... and copy the string's contents together.
	 */
	for (i=string_offset=0;i<n;i++) {
		strcpy(dest_string+string_offset,str[i]);
		string_offset+=sl[i];
	}
	free(sl);
	free(str);
	
	return dest_string;
}
