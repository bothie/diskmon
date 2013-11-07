/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "crypt.h"

#include "common.h"

#include <errno.h>
#include <string.h>
#include <vasiar.h>

VASIAR(struct crypt_alg *) algs;

struct crypt_alg * crypt_register_alg(
	const char * name,
	crypt_alg_makekey_function makekey,
	unsigned block_size,
	unsigned key_length
//	unsigned min_keysize,
//	unsigned max_keysize
) {
	for (size_t i=0;i<VASIZE(algs);++i) {
		struct crypt_alg * iterator=VAACCESS(algs,i);
		if (!strcmp(iterator->name,name)) {
			ERRORF("Trying to register an already registered crypto cipher (%s).",name);
			return NULL;
		}
	}
	struct crypt_alg * new=malloc(sizeof(*new));
	if (!new) {
		ERRORF("Not enough memory while trying to register the crypto cipher (%s).",name);
		return NULL;
	}
	
	new->name=name;
	new->makekey=makekey;
	new->block_size=block_size;
	new->key_length=key_length;
//	new->min_keysize=min_keysize;
//	new->max_keysize=max_keysize;
	
	VANEW(algs)=new;
	
	INFOF(
		"crypt_register_alg: %s, bs=%u, kl=%u." // keysize={min=%u, max=%u}."
		,new->name
		,new->block_size
		,new->key_length
//		,new->min_keysize
//		,new->max_keysize
	);
	
	return new;
}

void crypt_deregister_alg_by_name(const char * name) {
	for (size_t i=0;i<VASIZE(algs);++i) {
		struct crypt_alg * iterator=VAACCESS(algs,i);
		if (!strcmp(iterator->name,name)) {
			VAACCESS(algs,i)=VAACCESS(algs,VASIZE(algs)-1);
			VATRUNCATE(algs,VASIZE(algs)-1);
			return;
		}
	}
	ERRORF("Trying to deregister a crypto cipher (%s) that doesn't exist",name);
}

void crypt_deregister_alg_by_ref(struct crypt_alg * ref) {
	for (size_t i=0;i<VASIZE(algs);++i) {
		struct crypt_alg * iterator=VAACCESS(algs,i);
		if (iterator==ref) {
			VAACCESS(algs,i)=VAACCESS(algs,VASIZE(algs)-1);
			VATRUNCATE(algs,VASIZE(algs)-1);
			return;
		}
	}
	ERRORF("Trying to deregister a crypto cipher %p (%s) that doesn't exist",(void*)ref,ref->name);
}

struct crypt_alg * crypt_lookup_alg(const char * name) {
	for (size_t i=0;i<VASIZE(algs);++i) {
		struct crypt_alg * iterator=VAACCESS(algs,i);
		if (!strcmp(iterator->name,name)) {
			return iterator;
		}
	}
	errno=ENOENT;
	return NULL;
}
