/*
 * pki_crypto.c - PKI infrastructure using OpenSSL
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2003-2009 by Aris Adamantiadis
 * Copyright (c) 2009-2013 by Andreas Schneider <asn@cryptomilk.org>
 * Copyright (c) 2019      by Sahana Prasad     <sahana@redhat.com>
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

#ifndef _PKI_CRYPTO_H
#define _PKI_CRYPTO_H

#include "config.h"

#include "libssh/priv.h"
#include "libcrypto-compat.h"

#include <openssl/pem.h>
#include <openssl/evp.h>
#if defined(WITH_PKCS11_URI) && !defined(WITH_PKCS11_PROVIDER)
#include <openssl/engine.h>
#endif
#include <openssl/err.h>
#if OPENSSL_VERSION_NUMBER < 0x30000000L
#include <openssl/dsa.h>
#include <openssl/rsa.h>
#else
#include <openssl/params.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#if defined(WITH_PKCS11_URI) && defined(WITH_PKCS11_PROVIDER)
#include <openssl/store.h>
#endif
#endif /* OPENSSL_VERSION_NUMBER */

#ifdef HAVE_OPENSSL_EC_H
#include <openssl/ec.h>
#endif
#ifdef HAVE_OPENSSL_ECDSA_H
#include <openssl/ecdsa.h>
#endif

#include "libssh/libssh.h"
#include "libssh/buffer.h"
#include "libssh/session.h"
#include "libssh/pki.h"
#include "libssh/pki_priv.h"
#include "libssh/bignum.h"

struct pem_get_password_struct {
    ssh_auth_callback fn;
    void *data;
};

static int pem_get_password(char *buf, int size, int rwflag, void *userdata) {
    struct pem_get_password_struct *pgp = userdata;

    (void) rwflag; /* unused */

    if (buf == NULL) {
        return 0;
    }

    memset(buf, '\0', size);
    if (pgp) {
        int rc;

        rc = pgp->fn("Passphrase for private key:",
                     buf, size, 0, 0,
                     pgp->data);
        if (rc == 0) {
            return (int)strlen(buf);
        }
    }

    return 0;
}

void pki_key_clean(ssh_key key)
{
    if (key == NULL)
        return;
    EVP_PKEY_free(key->key);
    key->key = NULL;
}

#ifdef HAVE_OPENSSL_ECC
#if OPENSSL_VERSION_NUMBER < 0x30000000L
static int pki_key_ecdsa_to_nid(EC_KEY *k)
{
    const EC_GROUP *g = EC_KEY_get0_group(k);
    int nid;

    if (g == NULL) {
        return -1;
    }
    nid = EC_GROUP_get_curve_name(g);
    if (nid) {
        return nid;
    }

    return -1;
}
#else
static int pki_key_ecdsa_to_nid(EVP_PKEY *k)
{
    char gname[25] = { 0 };
    int rc;

    rc = EVP_PKEY_get_utf8_string_param(k,
                                        OSSL_PKEY_PARAM_GROUP_NAME,
                                        gname,
                                        25,
                                        NULL);
    if (rc != 1) {
        return -1;
    }

    return pki_key_ecgroup_name_to_nid(gname);
}
#endif /* OPENSSL_VERSION_NUMBER */

#if OPENSSL_VERSION_NUMBER < 0x30000000L
static enum ssh_keytypes_e pki_key_ecdsa_to_key_type(EC_KEY *k)
#else
static enum ssh_keytypes_e pki_key_ecdsa_to_key_type(EVP_PKEY *k)
#endif /* OPENSSL_VERSION_NUMBER */
{
    int nid;

    nid = pki_key_ecdsa_to_nid(k);

    switch (nid) {
        case NID_X9_62_prime256v1:
            return SSH_KEYTYPE_ECDSA_P256;
        case NID_secp384r1:
            return SSH_KEYTYPE_ECDSA_P384;
        case NID_secp521r1:
            return SSH_KEYTYPE_ECDSA_P521;
        default:
            return SSH_KEYTYPE_UNKNOWN;
    }
}

const char *pki_key_ecdsa_nid_to_name(int nid)
{
    switch (nid) {
        case NID_X9_62_prime256v1:
            return "ecdsa-sha2-nistp256";
        case NID_secp384r1:
            return "ecdsa-sha2-nistp384";
        case NID_secp521r1:
            return "ecdsa-sha2-nistp521";
        default:
            break;
    }

    return "unknown";
}

static const char *pki_key_ecdsa_nid_to_char(int nid)
{
    switch (nid) {
        case NID_X9_62_prime256v1:
            return "nistp256";
        case NID_secp384r1:
            return "nistp384";
        case NID_secp521r1:
            return "nistp521";
        default:
            break;
    }

    return "unknown";
}

int pki_key_ecdsa_nid_from_name(const char *name)
{
    if (strcmp(name, "nistp256") == 0) {
        return NID_X9_62_prime256v1;
    } else if (strcmp(name, "nistp384") == 0) {
        return NID_secp384r1;
    } else if (strcmp(name, "nistp521") == 0) {
        return NID_secp521r1;
    }

    return -1;
}

int pki_privkey_build_ecdsa(ssh_key key, int nid, ssh_string e, ssh_string exp)
{
    int rc = 0;
    BIGNUM *bexp = NULL;

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_POINT *p = NULL;
    const EC_GROUP *g = NULL;
    EC_KEY *ecdsa = NULL;
#else
    const char *group_name = OSSL_EC_curve_nid2name(nid);
    OSSL_PARAM_BLD *param_bld = NULL;

    if (group_name == NULL) {
        return -1;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    bexp = ssh_make_string_bn(exp);
    if (bexp == NULL) {
        return -1;
    }

    key->ecdsa_nid = nid;

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    ecdsa = EC_KEY_new_by_curve_name(key->ecdsa_nid);
    if (ecdsa == NULL) {
        rc = -1;
        goto cleanup;
    }

    g = EC_KEY_get0_group(ecdsa);

    p = EC_POINT_new(g);
    if (p == NULL) {
        rc = -1;
        goto cleanup;
    }

    rc = EC_POINT_oct2point(g,
                            p,
                            ssh_string_data(e),
                            ssh_string_len(e),
                            NULL);
    if (rc != 1) {
        rc = -1;
        goto cleanup;
    }

    /* EC_KEY_set_public_key duplicates p */
    rc = EC_KEY_set_public_key(ecdsa, p);
    if (rc != 1) {
        rc = -1;
        goto cleanup;
    }

    /* EC_KEY_set_private_key duplicates exp */
    rc = EC_KEY_set_private_key(ecdsa, bexp);
    if (rc != 1) {
        rc = -1;
        goto cleanup;
    }

    key->key = EVP_PKEY_new();
    if (key->key == NULL) {
        rc = -1;
        goto cleanup;
    }

    /* ecdsa will be freed when the EVP_PKEY key->key is freed */
    rc = EVP_PKEY_assign_EC_KEY(key->key, ecdsa);
    if (rc != 1) {
        rc = -1;
        goto cleanup;
    }
    /* ssh_key is now the owner of this memory */
    ecdsa = NULL;

    /* set rc to 0 if everything went well */
    rc = 0;

cleanup:
    EC_KEY_free(ecdsa);
    EC_POINT_free(p);
    BN_free(bexp);
    return rc;
#else
    param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL){
        rc = -1;
        goto cleanup;
    }

    rc = OSSL_PARAM_BLD_push_utf8_string(param_bld, OSSL_PKEY_PARAM_GROUP_NAME,
                                         group_name, strlen(group_name));
    if (rc != 1) {
        rc = -1;
        goto cleanup;
    }

    rc = OSSL_PARAM_BLD_push_octet_string(param_bld, OSSL_PKEY_PARAM_PUB_KEY,
                                          ssh_string_data(e), ssh_string_len(e));
    if (rc != 1) {
        rc = -1;
        goto cleanup;
    }

    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_PRIV_KEY, bexp);
    if (rc != 1) {
        rc = -1;
        goto cleanup;
    }

    rc = evp_build_pkey("EC", param_bld, &(key->key), EVP_PKEY_KEYPAIR);

cleanup:
    OSSL_PARAM_BLD_free(param_bld);
    BN_free(bexp);
    return rc;
#endif /* OPENSSL_VERSION_NUMBER */
}

int pki_pubkey_build_ecdsa(ssh_key key, int nid, ssh_string e)
{
    int rc;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_POINT *p = NULL;
    const EC_GROUP *g = NULL;
    EC_KEY *ecdsa = NULL;
    int ok;
#else
    const char *group_name = OSSL_EC_curve_nid2name(nid);
    OSSL_PARAM_BLD *param_bld = NULL;
#endif /* OPENSSL_VERSION_NUMBER */

    key->ecdsa_nid = nid;

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    ecdsa = EC_KEY_new_by_curve_name(key->ecdsa_nid);
    if (ecdsa == NULL) {
        return -1;
    }

    g = EC_KEY_get0_group(ecdsa);

    p = EC_POINT_new(g);
    if (p == NULL) {
        EC_KEY_free(ecdsa);
        return -1;
    }

    ok = EC_POINT_oct2point(g,
                            p,
                            ssh_string_data(e),
                            ssh_string_len(e),
                            NULL);
    if (!ok) {
        EC_KEY_free(ecdsa);
        EC_POINT_free(p);
        return -1;
    }

    /* EC_KEY_set_public_key duplicates p */
    ok = EC_KEY_set_public_key(ecdsa, p);
    EC_POINT_free(p);
    if (!ok) {
        EC_KEY_free(ecdsa);
        return -1;
    }

    key->key = EVP_PKEY_new();
    if (key->key == NULL) {
        EC_KEY_free(ecdsa);
        return -1;
    }

    rc = EVP_PKEY_assign_EC_KEY(key->key, ecdsa);
    if (rc != 1) {
        EC_KEY_free(ecdsa);
        return -1;
    }

    return 0;
#else
    param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL)
        goto err;

    rc = OSSL_PARAM_BLD_push_utf8_string(param_bld, OSSL_PKEY_PARAM_GROUP_NAME,
                                         group_name, strlen(group_name));
    if (rc != 1)
        goto err;
    rc = OSSL_PARAM_BLD_push_octet_string(param_bld, OSSL_PKEY_PARAM_PUB_KEY,
                                          ssh_string_data(e), ssh_string_len(e));
    if (rc != 1)
        goto err;

    rc = evp_build_pkey("EC", param_bld, &(key->key), EVP_PKEY_PUBLIC_KEY);
    OSSL_PARAM_BLD_free(param_bld);

    return rc;
err:
    OSSL_PARAM_BLD_free(param_bld);
    return -1;
#endif /* OPENSSL_VERSION_NUMBER */
}
#endif /* HAVE_OPENSSL_ECC */

int pki_privkey_build_ed25519(ssh_key key,
                              ssh_string pubkey,
                              ssh_string privkey)
{
    EVP_PKEY *pkey = NULL;

    if (ssh_string_len(pubkey) != ED25519_KEY_LEN ||
        ssh_string_len(privkey) != (2 * ED25519_KEY_LEN)) {
        SSH_LOG(SSH_LOG_TRACE, "Invalid ed25519 key len");
        return SSH_ERROR;
    }

    pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519,
                                        NULL,
                                        (const uint8_t *)ssh_string_data(privkey),
                                        ED25519_KEY_LEN);
    if (pkey == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to create ed25519 EVP_PKEY: %s",
                ERR_error_string(ERR_get_error(), NULL));
        return SSH_ERROR;
    }

    key->key = pkey;

    return SSH_OK;
}

int pki_pubkey_build_ed25519(ssh_key key, ssh_string pubkey)
{
    EVP_PKEY *pkey = NULL;

    if (ssh_string_len(pubkey) != ED25519_KEY_LEN) {
        SSH_LOG(SSH_LOG_TRACE, "Invalid ed25519 key len");
        return SSH_ERROR;
    }

    if (ssh_fips_mode()) {
        /* We do not want to fail here as we know the algorithm, but we can not
         * use it. Just store the public key here. We won't be able to use it
         * for anything though. */
        key->ed25519_pubkey = malloc(ED25519_KEY_LEN);
        if (key->ed25519_pubkey == NULL) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Failed to allocate memory for the Ed25519 public key");
            return SSH_ERROR;
        }

        memcpy(key->ed25519_pubkey, ssh_string_data(pubkey), ED25519_KEY_LEN);
        return SSH_OK;
    }

    pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519,
                                       NULL,
                                       (const uint8_t *)ssh_string_data(pubkey),
                                       ED25519_KEY_LEN);
    if (pkey == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to create ed25519 EVP_PKEY: %s",
                ERR_error_string(ERR_get_error(), NULL));
        return SSH_ERROR;
    }

    key->key = pkey;

    return SSH_OK;
}

ssh_key pki_key_dup(const ssh_key key, int demote)
{
    ssh_key new = NULL;
    int rc;

    new = pki_key_dup_common_init(key, demote);
    if (new == NULL) {
        return NULL;
    }

    switch (key->type) {
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA1: {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        const BIGNUM *n = NULL, *e = NULL, *d = NULL;
        BIGNUM *nn, *ne, *nd;
        RSA *new_rsa = NULL;
        const RSA *key_rsa = EVP_PKEY_get0_RSA(key->key);
#endif /* OPENSSL_VERSION_NUMBER < 0x30000000L */
#ifdef WITH_PKCS11_URI
        /* Take the PKCS#11 keys as they are */
        if (key->flags & SSH_KEY_FLAG_PKCS11_URI && !demote) {
            rc = EVP_PKEY_up_ref(key->key);
            if (rc != 1) {
                goto fail;
            }
            new->key = key->key;
            return new;
        }
#endif /* WITH_PKCS11_URI */
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        new_rsa = RSA_new();
        if (new_rsa == NULL) {
            goto fail;
        }

        /*
         * n    = public modulus
         * e    = public exponent
         * d    = private exponent
         * p    = secret prime factor
         * q    = secret prime factor
         * dmp1 = d mod (p-1)
         * dmq1 = d mod (q-1)
         * iqmp = q^-1 mod p
         */
        RSA_get0_key(key_rsa, &n, &e, &d);
        nn = BN_dup(n);
        ne = BN_dup(e);
        if (nn == NULL || ne == NULL) {
            RSA_free(new_rsa);
            BN_free(nn);
            BN_free(ne);
            goto fail;
        }

        /* Memory management of nn and ne is transferred to RSA object */
        rc = RSA_set0_key(new_rsa, nn, ne, NULL);
        if (rc == 0) {
            RSA_free(new_rsa);
            BN_free(nn);
            BN_free(ne);
            goto fail;
        }

        if (!demote && (key->flags & SSH_KEY_FLAG_PRIVATE)) {
            const BIGNUM *p = NULL, *q = NULL, *dmp1 = NULL,
              *dmq1 = NULL, *iqmp = NULL;
            BIGNUM *np, *nq, *ndmp1, *ndmq1, *niqmp;

            nd = BN_dup(d);
            if (nd == NULL) {
                RSA_free(new_rsa);
                goto fail;
            }

            /* Memory management of nd is transferred to RSA object */
            rc = RSA_set0_key(new_rsa, NULL, NULL, nd);
            if (rc == 0) {
                RSA_free(new_rsa);
                goto fail;
            }

            /* p, q, dmp1, dmq1 and iqmp may be NULL in private keys, but the
             * RSA operations are much faster when these values are available.
             */
            RSA_get0_factors(key_rsa, &p, &q);
            if (p != NULL && q != NULL) { /* need to set both of them */
                np = BN_dup(p);
                nq = BN_dup(q);
                if (np == NULL || nq == NULL) {
                    RSA_free(new_rsa);
                    BN_free(np);
                    BN_free(nq);
                    goto fail;
                }

                /* Memory management of np and nq is transferred to RSA object */
                rc = RSA_set0_factors(new_rsa, np, nq);
                if (rc == 0) {
                    RSA_free(new_rsa);
                    BN_free(np);
                    BN_free(nq);
                    goto fail;
                }
            }

            RSA_get0_crt_params(key_rsa, &dmp1, &dmq1, &iqmp);
            if (dmp1 != NULL || dmq1 != NULL || iqmp != NULL) {
                ndmp1 = BN_dup(dmp1);
                ndmq1 = BN_dup(dmq1);
                niqmp = BN_dup(iqmp);
                if (ndmp1 == NULL || ndmq1 == NULL || niqmp == NULL) {
                    RSA_free(new_rsa);
                    BN_free(ndmp1);
                    BN_free(ndmq1);
                    BN_free(niqmp);
                    goto fail;
                }

                /* Memory management of ndmp1, ndmq1 and niqmp is transferred
                 * to RSA object */
                rc = RSA_set0_crt_params(new_rsa, ndmp1, ndmq1, niqmp);
                if (rc == 0) {
                    RSA_free(new_rsa);
                    BN_free(ndmp1);
                    BN_free(ndmq1);
                    BN_free(niqmp);
                    goto fail;
                }
            }
        }

        new->key = EVP_PKEY_new();
        if (new->key == NULL) {
            RSA_free(new_rsa);
            goto fail;
        }

        rc = EVP_PKEY_assign_RSA(new->key, new_rsa);
        if (rc != 1) {
            EVP_PKEY_free(new->key);
            RSA_free(new_rsa);
            goto fail;
        }

        new_rsa = NULL;
#else
        rc = evp_dup_rsa_pkey(key, new, demote);
        if (rc != SSH_OK) {
            goto fail;
        }
#endif /* OPENSSL_VERSION_NUMBER */
        break;
    }
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_ECDSA_P384:
    case SSH_KEYTYPE_ECDSA_P521:
    case SSH_KEYTYPE_SK_ECDSA:
#ifdef HAVE_OPENSSL_ECC
        new->ecdsa_nid = key->ecdsa_nid;
#ifdef WITH_PKCS11_URI
        /* Take the PKCS#11 keys as they are */
        if (key->flags & SSH_KEY_FLAG_PKCS11_URI && !demote) {
            rc = EVP_PKEY_up_ref(key->key);
            if (rc != 1) {
                goto fail;
            }
            new->key = key->key;
            return new;
        }
#endif /* WITH_PKCS11_URI */
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        /* privkey -> pubkey */
        if (demote && ssh_key_is_private(key)) {
            const EC_POINT *p = NULL;
            EC_KEY *new_ecdsa = NULL, *old_ecdsa = NULL;
            int ok;

            new_ecdsa = EC_KEY_new_by_curve_name(key->ecdsa_nid);
            if (new_ecdsa == NULL) {
                goto fail;
            }

            old_ecdsa = EVP_PKEY_get0_EC_KEY(key->key);
            if (old_ecdsa == NULL) {
                EC_KEY_free(new_ecdsa);
                goto fail;
            }

            p = EC_KEY_get0_public_key(old_ecdsa);
            if (p == NULL) {
                EC_KEY_free(new_ecdsa);
                goto fail;
            }

            ok = EC_KEY_set_public_key(new_ecdsa, p);
            if (ok != 1) {
                EC_KEY_free(new_ecdsa);
                goto fail;
            }

            new->key = EVP_PKEY_new();
            if (new->key == NULL) {
                EC_KEY_free(new_ecdsa);
                goto fail;
            }

            ok = EVP_PKEY_assign_EC_KEY(new->key, new_ecdsa);
            if (ok != 1) {
                EC_KEY_free(new_ecdsa);
                goto fail;
            }
        } else {
            rc = EVP_PKEY_up_ref(key->key);
            if (rc != 1) {
                goto fail;
            }
            new->key = key->key;
        }
#else
        rc = evp_dup_ecdsa_pkey(key, new, demote);
        if (rc != SSH_OK) {
            goto fail;
        }
#endif /* OPENSSL_VERSION_NUMBER */
        break;
#endif /* HAVE_OPENSSL_ECC */
    case SSH_KEYTYPE_ED25519:
    case SSH_KEYTYPE_SK_ED25519: {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        /* Take the PKCS#11 keys as they are */
        if (key->flags & SSH_KEY_FLAG_PKCS11_URI && !demote) {
            rc = EVP_PKEY_up_ref(key->key);
            if (rc != 1) {
                goto fail;
            }
            new->key = key->key;
            return new;
        }

        if (!demote && (key->flags & SSH_KEY_FLAG_PRIVATE) &&
            key->type == SSH_KEYTYPE_ED25519) {
            rc = EVP_PKEY_up_ref(key->key);
            if (rc != 1) {
                goto fail;
            }
            new->key = key->key;
        } else {
            unsigned char *ed25519_pubkey = NULL;
            size_t key_len = 0;

            rc = EVP_PKEY_get_raw_public_key(key->key, NULL, &key_len);
            if (rc != 1) {
                SSH_LOG(SSH_LOG_TRACE,
                        "Failed to get ed25519 raw public key length: %s",
                        ERR_error_string(ERR_get_error(), NULL));
                goto fail;
            }

            if (key_len != ED25519_KEY_LEN) {
                SSH_LOG(SSH_LOG_TRACE,
                        "Unexpected length of public key %zu. Expected %d.",
                        key_len,
                        ED25519_KEY_LEN);
                goto fail;
            }

            ed25519_pubkey = malloc(key_len);
            if (ed25519_pubkey == NULL) {
                SSH_LOG(SSH_LOG_TRACE, "Out of memory");
                goto fail;
            }

            rc = EVP_PKEY_get_raw_public_key(key->key,
                                             ed25519_pubkey,
                                             &key_len);
            if (rc != 1) {
                SSH_LOG(SSH_LOG_TRACE,
                        "Failed to get ed25519 raw public key: %s",
                        ERR_error_string(ERR_get_error(), NULL));
                free(ed25519_pubkey);
                goto fail;
            }

            new->key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519,
                                                   NULL,
                                                   ed25519_pubkey,
                                                   key_len);
            free(ed25519_pubkey);
        }

#else
        rc = evp_dup_ed25519_pkey(key, new, demote);
        if (rc != SSH_OK) {
            goto fail;
        }
#endif /* OPENSSL_VERSION_NUMBER < 0x30000000L */
        break;
    }
    case SSH_KEYTYPE_UNKNOWN:
    default:
        ssh_key_free(new);
        return NULL;
    }

    return new;
fail:
    ssh_key_free(new);
    return NULL;
}

int pki_key_generate_rsa(ssh_key key, int parameter){
	int rc;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    BIGNUM *e = NULL;
    RSA *key_rsa = NULL;
#else
    OSSL_PARAM params[3];
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
    unsigned e = 65537;
#endif /* OPENSSL_VERSION_NUMBER */

    if (parameter == 0) {
        parameter = RSA_DEFAULT_KEY_SIZE;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    e = BN_new();
    key_rsa = RSA_new();
    if (key_rsa == NULL) {
        return SSH_ERROR;
    }

    BN_set_word(e, 65537);
    rc = RSA_generate_key_ex(key_rsa, parameter, e, NULL);

    BN_free(e);

    if (rc <= 0 || key_rsa == NULL) {
        return SSH_ERROR;
    }

    key->key = EVP_PKEY_new();
    if (key->key == NULL) {
        RSA_free(key_rsa);
        return SSH_ERROR;
    }

    rc = EVP_PKEY_assign_RSA(key->key, key_rsa);
    if (rc != 1) {
        RSA_free(key_rsa);
        EVP_PKEY_free(key->key);
        return SSH_ERROR;
    }

    key_rsa = NULL;
#else
    key->key = NULL;

    rc = EVP_PKEY_keygen_init(pctx);
    if (rc != 1) {
        EVP_PKEY_CTX_free(pctx);
        return SSH_ERROR;
    }

    params[0] = OSSL_PARAM_construct_int("bits", &parameter);
    params[1] = OSSL_PARAM_construct_uint("e", &e);
    params[2] = OSSL_PARAM_construct_end();
    rc = EVP_PKEY_CTX_set_params(pctx, params);
    if (rc != 1) {
        EVP_PKEY_CTX_free(pctx);
        return SSH_ERROR;
    }

    rc = EVP_PKEY_generate(pctx, &(key->key));

    EVP_PKEY_CTX_free(pctx);

    if (rc != 1 || key->key == NULL)
        return SSH_ERROR;
#endif /* OPENSSL_VERSION_NUMBER */
	return SSH_OK;
}

#ifdef HAVE_OPENSSL_ECC
int pki_key_generate_ecdsa(ssh_key key, int parameter)
{
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ecdsa = NULL;
    int ok;
#else
    const char *group_name = NULL;
#endif /* OPENSSL_VERSION_NUMBER */
    switch (parameter) {
        case 256:
            key->ecdsa_nid = NID_X9_62_prime256v1;
            key->type = SSH_KEYTYPE_ECDSA_P256;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
            group_name = NISTP256;
#endif /* OPENSSL_VERSION_NUMBER */
            break;
        case 384:
            key->ecdsa_nid = NID_secp384r1;
            key->type = SSH_KEYTYPE_ECDSA_P384;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
            group_name = NISTP384;
#endif /* OPENSSL_VERSION_NUMBER */
            break;
        case 521:
            key->ecdsa_nid = NID_secp521r1;
            key->type = SSH_KEYTYPE_ECDSA_P521;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
            group_name = NISTP521;
#endif /* OPENSSL_VERSION_NUMBER */
            break;
        default:
            SSH_LOG(SSH_LOG_TRACE, "Invalid parameter %d for ECDSA key "
                    "generation", parameter);
            return SSH_ERROR;
    }
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    ecdsa = EC_KEY_new_by_curve_name(key->ecdsa_nid);
    if (ecdsa == NULL) {
        return SSH_ERROR;
    }
    ok = EC_KEY_generate_key(ecdsa);
    if (!ok) {
        EC_KEY_free(ecdsa);
        return SSH_ERROR;
    }

    EC_KEY_set_asn1_flag(ecdsa, OPENSSL_EC_NAMED_CURVE);

    key->key = EVP_PKEY_new();
    if (key->key == NULL) {
        EC_KEY_free(ecdsa);
        return SSH_ERROR;
    }

    ok = EVP_PKEY_assign_EC_KEY(key->key, ecdsa);
    if (ok != 1) {
        return SSH_ERROR;
    }

#else
    key->key = EVP_EC_gen(group_name);
    if (key->key == NULL) {
        return SSH_ERROR;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    return SSH_OK;
}
#endif /* HAVE_OPENSSL_ECC */

/* With OpenSSL 3.0 and higher the parameter 'what'
 * is ignored and the comparison is done by OpenSSL
 */
int pki_key_compare(const ssh_key k1, const ssh_key k2, enum ssh_keycmp_e what)
{
    int rc, cmp;

    (void)what;

    /* We got here only if the types match */
    switch (ssh_key_type_plain(k1->type)) {
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_ECDSA_P384:
    case SSH_KEYTYPE_ECDSA_P521:
    case SSH_KEYTYPE_SK_ECDSA:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
#ifdef HAVE_OPENSSL_ECC
    {
        const EC_KEY *ec1 = EVP_PKEY_get0_EC_KEY(k1->key);
        const EC_KEY *ec2 = EVP_PKEY_get0_EC_KEY(k2->key);
        const EC_POINT *p1 = NULL;
        const EC_POINT *p2 = NULL;
        const EC_GROUP *g1 = NULL;
        const EC_GROUP *g2 = NULL;

        if (ec1 == NULL || ec2 == NULL) {
            return 1;
        }

        p1 = EC_KEY_get0_public_key(ec1);
        p2 = EC_KEY_get0_public_key(ec2);
        g1 = EC_KEY_get0_group(ec1);
        g2 = EC_KEY_get0_group(ec2);

        if (p1 == NULL || p2 == NULL || g1 == NULL || g2 == NULL) {
            return 1;
        }

        if (EC_GROUP_cmp(g1, g2, NULL) != 0) {
            return 1;
        }

        if (EC_POINT_cmp(g1, p1, p2, NULL) != 0) {
            return 1;
        }

        if (what == SSH_KEY_CMP_PRIVATE && !is_sk_key_type(k1->type)) {
            if (bignum_cmp(EC_KEY_get0_private_key(ec1),
                           EC_KEY_get0_private_key(ec2))) {
                return 1;
            }
        }
        break;
    }
#endif /* HAVE_OPENSSL_ECC */
#endif /* OPENSSL_VERSION_NUMBER */
    case SSH_KEYTYPE_ED25519:
    case SSH_KEYTYPE_SK_ED25519:
        /* In FIPS mode, we can not use OpenSSL to compare Ed25519 keys.
         * The OpenSSL < 3.0 also crashes in EVP_PKEY_eq() when either of
         * keys keys is NULL so catch it here. */
        if (ssh_fips_mode() && k1->key == NULL && k2->key == NULL) {
            if (what == SSH_KEY_CMP_PRIVATE) {
                /* we should never have Ed25519 private key in FIPS mode */
                return 1;
            }
            cmp = memcmp(k1->ed25519_pubkey,
                         k2->ed25519_pubkey,
                         ED25519_KEY_LEN);
            if (cmp != 0) {
                return 1;
            }
            /* they match */
            return 0;
        }
        FALL_THROUGH;
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA1:
        rc = EVP_PKEY_eq(k1->key, k2->key);
        if (rc != 1) {
            return 1;
        }
        break;
    case SSH_KEYTYPE_UNKNOWN:
    default:
        return 1;
    }
    return 0;
}

ssh_string pki_private_key_to_pem(const ssh_key key,
                                  const char *passphrase,
                                  ssh_auth_callback auth_fn,
                                  void *auth_data)
{
    ssh_string blob = NULL;
    BUF_MEM *buf = NULL;
    BIO *mem = NULL;
    EVP_PKEY *pkey = NULL;
    int rc;

    mem = BIO_new(BIO_s_mem());
    if (mem == NULL) {
        return NULL;
    }

    switch (key->type) {
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA1:
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_ECDSA_P384:
    case SSH_KEYTYPE_ECDSA_P521:
    case SSH_KEYTYPE_ED25519:
        rc = EVP_PKEY_up_ref(key->key);
        if (rc != 1) {
            goto err;
        }
        pkey = key->key;

        /* Mark the operation as successful as for the other key types */
        rc = 1;

        break;
    case SSH_KEYTYPE_RSA_CERT01:
    case SSH_KEYTYPE_ECDSA_P256_CERT01:
    case SSH_KEYTYPE_ECDSA_P384_CERT01:
    case SSH_KEYTYPE_ECDSA_P521_CERT01:
    case SSH_KEYTYPE_ED25519_CERT01:
    case SSH_KEYTYPE_UNKNOWN:
    default:
        SSH_LOG(SSH_LOG_TRACE,
                "Unknown or invalid private key type %d",
                key->type);
        goto err;
    }
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to initialize EVP_PKEY structure");
        goto err;
    }

    if (passphrase == NULL) {
        struct pem_get_password_struct pgp = { auth_fn, auth_data };

        rc = PEM_write_bio_PrivateKey(mem,
                                      pkey,
                                      NULL, /* cipher */
                                      NULL, /* kstr */
                                      0, /* klen */
                                      pem_get_password,
                                      &pgp);
    } else {
        rc = PEM_write_bio_PrivateKey(mem,
                                      pkey,
                                      EVP_aes_128_cbc(),
                                      NULL, /* kstr */
                                      0, /* klen */
                                      NULL, /* auth_fn */
                                      (void*) passphrase);
    }
    EVP_PKEY_free(pkey);
    pkey = NULL;

    if (rc != 1) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to write private key: %s\n",
                ERR_error_string(ERR_get_error(), NULL));
        goto err;
    }

    BIO_get_mem_ptr(mem, &buf);

    blob = ssh_string_new(buf->length);
    if (blob == NULL) {
        goto err;
    }

    rc = ssh_string_fill(blob, buf->data, buf->length);
    if (rc < 0) {
        ssh_string_free(blob);
        goto err;
    }

    BIO_free(mem);

    return blob;

err:
    EVP_PKEY_free(pkey);
    BIO_free(mem);
    return NULL;
}

ssh_key pki_private_key_from_base64(const char *b64_key,
                                    const char *passphrase,
                                    ssh_auth_callback auth_fn,
                                    void *auth_data)
{
    BIO *mem = NULL;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY *ecdsa = NULL;
#endif /* OPENSSL_VERSION_NUMBER */
    ssh_key key = NULL;
    enum ssh_keytypes_e type = SSH_KEYTYPE_UNKNOWN;
    EVP_PKEY *pkey = NULL;

    mem = BIO_new_mem_buf((void*)b64_key, -1);

    if (passphrase == NULL) {
        if (auth_fn) {
            struct pem_get_password_struct pgp = { auth_fn, auth_data };

            pkey = PEM_read_bio_PrivateKey(mem, NULL, pem_get_password, &pgp);
        } else {
            /* openssl uses its own callback to get the passphrase here */
            pkey = PEM_read_bio_PrivateKey(mem, NULL, NULL, NULL);
        }
    } else {
        pkey = PEM_read_bio_PrivateKey(mem, NULL, NULL, (void *) passphrase);
    }

    BIO_free(mem);

    if (pkey == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Error parsing private key: %s",
                ERR_error_string(ERR_get_error(), NULL));
        return NULL;
    }
    switch (EVP_PKEY_base_id(pkey)) {
    case EVP_PKEY_RSA:
        type = SSH_KEYTYPE_RSA;
        break;
    case EVP_PKEY_EC:
#ifdef HAVE_OPENSSL_ECC
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        ecdsa = EVP_PKEY_get0_EC_KEY(pkey);
        if (ecdsa == NULL) {
            SSH_LOG(SSH_LOG_TRACE,
		    "Error parsing private key: %s",
                    ERR_error_string(ERR_get_error(), NULL));
            goto fail;
        }
#endif /* OPENSSL_VERSION_NUMBER */

        /* pki_privatekey_type_from_string always returns P256 for ECDSA
         * keys, so we need to figure out the correct type here */
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        type = pki_key_ecdsa_to_key_type(ecdsa);
#else
        type = pki_key_ecdsa_to_key_type(pkey);
#endif /* OPENSSL_VERSION_NUMBER */
        if (type == SSH_KEYTYPE_UNKNOWN) {
            SSH_LOG(SSH_LOG_TRACE, "Invalid private key.");
            goto fail;
        }

        break;
#endif /* HAVE_OPENSSL_ECC */
    case EVP_PKEY_ED25519:
        type = SSH_KEYTYPE_ED25519;
        break;
    default:
        SSH_LOG(SSH_LOG_TRACE, "Unknown or invalid private key type %d",
                EVP_PKEY_base_id(pkey));
        EVP_PKEY_free(pkey);
        return NULL;
    }

    key = ssh_key_new();
    if (key == NULL) {
        goto fail;
    }

    key->type = type;
    key->type_c = ssh_key_type_to_char(type);
    key->flags = SSH_KEY_FLAG_PRIVATE | SSH_KEY_FLAG_PUBLIC;
    key->key = pkey;
#ifdef HAVE_OPENSSL_ECC
    if (is_ecdsa_key_type(key->type)) {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        key->ecdsa_nid = pki_key_ecdsa_to_nid(ecdsa);
#else
        key->ecdsa_nid = pki_key_ecdsa_to_nid(key->key);
#endif /* OPENSSL_VERSION_NUMBER */
    }
#endif /* HAVE_OPENSSL_ECC */

    return key;
fail:
    EVP_PKEY_free(pkey);
    ssh_key_free(key);
    return NULL;
}

int pki_privkey_build_rsa(ssh_key key,
                          ssh_string n,
                          ssh_string e,
                          ssh_string d,
                          ssh_string iqmp,
                          ssh_string p,
                          ssh_string q)
{
    int rc;
    BIGNUM *be = NULL, *bn = NULL, *bd = NULL;
    BIGNUM *biqmp = NULL, *bp = NULL, *bq = NULL;
    BIGNUM *aux = NULL, *d_consttime = NULL;
    BIGNUM *bdmq1 = NULL, *bdmp1 = NULL;
    BN_CTX *ctx = NULL;

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    OSSL_PARAM_BLD *param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL) {
        return SSH_ERROR;
    }
#else
    RSA *key_rsa = RSA_new();
    if (key_rsa == NULL) {
        return SSH_ERROR;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    bn = ssh_make_string_bn(n);
    be = ssh_make_string_bn(e);
    bd = ssh_make_string_bn(d);
    biqmp = ssh_make_string_bn(iqmp);
    bp = ssh_make_string_bn(p);
    bq = ssh_make_string_bn(q);
    if (be == NULL || bn == NULL || bd == NULL ||
        /*biqmp == NULL ||*/ bp == NULL || bq == NULL) {
        rc = SSH_ERROR;
        goto fail;
    }

    /* Calculate remaining CRT parameters for OpenSSL to be happy
     * taken from OpenSSH */
    if ((ctx = BN_CTX_new()) == NULL) {
        rc = SSH_ERROR;
        goto fail;
    }
    if ((aux = BN_new()) == NULL ||
        (bdmq1 = BN_new()) == NULL ||
        (bdmp1 = BN_new()) == NULL) {
        rc = SSH_ERROR;
        goto fail;
    }
    if ((d_consttime = BN_dup(bd)) == NULL) {
        rc = SSH_ERROR;
        goto fail;
    }
    BN_set_flags(aux, BN_FLG_CONSTTIME);
    BN_set_flags(d_consttime, BN_FLG_CONSTTIME);

    if ((BN_sub(aux, bq, BN_value_one()) == 0) ||
        (BN_mod(bdmq1, d_consttime, aux, ctx) == 0) ||
        (BN_sub(aux, bp, BN_value_one()) == 0) ||
        (BN_mod(bdmp1, d_consttime, aux, ctx) == 0)) {
        rc = SSH_ERROR;
        goto fail;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    /* Memory management of be, bn and bd is transferred to RSA object */
    rc = RSA_set0_key(key_rsa, bn, be, bd);
    if (rc == 0) {
        goto fail;
    }

    /* Memory management of bp and bq is transferred to RSA object */
    rc = RSA_set0_factors(key_rsa, bp, bq);
    if (rc == 0) {
        goto fail;
    }

    /* p, q, dmp1, dmq1 and iqmp may be NULL in private keys, but the RSA
     * operations are much faster when these values are available.
     * https://www.openssl.org/docs/man1.0.2/crypto/rsa.html
     * And OpenSSL fails to export these keys to PEM if these are missing:
     * https://github.com/openssl/openssl/issues/21826
     */
    rc = RSA_set0_crt_params(key_rsa, bdmp1, bdmq1, biqmp);
    if (rc == 0) {
        goto fail;
    }
    bignum_safe_free(aux);
    bignum_safe_free(d_consttime);

    key->key = EVP_PKEY_new();
    if (key->key == NULL) {
        goto fail;
    }

    rc = EVP_PKEY_assign_RSA(key->key, key_rsa);
    if (rc != 1) {
        goto fail;
    }

    return SSH_OK;
fail:
    RSA_free(key_rsa);
    EVP_PKEY_free(key->key);
    return SSH_ERROR;
#else
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_N, bn);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_E, be);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_D, bd);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_FACTOR1, bp);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_FACTOR2, bq);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_EXPONENT1, bdmp1);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_EXPONENT2, bdmq1);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_COEFFICIENT1, biqmp);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = evp_build_pkey("RSA", param_bld, &(key->key), EVP_PKEY_KEYPAIR);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to import private key: %s\n",
                ERR_error_string(ERR_get_error(), NULL));
        rc = SSH_ERROR;
        goto fail;
    }

fail:
    OSSL_PARAM_BLD_free(param_bld);
    bignum_safe_free(bn);
    bignum_safe_free(be);
    bignum_safe_free(bd);
    bignum_safe_free(bp);
    bignum_safe_free(bq);
    bignum_safe_free(biqmp);

    bignum_safe_free(aux);
    bignum_safe_free(d_consttime);
    bignum_safe_free(bdmp1);
    bignum_safe_free(bdmq1);
    BN_CTX_free(ctx);
    return rc;
#endif /* OPENSSL_VERSION_NUMBER */
}

int pki_pubkey_build_rsa(ssh_key key,
                         ssh_string e,
                         ssh_string n) {
    int rc;
    BIGNUM *be = NULL, *bn = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    OSSL_PARAM_BLD *param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL) {
        return SSH_ERROR;
    }
#else
    RSA *key_rsa = RSA_new();
    if (key_rsa == NULL) {
        return SSH_ERROR;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    be = ssh_make_string_bn(e);
    if (be == NULL) {
        rc = SSH_ERROR;
        goto fail;
    }
    bn = ssh_make_string_bn(n);
    if (bn == NULL) {
        rc = SSH_ERROR;
        goto fail;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    rc = RSA_set0_key(key_rsa, bn, be, NULL);
    if (rc == 0) {
        goto fail;
    }
    /* Memory management of bn and be is transferred to RSA object */
    bn = NULL;
    be = NULL;

    key->key = EVP_PKEY_new();
    if (key->key == NULL) {
        goto fail;
    }

    rc = EVP_PKEY_assign_RSA(key->key, key_rsa);
    if (rc != 1) {
        goto fail;
    }

    return SSH_OK;
#else
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_N, bn);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_E, be);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = evp_build_pkey("RSA", param_bld, &(key->key), EVP_PKEY_PUBLIC_KEY);
#endif /* OPENSSL_VERSION_NUMBER */

fail:
    bignum_safe_free(bn);
    bignum_safe_free(be);
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EVP_PKEY_free(key->key);
    RSA_free(key_rsa);

    return SSH_ERROR;
#else
    OSSL_PARAM_BLD_free(param_bld);

    return rc;
#endif /* OPENSSL_VERSION_NUMBER */
}

ssh_string pki_key_to_blob(const ssh_key key, enum ssh_key_e type)
{
    ssh_buffer buffer = NULL;
    ssh_string type_s = NULL;
    ssh_string str = NULL;
    ssh_string e = NULL;
    ssh_string n = NULL;
    ssh_string p = NULL;
    ssh_string g = NULL;
    ssh_string q = NULL;
    ssh_string d = NULL;
    ssh_string iqmp = NULL;
    int rc;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    BIGNUM *bp = NULL, *bq = NULL, *bg = NULL, *bpub_key = NULL,
           *bn = NULL, *be = NULL,
           *bd = NULL, *biqmp = NULL;
    OSSL_PARAM *params = NULL;
#endif /* OPENSSL_VERSION_NUMBER */
    uint8_t *ed25519_pubkey = NULL;
    uint8_t *ed25519_privkey = NULL;
    size_t key_len = 0;

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        return NULL;
    }
    /* The buffer will contain sensitive information. Make sure it is erased */
    ssh_buffer_set_secure(buffer);

    if (key->cert != NULL) {
        rc = ssh_buffer_add_buffer(buffer, key->cert);
        if (rc < 0) {
            SSH_BUFFER_FREE(buffer);
            return NULL;
        }
        goto makestring;
    }

    type_s = ssh_string_from_char(key->type_c);
    if (type_s == NULL) {
        SSH_BUFFER_FREE(buffer);
        return NULL;
    }

    rc = ssh_buffer_add_ssh_string(buffer, type_s);
    SSH_STRING_FREE(type_s);
    if (rc < 0) {
        SSH_BUFFER_FREE(buffer);
        return NULL;
    }

    switch (key->type) {
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA1: {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        const BIGNUM *be = NULL, *bn = NULL;
        const RSA *key_rsa = EVP_PKEY_get0_RSA(key->key);
        RSA_get0_key(key_rsa, &bn, &be, NULL);
#else
        const OSSL_PARAM *out_param = NULL;
        rc = EVP_PKEY_todata(key->key, EVP_PKEY_PUBLIC_KEY, &params);
        if (rc != 1) {
            goto fail;
        }
        out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_RSA_E);
        if (out_param == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "RSA: No param E has been found");
            goto fail;
        }
        rc = OSSL_PARAM_get_BN(out_param, &be);
        if (rc != 1) {
            goto fail;
        }
        out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_RSA_N);
        if (out_param == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "RSA: No param N has been found");
            goto fail;
        }
        rc = OSSL_PARAM_get_BN(out_param, &bn);
        if (rc != 1) {
            goto fail;
        }
#endif /* OPENSSL_VERSION_NUMBER */
        e = ssh_make_bignum_string((BIGNUM *)be);
        if (e == NULL) {
            goto fail;
        }

        n = ssh_make_bignum_string((BIGNUM *)bn);
        if (n == NULL) {
            goto fail;
        }

        if (type == SSH_KEY_PUBLIC) {
            /* The N and E parts are swapped in the public key export ! */
            rc = ssh_buffer_add_ssh_string(buffer, e);
            if (rc < 0) {
                goto fail;
            }
            rc = ssh_buffer_add_ssh_string(buffer, n);
            if (rc < 0) {
                goto fail;
            }
        } else if (type == SSH_KEY_PRIVATE) {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
            const BIGNUM *bd, *biqmp, *bp, *bq;
            RSA_get0_key(key_rsa, NULL, NULL, &bd);
            RSA_get0_factors(key_rsa, &bp, &bq);
            RSA_get0_crt_params(key_rsa, NULL, NULL, &biqmp);
#else
            OSSL_PARAM_free(params);
            rc = EVP_PKEY_todata(key->key, EVP_PKEY_KEYPAIR, &params);
            if (rc != 1) {
                goto fail;
            }

            out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_RSA_D);
            if (out_param == NULL) {
                SSH_LOG(SSH_LOG_TRACE, "RSA: No param D has been found");
                goto fail;
            }
            rc = OSSL_PARAM_get_BN(out_param, &bd);
            if (rc != 1) {
                goto fail;
            }

            out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_RSA_FACTOR1);
            if (out_param == NULL) {
                SSH_LOG(SSH_LOG_TRACE, "RSA: No param P has been found");
                goto fail;
            }
            rc = OSSL_PARAM_get_BN(out_param, &bp);
            if (rc != 1) {
                goto fail;
            }

            out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_RSA_FACTOR2);
            if (out_param == NULL) {
                SSH_LOG(SSH_LOG_TRACE, "RSA: No param Q has been found");
                goto fail;
            }
            rc = OSSL_PARAM_get_BN(out_param, &bq);
            if (rc != 1) {
                goto fail;
            }

            out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_RSA_COEFFICIENT1);
            if (out_param == NULL) {
                SSH_LOG(SSH_LOG_TRACE, "RSA: No param IQMP has been found");
                goto fail;
            }
            rc = OSSL_PARAM_get_BN(out_param, &biqmp);
            if (rc != 1) {
                goto fail;
            }
#endif /* OPENSSL_VERSION_NUMBER */
            rc = ssh_buffer_add_ssh_string(buffer, n);
            if (rc < 0) {
                goto fail;
            }
            rc = ssh_buffer_add_ssh_string(buffer, e);
            if (rc < 0) {
                goto fail;
            }

            d = ssh_make_bignum_string((BIGNUM *)bd);
            if (d == NULL) {
                goto fail;
            }

            iqmp = ssh_make_bignum_string((BIGNUM *)biqmp);
            if (iqmp == NULL) {
                goto fail;
            }

            p = ssh_make_bignum_string((BIGNUM *)bp);
            if (p == NULL) {
                goto fail;
            }

            q = ssh_make_bignum_string((BIGNUM *)bq);
            if (q == NULL) {
                goto fail;
            }

            rc = ssh_buffer_add_ssh_string(buffer, d);
            if (rc < 0) {
                goto fail;
            }
            rc = ssh_buffer_add_ssh_string(buffer, iqmp);
            if (rc < 0) {
                goto fail;
            }
            rc = ssh_buffer_add_ssh_string(buffer, p);
            if (rc < 0) {
                goto fail;
            }
            rc = ssh_buffer_add_ssh_string(buffer, q);
            if (rc < 0) {
                goto fail;
            }

            ssh_string_burn(d);
            SSH_STRING_FREE(d);
            ssh_string_burn(iqmp);
            SSH_STRING_FREE(iqmp);
            ssh_string_burn(p);
            SSH_STRING_FREE(p);
            ssh_string_burn(q);
            SSH_STRING_FREE(q);
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
            bignum_safe_free(bd);
            bignum_safe_free(biqmp);
            bignum_safe_free(bp);
            bignum_safe_free(bq);
#endif /* OPENSSL_VERSION_NUMBER */
        }
        ssh_string_burn(e);
        SSH_STRING_FREE(e);
        ssh_string_burn(n);
        SSH_STRING_FREE(n);
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        bignum_safe_free(bn);
        bignum_safe_free(be);
        OSSL_PARAM_free(params);
        params = NULL;
#endif /* OPENSSL_VERSION_NUMBER */
        break;
    }
    case SSH_KEYTYPE_ED25519:
    case SSH_KEYTYPE_SK_ED25519:
        rc = EVP_PKEY_get_raw_public_key(key->key, NULL, &key_len);
        if (rc != 1) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Failed to get ed25519 raw public key length: %s",
                    ERR_error_string(ERR_get_error(), NULL));
            goto fail;
        }

        if (key_len != ED25519_KEY_LEN) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Unexpected length of private key %zu. Expected %d.",
                    key_len,
                    ED25519_KEY_LEN);
            goto fail;
        }

        ed25519_pubkey = malloc(key_len);
        if (ed25519_pubkey == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "Out of memory");
            goto fail;
        }

        rc = EVP_PKEY_get_raw_public_key(key->key,
                                         (uint8_t *)ed25519_pubkey,
                                         &key_len);
        if (rc != 1) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Failed to get ed25519 raw public key:  %s",
                    ERR_error_string(ERR_get_error(), NULL));
            goto fail;
        }

        rc = ssh_buffer_pack(buffer,
                             "dP",
                             (uint32_t)ED25519_KEY_LEN,
                             (size_t)ED25519_KEY_LEN,
                             ed25519_pubkey);
        if (rc == SSH_ERROR) {
            goto fail;
        }

        if (type == SSH_KEY_PRIVATE && key->type == SSH_KEYTYPE_ED25519) {
            key_len = 0;
            rc = EVP_PKEY_get_raw_private_key(key->key, NULL, &key_len);
            if (rc != 1) {
                SSH_LOG(SSH_LOG_TRACE,
                        "Failed to get ed25519 raw private key length: %s",
                        ERR_error_string(ERR_get_error(), NULL));
                goto fail;
            }

            if (key_len != ED25519_KEY_LEN) {
                SSH_LOG(SSH_LOG_TRACE,
                        "Unexpected length of private key %zu. Expected %d.",
                        key_len,
                        ED25519_KEY_LEN);
                goto fail;
            }

            ed25519_privkey = malloc(key_len);
            if (ed25519_privkey == NULL) {
                SSH_LOG(SSH_LOG_TRACE, "Out of memory");
                goto fail;
            }

            rc = EVP_PKEY_get_raw_private_key(key->key,
                                              ed25519_privkey,
                                              &key_len);
            if (rc != 1) {
                SSH_LOG(SSH_LOG_TRACE,
                        "Failed to get ed25519 raw private key: %s",
                        ERR_error_string(ERR_get_error(), NULL));
                goto fail;
            }

            rc = ssh_buffer_pack(buffer,
                                 "dPP",
                                 (uint32_t)(2 * ED25519_KEY_LEN),
                                 (size_t)ED25519_KEY_LEN,
                                 ed25519_privkey,
                                 (size_t)ED25519_KEY_LEN,
                                 ed25519_pubkey);
            if (rc == SSH_ERROR) {
                goto fail;
            }
            ssh_burn(ed25519_privkey, ED25519_KEY_LEN);
            SAFE_FREE(ed25519_privkey);
        } else if (type == SSH_KEY_PRIVATE &&
                   key->type == SSH_KEYTYPE_SK_ED25519) {

            rc = pki_buffer_pack_sk_priv_data(buffer, key);
            if (rc == SSH_ERROR) {
                goto fail;
            }
        } else if (type == SSH_KEY_PUBLIC &&
                   key->type == SSH_KEYTYPE_SK_ED25519) {
            /* public key can contain certificate sk information */
            rc = ssh_buffer_add_ssh_string(buffer, key->sk_application);
            if (rc != SSH_OK) {
                goto fail;
            }
        }

        SAFE_FREE(ed25519_pubkey);
        break;
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_ECDSA_P384:
    case SSH_KEYTYPE_ECDSA_P521:
    case SSH_KEYTYPE_SK_ECDSA:
#ifdef HAVE_OPENSSL_ECC
    {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        EC_GROUP *group = NULL;
        EC_POINT *point = NULL;
        const void *pubkey = NULL;
        size_t pubkey_len;
        OSSL_PARAM *locate_param = NULL;
#else
        const EC_GROUP *group = NULL;
        const EC_POINT *point = NULL;
        const BIGNUM *exp = NULL;
        EC_KEY *ec = NULL;
#endif /* OPENSSL_VERSION_NUMBER */

        type_s = ssh_string_from_char(pki_key_ecdsa_nid_to_char(key->ecdsa_nid));
        if (type_s == NULL) {
            SSH_BUFFER_FREE(buffer);
            return NULL;
        }

        rc = ssh_buffer_add_ssh_string(buffer, type_s);
        SSH_STRING_FREE(type_s);
        if (rc < 0) {
            SSH_BUFFER_FREE(buffer);
            return NULL;
        }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
        ec = EVP_PKEY_get0_EC_KEY(key->key);
        if (ec == NULL) {
            goto fail;
        }
#ifdef WITH_PKCS11_URI
        if (ssh_key_is_private(key) && !EC_KEY_get0_public_key(ec)) {
            SSH_LOG(SSH_LOG_TRACE,
                    "It is mandatory to have separate"
                    " public ECDSA key objects in the PKCS #11 device."
                    " Unlike RSA, ECDSA public keys cannot be derived"
                    " from their private keys.");
            goto fail;
        }
#endif /* WITH_PKCS11_URI */
        group = EC_KEY_get0_group(ec);
        point = EC_KEY_get0_public_key(ec);
        if (group == NULL || point == NULL) {
            goto fail;
        }
        e = pki_key_make_ecpoint_string(group, point);
#else
        rc = EVP_PKEY_todata(key->key, EVP_PKEY_PUBLIC_KEY, &params);
        if (rc < 0) {
            goto fail;
        }

        locate_param = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_PUB_KEY);
#ifdef WITH_PKCS11_URI
        if (ssh_key_is_private(key) && !locate_param) {
            SSH_LOG(SSH_LOG_TRACE,
                    "It is mandatory to have separate"
                    " public ECDSA key objects in the PKCS #11 device."
                    " Unlike RSA, ECDSA public keys cannot be derived"
                    " from their private keys.");
            goto fail;
        }
#endif /* WITH_PKCS11_URI */

        rc = OSSL_PARAM_get_octet_string_ptr(locate_param, &pubkey, &pubkey_len);
        if (rc != 1) {
            goto fail;
        }
        /* Convert the data to low-level representation */
        group = EC_GROUP_new_by_curve_name_ex(NULL, NULL, key->ecdsa_nid);
        point = EC_POINT_new(group);
        rc = EC_POINT_oct2point(group, point, pubkey, pubkey_len, NULL);
        if (group == NULL || point == NULL || rc != 1) {
            EC_GROUP_free(group);
            EC_POINT_free(point);
            goto fail;
        }

        e = pki_key_make_ecpoint_string(group, point);
        EC_GROUP_free(group);
        EC_POINT_free(point);
#endif /* OPENSSL_VERSION_NUMBER */
        if (e == NULL) {
            SSH_BUFFER_FREE(buffer);
            return NULL;
        }

        rc = ssh_buffer_add_ssh_string(buffer, e);
        if (rc < 0) {
            goto fail;
        }

        ssh_string_burn(e);
        SSH_STRING_FREE(e);
        e = NULL;

        if (type == SSH_KEY_PRIVATE && key->type != SSH_KEYTYPE_SK_ECDSA) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
            OSSL_PARAM_free(params);
            rc = EVP_PKEY_todata(key->key, EVP_PKEY_KEYPAIR, &params);
            if (rc < 0) {
                goto fail;
            }

            locate_param = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_PRIV_KEY);
            rc = OSSL_PARAM_get_BN(locate_param, &bd);
            if (rc != 1) {
                goto fail;
            }
            d = ssh_make_bignum_string((BIGNUM *)bd);
            if (d == NULL) {
                goto fail;
            }
            if (ssh_buffer_add_ssh_string(buffer, d) < 0) {
                goto fail;
            }
#else
            exp = EC_KEY_get0_private_key(ec);
            if (exp == NULL) {
                goto fail;
            }
            d = ssh_make_bignum_string((BIGNUM *)exp);
            if (d == NULL) {
                goto fail;
            }
            rc = ssh_buffer_add_ssh_string(buffer, d);
            if (rc < 0) {
                goto fail;
            }
#endif /* OPENSSL_VERSION_NUMBER */
            ssh_string_burn(d);
            SSH_STRING_FREE(d);
            d = NULL;
        } else if (type == SSH_KEY_PRIVATE &&
                   key->type == SSH_KEYTYPE_SK_ECDSA) {

            rc = pki_buffer_pack_sk_priv_data(buffer, key);
            if (rc == SSH_ERROR) {
                goto fail;
            }
        } else if (type == SSH_KEY_PUBLIC &&
                   key->type == SSH_KEYTYPE_SK_ECDSA) {
            /* public key can contain certificate sk information */
            rc = ssh_buffer_add_ssh_string(buffer, key->sk_application);
            if (rc != SSH_OK) {
                goto fail;
            }
        }
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        bignum_safe_free(bd);
        OSSL_PARAM_free(params);
        params = NULL;
#endif /* OPENSSL_VERSION_NUMBER */
        break;
    }
#endif /* HAVE_OPENSSL_ECC */
    case SSH_KEYTYPE_UNKNOWN:
    default:
        goto fail;
    }

makestring:
    str = ssh_string_new(ssh_buffer_get_len(buffer));
    if (str == NULL) {
        goto fail;
    }

    rc = ssh_string_fill(str, ssh_buffer_get(buffer), ssh_buffer_get_len(buffer));
    if (rc < 0) {
        goto fail;
    }
    SSH_BUFFER_FREE(buffer);

    return str;
fail:
    SSH_BUFFER_FREE(buffer);
    ssh_string_burn(str);
    SSH_STRING_FREE(str);
    ssh_string_burn(e);
    SSH_STRING_FREE(e);
    ssh_string_burn(p);
    SSH_STRING_FREE(p);
    ssh_string_burn(g);
    SSH_STRING_FREE(g);
    ssh_string_burn(q);
    SSH_STRING_FREE(q);
    ssh_string_burn(n);
    SSH_STRING_FREE(n);
    ssh_string_burn(d);
    SSH_STRING_FREE(d);
    ssh_string_burn(iqmp);
    SSH_STRING_FREE(iqmp);
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    bignum_safe_free(bp);
    bignum_safe_free(bq);
    bignum_safe_free(bg);
    bignum_safe_free(bpub_key);
    bignum_safe_free(bn);
    bignum_safe_free(be);
    bignum_safe_free(bd);
    bignum_safe_free(biqmp);
    OSSL_PARAM_free(params);
#endif /* OPENSSL_VERSION_NUMBER */
    free(ed25519_pubkey);
    if (ed25519_privkey) {
        ssh_burn(ed25519_privkey, ED25519_KEY_LEN);
        free(ed25519_privkey);
    }

    return NULL;
}

static ssh_string pki_ecdsa_signature_to_blob(const ssh_signature sig)
{
    ssh_string r = NULL;
    ssh_string s = NULL;

    ssh_buffer buf = NULL;
    ssh_string sig_blob = NULL;

    const BIGNUM *pr = NULL, *ps = NULL;

    const unsigned char *raw_sig_data = NULL;
    long raw_sig_len;

    ECDSA_SIG *ecdsa_sig = NULL;

    int rc;

    if (sig == NULL || sig->raw_sig == NULL) {
        return NULL;
    }
    raw_sig_data = ssh_string_data(sig->raw_sig);
    if (raw_sig_data == NULL) {
        return NULL;
    }
    raw_sig_len = (long)ssh_string_len(sig->raw_sig);

    ecdsa_sig = d2i_ECDSA_SIG(NULL, &raw_sig_data, raw_sig_len);
    if (ecdsa_sig == NULL) {
        return NULL;
    }

    ECDSA_SIG_get0(ecdsa_sig, &pr, &ps);
    if (pr == NULL || ps == NULL) {
        goto error;
    }

    r = ssh_make_bignum_string((BIGNUM *)pr);
    if (r == NULL) {
        goto error;
    }

    s = ssh_make_bignum_string((BIGNUM *)ps);
    if (s == NULL) {
        goto error;
    }

    buf = ssh_buffer_new();
    if (buf == NULL) {
        goto error;
    }

    rc = ssh_buffer_add_ssh_string(buf, r);
    if (rc < 0) {
        goto error;
    }

    rc = ssh_buffer_add_ssh_string(buf, s);
    if (rc < 0) {
        goto error;
    }

    sig_blob = ssh_string_new(ssh_buffer_get_len(buf));
    if (sig_blob == NULL) {
        goto error;
    }

    rc = ssh_string_fill(sig_blob, ssh_buffer_get(buf), ssh_buffer_get_len(buf));
    if (rc < 0) {
        goto error;
    }

    SSH_STRING_FREE(r);
    SSH_STRING_FREE(s);
    ECDSA_SIG_free(ecdsa_sig);
    SSH_BUFFER_FREE(buf);

    return sig_blob;

error:
    SSH_STRING_FREE(sig_blob);
    SSH_STRING_FREE(r);
    SSH_STRING_FREE(s);
    ECDSA_SIG_free(ecdsa_sig);
    SSH_BUFFER_FREE(buf);
    return NULL;
}

ssh_string pki_signature_to_blob(const ssh_signature sig)
{
    ssh_string sig_blob = NULL;

    switch(sig->type) {
        case SSH_KEYTYPE_RSA:
        case SSH_KEYTYPE_RSA1:
            sig_blob = ssh_string_copy(sig->raw_sig);
            break;
        case SSH_KEYTYPE_ED25519:
            sig_blob = pki_ed25519_signature_to_blob(sig);
            break;
        case SSH_KEYTYPE_ECDSA_P256:
        case SSH_KEYTYPE_ECDSA_P384:
        case SSH_KEYTYPE_ECDSA_P521:
#ifdef HAVE_OPENSSL_ECC
            sig_blob = pki_ecdsa_signature_to_blob(sig);
            break;
#endif /* HAVE_OPENSSL_ECC */
        case SSH_KEYTYPE_SK_ECDSA:
        case SSH_KEYTYPE_SK_ED25519:
            /* For SK keys, signature data is already in raw_sig */
            sig_blob = ssh_string_copy(sig->raw_sig);
            break;
        default:
        case SSH_KEYTYPE_UNKNOWN:
            SSH_LOG(SSH_LOG_TRACE, "Unknown signature key type: %s", sig->type_c);
            return NULL;
    }

    return sig_blob;
}

static int pki_signature_from_rsa_blob(const ssh_key pubkey,
                                       const ssh_string sig_blob,
                                       ssh_signature sig)
{
    size_t pad_len = 0;
    char *blob_orig = NULL;
    char *blob_padded_data = NULL;
    ssh_string sig_blob_padded = NULL;

    size_t rsalen = 0;
    size_t len = ssh_string_len(sig_blob);

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    const RSA *rsa = EVP_PKEY_get0_RSA(pubkey->key);

    if (rsa == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "RSA field NULL");
        goto errout;
    }

    rsalen = RSA_size(rsa);
#else
    if (EVP_PKEY_get_base_id(pubkey->key) != EVP_PKEY_RSA) {
        SSH_LOG(SSH_LOG_TRACE, "Key has no RSA pubkey");
        goto errout;
    }

    rsalen = EVP_PKEY_size(pubkey->key);
#endif /* OPENSSL_VERSION_NUMBER */
    if (len > rsalen) {
        SSH_LOG(SSH_LOG_TRACE,
                "Signature is too big: %lu > %lu",
                (unsigned long)len,
                (unsigned long)rsalen);
        goto errout;
    }

#ifdef DEBUG_CRYPTO
    SSH_LOG(SSH_LOG_DEBUG, "RSA signature len: %lu", (unsigned long)len);
    ssh_log_hexdump("RSA signature", ssh_string_data(sig_blob), len);
#endif /* DEBUG_CRYPTO */

    if (len == rsalen) {
        sig->raw_sig = ssh_string_copy(sig_blob);
    } else {
        /* pad the blob to the expected rsalen size */
        SSH_LOG(SSH_LOG_DEBUG,
                "RSA signature len %lu < %lu",
                (unsigned long)len,
                (unsigned long)rsalen);

        pad_len = rsalen - len;

        sig_blob_padded = ssh_string_new(rsalen);
        if (sig_blob_padded == NULL) {
            goto errout;
        }

        blob_padded_data = (char *) ssh_string_data(sig_blob_padded);
        blob_orig = (char *) ssh_string_data(sig_blob);

        if (blob_padded_data == NULL || blob_orig == NULL) {
            goto errout;
        }

        /* front-pad the buffer with zeroes */
        ssh_burn(blob_padded_data, pad_len);
        /* fill the rest with the actual signature blob */
        memcpy(blob_padded_data + pad_len, blob_orig, len);

        sig->raw_sig = sig_blob_padded;
    }

    return SSH_OK;

errout:
    SSH_STRING_FREE(sig_blob_padded);
    return SSH_ERROR;
}

static int pki_signature_from_ecdsa_blob(UNUSED_PARAM(const ssh_key pubkey),
                                         const ssh_string sig_blob,
                                         ssh_signature sig)
{
    ECDSA_SIG *ecdsa_sig = NULL;
    BIGNUM *pr = NULL, *ps = NULL;

    ssh_string r = NULL;
    ssh_string s = NULL;

    ssh_buffer buf = NULL;
    uint32_t rlen;

    unsigned char *raw_sig_data = NULL;
    unsigned char *temp_raw_sig = NULL;
    size_t raw_sig_len = 0;

    int rc;

    /* build ecdsa signature */
    buf = ssh_buffer_new();
    if (buf == NULL) {
        return SSH_ERROR;
    }

    /* The buffer will contain sensitive information. Make sure it is erased */
    ssh_buffer_set_secure(buf);

    rc = ssh_buffer_add_data(buf,
                             ssh_string_data(sig_blob),
                             (uint32_t)ssh_string_len(sig_blob));
    if (rc < 0) {
        goto error;
    }

    r = ssh_buffer_get_ssh_string(buf);
    if (r == NULL) {
        goto error;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("r", ssh_string_data(r), ssh_string_len(r));
#endif

    pr = ssh_make_string_bn(r);
    ssh_string_burn(r);
    SSH_STRING_FREE(r);
    if (pr == NULL) {
        goto error;
    }

    s = ssh_buffer_get_ssh_string(buf);
    rlen = ssh_buffer_get_len(buf);
    SSH_BUFFER_FREE(buf);
    if (s == NULL) {
        goto error;
    }

    if (rlen != 0) {
        ssh_string_burn(s);
        SSH_STRING_FREE(s);
        SSH_LOG(SSH_LOG_TRACE,
                "Signature has remaining bytes in inner "
                "sigblob: %lu",
                (unsigned long)rlen);
        goto error;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("s", ssh_string_data(s), ssh_string_len(s));
#endif

    ps = ssh_make_string_bn(s);
    ssh_string_burn(s);
    SSH_STRING_FREE(s);
    if (ps == NULL) {
        goto error;
    }

    ecdsa_sig = ECDSA_SIG_new();
    if (ecdsa_sig == NULL) {
        goto error;
    }

    /* Memory management of pr and ps is transferred to
     * ECDSA signature object */
    rc = ECDSA_SIG_set0(ecdsa_sig, pr, ps);
    if (rc == 0) {
        goto error;
    }
    pr = NULL;
    ps = NULL;

    /* Get the expected size of the buffer */
    rc = i2d_ECDSA_SIG(ecdsa_sig, NULL);
    if (rc <= 0) {
        goto error;
    }
    raw_sig_len = rc;

    raw_sig_data = (unsigned char *)calloc(1, raw_sig_len);
    if (raw_sig_data == NULL) {
        goto error;
    }
    temp_raw_sig = raw_sig_data;

    /* It is necessary to use a temporary pointer as i2d_* "advances" the
     * pointer */
    rc = i2d_ECDSA_SIG(ecdsa_sig, &temp_raw_sig);
    if (rc <= 0) {
        goto error;
    }

    sig->raw_sig = ssh_string_new(raw_sig_len);
    if (sig->raw_sig == NULL) {
        ssh_burn(raw_sig_data, raw_sig_len);
        goto error;
    }

    rc = ssh_string_fill(sig->raw_sig, raw_sig_data, raw_sig_len);
    if (rc < 0) {
        ssh_burn(raw_sig_data, raw_sig_len);
        goto error;
    }

    ssh_burn(raw_sig_data, raw_sig_len);
    SAFE_FREE(raw_sig_data);
    ECDSA_SIG_free(ecdsa_sig);
    return SSH_OK;

error:
    SSH_BUFFER_FREE(buf);
    bignum_safe_free(ps);
    bignum_safe_free(pr);
    SAFE_FREE(raw_sig_data);
    if (ecdsa_sig != NULL) {
        ECDSA_SIG_free(ecdsa_sig);
    }
    return SSH_ERROR;
}

ssh_signature pki_signature_from_blob(const ssh_key pubkey,
                                      const ssh_string sig_blob,
                                      enum ssh_keytypes_e type,
                                      enum ssh_digest_e hash_type)
{
    ssh_signature sig;
    int rc;

    if (ssh_key_type_plain(pubkey->type) != type) {
        SSH_LOG(SSH_LOG_TRACE,
                "Incompatible public key provided (%d) expecting (%d)",
                type,
                pubkey->type);
        return NULL;
    }

    sig = ssh_signature_new();
    if (sig == NULL) {
        return NULL;
    }

    sig->type = type;
    sig->type_c = ssh_key_signature_to_char(type, hash_type);
    sig->hash_type = hash_type;

    switch(type) {
        case SSH_KEYTYPE_RSA:
        case SSH_KEYTYPE_RSA1:
            rc = pki_signature_from_rsa_blob(pubkey, sig_blob, sig);
            if (rc != SSH_OK) {
                goto error;
            }
            break;
        case SSH_KEYTYPE_ED25519:
        case SSH_KEYTYPE_SK_ED25519:
            rc = pki_signature_from_ed25519_blob(sig, sig_blob);
            if (rc != SSH_OK){
                goto error;
            }
            break;
        case SSH_KEYTYPE_ECDSA_P256:
        case SSH_KEYTYPE_ECDSA_P384:
        case SSH_KEYTYPE_ECDSA_P521:
        case SSH_KEYTYPE_ECDSA_P256_CERT01:
        case SSH_KEYTYPE_ECDSA_P384_CERT01:
        case SSH_KEYTYPE_ECDSA_P521_CERT01:
        case SSH_KEYTYPE_SK_ECDSA:
        case SSH_KEYTYPE_SK_ECDSA_CERT01:
#ifdef HAVE_OPENSSL_ECC
            rc = pki_signature_from_ecdsa_blob(pubkey, sig_blob, sig);
            if (rc != SSH_OK) {
                goto error;
            }
            break;
#endif
        default:
        case SSH_KEYTYPE_UNKNOWN:
            SSH_LOG(SSH_LOG_TRACE, "Unknown signature type");
            goto error;
    }

    return sig;

error:
    ssh_signature_free(sig);
    return NULL;
}

static const EVP_MD *pki_digest_to_md(enum ssh_digest_e hash_type)
{
    const EVP_MD *md = NULL;

    switch (hash_type) {
    case SSH_DIGEST_SHA256:
        md = EVP_sha256();
        break;
    case SSH_DIGEST_SHA384:
        md = EVP_sha384();
        break;
    case SSH_DIGEST_SHA512:
        md = EVP_sha512();
        break;
    case SSH_DIGEST_SHA1:
        md = EVP_sha1();
        break;
    case SSH_DIGEST_AUTO:
        md = NULL;
        break;
    default:
        SSH_LOG(SSH_LOG_TRACE, "Unknown hash algorithm for type: %d",
                hash_type);
        return NULL;
    }

    return md;
}

static EVP_PKEY *pki_key_to_pkey(ssh_key key)
{
    EVP_PKEY *pkey = NULL;
    int rc = 0;

    switch (key->type) {
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA1:
    case SSH_KEYTYPE_RSA_CERT01:
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_ECDSA_P384:
    case SSH_KEYTYPE_ECDSA_P521:
    case SSH_KEYTYPE_ECDSA_P256_CERT01:
    case SSH_KEYTYPE_ECDSA_P384_CERT01:
    case SSH_KEYTYPE_ECDSA_P521_CERT01:
    case SSH_KEYTYPE_SK_ECDSA:
    case SSH_KEYTYPE_SK_ECDSA_CERT01:
    case SSH_KEYTYPE_ED25519:
    case SSH_KEYTYPE_ED25519_CERT01:
    case SSH_KEYTYPE_SK_ED25519:
    case SSH_KEYTYPE_SK_ED25519_CERT01:
        if (key->key == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "NULL key->key");
            goto error;
        }
        rc = EVP_PKEY_up_ref(key->key);
        if (rc != 1) {
            SSH_LOG(SSH_LOG_TRACE, "Failed to reference EVP_PKEY");
            return NULL;
        }
        pkey = key->key;
        break;
    case SSH_KEYTYPE_UNKNOWN:
    default:
        SSH_LOG(SSH_LOG_TRACE, "Unknown private key algorithm for type: %d",
                key->type);
        goto error;
    }

    return pkey;

error:
    EVP_PKEY_free(pkey);
    return NULL;
}

/**
 * @internal
 *
 * @brief Sign the given input data. The digest to be signed is calculated
 * internally as necessary.
 *
 * @param[in]   privkey     The private key to be used for signing.
 * @param[in]   hash_type   The digest algorithm to be used.
 * @param[in]   input       The data to be signed.
 * @param[in]   input_len   The length of the data to be signed.
 *
 * @return  a newly allocated ssh_signature or NULL on error.
 */
ssh_signature pki_sign_data(const ssh_key privkey,
                            enum ssh_digest_e hash_type,
                            const unsigned char *input,
                            size_t input_len)
{
    const EVP_MD *md = NULL;
    EVP_MD_CTX *ctx = NULL;
    EVP_PKEY *pkey = NULL;

    unsigned char *raw_sig_data = NULL;
    size_t raw_sig_len;

    ssh_signature sig = NULL;

    int rc;

    if (privkey == NULL || !ssh_key_is_private(privkey) || input == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Bad parameter provided to "
                               "pki_sign_data()");
        return NULL;
    }

    /* Check if public key and hash type are compatible */
    rc = pki_key_check_hash_compatible(privkey, hash_type);
    if (rc != SSH_OK) {
        return NULL;
    }

    /* Set hash algorithm to be used */
    md = pki_digest_to_md(hash_type);
    if (md == NULL) {
        if (hash_type != SSH_DIGEST_AUTO) {
            return NULL;
        }
    }

    /* Setup private key EVP_PKEY */
    pkey = pki_key_to_pkey(privkey);
    if (pkey == NULL) {
        return NULL;
    }

    /* Allocate buffer for signature */
    raw_sig_len = (size_t)EVP_PKEY_size(pkey);
    raw_sig_data = (unsigned char *)malloc(raw_sig_len);
    if (raw_sig_data == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Out of memory");
        goto out;
    }

    /* Create the context */
    ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Out of memory");
        goto out;
    }

    /* Sign the data */
    rc = EVP_DigestSignInit(ctx, NULL, md, NULL, pkey);
    if (rc != 1){
        SSH_LOG(SSH_LOG_TRACE,
                "EVP_DigestSignInit() failed: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    rc = EVP_DigestSign(ctx, raw_sig_data, &raw_sig_len, input, input_len);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "EVP_DigestSign() failed: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

#ifdef DEBUG_CRYPTO
        ssh_log_hexdump("Generated signature", raw_sig_data, raw_sig_len);
#endif

    /* Allocate and fill output signature */
    sig = ssh_signature_new();
    if (sig == NULL) {
        goto out;
    }

    sig->raw_sig = ssh_string_new(raw_sig_len);
    if (sig->raw_sig == NULL) {
        ssh_signature_free(sig);
        sig = NULL;
        goto out;
    }

    rc = ssh_string_fill(sig->raw_sig, raw_sig_data, raw_sig_len);
    if (rc < 0) {
        ssh_signature_free(sig);
        sig = NULL;
        goto out;
    }

    sig->type = privkey->type;
    sig->hash_type = hash_type;
    sig->type_c = ssh_key_signature_to_char(privkey->type, hash_type);

out:
    if (ctx != NULL) {
        EVP_MD_CTX_free(ctx);
    }
    if (raw_sig_data != NULL) {
        ssh_burn(raw_sig_data, raw_sig_len);
    }
    SAFE_FREE(raw_sig_data);
    EVP_PKEY_free(pkey);
    return sig;
}

/**
 * @internal
 *
 * @brief Verify the signature of a given input. The digest of the input is
 * calculated internally as necessary.
 *
 * @param[in]   signature   The signature to be verified.
 * @param[in]   pubkey      The public key used to verify the signature.
 * @param[in]   input       The signed data.
 * @param[in]   input_len   The length of the signed data.
 *
 * @return  SSH_OK if the signature is valid; SSH_ERROR otherwise.
 */
int pki_verify_data_signature(ssh_signature signature,
                              const ssh_key pubkey,
                              const unsigned char *input,
                              size_t input_len)
{
    const EVP_MD *md = NULL;
    EVP_MD_CTX *ctx = NULL;
    EVP_PKEY *pkey = NULL;

    unsigned char *raw_sig_data = NULL;
    size_t raw_sig_len;

    /* Function return code
     * Do not change this variable throughout the function until the signature
     * is successfully verified!
     */
    int rc = SSH_ERROR;
    int ok;

    if (pubkey == NULL || ssh_key_is_private(pubkey) || input == NULL ||
        signature == NULL || signature->raw_sig == NULL)
    {
        SSH_LOG(SSH_LOG_TRACE, "Bad parameter provided to "
                               "pki_verify_data_signature()");
        return SSH_ERROR;
    }

    /* Check if public key and hash type are compatible */
    ok = pki_key_check_hash_compatible(pubkey, signature->hash_type);
    if (ok != SSH_OK) {
        return SSH_ERROR;
    }

    /* Get the signature to be verified */
    raw_sig_data = ssh_string_data(signature->raw_sig);
    raw_sig_len = ssh_string_len(signature->raw_sig);
    if (raw_sig_data == NULL) {
        return SSH_ERROR;
    }

    /* Set hash algorithm to be used */
    md = pki_digest_to_md(signature->hash_type);
    if (md == NULL) {
        if (signature->hash_type != SSH_DIGEST_AUTO) {
            return SSH_ERROR;
        }
    }

    /* Setup public key EVP_PKEY */
    pkey = pki_key_to_pkey(pubkey);
    if (pkey == NULL) {
        return SSH_ERROR;
    }

    /* Create the context */
    ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to create EVP_MD_CTX: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    /* Verify the signature */
    ok = EVP_DigestVerifyInit(ctx, NULL, md, NULL, pkey);
    if (ok != 1){
        SSH_LOG(SSH_LOG_TRACE,
                "EVP_DigestVerifyInit() failed: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    ok = EVP_DigestVerify(ctx, raw_sig_data, raw_sig_len, input, input_len);
    if (ok != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Signature invalid: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    SSH_LOG(SSH_LOG_TRACE, "Signature valid");
    rc = SSH_OK;

out:
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc;
}

int ssh_key_size(ssh_key key)
{
    int bits = 0;
    EVP_PKEY *pkey = NULL;

    switch (key->type) {
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA_CERT01:
    case SSH_KEYTYPE_RSA1:
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_ECDSA_P256_CERT01:
    case SSH_KEYTYPE_ECDSA_P384:
    case SSH_KEYTYPE_ECDSA_P384_CERT01:
    case SSH_KEYTYPE_ECDSA_P521:
    case SSH_KEYTYPE_ECDSA_P521_CERT01:
    case SSH_KEYTYPE_SK_ECDSA:
    case SSH_KEYTYPE_SK_ECDSA_CERT01:
        pkey = pki_key_to_pkey(key);
        if (pkey == NULL) {
            return SSH_ERROR;
        }
        bits = EVP_PKEY_bits(pkey);
        EVP_PKEY_free(pkey);
        return bits;
    case SSH_KEYTYPE_ED25519:
    case SSH_KEYTYPE_ED25519_CERT01:
    case SSH_KEYTYPE_SK_ED25519:
    case SSH_KEYTYPE_SK_ED25519_CERT01:
        /* ed25519 keys have fixed size */
        return 255;
    case SSH_KEYTYPE_DSS:   /* deprecated */
    case SSH_KEYTYPE_DSS_CERT01:    /* deprecated */
    case SSH_KEYTYPE_UNKNOWN:
    default:
        return SSH_ERROR;
    }
}

int pki_key_generate_ed25519(ssh_key key)
{
    int evp_rc;
    EVP_PKEY_CTX *pctx = NULL;
    EVP_PKEY *pkey = NULL;

    if (key == NULL) {
        return SSH_ERROR;
    }

    pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (pctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to create ed25519 EVP_PKEY_CTX: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto error;
    }

    evp_rc = EVP_PKEY_keygen_init(pctx);
    if (evp_rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to initialize ed25519 key generation: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto error;
    }

    evp_rc = EVP_PKEY_keygen(pctx, &pkey);
    if (evp_rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to generate ed25519 key: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto error;
    }
    key->key = pkey;

    EVP_PKEY_CTX_free(pctx);
    return SSH_OK;

error:
    if (pctx != NULL) {
        EVP_PKEY_CTX_free(pctx);
    }
    if (pkey != NULL) {
        EVP_PKEY_free(pkey);
    }

    return SSH_ERROR;
}

#ifdef WITH_PKCS11_URI

/**
 * @internal
 *
 * @brief Populate the public/private ssh_key from the engine/provider with
 * PKCS#11 URIs as the look up.
 *
 * @param[in]   uri_name    The PKCS#11 URI
 * @param[in]   nkey        The ssh-key context for
 *                          the key loaded from the engine/provider.
 * @param[in]   key_type    The type of the key used. Public/Private.
 *
 * @return  SSH_OK if ssh-key is valid; SSH_ERROR otherwise.
 */
int pki_uri_import(const char *uri_name,
                     ssh_key *nkey,
                     enum ssh_key_e key_type)
{
    EVP_PKEY *pkey = NULL;
    ssh_key key = NULL;
    enum ssh_keytypes_e type = SSH_KEYTYPE_UNKNOWN;
#if OPENSSL_VERSION_NUMBER < 0x30000000L && HAVE_OPENSSL_ECC
    EC_KEY *ecdsa = NULL;
#endif
#ifndef WITH_PKCS11_PROVIDER
    ENGINE *engine = NULL;

    /* Do the init only once */
    engine = pki_get_engine();
    if (engine == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to initialize engine");
        goto fail;
    }

    switch (key_type) {
    case SSH_KEY_PRIVATE:
        pkey = ENGINE_load_private_key(engine, uri_name, NULL, NULL);
        if (pkey == NULL) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Could not load key: %s",
                    ERR_error_string(ERR_get_error(), NULL));
            goto fail;
        }
        break;
    case SSH_KEY_PUBLIC:
        pkey = ENGINE_load_public_key(engine, uri_name, NULL, NULL);
        if (pkey == NULL) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Could not load key: %s",
                    ERR_error_string(ERR_get_error(), NULL));
            goto fail;
        }
        break;
    default:
        SSH_LOG(SSH_LOG_TRACE,
                "Invalid key type: %d", key_type);
        goto fail;
    }
#else /* WITH_PKCS11_PROVIDER */
    OSSL_STORE_CTX *store = NULL;
    OSSL_STORE_INFO *info = NULL;
    int rv, expect_type = OSSL_STORE_INFO_PKEY;

    /* The provider can be either configured in openssl.cnf or dynamically
     * loaded, assuming it does not need any special configuration */
    rv = pki_load_pkcs11_provider();
    if (rv != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to load or initialize pkcs11 provider");
        goto fail;
    }

    store = OSSL_STORE_open(uri_name, NULL, NULL, NULL, NULL);
    if (store == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to open OpenSSL store: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto fail;
    }
    if (key_type == SSH_KEY_PUBLIC) {
        expect_type = OSSL_STORE_INFO_PUBKEY;
    }
    rv = OSSL_STORE_expect(store, expect_type);
    if (rv != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to set the store preference. Ignoring the error: %s",
                ERR_error_string(ERR_get_error(), NULL));
    }

    for (info = OSSL_STORE_load(store);
         info != NULL;
         info = OSSL_STORE_load(store)) {
        int ossl_type = OSSL_STORE_INFO_get_type(info);

        if (ossl_type == OSSL_STORE_INFO_PUBKEY && key_type == SSH_KEY_PUBLIC) {
            pkey = OSSL_STORE_INFO_get1_PUBKEY(info);
        } else if (ossl_type == OSSL_STORE_INFO_PKEY &&
                   key_type == SSH_KEY_PRIVATE) {
            pkey = OSSL_STORE_INFO_get1_PKEY(info);
        } else {
            SSH_LOG(SSH_LOG_TRACE,
                    "Ignoring object not matching our type: %d",
                    ossl_type);
            OSSL_STORE_INFO_free(info);
            continue;
        }
        OSSL_STORE_INFO_free(info);
        break;
    }
    OSSL_STORE_close(store);
    if (pkey == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "No key found in the pkcs11 store: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto fail;
    }

#endif /* WITH_PKCS11_PROVIDER */

    key = ssh_key_new();
    if (key == NULL) {
        goto fail;
    }

    switch (EVP_PKEY_base_id(pkey)) {
    case EVP_PKEY_RSA:
        type = SSH_KEYTYPE_RSA;
        break;
    case EVP_PKEY_EC:
#ifdef HAVE_OPENSSL_ECC
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        ecdsa = EVP_PKEY_get0_EC_KEY(pkey);
        if (ecdsa == NULL) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Parsing pub key: %s",
                    ERR_error_string(ERR_get_error(), NULL));
            goto fail;
        }

        /* pki_privatekey_type_from_string always returns P256 for ECDSA
         * keys, so we need to figure out the correct type here */
        type = pki_key_ecdsa_to_key_type(ecdsa);
#else
        type = pki_key_ecdsa_to_key_type(pkey);
#endif /* OPENSSL_VERSION_NUMBER */
        if (type == SSH_KEYTYPE_UNKNOWN) {
            SSH_LOG(SSH_LOG_TRACE, "Invalid pub key.");
            goto fail;
        }

        break;
#endif
    case EVP_PKEY_ED25519:
        type = SSH_KEYTYPE_ED25519;
        break;
    default:
        SSH_LOG(SSH_LOG_TRACE, "Unknown or invalid public key type %d",
                EVP_PKEY_base_id(pkey));
        goto fail;
    }

    key->key = pkey;
    key->type = type;
    key->type_c = ssh_key_type_to_char(type);
    key->flags = SSH_KEY_FLAG_PUBLIC | SSH_KEY_FLAG_PKCS11_URI;
    if (key_type == SSH_KEY_PRIVATE) {
        key->flags |= SSH_KEY_FLAG_PRIVATE;
    }
#ifdef HAVE_OPENSSL_ECC
    if (is_ecdsa_key_type(key->type)) {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        key->ecdsa_nid = pki_key_ecdsa_to_nid(ecdsa);
#else
        key->ecdsa_nid = pki_key_ecdsa_to_nid(key->key);
#endif /* OPENSSL_VERSION_NUMBER */
    }
#endif

    *nkey = key;

    return SSH_OK;

fail:
    EVP_PKEY_free(pkey);
    ssh_key_free(key);

    return SSH_ERROR;
}
#endif /* WITH_PKCS11_URI */

#endif /* _PKI_CRYPTO_H */
