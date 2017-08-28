/*
 * bttime.h. Part of the bothie-utils.
 *
 * Copyright (C) 2007-2015 Bodo Thiesen <bothie@gmx.de>
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

#ifndef BTLINUXLIBRARY_BTTIME_H
#define BTLINUXLIBRARY_BTTIME_H

#ifndef NEED_LONG_LONG
#define NEED_LONG_LONG
#endif // #ifndef NEED_LONG_LONG

#include "bttypes.h"

#ifdef __cplusplus
#include <cstdbool>
#include <ctime>
#else // #ifdef __cplusplus
#include <stdbool.h>
#include <time.h>
#endif // #ifdef __cplusplus, else

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus

typedef u64 bttime_t;

#define BTTIME_MAX ((bttime_t)-1)

static inline bttime_t bttime(void) {
	struct timeval currtime;
	
	if (gettimeofday(&currtime,NULL)) {
		return -1;
	}
	
	return (bttime_t)currtime.tv_sec*(bttime_t)1000000000+(bttime_t)currtime.tv_usec*(bttime_t)1000;
}

char * mstrftime(const char * fmt,const struct tm * tm);

static inline char * mstrftime_t(const char * fmt,time_t t) {
	return mstrftime(fmt,localtime(&t));
}

static inline char * mstrfcurrentlocaltime(const char * fmt) {
	return mstrftime_t(fmt,time(NULL));
}

static inline int btnanosleep(bttime_t nanoseconds) {
	struct timespec tv;
	tv.tv_sec=nanoseconds/1000000000ULL;
	tv.tv_nsec=nanoseconds%1000000000ULL;
	if (nanosleep(&tv,&tv)) {
		return tv.tv_sec*1000000000ULL+tv.tv_nsec;
	}
	return 0;
}

#ifdef __cplusplus
} // extern "C" {
#endif // #ifdef __cplusplus

#endif // #ifndef BTLINUXLIBRARY_BTTIME_H
