/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2025 by Red Hat, Inc.
 *
 * Author: Pavol Žáčik <pzacik@redhat.com>
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

#ifndef MLKEM_H_
#define MLKEM_H_

#include "libssh/crypto.h"
#include "libssh/libssh.h"
#include "libssh/session.h"

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mlkem_type_info {
    size_t pubkey_size;
    size_t ciphertext_size;
#ifdef HAVE_GCRYPT_MLKEM
    size_t privkey_size;
    enum gcry_kem_algos alg;
#elif defined(HAVE_OPENSSL_MLKEM)
    const char *name;
#else
    size_t privkey_size;
#endif
};

extern const struct mlkem_type_info MLKEM768_INFO;
#ifdef HAVE_MLKEM1024
extern const struct mlkem_type_info MLKEM1024_INFO;
#endif

#define MLKEM_SHARED_SECRET_SIZE 32

typedef unsigned char ssh_mlkem_shared_secret[MLKEM_SHARED_SECRET_SIZE];

const struct mlkem_type_info *
kex_type_to_mlkem_info(enum ssh_key_exchange_e kex_type);

int ssh_mlkem_init(ssh_session session);

int ssh_mlkem_encapsulate(ssh_session session,
                          ssh_mlkem_shared_secret shared_secret);

int ssh_mlkem_decapsulate(const ssh_session session,
                          ssh_mlkem_shared_secret shared_secret);

#ifdef __cplusplus
}
#endif

#endif /* MLKEM_H_ */
