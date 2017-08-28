/*
 * avl_insert_rc.c. Part of the bothie-utils.
 *
 * Copyright (C) 2013-2015 Bodo Thiesen <bothie@gmx.de>
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

#include "bterror.h"

bool avl_insert_rc(struct avl_tree * tree, struct refcounter * rc_key) {
	union entry entry;
	
	entry.rc = rc_key;
	
	return avl_do_insert(tree, entry, NULL);
}
