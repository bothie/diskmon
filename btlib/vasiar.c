/*
 * vasiar.c. Part of the bothie-utils.
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

#include "vasiar.h"

#include "bterror.h"

#include <execinfo.h>
#include <stdlib.h>

void dump_stack_trace() {
	void * return_address[100];
	int count=backtrace(return_address,100);
	char * * function_name=backtrace_symbols(return_address,count);
	
	// Print the stack trace
	for (int i=0; i<count; ++i) {
		eprintf("%s\n",function_name[i]);
	}
	
	// Free the string pointers
	free(function_name);
}

void * vasiar_access(
	void * * addr,
	size_t * num,
	size_t * max,
	size_t size,
	size_t idx
) {
	if (idx==(size_t)-1) {
		eprintf(
			"%s:%s:%i (libBtLinuxLibrary): Trying to access vasiar idx==-1\n"
			, __FILE__, __func__, __LINE__
		);
		dump_stack_trace();
		return NULL;
	}
	if (idx>=*max) {
		while (idx>=*max) {
			*max=*max*2+1;
		}
		void * oldptr=*addr;
		if (!(*addr=realloc(oldptr,*max*size))) {
			eprintf(
				"%s:%s:%i (libBtLinuxLibrary): realloc(%p,%i)=NULL: "
				, __FILE__, __func__, __LINE__
				, oldptr
				, (int)(*max * size)
			);
			dump_stack_trace();
			assert_never_reached();
		}
	}
	
	if (idx>=*num) {
		*num=idx+1;
	}
	
	return ((char*)*addr+size*idx);
}

size_t vasiar_access2(
	void * * addr,
	size_t * num,
	size_t * max,
	size_t size,
	size_t idx
) {
	if (idx>=*max) {
		while (idx>=*max) {
			*max=*max*2+1;
		}
		void * oldptr=*addr;
		if (!(*addr=realloc(oldptr,*max*size))) {
			eprintf(
				"%s:%s:%i (libBtLinuxLibrary): realloc(%p,%i)=NULL: "
				, __FILE__, __func__, __LINE__
				, oldptr
				, (int)(*max * size)
			);
			dump_stack_trace();
			assert_never_reached();
		}
	}
	
	if (idx>=*num) {
		*num=idx+1;
	}
	
	return idx;
}
