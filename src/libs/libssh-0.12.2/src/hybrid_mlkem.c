/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2025 by Red Hat, Inc.
 *
 * Author: Sahana Prasad <sahana@redhat.com>
 * Author: Pavol Žáčik <pzacik@redhat.com>
 * Author: Claude (Anthropic)
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

#include "libssh/buffer.h"
#include "libssh/hybrid_mlkem.h"
#include "libssh/pki.h"
#include "libssh/ssh2.h"

/* sorry, this needs to come last to avoid header dependency issues */
#include "libssh/bignum.h"

static SSH_PACKET_CALLBACK(ssh_packet_client_hybrid_mlkem_reply);

static ssh_packet_callback dh_client_callbacks[] = {
    ssh_packet_client_hybrid_mlkem_reply,
};

static struct ssh_packet_callbacks_struct ssh_hybrid_mlkem_client_callbacks = {
    .start = SSH2_MSG_KEX_HYBRID_REPLY,
    .n_callbacks = 1,
    .callbacks = dh_client_callbacks,
    .user = NULL,
};

static ssh_string derive_curve25519_secret(ssh_session session)
{
    ssh_string secret = NULL;
    int rc;

    secret = ssh_string_new(CURVE25519_PUBKEY_SIZE);
    if (secret == NULL) {
        ssh_set_error_oom(session);
        return NULL;
    }

    rc = ssh_curve25519_create_k(session, ssh_string_data(secret));
    if (rc != SSH_OK) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Curve25519 secret derivation failed");
        ssh_string_free(secret);
        return NULL;
    }

    return secret;
}

static ssh_string derive_nist_curve_secret(ssh_session session,
                                           size_t secret_size)
{
    struct ssh_crypto_struct *crypto = session->next_crypto;
    ssh_string secret = NULL;
    int rc;

    rc = ecdh_build_k(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "ECDH secret derivation failed");
        return NULL;
    }

    secret = ssh_make_padded_bignum_string(crypto->shared_secret, secret_size);
    if (secret == NULL) {
        ssh_set_error(session, SSH_FATAL, "Failed to encode the shared secret");
    }

    bignum_safe_free(crypto->shared_secret);

    return secret;
}

static ssh_string derive_ecdh_secret(ssh_session session)
{
    ssh_string secret = NULL;

    switch (session->next_crypto->kex_type) {
    case SSH_KEX_MLKEM768X25519_SHA256:
        secret = derive_curve25519_secret(session);
        break;
    case SSH_KEX_MLKEM768NISTP256_SHA256:
        secret = derive_nist_curve_secret(session, NISTP256_SHARED_SECRET_SIZE);
        break;
#ifdef HAVE_MLKEM1024
    case SSH_KEX_MLKEM1024NISTP384_SHA384:
        secret = derive_nist_curve_secret(session, NISTP384_SHARED_SECRET_SIZE);
        break;
#endif
    default:
        ssh_set_error(session, SSH_FATAL, "Unsupported KEX type");
        return NULL;
    }

    return secret;
}

static int derive_hybrid_secret(ssh_session session,
                                ssh_mlkem_shared_secret mlkem_shared_secret,
                                ssh_string ecdh_shared_secret)
{
    struct ssh_crypto_struct *crypto = session->next_crypto;
    ssh_buffer combined_secret = NULL;
    int (*digest)(const unsigned char *, size_t, unsigned char *) = NULL;
    size_t digest_len;
    int rc, ret = SSH_ERROR;

    switch (crypto->kex_type) {
    case SSH_KEX_MLKEM768X25519_SHA256:
    case SSH_KEX_MLKEM768NISTP256_SHA256:
        digest = sha256;
        digest_len = SHA256_DIGEST_LEN;
        break;
#ifdef HAVE_MLKEM1024
    case SSH_KEX_MLKEM1024NISTP384_SHA384:
        digest = sha384;
        digest_len = SHA384_DIGEST_LEN;
        break;
#endif
    default:
        ssh_set_error(session, SSH_FATAL, "Unsupported KEX type");
        goto cleanup;
    }

    /* Concatenate the two shared secrets */
    combined_secret = ssh_buffer_new();
    if (combined_secret == NULL) {
        ssh_set_error_oom(session);
        goto cleanup;
    }
    ssh_buffer_set_secure(combined_secret);

    rc = ssh_buffer_pack(combined_secret,
                         "PP",
                         (size_t)MLKEM_SHARED_SECRET_SIZE,
                         mlkem_shared_secret,
                         ssh_string_len(ecdh_shared_secret),
                         ssh_string_data(ecdh_shared_secret));
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to concatenate shared secrets");
        goto cleanup;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("Concatenated shared secrets",
                    ssh_buffer_get(combined_secret),
                    ssh_buffer_get_len(combined_secret));
#endif

    /* Store the hashed combined shared secrets */
    ssh_string_burn(crypto->hybrid_shared_secret);
    ssh_string_free(crypto->hybrid_shared_secret);
    crypto->hybrid_shared_secret = ssh_string_new(digest_len);
    if (crypto->hybrid_shared_secret == NULL) {
        ssh_set_error_oom(session);
        goto cleanup;
    }

    rc = digest(ssh_buffer_get(combined_secret),
                ssh_buffer_get_len(combined_secret),
                ssh_string_data(crypto->hybrid_shared_secret));
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Shared secret hashing failed");
        goto cleanup;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("Hybrid shared secret",
                    ssh_string_data(crypto->hybrid_shared_secret),
                    digest_len);
#endif

    ret = SSH_OK;

cleanup:
    ssh_buffer_free(combined_secret);
    return ret;
}

int ssh_client_hybrid_mlkem_init(ssh_session session)
{
    struct ssh_crypto_struct *crypto = session->next_crypto;
    ssh_buffer client_init_buffer = NULL;
    int rc, ret = SSH_ERROR;

    SSH_LOG(SSH_LOG_TRACE, "Initializing hybrid ML-KEM key exchange");

    /* Prepare a buffer to concatenate ML-KEM + ECDH public keys */
    client_init_buffer = ssh_buffer_new();
    if (client_init_buffer == NULL) {
        ssh_set_error_oom(session);
        goto cleanup;
    }

    /* Generate an ML-KEM keypair */
    rc = ssh_mlkem_init(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to generate an ML-KEM keypair");
        goto cleanup;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("ML-KEM client pubkey",
                    ssh_string_data(crypto->mlkem_client_pubkey),
                    ssh_string_len(crypto->mlkem_client_pubkey));
#endif

    /* Generate an ECDH keypair and concatenate the public keys  */
    switch (crypto->kex_type) {
    case SSH_KEX_MLKEM768X25519_SHA256:
        rc = ssh_curve25519_init(session);
        if (rc != SSH_OK) {
            ssh_set_error(session,
                          SSH_FATAL,
                          "Failed to generate a Curve25519 ECDH keypair");
            goto cleanup;
        }
#ifdef DEBUG_CRYPTO
        ssh_log_hexdump("Curve25519 client pubkey",
                        crypto->curve25519_client_pubkey,
                        CURVE25519_PUBKEY_SIZE);
#endif
        rc = ssh_buffer_pack(client_init_buffer,
                             "PP",
                             ssh_string_len(crypto->mlkem_client_pubkey),
                             ssh_string_data(crypto->mlkem_client_pubkey),
                             (size_t)CURVE25519_PUBKEY_SIZE,
                             crypto->curve25519_client_pubkey);
        break;
    case SSH_KEX_MLKEM768NISTP256_SHA256:
#ifdef HAVE_MLKEM1024
    case SSH_KEX_MLKEM1024NISTP384_SHA384:
#endif
        rc = ssh_ecdh_init(session);
        if (rc != SSH_OK) {
            ssh_set_error(session,
                          SSH_FATAL,
                          "Failed to generate a NIST-curve ECDH keypair");
            goto cleanup;
        }
#ifdef DEBUG_CRYPTO
        ssh_log_hexdump("ECDH client pubkey",
                        ssh_string_data(crypto->ecdh_client_pubkey),
                        ssh_string_len(crypto->ecdh_client_pubkey));
#endif
        rc = ssh_buffer_pack(client_init_buffer,
                             "PP",
                             ssh_string_len(crypto->mlkem_client_pubkey),
                             ssh_string_data(crypto->mlkem_client_pubkey),
                             ssh_string_len(crypto->ecdh_client_pubkey),
                             ssh_string_data(crypto->ecdh_client_pubkey));

        break;
    default:
        ssh_set_error(session, SSH_FATAL, "Unsupported KEX type");
        goto cleanup;
    }
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to construct client init buffer");
        goto cleanup;
    }

    /* Convert the client init buffer to an SSH string */
    ssh_string_free(crypto->hybrid_client_init);
    crypto->hybrid_client_init = ssh_string_new(ssh_buffer_get_len(client_init_buffer));
    if (crypto->hybrid_client_init == NULL) {
        ssh_set_error_oom(session);
        goto cleanup;
    }

    rc = ssh_string_fill(crypto->hybrid_client_init,
                         ssh_buffer_get(client_init_buffer),
                         ssh_buffer_get_len(client_init_buffer));
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to convert client init to string");
        goto cleanup;
    }

    rc = ssh_buffer_pack(session->out_buffer,
                         "bS",
                         SSH2_MSG_KEX_HYBRID_INIT,
                         crypto->hybrid_client_init);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to construct SSH_MSG_KEX_HYBRID_INIT");
        goto cleanup;
    }

    ssh_packet_set_callbacks(session, &ssh_hybrid_mlkem_client_callbacks);
    session->dh_handshake_state = DH_STATE_INIT_SENT;

    rc = ssh_packet_send(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to send SSH_MSG_KEX_HYBRID_INIT");
        goto cleanup;
    }

    ret = SSH_OK;

cleanup:
    ssh_buffer_free(client_init_buffer);
    return ret;
}

static SSH_PACKET_CALLBACK(ssh_packet_client_hybrid_mlkem_reply)
{
    struct ssh_crypto_struct *crypto = session->next_crypto;
    const struct mlkem_type_info *mlkem_info = NULL;
    ssh_string pubkey_blob = NULL;
    ssh_string signature = NULL;
    ssh_mlkem_shared_secret mlkem_shared_secret;
    ssh_string ecdh_shared_secret = NULL;
    ssh_buffer server_reply_buffer = NULL;
    size_t read_len;
    size_t ecdh_server_pubkey_size;
    int rc;
    (void)type;
    (void)user;

    SSH_LOG(SSH_LOG_TRACE, "Received ML-KEM hybrid server reply");

    ssh_client_hybrid_mlkem_remove_callbacks(session);

    mlkem_info = kex_type_to_mlkem_info(crypto->kex_type);
    if (mlkem_info == NULL) {
        ssh_set_error(session, SSH_FATAL, "Unknown ML-KEM type");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    pubkey_blob = ssh_buffer_get_ssh_string(packet);
    if (pubkey_blob == NULL) {
        ssh_set_error(session, SSH_FATAL, "No public key in packet");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    rc = ssh_dh_import_next_pubkey_blob(session, pubkey_blob);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to import public key");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Get server reply containing ML-KEM ciphertext + ECDH public key */
    ssh_string_free(crypto->hybrid_server_reply);
    crypto->hybrid_server_reply = ssh_buffer_get_ssh_string(packet);
    if (crypto->hybrid_server_reply == NULL) {
        ssh_set_error(session, SSH_FATAL, "No server reply in packet");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    server_reply_buffer = ssh_buffer_new();
    if (server_reply_buffer == NULL) {
        ssh_set_error_oom(session);
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    rc = ssh_buffer_add_data(server_reply_buffer,
                             ssh_string_data(crypto->hybrid_server_reply),
                             ssh_string_len(crypto->hybrid_server_reply));
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to pack server reply to a buffer");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Store ML-KEM ciphertext for decapsulation and sessionid calculation */
    ssh_string_free(crypto->mlkem_ciphertext);
    crypto->mlkem_ciphertext = ssh_string_new(mlkem_info->ciphertext_size);
    if (crypto->mlkem_ciphertext == NULL) {
        ssh_set_error_oom(session);
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    read_len = ssh_buffer_get_data(server_reply_buffer,
                                   ssh_string_data(crypto->mlkem_ciphertext),
                                   mlkem_info->ciphertext_size);
    if (read_len != mlkem_info->ciphertext_size) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Could not read ML-KEM ciphertext from "
                      "the server reply buffer, buffer too short");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("ML-KEM ciphertext",
                    ssh_string_data(crypto->mlkem_ciphertext),
                    ssh_string_len(crypto->mlkem_ciphertext));
#endif

    /* Extract server ECDH public key */
    switch (crypto->kex_type) {
    case SSH_KEX_MLKEM768X25519_SHA256:
        read_len = ssh_buffer_get_data(server_reply_buffer,
                                       crypto->curve25519_server_pubkey,
                                       CURVE25519_PUBKEY_SIZE);
        if (read_len != CURVE25519_PUBKEY_SIZE) {
            ssh_set_error(session,
                          SSH_FATAL,
                          "Could not read Curve25519 pubkey from "
                          "the server reply buffer, buffer too short");
            session->session_state = SSH_SESSION_STATE_ERROR;
            goto cleanup;
        }
        if (ssh_buffer_get_len(server_reply_buffer) > 0) {
            ssh_set_error(session,
                          SSH_FATAL,
                          "Unrecognized data in the server reply buffer");
            session->session_state = SSH_SESSION_STATE_ERROR;
            goto cleanup;
        }
#ifdef DEBUG_CRYPTO
        ssh_log_hexdump("Curve25519 server pubkey",
                        crypto->curve25519_server_pubkey,
                        CURVE25519_PUBKEY_SIZE);
#endif
        break;
    case SSH_KEX_MLKEM768NISTP256_SHA256:
#ifdef HAVE_MLKEM1024
    case SSH_KEX_MLKEM1024NISTP384_SHA384:
#endif
        ecdh_server_pubkey_size = ssh_buffer_get_len(server_reply_buffer);
        ssh_string_free(crypto->ecdh_server_pubkey);
        crypto->ecdh_server_pubkey = ssh_string_new(ecdh_server_pubkey_size);
        if (crypto->ecdh_server_pubkey == NULL) {
            ssh_set_error_oom(session);
            session->session_state = SSH_SESSION_STATE_ERROR;
            goto cleanup;
        }
        ssh_buffer_get_data(server_reply_buffer,
                            ssh_string_data(crypto->ecdh_server_pubkey),
                            ecdh_server_pubkey_size);
#ifdef DEBUG_CRYPTO
        ssh_log_hexdump("ECDH server pubkey",
                        ssh_string_data(crypto->ecdh_server_pubkey),
                        ssh_string_len(crypto->ecdh_server_pubkey));
#endif
        break;
    default:
        ssh_set_error(session, SSH_FATAL, "Unsupported KEX type");
        goto cleanup;
    }

    /* Decapsulate ML-KEM shared secret */
    rc = ssh_mlkem_decapsulate(session, mlkem_shared_secret);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "ML-KEM decapsulation failed");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("ML-KEM shared secret",
                    mlkem_shared_secret,
                    MLKEM_SHARED_SECRET_SIZE);
#endif

    /* Derive the classical ECDH shared secret */
    ecdh_shared_secret = derive_ecdh_secret(session);
    if (ecdh_shared_secret == NULL) {
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("ECDH shared secret",
                    ssh_string_data(ecdh_shared_secret),
                    ssh_string_len(ecdh_shared_secret));
#endif

    /* Derive the final shared secret */
    rc = derive_hybrid_secret(session, mlkem_shared_secret, ecdh_shared_secret);
    if (rc != SSH_OK) {
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Get signature for verification */
    signature = ssh_buffer_get_ssh_string(packet);
    if (signature == NULL) {
        ssh_set_error(session, SSH_FATAL, "No signature in packet");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }
    crypto->dh_server_signature = signature;

    /* Send the MSG_NEWKEYS */
    rc = ssh_packet_send_newkeys(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to send SSH_MSG_NEWKEYS");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }
    session->dh_handshake_state = DH_STATE_NEWKEYS_SENT;

cleanup:
    ssh_burn(mlkem_shared_secret, sizeof(mlkem_shared_secret));
    ssh_string_burn(ecdh_shared_secret);
    ssh_string_free(ecdh_shared_secret);
    ssh_string_free(pubkey_blob);
    ssh_buffer_free(server_reply_buffer);
    return SSH_PACKET_USED;
}

void ssh_client_hybrid_mlkem_remove_callbacks(ssh_session session)
{
    ssh_packet_remove_callbacks(session, &ssh_hybrid_mlkem_client_callbacks);
}

#ifdef WITH_SERVER

static SSH_PACKET_CALLBACK(ssh_packet_server_hybrid_mlkem_init);

static ssh_packet_callback dh_server_callbacks[] = {
    ssh_packet_server_hybrid_mlkem_init,
};

static struct ssh_packet_callbacks_struct ssh_hybrid_mlkem_server_callbacks = {
    .start = SSH2_MSG_KEX_HYBRID_INIT,
    .n_callbacks = 1,
    .callbacks = dh_server_callbacks,
    .user = NULL,
};

static SSH_PACKET_CALLBACK(ssh_packet_server_hybrid_mlkem_init)
{
    struct ssh_crypto_struct *crypto = session->next_crypto;
    const struct mlkem_type_info *mlkem_info = NULL;
    ssh_string ecdh_shared_secret = NULL;
    ssh_mlkem_shared_secret mlkem_shared_secret;
    ssh_buffer server_reply_buffer = NULL;
    ssh_buffer client_init_buffer = NULL;
    ssh_key privkey = NULL;
    enum ssh_digest_e digest = SSH_DIGEST_AUTO;
    ssh_string signature = NULL;
    ssh_string pubkey_blob = NULL;
    size_t ecdh_client_pubkey_size;
    size_t read_len;
    int rc;
    (void)type;
    (void)user;

    SSH_LOG(SSH_LOG_TRACE, "Received ML-KEM hybrid client init");

    ssh_packet_remove_callbacks(session, &ssh_hybrid_mlkem_server_callbacks);

    mlkem_info = kex_type_to_mlkem_info(crypto->kex_type);
    if (mlkem_info == NULL) {
        ssh_set_error(session, SSH_FATAL, "Unknown ML-KEM type");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Generate an ECDH keypair  */
    switch (crypto->kex_type) {
    case SSH_KEX_MLKEM768X25519_SHA256:
        rc = ssh_curve25519_init(session);
        if (rc != SSH_OK) {
            ssh_set_error(session,
                          SSH_FATAL,
                          "Failed to generate a Curve25519 ECDH keypair");
            goto cleanup;
        }
#ifdef DEBUG_CRYPTO
        ssh_log_hexdump("Curve25519 server pubkey",
                        crypto->curve25519_server_pubkey,
                        CURVE25519_PUBKEY_SIZE);
#endif
        break;
    case SSH_KEX_MLKEM768NISTP256_SHA256:
#ifdef HAVE_MLKEM1024
    case SSH_KEX_MLKEM1024NISTP384_SHA384:
#endif
        rc = ssh_ecdh_init(session);
        if (rc != SSH_OK) {
            ssh_set_error(session,
                          SSH_FATAL,
                          "Failed to generate a NIST-curve ECDH keypair");
            goto cleanup;
        }
#ifdef DEBUG_CRYPTO
        ssh_log_hexdump("ECDH server pubkey",
                        ssh_string_data(crypto->ecdh_server_pubkey),
                        ssh_string_len(crypto->ecdh_server_pubkey));
#endif
        break;
    default:
        ssh_set_error(session, SSH_FATAL, "Unsupported KEX type");
        goto cleanup;
    }

    /* Get client init: ML-KEM public key + ECDH public key */
    ssh_string_free(crypto->hybrid_client_init);
    crypto->hybrid_client_init = ssh_buffer_get_ssh_string(packet);
    if (crypto->hybrid_client_init == NULL) {
        ssh_set_error(session, SSH_FATAL, "No client public keys in packet");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    client_init_buffer = ssh_buffer_new();
    if (client_init_buffer == NULL) {
        ssh_set_error_oom(session);
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    rc = ssh_buffer_add_data(client_init_buffer,
                             ssh_string_data(crypto->hybrid_client_init),
                             ssh_string_len(crypto->hybrid_client_init));
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to pack client init to a buffer");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Extract client ML-KEM public key */
    ssh_string_free(crypto->mlkem_client_pubkey);
    crypto->mlkem_client_pubkey = ssh_string_new(mlkem_info->pubkey_size);
    if (crypto->mlkem_client_pubkey == NULL) {
        ssh_set_error_oom(session);
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    read_len = ssh_buffer_get_data(client_init_buffer,
                                   ssh_string_data(crypto->mlkem_client_pubkey),
                                   mlkem_info->pubkey_size);
    if (read_len != mlkem_info->pubkey_size) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Could not read ML-KEM pubkey from "
                      "the client init buffer, buffer too short");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("ML-KEM client pubkey",
                    ssh_string_data(crypto->mlkem_client_pubkey),
                    ssh_string_len(crypto->mlkem_client_pubkey));
#endif

    /* Extract client ECDH public key */
    switch (crypto->kex_type) {
    case SSH_KEX_MLKEM768X25519_SHA256:
        read_len = ssh_buffer_get_data(client_init_buffer,
                                       crypto->curve25519_client_pubkey,
                                       CURVE25519_PUBKEY_SIZE);
        if (read_len != CURVE25519_PUBKEY_SIZE) {
            ssh_set_error(session,
                          SSH_FATAL,
                          "Could not read Curve25519 pubkey from "
                          "the client init buffer, buffer too short");
            session->session_state = SSH_SESSION_STATE_ERROR;
            goto cleanup;
        }
        if (ssh_buffer_get_len(client_init_buffer) > 0) {
            ssh_set_error(session,
                          SSH_FATAL,
                          "Unrecognized data in the client init buffer");
            session->session_state = SSH_SESSION_STATE_ERROR;
            goto cleanup;
        }
#ifdef DEBUG_CRYPTO
        ssh_log_hexdump("Curve25519 client pubkey",
                        crypto->curve25519_client_pubkey,
                        CURVE25519_PUBKEY_SIZE);
#endif
        break;
    case SSH_KEX_MLKEM768NISTP256_SHA256:
#ifdef HAVE_MLKEM1024
    case SSH_KEX_MLKEM1024NISTP384_SHA384:
#endif
        ecdh_client_pubkey_size = ssh_buffer_get_len(client_init_buffer);
        ssh_string_free(crypto->ecdh_client_pubkey);
        crypto->ecdh_client_pubkey = ssh_string_new(ecdh_client_pubkey_size);
        if (crypto->ecdh_client_pubkey == NULL) {
            ssh_set_error_oom(session);
            session->session_state = SSH_SESSION_STATE_ERROR;
            goto cleanup;
        }
        ssh_buffer_get_data(client_init_buffer,
                            ssh_string_data(crypto->ecdh_client_pubkey),
                            ecdh_client_pubkey_size);
#ifdef DEBUG_CRYPTO
        ssh_log_hexdump("ECDH client pubkey",
                        ssh_string_data(crypto->ecdh_client_pubkey),
                        ssh_string_len(crypto->ecdh_client_pubkey));
#endif
        break;
    default:
        ssh_set_error(session, SSH_FATAL, "Unsupported KEX type");
        goto cleanup;
    }

    /* Encapsulate an ML-KEM shared secret using client's ML-KEM public key */
    rc = ssh_mlkem_encapsulate(session, mlkem_shared_secret);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "ML-KEM encapsulation failed");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("ML-KEM shared secret",
                    mlkem_shared_secret,
                    MLKEM_SHARED_SECRET_SIZE);
    ssh_log_hexdump("ML-KEM ciphertext",
                    ssh_string_data(crypto->mlkem_ciphertext),
                    ssh_string_len(crypto->mlkem_ciphertext));
#endif

    /* Derive the classical ECDH shared secret */
    ecdh_shared_secret = derive_ecdh_secret(session);
    if (ecdh_shared_secret == NULL) {
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("ECDH shared secret",
                    ssh_string_data(ecdh_shared_secret),
                    ssh_string_len(ecdh_shared_secret));
#endif

    /* Derive the final shared secret */
    rc = derive_hybrid_secret(session, mlkem_shared_secret, ecdh_shared_secret);
    if (rc != SSH_OK) {
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Create server reply: ML-KEM ciphertext + ECDH public key */
    server_reply_buffer = ssh_buffer_new();
    if (server_reply_buffer == NULL) {
        ssh_set_error_oom(session);
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    switch (crypto->kex_type) {
    case SSH_KEX_MLKEM768X25519_SHA256:
        rc = ssh_buffer_pack(server_reply_buffer,
                             "PP",
                             ssh_string_len(crypto->mlkem_ciphertext),
                             ssh_string_data(crypto->mlkem_ciphertext),
                             (size_t)CURVE25519_PUBKEY_SIZE,
                             crypto->curve25519_server_pubkey);
        break;
    case SSH_KEX_MLKEM768NISTP256_SHA256:
#ifdef HAVE_MLKEM1024
    case SSH_KEX_MLKEM1024NISTP384_SHA384:
#endif
        rc = ssh_buffer_pack(server_reply_buffer,
                             "PP",
                             ssh_string_len(crypto->mlkem_ciphertext),
                             ssh_string_data(crypto->mlkem_ciphertext),
                             ssh_string_len(crypto->ecdh_server_pubkey),
                             ssh_string_data(crypto->ecdh_server_pubkey));
        break;
    default:
        ssh_set_error(session, SSH_FATAL, "Unsupported KEX type");
        goto cleanup;
    }
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to construct server reply buffer");
        goto cleanup;
    }

    /* Convert the reply buffer to an SSH string for sending */
    ssh_string_free(crypto->hybrid_server_reply);
    crypto->hybrid_server_reply = ssh_string_new(ssh_buffer_get_len(server_reply_buffer));
    if (crypto->hybrid_server_reply == NULL) {
        ssh_set_error_oom(session);
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    rc = ssh_string_fill(crypto->hybrid_server_reply,
                         ssh_buffer_get(server_reply_buffer),
                         ssh_buffer_get_len(server_reply_buffer));
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to convert reply buffer to string");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Add MSG_KEX_ECDH_REPLY header */
    rc = ssh_buffer_add_u8(session->out_buffer, SSH2_MSG_KEX_HYBRID_REPLY);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to add MSG_KEX_HYBRID_REPLY to buffer");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Get server host key */
    rc = ssh_get_key_params(session, &privkey, &digest);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Could not get server key params");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Build session ID */
    rc = ssh_make_sessionid(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Could not create a session id");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    rc = ssh_dh_get_next_server_publickey_blob(session, &pubkey_blob);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Could not export server public key");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Add server public key to output */
    rc = ssh_buffer_add_ssh_string(session->out_buffer, pubkey_blob);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to add server hostkey to buffer");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Add server reply */
    rc = ssh_buffer_add_ssh_string(session->out_buffer, crypto->hybrid_server_reply);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to add server reply to buffer");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Sign the exchange hash */
    signature = ssh_srv_pki_do_sign_sessionid(session, privkey, digest);
    if (signature == NULL) {
        ssh_set_error(session, SSH_FATAL, "Could not sign the session id");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Add signature */
    rc = ssh_buffer_add_ssh_string(session->out_buffer, signature);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to add signature to buffer");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    rc = ssh_packet_send(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to send SSH_MSG_KEX_ECDH_REPLY");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }

    /* Send the MSG_NEWKEYS */
    rc = ssh_packet_send_newkeys(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Failed to send SSH_MSG_NEWKEYS");
        session->session_state = SSH_SESSION_STATE_ERROR;
        goto cleanup;
    }
    session->dh_handshake_state = DH_STATE_NEWKEYS_SENT;

cleanup:
    ssh_burn(mlkem_shared_secret, sizeof(mlkem_shared_secret));
    ssh_string_burn(ecdh_shared_secret);
    ssh_string_free(ecdh_shared_secret);
    ssh_string_free(pubkey_blob);
    ssh_string_free(signature);
    ssh_buffer_free(client_init_buffer);
    ssh_buffer_free(server_reply_buffer);
    return SSH_PACKET_USED;
}

void ssh_server_hybrid_mlkem_init(ssh_session session)
{
    SSH_LOG(SSH_LOG_TRACE, "Setting up ML-KEM hybrid server callbacks");
    ssh_packet_set_callbacks(session, &ssh_hybrid_mlkem_server_callbacks);
}

#endif /* WITH_SERVER */
