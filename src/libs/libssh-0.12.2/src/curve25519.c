/*
 * curve25519.c - Curve25519 ECDH functions for key exchange
 * curve25519-sha256@libssh.org and curve25519-sha256
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2013      by Aris Adamantiadis <aris@badcode.be>
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
#ifdef HAVE_CURVE25519

#include "libssh/bignum.h"
#include "libssh/buffer.h"
#include "libssh/crypto.h"
#include "libssh/dh.h"
#include "libssh/pki.h"
#include "libssh/priv.h"
#include "libssh/session.h"
#include "libssh/ssh2.h"

static SSH_PACKET_CALLBACK(ssh_packet_client_curve25519_reply);

static ssh_packet_callback dh_client_callbacks[] = {
    ssh_packet_client_curve25519_reply,
};

static struct ssh_packet_callbacks_struct ssh_curve25519_client_callbacks = {
    .start = SSH2_MSG_KEX_ECDH_REPLY,
    .n_callbacks = 1,
    .callbacks = dh_client_callbacks,
    .user = NULL,
};

int ssh_curve25519_create_k(ssh_session session, ssh_curve25519_pubkey k)
{
    int rc;

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("Session server cookie",
                    session->next_crypto->server_kex.cookie,
                    16);
    ssh_log_hexdump("Session client cookie",
                    session->next_crypto->client_kex.cookie,
                    16);
#endif

    rc = curve25519_do_create_k(session, k);
    return rc;
}

/** @internal
 * @brief Starts curve25519-sha256@libssh.org / curve25519-sha256 key exchange
 */
int ssh_client_curve25519_init(ssh_session session)
{
    int rc;

    rc = ssh_curve25519_init(session);
    if (rc != SSH_OK) {
        return rc;
    }

    rc = ssh_buffer_pack(session->out_buffer,
                         "bdP",
                         SSH2_MSG_KEX_ECDH_INIT,
                         CURVE25519_PUBKEY_SIZE,
                         (size_t)CURVE25519_PUBKEY_SIZE,
                         session->next_crypto->curve25519_client_pubkey);
    if (rc != SSH_OK) {
        ssh_set_error_oom(session);
        return SSH_ERROR;
    }

    /* register the packet callbacks */
    ssh_packet_set_callbacks(session, &ssh_curve25519_client_callbacks);
    session->dh_handshake_state = DH_STATE_INIT_SENT;
    rc = ssh_packet_send(session);

    return rc;
}

void ssh_client_curve25519_remove_callbacks(ssh_session session)
{
    ssh_packet_remove_callbacks(session, &ssh_curve25519_client_callbacks);
}

int ssh_curve25519_build_k(ssh_session session)
{
    ssh_curve25519_pubkey k;
    int rc;

    rc = ssh_curve25519_create_k(session, k);
    if (rc != SSH_OK) {
        return rc;
    }

    bignum_bin2bn(k,
                  CURVE25519_PUBKEY_SIZE,
                  &session->next_crypto->shared_secret);
    if (session->next_crypto->shared_secret == NULL) {
        return SSH_ERROR;
    }

#ifdef DEBUG_CRYPTO
    ssh_print_bignum("Shared secret key", session->next_crypto->shared_secret);
#endif

    return SSH_OK;
}

/** @internal
 * @brief parses a SSH_MSG_KEX_ECDH_REPLY packet and sends back
 * a SSH_MSG_NEWKEYS
 */
static SSH_PACKET_CALLBACK(ssh_packet_client_curve25519_reply)
{
    ssh_string q_s_string = NULL;
    ssh_string pubkey_blob = NULL;
    ssh_string signature = NULL;
    int rc;
    (void)type;
    (void)user;

    ssh_client_curve25519_remove_callbacks(session);

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
        ssh_set_error(session, SSH_FATAL, "No Q_S ECC point in packet");
        goto error;
    }
    if (ssh_string_len(q_s_string) != CURVE25519_PUBKEY_SIZE) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Incorrect size for server Curve25519 public key: %zu",
                      ssh_string_len(q_s_string));
        SSH_STRING_FREE(q_s_string);
        goto error;
    }
    memcpy(session->next_crypto->curve25519_server_pubkey,
           ssh_string_data(q_s_string),
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
    if (ssh_curve25519_build_k(session) < 0) {
        ssh_set_error(session, SSH_FATAL, "Cannot build k number");
        goto error;
    }

    /* Send the MSG_NEWKEYS */
    rc = ssh_packet_send_newkeys(session);
    if (rc == SSH_ERROR) {
        goto error;
    }
    session->dh_handshake_state = DH_STATE_NEWKEYS_SENT;

    return SSH_PACKET_USED;

error:
    session->session_state = SSH_SESSION_STATE_ERROR;
    return SSH_PACKET_USED;
}

#ifdef WITH_SERVER

static SSH_PACKET_CALLBACK(ssh_packet_server_curve25519_init);

static ssh_packet_callback dh_server_callbacks[] = {
    ssh_packet_server_curve25519_init,
};

static struct ssh_packet_callbacks_struct ssh_curve25519_server_callbacks = {
    .start = SSH2_MSG_KEX_ECDH_INIT,
    .n_callbacks = 1,
    .callbacks = dh_server_callbacks,
    .user = NULL,
};

/** @internal
 * @brief sets up the curve25519-sha256@libssh.org kex callbacks
 */
void ssh_server_curve25519_init(ssh_session session)
{
    /* register the packet callbacks */
    ssh_packet_set_callbacks(session, &ssh_curve25519_server_callbacks);
}

/** @brief Parse a SSH_MSG_KEXDH_INIT packet (server) and send a
 * SSH_MSG_KEXDH_REPLY
 */
static SSH_PACKET_CALLBACK(ssh_packet_server_curve25519_init)
{
    /* ECDH keys */
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

    ssh_packet_remove_callbacks(session, &ssh_curve25519_server_callbacks);

    /* Extract the client pubkey from the init packet */
    q_c_string = ssh_buffer_get_ssh_string(packet);
    if (q_c_string == NULL) {
        ssh_set_error(session, SSH_FATAL, "No Q_C ECC point in packet");
        goto error;
    }
    if (ssh_string_len(q_c_string) != CURVE25519_PUBKEY_SIZE) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Incorrect size for server Curve25519 public key: %zu",
                      ssh_string_len(q_c_string));
        goto error;
    }

    memcpy(session->next_crypto->curve25519_client_pubkey,
           ssh_string_data(q_c_string),
           CURVE25519_PUBKEY_SIZE);
    SSH_STRING_FREE(q_c_string);

    /* Build server's key pair */
    rc = ssh_curve25519_init(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to generate curve25519 keys");
        goto error;
    }

    rc = ssh_buffer_add_u8(session->out_buffer, SSH2_MSG_KEX_ECDH_REPLY);
    if (rc < 0) {
        ssh_set_error_oom(session);
        goto error;
    }

    /* build k and session_id */
    rc = ssh_curve25519_build_k(session);
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
    q_s_string = ssh_string_new(CURVE25519_PUBKEY_SIZE);
    if (q_s_string == NULL) {
        ssh_set_error_oom(session);
        goto error;
    }

    rc = ssh_string_fill(q_s_string,
                         session->next_crypto->curve25519_server_pubkey,
                         CURVE25519_PUBKEY_SIZE);
    if (rc < 0) {
        ssh_set_error(session, SSH_FATAL, "Could not copy public key");
        goto error;
    }

    rc = ssh_buffer_add_ssh_string(session->out_buffer, q_s_string);
    SSH_STRING_FREE(q_s_string);
    if (rc < 0) {
        ssh_set_error_oom(session);
        goto error;
    }
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

    SSH_LOG(SSH_LOG_DEBUG, "SSH_MSG_KEX_ECDH_REPLY sent");
    rc = ssh_packet_send(session);
    if (rc == SSH_ERROR) {
        return SSH_ERROR;
    }

    session->dh_handshake_state = DH_STATE_NEWKEYS_SENT;

    /* Send the MSG_NEWKEYS */
    rc = ssh_packet_send_newkeys(session);
    if (rc == SSH_ERROR) {
        goto error;
    }

    return SSH_PACKET_USED;
error:
    SSH_STRING_FREE(q_c_string);
    SSH_STRING_FREE(q_s_string);
    ssh_buffer_reinit(session->out_buffer);
    session->session_state = SSH_SESSION_STATE_ERROR;
    return SSH_PACKET_USED;
}

#endif /* WITH_SERVER */

#endif /* HAVE_CURVE25519 */
