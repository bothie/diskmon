/*
 * Plain IV generation algorithm:
 *
 * the initial vector is the little-endian version of the lower 32-bit of the 
 * block number, padded with zeros if neccessary.
 */

#include "iv.h"

#define IV_NAME "null"

static bool initialized;

static void dm_crypt_iv_null_generate(void * buffer,block_t block_number);

CRYPT_INIT {
	if (!dm_crypt_register_iv(IV_NAME,dm_crypt_iv_null_generate,1)) {
		eprintf("Couldn't register iv algorithm %s",IV_NAME);
	} else {
		initialized=true;
	}
}

CRYPT_FINI {
	if (initialized) {
		dm_crypt_deregister_iv(IV_NAME);
	}
}

void dm_crypt_iv_null_generate(void * buffer,block_t block_number) {
	ignore(block_number);
	*(u8 *)buffer=0;
}
