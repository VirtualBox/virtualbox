/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2009 by Aris Adamantiadis
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */

#include "libssh/priv.h"
#include "libssh/session.h"
#include "libssh/crypto.h"
#include "libssh/wrapper.h"
#include "libssh/libcrypto.h"
#include "libssh/pki.h"
#ifdef HAVE_OPENSSL_EVP_CHACHA20
#include "libssh/bytearray.h"
#include "libssh/chacha20-poly1305-common.h"
#endif

#ifdef HAVE_LIBCRYPTO

#include <openssl/err.h>
#include <openssl/md5.h>
#include <openssl/opensslv.h>
#include <openssl/sha.h>
#if OPENSSL_VERSION_NUMBER < 0x30000000L
#include <openssl/rsa.h>
#include <openssl/hmac.h>
#else
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <openssl/provider.h>
#endif /* OPENSSL_VERSION_NUMBER */
#include <openssl/rand.h>
#if defined(WITH_PKCS11_URI) && !defined(WITH_PKCS11_PROVIDER)
#include <openssl/engine.h>
#endif

#include "libcrypto-compat.h"

#ifdef HAVE_OPENSSL_AES_H
#define HAS_AES
#include <openssl/aes.h>
#endif /* HAVE_OPENSSL_AES_H */
#ifdef HAVE_OPENSSL_DES_H
#define HAS_DES
#include <openssl/des.h>
#endif /* HAVE_OPENSSL_DES_H */

#if (defined(HAVE_VALGRIND_VALGRIND_H) && defined(HAVE_OPENSSL_IA32CAP_LOC))
#include <valgrind/valgrind.h>
#define CAN_DISABLE_AESNI
#endif

#include "libssh/crypto.h"

#ifdef HAVE_OPENSSL_EVP_KDF_CTX
#include <openssl/kdf.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/param_build.h>
#include <openssl/core_names.h>
#endif /* OPENSSL_VERSION_NUMBER */
#endif /* HAVE_OPENSSL_EVP_KDF_CTX */

#include "libssh/crypto.h"

static int libcrypto_initialized = 0;


void ssh_reseed(void){
#ifndef _WIN32
    struct timeval tv;
    gettimeofday(&tv, NULL);
    RAND_add(&tv, sizeof(tv), 0.0);
#endif
}

#if defined(WITH_PKCS11_URI)
#if defined(WITH_PKCS11_PROVIDER)
static OSSL_PROVIDER *provider = NULL;
static bool pkcs11_provider_failed = false;

int pki_load_pkcs11_provider(void)
{
    if (OSSL_PROVIDER_available(NULL, "pkcs11") == 1) {
        /* the provider is already available.
         * Loaded through a configuration file? */
        return SSH_OK;
    }

    if (pkcs11_provider_failed) {
        /* the loading failed previously -- do not retry */
        return SSH_ERROR;
    }

    provider = OSSL_PROVIDER_try_load(NULL, "pkcs11", 1);
    if (provider != NULL) {
        return SSH_OK;
    }

    SSH_LOG(SSH_LOG_TRACE,
            "Failed to load the pkcs11 provider: %s",
            ERR_error_string(ERR_get_error(), NULL));
    /* Do not attempt to load it again */
    pkcs11_provider_failed = true;
    return SSH_ERROR;
}
#else
static ENGINE *engine = NULL;

ENGINE *pki_get_engine(void)
{
    int ok;

    if (engine == NULL) {
        ENGINE_load_builtin_engines();

        engine = ENGINE_by_id("pkcs11");
        if (engine == NULL) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Could not load the engine: %s",
                    ERR_error_string(ERR_get_error(), NULL));
            return NULL;
        }
        SSH_LOG(SSH_LOG_DEBUG, "Engine loaded successfully");

        ok = ENGINE_init(engine);
        if (!ok) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Could not initialize the engine: %s",
                    ERR_error_string(ERR_get_error(), NULL));
            ENGINE_free(engine);
            return NULL;
        }

        SSH_LOG(SSH_LOG_DEBUG, "Engine init success");
    }
    return engine;
}
#endif /* defined(WITH_PKCS11_PROVIDER) */
#endif /* defined(WITH_PKCS11_URI) */

#ifdef HAVE_OPENSSL_EVP_KDF_CTX
#if OPENSSL_VERSION_NUMBER < 0x30000000L
static const EVP_MD *sshkdf_digest_to_md(enum ssh_kdf_digest digest_type)
{
    switch (digest_type) {
    case SSH_KDF_SHA1:
        return EVP_sha1();
    case SSH_KDF_SHA256:
        return EVP_sha256();
    case SSH_KDF_SHA384:
        return EVP_sha384();
    case SSH_KDF_SHA512:
        return EVP_sha512();
    }
    return NULL;
}
#else
static const char *sshkdf_digest_to_md(enum ssh_kdf_digest digest_type)
{
    switch (digest_type) {
    case SSH_KDF_SHA1:
        return SN_sha1;
    case SSH_KDF_SHA256:
        return SN_sha256;
    case SSH_KDF_SHA384:
        return SN_sha384;
    case SSH_KDF_SHA512:
        return SN_sha512;
    }
    return NULL;
}
#endif /* OPENSSL_VERSION_NUMBER */

int ssh_kdf(struct ssh_crypto_struct *crypto,
            unsigned char *key, size_t key_len,
            uint8_t key_type, unsigned char *output,
            size_t requested_len)
{
    int ret = SSH_ERROR, rv;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EVP_KDF_CTX *ctx = EVP_KDF_CTX_new_id(EVP_KDF_SSHKDF);
#else
    EVP_KDF_CTX *ctx = NULL;
    OSSL_PARAM_BLD *param_bld = NULL;
    OSSL_PARAM *params = NULL;
    const char *md = NULL;
    EVP_KDF *kdf = NULL;

    md = sshkdf_digest_to_md(crypto->digest_type);
    if (md == NULL) {
        return -1;
    }

    kdf = EVP_KDF_fetch(NULL, "SSHKDF", NULL);
    if (kdf == NULL) {
        return -1;
    }
    ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);

    param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL) {
        EVP_KDF_CTX_free(ctx);
        return -1;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    if (ctx == NULL) {
        goto out;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    rv = EVP_KDF_ctrl(ctx,
                      EVP_KDF_CTRL_SET_MD,
                      sshkdf_digest_to_md(crypto->digest_type));
    if (rv != 1) {
        goto out;
    }
    rv = EVP_KDF_ctrl(ctx, EVP_KDF_CTRL_SET_KEY, key, key_len);
    if (rv != 1) {
        goto out;
    }
    rv = EVP_KDF_ctrl(ctx,
                      EVP_KDF_CTRL_SET_SSHKDF_XCGHASH,
                      crypto->secret_hash,
                      crypto->digest_len);
    if (rv != 1) {
        goto out;
    }
    rv = EVP_KDF_ctrl(ctx, EVP_KDF_CTRL_SET_SSHKDF_TYPE, key_type);
    if (rv != 1) {
        goto out;
    }
    rv = EVP_KDF_ctrl(ctx,
                      EVP_KDF_CTRL_SET_SSHKDF_SESSION_ID,
                      crypto->session_id,
                      crypto->session_id_len);
    if (rv != 1) {
        goto out;
    }
    rv = EVP_KDF_derive(ctx, output, requested_len);
    if (rv != 1) {
        goto out;
    }
#else
    rv = OSSL_PARAM_BLD_push_utf8_string(param_bld,
                                         OSSL_KDF_PARAM_DIGEST,
                                         md,
                                         strlen(md));
    if (rv != 1) {
        goto out;
    }
    rv = OSSL_PARAM_BLD_push_octet_string(param_bld,
                                          OSSL_KDF_PARAM_KEY,
                                          key,
                                          key_len);
    if (rv != 1) {
        goto out;
    }
    rv = OSSL_PARAM_BLD_push_octet_string(param_bld,
                                          OSSL_KDF_PARAM_SSHKDF_XCGHASH,
                                          crypto->secret_hash,
                                          crypto->digest_len);
    if (rv != 1) {
        goto out;
    }
    rv = OSSL_PARAM_BLD_push_octet_string(param_bld,
                                          OSSL_KDF_PARAM_SSHKDF_SESSION_ID,
                                          crypto->session_id,
                                          crypto->session_id_len);
    if (rv != 1) {
        goto out;
    }
    rv = OSSL_PARAM_BLD_push_utf8_string(param_bld,
                                         OSSL_KDF_PARAM_SSHKDF_TYPE,
                                         (const char *)&key_type,
                                         1);
    if (rv != 1) {
        goto out;
    }

    params = OSSL_PARAM_BLD_to_param(param_bld);
    if (params == NULL) {
        goto out;
    }

    rv = EVP_KDF_derive(ctx, output, requested_len, params);
    if (rv != 1) {
        goto out;
    }
#endif /* OPENSSL_VERSION_NUMBER */
    ret = SSH_OK;

out:
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    OSSL_PARAM_BLD_free(param_bld);
    OSSL_PARAM_free(params);
#endif
    EVP_KDF_CTX_free(ctx);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

#else
int ssh_kdf(struct ssh_crypto_struct *crypto,
            unsigned char *key, size_t key_len,
            uint8_t key_type, unsigned char *output,
            size_t requested_len)
{
    return sshkdf_derive_key(crypto, key, key_len,
                             key_type, output, requested_len);
}
#endif /* HAVE_OPENSSL_EVP_KDF_CTX */

HMACCTX hmac_init(const void *key, size_t len, enum ssh_hmac_e type)
{
    HMACCTX ctx = NULL;
    EVP_PKEY *pkey = NULL;
    int rc = -1;

    ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        return NULL;
    }

    pkey = EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, NULL, key, len);
    if (pkey == NULL) {
        goto error;
    }

    switch (type) {
    case SSH_HMAC_SHA1:
        rc = EVP_DigestSignInit(ctx, NULL, EVP_sha1(), NULL, pkey);
        break;
    case SSH_HMAC_SHA256:
        rc = EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, pkey);
        break;
    case SSH_HMAC_SHA512:
        rc = EVP_DigestSignInit(ctx, NULL, EVP_sha512(), NULL, pkey);
        break;
    case SSH_HMAC_MD5:
        rc = EVP_DigestSignInit(ctx, NULL, EVP_md5(), NULL, pkey);
        break;
    default:
        rc = -1;
        break;
    }

    EVP_PKEY_free(pkey);
    if (rc != 1) {
        goto error;
    }
    return ctx;

error:
    EVP_MD_CTX_free(ctx);
    return NULL;
}

int hmac_update(HMACCTX ctx, const void *data, size_t len)
{
    return EVP_DigestSignUpdate(ctx, data, len);
}

int hmac_final(HMACCTX ctx, unsigned char *hashmacbuf, size_t *len)
{
    size_t res = *len;
    int rc;
    rc = EVP_DigestSignFinal(ctx, hashmacbuf, &res);
    EVP_MD_CTX_free(ctx);
    if (rc == 1) {
        *len = res;
    }

    return rc;
}

static void evp_cipher_init(struct ssh_cipher_struct *cipher)
{
    if (cipher->ctx == NULL) {
        cipher->ctx = EVP_CIPHER_CTX_new();
    } else {
        EVP_CIPHER_CTX_init(cipher->ctx);
    }

    switch(cipher->ciphertype){
    case SSH_AES128_CBC:
        cipher->cipher = EVP_aes_128_cbc();
        break;
    case SSH_AES192_CBC:
        cipher->cipher = EVP_aes_192_cbc();
        break;
    case SSH_AES256_CBC:
        cipher->cipher = EVP_aes_256_cbc();
        break;
    case SSH_AES128_CTR:
        cipher->cipher = EVP_aes_128_ctr();
        break;
    case SSH_AES192_CTR:
        cipher->cipher = EVP_aes_192_ctr();
        break;
    case SSH_AES256_CTR:
        cipher->cipher = EVP_aes_256_ctr();
        break;
    case SSH_AEAD_AES128_GCM:
        cipher->cipher = EVP_aes_128_gcm();
        break;
    case SSH_AEAD_AES256_GCM:
        cipher->cipher = EVP_aes_256_gcm();
        break;
    case SSH_3DES_CBC:
        SSH_LOG(SSH_LOG_WARNING, "The DES cipher cannot be handled here");
        break;
#ifdef HAVE_BLOWFISH
    case SSH_BLOWFISH_CBC:
        cipher->cipher = EVP_bf_cbc();
        break;
        /* ciphers not using EVP */
#endif /* HAVE_BLOWFISH */
    case SSH_AEAD_CHACHA20_POLY1305:
        SSH_LOG(SSH_LOG_TRACE, "The ChaCha cipher cannot be handled here");
        break;
    case SSH_NO_CIPHER:
        SSH_LOG(SSH_LOG_TRACE, "No valid ciphertype found");
        break;
    }
}

static int evp_cipher_set_encrypt_key(struct ssh_cipher_struct *cipher,
            void *key, void *IV)
{
    int rc;

    evp_cipher_init(cipher);

    rc = EVP_EncryptInit_ex(cipher->ctx, cipher->cipher, NULL, key, IV);
    if (rc != 1){
        SSH_LOG(SSH_LOG_TRACE, "EVP_EncryptInit_ex failed");
        return SSH_ERROR;
    }

    /* For AES-GCM we need to set IV in specific way */
    if (cipher->ciphertype == SSH_AEAD_AES128_GCM ||
        cipher->ciphertype == SSH_AEAD_AES256_GCM) {
        rc = EVP_CIPHER_CTX_ctrl(cipher->ctx,
                                 EVP_CTRL_GCM_SET_IV_FIXED,
                                 -1,
                                 (uint8_t *)IV);
        if (rc != 1) {
            SSH_LOG(SSH_LOG_TRACE, "EVP_CTRL_GCM_SET_IV_FIXED failed");
            return SSH_ERROR;
        }
    }

    EVP_CIPHER_CTX_set_padding(cipher->ctx, 0);

    return SSH_OK;
}

static int evp_cipher_set_decrypt_key(struct ssh_cipher_struct *cipher,
            void *key, void *IV) {
    int rc;

    evp_cipher_init(cipher);

    rc = EVP_DecryptInit_ex(cipher->ctx, cipher->cipher, NULL, key, IV);
    if (rc != 1){
        SSH_LOG(SSH_LOG_TRACE, "EVP_DecryptInit_ex failed");
        return SSH_ERROR;
    }

    /* For AES-GCM we need to set IV in specific way */
    if (cipher->ciphertype == SSH_AEAD_AES128_GCM ||
        cipher->ciphertype == SSH_AEAD_AES256_GCM) {
        rc = EVP_CIPHER_CTX_ctrl(cipher->ctx,
                                 EVP_CTRL_GCM_SET_IV_FIXED,
                                 -1,
                                 (uint8_t *)IV);
        if (rc != 1) {
            SSH_LOG(SSH_LOG_TRACE, "EVP_CTRL_GCM_SET_IV_FIXED failed");
            return SSH_ERROR;
        }
    }

    EVP_CIPHER_CTX_set_padding(cipher->ctx, 0);

    return SSH_OK;
}

/* EVP wrapper function for encrypt/decrypt */
static void evp_cipher_encrypt(struct ssh_cipher_struct *cipher,
                               void *in,
                               void *out,
                               size_t len)
{
    int outlen = 0;
    int rc = 0;

    rc = EVP_EncryptUpdate(cipher->ctx,
                           (unsigned char *)out,
                           &outlen,
                           (unsigned char *)in,
                           (int)len);
    if (rc != 1){
        SSH_LOG(SSH_LOG_TRACE, "EVP_EncryptUpdate failed");
        return;
    }
    if (outlen != (int)len){
        SSH_LOG(SSH_LOG_DEBUG,
                "EVP_EncryptUpdate: output size %d for %zu in",
                outlen,
                len);
        return;
    }
}

static void evp_cipher_decrypt(struct ssh_cipher_struct *cipher,
                               void *in,
                               void *out,
                               size_t len)
{
    int outlen = 0;
    int rc = 0;

    rc = EVP_DecryptUpdate(cipher->ctx,
                           (unsigned char *)out,
                           &outlen,
                           (unsigned char *)in,
                           (int)len);
    if (rc != 1){
        SSH_LOG(SSH_LOG_TRACE, "EVP_DecryptUpdate failed");
        return;
    }
    if (outlen != (int)len){
        SSH_LOG(SSH_LOG_DEBUG,
                "EVP_DecryptUpdate: output size %d for %zu in",
                outlen,
                len);
        return;
    }
}

static void evp_cipher_cleanup(struct ssh_cipher_struct *cipher) {
    if (cipher->ctx != NULL) {
        EVP_CIPHER_CTX_free(cipher->ctx);
    }
}

static int
evp_cipher_aead_get_length(struct ssh_cipher_struct *cipher,
                           void *in,
                           uint8_t *out,
                           size_t len,
                           uint64_t seq)
{
    (void)cipher;
    (void)seq;

    /* The length is not encrypted: Copy it to the result buffer */
    memcpy(out, in, len);

    return SSH_OK;
}

static void
evp_cipher_aead_encrypt(struct ssh_cipher_struct *cipher,
                        void *in,
                        void *out,
                        size_t len,
                        uint8_t *tag,
                        uint64_t seq)
{
    size_t authlen, aadlen;
    uint8_t lastiv[1];
    int tmplen = 0;
    size_t outlen;
    int rc;

    (void) seq;

    aadlen = cipher->lenfield_blocksize;
    authlen = cipher->tag_size;

    /* increment IV */
    rc = EVP_CIPHER_CTX_ctrl(cipher->ctx,
                             EVP_CTRL_GCM_IV_GEN,
                             1,
                             lastiv);
    if (rc == 0) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CTRL_GCM_IV_GEN failed");
        return;
    }

    /* Pass over the authenticated data (not encrypted) */
    rc = EVP_EncryptUpdate(cipher->ctx,
                           NULL,
                           &tmplen,
                           (unsigned char *)in,
                           (int)aadlen);
    outlen = tmplen;
    if (rc == 0 || outlen != aadlen) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to pass authenticated data");
        return;
    }
    memcpy(out, in, aadlen);

    /* Encrypt the rest of the data */
    rc = EVP_EncryptUpdate(cipher->ctx,
                           (unsigned char *)out + aadlen,
                           &tmplen,
                           (unsigned char *)in + aadlen,
                           (int)len - aadlen);
    outlen = tmplen;
    if (rc != 1 || outlen != (int)len - aadlen) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_EncryptUpdate failed");
        return;
    }

    /* compute tag */
    rc = EVP_EncryptFinal(cipher->ctx,
                          NULL,
                          &tmplen);
    if (rc < 0) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_EncryptFinal failed: Failed to create a tag");
        return;
    }

    rc = EVP_CIPHER_CTX_ctrl(cipher->ctx,
                             EVP_CTRL_GCM_GET_TAG,
                             authlen,
                             (unsigned char *)tag);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CTRL_GCM_GET_TAG failed");
        return;
    }
}

static int
evp_cipher_aead_decrypt(struct ssh_cipher_struct *cipher,
                        void *complete_packet,
                        uint8_t *out,
                        size_t encrypted_size,
                        uint64_t seq)
{
    size_t authlen, aadlen;
    uint8_t lastiv[1];
    int outlen = 0;
    int rc = 0;

    (void)seq;

    aadlen = cipher->lenfield_blocksize;
    authlen = cipher->tag_size;

    /* increment IV */
    rc = EVP_CIPHER_CTX_ctrl(cipher->ctx,
                             EVP_CTRL_GCM_IV_GEN,
                             1,
                             lastiv);
    if (rc == 0) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CTRL_GCM_IV_GEN failed");
        return SSH_ERROR;
    }

    /* set tag for authentication */
    rc = EVP_CIPHER_CTX_ctrl(cipher->ctx,
                             EVP_CTRL_GCM_SET_TAG,
                             authlen,
                             (unsigned char *)complete_packet + aadlen + encrypted_size);
    if (rc == 0) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CTRL_GCM_SET_TAG failed");
        return SSH_ERROR;
    }

    /* Pass over the authenticated data (not encrypted) */
    rc = EVP_DecryptUpdate(cipher->ctx,
                           NULL,
                           &outlen,
                           (unsigned char *)complete_packet,
                           (int)aadlen);
    if (rc == 0) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to pass authenticated data");
        return SSH_ERROR;
    }
    /* Do not copy the length to the target buffer, because it is already processed */
    //memcpy(out, complete_packet, aadlen);

    /* Decrypt the rest of the data */
    rc = EVP_DecryptUpdate(cipher->ctx,
                           (unsigned char *)out,
                           &outlen,
                           (unsigned char *)complete_packet + aadlen,
                           encrypted_size /* already subtracted aadlen */);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_DecryptUpdate failed");
        return SSH_ERROR;
    }

    if (outlen != (int)encrypted_size) {
        SSH_LOG(SSH_LOG_TRACE,
                "EVP_DecryptUpdate: output size %d for %zd in",
                outlen,
                encrypted_size);
        return SSH_ERROR;
    }

    /* verify tag */
    rc = EVP_DecryptFinal(cipher->ctx,
                          NULL,
                          &outlen);
    if (rc < 0) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_DecryptFinal failed: Failed authentication");
        return SSH_ERROR;
    }

    return SSH_OK;
}

#ifdef HAVE_OPENSSL_EVP_CHACHA20

struct chacha20_poly1305_keysched {
    /* cipher handle used for encrypting the packets */
    EVP_CIPHER_CTX *main_evp;
    /* cipher handle used for encrypting the length field */
    EVP_CIPHER_CTX *header_evp;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    /* mac handle used for authenticating the packets */
    EVP_PKEY_CTX *pctx;
    /* Poly1305 key */
    EVP_PKEY *key;
    /* MD context for digesting data in poly1305 */
    EVP_MD_CTX *mctx;
#else
    /* MAC context used to do poly1305 */
    EVP_MAC_CTX *mctx;
#endif /* OPENSSL_VERSION_NUMBER */
};

static void
chacha20_poly1305_cleanup(struct ssh_cipher_struct *cipher)
{
    struct chacha20_poly1305_keysched *ctx = NULL;

    if (cipher->chacha20_schedule == NULL) {
        return;
    }

    ctx = cipher->chacha20_schedule;

    EVP_CIPHER_CTX_free(ctx->main_evp);
    ctx->main_evp  = NULL;
    EVP_CIPHER_CTX_free(ctx->header_evp);
    ctx->header_evp = NULL;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    /* ctx->pctx is freed as part of MD context */
    EVP_PKEY_free(ctx->key);
    ctx->key = NULL;
    EVP_MD_CTX_free(ctx->mctx);
    ctx->mctx = NULL;
#else
    EVP_MAC_CTX_free(ctx->mctx);
    ctx->mctx = NULL;
#endif /* OPENSSL_VERSION_NUMBER */

    SAFE_FREE(cipher->chacha20_schedule);
}

static int
chacha20_poly1305_set_key(struct ssh_cipher_struct *cipher,
                          void *key,
                          UNUSED_PARAM(void *IV))
{
    struct chacha20_poly1305_keysched *ctx = NULL;
    uint8_t *u8key = key;
    int ret = SSH_ERROR, rv;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    EVP_MAC *mac = NULL;
#endif

    if (cipher->chacha20_schedule == NULL) {
        ctx = calloc(1, sizeof(*ctx));
        if (ctx == NULL) {
            return -1;
        }
        cipher->chacha20_schedule = ctx;
    } else {
        ctx = cipher->chacha20_schedule;
    }

    /* ChaCha20 initialization */
    /* K2 uses the first half of the key */
    ctx->main_evp = EVP_CIPHER_CTX_new();
    if (ctx->main_evp == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CIPHER_CTX_new failed");
        goto out;
    }
    rv = EVP_EncryptInit_ex(ctx->main_evp, EVP_chacha20(), NULL, u8key, NULL);
    if (rv != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CipherInit failed");
        goto out;
    }
    /* K1 uses the second half of the key */
    ctx->header_evp = EVP_CIPHER_CTX_new();
    if (ctx->header_evp == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CIPHER_CTX_new failed");
        goto out;
    }
    rv = EVP_EncryptInit_ex(ctx->header_evp, EVP_chacha20(), NULL,
                             u8key + CHACHA20_KEYLEN, NULL);
    if (rv != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CipherInit failed");
        goto out;
    }

    /* The Poly1305 key initialization is delayed to the time we know
     * the actual key for packet so we do not need to create a bogus keys
     */
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    ctx->mctx = EVP_MD_CTX_new();
    if (ctx->mctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_MD_CTX_new failed");
        return SSH_ERROR;
    }
#else
    mac = EVP_MAC_fetch(NULL, "poly1305", NULL);
    if (mac == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_MAC_fetch failed");
        goto out;
    }
    ctx->mctx = EVP_MAC_CTX_new(mac);
    if (ctx->mctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_MAC_CTX_new failed");
        goto out;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    ret = SSH_OK;
out:
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    EVP_MAC_free(mac);
#endif
    if (ret != SSH_OK) {
        chacha20_poly1305_cleanup(cipher);
    }
    return ret;
}

static const uint8_t zero_block[CHACHA20_BLOCKSIZE] = {0};

static int
chacha20_poly1305_set_iv(struct ssh_cipher_struct *cipher,
                         uint64_t seq,
                         int do_encrypt)
{
    struct chacha20_poly1305_keysched *ctx = cipher->chacha20_schedule;
    uint8_t seqbuf[16] = {0};
    int ret;

    /* Prepare the IV for OpenSSL -- it needs to be 128 b long. First 32 b is
     * counter the rest is nonce. The memory is initialized to zeros
     * (counter starts from 0) and we set the sequence number in the second half
     */
    PUSH_BE_U64(seqbuf, 8, seq);
#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("seqbuf (chacha20 IV)", seqbuf, sizeof(seqbuf));
#endif /* DEBUG_CRYPTO */

    ret = EVP_CipherInit_ex(ctx->header_evp, NULL, NULL, NULL, seqbuf, do_encrypt);
    if (ret != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CipherInit_ex(header_evp) failed");
        return SSH_ERROR;
    }

    ret = EVP_CipherInit_ex(ctx->main_evp, NULL, NULL, NULL, seqbuf, do_encrypt);
    if (ret != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CipherInit_ex(main_evp) failed");
        return SSH_ERROR;
    }

    return SSH_OK;
}

static int
chacha20_poly1305_packet_setup(struct ssh_cipher_struct *cipher,
                               uint64_t seq,
                               int do_encrypt)
{
    struct chacha20_poly1305_keysched *ctx = cipher->chacha20_schedule;
    uint8_t poly_key[CHACHA20_BLOCKSIZE];
    int ret = SSH_ERROR, len, rv;

    /* The initialization for decrypt was already done with the length block */
    if (do_encrypt) {
        rv = chacha20_poly1305_set_iv(cipher, seq, do_encrypt);
        if (rv != SSH_OK) {
            return SSH_ERROR;
        }
    }

    /* Output full ChaCha block so that counter increases by one for
     * next step. */
    rv = EVP_CipherUpdate(ctx->main_evp, poly_key, &len,
                           (unsigned char *)zero_block, sizeof(zero_block));
    if (rv != 1 || len != CHACHA20_BLOCKSIZE) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_EncryptUpdate failed");
        goto out;
    }
#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("poly_key", poly_key, POLY1305_KEYLEN);
#endif /* DEBUG_CRYPTO */

    /* Set the Poly1305 key */
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    if (ctx->key == NULL) {
        /* Poly1305 Initialization needs to know the actual key */
        ctx->key = EVP_PKEY_new_mac_key(EVP_PKEY_POLY1305, NULL,
                                        poly_key, POLY1305_KEYLEN);
        if (ctx->key == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "EVP_PKEY_new_mac_key failed");
            goto out;
        }
        rv = EVP_DigestSignInit(ctx->mctx, &ctx->pctx, NULL, NULL, ctx->key);
        if (rv != 1) {
            SSH_LOG(SSH_LOG_TRACE, "EVP_DigestSignInit failed");
            goto out;
        }
    } else {
        /* Updating the key is easier but less obvious */
        rv = EVP_PKEY_CTX_ctrl(ctx->pctx, -1, EVP_PKEY_OP_SIGNCTX,
                                EVP_PKEY_CTRL_SET_MAC_KEY,
                                POLY1305_KEYLEN, (void *)poly_key);
        if (rv <= 0) {
            SSH_LOG(SSH_LOG_TRACE, "EVP_PKEY_CTX_ctrl failed");
            goto out;
        }
    }
#else
    rv = EVP_MAC_init(ctx->mctx, poly_key, POLY1305_KEYLEN, NULL);
    if (rv != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_MAC_init failed");
        goto out;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    ret = SSH_OK;
out:
    explicit_bzero(poly_key, sizeof(poly_key));
    return ret;
}

static int
chacha20_poly1305_aead_decrypt_length(struct ssh_cipher_struct *cipher,
                                      void *in,
                                      uint8_t *out,
                                      size_t len,
                                      uint64_t seq)
{
    struct chacha20_poly1305_keysched *ctx = cipher->chacha20_schedule;
    int rv, outlen;

    if (len < sizeof(uint32_t)) {
        return SSH_ERROR;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("encrypted length", (uint8_t *)in, sizeof(uint32_t));
#endif /* DEBUG_CRYPTO */

    /* Set IV for the header EVP */
    rv = chacha20_poly1305_set_iv(cipher, seq, 0);
    if (rv != SSH_OK) {
        return SSH_ERROR;
    }

    rv = EVP_CipherUpdate(ctx->header_evp, out, &outlen, in, len);
    if (rv != 1 || outlen != sizeof(uint32_t)) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CipherUpdate failed");
        return SSH_ERROR;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("deciphered length", out, sizeof(uint32_t));
#endif /* DEBUG_CRYPTO */

    rv = EVP_CipherFinal_ex(ctx->header_evp, out + outlen, &outlen);
    if (rv != 1 || outlen != 0) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CipherFinal_ex failed");
        return SSH_ERROR;
    }

    return SSH_OK;
}

static int
chacha20_poly1305_aead_decrypt(struct ssh_cipher_struct *cipher,
                               void *complete_packet,
                               uint8_t *out,
                               size_t encrypted_size,
                               uint64_t seq)
{
    struct chacha20_poly1305_keysched *ctx = cipher->chacha20_schedule;
    uint8_t *mac = (uint8_t *)complete_packet + sizeof(uint32_t) +
                   encrypted_size;
    uint8_t tag[POLY1305_TAGLEN] = {0};
    int ret = SSH_ERROR;
    int rv, cmp, len = 0;
    size_t taglen = POLY1305_TAGLEN;

    /* Prepare the Poly1305 key */
    rv = chacha20_poly1305_packet_setup(cipher, seq, 0);
    if (rv != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to setup packet");
        goto out;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("received mac", mac, POLY1305_TAGLEN);
#endif /* DEBUG_CRYPTO */

    /* Calculate MAC of received data */
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    rv = EVP_DigestSignUpdate(ctx->mctx, complete_packet,
                              encrypted_size + sizeof(uint32_t));
    if (rv != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_DigestSignUpdate failed");
        goto out;
    }

    rv = EVP_DigestSignFinal(ctx->mctx, tag, &taglen);
    if (rv != 1) {
        SSH_LOG(SSH_LOG_TRACE, "poly1305 verify error");
        goto out;
    }
#else
    rv = EVP_MAC_update(ctx->mctx, complete_packet,
                        encrypted_size + sizeof(uint32_t));
    if (rv != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_MAC_update failed");
        goto out;
    }

    rv = EVP_MAC_final(ctx->mctx, tag, &taglen, POLY1305_TAGLEN);
    if (rv != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_MAC_final failed");
        goto out;
    }
#endif /* OPENSSL_VERSION_NUMBER */

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("calculated mac", tag, POLY1305_TAGLEN);
#endif /* DEBUG_CRYPTO */

    /* Verify the calculated MAC matches the attached MAC */
    cmp = CRYPTO_memcmp(tag, mac, POLY1305_TAGLEN);
    if (cmp != 0) {
        /* mac error */
        SSH_LOG(SSH_LOG_PACKET, "poly1305 verify error");
        return SSH_ERROR;
    }

    /* Decrypt the message */
    rv = EVP_CipherUpdate(ctx->main_evp, out, &len,
                          (uint8_t *)complete_packet + sizeof(uint32_t),
                          encrypted_size);
    if (rv != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CipherUpdate failed");
        goto out;
    }

    rv = EVP_CipherFinal_ex(ctx->main_evp, out + len, &len);
    if (rv != 1 || len != 0) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CipherFinal_ex failed");
        goto out;
    }

    ret = SSH_OK;
out:
    return ret;
}

static void
chacha20_poly1305_aead_encrypt(struct ssh_cipher_struct *cipher,
                               void *in,
                               void *out,
                               size_t len,
                               uint8_t *tag,
                               uint64_t seq)
{
    struct ssh_packet_header *in_packet = in, *out_packet = out;
    struct chacha20_poly1305_keysched *ctx = cipher->chacha20_schedule;
    size_t taglen = POLY1305_TAGLEN;
    int ret, outlen = 0;

    /* Prepare the Poly1305 key */
    ret = chacha20_poly1305_packet_setup(cipher, seq, 1);
    if (ret != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to setup packet");
        return;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("plaintext length",
                    (unsigned char *)&in_packet->length, sizeof(uint32_t));
#endif /* DEBUG_CRYPTO */
    /* step 2, encrypt length field */
    ret = EVP_CipherUpdate(ctx->header_evp,
                           (unsigned char *)&out_packet->length,
                           &outlen,
                           (unsigned char *)&in_packet->length,
                           sizeof(uint32_t));
    if (ret != 1 || outlen != sizeof(uint32_t)) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CipherUpdate failed");
        return;
    }
#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("encrypted length",
                    (unsigned char *)&out_packet->length, outlen);
#endif /* DEBUG_CRYPTO */
    ret = EVP_CipherFinal_ex(ctx->header_evp, (uint8_t *)out + outlen, &outlen);
    if (ret != 1 || outlen != 0) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_EncryptFinal_ex failed");
        return;
    }

    /* step 3, encrypt packet payload (main_evp counter == 1) */
    /* We already did encrypt one block so the counter should be in the correct position */
    ret = EVP_CipherUpdate(ctx->main_evp,
                           out_packet->payload,
                           &outlen,
                           in_packet->payload,
                           len - sizeof(uint32_t));
    if (ret != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_CipherUpdate failed");
        return;
    }

    /* step 4, compute the MAC */
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    ret = EVP_DigestSignUpdate(ctx->mctx, out_packet, len);
    if (ret <= 0) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_DigestSignUpdate failed");
        return;
    }
    ret = EVP_DigestSignFinal(ctx->mctx, tag, &taglen);
    if (ret <= 0) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_DigestSignFinal failed");
        return;
    }
#else
    ret = EVP_MAC_update(ctx->mctx, (void*)out_packet, len);
    if (ret != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_MAC_update failed");
        return;
    }

    ret = EVP_MAC_final(ctx->mctx, tag, &taglen, POLY1305_TAGLEN);
    if (ret != 1) {
        SSH_LOG(SSH_LOG_TRACE, "EVP_MAC_final failed");
        return;
    }
#endif /* OPENSSL_VERSION_NUMBER */
}
#endif /* HAVE_OPENSSL_EVP_CHACHA20 */

#ifdef WITH_INSECURE_NONE
static void
none_crypt(UNUSED_PARAM(struct ssh_cipher_struct *cipher),
           void *in,
           void *out,
           size_t len)
{
    memcpy(out, in, len);
}
#endif /* WITH_INSECURE_NONE */

/*
 * The table of supported ciphers
 */
static struct ssh_cipher_struct ssh_ciphertab[] = {
#ifdef HAVE_BLOWFISH
  {
    .name = "blowfish-cbc",
    .blocksize = 8,
    .ciphertype = SSH_BLOWFISH_CBC,
    .keysize = 128,
    .set_encrypt_key = evp_cipher_set_encrypt_key,
    .set_decrypt_key = evp_cipher_set_decrypt_key,
    .encrypt = evp_cipher_encrypt,
    .decrypt = evp_cipher_decrypt,
    .cleanup = evp_cipher_cleanup
  },
#endif /* HAVE_BLOWFISH */
#ifdef HAS_AES
  {
    .name = "aes128-ctr",
    .blocksize = AES_BLOCK_SIZE,
    .ciphertype = SSH_AES128_CTR,
    .keysize = 128,
    .set_encrypt_key = evp_cipher_set_encrypt_key,
    .set_decrypt_key = evp_cipher_set_decrypt_key,
    .encrypt = evp_cipher_encrypt,
    .decrypt = evp_cipher_decrypt,
    .cleanup = evp_cipher_cleanup
  },
  {
    .name = "aes192-ctr",
    .blocksize = AES_BLOCK_SIZE,
    .ciphertype = SSH_AES192_CTR,
    .keysize = 192,
    .set_encrypt_key = evp_cipher_set_encrypt_key,
    .set_decrypt_key = evp_cipher_set_decrypt_key,
    .encrypt = evp_cipher_encrypt,
    .decrypt = evp_cipher_decrypt,
    .cleanup = evp_cipher_cleanup
  },
  {
    .name = "aes256-ctr",
    .blocksize = AES_BLOCK_SIZE,
    .ciphertype = SSH_AES256_CTR,
    .keysize = 256,
    .set_encrypt_key = evp_cipher_set_encrypt_key,
    .set_decrypt_key = evp_cipher_set_decrypt_key,
    .encrypt = evp_cipher_encrypt,
    .decrypt = evp_cipher_decrypt,
    .cleanup = evp_cipher_cleanup
  },
  {
    .name = "aes128-cbc",
    .blocksize = AES_BLOCK_SIZE,
    .ciphertype = SSH_AES128_CBC,
    .keysize = 128,
    .set_encrypt_key = evp_cipher_set_encrypt_key,
    .set_decrypt_key = evp_cipher_set_decrypt_key,
    .encrypt = evp_cipher_encrypt,
    .decrypt = evp_cipher_decrypt,
    .cleanup = evp_cipher_cleanup
  },
  {
    .name = "aes192-cbc",
    .blocksize = AES_BLOCK_SIZE,
    .ciphertype = SSH_AES192_CBC,
    .keysize = 192,
    .set_encrypt_key = evp_cipher_set_encrypt_key,
    .set_decrypt_key = evp_cipher_set_decrypt_key,
    .encrypt = evp_cipher_encrypt,
    .decrypt = evp_cipher_decrypt,
    .cleanup = evp_cipher_cleanup
  },
  {
    .name = "aes256-cbc",
    .blocksize = AES_BLOCK_SIZE,
    .ciphertype = SSH_AES256_CBC,
    .keysize = 256,
    .set_encrypt_key = evp_cipher_set_encrypt_key,
    .set_decrypt_key = evp_cipher_set_decrypt_key,
    .encrypt = evp_cipher_encrypt,
    .decrypt = evp_cipher_decrypt,
    .cleanup = evp_cipher_cleanup
  },
  {
    .name = "aes128-gcm@openssh.com",
    .blocksize = AES_BLOCK_SIZE,
    .lenfield_blocksize = 4, /* not encrypted, but authenticated */
    .ciphertype = SSH_AEAD_AES128_GCM,
    .keysize = 128,
    .tag_size = AES_GCM_TAGLEN,
    .set_encrypt_key = evp_cipher_set_encrypt_key,
    .set_decrypt_key = evp_cipher_set_decrypt_key,
    .aead_encrypt = evp_cipher_aead_encrypt,
    .aead_decrypt_length = evp_cipher_aead_get_length,
    .aead_decrypt = evp_cipher_aead_decrypt,
    .cleanup = evp_cipher_cleanup
  },
  {
    .name = "aes256-gcm@openssh.com",
    .blocksize = AES_BLOCK_SIZE,
    .lenfield_blocksize = 4, /* not encrypted, but authenticated */
    .ciphertype = SSH_AEAD_AES256_GCM,
    .keysize = 256,
    .tag_size = AES_GCM_TAGLEN,
    .set_encrypt_key = evp_cipher_set_encrypt_key,
    .set_decrypt_key = evp_cipher_set_decrypt_key,
    .aead_encrypt = evp_cipher_aead_encrypt,
    .aead_decrypt_length = evp_cipher_aead_get_length,
    .aead_decrypt = evp_cipher_aead_decrypt,
    .cleanup = evp_cipher_cleanup
  },
#endif /* HAS_AES */
#ifdef HAS_DES
  {
    .name = "3des-cbc",
    .blocksize = 8,
    .ciphertype = SSH_3DES_CBC,
    .keysize = 192,
    .set_encrypt_key = evp_cipher_set_encrypt_key,
    .set_decrypt_key = evp_cipher_set_decrypt_key,
    .encrypt = evp_cipher_encrypt,
    .decrypt = evp_cipher_decrypt,
    .cleanup = evp_cipher_cleanup
  },
#endif /* HAS_DES */
  {
#ifdef HAVE_OPENSSL_EVP_CHACHA20
    .ciphertype = SSH_AEAD_CHACHA20_POLY1305,
    .name = "chacha20-poly1305@openssh.com",
    .blocksize = CHACHA20_BLOCKSIZE/8,
    .lenfield_blocksize = 4,
    .keylen = sizeof(struct chacha20_poly1305_keysched),
    .keysize = 2 * CHACHA20_KEYLEN * 8,
    .tag_size = POLY1305_TAGLEN,
    .set_encrypt_key = chacha20_poly1305_set_key,
    .set_decrypt_key = chacha20_poly1305_set_key,
    .aead_encrypt = chacha20_poly1305_aead_encrypt,
    .aead_decrypt_length = chacha20_poly1305_aead_decrypt_length,
    .aead_decrypt = chacha20_poly1305_aead_decrypt,
    .cleanup = chacha20_poly1305_cleanup
#else
    .name = "chacha20-poly1305@openssh.com"
#endif /* HAVE_OPENSSL_EVP_CHACHA20 */
  },
#ifdef WITH_INSECURE_NONE
  {
    .name = "none",
    .blocksize = 8,
    .keysize = 0,
    .encrypt = none_crypt,
    .decrypt = none_crypt,
  },
#endif /* WITH_INSECURE_NONE */
  {
    .name = NULL
  }
};

struct ssh_cipher_struct *ssh_get_ciphertab(void)
{
  return ssh_ciphertab;
}

/**
 * @internal
 * @brief Initialize libcrypto's subsystem
 */
int ssh_crypto_init(void)
{
#ifndef HAVE_OPENSSL_EVP_CHACHA20
    size_t i;
#endif

    if (libcrypto_initialized) {
        return SSH_OK;
    }
    if (OpenSSL_version_num() != OPENSSL_VERSION_NUMBER){
        SSH_LOG(SSH_LOG_DEBUG, "libssh compiled with %s "
            "headers, currently running with %s.",
            OPENSSL_VERSION_TEXT,
            OpenSSL_version(OpenSSL_version_num())
        );
    }
#ifdef CAN_DISABLE_AESNI
    /*
     * disable AES-NI when running within Valgrind, because they generate
     * too many "uninitialized memory access" false positives
     */
    if (RUNNING_ON_VALGRIND){
        SSH_LOG(SSH_LOG_INFO, "Running within Valgrind, disabling AES-NI");
        /* Bit #57 denotes AES-NI instruction set extension */
        OPENSSL_ia32cap &= ~(1LL << 57);
    }
#endif /* CAN_DISABLE_AESNI */

#ifndef HAVE_OPENSSL_EVP_CHACHA20
    for (i = 0; ssh_ciphertab[i].name != NULL; i++) {
        int cmp;

        cmp = strcmp(ssh_ciphertab[i].name, "chacha20-poly1305@openssh.com");
        if (cmp == 0) {
            memcpy(&ssh_ciphertab[i],
                   ssh_get_chacha20poly1305_cipher(),
                   sizeof(struct ssh_cipher_struct));
            break;
        }
    }
#endif /* HAVE_OPENSSL_EVP_CHACHA20 */

    libcrypto_initialized = 1;

    return SSH_OK;
}

/**
 * @internal
 * @brief Finalize libcrypto's subsystem
 */
void ssh_crypto_finalize(void)
{
    if (!libcrypto_initialized) {
        return;
    }

/* TODO this should finalize engine if it was started, but during atexit calls,
 * we are crashing. AFAIK this is related to the dlopened pkcs11 modules calling
 * the crypto cleanups earlier. */
#if 0
    if (engine != NULL) {
        ENGINE_finish(engine);
        ENGINE_free(engine);
        engine = NULL;
    }
#endif
#if defined(WITH_PKCS11_URI)
#if defined(WITH_PKCS11_PROVIDER)
    if (provider != NULL) {
        OSSL_PROVIDER_unload(provider);
        provider = NULL;
    }
#endif /* WITH_PKCS11_PROVIDER */
#endif /* WITH_PKCS11_URI */

    libcrypto_initialized = 0;
}

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
/**
 * @internal
 * @brief Create EVP_PKEY from parameters
 *
 * @param[in] name Algorithm to use. For more info see manpage of EVP_PKEY_CTX_new_from_name
 *
 * @param[in] param_bld Constructed param builder for the pkey
 *
 * @param[out] pkey Created EVP_PKEY variable
 *
 * @param[in] selection Reference selections at man EVP_PKEY_FROMDATA
 *
 * @return 0 on success, -1 on error
 */
int evp_build_pkey(const char* name, OSSL_PARAM_BLD *param_bld,
                   EVP_PKEY **pkey, int selection)
{
    int rc;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, name, NULL);
    OSSL_PARAM *params = NULL;

    if (ctx == NULL) {
        return -1;
    }

    params = OSSL_PARAM_BLD_to_param(param_bld);
    if (params == NULL) {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }

    rc = EVP_PKEY_fromdata_init(ctx);
    if (rc != 1) {
        OSSL_PARAM_free(params);
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }

    rc = EVP_PKEY_fromdata(ctx, pkey, selection, params);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to import private key: %s\n",
                ERR_error_string(ERR_get_error(), NULL));
        OSSL_PARAM_free(params);
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }

    OSSL_PARAM_free(params);
    EVP_PKEY_CTX_free(ctx);

    return SSH_OK;
}

/**
 * @brief creates a copy of EVP_PKEY
 *
 * @param[in] name Algorithm to use. For more info see manpage of
 *                 EVP_PKEY_CTX_new_from_name
 *
 * @param[in] key Key being duplicated from
 *
 * @param[in] demote Same as at pki_key_dup, only the public
 *                   part of the key gets duplicated if true
 *
 * @param[out] new_key The key where the duplicate is saved
 *
 * @return 0 on success, -1 on error
 */
static int
evp_dup_pkey(const char *name, const ssh_key key, int demote, ssh_key new_key)
{
    int rc;
    EVP_PKEY_CTX *ctx = NULL;
    OSSL_PARAM *params = NULL;

    /* The simple case -- just reference the existing key */
    if (!demote || (key->flags & SSH_KEY_FLAG_PRIVATE) == 0) {
        rc = EVP_PKEY_up_ref(key->key);
        if (rc != 1) {
            return -1;
        }
        new_key->key = key->key;
        return SSH_OK;
    }

    /* demote == 1 */
    ctx = EVP_PKEY_CTX_new_from_name(NULL, name, NULL);
    if (ctx == NULL) {
        return -1;
    }

    rc = EVP_PKEY_todata(key->key, EVP_PKEY_PUBLIC_KEY, &params);
    if (rc != 1) {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }

    if (strcmp(name, "EC") == 0) {
        OSSL_PARAM *locate_param = NULL;
        /* For ECC keys provided by engine or provider, we need to have the
         * explicit public part available, otherwise the key will not be
         * usable */
        locate_param = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_PUB_KEY);
        if (locate_param == NULL) {
            EVP_PKEY_CTX_free(ctx);
            OSSL_PARAM_free(params);
            return -1;
        }
    }
    rc = EVP_PKEY_fromdata_init(ctx);
    if (rc != 1) {
        EVP_PKEY_CTX_free(ctx);
        OSSL_PARAM_free(params);
        return -1;
    }

    rc = EVP_PKEY_fromdata(ctx, &(new_key->key), EVP_PKEY_PUBLIC_KEY, params);
    if (rc != 1) {
        EVP_PKEY_CTX_free(ctx);
        OSSL_PARAM_free(params);
        return -1;
    }

    OSSL_PARAM_free(params);
    EVP_PKEY_CTX_free(ctx);

    return SSH_OK;
}

int evp_dup_rsa_pkey(const ssh_key key, ssh_key new_key, int demote)
{
    return evp_dup_pkey("RSA", key, demote, new_key);
}

int evp_dup_ecdsa_pkey(const ssh_key key, ssh_key new_key, int demote)
{
    return evp_dup_pkey("EC", key, demote, new_key);
}
#endif /* OPENSSL_VERSION_NUMBER */

ssh_string
pki_key_make_ecpoint_string(const EC_GROUP *g, const EC_POINT *p)
{
    ssh_string s = NULL;
    size_t len;

    len = EC_POINT_point2oct(g,
                             p,
                             POINT_CONVERSION_UNCOMPRESSED,
                             NULL,
                             0,
                             NULL);
    if (len == 0) {
        return NULL;
    }

    s = ssh_string_new(len);
    if (s == NULL) {
        return NULL;
    }

    len = EC_POINT_point2oct(g,
                             p,
                             POINT_CONVERSION_UNCOMPRESSED,
                             ssh_string_data(s),
                             ssh_string_len(s),
                             NULL);
    if (len != ssh_string_len(s)) {
        SSH_STRING_FREE(s);
        return NULL;
    }

    return s;
}

int pki_key_ecgroup_name_to_nid(const char *group)
{
    if (strcmp(group, NISTP256) == 0 ||
        strcmp(group, "secp256r1") == 0 ||
        strcmp(group, "prime256v1") == 0) {
        return NID_X9_62_prime256v1;
    } else if (strcmp(group, NISTP384) == 0 ||
               strcmp(group, "secp384r1") == 0) {
        return NID_secp384r1;
    } else if (strcmp(group, NISTP521) == 0 ||
               strcmp(group, "secp521r1") == 0) {
        return NID_secp521r1;
    }
    return -1;
}
#endif /* LIBCRYPTO */
