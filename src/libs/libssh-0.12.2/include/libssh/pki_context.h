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

#ifndef PKI_CONTEXT_H
#define PKI_CONTEXT_H

#include "libssh/callbacks.h"
#include "libssh/libssh.h"

/**
 * @brief Security key context structure
 *
 * Context structure containing all parameters and callbacks
 * needed for FIDO2/U2F security key operations.
 */
struct ssh_pki_ctx_struct {
    /** @brief Desired RSA modulus size in bits
     *
     * Specified size of RSA keys to generate. If set to 0, defaults to 3072
     * bits. Must be greater than or equal to 1024, as anything below is
     * considered insecure.
     */
    int rsa_key_size;

    /** @brief Security key callbacks
     *
     * Provides enroll/sign/load_resident_keys operations.
     */
    const struct ssh_sk_callbacks_struct *sk_callbacks;

    /** @brief Application identifier string for the security key credential
     *
     * FIDO2 relying party identifier, typically "ssh:user@hostname" format.
     * This is required for all security key operations.
     */
    char *sk_application;

    /** @brief FIDO2 operation flags
     *
     * Bitfield controlling authenticator behavior. Combine with bitwise OR:
     * - SSH_SK_USER_PRESENCE_REQD (0x01): Require user touch
     * - SSH_SK_USER_VERIFICATION_REQD (0x04): Require PIN/biometric
     * - SSH_SK_FORCE_OPERATION (0x10): Override duplicate detection
     * - SSH_SK_RESIDENT_KEY (0x20): Create discoverable credential
     */
    uint8_t sk_flags;

    /** @brief PIN callback for authenticator user verification (optional)
     *
     * Callback invoked to obtain a PIN or perform user verification when
     * SSH_SK_USER_VERIFICATION_REQD is set or the authenticator requires it.
     * If NULL, no interactive PIN retrieval is performed.
     */
    ssh_auth_callback sk_pin_callback;

    /** @brief User supplied pointer passed to callbacks (optional)
     *
     * Generic pointer set by the application and forwarded to
     * interactive callbacks (e.g. PIN callback) to allow applications to
     * carry state context.
     */
    void *sk_userdata;

    /** @brief Custom challenge data for enrollment (optional)
     *
     * Buffer containing challenge data signed by the authenticator.
     * If NULL, a random 32-byte challenge is automatically generated.
     */
    ssh_buffer sk_challenge_buffer;

    /** @brief Options to be passed to the sk_callbacks (optional)
     *
     * NULL-terminated array of sk_option pointers owned by this context.
     */
    struct sk_option **sk_callbacks_options;

    /** @brief The buffer used to store attestation information returned in a
     * key enrollment operation
     */
    ssh_buffer sk_attestation_buffer;
};

/* Internal PKI context functions */
ssh_pki_ctx ssh_pki_ctx_dup(const ssh_pki_ctx context);

#endif /* PKI_CONTEXT_H */
