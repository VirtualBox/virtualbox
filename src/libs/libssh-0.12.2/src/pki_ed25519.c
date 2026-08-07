/*
 * pki_ed25519.c - PKI infrastructure using ed25519
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2014 by Aris Adamantiadis
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

#include "libssh/pki.h"
#include "libssh/pki_priv.h"
#include "libssh/ed25519.h"
#include "libssh/buffer.h"

int pki_pubkey_build_ed25519(ssh_key key, ssh_string pubkey)
{
    if (ssh_string_len(pubkey) != ED25519_KEY_LEN) {
        SSH_LOG(SSH_LOG_TRACE, "Invalid ed25519 key len");
        return SSH_ERROR;
    }

    key->ed25519_pubkey = malloc(ED25519_KEY_LEN);
    if (key->ed25519_pubkey == NULL) {
        return SSH_ERROR;
    }

    memcpy(key->ed25519_pubkey, ssh_string_data(pubkey), ED25519_KEY_LEN);

    return SSH_OK;
}

int pki_privkey_build_ed25519(ssh_key key,
                              ssh_string pubkey,
                              ssh_string privkey)
{
    if (ssh_string_len(pubkey) != ED25519_KEY_LEN ||
        ssh_string_len(privkey) != (2 * ED25519_KEY_LEN)) {
        SSH_LOG(SSH_LOG_TRACE, "Invalid ed25519 key len");
        return SSH_ERROR;
    }

    /* In the internal implementation, the private key is the concatenation of
     * the private seed with the public key. */
    key->ed25519_privkey = malloc(2 * ED25519_KEY_LEN);
    if (key->ed25519_privkey == NULL) {
        goto error;
    }

    key->ed25519_pubkey = malloc(ED25519_KEY_LEN);
    if (key->ed25519_pubkey == NULL) {
        goto error;
    }

    memcpy(key->ed25519_privkey, ssh_string_data(privkey), 2 * ED25519_KEY_LEN);
    memcpy(key->ed25519_pubkey, ssh_string_data(pubkey), ED25519_KEY_LEN);

    return SSH_OK;

error:
    SAFE_FREE(key->ed25519_privkey);
    SAFE_FREE(key->ed25519_pubkey);

    return SSH_ERROR;
}

/**
 * @internal
 *
 * @brief Compare ed25519 keys if they are equal.
 *
 * @param[in] k1        The first key to compare.
 *
 * @param[in] k2        The second key to compare.
 *
 * @param[in] what      What part or type of the key do you want to compare.
 *
 * @return              0 if equal, 1 if not.
 */
int
pki_ed25519_key_cmp(const ssh_key k1, const ssh_key k2, enum ssh_keycmp_e what)
{
    int cmp;

    switch (what) {
    case SSH_KEY_CMP_PRIVATE:
        if (k1->ed25519_privkey == NULL || k2->ed25519_privkey == NULL) {
            return 1;
        }
        /* In the internal implementation, the private key is the concatenation
         * of the private seed with the public key. */
        cmp = secure_memcmp(k1->ed25519_privkey,
                            k2->ed25519_privkey,
                            2 * ED25519_KEY_LEN);
        if (cmp != 0) {
            return 1;
        }
        FALL_THROUGH;
    case SSH_KEY_CMP_PUBLIC:
        if (k1->ed25519_pubkey == NULL || k2->ed25519_pubkey == NULL) {
            return 1;
        }
        cmp = memcmp(k1->ed25519_pubkey, k2->ed25519_pubkey, ED25519_KEY_LEN);
        if (cmp != 0) {
            return 1;
        }
        break;
    case SSH_KEY_CMP_CERTIFICATE:
        /* handled globally */
        return 1;
    }

    return 0;
}

/**
 * @internal
 *
 * @brief Duplicate an Ed25519 key
 *
 * @param[out] new Pre-initialized ssh_key structure
 *
 * @param[in] key Key to copy
 *
 * @return SSH_ERROR on error, SSH_OK on success
 */
int pki_ed25519_key_dup(ssh_key new_key, const ssh_key key)
{
    if (key->ed25519_privkey == NULL && key->ed25519_pubkey == NULL) {
        return SSH_ERROR;
    }

    if (key->ed25519_privkey != NULL) {
        /* In the internal implementation, the private key is the concatenation
         * of the private seed with the public key. */
        new_key->ed25519_privkey = malloc(2 * ED25519_KEY_LEN);
        if (new_key->ed25519_privkey == NULL) {
            return SSH_ERROR;
        }
        memcpy(new_key->ed25519_privkey,
               key->ed25519_privkey,
               2 * ED25519_KEY_LEN);
    }

    if (key->ed25519_pubkey != NULL) {
        new_key->ed25519_pubkey = malloc(ED25519_KEY_LEN);
        if (new_key->ed25519_pubkey == NULL) {
            SAFE_FREE(new_key->ed25519_privkey);
            return SSH_ERROR;
        }
        memcpy(new_key->ed25519_pubkey, key->ed25519_pubkey, ED25519_KEY_LEN);
    }

    return SSH_OK;
}

/**
 * @internal
 *
 * @brief Outputs an Ed25519 public key in a blob buffer.
 *
 * @param[out] buffer Output buffer
 *
 * @param[in] key Key to output
 *
 * @return SSH_ERROR on error, SSH_OK on success
 */
int pki_ed25519_public_key_to_blob(ssh_buffer buffer, ssh_key key)
{
    int rc;

    if (key->ed25519_pubkey == NULL) {
        return SSH_ERROR;
    }

    rc = ssh_buffer_pack(buffer,
                         "dP",
                         (uint32_t)ED25519_KEY_LEN,
                         (size_t)ED25519_KEY_LEN,
                         key->ed25519_pubkey);

    return rc;
}

/** @internal
 * @brief exports a ed25519 private key to a string blob.
 * @param[in] privkey private key to convert
 * @param[out] buffer buffer to write the blob in.
 * @returns SSH_OK on success
 */
int pki_ed25519_private_key_to_blob(ssh_buffer buffer, const ssh_key privkey)
{
    int rc;

    if (privkey->type != SSH_KEYTYPE_ED25519) {
        SSH_LOG(SSH_LOG_TRACE, "Type %s not supported", privkey->type_c);
        return SSH_ERROR;
    }
    if (privkey->ed25519_privkey == NULL || privkey->ed25519_pubkey == NULL) {
        return SSH_ERROR;
    }
    rc = ssh_buffer_pack(buffer,
                         "dPdPP",
                         (uint32_t)ED25519_KEY_LEN,
                         (size_t)ED25519_KEY_LEN,
                         privkey->ed25519_pubkey,
                         (uint32_t)(2 * ED25519_KEY_LEN),
                         (size_t)ED25519_KEY_LEN,
                         privkey->ed25519_privkey,
                         (size_t)ED25519_KEY_LEN,
                         privkey->ed25519_pubkey);
    return rc;
}

int pki_key_generate_ed25519(ssh_key key)
{
    int rc;

    key->ed25519_privkey = malloc(sizeof (ed25519_privkey));
    if (key->ed25519_privkey == NULL) {
        goto error;
    }

    key->ed25519_pubkey = malloc(sizeof (ed25519_pubkey));
    if (key->ed25519_pubkey == NULL) {
        goto error;
    }

    rc = crypto_sign_ed25519_keypair(*key->ed25519_pubkey,
                                     *key->ed25519_privkey);
    if (rc != 0) {
        goto error;
    }

    return SSH_OK;
error:
    SAFE_FREE(key->ed25519_privkey);
    SAFE_FREE(key->ed25519_pubkey);

    return SSH_ERROR;
}

int pki_ed25519_sign(const ssh_key privkey,
                     ssh_signature sig,
                     const unsigned char *hash,
                     size_t hlen)
{
    int rc;
    uint8_t *buffer = NULL;
    uint64_t dlen = 0;

    buffer = malloc(hlen + ED25519_SIG_LEN);
    if (buffer == NULL) {
        return SSH_ERROR;
    }

    rc = crypto_sign_ed25519(buffer,
                             &dlen,
                             hash,
                             hlen,
                             *privkey->ed25519_privkey);
    if (rc != 0) {
        goto error;
    }

    /* This shouldn't happen */
    if (dlen - hlen != ED25519_SIG_LEN) {
        goto error;
    }

    sig->ed25519_sig = malloc(ED25519_SIG_LEN);
    if (sig->ed25519_sig == NULL) {
        goto error;
    }

    memcpy(sig->ed25519_sig, buffer, ED25519_SIG_LEN);
    SAFE_FREE(buffer);

    return SSH_OK;
error:
    SAFE_FREE(buffer);
    return SSH_ERROR;
}

int pki_ed25519_verify(const ssh_key pubkey,
                       ssh_signature sig,
                       const unsigned char *hash,
                       size_t hlen)
{
    uint64_t mlen = 0;
    uint8_t *buffer = NULL;
    uint8_t *buffer2 = NULL;
    int rc;

    if (pubkey == NULL || sig == NULL ||
        hash == NULL || sig->ed25519_sig == NULL) {
        return SSH_ERROR;
    }

    buffer = malloc(hlen + ED25519_SIG_LEN);
    if (buffer == NULL) {
        return SSH_ERROR;
    }

    buffer2 = malloc(hlen + ED25519_SIG_LEN);
    if (buffer2 == NULL) {
        goto error;
    }

    memcpy(buffer, sig->ed25519_sig, ED25519_SIG_LEN);
    memcpy(buffer + ED25519_SIG_LEN, hash, hlen);

    rc = crypto_sign_ed25519_open(buffer2,
                                  &mlen,
                                  buffer,
                                  hlen + ED25519_SIG_LEN,
                                  *pubkey->ed25519_pubkey);

    ssh_burn(buffer, hlen + ED25519_SIG_LEN);
    ssh_burn(buffer2, hlen);
    SAFE_FREE(buffer);
    SAFE_FREE(buffer2);
    if (rc == 0) {
        return SSH_OK;
    } else {
        return SSH_ERROR;
    }
error:
    SAFE_FREE(buffer);
    SAFE_FREE(buffer2);

    return SSH_ERROR;
}

