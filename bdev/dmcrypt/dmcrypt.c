#include "bdev.h"
#include "common.h"
#include "crypt.h"
#include "iv.h"

#include <assert.h>
#include <bterror.h>
// #include <btmacros.h>
// #include <bttime.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <gcrypt.h>
#include <mprintf.h>
// #include <stdbool.h>
// #include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DRIVER_NAME "dmcrypt"

/*
 * Public interface
 */

// No special public functions defined

/*
 * Indirect public interface (via struct dev)
 */
 
static block_t dmcrypt_read(void * _private,block_t first,block_t num,unsigned char * data);
static block_t dmcrypt_write(void * _private,block_t first,block_t num,const unsigned char * data);

static bool dmcrypt_destroy(void * _private);

static struct bdev * dmcrypt_init(struct bdev_driver * bdev_driver,char * name,const char * args);

static bool initialized;

BDEV_INIT {
	if (!gcry_check_version("1.0.0")) {
		ERROR("GNU crypt doesn't match version 1.0.0.");
		return;
	}
	
	if (!bdev_register_driver(DRIVER_NAME,&dmcrypt_init)) {
		eprintf("Couldn't register driver %s",DRIVER_NAME);
	} else {
		initialized=true;
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

// cryptsetup specific code:
// static
void * hash_passphrase(const char * algo_name,const char * passphrase,unsigned needed_key_length) {
	enum gcry_md_algos hash_id=gcry_md_map_name(algo_name);
	if (!hash_id) {
		ERRORF("Your version of GNU crypt doesn't support message digest algorithm %s yet.",algo_name);
		errno=ENOSYS;
		return NULL;
	}
	
	size_t hash_length=gcry_md_get_algo_dlen(hash_id);
	void * hash=malloc(hash_length);
	if (!hash) {
		ERRORF("Couldn't allocate %u bytes of memory: %s.",(unsigned)hash_length,strerror(errno));
		return NULL;
	}
	
	void * retval=malloc(needed_key_length);
	if (!retval) {
		free(hash);
		ERRORF("Couldn't allocate %u bytes of memory: %s.",(unsigned)needed_key_length,strerror(errno));
		return NULL;
	}
	char * key=retval;
	
	size_t sl=strlen(passphrase);
	
	gcry_error_t error;
	
	gcry_md_hd_t digest_ctx;
	
	if ((error=gcry_md_open(&digest_ctx,hash_id,0))) {
		ERRORF("Couldn't create message digest context: %s.",gcry_strerror(error));
		free(retval);
		free(hash);
		return NULL;
	}
	
	for (int round=0;needed_key_length;++round) {
		/*
		 * If the message digest function returns lesser bytes as are 
		 * required for the key, we use the following method to get a 
		 * key of appropriate length:
		 *
		 * First, we calculate md(passphrase) and take that bytes. Then 
		 * we put an "A" in front of the passphrase and repeat this 
		 * procedure until we have enough bytes together [i.e. we 
		 * calculate md(pass)+md("A"+pass)+md("AA"+pass)+..., where "+"
		 * concats the strings].
		 */
		for (int i=0;i<round;++i) {
			gcry_md_write(digest_ctx,"A",1);
		}
		
		gcry_md_write(digest_ctx,passphrase,sl);
		
		if (hash_length>needed_key_length) {
			hash_length=needed_key_length;
		}
		
		memcpy(key,gcry_md_read(digest_ctx,hash_id),hash_length);
		
		key+=hash_length;
		needed_key_length-=hash_length;
		
		if (needed_key_length) {
			gcry_md_reset(digest_ctx);
		}
	}
	
	gcry_md_close(digest_ctx);
	
	free(hash);
	
	return retval;
}

struct dmcrypt_bdev {
//	struct bdev this;
	struct bdev * backing_dev;
	struct dm_crypt_iv * iv;
/*
	struct crypt_alg * alg;
	unsigned key_length;
	void * key;
*/
	struct crypt_alg_key * key;
};

struct dm_crypt_iv * get_iv(const char * name) {
	struct dm_crypt_iv * retval=dm_crypt_lookup_iv(name);
	
	if (!retval) {
		char * filename=mprintf("%s.so",name);
		if (!filename) {
			ERRORF(
				"mprintf(\"%%s.so\",\"%s\") failed: %s\n"
				,name
				,strerror(errno)
			);
			return NULL;
		}
		void * handle=dlopen(filename,RTLD_NOW|RTLD_GLOBAL);
		if (!handle) {
			ERRORF(
				"dlopen(\"%s\",RTLD_NOW|RTLD_GLOBAL) failed: %s\n"
				,filename
				,dlerror()
			);
			free(filename);
			return NULL;
		}
		retval=dm_crypt_lookup_iv(name);
		if (!retval) {
			ERRORF(
				"Loaded %s.so successfully, but it didn't register itself to the iv framework.\n"
				,filename
			);
			free(filename);
			return NULL;
		}
		free(filename);
	}
	
	return retval;
}

struct crypt_alg * get_crypt_alg(const char * name) {
	struct crypt_alg * retval=crypt_lookup_alg(name);
	
	if (!retval) {
		char * filename=mprintf("%s.so",name);
		if (!filename) {
			ERRORF(
				"mprintf(\"%%s.so\",\"%s\") failed: %s\n"
				,name
				,strerror(errno)
			);
			return NULL;
		}
		void * handle=dlopen(filename,RTLD_NOW|RTLD_GLOBAL);
		if (!handle) {
			ERRORF(
				"dlopen(\"%s\",RTLD_NOW|RTLD_GLOBAL) failed: %s\n"
				,filename
				,dlerror()
			);
			free(filename);
			return NULL;
		}
		retval=crypt_lookup_alg(name);
		if (!retval) {
			ERRORF(
				"Loaded %s.so successfully, but it didn't register itself to the crypt framework.\n"
				,filename
			);
			free(filename);
			return NULL;
		}
		free(filename);
	}
	
	return retval;
}

// aes 313233343536383739304142434445464748494a4b4c4d4e4f50515253545556 0 /dev/loop0 1

struct bdev * dmcrypt_init(struct bdev_driver * bdev_driver,char * name,const char * args) {
	ignore(bdev_driver);
	
// #define DEBUGGING
	
	// Zu Testzwecken platt durchmarschieren:
	
#ifdef DEBUGGING
#define KEY_FILE "chkraid.key"
#warning +DEBUGGING
#else
#define KEY_FILE "/ramfs/aesraid.key"
#warning -DEBUGGING
#endif
	int fd=open(KEY_FILE,O_RDONLY);
	if (fd<0) {
		ERRORF("File %s doesn't exist.",KEY_FILE);
		return NULL;
	}
	
	char passphrase[100];
	int passphrase_len=read(fd,passphrase+2,sizeof(passphrase)-2);
	close(fd);
	if (passphrase_len<0 || passphrase_len==98) {
		ERROR("passphrase-problem.");
		return NULL;
	}
	passphrase[passphrase_len+2]=0;
	passphrase[1]=':';
	passphrase[0]=strlen(args)?args[strlen(args)-1]:' ';
	passphrase[32]=0;
#ifdef DEBUGGING
	printf("key =");
	for (int i=0;i<passphrase_len+2;++i) {
		printf(" %02x",passphrase[i]);
	}
	printf(" (\"%s\")\n",passphrase);
#endif
	
	// aes-cbc-plain
	
	const char * backing_dev_name=args;
	
	struct dmcrypt_bdev * private=malloc(sizeof(*private));
	
	private->backing_dev=bdev_lookup_bdev(backing_dev_name);
	if (!private->backing_dev) {
		ERRORF("Couldn't lookup device %s.",backing_dev_name);
		free(backing_dev_name);
		return NULL;
	}
	
	{
		struct crypt_alg * alg;
		
		alg=get_crypt_alg("aes256");
		if (!alg) {
			ERRORF("Couldn't lookup cipher algorithm %s.","aes256");
			return NULL;
		}
		
/*
		{
			void * key=hash_passphrase("RIPEMD160",passphrase,alg->key_length);
			private->key=alg->makekey(key);
			free(key);
		}
*/
		private->key=alg->makekey(passphrase);
	}
	if (!private->key) {
		ERROR("Couldn't create cipher key.");
		return NULL;
	}
	
	private->iv=get_iv("plain");
	if (!private->iv) {
		ERRORF("Couldn't lookup iv algorithm %s.","plain");
		return NULL;
	}
	
	struct bdev * retval;
	
	if (!(retval=bdev_register_bdev(
		bdev_driver,
		name,
		bdev_get_size(private->backing_dev)-1,
		bdev_get_block_size(private->backing_dev),
		dmcrypt_destroy,
		private
	))) {
		ERRORF("Couldn't register %s.",name);
		private->key->killkey(private->key);
		free(private);
		return NULL;
	}
	bdev_set_read_function(retval,dmcrypt_read);
	bdev_set_write_function(retval,dmcrypt_write);
	
	return retval;
}

/*
unsigned int cbc_process_decrypt(
	const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes)
{
	struct crypto_tfm *tfm = desc->tfm;
	void (*xor)(u8 *, const u8 *) = tfm->crt_u.cipher.cit_xor_block;
	int bsize = crypto_tfm_alg_blocksize(tfm);
	unsigned long alignmask = crypto_tfm_alg_alignmask(desc->tfm);

	u8 stack[src == dst ? bsize + alignmask : 0];
	u8 *buf = (u8 *)ALIGN((unsigned long)stack, alignmask + 1);
	u8 **dst_p = src == dst ? &buf : &dst;

	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) = desc->crfn;
	u8 *iv = desc->info;
	unsigned int done = 0;

	nbytes -= bsize;

	do {
		u8 *tmp_dst = *dst_p;

		fn(tfm, tmp_dst, src);
		xor(tmp_dst, iv);
		memcpy(iv, src, bsize);
		if (tmp_dst != dst)
			memcpy(dst, tmp_dst, bsize);

		src += bsize;
		dst += bsize;
	} while ((done += bsize) <= nbytes);

	return done;
}
*/

static bool dmcrypt_encrypt_block(
	block_t block,
	unsigned char * block_buffer,
	struct crypt_alg_key * key,
	struct dm_crypt_iv * iv
) {
	struct crypt_alg_ctx * ctx=key->makectx(key);
	if (!ctx) {
		ERROR("Couldn't create cipher ctx.");
		return false;
	}
	
	unsigned bs=key->alg->block_size;
	assert(bs == iv->iv_size);
	u32 v[bs/4];
	iv->generate(v,block);
	for (int i=512/bs;i--;) {
		for (int j=0;j<4;++j) {
			((u32*)block_buffer)[j]^=v[j];
		}
		ctx->encrypt(ctx,block_buffer,block_buffer);
		memcpy(v,block_buffer,bs);
		block_buffer+=bs;
	}
	ctx->killctx(ctx);
	
	return true;
}

static bool dmcrypt_decrypt_block(
	block_t block,
	unsigned char * block_buffer,
	struct crypt_alg_key * key,
	struct dm_crypt_iv * iv
) {
	struct crypt_alg_ctx * ctx=key->makectx(key);
	if (!ctx) {
		ERROR("Couldn't create cipher ctx.");
		return false;
	}
	
	unsigned bs=key->alg->block_size;
	assert(bs == iv->iv_size);
	u32 v[bs/4];
	iv->generate(v,block);
	u32 tmp[bs/4];
	for (int i=512/bs;i--;) {
		ctx->decrypt(ctx,block_buffer,tmp);
		for (int j=0;j<4;++j) {
			tmp[j]^=v[j];
			v[j]=((u32*)block_buffer)[j];
			((u32*)block_buffer)[j]=tmp[j];
		}
		block_buffer+=bs;
	}
	ctx->killctx(ctx);
	
	return true;
}

static block_t dmcrypt_read(void * _private,block_t first,block_t num,unsigned char * data) {
	struct dmcrypt_bdev * dev=(struct dmcrypt_bdev *)_private;
	
	num=bdev_read(dev->backing_dev,first+1,num,data);
	
	if (num==(block_t)-1) return num;
	
	block_t n=num;
	
	while (n--) {
#ifdef VERIFY_ENCRYPTION
		unsigned char undecrypted[512];
		
		memcpy(undecrypted,data,512);
		
		eprintf("UD:");
		for (size_t i=0;i<16;++i) {
			eprintf(" %02x",(unsigned)undecrypted[i]);
		}
		eprintf("\n");
#endif // #ifdef VERIFY_ENCRYPTION
		
/*
		printf(
			"decrypt(%llu,%p,key,iv)\n"
			,(unsigned long long)first
			,data
		);
*/
		if (!dmcrypt_decrypt_block(first,data,dev->key,dev->iv)) {
			return -1;
		}
		
#ifdef VERIFY_ENCRYPTION
		unsigned char reencrypted[512];
		
		eprintf("DE:");
		for (size_t i=0;i<16;++i) {
			eprintf(" %02x",(unsigned)data[i]);
		}
		eprintf("\n");
		
		memcpy(reencrypted,data,512);
		
		dmcrypt_encrypt_block(first,reencrypted,dev->key,dev->iv);
		
		eprintf("RE:");
		for (size_t i=0;i<16;++i) {
			eprintf(" %02x",(unsigned)reencrypted[i]);
		}
		eprintf("\n");
		
		if (memcmp(undecrypted,reencrypted,512)) {
			ERROR("dmcrypt_read: undecrypted!=reencrypted");
			abort();
		}
#endif // #ifdef VERIFY_ENCRYPTION
		
/*
		struct crypt_alg_ctx * ctx=dev->key->makectx(dev->key);
		if (!ctx) {
			ERROR("Couldn't create cipher ctx.");
			return -1;
		}
		
		unsigned bs=ctx->key->alg->block_size;
		assert(bs == dev->iv->iv_size);
		u32 iv[bs/4];
		// dev->iv->use(ctx,first);
		dev->iv->generate(iv,first);
		u32 tmp[bs/4];
		for (int i=512/bs;i--;) {
			ctx->decrypt(ctx,data,tmp);
			for (int j=0;j<4;++j) {
				tmp[j]^=iv[j];
				iv[j]=((u32*)data)[j];
				((u32*)data)[j]=tmp[j];
			}
			data+=bs;
		}
		ctx->killctx(ctx);
*/
		data+=512;
		++first;
	}
	
	return num;
}

/*
static unsigned int cbc_process_encrypt(const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes)
{
	struct crypto_tfm *tfm = desc->tfm;
	void (*xor)(u8 *, const u8 *) = tfm->crt_u.cipher.cit_xor_block;
	int bsize = crypto_tfm_alg_blocksize(tfm);

	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) = desc->crfn;
	u8 *iv = desc->info;
	unsigned int done = 0;

	nbytes -= bsize;

	do {
		xor(iv, src);
		fn(tfm, dst, iv);
		memcpy(iv, dst, bsize);

		src += bsize;
		dst += bsize;
	} while ((done += bsize) <= nbytes);

	return done;
}
*/

/*
 * BROKEN:
block_t dmcrypt_write(void * _private,block_t first,block_t num,const unsigned char * data) {
	struct dmcrypt_bdev * dev=(struct dmcrypt_bdev *)_private;
	
	block_t n=num;
	block_t f=first;
	
	unsigned char * encrypted=malloc(512*num);;
	if (!encrypted) {
		return -1;
	}
	
	unsigned char * eptr=encrypted;
	while (n) {
		dmcrypt_encrypt_block(first,reencrypted,dev->key,dev->iv);
		
		// dmcrypt_encrypt_block(first,reencrypted,dev->key,dev->iv);
		struct crypt_alg_ctx * ctx=dev->key->makectx(dev->key);
		if (!ctx) {
			ERROR("Couldn't create cipher ctx.");
			return -1;
		}
		
//		dev->iv->use(ctx,f);
		for (int i=512/ctx->key->alg->block_size;i--;) {
			ctx->encrypt(ctx,data,eptr);
			data+=ctx->key->alg->block_size;
			eptr+=ctx->key->alg->block_size;
		}
		ctx->killctx(ctx);
		++f;
		--n;
	}
	
	block_t retval=bdev_write(dev->backing_dev,first+1,num,encrypted);
	
	free(encrypted);
	
	return retval;
}
*/

static block_t dmcrypt_write(void * _private,block_t first,block_t num,const unsigned char * data) {
	struct dmcrypt_bdev * dev=(struct dmcrypt_bdev *)_private;
	
	block_t n=num;
	
	while (n--) {
#ifdef VERIFY_ENCRYPTION
		unsigned char unencrypted[512];
		
		memcpy(unencrypted,data,512);
		
		eprintf("UE:");
		for (size_t i=0;i<16;++i) {
			eprintf(" %02x",(unsigned)unencrypted[i]);
		}
		eprintf("\n");
#endif // #ifdef VERIFY_ENCRYPTION
		
/*
		printf(
			"decrypt(%llu,%p,key,iv)\n"
			,(unsigned long long)first
			,data
		);
*/
		if (!dmcrypt_encrypt_block(first,data,dev->key,dev->iv)) {
			return -1;
		}
		
#ifdef VERIFY_ENCRYPTION
		unsigned char redecrypted[512];
		
		eprintf("EN:");
		for (size_t i=0;i<16;++i) {
			eprintf(" %02x",(unsigned)data[i]);
		}
		eprintf("\n");
		
		memcpy(redecrypted,data,512);
		
		dmcrypt_decrypt_block(first,redecrypted,dev->key,dev->iv);
		
		eprintf("RD:");
		for (size_t i=0;i<16;++i) {
			eprintf(" %02x",(unsigned)redecrypted[i]);
		}
		eprintf("\n");
		
		if (memcmp(unencrypted,redecrypted,512)) {
			ERROR("dmcrypt_write: unencrypted!=redecrypted");
			abort();
		}
#endif // #ifdef VERIFY_ENCRYPTION
		
/*
		struct crypt_alg_ctx * ctx=dev->key->makectx(dev->key);
		if (!ctx) {
			ERROR("Couldn't create cipher ctx.");
			return -1;
		}
		
		unsigned bs=ctx->key->alg->block_size;
		assert(bs == dev->iv->iv_size);
		u32 iv[bs/4];
		// dev->iv->use(ctx,first);
		dev->iv->generate(iv,first);
		u32 tmp[bs/4];
		for (int i=512/bs;i--;) {
			ctx->decrypt(ctx,data,tmp);
			for (int j=0;j<4;++j) {
				tmp[j]^=iv[j];
				iv[j]=((u32*)data)[j];
				((u32*)data)[j]=tmp[j];
			}
			data+=bs;
		}
		ctx->killctx(ctx);
*/
		data+=512;
		++first;
	}
	
	data-=512*num;
	first-=num;
	
	return bdev_write(dev->backing_dev,first+1,num,data);
}

bool dmcrypt_destroy(void * _private) {
	struct dmcrypt_bdev * dev=(struct dmcrypt_bdev *)_private;
	
	free(dev);
	
	return true;
}

#if 0

/*
	cc->iv_size = max(crypto_tfm_alg_ivsize(tfm),(unsigned int)(sizeof(u64) / sizeof(u8)));
*/

/*
 * Decode key from its hex representation
 *
 * Returns the key length or -1 on error
 */
/*
static int crypt_decode_key(u8 * key,char * hex) {
	char buffer[3];
	char * endp;
	unsigned int i;
	
	unsigned int size=strlen(hex)>>1;
	
	buffer[2]=0;
	
	for (i=0;i<size;i++) {
		buffer[0]=*hex++;
		buffer[1]=*hex++;
		
		key[i]=(u8)strtoul(buffer,&endp,16);
		
		if (endp!=buffer+2) {
			errno=EINVAL;
			return -1;
		}
	}
	
	if (*hex) {
		errno=EINVAL;
		return -1;
	}
	
	return size;
}
*/

/*
 * Encode key into its hex representation
 */
/*
static void crypt_encode_key(char *hex, u8 *key, unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++) {
		sprintf(hex, "%02x", *key);
		hex += 2;
		key++;
	}
}
*/

/*
static bool crypt_set_key(struct crypt_config * cc,char * key) {
	cc->key_size=crypt_decode_key(cc->key,key);
}

static int crypt_wipe_key(struct crypt_config *cc)
{
	clear_bit(DM_CRYPT_KEY_VALID, &cc->flags);
	memset(&cc->key, 0, cc->key_size * sizeof(u8));
	return 0;
}
*/

// dmcrypt:<new-dev>=aes(32,ripemd160("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefg"))
//
// name will be reowned by init, args will be freed by the caller.
static struct dev * dmcrypt_init(char * name,char * args) {
	struct crypt_config * cc=NULL;
	char * argv[]=NULL;
	int argc;
	if (!args_split(args,&argc,&argv)) {
		ERROR("Couldn't split argument string");
		return NULL;
	}
	
	if (argc!=5) {
		ERROR("Wrong number of arguments (must be 5).");
		return NULL;
	}
	
	char * tmp=argv[0];
	char * cipher=strsep(&tmp,"-");
	char * chainmode=strsep(&tmp,"-");
	char * ivmode=strsep(&tmp,"-");
	char * ivopts=strsep(&ivopts,":");
	
	eprintf(
		"cipher=%s\n"
		"chainm=%s\n"
		"ivmode=%s\n"
		"ivopts=%s\n"
		,cipher
		,chainmode
		,ivmode
		,ivopts
	);
	
	if (tmp) {
		WARNING("Unexpected additional cipher options");
	}
	
	unsigned int key_size=strlen(argv[1])>>1;
	cc=malloc(sizeof(*cc));
	if (!cc) {
		ERROR("Cannot allocate transparent encryption context");
		goto err;
	}
	memset(cc,0,sizeof(*cc));
	cc->key=malloc(key_size);
	if (!cc->key) {
		ERROR("Cannot allocate memory for encryption key");
		free(cc);
		goto err;
	}
	
	cc->key_size=crypt_decode_key(cc->key,argv[1]);
	if (cc->key_size<0) {
		ERROR("Error decoding key");
		goto err;
	}
	
	if (strcmp(chainmode,"ecb") && !ivmode) {
		ERROR("This chaining mode requires an IV mechanism");
		goto err;
	}
	
	if (snprintf(cc->cipher,CRYPTO_MAX_ALG_NAME,"%s(%s)",chainmode,cipher)>=CRYPTO_MAX_ALG_NAME) {
		ERROR("Chain mode + cipher name is too long");
		goto err;
	}
	
	tfm=crypto_alloc_blkcipher(cc->cipher,0,CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		ERROR("Error allocating crypto tfm");
		goto err;
	}
	
	strcpy(cc->cipher,cipher);
	strcpy(cc->chainmode,chainmode);
	cc->tfm=tfm;
	
	/*
	 * Choose ivmode. Valid modes: "plain", "essiv:<esshash>".
	 * See comments at iv code
	 */
	if (!ivmode) {
		cc->iv_gen_ops=NULL;
	} else if (!strcmp(ivmode,"plain")) {
		cc->iv_gen_ops=&crypt_iv_plain_ops;
	} else if (!strcmp(ivmode,"essiv")) {
		cc->iv_gen_ops=&crypt_iv_essiv_ops;
	} else {
		ERROR("Invalid IV mode");
		goto err;
	}
	
#endif // #if 0
