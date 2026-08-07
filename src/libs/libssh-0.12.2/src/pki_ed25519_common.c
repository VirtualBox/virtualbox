/*
 * pki_ed25519_common.c - Common ed25519 functions
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2014 by Aris Adamantiadis
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

#include "libssh/pki.h"
#include "libssh/pki_priv.h"
#include "libssh/buffer.h"

/**
 * @internal
 *
 * @brief output a signature blob from an ed25519 signature
 *
 * @param[in] sig signature to convert
 *
 * @return Signature blob in SSH string, or NULL on error
 */
ssh_string pki_ed25519_signature_to_blob(ssh_signature sig)
{
    ssh_string sig_blob = NULL;
    int rc;

#ifdef HAVE_LIBCRYPTO
    /* When using the OpenSSL implementation, the signature is stored in raw_sig
     * which is shared by all algorithms.*/
    if (sig->raw_sig == NULL) {
        return NULL;
    }
#else
    /* When using the internal implementation, the signature is stored in an
     * algorithm specific field. */
    if (sig->ed25519_sig == NULL) {
        return NULL;
    }
#endif

    sig_blob = ssh_string_new(ED25519_SIG_LEN);
    if (sig_blob == NULL) {
        return NULL;
    }

#ifdef HAVE_LIBCRYPTO
    rc = ssh_string_fill(sig_blob, ssh_string_data(sig->raw_sig),
                         ssh_string_len(sig->raw_sig));
#else
    rc = ssh_string_fill(sig_blob, sig->ed25519_sig, ED25519_SIG_LEN);
#endif
    if (rc < 0) {
        SSH_STRING_FREE(sig_blob);
        return NULL;
    }

    return sig_blob;
}

/**
 * @internal
 *
 * @brief Convert a signature blob in an ed25519 signature.
 *
 * @param[out] sig a preinitialized signature
 *
 * @param[in] sig_blob a signature blob
 *
 * @return SSH_ERROR on error, SSH_OK on success
 */
int pki_signature_from_ed25519_blob(ssh_signature sig, ssh_string sig_blob)
{
    size_t len;

    len = ssh_string_len(sig_blob);
    if (len != ED25519_SIG_LEN){
        SSH_LOG(SSH_LOG_TRACE, "Invalid ssh-ed25519 signature len: %zu", len);
        return SSH_ERROR;
    }

#ifdef HAVE_LIBCRYPTO
    sig->raw_sig = ssh_string_copy(sig_blob);
#else
    sig->ed25519_sig = malloc(ED25519_SIG_LEN);
    if (sig->ed25519_sig == NULL){
        return SSH_ERROR;
    }
    memcpy(sig->ed25519_sig, ssh_string_data(sig_blob), ED25519_SIG_LEN);
#endif

    return SSH_OK;
}
