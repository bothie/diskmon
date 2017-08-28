/*
 * avl_remove.c. Part of the bothie-utils.
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

#include <assert.h>
#include <errno.h>

#define t (*_t)

#include "mprintf.h"
#include "btstr.h"

struct avl_remove_state {
	struct avl_node * * removing_root;
	struct avl_node * removing_node;
	struct avl_node * replacement_node;
	const object_t * key;
};

/*
 * Left tree is shorter than right tree.
 */
static bool avl_node_rebalanceL(struct avl_node * * _t) {
	int tb;
	
	switch (inc_balance(&t->tree_balance)) {
		case  0: {
			return true;
		}
		case  1: {
			return false;
		}
	}
	set_balance(&t->tree_balance, 1);
	
	if (0>(tb=t->right->tree_balance->balance)) {
		avl_node_rotateRL(_t);
		return true;
	}
	avl_node_rotateRR(_t);
	if (!tb) {
		set_balance(&t->tree_balance, -1);
		set_balance(&t->left->tree_balance, 1);
		return false;
	}
	return true;
}

/*
 * Right tree is shorter than left tree.
 */
static bool avl_node_rebalanceR(struct avl_node * * _t) {
	int tb;
	
	switch (dec_balance(&t->tree_balance)) {
		case  0: {
			return true;
		}
		case -1: {
			return false;
		}
	}
	set_balance(&t->tree_balance, -1);
	
	if (0<(tb=t->left->tree_balance->balance)) {
		avl_node_rotateLR(_t);
		return true;
	}
	avl_node_rotateLL(_t);
	if (!tb) {
		set_balance(&t->tree_balance, 1);
		set_balance(&t->right->tree_balance, -1);
		return false;
	}
	return true;
}

#define subtree (*_subtree)
static bool avl_node_rm_recursiveR(
	struct avl_remove_state * rs,
	// unsigned do_extra_updates,
	struct avl_node * * _subtree
) {
	if (subtree->left) {
		if (!avl_node_rm_recursiveR(rs, /* ++do_extra_updates, */ &subtree->left)) {
			return false;
		}
		return avl_node_rebalanceL(_subtree);
	}
	
	rs->replacement_node = subtree;
	
	// set_balance(&subtree->tree_balance, (*rs->removing_root)->tree_balance->balance);
	
	// subtree->left = (*rs->removing_root)->left;
	
	// if (do_extra_updates) {
		// struct avl_node * tmp = subtree->right;
		
		// subtree->right = (*rs->removing_root)->right;
		
		// subtree = tmp;
		
		subtree = subtree->right;
	// }
	
	// (*rs->removing_root) = subtree;
	
	return true;
}

static bool avl_node_rm_recursiveL(
	struct avl_remove_state * rs,
	// unsigned do_extra_updates,
	struct avl_node * * _subtree
) {
	if (subtree->right) {
		if (!avl_node_rm_recursiveL(rs, /* ++do_extra_updates, */ &subtree->right)) {
			return false;
		}
		return avl_node_rebalanceR(_subtree);
	}
	
	rs->replacement_node = subtree;
	
	// set_balance(&subtree->tree_balance, (*rs->removing_root)->tree_balance->balance);
	
	// subtree->right = (*rs->removing_root)->right;
	
	// if (do_extra_updates) {
		// struct avl_node * tmp = subtree->left;
		
		// subtree->left = (*rs->removing_root)->left;
		
		// subtree = tmp;
		
		subtree = subtree->left;
	// }
	
	// (*rs->removing_root) = subtree;
	
	return true;
}

static bool avl_node_remove_recursive(struct avl_remove_state * rs, struct avl_node * * _t) {
	bool retval;
	int cmp;
	
	if (!t) {
		return false;
	}
	
	cmp = avl_node_compare(t, rs->key, t->tree_balance->tree->get_data(t->entry));
	
	if (cmp) {
		if (cmp < 0) {
			if (!avl_node_remove_recursive(rs, &t->left)) {
				return false;
			}
			return avl_node_rebalanceL(_t);
		} else {
			if (!avl_node_remove_recursive(rs, &t->right)) {
				return false;
			}
			return avl_node_rebalanceR(_t);
		}
	}
	
	rs->removing_node = t;
	rs->removing_root = _t;
	
	if (!t->right && !t->left) {
		t = NULL;
		
		return true;
	}
	
	/*
	if (!t->right) {
		if (t->left) {
			assert(!t->left->tree_balance->balance);
			
			t = t->left;
		}
		
		return true;
	}
	
	if (!t->left) {
		assert(!t->right->tree_balance->balance);
		t = t->right;
		
		return true;
	}
	*/
	
	if (t->tree_balance->balance == 1) {
		if (!avl_node_rm_recursiveR(rs, /* 0, */ &t->right)) {
			return false;
		}
		retval = avl_node_rebalanceR(_t);
	} else {
		if (!avl_node_rm_recursiveL(rs, /* 0, */ &t->left)) {
			return false;
		}
		retval = avl_node_rebalanceL(_t);
	}
	
	return retval;
}
#undef t

static char * stringify(struct avl_tree * tree, struct avl_node * node) {
	if (!node) {
		return mstrcpy("(null)");
	}
	
	if (rc_get_object(tree->type) && obj_has_display_method(avl_get_obj_type(tree))) {
		return obj_call_display(avl_get_obj_type(tree), tree->get_data(node->entry));
	} else {
		return mprintf("%p", (void*)(tree->get_data(node->entry)));
	}
}

bool avl_remove(struct avl_tree * tree, const object_t * key) {
	struct avl_remove_state rs;
	
	rs.removing_root = NULL;
	rs.replacement_node = NULL;
	
	if (DEBUG_AVL) {
		avl_dump_tree(tree, ADTF_ON_ERROR | ADTF_BALANCE_VALUES, 7, 0, NULL, NULL, eprintf);
		
		char * entry_string;
		
		if (rc_get_object(tree->type) && obj_has_display_method(avl_get_obj_type(tree))) {
			entry_string = obj_call_display(avl_get_obj_type(tree), key);
		} else {
			entry_string = mprintf("%p", (void*)key);
		}
		eprintf("avl_remove: Removing key %s ... ", entry_string);
		free(entry_string);
	}
	
	rs.removing_node = NULL;
	rs.key = key;
	
	avl_node_remove_recursive(&rs, &tree->root);
	
	if (!rs.removing_node) {
		if (DEBUG_AVL) {
			eprintf("ENOENT\n");
		}
		errno = ENOENT;
		return false;
	}
	
	assert(tree->num_elements);
	
	unsigned flags = ADTF_ON_ERROR;
	if (!(!(*rs.removing_root) || (*rs.removing_root) == rs.removing_node)) {
		flags = 0;
	}
	if (DEBUG_AVL || !flags) {
		char * rr  = stringify(tree, *rs.removing_root);
		char * rn  = stringify(tree, rs.removing_node);
		char * rnl = stringify(tree, rs.removing_node ? rs.removing_node->left : NULL);
		char * rnr = stringify(tree, rs.removing_node ? rs.removing_node->right : NULL);
		char * xn  = stringify(tree, rs.replacement_node);
		char * xnl = stringify(tree, rs.replacement_node ? rs.replacement_node->left : NULL);
		char * xnr = stringify(tree, rs.replacement_node ? rs.replacement_node->right : NULL);
		
		eprintf("*rs.removing_root = %p (%s)\n", (void*)*rs.removing_root, rr);
		eprintf("rs.removing_node = %p (%s [%s|%s])\n", (void*)rs.removing_node, rn, rnl, rnr);
		eprintf("rs.replacement_node = %p (%s [%s|%s])\n", (void*)rs.replacement_node, xn, xnl, xnr);
		
		free(rr);
		free(rn);
		free(rnl);
		free(rnr);
		free(xn);
		free(xnl);
		free(xnr);
		
		avl_dump_tree(tree, flags | ADTF_BALANCE_VALUES, 7, 0, NULL, NULL, eprintf);
	}
	
	assert(!(*rs.removing_root) || (*rs.removing_root) == rs.removing_node);
	
	if (*rs.removing_root) {
		(*rs.removing_root) = rs.replacement_node;
		
		set_balance(&rs.replacement_node->tree_balance, rs.removing_node->tree_balance->balance);
		
		rs.replacement_node->left = rs.removing_node->left;
		rs.replacement_node->right = rs.removing_node->right;
	} else {
		assert(!rs.replacement_node);
	}
	
	rs.removing_node->left = NULL;
	rs.removing_node->right = NULL;
	
	if (rs.removing_node->next) {
		rs.removing_node->next->prev = rs.removing_node->prev;
	} else {
		tree->last = rs.removing_node->prev;
	}
	
	if (rs.removing_node->prev) {
		rs.removing_node->prev->next = rs.removing_node->next;
	} else {
		tree->frst = rs.removing_node->next;
	}
	
	--tree->num_elements;
	
	if (DEBUG_AVL) {
		avl_dump_tree(tree, ADTF_ON_ERROR | ADTF_BALANCE_VALUES, 7, 0, NULL, NULL, eprintf);
	}
	
	avl_node_free(rs.removing_node);
	
	return true;
}
