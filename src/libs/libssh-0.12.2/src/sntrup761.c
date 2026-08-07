/*
 * sntrup761.c - SNTRUP761x25519 ECDH functions for key exchange
 * sntrup761x25519-sha512@openssh.com - based on curve25519.c.
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2013      by Aris Adamantiadis <aris@badcode.be>
 * Copyright (c) 2023 Simon Josefsson <simon@josefsson.org>
 * Copyright (c) 2025 Jakub Jelen <jjelen@redhat.com>
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

#include "libssh/sntrup761.h"
#ifdef HAVE_SNTRUP761

#include "libssh/bignum.h"
#include "libssh/buffer.h"
#include "libssh/crypto.h"
#include "libssh/dh.h"
#include "libssh/pki.h"
#include "libssh/priv.h"
#include "libssh/session.h"
#include "libssh/ssh2.h"

#ifndef HAVE_LIBGCRYPT
static void crypto_random(void *ctx, size_t length, uint8_t *dst)
{
    int *err = ctx;
    *err = ssh_get_random(dst, length, 1);
}
#endif /* HAVE_LIBGCRYPT */

static SSH_PACKET_CALLBACK(ssh_packet_client_sntrup761x25519_reply);

static ssh_packet_callback dh_client_callbacks[] = {
    ssh_packet_client_sntrup761x25519_reply,
};

static struct ssh_packet_callbacks_struct ssh_sntrup761x25519_client_callbacks =
    {
        .start = SSH2_MSG_KEX_ECDH_REPLY,
        .n_callbacks = 1,
        .callbacks = dh_client_callbacks,
        .user = NULL,
};

static int ssh_sntrup761x25519_init(ssh_session session)
{
    int rc;

    rc = ssh_curve25519_init(session);
    if (rc != SSH_OK) {
        return rc;
    }

    if (!session->server) {
#ifdef HAVE_LIBGCRYPT
        gcry_error_t err;

        err = gcry_kem_keypair(GCRY_KEM_SNTRUP761,
                               session->next_crypto->sntrup761_client_pubkey,
                               SNTRUP761_PUBLICKEY_SIZE,
                               session->next_crypto->sntrup761_privkey,
                               SNTRUP761_SECRETKEY_SIZE);
        if (err) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Failed to generate sntrup761 key: %s",
                    gpg_strerror(err));
            return SSH_ERROR;
        }
#else
        sntrup761_keypair(session->next_crypto->sntrup761_client_pubkey,
                          session->next_crypto->sntrup761_privkey,
                          &rc,
                          crypto_random);
        if (rc != 1) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Failed to generate sntrup761 key: PRNG failure");
            return SSH_ERROR;
        }
#endif /* HAVE_LIBGCRYPT */
    }

    return SSH_OK;
}

/** @internal
 * @brief Starts sntrup761x25519-sha512@openssh.com key exchange
 */
int ssh_client_sntrup761x25519_init(ssh_session session)
{
    int rc;

    rc = ssh_sntrup761x25519_init(session);
    if (rc != SSH_OK) {
        return rc;
    }

    rc = ssh_buffer_pack(session->out_buffer,
                         "bdPP",
                         SSH2_MSG_KEX_ECDH_INIT,
                         CURVE25519_PUBKEY_SIZE + SNTRUP761_PUBLICKEY_SIZE,
                         (size_t)SNTRUP761_PUBLICKEY_SIZE,
                         session->next_crypto->sntrup761_client_pubkey,
                         (size_t)CURVE25519_PUBKEY_SIZE,
                         session->next_crypto->curve25519_client_pubkey);
    if (rc != SSH_OK) {
        ssh_set_error_oom(session);
        return SSH_ERROR;
    }

    /* register the packet callbacks */
    ssh_packet_set_callbacks(session, &ssh_sntrup761x25519_client_callbacks);
    session->dh_handshake_state = DH_STATE_INIT_SENT;
    rc = ssh_packet_send(session);

    return rc;
}

void ssh_client_sntrup761x25519_remove_callbacks(ssh_session session)
{
    ssh_packet_remove_callbacks(session, &ssh_sntrup761x25519_client_callbacks);
}

static int ssh_sntrup761x25519_build_k(ssh_session session)
{
    unsigned char ssk[SNTRUP761_SIZE + CURVE25519_PUBKEY_SIZE];
    unsigned char *k = ssk + SNTRUP761_SIZE;
    unsigned char hss[SHA512_DIGEST_LEN];
    int rc;

    rc = ssh_curve25519_create_k(session, k);
    if (rc != SSH_OK) {
        goto cleanup;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("Curve25519 shared secret", k, CURVE25519_PUBKEY_SIZE);
#endif

#ifdef HAVE_LIBGCRYPT
    if (session->server) {
        gcry_error_t err;
        err = gcry_kem_encap(GCRY_KEM_SNTRUP761,
                             session->next_crypto->sntrup761_client_pubkey,
                             SNTRUP761_PUBLICKEY_SIZE,
                             session->next_crypto->sntrup761_ciphertext,
                             SNTRUP761_CIPHERTEXT_SIZE,
                             ssk,
                             SNTRUP761_SIZE,
                             NULL,
                             0);
        if (err) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Failed to encapsulate sntrup761 shared secret: %s",
                    gpg_strerror(err));
            rc = SSH_ERROR;
            goto cleanup;
        }
    } else {
        gcry_error_t err;
        err = gcry_kem_decap(GCRY_KEM_SNTRUP761,
                             session->next_crypto->sntrup761_privkey,
                             SNTRUP761_SECRETKEY_SIZE,
                             session->next_crypto->sntrup761_ciphertext,
                             SNTRUP761_CIPHERTEXT_SIZE,
                             ssk,
                             SNTRUP761_SIZE,
                             NULL,
                             0);
        if (err) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Failed to decapsulate sntrup761 shared secret: %s",
                    gpg_strerror(err));
            rc = SSH_ERROR;
            goto cleanup;
        }
    }
#else
    if (session->server) {
        sntrup761_enc(session->next_crypto->sntrup761_ciphertext,
                      ssk,
                      session->next_crypto->sntrup761_client_pubkey,
                      &rc,
                      crypto_random);
        if (rc != 1) {
            rc = SSH_ERROR;
            goto cleanup;
        }
    } else {
        sntrup761_dec(ssk,
                      session->next_crypto->sntrup761_ciphertext,
                      session->next_crypto->sntrup761_privkey);
    }
#endif /* HAVE_LIBGCRYPT */

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("server cipher text",
                    session->next_crypto->sntrup761_ciphertext,
                    SNTRUP761_CIPHERTEXT_SIZE);
    ssh_log_hexdump("kem key", ssk, SNTRUP761_SIZE);
#endif

    sha512(ssk, sizeof ssk, hss);

    bignum_bin2bn(hss, sizeof hss, &session->next_crypto->shared_secret);
    if (session->next_crypto->shared_secret == NULL) {
        rc = SSH_ERROR;
        goto cleanup;
    }

#ifdef DEBUG_CRYPTO
    ssh_print_bignum("Shared secret key", session->next_crypto->shared_secret);
#endif

    return 0;
cleanup:
    ssh_burn(ssk, sizeof ssk);
    ssh_burn(hss, sizeof hss);

    return rc;
}

/** @internal
 * @brief parses a SSH_MSG_KEX_ECDH_REPLY packet and sends back
 * a SSH_MSG_NEWKEYS
 */
static SSH_PACKET_CALLBACK(ssh_packet_client_sntrup761x25519_reply)
{
    ssh_string q_s_string = NULL;
    ssh_string pubkey_blob = NULL;
    ssh_string signature = NULL;
    int rc;
    (void)type;
    (void)user;

    ssh_client_sntrup761x25519_remove_callbacks(session);

    pubkey_blob = ssh_buffer_get_ssh_string(packet);
    if (pubkey_blob == NULL) {
        ssh_set_error(session, SSH_FATAL, "No public key in packet");
        goto error;
    }

    rc = ssh_dh_import_next_pubkey_blob(session, pubkey_blob);
    SSH_STRING_FREE(pubkey_blob);
    if (rc != 0) {
        ssh_set_error(session, SSH_FATAL, "Failed to import next public key");
        goto error;
    }

    q_s_string = ssh_buffer_get_ssh_string(packet);
    if (q_s_string == NULL) {
        ssh_set_error(session, SSH_FATAL, "No sntrup761x25519 Q_S in packet");
        goto error;
    }
    if (ssh_string_len(q_s_string) != (SNTRUP761_CIPHERTEXT_SIZE + CURVE25519_PUBKEY_SIZE)) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Incorrect size for server sntrup761x25519 ciphertext+key: %d",
                      (int)ssh_string_len(q_s_string));
        SSH_STRING_FREE(q_s_string);
        goto error;
    }
    memcpy(session->next_crypto->sntrup761_ciphertext,
           ssh_string_data(q_s_string),
           SNTRUP761_CIPHERTEXT_SIZE);
    memcpy(session->next_crypto->curve25519_server_pubkey,
           (char *)ssh_string_data(q_s_string) + SNTRUP761_CIPHERTEXT_SIZE,
           CURVE25519_PUBKEY_SIZE);
    SSH_STRING_FREE(q_s_string);

    signature = ssh_buffer_get_ssh_string(packet);
    if (signature == NULL) {
        ssh_set_error(session, SSH_FATAL, "No signature in packet");
        goto error;
    }
    session->next_crypto->dh_server_signature = signature;
    signature = NULL; /* ownership changed */
    /* TODO: verify signature now instead of waiting for NEWKEYS */
    if (ssh_sntrup761x25519_build_k(session) < 0) {
        ssh_set_error(session, SSH_FATAL, "Cannot build k number");
        goto error;
    }

    /* Send the MSG_NEWKEYS */
    if (ssh_buffer_add_u8(session->out_buffer, SSH2_MSG_NEWKEYS) < 0) {
        goto error;
    }

    rc = ssh_packet_send(session);
    if (rc == SSH_ERROR) {
        goto error;
    }

    SSH_LOG(SSH_LOG_DEBUG, "SSH_MSG_NEWKEYS sent");
    session->dh_handshake_state = DH_STATE_NEWKEYS_SENT;

    return SSH_PACKET_USED;

error:
    session->session_state = SSH_SESSION_STATE_ERROR;
    return SSH_PACKET_USED;
}

#ifdef WITH_SERVER

static SSH_PACKET_CALLBACK(ssh_packet_server_sntrup761x25519_init);

static ssh_packet_callback dh_server_callbacks[] = {
    ssh_packet_server_sntrup761x25519_init,
};

static struct ssh_packet_callbacks_struct ssh_sntrup761x25519_server_callbacks =
    {
        .start = SSH2_MSG_KEX_ECDH_INIT,
        .n_callbacks = 1,
        .callbacks = dh_server_callbacks,
        .user = NULL,
};

/** @internal
 * @brief sets up the sntrup761x25519-sha512@openssh.com kex callbacks
 */
void ssh_server_sntrup761x25519_init(ssh_session session)
{
    /* register the packet callbacks */
    ssh_packet_set_callbacks(session, &ssh_sntrup761x25519_server_callbacks);
}

/** @brief Parse a SSH_MSG_KEXDH_INIT packet (server) and send a
 * SSH_MSG_KEXDH_REPLY
 */
static SSH_PACKET_CALLBACK(ssh_packet_server_sntrup761x25519_init)
{
    /* ECDH/SNTRUP761 keys */
    ssh_string q_c_string = NULL;
    ssh_string q_s_string = NULL;
    ssh_string server_pubkey_blob = NULL;

    /* SSH host keys (rsa, ed25519 and ecdsa) */
    ssh_key privkey = NULL;
    enum ssh_digest_e digest = SSH_DIGEST_AUTO;
    ssh_string sig_blob = NULL;
    int rc;
    (void)type;
    (void)user;

    ssh_packet_remove_callbacks(session, &ssh_sntrup761x25519_server_callbacks);

    /* Extract the client pubkey from the init packet */
    q_c_string = ssh_buffer_get_ssh_string(packet);
    if (q_c_string == NULL) {
        ssh_set_error(session, SSH_FATAL, "No sntrup761x25519 Q_C in packet");
        goto error;
    }
    if (ssh_string_len(q_c_string) != (SNTRUP761_PUBLICKEY_SIZE + CURVE25519_PUBKEY_SIZE)) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Incorrect size for server sntrup761x25519 public key: %zu",
                      ssh_string_len(q_c_string));
        goto error;
    }

    memcpy(session->next_crypto->sntrup761_client_pubkey,
           ssh_string_data(q_c_string),
           SNTRUP761_PUBLICKEY_SIZE);
    memcpy(session->next_crypto->curve25519_client_pubkey,
           ((char *)ssh_string_data(q_c_string)) + SNTRUP761_PUBLICKEY_SIZE,
           CURVE25519_PUBKEY_SIZE);
    SSH_STRING_FREE(q_c_string);

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("client public key sntrup761",
                    session->next_crypto->sntrup761_client_pubkey,
                    SNTRUP761_PUBLICKEY_SIZE);
    ssh_log_hexdump("client public key c25519",
                    session->next_crypto->curve25519_client_pubkey,
                    CURVE25519_PUBKEY_SIZE);
#endif

    /* Build server's key pair */
    rc = ssh_sntrup761x25519_init(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to generate sntrup761 keys");
        goto error;
    }

    rc = ssh_buffer_add_u8(session->out_buffer, SSH2_MSG_KEX_ECDH_REPLY);
    if (rc < 0) {
        ssh_set_error_oom(session);
        goto error;
    }

    /* build k and session_id */
    rc = ssh_sntrup761x25519_build_k(session);
    if (rc < 0) {
        ssh_set_error(session, SSH_FATAL, "Cannot build k number");
        goto error;
    }

    /* privkey is not allocated */
    rc = ssh_get_key_params(session, &privkey, &digest);
    if (rc == SSH_ERROR) {
        goto error;
    }

    rc = ssh_make_sessionid(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Could not create a session id");
        goto error;
    }

    rc = ssh_dh_get_next_server_publickey_blob(session, &server_pubkey_blob);
    if (rc != 0) {
        ssh_set_error(session, SSH_FATAL, "Could not export server public key");
        goto error;
    }

    /* add host's public key */
    rc = ssh_buffer_add_ssh_string(session->out_buffer, server_pubkey_blob);
    SSH_STRING_FREE(server_pubkey_blob);
    if (rc < 0) {
        ssh_set_error_oom(session);
        goto error;
    }

    /* add ecdh public key */
    rc = ssh_buffer_add_u32(session->out_buffer,
                            ntohl(SNTRUP761_CIPHERTEXT_SIZE
                                  + CURVE25519_PUBKEY_SIZE));
    if (rc < 0) {
        ssh_set_error_oom(session);
        goto error;
    }

    rc = ssh_buffer_add_data(session->out_buffer,
                             session->next_crypto->sntrup761_ciphertext,
                             SNTRUP761_CIPHERTEXT_SIZE);
    if (rc < 0) {
        ssh_set_error_oom(session);
        goto error;
    }

    rc = ssh_buffer_add_data(session->out_buffer,
                             session->next_crypto->curve25519_server_pubkey,
                             CURVE25519_PUBKEY_SIZE);
    if (rc < 0) {
        ssh_set_error_oom(session);
        goto error;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("server public key c25519",
                    session->next_crypto->curve25519_server_pubkey,
                    CURVE25519_PUBKEY_SIZE);
#endif

    /* add signature blob */
    sig_blob = ssh_srv_pki_do_sign_sessionid(session, privkey, digest);
    if (sig_blob == NULL) {
        ssh_set_error(session, SSH_FATAL, "Could not sign the session id");
        goto error;
    }

    rc = ssh_buffer_add_ssh_string(session->out_buffer, sig_blob);
    SSH_STRING_FREE(sig_blob);
    if (rc < 0) {
        ssh_set_error_oom(session);
        goto error;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("ECDH_REPLY:",
                    ssh_buffer_get(session->out_buffer),
                    ssh_buffer_get_len(session->out_buffer));
#endif

    SSH_LOG(SSH_LOG_DEBUG, "SSH_MSG_KEX_ECDH_REPLY sent");
    rc = ssh_packet_send(session);
    if (rc == SSH_ERROR) {
        return SSH_ERROR;
    }

    /* Send the MSG_NEWKEYS */
    rc = ssh_buffer_add_u8(session->out_buffer, SSH2_MSG_NEWKEYS);
    if (rc < 0) {
        goto error;
    }

    session->dh_handshake_state = DH_STATE_NEWKEYS_SENT;
    rc = ssh_packet_send(session);
    if (rc == SSH_ERROR) {
        goto error;
    }
    SSH_LOG(SSH_LOG_DEBUG, "SSH_MSG_NEWKEYS sent");

    return SSH_PACKET_USED;
error:
    SSH_STRING_FREE(q_c_string);
    SSH_STRING_FREE(q_s_string);
    ssh_buffer_reinit(session->out_buffer);
    session->session_state = SSH_SESSION_STATE_ERROR;
    return SSH_PACKET_USED;
}

#endif /* WITH_SERVER */

#endif /* HAVE_SNTRUP761 */
