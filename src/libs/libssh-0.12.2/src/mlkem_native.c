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
#include "libssh/mlkem_native.h"
#include "libssh/session.h"

#define crypto_kem_mlkem768_PUBLICKEYBYTES  1184
#define crypto_kem_mlkem768_SECRETKEYBYTES  2400
#define crypto_kem_mlkem768_CIPHERTEXTBYTES 1088

const struct mlkem_type_info MLKEM768_INFO = {
    .pubkey_size = crypto_kem_mlkem768_PUBLICKEYBYTES,
    .privkey_size = crypto_kem_mlkem768_SECRETKEYBYTES,
    .ciphertext_size = crypto_kem_mlkem768_CIPHERTEXTBYTES,
};

int ssh_mlkem_init(ssh_session session)
{
    int ret = SSH_ERROR;
    struct ssh_crypto_struct *crypto = session->next_crypto;
    const struct mlkem_type_info *mlkem_info = NULL;
    unsigned char rnd[LIBCRUX_ML_KEM_KEY_PAIR_PRNG_LEN];
    struct libcrux_mlkem768_keypair keypair;
    int err;

    mlkem_info = kex_type_to_mlkem_info(crypto->kex_type);
    if (mlkem_info == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Unknown ML-KEM type");
        goto cleanup;
    }

    err = ssh_get_random(rnd, sizeof(rnd), 0);
    if (err != 1) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to generate random data for ML-KEM keygen");
        goto cleanup;
    }

    keypair = libcrux_ml_kem_mlkem768_portable_generate_key_pair(rnd);

    if (ssh_string_len(crypto->mlkem_client_pubkey) < mlkem_info->pubkey_size) {
        SSH_STRING_FREE(crypto->mlkem_client_pubkey);
    }
    if (crypto->mlkem_client_pubkey == NULL) {
        crypto->mlkem_client_pubkey = ssh_string_new(mlkem_info->pubkey_size);
        if (crypto->mlkem_client_pubkey == NULL) {
            ssh_set_error_oom(session);
            goto cleanup;
        }
    }
    err = ssh_string_fill(crypto->mlkem_client_pubkey,
                          keypair.pk.value,
                          mlkem_info->pubkey_size);
    if (err) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to fill the string with client pubkey");
        goto cleanup;
    }

    if (crypto->mlkem_privkey == NULL) {
        crypto->mlkem_privkey = malloc(mlkem_info->privkey_size);
        if (crypto->mlkem_privkey == NULL) {
            ssh_set_error_oom(session);
            goto cleanup;
        }
    }
    memcpy(crypto->mlkem_privkey, keypair.sk.value, mlkem_info->privkey_size);
    crypto->mlkem_privkey_len = mlkem_info->privkey_size;

    ret = SSH_OK;

cleanup:
    ssh_burn(&keypair, sizeof(keypair));
    ssh_burn(rnd, sizeof(rnd));
    return ret;
}

int ssh_mlkem_encapsulate(ssh_session session,
                          ssh_mlkem_shared_secret shared_secret)
{
    int ret = SSH_ERROR;
    const struct mlkem_type_info *mlkem_info = NULL;
    struct ssh_crypto_struct *crypto = session->next_crypto;
    const unsigned char *pubkey_data = NULL;
    ssh_string pubkey = crypto->mlkem_client_pubkey;
    struct libcrux_mlkem768_enc_result enc;
    struct libcrux_mlkem768_pk mlkem_pub = {0};
    unsigned char rnd[LIBCRUX_ML_KEM_ENC_PRNG_LEN];
    int err;

    if (pubkey == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Missing pubkey in session");
        return SSH_ERROR;
    }

    mlkem_info = kex_type_to_mlkem_info(crypto->kex_type);
    if (mlkem_info == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Unknown ML-KEM type");
        return SSH_ERROR;
    }

    pubkey_data = ssh_string_data(pubkey);
    memcpy(mlkem_pub.value, pubkey_data, mlkem_info->pubkey_size);
    err = libcrux_ml_kem_mlkem768_portable_validate_public_key(&mlkem_pub);
    if (err == 0) {
        SSH_LOG(SSH_LOG_WARNING, "Invalid public key");
        return SSH_ERROR;
    }

    err = ssh_get_random(rnd, sizeof(rnd), 0);
    if (err != 1) {
        SSH_LOG(SSH_LOG_WARNING,
                "Failed to generate random data for ML-KEM keygen");
        goto cleanup;
    }

    enc = libcrux_ml_kem_mlkem768_portable_encapsulate(&mlkem_pub, rnd);

    if (ssh_string_len(crypto->mlkem_ciphertext) < mlkem_info->ciphertext_size) {
        SSH_STRING_FREE(crypto->mlkem_ciphertext);
    }
    if (crypto->mlkem_ciphertext == NULL) {
        crypto->mlkem_ciphertext = ssh_string_new(mlkem_info->ciphertext_size);
        if (crypto->mlkem_ciphertext == NULL) {
            ssh_set_error_oom(session);
            goto cleanup;
        }
    }
    err = ssh_string_fill(crypto->mlkem_ciphertext,
                          enc.fst.value,
                          sizeof(enc.fst.value));
    if (err != SSH_OK) {
        SSH_LOG(SSH_LOG_WARNING, "Failed to fill the string with ciphertext");
        goto cleanup;
    }
    memcpy(shared_secret, enc.snd, sizeof(enc.snd));

    ret = SSH_OK;

cleanup:
    ssh_burn(rnd, sizeof(rnd));
    ssh_burn(&enc, sizeof(enc));
    return ret;
}

int ssh_mlkem_decapsulate(const ssh_session session,
                          ssh_mlkem_shared_secret shared_secret)
{
    const struct mlkem_type_info *mlkem_info = NULL;
    struct ssh_crypto_struct *crypto = session->next_crypto;
    ssh_string ciphertext = NULL;
    unsigned char *ciphertext_data = NULL;
    struct libcrux_mlkem768_sk mlkem_priv = {0};
    struct libcrux_mlkem768_ciphertext mlkem_ciphertext = {0};

    mlkem_info = kex_type_to_mlkem_info(crypto->kex_type);
    if (mlkem_info == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Unknown ML-KEM type");
        return SSH_ERROR;
    }

    ciphertext = crypto->mlkem_ciphertext;
    if (ciphertext == NULL) {
        SSH_LOG(SSH_LOG_WARNING, "Missing ciphertext in session");
        return SSH_ERROR;
    }

    ciphertext_data = ssh_string_data(ciphertext);
    memcpy(mlkem_ciphertext.value,
           ciphertext_data,
           sizeof(mlkem_ciphertext.value));

    memcpy(mlkem_priv.value, crypto->mlkem_privkey, crypto->mlkem_privkey_len);

    libcrux_ml_kem_mlkem768_portable_decapsulate(&mlkem_priv,
                                                 &mlkem_ciphertext,
                                                 shared_secret);
    return SSH_OK;
}
