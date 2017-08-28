/*
 * bterror.h. Part of the bothie-utils.
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

#ifndef BTERROR_H
#define BTERROR_H

#include "btmacros.h"

#ifdef __cplusplus

#include <cstdarg>
extern "C" {

#else /* #ifdef __cplusplus */

#include <stdarg.h>

#endif /* #ifdef __cplusplus, else */

int  /*@alt void@*/ eprintf (          const char * format,...) PRINTF_FORMAT(1,2);
int  /*@alt void@*/ veprintf(          const char * format,va_list ap) PRINTF_FORMAT(1,0);
void                error   (int errnr,const char * format,...) PRINTF_FORMAT(2,3);
void                fatal   (          const char * format,...) PRINTF_FORMAT(1,2);
int  /*@alt void@*/ warning (          const char * format,...) PRINTF_FORMAT(1,2);
int  /*@alt void@*/ info    (          const char * format,...) PRINTF_FORMAT(1,2);
void                eexit   (          const char * format,...) PRINTF_FORMAT(1,2);
void                perrorf (          const char * format,...) PRINTF_FORMAT(1,2);

#ifdef __cplusplus
} /* extern "C" { */
#endif /* #ifdef __cplusplus */

#endif /* #ifndef BTERROR_H */
