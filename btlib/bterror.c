/*
 * bterror.c. Part of the bothie-utils.
 *
 * Copyright (C) 2002-2004 Bodo Thiesen <bothie@gmx.de>
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

#include "bterror.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int eprintf(const char * format,...) {
	int RC;
	
	va_list ap;
	va_start(ap,format);
	RC=vfprintf(stderr,format,ap);
	va_end(ap);
	
	return RC;
}

int veprintf(const char * format,va_list ap) {
	return vfprintf(stderr,format,ap);
}

void error(int errnr,const char * format,...) {
	va_list ap;
	va_start(ap,format);
	vfprintf(stderr,format,ap);
	va_end(ap);
	errno=errnr;
	eprintf(": %s\n",strerror(errno));
	exit(errnr);
}

void fatal(const char * format,...) {
	va_list ap;
	va_start(ap,format);
	vfprintf(stderr,format,ap);
	va_end(ap);
	exit(EFAULT);
}

int warning(const char * format,...) {
	int RC;
	
	va_list ap;
	va_start(ap,format);
	RC=vfprintf(stderr,format,ap);
	va_end(ap);
	
	return RC;
}

int info(const char * format,...) {
	int RC;
	
	va_list ap;
	va_start(ap,format);
	RC=vfprintf(stdout,format,ap);
	va_end(ap);
	
	return RC;
}

void eexit(const char * format,...) {
	int e=errno;
	va_list ap;
	va_start(ap,format);
	vfprintf(stderr,format,ap);
	va_end(ap);
	eprintf(": %s\n",strerror(e));
	exit(e);
}

void perrorf(const char * format,...) {
	int e=errno;
	va_list ap;
	va_start(ap,format);
	vfprintf(stderr,format,ap);
	va_end(ap);
	eprintf(": %s\n",strerror(e));
	errno=e;
}
