#define _GNU_SOURCE

#include "common.h"
#include "bdev.h"

#include <btfileio.h>
#include <btlock.h>
#include <fcntl.h>
#include <errno.h>
#include <object.h>
#include <parseutil.h>
#include <set.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vasiar.h>

#define DRIVER_NAME "cache"

#define DEBUG_DATA_PTR_T

#define MAX_BLOCKS_CACHED 1024*1024 // 512 MB with 512 byte blocks

/*
 * Public interface
 */

// No special public functions defined

/*
 * Indirect public interface
 */
static block_t cache_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data);
static block_t cache_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data);

static bool cache_destroy(struct bdev * bdev);

struct bdev_private;

static bool cache_destroy_private(struct bdev_private * private);

static struct bdev * bdev_init(struct bdev_driver * bdev_driver, char * name, const char * args);

static bool initialized;

static struct obj_type * block_lookup_obj_type;

struct block_lookup_entry {
	block_t block;
	size_t old_entry;
	size_t new_entry;
};

int block_lookup_compare(const void * _e1, const void * _e2) {
	const struct block_lookup_entry * e1 = (struct block_lookup_entry *)_e1;
	const struct block_lookup_entry * e2 = (struct block_lookup_entry *)_e2;
	
	block_t block1 = e1->block;
	block_t block2 = e2->block;
	
	if (block1 < block2) {
		return -1;
	}
	
	if (block1 > block2) {
		return +1;
	}
	
	return 0;
}

BDEV_INIT {
	if (!(block_lookup_obj_type = obj_mk_type_1("block lookup", block_lookup_compare))
	||  !bdev_register_driver(DRIVER_NAME, &bdev_init)) {
		ERRORF("Couldn't register driver %s", DRIVER_NAME);
	} else {
		initialized=true;
	}
}

BDEV_FINI {
	if (initialized) {
		bdev_deregister_driver(DRIVER_NAME);
	}
	obj_free(block_lookup_obj_type);
}

/*
 * Implementation - everything non-public (except for init of course).
 */
struct entry {
	block_t block;
	unsigned char * data;
	unsigned long num_accesses;
};

struct bdev_private {
	struct bdev * backing_storage;
	char * cache_files_directory;
	struct btlock_lock * lock;
	
	struct cache {
		int info_fd;
		int data_fd;
		VASIAR(struct entry) data;
		char * info_file_name;
		char * data_file_name;
	} old, new;
	
	size_t next_read_index;
	size_t num_blocks_cached;
	
	struct set * block_lookup_set;
	VASIAR(block_t) first_access;
};

// cache:blah=\"backing_bdev\" \"cache_files_directory\"
//
// name will be reowned by init, args will be freed by the caller.
static struct bdev * bdev_init(struct bdev_driver * bdev_driver, char * name, const char * args) {
	char * info_file_name = NULL;
	char * data_file_name = NULL;
	
	struct bdev_private * private = zmalloc(sizeof(*private));
	if (!private) {
		ERRORF("%s:%s: bdev_init: malloc: %s", DRIVER_NAME, name, strerror(errno));
		goto err;
	}
	
	private->old.info_fd = -1;
	private->old.data_fd = -1;
	private->new.info_fd = -1;
	private->new.data_fd = -1;
	
	VAINIT(private->first_access);
	
	private->lock = btlock_lock_mk();
	if (!private->lock) {
		ERRORF("%s:%s: bdev_init: lock_mk: %s", DRIVER_NAME, name, strerror(errno));
		goto err;
	}
	
	unsigned line = -1;
	
	char * bdev_name;
	
	const char * cp = args;
	char c;
	
	c = parse_c_string(&cp, true, &bdev_name, NULL);
	if (c!=' ') { line=__LINE__; goto err_msg_open; }
	if (!parse_skip(&cp, ' ')) { line=__LINE__; goto err_msg_open; }
	c = parse_c_string(&cp, true, &private->cache_files_directory, NULL);
	if (c) { line=__LINE__; goto err_msg_open; }
	
	if (*cp) { line=__LINE__; goto err_msg_open; }
	
	private->backing_storage = bdev_lookup_bdev(bdev_name);
	if (!private->backing_storage) {
		ERRORF("%s:%s: Couldn't lookup bdev %s.", DRIVER_NAME, name, bdev_name);
		goto err;
	}
	
	struct stat stat_buffer;
	if (stat(private->cache_files_directory, &stat_buffer)) {
		ERRORF("%s:%s: bdev_init: Couldn't stat cache files directory %s: %s.", DRIVER_NAME, name, private->cache_files_directory, strerror(errno));
		if (errno != ENOENT) {
			goto err;
		}
		
		if (mkdir(private->cache_files_directory, 0777)) {
			ERRORF("%s:%s: bdev_init: Couldn't mkdir cache files directory %s: %s.", DRIVER_NAME, name, private->cache_files_directory, strerror(errno));
			goto err;
		}
	} else {
		if (!S_ISDIR(stat_buffer.st_mode)) {
			ERRORF("%s:%s: bdev_init: Object %s exists but is not a directory.", DRIVER_NAME, name, private->cache_files_directory);
			goto err;
		}
	}
	
	info_file_name              = mprintf("%s/info",     private->cache_files_directory);
	private->old.info_file_name = mprintf("%s/info.old", private->cache_files_directory);
	private->new.info_file_name = mprintf("%s/info.new", private->cache_files_directory);
	
	data_file_name              = mprintf("%s/data",     private->cache_files_directory);
	private->old.data_file_name = mprintf("%s/data.old", private->cache_files_directory);
	private->new.data_file_name = mprintf("%s/data.new", private->cache_files_directory);
	
	int r1, r2, e1, e2;
	
	r1 = rename(info_file_name, private->old.info_file_name); e1 = errno;
	r2 = rename(data_file_name, private->old.data_file_name); e2 = errno;
	
	if (e1 == ENOENT) r1 = 0;
	if (e2 == ENOENT) r2 = 0;
	
	if (r1 || r2) {
		if (r1) {
			ERRORF("%s:%s: Couldn't rename %s to %s: %s.", DRIVER_NAME, name, info_file_name, private->old.info_file_name, strerror(errno));
		}
		if (r2) {
			ERRORF("%s:%s: Couldn't rename %s to %s: %s.", DRIVER_NAME, name, data_file_name, private->old.data_file_name, strerror(errno));
		}
		goto err;
	}
	
	if (unlink(private->new.info_file_name) && errno != ENOENT) {
		NOTIFYF("%s:%s: New info file %s exists and can't be deleted: %s,", DRIVER_NAME, name, private->new.info_file_name, strerror(errno));
		goto err;
	}
	
	if (unlink(private->new.data_file_name) && errno != ENOENT) {
		NOTIFYF("%s:%s: New data file %s exists and can't be deleted: %s,", DRIVER_NAME, name, private->new.data_file_name, strerror(errno));
		goto err;
	}
	
	private->old.info_fd = open(private->old.info_file_name, O_RDONLY); e1 = errno;
	
	if (private->old.info_fd < 0 && e1 != ENOENT) {
		ERRORF("%s:%s: Couldn't open info file %s: %s.", DRIVER_NAME, name, private->old.info_file_name, strerror(e1));
		goto err;
	}
	
	private->old.data_fd = open(private->old.data_file_name, O_RDONLY); e2 = errno;
	
	if (private->old.data_fd < 0 && e2 != ENOENT) {
		ERRORF("%s:%s: Couldn't open data file %s: %s.", DRIVER_NAME, name, private->old.data_file_name, strerror(e2));
		goto err;
	}
	
	if (private->old.info_fd>=0 && private->old.data_fd<0) {
		ERRORF("%s:%s: Could open info file %s but couldn't open data file: %s.", DRIVER_NAME, name, private->old.info_file_name, strerror(e2));
		goto err;
	}
	
	if (private->old.info_fd<0 && private->old.data_fd>=0) {
		ERRORF("%s:%s: Could open data file %s but couldn't open info file: %s.", DRIVER_NAME, name, private->old.data_file_name, strerror(e1));
		goto err;
	}
	
	if (private->old.info_fd<0 /* this means also private->old.data_fd<0 */ && (e1 != ENOENT || e2 != ENOENT)) {
		goto err;
	}
	
	private->new.info_fd = open(private->new.info_file_name, O_WRONLY|O_CREAT|O_TRUNC, 0666); e1 = errno;
	
	if (private->new.info_fd<0) {
		ERRORF("%s:%s: Couldn't open new info file %s: %s.", DRIVER_NAME, name, private->new.info_file_name, strerror(e1));
		goto err;
	}
	
	private->new.data_fd = open(private->new.data_file_name, O_WRONLY|O_CREAT|O_TRUNC, 0666); e2 = errno;
	
	if (private->new.data_fd<0) {
		ERRORF("%s:%s: Couldn't open new data file %s: %s.", DRIVER_NAME, name, private->new.data_file_name, strerror(e2));
		goto err;
	}
	
	int info_file_size;
	unsigned char * info_file_data;
	if (private->old.info_fd != -1) {
		info_file_data = (unsigned char *)read_entire_file(private->old.info_fd, &info_file_size);
		close(private->old.info_fd);
		if (!info_file_data) {
			ERRORF("%s:%s: Couldn't read old info file %s: %s.", DRIVER_NAME, name, private->old.info_file_name, strerror(errno));
			goto err;
		}
	} else {
		info_file_size = 0;
		info_file_data = NULL;
	}
	
	private->block_lookup_set = set_mk(block_lookup_obj_type);
	
	int i = 0;
	int entry = 0;
	while ((int)(i + sizeof(block_t) + sizeof(unsigned long)) <= info_file_size) {
		struct entry * e = &VANEW(private->old.data);
		e->block = *(block_t *)(info_file_data + i); i += sizeof(block_t);
		e->data = NULL;
		e->num_accesses = *(unsigned long *)(info_file_data + i); i += sizeof(unsigned long);
		struct block_lookup_entry * ble = malloc(sizeof(*ble));
		ble->old_entry = entry++;
		ble->new_entry = -1;
		ble->block = e->block;
		if (!set_add(private->block_lookup_set, ble)) {
			ERRORF(
				"%s:%s: bdev_init: Couldn't add entry for block %llu to block lookup set."
				, DRIVER_NAME, name, (unsigned long long)ble->block
			);
			goto err;
		}
	}
	free(info_file_data);
	
	if (i != info_file_size) {
		NOTIFYF("%s:%s: Info file has bogus size %i. Something dividable by %i was expected.", DRIVER_NAME, name, info_file_size, (int)(sizeof(block_t) + sizeof(unsigned long)));
	}
	
	struct bdev * retval = bdev_register_bdev(
		bdev_driver,
		name,
		bdev_get_size(private->backing_storage),
		bdev_get_block_size(private->backing_storage),
		cache_destroy,
		private
	);
	
	if (unlikely(!retval)) {
		goto err;
	}
	
	bdev_set_read_function(retval, cache_read);
	bdev_set_write_function(retval, cache_write);
	
	free(info_file_name);
	free(data_file_name);
	free(bdev_name);
	
	return retval;

err_msg_open:
	assert(line != (unsigned)-1);
	ERRORF("%s:%s: looperr_init:%u: Couldn't open %s: %s.", DRIVER_NAME, name, line, args, (pwerrno==PW_ERRNO) ? strerror(errno) : pwerror(pwerrno));
err:
	free(info_file_name);
	free(data_file_name);
	free(bdev_name);
	
	if (private) {
		cache_destroy_private(private);
	}
	free(name);
	return NULL;
}

static bool cache_destroy_private(struct bdev_private * private) {
	btlock_lock_free(private->lock);
	
	for (size_t i = VASIZE(private->old.data); i--; ) {
		free(VAACCESS(private->old.data, i).data);
	}
	VAFREE(private->old.data);
	close(private->old.info_fd); free(private->old.info_file_name);
	close(private->old.data_fd); free(private->old.data_file_name);
	
	for (size_t i = VASIZE(private->new.data); i--; ) {
		free(VAACCESS(private->new.data, i).data);
	}
	VAFREE(private->new.data);
	close(private->new.info_fd); free(private->new.info_file_name);
	close(private->new.data_fd); free(private->new.data_file_name);
	
	free(private->cache_files_directory);
	
	set_free(private->block_lookup_set);
	
	free(private);
	
	return true;
}

static bool cache_destroy(struct bdev * bdev) {
	BDEV_PRIVATE(struct bdev_private);
	return cache_destroy_private(private);
}

#ifdef DEBUG_DATA_PTR_T
typedef struct {
#else // #ifdef DEBUG_DATA_PTR_T
typedef union {
#endif // #ifdef DEBUG_DATA_PTR_T, else
	      unsigned char * wptr;
	const unsigned char * rptr;
} data_ptr_t;

static bool access_one_block(
	struct bdev * bdev,
	struct bdev_private * private,
	block_t block,
	data_ptr_t data,
	bool shall_write
) {
	if (block >= bdev_get_size(bdev)) {
		WARNINGF("%s:%s: Attempt to access beyond end of device.", DRIVER_NAME, bdev_get_name(bdev));
		errno = EINVAL;
		return false;
	}
	
	if (shall_write) {
		block_t w = bdev_write(private->backing_storage, block, 1, data.rptr);
		
		if (w == 1) {
			// Can't do anything on error - so ignore errors
			unlink(private->old.info_file_name);
			unlink(private->old.data_file_name);
			// FIXME: If block is cached, kick it - both from in-memory cache and from info file stuff
		}
		
		return w == 1;
	}
	
	if (!private->cache_files_directory) {
		return bdev_read(private->backing_storage, block, 1, data.wptr) == 1;
	}
	
	unsigned bs = bdev_get_block_size(bdev);
	
	/*
	 * Please keep the lock call and the retval declaration together as 
	 * they are to make sure that no one jumps to "out" prior to lock()ing. 
	 * If somebody does, he should get an error or at least a warning 
	 * bypassing the initialisation of retval which is absolutely intendet 
	 * here.
	 */
	btlock_lock(private->lock);
	bool retval = false;
	
	struct block_lookup_entry ble_search;
	ble_search.block = block;
	struct block_lookup_entry * ble = set_find(private->block_lookup_set, &ble_search);
	if (!ble) {
		block_t r = bdev_read(private->backing_storage, block, 1, data.wptr);
		
		if (r != 1) {
			goto out;
		}
		
		retval = true;
		
		ble = malloc(sizeof(*ble));
		ble->block = block;
		ble->old_entry = -1;
		ble->new_entry = -1;
		
		if (!set_add(private->block_lookup_set, ble)) {
			ERRORF("%s:%s: Couldn't add block %llu (%p) to block lookup table: %s.", DRIVER_NAME, bdev_get_name(bdev), (unsigned long long)ble->block, (void *)ble, strerror(errno));
			free(ble);
			ble = set_find(private->block_lookup_set, &ble_search);
			unlink(private->new.info_file_name); close(private->new.info_fd); private->new.info_fd = -1;
			unlink(private->new.data_file_name); close(private->new.data_fd); private->new.data_fd = -1;
		}
	} else {
		if (ble->old_entry != (size_t)-1) {
			assert(VAACCESS(private->old.data, ble->old_entry).block == block);
			if (!VAACCESS(private->old.data, ble->old_entry).num_accesses) {
				ble->old_entry = -1;
				goto direct_read;
			}
			while (!VAACCESS(private->old.data, ble->old_entry).data) {
				if (private->num_blocks_cached == MAX_BLOCKS_CACHED) {
					ERRORF("%s:%s: Max blocks cached reached. Skipping cache for access to block %llu.", DRIVER_NAME, bdev_get_name(bdev), (unsigned long long)block);
					goto direct_read;
				}
				VAACCESS(private->old.data, private->next_read_index).data = malloc(bs);
				ssize_t r = read(private->old.data_fd, VAACCESS(private->old.data, private->next_read_index).data, bs);
				if (bs != r) {
					ERRORF("%s:%s: Couldn't read block from cache data file: %s", DRIVER_NAME, bdev_get_name(bdev), r >= 0 ? "Short read. Premature EOF?" : strerror(errno));
					VAACCESS(private->old.data, private->next_read_index).num_accesses = 0;
					long offset = bs * private->next_read_index;
					if (offset != lseek(private->old.data_fd, offset, SEEK_SET)) {
						ERRORF("%s:%s: Couldn't lseek to correct location after read error: %s.", DRIVER_NAME, bdev_get_name(bdev), strerror(errno));
						// Make sure subsequent read attemts will fail
						close(private->old.data_fd); private->old.data_fd = -1;
					}
					free(VAACCESS(private->old.data, private->next_read_index).data);
					VAACCESS(private->old.data, private->next_read_index).data = NULL;
				} else {
					++private->num_blocks_cached;
				}
				++private->next_read_index;
			}
			memcpy(data.wptr, VAACCESS(private->old.data, ble->old_entry).data, bs);
			retval = true;
			if (!--VAACCESS(private->old.data, ble->old_entry).num_accesses) {
				free(VAACCESS(private->old.data, ble->old_entry).data);
				VAACCESS(private->old.data, ble->old_entry).data = NULL;
				ble->old_entry = -1;
				--private->num_blocks_cached;
			}
		} else {
direct_read: ;
			block_t r = bdev_read(private->backing_storage, block, 1, data.wptr);
			
			if (r != 1) {
				goto out;
			}
			
			retval = true;
		}
	}
	
	struct entry * e;
	if (ble->new_entry == (size_t)-1) {
		ble->new_entry = VASIZE(private->new.data);
		e = &VANEW(private->new.data);
		e->block = block;
		e->data = NULL;
		e->num_accesses = 0;
		if (private->new.data_fd != -1) {
			ssize_t w = write(private->new.data_fd, data.wptr, bs);
			if (w != bs) {
				ERRORF("%s:%s: Couldn't write block to new data file: %s.", DRIVER_NAME, bdev_get_name(bdev), w >= 0 ? "Short write. Disk full?" : strerror(errno));
				unlink(private->new.info_file_name); close(private->new.info_fd); private->new.info_fd = -1;
				unlink(private->new.data_file_name); close(private->new.data_fd); private->new.data_fd = -1;
			}
		}
	} else {
		e = &VAACCESS(private->new.data, ble->new_entry);
	}
	
	long offset = (sizeof(block_t) + sizeof(unsigned long)) * ble->new_entry;
	if (e->num_accesses) {
		offset += sizeof(block_t);
	}
	if (offset != lseek(private->new.info_fd, offset, SEEK_SET)) {
		ERRORF("%s:%s: Seeking in new info file failed: %s.", DRIVER_NAME, bdev_get_name(bdev), strerror(errno));
		unlink(private->new.info_file_name); close(private->new.info_fd); private->new.info_fd = -1;
		unlink(private->new.data_file_name); close(private->new.data_fd); private->new.data_fd = -1;
	}
	if (!e->num_accesses) {
		ssize_t w = write(private->new.info_fd, &e->block, sizeof(block_t));
		if (w != sizeof(block_t)) {
			ERRORF("%s:%s: Couldn't write info entry for new block: %s.", DRIVER_NAME, bdev_get_name(bdev), w >= 0 ? "Short write. Disk full?" : strerror(errno));
			unlink(private->new.info_file_name); close(private->new.info_fd); private->new.info_fd = -1;
			unlink(private->new.data_file_name); close(private->new.data_fd); private->new.data_fd = -1;
			goto out;
		}
	}
	
	++e->num_accesses;
	
	ssize_t w = write(private->new.info_fd, &e->num_accesses, sizeof(unsigned long));
	if (w != sizeof(block_t)) {
		ERRORF("%s:%s: Couldn't write info entry for block: %s.", DRIVER_NAME, bdev_get_name(bdev), w >= 0 ? "Short write. Disk full?" : strerror(errno));
		unlink(private->new.info_file_name); close(private->new.info_fd); private->new.info_fd = -1;
		unlink(private->new.data_file_name); close(private->new.data_fd); private->new.data_fd = -1;
		goto out;
	}

out:
	btlock_unlock(private->lock);
	return retval;
}

static block_t access_blocks(
	struct bdev * bdev,
	block_t first, block_t num, data_ptr_t data_ptr,
	bool shall_write
) {
	BDEV_PRIVATE(struct bdev_private);
	
	unsigned bs = bdev_get_block_size(bdev);
	
	block_t retval;
	
	for (retval = 0; retval < num; ++retval) {
		if (unlikely(!access_one_block(bdev, private, first, data_ptr, shall_write))) {
			if (unlikely(retval)) {
				return retval;
			}
			return -1;
		}
		++first;
		if (shall_write) {
			data_ptr.rptr += bs;
		} else {
			data_ptr.wptr += bs;
		}
	}
	
	return retval;
}

static block_t cache_read(struct bdev * bdev, block_t first, block_t num, unsigned char * data) {
	data_ptr_t data_ptr;
#ifdef DEBUG_DATA_PTR_T
	data_ptr.rptr = NULL;
#endif // #ifdef DEBUG_DATA_PTR_T
	data_ptr.wptr = data;
	
	return access_blocks(bdev, first, num, data_ptr, false);
}

static block_t cache_write(struct bdev * bdev, block_t first, block_t num, const unsigned char * data) {
	data_ptr_t data_ptr;
#ifdef DEBUG_DATA_PTR_T
	data_ptr.wptr = NULL;
#endif // #ifdef DEBUG_DATA_PTR_T
	data_ptr.rptr = data;
	
	return access_blocks(bdev, first, num, data_ptr, true);
}
