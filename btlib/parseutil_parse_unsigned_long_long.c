/*
 * parseutil_parse_unsigned_long_long.c. Part of the bothie-utils.
 *
 * Copyright (C) 2009 Bodo Thiesen <bothie@gmx.de>
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

#include "btmacros.h"

#include <assert.h>
#include <errno.h>

char parse_unsigned_long_long(const char * * p,unsigned long long * target) {
	int base=0;
	const char * initial_p=*p;
	
	if (**p<'0' || **p>'9') {
		pwerrno=PW_INVAL_CHAR;
		return 0;
	}
	
	if (**p=='0') {
		if (*++(*p)=='x') {
			base=16;
			++(*p);
		} else {
			--(*p);
			base=8;
		}
	} else {
		base=10;
	}
	
	if (!is_digit(base,**p)) {
		*p=initial_p;
		pwerrno=PW_INVAL_CHAR;
		return 0;
	}
	
	*target=0;
	
	while (true) {
		if (!is_digit(base,**p)) {
			pwerrno=0;
			return **p;
		}
		unsigned long long prevtarget=*target;
		if (**p>='0' && **p<='9') {
			*target=*target*base+*((*p)++)-'0';
		} else {
			switch (*((*p)++)) {
				case 'a': case 'A': *target=*target*base+10; break;
				case 'b': case 'B': *target=*target*base+11; break;
				case 'c': case 'C': *target=*target*base+12; break;
				case 'd': case 'D': *target=*target*base+13; break;
				case 'e': case 'E': *target=*target*base+14; break;
				case 'f': case 'F': *target=*target*base+15; break;
				default: assert(never_reached);
			}
		}
		
		if (prevtarget>*target) {
			*p=initial_p;
			pwerrno=PW_ERRNO;
			errno=ERANGE;
			return 0;
		}
	}
}
