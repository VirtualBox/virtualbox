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

#include <stdlib.h>
#include <string.h>

#include "libssh/callbacks.h"
#include "libssh/priv.h"
#include "libssh/sk_common.h"

#ifdef HAVE_LIBFIDO2
#include "libssh/sk_usbhid.h"
#endif

const char *ssh_sk_err_to_string(int sk_err)
{
    switch (sk_err) {
    case SSH_SK_ERR_UNSUPPORTED:
        return "Unsupported operation";
    case SSH_SK_ERR_PIN_REQUIRED:
        return "PIN required but is either missing or invalid";
    case SSH_SK_ERR_DEVICE_NOT_FOUND:
        return "No suitable device found";
    case SSH_SK_ERR_CREDENTIAL_EXISTS:
        return "Credential already exists";
    case SSH_SK_ERR_GENERAL:
        return "General error";
    default:
        return "Unknown error";
    }
}

void sk_enroll_response_burn(struct sk_enroll_response *enroll_response)
{
    if (enroll_response == NULL) {
        return;
    }

    BURN_FREE(enroll_response->public_key, enroll_response->public_key_len);
    BURN_FREE(enroll_response->key_handle, enroll_response->key_handle_len);
    BURN_FREE(enroll_response->signature, enroll_response->signature_len);
    BURN_FREE(enroll_response->attestation_cert,
              enroll_response->attestation_cert_len);
    BURN_FREE(enroll_response->authdata, enroll_response->authdata_len);

    ssh_burn(enroll_response, sizeof(*enroll_response));
}

void sk_enroll_response_free(struct sk_enroll_response *enroll_response)
{
    sk_enroll_response_burn(enroll_response);
    SAFE_FREE(enroll_response);
}

void sk_sign_response_free(struct sk_sign_response *sign_response)
{
    if (sign_response == NULL) {
        return;
    }

    BURN_FREE(sign_response->sig_r, sign_response->sig_r_len);
    BURN_FREE(sign_response->sig_s, sign_response->sig_s_len);
    SAFE_FREE(sign_response);
}

void sk_resident_key_free(struct sk_resident_key *resident_key)
{
    if (resident_key == NULL) {
        return;
    }

    SAFE_FREE(resident_key->application);
    BURN_FREE(resident_key->user_id, resident_key->user_id_len);
    sk_enroll_response_burn(&resident_key->key);
    SAFE_FREE(resident_key);
}

void sk_options_free(struct sk_option **options)
{
    size_t i;

    if (options == NULL) {
        return;
    }

    for (i = 0; options[i] != NULL; i++) {
        SAFE_FREE(options[i]->name);
        SAFE_FREE(options[i]->value);
        SAFE_FREE(options[i]);
    }
    SAFE_FREE(options);
}

int sk_options_validate_get(const struct sk_option **options,
                            const char **keys,
                            char ***values)
{
    size_t i, j;
    size_t key_count = 0;
    int found;

    if (keys == NULL || values == NULL || options == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Invalid parameter(s) provided");
        return SSH_ERROR;
    }

    while (keys[key_count] != NULL) {
        key_count++;
    }

    *values = calloc(key_count + 1, sizeof(char *));
    if (*values == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate values array");
        return SSH_ERROR;
    }

    for (i = 0; options[i] != NULL; i++) {
        const struct sk_option *option = options[i];

        found = 0;

        /* Look for this option name in the supported keys */
        for (j = 0; j < key_count; j++) {

            if (strcmp(option->name, keys[j]) == 0) {
                /* Copy the value string if it exists */
                if (option->value != NULL) {
                    (*values)[j] = strdup(option->value);
                    if ((*values)[j] == NULL) {
                        SSH_LOG(SSH_LOG_WARN, "Failed to copy option value");
                        goto error;
                    }

                } else {
                    (*values)[j] = NULL;
                }
                found = 1;
                break;
            }
        }

        /* If option is required but not supported, fail */
        if (!found && option->required) {
            SSH_LOG(SSH_LOG_WARN,
                    "Required option '%s' is not supported",
                    option->name);
            goto error;
        }
    }

    return SSH_OK;

error:
    for (j = 0; j < key_count; j++) {
        SAFE_FREE((*values)[j]);
    }

    SAFE_FREE(*values);
    return SSH_ERROR;
}

struct sk_option **sk_options_dup(const struct sk_option **options)
{
    struct sk_option **new_options = NULL;
    size_t count = 0;
    size_t i;

    if (options == NULL) {
        return NULL;
    }

    /* Count the number of options */
    while (options[count] != NULL) {
        count++;
    }

    new_options = calloc(count + 1, sizeof(struct sk_option *));
    if (new_options == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for options array");
        return NULL;
    }

    /* Copy each option */
    for (i = 0; i < count; i++) {
        const struct sk_option *option = options[i];
        struct sk_option *new_option = NULL;

        new_option = calloc(1, sizeof(struct sk_option));
        if (new_option == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for option");
            goto error;
        }

        /* Copy option name */
        if (option->name != NULL) {
            new_option->name = strdup(option->name);
            if (new_option->name == NULL) {
                SSH_LOG(SSH_LOG_WARN, "Failed to copy option name");
                SAFE_FREE(new_option);
                goto error;
            }
        }

        /* Copy option value */
        if (option->value != NULL) {
            new_option->value = strdup(option->value);
            if (new_option->value == NULL) {
                SSH_LOG(SSH_LOG_WARN, "Failed to copy option value");
                SAFE_FREE(new_option->name);
                SAFE_FREE(new_option);
                goto error;
            }
        }

        new_option->required = option->required;
        new_options[i] = new_option;
    }

    new_options[count] = NULL;
    return new_options;

error:
    SK_OPTIONS_FREE(new_options);
    return NULL;
}

bool sk_callbacks_check_compatibility(
    const struct ssh_sk_callbacks_struct *callbacks)
{
    uint32_t callback_version;
    uint32_t callback_version_major;
    uint32_t libssh_version_major;

    if (callbacks == NULL) {
        SSH_LOG(SSH_LOG_WARN, "SK callbacks cannot be NULL");
        return false;
    }

    /* Check if the api_version callback is provided */
    if (!ssh_callbacks_exists(callbacks, api_version)) {
        SSH_LOG(SSH_LOG_WARN, "SK callbacks missing api_version callback");
        return false;
    }

    /* Extract major version from callback provider */
    callback_version = callbacks->api_version();
    callback_version_major = callback_version & SSH_SK_VERSION_MAJOR_MASK;

    libssh_version_major = SSH_SK_VERSION_MAJOR;

    /* Check if major versions are compatible */
    if (callback_version_major != libssh_version_major) {
        SSH_LOG(SSH_LOG_WARN,
                "SK API major version mismatch: callback provides 0x%08x, "
                "libssh supports 0x%08x",
                callback_version_major,
                libssh_version_major);
        return false;
    }

    return true;
}

const struct ssh_sk_callbacks_struct *ssh_sk_get_default_callbacks(void)
{
#ifdef HAVE_LIBFIDO2
    return ssh_sk_get_usbhid_callbacks();
#else
    return NULL;
#endif /* HAVE_LIBFIDO2 */
}
