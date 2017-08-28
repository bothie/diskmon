/*
 * parseutil.h. Part of the bothie-utils.
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

#ifndef PARSEUTIL_H
#define PARSEUTIL_H

#ifdef __cplusplus
#include <cstdbool>
#include <cstdio>
#else // #ifdef __cplusplus
#include <stdbool.h>
#include <stdio.h>
#endif // #ifdef __cplusplus, else

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus

#define PARSE_UPPERALPHA                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define PARSE_LOWERALPHA                "abcdefghijklmnopqrstuvwxyz"
#define PARSE_NUM                       "0123456789"
#define PARSE_NUMFRACT                  PARSE_NUM "."
#define PARSE_HEXNUM                    PARSE_NUM "abcdefABCDEFx"
#define PARSE_HEXNUMFRACT               PARSE_HEXNUMFRACT "abcdefABCDEFx"

#define PARSE_UPPERALPHAUNDERSCORE      PARSE_UPPERALPHA "_"
#define PARSE_UPPERALPHANUM             PARSE_UPPERALPHA PARSE_NUM
#define PARSE_UPPERALPHANUMUNDERSCORE   PARSE_UPPERALPHA PARSE_NUM "_"

#define PARSE_LOWERALPHAUNDERSCORE      PARSE_LOWERALPHA "_"
#define PARSE_LOWERALPHANUM             PARSE_LOWERALPHA PARSE_NUM
#define PARSE_LOWERALPHANUMUNDERSCORE   PARSE_LOWERALPHA PARSE_NUM "_"

#define PARSE_ALPHA                     PARSE_UPPERALPHA PARSE_LOWERALPHA
#define PARSE_ALPHAUNDERSCORE           "_" PARSE_ALPHA
#define PARSE_ALPHANUM                  PARSE_ALPHA PARSE_NUM
#define PARSE_ALPHANUMUNDERSCORE        PARSE_ALPHAUNDERSCORE PARSE_NUM

#define PARSE_WHITESPACE " \f\t\r\n\v"

#define is_digit(base,c) ( \
	(            c>='0' && c<='7') || \
	(base>=10 && c>='8' && c<='9') || \
	(base==16 && (c=='a' || c=='A' \
	||            c=='b' || c=='B' \
	||            c=='c' || c=='C' \
	||            c=='d' || c=='D' \
	||            c=='e' || c=='E' \
	||            c=='f' || c=='F')))

bool lineprocessfile(const char * filename,bool (*parseline)(const char *),int * line);

#define PW_ERRNO -1
#define PW_NOERROR 0
#define PW_EMPTY 1
#define PW_INVAL_CHAR 2
#define PW_EOF 3
#define PW_NEED_QUOTE 4
#define PW_LATE_QUOTE 5
#define PW_MAX PW_LATE_QUOTE

extern int pwerrno;
extern char pwe_invalid_char;

const char * pwerror(int pw_errno);

/*
 * Liest beginnend von source einen String, der aus mindestens einem Zeichen 
 * aus valid_first und beliebig vielen weiteren Zeichen aus valid_other 
 * besteht. Wenn dieser String leer ist, wird pwerrno auf PW_EMPTY gesetzt, 
 * und 0 zurückgeliefert. Sonst wird der so gefundene String in einen neuen 
 * String kopiert, dessen Adresse über destination zugewiesen wird. Stößt die 
 * Funktion auf ein ungültiges Zeichen, so wird pwerrno auf PW_INVAL_CHAR 
 * gesetzt. Bei Fehlern, die nicht im Einflußbereich von parse_string liegen 
 * (also malloc) wird pwerrno auf PW_ERRNO gesetzt, der eigentliche Fehlercode 
 * findet sich dann in errno.
 *
 * Der Rückgabewert ist 0, wenn der String destination ungültig ist, sonst ist 
 * er das Ende-Zeichen aus delimiters (und pwerrno ist 0).
 */
char parse_string(
	const char * * source,const char * delimiters,
	const char * valid_first,const char * valid_other,
	char * * destination
);

/*
 * Überspringt ein Zeichen des über fmt referierten String, wenn es ==c ist.
 *
 * Rückgabewert ist true, wenn das Zeichen c übersprungen wurde, sonst false.
 */
bool parse_skip(const char * * fmt,char c);

/*
 * Überspringt alle white spaces, also Leerzeichen (' '), Carriage Return 
 * ('\r'), Line Feed ('\n') und Tabulatoren (horizontale ('\t') als auch 
 * verhertikale (???)).
 *
 * Rückgabewert ist true, wenn mindestens ein Zeichen übersprungen wurde.
 */
bool parse_skip_ws(const char * * fmt);

/*
 * Überspringt den String substring in fmt. Rückgabewert true, wenn der 
 * String komplett übersprungen wurde, sonst false. Hinweis: Diese Funktion 
 * arbeitet atomisch. D.h. wenn der Rückgabewert false ist, wurde garnichts 
 * übersprungen, selbst wenn der Unterschied z.B. erst im letzten Zeichen 
 * auftrat.
 */
bool parse_skip_string(const char * * fmt,const char * substring);

/*
 * Parses a string like those of the C programming language. If need_quote 
 * is set to true, the string must start with a quote ("). The string will 
 * be written to the newly malloc()ed memory whiches pointer is written to 
 * destination (which may NOT be NULL) and it's length will be written to 
 * the variable pointed to by destination_length, if destination_length is 
 * not NULL.
 *
 * Return value:
 * 	0 if an error occured (error code in pwerror)
 * 		PW_NEED_QUOTE:
 * 			If need_quote is true, but first character is not a
 * 			literal quote '"'
 * 		PW_LATE_QUOTE:
 * 			Returned upon hitting an unmatched quote if the first
 * 			character was not a literal quote (and need_quote was
 * 			false, because if it was true, PW_NEED_QUOTE would
 * 			have been returned). If the first character was a
 * 			literal quote, then another literal quote denotes end
 * 			of string and thus represents a successful parse of a
 * 			C string literal.
 * 		PW_INVAL_CHAR:
 * 			The last character was a backslash (not escaped by a
 * 			previous backslash - pwe_invalid_char will be zero
 * 			then) or any character following a backslash which
 * 			doesn't make up a valid backslash sequence
 * 			(pwe_invalid_char will be that character then).
 * 		PW_EOF:
 * 			The string started with a literal quote (regardless
 * 			of need_quote) but didn't end with one.
 * 			-> Reaching the end of the string is NOT considered
 * 			an error, if AND ONLY IF BOTH a) need_quote is false
 * 			AND b) the first character was not a literal quote
 * 			ARE TRUE.
 * 		PW_ERRNO:
 * 			malloc failed (with errno most probably ENOMEM)
 * 	else it's the first value not meant to be part of the C string (which
 * 	may perfectly be 0 as well, then with pwerrno == PW_NOERROR == 0).
 * 	
 * 	If need_quote is true OR the first character was a quote, it's the
 * 	character following the ending quote.
 * 	
 * 	If need_quote is false AND the first character was not a quote, the
 * 	return value is always zero, either with a success code PW_NOERROR
 * 	in pwerror or because an error occured.
 */
char parse_c_string(
	const char * * source,
	bool need_quote,
	char * * destination,size_t * destination_length
);

/*
 * @deprecated
 */
bool parse_ulong(const char * * p,unsigned long * ul);

/*
 * Parses an unsigned long value stopping at the first character which can't 
 * be transformed. Returns that character.
 */
char parse_unsigned_long(const char * * p,unsigned long * target);

#ifdef NEED_LONG_LONG
/*
 * Like parse_unsigned_long but for unsigned long long
 */
char parse_unsigned_long_long(const char * * p,unsigned long long * target);
#endif // #ifdef NEED_LONG_LONG

#ifdef __cplusplus

} /* extern "C" { */

#endif /* #ifdef __cplusplus */

#endif /* #ifndef PARSEUTIL_H */
