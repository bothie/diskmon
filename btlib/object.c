/*
 * object.c. Part of the bothie-utils.
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

#include "object_private.h"

#include "btmacros.h"
#include "btstr.h"
#include "refcounter.h"
#include "stddbg.h"

#include <assert.h>
#include <string.h>

#define MEMBER_FUNCTIONS_1(rettype,funcname,type1) \
	bool obj_has_ ## funcname ## _method(const struct obj_type * type) { \
		return !!type->funcname; \
	} \
	\
	rettype (*obj_set_ ## funcname ## _method(struct obj_type * type,rettype (*funcname)(type1)))(type1) { \
		rettype (*retval)(type1); \
		retval=type->funcname; \
		type->funcname=funcname; \
		return retval; \
	} \
	\
	rettype obj_call_ ## funcname(const struct obj_type * type,type1 v) { \
		return type->funcname(v); \
	} \
	\
	rettype (*obj_get_ ## funcname ## _method(const struct obj_type * type))(type1) { \
		return type->funcname; \
	} \

#define MEMBER_PROCEDURE_1(rettype,funcname,type1) \
	bool obj_has_ ## funcname ## _method(const struct obj_type * type) { \
		return !!type->funcname; \
	} \
	\
	rettype (*obj_set_ ## funcname ## _method(struct obj_type * type,rettype (*funcname)(type1)))(type1) { \
		rettype (*retval)(type1); \
		retval=type->funcname; \
		type->funcname=funcname; \
		return retval; \
	} \
	\
	rettype obj_call_ ## funcname(const struct obj_type * type, type1 v1) { \
		type->funcname(v1); \
	} \
	\
	rettype (*obj_get_ ## funcname ## _method(const struct obj_type * type))(type1) { \
		return type->funcname; \
	} \

#define MEMBER_FUNCTIONS_2(rettype,funcname,type1,type2) \
	bool obj_has_ ## funcname ## _method(const struct obj_type * type) { \
		return !!type->funcname; \
	} \
	\
	rettype (*obj_set_ ## funcname ## _method(struct obj_type * type,rettype (*funcname)(type1,type2)))(type1,type2) { \
		rettype (*retval)(type1,type2); \
		retval=type->funcname; \
		type->funcname=funcname; \
		return retval; \
	} \
	\
	rettype obj_call_ ## funcname(const struct obj_type * type,type1 v1,type2 v2) { \
		return type->funcname(v1,v2); \
	} \
	\
	rettype (*obj_get_ ## funcname ## _method(const struct obj_type * type))(type1, type2) { \
		return type->funcname; \
	} \

#define MEMBER_FUNCTIONS_3(rettype, funcname, type1, type2, type3) \
	bool obj_has_ ## funcname ## _method(const struct obj_type * type) { \
		return !!type->funcname; \
	} \
	\
	rettype (*obj_set_ ## funcname ## _method(struct obj_type * type,rettype (*funcname)(type1, type2, type3)))(type1, type2, type3) { \
		rettype (*retval)(type1, type2, type3); \
		retval = type->funcname; \
		type->funcname = funcname; \
		return retval; \
	} \
	\
	rettype obj_call_ ## funcname(const struct obj_type * type, type1 v1, type2 v2, type3 v3) { \
		return type->funcname(v1, v2, v3); \
	} \
	\
	rettype (*obj_get_ ## funcname ## _method(const struct obj_type * type))(type1, type2, type3) { \
		return type->funcname; \
	} \

#define MEMBER_PROCEDURE_3(rettype, funcname, type1, type2, type3) \
	bool obj_has_ ## funcname ## _method(const struct obj_type * type) { \
		return !!type->funcname; \
	} \
	\
	rettype (*obj_set_ ## funcname ## _method(struct obj_type * type, rettype (*funcname)(type1, type2, type3)))(type1, type2, type3) { \
		rettype (*retval)(type1, type2, type3); \
		retval = type->funcname; \
		type->funcname = funcname; \
		return retval; \
	} \
	\
	rettype obj_call_ ## funcname(const struct obj_type * type, type1 v1, type2 v2, type3 v3) { \
		type->funcname(v1, v2, v3); \
	} \
	\
	rettype (*obj_get_ ## funcname ## _method(const struct obj_type * type))(type1, type2, type3) { \
		return type->funcname; \
	} \

#include "object.mf"

static int obj_type_compare(const void * v1,const void * v2) {
	const struct obj_type * ot1=v1;
	const struct obj_type * ot2=v2;
	
	return strcmp(ot1->name,ot2->name);
}

static void obj_type_free(void * v) {
	struct obj_type * ot = v;
	
	free(ot->name);
	free(ot);
}

struct obj_type * obj_refcount_type;

static struct obj_type * do_obj_mk_type(
	const char * tname,
	bool do_refcounting,
	int (*obj_compare)(const object_t * entry1,const object_t * entry2),
	void (*obj_free_func)(object_t *)
) {
	struct obj_type * retval;
	bool making_rc_obj_type;
	
//	eprintf("--> obj_mk_type_2 [initial]\n");
	
start_over:
//	eprintf("--- obj_mk_type_2 [beginning work]\n");
	
	making_rc_obj_type=!obj_refcount_type;
	
	NEWRETVAL();
	
#define MEMBER_FUNCTIONS_1(rettype,funcname,type1) retval->funcname=NULL;
#define MEMBER_FUNCTIONS_2(rettype,funcname,type1,type2) retval->funcname=NULL;
#define MEMBER_FUNCTIONS_3(rettype,funcname,type1,type2,type3) retval->funcname=NULL;
#include "object.mf"
	
	retval->avl_node_user = NULL;
	
//	bprintf("Just initialized method pointers of %p\n",retval);
	if (unlikely(making_rc_obj_type)) {
//		eprintf("--- obj_mk_type_2 [obj_type]\n");
		retval->name=concat(1,"obj_type");
		retval->compare=obj_type_compare;
		retval->free=obj_type_free;
		retval->do_refcounting = true;
		obj_refcount_type=retval;
	} else {
//		eprintf("--- obj_mk_type_2 [\"%s\"]\n",tname);
		retval->name=concat(1,tname);
		retval->compare = obj_compare;
		retval->free = obj_free_func;
		retval->do_refcounting = do_refcounting;
	}
	
	if (!retval->name) {
//		eprintf("<-- obj_mk_type_2 [NULL(%i)]\n",__LINE__);
		free(retval);
		if (unlikely(making_rc_obj_type)) {
			obj_refcount_type=NULL;
		}
		return NULL;
	}
	
//	eprintf("--- obj_mk_type_2 [rc_get_refcounter_for]\n",__LINE__);
	struct refcounter * rc=rc_get_refcounter_for(obj_refcount_type,retval);
	if (!rc) {
//		eprintf("<-- obj_mk_type_2 [NULL(%i)]\n",__LINE__);
		if (unlikely(making_rc_obj_type)) {
			eprintf(
				"%s:%s:%i: Can't proceed after rc_get_refcounter_for failed in obj_mk_type_2 while initializing obj_refcount_type\n"
				, __FILE__, __func__, __LINE__
			);
			assert(never_reached);
		}
		free(retval->name);
		free(retval);
		return NULL;
	}
//	eprintf("--- obj_mk_type_2 [rc_inc_refcount]\n",__LINE__);
	rc_inc_refcount(rc);
	
	if (unlikely(making_rc_obj_type)) {
		static struct refcounter * valgrind_rc;
		valgrind_rc = rc;
		ignore(valgrind_rc);
//		eprintf("--- obj_mk_type_2 [starting over]\n",__LINE__);
		goto start_over;
	}
	
//	eprintf("<-- obj_mk_type_2 [successfully quitting]\n",__LINE__);
	return retval;
}

struct obj_type * obj_mk_type_3(const char * tname
	, int (*compare)(const object_t * entry1, const object_t * entry2)
) {
	return do_obj_mk_type(tname, false, compare, NULL);
}

struct obj_type * obj_mk_type_2(const char * tname
	, int (*compare)(const object_t * entry1, const object_t * entry2)
	, void (*free)(object_t *)
) {
	return do_obj_mk_type(tname, true, compare, free);
}

struct obj_type * obj_mk_type_1(const char * tname
	, int (*compare)(const object_t * entry1, const object_t * entry2)
) {
	return do_obj_mk_type(tname, true, compare, NULL);
}

void obj_free(struct obj_type * type) {
	rc_dec_refcount(rc_get_refcounter_for(obj_refcount_type, type));
}

const char * obj_name(struct obj_type * type) {
	return type->name;
}

bool obj_has_refcounter(struct obj_type * type) {
	return type->do_refcounting;
}
