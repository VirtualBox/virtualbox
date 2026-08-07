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

#include "config.h"

#include "libssh/buffer.h"
#include "libssh/pki_context.h"
#include "libssh/pki_priv.h"
#include "libssh/pki_sk.h"
#include "libssh/sk_common.h"

#include <stdint.h>
#include <string.h>

#define DEFAULT_PIN_PROMPT "Enter SK PIN: "
#define PIN_BUF_SIZE       64

/**
 * @addtogroup libssh_pki
 * @{
 */

/**
 * @brief Serialize FIDO2 attestation data into an SSH buffer
 *
 * Serializes the attestation certificate, signature, and authenticator data
 * from a FIDO2 enrollment response into an SSH buffer in the
 * "ssh-sk-attest-v01" format.
 *
 * @param[in] enroll_response The sk_enroll_response struct containing
 *                           attestation data from FIDO2 enrollment
 * @param[in,out] attestation_buffer SSH buffer to store the serialized
 *                                  attestation data
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int pki_sk_serialise_attestation_cert(
    const struct sk_enroll_response *enroll_response,
    ssh_buffer attestation_buffer)
{
    int rc;

    if (attestation_buffer == NULL || enroll_response == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Parameters cannot be NULL");
        return SSH_ERROR;
    }

    /* Check if attestation data is available */
    if (enroll_response->attestation_cert == NULL ||
        enroll_response->attestation_cert_len == 0) {
        SSH_LOG(SSH_LOG_INFO, "No attestation certificate available");
        return SSH_ERROR;
    }

    if (enroll_response->signature == NULL ||
        enroll_response->signature_len == 0) {
        SSH_LOG(SSH_LOG_INFO, "No attestation signature available");
        return SSH_ERROR;
    }

    if (enroll_response->authdata == NULL ||
        enroll_response->authdata_len == 0) {
        SSH_LOG(SSH_LOG_INFO, "No authenticator data available");
        return SSH_ERROR;
    }

    rc = ssh_buffer_pack(attestation_buffer,
                         "sdPdPdPds",
                         "ssh-sk-attest-v01",
                         (uint32_t)enroll_response->attestation_cert_len,
                         enroll_response->attestation_cert_len,
                         enroll_response->attestation_cert,
                         (uint32_t)enroll_response->signature_len,
                         enroll_response->signature_len,
                         enroll_response->signature,
                         (uint32_t)enroll_response->authdata_len,
                         enroll_response->authdata_len,
                         enroll_response->authdata,
                         (uint32_t)0, /* reserved flags */
                         "");         /* reserved */
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to pack attestation data into buffer");
        return SSH_ERROR;
    }

    return SSH_OK;
}

/**
 * @brief Create an ssh_key from an sk_enroll_response struct
 *
 * Constructs an ssh_key structure from an sk_enroll_response
 * struct for both ECDSA and Ed25519 algorithms.
 *
 * @param[in] algorithm       The algorithm type (SSH_SK_ECDSA or
 *                            SSH_SK_ED25519)
 * @param[in] application     The application string (relying party ID)
 * @param[in] enroll_response The sk_enroll_response struct containing key data
 * @param[out] ssh_key_result Pointer to store the newly created ssh_key
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int pki_sk_enroll_response_to_ssh_key(
    int algorithm,
    const char *application,
    const struct sk_enroll_response *enroll_response,
    ssh_key *ssh_key_result)
{
    ssh_key key_to_build = NULL;
    ssh_string public_key_string = NULL;
    int rc, ret = SSH_ERROR;

    /* Validate input parameters */
    if (ssh_key_result == NULL) {
        SSH_LOG(SSH_LOG_WARN, "ssh_key pointer cannot be NULL");
        return SSH_ERROR;
    }

    *ssh_key_result = NULL;

    if (enroll_response == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Enrollment response cannot be NULL");
        return SSH_ERROR;
    }

    /* Validate response data */
    if (enroll_response->public_key == NULL ||
        enroll_response->key_handle == NULL) {
        SSH_LOG(
            SSH_LOG_WARN,
            "Invalid enrollment response: missing public key or key handle");
        return SSH_ERROR;
    }

    key_to_build = ssh_key_new();
    if (key_to_build == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate new ssh_key");
        return SSH_ERROR;
    }

    /* Set key type based on algorithm */
    switch (algorithm) {
#ifdef HAVE_ECC
    case SSH_SK_ECDSA:
        key_to_build->type = SSH_KEYTYPE_SK_ECDSA;
        break;
#endif /* HAVE_ECC */
    case SSH_SK_ED25519:
        key_to_build->type = SSH_KEYTYPE_SK_ED25519;
        break;
    default:
        SSH_LOG(SSH_LOG_WARN, "Unsupported algorithm: %d", algorithm);
        goto out;
    }
    key_to_build->type_c = ssh_key_type_to_char(key_to_build->type);

    public_key_string = ssh_string_from_data(enroll_response->public_key,
                                             enroll_response->public_key_len);
    if (public_key_string == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create public key string");
        goto out;
    }

    switch (algorithm) {
#ifdef HAVE_ECC
    case SSH_SK_ECDSA:
        rc = pki_pubkey_build_ecdsa(key_to_build,
                                    pki_key_ecdsa_nid_from_name("nistp256"),
                                    public_key_string);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_WARN, "Failed to build ECDSA public key");
            goto out;
        }
        break;
#endif /* HAVE_ECC */
    case SSH_SK_ED25519:
        rc = pki_pubkey_build_ed25519(key_to_build, public_key_string);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_WARN, "Failed to build ED25519 public key");
            goto out;
        }
        break;
    }

    /* Set security key specific fields */
    key_to_build->sk_application = ssh_string_from_char(application);
    if (key_to_build->sk_application == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create sk_application string");
        goto out;
    }

    /* Set key handle */
    key_to_build->sk_key_handle =
        ssh_string_from_data(enroll_response->key_handle,
                             enroll_response->key_handle_len);
    if (key_to_build->sk_key_handle == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create sk_key_handle string");
        goto out;
    }

    key_to_build->sk_reserved = ssh_string_from_data(NULL, 0);
    if (key_to_build->sk_reserved == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create sk_reserved string");
        goto out;
    }

    key_to_build->sk_flags = enroll_response->flags;
    key_to_build->flags = SSH_KEY_FLAG_PRIVATE | SSH_KEY_FLAG_PUBLIC;

    *ssh_key_result = key_to_build;
    key_to_build = NULL;
    ret = SSH_OK;

out:
    ssh_string_burn(public_key_string);
    SSH_STRING_FREE(public_key_string);
    SSH_KEY_FREE(key_to_build);

    return ret;
}

int pki_sk_enroll_key(ssh_pki_ctx context,
                      enum ssh_keytypes_e key_type,
                      ssh_key *enrolled_key_result)
{
    const struct ssh_sk_callbacks_struct *sk_callbacks = NULL;

    struct sk_enroll_response *enroll_response = NULL;
    ssh_key enrolled_key = NULL;

    char pin_buf[PIN_BUF_SIZE] = {0};
    const char *pin_to_use = NULL;

    unsigned char random_challenge[32];
    const unsigned char *challenge = NULL;
    size_t challenge_length = 0;

    ssh_buffer challenge_buffer = NULL;
    ssh_buffer attestation = NULL;

    int rc, ret = SSH_ERROR;
    int algorithm;

    /* Validate input parameters */
    if (context == NULL) {
        SSH_LOG(SSH_LOG_WARN, "SK context cannot be NULL");
        return SSH_ERROR;
    }

    if (enrolled_key_result == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Enrolled key result pointer cannot be NULL");
        return SSH_ERROR;
    }

    /* Initialize output parameter */
    *enrolled_key_result = NULL;

    /* Clear any existing attestation data */
    SSH_BUFFER_FREE(context->sk_attestation_buffer);

    /* Get security key callbacks from context */
    sk_callbacks = context->sk_callbacks;
    if (sk_callbacks == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Security key callbacks cannot be NULL");
        return SSH_ERROR;
    }

    if (!ssh_callbacks_exists(sk_callbacks, enroll)) {
        SSH_LOG(SSH_LOG_WARN,
                "Security key enroll callback is not implemented");
        return SSH_ERROR;
    }

    /* Validate required fields */
    if (context->sk_application == NULL || *context->sk_application == '\0') {
        SSH_LOG(SSH_LOG_WARN, "Application identifier cannot be NULL or empty");
        return SSH_ERROR;
    }

    /* Extract parameters from context */
    challenge_buffer = context->sk_challenge_buffer;

    /* Determine algorithm based on key type */
    switch (key_type) {
#ifdef HAVE_ECC
    case SSH_KEYTYPE_SK_ECDSA:
        algorithm = SSH_SK_ECDSA;
        break;
#endif /* HAVE_ECC */
    case SSH_KEYTYPE_SK_ED25519:
        algorithm = SSH_SK_ED25519;
        break;
    default:
        SSH_LOG(SSH_LOG_WARN,
                "Unsupported key type for security key enrollment");
        goto out;
    }

    /* Determine challenge to use */
    if (challenge_buffer == NULL) {
        SSH_LOG(SSH_LOG_DEBUG, "Using randomly generated challenge");

        rc = ssh_get_random(random_challenge, sizeof(random_challenge), 0);
        if (rc != 1) {
            SSH_LOG(SSH_LOG_WARN, "Failed to generate random challenge");
            goto out;
        }

        challenge = random_challenge;
        challenge_length = sizeof(random_challenge);

    } else {
        challenge_length = ssh_buffer_get_len(challenge_buffer);
        if (challenge_length == 0) {
            SSH_LOG(SSH_LOG_WARN, "Challenge buffer cannot be empty");
            goto out;
        }

        challenge = ssh_buffer_get(challenge_buffer);
        SSH_LOG(SSH_LOG_DEBUG,
                "Using provided challenge of length %zu",
                challenge_length);
    }

    if (context->sk_pin_callback != NULL) {
        rc = context->sk_pin_callback(DEFAULT_PIN_PROMPT,
                                      pin_buf,
                                      sizeof(pin_buf),
                                      0,
                                      0,
                                      context->sk_userdata);
        if (rc == SSH_OK) {
            pin_to_use = pin_buf;
        } else {
            SSH_LOG(SSH_LOG_WARN, "Failed to fetch PIN from callback");
            ssh_burn(pin_buf, sizeof(pin_buf));
            goto out;
        }
    } else {
        SSH_LOG(SSH_LOG_INFO, "Trying operation without PIN");
    }

    rc = sk_callbacks->enroll(algorithm,
                              challenge,
                              challenge_length,
                              context->sk_application,
                              context->sk_flags,
                              pin_to_use,
                              context->sk_callbacks_options,
                              &enroll_response);
    ssh_burn(pin_buf, sizeof(pin_buf));
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Security key enroll callback failed: %s (%d)",
                ssh_sk_err_to_string(rc),
                rc);
        goto out;
    }

    /* Convert SK enroll response to ssh_key */
    rc = pki_sk_enroll_response_to_ssh_key(algorithm,
                                           context->sk_application,
                                           enroll_response,
                                           &enrolled_key);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to convert enroll response to ssh_key");
        goto out;
    }

    /* Try to serialize attestation data and store in context */
    attestation = ssh_buffer_new();
    if (attestation == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate attestation buffer");
        goto out;
    } else {
        rc = pki_sk_serialise_attestation_cert(enroll_response, attestation);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_INFO,
                    "Failed to serialize attestation data, continuing without "
                    "attestation");
        } else {
            context->sk_attestation_buffer = attestation;
            attestation = NULL;
        }
    }

    *enrolled_key_result = enrolled_key;
    enrolled_key = NULL;
    ret = SSH_OK;

out:
    if (challenge == random_challenge) {
        ssh_burn(random_challenge, sizeof(random_challenge));
    }

    SK_ENROLL_RESPONSE_FREE(enroll_response);
    SSH_KEY_FREE(enrolled_key);
    SSH_BUFFER_FREE(attestation);

    return ret;
}

static int
pki_sk_pack_ecdsa_signature(const struct sk_sign_response *sign_response,
                            ssh_buffer sig_buffer)
{

    bignum r_bn = NULL, s_bn = NULL;
    ssh_buffer inner_buffer = NULL;
    int rc = SSH_ERROR;

    /* Convert raw r and s bytes to bignums */
    bignum_bin2bn(sign_response->sig_r, (int)sign_response->sig_r_len, &r_bn);
    if (r_bn == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to convert sig_r to bignum");
        goto out;
    }

    bignum_bin2bn(sign_response->sig_s, (int)sign_response->sig_s_len, &s_bn);
    if (s_bn == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to convert sig_s to bignum");
        goto out;
    }

    /* Create inner buffer with r and s as SSH strings */
    inner_buffer = ssh_buffer_new();
    if (inner_buffer == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create inner buffer");
        goto out;
    }
    ssh_buffer_set_secure(inner_buffer);

    rc = ssh_buffer_pack(inner_buffer, "BB", r_bn, s_bn);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to pack r and s into inner buffer");
        goto out;
    }

    rc = ssh_buffer_pack(sig_buffer,
                         "P",
                         (size_t)ssh_buffer_get_len(inner_buffer),
                         ssh_buffer_get(inner_buffer));
    if (rc != SSH_OK) {
        goto out;
    }

    rc = SSH_OK;

out:
    SSH_BUFFER_FREE(inner_buffer);
    bignum_safe_free(s_bn);
    bignum_safe_free(r_bn);

    return rc;
}

static int
pki_sk_pack_ed25519_signature(const struct sk_sign_response *sign_response,
                              ssh_buffer sig_buffer)
{
    int rc = SSH_ERROR;

    rc = ssh_buffer_pack(sig_buffer,
                         "P",
                         sign_response->sig_r_len,
                         sign_response->sig_r);
    if (rc != SSH_OK) {
        return SSH_ERROR;
    }

    return SSH_OK;
}

/**
 * @brief Create an ssh_signature from a sk_sign_response structure
 *
 * Serializes a security key sign response into an ssh_signature structure
 * for both ECDSA and Ed25519 algorithms.
 *
 * @param[in] algorithm The algorithm used (SSH_SK_ECDSA or SSH_SK_ED25519)
 * @param[in] key_type  The SSH key type for setting signature type
 * @param[in] sign_response The sk_sign_response containing signature data
 * @param[out] ssh_signature_result Pointer to store the created ssh_signature
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int pki_sk_sign_response_to_ssh_signature(
    int algorithm,
    enum ssh_keytypes_e key_type,
    const struct sk_sign_response *sign_response,
    ssh_signature *ssh_signature_result)
{
    ssh_signature signature_to_build = NULL;
    ssh_buffer sig_buffer = NULL;
    int rc;

    /* Validate input parameters */
    if (ssh_signature_result == NULL) {
        SSH_LOG(SSH_LOG_WARN, "ssh_signature pointer cannot be NULL");
        return SSH_ERROR;
    }

    *ssh_signature_result = NULL;

    if (sign_response == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Sign response cannot be NULL");
        return SSH_ERROR;
    }

    /* Validate response data based on algorithm */
    switch (algorithm) {
#ifdef HAVE_ECC
    case SSH_SK_ECDSA:
        if (sign_response->sig_r == NULL || sign_response->sig_s == NULL) {
            SSH_LOG(SSH_LOG_WARN,
                    "Invalid ECDSA sign response: missing sig_r or sig_s");
            return SSH_ERROR;
        }
        break;
#endif /* HAVE_ECC */
    case SSH_SK_ED25519:
        if (sign_response->sig_r == NULL ||
            sign_response->sig_r_len != ED25519_SIG_LEN) {
            SSH_LOG(SSH_LOG_WARN, "Invalid sig_r in Ed25519 sign response");
            return SSH_ERROR;
        }
        break;
    default:
        SSH_LOG(SSH_LOG_WARN, "Unsupported algorithm: %d", algorithm);
        return SSH_ERROR;
    }

    /* Create new ssh_signature */
    signature_to_build = ssh_signature_new();
    if (signature_to_build == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate new ssh_signature");
        return SSH_ERROR;
    }

    /* Set signature type and metadata */
    signature_to_build->type = key_type;
    signature_to_build->type_c = ssh_key_type_to_char(key_type);

    /* Set security key specific fields */
    signature_to_build->sk_flags = sign_response->flags;
    signature_to_build->sk_counter = sign_response->counter;

    /* Create a buffer to hold the signature data */
    sig_buffer = ssh_buffer_new();
    if (sig_buffer == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create signature buffer");
        goto error;
    }
    ssh_buffer_set_secure(sig_buffer);

    /* Build the signature based on algorithm */
    switch (algorithm) {
#ifdef HAVE_ECC
    case SSH_SK_ECDSA:
        signature_to_build->hash_type = SSH_DIGEST_SHA256;

        rc = pki_sk_pack_ecdsa_signature(sign_response, sig_buffer);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_WARN, "Failed to pack ECDSA signature");
            goto error;
        }
        break;
#endif /* HAVE_ECC */
    case SSH_SK_ED25519:
        signature_to_build->hash_type = SSH_DIGEST_AUTO;

        rc = pki_sk_pack_ed25519_signature(sign_response, sig_buffer);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_WARN, "Failed to pack Ed25519 signature");
            goto error;
        }
        break;
    }

    /* Set the signature data */
    signature_to_build->raw_sig =
        ssh_string_from_data(ssh_buffer_get(sig_buffer),
                             ssh_buffer_get_len(sig_buffer));
    if (signature_to_build->raw_sig == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create raw signature string");
        goto error;
    }

    *ssh_signature_result = signature_to_build;
    SSH_BUFFER_FREE(sig_buffer);

    return SSH_OK;

error:
    SSH_SIGNATURE_FREE(signature_to_build);
    SSH_BUFFER_FREE(sig_buffer);

    return SSH_ERROR;
}

ssh_signature pki_sk_do_sign(ssh_pki_ctx context,
                             const ssh_key key,
                             const unsigned char *data,
                             size_t data_len)
{
    const struct ssh_sk_callbacks_struct *sk_callbacks = NULL;
    struct sk_sign_response *sign_response = NULL;
    ssh_signature signature = NULL;

    char pin_buf[PIN_BUF_SIZE] = {0};
    const char *pin_to_use = NULL;

    int algorithm;
    int rc = SSH_ERROR;

    /* Validate input parameters */
    if (context == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Context cannot be NULL");
        return NULL;
    }

    /* Get security key callbacks from context */
    sk_callbacks = context->sk_callbacks;
    if (sk_callbacks == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Security key callbacks cannot be NULL");
        return NULL;
    }

    if (!ssh_callbacks_exists(sk_callbacks, sign)) {
        SSH_LOG(SSH_LOG_WARN, "Security key sign callback is not implemented");
        return NULL;
    }

    if (key == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Key cannot be NULL");
        return NULL;
    }

    if (data == NULL || data_len == 0) {
        SSH_LOG(SSH_LOG_WARN, "Data cannot be NULL or empty");
        return NULL;
    }

    /* Validate key type and determine algorithm */
    switch (key->type) {
#ifdef HAVE_ECC
    case SSH_KEYTYPE_SK_ECDSA:
        algorithm = SSH_SK_ECDSA;
        break;
#endif /* HAVE_ECC */
    case SSH_KEYTYPE_SK_ED25519:
        algorithm = SSH_SK_ED25519;
        break;
    default:
        SSH_LOG(SSH_LOG_WARN, "Unsupported key type for security key signing");
        return NULL;
    }

    /* Validate security key specific fields */
    if (key->sk_key_handle == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Security key handle cannot be NULL");
        return NULL;
    }

    if (key->sk_application == NULL ||
        ssh_string_len(key->sk_application) == 0) {
        SSH_LOG(SSH_LOG_WARN,
                "Security key application cannot be NULL or empty");
        return NULL;
    }

    if (context->sk_pin_callback != NULL) {
        rc = context->sk_pin_callback(DEFAULT_PIN_PROMPT,
                                      pin_buf,
                                      sizeof(pin_buf),
                                      0,
                                      0,
                                      context->sk_userdata);
        if (rc == SSH_OK) {
            pin_to_use = pin_buf;
        } else {
            SSH_LOG(SSH_LOG_WARN, "Failed to fetch PIN from callback");
            ssh_burn(pin_buf, sizeof(pin_buf));
            goto error;
        }
    } else {
        SSH_LOG(SSH_LOG_INFO, "Trying operation without PIN");
    }

    rc = sk_callbacks->sign(algorithm,
                            data,
                            data_len,
                            ssh_string_get_char(key->sk_application),
                            ssh_string_data(key->sk_key_handle),
                            ssh_string_len(key->sk_key_handle),
                            key->sk_flags,
                            pin_to_use,
                            context->sk_callbacks_options,
                            &sign_response);
    ssh_burn(pin_buf, sizeof(pin_buf));
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Security key sign callback failed: %s (%d)",
                ssh_sk_err_to_string(rc),
                rc);
        goto error;
    }

    /* Convert SK sign response to ssh_signature */
    rc = pki_sk_sign_response_to_ssh_signature(algorithm,
                                               key->type,
                                               sign_response,
                                               &signature);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to convert sign response to signature");
        goto error;
    }

    SK_SIGN_RESPONSE_FREE(sign_response);
    return signature;

error:
    SK_SIGN_RESPONSE_FREE(sign_response);
    SSH_SIGNATURE_FREE(signature);
    return NULL;
}

/**
 * @brief Load resident keys from FIDO2 security keys
 *
 * This function loads all resident keys (discoverable credentials) stored
 * on FIDO2 security keys using the context's security key callbacks.
 * Resident keys are credentials stored directly on the security key device
 * and can be discovered without prior knowledge of key handles.
 *
 * Only resident keys with SSH application identifiers (starting with
 * "ssh:") are returned.
 *
 * @param[in] pki_context The PKI context containing security key callbacks.
 *                    Can be NULL, in which case a default context with
 *                    default callbacks will be used. If provided, the context
 *                    must have valid sk_callbacks configured.
 * @param[out] resident_keys_result Array of ssh_key structs representing the
 *                                  resident keys found and loaded
 * @param[out] num_keys_found_result Number of resident keys found and loaded
 *
 * @return SSH_OK on success, SSH_ERROR on error
 *
 * @note The resident_keys_result array and its contents must be freed by
 *       the caller using ssh_sk_resident_key_free() for each key and then
 *       freeing the array itself when no longer needed.
 */
int ssh_sk_resident_keys_load(const struct ssh_pki_ctx_struct *pki_context,
                              ssh_key **resident_keys_result,
                              size_t *num_keys_found_result)
{
    const struct ssh_sk_callbacks_struct *sk_callbacks = NULL;
    struct sk_resident_key **raw_resident_keys = NULL;

    ssh_key cur_resident_key = NULL, *result_keys = NULL, *temp_keys = NULL;
    ssh_pki_ctx temp_ctx = NULL;
    const struct ssh_pki_ctx_struct *ctx_to_use = NULL;

    size_t raw_keys_count = 0, result_keys_count = 0, i;
    uint8_t sk_flags;

    char pin_buf[PIN_BUF_SIZE] = {0};
    const char *pin_to_use = NULL;

    int rc = SSH_ERROR;

    /* If no context provided, create a temporary default one */
    if (pki_context == NULL) {
        SSH_LOG(SSH_LOG_INFO, "No PKI context provided, using the default one");

        temp_ctx = ssh_pki_ctx_new();
        if (temp_ctx == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Failed to create temporary PKI context");
            return SSH_ERROR;
        }
        ctx_to_use = temp_ctx;
    } else {
        ctx_to_use = pki_context;
    }

    /* Get security key callbacks from context */
    sk_callbacks = ctx_to_use->sk_callbacks;
    if (sk_callbacks == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Security key callbacks cannot be NULL");
        goto out;
    }

    if (!ssh_callbacks_exists(sk_callbacks, load_resident_keys)) {
        SSH_LOG(SSH_LOG_WARN,
                "Security key load resident keys callback is not implemented");
        goto out;
    }

    if (resident_keys_result == NULL || num_keys_found_result == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Result pointers cannot be NULL");
        goto out;
    }

    /* Initialize output parameters */
    *resident_keys_result = NULL;
    *num_keys_found_result = 0;

    if (ctx_to_use->sk_pin_callback != NULL) {
        rc = ctx_to_use->sk_pin_callback(DEFAULT_PIN_PROMPT,
                                         pin_buf,
                                         sizeof(pin_buf),
                                         0,
                                         0,
                                         ctx_to_use->sk_userdata);
        if (rc == SSH_OK) {
            pin_to_use = pin_buf;
        } else {
            SSH_LOG(SSH_LOG_WARN, "Failed to fetch PIN from callback");
            ssh_burn(pin_buf, sizeof(pin_buf));
            goto out;
        }
    } else {
        SSH_LOG(SSH_LOG_INFO, "Trying operation without PIN");
    }

    rc = sk_callbacks->load_resident_keys(pin_to_use,
                                          ctx_to_use->sk_callbacks_options,
                                          &raw_resident_keys,
                                          &raw_keys_count);
    ssh_burn(pin_buf, sizeof(pin_buf));
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Security key load_resident_keys callback failed: %s (%d)",
                ssh_sk_err_to_string(rc),
                rc);
        goto out;
    }

    /* Process each raw resident key */
    for (i = 0; i < raw_keys_count; i++) {
        SSH_LOG(
            SSH_LOG_DEBUG,
            "Processing resident key %zu: alg %d, app \"%s\", user_id_len %zu",
            i,
            raw_resident_keys[i]->alg,
            raw_resident_keys[i]->application,
            raw_resident_keys[i]->user_id_len);

        /* Filter out non-SSH applications */
        if (strncmp(raw_resident_keys[i]->application, "ssh:", 4) != 0) {
            SSH_LOG(SSH_LOG_DEBUG,
                    "Skipping non-SSH application: %s",
                    raw_resident_keys[i]->application);
            continue;
        }

        /* Check supported algorithms */
        switch (raw_resident_keys[i]->alg) {
#ifdef HAVE_ECC
        case SSH_SK_ECDSA:
            break;
#endif /* HAVE_ECC */
        case SSH_SK_ED25519:
            break;
        default:
            SSH_LOG(SSH_LOG_WARN,
                    "Unsupported algorithm %d, skipping",
                    raw_resident_keys[i]->alg);
            continue;
        }

        /* Set up security key flags */
        sk_flags = SSH_SK_USER_PRESENCE_REQD | SSH_SK_RESIDENT_KEY;
        if (raw_resident_keys[i]->flags & SSH_SK_USER_VERIFICATION_REQD) {
            sk_flags |= SSH_SK_USER_VERIFICATION_REQD;
        }

        /* Convert raw resident key to libssh key structure */
        rc =
            pki_sk_enroll_response_to_ssh_key(raw_resident_keys[i]->alg,
                                              raw_resident_keys[i]->application,
                                              &raw_resident_keys[i]->key,
                                              &cur_resident_key);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_WARN,
                    "Failed to convert resident key %zu to ssh_key",
                    i);
            continue;
        }

        /* Set the security key flags on the converted key */
        cur_resident_key->sk_flags = sk_flags;

        /* Copy user ID if present */
        if (raw_resident_keys[i]->user_id != NULL &&
            raw_resident_keys[i]->user_id_len > 0) {

            cur_resident_key->sk_user_id =
                ssh_string_from_data(raw_resident_keys[i]->user_id,
                                     raw_resident_keys[i]->user_id_len);
            if (cur_resident_key->sk_user_id == NULL) {
                SSH_LOG(SSH_LOG_WARN,
                        "Failed to allocate user_id string for key %zu",
                        i);
                goto out;
            }
        }

        /* Grow the result array */
        temp_keys =
            realloc(result_keys, sizeof(ssh_key) * (result_keys_count + 1));
        if (temp_keys == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Failed to reallocate result keys array");
            goto out;
        }

        /* Add the current resident key to the result array */
        result_keys = temp_keys;
        result_keys[result_keys_count] = cur_resident_key;
        result_keys_count++;
        cur_resident_key = NULL;
    }

    /* Set output parameters */
    *resident_keys_result = result_keys;
    *num_keys_found_result = result_keys_count;
    result_keys = NULL;
    result_keys_count = 0;
    rc = SSH_OK;

out:

    if (raw_resident_keys != NULL) {
        for (i = 0; i < raw_keys_count; i++) {
            SK_RESIDENT_KEY_FREE(raw_resident_keys[i]);
        }
        SAFE_FREE(raw_resident_keys);
    }

    SSH_KEY_FREE(cur_resident_key);
    for (i = 0; i < result_keys_count; i++) {
        SSH_KEY_FREE(result_keys[i]);
    }
    SAFE_FREE(result_keys);

    /* Clean up temporary context if we created one */
    if (temp_ctx != NULL) {
        SSH_PKI_CTX_FREE(temp_ctx);
    }

    return rc;
}

/** @} */
