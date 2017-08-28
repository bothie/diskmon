/*
 * refcounter.c. Part of the bothie-utils.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "refcounter.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>

#include "avl_private.h"
#include "btlock.h"
#include "btmacros.h"
#include "object.h"
#include "vasiar.h"

static struct btlock_lock * rc_lock;

static int refcounters_compare(const object_t * ptr1,const object_t * ptr2) {
	if ((const char*)ptr1-(const char*)ptr2 > 0) return  1;
	if ((const char*)ptr1-(const char*)ptr2 < 0) return -1;
	return 0;
}

static struct avl_tree * refcounters=NULL;
static struct obj_type * refcounter_type=NULL;

struct refcounter_cache {
	struct refcounter * base;
	struct refcounter * frst_free;
	size_t num_free;
	size_t num_all;
};

static VASIAR(struct refcounter_cache) refcounter_cache;
static size_t sum_free;
static size_t sum_all;
static size_t next_alloc = 256;

struct refcounter {
	void * data;
	struct obj_type * type;
	signed refcount;
};

static inline struct refcounter * rc_new_refcounter() {
	struct refcounter * retval;
	
	// TODO: btlock_assert_locked(rc_lock);
	for (size_t i = 0; i < VASIZE(refcounter_cache); ++i) {
		struct refcounter_cache * rcc = &VAACCESS(refcounter_cache, i);
		
		if (rcc->num_free) {
			retval = rcc->frst_free;
			rcc->frst_free = (struct refcounter *)rcc->frst_free->data;
			--rcc->num_free;
			--sum_free;
			return retval;
		}
	}
	
	struct refcounter_cache * rcc = &VANEW(refcounter_cache);
	rcc->frst_free = (rcc->base = malloc(next_alloc * sizeof(*rcc->base))) + 1;
	if (!rcc->base) {
		VATRUNCATE(refcounter_cache, VASIZE(refcounter_cache) - 1);
		return NULL;
	}
	sum_free += (rcc->num_free = (rcc->num_all = next_alloc) - 1);
	sum_all += next_alloc;
	
	for (size_t i = 2; i < next_alloc; ++i) {
		rcc->base[i-1].data = rcc->base + i;
	}
	rcc->base[next_alloc - 1].data = NULL;
	
	if (2 * next_alloc <= sum_all / 10) {
		next_alloc *= 2;
	}
	
	return rcc->base;
}

static inline void rc_delete_refcounter(struct refcounter * rc) {
	btlock_lock(rc_lock);
	for (size_t i = 0; i < VASIZE(refcounter_cache); ++i) {
		struct refcounter_cache * rcc = &VAACCESS(refcounter_cache, i);
		if (rc >= rcc->base && rc < (rcc->base + rcc->num_all)) {
			rc->data = rcc->frst_free;
			rcc->frst_free = rc;
			++rcc->num_free;
			++sum_free;
			btlock_unlock(rc_lock);
			return;
		}
	}
	assert_never_reached();
}

/******************************************************************************
 *
 * Publically visible functions.
 *
 ******************************************************************************/

/*
 * Increments the references counter for the wrapped object by one. This 
 * function returns it's argument.
 */
//		,(rc_get_object(rc->type) && obj_has_display_method(rc_get_object(rc->type)))?(tmp=obj_call_display(rc_get_object(rc->type),rc_get_object(rc))):""
struct refcounter * rc_inc_refcount(struct refcounter * rc) {
	++rc->refcount;
	assert(rc->refcount);
	if (!(rc->refcount)) {
		eprintf(
			"%s:%s:%i: ASSERT didn't work!\n"
			, __FILE__, __func__, __LINE__
		);
		raise(SIGKILL);
	}
	return rc;
}

/*
 * Decrements the references counter for the wrapped object by one
 *
 * if the references counter dropps to zero, the function returns true, false 
 * in any other case. If the function returns true, AND a free method was 
 * specified the data object is being freed using that free method else the 
 * data object is being free'd using stdlib's free function. However if the 
 * data object has been freed, the rc object itself is being free'd using 
 * strlib's free function.
 *
 * AVL crossing:
 *
 * This function uses the AVL functions to remove this object from the AVL tree 
 * when refcount reaches zero.
 */
bool rc_dec_refcount(struct refcounter * rc) {
	if (!rc->refcount) {
		return false;
		// The caller MUST NOT do any cleaning up before we have 
		// completed. So return false. The other incarnation of us will 
		// return true to it's caller later.
	}
	
	assert(rc->refcount>0);
	if (!(rc->refcount>0)) {
		eprintf(
			"%s:%s:%i: ASSERT didn't work!\n"
			, __FILE__, __func__, __LINE__
		);
		raise(SIGKILL);
	}
	
	if (--rc->refcount) {
		return false;
	}
	
	btlock_lock(rc_lock);
	avl_remove_rc(refcounters,rc); // rc's free method will not be called because of retval->refcount=1 in rc_init
	btlock_unlock(rc_lock);
	
	if (rc->type) {
		obj_call_free(rc->type,rc->data);
	} else {
		free(rc->data);
	}
	rc_delete_refcounter(rc);
	
	return true;
}

/*
 * Returns the address of the object refered to by this rc.
 */
object_t * rc_get_object(const struct refcounter * rc) {
	// assert(rc == rc_get_refcounter_for(rc->type, rc->data));
	return rc->data;
}

static void refcounters_free(object_t * mem) {
	free(mem);
}

static void kill_refcounter(void) {
	static struct avl_tree * valgrind = NULL;
	valgrind = refcounters;
	ignore(valgrind);
	refcounters=NULL;
	btlock_lock_free(rc_lock);
}

static void rc_init() {
	struct refcounter * rc;
	
	assert(!refcounter_type && !refcounters);
	
	if (!rc_lock) {
		rc_lock = btlock_lock_mk();
		if (!rc_lock) {
			return;
		}
	}
	
	rc = rc_new_refcounter();
	if (!rc) {
		return;
	}
	
	rc->refcount=1; // never clean up us ;-) - Assumption needed in call of avl_remove_rc(refcounters,rc) in rc_dec_refcount
	rc->data=NULL;
	rc->type=NULL;
	
	// We can't build a fully functional AVL tree yet, because we don't 
	// have an obj_type and we can't just create an obj_type because that 
	// would need the refcounters AVL tree -> hen-egg-problem. So, we 
	// create a special avl_mk_tree-function which creates an AVL tree 
	// without compare function and later we change the type of the 
	// obj_type_rc to a type, which does have a compare function.
	
	refcounters=avl_mk_tree_zero(rc);
	if (!refcounters) {
		btlock_lock_free(rc_lock);
		rc_delete_refcounter(rc);
		return;
	}
	
	refcounter_type=obj_mk_type_2("refcounter",refcounters_compare,refcounters_free);
	if (!refcounter_type) {
		avl_free(refcounters);
		refcounters=NULL;
		btlock_lock_free(rc_lock);
		rc_delete_refcounter(rc);
		return;
	}
	
	rc->data=refcounter_type;
	rc->type=refcounter_type;
	
	// Now the AVL tree is fully functional.
	
/*
	if (!avl_add_rc(refcounters,rc)) {
		avl_free(refcounters);
		obj_free(refcounter_type);
		rc_delete_refcounter(rc);
		return;
	}
*/
	
	atexit(kill_refcounter);
	
	return;
}

/*
 * Finds the refcounter responsible for the data object pointed to by data or 
 * create such a one, if there isn't any instance for this object yet.
 *
 * Note: The refount of newly created refcounters is initialized to ZERO, not 
 * ONE. This means:
 *
 * a) It is ILLEGAL to call dec_refcount immediatelly after this function, if 
 *    you are not really sure, that there exists an instance already which had 
 *    inc_refcount called more often than dec_refcount previously.
 * b) If you call dec_refcount the same number of times as you called 
 *    inc_refcount at any time, then the object will be immediatelly destroyed.
 *
 * Return: The refcounter responsible for data or NULL on errors.
 *
 * AVL crossing:
 *
 * This function uses the AVL function avl_rc_find to retrieve the 
 * refcounter if it exits and avl_insert_rc if to add any new one to the AVL 
 * tree.
 */
struct refcounter * rc_get_refcounter_for(struct obj_type * type, const object_t * data) {
	struct refcounter * retval;
	
	assert(type);
	
	if (unlikely(!refcounters)) {
		// It's nasty that this has to be done on every call.
		rc_init();
		if (unlikely(!refcounters)) {
			errno = ENOMEDIUM;
			return NULL;
		}
	}
	
	btlock_lock(rc_lock);
	if ((retval=avl_rc_find(refcounters,data))) {
		/*
		 * Currently any refcounter is responsible for itself. This is 
		 * not fine but BCP.
		 */
		btlock_unlock(rc_lock);
		if (retval != (struct refcounter *)data) {
			assert(data == rc_get_object(retval));
		}
		return retval;
	}
	
	retval = rc_new_refcounter();
	if (!retval) {
		btlock_unlock(rc_lock);
		return NULL;
	}
	
	retval->refcount=0;
	retval->data=(void*)data;
	retval->type=type;
	
	if (!avl_insert_rc(refcounters,retval)) {
		int e = errno;
		rc_delete_refcounter(retval);
		btlock_unlock(rc_lock);
		errno = e;
		return NULL;
	}
	btlock_unlock(rc_lock);
	
	--retval->refcount; // avl_insert_rc has incremented this by one. Reverse that now.
	
	assert(data == rc_get_object(retval));
	
	return retval;
}

struct obj_type * rc_get_type(const struct refcounter * rc) {
	return rc->type;
}
