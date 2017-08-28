/*
 * btstr.h. Part of the bothie-utils.
 *
 * Copyright (C) 2006-2016 Bodo Thiesen <bothie@gmx.de>
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

#ifndef BTSTR_H
#define BTSTR_H

#include "bttypes.h"
#include "vasiar.h"

#ifdef __cplusplus
extern "C" {
#endif

struct lstring;

/*
 * Fügt n strings aneinander, und liefert den Zeiger auf ein neu ge-malloc()ten 
 * Speicherbereich zurück, der diesen zusammengefügten String enthält.
 */
/*
 * Function char* concat(int n,'n times:'char* str_i);
 * 
 * Allocates memory and copies the contents of str_i, beginning with the
 * content of str_0 and finnishing with the content of str_(n-1) to the
 * newly allocated memory.
 * 
 * The return value is the address of the new memory area. The latter
 * must be freed by the caller after use.
 */
char * concat(int n,...);

/*
 * Vergleicht das Ende des Strings s1 mit String s2.
 * Rückgabe von -1 wenn strlen(s1)<strlen(s2), ansonsten der Rückgabewert von 
 * strcmp im entsprechenden Bereich.
 */
int strendcmp(const char * s1,const char * s2);

/*
 * Vergleicht den Anfang des Strings s1 mit String s2.
 * Rückgabe von -1 wenn strlen(s1)<strlen(s2), ansonsten der Rückgabewert von 
 * strcmp im entsprechenden Bereich.
 */
int strstartcmp(const char * s1,const char * s2);

/*
 * Vertauscht die Inhalte der beiden Speicherbereiche.
 */
void memswap(void * s1,void * s2,size_t n);

/*
 * Kopiert (wie strcpy) einen string in einen zuvor ge-malloc()ten buffer.
 */
char * mstrcpy(const char * str);
#define mstrdup mstrcpy

/*
 * Kopiert (wie memcpy) einen buffer mit groesse bs in einen zuvor ge-malloc()ten buffer.
 */
char * mmemcpy(const char * b,size_t bs);

/*
 * Like strcpy but returns the pointer to the \0 byte copied to dst instead of 
 * the already known dst pointer itself.
 */
char * btstrcpy(char * dst,const char * src);

#define get_lstring get_lstring_reown1
#define get_lstring_reown1(s) get_lstring_reownX(x, true)
struct lstring * get_lstring_reownX(char * s, bool reown);
static inline struct lstring * get_lstring_reown0(const char * s) { return get_lstring_reownX((char*)s, false); }

void lstring_print_quoted(const struct lstring * ls);
char * lstring_mprint_quoted(const struct lstring * ls);
int lstrcmp(const struct lstring * ls1, const struct lstring * ls2);
int lstrncmp(const struct lstring * ls1, const struct lstring * ls2, size_t n);

struct lstring * lstrcpy(const struct lstring * ls);

/*
 * @func memdiff
 * @param a - first memory block to compare
 * @param b - second memory block to compare
 * @param sl - size of the two memory blocks to compare (or the size of the
 *             shorter one if they differ in size)
 * @return the offset of the first byte to differ
 *
 * @desc
 *
 * Compares the memory areas pointed to by a and b and returns the first byte
 * found to differ. If there are no differences, sl is returned.
 */
size_t memdiff(const void * a, const void * b, size_t sl);

/*
 * @func strdiff
 * @param a - first memory block to compare
 * @param b - second memory block to compare
 * @return the offset of the first byte to differ
 *
 * @desc
 *
 * Compares the strings pointed to by a and b and returns the first byte
 * found to differ. If there are no differences, strlen of both strings is
 * returned. If the string have different sizes, the size of the shorter
 * string is returned (i.e. the end of string marker will be considered as
 * "different" from the character in the other string at that position).
 */
size_t strdiff(const char * a, const char * b);

/*
 * @func strpos
 * @param haystack
 * @param needle
 * @return position of needle in haystack
 *
 * @desc
 *
 * Inspired by PHP's strpos this function returns the offset of the first
 * occurence of needle in haystack, returning -1 (cast to size_t) if needle
 * was not found.
 */
size_t strpos(const char * haystack, const char * needle);

/*
 * @func php_strpos
 * @param haystack
 * @param needle
 * @param offset
 * @return position of needle in haystack
 *
 * @desc
 *
 * Inspired by PHP's strpos this function returns the offset of the first
 * occurence of needle in haystack, returning -1 (cast to size_t) if needle
 * was not found. The search skips the first offset bytes in haystack but
 * returns the offset relative to the beginning of haystack (not relative to
 * offset).
 */
size_t php_strpos(const char * haystack, const char * needle, size_t offset);

/*
 * @func explode
 * @param separator
 * @param string
 * @return array of the substrings of "string" separated by separator.
 *
 * @desc
 *
 * Inspired by PHP's explode this function splits string on each occurence of
 * separator into substrings which are added to a newly malloc()ed VASIAR of
 * char *.
 * Like in PHP's explode function, an empty string will yield a non-empty
 * VASIAR with one empty string.
 */
vasiar_char_ptr * explode(const char * separator, const char * string);

/*
 * @func msubstr
 * @param str - the string to get the substring of
 * @param offset - the offset where to begin extracting characters
 * @param length - the length of the returned string
 * @return the substring
 * 
 * @desc
 * 
 * Inspired by PHP's substr, this function will return a copy of the specified
 * substring in a newly allocated buffer.
 * 
 * If offset is negative, it is first modified by adding strlen(str). If it
 * remains negative, NULL will be returned with errno set to ERANGE.
 * Otherwise, in any case offset now addresses the first character of the
 * string directly. If offset is past the end of the string (i.e. offset >
 * strlen(str)), NULL will be returned with errno set to ERANGE.
 * 
 * If length is negative, it addresses the first character from the end of
 * string not to copy any more. If the start of the string denoted by offset
 * lies behind the end of the string calculated by length, NULL will be
 * returned with errno set to ERANGE.
 * 
 * If length is non-negative, it is the number of characters to copy.
 * 
 * If length is bigger than strlen of the rest of the string, length will be
 * set to that strlen.
 * 
 * Hint: Since you cannot call this function without passing length, to get
 * the rest of the string, either pass strlen(str) or simply pass
 * SSIZE_MAX.
 */
char * msubstr(const char * str, ssize_t offset, ssize_t length);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // #ifndef BTSTR_H
