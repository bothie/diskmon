/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "common.h"
#include "bdev.h"

#include <btstr.h>
#include <errno.h>
#include <parseutil.h>

#define DRIVER_NAME "mirror"

/*
 * Public interface
 */

/*
 * Indirect public interface (via struct bdev)
 */

static bool mirror_destroy(struct bdev * bdev);

static block_t mirror_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data, const char * reason);
/*
static block_t mirror_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data);
*/
static bool mirror_destroy(struct bdev * bdev);
/*
static block_t mirror_short_read(struct bdev * bdev, block_t first, block_t num, u8 * data, u8 * error_map, const char * reason);
*/

static struct bdev * mirror_init(struct bdev_driver * bdev_driver, char * name, const char * args);

static bool initialized;

BDEV_INIT {
	if (!bdev_register_driver(DRIVER_NAME, &mirror_init)) {
		eprintf("Couldn't register driver %s", DRIVER_NAME);
	} else {
		initialized = true;
	}
}

BDEV_FINI {
	if (initialized) {
		bdev_deregister_driver(DRIVER_NAME);
	}
}

/*
 * Implementation - everything non-public (except for init of course).
 */

struct bdev_private {
	size_t num_bdevs;
	struct bdev * * bdev;
	char * name;
	block_t size;
	unsigned bs;
};

static struct bdev * usage() {
	ERROR("Syntax: \"device1\"[ \"device2\"[...]]");
	return NULL;
}

// <dev1>
static struct bdev * mirror_init(struct bdev_driver * bdev_driver, char * name, const char * args) {
	const char * p = args;
	struct bdev_private * private = malloc(sizeof(*private));
	if (!private) {
		return NULL;
	}
	private->bdev = NULL;
	private->num_bdevs = 0;
	private->bs = 0;
	private->size = 0;
	for (;;) {
		char * bdev_name;
		char c = parse_c_string(&p, true, private->bdev ? &bdev_name : NULL, NULL);
		if (private->bdev) {
			private->bdev[private->num_bdevs] = bdev_lookup_bdev(bdev_name);
			if (!private->bdev[private->num_bdevs]) {
				ERRORF("Couldn't lookup device (%s).", bdev_name);
				free(bdev_name);
				free(private->bdev);
				free(private);
				free(name);
				return NULL;
			}
			if (!private->bs) {
				private->bs = bdev_get_block_size(private->bdev[private->num_bdevs]);
				if (!private->bs) {
					ERRORF("Blocksize of device %s is 0!", bdev_name);
					free(bdev_name);
					free(private->bdev);
					free(private);
					free(name);
					return NULL;
				}
				private->size = bdev_get_size(private->bdev[private->num_bdevs]);
			} else {
				if (private->bs != bdev_get_block_size(private->bdev[private->num_bdevs])) {
					ERROR("Blocksizes don't match!");
					free(bdev_name);
					free(private->bdev);
					free(private);
					free(name);
					return NULL;
				}
				block_t size = bdev_get_size(private->bdev[private->num_bdevs]);
				if (size != private->size) {
					WARNING("Devices have different sizes, limiting to the smallest device size.");
				}
				if (size < private->size) {
					private->size = size;
				}
			}
			free(bdev_name);
		}
		++private->num_bdevs;
		if (!c) {
			if (pwerrno) {
				return usage();
			}
			if (private->bdev) {
				break;
			} else {
				private->bdev = malloc(sizeof(*private->bdev) * private->num_bdevs);
				if (!private->bdev) {
					ERRORF("malloc: %s", strerror(errno));
					free(private);
					free(name);
					return NULL;
				}
				p = args;
				private->num_bdevs = 0;
				continue;
			}
		}
		bool ok = false;
		while (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			ok = true;
			parse_skip(&p, c);
			c = *p;
		}
		if (!ok || c!='"') {
			return usage();
		}
	}
	struct bdev * retval=bdev_register_bdev(
		bdev_driver,
		name,
		private->size,
		private->bs,
		mirror_destroy,
		private
	);
	if (!retval) {
		ERRORF("Couldn't register %s", name);
		free(private->bdev);
		free(private);
		free(name);
		return NULL;
	}
	
	bdev_set_read_function(retval, mirror_read);
	
	return retval;
}

bool mirror_destroy(struct bdev * bdev) {
	struct bdev_private * private = bdev_get_private(bdev);
	free(private->bdev);
	free(private);
	return true;
}

block_t mirror_read(struct bdev * bdev, block_t first, block_t num, u8 * data, const char * reason) {
	struct bdev_private * private = bdev_get_private(bdev);
	
	if (first + num > private->size || first + num < first) {
		ERRORF("%s: Attempt to access beyond end of partition.", private->name);
		if (first < private->size) {
			num = private->size - first;
		} else {
			return 0;
		}
	}
	block_t retval = 0;
	for (size_t i = 0; i < private->num_bdevs; ++i) {
		block_t r;
		if (!i) {
			r = bdev_read(private->bdev[i], first, num, data, reason);
		} else {
			r = bdev_read(private->bdev[i], first,   1, data, reason);
			if (r) i = -1;
		}
		retval += r;
		if (!(num -= r)) {
			return retval;
		}
		first += r;
		data += r * private->bs;
	}
	return retval;
}
