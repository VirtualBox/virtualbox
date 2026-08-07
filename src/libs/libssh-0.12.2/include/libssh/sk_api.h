/*
 * Copyright (c) 2019 Google LLC
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file is a copy of the OpenSSH project's sk-api.h file pulled from
 * https://github.com/openssh/openssh-portable/commit/a9cbe10da2be5be76755af0cea029db0f9c1f263
 * with only the flags, algorithms, error codes, and struct definitions. The
 * function declarations and other OpenSSH-specific code have been removed.
 */

#ifndef SK_API_H
#define SK_API_H 1

#include <stddef.h>
#include <stdint.h>

/* FIDO2/U2F Operation Flags */

/** Requires user presence confirmation (tap/touch) */
#ifndef SSH_SK_USER_PRESENCE_REQD
#define SSH_SK_USER_PRESENCE_REQD 0x01
#endif

/** Requires user verification (PIN/biometric) - FIDO2 only */
#ifndef SSH_SK_USER_VERIFICATION_REQD
#define SSH_SK_USER_VERIFICATION_REQD 0x04
#endif

/** Force resident key enrollment even if a resident key with given user ID
 * already exists - FIDO2 only */
#ifndef SSH_SK_FORCE_OPERATION
#define SSH_SK_FORCE_OPERATION 0x10
#endif

/** Create/use resident key stored on authenticator - FIDO2 only */
#ifndef SSH_SK_RESIDENT_KEY
#define SSH_SK_RESIDENT_KEY 0x20
#endif

/* Algorithms */

/** ECDSA with P-256 curve */
#define SSH_SK_ECDSA 0x00

/** Ed25519 - FIDO2 only */
#define SSH_SK_ED25519 0x01

/* Error codes */

/** General unspecified failure */
#define SSH_SK_ERR_GENERAL -1

/** Requested algorithm/feature/option not supported */
#define SSH_SK_ERR_UNSUPPORTED -2

/** PIN (or other user verification) required but either missing or invalid */
#define SSH_SK_ERR_PIN_REQUIRED -3

/** No suitable security key / authenticator device was found */
#define SSH_SK_ERR_DEVICE_NOT_FOUND -4

/** Attempt to create a resident key that already exists (duplicate) */
#define SSH_SK_ERR_CREDENTIAL_EXISTS -5

/**
 * @brief Response structure for FIDO2/U2F key enrollment operations
 *
 * Contains all data returned by a FIDO2/U2F authenticator after successful
 * enrollment of a new credential.
 */
struct sk_enroll_response {
    /** @brief FIDO2/U2F authenticator flags from the enrollment operation
     *
     * Contains flags indicating authenticator capabilities and state during
     * enrollment, such as user presence (UP), user verification
     * (UV), and resident key.
     */
    uint8_t flags;

    /** @brief Public key data in standard format
     *
     * For ECDSA (P-256): 65 bytes in SEC1 uncompressed point format
     * (0x04 prefix + 32-byte X coordinate + 32-byte Y coordinate)
     * For Ed25519: 32 bytes containing the raw public key (FIDO2 only)
     */
    uint8_t *public_key;

    /** @brief Length of public_key buffer in bytes
     *
     * Expected values: 65 for ECDSA P-256, 32 for Ed25519
     */
    size_t public_key_len;

    /** @brief Opaque credential handle/ID used to identify this key
     *
     * Authenticator-generated binary data that uniquely identifies this
     * credential. Used in subsequent sign operations to specify which
     * key to use. Format and contents are authenticator-specific.
     */
    uint8_t *key_handle;

    /** @brief Length of key_handle buffer in bytes
     *
     * Length varies by authenticator.
     */
    size_t key_handle_len;

    /** @brief Enrollment signature over the enrollment data
     *
     * FIDO2/U2F authenticator signature proving the credential was created
     * by this specific authenticator. Used for enrollment verification.
     * Format depends on algorithm.
     */
    uint8_t *signature;

    /** @brief Length of signature buffer in bytes
     *
     * Length varies by algorithm.
     */
    size_t signature_len;

    /** @brief X.509 attestation certificate
     *
     * Certificate that attests to the authenticity of the authenticator
     * and the enrollment operation. Used to verify the authenticator's
     * identity and manufacturer.
     */
    uint8_t *attestation_cert;

    /** @brief Length of attestation_cert buffer in bytes */
    size_t attestation_cert_len;

    /** @brief FIDO2/U2F authenticator data from enrollment
     *
     * CBOR-encoded authenticator data containing RP ID hash, flags,
     * counter, and attested credential data. Used for attestation
     * verification according to the FIDO2 specification.
     */
    uint8_t *authdata;

    /** @brief Length of authdata buffer in bytes
     *
     * Length varies depending on credential data and extensions.
     */
    size_t authdata_len;
};

/**
 * @brief Response structure for FIDO2/U2F key signing operations
 *
 * Contains signature components and metadata returned by a FIDO2/U2F
 * authenticator after a successful signing operation.
 */
struct sk_sign_response {
    /** @brief FIDO2/U2F authenticator flags from the signing operation
     *
     * Contains flags indicating authenticator state during signing,
     * including user presence (UP) and user verification (UV) flags.
     * Used to verify that proper user interaction occurred while signing.
     */
    uint8_t flags;

    /** @brief Authenticator signature counter value
     *
     * Monotonically increasing counter maintained by the authenticator.
     * Incremented on each successful signing operation. Used to detect
     * cloned or duplicated authenticators.
     */
    uint32_t counter;

    /** @brief R component of ECDSA signature or Ed25519 signature */
    uint8_t *sig_r;

    /** @brief Length of sig_r buffer in bytes */
    size_t sig_r_len;

    /** @brief S component of ECDSA signature */
    uint8_t *sig_s;

    /** @brief Length of sig_s buffer in bytes */
    size_t sig_s_len;
};

/**
 * @brief Structure representing a resident/discoverable credential
 *
 * Represents a FIDO2 resident key (discoverable credential) that is
 * stored on the authenticator and can be discovered without providing
 * a credential ID.
 */
struct sk_resident_key {
    /** @brief Cryptographic algorithm identifier for this key
     *
     * SSH_SK_ECDSA (0x00): ECDSA with P-256 curve
     * SSH_SK_ED25519 (0x01): Ed25519 signature algorithm
     */
    uint32_t alg;

    /** @brief Slot/index number of this key on the authenticator
     *
     * Zero-based index indicating the position of this resident key
     * in the authenticator's internal storage. Used for key management
     * and identification when multiple resident keys exist.
     */
    size_t slot;

    /** @brief Relying Party (application) identifier string
     *
     * The RP ID (typically a domain name) that this resident key
     * is associated with. Determines which application/service
     * this key can be used for.
     */
    char *application;

    /** @brief Embedded enrollment response containing key material
     *
     * Contains the same data as returned during initial enrollment,
     * including public key, key handle, and associated metadata.
     */
    struct sk_enroll_response key;

    /** @brief Flags associated with this resident key
     *
     * SSH_SK_USER_PRESENCE_REQD: Requires user presence for operations
     * SSH_SK_USER_VERIFICATION_REQD: Requires user verification
     * (PIN/biometric)
     */
    uint8_t flags;

    /** @brief User identifier associated with this resident key
     *
     * Binary user ID that was provided during key enrollment.
     * Used to identify which user account this key belongs to.
     */
    uint8_t *user_id;

    /** @brief Length of user_id buffer in bytes
     *
     * Length of the user identifier.
     */
    size_t user_id_len;
};

/**
 * @brief Configuration option structure for FIDO2/U2F operations
 *
 * Represents a single configuration parameter that can be passed
 * to FIDO2/U2F middleware.
 */
struct sk_option {
    /** @brief Option name/key identifier */
    char *name;

    /** @brief Option value as bytes */
    char *value;

    /** @brief Indicates if this option is required for the operation
     *
     * Non-zero if this option must be processed and cannot be ignored.
     * Zero if this option is advisory and can be skipped if the
     * middleware does not support it.
     */
    uint8_t required;
};

/** Current SK API version */
#define SSH_SK_VERSION_MAJOR      0x000a0000
#define SSH_SK_VERSION_MAJOR_MASK 0xffff0000

#endif /* SK_API_H */
