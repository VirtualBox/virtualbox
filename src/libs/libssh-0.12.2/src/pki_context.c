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

#include "libssh/libssh.h"
#include "libssh/pki.h"
#include "libssh/pki_context.h"
#include "libssh/priv.h"
#include "libssh/sk_common.h"

#ifdef WITH_FIDO2
#include "libssh/buffer.h"
#include "libssh/callbacks.h"
#include "libssh/sk_api.h"
#endif /* WITH_FIDO2 */

/**
 * @addtogroup libssh_pki
 * @{
 */

/**
 * @brief Allocate a new generic PKI context container.
 *
 * Allocates and default-initializes a new ssh_pki_ctx instance.
 *
 * @return Newly allocated context on success, or NULL on allocation failure.
 * @see ssh_pki_ctx_free()
 */
ssh_pki_ctx ssh_pki_ctx_new(void)
{
    struct ssh_pki_ctx_struct *ctx = NULL;

    ctx = calloc(1, sizeof(struct ssh_pki_ctx_struct));
    if (ctx == NULL) {
        return NULL;
    }

#ifdef WITH_FIDO2
    /* Initialize SK fields with default, if available. */
    ctx->sk_callbacks = ssh_sk_get_default_callbacks();

    /*
     * Both OpenSSH security key enrollment and server authentication require
     * user presence by default, so we replicate that for consistency.
     */
    ctx->sk_flags = SSH_SK_USER_PRESENCE_REQD;

    ctx->sk_application = strdup("ssh:");
    if (ctx->sk_application == NULL) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to allocate memory for default application");
        SAFE_FREE(ctx);
        return NULL;
    }
#endif /* WITH_FIDO2 */

    return ctx;
}

/**
 * @brief Free a generic PKI context container.
 *
 * @param[in] context  The PKI context to free (may be NULL).
 * @see ssh_pki_ctx_new()
 */
void ssh_pki_ctx_free(ssh_pki_ctx context)
{
    if (context == NULL) {
        return;
    }

#ifdef WITH_FIDO2
    SAFE_FREE(context->sk_application);
    SSH_BUFFER_FREE(context->sk_challenge_buffer);
    SSH_BUFFER_FREE(context->sk_attestation_buffer);
    SK_OPTIONS_FREE(context->sk_callbacks_options);
#endif /* WITH_FIDO2 */

    SAFE_FREE(context);
}

/**
 * @brief Set various options for a PKI context.
 *
 * This function can set all possible PKI context options.
 *
 * @param[in] context  Target PKI context.
 * @param option  The option type to set. This could be one of the following:
 *
 *                - SSH_PKI_OPTION_RSA_KEY_SIZE (int):
 *                  Set the RSA key size in bits for key generation.
 *                  Typically 2048, 3072, or 4096 bits. Must be greater
 *                  than or equal to 1024, as anything below is considered
 *                  insecure.
 *
 *                - SSH_PKI_OPTION_SK_APPLICATION (const char *):
 *                  The Relying Party identifier (application string) that
 *                  determines which service/domain this security key
 *                  credential will be associated with. This is a required
 *                  field for all security key generation operations.
 *                  The application string typically starts with "ssh:" for
 *                  SSH keys. It is copied internally and can be freed
 *                  after setting.
 *
 *                - SSH_PKI_SK_OPTION_FLAGS (uint8_t):
 *                  Set FIDO2/U2F operation flags that control how the FIDO2/U2F
 *                  authenticator behaves during generation operations. Multiple
 *                  flags can be combined using bitwise OR operations. The
 *                  pointer must not be NULL.
 *
 *                  Available flags:
 *
 *                  SSH_SK_USER_PRESENCE_REQD: Requires user presence
 *
 *                  SSH_SK_USER_VERIFICATION_REQD: Requires user verification
 *
 *                  SSH_SK_FORCE_OPERATION: Forces generation even if a
 *                  resident key already exists.
 *
 *                  SSH_SK_RESIDENT_KEY: Creates a resident
 *                  key stored on the authenticator.
 *
 *                - SSH_PKI_OPTION_SK_USER_ID (const char *):
 *                  Sets the user identifier to associate with a resident
 *                  credential during enrollment. When a resident key is
 *                  requested (SSH_SK_RESIDENT_KEY), this ID is stored on the
 *                  authenticator and later used to look up or prevent duplicate
 *                  credentials. Maximum length is SK_MAX_USER_ID_LEN bytes;
 *                  longer values will cause the operation to fail.
 *
 *                - SSH_PKI_OPTION_SK_CHALLENGE (ssh_buffer):
 *                  Set custom cryptographic challenge data to be included in
 *                  the generation operation. The challenge is signed by the
 *                  authenticator during key generation. If not provided,
 *                  a random 32-byte challenge will be automatically generated.
 *                  The challenge data is copied internally and the caller
 *                  retains ownership of the provided buffer.
 *
 *                - SSH_PKI_OPTION_SK_CALLBACKS (ssh_sk_callbacks):
 *                  Set the security key callback structure to use custom
 *                  callback functions for FIDO2/U2F operations like enrollment,
 *                  signing, and loading resident keys. The structure is not
 *                  copied so it needs to be valid for the whole context
 *                  lifetime or until replaced.
 *
 * @param value     The value to set. This is a generic pointer and the
 *                  datatype which is used should be set according to the
 *                  option type.
 *
 * @return          SSH_OK on success, SSH_ERROR on error.
 *
 * @warning         When the option value to set is represented via a pointer
 *                  (e.g const char *, ssh_buffer), the value parameter
 *                  should be that pointer. Do NOT pass a pointer to a
 *                  pointer.
 *
 * @warning         When the option value to set is not a pointer (e.g int,
 *                  uint8_t), the value parameter should be a pointer to the
 *                  location storing the value to set (int *, uint8_t *).
 */
int ssh_pki_ctx_options_set(ssh_pki_ctx context,
                            enum ssh_pki_options_e option,
                            const void *value)
{
    if (context == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Invalid PKI context passed");
        return SSH_ERROR;
    }

    switch (option) {
    case SSH_PKI_OPTION_RSA_KEY_SIZE:
        if (value == NULL) {
            SSH_LOG(SSH_LOG_WARN, "RSA key size pointer must not be NULL");
            return SSH_ERROR;
        } else if (*(int *)value != 0 && *(int *)value <= RSA_MIN_KEY_SIZE) {
            SSH_LOG(
                SSH_LOG_WARN,
                "RSA key size must be greater than %d bits or 0 for default",
                RSA_MIN_KEY_SIZE);
            return SSH_ERROR;
        }
        context->rsa_key_size = *(int *)value;
        break;

#ifdef WITH_FIDO2
    case SSH_PKI_OPTION_SK_APPLICATION:
        SAFE_FREE(context->sk_application);
        if (value != NULL) {
            context->sk_application = strdup((char *)value);
            if (context->sk_application == NULL) {
                SSH_LOG(SSH_LOG_WARN,
                        "Failed to allocate memory for application");
                return SSH_ERROR;
            }
        }
        break;

    case SSH_PKI_OPTION_SK_FLAGS:
        if (value == NULL) {
            return SSH_ERROR;
        } else {
            context->sk_flags = *(uint8_t *)value;
        }
        break;

    case SSH_PKI_OPTION_SK_USER_ID: {
        int rc;

        /*
         * Set required to false, because only the enrollment callback supports
         * the user ID option, and if this context is used for any other
         * operation, it would fail unnecessarily.
         */
        rc = ssh_pki_ctx_sk_callbacks_option_set(context,
                                                 SSH_SK_OPTION_NAME_USER_ID,
                                                 value,
                                                 false);
        if (rc != SSH_OK) {
            return SSH_ERROR;
        }
        break;
    }

    case SSH_PKI_OPTION_SK_CHALLENGE: {
        SSH_BUFFER_FREE(context->sk_challenge_buffer);
        if (value == NULL) {
            break;
        }

        context->sk_challenge_buffer = ssh_buffer_dup((ssh_buffer)value);
        if (context->sk_challenge_buffer == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Failed to duplicate challenge buffer");
            return SSH_ERROR;
        }
        ssh_buffer_set_secure(context->sk_challenge_buffer);
        break;
    }

    case SSH_PKI_OPTION_SK_CALLBACKS: {
        bool is_compatible = sk_callbacks_check_compatibility(value);
        if (!is_compatible) {
            return SSH_ERROR;
        }
        context->sk_callbacks = value;
        break;
    }
#else  /* WITH_FIDO2 */
    case SSH_PKI_OPTION_SK_APPLICATION:
    case SSH_PKI_OPTION_SK_FLAGS:
    case SSH_PKI_OPTION_SK_USER_ID:
    case SSH_PKI_OPTION_SK_CHALLENGE:
    case SSH_PKI_OPTION_SK_CALLBACKS:
        SSH_LOG(SSH_LOG_WARN, SK_NOT_SUPPORTED_MSG);
        return SSH_ERROR;
#endif /* WITH_FIDO2 */

    default:
        SSH_LOG(SSH_LOG_WARN, "Unknown PKI context option: %d", option);
        return SSH_ERROR;
    }

    return SSH_OK;
}

/**
 * @brief Set the PIN callback function to get the PIN for security
 * key authenticator access.
 *
 * @param context      The PKI context to modify.
 * @param pin_callback The callback used when the authenticator requires PIN
 *                     entry for verification.
 * @param userdata     A generic pointer that is passed as the userdata
 *                     argument to the callback function. Can be NULL.
 *
 * @return SSH_OK on success, SSH_ERROR if context is NULL.
 *
 * @note The callback and userdata are stored internally in the context
 *       structure and must remain valid until the context is freed or
 *       replaced.
 *
 * @see ssh_auth_callback
 */
int ssh_pki_ctx_set_sk_pin_callback(ssh_pki_ctx context,
                                    ssh_auth_callback pin_callback,
                                    void *userdata)
{
#ifdef WITH_FIDO2
    if (context == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Context should not be NULL");
        return SSH_ERROR;
    }

    context->sk_pin_callback = pin_callback;
    context->sk_userdata = userdata;

    return SSH_OK;

#else
    (void)context;
    (void)pin_callback;
    (void)userdata;

    SSH_LOG(SSH_LOG_WARN, SK_NOT_SUPPORTED_MSG);
    return SSH_ERROR;
#endif /* WITH_FIDO2 */
}

/**
 * @brief Set a security key (FIDO2/U2F) callback option in the
 * context. These options are passed to the sk_callbacks during
 * enroll/sign/load_resident_keys operations.
 *
 * Both the name and value strings are duplicated internally so the caller
 * retains ownership of the original pointers.
 *
 * @param[in] context   The PKI context. Must not be NULL.
 * @param[in] name      option name string. Must not be NULL.
 * @param[in] value     option value string. Must not be NULL.
 * @param[in] required  Set to true if the option is mandatory. If set and the
 *                      ssh_sk_callbacks do not recognize the option,
 *                      the operation should fail.
 *
 * @return SSH_OK on success, SSH_ERROR on allocation failure or invalid args.
 *
 * @note The option objects are freed automatically when the context is freed
 *       via ssh_pki_sk_ctx_free().
 *
 * @see ssh_sk_callbacks_struct
 */
int ssh_pki_ctx_sk_callbacks_option_set(ssh_pki_ctx context,
                                        const char *name,
                                        const char *value,
                                        bool required)
{
#ifdef WITH_FIDO2
    struct sk_option *new_option = NULL;
    struct sk_option **temp = NULL;
    size_t count = 0;

    if (context == NULL || name == NULL || value == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Invalid parameters passed");
        return SSH_ERROR;
    }

    /* Count existing options */
    if (context->sk_callbacks_options != NULL) {
        while (context->sk_callbacks_options[count] != NULL) {
            count++;
        }
    }

    /* Allocate new option */
    new_option = calloc(1, sizeof(struct sk_option));
    if (new_option == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for new option");
        return SSH_ERROR;
    }

    new_option->name = strdup(name);
    if (new_option->name == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for option name");
        SAFE_FREE(new_option);
        return SSH_ERROR;
    }

    new_option->value = strdup(value);
    if (new_option->value == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for option value");
        SAFE_FREE(new_option->name);
        SAFE_FREE(new_option);
        return SSH_ERROR;
    }

    new_option->required = required;

    /* Reallocate array to accommodate new option */
    temp = realloc(context->sk_callbacks_options,
                   (count + 2) * sizeof(struct sk_option *));
    if (temp == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to reallocate options array");
        SAFE_FREE(new_option->name);
        SAFE_FREE(new_option->value);
        SAFE_FREE(new_option);
        return SSH_ERROR;
    }

    context->sk_callbacks_options = temp;
    context->sk_callbacks_options[count] = new_option;
    context->sk_callbacks_options[count + 1] = NULL;

    return SSH_OK;
#else
    (void)context;
    (void)name;
    (void)value;
    (void)required;

    SSH_LOG(SSH_LOG_WARN, SK_NOT_SUPPORTED_MSG);
    return SSH_ERROR;
#endif /* WITH_FIDO2 */
}

/**
 * @brief Clear all sk_callbacks options.
 *
 * Removes and frees all previously set sk_callbacks options from the context.
 *
 * @param[in] context The PKI context to modify.
 *
 * @return SSH_OK on success, SSH_ERROR if context is NULL.
 */
int ssh_pki_ctx_sk_callbacks_options_clear(ssh_pki_ctx context)
{
#ifdef WITH_FIDO2
    if (context == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Context should not be NULL");
        return SSH_ERROR;
    }

    SK_OPTIONS_FREE(context->sk_callbacks_options);
    return SSH_OK;
#else
    (void)context;

    SSH_LOG(SSH_LOG_WARN, SK_NOT_SUPPORTED_MSG);
    return SSH_ERROR;
#endif /* WITH_FIDO2 */
}

/**
 * @brief Get a copy of the attestation buffer from a PKI context.
 *
 * Retrieves a copy of the attestation buffer stored in the context after a key
 * enrollment operation. The attestation buffer contains serialized attestation
 * information in the "ssh-sk-attest-v01" format.
 *
 * @param[in] context The PKI context. Must not be NULL.
 * @param[out] attestation_buffer Pointer to store a copy of the attestation
 *                                buffer. Will be set to NULL if no attestation
 *                                data is available (e.g., authenticator doesn't
 *                                support attestation, or attestation data
 *                                was invalid/incomplete).
 *
 * @return SSH_OK on success, SSH_ERROR if context or attestation_buffer is
 *         NULL, or if buffer duplication fails.
 *
 * @note The caller is responsible for freeing the returned buffer using
 *       SSH_BUFFER_FREE().
 */
int ssh_pki_ctx_get_sk_attestation_buffer(
    const struct ssh_pki_ctx_struct *context,
    ssh_buffer *attestation_buffer)
{
#ifdef WITH_FIDO2
    if (context == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Context should not be NULL");
        return SSH_ERROR;
    }

    if (attestation_buffer == NULL) {
        SSH_LOG(SSH_LOG_WARN, "attestation_buffer pointer should not be NULL");
        return SSH_ERROR;
    }

    *attestation_buffer = ssh_buffer_dup(context->sk_attestation_buffer);
    if (*attestation_buffer == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to duplicate attestation buffer");
        return SSH_ERROR;
    }

    return SSH_OK;
#else
    (void)context;
    (void)attestation_buffer;

    SSH_LOG(SSH_LOG_WARN, SK_NOT_SUPPORTED_MSG);
    return SSH_ERROR;
#endif /* WITH_FIDO2 */
}

/**
 * @brief Duplicate an existing PKI context
 *
 * Creates a new PKI context and copies all fields from the source context.
 * This function performs deep copying for all dynamically allocated fields
 * to ensure independent ownership between source and destination contexts.
 *
 * @param[in] context  The PKI context to copy from
 *
 * @return             New PKI context with copied data on success,
 *                     NULL on failure or if src_context is NULL
 */
ssh_pki_ctx ssh_pki_ctx_dup(const ssh_pki_ctx context)
{
    ssh_pki_ctx new_context = NULL;

    if (context == NULL) {
        return NULL;
    }

    new_context = ssh_pki_ctx_new();
    if (new_context == NULL) {
        goto error;
    }

    new_context->rsa_key_size = context->rsa_key_size;

#ifdef WITH_FIDO2
    new_context->sk_callbacks = context->sk_callbacks;

    // Free the default application string before copying
    SAFE_FREE(new_context->sk_application);

    if (context->sk_application != NULL) {
        new_context->sk_application = strdup(context->sk_application);
        if (new_context->sk_application == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Failed to copy SK application string");
            goto error;
        }
    }

    new_context->sk_flags = context->sk_flags;

    new_context->sk_pin_callback = context->sk_pin_callback;
    new_context->sk_userdata = context->sk_userdata;

    if (context->sk_challenge_buffer != NULL) {
        new_context->sk_challenge_buffer =
            ssh_buffer_dup(context->sk_challenge_buffer);
        if (new_context->sk_challenge_buffer == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Failed to copy SK challenge buffer");
            goto error;
        }
    }

    if (context->sk_callbacks_options != NULL) {
        new_context->sk_callbacks_options = sk_options_dup(
            (const struct sk_option **)context->sk_callbacks_options);
        if (new_context->sk_callbacks_options == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Failed to copy SK callbacks options");
            goto error;
        }
    }

    if (context->sk_attestation_buffer != NULL) {
        new_context->sk_attestation_buffer =
            ssh_buffer_dup(context->sk_attestation_buffer);
        if (new_context->sk_attestation_buffer == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Failed to copy SK attestation buffer");
            goto error;
        }
    }
#endif /* WITH_FIDO2 */

    return new_context;

error:
    SSH_PKI_CTX_FREE(new_context);
    return NULL;
}

/** @} */
