/*
 * parseutil_pwerror.c. Part of the bothie-utils.
 *
 * Copyright (C) 2004-2015 Bodo Thiesen <bothie@gmx.de>
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

#include "parseutil_internals.h"

#include <errno.h>
#include <string.h>

static char * _pwerror[PW_MAX + 1]={
	/*   0 - PW_NOERROR */ "Success",
	/*   1 - PW_EMPTY */ "Empty string",
	/*   2 - PW_INVAL_CHAR */ "Invalid character '%c'",
	/*   3 - PW_EOF */ "Reached end of string",
	/*   4 - PW_NEED_QUOTE */ "String should start with a quote character (\"), but it starts with '%c'",
	/*   5 - PW_LATE_QUOTE */ "String contains an unescaped quote character somewhere in the middle",
	NULL
};

const char * pwerror(int pw_errno) {
	static char buffer[
		5 + (
			sizeof _pwerror[PW_INVAL_CHAR] > sizeof _pwerror[PW_NEED_QUOTE]
			?  sizeof _pwerror[PW_INVAL_CHAR] : sizeof _pwerror[PW_NEED_QUOTE]
		)
	];
	if (pw_errno<0 || pw_errno>PW_MAX) {
		if (pw_errno==PW_ERRNO) {
			return strerror(errno);
		}
		snprintf(buffer, sizeof buffer, "PW_Error %i", pw_errno);
		return buffer;
	}
	switch (pw_errno) {
		case PW_INVAL_CHAR:
		case PW_NEED_QUOTE:
		/* update buffer declaration if you add more errors */
			snprintf(buffer, sizeof buffer, _pwerror[pw_errno], pwe_invalid_char);
			return buffer;
		default:
			return _pwerror[pw_errno];
	}
}
