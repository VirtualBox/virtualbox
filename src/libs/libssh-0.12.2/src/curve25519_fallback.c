/*
 * curve25519_fallback.c - Curve25519 ECDH functions for key exchange
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

#ifdef WITH_NACL
#include "nacl/crypto_scalarmult_curve25519.h"
#endif

int ssh_curve25519_init(ssh_session session)
{
    ssh_curve25519_pubkey *pubkey_loc = NULL;
    int rc;

    if (session->server) {
        pubkey_loc = &session->next_crypto->curve25519_server_pubkey;
    } else {
        pubkey_loc = &session->next_crypto->curve25519_client_pubkey;
    }

    rc = ssh_get_random(session->next_crypto->curve25519_privkey,
                        CURVE25519_PRIVKEY_SIZE,
                        1);
    if (rc != 1) {
        ssh_set_error(session, SSH_FATAL, "PRNG error");
        return SSH_ERROR;
    }

    crypto_scalarmult_base(*pubkey_loc,
                           session->next_crypto->curve25519_privkey);

    return SSH_OK;
}

int curve25519_do_create_k(ssh_session session, ssh_curve25519_pubkey k)
{
    ssh_curve25519_pubkey *peer_pubkey_loc = NULL;

    if (session->server) {
        peer_pubkey_loc = &session->next_crypto->curve25519_client_pubkey;
    } else {
        peer_pubkey_loc = &session->next_crypto->curve25519_server_pubkey;
    }

    crypto_scalarmult(k,
                      session->next_crypto->curve25519_privkey,
                      *peer_pubkey_loc);
    return SSH_OK;
}
