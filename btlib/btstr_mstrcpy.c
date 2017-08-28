/*
 * btstr_mstrcpy.c. Part of the bothie-utils.
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

#include "btstr.h"

#include <string.h>

char * mstrcpy(const char * str) {
	size_t sl=strlen(str);
	char * retval = (char *)malloc(sl + 1);
	
	if (expect(retval,1)) {
		memcpy(retval,str,sl+1);
	}
	
	return retval;
}
