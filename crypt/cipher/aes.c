/*
 * Rijndael (AES) Cipher Algorithm
 *
 * Copyright (C) 2007 Bodo Thiesen
 *
 * Shamelessly stolen from the Linux Kernel Sources. The rest of this comment 
 * was left unchanged.
 *
 * Based on Brian Gladman's code.
 *
 * Linux developers:
 *  Alexander Kjeldaas <astor@fast.no>
 *  Herbert Valerio Riedel <hvr@hvrlab.org>
 *  Kyle McMartin <kyle@debian.org>
 *  Adam J. Richter <adam@yggdrasil.com> (conversion to 2.5 API).
 *
 * This file is dually licensed, choose if you want to conform to GPL or BSD
 *
 * - GPL: --------------------------------------------------------------------
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * - BSD: --------------------------------------------------------------------
 * Copyright (c) 2002, Dr Brian Gladman <brg@gladman.me.uk>, Worcester, UK.
 * All rights reserved.
 *
 * LICENSE TERMS
 *
 * The free distribution and use of this software in both source and binary
 * form is allowed (with or without changes) provided that:
 *
 *   1. distributions of this source code include the above copyright
 *      notice, this list of conditions and the following disclaimer;
 *
 *   2. distributions in binary form include the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other associated materials;
 *
 *   3. the copyright holder's name is not used to endorse products
 *      built using this software without specific written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this product
 * may be distributed under the terms of the GNU General Public License (GPL),
 * in which case the provisions of the GPL apply INSTEAD OF those given above.
 *
 * DISCLAIMER
 *
 * This software is provided 'as is' with no explicit or implied warranties
 * in respect of its properties, including, but not limited to, correctness
 * and/or fitness for purpose.
 * ---------------------------------------------------------------------------
 */

/*
 * Some changes from the Gladman version:
 * s/RIJNDAEL(e_key)/E_KEY/g
 * s/RIJNDAEL(d_key)/D_KEY/g
 */

#include "crypt.h"

#include "common.h"

#include <btstr.h>
char * mmemcpy(const char *b,size_t bs); // Ist noch nicht im neuesten Release enthalten
#include <bttypes.h>
#include <byteswap.h>
#include <endian.h>
#include <errno.h>
#include <string.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define host2le32(x) (x)
#define le2host32(x) (x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define host2le32(x) bswap_32(x)
#define le2host32(x) bswap_32(x)
#else
#error PDP? You'r kidding, aren't you?
#endif

// #define rol32(value,bits) ((value<<bits) | (value>>(32-bits)))
// #define ror32(value,bits) ((value>>bits) | (value<<(32-bits)))
/**
 * rol32 - rotate a 32-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline u32 rol32(u32 word,unsigned int shift) {
	return (word<<shift)|(word>>(32-shift));
}

/**
 * ror32 - rotate a 32-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline u32 ror32(u32 word,unsigned int shift) {
	return (word>>shift)|(word<<(32-shift));
}

#define CIPHER_NAME "aes"

#define AES_MIN_KEY_SIZE 16
#define AES_MAX_KEY_SIZE 32
#define AES_BLOCK_SIZE 16

static void gen_tabs(void);

static struct crypt_alg_key * aes_makekey16(const void * key);
static struct crypt_alg_key * aes_makekey24(const void * key);
static struct crypt_alg_key * aes_makekey32(const void * key);

static void aes_killkey(struct crypt_alg_key *);

static struct crypt_alg_ctx * aes_makectx(struct crypt_alg_key * crypt_alg_key);

static void aes_encrypt(struct crypt_alg_ctx * crypt_alg_ctx,const void * _in,void * _out);
static void aes_decrypt(struct crypt_alg_ctx * crypt_alg_ctx,const void * _in,void * _out);
static void aes_killctx(struct crypt_alg_ctx * crypt_alg_ctx);

static bool initialized;

static struct crypt_alg * me16;
static struct crypt_alg * me24;
static struct crypt_alg * me32;

CRYPT_INIT {
	gen_tabs();
	me16=crypt_register_alg(CIPHER_NAME"128",aes_makekey16,AES_BLOCK_SIZE,16);
	me24=crypt_register_alg(CIPHER_NAME"192",aes_makekey24,AES_BLOCK_SIZE,24);
	me32=crypt_register_alg(CIPHER_NAME"256",aes_makekey32,AES_BLOCK_SIZE,32);
	if (!me16 || !me24 || !me32) {
		if (me16) crypt_deregister_alg_by_ref(me16);
		if (me24) crypt_deregister_alg_by_ref(me24);
		if (me32) crypt_deregister_alg_by_ref(me32);
		eprintf("Couldn't register cipher algorithm %s",CIPHER_NAME);
	} else {
		initialized=true;
	}
}

CRYPT_FINI {
	if (initialized) {
		crypt_deregister_alg_by_ref(me16);
		crypt_deregister_alg_by_ref(me24);
		crypt_deregister_alg_by_ref(me32);
	}
}

/*
 * #define byteof(x, nr) ((unsigned char)((x) >> (nr*8))) 
 */
static inline u8 byteof(const u32 x,const unsigned n) {
	return x>>(n<<3);
}

#define E_KEY (&ctx->buf[0])
#define D_KEY (&ctx->buf[60])

static u8 pow_tab[256]; // __initdata;
static u8 log_tab[256]; // __initdata;
static u8 sbx_tab[256]; // __initdata;
static u8 isb_tab[256]; // __initdata;
static u32 rco_tab[10];
static u32 ft_tab[4][256];
static u32 it_tab[4][256];

static u32 fl_tab[4][256];
static u32 il_tab[4][256];

static inline u8 f_mult(u8 a,u8 b) {
	u8 aa=log_tab[a];
	u8 cc=aa+log_tab[b];
	
	return pow_tab[cc+(cc<aa?1:0)];
}

#define ff_mult(a,b) (a&&b?f_mult(a,b):0)

#define f_rn(bo,bi,n,k)	bo[n]= \
	ft_tab[0][byteof(bi[(n+0)&3],0)]^ \
	ft_tab[1][byteof(bi[(n+1)&3],1)]^ \
	ft_tab[2][byteof(bi[(n+2)&3],2)]^ \
	ft_tab[3][byteof(bi[(n+3)&3],3)]^*(k+n)

#define i_rn(bo,bi,n,k) bo[n]= \
	it_tab[0][byteof(bi[(n+0)&3],0)]^ \
	it_tab[1][byteof(bi[(n+3)&3],1)]^ \
	it_tab[2][byteof(bi[(n+2)&3],2)]^ \
	it_tab[3][byteof(bi[(n+1)&3],3)]^*(k+n)

#define ls_box(x) ( \
	fl_tab[0][byteof(x,0)]^ \
	fl_tab[1][byteof(x,1)]^ \
	fl_tab[2][byteof(x,2)]^ \
	fl_tab[3][byteof(x,3)] \
)

#define f_rl(bo,bi,n,k) bo[n]= \
	fl_tab[0][byteof(bi[(n+0)&3],0)]^ \
	fl_tab[1][byteof(bi[(n+1)&3],1)]^ \
	fl_tab[2][byteof(bi[(n+2)&3],2)]^ \
	fl_tab[3][byteof(bi[(n+3)&3],3)]^*(k+n)

#define i_rl(bo,bi,n,k) bo[n]= \
	il_tab[0][byteof(bi[(n+0)&3],0)]^ \
	il_tab[1][byteof(bi[(n+3)&3],1)]^ \
	il_tab[2][byteof(bi[(n+2)&3],2)]^ \
	il_tab[3][byteof(bi[(n+1)&3],3)]^*(k+n)

void gen_tabs(void) {
	u32 i,t;
	u8 p,q;
	
	/*
	 * log and power tables for GF(2**8) finite field with
	 * 0x011b as modular polynomial - the simplest primitive
	 * root is 0x03, used here to generate the tables
	 */
	for (i=0,p=1;i<256;++i) {
		pow_tab[i]=(u8)p;
		log_tab[p]=(u8)i;
		
		p^=(p<<1)^(p&0x80?0x01b:0);
	}
	
	log_tab[1]=0;
	
	for (i=0,p=1;i<10;++i) {
		rco_tab[i]=p;
		
		p=(p<<1)^(p&0x80?0x01b:0);
	}
	
	for (i=0;i<256;++i) {
		p=(i?pow_tab[255-log_tab[i]]:0);
		q=((p>>7)|(p<<1))^((p>>6)|(p<<2));
		p^=0x63^q^((q>>6)|(q<<2));
		sbx_tab[i]=p;
		isb_tab[p]=(u8)i;
	}
	
	for (i=0;i<256;++i) {
		p=sbx_tab[i];
		
		t=p;
		fl_tab[0][i]=t;
		fl_tab[1][i]=rol32(t, 8);
		fl_tab[2][i]=rol32(t,16);
		fl_tab[3][i]=rol32(t,24);
		
		t=((u32)ff_mult(2,p))|((u32)p<<8)|((u32)p<<16)|((u32)ff_mult(3,p)<<24);
		
		ft_tab[0][i]=t;
		ft_tab[1][i]=rol32(t,8);
		ft_tab[2][i]=rol32(t,16);
		ft_tab[3][i]=rol32(t,24);
		
		p=isb_tab[i];
		
		t=p;
		il_tab[0][i]=t;
		il_tab[1][i]=rol32(t, 8);
		il_tab[2][i]=rol32(t,16);
		il_tab[3][i]=rol32(t,24);
		
		t=((u32)ff_mult(14,p)    )
		| ((u32)ff_mult( 9,p)<< 8)
		| ((u32)ff_mult(13,p)<<16)
		| ((u32)ff_mult(11,p)<<24);
		
		it_tab[0][i]=t;
		it_tab[1][i]=rol32(t, 8);
		it_tab[2][i]=rol32(t,16);
		it_tab[3][i]=rol32(t,24);
	}
}

#define star_x(x) (((x)&0x7f7f7f7f)<<1)^((((x)&0x80808080 )>>7)*0x1b)

#define imix_col(_y,_x) do { \
	u32 t,u,v,w; \
	u32 * y=&(_y); \
	u32 * x=&(_x); \
	u=star_x(*x); \
	v=star_x(u); \
	w=star_x(v); \
	t=w^*x; \
	*y=u^v^w^ror32(u^t,8)^ror32(v^t,16)^ror32(t,24); \
} while (0);

/*
 * initialise the key schedule from the user supplied key
 */

#define loop4(i) do { \
	t=ror32(t,8); \
	t=ls_box(t)^rco_tab[i]; \
	t^=E_KEY[4*i+0]; E_KEY[4*i+4]=t; \
	t^=E_KEY[4*i+1]; E_KEY[4*i+5]=t; \
	t^=E_KEY[4*i+2]; E_KEY[4*i+6]=t; \
	t^=E_KEY[4*i+3]; E_KEY[4*i+7]=t; \
} while(0)

#define loop6(i) do { \
	t=ror32(t,8); \
	t=ls_box(t)^rco_tab[i]; \
	t^=E_KEY[6*i+0]; E_KEY[6*i+ 6]=t; \
	t^=E_KEY[6*i+1]; E_KEY[6*i+ 7]=t; \
	t^=E_KEY[6*i+2]; E_KEY[6*i+ 8]=t; \
	t^=E_KEY[6*i+3]; E_KEY[6*i+ 9]=t; \
	t^=E_KEY[6*i+4]; E_KEY[6*i+10]=t; \
	t^=E_KEY[6*i+5]; E_KEY[6*i+11]=t; \
} while(0)

#define loop8(i) do { \
	t=ror32(t,8); \
	t=ls_box(t)^rco_tab[i]; \
	t^=E_KEY[8*i+0]; E_KEY[8*i+ 8]=t; \
	t^=E_KEY[8*i+1]; E_KEY[8*i+ 9]=t; \
	t^=E_KEY[8*i+2]; E_KEY[8*i+10]=t; \
	t^=E_KEY[8*i+3]; E_KEY[8*i+11]=t; \
	t=ls_box(t); \
	t^=E_KEY[8*i+4]; E_KEY[8*i+12]=t; \
	t^=E_KEY[8*i+5]; E_KEY[8*i+13]=t; \
	t^=E_KEY[8*i+6]; E_KEY[8*i+14]=t; \
	t^=E_KEY[8*i+7]; E_KEY[8*i+15]=t; \
} while(0)

struct aes_key {
	struct crypt_alg_key this;
	unsigned key_length;
	u32 * key;
};

struct crypt_alg_key * aes_makekey(
	struct crypt_alg * me,
	const void * key
) {
	struct aes_key * retval;
	
	retval=zmalloc(sizeof(*retval));
	if (!retval) return NULL;
	
	retval->key=(u32*)mmemcpy(key,retval->key_length=me->key_length);
	if (!retval->key) {
		free(retval);
		return NULL;
	}
	
	retval->this.makectx=aes_makectx;
	retval->this.killkey=aes_killkey;
	retval->this.alg=me;
	
	return &retval->this;
}

struct crypt_alg_key * aes_makekey16(const void * key) {
	return aes_makekey(me16,key);
}

struct crypt_alg_key * aes_makekey24(const void * key) {
	return aes_makekey(me24,key);
}

struct crypt_alg_key * aes_makekey32(const void * key) {
	return aes_makekey(me32,key);
}

void aes_killkey(struct crypt_alg_key * crypt_alg_key) {
	struct aes_key * key=(struct aes_key *)crypt_alg_key;
	free(key->key);
	free(key);
}

struct aes_ctx {
	struct crypt_alg_ctx this;
	unsigned int key_length;
	u32 buf[120];
};

struct crypt_alg_ctx * aes_makectx(
	struct crypt_alg_key * crypt_alg_key
) {
	struct aes_ctx * retval=zmalloc(sizeof(*retval));
	if (!retval) return NULL;
#define ctx retval
	
	retval->this.encrypt=aes_encrypt;
	retval->this.decrypt=aes_decrypt;
	retval->this.killctx=aes_killctx;
	retval->this.key=crypt_alg_key;
	
	const u32 * key=((struct aes_key *)crypt_alg_key)->key;
	
	retval->key_length=((struct aes_key *)crypt_alg_key)->key_length;
	
	for (u32 i=retval->key_length/4;--i;) {
		E_KEY[i]=key[i];
	}
	
	switch (retval->key_length) {
		case 16: {
			u32 t=E_KEY[3];
			
			for (u32 i=0;i<10;++i) {
				loop4(i);
			}
			
			break;
		}
		
		case 24: {
			u32 t=E_KEY[5];
			
			for (u32 i=0;i< 8;++i) {
				loop6(i);
			}
			
			break;
		}
		
		case 32: {
			u32 t=E_KEY[7];
			
			for (u32 i=0;i< 7;++i) {
				loop8(i);
			}
			
			break;
		}
	}
	
	D_KEY[0]=E_KEY[0];
	D_KEY[1]=E_KEY[1];
	D_KEY[2]=E_KEY[2];
	D_KEY[3]=E_KEY[3];
	
	for (u32 i=4;i<retval->key_length+24;++i) {
		imix_col(D_KEY[i],E_KEY[i]);
	}
	
	return &retval->this;
#undef ctx
}

/*
 * encrypt a block of text
 */

#define f_nround(bo,bi,k) do { \
	f_rn(bo,bi,0,k); \
	f_rn(bo,bi,1,k); \
	f_rn(bo,bi,2,k); \
	f_rn(bo,bi,3,k); \
	k+=4; \
} while (0)

#define f_lround(bo,bi,k) do { \
	f_rl(bo,bi,0,k); \
	f_rl(bo,bi,1,k); \
	f_rl(bo,bi,2,k); \
	f_rl(bo,bi,3,k); \
} while (0)

void aes_encrypt(
	struct crypt_alg_ctx * crypt_alg_ctx,
	const void * _in,
	void * _out
) {
	const struct aes_ctx * ctx=(struct aes_ctx *)crypt_alg_ctx;
	const u32 * src=(const u32 *)_in;
	u32 * dst=(u32 *)_out;
	u32 b0[4];
	u32 b1[4];
	const u32 * kp=E_KEY+4;
	
	b0[0]=le2host32(src[0])^E_KEY[0];
	b0[1]=le2host32(src[1])^E_KEY[1];
	b0[2]=le2host32(src[2])^E_KEY[2];
	b0[3]=le2host32(src[3])^E_KEY[3];
	
	if (ctx->key_length>24) {
		f_nround(b1,b0,kp);
		f_nround(b0,b1,kp);
	}
	
	if (ctx->key_length > 16) {
		f_nround(b1,b0,kp);
		f_nround(b0,b1,kp);
	}
	
	f_nround(b1,b0,kp);
	f_nround(b0,b1,kp);
	f_nround(b1,b0,kp);
	f_nround(b0,b1,kp);
	f_nround(b1,b0,kp);
	f_nround(b0,b1,kp);
	f_nround(b1,b0,kp);
	f_nround(b0,b1,kp);
	f_nround(b1,b0,kp);
	f_lround(b0,b1,kp);
	
	dst[0]=host2le32(b0[0]);
	dst[1]=host2le32(b0[1]);
	dst[2]=host2le32(b0[2]);
	dst[3]=host2le32(b0[3]);
}

/* decrypt a block of text */

#define i_nround(bo,bi,k) do { \
	i_rn(bo,bi,0,k); \
	i_rn(bo,bi,1,k); \
	i_rn(bo,bi,2,k); \
	i_rn(bo,bi,3,k); \
	k-=4; \
} while (0)

#define i_lround(bo,bi,k) do { \
	i_rl(bo,bi,0,k); \
	i_rl(bo,bi,1,k); \
	i_rl(bo,bi,2,k); \
	i_rl(bo,bi,3,k); \
} while (0)

void aes_decrypt(
	struct crypt_alg_ctx * crypt_alg_ctx,
	const void * _in,
	void * _out
) {
	const struct aes_ctx * ctx=(struct aes_ctx *)crypt_alg_ctx;
	const u32 * src=(const u32 *)_in;
	u32 * dst=(u32 *)_out;
	u32 b0[4];
	u32 b1[4];
	const int key_len=ctx->key_length;
	const u32 * kp=D_KEY+key_len+20;
	
	b0[0]=le2host32(src[0])^E_KEY[key_len+24];
	b0[1]=le2host32(src[1])^E_KEY[key_len+25];
	b0[2]=le2host32(src[2])^E_KEY[key_len+26];
	b0[3]=le2host32(src[3])^E_KEY[key_len+27];
	
	if (key_len>24) {
		i_nround(b1,b0,kp);
		i_nround(b0,b1,kp);
	}
	
	if (key_len>16) {
		i_nround(b1,b0,kp);
		i_nround(b0,b1,kp);
	}
	
	i_nround(b1,b0,kp);
	i_nround(b0,b1,kp);
	i_nround(b1,b0,kp);
	i_nround(b0,b1,kp);
	i_nround(b1,b0,kp);
	i_nround(b0,b1,kp);
	i_nround(b1,b0,kp);
	i_nround(b0,b1,kp);
	i_nround(b1,b0,kp);
	i_lround(b0,b1,kp);
	
	dst[0]=host2le32(b0[0]);
	dst[1]=host2le32(b0[1]);
	dst[2]=host2le32(b0[2]);
	dst[3]=host2le32(b0[3]);
}

void aes_killctx(struct crypt_alg_ctx * crypt_alg_ctx) {
	free(crypt_alg_ctx);
}
