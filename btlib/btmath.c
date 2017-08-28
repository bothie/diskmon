/*
 * btmath.c. Part of the bothie-utils.
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

#include "btmath.h"

#include <stdlib.h>

int btrand(int num) {
	int perioden=RAND_MAX/num;
	int max_accept=perioden*num-1;
	int r;
	
	while ((r=rand())>max_accept) ;
	
	return r%num;
}

int min2(int a,int b) {
	return (a<b)?a:b;
}

int min3(int a,int b,int c) {
	return min2(min2(a,b),c);
}

int min4(int a,int b,int c,int d) {
	return min2(min2(a,b),min2(c,d));
}

int min5(int a,int b,int c,int d,int e) {
	return min2(min2(a,b),min3(c,d,e));
}

int max2(int a,int b) {
	return (a>b)?a:b;
}

int max3(int a,int b,int c) {
	return max2(max2(a,b),c);
}

int max4(int a,int b,int c,int d) {
	return max2(max2(a,b),max2(c,d));
}

int max5(int a,int b,int c,int d,int e) {
	return max2(max2(a,b),max3(c,d,e));
}

