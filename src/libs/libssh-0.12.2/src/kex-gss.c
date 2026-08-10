/*
 * kex-gss.c - GSSAPI key exchange
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2024 by Gauravsingh Sisodia <xaerru@gmail.com>
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

#include "libssh/gssapi.h"
#include <errno.h>
#include <gssapi/gssapi.h>
#include <stdio.h>

#include "libssh/buffer.h"
#include "libssh/crypto.h"
#include "libssh/kex-gss.h"
#include "libssh/bignum.h"
#include "libssh/curve25519.h"
#include "libssh/ecdh.h"
#include "libssh/dh.h"
#include "libssh/priv.h"
#include "libssh/session.h"
#include "libssh/ssh2.h"

static SSH_PACKET_CALLBACK(ssh_packet_client_gss_kex_reply);

static ssh_packet_callback gss_kex_client_callbacks[] = {
    ssh_packet_client_gss_kex_reply,
};

static struct ssh_packet_callbacks_struct ssh_gss_kex_client_callbacks = {
    .start = SSH2_MSG_KEXGSS_COMPLETE,
    .n_callbacks = 1,
    .callbacks = gss_kex_client_callbacks,
    .user = NULL,
};

static SSH_PACKET_CALLBACK(ssh_packet_client_gss_kex_hostkey);

static ssh_packet_callback gss_kex_client_callback_hostkey[] = {
    ssh_packet_client_gss_kex_hostkey,
};

static struct ssh_packet_callbacks_struct ssh_gss_kex_client_callback_hostkey = {
    .start = SSH2_MSG_KEXGSS_HOSTKEY,
    .n_callbacks = 1,
    .callbacks = gss_kex_client_callback_hostkey,
    .user = NULL,
};

static ssh_string dh_init(ssh_session session)
{
    int rc, keypair;
#if !defined(HAVE_LIBCRYPTO) || OPENSSL_VERSION_NUMBER < 0x30000000L
    const_bignum const_pubkey;
#endif
    bignum pubkey = NULL;
    ssh_string pubkey_string = NULL;
    struct ssh_crypto_struct *crypto = session->next_crypto;

    if (session->server) {
        keypair = DH_SERVER_KEYPAIR;
    } else {
        keypair = DH_CLIENT_KEYPAIR;
    }

    rc = ssh_dh_init_common(crypto);
    if (rc != SSH_OK) {
        goto end;
    }

    rc = ssh_dh_keypair_gen_keys(crypto->dh_ctx, keypair);
    if (rc != SSH_OK) {
        goto end;
    }

#if !defined(HAVE_LIBCRYPTO) || OPENSSL_VERSION_NUMBER < 0x30000000L
    rc = ssh_dh_keypair_get_keys(crypto->dh_ctx, keypair, NULL, &const_pubkey);
    bignum_dup(const_pubkey, &pubkey);
#else
    rc = ssh_dh_keypair_get_keys(crypto->dh_ctx, keypair, NULL, &pubkey);
#endif
    if (rc != SSH_OK) {
        goto end;
    }

    pubkey_string = ssh_make_bignum_string(pubkey);

end:
    bignum_safe_free(pubkey);
    return pubkey_string;
}

static int dh_import_peer_key(ssh_session session, ssh_string peer_key)
{
    int rc, keypair;
    bignum peer_key_bn;
    struct ssh_crypto_struct *crypto = session->next_crypto;

    if (session->server) {
        keypair = DH_CLIENT_KEYPAIR;
    } else {
        keypair = DH_SERVER_KEYPAIR;
    }

    peer_key_bn = ssh_make_string_bn(peer_key);
    rc = ssh_dh_keypair_set_keys(crypto->dh_ctx, keypair, NULL, peer_key_bn);
    if (rc != SSH_OK) {
        bignum_safe_free(peer_key_bn);
    }

    return rc;
}

/** @internal
 * @brief Starts gssapi key exchange
 */
int ssh_client_gss_kex_init(ssh_session session)
{
    struct ssh_crypto_struct *crypto = session->next_crypto;
    int rc, ret = SSH_ERROR;
    /* oid selected for authentication */
    gss_OID_set selected = GSS_C_NO_OID_SET;
    OM_uint32 maj_stat, min_stat;
    const char *gss_host = session->opts.host;
    gss_buffer_desc input_token = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc output_token = GSS_C_EMPTY_BUFFER;
    OM_uint32 oflags;
    ssh_string pubkey = NULL;

    switch (crypto->kex_type) {
    case SSH_GSS_KEX_DH_GROUP14_SHA256:
    case SSH_GSS_KEX_DH_GROUP16_SHA512:
        pubkey = dh_init(session);
        if (pubkey == NULL) {
            ssh_set_error(session, SSH_FATAL, "Failed to generate DH keypair");
            goto out;
        }
        break;
    case SSH_GSS_KEX_ECDH_NISTP256_SHA256:
        rc = ssh_ecdh_init(session);
        if (rc != SSH_OK) {
            ssh_set_error(session, SSH_FATAL, "Failed to generate ECDH keypair");
            goto out;
        }
        pubkey = ssh_string_copy(crypto->ecdh_client_pubkey);
        break;
    case SSH_GSS_KEX_CURVE25519_SHA256:
        rc = ssh_curve25519_init(session);
        if (rc != SSH_OK) {
            ssh_set_error(session, SSH_FATAL, "Failed to generate Curve25519 keypair");
            goto out;
        }
        pubkey = ssh_string_new(CURVE25519_PUBKEY_SIZE);
        if (pubkey == NULL) {
            ssh_set_error_oom(session);
            goto out;
        }
        rc = ssh_string_fill(pubkey,
                             crypto->curve25519_client_pubkey,
                             CURVE25519_PUBKEY_SIZE);
        if (rc != SSH_OK) {
            ssh_set_error(session, SSH_FATAL, "Failed to copy Curve25519 pubkey");
            goto out;
        }
        break;
    default:
        ssh_set_error(session, SSH_FATAL, "Unsupported GSSAPI KEX method");
        goto out;
    }

    rc = ssh_gssapi_init(session);
    if (rc != SSH_OK) {
        goto out;
    }

    if (session->opts.gss_server_identity != NULL) {
        gss_host = session->opts.gss_server_identity;
    }

    rc = ssh_gssapi_import_name(session->gssapi, gss_host);
    if (rc != SSH_OK) {
        goto out;
    }

    rc = ssh_gssapi_client_identity(session, &selected);
    if (rc != SSH_OK) {
        goto out;
    }

    session->gssapi->client.flags = GSS_C_MUTUAL_FLAG | GSS_C_INTEG_FLAG;
    maj_stat = ssh_gssapi_init_ctx(session->gssapi,
                                   &input_token,
                                   &output_token,
                                   &oflags);
    gss_release_oid_set(&min_stat, &selected);
    if (GSS_ERROR(maj_stat)) {
        ssh_gssapi_log_error(SSH_LOG_WARN,
                             "Initializing gssapi context",
                             maj_stat,
                             min_stat);
        goto out;
    }
    if (!(oflags & GSS_C_INTEG_FLAG) || !(oflags & GSS_C_MUTUAL_FLAG)) {
        SSH_LOG(SSH_LOG_WARN,
                "GSSAPI(init) integrity and mutual flags were not set");
        goto out;
    }

    rc = ssh_buffer_pack(session->out_buffer,
                         "bdPS",
                         SSH2_MSG_KEXGSS_INIT,
                         output_token.length,
                         (size_t)output_token.length,
                         output_token.value,
                         pubkey);
    if (rc != SSH_OK) {
        goto out;
    }

    /* register the packet callbacks */
    ssh_packet_set_callbacks(session, &ssh_gss_kex_client_callbacks);
    ssh_packet_set_callbacks(session, &ssh_gss_kex_client_callback_hostkey);
    session->dh_handshake_state = DH_STATE_INIT_SENT;

    rc = ssh_packet_send(session);
    if (rc != SSH_OK) {
        goto out;
    }

    ret = SSH_OK;

out:
    gss_release_buffer(&min_stat, &output_token);
    ssh_string_free(pubkey);
    return ret;
}

void ssh_client_gss_kex_remove_callbacks(ssh_session session)
{
    ssh_packet_remove_callbacks(session, &ssh_gss_kex_client_callbacks);
}

void ssh_client_gss_kex_remove_callback_hostkey(ssh_session session)
{
    ssh_packet_remove_callbacks(session, &ssh_gss_kex_client_callback_hostkey);
}

SSH_PACKET_CALLBACK(ssh_packet_client_gss_kex_reply)
{
    struct ssh_crypto_struct *crypto = session->next_crypto;
    ssh_string mic = NULL, otoken = NULL, server_pubkey = NULL;
    uint8_t b;
    int rc;
    gss_buffer_desc input_token = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc output_token = GSS_C_EMPTY_BUFFER;
    OM_uint32 oflags;
    OM_uint32 maj_stat;

    (void)type;
    (void)user;

    ssh_client_gss_kex_remove_callbacks(session);

    rc = ssh_buffer_unpack(packet, "SSbS", &server_pubkey, &mic, &b, &otoken);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "No public key in server reply");
        goto error;
    }

    SSH_STRING_FREE(session->gssapi_key_exchange_mic);
    session->gssapi_key_exchange_mic = mic;
    input_token.length = ssh_string_len(otoken);
    input_token.value = ssh_string_data(otoken);
    maj_stat = ssh_gssapi_init_ctx(session->gssapi,
                                   &input_token,
                                   &output_token,
                                   &oflags);
    if (maj_stat != GSS_S_COMPLETE) {
        goto error;
    }
    SSH_STRING_FREE(otoken);

    switch (crypto->kex_type) {
    case SSH_GSS_KEX_DH_GROUP14_SHA256:
    case SSH_GSS_KEX_DH_GROUP16_SHA512:
        rc = dh_import_peer_key(session, server_pubkey);
        if (rc != SSH_OK) {
            ssh_set_error(session, SSH_FATAL, "Could not import server pubkey");
            goto error;
        }
        rc = ssh_dh_compute_shared_secret(crypto->dh_ctx,
                                          DH_CLIENT_KEYPAIR,
                                          DH_SERVER_KEYPAIR,
                                          &crypto->shared_secret);
        ssh_dh_debug_crypto(crypto);
        break;
    case SSH_GSS_KEX_ECDH_NISTP256_SHA256:
        crypto->ecdh_server_pubkey = ssh_string_copy(server_pubkey);
        rc = ecdh_build_k(session);
        break;
    case SSH_GSS_KEX_CURVE25519_SHA256:
        if (ssh_string_len(server_pubkey) != CURVE25519_PUBKEY_SIZE) {
            ssh_set_error(session,
                          SSH_FATAL,
                          "Incorrect length of received server Curve25519 pubkey");
            goto error;
        }
        memcpy(crypto->curve25519_server_pubkey,
               ssh_string_data(server_pubkey),
               CURVE25519_PUBKEY_SIZE);
        rc = ssh_curve25519_build_k(session);
        break;
    default:
        ssh_set_error(session, SSH_FATAL, "Unsupported GSSAPI KEX method");
        goto error;
    }
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Could not derive shared secret");
        goto error;
    }

    /* Send the MSG_NEWKEYS */
    rc = ssh_packet_send_newkeys(session);
    if (rc == SSH_ERROR) {
        goto error;
    }

    ssh_string_free(server_pubkey);
    session->dh_handshake_state = DH_STATE_NEWKEYS_SENT;
    return SSH_PACKET_USED;

error:
    ssh_string_free(server_pubkey);
    session->session_state = SSH_SESSION_STATE_ERROR;
    return SSH_PACKET_USED;
}

SSH_PACKET_CALLBACK(ssh_packet_client_gss_kex_hostkey)
{
    ssh_string pubkey_blob = NULL;
    int rc;

    (void)type;
    (void)user;

    ssh_client_gss_kex_remove_callback_hostkey(session);

    rc = ssh_buffer_unpack(packet, "S", &pubkey_blob);
    if (rc == SSH_ERROR) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Invalid SSH2_MSG_KEXGSS_HOSTKEY packet");
        goto error;
    }

    rc = ssh_dh_import_next_pubkey_blob(session, pubkey_blob);
    SSH_STRING_FREE(pubkey_blob);
    if (rc != 0) {
        goto error;
    }

    return SSH_PACKET_USED;
error:
    session->session_state = SSH_SESSION_STATE_ERROR;
    return SSH_PACKET_USED;
}

#ifdef WITH_SERVER

static SSH_PACKET_CALLBACK(ssh_packet_server_gss_kex_init);

static ssh_packet_callback gss_kex_server_callbacks[] = {
    ssh_packet_server_gss_kex_init,
};

static struct ssh_packet_callbacks_struct ssh_gss_kex_server_callbacks = {
    .start = SSH2_MSG_KEXGSS_INIT,
    .n_callbacks = 1,
    .callbacks = gss_kex_server_callbacks,
    .user = NULL,
};

/** @internal
 * @brief sets up the gssapi kex callbacks
 */
void ssh_server_gss_kex_init(ssh_session session)
{
    /* register the packet callbacks */
    ssh_packet_set_callbacks(session, &ssh_gss_kex_server_callbacks);
}

/** @internal
 * @brief processes a SSH_MSG_KEXGSS_INIT and sends
 * the appropriate SSH_MSG_KEXGSS_COMPLETE
 */
int ssh_server_gss_kex_process_init(ssh_session session, ssh_buffer packet)
{
    struct ssh_crypto_struct *crypto = session->next_crypto;
    ssh_key privkey = NULL;
    enum ssh_digest_e digest = SSH_DIGEST_AUTO;
    ssh_string client_pubkey = NULL;
    ssh_string server_pubkey = NULL;
    int rc;
    gss_buffer_desc input_token = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc output_token = GSS_C_EMPTY_BUFFER;
    ssh_string otoken = NULL;
    ssh_string server_pubkey_blob = NULL;
    OM_uint32 maj_stat, min_stat;
    gss_name_t client_name = GSS_C_NO_NAME;
    OM_uint32 ret_flags = 0;
    gss_buffer_desc mic = GSS_C_EMPTY_BUFFER, msg = GSS_C_EMPTY_BUFFER;
    char *hostname = NULL;
    char err_msg[SSH_ERRNO_MSG_MAX] = {0};

    rc = ssh_buffer_unpack(packet, "S", &otoken);
    if (rc == SSH_ERROR) {
        ssh_set_error(session, SSH_FATAL, "No token in client request");
        goto error;
    }
    input_token.length = ssh_string_len(otoken);
    input_token.value = ssh_string_data(otoken);

    rc = ssh_buffer_unpack(packet, "S", &client_pubkey);
    if (rc == SSH_ERROR) {
        ssh_set_error(session, SSH_FATAL, "No public key in client request");
        goto error;
    }

    switch (crypto->kex_type) {
    case SSH_GSS_KEX_DH_GROUP14_SHA256:
    case SSH_GSS_KEX_DH_GROUP16_SHA512:
        server_pubkey = dh_init(session);
        if (server_pubkey == NULL) {
            ssh_set_error(session, SSH_FATAL, "Could not generate a DH keypair");
            goto error;
        }
        rc = dh_import_peer_key(session, client_pubkey);
        if (rc != SSH_OK) {
            ssh_set_error(session, SSH_FATAL, "Could not import client pubkey");
            goto error;
        }
        rc = ssh_dh_compute_shared_secret(crypto->dh_ctx,
                                          DH_SERVER_KEYPAIR,
                                          DH_CLIENT_KEYPAIR,
                                          &crypto->shared_secret);
        ssh_dh_debug_crypto(crypto);
        break;
    case SSH_GSS_KEX_ECDH_NISTP256_SHA256:
        rc = ssh_ecdh_init(session);
        if (rc != SSH_OK) {
            ssh_set_error(session, SSH_FATAL, "Could not generate an ECDH keypair");
            goto error;
        }
        crypto->ecdh_client_pubkey = ssh_string_copy(client_pubkey);
        server_pubkey = ssh_string_copy(crypto->ecdh_server_pubkey);
        rc = ecdh_build_k(session);
        break;
    case SSH_GSS_KEX_CURVE25519_SHA256:
        rc = ssh_curve25519_init(session);
        if (rc != SSH_OK) {
            ssh_set_error(session, SSH_FATAL, "Could not generate a Curve25519 keypair");
            goto error;
        }
        server_pubkey = ssh_string_new(CURVE25519_PUBKEY_SIZE);
        if (server_pubkey == NULL) {
            ssh_set_error_oom(session);
            goto error;
        }
        rc = ssh_string_fill(server_pubkey,
                             crypto->curve25519_server_pubkey,
                             CURVE25519_PUBKEY_SIZE);
        if (rc != SSH_OK) {
            ssh_set_error(session, SSH_FATAL, "Failed to copy Curve25519 pubkey");
            goto error;
        }
        if (ssh_string_len(client_pubkey) != CURVE25519_PUBKEY_SIZE) {
            ssh_set_error(session,
                          SSH_FATAL,
                          "Incorrect length of received client Curve25519 pubkey");
            goto error;
        }
        memcpy(crypto->curve25519_client_pubkey,
               ssh_string_data(client_pubkey),
               CURVE25519_PUBKEY_SIZE);
        rc = ssh_curve25519_build_k(session);
        break;
    default:
        ssh_set_error(session, SSH_FATAL, "Unsupported GSSAPI KEX method");
        goto error;
    }
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Could not derive shared secret");
        goto error;
    }

    /* Also imports next_crypto->server_pubkey
     * Can give error when using null hostkey */
    ssh_get_key_params(session, &privkey, &digest);

    rc = ssh_make_sessionid(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Could not create a session id");
        goto error;
    }

    if (strcmp(crypto->kex_methods[SSH_HOSTKEYS], "null") != 0) {
        rc =
            ssh_dh_get_next_server_publickey_blob(session, &server_pubkey_blob);
        if (rc != SSH_OK) {
            goto error;
        }
        rc = ssh_buffer_pack(session->out_buffer,
                             "bS",
                             SSH2_MSG_KEXGSS_HOSTKEY,
                             server_pubkey_blob);
        if (rc != SSH_OK) {
            ssh_set_error_oom(session);
            ssh_buffer_reinit(session->out_buffer);
            goto error;
        }

        rc = ssh_packet_send(session);
        if (rc == SSH_ERROR) {
            goto error;
        }
        SSH_LOG(SSH_LOG_DEBUG, "Sent SSH2_MSG_KEXGSS_HOSTKEY");
        SSH_STRING_FREE(server_pubkey_blob);
    }

    rc = ssh_gssapi_init(session);
    if (rc == SSH_ERROR) {
        goto error;
    }

    hostname = ssh_get_local_hostname();
    if (hostname == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Error getting hostname: %s",
                ssh_strerror(errno, err_msg, SSH_ERRNO_MSG_MAX));
        goto error;
    }

    rc = ssh_gssapi_import_name(session->gssapi, hostname);
    SAFE_FREE(hostname);
    if (rc != SSH_OK) {
        goto error;
    }

    maj_stat = gss_acquire_cred(&min_stat,
                                session->gssapi->client.server_name,
                                0,
                                GSS_C_NO_OID_SET,
                                GSS_C_ACCEPT,
                                &session->gssapi->server_creds,
                                NULL,
                                NULL);
    if (maj_stat != GSS_S_COMPLETE) {
        ssh_gssapi_log_error(SSH_LOG_TRACE,
                             "acquiring credentials",
                             maj_stat,
                             min_stat);
        goto error;
    }

    maj_stat = gss_accept_sec_context(&min_stat,
                                      &session->gssapi->ctx,
                                      session->gssapi->server_creds,
                                      &input_token,
                                      GSS_C_NO_CHANNEL_BINDINGS,
                                      &client_name,
                                      NULL /*mech_oid*/,
                                      &output_token,
                                      &ret_flags,
                                      NULL /*time*/,
                                      &session->gssapi->client_creds);
    if (GSS_ERROR(maj_stat)) {
        ssh_gssapi_log_error(SSH_LOG_DEBUG,
                             "accepting token failed",
                             maj_stat,
                             min_stat);
        goto error;
    }
    SSH_STRING_FREE(otoken);
    if (client_name != GSS_C_NO_NAME) {
        session->gssapi->canonic_user = ssh_gssapi_name_to_char(client_name);
    }
    gss_release_name(&min_stat, &client_name);
    if (!(ret_flags & GSS_C_INTEG_FLAG) || !(ret_flags & GSS_C_MUTUAL_FLAG)) {
        SSH_LOG(SSH_LOG_WARN,
                "GSSAPI(accept) integrity and mutual flags were not set");
        gss_release_buffer(&min_stat, &output_token);
        goto error;
    }
    SSH_LOG(SSH_LOG_DEBUG, "token accepted");

    msg.length = session->next_crypto->digest_len;
    msg.value = session->next_crypto->secret_hash;
    maj_stat = gss_get_mic(&min_stat,
                           session->gssapi->ctx,
                           GSS_C_QOP_DEFAULT,
                           &msg,
                           &mic);
    if (GSS_ERROR(maj_stat)) {
        ssh_gssapi_log_error(SSH_LOG_DEBUG,
                             "creating mic failed",
                             maj_stat,
                             min_stat);
        gss_release_buffer(&min_stat, &output_token);
        goto error;
    }

    rc = ssh_buffer_pack(session->out_buffer,
                         "bSdPbdP",
                         SSH2_MSG_KEXGSS_COMPLETE,
                         server_pubkey,
                         mic.length,
                         (size_t)mic.length,
                         mic.value,
                         1,
                         output_token.length,
                         (size_t)output_token.length,
                         output_token.value);
    gss_release_buffer(&min_stat, &output_token);
    gss_release_buffer(&min_stat, &mic);
    if (rc != SSH_OK) {
        ssh_set_error_oom(session);
        ssh_buffer_reinit(session->out_buffer);
        goto error;
    }

    rc = ssh_packet_send(session);
    if (rc == SSH_ERROR) {
        goto error;
    }
    SSH_LOG(SSH_LOG_DEBUG, "Sent SSH2_MSG_KEXGSS_COMPLETE");

    session->dh_handshake_state = DH_STATE_NEWKEYS_SENT;
    /* Send the MSG_NEWKEYS */
    rc = ssh_packet_send_newkeys(session);
    if (rc == SSH_ERROR) {
        goto error;
    }

    ssh_string_free(server_pubkey);
    ssh_string_free(client_pubkey);
    return SSH_OK;
error:
    SSH_STRING_FREE(server_pubkey_blob);
    ssh_string_free(server_pubkey);
    ssh_string_free(client_pubkey);
    session->session_state = SSH_SESSION_STATE_ERROR;
    return SSH_ERROR;
}

/** @internal
 * @brief parse an incoming SSH_MSG_KEXGSS_INIT packet and complete
 *        Diffie-Hellman key exchange
 **/
static SSH_PACKET_CALLBACK(ssh_packet_server_gss_kex_init)
{
    (void)type;
    (void)user;
    SSH_LOG(SSH_LOG_DEBUG, "Received SSH_MSG_KEXGSS_INIT");
    ssh_packet_remove_callbacks(session, &ssh_gss_kex_server_callbacks);
    ssh_server_gss_kex_process_init(session, packet);
    return SSH_PACKET_USED;
}

#endif /* WITH_SERVER */
