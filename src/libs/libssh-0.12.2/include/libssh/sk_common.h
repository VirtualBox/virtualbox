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

#ifndef SK_COMMON_H
#define SK_COMMON_H

#include "libssh/callbacks.h"
#include "libssh/sk_api.h"

#include <stdbool.h>

#define SK_MAX_USER_ID_LEN 64

#define SK_NOT_SUPPORTED_MSG                                                \
    "Security Key functionality is not supported in this build of libssh. " \
    "Please enable support by building using the WITH_FIDO2 build option."

/**
 * @brief Convert security key error code to human-readable string
 *
 * Converts a security key error code to a descriptive string representation
 * that can be used for logging user-facing error messages.
 *
 * @param[in] sk_err The security key error code to convert.
 *
 * @return Constant string describing the error. Never returns NULL.
 *         Returns "Unknown error" for unrecognized error codes.
 *
 * @note The returned string is statically allocated and should not be freed.
 */
const char *ssh_sk_err_to_string(int sk_err);

/**
 * @brief Securely clear the contents of an sk_enroll_response structure
 *
 * Overwrites sensitive data within the enrollment response structure with
 * zeros to prevent information leakage. This function only clears and frees the
 * contents and does not free the structure itself.
 *
 * @param[in] enroll_response The enrollment response structure to clear.
 *                            Can be NULL (no operation performed).
 *
 * @note This function only frees the memory for the contents and does not free
 * memory for the structure itself. Use sk_enroll_response_free() for complete
 * cleanup, which also performs secure clearing internally.
 */
void sk_enroll_response_burn(struct sk_enroll_response *enroll_response);

/**
 * @brief Securely free an sk_enroll_response structure
 *
 * Performs secure clearing of sensitive data within the enrollment response
 * structure before freeing the allocated memory. This function internally
 * calls sk_enroll_response_burn() before deallocation.
 *
 * @param[in] enroll_response The enrollment response structure to free.
 *                            Can be NULL (no operation performed).
 *
 * @note Developers do not need to call sk_enroll_response_burn() before
 *       calling this function, as secure clearing is performed automatically.
 */
void sk_enroll_response_free(struct sk_enroll_response *enroll_response);

/**
 * @brief Free an sk_sign_response structure
 *
 * Frees the memory allocated for a sign response structure and all its
 * associated data. This function performs secure clearing of sensitive
 * data before deallocation.
 *
 * @param[in] sign_response The sign response structure to free.
 *                          Can be NULL (no operation performed).
 *
 * @note This is a secure free operation that clears sensitive data before
 *       memory deallocation to prevent information leakage.
 */
void sk_sign_response_free(struct sk_sign_response *sign_response);

/**
 * @brief Free an sk_resident_key structure
 *
 * Frees the memory allocated for a resident key structure and all its
 * associated data. This function performs secure clearing of sensitive
 * data before deallocation.
 *
 * @param[in] resident_key The resident key structure to free.
 *                         Can be NULL (no operation performed).
 *
 * @note This is a secure free operation that clears sensitive data before
 *       memory deallocation to prevent information leakage.
 */
void sk_resident_key_free(struct sk_resident_key *resident_key);

/**
 * @brief Free an sk_option array and all its contents
 *
 * Frees a NULL-terminated array of sk_option structures, including all
 * allocated memory for option names and values within each structure.
 *
 * @param[in] options NULL-terminated array of sk_option pointers to free.
 *                    Can be NULL (no operation performed).
 *
 * @note The options array must be NULL-terminated for proper freeing.
 *       Each sk_option structure and its name/value strings will be freed.
 */
void sk_options_free(struct sk_option **options);

/**
 * @brief Validate options and extract values for specific keys
 *
 * Validates that all required options are supported and extracts values
 * for the specified keys. This function is primarily intended for use
 * by the SK callback implementations.
 *
 * @param[in] options NULL-terminated array of sk_option pointers to validate.
 * @param[in] keys    NULL-terminated array of supported option keys.
 * @param[out] values Pointer to array that will be allocated and filled with
 *                    copied values (same order as keys). The caller must free
 *                    this array and all contained strings when done.
 *
 * @return SSH_OK on success, SSH_ERROR if unsupported required options found
 *         or memory allocation fails.
 *
 * @note The values array is allocated by this function and contains copies
 *       of the option values. The caller must free both the array and all
 *       non-NULL string values within it. Values for keys not found in
 *       options will be set to NULL.
 */
int sk_options_validate_get(const struct sk_option **options,
                            const char **keys,
                            char ***values);

/**
 * @brief Duplicate an array of sk_option structures
 *
 * Creates a deep copy of an array of security key options. Each option
 * structure and its string fields are duplicated.
 *
 * @param[in] options The array of options to duplicate. Must be
 *                    NULL-terminated array of struct sk_option pointers.
 *                    Can be NULL.
 *
 * @return A newly allocated array of duplicated options on success,
 *         NULL on failure or if options is NULL.
 *         The returned array should be freed with SK_OPTIONS_FREE().
 */
struct sk_option **sk_options_dup(const struct sk_option **options);

/**
 * @brief Check version compatibility of security key callbacks
 *
 * Validates that the provided security key callbacks use an SK API
 * version whose major portion is the same as the major version that libssh
 * supports.
 *
 * @param[in] callbacks Pointer to the sk_callbacks structure to check.
 *
 * @return true if the callbacks are compatible, false otherwise.
 */
bool sk_callbacks_check_compatibility(
    const struct ssh_sk_callbacks_struct *callbacks);

/* Convenience macros for secure freeing with NULL checks and pointer reset */
#define SK_ENROLL_RESPONSE_FREE(x)      \
    do {                                \
        if ((x) != NULL) {              \
            sk_enroll_response_free(x); \
            x = NULL;                   \
        }                               \
    } while (0)

#define SK_SIGN_RESPONSE_FREE(x)      \
    do {                              \
        if ((x) != NULL) {            \
            sk_sign_response_free(x); \
            x = NULL;                 \
        }                             \
    } while (0)

#define SK_RESIDENT_KEY_FREE(x)      \
    do {                             \
        if ((x) != NULL) {           \
            sk_resident_key_free(x); \
            x = NULL;                \
        }                            \
    } while (0)

#define SK_OPTIONS_FREE(x)      \
    do {                        \
        if ((x) != NULL) {      \
            sk_options_free(x); \
            x = NULL;           \
        }                       \
    } while (0)

#endif /* SK_COMMON_H */
