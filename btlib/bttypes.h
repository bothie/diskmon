/*
 * bttypes.h. Part of the bothie-utils.
 *
 * Copyright (C) 2002-2015 Bodo Thiesen <bothie@gmx.de>
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

#ifdef __cplusplus
#include <cstdbool>
#include <cstdint>
#else // #ifdef __cplusplus
#include <stdbool.h>
#include <stdint.h>
#endif // #ifdef __cplusplus, else

#ifndef BTTYPES_H
# define BTTYPES_H

                           /* Should be: */
typedef uint8_t  byte;     /*  8 Bits long */
typedef  int8_t  shortint; /*  8 Bits long */

typedef uint16_t word;     /* 16 Bits long */
typedef  int16_t integer;  /* 16 Bits long */

typedef uint32_t dword;    /* 32 Bits long */
typedef  int32_t longint;  /* 32 Bits long */

typedef uint64_t qword;    /* 64 Bits long */
typedef  int64_t comp;     /* 64 Bits long */

/*
 * XXX: Please ensure, that this typedefs are what they should be:
 *       *8 should be  8 bit wide,
 *      *16 should be 16 bit wide,
 *      *32 should be 32 bit wide,
 *      *64 should be 64 bit wide,
 *       s* should be signed,
 *       u* should be unsigned!
 *
 * If this typedefs produce anything else on your machine, feel free to add
 * some #ifdef-#endif-blocks here.
 */
typedef  int8_t   s8;
typedef uint8_t   u8;
typedef  int16_t s16;
typedef uint16_t u16;
typedef  int32_t s32;
typedef uint32_t u32;
typedef  int64_t s64;
typedef uint64_t u64;

#define HAVE_U64

# define elseif else if

# include "btmacros.h"

#endif /* #ifndef BTTYPES_H */
