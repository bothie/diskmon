/*
 * avl_do_insert.c. Part of the bothie-utils.
 *
 * Copyright (C) 2005-2017 Bodo Thiesen <bothie@gmx.de>
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
#include "refcounter.h"

#include <assert.h>
#include <errno.h>

#include "mprintf.h"

struct avl_tree * avl_mk_tree_zero(struct refcounter * rc_type) {
	struct avl_tree * retval;
	
	NEWRETVAL();
	
	avl_InitMembers(retval, rc_type);
	
	return retval;
}

static struct avl_node * avl_do_node_mk_tree(struct avl_tree * tree, union entry entry_key) {
	struct avl_node * retval;
	object_t * entry = tree->get_data(entry_key);
	
	if (!entry) return NULL;
	
	retval = tree->avl_node_new(tree->avl_node_user, entry);
	if (!retval) return NULL;
	
	tree->inc_refcount(entry_key);
	
	retval->entry = entry_key;
	retval->left = retval->right = retval->prev = retval->next = NULL;
	set_tree_balance(&retval->tree_balance, tree, 0);
	
	return retval;
}

#define t (*_t)

void avl_node_rotateLL(struct avl_node * * _t) {
	struct avl_node * tl;
	
	tl=t->left;
	t->left=tl->right;
	tl->right=t;
	set_balance(&tl->tree_balance, 0);
	set_balance(&t->tree_balance, 0);
	
	t=tl;
}

void avl_node_rotateLR(struct avl_node * * _t) {
	struct avl_node * tl=t->left;
	struct avl_node * tlr=tl->right;
	
	tl->right=tlr->left;
	tlr->left=tl;
	t->left=tlr->right;
	
	switch (tlr->tree_balance->balance) {
		case  1: set_balance(&tl->tree_balance, -1); set_balance(&t->tree_balance,  0); break;
		case  0: set_balance(&tl->tree_balance,  0); set_balance(&t->tree_balance,  0); break;
		case -1: set_balance(&tl->tree_balance,  0); set_balance(&t->tree_balance,  1); break;
		default: assert(never_reached);
	}
	set_balance(&tlr->tree_balance,  0);
	
	tlr->right=t;
	
	t=tlr;
}

void avl_node_rotateRL(struct avl_node * * _t) {
	struct avl_node * tr=t->right;
	struct avl_node * trl=tr->left;
	
	tr->left=trl->right;
	trl->right=tr;
	t->right=trl->left;
	
	switch (trl->tree_balance->balance) {
		case  1: set_balance(&tr->tree_balance,  0); set_balance(&t->tree_balance, -1); break;
		case  0: set_balance(&tr->tree_balance,  0); set_balance(&t->tree_balance,  0); break;
		case -1: set_balance(&tr->tree_balance,  1); set_balance(&t->tree_balance,  0); break;
		default: assert(never_reached);
	}
	set_balance(&trl->tree_balance,  0);
	
	trl->left=t;
	
	t=trl;
}

void avl_node_rotateRR(struct avl_node * * _t) {
	struct avl_node * tr=t->right;
	
	t->right=tr->left;
	tr->left=t;
	set_balance(&tr->tree_balance, 0);
	set_balance(&t->tree_balance, 0);
	
	t=tr;
}

enum came_from {
	RIGHT,
	LEFT,
};

inline static void avl_do_insert_helper(
	struct avl_node * * _new_node,
	struct avl_node * prev_node,
	enum came_from came_from
) {
	switch (came_from) {
		case RIGHT: {
			prev_node->right = *_new_node;
			prev_node->right->prev = prev_node;
			prev_node->right->next = prev_node->next;
			prev_node->next = prev_node->right;
			if (prev_node->right->next) {
				prev_node->right->next->prev = prev_node->right;
			} else {
				prev_node->tree_balance->tree->last = prev_node->right;
			}
			break;
		}
		case LEFT: {
			prev_node->left = *_new_node;
			prev_node->left->next = prev_node;
			prev_node->left->prev = prev_node->prev;
			prev_node->prev = prev_node->left;
			if (prev_node->left->prev) {
				prev_node->left->prev->next = prev_node->left;
			} else {
				prev_node->tree_balance->tree->frst = prev_node->left;
			}
			break;
		}
	}
}

static bool avl_do_insert_recursive(
	struct avl_node * * _new_node,
	struct avl_node * * _t
) {
	int anc = avl_node_compare(t, t->tree_balance->tree->get_data((*_new_node)->entry), t->tree_balance->tree->get_data(t->entry));
	
	if (anc < 0) {
		if (t->left) {
			if (!avl_do_insert_recursive(_new_node, &t->left)) {
				return false;
			}
		} else {
			avl_do_insert_helper(_new_node, t, LEFT);
		}
		switch (dec_balance(&t->tree_balance)) {
			case -1: {
				return true;
			}
			case -2: {
				if (t->left->tree_balance->balance==-1) {
					avl_node_rotateLL(_t);
				} else {
					avl_node_rotateLR(_t);
				}
				set_balance(&t->tree_balance, 0);
			}
		}
	} else if (anc > 0) {
		if (t->right) {
			if (!avl_do_insert_recursive(_new_node, &t->right)) {
				return false;
			}
		} else {
			avl_do_insert_helper(_new_node, t, RIGHT);
		}
		switch (inc_balance(&t->tree_balance)) {
			case 1: {
				return true;
			}
			
			case 2: {
				if (t->right->tree_balance->balance== 1) {
					avl_node_rotateRR(_t);
				} else {
					avl_node_rotateRL(_t);
				}
				set_balance(&t->tree_balance, 0);
			}
		}
	} else {
		assert(!anc);
		
		*_new_node = (*_t);
	}
	
	return false;
}

bool avl_do_insert(struct avl_tree * tree, union entry entry_key, union entry * eexist) {
	if (DEBUG_AVL) {
		avl_dump_tree(tree, ADTF_ON_ERROR | ADTF_BALANCE_VALUES, 7, 0, NULL, NULL, eprintf);
		
		char * entry_string;
		
		if (rc_get_object(tree->type) && obj_has_display_method(avl_get_obj_type(tree))) {
			entry_string = obj_call_display(avl_get_obj_type(tree), tree->get_data(entry_key));
		} else {
			entry_string = mprintf("%p", (void*)tree->get_data(entry_key));
		}
		eprintf("avl_insert_rc: Inserting rc_key %s ... ", entry_string);
		free(entry_string);
	}
	
	struct avl_node * new_node = avl_do_node_mk_tree(tree, entry_key);
	if (!new_node) {
		if (eexist) {
			union entry zero = { 0 };
			
			*eexist = zero;
		}
		
		return false;
	}
	if (tree->root) {
		if (DEBUG_AVL) {
			eprintf(
				"avl_do_insert: >>> avl_do_insert_recursive(%p -> %p)\n"
				, void_ptr(new_node)
				, tree->get_data(new_node->entry)
			);
		}
		struct avl_node * eexist_node = new_node;
		avl_do_insert_recursive(&eexist_node, &tree->root);
		if (DEBUG_AVL) {
			eprintf(
				"avl_do_insert: <<< avl_do_insert_recursive(%p -> %p) = %p -> %p\n"
				, void_ptr(new_node)
				, tree->get_data(new_node->entry)
				, void_ptr(eexist_node)
				, tree->get_data(eexist_node->entry)
			);
		}
		if (new_node != eexist_node) {
			if (DEBUG_AVL) {
				eprintf("EEXIST\n");
			}
			avl_node_free(new_node);
			errno=EEXIST;
			if (eexist) {
				*eexist = eexist_node->entry;
			}
			return false;
		}
	} else {
		tree->root = new_node;
		tree->frst = new_node;
		tree->last = new_node;
	}
	++tree->num_elements;
	assert(tree->num_elements);
	
	if (DEBUG_AVL) {
		eprintf("done\n");
		
		avl_dump_tree(tree, ADTF_ON_ERROR | ADTF_BALANCE_VALUES, 7, 0, NULL, NULL, eprintf);
	}
	
	if (eexist) {
		union entry zero = { 0 };
		
		*eexist = zero;
	}
	
	return true;
}
