/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2025 by Red Hat, Inc.
 *
 * Author: Pavol Žáčik <pzacik@redhat.com>
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 2.1 of the License.
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

#include "libssh/crypto.h"
#include "libssh/mlkem.h"
#include "libssh/session.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ml_kem.h>

const struct mlkem_type_info MLKEM768_INFO = {
    .pubkey_size = OSSL_ML_KEM_768_PUBLIC_KEY_BYTES,
    .ciphertext_size = OSSL_ML_KEM_768_CIPHERTEXT_BYTES,
    .name = LN_ML_KEM_768,
};

const struct mlkem_type_info MLKEM1024_INFO = {
    .pubkey_size = OSSL_ML_KEM_1024_PUBLIC_KEY_BYTES,
    .ciphertext_size = OSSL_ML_KEM_1024_CIPHERTEXT_BYTES,
    .name = LN_ML_KEM_1024,
};

static const char *ssh_mlkem_get_propq(const struct mlkem_type_info *mlkem_info)
{
    static const char *propq = NULL;
    static bool is_cached = false;
    EVP_KEM *kem = NULL;

    if (is_cached) {
        return propq;
    }
    is_cached = true;

    if (!ssh_fips_mode()) {
        return propq;
    }

    kem = EVP_KEM_fetch(NULL, mlkem_info->name, NULL);
    if (kem != NULL) {
        EVP_KEM_free(kem);
        return propq;
    }

    propq = FIPS_FALLBACK_PROPQ;
    return propq;
}

int ssh_mlkem_init(ssh_session session)
{
    struct ssh_crypto_struct *crypto = session->next_crypto;
    EVP_PKEY_CTX *ctx = NULL;
    EVP_PKEY *pkey = NULL;
    int rc, ret = SSH_ERROR;
    const struct mlkem_type_info *mlkem_info = NULL;
    ssh_string pubkey = NULL;
    size_t pubkey_size;
    const char *propq = NULL;

    mlkem_info = kex_type_to_mlkem_info(crypto->kex_type);
    if (mlkem_info == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Unknown ML-KEM type");
        goto cleanup;
    }

    propq = ssh_mlkem_get_propq(mlkem_info);

    ctx = EVP_PKEY_CTX_new_from_name(NULL, mlkem_info->name, propq);
    if (ctx == NULL) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to create ML-KEM context: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto cleanup;
    }

    rc = EVP_PKEY_keygen_init(ctx);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to initialize ML-KEM keygen: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto cleanup;
    }

    rc = EVP_PKEY_keygen(ctx, &pkey);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to perform ML-KEM keygen: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto cleanup;
    }

    EVP_PKEY_free(crypto->mlkem_privkey);
    crypto->mlkem_privkey = pkey;

    pubkey_size = mlkem_info->pubkey_size;
    pubkey = ssh_string_new(pubkey_size);
    if (pubkey == NULL) {
        ssh_set_error_oom(session);
        goto cleanup;
    }

    rc = EVP_PKEY_get_raw_public_key(pkey,
                                     ssh_string_data(pubkey),
                                     &pubkey_size);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to extract ML-KEM public key: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto cleanup;
    }

    ssh_string_free(crypto->mlkem_client_pubkey);
    crypto->mlkem_client_pubkey = pubkey;
    pubkey = NULL;

    ret = SSH_OK;

cleanup:
    ssh_string_free(pubkey);
    EVP_PKEY_CTX_free(ctx);
    return ret;
}

int ssh_mlkem_encapsulate(ssh_session session,
                          ssh_mlkem_shared_secret shared_secret)
{
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    int rc, ret = SSH_ERROR;
    const struct mlkem_type_info *mlkem_info = NULL;
    struct ssh_crypto_struct *crypto = session->next_crypto;
    const unsigned char *pubkey = ssh_string_data(crypto->mlkem_client_pubkey);
    const size_t pubkey_len = ssh_string_len(crypto->mlkem_client_pubkey);
    size_t shared_secret_size = MLKEM_SHARED_SECRET_SIZE;
    ssh_string ciphertext = NULL;
    size_t ciphertext_size;
    const char *propq = NULL;

    mlkem_info = kex_type_to_mlkem_info(crypto->kex_type);
    if (mlkem_info == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Unknown ML-KEM type");
        goto cleanup;
    }

    propq = ssh_mlkem_get_propq(mlkem_info);

    pkey = EVP_PKEY_new_raw_public_key_ex(NULL,
                                          mlkem_info->name,
                                          propq,
                                          pubkey,
                                          pubkey_len);
    if (pkey == NULL) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to create ML-KEM public key from raw data: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto cleanup;
    }

    ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pkey, propq);
    if (ctx == NULL) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to create ML-KEM context: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto cleanup;
    }

    rc = EVP_PKEY_encapsulate_init(ctx, NULL);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to initialize ML-KEM encapsulation: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto cleanup;
    }

    ciphertext_size = mlkem_info->ciphertext_size;
    ciphertext = ssh_string_new(ciphertext_size);
    if (ciphertext == NULL) {
        ssh_set_error_oom(session);
        goto cleanup;
    }

    rc = EVP_PKEY_encapsulate(ctx,
                              ssh_string_data(ciphertext),
                              &ciphertext_size,
                              shared_secret,
                              &shared_secret_size);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to perform ML-KEM encapsulation: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto cleanup;
    }

    ssh_string_free(crypto->mlkem_ciphertext);
    crypto->mlkem_ciphertext = ciphertext;
    ciphertext = NULL;

    ret = SSH_OK;

cleanup:
    ssh_string_free(ciphertext);
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return ret;
}

int ssh_mlkem_decapsulate(const ssh_session session,
                          ssh_mlkem_shared_secret shared_secret)
{
    EVP_PKEY_CTX *ctx = NULL;
    int rc, ret = SSH_ERROR;
    size_t shared_secret_size = MLKEM_SHARED_SECRET_SIZE;
    struct ssh_crypto_struct *crypto = session->next_crypto;
    const struct mlkem_type_info *mlkem_info = NULL;
    const char *propq = NULL;

    mlkem_info = kex_type_to_mlkem_info(crypto->kex_type);
    if (mlkem_info == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Unknown ML-KEM type");
        goto cleanup;
    }

    propq = ssh_mlkem_get_propq(mlkem_info);

    ctx = EVP_PKEY_CTX_new_from_pkey(NULL, crypto->mlkem_privkey, propq);
    if (ctx == NULL) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to create ML-KEM context: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto cleanup;
    }

    rc = EVP_PKEY_decapsulate_init(ctx, NULL);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to initialize ML-KEM decapsulation: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto cleanup;
    }

    rc = EVP_PKEY_decapsulate(ctx,
                              shared_secret,
                              &shared_secret_size,
                              ssh_string_data(crypto->mlkem_ciphertext),
                              ssh_string_len(crypto->mlkem_ciphertext));
    if (rc != 1) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to perform ML-KEM decapsulation: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto cleanup;
    }

    ret = SSH_OK;

cleanup:
    EVP_PKEY_CTX_free(ctx);
    return ret;
}
