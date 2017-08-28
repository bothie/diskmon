/*
 * avl_mk_tree_rc.c. Part of the bothie-utils.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "avl_private.h"

#include "btmacros.h"

#include <errno.h>

struct avl_node * default_avl_node_new(void * user, object_t * entry) {
	ignore(user);
	ignore(entry);
	/*
	return malloc(sizeof(struct avl_node));
	*/
	struct avl_node * retval;
	retval = malloc(sizeof(*retval));
	return retval;
}

void default_avl_node_free(void * user, object_t * entry, struct avl_node * node) {
	ignore(user);
	ignore(entry);
	free(node);
}

struct avl_tree * avl_mk_tree_rc(struct refcounter * rc_type) {
	struct obj_type * object_type = (struct obj_type *)rc_get_object(rc_type);
	
	if (!obj_has_compare_method(object_type)) {
		errno=EINVAL;
		return NULL;
	}
	
	if (obj_has_avl_node_new_method(object_type)
	!=  obj_has_avl_node_free_method(object_type)) {
		errno=EINVAL;
		return NULL;
	}
	
	if (!obj_has_avl_node_new_method(object_type)) {
		obj_set_avl_node_memory_management(object_type, NULL, default_avl_node_new, default_avl_node_free);
	}
	
	return avl_mk_tree_zero(rc_type);
}
