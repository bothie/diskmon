/*
 * parseutil_parse_c_string.c. Part of the bothie-utils.
 *
 * Copyright (C) 2008-2015 Bodo Thiesen <bothie@gmx.de>
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

#include "parseutil.h"

#include "bterror.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

bool parse_c_string_debug=false;
char parse_c_string(
	const char * * source,
	bool need_quote,
	char * * destination,size_t * destination_length
) {
//	bool quote=false;
	bool backslash=false;
	bool have_quote=false;
	unsigned num_digits=0;
	size_t dl=0;
	char c;
	char * d=NULL;
	unsigned i;
	
	if (parse_c_string_debug) eprintf(
		">>> parse_c_string(*source=[%s], need_quote=%s, destination=%p, destination_length=%p)\n"
		,*source
		,bool_str(need_quote)
		,(void*)destination
		,(void*)destination_length
	);
	for (;;) {
		for (i=0;(c=(*source)[i]);++i) {
			if (backslash) {
				bool new_digit;
				if (c>='0' && c<='9') {
					if (++num_digits<3) {
						continue;
					}
					new_digit=true;
				} else {
					new_digit=false;
				}
				if (num_digits) {
					if (!new_digit || num_digits==3) {
						backslash=false;
						if (d) {
							c=64*(((num_digits==3)?(*source)[i+new_digit-3]:'0')-'0');
							c+=8*(((num_digits==2)?(*source)[i+new_digit-2]:'0')-'0');
							c+=1*(((num_digits==1)?(*source)[i+new_digit-1]:'0')-'0');
						}
						num_digits=0;
						if (!new_digit) {
							--i;
						}
					}
				} else {
					backslash=false;
					switch (c) {
						case 'a': c='\a'; break;
						case 'b': c='\b'; break;
						case 'f': c='\f'; break;
						case 'n': c='\n'; break;
						case 'r': c='\r'; break;
						case 't': c='\t'; break;
						case 'v': c='\v'; break;
						case '\\': break;
						case '"': break;
						default: {
							pwerrno=PW_INVAL_CHAR;
							pwe_invalid_char=c;
							return 0;
						}
					}
				}
			} else {
				if (c=='"') {
					if (have_quote) {
						break;
					}
					if (i) {
						pwerrno=PW_LATE_QUOTE;
						return 0;
					}
					have_quote=true;
					if (!need_quote) {
						need_quote=true;
					}
					continue;
				}
				if (need_quote && !have_quote) {
					pwerrno=PW_NEED_QUOTE;
					pwe_invalid_char=c;
					return 0;
				}
				if (c=='\\') {
					backslash=true;
					continue;
				}
			}
			if (d) {
				*(d++)=c;
			} else {
				++dl;
			}
		}
		if (parse_c_string_debug) eprintf(
			"=== dl=%u, d=%p ([%s])\n"
			, (unsigned)dl
			,(void*)d
			,d
		);
		if (d) {
//			*(d++)=0;
			assert(*destination == (d-dl));
			if (parse_c_string_debug) eprintf("=== parse_c_string: returning [%s]\n",*destination);
			break;
		}
		if (parse_c_string_debug) eprintf("=== Check for have_quote && c!='\"' ... ");
		if (have_quote && c!='"') {
			if (parse_c_string_debug) eprintf("failed\n");
			pwerrno=PW_INVAL_CHAR;
			return 0;
		}
		if (parse_c_string_debug) eprintf("passed ;-)\n");
		if (parse_c_string_debug) eprintf("=== Check for need_quote && !have_quote ... ");
		if (need_quote && !have_quote) {
			if (parse_c_string_debug) eprintf("failed (");
			assert(!(*source)[0]);
			pwerrno=PW_NEED_QUOTE;
			pwe_invalid_char=c;
			if (parse_c_string_debug) eprintf("PW_NEED_QUOTE)\n");
			return 0;
		}
		if (parse_c_string_debug) eprintf("passed ;-)\n");
		have_quote=false;
		if (!destination) {
			if (parse_c_string_debug) eprintf("=== destination==NULL => skipping pass 2\n");
			break;
		}
		d=malloc(dl+1);
		if (!d) {
			if (parse_c_string_debug) eprintf(
				"=== malloc(%u) failed\n"
				, (unsigned)dl
			);
			pwerrno=PW_ERRNO;
			return 0;
		}
		d[dl]=0;
		*destination=d;
	}
	(*source)+=i+(c=='"');
	pwerrno=PW_NOERROR;
	if (destination_length) {
		*destination_length=dl;
	}
	if (parse_c_string_debug) eprintf(
		"<<< parse_c_string(*source=[%s], need_quote=%s, *destination=%p [%s], *destination_length=%u)\n"
		,*source
		,bool_str(need_quote)
		,(void*)(destination?*destination:NULL)
		,destination?*destination:NULL
		, destination_length ? (unsigned)*destination_length : 0
	);
	return **source;
}
