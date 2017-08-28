/*
 * refcounter.h. Part of the bothie-utils.
 *
 * Copyright (C) 2005-2015 Bodo Thiesen <bothie@gmx.de>
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

#ifndef BTLINUXLIBRARY_REFCOUNTER_H
#define BTLINUXLIBRARY_REFCOUNTER_H

#ifdef __cplusplus
#include <cstdbool>
#else // #ifdef __cplusplus
#include <stdbool.h>
#endif // #ifdef __cplusplus, else

#include "object_t.h"

struct refcounter;
struct obj_type;

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus

struct refcounter * rc_inc_refcount(struct refcounter * rc);
bool rc_dec_refcount(struct refcounter * rc);
object_t * rc_get_object(const struct refcounter * rc);
struct obj_type * rc_get_type(const struct refcounter * rc);
struct refcounter * rc_get_refcounter_for(struct obj_type * type, const object_t * data);

#ifdef __cplusplus
} // extern "C" {
#endif // #ifdef __cplusplus

#endif // #ifndef BTLINUXLIBRARY_REFCOUNTER_H
