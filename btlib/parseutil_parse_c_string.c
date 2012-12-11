/*
 * parseutil_parse_c_string.c. Part of the bothie-utils.
 *
 * Copyright (C) 2008 Bodo Thiesen <bothie@gmx.de>
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

#include <stdbool.h>
#include <stdlib.h>

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
	int step=0;
	unsigned i;
restart:
	for (i=0;(c=(*source)[i]);++i) {
		if (backslash) {
			if (c>='0' && c<='9') {
				if (++num_digits==3) {
					goto decode_digit;
				}
				continue;
			}
			backslash=false;
			if (num_digits) {
decode_digit:
				if (step) {
					c=64*(((num_digits==3)?(*source)[i-2]:'0')-'0');
					c+=8*(((num_digits==2)?(*source)[i-2]:'0')-'0');
					c+=1*(((num_digits==1)?(*source)[i-1]:'0')-'0');
				}
				num_digits=0;
			} else {
				switch (c) {
					case 'a': c='\a'; break;
					case 'b': c='\b'; break;
					case 'f': c='\f'; break;
					case 'n': c='\n'; break;
					case 'r': c='\r'; break;
					case 't': c='\t'; break;
					case 'v': c='\v'; break;
					case 0: {
						pwerrno=PW_INVAL_CHAR;
						return 0;
					}
					default: break; // nothing to do
				}
			}
		} else {
			if (c=='\\') {
				backslash=true;
				continue;
			}
			if (c=='"' || have_quote) {
				if (!have_quote && need_quote) {
					have_quote=true;
					continue;
				}
				if (step) {
					*(d++)=0;
					goto out_ok;
				} else {
					d=malloc(dl+1);
					if (!d) {
						pwerrno=PW_ERRNO;
						return 0;
					}
					have_quote=false;
					++step;
					goto restart;
				}
			}
		}
		if (step) {
			*(d++)=c;
		} else {
			++dl;
		}
	}
	if (need_quote && !have_quote) {
		pwerrno=PW_EOF;
		if (d) {
			free(d);
		}
	} else {
out_ok:
		(*source)+=i;
		pwerrno=0;
		*destination=d-(dl+1);
		if (destination_length) {
			*destination_length=dl;
		}
	}
	return c;
}
