/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2011-2013 by Aris Adamantiadis
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
#include "libssh/session.h"
#include "libssh/ecdh.h"
#include "libssh/dh.h"
#include "libssh/buffer.h"
#include "libssh/ssh2.h"
#include "libssh/pki.h"
#include "libssh/bignum.h"

#ifdef HAVE_ECDH
#include <openssl/ecdh.h>
#if OPENSSL_VERSION_NUMBER < 0x30000000L
#define NISTP256 NID_X9_62_prime256v1
#define NISTP384 NID_secp384r1
#define NISTP521 NID_secp521r1
#else
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/params.h>
#include <openssl/core_names.h>
#include "libcrypto-compat.h"
#endif /* OPENSSL_VERSION_NUMBER */

/** @internal
 * @brief Map the given key exchange enum value to its curve name.
 */
#if OPENSSL_VERSION_NUMBER < 0x30000000L
static int ecdh_kex_type_to_curve(enum ssh_key_exchange_e kex_type) {
#else
static const char *ecdh_kex_type_to_curve(enum ssh_key_exchange_e kex_type) {
#endif /* OPENSSL_VERSION_NUMBER */
    if (kex_type == SSH_KEX_ECDH_SHA2_NISTP256) {
        return NISTP256;
    } else if (kex_type == SSH_KEX_ECDH_SHA2_NISTP384) {
        return NISTP384;
    } else if (kex_type == SSH_KEX_ECDH_SHA2_NISTP521) {
        return NISTP521;
    }
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    return SSH_ERROR;
#else
    return NULL;
#endif
}

/* @internal
 * @brief Generate ECDH key pair for ecdh key exchange and store it in the
 * session->next_crypto structure
 */
static ssh_string ssh_ecdh_generate(ssh_session session)
{
    ssh_string pubkey_string = NULL;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    const EC_POINT *point = NULL;
    const EC_GROUP *group = NULL;
    EC_KEY *key = NULL;
    int curve;
#else
    EC_POINT *point = NULL;
    EC_GROUP *group = NULL;
    const char *curve = NULL;
    EVP_PKEY *key = NULL;
    OSSL_PARAM *out_params = NULL;
    const OSSL_PARAM *pubkey_param = NULL;
    const void *pubkey = NULL;
    size_t pubkey_len;
    int nid;
    int rc;
#endif /* OPENSSL_VERSION_NUMBER */

    curve = ecdh_kex_type_to_curve(session->next_crypto->kex_type);
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    if (curve == SSH_ERROR) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to get curve name");
        return NULL;
    }

    key = EC_KEY_new_by_curve_name(curve);
#else
    if (curve == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to get curve name");
        return NULL;
    }

    key = EVP_EC_gen(curve);
#endif /* OPENSSL_VERSION_NUMBER */
    if (key == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to generate key");
        return NULL;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    group = EC_KEY_get0_group(key);

    EC_KEY_generate_key(key);

    point = EC_KEY_get0_public_key(key);

    pubkey_string = pki_key_make_ecpoint_string(group, point);
#else
    rc = EVP_PKEY_todata(key, EVP_PKEY_PUBLIC_KEY, &out_params);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to export public key");
        EVP_PKEY_free(key);
        return NULL;
    }

    pubkey_param = OSSL_PARAM_locate_const(out_params, OSSL_PKEY_PARAM_PUB_KEY);
    if (pubkey_param == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to find public key");
        EVP_PKEY_free(key);
        OSSL_PARAM_free(out_params);
        return NULL;
    }

    rc = OSSL_PARAM_get_octet_string_ptr(pubkey_param,
                                         (const void**)&pubkey,
                                         &pubkey_len);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to read public key");
        OSSL_PARAM_free(out_params);
        EVP_PKEY_free(key);
        return NULL;
    }

    /* Convert the data to low-level representation */
    nid = pki_key_ecgroup_name_to_nid(curve);
    group = EC_GROUP_new_by_curve_name_ex(NULL, NULL, nid);
    if (group == NULL) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Could not create group: %s",
                      ERR_error_string(ERR_get_error(), NULL));
        OSSL_PARAM_free(out_params);
        EVP_PKEY_free(key);
        return NULL;
    }
    point = EC_POINT_new(group);
    if (point == NULL) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Could not create point: %s",
                      ERR_error_string(ERR_get_error(), NULL));
        EC_GROUP_free(group);
        OSSL_PARAM_free(out_params);
        EVP_PKEY_free(key);
        return NULL;
    }
    rc = EC_POINT_oct2point(group, point, pubkey, pubkey_len, NULL);
    OSSL_PARAM_free(out_params);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to export public key");
        EC_GROUP_free(group);
        EC_POINT_free(point);
        EVP_PKEY_free(key);
        return NULL;
    }

    pubkey_string = pki_key_make_ecpoint_string(group, point);
    EC_GROUP_free(group);
    EC_POINT_free(point);
#endif /* OPENSSL_VERSION_NUMBER */
    if (pubkey_string == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to convert public key");
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        EC_KEY_free(key);
#else
        EVP_PKEY_free(key);
#endif /* OPENSSL_VERSION_NUMBER */
        return NULL;
    }

    /* Free any previously allocated privkey */
    if (session->next_crypto->ecdh_privkey != NULL) {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        EC_KEY_free(session->next_crypto->ecdh_privkey);
#else
        EVP_PKEY_free(session->next_crypto->ecdh_privkey);
#endif
        session->next_crypto->ecdh_privkey = NULL;
    }

    session->next_crypto->ecdh_privkey = key;
    return pubkey_string;
}

/** @internal
 * @brief Starts ecdh-sha2-nistp256 key exchange
 */
int ssh_client_ecdh_init(ssh_session session)
{
    ssh_string client_pubkey = NULL;
    int rc;

    rc = ssh_buffer_add_u8(session->out_buffer, SSH2_MSG_KEX_ECDH_INIT);
    if (rc < 0) {
        return SSH_ERROR;
    }

    client_pubkey = ssh_ecdh_generate(session);
    if (client_pubkey == NULL) {
        return SSH_ERROR;
    }

    rc = ssh_buffer_add_ssh_string(session->out_buffer, client_pubkey);
    if (rc < 0) {
        ssh_string_free(client_pubkey);
        return SSH_ERROR;
    }

    ssh_string_free(session->next_crypto->ecdh_client_pubkey);
    session->next_crypto->ecdh_client_pubkey = client_pubkey;

    /* register the packet callbacks */
    ssh_packet_set_callbacks(session, &ssh_ecdh_client_callbacks);
    session->dh_handshake_state = DH_STATE_INIT_SENT;

    rc = ssh_packet_send(session);

    return rc;
}

int ecdh_build_k(ssh_session session)
{
  struct ssh_crypto_struct *next_crypto = session->next_crypto;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  const EC_GROUP *group = EC_KEY_get0_group(next_crypto->ecdh_privkey);
  EC_POINT *pubkey = NULL;
  void *buffer = NULL;
  int rc;
  int len = (EC_GROUP_get_degree(group) + 7) / 8;
  bignum_CTX ctx = bignum_ctx_new();
  if (ctx == NULL) {
    return -1;
  }
  pubkey = EC_POINT_new(group);
  if (pubkey == NULL) {
    bignum_ctx_free(ctx);
    return -1;
  }

  if (session->server) {
      rc = EC_POINT_oct2point(group,
                              pubkey,
                              ssh_string_data(next_crypto->ecdh_client_pubkey),
                              ssh_string_len(next_crypto->ecdh_client_pubkey),
                              ctx);
  } else {
      rc = EC_POINT_oct2point(group,
                              pubkey,
                              ssh_string_data(next_crypto->ecdh_server_pubkey),
                              ssh_string_len(next_crypto->ecdh_server_pubkey),
                              ctx);
  }
  bignum_ctx_free(ctx);
  if (rc <= 0) {
      EC_POINT_clear_free(pubkey);
      return -1;
  }

  buffer = malloc(len);
  if (buffer == NULL) {
      EC_POINT_clear_free(pubkey);
      return -1;
  }

  rc = ECDH_compute_key(buffer,
                        len,
                        pubkey,
                        next_crypto->ecdh_privkey,
                        NULL);
  EC_POINT_clear_free(pubkey);
  if (rc <= 0) {
      free(buffer);
      return -1;
  }

  bignum_bin2bn(buffer, len, &next_crypto->shared_secret);
  free(buffer);
#else
  const char *curve = NULL;
  EVP_PKEY *pubkey = NULL;
  void *secret = NULL;
  size_t secret_len;
  int rc;
  ssh_string peer_pubkey = NULL;
  OSSL_PARAM_BLD *param_bld = OSSL_PARAM_BLD_new();
  EVP_PKEY_CTX *dh_ctx = EVP_PKEY_CTX_new_from_pkey(NULL,
                                                    next_crypto->ecdh_privkey,
                                                    NULL);

  if (dh_ctx == NULL || param_bld == NULL) {
      ssh_set_error_oom(session);
      EVP_PKEY_CTX_free(dh_ctx);
      OSSL_PARAM_BLD_free(param_bld);
      return -1;
  }

  rc = EVP_PKEY_derive_init(dh_ctx);
  if (rc != 1) {
      ssh_set_error(session,
                    SSH_FATAL,
                    "Could not init PKEY derive: %s",
                    ERR_error_string(ERR_get_error(), NULL));
      EVP_PKEY_CTX_free(dh_ctx);
      OSSL_PARAM_BLD_free(param_bld);
      return -1;
  }

  if (session->server) {
      peer_pubkey = next_crypto->ecdh_client_pubkey;
  } else {
      peer_pubkey = next_crypto->ecdh_server_pubkey;
  }
  rc = OSSL_PARAM_BLD_push_octet_string(param_bld,
                                        OSSL_PKEY_PARAM_PUB_KEY,
                                        ssh_string_data(peer_pubkey),
                                        ssh_string_len(peer_pubkey));
  if (rc != 1) {
      ssh_set_error(session,
                    SSH_FATAL,
                    "Could not push the pub key: %s",
                    ERR_error_string(ERR_get_error(), NULL));
      EVP_PKEY_CTX_free(dh_ctx);
      OSSL_PARAM_BLD_free(param_bld);
      return -1;
  }
  curve = ecdh_kex_type_to_curve(next_crypto->kex_type);
  rc = OSSL_PARAM_BLD_push_utf8_string(param_bld,
                                       OSSL_PKEY_PARAM_GROUP_NAME,
                                       (char *)curve,
                                       strlen(curve));
  if (rc != 1) {
      ssh_set_error(session,
                    SSH_FATAL,
                    "Could not push the group name: %s",
                    ERR_error_string(ERR_get_error(), NULL));
      EVP_PKEY_CTX_free(dh_ctx);
      OSSL_PARAM_BLD_free(param_bld);
      return -1;
  }

  rc = evp_build_pkey("EC", param_bld, &pubkey, EVP_PKEY_PUBLIC_KEY);
  OSSL_PARAM_BLD_free(param_bld);
  if (rc != SSH_OK) {
      ssh_set_error(session,
                    SSH_FATAL,
                    "Could not build the pkey: %s",
                    ERR_error_string(ERR_get_error(), NULL));
      EVP_PKEY_CTX_free(dh_ctx);
      return -1;
  }

  rc = EVP_PKEY_derive_set_peer(dh_ctx, pubkey);
  EVP_PKEY_free(pubkey);
  if (rc != 1) {
      ssh_set_error(session,
                    SSH_FATAL,
                    "Could not set peer pubkey: %s",
                    ERR_error_string(ERR_get_error(), NULL));
      EVP_PKEY_CTX_free(dh_ctx);
      return -1;
  }

  /* get the max length of the secret */
  rc = EVP_PKEY_derive(dh_ctx, NULL, &secret_len);
  if (rc != 1) {
      ssh_set_error(session,
                    SSH_FATAL,
                    "Could not set peer pubkey: %s",
                    ERR_error_string(ERR_get_error(), NULL));
      EVP_PKEY_CTX_free(dh_ctx);
      return -1;
  }

  secret = malloc(secret_len);
  if (secret == NULL) {
      ssh_set_error_oom(session);
      EVP_PKEY_CTX_free(dh_ctx);
      return -1;
  }

  rc = EVP_PKEY_derive(dh_ctx, secret, &secret_len);
  if (rc != 1) {
      ssh_set_error(session,
                    SSH_FATAL,
                    "Could not derive shared key: %s",
                    ERR_error_string(ERR_get_error(), NULL));
      EVP_PKEY_CTX_free(dh_ctx);
      free(secret);
      return -1;
  }

  EVP_PKEY_CTX_free(dh_ctx);

  bignum_bin2bn(secret, secret_len, &next_crypto->shared_secret);
  free(secret);
#endif /* OPENSSL_VERSION_NUMBER */
  if (next_crypto->shared_secret == NULL) {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
      EC_KEY_free(next_crypto->ecdh_privkey);
#else
      EVP_PKEY_free(next_crypto->ecdh_privkey);
#endif /* OPENSSL_VERSION_NUMBER */
      next_crypto->ecdh_privkey = NULL;
      return -1;
  }
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  EC_KEY_free(next_crypto->ecdh_privkey);
#else
  EVP_PKEY_free(next_crypto->ecdh_privkey);
#endif /* OPENSSL_VERSION_NUMBER */
  next_crypto->ecdh_privkey = NULL;

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("Session server cookie",
                   next_crypto->server_kex.cookie, 16);
    ssh_log_hexdump("Session client cookie",
                   next_crypto->client_kex.cookie, 16);
    ssh_print_bignum("Shared secret key", next_crypto->shared_secret);
#endif /* DEBUG_CRYPTO */

  return 0;
}

#ifdef WITH_SERVER

/** @brief Handle a SSH_MSG_KEXDH_INIT packet (server) and send a
 * SSH_MSG_KEXDH_REPLY
 */
SSH_PACKET_CALLBACK(ssh_packet_server_ecdh_init)
{
    /* ECDH keys */
    ssh_string q_c_string = NULL;
    ssh_string q_s_string = NULL;
    /* SSH host keys (rsa, ed25519 and ecdsa) */
    ssh_key privkey = NULL;
    enum ssh_digest_e digest = SSH_DIGEST_AUTO;
    ssh_string sig_blob = NULL;
    ssh_string pubkey_blob = NULL;
    int rc;
    (void)type;
    (void)user;

    SSH_LOG(SSH_LOG_TRACE, "Processing SSH_MSG_KEXDH_INIT");

    ssh_packet_remove_callbacks(session, &ssh_ecdh_server_callbacks);
    /* Extract the client pubkey from the init packet */
    q_c_string = ssh_buffer_get_ssh_string(packet);
    if (q_c_string == NULL) {
        ssh_set_error(session, SSH_FATAL, "No Q_C ECC point in packet");
        goto error;
    }
    session->next_crypto->ecdh_client_pubkey = q_c_string;

    q_s_string = ssh_ecdh_generate(session);
    if (q_s_string == NULL) {
        goto error;
    }

    session->next_crypto->ecdh_server_pubkey = q_s_string;

    /* build k and session_id */
    rc = ecdh_build_k(session);
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

    sig_blob = ssh_srv_pki_do_sign_sessionid(session, privkey, digest);
    if (sig_blob == NULL) {
        ssh_set_error(session, SSH_FATAL, "Could not sign the session id");
        goto error;
    }

    rc = ssh_dh_get_next_server_publickey_blob(session, &pubkey_blob);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Could not export server public key");
        SSH_STRING_FREE(sig_blob);
        return SSH_ERROR;
    }

    rc = ssh_buffer_pack(session->out_buffer,
                         "bSSS",
                         SSH2_MSG_KEXDH_REPLY,
                         pubkey_blob, /* host's pubkey */
                         q_s_string, /* ecdh public key */
                         sig_blob); /* signature blob */

    SSH_STRING_FREE(sig_blob);
    SSH_STRING_FREE(pubkey_blob);

    if (rc != SSH_OK) {
        ssh_set_error_oom(session);
        goto error;
    }

    SSH_LOG(SSH_LOG_DEBUG, "SSH_MSG_KEXDH_REPLY sent");
    rc = ssh_packet_send(session);
    if (rc == SSH_ERROR) {
        goto error;
    }

    session->dh_handshake_state = DH_STATE_NEWKEYS_SENT;
    /* Send the MSG_NEWKEYS */
    rc = ssh_packet_send_newkeys(session);
    if (rc == SSH_ERROR) {
        goto error;
    }

    return SSH_PACKET_USED;
error:
    ssh_buffer_reinit(session->out_buffer);
    session->session_state = SSH_SESSION_STATE_ERROR;
    return SSH_PACKET_USED;
}

#endif /* WITH_SERVER */

#endif /* HAVE_ECDH */
