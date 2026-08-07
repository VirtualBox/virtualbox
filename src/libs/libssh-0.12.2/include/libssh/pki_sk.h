/*
 * This file is part of the SSH Library
 *
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

#ifndef PKI_SK_H
#define PKI_SK_H

#include "libssh/libssh.h"
#include "libssh/pki.h"

#include <stdint.h>

#define SSH_SK_MAX_USER_ID_LEN 64

/**
 * @brief Enroll a new security key using a U2F/FIDO2 authenticator
 *
 * Creates a new security key credential configured according to the parameters
 * in the PKI context. This function handles key enrollment for both ECDSA and
 * Ed25519 algorithms, generates appropriate challenges, and returns the
 * enrolled key with optional attestation data.
 *
 * The PKI context must be configured with appropriate security key parameters
 * using ssh_pki_ctx_options_set() before calling this function. Required
 * options include SSH_PKI_OPTION_SK_APPLICATION, SSH_PKI_OPTION_SK_USER_ID, and
 * SSH_PKI_OPTION_SK_CALLBACKS.
 *
 * @param[in] context The PKI context containing security key configuration and
 *                    parameters
 * @param[in] key_type The type of key to enroll (SSH_KEYTYPE_SK_ECDSA or
 *                     SSH_KEYTYPE_SK_ED25519)
 * @param[out] enrolled_key_result Pointer to store the enrolled ssh_key
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 *
 * @see ssh_pki_ctx_new()
 * @see ssh_pki_ctx_options_set()
 * @see ssh_pki_ctx_get_sk_attestation_buffer()
 */
int pki_sk_enroll_key(ssh_pki_ctx context,
                      enum ssh_keytypes_e key_type,
                      ssh_key *enrolled_key_result);

/**
 * @brief Sign arbitrary data using a security key and a PKI context
 *
 * This function performs signing operations configured according to the
 * parameters in the PKI context and returns a properly formatted
 * ssh_signature. The caller must free the signature when it is no longer
 * needed.
 *
 * The PKI context should be configured with appropriate security key parameters
 * using ssh_pki_ctx_options_set() before calling this function. The security
 * key must have been previously enrolled or loaded.
 *
 * @param[in] context The PKI context containing security key configuration and
 *                    parameters
 * @param[in] key The security key to use for signing
 * @param[in] data The data to sign
 * @param[in] data_len Length of data to sign
 *
 * @return A valid ssh_signature on success, NULL on failure
 *
 * @see ssh_pki_ctx_new()
 * @see ssh_pki_ctx_options_set()
 * @see pki_sk_enroll_key()
 * @see ssh_signature_free()
 */
ssh_signature pki_sk_do_sign(ssh_pki_ctx context,
                             const ssh_key key,
                             const uint8_t *data,
                             size_t data_len);

#endif /* PKI_SK_H */
