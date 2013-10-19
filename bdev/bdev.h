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
	void * private
);

struct bdev * bdev_lookup_bdev(const char * bdev_name);

const char * bdev_get_name(const struct bdev * bdev);
block_t bdev_get_size(const struct bdev * bdev);
unsigned bdev_get_block_size(const struct bdev * bdev);

void * bdev_get_private(const struct bdev * bdev);
#define BDEV_PRIVATE(type) type * private = (type *)bdev_get_private(bdev)

#endif // #ifndef BDEV_H
