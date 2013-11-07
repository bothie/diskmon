/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#ifndef CRYPT_H
#define CRYPT_H

#include <stdbool.h>
#include <sys/types.h>

#include "bttypes.h"

#define CRYPT_INIT __attribute__((constructor)) static void init(void) 
#define CRYPT_FINI __attribute__((destructor)) static void fini(void) 

#ifndef zmalloc
#define zmalloc(size) calloc(1,size)
#endif // #ifndef zmalloc

/*
 * Ciper Algorithm functions:
 */

struct crypt_alg;
struct crypt_alg_key;
struct crypt_alg_ctx;

typedef struct crypt_alg_key * (*crypt_alg_makekey_function)(/* unsigned int key_len,*/const void * key);

typedef struct crypt_alg_ctx * (*crypt_alg_makectx_function)(struct crypt_alg_key * crypt_alg_key);
typedef void (*crypt_alg_killkey_function)(struct crypt_alg_key * crypt_alg_key);

typedef void (*crypt_alg_encrypt_function)(struct crypt_alg_ctx * crypt_alg_ctx,const void * _in,void * _out);
typedef void (*crypt_alg_decrypt_function)(struct crypt_alg_ctx * crypt_alg_ctx,const void * _in,void * _out);
typedef void (*crypt_alg_killctx_function)(struct crypt_alg_ctx * crypt_alg_ctx);

struct crypt_alg {
	const char * name;
	unsigned block_size;
//	unsigned min_keysize;
//	unsigned max_keysize;
	unsigned key_length;
	
	crypt_alg_makekey_function makekey;
};

struct crypt_alg_key {
	/*
	 * The two variables key_length and key are here only to allow their 
	 * output in debug code etc, in fact they should be private to the 
	 * algorithm. (No one prevents one algorithm from defining their own 
	 * key and key_length variables. In spite this not being senseful, 
	 * it's important that those code at lease sets key_length and key to 
	 * 0 here.)
	 */
	struct crypt_alg * alg;
//	unsigned key_length;
//	u8 * key;
	
	crypt_alg_makectx_function makectx;
	crypt_alg_killkey_function killkey;
};

struct crypt_alg_ctx {
	struct crypt_alg_key * key;
	
	crypt_alg_encrypt_function encrypt;
	crypt_alg_decrypt_function decrypt;
	crypt_alg_killctx_function killctx;
};

struct crypt_alg * crypt_register_alg(
	const char * name,
	crypt_alg_makekey_function makekey,
	unsigned block_size,
	unsigned key_length
//	unsigned min_keysize,
//	unsigned max_keysize
);

void crypt_deregister_alg_by_name(const char * name);
void crypt_deregister_alg_by_ref(struct crypt_alg * ref);
struct crypt_alg * crypt_lookup_alg(const char * name);

/*
 * Initialization vector functions:
 */

#endif // #ifndef BDEV_H

/*
if (_in!=_out) {
	memcpy(_out,_in,block_size);
	_in=_out;
}

if (_in==_out) {
	memcpy(local_storage,_in,block_size);
	_in=local_storage;
}
*/
