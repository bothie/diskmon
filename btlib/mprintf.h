/*
 * mprintf.h. Part of the bothie-utils.
 *
 * Copyright (C) 2002-2015 Bodo Thiesen <bothie@gmx.de>
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

#ifndef MPRINTF_H
#define MPRINTF_H

#include "btmacros.h"

#ifdef __cplusplus

#include <cstdarg>
#include <cstdlib>

extern "C" {

#else /* #ifdef __cplusplus */

#include <stdarg.h>
#include <stdlib.h>

#endif /* #ifdef __cplusplus, else */

char * vmprintf_sl(/*@out@*/ size_t * sl,const char * fmt,va_list ap) PRINTF_FORMAT(2,0);
char * mprintf_sl(/*@out@*/ size_t * sl,const char * fmt,...) PRINTF_FORMAT(2,3);
char * mprintf(const char * fmt,...) PRINTF_FORMAT(1,2);

static inline char * vmprintf(const char * fmt,va_list ap) PRINTF_FORMAT(1,0);
static inline char * vmprintf(const char * fmt,va_list ap) {
	size_t sl;
	return vmprintf_sl(&sl,fmt,ap);
}

/* FreeBSD: */
/*
#define asprintf  mprintf
#define vasprintf vmprintf
*/

#ifdef __cplusplus
} /* extern "C" { */
#endif /* #ifdef __cplusplus */

#endif /* #ifndef MPRINTF_H */
