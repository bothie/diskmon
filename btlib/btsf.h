/*
 * btsf.c. Part of the bothie-utils.
 *
 * Copyright (C) 2010-2015 Bodo Thiesen <bothie@gmx.de>
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

#ifndef BTSF_H
#define BTSF_H

#include "btmacros.h"
#include "btstr.h"
#include "mprintf.h"

#ifdef __cplusplus
#include <cstdbool>
#include <cstdlib>
#include <cstring>
#else // #ifdef __cplusplus
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#endif // #ifdef __cplusplus, else

#ifdef __cplusplus
extern "C"
#endif

struct string_factory;

struct string_factory * sf_new();

void sf_free(struct string_factory * sf);

/*
 * sf_c_str
 * sf_c_str_reown0
 * sf_c_str_reown1
 * sf_c_str_reownX
 *
 * Builds and returns the string stored in the string factory.
 *
 * sf_c_str is a shortcut for sf_c_str_reown1
 * sf_c_str_reown1/sf_c_str_reown0 are shortcuts for sf_c_str_reownX 
 * being called with reown=true/false
 * sf_c_str_reownX actually builds the string. If reown is true, sf will be 
 * freed before the call returns (even if it fails). sf may not be used again.
 * A call with reown=0 builds the string and returns it, but leaves the 
 * string factory intact allowing for incremental results to be returned. Also 
 * this may be used for debugging.
 */
#define sf_c_str sf_c_str_reown1
#define sf_c_str_reown0(sf) sf_c_str_reownX(sf,false)
#define sf_c_str_reown1(sf) sf_c_str_reownX(sf,true)
char * sf_c_str_reownX(struct string_factory * sf,bool reown);

#define sf_lstr sf_lstr_reown1
#define sf_lstr_reown0(sf) sf_lstr_reownX(sf, false)
#define sf_lstr_reown1(sf) sf_lstr_reownX(sf, true)
struct lstring * sf_lstr_reownX(struct string_factory * sf, bool reown);

#define sf_add_buffer sf_add_buffer_reown1
#define sf_add_string sf_add_string_reown1

bool sf_add_buffer_reown1(struct string_factory * sf,      char * b,size_t bs);

static inline bool sf_add_string_reown1(struct string_factory * sf,char * s) {
	if (unlikely(!s)) return true;
	return sf_add_buffer_reown1(sf,s,strlen(s));
}

static inline bool sf_add_buffer_reown0(struct string_factory * sf,const char * b,size_t bs) {
	if (unlikely(!b)) return true;
	return sf_add_buffer_reown1(sf,mmemcpy(b,bs),bs);
}

static inline bool sf_add_string_reown0(struct string_factory * sf,const char * s) {
	if (unlikely(!s)) return true;
	return sf_add_buffer_reown0(sf,s,strlen(s));
}

#define sf_add_lstring sf_add_lstring_reown1
#define sf_add_lstring_reown0(sf, ls) sf_add_lstring_reownX(sf, ls, false)
#define sf_add_lstring_reown1(sf, ls) sf_add_lstring_reownX(sf, ls, true)
bool sf_add_lstring_reownX(struct string_factory * sf, struct lstring * lstring, bool reown);

bool sf_add_char(struct string_factory * sf, char c);

static inline bool sf_vprintf(struct string_factory * sf,const char * fmt,va_list ap) {
	size_t sl;
	char * str=vmprintf_sl(&sl,fmt,ap);
	if (!str) return false;
	return sf_add_buffer_reown1(sf,str,sl);
}

static inline bool sf_printf(struct string_factory * sf,const char * fmt,...) {
	bool retval;
	va_list ap;
	
	va_start(ap,fmt);
	retval=sf_vprintf(sf,fmt,ap);
	va_end(ap);
	return retval;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // #ifndef BTSF_H
