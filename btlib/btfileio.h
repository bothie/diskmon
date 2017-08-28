/*
 * btfileio.h. Part of the bothie-utils.
 *
 * Copyright (C) 2006-2015 Bodo Thiesen <bothie@gmx.de>
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

#ifndef BTFILEIO_H
#define BTFILEIO_H

#include "bttypes.h"

#include <sys/types.h>

#ifdef __cplusplus
#include <cstdio>
#else // #ifdef __cplusplus
#include <stdio.h>
#endif // #ifdef __cplusplus, else

#ifndef STDIN
#define STDIN  STDIN_FILENO
#define STDOUT STDOUT_FILENO
#define STDERR STDERR_FILENO
#endif

#ifdef __cplusplus
extern "C" {
#endif

unsigned char * read_entire_file(int fd, size_t * size);
char * add_extension(const char * original_file_name,const char * suffix);

int fdprintf(int fd,const char * fmt,...);
int vfdprintf(int fd,const char * fmt,va_list ap);

char * freadline(FILE * stream);
char * fdreadline(int fd);

extern char * readline_prefix_buffer;
extern size_t readline_prefix_buffsize;
extern size_t readline_prefix_offset;
extern int    readline_fd;

struct btf;

/**
 * @function get_btf_by_fd
 * 
 * @param int fd - the file descriptor to be associated with the newly created 
 * btf context.
 * 
 * @return struct btf * - the newly created btf context on success, or NULL on 
 * error.
 * 
 * @desc
 * Allocates a new btf context, associates the file descriptor with it and 
 * returns the context.
 * 
 * The file descriptor MUST be readable.
 * 
 * @author Bodo Thiesen <bothie@gmx.de>
 */
struct btf * get_btf_by_fd(int fd);
#define btf_new_by_fd get_btf_by_fd

/**
 * @function get_btf_by_FILE
 * 
 * @param int fd - the stream pointer to be associated with the newly created 
 * btf context.
 * 
 * @return struct btf * - the newly created btf context on success, or NULL on 
 * error.
 * 
 * @desc
 * Allocates a new btf context, associates the stream with it and returns the 
 * context.
 * 
 * The stream MUST be readable.
 * 
 * @author Bodo Thiesen <bothie@gmx.de>
 */
struct btf * get_btf_by_FILE(FILE * file);
#define btf_new_by_FILE get_btf_by_FILE

/**
 * @function btf_creat
 * 
 * @param path - the path of the file to be opened / created
 * @param flags - the open flags
 * @param mode - the mode of the newly created file
 * 
 * @return struct btf * - the newly created btf context on success, or NULL on
 * error.
 * 
 * @desc
 * Essentially, this function does, what a call to the UNIX function open with 
 * the three arguments followed by a call to get_btf_by_fd would do, however 
 * in a way, which makes sure, the call to open doesn't take place if 
 * get_btf_by_fd would fail.
 * 
 * @author Bodo Thiesen <bothie@gmx.de>
 */
struct btf * btf_creat(const char * path,int flags,mode_t mode);

/**
 * @function btf_open
 * 
 * @param path - the path of the file to be opened / created
 * @param flags - the open flags
 * 
 * @return struct btf * - the newly created btf context on success, or NULL on
 * error.
 * 
 * @desc
 * Essentially, this function does, what a call to the UNIX function open with 
 * the two arguments plus 0666 as the mode argument followed by a call to 
 * get_btf_by_fd would do, however in a way, which makes sure, the call to 
 * open doesn't take place if get_btf_by_fd would fail.
 * 
 * @author Bodo Thiesen <bothie@gmx.de>
 */
static inline struct btf * btf_open(const char * path,int flags) {
	return btf_creat(path,flags,0666);
}

/**
 * @function btf_fopen
 * 
 * @param path - the path of the file to be opened / created
 * @param mode - the open flags
 * 
 * @return struct btf * - the newly created btf context on success, or NULL on
 * error.
 * 
 * @desc
 * Essentially, this function does, what a call to the ISO-C function fopen 
 * followed by a call to get_btf_by_FILE would do, however in a way, which makes 
 * sure, the call to fopen doesn't take place if get_btf_by_FILE would fail.
 * 
 * @author Bodo Thiesen <bothie@gmx.de>
 */
struct btf * btf_fopen(const char * path,char *mode);

/**
 * @function free_btf
 * 
 * @param struct btf * ctx - the context to be destroyed.
 * 
 * @return int - the return value of close/fclose: zero on success, -1 on 
 * error hinting to look in errno for the actual error
 * 
 * @desc
 * Destroys the btf context and frees the ressources associated with it, 
 * including the buffer, which may still contain data.
 * 
 * The actual file associated with this context will NOT be closed. However 
 * because of the nature of the buffering, it may be difficult to continue 
 * reading from that file via the standard read functions and get expected 
 * results.
 * 
 * @see freeclose_btf
 * 
 * @author Bodo Thiesen <bothie@gmx.de>
 */
int free_btf(struct btf * ctx);
#define btf_free free_btf

/**
 * @function freeclose_btf
 * 
 * @param struct btf * ctx - the context to be destroyed.
 * 
 * @return int - the return value of close/fclose: zero on success, -1 on 
 * error hinting to look in errno for the actual error
 * 
 * @desc
 * Like free_btf: Free the ressources associated with this btf contect. 
 * Additionally, close the underlaying file by a call to close or fclose 
 * respectively. This is a convenience function to save you the call to 
 * the close function.
 * 
 * Note: Even if this function reports an error, the btf context will have 
 * been destroyed on return.
 * 
 * @see free_btf
 * 
 * @author Bodo Thiesen <bothie@gmx.de>
 */
int freeclose_btf(struct btf * ctx);
#define btf_freeclose freeclose_btf

/**
 * @function btf_readline
 * 
 * @param struct btf * ctx - the btf context from which is to read. (It must 
 * have been created by a call of any of the get_btf_by_... functions).
 * 
 * @return string - The pointer to the newly malloc()'ed string countaining 
 * the line which was just read without the line end marker(s) or NULL if an 
 * error occured.
 * 
 * @desc
 * Reads at least[1] enough bytes from the underlaying file desctriptor/stream 
 * to return the next line. A line is everything separated by a CR a LF a CRLF 
 * or a LFCR. Detection works as following: If a CR or LF is read, a flag is 
 * set telling which one was encountered. If the next char is either the same 
 * or none of both, the end of line is assumed. Else, another flag is set. The 
 * next byte will trigger the mechanism now in any case.
 * 
 * Upon reaching EOF, if anything was read already, everything read so far 
 * will be returned and no error will be reported. Subsequent calls to this 
 * function will then return NULL and set errno to the library specific error 
 * code EREOF (Error: Reached End Of File).
 * 
 * [1] Currently, the implementation of this function is poor in the way, that 
 *     it reads one character at a time until the whole string (plus maybe the 
 *     first character behind it) has been read. However, it's planned to 
 *     replace this behavior by a much more sophisticated buffering mechanism.
 * 
 * @author Bodo Thiesen <bothie@gmx.de>
 * @file btfileio_btf_readline.c
 */
char * btf_readline(struct btf * ctx);

/**
 * @function btf_read
 * 
 * @param struct btf * ctx - the btf context from which is to read. (It must 
 * have been created by a call of any of the get_btf_by_... functions).
 * 
 * @param void * ptr - The pointer to the buffer for the data.
 * 
 * @param size_t count - The number of bytes to read to the buffer pointed to 
 * by ptr.
 * 
 * @return size_t - The positive (non null) number of bytes read or 0 if an 
 * error occured (reaching EOF before reading the first byte is considered an 
 * error by this function and hence return 0 with errno set to EREOF.)
 * 
 * @desc
 * This function tries to read count bytes and stores them to the buffer 
 * pointed to by ptr which's size therefore MUST be at least count bytes.
 * If an error occurs, 0 will be returned and errno set appropriately. On 
 * EOF, the return value will be the number of bytes read until EOF - if there 
 * was at least one byte read, 0 in conjunction with errno=EREOF otherwise.
 * 
 * @author Bodo Thiesen <bothie@gmx.de>
 * @file btfileio_btf_read.c
 */
size_t btf_read(struct btf * ctx,void * ptr,size_t count);

/*
 * @function btf_aquire
 * 
 * @param struct btf * ctx - the btf context for which to aquire additional 
 * data.
 * 
 * @param size_t count . The number of bytes to aquire
 * 
 * @return size_t - the number of bytes ready to read or 0 if either an error 
 * occured or not enough data are available yet.
 * 
 * @desc
 * This function tries to make sure that count bytes may be read atomically 
 * without accessing the underlaying stream. If more bytes are ready already, 
 * that number of bytes will be returned (i.e. btf_aquire(ctx,0) may be used 
 * to query the number of currently buffered bytes). If the underlaying stream 
 * doesn't provide any additional data either because of EOF or because of an 
 * other error, 0 will be returned and errno set appropriately (EREOF on eof). 
 * Additionally, 0 will be returned if data could be read not the requested 
 * number. In this case, errno will be set to EAGAIN, as it would, is NO data 
 * would have been read.
 */
size_t btf_aquire(struct btf * ctx,size_t count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // #ifndef BTFILEIO_H
