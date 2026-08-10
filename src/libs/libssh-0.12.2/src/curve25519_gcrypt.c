/*
 * curve25519_gcrypt.c - Curve25519 ECDH functions for key exchange (Gcrypt)
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

#include "libssh/buffer.h"
#include "libssh/crypto.h"
#include "libssh/priv.h"
#include "libssh/session.h"

#include <gcrypt.h>

int ssh_curve25519_init(ssh_session session)
{
    ssh_curve25519_pubkey *pubkey_loc = NULL;
    gcry_error_t gcry_err;
    gcry_sexp_t param = NULL, keypair_sexp = NULL;
    ssh_string pubkey = NULL;
    const char *pubkey_data = NULL;
    int ret = SSH_ERROR;

    if (session->server) {
        pubkey_loc = &session->next_crypto->curve25519_server_pubkey;
    } else {
        pubkey_loc = &session->next_crypto->curve25519_client_pubkey;
    }

    gcry_err =
        gcry_sexp_build(&param, NULL, "(genkey (ecdh (curve Curve25519)))");
    if (gcry_err != GPG_ERR_NO_ERROR) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to create keypair sexp: %s",
                gcry_strerror(gcry_err));
        goto out;
    }

    gcry_err = gcry_pk_genkey(&keypair_sexp, param);
    if (gcry_err != GPG_ERR_NO_ERROR) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to generate keypair: %s",
                gcry_strerror(gcry_err));
        goto out;
    }

    /* Extract the public key */
    pubkey = ssh_sexp_extract_mpi(keypair_sexp,
                                  "q",
                                  GCRYMPI_FMT_USG,
                                  GCRYMPI_FMT_STD);
    if (pubkey == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to extract public key: %s",
                gcry_strerror(gcry_err));
        goto out;
    }

    /* Store the public key in the session */
    /* The first byte should be 0x40 indicating that the point is compressed, so
     * we skip storing it */
    pubkey_data = (char *)ssh_string_data(pubkey);
    if (ssh_string_len(pubkey) != CURVE25519_PUBKEY_SIZE + 1 ||
        pubkey_data[0] != 0x40) {
        SSH_LOG(SSH_LOG_TRACE,
                "Invalid public key with length: %zu",
                ssh_string_len(pubkey));
        goto out;
    }

    memcpy(*pubkey_loc, pubkey_data + 1, CURVE25519_PUBKEY_SIZE);

    /* Free any previously allocated privkey */
    if (session->next_crypto->curve25519_privkey != NULL) {
        gcry_sexp_release(session->next_crypto->curve25519_privkey);
        session->next_crypto->curve25519_privkey = NULL;
    }

    /* Store the private key */
    session->next_crypto->curve25519_privkey = keypair_sexp;
    keypair_sexp = NULL;
    ret = SSH_OK;

out:
    ssh_string_burn(pubkey);
    SSH_STRING_FREE(pubkey);
    gcry_sexp_release(param);
    gcry_sexp_release(keypair_sexp);
    return ret;
}

int curve25519_do_create_k(ssh_session session, ssh_curve25519_pubkey k)
{
    ssh_curve25519_pubkey *peer_pubkey_loc = NULL;
    gcry_error_t gcry_err;
    gcry_sexp_t pubkey_sexp = NULL, privkey_data_sexp = NULL,
                result_sexp = NULL;
    ssh_string shared_secret = NULL, privkey = NULL;
    char *shared_secret_data = NULL;
    int ret = SSH_ERROR;

    if (session->server) {
        peer_pubkey_loc = &session->next_crypto->curve25519_client_pubkey;
    } else {
        peer_pubkey_loc = &session->next_crypto->curve25519_server_pubkey;
    }

    gcry_err = gcry_sexp_build(
        &pubkey_sexp,
        NULL,
        "(key-data(public-key (ecdh (curve Curve25519) (q %b))))",
        CURVE25519_PUBKEY_SIZE,
        *peer_pubkey_loc);
    if (gcry_err != GPG_ERR_NO_ERROR) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to create peer public key sexp: %s",
                gcry_strerror(gcry_err));
        goto out;
    }

    privkey = ssh_sexp_extract_mpi(session->next_crypto->curve25519_privkey,
                                   "d",
                                   GCRYMPI_FMT_USG,
                                   GCRYMPI_FMT_STD);
    if (privkey == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to extract private key");
        goto out;
    }

    gcry_err = gcry_sexp_build(&privkey_data_sexp,
                               NULL,
                               "(data(flags raw)(value %b))",
                               ssh_string_len(privkey),
                               ssh_string_data(privkey));
    if (gcry_err != GPG_ERR_NO_ERROR) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to create private key sexp: %s",
                gcry_strerror(gcry_err));
        goto out;
    }

    gcry_err = gcry_pk_encrypt(&result_sexp, privkey_data_sexp, pubkey_sexp);
    if (gcry_err != GPG_ERR_NO_ERROR) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to compute shared secret: %s",
                gcry_strerror(gcry_err));
        goto out;
    }

    shared_secret = ssh_sexp_extract_mpi(result_sexp,
                                         "s",
                                         GCRYMPI_FMT_USG,
                                         GCRYMPI_FMT_USG);
    if (shared_secret == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to extract shared secret");
        goto out;
    }

    /* Copy the shared secret to the output buffer */
    /* The first byte should be 0x40 indicating that it is a compressed point,
     * so we skip it */
    shared_secret_data = (char *)ssh_string_data(shared_secret);
    if (ssh_string_len(shared_secret) != CURVE25519_PUBKEY_SIZE + 1 ||
        shared_secret_data[0] != 0x40) {
        SSH_LOG(SSH_LOG_TRACE,
                "Invalid shared secret with length: %zu",
                ssh_string_len(shared_secret));
        goto out;
    }

    memcpy(k, shared_secret_data + 1, CURVE25519_PUBKEY_SIZE);

    ret = SSH_OK;
    gcry_sexp_release(session->next_crypto->curve25519_privkey);
    session->next_crypto->curve25519_privkey = NULL;

out:
    ssh_string_burn(shared_secret);
    SSH_STRING_FREE(shared_secret);
    ssh_string_burn(privkey);
    SSH_STRING_FREE(privkey);
    gcry_sexp_release(privkey_data_sexp);
    gcry_sexp_release(pubkey_sexp);
    gcry_sexp_release(result_sexp);
    return ret;
}
