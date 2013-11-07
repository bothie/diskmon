/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "iv.h"

#include "common.h"

#include <errno.h>
#include <string.h>
#include <vasiar.h>

VASIAR(struct dm_crypt_iv *) dm_crypt_ivs;

struct dm_crypt_iv * dm_crypt_register_iv(const char * name,dm_crypt_iv_generate_function generate,unsigned iv_size) {
	struct dm_crypt_iv * iterator;
	for (size_t i=0;i<VASIZE(dm_crypt_ivs);++i) {
		iterator=VAACCESS(dm_crypt_ivs,i);
		if (!strcmp(iterator->name,name)) {
			ERRORF("Trying to register an already registered driver name %s",name);
			return NULL;
		}
	}
	iterator=malloc(sizeof(*iterator));
	iterator->name=name;
	iterator->generate=generate;
	iterator->iv_size=iv_size;
	VANEW(dm_crypt_ivs)=iterator;
	return iterator;
}

void dm_crypt_deregister_iv(const char * name) {
	for (size_t i=0;i<VASIZE(dm_crypt_ivs);++i) {
		struct dm_crypt_iv * iterator=VAACCESS(dm_crypt_ivs,i);
		if (!strcmp(iterator->name,name)) {
			free(iterator);
			VAACCESS(dm_crypt_ivs,i)=VAACCESS(dm_crypt_ivs,VASIZE(dm_crypt_ivs)-1);
			VATRUNCATE(dm_crypt_ivs,VASIZE(dm_crypt_ivs)-1);
			return;
		}
	}
	ERROR("Trying to deregister a device that doesn't exist");
}

struct dm_crypt_iv * dm_crypt_lookup_iv(const char * name) {
	for (size_t i=0;i<VASIZE(dm_crypt_ivs);++i) {
		struct dm_crypt_iv * iterator=VAACCESS(dm_crypt_ivs,i);
		if (!strcmp(iterator->name,name)) {
			return iterator;
		}
	}
	errno=ENOENT;
	return NULL;
}

/*
void dm_crypt_iv_encrypt(struct crypt_alg_ctx * ctx,struct dm_crypt_iv * iv_method,block_t block_number) {
	static bool warned=false;
	
	unsigned native_bs=ctx->key->alg->block_size;
	unsigned bs=native_bs;
	
	bs=bs*((bs+3)/bs); // Make sure, we have at least 4 bytes AND are a natural multiple of the native block size
	
	u8 iv[bs];
	
	memset(iv,0,bs);
	
	iv[0]=block_number;
	iv[1]=block_number>>8;
	iv[2]=block_number>>16;
	iv[3]=block_number>>24;
	
	if (block_number>0xffffffff && !warned) {
		WARNING("Access to block number >0xffffffff may cause compatibility problems.");
		warned=true;
	}
	
	printf("Initializing dencryption context for block %llu:\n",(long long unsigned)block_number);
	for (int i=0;bs;++i,bs-=native_bs) {
		ctx->encrypt(ctx,iv+i*native_bs,iv);
		printf("\tctx->encrypt(ctx,%u,iv)\n",(unsigned)(iv+i*native_bs));
	}
}
*/
