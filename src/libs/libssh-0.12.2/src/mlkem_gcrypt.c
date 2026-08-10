/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2025 by Red Hat, Inc.
 *
 * Author: Jakub Jelen <jjelen@redhat.com>
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

#include <gcrypt.h>

const struct mlkem_type_info MLKEM768_INFO = {
    .pubkey_size = GCRY_KEM_MLKEM768_PUBKEY_LEN,
    .privkey_size = GCRY_KEM_MLKEM768_SECKEY_LEN,
    .ciphertext_size = GCRY_KEM_MLKEM768_CIPHER_LEN,
    .alg = GCRY_KEM_MLKEM768,
};

const struct mlkem_type_info MLKEM1024_INFO = {
    .pubkey_size = GCRY_KEM_MLKEM1024_PUBKEY_LEN,
    .privkey_size = GCRY_KEM_MLKEM1024_SECKEY_LEN,
    .ciphertext_size = GCRY_KEM_MLKEM1024_CIPHER_LEN,
    .alg = GCRY_KEM_MLKEM1024,
};

int ssh_mlkem_init(ssh_session session)
{
    int ret = SSH_ERROR;
    struct ssh_crypto_struct *crypto = session->next_crypto;
    const struct mlkem_type_info *mlkem_info = NULL;
    ssh_string pubkey = NULL;
    unsigned char *privkey = NULL, *pubkey_data = NULL;
    gcry_error_t err;

    mlkem_info = kex_type_to_mlkem_info(crypto->kex_type);
    if (mlkem_info == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Unknown ML-KEM type");
        goto cleanup;
    }

    privkey = malloc(mlkem_info->privkey_size);
    if (privkey == NULL) {
        ssh_set_error_oom(session);
        goto cleanup;
    }

    pubkey = ssh_string_new(mlkem_info->pubkey_size);
    if (pubkey == NULL) {
        ssh_set_error_oom(session);
        goto cleanup;
    }

    pubkey_data = ssh_string_data(pubkey);
    err = gcry_kem_keypair(mlkem_info->alg,
                           pubkey_data,
                           mlkem_info->pubkey_size,
                           privkey,
                           mlkem_info->privkey_size);
    if (err) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to generate ML-KEM key: %s",
                gpg_strerror(err));
        goto cleanup;
    }

    ssh_string_free(crypto->mlkem_client_pubkey);
    crypto->mlkem_client_pubkey = pubkey;
    pubkey = NULL;

    free(crypto->mlkem_privkey);
    crypto->mlkem_privkey = privkey;
    crypto->mlkem_privkey_len = mlkem_info->privkey_size;
    privkey = NULL;

    ret = SSH_OK;

cleanup:
    ssh_string_free(pubkey);
    if (privkey != NULL) {
        ssh_burn(privkey, mlkem_info->privkey_size);
        free(privkey);
    }
    return ret;
}

int ssh_mlkem_encapsulate(ssh_session session,
                          ssh_mlkem_shared_secret shared_secret)
{
    int ret = SSH_ERROR;
    const struct mlkem_type_info *mlkem_info = NULL;
    struct ssh_crypto_struct *crypto = session->next_crypto;
    const unsigned char *pubkey_data = NULL;
    unsigned char *ciphertext_data = NULL;
    ssh_string ciphertext = NULL;
    ssh_string pubkey = crypto->mlkem_client_pubkey;
    gcry_error_t err;

    if (pubkey == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Missing pubkey in session");
        return SSH_ERROR;
    }

    mlkem_info = kex_type_to_mlkem_info(crypto->kex_type);
    if (mlkem_info == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Unknown ML-KEM type");
        return SSH_ERROR;
    }

    ciphertext = ssh_string_new(mlkem_info->ciphertext_size);
    if (ciphertext == NULL) {
        ssh_set_error_oom(session);
        return SSH_ERROR;
    }

    pubkey_data = ssh_string_data(pubkey);
    ciphertext_data = ssh_string_data(ciphertext);
    err = gcry_kem_encap(mlkem_info->alg,
                         pubkey_data,
                         mlkem_info->pubkey_size,
                         ciphertext_data,
                         mlkem_info->ciphertext_size,
                         shared_secret,
                         MLKEM_SHARED_SECRET_SIZE,
                         NULL,
                         0);
    if (err) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to encapsulate ML-KEM shared secret: %s",
                gpg_strerror(err));
        goto cleanup;
    }

    ssh_string_free(crypto->mlkem_ciphertext);
    crypto->mlkem_ciphertext = ciphertext;
    ciphertext = NULL;

    ret = SSH_OK;

cleanup:
    ssh_string_free(ciphertext);
    return ret;
}

int ssh_mlkem_decapsulate(const ssh_session session,
                          ssh_mlkem_shared_secret shared_secret)
{
    const struct mlkem_type_info *mlkem_info = NULL;
    struct ssh_crypto_struct *crypto = session->next_crypto;
    ssh_string ciphertext = NULL;
    unsigned char *ciphertext_data = NULL;
    gcry_error_t err;

    ciphertext = crypto->mlkem_ciphertext;
    if (ciphertext == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Missing ciphertext in session");
        return SSH_ERROR;
    }

    if (crypto->mlkem_privkey == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Missing ML-KEM private key in session");
        return SSH_ERROR;
    }

    mlkem_info = kex_type_to_mlkem_info(crypto->kex_type);
    if (mlkem_info == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Unknown ML-KEM type");
        return SSH_ERROR;
    }

    ciphertext_data = ssh_string_data(ciphertext);
    err = gcry_kem_decap(mlkem_info->alg,
                         crypto->mlkem_privkey,
                         mlkem_info->privkey_size,
                         ciphertext_data,
                         mlkem_info->ciphertext_size,
                         shared_secret,
                         MLKEM_SHARED_SECRET_SIZE,
                         NULL,
                         0);
    if (err) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to decapsulate ML-KEM shared secret: %s",
                gpg_strerror(err));
        return SSH_ERROR;
    }

    return SSH_OK;
}
