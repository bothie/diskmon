/*
 * object.h. Part of the bothie-utils.
 *
 * Copyright (C) 2005-2007 Bodo Thiesen <bothie@gmx.de>
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

#ifndef BTLINUXLIBRARY_OBJECT_T_H
#define BTLINUXLIBRARY_OBJECT_T_H

/*
 * Allow type safety by defining object_t to whatever object type you want to 
 * manage.
 * E.g. you have a struct something. then
 * #define object_t struct something
 * All functions then should be declared to operate on object_t * or struct 
 * something *. This way, mixing pointers can be detected and will at least 
 * cause a compiler warning.
 * Default is object_t == void which literally switches off any type checking.
 * There is no functional change upon definition of object_t.
 */
#ifndef object_t
#define object_t void
#endif // #ifndef object_t

#endif // #ifndef BTLINUXLIBRARY_OBJECT_T_H
