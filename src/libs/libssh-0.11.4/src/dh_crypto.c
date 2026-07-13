/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2019 by Simo Sorce - Red Hat, Inc.
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
#include "libssh/session.h"
#include "libssh/dh.h"
#include "libssh/buffer.h"
#include "libssh/ssh2.h"
#include "libssh/pki.h"
#include "libssh/bignum.h"

#include "openssl/crypto.h"
#include "openssl/dh.h"
#include "libcrypto-compat.h"
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <openssl/err.h>
#endif /* OPENSSL_VERSION_NUMBER */

extern bignum ssh_dh_generator;
extern bignum ssh_dh_group1;
extern bignum ssh_dh_group14;
extern bignum ssh_dh_group16;
extern bignum ssh_dh_group18;

struct dh_ctx {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    DH *keypair[2];
#else
    EVP_PKEY *keypair[2];
#endif /* OPENSSL_VERSION_NUMBER */
};

void ssh_dh_debug_crypto(struct ssh_crypto_struct *c)
{
#ifdef DEBUG_CRYPTO
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    const_bignum x = NULL, y = NULL, e = NULL, f = NULL;
#else
    bignum x = NULL, y = NULL, e = NULL, f = NULL;
#endif /* OPENSSL_VERSION_NUMBER */

    ssh_dh_keypair_get_keys(c->dh_ctx, DH_CLIENT_KEYPAIR, &x, &e);
    ssh_dh_keypair_get_keys(c->dh_ctx, DH_SERVER_KEYPAIR, &y, &f);
    ssh_print_bignum("x", x);
    ssh_print_bignum("y", y);
    ssh_print_bignum("e", e);
    ssh_print_bignum("f", f);
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    bignum_safe_free(x);
    bignum_safe_free(y);
    bignum_safe_free(e);
    bignum_safe_free(f);
#endif /* OPENSSL_VERSION_NUMBER */

    ssh_log_hexdump("Session server cookie", c->server_kex.cookie, 16);
    ssh_log_hexdump("Session client cookie", c->client_kex.cookie, 16);
    ssh_print_bignum("k", c->shared_secret);

#else
    (void)c; /* UNUSED_PARAM */
#endif /* DEBUG_CRYPTO */
}

#if OPENSSL_VERSION_NUMBER < 0x30000000L
int ssh_dh_keypair_get_keys(struct dh_ctx *ctx, int peer,
                            const_bignum *priv, const_bignum *pub)
{
    if (((peer != DH_CLIENT_KEYPAIR) && (peer != DH_SERVER_KEYPAIR)) ||
        ((priv == NULL) && (pub == NULL)) || (ctx == NULL) ||
        (ctx->keypair[peer] == NULL)) {
        return SSH_ERROR;
    }

    DH_get0_key(ctx->keypair[peer], pub, priv);

    if (priv && (*priv == NULL || bignum_num_bits(*priv) == 0)) {
        return SSH_ERROR;
    }
    if (pub && (*pub == NULL || bignum_num_bits(*pub) == 0)) {
        return SSH_ERROR;
    }

    return SSH_OK;
}

#else
/* If set *priv and *pub should be initialized
 * to NULL before calling this function*/
int ssh_dh_keypair_get_keys(struct dh_ctx *ctx, int peer,
                            bignum *priv, bignum *pub)
{
    int rc;
    if (((peer != DH_CLIENT_KEYPAIR) && (peer != DH_SERVER_KEYPAIR)) ||
        ((priv == NULL) && (pub == NULL)) || (ctx == NULL) ||
        (ctx->keypair[peer] == NULL)) {
        return SSH_ERROR;
    }

    if (priv) {
        rc = EVP_PKEY_get_bn_param(ctx->keypair[peer],
                                   OSSL_PKEY_PARAM_PRIV_KEY,
                                   priv);
        if (rc != 1) {
            return SSH_ERROR;
        }
    }
    if (pub) {
        rc = EVP_PKEY_get_bn_param(ctx->keypair[peer],
                                   OSSL_PKEY_PARAM_PUB_KEY,
                                   pub);
        if (rc != 1) {
            return SSH_ERROR;
        }
    }
    if (priv && (*priv == NULL || bignum_num_bits(*priv) == 0)) {
        if (pub && (*pub != NULL && bignum_num_bits(*pub) != 0)) {
            bignum_safe_free(*pub);
            *pub = NULL;
        }
        return SSH_ERROR;
    }
    if (pub && (*pub == NULL || bignum_num_bits(*pub) == 0)) {
        if (priv) {
            bignum_safe_free(*priv);
            *priv = NULL;
        }
        return SSH_ERROR;
    }

    return SSH_OK;
}
#endif /* OPENSSL_VERSION_NUMBER */

int ssh_dh_keypair_set_keys(struct dh_ctx *ctx, int peer,
                            bignum priv, bignum pub)
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    int rc;
    OSSL_PARAM *params = NULL, *out_params = NULL, *merged_params = NULL;
    OSSL_PARAM_BLD *param_bld = NULL;
    EVP_PKEY_CTX *evp_ctx = NULL;
#endif /* OPENSSL_VERSION_NUMBER */

    if (((peer != DH_CLIENT_KEYPAIR) && (peer != DH_SERVER_KEYPAIR)) ||
        ((priv == NULL) && (pub == NULL)) || (ctx == NULL) ||
        (ctx->keypair[peer] == NULL)) {
        return SSH_ERROR;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    (void)DH_set0_key(ctx->keypair[peer], pub, priv);

    return SSH_OK;
#else
    rc = EVP_PKEY_todata(ctx->keypair[peer], EVP_PKEY_KEYPAIR, &out_params);
    if (rc != 1) {
        return SSH_ERROR;
    }

    param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL) {
        rc = SSH_ERROR;
        goto out;
    }

    evp_ctx = EVP_PKEY_CTX_new_from_pkey(NULL, ctx->keypair[peer], NULL);
    if (evp_ctx == NULL) {
        rc = SSH_ERROR;
        goto out;
    }

    rc = EVP_PKEY_fromdata_init(evp_ctx);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto out;
    }

    if (priv) {
        rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_PRIV_KEY, priv);
        if (rc != 1) {
            rc = SSH_ERROR;
            goto out;
        }
    }
    if (pub) {
        rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_PUB_KEY, pub);
        if (rc != 1) {
            rc = SSH_ERROR;
            goto out;
        }
    }

    params = OSSL_PARAM_BLD_to_param(param_bld);
    if (params == NULL) {
        rc = SSH_ERROR;
        goto out;
    }
    OSSL_PARAM_BLD_free(param_bld);

    merged_params = OSSL_PARAM_merge(out_params, params);
    if (merged_params == NULL) {
        rc = SSH_ERROR;
        goto out;
    }

    rc = EVP_PKEY_fromdata(evp_ctx,
                           &(ctx->keypair[peer]),
                           EVP_PKEY_PUBLIC_KEY,
                           merged_params);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto out;
    }

    rc = SSH_OK;
out:
    bignum_safe_free(priv);
    bignum_safe_free(pub);
    EVP_PKEY_CTX_free(evp_ctx);
    OSSL_PARAM_free(out_params);
    OSSL_PARAM_free(params);
    OSSL_PARAM_free(merged_params);

    return rc;
#endif /* OPENSSL_VERSION_NUMBER */
}

#if OPENSSL_VERSION_NUMBER < 0x30000000L
int ssh_dh_get_parameters(struct dh_ctx *ctx,
                          const_bignum *modulus, const_bignum *generator)
{
    if (ctx == NULL || ctx->keypair[0] == NULL) {
        return SSH_ERROR;
    }
    DH_get0_pqg(ctx->keypair[0], modulus, NULL, generator);
    return SSH_OK;
}
#else
int ssh_dh_get_parameters(struct dh_ctx *ctx,
                          bignum *modulus, bignum *generator)
{
    int rc;

    if (ctx == NULL || ctx->keypair[0] == NULL) {
        return SSH_ERROR;
    }

    rc = EVP_PKEY_get_bn_param(ctx->keypair[0], OSSL_PKEY_PARAM_FFC_P, (BIGNUM**)modulus);
    if (rc != 1) {
        return SSH_ERROR;
    }
    rc = EVP_PKEY_get_bn_param(ctx->keypair[0], OSSL_PKEY_PARAM_FFC_G, (BIGNUM**)generator);
    if (rc != 1) {
        bignum_safe_free(*modulus);
        return SSH_ERROR;
    }

    return SSH_OK;
}
#endif /* OPENSSL_VERSION_NUMBER */

int ssh_dh_set_parameters(struct dh_ctx *ctx,
                          const bignum modulus, const bignum generator)
{
    size_t i;
    int rc;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    OSSL_PARAM *params = NULL;
    OSSL_PARAM_BLD *param_bld = NULL;
    EVP_PKEY_CTX *evp_ctx = NULL;
#endif /* OPENSSL_VERSION_NUMBER */

    if ((ctx == NULL) || (modulus == NULL) || (generator == NULL)) {
        return SSH_ERROR;
    }

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    evp_ctx = EVP_PKEY_CTX_new_from_name(NULL, "DHX", NULL);
#endif

    for (i = 0; i < 2; i++) {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        bignum p = NULL;
        bignum g = NULL;

        /* when setting modulus or generator,
         * make sure to invalidate existing keys */
        DH_free(ctx->keypair[i]);
        ctx->keypair[i] = DH_new();
        if (ctx->keypair[i] == NULL) {
            rc = SSH_ERROR;
            goto done;
        }

        p = BN_dup(modulus);
        g = BN_dup(generator);
        rc = DH_set0_pqg(ctx->keypair[i], p, NULL, g);
        if (rc != 1) {
            BN_free(p);
            BN_free(g);
            rc = SSH_ERROR;
            goto done;
        }
#else
        param_bld = OSSL_PARAM_BLD_new();

        if (param_bld == NULL) {
            rc = SSH_ERROR;
            goto done;
        }

        rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_FFC_P, modulus);
        if (rc != 1) {
            rc = SSH_ERROR;
            goto done;
        }
        rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_FFC_G, generator);
        if (rc != 1) {
            rc = SSH_ERROR;
            goto done;
        }
        params = OSSL_PARAM_BLD_to_param(param_bld);
        if (params == NULL) {
            OSSL_PARAM_BLD_free(param_bld);
            rc = SSH_ERROR;
            goto done;
        }
        OSSL_PARAM_BLD_free(param_bld);

        rc = EVP_PKEY_fromdata_init(evp_ctx);
        if (rc != 1) {
            OSSL_PARAM_free(params);
            rc = SSH_ERROR;
            goto done;
        }

        /* make sure to invalidate existing keys */
        EVP_PKEY_free(ctx->keypair[i]);
        ctx->keypair[i] = NULL;

        rc = EVP_PKEY_fromdata(evp_ctx,
                               &(ctx->keypair[i]),
                               EVP_PKEY_KEY_PARAMETERS,
                               params);
        if (rc != 1) {
            OSSL_PARAM_free(params);
            rc = SSH_ERROR;
            goto done;
        }

        OSSL_PARAM_free(params);
#endif /* OPENSSL_VERSION_NUMBER */
    }

    rc = SSH_OK;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
done:
    if (rc != SSH_OK) {
        DH_free(ctx->keypair[0]);
        DH_free(ctx->keypair[1]);
    }
#else
done:
    EVP_PKEY_CTX_free(evp_ctx);

    if (rc != SSH_OK) {
        EVP_PKEY_free(ctx->keypair[0]);
        EVP_PKEY_free(ctx->keypair[1]);
    }
#endif /* OPENSSL_VERSION_NUMBER */
    if (rc != SSH_OK) {
        ctx->keypair[0] = NULL;
        ctx->keypair[1] = NULL;
    }

    return rc;
}

/**
 * @internal
 * @brief allocate and initialize ephemeral values used in dh kex
 */
int ssh_dh_init_common(struct ssh_crypto_struct *crypto)
{
    struct dh_ctx *ctx = NULL;
    int rc;

    /* Cleanup any previously allocated dh_ctx */
    if (crypto->dh_ctx != NULL) {
        ssh_dh_cleanup(crypto);
    }

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return SSH_ERROR;
    }
    crypto->dh_ctx = ctx;

    switch (crypto->kex_type) {
    case SSH_KEX_DH_GROUP1_SHA1:
        rc = ssh_dh_set_parameters(ctx, ssh_dh_group1, ssh_dh_generator);
        break;
    case SSH_KEX_DH_GROUP14_SHA1:
    case SSH_KEX_DH_GROUP14_SHA256:
        rc = ssh_dh_set_parameters(ctx, ssh_dh_group14, ssh_dh_generator);
        break;
    case SSH_KEX_DH_GROUP16_SHA512:
        rc = ssh_dh_set_parameters(ctx, ssh_dh_group16, ssh_dh_generator);
        break;
    case SSH_KEX_DH_GROUP18_SHA512:
        rc = ssh_dh_set_parameters(ctx, ssh_dh_group18, ssh_dh_generator);
        break;
    default:
        rc = SSH_OK;
        break;
    }

    if (rc != SSH_OK) {
        ssh_dh_cleanup(crypto);
    }
    return rc;
}

void ssh_dh_cleanup(struct ssh_crypto_struct *crypto)
{
    if (crypto->dh_ctx != NULL) {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        DH_free(crypto->dh_ctx->keypair[0]);
        DH_free(crypto->dh_ctx->keypair[1]);
#else
        EVP_PKEY_free(crypto->dh_ctx->keypair[0]);
        EVP_PKEY_free(crypto->dh_ctx->keypair[1]);
#endif /* OPENSSL_VERSION_NUMBER */
        free(crypto->dh_ctx);
        crypto->dh_ctx = NULL;
    }
}

/** @internal
 * @brief generates a secret DH parameter of at least DH_SECURITY_BITS
 *        security as well as the corresponding public key.
 *
 * @param[out] params a dh_ctx that will hold the new keys.
 * @param peer Select either client or server key storage. Valid values are:
 *        DH_CLIENT_KEYPAIR or DH_SERVER_KEYPAIR
 *
 * @return SSH_OK on success, SSH_ERROR on error
 */
int ssh_dh_keypair_gen_keys(struct dh_ctx *dh_ctx, int peer)
{
    int rc;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    EVP_PKEY_CTX *evp_ctx = NULL;
#endif

    if ((dh_ctx == NULL) || (dh_ctx->keypair[peer] == NULL)) {
        return SSH_ERROR;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    rc = DH_generate_key(dh_ctx->keypair[peer]);
    if (rc != 1) {
        return SSH_ERROR;
    }
#else
    evp_ctx = EVP_PKEY_CTX_new_from_pkey(NULL, dh_ctx->keypair[peer], NULL);
    if (evp_ctx == NULL) {
        return SSH_ERROR;
    }

    rc = EVP_PKEY_keygen_init(evp_ctx);
    if (rc != 1) {
        EVP_PKEY_CTX_free(evp_ctx);
        return SSH_ERROR;
    }

    rc = EVP_PKEY_generate(evp_ctx, &(dh_ctx->keypair[peer]));
    if (rc != 1) {
        EVP_PKEY_CTX_free(evp_ctx);
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to generate DH: %s",
                ERR_error_string(ERR_get_error(), NULL));
        return SSH_ERROR;
    }

    EVP_PKEY_CTX_free(evp_ctx);
#endif /* OPENSSL_VERSION_NUMBER */

    return SSH_OK;
}

/** @internal
 * @brief generates a shared secret between the local peer and the remote
 *        peer. The local peer must have been initialized using either the
 *        ssh_dh_keypair_gen_keys() function or by seetting manually both
 *        the private and public keys. The remote peer only needs to have
 *        the remote's peer public key set.
 * @param[in] local peer identifier (DH_CLIENT_KEYPAIR or DH_SERVER_KEYPAIR)
 * @param[in] remote peer identifier (DH_CLIENT_KEYPAIR or DH_SERVER_KEYPAIR)
 * @param[out] dest a new bignum with the shared secret value is returned.
 * @return SSH_OK on success, SSH_ERROR on error
 */
int ssh_dh_compute_shared_secret(struct dh_ctx *dh_ctx, int local, int remote,
                                 bignum *dest)
{
    unsigned char *kstring = NULL;
    int rc;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    const_bignum pub_key = NULL;
    int klen;
#else
    size_t klen;
    EVP_PKEY_CTX *evp_ctx = NULL;
#endif /* OPENSSL_VERSION_NUMBER */

    if ((dh_ctx == NULL) ||
        (dh_ctx->keypair[local] == NULL) ||
        (dh_ctx->keypair[remote] == NULL)) {
        return SSH_ERROR;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    kstring = malloc(DH_size(dh_ctx->keypair[local]));
    if (kstring == NULL) {
        rc = SSH_ERROR;
        goto done;
    }

    rc = ssh_dh_keypair_get_keys(dh_ctx, remote, NULL, &pub_key);
    if (rc != SSH_OK) {
        rc = SSH_ERROR;
        goto done;
    }

    klen = DH_compute_key(kstring, pub_key, dh_ctx->keypair[local]);
    if (klen == -1) {
        rc = SSH_ERROR;
        goto done;
    }
#else
    evp_ctx = EVP_PKEY_CTX_new_from_pkey(NULL, dh_ctx->keypair[local], NULL);

    rc = EVP_PKEY_derive_init(evp_ctx);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto done;
    }

    rc = EVP_PKEY_derive_set_peer(evp_ctx, dh_ctx->keypair[remote]);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to set peer key: %s",
                ERR_error_string(ERR_get_error(), NULL));
        rc = SSH_ERROR;
        goto done;
    }

    /* getting the size of the secret */
    rc = EVP_PKEY_derive(evp_ctx, kstring, &klen);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto done;
    }

    kstring = malloc(klen);
    if (kstring == NULL) {
        rc = SSH_ERROR;
        goto done;
    }

    rc = EVP_PKEY_derive(evp_ctx, kstring, &klen);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto done;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    *dest = BN_bin2bn(kstring, klen, NULL);
    if (*dest == NULL) {
        rc = SSH_ERROR;
        goto done;
    }

    rc = SSH_OK;
done:
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    EVP_PKEY_CTX_free(evp_ctx);
#endif
    free(kstring);
    return rc;
}
