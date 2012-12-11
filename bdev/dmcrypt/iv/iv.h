#ifndef IV_H
#define IV_H

#include "bdev.h"
#include "crypt.h"

#include <stdbool.h>
#include <sys/types.h>

#define IV_INIT __attribute__((constructor)) static void init(void) 
#define IV_FINI __attribute__((destructor)) static void fini(void) 

#ifndef zmalloc
#define zmalloc(size) calloc(1,size)
#endif // #ifndef zmalloc

// typedef void (*dm_crypt_iv_use_function)(struct crypt_alg_ctx * ctx,block_t block_number);
typedef void (*dm_crypt_iv_generate_function)(void * buffer,block_t block_number);

struct dm_crypt_iv {
	const char * name;
	unsigned iv_size;
	
//	dm_crypt_iv_use_function use;
	dm_crypt_iv_generate_function generate;
};

struct dm_crypt_iv * dm_crypt_register_iv(const char * name,dm_crypt_iv_generate_function generate,unsigned iv_size);
void dm_crypt_deregister_iv(const char * name);
struct dm_crypt_iv * dm_crypt_lookup_iv(const char * name);

#endif // #ifndef IV_H
