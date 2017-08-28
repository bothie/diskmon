/*
 * object.h. Part of the bothie-utils.
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

#ifndef BTLINUXLIBRARY_OBJECT_H
#define BTLINUXLIBRARY_OBJECT_H

#ifdef __cplusplus
#include <cstdbool>
#else // #ifdef __cplusplus
#include <stdbool.h>
#endif // #ifdef __cplusplus, else

#include "object_t.h"

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus

struct obj_type;

extern struct obj_type * obj_refcount_type;

/*
 * obj_mk_type_1, obj_mk_type_2, obj_mk_type_3
 * 
 * Create a new object type.
 * 
 * char * tname - a human readable name for the object type.
 * func compare - the compare function which orders differing objects in any
 *                form of greater/lesser relation which doesn't need to mean
 *                anything but must produce a total ordering.
 * func free    - the free function which is used on the object once the
 *                refcounter reaches zero.
 * 
 * When using obj_mk_type_1, the stdlib function free will be used instead of 
 * a user defined free function. It can later be overridden by the function 
 * obj_set_free_method.
 * When using obj_mk_type_3, there will be no refcounting be performed on
 * this object type. Consequently, the free method won't be used and 
 * doesn't need to be specified upon object type creation.
 */
struct obj_type * obj_mk_type_1(const char * tname
	, int (*compare)(const object_t * entry1, const object_t * entry2)
);
struct obj_type * obj_mk_type_2(const char * tname
	, int (*compare)(const object_t * entry1, const object_t * entry2)
	, void (*free)(object_t *)
);
struct obj_type * obj_mk_type_3(const char * tname
	, int (*compare)(const object_t * entry1, const object_t * entry2)
);

void obj_free(struct obj_type * type);
const char * obj_name(struct obj_type * type);
bool obj_has_refcounter(struct obj_type * type);

/*
 * Here we declare our callback stuff. For each method (look in object.mf for 
 * what methods there are) there are three functions:
 *
 * bool obj_has_xxx(const struct obj_type * type);
 * rettype (*obj_set_xxx_method(const struct obj_type * type,rettype (*new)(arguments)))(arguments);
 * rettype obj_call_xxx(const struct obj_type * type,arguments);
 *
 * obj_set_xxx_method sets a new function pointer and returns the old one (just 
 * for the case s.b. wants to build a chain). obj_call_xxx calls the function 
 * with the given arguments. obj_has_xxx checks wether ther is actually a 
 * function set.
 *
 * Currently defined are: compare, free, display and print. For more details, 
 * understand the following lines ^^
 */
#define DECLARE_FUNCTIONS_1(rettype,funcname,type1) \
	bool obj_has_ ## funcname ## _method(const struct obj_type * type); \
	rettype (*obj_set_ ## funcname ## _method(struct obj_type * type,rettype (*funcname)(type1)))(type1); \
	rettype obj_call_ ## funcname(const struct obj_type * type,type1); \
	rettype (*obj_get_ ## funcname ## _method(const struct obj_type * type))(type1); \

#define DECLARE_FUNCTIONS_2(rettype,funcname,type1,type2) \
	bool obj_has_ ## funcname ## _method(const struct obj_type * type); \
	rettype (*obj_set_ ## funcname ## _method(struct obj_type * type,rettype (*funcname)(type1,type2)))(type1,type2); \
	rettype obj_call_ ## funcname(const struct obj_type * type,type1,type2); \
	rettype (*obj_get_ ## funcname ## _method(const struct obj_type * type))(type1,type2); \

#define DECLARE_FUNCTIONS_3(rettype,funcname,type1,type2,type3) \
	bool obj_has_ ## funcname ## _method(const struct obj_type * type); \
	rettype (*obj_set_ ## funcname ## _method(struct obj_type * type,rettype (*funcname)(type1,type2,type3)))(type1,type2,type3); \
	rettype obj_call_ ## funcname(const struct obj_type * type,type1,type2,type3); \
	rettype (*obj_get_ ## funcname ## _method(const struct obj_type * type))(type1,type2,type3); \

#define MEMBER_FUNCTIONS_1 DECLARE_FUNCTIONS_1
#define MEMBER_FUNCTIONS_2 DECLARE_FUNCTIONS_2
#define MEMBER_FUNCTIONS_3 DECLARE_FUNCTIONS_3

#include "object.mf"

// obj_set_avl_node_memory_management declared in avl.h

#ifdef __cplusplus
} // extern "C" {
#endif // #ifdef __cplusplus

#endif // #ifndef BTLINUXLIBRARY_OBJECT_H
