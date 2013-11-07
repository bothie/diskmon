/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

/*
 * Plain IV generation algorithm:
 *
 * the initial vector is the little-endian version of the lower 32-bit of the 
 * block number, padded with zeros if neccessary.
 */

#include "iv.h"

#include "common.h"

#include <string.h>

#define IV_NAME "plain"

static bool initialized;

static void dm_crypt_iv_plain_generate(void * buffer,block_t block_number);

CRYPT_INIT {
	if (!dm_crypt_register_iv(IV_NAME,dm_crypt_iv_plain_generate,16)) {
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

void dm_crypt_iv_plain_generate(void * buffer,block_t block_number) {
	u8 * iv=buffer;
	
	iv[0]=                block_number     ;
	iv[1]=((unsigned     )block_number)>> 8;
	iv[2]=((unsigned long)block_number)>>16;
	iv[3]=((unsigned long)block_number)>>24;
	
	memset(iv+4,0,12);
	
	return;
}
