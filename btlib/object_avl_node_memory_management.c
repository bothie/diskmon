/*
 * object_avl_node_memory_management.c. Part of the bothie-utils.
 *
 * Copyright (C) 2005-2014 Bodo Thiesen <bothie@gmx.de>
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

void obj_set_avl_node_memory_management(struct obj_type * type
	, void * user
	, struct avl_node * (*avl_node_new_func)(void *, object_t *)
	, void (*avl_node_free_func)(void *, object_t *, struct avl_node *)
) {
	type->avl_node_user = user;
	
	obj_set_avl_node_new_method(type, avl_node_new_func);
	obj_set_avl_node_free_method(type, avl_node_free_func);
}

void * obj_get_avl_node_user_value(struct obj_type * type) {
	return type->avl_node_user;
}
