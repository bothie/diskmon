/*
 * bttime_mstrftime.c. Part of the bothie-utils.
 *
 * Copyright (C) 2007 Bodo Thiesen <bothie@gmx.de>
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

#include "bttime.h"

#include <string.h>

char * mstrftime(const char * fmt,const struct tm * tm) {
	size_t max=0;
	char * str=NULL;
	
	/*
	 * strftime may return 0 in some locales e.g. on %p. So, we 
	 * add a %m to force some output which we will later remove
	 */
	size_t sl=strlen(fmt);
	char * format=(char*)malloc(sl+3);
	if (!format) return NULL;
	memcpy(format,fmt,sl);
	memcpy(format+sl,"%m",3);
	
	while (!max || !(sl=strftime(str,max,format,tm)) || sl==max) {
		max=1+max*2;
		char * prev_str=str;
		str=(char*)realloc(str,max);
		if (unlikely(!str)) {
			free(prev_str);
			free(format);
			return NULL;
		}
	}
	
	strftime(str,max,fmt,tm);
	
	free(format);
	
	return str;
}
