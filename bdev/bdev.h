/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#ifndef BDEV_H
#define BDEV_H

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

#define BDEV_INIT __attribute__((constructor)) static void init(void) 
#define BDEV_FINI __attribute__((destructor)) static void fini(void) 

#ifndef zmalloc
#define zmalloc(size) calloc(1,size)
#endif // #ifndef zmalloc

/*
 * This is a little bit ugly, but it should work:
 * The drivers register a private argument which is passed back to their 
 * specific read/write etc. functions unchanged. This allows a driver to 
 * support more than one block device at the same time, because all 
 * neccessary data can be stored in a dynamically allocated struct whiches 
 * address is then passed as private argument. However, the function 
 * prototypes can't know the type of the struct, so normally one would now 
 * fall back to passing a 'void *'. That in turn forces every single 
 * function to cast it back to it's respective pointer type. To prevent 
 * this, we just declare a struct bdev_private, which in turn may be
 * defined by the drivers locally to their needs. Because that struct is
 * private to any single driver, it doesn't have to be defined in any
 * public header file so no conflict by different definitions between the 
 * drivers will occur.
 */
struct bdev_private;

typedef off_t block_t;

struct bdev;

struct bdev;
struct bdev_driver;

typedef struct bdev * (*bdev_driver_init_function)(struct bdev_driver * bdev_driver,char * name,const char * args);

struct bdev_driver {
	const char * name;
	bdev_driver_init_function init;
};

struct bdev_driver * bdev_register_driver(const char * name,bdev_driver_init_function init);
struct bdev_driver * bdev_lookup_driver(const char * name);
void bdev_deregister_driver(const char * name);

#include "bdev_autogen.h"

/*
#define FUNCTION(func_name,func_rettype,func_args) \
	typedef func_rettype (*bdev_ ## func_name ## _function)(void * _private func_args); \
	bdev_ ## func_name ## _function bdev_register_ ## func_name ## _function(struct bdev_driver * driver,bdev_ ## func_name ## _function new_ ## func_name ## _function) \
	func_rettype bdev_ ## func_name ## (struct bdev * dev func_args);

#include "bdev_funcs.h"

#undef FUNCTION
*/

struct bdev * bdev_register_bdev(
	struct bdev_driver * bdev_driver,
	char * name,
	block_t size,
	unsigned block_size,
	bdev_destroy_function destroy,
	struct bdev_private * private
);

bool bdev_rename_bdev(
	struct bdev * bdev,
	char * new_name
);

struct bdev * bdev_lookup_bdev(const char * bdev_name);

const char * bdev_get_name(const struct bdev * bdev);
block_t bdev_get_size(const struct bdev * bdev);
unsigned bdev_get_block_size(const struct bdev * bdev);

struct bdev_private * bdev_get_private(const struct bdev * bdev);
#define BDEV_PRIVATE(type) type * private = (type *)bdev_get_private(bdev)

#endif // #ifndef BDEV_H
