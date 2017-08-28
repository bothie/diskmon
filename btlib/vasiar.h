/*
 * vasiar.h. Part of the bothie-utils.
 *
 * Copyright (C) 2006-2016 Bodo Thiesen <bothie@gmx.de>
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

#ifndef VASIAR_H
#define VASIAR_H

#include "btmacros.h"

#ifdef __cplusplus
#include <cstdlib>
#else // #ifdef __cplusplus
#include <stdlib.h>
#endif // #ifdef __cplusplus, else

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus

/*@temp@*/ void * vasiar_access(
	void * * addr,
	size_t * num,
	size_t * max,
	size_t size,
	size_t idx
);

size_t vasiar_access2(
	void * * addr,
	size_t * num,
	size_t * max,
	size_t size,
	size_t idx
);

// This structs should actually be private. But to allow the macros to access 
// the entries, they must be publically known. This structs are subject to 
// change without any prior warning. Don't access them directly if you want 
// your code to compile in future.
#define RAW_VASIAR(type) { \
	union { \
		type * vartype; \
		void * nontype; \
	} addr; \
	size_t num; \
	size_t max; \
}
#define VASIAR(type) struct RAW_VASIAR(type)
#define VASIAR_CPP(type,typename) struct vasiar_##typename RAW_VASIAR(type)

static inline /*@temp@*/ void * fast_vasiar_access(
	void * * addr,
	size_t * num,
	size_t * max,
	size_t size,
	size_t idx
) {
	if (likely(idx<*max)) {
		if (unlikely(idx>=*num)) {
			*num=idx+1;
		}
		
		return ((char*)*addr+size*idx);
	}
	
	return vasiar_access(addr,num,max,size,idx);
}

#define VAACCESS(_var, idx) (*braced_group( \
	__auto_type UNIQUE(vaaccess_var) = &(_var); \
	 \
	(typeof(UNIQUE(vaaccess_var)->addr.vartype)) \
	fast_vasiar_access( \
		&UNIQUE(vaaccess_var)->addr.nontype, \
		&UNIQUE(vaaccess_var)->num, \
		&UNIQUE(vaaccess_var)->max, \
		sizeof(*UNIQUE(vaaccess_var)->addr.vartype), \
		(idx) \
	); \
))

void dump_stack_trace();

static inline /*@temp@*/ void * fast_const_vasiar_access(
	const void * addr,
	size_t num,
	size_t size,
	size_t idx
) {
	if (likely(idx < num)) {
		return ((char*)addr + size * idx);
	}
	
	eprintf(
		"%s:%s:%i (libBtLinuxLibrary): Trying to access const vasiar beyond end.\n"
		, __FILE__, __func__, __LINE__
	);
	dump_stack_trace();
	return NULL;
}

#define CVAACCESS(_var, idx) (*braced_group( \
	__auto_type UNIQUE(cvaaccess_var) = &(_var); \
	 \
	(typeof(UNIQUE(cvaaccess_var)->addr.vartype)) \
	fast_const_vasiar_access( \
		UNIQUE(cvaaccess_var)->addr.nontype, \
		UNIQUE(cvaaccess_var)->num, \
		sizeof(*UNIQUE(cvaaccess_var)->addr.vartype), \
		(idx) \
	); \
))

#define VASIZE(var) ((const size_t)(var).num)
#define VAADDR(var) ((var).addr.vartype)

#define VAADDR_REOWN(_var) braced_group( \
	__auto_type UNIQUE(vaaddr_reown_var) = &(_var); \
	__auto_type UNIQUE(vaaddr_reown_retval) = UNIQUE(vaaddr_reown_var)->addr.vartype; \
	 \
	if (likely(UNIQUE(vaaddr_reown_var)->num != UNIQUE(vaaddr_reown_var)->max)) { \
		void * UNIQUE(vaaddr_reown_tmp) = realloc(UNIQUE(vaaddr_reown_retval), UNIQUE(vaaddr_reown_var)->num * sizeof(*UNIQUE(vaaddr_reown_var)->addr.vartype)); \
		 \
		if (likely(UNIQUE(vaaddr_reown_tmp))) { \
			UNIQUE(vaaddr_reown_retval) = UNIQUE(vaaddr_reown_tmp); \
		} \
	} \
	 \
	VAINIT(*UNIQUE(vaaddr_reown_var)); \
	 \
	UNIQUE(vaaddr_reown_retval); \
)

#define VATRUNCATE(_var, _size) do { size_t UNIQUE(ns) = (_size); __auto_type UNIQUE(var) = &(_var); if (UNIQUE(ns) < UNIQUE(var)->num) UNIQUE(var)->num = UNIQUE(ns); } while (0)
#define VANEW(_var) (*braced_group( __auto_type UNIQUE(var) = &(_var); &VAACCESS(*UNIQUE(var), VASIZE(*UNIQUE(var))); ))
#define VALAST(_var) (*braced_group( __auto_type UNIQUE(var) = &(_var); &VAACCESS(*UNIQUE(var), VASIZE(*UNIQUE(var)) - 1); ))

#define VAINIT(_var) do { __auto_type UNIQUE(var) = &(_var); UNIQUE(var)->addr.nontype = NULL; UNIQUE(var)->num = 0; UNIQUE(var)->max = 0; } while (0)
#define VAFREE(_var) do { __auto_type UNIQUE(vafree_var) = &(_var); free(UNIQUE(vafree_var)->addr.nontype); VAINIT(*UNIQUE(vafree_var)); } while (0)
#define VARESET(var) do { (var).num=0; } while (0)

typedef VASIAR(char *) vasiar_char_ptr;

#ifdef __cplusplus
} // extern "C"
#endif // #ifdef __cplusplus

#endif // #ifndef VASIAR_H
