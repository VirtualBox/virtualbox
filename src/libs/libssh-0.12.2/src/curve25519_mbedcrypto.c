/*
 * curve25519_mbedcrypto.c - Curve25519 ECDH functions for key exchange
 * (MbedTLS)
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2013-2023 by Aris Adamantiadis <aris@badcode.be>
 * Copyright (c) 2025 Praneeth Sarode <praneethsarode@gmail.com>
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
#include "mbedcrypto-compat.h"

#include <mbedtls/ecdh.h>
#include <mbedtls/error.h>

int ssh_curve25519_init(ssh_session session)
{
    ssh_curve25519_pubkey *pubkey_loc = NULL;
    mbedtls_ecdh_context ecdh_ctx;
    mbedtls_ecdh_params *ecdh_params = NULL;
    mbedtls_ctr_drbg_context *ctr_drbg = NULL;
    int rc, ret = SSH_ERROR;
    char error_buf[128];

    if (session->server) {
        pubkey_loc = &session->next_crypto->curve25519_server_pubkey;
    } else {
        pubkey_loc = &session->next_crypto->curve25519_client_pubkey;
    }

    ctr_drbg = ssh_get_mbedtls_ctr_drbg_context();

    mbedtls_ecdh_init(&ecdh_ctx);
    rc = mbedtls_ecdh_setup(&ecdh_ctx, MBEDTLS_ECP_DP_CURVE25519);
    if (rc != 0) {
        mbedtls_strerror(rc, error_buf, sizeof(error_buf));
        SSH_LOG(SSH_LOG_TRACE, "Failed to setup X25519 context: %s", error_buf);
        goto out;
    }

    ecdh_params = &MBEDTLS_ECDH_PARAMS(ecdh_ctx);

    rc = mbedtls_ecdh_gen_public(&ecdh_params->MBEDTLS_ECDH_PRIVATE(grp),
                                 &ecdh_params->MBEDTLS_ECDH_PRIVATE(d),
                                 &ecdh_params->MBEDTLS_ECDH_PRIVATE(Q),
                                 mbedtls_ctr_drbg_random,
                                 ctr_drbg);
    if (rc != 0) {
        mbedtls_strerror(rc, error_buf, sizeof(error_buf));
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to generate X25519 keypair: %s",
                error_buf);
        goto out;
    }

    rc = mbedtls_mpi_write_binary_le(&ecdh_params->MBEDTLS_ECDH_PRIVATE(d),
                                     session->next_crypto->curve25519_privkey,
                                     CURVE25519_PRIVKEY_SIZE);
    if (rc != 0) {
        mbedtls_strerror(rc, error_buf, sizeof(error_buf));
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to write X25519 private key: %s",
                error_buf);
        goto out;
    }

    rc = mbedtls_mpi_write_binary_le(
        &ecdh_params->MBEDTLS_ECDH_PRIVATE(Q).MBEDTLS_ECDH_PRIVATE(X),
        *pubkey_loc,
        CURVE25519_PUBKEY_SIZE);
    if (rc != 0) {
        mbedtls_strerror(rc, error_buf, sizeof(error_buf));
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to write X25519 public key: %s",
                error_buf);
        goto out;
    }

    ret = SSH_OK;

out:
    mbedtls_ecdh_free(&ecdh_ctx);
    return ret;
}

int curve25519_do_create_k(ssh_session session, ssh_curve25519_pubkey k)
{
    ssh_curve25519_pubkey *peer_pubkey_loc = NULL;
    int rc, ret = SSH_ERROR;
    mbedtls_ecdh_context ecdh_ctx;
    mbedtls_ecdh_params *ecdh_params = NULL;
    mbedtls_ctr_drbg_context *ctr_drbg = NULL;
    char error_buf[128];

    if (session->server) {
        peer_pubkey_loc = &session->next_crypto->curve25519_client_pubkey;
    } else {
        peer_pubkey_loc = &session->next_crypto->curve25519_server_pubkey;
    }

    ctr_drbg = ssh_get_mbedtls_ctr_drbg_context();

    mbedtls_ecdh_init(&ecdh_ctx);
    rc = mbedtls_ecdh_setup(&ecdh_ctx, MBEDTLS_ECP_DP_CURVE25519);
    if (rc != 0) {
        mbedtls_strerror(rc, error_buf, sizeof(error_buf));
        SSH_LOG(SSH_LOG_TRACE, "Failed to setup X25519 context: %s", error_buf);
        goto out;
    }

    ecdh_params = &MBEDTLS_ECDH_PARAMS(ecdh_ctx);

    rc = mbedtls_mpi_read_binary_le(&ecdh_params->MBEDTLS_ECDH_PRIVATE(d),
                                    session->next_crypto->curve25519_privkey,
                                    CURVE25519_PRIVKEY_SIZE);
    if (rc != 0) {
        mbedtls_strerror(rc, error_buf, sizeof(error_buf));
        SSH_LOG(SSH_LOG_TRACE, "Failed to read private key: %s", error_buf);
        goto out;
    }

    rc = mbedtls_mpi_read_binary_le(
        &ecdh_params->MBEDTLS_ECDH_PRIVATE(Qp).MBEDTLS_ECDH_PRIVATE(X),
        *peer_pubkey_loc,
        CURVE25519_PUBKEY_SIZE);
    if (rc != 0) {
        mbedtls_strerror(rc, error_buf, sizeof(error_buf));
        SSH_LOG(SSH_LOG_TRACE, "Failed to read peer public key: %s", error_buf);
        goto out;
    }

    rc = mbedtls_mpi_lset(
        &ecdh_params->MBEDTLS_ECDH_PRIVATE(Qp).MBEDTLS_ECDH_PRIVATE(Z),
        1);
    if (rc != 0) {
        mbedtls_strerror(rc, error_buf, sizeof(error_buf));
        SSH_LOG(SSH_LOG_TRACE, "Failed to set Z coordinate: %s", error_buf);
        goto out;
    }

    rc = mbedtls_ecdh_compute_shared(&ecdh_params->MBEDTLS_ECDH_PRIVATE(grp),
                                     &ecdh_params->MBEDTLS_ECDH_PRIVATE(z),
                                     &ecdh_params->MBEDTLS_ECDH_PRIVATE(Qp),
                                     &ecdh_params->MBEDTLS_ECDH_PRIVATE(d),
                                     mbedtls_ctr_drbg_random,
                                     ctr_drbg);
    if (rc != 0) {
        mbedtls_strerror(rc, error_buf, sizeof(error_buf));
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to compute shared secret: %s",
                error_buf);
        goto out;
    }

    rc = mbedtls_mpi_write_binary_le(&ecdh_params->MBEDTLS_ECDH_PRIVATE(z),
                                     k,
                                     CURVE25519_PUBKEY_SIZE);
    if (rc != 0) {
        mbedtls_strerror(rc, error_buf, sizeof(error_buf));
        SSH_LOG(SSH_LOG_TRACE, "Failed to write shared secret: %s", error_buf);
        goto out;
    }

    ret = SSH_OK;

out:
    mbedtls_ecdh_free(&ecdh_ctx);
    return ret;
}
