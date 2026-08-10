/*
 * pki.c
 * This file is part of the SSH Library
 *
 * Copyright (c) 2010 by Aris Adamantiadis
 * Copyright (c) 2011-2013 Andreas Schneider <asn@cryptomilk.org>
 * Copyright (c) 2019      Sahana Prasad     <sahana@redhat.com>
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

/**
 * @defgroup libssh_pki The SSH Public Key Infrastructure
 * @ingroup libssh
 *
 * Functions for the creation, importation and manipulation of public and
 * private keys in the context of the SSH protocol
 *
 * @{
 */

#include "config.h"
#include "libssh/wrapper.h"

#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "libssh/agent.h"
#include "libssh/buffer.h"
#include "libssh/keys.h"
#include "libssh/libssh.h"
#include "libssh/misc.h"
#include "libssh/pki.h"
#include "libssh/pki_context.h"
#include "libssh/pki_priv.h"
#include "libssh/pki_sk.h"
#include "libssh/priv.h"
#include "libssh/session.h"
#include "libssh/sk_common.h" /* For SK_NOT_SUPPORTED_MSG */

#ifndef MAX_LINE_SIZE
#define MAX_LINE_SIZE 4096
#endif /* NOT MAX_LINE_SIZE */

#define PKCS11_URI "pkcs11:"

enum ssh_keytypes_e pki_privatekey_type_from_string(const char *privkey)
{
    const char *start = NULL;

    start = strstr(privkey, RSA_HEADER_BEGIN);
    if (start != NULL) {
        return SSH_KEYTYPE_RSA;
    }

    start = strstr(privkey, ECDSA_HEADER_BEGIN);
    if (start != 0) {
        /* We don't know what the curve is at this point, so we don't actually
         * know the type. We figure out the actual curve and fix things up in
         * pki_private_key_from_base64 */
        return SSH_KEYTYPE_ECDSA_P256;
    }

    return SSH_KEYTYPE_UNKNOWN;
}

/**
 * @brief returns the ECDSA key name ("ecdsa-sha2-nistp256" for example)
 *
 * @param[in] key the ssh_key whose ECDSA name to get
 *
 * @returns the ECDSA key name ("ecdsa-sha2-nistp256" for example)
 *
 * @returns "unknown" if the ECDSA key name is not known
 */
const char *ssh_pki_key_ecdsa_name(const ssh_key key)
{
    if (key == NULL) {
        return NULL;
    }

#ifdef HAVE_ECC /* FIXME Better ECC check needed */
    return pki_key_ecdsa_nid_to_name(key->ecdsa_nid);
#else
    return NULL;
#endif /* HAVE_ECC */
}

/**
 * @brief creates a new empty SSH key
 *
 * @returns an empty ssh_key handle, or NULL on error.
 */
ssh_key ssh_key_new (void)
{
    ssh_key ptr = malloc (sizeof (struct ssh_key_struct));
    if (ptr == NULL) {
        return NULL;
    }
    ZERO_STRUCTP(ptr);
    return ptr;
}

/**
 * @internal
 *
 * @brief Initialize a new SSH key by duplicating common fields from an existing
 * key.
 *
 * This function creates a new SSH key and copies the common fields from the
 * source key, including the key type, type string, flags, and security key
 * fields if applicable. This is a helper function used by key duplication
 * routines.
 *
 * @param[in] key     The source ssh_key to copy common fields from.
 * @param[in] demote  Whether to demote the new key to public only. If non-zero,
 *                    only the public fields will be copied and the flags will
 *                    be set accordingly.
 *
 * @return            A new ssh_key with common fields initialized, or NULL on
 * error.
 *
 * @note The caller is responsible for freeing the returned key with
 * ssh_key_free().
 */
ssh_key pki_key_dup_common_init(const ssh_key key, int demote)
{
    ssh_key new = NULL;

    if (key == NULL) {
        return NULL;
    }

    new = ssh_key_new();
    if (new == NULL) {
        return NULL;
    }

    new->type = key->type;
    new->type_c = key->type_c;
    if (demote) {
        new->flags = SSH_KEY_FLAG_PUBLIC;
    } else {
        new->flags = key->flags;
    }

    /* Copy security key fields if present */
    if (is_sk_key_type(key->type)) {
        new->sk_application = ssh_string_copy(key->sk_application);
        if (new->sk_application == NULL) {
            goto fail;
        }

        if (key->sk_user_id != NULL) {
            new->sk_user_id = ssh_string_copy(key->sk_user_id);
            if (new->sk_user_id == NULL) {
                goto fail;
            }
        }

        if (!demote) {
            new->sk_flags = key->sk_flags;

            new->sk_key_handle = ssh_string_copy(key->sk_key_handle);
            if (new->sk_key_handle == NULL) {
                goto fail;
            }

            new->sk_reserved = ssh_string_copy(key->sk_reserved);
            if (new->sk_reserved == NULL) {
                goto fail;
            }
        }
    }

    return new;

fail:
    SSH_KEY_FREE(new);
    return NULL;
}

/**
 * @brief duplicates the key
 *
 * @param key An ssh_key to duplicate
 *
 * @return A duplicated ssh_key key
 */
ssh_key ssh_key_dup(const ssh_key key)
{
    if (key == NULL) {
        return NULL;
    }

    return pki_key_dup(key, 0);
}

/**
 * @brief clean up the key and deallocate all existing keys
 * @param[in] key ssh_key to clean
 */
void ssh_key_clean (ssh_key key)
{
    if (key == NULL)
        return;

    pki_key_clean(key);

#ifndef HAVE_LIBCRYPTO
    if (key->ed25519_privkey != NULL) {
        ssh_burn(key->ed25519_privkey, sizeof(ed25519_privkey));
        SAFE_FREE(key->ed25519_privkey);
    }
    SAFE_FREE(key->ed25519_pubkey);
#endif /* HAVE_LIBCRYPTO */
    if (key->cert != NULL) {
        SSH_BUFFER_FREE(key->cert);
    }
    if (is_sk_key_type(key->type)) {
        ssh_string_burn(key->sk_application);
        ssh_string_free(key->sk_application);
        ssh_string_burn(key->sk_key_handle);
        ssh_string_free(key->sk_key_handle);
        ssh_string_burn(key->sk_reserved);
        ssh_string_free(key->sk_reserved);
        ssh_string_burn(key->sk_user_id);
        ssh_string_free(key->sk_user_id);
        key->sk_flags = 0;
    }
    key->cert_type = SSH_KEYTYPE_UNKNOWN;
    key->flags = SSH_KEY_FLAG_EMPTY;
    key->type = SSH_KEYTYPE_UNKNOWN;
    key->ecdsa_nid = 0;
    key->type_c = NULL;
}

/**
 * @brief deallocate a SSH key
 * @param[in] key ssh_key handle to free
 */
void ssh_key_free (ssh_key key)
{
    if (key) {
        ssh_key_clean(key);
        SAFE_FREE(key);
    }
}

/**
 * @brief returns the type of a ssh key
 * @param[in] key the ssh_key handle
 * @returns one of SSH_KEYTYPE_RSA,
 *          SSH_KEYTYPE_ECDSA_P256, SSH_KEYTYPE_ECDSA_P384,
 *          SSH_KEYTYPE_ECDSA_P521, SSH_KEYTYPE_ED25519,
 *          SSH_KEYTYPE_RSA_CERT01, SSH_KEYTYPE_ECDSA_P256_CERT01,
 *          SSH_KEYTYPE_ECDSA_P384_CERT01, SSH_KEYTYPE_ECDSA_P521_CERT01, or
 *          SSH_KEYTYPE_ED25519_CERT01.
 * @returns SSH_KEYTYPE_UNKNOWN if the type is unknown
 */
enum ssh_keytypes_e ssh_key_type(const ssh_key key)
{
    if (key == NULL) {
        return SSH_KEYTYPE_UNKNOWN;
    }
    return key->type;
}

/**
 * @brief Get security key (FIDO2) flags for a security key backed ssh_key.
 *
 * The returned value contains a bitmask of SSH_SK_* flags (e.g.
 * SSH_SK_USER_PRESENCE_REQD, SSH_SK_USER_VERIFICATION_REQD, etc.).
 * If NULL is passed, then 0 is returned.
 *
 * @param[in] key  The ssh_key handle.
 *
 * @return Bitmask of security key flags, or 0 if not applicable.
 */
uint32_t ssh_key_get_sk_flags(const ssh_key key)
{
    if (key == NULL) {
        return 0;
    }
    return key->sk_flags;
}

/**
 * @brief Get the application (RP ID) associated with a security key.
 *
 * This function returns a freshly allocated ssh_string containing a copy of the
 * application (RP ID). The caller owns the returned ssh_string and must free it
 * with SSH_STRING_FREE() when no longer needed.
 *
 * Returns NULL if the key is NULL, not a security key type or if the field is
 * not set.
 *
 * @param[in] key  The ssh_key handle.
 *
 * @return ssh_string copy of the application (RP ID) or NULL if not available.
 */
ssh_string ssh_key_get_sk_application(const ssh_key key)
{
    if (key == NULL || key->sk_application == NULL) {
        return NULL;
    }

    return ssh_string_copy(key->sk_application);
}

/**
 * @brief Get a copy of the user ID associated with a resident security key
 * credential.
 *
 * For resident (discoverable) credentials, authenticators may provide a user
 * id which can be arbitrary binary data to allow for storing multiple keys for
 * the same Relying Party. This function returns a freshly allocated ssh_string
 * containing a copy of that user id. The caller owns the returned ssh_string
 * and must free it with SSH_STRING_FREE() when no longer needed.
 *
 * @note This function will only return useful information if the ssh_key
 * passed represents a resident key loaded using the ssh_sk_resident_keys_load()
 * function.
 *
 * @param[in] key  The ssh_key handle.
 *
 * @return ssh_string copy of user id or NULL if not available.
 */
ssh_string ssh_key_get_sk_user_id(const ssh_key key)
{
    if (key == NULL) {
        return NULL;
    }
    return ssh_string_copy(key->sk_user_id);
}

/**
 * @brief Convert a signature type to a string.
 *
 * @param[in]  type     The algorithm type to convert.
 *
 * @param[in] hash_type The hash type to convert
 *
 * @return              A string for the keytype or NULL if unknown.
 */
const char *
ssh_key_signature_to_char(enum ssh_keytypes_e type,
                          enum ssh_digest_e hash_type)
{
    switch (type) {
    case SSH_KEYTYPE_RSA:
        switch (hash_type) {
        case SSH_DIGEST_SHA256:
            return "rsa-sha2-256";
        case SSH_DIGEST_SHA512:
            return "rsa-sha2-512";
        case SSH_DIGEST_SHA1:
        case SSH_DIGEST_AUTO:
            return "ssh-rsa";
        default:
            return NULL;
        }
        break;
    case SSH_KEYTYPE_RSA_CERT01:
        switch (hash_type) {
        case SSH_DIGEST_SHA256:
            return "rsa-sha2-256-cert-v01@openssh.com";
        case SSH_DIGEST_SHA512:
            return "rsa-sha2-512-cert-v01@openssh.com";
        case SSH_DIGEST_SHA1:
        case SSH_DIGEST_AUTO:
            return "ssh-rsa-cert-v01@openssh.com";
        default:
            return NULL;
        }
        break;
    default:
        return ssh_key_type_to_char(type);
    }

    /* We should never reach this */
    return NULL;
}

/**
 * @brief Convert a key type to a string.
 *
 * @param[in]  type     The type to convert.
 *
 * @return              A string for the keytype or NULL if unknown.
 */
const char *ssh_key_type_to_char(enum ssh_keytypes_e type) {
  switch (type) {
    case SSH_KEYTYPE_RSA:
      return "ssh-rsa";
    case SSH_KEYTYPE_ECDSA:
      return "ssh-ecdsa"; /* deprecated. invalid value */
    case SSH_KEYTYPE_ECDSA_P256:
      return "ecdsa-sha2-nistp256";
    case SSH_KEYTYPE_ECDSA_P384:
      return "ecdsa-sha2-nistp384";
    case SSH_KEYTYPE_ECDSA_P521:
      return "ecdsa-sha2-nistp521";
    case SSH_KEYTYPE_ED25519:
      return "ssh-ed25519";
    case SSH_KEYTYPE_RSA_CERT01:
      return "ssh-rsa-cert-v01@openssh.com";
    case SSH_KEYTYPE_ECDSA_P256_CERT01:
      return "ecdsa-sha2-nistp256-cert-v01@openssh.com";
    case SSH_KEYTYPE_ECDSA_P384_CERT01:
      return "ecdsa-sha2-nistp384-cert-v01@openssh.com";
    case SSH_KEYTYPE_ECDSA_P521_CERT01:
      return "ecdsa-sha2-nistp521-cert-v01@openssh.com";
    case SSH_KEYTYPE_ED25519_CERT01:
      return "ssh-ed25519-cert-v01@openssh.com";
    case SSH_KEYTYPE_SK_ECDSA:
      return "sk-ecdsa-sha2-nistp256@openssh.com";
    case SSH_KEYTYPE_SK_ED25519:
      return "sk-ssh-ed25519@openssh.com";
    case SSH_KEYTYPE_SK_ECDSA_CERT01:
      return "sk-ecdsa-sha2-nistp256-cert-v01@openssh.com";
    case SSH_KEYTYPE_SK_ED25519_CERT01:
      return "sk-ssh-ed25519-cert-v01@openssh.com";
    case SSH_KEYTYPE_DSS:   /* deprecated */
    case SSH_KEYTYPE_RSA1:
    case SSH_KEYTYPE_DSS_CERT01:    /* deprecated */
    case SSH_KEYTYPE_UNKNOWN:
      return NULL;
  }

  /* We should never reach this */
  return NULL;
}

enum ssh_digest_e ssh_key_hash_from_name(const char *name)
{
    if (name == NULL) {
        /* TODO we should rather fail */
        return SSH_DIGEST_AUTO;
    }

    if (strcmp(name, "ssh-rsa") == 0) {
        return SSH_DIGEST_SHA1;
    } else if (strcmp(name, "rsa-sha2-256") == 0) {
        return SSH_DIGEST_SHA256;
    } else if (strcmp(name, "rsa-sha2-512") == 0) {
        return SSH_DIGEST_SHA512;
    } else if (strcmp(name, "ecdsa-sha2-nistp256") == 0) {
        return SSH_DIGEST_SHA256;
    } else if (strcmp(name, "ecdsa-sha2-nistp384") == 0) {
        return SSH_DIGEST_SHA384;
    } else if (strcmp(name, "ecdsa-sha2-nistp521") == 0) {
        return SSH_DIGEST_SHA512;
    } else if (strcmp(name, "ssh-ed25519") == 0) {
        return SSH_DIGEST_AUTO;
    } else if (strcmp(name, "sk-ecdsa-sha2-nistp256@openssh.com") == 0) {
        return SSH_DIGEST_SHA256;
    } else if (strcmp(name, "sk-ssh-ed25519@openssh.com") == 0) {
        return SSH_DIGEST_AUTO;
    }

    SSH_LOG(SSH_LOG_TRACE, "Unknown signature name %s", name);

    /* TODO we should rather fail */
    return SSH_DIGEST_AUTO;
}

/**
 * @brief Checks the given key against the configured allowed
 * public key algorithm types
 *
 * @param[in] session The SSH session
 * @param[in] type    The key algorithm to check
 * @returns           1 if the key algorithm is allowed, 0 otherwise
 */
int ssh_key_algorithm_allowed(ssh_session session, const char *type)
{
    const char *allowed_list = NULL;

    if (session->client) {
        allowed_list = session->opts.pubkey_accepted_types;
        if (allowed_list == NULL) {
            if (ssh_fips_mode()) {
                allowed_list = ssh_kex_get_fips_methods(SSH_HOSTKEYS);
            } else {
                allowed_list = ssh_kex_get_default_methods(SSH_HOSTKEYS);
            }
        }
    }
#ifdef WITH_SERVER
    else if (session->server) {
        allowed_list = session->opts.wanted_methods[SSH_HOSTKEYS];
        if (allowed_list == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "Session invalid: no host key available");
            return 0;
        }
    }
#endif /* WITH_SERVER */
    else {
        SSH_LOG(SSH_LOG_TRACE, "Session invalid: not set as client nor server");
        return 0;
    }

    SSH_LOG(SSH_LOG_DEBUG, "Checking %s with list <%s>", type, allowed_list);
    return match_group(allowed_list, type);
}

bool ssh_key_size_allowed_rsa(int min_size, ssh_key key)
{
    int key_size = ssh_key_size(key);

    if (min_size < RSA_MIN_KEY_SIZE) {
        if (ssh_fips_mode()) {
            min_size = RSA_MIN_FIPS_KEY_SIZE;
        } else {
            min_size = RSA_MIN_KEY_SIZE;
        }
    }
    return (key_size >= min_size);
}

/**
 * @brief Check the given key is acceptable in regards to the key size policy
 * specified by the configuration
 *
 * @param[in] session The SSH session
 * @param[in] key     The SSH key
 * @returns           true if the key is allowed, false otherwise
 */
bool ssh_key_size_allowed(ssh_session session, ssh_key key)
{
    int min_size = 0;

    switch (ssh_key_type(key)) {
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA_CERT01:
        min_size = session->opts.rsa_min_size;
        return ssh_key_size_allowed_rsa(min_size, key);
    default:
        return true;
    }
}

/**
 * @brief Helper function to convert a key type to a hash type.
 *
 * @param[in]  type     The type to convert.
 *
 * @return              A hash type to be used.
 *
 * @warning This helper function is available for use without session (for
 *          example for signing commits) and might cause interoperability issues
 *          when used within session! It is recommended to use
 *          ssh_key_type_to_hash() instead of this helper directly when a
 *          session is available.
 *
 * @note    In order to follow current security best practises for RSA, defaults
 *          to SHA-2 with SHA-512 digest (RFC8332) instead of the default for
 *          the SSH protocol (SHA1 with RSA ; RFC 4253).
 *
 * @see     ssh_key_type_to_hash()
 */
static enum ssh_digest_e key_type_to_hash(enum ssh_keytypes_e type)
{
    switch (type) {
    case SSH_KEYTYPE_RSA_CERT01:
    case SSH_KEYTYPE_RSA:
        return SSH_DIGEST_SHA512;
    case SSH_KEYTYPE_ECDSA_P256_CERT01:
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_SK_ECDSA:
        return SSH_DIGEST_SHA256;
    case SSH_KEYTYPE_ECDSA_P384_CERT01:
    case SSH_KEYTYPE_ECDSA_P384:
        return SSH_DIGEST_SHA384;
    case SSH_KEYTYPE_ECDSA_P521_CERT01:
    case SSH_KEYTYPE_ECDSA_P521:
        return SSH_DIGEST_SHA512;
    case SSH_KEYTYPE_ED25519_CERT01:
    case SSH_KEYTYPE_ED25519:
    case SSH_KEYTYPE_SK_ED25519:
        return SSH_DIGEST_AUTO;
    case SSH_KEYTYPE_RSA1:
    case SSH_KEYTYPE_DSS:        /* deprecated */
    case SSH_KEYTYPE_DSS_CERT01: /* deprecated */
    case SSH_KEYTYPE_ECDSA:
    case SSH_KEYTYPE_UNKNOWN:
    default:
        SSH_LOG(SSH_LOG_WARN,
                "Digest algorithm to be used with key type %u "
                "is not defined",
                type);
    }

    /* We should never reach this */
    return SSH_DIGEST_AUTO;
}

/**
 * @brief Convert a key type to a hash type. This is usually unambiguous
 * for all the key types, unless the SHA2 extension (RFC 8332) is
 * negotiated during key exchange.
 *
 * @param[in]  session  SSH Session.
 *
 * @param[in]  type     The type to convert.
 *
 * @return              A hash type to be used.
 */
enum ssh_digest_e ssh_key_type_to_hash(ssh_session session,
                                       enum ssh_keytypes_e type)
{
    switch (type) {
    case SSH_KEYTYPE_RSA_CERT01:
        /* If we are talking to an old OpenSSH version which does not support
         * SHA2 in certificates */
        if ((session->openssh > 0) &&
            (session->openssh < SSH_VERSION_INT(7, 2, 0)))
        {
            SSH_LOG(SSH_LOG_DEBUG,
                    "We are talking to an old OpenSSH (%x); "
                    "returning SSH_DIGEST_SHA1",
                    session->openssh);

            return SSH_DIGEST_SHA1;
        }
        FALL_THROUGH;
    case SSH_KEYTYPE_RSA:
        if (ssh_key_algorithm_allowed(session, "rsa-sha2-512") &&
            (session->extensions & SSH_EXT_SIG_RSA_SHA512)) {
            return SSH_DIGEST_SHA512;
        }

        if (ssh_key_algorithm_allowed(session, "rsa-sha2-256") &&
            (session->extensions & SSH_EXT_SIG_RSA_SHA256)) {
            return SSH_DIGEST_SHA256;
        }

        /* Default algorithm for RSA is SHA1 */
        return SSH_DIGEST_SHA1;

    default:
        return key_type_to_hash(type);
    }

    /* We should never reach this */
    return SSH_DIGEST_AUTO;
}

/**
 * @brief Gets signature algorithm name to be used with the given
 *        key type.
 *
 * @param[in]  session  SSH session.
 * @param[in]  type     The algorithm type to convert.
 *
 * @return              A string for the keytype or NULL if unknown.
 */
const char *
ssh_key_get_signature_algorithm(ssh_session session,
                                enum ssh_keytypes_e type)
{
    enum ssh_digest_e hash_type;

    if (type == SSH_KEYTYPE_RSA_CERT01) {
        /* If we are talking to an old OpenSSH version which does not support
         * rsa-sha2-{256,512}-cert-v01@openssh.com */
        if ((session->openssh > 0) &&
            (session->openssh < SSH_VERSION_INT(7, 8, 0)))
        {
            SSH_LOG(SSH_LOG_DEBUG,
                    "We are talking to an old OpenSSH (%x); "
                    "using old cert format",
                    session->openssh);

            return "ssh-rsa-cert-v01@openssh.com";
        }
    }

    hash_type = ssh_key_type_to_hash(session, type);

    return ssh_key_signature_to_char(type, hash_type);
}

/**
 * @brief Convert a ssh key algorithm name to a ssh key algorithm type.
 *
 * @param[in] name      The name to convert.
 *
 * @return              The enum ssh key algorithm type.
 */
enum ssh_keytypes_e ssh_key_type_from_signature_name(const char *name) {
    if (name == NULL) {
        return SSH_KEYTYPE_UNKNOWN;
    }

    if ((strcmp(name, "rsa-sha2-256") == 0) ||
        (strcmp(name, "rsa-sha2-512") == 0)) {
        return SSH_KEYTYPE_RSA;
    }

    /* Otherwise the key type matches the signature type */
    return ssh_key_type_from_name(name);
}

/**
 * @brief Convert a ssh key name to a ssh key type.
 *
 * @param[in] name      The name to convert.
 *
 * @return              The enum ssh key type.
 */
enum ssh_keytypes_e ssh_key_type_from_name(const char *name)
{
    if (name == NULL) {
        return SSH_KEYTYPE_UNKNOWN;
    }

    if (strcmp(name, "rsa") == 0) {
        return SSH_KEYTYPE_RSA;
    } else if (strcmp(name, "ssh-rsa") == 0) {
        return SSH_KEYTYPE_RSA;
    } else if (strcmp(name, "ssh-ecdsa") == 0
            || strcmp(name, "ecdsa") == 0
            || strcmp(name, "ecdsa-sha2-nistp256") == 0) {
        return SSH_KEYTYPE_ECDSA_P256;
    } else if (strcmp(name, "ecdsa-sha2-nistp384") == 0) {
        return SSH_KEYTYPE_ECDSA_P384;
    } else if (strcmp(name, "ecdsa-sha2-nistp521") == 0) {
        return SSH_KEYTYPE_ECDSA_P521;
    } else if (strcmp(name, "ssh-ed25519") == 0){
        return SSH_KEYTYPE_ED25519;
    } else if (strcmp(name, "ssh-rsa-cert-v01@openssh.com") == 0) {
        return SSH_KEYTYPE_RSA_CERT01;
    } else if (strcmp(name, "ecdsa-sha2-nistp256-cert-v01@openssh.com") == 0) {
        return SSH_KEYTYPE_ECDSA_P256_CERT01;
    } else if (strcmp(name, "ecdsa-sha2-nistp384-cert-v01@openssh.com") == 0) {
        return SSH_KEYTYPE_ECDSA_P384_CERT01;
    } else if (strcmp(name, "ecdsa-sha2-nistp521-cert-v01@openssh.com") == 0) {
        return SSH_KEYTYPE_ECDSA_P521_CERT01;
    } else if (strcmp(name, "ssh-ed25519-cert-v01@openssh.com") == 0) {
        return SSH_KEYTYPE_ED25519_CERT01;
    } else if(strcmp(name, "sk-ecdsa-sha2-nistp256@openssh.com") == 0) {
        return SSH_KEYTYPE_SK_ECDSA;
    } else if(strcmp(name, "sk-ecdsa-sha2-nistp256-cert-v01@openssh.com") == 0) {
        return SSH_KEYTYPE_SK_ECDSA_CERT01;
    } else if(strcmp(name, "sk-ssh-ed25519@openssh.com") == 0) {
        return SSH_KEYTYPE_SK_ED25519;
    } else if(strcmp(name, "sk-ssh-ed25519-cert-v01@openssh.com") == 0) {
        return SSH_KEYTYPE_SK_ED25519_CERT01;
    }

    return SSH_KEYTYPE_UNKNOWN;
}

/**
 * @brief Get the public key type corresponding to a certificate type.
 *
 * @param[in] type   The certificate or public key type.
 *
 * @return           The matching public key type.
 */
enum ssh_keytypes_e ssh_key_type_plain(enum ssh_keytypes_e type)
{
    switch (type) {
        case SSH_KEYTYPE_RSA_CERT01:
            return SSH_KEYTYPE_RSA;
        case SSH_KEYTYPE_ECDSA_P256_CERT01:
            return SSH_KEYTYPE_ECDSA_P256;
        case SSH_KEYTYPE_ECDSA_P384_CERT01:
            return SSH_KEYTYPE_ECDSA_P384;
        case SSH_KEYTYPE_ECDSA_P521_CERT01:
            return SSH_KEYTYPE_ECDSA_P521;
        case SSH_KEYTYPE_ED25519_CERT01:
            return SSH_KEYTYPE_ED25519;
        case SSH_KEYTYPE_SK_ECDSA_CERT01:
            return SSH_KEYTYPE_SK_ECDSA;
        case SSH_KEYTYPE_SK_ED25519_CERT01:
            return SSH_KEYTYPE_SK_ED25519;
        default:
            return type;
    }
}

/**
 * @brief Check if the key has/is a public key.
 *
 * @param[in] k         The key to check.
 *
 * @return              1 if it is a public key, 0 if not.
 */
int ssh_key_is_public(const ssh_key k)
{
    if (k == NULL) {
        return 0;
    }

    return (k->flags & SSH_KEY_FLAG_PUBLIC) == SSH_KEY_FLAG_PUBLIC;
}

/**
 * @brief Check if the key is a private key.
 *
 * @param[in] k         The key to check.
 *
 * @return              1 if it is a private key, 0 if not.
 */
int ssh_key_is_private(const ssh_key k) {
    if (k == NULL) {
        return 0;
    }

    return (k->flags & SSH_KEY_FLAG_PRIVATE) == SSH_KEY_FLAG_PRIVATE;
}

/**
 * @brief Compare keys if they are equal.
 *
 * Note that comparing private keys is almost never needed. The private key
 * is cryptographically bound to the public key and comparing public keys should
 * always be preferred.
 *
 * @param[in] k1        The first key to compare.
 *
 * @param[in] k2        The second key to compare.
 *
 * @param[in] what      What part or type of the key do you want to compare.
 *
 * @return              0 if equal, 1 if not.
 */
int ssh_key_cmp(const ssh_key k1,
                const ssh_key k2,
                enum ssh_keycmp_e what)
{
    if (k1 == NULL || k2 == NULL) {
        return 1;
    }

    if (ssh_key_type_plain(k1->type) != ssh_key_type_plain(k2->type)) {
        SSH_LOG(SSH_LOG_DEBUG, "key types don't match!");
        return 1;
    }

    if (what == SSH_KEY_CMP_PRIVATE) {
        if (!ssh_key_is_private(k1) ||
            !ssh_key_is_private(k2)) {
            return 1;
        }
    }

    if (is_sk_key_type(k1->type)) {
        if (ssh_string_cmp(k1->sk_application, k2->sk_application) != 0) {
            return 1;
        }

        if (ssh_string_cmp(k1->sk_user_id, k2->sk_user_id) != 0) {
            return 1;
        }

        if (what == SSH_KEY_CMP_PRIVATE) {
            if (k1->sk_flags != k2->sk_flags) {
                return 1;
            }

            if (ssh_string_cmp(k1->sk_key_handle, k2->sk_key_handle) != 0) {
                return 1;
            }

            if (ssh_string_cmp(k1->sk_reserved, k2->sk_reserved) != 0) {
                return 1;
            }
        }
    }

    if (what == SSH_KEY_CMP_CERTIFICATE) {
        if (!is_cert_type(k1->type) ||
            !is_cert_type(k2->type)) {
            return 1;
        }
        if (k1->cert == NULL || k2->cert == NULL) {
            return 1;
        }
        if (ssh_buffer_get_len(k1->cert) != ssh_buffer_get_len(k2->cert)) {
            return 1;
        }
        return memcmp(ssh_buffer_get(k1->cert),
                      ssh_buffer_get(k2->cert),
                      ssh_buffer_get_len(k1->cert));
    }

#ifndef HAVE_LIBCRYPTO
    if (ssh_key_type_plain(k1->type) == SSH_KEYTYPE_ED25519) {
        return pki_ed25519_key_cmp(k1, k2, what);
    } else if (ssh_key_type_plain(k1->type) == SSH_KEYTYPE_SK_ED25519) {
        return pki_ed25519_key_cmp(k1, k2, SSH_KEY_CMP_PUBLIC);
    }
#endif

    return pki_key_compare(k1, k2, what);
}

ssh_signature ssh_signature_new(void)
{
    struct ssh_signature_struct *sig = NULL;

    sig = calloc(1, sizeof(struct ssh_signature_struct));
    if (sig == NULL) {
        return NULL;
    }

    return sig;
}

void ssh_signature_free(ssh_signature sig)
{
    if (sig == NULL) {
        return;
    }

    switch(sig->type) {
        case SSH_KEYTYPE_RSA:
#ifdef HAVE_LIBGCRYPT
            gcry_sexp_release(sig->rsa_sig);
#elif defined HAVE_LIBMBEDCRYPTO
            SAFE_FREE(sig->rsa_sig);
#endif /* HAVE_LIBGCRYPT */
            break;
        case SSH_KEYTYPE_ECDSA_P256:
        case SSH_KEYTYPE_ECDSA_P384:
        case SSH_KEYTYPE_ECDSA_P521:
        case SSH_KEYTYPE_SK_ECDSA:
#ifdef HAVE_GCRYPT_ECC
            gcry_sexp_release(sig->ecdsa_sig);
#elif defined HAVE_LIBMBEDCRYPTO
            bignum_safe_free(sig->ecdsa_sig.r);
            bignum_safe_free(sig->ecdsa_sig.s);
#endif /* HAVE_GCRYPT_ECC */
            break;
        case SSH_KEYTYPE_ED25519:
        case SSH_KEYTYPE_SK_ED25519:
#ifndef HAVE_LIBCRYPTO
            /* When using OpenSSL, the signature is stored in sig->raw_sig */
            SAFE_FREE(sig->ed25519_sig);
#endif /* HAVE_LIBCRYPTO */
            break;
        case SSH_KEYTYPE_DSS:   /* deprecated */
        case SSH_KEYTYPE_DSS_CERT01:    /* deprecated */
        case SSH_KEYTYPE_RSA_CERT01:
        case SSH_KEYTYPE_ECDSA_P256_CERT01:
        case SSH_KEYTYPE_ECDSA_P384_CERT01:
        case SSH_KEYTYPE_ECDSA_P521_CERT01:
        case SSH_KEYTYPE_ED25519_CERT01:
        case SSH_KEYTYPE_SK_ECDSA_CERT01:
        case SSH_KEYTYPE_SK_ED25519_CERT01:
        case SSH_KEYTYPE_RSA1:
        case SSH_KEYTYPE_ECDSA:
        case SSH_KEYTYPE_UNKNOWN:
            break;
    }

    /* Explicitly zero the signature content before free */
    ssh_string_burn(sig->raw_sig);
    SSH_STRING_FREE(sig->raw_sig);
    SAFE_FREE(sig);
}

/**
 * @brief import a base64 formatted key from a memory c-string
 *
 * @param[in]  b64_key  The c-string holding the base64 encoded key
 *
 * @param[in]  passphrase The passphrase to decrypt the key, or NULL
 *
 * @param[in]  auth_fn  An auth function you may want to use or NULL.
 *
 * @param[in]  auth_data Private data passed to the auth function.
 *
 * @param[out] pkey     A pointer where the allocated key can be stored. You
 *                      need to free the memory using ssh_key_free()
 *
 * @return  SSH_ERROR in case of error, SSH_OK otherwise.
 *
 * @see ssh_key_free()
 */
int ssh_pki_import_privkey_base64(const char *b64_key,
                                  const char *passphrase,
                                  ssh_auth_callback auth_fn,
                                  void *auth_data,
                                  ssh_key *pkey)
{
    ssh_key key = NULL;
    const char *openssh_header = NULL;

    if (b64_key == NULL || pkey == NULL) {
        return SSH_ERROR;
    }

    if (b64_key == NULL || !*b64_key) {
        return SSH_ERROR;
    }

    SSH_LOG(SSH_LOG_DEBUG,
            "Trying to decode privkey passphrase=%s",
            passphrase ? "true" : "false");

    /* Test for OpenSSH key format first */
    openssh_header = strstr(b64_key, OPENSSH_HEADER_BEGIN);
    if (openssh_header != NULL) {
        key = ssh_pki_openssh_privkey_import(openssh_header,
                                             passphrase,
                                             auth_fn,
                                             auth_data);
    } else {
        /* fallback on PEM decoder */
        key = pki_private_key_from_base64(b64_key,
                                          passphrase,
                                          auth_fn,
                                          auth_data);
    }
    if (key == NULL) {
        return SSH_ERROR;
    }

    *pkey = key;

    return SSH_OK;
}


 /**
 * @brief Convert a private key to a base64 encoded key in given format
 *
 * @param[in]  privkey  The private key to export.
 *
 * @param[in]  passphrase The passphrase to use to encrypt the key with or
 *             NULL. An empty string means no passphrase.
 *
 * @param[in]  auth_fn  An auth function you may want to use or NULL.
 *
 * @param[in]  auth_data Private data passed to the auth function.
 *
 * @param[out] b64_key  A pointer to store the allocated base64 encoded key. You
 *                      need to free the buffer using ssh_string_from_char().
 *
 * @param[in]  format   The file format (OpenSSH, PEM, or default)
 *
 * @return     SSH_OK on success, SSH_ERROR on error.
 *
 * @see ssh_string_free_char()
 */
int
ssh_pki_export_privkey_base64_format(const ssh_key privkey,
                                     const char *passphrase,
                                     ssh_auth_callback auth_fn,
                                     void *auth_data,
                                     char **b64_key,
                                     enum ssh_file_format_e format)
{
    ssh_string blob = NULL;
    char *b64 = NULL;

    if (privkey == NULL || !ssh_key_is_private(privkey)) {
        return SSH_ERROR;
    }

    /*
     * For historic reasons, the Ed25519 keys are exported in OpenSSH file
     * format by default also when built with OpenSSL.
     *
     * The FIDO2/U2F security keys are an extension to the SSH protocol
     * proposed by OpenSSH, and do not have any representation in PEM format.
     * So, they are always exported in the OpenSSH file format.
     */
#ifdef HAVE_LIBCRYPTO
    if (format == SSH_FILE_FORMAT_DEFAULT &&
        privkey->type != SSH_KEYTYPE_ED25519 &&
        !is_sk_key_type(privkey->type)) {
        format = SSH_FILE_FORMAT_PEM;
    }
#endif /* HAVE_LIBCRYPTO */

    switch (format) {
    case SSH_FILE_FORMAT_PEM:
        blob = pki_private_key_to_pem(privkey,
                                      passphrase,
                                      auth_fn,
                                      auth_data);
        break;
    case SSH_FILE_FORMAT_DEFAULT:
        /* default except (OpenSSL && !ED25519) handled above */
    case SSH_FILE_FORMAT_OPENSSH:
        blob = ssh_pki_openssh_privkey_export(privkey,
                                              passphrase,
                                              auth_fn,
                                              auth_data);
        break;
    }
    if (blob == NULL) {
        return SSH_ERROR;
    }

    b64 = strndup(ssh_string_data(blob), ssh_string_len(blob));
    SSH_STRING_FREE(blob);
    if (b64 == NULL) {
        return SSH_ERROR;
    }

    *b64_key = b64;

    return SSH_OK;
}

 /**
 * @brief Convert a private key to a pem base64 encoded key, or OpenSSH format for
 *        keytype ssh-ed25519
 *
 * @param[in]  privkey  The private key to export.
 *
 * @param[in]  passphrase The passphrase to use to encrypt the key with or
 *             NULL. An empty string means no passphrase.
 *
 * @param[in]  auth_fn  An auth function you may want to use or NULL.
 *
 * @param[in]  auth_data Private data passed to the auth function.
 *
 * @param[out] b64_key  A pointer to store the allocated base64 encoded key. You
 *                      need to free the buffer using ssh_string_from_char().
 *
 * @return     SSH_OK on success, SSH_ERROR on error.
 *
 * @see ssh_string_free_char()
 */
int ssh_pki_export_privkey_base64(const ssh_key privkey,
                                  const char *passphrase,
                                  ssh_auth_callback auth_fn,
                                  void *auth_data,
                                  char **b64_key)
{
    return ssh_pki_export_privkey_base64_format(privkey,
                                                passphrase,
                                                auth_fn,
                                                auth_data,
                                                b64_key,
                                                SSH_FILE_FORMAT_DEFAULT);
}



/**
 * @brief Import a private key from a file or a PKCS #11 device.
 *
 * @param[in]  filename The filename of the private key or the
 *                      PKCS #11 URI corresponding to the private key.
 *
 * @param[in]  passphrase The passphrase to decrypt the private key. Set to NULL
 *                        if none is needed or it is unknown.
 *
 * @param[in]  auth_fn  An auth function you may want to use or NULL.
 *
 * @param[in]  auth_data Private data passed to the auth function.
 *
 * @param[out] pkey     A pointer to store the allocated ssh_key. You need to
 *                      free the key using ssh_key_free().
 *
 * @returns SSH_OK on success, SSH_EOF if the file doesn't exist or permission
 *          denied, SSH_ERROR otherwise.
 *
 * @see ssh_key_free()
 **/
int ssh_pki_import_privkey_file(const char *filename,
                                const char *passphrase,
                                ssh_auth_callback auth_fn,
                                void *auth_data,
                                ssh_key *pkey) {
    struct stat sb;
    char *key_buf = NULL;
    FILE *file = NULL;
    off_t size;
    int rc;
    char err_msg[SSH_ERRNO_MSG_MAX] = {0};

    if (pkey == NULL || filename == NULL || *filename == '\0') {
        return SSH_ERROR;
    }

#ifdef WITH_PKCS11_URI
    if (ssh_pki_is_uri(filename)) {
        rc = pki_uri_import(filename, pkey, SSH_KEY_PRIVATE);
        return rc;
    }
#endif /* WITH_PKCS11_URI */

    file = fopen(filename, "rb");
    if (file == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Error opening %s: %s",
                filename,
                ssh_strerror(errno, err_msg, SSH_ERRNO_MSG_MAX));
        return SSH_EOF;
    }

    rc = fstat(fileno(file), &sb);
    if (rc < 0) {
        fclose(file);
        SSH_LOG(SSH_LOG_TRACE,
                "Error getting stat of %s: %s",
                filename,
                ssh_strerror(errno, err_msg, SSH_ERRNO_MSG_MAX));
        switch (errno) {
            case ENOENT:
            case EACCES:
                return SSH_EOF;
        }

        return SSH_ERROR;
    }

    if (sb.st_size > MAX_PRIVKEY_SIZE) {
        SSH_LOG(SSH_LOG_TRACE,
                "Private key is bigger than 4M.");
        fclose(file);
        return SSH_ERROR;
    }

    key_buf = malloc(sb.st_size + 1);
    if (key_buf == NULL) {
        fclose(file);
        SSH_LOG(SSH_LOG_TRACE, "Out of memory!");
        return SSH_ERROR;
    }

    size = fread(key_buf, 1, sb.st_size, file);
    fclose(file);

    if (size != sb.st_size) {
        SAFE_FREE(key_buf);
        SSH_LOG(SSH_LOG_TRACE,
                "Error reading %s: %s",
                filename,
                ssh_strerror(errno, err_msg, SSH_ERRNO_MSG_MAX));
        return SSH_ERROR;
    }
    key_buf[size] = 0;

    rc = ssh_pki_import_privkey_base64(key_buf,
                                       passphrase,
                                       auth_fn,
                                       auth_data,
                                       pkey);

    SAFE_FREE(key_buf);
    return rc;
}

/**
 * @brief Export a private key to a file in format specified in the argument
 *
 * @param[in]  privkey  The private key to export.
 *
 * @param[in]  passphrase The passphrase to use to encrypt the key with or
 *             NULL. An empty string means no passphrase.
 *
 * @param[in]  auth_fn  An auth function you may want to use or NULL.
 *
 * @param[in]  auth_data Private data passed to the auth function.
 *
 * @param[in]  filename  The path where to store the pem file.
 *
 * @param[in]  format    The file format (OpenSSH, PEM, or default)
 *
 * @return     SSH_OK on success, SSH_ERROR on error.
 */

int
ssh_pki_export_privkey_file_format(const ssh_key privkey,
                                   const char *passphrase,
                                   ssh_auth_callback auth_fn,
                                   void *auth_data,
                                   const char *filename,
                                   enum ssh_file_format_e format)
{
    ssh_string blob = NULL;
    FILE *fp = NULL;
    int rc;

    if (privkey == NULL || !ssh_key_is_private(privkey)) {
        return SSH_ERROR;
    }

    fp = fopen(filename, "wb");
    if (fp == NULL) {
        char err_msg[SSH_ERRNO_MSG_MAX] = {0};
        SSH_LOG(SSH_LOG_FUNCTIONS, "Error opening %s: %s",
                filename, ssh_strerror(errno, err_msg, SSH_ERRNO_MSG_MAX));
        return SSH_EOF;
    }

    /*
     * For historic reasons, the Ed25519 keys are exported in OpenSSH file
     * format by default also when built with OpenSSL.
     *
     * The FIDO2/U2F security keys are an extension to the SSH protocol
     * proposed by OpenSSH, and do not have any representation in PEM format.
     * So, they are always exported in the OpenSSH file format.
     */
#ifdef HAVE_LIBCRYPTO
    if (format == SSH_FILE_FORMAT_DEFAULT &&
        privkey->type != SSH_KEYTYPE_ED25519 &&
        !is_sk_key_type(privkey->type)) {

        format = SSH_FILE_FORMAT_PEM;
    }
#endif /* HAVE_LIBCRYPTO */

    switch (format) {
    case SSH_FILE_FORMAT_PEM:
        blob = pki_private_key_to_pem(privkey,
                                      passphrase,
                                      auth_fn,
                                      auth_data);
        break;
    case SSH_FILE_FORMAT_DEFAULT:
        /* default except (OpenSSL && !ED25519) handled above */
    case SSH_FILE_FORMAT_OPENSSH:
        blob = ssh_pki_openssh_privkey_export(privkey,
                                              passphrase,
                                              auth_fn,
                                              auth_data);
        break;
    }
    if (blob == NULL) {
        fclose(fp);
        return -1;
    }

    rc = fwrite(ssh_string_data(blob), ssh_string_len(blob), 1, fp);
    SSH_STRING_FREE(blob);
    if (rc != 1 || ferror(fp)) {
        fclose(fp);
        unlink(filename);
        return SSH_ERROR;
    }
    fclose(fp);

    return SSH_OK;
}

/**
 * @brief Export a private key to a pem file on disk, or OpenSSH format for
 *        keytype ssh-ed25519
 *
 * @param[in]  privkey  The private key to export.
 *
 * @param[in]  passphrase The passphrase to use to encrypt the key with or
 *             NULL. An empty string means no passphrase.
 *
 * @param[in]  auth_fn  An auth function you may want to use or NULL.
 *
 * @param[in]  auth_data Private data passed to the auth function.
 *
 * @param[in]  filename  The path where to store the pem file.
 *
 * @return     SSH_OK on success, SSH_ERROR on error.
 */
int
ssh_pki_export_privkey_file(const ssh_key privkey,
                            const char *passphrase,
                            ssh_auth_callback auth_fn,
                            void *auth_data,
                            const char *filename)
{
    return ssh_pki_export_privkey_file_format(privkey,
                                              passphrase,
                                              auth_fn,
                                              auth_data,
                                              filename,
                                              SSH_FILE_FORMAT_DEFAULT);
}

/* temporary function to migrate seamlessly to ssh_key */
ssh_public_key ssh_pki_convert_key_to_publickey(const ssh_key key)
{
    ssh_public_key pub = NULL;
    ssh_key tmp = NULL;

    if (key == NULL) {
        return NULL;
    }

    tmp = ssh_key_dup(key);
    if (tmp == NULL) {
        return NULL;
    }

    pub = calloc(1, sizeof(struct ssh_public_key_struct));
    if (pub == NULL) {
        ssh_key_free(tmp);
        return NULL;
    }

    pub->type = tmp->type;
    pub->type_c = tmp->type_c;

#if defined(HAVE_LIBMBEDCRYPTO)
    pub->rsa_pub = tmp->pk;
    tmp->pk = NULL;
#elif defined(HAVE_LIBCRYPTO)
    pub->key_pub = tmp->key;
    tmp->key = NULL;
#else
    pub->rsa_pub = tmp->rsa;
    tmp->rsa = NULL;
#endif /* HAVE_LIBCRYPTO */

    ssh_key_free(tmp);

    return pub;
}

ssh_private_key ssh_pki_convert_key_to_privatekey(const ssh_key key)
{
    ssh_private_key privkey = NULL;

    privkey = calloc(1, sizeof(struct ssh_private_key_struct));
    if (privkey == NULL) {
        ssh_key_free(key);
        return NULL;
    }

    privkey->type = key->type;
#if defined(HAVE_LIBMBEDCRYPTO)
    privkey->rsa_priv = key->pk;
#elif defined(HAVE_LIBCRYPTO)
    privkey->key_priv = key->key;
#else
    privkey->rsa_priv = key->rsa;
#endif /* HAVE_LIBCRYPTO */

    return privkey;
}

int pki_import_privkey_buffer(enum ssh_keytypes_e type,
                              ssh_buffer buffer,
                              ssh_key *pkey)
{
    ssh_key key = NULL;
    int rc;

    key = ssh_key_new();
    if (key == NULL) {
        return SSH_ERROR;
    }

    key->type = type;
    key->type_c = ssh_key_type_to_char(type);
    key->flags = SSH_KEY_FLAG_PRIVATE | SSH_KEY_FLAG_PUBLIC;

    switch (type) {
    case SSH_KEYTYPE_RSA: {
        ssh_string n = NULL;
        ssh_string e = NULL;
        ssh_string d = NULL;
        ssh_string iqmp = NULL;
        ssh_string p = NULL;
        ssh_string q = NULL;

        rc = ssh_buffer_unpack(buffer, "SSSSSS", &n, &e, &d, &iqmp, &p, &q);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_TRACE, "Unpack error");
            goto fail;
        }

        rc = pki_privkey_build_rsa(key, n, e, d, iqmp, p, q);
#ifdef DEBUG_CRYPTO
        ssh_log_hexdump("n", ssh_string_data(n), ssh_string_len(n));
        ssh_log_hexdump("e", ssh_string_data(e), ssh_string_len(e));
        ssh_log_hexdump("d", ssh_string_data(d), ssh_string_len(d));
        ssh_log_hexdump("iqmp", ssh_string_data(iqmp), ssh_string_len(iqmp));
        ssh_log_hexdump("p", ssh_string_data(p), ssh_string_len(p));
        ssh_log_hexdump("q", ssh_string_data(q), ssh_string_len(q));
#endif /* DEBUG_CRYPTO */
        ssh_string_burn(n);
        SSH_STRING_FREE(n);
        ssh_string_burn(e);
        SSH_STRING_FREE(e);
        ssh_string_burn(d);
        SSH_STRING_FREE(d);
        ssh_string_burn(iqmp);
        SSH_STRING_FREE(iqmp);
        ssh_string_burn(p);
        SSH_STRING_FREE(p);
        ssh_string_burn(q);
        SSH_STRING_FREE(q);
        if (rc == SSH_ERROR) {
            SSH_LOG(SSH_LOG_TRACE, "Failed to build RSA private key");
            goto fail;
        }
        break;
    }
#ifdef HAVE_ECC
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_ECDSA_P384:
    case SSH_KEYTYPE_ECDSA_P521: {
        ssh_string e = NULL;
        ssh_string exp = NULL;
        ssh_string i = NULL;
        int nid;

        rc = ssh_buffer_unpack(buffer, "SSS", &i, &e, &exp);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_TRACE, "Unpack error");
            goto fail;
        }

        nid = pki_key_ecdsa_nid_from_name(ssh_string_get_char(i));
        SSH_STRING_FREE(i);
        if (nid == -1) {
            ssh_string_burn(e);
            SSH_STRING_FREE(e);
            ssh_string_burn(exp);
            SSH_STRING_FREE(exp);
            goto fail;
        }

        rc = pki_privkey_build_ecdsa(key, nid, e, exp);
        ssh_string_burn(e);
        SSH_STRING_FREE(e);
        ssh_string_burn(exp);
        SSH_STRING_FREE(exp);
        if (rc < 0) {
            SSH_LOG(SSH_LOG_TRACE, "Failed to build ECDSA private key");
            goto fail;
        }
        break;
    }
    case SSH_KEYTYPE_SK_ECDSA: {
        ssh_string type_str = NULL;
        ssh_string pubkey = NULL;
        int nid;

        rc = ssh_buffer_unpack(buffer, "SS", &type_str, &pubkey);
        if (rc != SSH_OK) {
            goto fail;
        }

        rc = pki_buffer_unpack_sk_priv_data(buffer, key);
        if (rc != SSH_OK) {
            SSH_STRING_FREE(type_str);
            SSH_STRING_FREE(pubkey);
            goto fail;
        }

        nid = pki_key_ecdsa_nid_from_name(ssh_string_get_char(type_str));
        SSH_STRING_FREE(type_str);

        if (nid == -1) {
            SSH_STRING_FREE(pubkey);
            goto fail;
        }

        rc = pki_pubkey_build_ecdsa(key, nid, pubkey);
        SSH_STRING_FREE(pubkey);
        if (rc != SSH_OK) {
            goto fail;
        }
        break;
    }
#endif /* HAVE_ECC */
    case SSH_KEYTYPE_ED25519: {
        ssh_string pubkey = NULL, privkey = NULL;

        if (ssh_fips_mode()) {
            SSH_LOG(SSH_LOG_TRACE, "Ed25519 keys not supported in FIPS mode");
            goto fail;
        }

        rc = ssh_buffer_unpack(buffer, "SS", &pubkey, &privkey);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_TRACE, "Unpack error");
            goto fail;
        }

        rc = pki_privkey_build_ed25519(key, pubkey, privkey);
        ssh_string_burn(privkey);
        SSH_STRING_FREE(privkey);
        SSH_STRING_FREE(pubkey);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_TRACE, "Failed to build ed25519 key");
            goto fail;
        }
        break;
    }
    case SSH_KEYTYPE_SK_ED25519: {
        ssh_string pubkey = NULL;

        if (ssh_fips_mode()) {
            SSH_LOG(SSH_LOG_TRACE, "Ed25519 keys not supported in FIPS mode");
            goto fail;
        }

        rc = ssh_buffer_unpack(buffer, "S", &pubkey);
        if (rc != SSH_OK) {
            goto fail;
        }

        rc = pki_buffer_unpack_sk_priv_data(buffer, key);
        if (rc != SSH_OK) {
            SSH_STRING_FREE(pubkey);
            goto fail;
        }

        rc = pki_pubkey_build_ed25519(key, pubkey);
        SSH_STRING_FREE(pubkey);
        if (rc != SSH_OK) {
            goto fail;
        }
        break;
    }
    case SSH_KEYTYPE_RSA_CERT01:
    case SSH_KEYTYPE_ECDSA_P256_CERT01:
    case SSH_KEYTYPE_ECDSA_P384_CERT01:
    case SSH_KEYTYPE_ECDSA_P521_CERT01:
    case SSH_KEYTYPE_ED25519_CERT01:
    case SSH_KEYTYPE_SK_ECDSA_CERT01:
    case SSH_KEYTYPE_SK_ED25519_CERT01:
    case SSH_KEYTYPE_RSA1:
    case SSH_KEYTYPE_UNKNOWN:
    default:
        SSH_LOG(SSH_LOG_TRACE, "Unknown private key type (%d)", type);
        goto fail;
    }

    *pkey = key;
    return SSH_OK;
fail:
    ssh_key_free(key);

    return SSH_ERROR;
}

static int pki_import_pubkey_buffer(ssh_buffer buffer,
                                    enum ssh_keytypes_e type,
                                    ssh_key *pkey)
{
    ssh_key key = NULL;
    int rc;

    key = ssh_key_new();
    if (key == NULL) {
        return SSH_ERROR;
    }

    key->type = type;
    key->type_c = ssh_key_type_to_char(type);
    key->flags = SSH_KEY_FLAG_PUBLIC;

    switch (type) {
        case SSH_KEYTYPE_RSA:
            {
                ssh_string e = NULL;
                ssh_string n = NULL;

                rc = ssh_buffer_unpack(buffer, "SS", &e, &n);
                if (rc != SSH_OK) {
                    SSH_LOG(SSH_LOG_TRACE, "Unpack error");
                    goto fail;
                }

                rc = pki_pubkey_build_rsa(key, e, n);
#ifdef DEBUG_CRYPTO
                ssh_log_hexdump("e", ssh_string_data(e), ssh_string_len(e));
                ssh_log_hexdump("n", ssh_string_data(n), ssh_string_len(n));
#endif /* DEBUG_CRYPTO */
                ssh_string_burn(e);
                SSH_STRING_FREE(e);
                ssh_string_burn(n);
                SSH_STRING_FREE(n);
                if (rc == SSH_ERROR) {
                    SSH_LOG(SSH_LOG_TRACE, "Failed to build RSA public key");
                    goto fail;
                }
            }
            break;
#ifdef HAVE_ECC
        case SSH_KEYTYPE_ECDSA: /* deprecated */
        case SSH_KEYTYPE_ECDSA_P256:
        case SSH_KEYTYPE_ECDSA_P384:
        case SSH_KEYTYPE_ECDSA_P521:
        case SSH_KEYTYPE_SK_ECDSA:
            {
                ssh_string e = NULL;
                ssh_string i = NULL;
                int nid;

                rc = ssh_buffer_unpack(buffer, "SS", &i, &e);
                if (rc != SSH_OK) {
                    SSH_LOG(SSH_LOG_TRACE, "Unpack error");
                    goto fail;
                }

                nid = pki_key_ecdsa_nid_from_name(ssh_string_get_char(i));
                SSH_STRING_FREE(i);
                if (nid == -1) {
                    ssh_string_burn(e);
                    SSH_STRING_FREE(e);
                    goto fail;
                }

                rc = pki_pubkey_build_ecdsa(key, nid, e);
                ssh_string_burn(e);
                SSH_STRING_FREE(e);
                if (rc < 0) {
                    SSH_LOG(SSH_LOG_TRACE, "Failed to build ECDSA public key");
                    goto fail;
                }

                /* Unpack SK specific parameters */
                if (type == SSH_KEYTYPE_SK_ECDSA) {
                    ssh_string application = ssh_buffer_get_ssh_string(buffer);
                    if (application == NULL) {
                        SSH_LOG(SSH_LOG_TRACE, "SK Unpack error");
                        goto fail;
                    }
                    key->sk_application = application;
                    key->type_c = ssh_key_type_to_char(key->type);
                }
            }
            break;
#endif /* HAVE_ECC */
        case SSH_KEYTYPE_ED25519:
        case SSH_KEYTYPE_SK_ED25519:
        {
            ssh_string pubkey = ssh_buffer_get_ssh_string(buffer);

            if (ssh_string_len(pubkey) != ED25519_KEY_LEN) {
                SSH_LOG(SSH_LOG_TRACE, "Invalid public key length");
                ssh_string_burn(pubkey);
                SSH_STRING_FREE(pubkey);
                goto fail;
            }

            rc = pki_pubkey_build_ed25519(key, pubkey);
            ssh_string_burn(pubkey);
            SSH_STRING_FREE(pubkey);
            if (rc < 0) {
                SSH_LOG(SSH_LOG_TRACE, "Failed to build ED25519 public key");
                goto fail;
            }

            if (type == SSH_KEYTYPE_SK_ED25519) {
                ssh_string application = ssh_buffer_get_ssh_string(buffer);
                if (application == NULL) {
                    SSH_LOG(SSH_LOG_TRACE, "SK Unpack error");
                    goto fail;
                }
                key->sk_application = application;
            }
        }
        break;
        case SSH_KEYTYPE_RSA_CERT01:
        case SSH_KEYTYPE_ECDSA_P256_CERT01:
        case SSH_KEYTYPE_ECDSA_P384_CERT01:
        case SSH_KEYTYPE_ECDSA_P521_CERT01:
        case SSH_KEYTYPE_SK_ECDSA_CERT01:
        case SSH_KEYTYPE_ED25519_CERT01:
        case SSH_KEYTYPE_SK_ED25519_CERT01:
        case SSH_KEYTYPE_RSA1:
        case SSH_KEYTYPE_UNKNOWN:
        default:
            SSH_LOG(SSH_LOG_TRACE, "Unknown public key type %d", type);
            goto fail;
    }

    *pkey = key;
    return SSH_OK;
fail:
    ssh_key_free(key);

    return SSH_ERROR;
}

static int pki_import_cert_buffer(ssh_buffer buffer,
                                  enum ssh_keytypes_e type,
                                  ssh_key *pkey)
{
    ssh_buffer cert = NULL;
    ssh_string tmp_s = NULL;
    const char *type_c = NULL;
    ssh_key key = NULL;
    int rc;

    /*
     * The cert blob starts with the key type as an ssh_string, but this
     * string has been read out of the buffer to identify the key type.
     * Simply add it again as first element before copying the rest.
     */
    cert = ssh_buffer_new();
    if (cert == NULL) {
        goto fail;
    }
    type_c = ssh_key_type_to_char(type);
    tmp_s = ssh_string_from_char(type_c);
    if (tmp_s == NULL) {
        goto fail;
    }
    rc = ssh_buffer_add_ssh_string(cert, tmp_s);
    SSH_STRING_FREE(tmp_s);
    if (rc != 0) {
        goto fail;
    }
    rc = ssh_buffer_add_buffer(cert, buffer);
    if (rc != 0) {
        goto fail;
    }

    /*
     * After the key type, comes an ssh_string nonce. Just after this comes the
     * cert public key, which can be parsed out of the buffer.
     */
    tmp_s = ssh_buffer_get_ssh_string(buffer);
    if (tmp_s == NULL) {
        goto fail;
    }
    SSH_STRING_FREE(tmp_s);

    switch (type) {
        case SSH_KEYTYPE_RSA_CERT01:
            rc = pki_import_pubkey_buffer(buffer, SSH_KEYTYPE_RSA, &key);
            break;
        case SSH_KEYTYPE_ECDSA_P256_CERT01:
            rc = pki_import_pubkey_buffer(buffer, SSH_KEYTYPE_ECDSA_P256, &key);
            break;
        case SSH_KEYTYPE_ECDSA_P384_CERT01:
            rc = pki_import_pubkey_buffer(buffer, SSH_KEYTYPE_ECDSA_P384, &key);
            break;
        case SSH_KEYTYPE_ECDSA_P521_CERT01:
            rc = pki_import_pubkey_buffer(buffer, SSH_KEYTYPE_ECDSA_P521, &key);
            break;
        case SSH_KEYTYPE_ED25519_CERT01:
            rc = pki_import_pubkey_buffer(buffer, SSH_KEYTYPE_ED25519, &key);
            break;
        case SSH_KEYTYPE_SK_ECDSA_CERT01:
            rc = pki_import_pubkey_buffer(buffer, SSH_KEYTYPE_SK_ECDSA, &key);
            break;
        case SSH_KEYTYPE_SK_ED25519_CERT01:
            rc = pki_import_pubkey_buffer(buffer, SSH_KEYTYPE_SK_ED25519, &key);
            break;
        default:
            key = ssh_key_new();
    }
    if (rc != 0 || key == NULL) {
        goto fail;
    }

    key->type = type;
    key->type_c = type_c;
    key->cert = cert;

    *pkey = key;
    return SSH_OK;

fail:
    ssh_key_free(key);
    SSH_BUFFER_FREE(cert);
    return SSH_ERROR;
}

/**
 * @brief Import a base64 formatted public key from a memory c-string.
 *
 * Note that the public key is just the base64 part (without the key
 * type prefix and comment suffix you can find in the OpenSSH public
 * key file or known_hosts file).
 *
 * @param[in]  b64_key  The base64 key to import.
 * @param[in]  type     The type of the key to import.
 * @param[out] pkey     A pointer where the allocated key can be stored. You
 *                      need to free the memory using ssh_key_free().
 *
 * @return              `SSH_OK` on success, `SSH_ERROR` on error.
 *
 * @see ssh_key_free()
 */
int ssh_pki_import_pubkey_base64(const char *b64_key,
                                 enum ssh_keytypes_e type,
                                 ssh_key *pkey)
{
    ssh_buffer buffer = NULL;
    ssh_string type_s = NULL;
    int rc;

    if (b64_key == NULL || pkey == NULL) {
        return SSH_ERROR;
    }

    buffer = base64_to_bin(b64_key);
    if (buffer == NULL) {
        return SSH_ERROR;
    }

    type_s = ssh_buffer_get_ssh_string(buffer);
    if (type_s == NULL) {
        SSH_BUFFER_FREE(buffer);
        return SSH_ERROR;
    }
    SSH_STRING_FREE(type_s);

    if (is_cert_type(type)) {
        rc = pki_import_cert_buffer(buffer, type, pkey);
    } else {
        rc = pki_import_pubkey_buffer(buffer, type, pkey);
    }
    SSH_BUFFER_FREE(buffer);

    return rc;
}

/**
 * @internal
 *
 * @brief Import a public key from a ssh string.
 *
 * @param[in]  key_blob The key blob to import as specified in RFC 4253 section
 *                      6.6 "Public Key Algorithms".
 *
 * @param[out] pkey     A pointer where the allocated key can be stored. You
 *                      need to free the memory using ssh_key_free().
 *
 * @return              SSH_OK on success, SSH_ERROR on error.
 *
 * @see ssh_key_free()
 */
int ssh_pki_import_pubkey_blob(const ssh_string key_blob,
                               ssh_key *pkey)
{
    ssh_buffer buffer = NULL;
    ssh_string type_s = NULL;
    enum ssh_keytypes_e type;
    int rc;

    if (key_blob == NULL || pkey == NULL) {
        return SSH_ERROR;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Out of memory!");
        return SSH_ERROR;
    }

    rc = ssh_buffer_add_data(buffer,
                             ssh_string_data(key_blob),
                             (uint32_t)ssh_string_len(key_blob));
    if (rc < 0) {
        SSH_LOG(SSH_LOG_TRACE, "Out of memory!");
        goto fail;
    }

    type_s = ssh_buffer_get_ssh_string(buffer);
    if (type_s == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Out of memory!");
        goto fail;
    }

    type = ssh_key_type_from_name(ssh_string_get_char(type_s));
    if (type == SSH_KEYTYPE_UNKNOWN) {
        SSH_LOG(SSH_LOG_TRACE, "Unknown key type found!");
        goto fail;
    }
    SSH_STRING_FREE(type_s);

    if (is_cert_type(type)) {
        rc = pki_import_cert_buffer(buffer, type, pkey);
    } else {
        rc = pki_import_pubkey_buffer(buffer, type, pkey);
    }

    SSH_BUFFER_FREE(buffer);

    return rc;
fail:
    SSH_BUFFER_FREE(buffer);
    SSH_STRING_FREE(type_s);

    return SSH_ERROR;
}

#ifdef WITH_PKCS11_URI
/**
 *@brief Detect if the pathname in cmp is a PKCS #11 URI.
 *
 * @param[in] cmp The path to the public/private key
 *                     or a private/public PKCS #11 URI.
 *
 * @returns true if filename is a URI starting with "pkcs11:"
 *          false otherwise.
 */
bool ssh_pki_is_uri(const char *cmp)
{
    int rc;

    rc = strncmp(cmp, PKCS11_URI, strlen(PKCS11_URI));
    if (rc == 0) {
        return true;
    }

    return false;
}

/**
 *@brief export a Public PKCS #11 URI from a Private PKCS #11 URI
 *       by replacing "type=private" to "type=public".
 *       TODO: Improve the parser
 *
 * @param[in] priv_uri Private PKCS #11 URI.
 *
 * @returns pointer to the public PKCS #11 URI. You need to free
 *          the memory using ssh_string_free_char().
 *
 * @see ssh_string_free_char().
 */
char *ssh_pki_export_pub_uri_from_priv_uri(const char *priv_uri)
{
    char *pub_uri_temp = NULL;

    pub_uri_temp = ssh_strreplace(priv_uri,
                                  "type=private",
                                  "type=public");

    return pub_uri_temp;
}
#endif /* WITH_PKCS11_URI */

/**
 * @brief Import a public key from a file or a PKCS #11 device.
 *
 * @param[in]  filename The filename of the public key or the
 *                      PKCS #11 URI corresponding to the public key.
 *
 * @param[out] pkey     A pointer to store the allocated public key. You need to
 *                      free the memory using ssh_key_free().
 *
 * @returns SSH_OK on success, SSH_EOF if the file doesn't exist or permission
 *          denied, SSH_ERROR otherwise.
 *
 * @see ssh_key_free()
 */
int ssh_pki_import_pubkey_file(const char *filename, ssh_key *pkey)
{
    enum ssh_keytypes_e type;
    struct stat sb;
    char *key_buf = NULL, *p = NULL;
    size_t buflen, i;
    const char *q = NULL;
    FILE *file = NULL;
    off_t size;
    int rc, cmp;
    char err_msg[SSH_ERRNO_MSG_MAX] = {0};
    ssh_key priv_key = NULL;

    if (pkey == NULL || filename == NULL || *filename == '\0') {
        return SSH_ERROR;
    }

#ifdef WITH_PKCS11_URI
    if (ssh_pki_is_uri(filename)) {
        rc = pki_uri_import(filename, pkey, SSH_KEY_PUBLIC);
        return rc;
    }
#endif /* WITH_PKCS11_URI */

    file = fopen(filename, "rb");
    if (file == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Error opening %s: %s",
                    filename, ssh_strerror(errno, err_msg, SSH_ERRNO_MSG_MAX));
        return SSH_EOF;
    }

    rc = fstat(fileno(file), &sb);
    if (rc < 0) {
        fclose(file);
        SSH_LOG(SSH_LOG_TRACE, "Error gettint stat of %s: %s",
                    filename, ssh_strerror(errno, err_msg, SSH_ERRNO_MSG_MAX));
        switch (errno) {
            case ENOENT:
            case EACCES:
                return SSH_EOF;
        }
        return SSH_ERROR;
    }

    if (sb.st_size > MAX_PUBKEY_SIZE) {
        fclose(file);
        return SSH_ERROR;
    }

    key_buf = malloc(sb.st_size + 1);
    if (key_buf == NULL) {
        fclose(file);
        SSH_LOG(SSH_LOG_TRACE, "Out of memory!");
        return SSH_ERROR;
    }

    size = fread(key_buf, 1, sb.st_size, file);
    fclose(file);

    if (size != sb.st_size) {
        SAFE_FREE(key_buf);
        SSH_LOG(SSH_LOG_TRACE, "Error reading %s: %s",
                filename, ssh_strerror(errno, err_msg, SSH_ERRNO_MSG_MAX));
        return SSH_ERROR;
    }
    key_buf[size] = '\0';
    buflen = strlen(key_buf);

    /* Test for new OpenSSH key format first */
    cmp = strncmp(key_buf, OPENSSH_HEADER_BEGIN, strlen(OPENSSH_HEADER_BEGIN));
    if (cmp == 0) {
        *pkey = ssh_pki_openssh_pubkey_import(key_buf);
        SAFE_FREE(key_buf);
        if (*pkey == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "Failed to import public key from OpenSSH"
                                  " private key file");
            return SSH_ERROR;
        }
        return SSH_OK;
    }

    /*
     * Try to parse key as PEM. Set empty passphrase, so user won't be prompted
     * for passphrase. Don't try to decrypt encrypted private key.
     */
    priv_key = pki_private_key_from_base64(key_buf, "", NULL, NULL);
    if (priv_key) {
        rc = ssh_pki_export_privkey_to_pubkey(priv_key, pkey);
        ssh_key_free(priv_key);
        SAFE_FREE(key_buf);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_WARN, "Failed to import public key from PEM"
                                  " private key file");
            return SSH_ERROR;
        }
        return SSH_OK;
    }

    /* This the old one-line public key format */
    q = p = key_buf;
    for (i = 0; i < buflen; i++) {
        if (isspace((int)p[i])) {
            p[i] = '\0';
            break;
        }
    }

    type = ssh_key_type_from_name(q);
    if (type == SSH_KEYTYPE_UNKNOWN) {
        SAFE_FREE(key_buf);
        return SSH_ERROR;
    }

    if (i >= buflen) {
        SAFE_FREE(key_buf);
        return SSH_ERROR;
    }
    q = &p[i + 1];
    for (; i < buflen; i++) {
        if (isspace((int)p[i])) {
            p[i] = '\0';
            break;
        }
    }

    rc = ssh_pki_import_pubkey_base64(q, type, pkey);
    SAFE_FREE(key_buf);

    return rc;
}

/**
 * @brief Import a base64 formatted certificate from a memory c-string.
 *
 * Note that the certificate is just the base64 part (without the key
 * type prefix and comment suffix you can find in the OpenSSH certificate
 * file).
 *
 * @param[in]  b64_cert  The base64 cert to import.
 * @param[in]  type     The type of the cert to import.
 * @param[out] pkey     A pointer where the allocated certificate can be stored.
 *                      You need to free the memory using ssh_key_free().
 *
 * @return              `SSH_OK` on success, `SSH_ERROR` on error.
 *
 * @see ssh_key_free()
 */
int ssh_pki_import_cert_base64(const char *b64_cert,
                               enum ssh_keytypes_e type,
                               ssh_key *pkey)
{
    return ssh_pki_import_pubkey_base64(b64_cert, type, pkey);
}

/**
 * @internal
 *
 * @brief Import a certificate from a ssh string.
 *
 * @param[in]  cert_blob The cert blob to import as specified in RFC 4253 section
 *                      6.6 "Public Key Algorithms".
 *
 * @param[out] pkey     A pointer where the allocated key can be stored. You
 *                      need to free the memory using ssh_key_free().
 *
 * @return              SSH_OK on success, SSH_ERROR on error.
 *
 * @see ssh_key_free()
 */
int ssh_pki_import_cert_blob(const ssh_string cert_blob,
                             ssh_key *pkey)
{
    return ssh_pki_import_pubkey_blob(cert_blob, pkey);
}

/**
 * @brief Import a certificate from the given filename.
 *
 * @param[in]  filename The path to the certificate.
 *
 * @param[out] pkey     A pointer to store the allocated certificate. You need to
 *                      free the memory using ssh_key_free().
 *
 * @returns SSH_OK on success, SSH_EOF if the file doesn't exist or permission
 *          denied, SSH_ERROR otherwise.
 *
 * @see ssh_key_free()
 */
int ssh_pki_import_cert_file(const char *filename, ssh_key *pkey)
{
    int rc;

    rc = ssh_pki_import_pubkey_file(filename, pkey);
    if (rc == SSH_OK) {
        /* check the key is a cert type. */
        if (!is_cert_type((*pkey)->type)) {
            SSH_KEY_FREE(*pkey);
            return SSH_ERROR;
        }
    }

    return rc;
}

/**
 * @internal
 *
 * @brief Internal function to generate a key pair.
 *
 * @param[in] type      Type of key to create
 *
 * @param[in] parameter Parameter to the creation of key:
 *                      rsa : length of the key in bits (e.g. 1024, 2048, 4096)
 *                      If parameter is 0, then the default size will be used.
 * @param[out] pkey     A pointer to store the allocated private key. You need
 *                      to free the memory using ssh_key_free().
 *
 * @return              SSH_OK on success, SSH_ERROR on error.
 */
static int pki_generate_key_internal(enum ssh_keytypes_e type,
                                     int parameter,
                                     ssh_key *pkey)
{
    int rc;
    ssh_key key = NULL;

    if (pkey == NULL) {
        return SSH_ERROR;
    }

    key = ssh_key_new();
    if (key == NULL) {
        return SSH_ERROR;
    }

    key->type = type;
    key->type_c = ssh_key_type_to_char(type);
    key->flags = SSH_KEY_FLAG_PRIVATE | SSH_KEY_FLAG_PUBLIC;

    switch(type){
        case SSH_KEYTYPE_RSA:
            if (parameter != 0 && parameter < RSA_MIN_KEY_SIZE) {
                SSH_LOG(
                    SSH_LOG_WARN,
                    "RSA key size parameter (%d) is below minimum allowed (%d)",
                    parameter,
                    RSA_MIN_KEY_SIZE);
                goto error;
            }

            rc = pki_key_generate_rsa(key, parameter);
            if(rc == SSH_ERROR)
                goto error;
            break;
#ifdef HAVE_ECC
        case SSH_KEYTYPE_ECDSA: /* deprecated */
            rc = pki_key_generate_ecdsa(key, parameter);
            if (rc == SSH_ERROR) {
                goto error;
            }

            /* Update key type */
            key->type_c = ssh_pki_key_ecdsa_name(key);
            break;
        case SSH_KEYTYPE_ECDSA_P256:
            rc = pki_key_generate_ecdsa(key, 256);
            if (rc == SSH_ERROR) {
                goto error;
            }
            break;
        case SSH_KEYTYPE_ECDSA_P384:
            rc = pki_key_generate_ecdsa(key, 384);
            if (rc == SSH_ERROR) {
                goto error;
            }
            break;
        case SSH_KEYTYPE_ECDSA_P521:
            rc = pki_key_generate_ecdsa(key, 521);
            if (rc == SSH_ERROR) {
                goto error;
            }
            break;
#endif /* HAVE_ECC */
        case SSH_KEYTYPE_ED25519:
            rc = pki_key_generate_ed25519(key);
            if (rc == SSH_ERROR) {
                goto error;
            }
            break;
        case SSH_KEYTYPE_RSA_CERT01:
        case SSH_KEYTYPE_ECDSA_P256_CERT01:
        case SSH_KEYTYPE_ECDSA_P384_CERT01:
        case SSH_KEYTYPE_ECDSA_P521_CERT01:
        case SSH_KEYTYPE_ED25519_CERT01:
        case SSH_KEYTYPE_SK_ECDSA:
        case SSH_KEYTYPE_SK_ECDSA_CERT01:
        case SSH_KEYTYPE_SK_ED25519:
        case SSH_KEYTYPE_SK_ED25519_CERT01:
        case SSH_KEYTYPE_RSA1:
        case SSH_KEYTYPE_UNKNOWN:
        default:
            goto error;
    }

    *pkey = key;
    return SSH_OK;
error:
    ssh_key_free(key);
    return SSH_ERROR;
}

/**
 * @brief Generates a key pair.
 *
 * @param[in] type      Type of key to create
 *
 * @param[in] parameter Parameter to the creation of key:
 *                      rsa : length of the key in bits (e.g. 1024, 2048, 4096)
 *                      If parameter is 0, then the default size will be used.
 * @param[out] pkey     A pointer to store the allocated private key. You need
 *                      to free the memory using ssh_key_free().
 *
 * @return              SSH_OK on success, SSH_ERROR on error.
 *
 * @warning             Generating a key pair may take some time.
 *
 * @see ssh_key_free()
 */
int ssh_pki_generate(enum ssh_keytypes_e type, int parameter, ssh_key *pkey)
{
    return pki_generate_key_internal(type, parameter, pkey);
}

/**
 * @brief Generates a key pair.
 *
 * @param[in] type        Type of key to create
 *
 * @param[in] pki_context PKI context containing various configuration
 *                        parameters and sub-contexts. Can be NULL for
 *                        standard SSH key types (RSA, ECDSA, ED25519) where
 *                        defaults will be used. Can also be NULL for security
 *                        key types (SK_*), in which case default callbacks and
 *                        settings will be used automatically.
 *
 * @param[out] pkey       A pointer to store the allocated private key. You need
 *                        to free the memory using ssh_key_free().
 *
 * @return              SSH_OK on success, SSH_ERROR on error.
 *
 * @see ssh_pki_ctx_new()
 * @see ssh_key_free()
 */
int ssh_pki_generate_key(enum ssh_keytypes_e type,
                         ssh_pki_ctx pki_context,
                         ssh_key *pkey)
{

    /* Handle Security Key types with the specialized function */
    if (is_sk_key_type(type)) {
#ifdef WITH_FIDO2
        ssh_pki_ctx temp_ctx = NULL;
        ssh_pki_ctx ctx_to_use = pki_context;
        int rc;

        /* If no context provided, create a temporary default one */
        if (pki_context == NULL) {
            SSH_LOG(SSH_LOG_INFO,
                    "No PKI context provided, using the default one");

            temp_ctx = ssh_pki_ctx_new();
            if (temp_ctx == NULL) {
                SSH_LOG(SSH_LOG_WARN, "Failed to create temporary PKI context");
                return SSH_ERROR;
            }
            ctx_to_use = temp_ctx;
        }

        /* Verify that we have valid SK callbacks */
        if (ctx_to_use->sk_callbacks == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Missing SK callbacks in PKI context");
            if (temp_ctx != NULL) {
                SSH_PKI_CTX_FREE(temp_ctx);
            }
            return SSH_ERROR;
        }

        rc = pki_sk_enroll_key(ctx_to_use, type, pkey);

        /* Clean up temporary context if we created one */
        if (temp_ctx != NULL) {
            SSH_PKI_CTX_FREE(temp_ctx);
        }

        return rc;
#else  /* WITH_FIDO2 */
        SSH_LOG(SSH_LOG_WARN, SK_NOT_SUPPORTED_MSG);
        return SSH_ERROR;
#endif /* WITH_FIDO2 */
    } else {
        int parameter = 0;

        if (type == SSH_KEYTYPE_RSA && pki_context != NULL) {
            parameter = pki_context->rsa_key_size;
        }

        return pki_generate_key_internal(type, parameter, pkey);
    }
}

/**
 * @brief Create a public key from a private key.
 *
 * @param[in]  privkey  The private key to get the public key from.
 *
 * @param[out] pkey     A pointer to store the newly allocated public key. You
 *                      NEED to free the key using ssh_key_free().
 *
 * @return              SSH_OK on success, SSH_ERROR on error.
 *
 * @see ssh_key_free()
 */
int ssh_pki_export_privkey_to_pubkey(const ssh_key privkey,
                                     ssh_key *pkey)
{
    ssh_key pubkey = NULL;

    if (privkey == NULL || !ssh_key_is_private(privkey)) {
        return SSH_ERROR;
    }

    pubkey = pki_key_dup(privkey, 1);
    if (pubkey == NULL) {
        return SSH_ERROR;
    }

    *pkey = pubkey;
    return SSH_OK;
}

/**
 * @internal
 *
 * @brief Pack security key private data into a buffer.
 *
 * This function packs the common security key fields (application, flags,
 * key handle, and reserved data) into a buffer.
 * This is used for both ECDSA and Ed25519 security keys when exporting
 * private key data.
 *
 * @param[in] buffer    The buffer to pack the security key data into.
 *
 * @param[in] key       The security key containing the data to pack.
 *                      Must be a security key type (SK_ECDSA or SK_ED25519).
 *
 * @return              SSH_OK on success, SSH_ERROR on error.
 *
 * @see ssh_buffer_pack()
 */
int pki_buffer_pack_sk_priv_data(ssh_buffer buffer, ssh_key key)
{
    return ssh_buffer_pack(buffer,
                           "SbSS",
                           key->sk_application,
                           key->sk_flags,
                           key->sk_key_handle,
                           key->sk_reserved);
}

/**
 * @internal
 *
 * @brief Unpack security key private data from a buffer.
 *
 * This function unpacks the common security key fields (application, flags,
 * key handle, and reserved data) from a buffer.
 * This is used for both ECDSA and Ed25519 security keys when importing
 * private key data.
 *
 * @param[in] buffer    The buffer to unpack the security key data from.
 *
 * @param[in] key       The security key to store the unpacked data into.
 *                      Must be a security key type (SK_ECDSA or SK_ED25519).
 *
 * @return              SSH_OK on success, SSH_ERROR on error.
 *
 * @see ssh_buffer_unpack()
 */
int pki_buffer_unpack_sk_priv_data(ssh_buffer buffer, ssh_key key)
{
    return ssh_buffer_unpack(buffer,
                             "SbSS",
                             &key->sk_application,
                             &key->sk_flags,
                             &key->sk_key_handle,
                             &key->sk_reserved);
}

/**
 * @internal
 *
 * @brief Create a key_blob from a public key.
 *
 * The "key_blob" is encoded as per RFC 4253 section 6.6 "Public Key
 * Algorithms" for any of the supported protocol 2 key types.
 * Encoding of EC keys is described in RFC 5656 section 3.1 "Key
 * Format".
 *
 * @param[in]  key      A public or private key to create the public ssh_string
 *                      from.
 *
 * @param[out] pblob    A pointer to store the newly allocated key blob. You
 *                      need to free it using ssh_string_free().
 *
 * @return              SSH_OK on success, SSH_ERROR otherwise.
 *
 * @see ssh_string_free()
 */
int ssh_pki_export_pubkey_blob(const ssh_key key,
                               ssh_string *pblob)
{
    ssh_string blob = NULL;

    if (key == NULL) {
        return SSH_OK;
    }

    blob = pki_key_to_blob(key, SSH_KEY_PUBLIC);
    if (blob == NULL) {
        return SSH_ERROR;
    }

    *pblob = blob;
    return SSH_OK;
}

/**
 * @internal
 *
 * @brief Create a key_blob from a private key.
 *
 * The "key_blob" is encoded as per draft-miller-ssh-agent-08 section 4.2
 * "Adding keys to the agent" for any of the supported key types.
 *
 * @param[in]  key      A private key to create the private ssh_string from.
 *
 * @param[out] pblob    A pointer to store the newly allocated key blob. You
 *                      need to free it using ssh_string_free().
 *
 * @return              SSH_OK on success, SSH_ERROR otherwise.
 *
 * @see ssh_string_free()
 */
int ssh_pki_export_privkey_blob(const ssh_key key,
                                ssh_string *pblob)
{
    ssh_string blob = NULL;

    if (key == NULL) {
        return SSH_OK;
    }

    blob = pki_key_to_blob(key, SSH_KEY_PRIVATE);
    if (blob == NULL) {
        return SSH_ERROR;
    }

    *pblob = blob;
    return SSH_OK;
}

/**
 * @brief Convert a public key to a base64 encoded key.
 *
 * @param[in] key       The key to hash
 *
 * @param[out] b64_key  A pointer to store the allocated base64 encoded key. You
 *                      need to free the buffer using ssh_string_free_char()
 *
 * @return              SSH_OK on success, SSH_ERROR on error.
 *
 * @see ssh_string_free_char()
 */
int ssh_pki_export_pubkey_base64(const ssh_key key,
                                 char **b64_key)
{
    ssh_string key_blob = NULL;
    unsigned char *b64 = NULL;

    if (key == NULL || b64_key == NULL) {
        return SSH_ERROR;
    }

    key_blob = pki_key_to_blob(key, SSH_KEY_PUBLIC);
    if (key_blob == NULL) {
        return SSH_ERROR;
    }

    b64 = bin_to_base64(ssh_string_data(key_blob), ssh_string_len(key_blob));
    SSH_STRING_FREE(key_blob);
    if (b64 == NULL) {
        return SSH_ERROR;
    }

    *b64_key = (char *)b64;

    return SSH_OK;
}

/**
 * @brief Export public key to file
 *
 * Exports the public key in AuthorizedKeysFile acceptable format.
 * For more information see `man sshd`
 *
 * @param key A key to export
 *
 * @param filename The name of the output file
 *
 * @returns SSH_OK on success, SSH_ERROR otherwise.
 */
int ssh_pki_export_pubkey_file(const ssh_key key,
                               const char *filename)
{
    char key_buf[MAX_LINE_SIZE];
    char *host = NULL;
    char *b64_key = NULL;
    char *user = NULL;
    FILE *fp = NULL;
    int rc;

    if (key == NULL || filename == NULL || *filename == '\0') {
        return SSH_ERROR;
    }

    user = ssh_get_local_username();
    if (user == NULL) {
        return SSH_ERROR;
    }

    host = ssh_get_local_hostname();
    if (host == NULL) {
        free(user);
        return SSH_ERROR;
    }

    rc = ssh_pki_export_pubkey_base64(key, &b64_key);
    if (rc < 0) {
        free(user);
        free(host);
        return SSH_ERROR;
    }

    rc = snprintf(key_buf, sizeof(key_buf),
                  "%s %s %s@%s\n",
                  key->type_c,
                  b64_key,
                  user,
                  host);
    free(user);
    free(host);
    free(b64_key);
    if (rc < 0) {
        return SSH_ERROR;
    }

    fp = fopen(filename, "wb+");
    if (fp == NULL) {
        return SSH_ERROR;
    }
    rc = fwrite(key_buf, strlen(key_buf), 1, fp);
    if (rc != 1 || ferror(fp)) {
        fclose(fp);
        unlink(filename);
        return SSH_ERROR;
    }
    fclose(fp);

    return SSH_OK;
}

/**
 * @brief Copy the certificate part of a public key into a private key.
 *
 * @param[in]  certkey  The certificate key.
 *
 * @param[in]  privkey  The target private key to copy the certificate to.
 *
 * @returns SSH_OK on success, SSH_ERROR otherwise.
 **/
int ssh_pki_copy_cert_to_privkey(const ssh_key certkey, ssh_key privkey)
{
    ssh_buffer cert_buffer = NULL;
    int rc, cmp;

    if (certkey == NULL || privkey == NULL) {
        return SSH_ERROR;
    }

    if (privkey->cert != NULL) {
        return SSH_ERROR;
    }

    if (certkey->cert == NULL) {
        return SSH_ERROR;
    }

    /* make sure the public keys match */
    cmp = ssh_key_cmp(certkey, privkey, SSH_KEY_CMP_PUBLIC);
    if (cmp != 0) {
        return SSH_ERROR;
    }

    cert_buffer = ssh_buffer_new();
    if (cert_buffer == NULL) {
        return SSH_ERROR;
    }

    rc = ssh_buffer_add_buffer(cert_buffer, certkey->cert);
    if (rc != 0) {
        SSH_BUFFER_FREE(cert_buffer);
        return SSH_ERROR;
    }

    privkey->cert = cert_buffer;
    privkey->cert_type = certkey->type;
    return SSH_OK;
}

int ssh_pki_export_signature_blob(const ssh_signature sig,
                                  ssh_string *sig_blob)
{
    ssh_buffer buf = NULL;
    ssh_string str = NULL;
    int rc;

    if (sig == NULL || sig_blob == NULL) {
        return SSH_ERROR;
    }

    buf = ssh_buffer_new();
    if (buf == NULL) {
        return SSH_ERROR;
    }

    str = ssh_string_from_char(sig->type_c);
    if (str == NULL) {
        SSH_BUFFER_FREE(buf);
        return SSH_ERROR;
    }

    rc = ssh_buffer_add_ssh_string(buf, str);
    SSH_STRING_FREE(str);
    if (rc < 0) {
        SSH_BUFFER_FREE(buf);
        return SSH_ERROR;
    }

    str = pki_signature_to_blob(sig);
    if (str == NULL) {
        SSH_BUFFER_FREE(buf);
        return SSH_ERROR;
    }

    rc = ssh_buffer_add_ssh_string(buf, str);
    SSH_STRING_FREE(str);
    if (rc < 0) {
        SSH_BUFFER_FREE(buf);
        return SSH_ERROR;
    }

    if (is_sk_key_type(sig->type)) {
        /* Add flags and counter for SK keys */
        rc = ssh_buffer_pack(buf, "bd", sig->sk_flags, sig->sk_counter);
        if (rc < 0) {
            SSH_BUFFER_FREE(buf);
            return SSH_ERROR;
        }
    }

    str = ssh_string_new(ssh_buffer_get_len(buf));
    if (str == NULL) {
        SSH_BUFFER_FREE(buf);
        return SSH_ERROR;
    }

    rc = ssh_string_fill(str, ssh_buffer_get(buf), ssh_buffer_get_len(buf));
    SSH_BUFFER_FREE(buf);
    if (rc < 0) {
        SSH_STRING_FREE(str);
        return SSH_ERROR;
    }

    *sig_blob = str;

    return SSH_OK;
}

int ssh_pki_import_signature_blob(const ssh_string sig_blob,
                                  const ssh_key pubkey,
                                  ssh_signature *psig)
{
    ssh_signature sig = NULL;
    enum ssh_keytypes_e type;
    enum ssh_digest_e hash_type;
    ssh_string algorithm = NULL, blob = NULL;
    ssh_buffer buf = NULL;
    const char *alg = NULL;
    uint8_t flags = 0;
    uint32_t counter = 0;
    int rc;

    if (sig_blob == NULL || psig == NULL) {
        return SSH_ERROR;
    }

    buf = ssh_buffer_new();
    if (buf == NULL) {
        return SSH_ERROR;
    }

    rc = ssh_buffer_add_data(buf,
                             ssh_string_data(sig_blob),
                             (uint32_t)ssh_string_len(sig_blob));
    if (rc < 0) {
        SSH_BUFFER_FREE(buf);
        return SSH_ERROR;
    }

    algorithm = ssh_buffer_get_ssh_string(buf);
    if (algorithm == NULL) {
        SSH_BUFFER_FREE(buf);
        return SSH_ERROR;
    }

    alg = ssh_string_get_char(algorithm);
    type = ssh_key_type_from_signature_name(alg);
    hash_type = ssh_key_hash_from_name(alg);
    SSH_STRING_FREE(algorithm);

    blob = ssh_buffer_get_ssh_string(buf);
    if (blob == NULL) {
        SSH_BUFFER_FREE(buf);
        return SSH_ERROR;
    }

    if (type == SSH_KEYTYPE_SK_ECDSA ||
        type == SSH_KEYTYPE_SK_ED25519) {
        rc = ssh_buffer_unpack(buf, "bd", &flags, &counter);
        if (rc < 0) {
            SSH_BUFFER_FREE(buf);
            SSH_STRING_FREE(blob);
            return SSH_ERROR;
        }
    }
    SSH_BUFFER_FREE(buf);

    sig = pki_signature_from_blob(pubkey, blob, type, hash_type);
    SSH_STRING_FREE(blob);
    if (sig == NULL) {
        return SSH_ERROR;
    }

    /* Set SK specific values */
    sig->sk_flags = flags;
    sig->sk_counter = counter;

    *psig = sig;
    return SSH_OK;
}

/**
 * @internal
 *
 * @brief Check if the provided key can be used with the provided hash type for
 * data signing or signature verification.
 *
 * @param[in]   key         The key to be checked.
 * @param[in]   hash_type   The digest algorithm to be checked.
 *
 * @return  SSH_OK if compatible; SSH_ERROR otherwise
 */
int pki_key_check_hash_compatible(ssh_key key,
                                  enum ssh_digest_e hash_type)
{
    if (key == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Null pointer provided as key to "
                               "pki_key_check_hash_compatible()");
        return SSH_ERROR;
    }

    switch(key->type) {
    case SSH_KEYTYPE_RSA_CERT01:
    case SSH_KEYTYPE_RSA:
        if (hash_type == SSH_DIGEST_SHA1) {
            if (ssh_fips_mode()) {
                SSH_LOG(SSH_LOG_TRACE, "SHA1 is not allowed in FIPS mode");
                return SSH_ERROR;
            } else {
                return SSH_OK;
            }
        }

        if (hash_type == SSH_DIGEST_SHA256 ||
            hash_type == SSH_DIGEST_SHA512)
        {
            return SSH_OK;
        }
        break;
    case SSH_KEYTYPE_ECDSA_P256_CERT01:
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_SK_ECDSA_CERT01:
    case SSH_KEYTYPE_SK_ECDSA:
        if (hash_type == SSH_DIGEST_SHA256) {
            return SSH_OK;
        }
        break;
    case SSH_KEYTYPE_ECDSA_P384_CERT01:
    case SSH_KEYTYPE_ECDSA_P384:
        if (hash_type == SSH_DIGEST_SHA384) {
            return SSH_OK;
        }
        break;
    case SSH_KEYTYPE_ECDSA_P521_CERT01:
    case SSH_KEYTYPE_ECDSA_P521:
        if (hash_type == SSH_DIGEST_SHA512) {
            return SSH_OK;
        }
        break;
    case SSH_KEYTYPE_ED25519_CERT01:
    case SSH_KEYTYPE_ED25519:
    case SSH_KEYTYPE_SK_ED25519_CERT01:
    case SSH_KEYTYPE_SK_ED25519:
        if (hash_type == SSH_DIGEST_AUTO) {
            return SSH_OK;
        }
        break;
    case SSH_KEYTYPE_DSS:   /* deprecated */
    case SSH_KEYTYPE_DSS_CERT01:    /* deprecated */
    case SSH_KEYTYPE_RSA1:
    case SSH_KEYTYPE_ECDSA:
    case SSH_KEYTYPE_UNKNOWN:
        SSH_LOG(SSH_LOG_TRACE, "Unknown key type %d", key->type);
        return SSH_ERROR;
    }

    SSH_LOG(SSH_LOG_TRACE, "Key type %d incompatible with hash type  %d",
            key->type, hash_type);

    return SSH_ERROR;
}

/**
 * @brief Prepare buffer for FIDO2/U2F security key signature verification
 *
 * This function creates a buffer containing the application hash, flags,
 * counter, and input hash for FIDO/U2F key signature verification.
 *
 * @param key          The SSH key containing sk_application
 * @param sig          The signature containing sk_flags and sk_counter
 * @param input        The input data to hash
 * @param input_len    Length of the input data
 * @param sk_buffer_out Pointer to store the created buffer
 *
 * @return SSH_OK on success, SSH_ERROR on error
 */
int pki_sk_signature_buffer_prepare(const ssh_key key,
                                    const ssh_signature sig,
                                    const unsigned char *input,
                                    size_t input_len,
                                    ssh_buffer *sk_buffer_out)
{
    ssh_buffer sk_buffer = NULL;
    SHA256CTX ctx = NULL;
    unsigned char application_hash[SHA256_DIGEST_LEN] = {0};
    unsigned char input_hash[SHA256_DIGEST_LEN] = {0};
    int rc, ret = SSH_ERROR;

    if (key == NULL || sig == NULL || input == NULL || sk_buffer_out == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Bad parameter(s) provided to %s()", __func__);
        return SSH_ERROR;
    }

    *sk_buffer_out = NULL;

    /* Calculate application hash */
    ctx = sha256_init();
    if (ctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Can not create SHA256CTX for application hash");
        return SSH_ERROR;
    }
    sha256_update(ctx,
                  ssh_string_data(key->sk_application),
                  ssh_string_len(key->sk_application));
    sha256_final(application_hash, ctx);

    /* Calculate input hash */
    ctx = sha256_init();
    if (ctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Can not create SHA256CTX for input hash");
        goto out;
    }
    sha256_update(ctx, input, input_len);
    sha256_final(input_hash, ctx);

    /* Create and pack the sk_buffer */
    sk_buffer = ssh_buffer_new();
    if (sk_buffer == NULL) {
        goto out;
    }

    rc = ssh_buffer_pack(sk_buffer,
                         "PbdP",
                         (size_t)SHA256_DIGEST_LEN,
                         application_hash,
                         sig->sk_flags,
                         sig->sk_counter,
                         (size_t)SHA256_DIGEST_LEN,
                         input_hash);
    if (rc != SSH_OK) {
        goto out;
    }

    *sk_buffer_out = sk_buffer;
    sk_buffer = NULL;
    ret = SSH_OK;

out:
    SSH_BUFFER_FREE(sk_buffer);
    ssh_burn(application_hash, SHA256_DIGEST_LEN);
    ssh_burn(input_hash, SHA256_DIGEST_LEN);

    return ret;
}

int ssh_pki_signature_verify(ssh_session session,
                             ssh_signature sig,
                             const ssh_key key,
                             const unsigned char *input,
                             size_t input_len)
{
    int rc;
    bool allowed;
    enum ssh_keytypes_e key_type;

    if (session == NULL || sig == NULL || key == NULL || input == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Bad parameter(s) provided to %s()", __func__);
        return SSH_ERROR;
    }
    key_type = ssh_key_type_plain(key->type);

    SSH_LOG(SSH_LOG_FUNCTIONS,
            "Going to verify a %s type signature",
            sig->type_c);

    if (key_type != sig->type) {
        SSH_LOG(SSH_LOG_TRACE,
                "Can not verify %s signature with %s key",
                sig->type_c, key->type_c);
        return SSH_ERROR;
    }

    allowed = ssh_key_size_allowed(session, key);
    if (!allowed) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "The '%s' key of size %d is not allowed by RSA_MIN_SIZE",
                      key->type_c,
                      ssh_key_size(key));
        return SSH_ERROR;
    }

    /* Check if public key and hash type are compatible */
    rc = pki_key_check_hash_compatible(key, sig->hash_type);
    if (rc != SSH_OK) {
        return SSH_ERROR;
    }

    if (is_sk_key_type(key->type)) {
        ssh_buffer sk_buffer = NULL;

        rc = pki_sk_signature_buffer_prepare(key,
                                             sig,
                                             input,
                                             input_len,
                                             &sk_buffer);
        if (rc != SSH_OK) {
            return SSH_ERROR;
        }

        rc = pki_verify_data_signature(sig,
                                       key,
                                       ssh_buffer_get(sk_buffer),
                                       ssh_buffer_get_len(sk_buffer));
        SSH_BUFFER_FREE(sk_buffer);
        return rc;
    }

    return pki_verify_data_signature(sig, key, input, input_len);
}

ssh_signature pki_do_sign(const ssh_key privkey,
                          const unsigned char *input,
                          size_t input_len,
                          enum ssh_digest_e hash_type)
{
    int rc;

    if (privkey == NULL || input == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Bad parameter provided to "
                               "pki_do_sign()");
        return NULL;
    }

    /* Check if public key and hash type are compatible */
    rc = pki_key_check_hash_compatible(privkey, hash_type);
    if (rc != SSH_OK) {
        return NULL;
    }

    return pki_sign_data(privkey, hash_type, input, input_len);
}

/**
 * @brief Encodes a binary signature blob as an sshsig armored signature
 *
 * @param blob       The binary signature blob to encode
 * @param out_str    Pointer to store the allocated base64 encoded string
 *                   Must be freed with ssh_string_free_char()
 *
 * @return SSH_OK on success, SSH_ERROR on error
 */
static int sshsig_armor(ssh_buffer blob, char **out_str)
{
    char *b64_data = NULL;
    char *armored = NULL;
    const unsigned char *data = NULL;
    size_t len, b64_len, armored_len, num_lines;
    size_t i, j;

    if (blob == NULL || out_str == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Invalid input parameters");
        return SSH_ERROR;
    }

    *out_str = NULL;

    data = ssh_buffer_get(blob);
    len = ssh_buffer_get_len(blob);

    b64_data = (char *)bin_to_base64(data, len);
    if (b64_data == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to base64 encode signature blob");
        return SSH_ERROR;
    }

    b64_len = strlen(b64_data);

    /* Calculate space needed: header + data with line breaks + footer */
    num_lines = (b64_len + SSHSIG_LINE_LENGTH - 1) /
                SSHSIG_LINE_LENGTH;                    /* Round up division */
    armored_len = strlen(SSHSIG_BEGIN_SIGNATURE) + 1 + /* header + \n */
                  b64_len + num_lines +                /* data + line breaks */
                  strlen(SSHSIG_END_SIGNATURE) + 1;    /* footer + \0 */

    armored = calloc(armored_len, 1);
    if (armored == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to allocate %zu bytes for armored signature",
                armored_len);
        SAFE_FREE(b64_data);
        return SSH_ERROR;
    }

    j = snprintf(armored, armored_len, SSHSIG_BEGIN_SIGNATURE "\n");
    for (i = 0; i < b64_len; i++) {
        if (i > 0 && i % SSHSIG_LINE_LENGTH == 0) {
            armored[j++] = '\n';
        }
        armored[j++] = b64_data[i];
    }
    armored[j++] = '\n';
    snprintf(armored + j, armored_len - j, SSHSIG_END_SIGNATURE);

    SAFE_FREE(b64_data);

    *out_str = armored;
    return SSH_OK;
}

/**
 * @brief Dearmor an sshsig signature from ASCII armored format to binary
 *
 * @param[in]  signature    The armored sshsig signature string
 * @param[out] out          Pointer to store the allocated binary buffer
 *
 * @return SSH_OK on success, SSH_ERROR on error
 */
static int sshsig_dearmor(const char *signature, ssh_buffer *out)
{
    const char *begin = NULL;
    const char *end = NULL;
    char *clean_b64 = NULL;
    ssh_buffer decoded_buffer = NULL;
    int i, j;
    int rc = SSH_ERROR;

    if (signature == NULL || out == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Invalid input parameters");
        return SSH_ERROR;
    }

    *out = NULL;

    rc = strncmp(signature,
                 SSHSIG_BEGIN_SIGNATURE,
                 strlen(SSHSIG_BEGIN_SIGNATURE));
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Signature does not start with expected header");
        return SSH_ERROR;
    }

    begin = signature + strlen(SSHSIG_BEGIN_SIGNATURE);
    while (isspace(*begin)) {
        begin++;
    }

    end = strstr(begin, SSHSIG_END_SIGNATURE);
    if (end == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Signature end marker not found");
        return SSH_ERROR;
    }

    /* Backtrack to find the real end of data */
    while (end > begin && (isspace(*(end - 1)))) {
        end--;
    }

    clean_b64 = calloc(end - begin + 1, 1);
    if (clean_b64 == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to allocate %td bytes for clean base64 data",
                end - begin + 1);
        return SSH_ERROR;
    }

    for (i = 0, j = 0; begin + i < end; i++) {
        if (!isspace(begin[i])) {
            clean_b64[j++] = begin[i];
        }
    }
    clean_b64[j] = '\0';

    decoded_buffer = base64_to_bin(clean_b64);
    SAFE_FREE(clean_b64);

    if (decoded_buffer == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to decode base64 signature data");
        return SSH_ERROR;
    }

    *out = decoded_buffer;
    return SSH_OK;
}

/**
 * @internal
 * @brief Common helper function to prepare the data in sshsig format
 *
 * This function handles the common logic to prepare the sshsig format:
 * 1. Hash the input data using the specified algorithm
 * 2. Build the data buffer to sign
 *
 * @param data           The raw data to process
 * @param data_length    The length of the data
 * @param hash_alg       The hash algorithm to use (sha256 or sha512)
 * @param sig_namespace  The signature namespace
 * @param tosign_buf     Pointer to store the allocated to-sign buffer
 *
 * @return SSH_OK on success, SSH_ERROR on error
 */
static int sshsig_prepare_data(const void *data,
                               size_t data_length,
                               const char *hash_alg,
                               const char *sig_namespace,
                               ssh_buffer *tosign_buf)
{
    ssh_buffer tosign = NULL;
    ssh_string hash_string = NULL;
    char hash[SHA512_DIGEST_LEN];
    size_t hash_len;
    int rc = SSH_ERROR;

    if (data == NULL || hash_alg == NULL || sig_namespace == NULL ||
        tosign_buf == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Invalid input parameters");
        return SSH_ERROR;
    }

    *tosign_buf = NULL;

    if (strcmp(hash_alg, "sha256") == 0) {
        hash_len = SHA256_DIGEST_LEN;
        rc = sha256(data, data_length, (unsigned char *)hash);
    } else if (strcmp(hash_alg, "sha512") == 0) {
        hash_len = SHA512_DIGEST_LEN;
        rc = sha512(data, data_length, (unsigned char *)hash);
    } else {
        SSH_LOG(SSH_LOG_TRACE, "Unsupported hash algorithm: %s", hash_alg);
        goto cleanup;
    }
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to compute %s hash of data", hash_alg);
        goto cleanup;
    }

    hash_string = ssh_string_new(hash_len);
    if (hash_string == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to allocate ssh_string for hash");
        goto cleanup;
    }

    rc = ssh_string_fill(hash_string, hash, hash_len);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to fill ssh_string with hash data");
        goto cleanup;
    }

    tosign = ssh_buffer_new();
    if (tosign == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to allocate buffer for signing data");
        goto cleanup;
    }

    rc = ssh_buffer_pack(tosign,
                         "tsssS",
                         SSHSIG_MAGIC_PREAMBLE,
                         sig_namespace,
                         "",
                         hash_alg,
                         hash_string);

    if (rc == SSH_OK) {
        *tosign_buf = tosign;
        tosign = NULL;
    } else {
        SSH_LOG(SSH_LOG_TRACE, "Failed to pack signing data into buffer");
    }

cleanup:
    SSH_BUFFER_FREE(tosign);
    SSH_STRING_FREE(hash_string);

    return rc;
}

/**
 * @brief Signs data in sshsig compatible format
 *
 * @param data            The data to sign
 * @param data_length     The length of the data
 * @param privkey         The private key to sign with
 * @param pki_context     The PKI context. For non-SK keys, this parameter is
 *                        ignored and can be NULL. For SK keys, can be NULL in
 *                        which case a default context with default callbacks
 *                        will be used. If provided, the context must have
 *                        sk_callbacks set with a valid sign callback
 *                        implementation. See ssh_pki_ctx_set_sk_callbacks().
 * @param sig_namespace   The signature namespace (e.g. "file", "email", etc.)
 * @param hash_alg        The hash algorithm to use (SSHSIG_DIGEST_SHA2_256 or
 *                        SSHSIG_DIGEST_SHA2_512)
 * @param signature       Pointer to store the allocated signature string in the
 *                        armored format. Must be freed with
 *                        ssh_string_free_char()
 *
 * @return SSH_OK on success, SSH_ERROR on error
 */
int sshsig_sign(const void *data,
                size_t data_length,
                ssh_key privkey,
                ssh_pki_ctx pki_context,
                const char *sig_namespace,
                enum sshsig_digest_e hash_alg,
                char **signature)
{
    ssh_buffer tosign = NULL;
    ssh_buffer signature_blob = NULL;
    ssh_signature sig = NULL;
    ssh_string sig_string = NULL;
    ssh_string pub_blob = NULL;
    ssh_pki_ctx temp_ctx = NULL;
    ssh_pki_ctx ctx_to_use = NULL;
    enum ssh_digest_e digest_type;
    const char *hash_alg_str = NULL;
    int rc = SSH_ERROR;

    if (privkey == NULL || data == NULL || sig_namespace == NULL ||
        signature == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Invalid parameters provided to sshsig_sign");
        return SSH_ERROR;
    }

    if (strlen(sig_namespace) == 0) {
        SSH_LOG(SSH_LOG_TRACE,
                "Invalid parameters provided to sshsig_sign: empty namespace "
                "string");
        return SSH_ERROR;
    }

    /* Check if this is an SK key that requires a PKI context */
    if (is_sk_key_type(privkey->type)) {
        /* If no context provided, create a temporary default one */
        if (pki_context == NULL) {
            SSH_LOG(SSH_LOG_INFO,
                    "No PKI context provided, using the default one");

            temp_ctx = ssh_pki_ctx_new();
            if (temp_ctx == NULL) {
                SSH_LOG(SSH_LOG_WARN, "Failed to create temporary PKI context");
                return SSH_ERROR;
            }
            ctx_to_use = temp_ctx;
        } else {
            ctx_to_use = pki_context;
        }

        /* Verify that we have valid SK callbacks */
        if (ctx_to_use->sk_callbacks == NULL) {
            SSH_LOG(SSH_LOG_WARN,
                    "Security Key callbacks not configured in PKI context");
            goto cleanup;
        }
    }

    *signature = NULL;

    if (hash_alg == SSHSIG_DIGEST_SHA2_256) {
        hash_alg_str = "sha256";
    } else if (hash_alg == SSHSIG_DIGEST_SHA2_512) {
        hash_alg_str = "sha512";
    } else {
        SSH_LOG(SSH_LOG_TRACE, "Invalid hash algorithm %d", hash_alg);
        return SSH_ERROR;
    }

    rc = sshsig_prepare_data(data,
                             data_length,
                             hash_alg_str,
                             sig_namespace,
                             &tosign);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to prepare data for sshsig signing");
        goto cleanup;
    }

    /* Use appropriate signing method based on key type */
    if (is_sk_key_type(privkey->type)) {
#ifdef WITH_FIDO2
        sig = pki_sk_do_sign(ctx_to_use,
                             privkey,
                             ssh_buffer_get(tosign),
                             ssh_buffer_get_len(tosign));
#else
        SSH_LOG(SSH_LOG_WARN, SK_NOT_SUPPORTED_MSG);
        goto cleanup;
#endif
    } else {
        digest_type = key_type_to_hash(ssh_key_type_plain(privkey->type));
        sig = pki_sign_data(privkey,
                            digest_type,
                            ssh_buffer_get(tosign),
                            ssh_buffer_get_len(tosign));
    }
    if (sig == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to sign data with private key");
        goto cleanup;
    }

    rc = ssh_pki_export_pubkey_blob(privkey, &pub_blob);
    if (rc != SSH_OK || pub_blob == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to export public key blob from private key");
        goto cleanup;
    }

    rc = ssh_pki_export_signature_blob(sig, &sig_string);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to export signature blob");
        goto cleanup;
    }

    signature_blob = ssh_buffer_new();
    if (signature_blob == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to allocate signature buffer");
        goto cleanup;
    }

    rc = ssh_buffer_pack(signature_blob,
                         "tdSsssS",
                         SSHSIG_MAGIC_PREAMBLE,
                         SSHSIG_VERSION,
                         pub_blob,
                         sig_namespace,
                         "",
                         hash_alg_str,
                         sig_string);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to pack signature blob");
        goto cleanup;
    }

    rc = sshsig_armor(signature_blob, signature);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to armor signature blob");
        goto cleanup;
    }

cleanup:
    SSH_BUFFER_FREE(tosign);
    SSH_BUFFER_FREE(signature_blob);
    SSH_SIGNATURE_FREE(sig);
    SSH_STRING_FREE(sig_string);
    SSH_STRING_FREE(pub_blob);

    /* Clean up temporary context if we created one */
    if (temp_ctx != NULL) {
        SSH_PKI_CTX_FREE(temp_ctx);
    }

    return rc;
}

/**
 * @brief Verifies an sshsig formatted signature against data
 *
 * @param data           The data to verify
 * @param data_length    The length of the data
 * @param signature      The armored sshsig signature
 * @param sig_namespace  The expected signature namespace
 * @param sign_key       If not NULL, returns the allocated public key that was
 *                       used for signing this data. Must be freed with
 *                       ssh_key_free(). Note that this is an output parameter
 *                       and is not checked against "allowed signers". The
 *                       caller needs to compare it with expected signer key
 *                       using ssh_key_cmp().
 *
 * @return SSH_OK on success, SSH_ERROR on verification failure
 */
int sshsig_verify(const void *data,
                  size_t data_length,
                  const char *signature,
                  const char *sig_namespace,
                  ssh_key *sign_key)
{
    ssh_buffer sig_buf = NULL;
    ssh_buffer tosign = NULL;
    ssh_key key = NULL;
    char *hash_alg_str = NULL;
    ssh_string sig_data = NULL;
    ssh_string sig_namespace_str = NULL;
    ssh_string reserved_str = NULL;
    ssh_string pubkey_blob = NULL;
    int rc = SSH_ERROR;
    ssh_signature signature_obj = NULL;
    uint32_t sig_version;

    if (sign_key != NULL) {
        *sign_key = NULL;
    }

    if (signature == NULL || data == NULL || sig_namespace == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Invalid parameters provided to sshsig_verify");
        return SSH_ERROR;
    }

    if (strlen(sig_namespace) == 0) {
        SSH_LOG(SSH_LOG_TRACE,
                "Invalid parameters provided to sshsig_verify: empty namespace "
                "string");
        return SSH_ERROR;
    }

    rc = sshsig_dearmor(signature, &sig_buf);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to dearmor signature");
        return SSH_ERROR;
    }

    if (ssh_buffer_get_len(sig_buf) < SSHSIG_MAGIC_PREAMBLE_LEN ||
        memcmp(ssh_buffer_get(sig_buf),
               SSHSIG_MAGIC_PREAMBLE,
               SSHSIG_MAGIC_PREAMBLE_LEN) != 0) {
        SSH_LOG(SSH_LOG_TRACE, "Invalid signature magic preamble");
        SSH_BUFFER_FREE(sig_buf);
        return SSH_ERROR;
    }

    ssh_buffer_pass_bytes(sig_buf, SSHSIG_MAGIC_PREAMBLE_LEN);
    rc = ssh_buffer_unpack(sig_buf,
                           "dSSSsS",
                           &sig_version,
                           &pubkey_blob,
                           &sig_namespace_str,
                           &reserved_str,
                           &hash_alg_str,
                           &sig_data);

    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to unpack signature buffer");
        SSH_BUFFER_FREE(sig_buf);
        return SSH_ERROR;
    }

    if (sig_version != SSHSIG_VERSION) {
        SSH_LOG(SSH_LOG_TRACE,
                "Unsupported signature version %u, expected %u",
                sig_version,
                SSHSIG_VERSION);
        rc = SSH_ERROR;
        goto cleanup;
    }

    rc = ssh_pki_import_pubkey_blob(pubkey_blob, &key);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to import public key from signature");
        goto cleanup;
    }

    if (ssh_string_len(sig_namespace_str) != strlen(sig_namespace) ||
        memcmp(ssh_string_data(sig_namespace_str),
               sig_namespace,
               strlen(sig_namespace)) != 0) {
        SSH_LOG(SSH_LOG_TRACE,
                "Signature namespace mismatch: expected '%s', got '%s'",
                sig_namespace,
                ssh_string_get_char(sig_namespace_str));
        rc = SSH_ERROR;
        goto cleanup;
    }

    if (strcmp(hash_alg_str, "sha256") != 0 &&
        strcmp(hash_alg_str, "sha512") != 0) {
        SSH_LOG(SSH_LOG_TRACE, "Unsupported hash algorithm '%s'", hash_alg_str);
        rc = SSH_ERROR;
        goto cleanup;
    }

    rc = sshsig_prepare_data(data,
                             data_length,
                             hash_alg_str,
                             sig_namespace,
                             &tosign);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to prepare data for sshsig verification");
        goto cleanup;
    }

    rc = ssh_pki_import_signature_blob(sig_data, key, &signature_obj);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Failed to import signature blob");
        goto cleanup;
    }

    if (is_sk_key_type(key->type)) {
        ssh_buffer sk_buffer = NULL;
        rc = pki_sk_signature_buffer_prepare(key,
                                             signature_obj,
                                             ssh_buffer_get(tosign),
                                             ssh_buffer_get_len(tosign),
                                             &sk_buffer);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_TRACE, "Failed to prepare sk signature buffer");
            goto cleanup;
        }

        rc = pki_verify_data_signature(signature_obj,
                                       key,
                                       ssh_buffer_get(sk_buffer),
                                       ssh_buffer_get_len(sk_buffer));
        SSH_BUFFER_FREE(sk_buffer);
    } else {
        rc = pki_verify_data_signature(signature_obj,
                                       key,
                                       ssh_buffer_get(tosign),
                                       ssh_buffer_get_len(tosign));
    }
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_TRACE, "Signature verification failed");
        goto cleanup;
    }

    if (sign_key != NULL) {
        *sign_key = key;
        key = NULL; /* Transferred ownership */
    }

cleanup:
    SSH_STRING_FREE(pubkey_blob);
    SSH_STRING_FREE(sig_namespace_str);
    SSH_STRING_FREE(reserved_str);
    SSH_STRING_FREE(sig_data);
    SSH_BUFFER_FREE(tosign);
    SSH_BUFFER_FREE(sig_buf);
    SSH_KEY_FREE(key);
    SAFE_FREE(hash_alg_str);
    SSH_SIGNATURE_FREE(signature_obj);

    return rc;
}

/*
 * This function signs the session id as a string then
 * the content of sigbuf */
ssh_string ssh_pki_do_sign(ssh_session session,
                           ssh_buffer sigbuf,
                           const ssh_key privkey,
                           enum ssh_digest_e hash_type)
{
    struct ssh_crypto_struct *crypto = NULL;

    ssh_signature sig = NULL;
    ssh_string sig_blob = NULL;

    ssh_string session_id = NULL;
    ssh_buffer sign_input = NULL;

    int rc;

    if (session == NULL || sigbuf == NULL || privkey == NULL ||
        !ssh_key_is_private(privkey))
    {
        SSH_LOG(SSH_LOG_TRACE, "Bad parameter provided to "
                               "ssh_pki_do_sign()");
        return NULL;
    }

    crypto = ssh_packet_get_current_crypto(session, SSH_DIRECTION_BOTH);
    if (crypto == NULL) {
        return NULL;
    }

    /* Get the session ID */
    session_id = ssh_string_new(crypto->session_id_len);
    if (session_id == NULL) {
        return NULL;
    }
    rc = ssh_string_fill(session_id, crypto->session_id, crypto->session_id_len);
    if (rc < 0) {
        goto end;
    }

    /* Fill the input */
    sign_input = ssh_buffer_new();
    if (sign_input == NULL) {
        goto end;
    }
    ssh_buffer_set_secure(sign_input);

    rc = ssh_buffer_pack(sign_input,
                         "SP",
                         session_id,
                         (size_t)ssh_buffer_get_len(sigbuf),
                         ssh_buffer_get(sigbuf));
    if (rc != SSH_OK) {
        goto end;
    }

    /* Generate the signature */
    if (is_sk_key_type(privkey->type)) {
#ifdef WITH_FIDO2
        if (session->pki_context == NULL ||
            session->pki_context->sk_callbacks == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Missing PKI context or SK callbacks");
            goto end;
        }

        rc = pki_key_check_hash_compatible(privkey, hash_type);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_WARN,
                    "Incompatible hash type %d for sk key type %d",
                    hash_type,
                    privkey->type);
            goto end;
        }

        sig = pki_sk_do_sign(session->pki_context,
                             privkey,
                             ssh_buffer_get(sign_input),
                             ssh_buffer_get_len(sign_input));
#else
        SSH_LOG(SSH_LOG_WARN, SK_NOT_SUPPORTED_MSG);
        goto end;
#endif /* WITH_FIDO2 */
    } else {
        sig = pki_do_sign(privkey,
                          ssh_buffer_get(sign_input),
                          ssh_buffer_get_len(sign_input),
                          hash_type);
    }

    if (sig == NULL) {
        goto end;
    }

    /* Convert the signature to blob */
    rc = ssh_pki_export_signature_blob(sig, &sig_blob);
    if (rc < 0) {
        sig_blob = NULL;
    }

end:
    ssh_signature_free(sig);
    SSH_BUFFER_FREE(sign_input);
    SSH_STRING_FREE(session_id);

    return sig_blob;
}

ssh_string ssh_pki_do_sign_agent(ssh_session session,
                                 struct ssh_buffer_struct *buf,
                                 const ssh_key pubkey)
{
    struct ssh_crypto_struct *crypto = NULL;
    ssh_string session_id = NULL;
    ssh_string sig_blob = NULL;
    ssh_buffer sig_buf = NULL;
    int rc;

    crypto = ssh_packet_get_current_crypto(session, SSH_DIRECTION_BOTH);
    if (crypto == NULL) {
        return NULL;
    }

    /* prepend session identifier */
    session_id = ssh_string_new(crypto->session_id_len);
    if (session_id == NULL) {
        return NULL;
    }
    rc = ssh_string_fill(session_id, crypto->session_id, crypto->session_id_len);
    if (rc < 0) {
        SSH_STRING_FREE(session_id);
        return NULL;
    }

    sig_buf = ssh_buffer_new();
    if (sig_buf == NULL) {
        SSH_STRING_FREE(session_id);
        return NULL;
    }

    rc = ssh_buffer_add_ssh_string(sig_buf, session_id);
    if (rc < 0) {
        SSH_STRING_FREE(session_id);
        SSH_BUFFER_FREE(sig_buf);
        return NULL;
    }
    SSH_STRING_FREE(session_id);

    /* append out buffer */
    if (ssh_buffer_add_buffer(sig_buf, buf) < 0) {
        SSH_BUFFER_FREE(sig_buf);
        return NULL;
    }

    /* create signature */
    sig_blob = ssh_agent_sign_data(session, pubkey, sig_buf);

    SSH_BUFFER_FREE(sig_buf);

    return sig_blob;
}

#ifdef WITH_SERVER
ssh_string ssh_srv_pki_do_sign_sessionid(ssh_session session,
                                         const ssh_key privkey,
                                         const enum ssh_digest_e digest)
{
    struct ssh_crypto_struct *crypto = NULL;
    bool allowed;
    ssh_signature sig = NULL;
    ssh_string sig_blob = NULL;

    ssh_buffer sign_input = NULL;

    int rc;

    if (session == NULL || privkey == NULL || !ssh_key_is_private(privkey)) {
        return NULL;
    }

    allowed = ssh_key_size_allowed(session, privkey);
    if (!allowed) {
        ssh_set_error(session, SSH_FATAL, "The hostkey size too small");
        return NULL;
    }

    crypto = session->next_crypto ? session->next_crypto :
                                    session->current_crypto;

    if (crypto->secret_hash == NULL){
        ssh_set_error(session, SSH_FATAL, "Missing secret_hash");
        return NULL;
    }

    /* Fill the input */
    sign_input = ssh_buffer_new();
    if (sign_input == NULL) {
        goto end;
    }
    ssh_buffer_set_secure(sign_input);

    rc = ssh_buffer_pack(sign_input,
                         "P",
                         crypto->digest_len,
                         crypto->secret_hash);
    if (rc != SSH_OK) {
        goto end;
    }

    /* Generate the signature */
    sig = pki_do_sign(privkey,
                      ssh_buffer_get(sign_input),
                      ssh_buffer_get_len(sign_input),
                      digest);
    if (sig == NULL) {
        goto end;
    }

    /* Convert the signature to blob */
    rc = ssh_pki_export_signature_blob(sig, &sig_blob);
    if (rc < 0) {
        sig_blob = NULL;
    }

end:
    ssh_signature_free(sig);
    SSH_BUFFER_FREE(sign_input);

    return sig_blob;
}
#endif /* WITH_SERVER */

/**
 * @}
 */
