/*
 * torture_sk.h - torture library for testing security keys
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2025 Praneeth Sarode <praneethsarode@gmail.com>
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

#ifndef _TORTURE_SK_H
#define _TORTURE_SK_H

#include "config.h"

#define LIBSSH_STATIC

#include "libssh/callbacks.h"
#include "libssh/pki.h"
#include "torture.h"
#include "torture_pki.h"

/**
 * @brief Validate a security key (ssh_key) structure
 *
 * Checks that the provided key is not NULL, matches the expected key type,
 * and other internal fields.
 *
 * @param[in] key           The key to validate
 * @param[in] expected_type The expected key type (e.g., SSH_KEYTYPE_SK_ECDSA)
 * @param[in] private       true if key should be private, false for public
 */
void assert_sk_key_valid(ssh_key key,
                         enum ssh_keytypes_e expected_type,
                         bool private);

/**
 * @brief Validate a security key signature structure
 *
 * Checks that the signature is not NULL, matches the expected key type, and
 * other internal fields. Also verifies that the signature was produced by the
 * given signing key.
 *
 * @param[in] signature     The signature to validate
 * @param[in] expected_type The expected key type (e.g., SSH_KEYTYPE_SK_ECDSA)
 * @param[in] signing_key   The key that should have produced the signature
 * @param[in] data          The signed data buffer
 * @param[in] data_len      Length of the signed data
 */
void assert_sk_signature_valid(ssh_signature signature,
                               enum ssh_keytypes_e expected_type,
                               ssh_key signing_key,
                               const uint8_t *data,
                               size_t data_len);

/**
 * @brief Create and initialize a PKI context configured for security key
 * operations.
 *
 * Parameters:
 * @param[in] application    Application string
 * @param[in] flags          SK flags
 * @param[in] challenge_data Optional challenge bytes (may be NULL)
 * @param[in] challenge_len  Length of challenge_data
 * @param[in] pin_callback   Callback used to obtain the PIN (may be NULL)
 * @param[in] device_path    Optional device path (may be NULL)
 * @param[in] user_id        Optional user_id string (may be NULL)
 * @param[in] sk_callbacks   Pointer to SK callbacks (may be NULL)
 *
 * @return A configured ssh_pki_ctx on success, or NULL on allocation failure.
 */
ssh_pki_ctx
torture_create_sk_pki_ctx(const char *application,
                          uint8_t flags,
                          const void *challenge_data,
                          size_t challenge_len,
                          ssh_auth_callback pin_callback,
                          const char *device_path,
                          const char *user_id,
                          const struct ssh_sk_callbacks_struct *sk_callbacks);

/**
 * @brief Validate a security key enrollment response structure
 *
 * Validates that an sk_enroll_response contains valid data from a FIDO2
 * enrollment operation, including public key, key handle, signature,
 * attestation certificate, and authenticator data.
 *
 * @param[in] response The enrollment response to validate
 * @param[in] flags    The expected flags that should match the response flags
 */
void assert_sk_enroll_response(struct sk_enroll_response *response, int flags);

/**
 * @brief Validate a security key sign response structure
 *
 * Validates that an sk_sign_response contains valid signature data from
 * a FIDO2 sign operation.
 *
 * @param[in] response The sign response to validate
 * @param[in] key_type The key type (e.g., SSH_SK_ECDSA, SSH_SK_ED25519)
 */
void assert_sk_sign_response(struct sk_sign_response *response,
                             enum ssh_keytypes_e key_type);

/**
 * @brief Validate a security key resident key structure
 *
 * Validates that an sk_resident_key contains valid data including application
 * identifier, user ID, public key, and key handle.
 *
 * @param[in] resident_key The resident key to validate
 */
void assert_sk_resident_key(struct sk_resident_key *resident_key);

/**
 * @brief Get security key PIN from environment variable
 *
 * Reads the TORTURE_SK_PIN environment variable and returns its value.
 *
 * @return Pointer to PIN string if set and non-empty, NULL otherwise
 */
const char *torture_get_sk_pin(void);

/**
 * @brief Get dummy security key callbacks for testing
 *
 * Returns dummy security key callbacks from openssh's sk-dummy
 * if available, or NULL if not.
 *
 * @return Pointer to ssh_sk_callbacks_struct or NULL if unavailable.
 *
 */
const struct ssh_sk_callbacks_struct *torture_get_sk_dummy_callbacks(void);

/**
 * @brief Get security key callbacks for testing
 *
 * Returns the default sk callbacks if TORTURE_SK_USBHID is set,
 * otherwise returns dummy callbacks from openssh sk-dummy, or NULL if
 * unavailable.
 *
 * @return Pointer to ssh_sk_callbacks_struct or NULL if unavailable
 */
const struct ssh_sk_callbacks_struct *torture_get_sk_callbacks(void);

/**
 * @brief Check if using sk-dummy callbacks for testing
 *
 * @return true if using sk-dummy callbacks, false otherwise
 */
bool torture_sk_is_using_sk_dummy(void);

#endif /* _TORTURE_SK_H */
