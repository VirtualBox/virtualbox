/*
 * curve25519_crypto.c - Curve25519 ECDH functions for key exchange (OpenSSL)
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2013-2023 by Aris Adamantiadis <aris@badcode.be>
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
#include "libssh/curve25519.h"

#include "libssh/crypto.h"
#include "libssh/priv.h"
#include "libssh/session.h"

#include <openssl/err.h>
#include <openssl/evp.h>

int ssh_curve25519_init(ssh_session session)
{
    ssh_curve25519_pubkey *pubkey_loc = NULL;
    EVP_PKEY_CTX *pctx = NULL;
    EVP_PKEY *pkey = NULL;
    size_t pubkey_len = CURVE25519_PUBKEY_SIZE;
    int rc;

    if (session->server) {
        pubkey_loc = &session->next_crypto->curve25519_server_pubkey;
    } else {
        pubkey_loc = &session->next_crypto->curve25519_client_pubkey;
    }

    pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (pctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to initialize X25519 context: %s",
                ERR_error_string(ERR_get_error(), NULL));
        return SSH_ERROR;
    }

    rc = EVP_PKEY_keygen_init(pctx);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to initialize X25519 keygen: %s",
                ERR_error_string(ERR_get_error(), NULL));
        EVP_PKEY_CTX_free(pctx);
        return SSH_ERROR;
    }

    rc = EVP_PKEY_keygen(pctx, &pkey);
    EVP_PKEY_CTX_free(pctx);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to generate X25519 keys: %s",
                ERR_error_string(ERR_get_error(), NULL));
        return SSH_ERROR;
    }

    rc = EVP_PKEY_get_raw_public_key(pkey, *pubkey_loc, &pubkey_len);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to get X25519 raw public key: %s",
                ERR_error_string(ERR_get_error(), NULL));
        EVP_PKEY_free(pkey);
        return SSH_ERROR;
    }

    /* Free any previously allocated privkey */
    if (session->next_crypto->curve25519_privkey != NULL) {
        EVP_PKEY_free(session->next_crypto->curve25519_privkey);
        session->next_crypto->curve25519_privkey = NULL;
    }

    session->next_crypto->curve25519_privkey = pkey;
    pkey = NULL;

    return SSH_OK;
}

int curve25519_do_create_k(ssh_session session, ssh_curve25519_pubkey k)
{
    ssh_curve25519_pubkey *peer_pubkey_loc = NULL;
    int rc, ret = SSH_ERROR;
    EVP_PKEY_CTX *pctx = NULL;
    EVP_PKEY *pkey = NULL, *pubkey = NULL;
    size_t shared_key_len = CURVE25519_PUBKEY_SIZE;

    if (session->server) {
        peer_pubkey_loc = &session->next_crypto->curve25519_client_pubkey;
    } else {
        peer_pubkey_loc = &session->next_crypto->curve25519_server_pubkey;
    }

    pkey = session->next_crypto->curve25519_privkey;
    if (pkey == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to create X25519 EVP_PKEY: %s",
                ERR_error_string(ERR_get_error(), NULL));
        return SSH_ERROR;
    }

    pctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (pctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to initialize X25519 context: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    rc = EVP_PKEY_derive_init(pctx);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to initialize X25519 key derivation: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    pubkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519,
                                         NULL,
                                         *peer_pubkey_loc,
                                         CURVE25519_PUBKEY_SIZE);
    if (pubkey == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to create X25519 public key EVP_PKEY: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    rc = EVP_PKEY_derive_set_peer(pctx, pubkey);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to set peer X25519 public key: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    rc = EVP_PKEY_derive(pctx, k, &shared_key_len);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to derive X25519 shared secret: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }
    ret = SSH_OK;

out:
    EVP_PKEY_free(pubkey);
    EVP_PKEY_CTX_free(pctx);
    return ret;
}
