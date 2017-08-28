/*
 * avl.c. Part of the bothie-utils.
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

#include "avl_private.h"

static object_t * avl_get_data_rc0(union entry entry) {
	return entry.obj;
}

static object_t * avl_get_data_rc1(union entry entry) {
	return entry.rc ? rc_get_object(entry.rc) : NULL;
}

static union entry avl_get_entry_for_rc0(const struct avl_tree * tree, object_t * obj) {
	ignore(tree);
	
	union entry retval;
	
	retval.obj = obj;
	
	return retval;
}

static union entry avl_get_entry_for_rc1(const struct avl_tree * tree, object_t * obj) {
	union entry retval;
	
	retval.rc = obj ? rc_get_refcounter_for(avl_get_obj_type(tree), obj) : NULL;
	
	return retval;
}

static bool avl_get_entry_for_succeeded_rc0(const struct avl_tree * tree, object_t * obj, union entry * entry) {
	ignore(tree);
	
	return (entry->obj = obj);
}

static bool avl_get_entry_for_succeeded_rc1(const struct avl_tree * tree, object_t * obj, union entry * entry) {
	return (entry->rc = obj ? rc_get_refcounter_for(avl_get_obj_type(tree), obj) : NULL);
}

static void inc_refcount_rc0(union entry entry) {
	ignore(entry);
	
	/* nothing to do */
}

static void inc_refcount_rc1(union entry entry) {
	rc_inc_refcount(entry.rc);
}

static void dec_refcount_rc0(union entry entry) {
	ignore(entry);
	
	/* nothing to do */
}

static void dec_refcount_rc1(union entry entry) {
	rc_dec_refcount(entry.rc);
}

/*
 * the first call to avl_InitMembers (from avl_mk_tree_zero from 
 * rc_get_refcounter_for from do_obj_mk_type) creates the refcounters 
 * avl_tree, which - at this state - will be given an illegal rc_type with 
 * type == NULL, which would make obj_has_refcounter segfault. Since we 
 * know that first call to be from the rc code, we just initialize the tree 
 * for a refcounting type and omit the call to obj_has_refcounter.
 */
static bool first = true;
void avl_InitMembers(struct avl_tree * tree, struct refcounter * rc_type) {
	tree->root = NULL;
	tree->num_elements = 0;
	if (rc_type) {
		tree->type = rc_inc_refcount(rc_type);
		
		struct obj_type * object_type = avl_get_obj_type(tree);
		
		for (int balance = -2; balance <= +2; ++balance) {
			tree->tree_balance[balance + 2].tree = tree;
			tree->tree_balance[balance + 2].balance = balance;
		}
		
		if (first || obj_has_refcounter(object_type)) {
			tree->get_data = avl_get_data_rc1;
			tree->get_entry_for = avl_get_entry_for_rc1;
			tree->get_entry_for_succeeded = avl_get_entry_for_succeeded_rc1;
			tree->inc_refcount = inc_refcount_rc1;
			tree->dec_refcount = dec_refcount_rc1;
			tree->avl_node_new = default_avl_node_new;
			tree->avl_node_free = default_avl_node_free;
			tree->avl_node_user = NULL;
			first = false;
		} else {
			tree->get_data = avl_get_data_rc0;
			tree->get_entry_for = avl_get_entry_for_rc0;
			tree->get_entry_for_succeeded = avl_get_entry_for_succeeded_rc0;
			tree->inc_refcount = inc_refcount_rc0;
			tree->dec_refcount = dec_refcount_rc0;
			tree->avl_node_new = obj_get_avl_node_new_method(object_type);
			tree->avl_node_free = obj_get_avl_node_free_method(object_type);
			tree->avl_node_user = obj_get_avl_node_user_value(object_type);
		}
		// This is far from standards compliant, but it should work anyways.
		tree->get_const_data = (const object_t * (*)(union const_entry))tree->get_data;
		tree->get_const_entry_for = (union const_entry (*)(const struct avl_tree *, const object_t *))tree->get_entry_for;
		tree->get_const_entry_for_succeeded = (bool (*)(const struct avl_tree *, const object_t *, union const_entry *))tree->get_entry_for_succeeded;
	}
}

/*
MEMBER_FUNCTIONS_2(struct avl_node *,avl_node_new,void *,object_t *)
MEMBER_PROCEDURE_3(void,avl_node_free,void *,object_t *,struct avl_node *)

#define DECLARE_FUNCTIONS_3(rettype,funcname,type1,type2,type3) \
	bool obj_has_ ## funcname ## _method(const struct obj_type * type); \
	rettype (*obj_set_ ## funcname ## _method(struct obj_type * type,rettype (*funcname)(type1,type2,type3)))(type1,type2,type3); \
	rettype obj_call_ ## funcname(const struct obj_type * type,type1,type2,type3); \
*/
