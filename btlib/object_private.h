/*
 * object_private.h. Part of the bothie-utils.
 *
 * Copyright (C) 2014-2014 Bodo Thiesen <bothie@gmx.de>
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

#ifdef BTLINUXLIBRARY_OBJECT_H
#error Never include object.h prior to object_private.h
#endif // #ifdef BTLINUXLIBRARY_OBJECT_H

#ifndef BTLINUXLIBRARY_OBJECT_PRIVATE_H
#define BTLINUXLIBRARY_OBJECT_PRIVATE_H

#include "object.h"

#define MEMBER_FUNCTIONS_1(rettype, funcname, type1) rettype (*funcname)(type1);
#define MEMBER_FUNCTIONS_2(rettype, funcname, type1, type2) rettype (*funcname)(type1, type2);
#define MEMBER_FUNCTIONS_3(rettype, funcname, type1, type2, type3) rettype (*funcname)(type1, type2, type3);

struct obj_type {
	char * name;
	
#include "object.mf"
	
	void * avl_node_user;
	bool do_refcounting;
};

#endif // #ifndef BTLINUXLIBRARY_OBJECT_PRIVATE_H
