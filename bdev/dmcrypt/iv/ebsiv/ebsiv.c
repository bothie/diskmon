/*
 * diskmon is Copyright (C) 2007-2013 by Bodo Thiesen <bothie@gmx.de>
 */

#include "crypt.h"

/*
 * encrypted block salt IV generation algorithm:
 *
 * The block number is encrypted with the bulk cipher using a salt as key. The 
 * salt should be derived from the bulk cipher's key via hashing.
 *
 * plumb: unimplemented, see:
 * http://article.gmane.org/gmane.linux.kernel.device-mapper.dm-crypt/454
 *
 * This IV method also known under the wrong abbreviation essiv (for encrypted 
 * SECTOR salt IV).
 */

static int crypt_iv_ebsiv_ctr(struct crypt_config *cc, struct dm_target *ti,
	                      const char *opts)
{
	struct crypto_tfm *ebsiv_tfm;
	struct crypto_tfm *hash_tfm;
	struct scatterlist sg;
	unsigned int saltsize;
	u8 *salt;

	if (opts == NULL) {
		ti->error = PFX "Digest algorithm missing for EBSIV mode";
		return -EINVAL;
	}

	/* Hash the cipher key with the given hash algorithm */
	hash_tfm = crypto_alloc_tfm(opts, CRYPTO_TFM_REQ_MAY_SLEEP);
	if (hash_tfm == NULL) {
		ti->error = PFX "Error initializing EBSIV hash";
		return -EINVAL;
	}

	if (crypto_tfm_alg_type(hash_tfm) != CRYPTO_ALG_TYPE_DIGEST) {
		ti->error = PFX "Expected digest algorithm for EBSIV hash";
		crypto_free_tfm(hash_tfm);
		return -EINVAL;
	}

	saltsize = crypto_tfm_alg_digestsize(hash_tfm);
	salt = kmalloc(saltsize, GFP_KERNEL);
	if (salt == NULL) {
		ti->error = PFX "Error kmallocing salt storage in EBSIV";
		crypto_free_tfm(hash_tfm);
		return -ENOMEM;
	}

	sg_set_buf(&sg, cc->key, cc->key_size);
	crypto_digest_digest(hash_tfm, &sg, 1, salt);
	crypto_free_tfm(hash_tfm);

	/* Setup the ebsiv_tfm with the given salt */
	ebsiv_tfm = crypto_alloc_tfm(crypto_tfm_alg_name(cc->tfm),
	                             CRYPTO_TFM_MODE_ECB |
	                             CRYPTO_TFM_REQ_MAY_SLEEP);
	if (ebsiv_tfm == NULL) {
		ti->error = PFX "Error allocating crypto tfm for EBSIV";
		kfree(salt);
		return -EINVAL;
	}
	if (crypto_tfm_alg_blocksize(ebsiv_tfm)
	    != crypto_tfm_alg_ivsize(cc->tfm)) {
		ti->error = PFX "Block size of EBSIV cipher does "
			        "not match IV size of block cipher";
		crypto_free_tfm(ebsiv_tfm);
		kfree(salt);
		return -EINVAL;
	}
	if (crypto_cipher_setkey(ebsiv_tfm, salt, saltsize) < 0) {
		ti->error = PFX "Failed to set key for EBSIV cipher";
		crypto_free_tfm(ebsiv_tfm);
		kfree(salt);
		return -EINVAL;
	}
	kfree(salt);

	cc->iv_gen_private = (void *)ebsiv_tfm;
	return 0;
}

static void crypt_iv_ebsiv_dtr(struct crypt_config *cc)
{
	crypto_free_tfm((struct crypto_tfm *)cc->iv_gen_private);
	cc->iv_gen_private = NULL;
}

static int crypt_iv_ebsiv_gen(struct crypt_config *cc, u8 *iv, sector_t sector)
{
	struct scatterlist sg;

	memset(iv, 0, cc->iv_size);
	*(u64 *)iv = cpu_to_le64(sector);

	sg_set_buf(&sg, iv, cc->iv_size);
	crypto_cipher_encrypt((struct crypto_tfm *)cc->iv_gen_private,
	                      &sg, &sg, cc->iv_size);

	return 0;
}

static struct crypt_iv_operations crypt_iv_ebsiv_ops = {
	.ctr       = crypt_iv_ebsiv_ctr,
	.dtr       = crypt_iv_ebsiv_dtr,
	.generator = crypt_iv_ebsiv_gen
};

CRYPT_INIT {
	crypt_register_iv("ebsiv",&crypt_iv_ebsiv_ops);
}

CRYPT_FINI {
	crypt_deregister_iv("ebsiv");
}
