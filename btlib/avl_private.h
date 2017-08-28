/*
 * avl_private.h. Part of the bothie-utils.
 *
 * Copyright (C) 2007-2015 Bodo Thiesen <bothie@gmx.de>
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

#ifndef BTLINUXLIBRARY_AVL_PRIVATE_H
#define BTLINUXLIBRARY_AVL_PRIVATE_H

#include "avl.h"

#include "object.h"

#define DEBUG_AVL 0

/*
 * If the object type this tree is composed of has refcounting, the member rc 
 * is valid. If not, the entry obj is valid for the object itself, no 
 * dereferencing using rc_get_data is performed then.
 */
union entry {
	struct refcounter * rc;
	object_t * obj;
};

union const_entry {
	const struct refcounter * rc;
	const object_t * obj;
};

static inline union const_entry make_const_entry(union entry entry) {
	union const_entry retval;
	retval.rc = entry.rc;
	retval.obj = entry.obj;
	return retval;
}

struct avl_tree * avl_mk_tree_cmp(
	struct refcounter * rc,
	int (*compare)(const object_t * entry1,const object_t * entry2),
	struct obj_type * * * refcounter_type_storage_ptr
);
struct avl_tree * avl_mk_tree_zero(struct refcounter * rc_type);

struct avl_balance_tree {
	struct avl_tree * tree;
	int balance;
};

struct avl_node {
	union entry entry;
	struct avl_node * left;
	struct avl_node * right;
	struct avl_node * next;
	struct avl_node * prev;
	/*
	 * tree_balance - combination of
	 *
	 * 	struct avl_tree * tree;
	 * 	int balance;
	 *
	 * This is done by the simple additional struct struct avl_balance_tree
	 * which is composed of this two elements. There are five entries in array
	 * tree->tree_balance, all their tree entries pointing back to tree and their
	 * balance fields set to -2, -1, 0, +1 and +2. Now, instead of storing tree
	 * and balance separately here, we take one of the five array entries, store
	 * their address and voila, we still know our tree AND we know the balance
	 * value. Changing the balance value is done by replacing the tree_balance
	 * pointer by another pointer to that array representing the new balance
	 * value.
	 * 
	 * The cost is: One additional memory dereference for every single access to
	 * either node->tree or node->balance, but that easily gets paid if the
	 * application doesn't start to swap because sizeof(avl_node) is 16 bytes
	 * bigger in space consumed on amd64. (This struct already is 48 bytes in
	 * size, plus the malloc overhead makes 64 bytes. With separate tree and
	 * balance it would be 80 bytes - per node of course.
	 */
	const struct avl_balance_tree * tree_balance;
};

struct avl_tree {
	size_t num_elements;
	struct refcounter * type;
	struct avl_node * root;
	struct avl_balance_tree tree_balance[5];
	struct avl_node * frst;
	struct avl_node * last;
	
	const object_t * (*get_const_data)(union const_entry entry);
	union const_entry (*get_const_entry_for)(const struct avl_tree * tree, const object_t * obj);
	bool (*get_const_entry_for_succeeded)(const struct avl_tree * tree, const object_t * obj, union const_entry * entry);
	
	object_t * (*get_data)(union entry entry);
	union entry (*get_entry_for)(const struct avl_tree * tree, object_t * obj);
	bool (*get_entry_for_succeeded)(const struct avl_tree * tree, object_t * obj, union entry * entry);
	
	void (*inc_refcount)(union entry entry);
	void (*dec_refcount)(union entry entry);
	
	struct avl_node * (*avl_node_new)(void * user, object_t * obj);
	void (*avl_node_free)(void * user, object_t * obj, struct avl_node * node);
	void * avl_node_user;
};

static inline object_t * avl_get_data(const struct avl_tree * const tree, union entry entry) {
	return tree->get_data(entry);
}

static inline struct refcounter * avl_rc_get_data(const struct avl_tree * const tree, union entry entry) {
	ignore(tree);
	return entry.rc;
}

static inline union entry avl_get_entry_for(const struct avl_tree * const tree, object_t * obj) {
	return tree->get_entry_for(tree, obj);
}

static inline union entry avl_get_entry_for_rc(const struct avl_tree * const tree, struct refcounter * rc) {
	ignore(tree);
	union entry retval;
	retval.rc = rc;
	return retval;
}

static inline union const_entry avl_get_const_entry_for(const struct avl_tree * const tree, const object_t * obj) {
	return tree->get_const_entry_for(tree, obj);
}

static inline union const_entry avl_get_const_entry_for_rc(const struct avl_tree * const tree, const struct refcounter * rc) {
	ignore(tree);
	union const_entry retval;
	retval.rc = rc;
	return retval;
}

static inline struct obj_type * avl_get_obj_type(const struct avl_tree * tree) {
	return (struct obj_type *)rc_get_object(tree->type);
}

static inline int avl_tree_compare(const struct avl_tree * tree, const object_t * key1, const object_t * key2) {
	return obj_call_compare(avl_get_obj_type(tree), key1, key2);
}

static inline int avl_node_compare(const struct avl_node * node, const object_t * key1, const object_t * key2) {
	return avl_tree_compare(node->tree_balance->tree, key1, key2);
}

static inline struct avl_balance_tree * deconst_tree_balance(const struct avl_balance_tree * tree_balance) {
	return (struct avl_balance_tree *)tree_balance;
}

static inline int set_tree_balance(const struct avl_balance_tree * * tbp, const struct avl_tree * tree, int new_balance) {
	(*tbp) = tree->tree_balance + 2 + new_balance;
	
	return (*tbp)->balance;
}

static inline int set_balance(const struct avl_balance_tree * * tbp, int new_balance) {
	return set_tree_balance(tbp, (*tbp)->tree, new_balance);
}

static inline int inc_balance(const struct avl_balance_tree * * tbp) {
	return set_balance(tbp, (*tbp)->balance + 1);
}

static inline int dec_balance(const struct avl_balance_tree * * tbp) {
	return set_balance(tbp, (*tbp)->balance - 1);
}

struct avl_iterator {
	const struct avl_tree * tree;
	const struct avl_node * node;
};

struct avl_iterator * avl_mk_iterator(const struct avl_tree * tree);
void avl_node_rotateLL(struct avl_node * * _t);
void avl_node_rotateLR(struct avl_node * * _t);
void avl_node_rotateRL(struct avl_node * * _t);
void avl_node_rotateRR(struct avl_node * * _t);

void avl_node_free(struct avl_node * node);
void avl_node_free_recursive(struct avl_node * node);

void avl_InitMembers(struct avl_tree * tree, struct refcounter * rc_type);

const struct avl_node * avl_node_find_authority(const struct avl_node * node, const object_t * key);

union entry avl_do_find(const struct avl_tree * tree, const object_t * key);
bool avl_do_insert(struct avl_tree * tree, union entry entry_key, union entry * eexist);

struct avl_node * default_avl_node_new(void * user, object_t * entry);
void default_avl_node_free(void * user, object_t * entry, struct avl_node * node);

#endif // #ifndef BTLINUXLIBRARY_AVL_PRIVATE_H
