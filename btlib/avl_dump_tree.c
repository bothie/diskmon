/*
 * avl_dump_tree.c. Part of the bothie-utils.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "avl_private.h"

#include <string.h>

#include "btmacros.h"
#include "btmath.h"
#include "mprintf.h"
#include "stddbg.h"

#ifndef AVL_NUM_IGNORE_DT_CALLS
#define AVL_NUM_IGNORE_DT_CALLS 0
#endif // #ifndef AVL_NUM_IGNORE_DT_CALLS

static unsigned max_entries;
static unsigned max_levels;
static unsigned real_max_levels;

struct array {
	char     * string;
	size_t     sl;
	size_t     left_gap;
	size_t     prev_lc;
	size_t     prev_rc;
	unsigned   xpos;
	struct avl_node * node;
};

static struct array * * global_level;
static bool * global_doubleline;

/*
 * This could be done non-recursive too.
 */
static unsigned visualize_tree_pass_1(
	unsigned flags,
	unsigned level,
	unsigned entry,
	struct array * * array,
	struct avl_node * subtree,
	unsigned * num_levels
) {
	struct avl_node * sl;
	struct avl_node * sr;
	unsigned retval;
	
	if (subtree) {
		struct avl_tree * tree = subtree->tree_balance->tree;
		struct refcounter * tree_type_rc = tree->type;
		
		retval=1;
		if (unlikely(*num_levels<level)) {
			*num_levels=level;
		}
		array[level][entry].node=subtree;
		if (rc_get_object(tree_type_rc) && obj_has_display_method(avl_get_obj_type(tree))) {
			array[level][entry].sl = strlen(
				array[level][entry].string = obj_call_display(avl_get_obj_type(tree), tree->get_data(subtree->entry))
			);
		} else {
			array[level][entry].string = mprintf_sl(&array[level][entry].sl, "%p", (void*)tree->get_data(subtree->entry));
		}
		if (flags & ADTF_BALANCE_VALUES) {
			char * tmp = mprintf_sl(&array[level][entry].sl, "%s(%2i)", array[level][entry].string, subtree->tree_balance->balance);
			free(array[level][entry].string);
			array[level][entry].string = tmp;
		}
		sl=subtree->left;
		sr=subtree->right;
	} else {
		retval=0;
		array[level][entry].node=NULL;
		array[level][entry].sl=0;
		array[level][entry].string=NULL;
		sl=NULL;
		sr=NULL;
	}
	
	array[level][entry].xpos=0;
	array[level][entry].left_gap=0;
	
	if (++level<max_levels) {
		entry*=2;
		retval += visualize_tree_pass_1(flags, level, entry + 0, array, sl, num_levels);
		retval += visualize_tree_pass_1(flags, level, entry + 1, array, sr, num_levels);
	}
	
	return retval;
}

static bool visualize_tree_pass_2(
	unsigned flags,
	unsigned level,
	unsigned entry,
	struct array * const * const array,
	bool * const doubleline,
	const unsigned num_passes,
	const unsigned num_levels
) {
	int parent_xpos;
	size_t left_child_xpos;
	int average_child_xpos;
	size_t right_child_xpos;
	int left_sibling_xpos;
	
	bool retval=false;
	
	if (level+1<num_levels) {
		if (visualize_tree_pass_2(
			flags,
			level+1,
			entry*2,
			array,
			doubleline,
			num_passes,
			num_levels
		)) {
			retval=true;
		}
	}
	
	if (likely(level)) {
		if (entry&1) {
			parent_xpos=
				array[level-1][entry/2].xpos+
				array[level-1][entry/2].sl+1+
				doubleline[level-1];
		} else {
			parent_xpos=
				array[level-1][entry/2].xpos-
				array[level  ][entry  ].sl-1-
				array[level-1][entry/2].left_gap-
				doubleline[level-1];
		}
	} else {
		parent_xpos=0;
	}
	
	if (level+1<num_levels) {
		int dl=doubleline[level];
		
		if (array[level+1][entry*2+0].sl) {
			left_child_xpos=
				array[level+1][entry*2+0].xpos+
				array[level+1][entry*2+0].sl+1+dl;
		} else {
			left_child_xpos=0;
		}
		if (array[level+1][entry*2+1].sl) {
			right_child_xpos=
				array[level+1][entry*2+1].xpos-
				(array[level][entry].sl+1+dl);
		} else {
			right_child_xpos=0;
		}
		if (right_child_xpos==array[level][entry].prev_rc
		&&   left_child_xpos==array[level][entry].prev_lc) {
			if (right_child_xpos && left_child_xpos) {
				if (right_child_xpos>left_child_xpos) {
					if (!doubleline[level]) {
						doubleline[level]=1;
						retval=true;
					}
					average_child_xpos=
						(right_child_xpos
						+left_child_xpos)/2;
				} else {
					average_child_xpos=left_child_xpos;
				}
			} else {
				if (right_child_xpos) {
					average_child_xpos=right_child_xpos;
				} else if (left_child_xpos) {
					average_child_xpos=left_child_xpos;
				} else {
					average_child_xpos=0;
				}
			}
		} else {
			average_child_xpos=left_child_xpos;
		}
		if (left_child_xpos) {
			unsigned lg=max2(array[level][entry].left_gap,average_child_xpos-left_child_xpos);
			if (lg!=array[level][entry].left_gap) {
				array[level][entry].left_gap=lg;
				retval=true;
			}
		}
	} else {
		left_child_xpos=right_child_xpos=average_child_xpos=0;
	}
	
	if (entry) {
		left_sibling_xpos=array[level][entry-1].xpos+array[level][entry-1].sl+!!array[level][entry-1].sl;
	} else {
		left_sibling_xpos=0;
	}
	
	unsigned currentxpos=max5(
		parent_xpos,
		left_child_xpos,
		average_child_xpos,
		left_sibling_xpos,
		array[level][entry].xpos
	);
	
	if (!array[level][entry].sl) {
		currentxpos=left_sibling_xpos;
	}
	
	if (array[level][entry].prev_rc!=right_child_xpos
	||  array[level][entry].prev_lc!= left_child_xpos
	||  array[level][entry].xpos   !=     currentxpos) {
		retval=true;
	}
	
	array[level][entry].prev_rc=right_child_xpos;	
	array[level][entry].prev_lc= left_child_xpos;
	array[level][entry].xpos   =     currentxpos;
	
	if (level+1<num_levels) {
		if (visualize_tree_pass_2(
			flags,
			level+1,
			entry*2+1,
			array,
			doubleline,
			num_passes,
			num_levels
		)) {
			retval=true;
		}
	}
	
	return retval;
}

static void show_pass_2(
	struct avl_tree * tree,
	unsigned flags,
	struct array * * level,
	const bool * const doubleline,
	bool paint_wires,
	int num_passes,
	size_t num_highlights,
	const object_t * const * highlight,
	const unsigned num_levels,
	void (*dt_highlight)(bool flag),
	int (*dt_printf)(const char * fmt,...)
) {
	int i,n=0; // Initialisation for gcc to shut up (warning: `int n' might be used uninitialized in this function)
	unsigned c=0,l;
	
	ignore(paint_wires); // FIXME!!!
	ignore(flags);
	
	if (flags & ADTF_DEBUG) {
		bprintf("Tree after %i passes\n",num_passes);
	}
	
	for (l=0;l<num_levels;++l) {
		int prev_n=n;
		int new_n=1<<l;
		
		n=new_n;
		
		for (i=0;i<n;++i) {
			if (level[l][i].sl) {
				break;
			}
		}
		if (i==n) break;
		
		n=prev_n;
		
		if (l) {
			for (int dl=0;dl<=doubleline[l-1];++dl) {
				for (c=i=0;i<n;++i) { // n IS defined.
					int skip;
					char fc1,fc2;
					
					if (!level[l][i*2].sl 
					&&  !level[l][i*2+1].sl)
						continue;
					
					if (level[l][i*2].sl) {
						skip=level[l  ][i*2].xpos+level[l  ][i*2].sl-c;
						dt_printf("%*s%c",skip,"",(dl==doubleline[l-1])?'/':' ');
						c+=skip+1;
						fc1=dl?' ':'_';
						fc2=dl?' ':'/';
					} else {
						fc1=' ';
						fc2=' ';
					}
					
					if (doubleline[l-1]) {
						while (++c<level[l-1][i].xpos) dt_printf("%c",fc1);
						dt_printf("%c",fc2);
					}
					
					if (level[l][i*2+1].sl) {
						skip=level[l-1][i  ].xpos-c+level[l-1][i  ].sl;
						dt_printf("%*s%c",skip,"",dl?' ':'\\');
						c+=skip+1;
						fc1=dl?' ':'_';
						fc2=dl?'\\':' ';
					} else {
						fc1=' ';
						fc2=' ';
					}
					
					if (doubleline[l-1]) {
						while (++c<level[l][i*2+1].xpos) dt_printf("%c",fc1);
						dt_printf("%c",fc2);
					}
				}
				dt_printf("\n");
			}
		}
		
		n=new_n;
		
		for (c=i=0;i<n;++i) {
			int skip=level[l][i].xpos-c;
			bool highlighted=false;
			
			if (level[l][i].node && dt_highlight) {
				for (size_t hl = 0; hl < num_highlights; ++hl) {
					if (tree->get_data(level[l][i].node->entry) == highlight[hl]) {
						highlighted=true;
						dt_highlight(true);
						break;
					}
				}
			}
			dt_printf(
				"%*s%s",skip,""
				,level[l][i].string?level[l][i].string:""
			);
			if (highlighted) {
				dt_highlight(false);
			}
			c+=skip+level[l][i].sl;
		}
		dt_printf("\n");
	}
}

static void dump_init(unsigned flags, unsigned avl_dumptree_max_levels) {
	unsigned i,j;
	
	for (i = 0, max_entries = 1; i < real_max_levels; ++i, max_entries *= 2) {
		if (flags & ADTF_DEBUG) {
			bprintf("dump_init: i = %u/%u - reinitializing\n", i + 1, real_max_levels);
		}
		for (j = 0; j < max_entries; ++j) {
			free(global_level[i][j].string);
			global_level[i][j].prev_rc = 0;
			global_level[i][j].prev_lc = 0;
			global_level[i][j].string = NULL;
		}
	}
	
	if (real_max_levels < avl_dumptree_max_levels) {
		real_max_levels = avl_dumptree_max_levels;
		REALLOC(global_level, struct array *, real_max_levels);
		REALLOC(global_doubleline, bool, (real_max_levels - 1));
		
		for (; i < real_max_levels; ++i, max_entries *= 2) {
			if (flags & ADTF_DEBUG) {
				bprintf("dump_init: i = %u/%u - allocating and initializing\n", i + 1, real_max_levels);
			}
			MALLOC(global_level[i], struct array, max_entries);
			for (j = 0; j < max_entries; ++j) {
				global_level[i][j].prev_rc = 0;
				global_level[i][j].prev_lc = 0;
				global_level[i][j].string = NULL;
			}
		}
	}
	
	max_levels = avl_dumptree_max_levels;
	
	for (i = 0, max_entries = 1; i < max_levels; ++i, max_entries *= 2) ;
	
	--max_entries;
}

static unsigned subtree_depth(struct avl_node * subtree, bool * balanced) {
	static unsigned num_recursions=-1000;
	unsigned rdepth,ldepth;
	
	struct avl_tree * tree = subtree->tree_balance->tree;
	
	if (!++num_recursions) {
		--num_recursions;
		eprintf("subtree_depth: Endless loop detected in AVL tree %p\n", (void*)tree);
		*balanced=false;
		return 0;
	}
	
	ldepth=subtree->left ?subtree_depth(subtree->left ,balanced):0;
	if (!*balanced) return 0;
	rdepth=subtree->right?subtree_depth(subtree->right,balanced):0;
	if (!*balanced) return 0;
	
	if (ldepth+1<rdepth || ldepth>rdepth+1) {
		eprintf(
			"subtree_depth: Tree is unbalanced at %p (%p->%u,%p->%u)\n"
			, (void*)tree->get_data(subtree->entry)
			, (void*)subtree->left,  ldepth
			, (void*)subtree->right, rdepth
		);
		*balanced=false;
	}
	
	if (ldepth+1==rdepth && subtree->tree_balance->balance!=+1) {
		eprintf(
			"subtree_depth: Tree is leaning right balanced but marked %i at %p (%p->%u,%p->%u)\n"
			, subtree->tree_balance->balance
			, (void*)tree->get_data(subtree->entry)
			, (void*)subtree->left,  ldepth
			, (void*)subtree->right, rdepth
		);
		*balanced=false;
	}
	
	if (ldepth==rdepth+1 && subtree->tree_balance->balance!=-1) {
		eprintf(
			"subtree_depth: Tree is leaning left balanced but marked %i at %p (%p->%u,%p->%u)\n"
			, subtree->tree_balance->balance
			, (void*)tree->get_data(subtree->entry)
			, (void*)subtree->left,  ldepth
			, (void*)subtree->right, rdepth
		);
		*balanced=false;
	}
	
	if (ldepth==rdepth   && subtree->tree_balance->balance!= 0) {
		eprintf(
			"subtree_depth: Tree is well balanced but marked %i at %p (%p->%u,%p->%u)\n"
			, subtree->tree_balance->balance
			, (void*)tree->get_data(subtree->entry)
			, (void*)subtree->left,  ldepth
			, (void*)subtree->right, rdepth
		);
		*balanced=false;
	}
	
	--num_recursions;
	
	return max2(ldepth,rdepth)+1;
}

static inline bool check_balance(struct avl_tree * tree) {
	bool retval=true;
	if (tree->root) subtree_depth(tree->root,&retval);
	return retval;
}

void avl_dump_tree(
	struct avl_tree * tree,
	unsigned flags,
	unsigned avl_dumptree_max_levels,
	size_t num_highlights,
	const object_t * const * highlight,
	void (*dt_highlight)(bool flag),
	int (*dt_printf)(const char * fmt,...)
) {
	static long long dt_call=0;
	static bool has_errors=false;
	
	if (flags & ADTF_DEBUG) {
		bprintf("dump_tree: Call %llu\n",++dt_call);
	}
	
	if (!check_balance(tree)) {
		unsigned * valgrind = malloc(sizeof(*valgrind));
		bprintf("Tree is no longer balanced -> forcing output! (ignore this value: %u)\n", *valgrind);
		free(valgrind);
		has_errors=true;
	}
	/*
	if (dt_call == 68) {
		bprintf("68th call, forcing output!\n");
		has_errors=true;
	}
	*/
	/*
	if (!has_errors) {
		if (flags & ADTF_DEBUG) {
			bprintf("Tree seems clean, forcing output due to !\n");
		}
		has_errors = true;
	}
	*/
	
	if (!has_errors && (flags & ADTF_ON_ERROR || (unsigned)AVL_NUM_IGNORE_DT_CALLS==(unsigned)-1 || dt_call<AVL_NUM_IGNORE_DT_CALLS)) {
		if (flags & ADTF_DEBUG) {
			bprintf("%llith call, skipping output - no errors and ADTF_ON_ERROR given\n", dt_call);
		}
		return;
	}
	
	if (has_errors) {
		flags |= ADTF_BALANCE_VALUES;
	}
	
	dump_init(flags, avl_dumptree_max_levels);
	
	if (flags & ADTF_DEBUG) {
		bprintf("avl_dump_tree: dump_init finished\n");
	}
	
	unsigned num_levels=0;
	
	unsigned nve=visualize_tree_pass_1(flags, 0, 0, global_level, tree->root, &num_levels);
	if (avl_size(tree)!=nve) {
		bprintf(
			"Tree is too big with %u entries (only %u will be shown).\n"
			"You may however tune this by increasing \n"
			"the value avl_dumptree_max_levels\n"
			"\n"
			, (unsigned)avl_size(tree)
			,nve
		);
	}
	
	++num_levels;
	
	if (flags & ADTF_DEBUG) {
		bprintf("avl_dump_tree: visualize_tree_pass_1 finished, num_levels = %u\n", num_levels);
	}
	
	for (unsigned i=0;i<max_levels-1;++i) {
		global_doubleline[i]=0;
	}
	int num_passes=0;
	while (visualize_tree_pass_2(flags, 0, 0, global_level, global_doubleline, num_passes, num_levels)) {
		++num_passes;
		if (flags & ADTF_DEBUG) {
			bprintf("avl_dump_tree: visualize_tree_pass_2 pass %i finished\n", num_passes);
		}
	}
	
	++num_passes;
	
	if (flags & ADTF_DEBUG) {
		bprintf("avl_dump_tree: final visualize_tree_pass_2 pass %i finished\n", num_passes);
	}
	
	show_pass_2(tree
		, flags
		, global_level
		, global_doubleline
		, true
		, num_passes
		, num_highlights
		, highlight
		, num_levels
		, dt_highlight
		, dt_printf
	);
}
