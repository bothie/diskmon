/*
 * avl.h. Part of the bothie-utils.
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

#ifndef BTLINUXLIBRARY_AVL_H
#define BTLINUXLIBRARY_AVL_H

#include "btmacros.h"
#include "refcounter.h"

#ifdef __cplusplus
#include <cstdbool>
#include <cstdlib>
#else // #ifdef __cplusplus
#include <stdbool.h>
#include <stdlib.h>
#endif // #ifdef __cplusplus, else

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus

struct avl_iterator;
struct avl_node;
struct avl_tree;
struct obj_type; // replacement for fully featured #include "object.h"

struct avl_tree * avl_mk_tree_rc(struct refcounter * rc_type);
struct avl_tree * avl_mk_tree(struct obj_type * type);
void avl_clear(struct avl_tree * tree);
void avl_free(struct avl_tree * tree);

#define DECLARE_DATARET_SOMEARG(name, somearg1) \
	       object_t   * avl_   ##name(somearg1); \
	struct refcounter * avl_rc_##name(somearg1); \

#define DECLARE_SOMERET_SOMEARG_DATAARG(someret, name, somearg1) \
	someret avl_##name     (somearg1,        object_t   *    key); \
	someret avl_##name##_rc(somearg1, struct refcounter * rc_key); \

#define DECLARE_SOMERET_SOMEARG_CDATAARG(someret, name, somearg1) \
	someret avl_##name     (somearg1, const        object_t     *    key); \
	someret avl_##name##_rc(somearg1, const struct refcounter * rc_key); \

#define DECLARE_DATARET_SOMEARG_CDATAARG(name, somearg1) \
	       object_t   * avl_   ##name     (somearg1, const        object_t   *    key); \
	       object_t   * avl_   ##name##_rc(somearg1, const struct refcounter * rc_key); \
	struct refcounter * avl_rc_##name     (somearg1, const        object_t   *    key); \
	struct refcounter * avl_rc_##name##_rc(somearg1, const struct refcounter * rc_key); \

#define DECLARE_DATARET_SOMEARG_DATAARG(name, somearg1) \
	       object_t   * avl_   ##name     (somearg1,        object_t   *    key); \
	       object_t   * avl_   ##name##_rc(somearg1, struct refcounter * rc_key); \
	struct refcounter * avl_rc_##name     (somearg1,        object_t   *    key); \
	struct refcounter * avl_rc_##name##_rc(somearg1, struct refcounter * rc_key); \

// DATA { avl_find | avl_find_rc | avl_rc_find | avl_rc_find_rc } (const struct avl_tree * tree, const DATA);
DECLARE_DATARET_SOMEARG_CDATAARG(find, const struct avl_tree * tree)

// bool { avl_insert | avl_insert_rc } (struct avl_tree * tree, DATA);
DECLARE_SOMERET_SOMEARG_DATAARG(bool, insert, struct avl_tree * tree)
#define avl_add_rc avl_insert_rc
#define avl_add avl_insert

// DATA { avl_insert_find | avl_insert_find_rc | avl_rc_insert_find | avl_rc_insert_find_rc } (const struct avl_tree * tree, DATA);
/*
 * This functions try to add the element to the avl tree. However, if it
 * already exists, they return the conflicting (i.e. already present) value
 * (like find would have done).
 * 
 * The return value of this function is the conflicting value or NULL, if
 * either an error occured OR the value was successfully added to the tree.
 * 
 * Upon return, errno is always defined to either EEXIST if the element
 * already exists (and hence the conflicting value was returned also), or 0,
 * if adding succeeeded or the actual error (most likely ENOMEM) on any other
 * failure.
 * 
 * insert success:  return NULL,     errno = 0
 * insert conflict: return conflict, errno = EEXIST
 * insert failure:  return NULL,     errno = whatever went wrong
 */
DECLARE_DATARET_SOMEARG_DATAARG(insert_find, struct avl_tree * tree)
#define    avl_add_find       avl_insert_find
#define    avl_add_find_rc    avl_insert_find_rc
#define avl_rc_add_find    avl_rc_insert_find
#define avl_rc_add_find_rc avl_rc_insert_find_rc

// bool { avl_remove | avl_remove_rc } (struct avl_tree * tree, const DATA);
DECLARE_SOMERET_SOMEARG_CDATAARG(bool, remove, struct avl_tree * tree)

size_t avl_size(const struct avl_tree * tree);

// struct avl_iterator * { avl_iterator_find | avl_iterator_find_rc } (const struct avl_tree * tree, const DATA);
DECLARE_SOMERET_SOMEARG_CDATAARG(struct avl_iterator *, iterator_find, const struct avl_tree * tree)

struct avl_iterator * avl_frst_entry(const struct avl_tree * tree);
struct avl_iterator * avl_last_entry(const struct avl_tree * tree);
#define avl_first_entry avl_frst_entry

// DATA { avl_dereference | avl_rc_dereference } (const struct avl_iterator * iterator);
DECLARE_DATARET_SOMEARG(dereference, const struct avl_iterator * iterator)

bool avl_next_entry(struct avl_iterator * iterator);
bool avl_prev_entry(struct avl_iterator * iterator);

void avl_iterator_free(struct avl_iterator * iterator);

size_t avl_get_node_alignment();
size_t avl_get_node_size();

void obj_set_avl_node_memory_management(struct obj_type * type
	, void * user
	, struct avl_node * (*avl_node_new_func)(void *, object_t *)
	, void (*avl_node_free_func)(void *, object_t *, struct avl_node *)
);

void * obj_get_avl_node_user_value(struct obj_type * type);

// Don't dump tree on call to avl_dump_tree if tree looks ok
#define ADTF_ON_ERROR 1

// Print balance values along side node values when printing tree. Will be fored on on errors
#define ADTF_BALANCE_VALUES 2

// Print internal avl_dump_tree stuff
#define ADTF_DEBUG 4

void avl_dump_tree(
	struct avl_tree * tree,
	unsigned flags,
	/*
	 * avl_dumptree_max_levels should be at least 15 or so, may be more,
	 * however memory consumption will be 2**avl_dumptree_max_levels, so
	 * you can't choose it too big.
	 */
	unsigned avl_dumptree_max_levels,
	size_t num_highlights,
	const object_t * const * highlight,
	void (*dt_highlight)(bool flag),
	int (*dt_printf)(const char * fmt,...)
);

void avl_dump_tree_rc(
	struct avl_tree * tree,
	unsigned flags,
	/*
	 * avl_dumptree_max_levels should be at least 15 or so, may be more,
	 * however memory consumption will be 2**avl_dumptree_max_levels, so
	 * you can't choose it too big.
	 */
	unsigned avl_dumptree_max_levels,
	size_t num_highlights,
	const struct refcounter * const * rc_highlight,
	void (*dt_highlight)(bool flag),
	int (*dt_printf)(const char * fmt,...)
);

#ifdef __cplusplus
} // extern "C" {
#endif // #ifdef __cplusplus

#endif // #ifndef BTLINUXLIBRARY_AVL_H
