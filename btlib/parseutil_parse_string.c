/*
 * parseutil_parse_string.c. Part of the bothie-utils.
 *
 * Copyright (C) 2004-2015 Bodo Thiesen <bothie@gmx.de>
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

#include "parseutil_internals.h"

#include <stdlib.h>
#include <string.h>

char parse_string(
	const char * * source,const char * delimiters,
	const char * valid_first,const char * valid_other,
	char * * destination
) {
	const char * initial_source=*source;
	
	if (delimiters && strchr(delimiters,**source)) {
		pwerrno=PW_EMPTY;
		return 0;
	}
	if (valid_first) {
		if (!strchr(valid_first,pwe_invalid_char=*((*source)++))) {
			pwerrno=PW_INVAL_CHAR;
			return 0;
		}
	} else {
		(*source)++;
	}
	while (**source && (!delimiters || !strchr(delimiters,**source))) {
		if (valid_other) {
			if (!strchr(valid_other,pwe_invalid_char=*((*source)++))) {
				if (!delimiters) {
					--(*source);
					break;
				}
				pwerrno=PW_INVAL_CHAR;
				return 0;
			}
		} else {
			(*source)++;
		}
	}
	if (!(*destination=malloc(*source-initial_source+1))) {
		pwerrno=PW_ERRNO;
		return 0;
	}
	
	memcpy(*destination,initial_source,*source-initial_source);
	(*destination)[*source-initial_source]=0;
	
	pwerrno=0;
	
	char retval=**source;
	if (delimiters && **source) {
		++(*source);
	}
	return retval;
}
