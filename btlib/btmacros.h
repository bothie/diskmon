/*
 * btmacros.h. Part of the bothie-utils.
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

#ifndef BTMACROS_H
#define BTMACROS_H

// #include "bterror.h" <--- moved to end of file to resolve cyclic dependency between btmacros.h and bterror.h

#include "bttypes.h"

#ifdef __cplusplus
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#else
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#endif

#define MALLOC(var, type, num) do { \
	type * * var_ptr = &(var); \
	size_t my_num = sizeof(type) * (size_t)(num); \
	 \
	if (!my_num) my_num = 1; \
	*var_ptr = (type *)malloc(my_num); \
	if (!*var_ptr) { \
		eexit( \
			"%s:%s:%i: malloc(%llu): " \
			, __FILE__, __func__, __LINE__ \
			, (unsigned long long)my_num \
		); \
	} \
} while (0)

#define REALLOC(var, type, num) do { \
	type * * var_ptr = &(var); \
	type *   old_ptr = *var_ptr; \
	int      my_num = sizeof(type) * (num); \
	 \
	if (!my_num) my_num = 1; \
	*var_ptr = (type *)realloc(old_ptr, my_num); \
	if (!*var_ptr && my_num) { \
		eexit( \
			"%s:%s:%i: realloc(%p,%i): " \
			, __FILE__, __func__, __LINE__ \
			, (void*)old_ptr \
			, my_num \
		); \
	} \
} while (0)

#define FREE(var) do { \
	if (var) { \
		free(var); \
		var = NULL; \
	} \
} while (0)

#define ZMALLOC(var, type, num) do { \
	type * * var_ptr = &(var); \
	int      num_arg = (num); \
	int      my_num = sizeof(type) * num_arg; \
	int      i; \
	 \
	if (!my_num) my_num = 1; \
	*var_ptr = (type *)malloc(my_num); \
	if (!*var_ptr) { \
		eexit( \
			"%s:%s:%i: malloc(%i): " \
			, __FILE__, __func__, __LINE__ \
			, my_num \
		); \
	} \
	 \
	for (i = num_arg; i--; ) { \
		(*var_ptr)[i] = 0; \
	} \
} while (0)

#define IMALLOC(var, type, num, initval) do { \
	type * * var_ptr     = &(var); \
	int      num_arg     = (num); \
	int      my_num      = sizeof(type) * num_arg; \
	type     my_init_val = initval; \
	int      i; \
	 \
	if (!my_num) my_num = 1; \
	*var_ptr = (type *)malloc(my_num); \
	if (!*var_ptr) { \
		eexit( \
			"%s:%s:%i: malloc(%i): " \
			, __FILE__, __func__, __LINE__ \
			, my_num \
		); \
	} \
	 \
	for (i = num_arg; i--; ) { \
		(*var_ptr)[i] = my_init_val; \
	} \
} while (0)

#define MALLOC2D(var, type, num1, num2) do { \
	type * * * MALLOC2D_var_ptr = &(var); \
	int        n1 = (num1), n2 = (num2), MALLOC2D_i; \
	 \
	MALLOC((*MALLOC2D_var_ptr), type *, n1); \
	for (MALLOC2D_i = n1; MALLOC2D_i--; ) { \
		MALLOC((*MALLOC2D_var_ptr)[MALLOC2D_i], type, n2); \
	} \
} while (0)

#define ZMALLOC2D(var, type, num1, num2) do { \
	type * * * ZMALLOC2D_var_ptr = &(var); \
	int        n1 = (num1), n2 = (num2), ZMALLOC2D_i; \
	 \
	MALLOC((*ZMALLOC2D_var_ptr), type *, n1); \
	for (ZMALLOC2D_i = n1; ZMALLOC2D_i--; ) { \
		ZMALLOC((*ZMALLOC2D_var_ptr)[ZMALLOC2D_i], type, n2); \
	} \
} while (0)

#define IMALLOC2D(var, type, num1, num2, initval) do { \
	type * * * IMALLOC2D_var_ptr = &(var); \
	int        n1 = (num1), n2 = (num2), IMALLOC2D_i; \
	type       IMALLOC2D_init_val = initval; \
	 \
	MALLOC((*IMALLOC2D_var_ptr), type *, n1); \
	for (IMALLOC2D_i = n1; IMALLOC2D_i--; ) { \
		IMALLOC((*IMALLOC2D_var_ptr)[IMALLOC2D_i], type, n2, IMALLOC2D_init_val); \
	} \
} while (0)

#define READ(fd, var) do { \
	if (read(fd, &var, sizeof(var)) != sizeof(var)) \
		eexit( \
			"%s:%s:%i: read(%i,%p,%i): " \
			, __FILE__, __func__, __LINE__ \
			, fd \
			, &var \
			, sizeof(var) \
		); \
} while (0)

#define READARRAY(fd, var, num) do { \
	if (read(fd, var, sizeof(*var) * num) != sizeof(*var) * num) \
		eexit( \
			"%s:%s:%i: read(%i,%p,%i): " \
			, __FILE__, __func__, __LINE__ \
			, fd \
			, var \
			, sizeof(*var) * num \
		); \
} while (0)

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SYM_CAT_H
#undef CONCAT2
#undef CONCAT3
#undef CONCAT4
#warning you included (directly or indirectly) symcat.h which defines just needless macros but which interferes with btmacros.h. If you experience problems with the symbols CONCAT2, CONCAT3 or CONCAT4, then you have a *real* problem now. If this is the case, then please include btmacros.h BEFORE symcat.h to get rid of the problems. However, you will not be able to use the functions CONCAT[234] from btmacros.h then.
#endif // #ifdef SYM_CAT_H

char * CONCAT1(const char * param1);
char * CONCAT2(const char * param1,const char * param2);
char * CONCAT3(const char * param1,const char * param2,const char * param3);
char * CONCAT4(const char * param1,const char * param2,const char * param3,const char * param4);

#ifdef __cplusplus
} /* extern "C" */
#endif

/*
#define CONCAT1(param1) ({\
	char * RC=concat(1,param1);\
	if (!RC)\
		eexit( \
			"%s:%s:%i: concat: " \
			, __FILE__, __func__, __LINE__ \
		); \
	RC;\
})

#define CONCAT2(param1,param2) ({\
	char * RC=concat(2,param1,param2);\
	if (!RC)\
		eexit( \
			"%s:%s:%i: concat: " \
			, __FILE__, __func__, __LINE__ \
		); \
	RC;\
})

#define CONCAT3(param1,param2,param3) ({\
	char * RC=concat(3,param1,param2,param3);\
	if (!RC)\
		eexit( \
			"%s:%s:%i: concat: " \
			, __FILE__, __func__, __LINE__ \
		); \
	RC;\
})

#define CONCAT4(param1,param2,param3,param4) ({\
	char * RC=concat(4,param1,param2,param3,param4);\
	if (!RC)\
		eexit( \
			"%s:%s:%i: concat: " \
			, __FILE__, __func__, __LINE__ \
		);\
	RC;\
})
*/

#define FORK(var) do { \
	int FORK_var; \
	FORK_var = fork(); \
	if (FORK_var == -1) \
		eexit( \
			"%s:%s:%i: fork(): " \
			, __FILE__, __func__, __LINE__ \
		); \
	var = FORK_var; \
} while (0)

#define NEWRETVAL() do { if (!(retval = malloc(sizeof(*retval)))) return NULL; } while (0)
#define RETVAL(type) type * retval; do { if (!(retval = malloc(sizeof(*retval)))) return NULL; } while (0)

static inline bool bool_free(/*@only@*/ void * mem) { free(mem); return true; }

#ifdef __cplusplus

template <class I> const I * ignore(const I &i) { return &i; }

template <typename T> T * malloc(size_t nmemb) {
	return static_cast<T *>(malloc(sizeof(T) * nmemb));
}

template <typename T> T * realloc(T * ptr, size_t nmemb) {
	return static_cast<T *>(realloc(static_cast<void *>(ptr), sizeof(T) * nmemb));
}

template <typename T> void free(T * ptr) {
	free(static_cast<void *>(ptr));
}

class fclose_on_leave {
private:
	FILE * f;

public:
	fclose_on_leave(FILE * file) { f=file; }
	~fclose_on_leave() { if (f) fclose(f); }
	void close_now() { if (f) fclose(f); f=NULL; }
};

#else /* #ifdef __cplusplus */

#define ignore(i) real_ignore(&i)

static inline /*@temp@*/ volatile const void * real_ignore(volatile const void * i) { return i; }

#endif /* #ifdef __cplusplus, else */

#define never_reached 0 // May be used like "assert(never_reached);"

extern char * null_pointer;
static inline void assert_never_reached() __attribute__ ((noreturn));

static inline void assert_never_reached() {
	volatile char c = *null_pointer;
	ignore(*(char*)&c);
	assert(false);
	exit(42);
}

#define assert_never_reached() do { \
	eprintf( \
		"%s:%s:%i: assert_never_reached() was invoked. The program will now terminate - possibly with SIGSEGV. This is intendet behavior.\n" \
		, __FILE__, __func__, __LINE__ \
	); \
	assert_never_reached(); \
} while (0)

#define expect(x,y) __builtin_expect(!!(x), (y))

#define   likely(x) expect((x), 1)
#define unlikely(x) expect((x), 0)

#define PRINTF_FORMAT(fmt, elip) __attribute__((__format__(__printf__, fmt, elip)))

/*
 * TRY/END_TRY macros. These macros are designed to ease the commonly known
 * step by step ressource-allocation/test-for-success/cleanup-on-failure paths 
 * used in many initialisation functions.
 * Use this macros like this:
 * 
 * 	TRY(char * filename=mprintf("...",...),filename,free(filename),eprintf("mprintf: %s",stderror(errno)))
 * 	TRY(int fd=open(filename,O_RDONLY),fd>=0,close(fd),eprintf("open: %s",stderror(errno)))
 * 	... do something with fd
 * 	END_TRY();
 * 	END_TRY();
 * 	// At this point, fd has been closed and filename has been freed, 
 * 	// regardless wether the steps initially succeeded or not.
 *
 * Additionally, there is a CATCH macro to implement a catch block:
 *
 * TRY(...) { ... } CATCH { ... } END_TRY
 */
#define TRY(action, test, cleanup, error) \
do { \
	bool bt_linux_library_try_catch; \
	action; \
	bt_linux_library_try_catch = (test); \
	if (!bt_linux_library_try_catch) { \
		error; \
	} \
	if (bt_linux_library_try_catch) { \
		while (true) { \
			if (!bt_linux_library_try_catch) { \
				cleanup; \
				break; \
			} \
			bt_linux_library_try_catch = false;

#define CATCH \
		} \
	} else { \
		bt_linux_library_try_catch = true; \
		while (bt_linux_library_try_catch) { \
			bt_linux_library_try_catch = false;

#define END_TRY \
		} \
	} \
} while (0)

#ifndef __cplusplus

// #define offsetof(type, member) ((char*)&((type*)0)->member-(char*)0)
#define alignof(type) offsetof(struct { char c; type member; }, member)

#endif // #ifndef __cplusplus

static inline const unsigned char * ccp2cucp(const char * p) {
	return (const unsigned char *)p;
}

static inline const char * cucp2ccp(const unsigned char * p) {
	return (const char *)p;
}

static inline unsigned char * cp2ucp(char * p) {
	return (unsigned char *)p;
}

static inline char * ucp2cp(unsigned char * p) {
	return (char *)p;
}

/*
 * C++ has namespace std { template<T> T min(T a, Tb); } and stuff like that,
 * so omit the following functions for C++
 */
#ifndef __cplusplus

static inline size_t min_size_t(size_t a, size_t b) {
	if (a < b) {
		return a;
	} else {
		return b;
	}
}

static inline size_t max_size_t(size_t a, size_t b) {
	if (a > b) {
		return a;
	} else {
		return b;
	}
}

#endif // #ifndef __cplusplus

static inline const char * bool_str(bool x) { return x ? "true" : "false"; }

/*
 * warning: format '%p' expects argument of type 'void *', but argument x has type 'some_other *' [-Wformat=]
 *
 * -> call printf-like functions with void_ptr(some_other_ptr). It just returns the same pointer
 *    casted to void * with type checking, which you would loose by writing (void *)some_other_ptr
 */
static inline void * void_ptr(void * ptr) {
	return ptr;
}

#define braced_group(x) __extension__({x})

#include "bterror.h"

#define UNIQUE_HELPER_HELPER(x, y) x ## y
#define UNIQUE_HELPER(x, y) UNIQUE_HELPER_HELPER(x, y)
#define UNIQUE(x) UNIQUE_HELPER(x ## _, __LINE__)

#endif /* #ifndef BTMACROS_H */
