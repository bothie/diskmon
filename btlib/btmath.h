/*
 * btmath.h. Part of the bothie-utils.
 *
 * Copyright (C) 2005 Bodo Thiesen <bothie@gmx.de>
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

#ifndef BTMATH_H
#define BTMATH_H

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus

int btrand(int num);

/*
 * Return the lowest of the given values.
 *
 * RETURN CODE
 *
 * This function returns the lowest/highest given values.
 *
 * ERROR CODES
 *
 * There are no error conditions defined.
 */
int min2(int a,int b);
int min3(int a,int b,int c);
int min4(int a,int b,int c,int d);
int min5(int a,int b,int c,int d,int e);

int max2(int a,int b);
int max3(int a,int b,int c);
int max4(int a,int b,int c,int d);
int max5(int a,int b,int c,int d,int e);

#ifdef __cplusplus
} // extern "C" {
#endif // #ifdef __cplusplus

#endif // #ifndef BTMATH_H
