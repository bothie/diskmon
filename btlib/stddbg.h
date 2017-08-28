/*
 * stddbg.h. Part of the bothie-utils.
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

#ifndef STDDBG_H
#define STDDBG_H

#ifdef __cplusplus
#include <cstdarg>
#include <cstdio>
#else // #ifdef __cplusplus
#include <stdarg.h>
#include <stdio.h>
#endif // #ifdef __cplusplus, else

#include "bttypes.h"

// #ifndef __cplusplus // variadic macros supported since C++11
// GNU syntax: 
// #define BPRINTF(fmt,args...) bprintf("%s:%s:%i: "fmt, __FILE__, __func__, __LINE__, args)
// C99 / C++11 syntax:
#define BPRINTF(fmt,...) bprintf("%s:%s:%i: " fmt, __FILE__, __func__, __LINE__, __VA_ARGS__)
// #endif // #ifndef __cplusplus

#define STDDBG_FILENO 3 /* Not a public std-descriptor */
extern FILE * stddbg;

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus

void init_stddbg();
int bprintf(const char * format,...) PRINTF_FORMAT(1,2);
int vbprintf(const char * format,va_list ap);

char set_level_character(char new_level_character);
FILE * set_level_file(FILE * new_level_file);
int   lprintf(int deltalevel,const char * format,...) PRINTF_FORMAT(2,3);
int  vlprintf(int deltalevel,const char * format,va_list ap);
int  nlprintf(               const char * format,...) PRINTF_FORMAT(1,2);
int vnlprintf(               const char * format,va_list ap);

#ifdef __cplusplus
} /* extern "C" */
#endif // #ifdef __cplusplus

#endif // #ifndef STDDBG_H
