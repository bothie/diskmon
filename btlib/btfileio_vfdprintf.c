/*
 * btfileio_vfdprintf.c. Part of the bothie-utils.
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

#include "btfileio.h"

#include "mprintf.h"

#include <string.h>
#include <unistd.h>

int vfdprintf(int fd,const char * fmt,va_list ap) {
	char * tmp=vmprintf(fmt,ap);
	
	if (!tmp) {
		return -1;
	}
	
	int retval=strlen(tmp);
	
	if (write(fd,tmp,retval)!=retval) {
		retval=-1;
	}
	
	free(tmp);
	
	return retval;
}
