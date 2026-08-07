/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2021 by Anderson Toshiyuki Sasaki - Red Hat, Inc.
 *
 * The SSH Library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the SSH Library; see the file COPYING. If not,
 * see <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <libssh/ed25519.h>
#include <libssh/priv.h>

#include "ed25519_override.h"

static bool internal_function_called = false;

int __wrap_crypto_sign_ed25519_keypair(ed25519_pubkey pk,
                                       ed25519_privkey sk)
{
    fprintf(stderr, "%s: Internal implementation was called\n", __func__);
    internal_function_called = true;
    return crypto_sign_ed25519_keypair(pk, sk);
}

int __wrap_crypto_sign_ed25519(unsigned char *sm,
                               uint64_t *smlen,
                               const unsigned char *m,
                               uint64_t mlen,
                               const ed25519_privkey sk)
{
    fprintf(stderr, "%s: Internal implementation was called\n", __func__);
    internal_function_called = true;
    return crypto_sign_ed25519(sm, smlen, m, mlen, sk);
}

int __wrap_crypto_sign_ed25519_open(unsigned char *m,
                                    uint64_t *mlen,
                                    const unsigned char *sm,
                                    uint64_t smlen,
                                    const ed25519_pubkey pk)
{
    fprintf(stderr, "%s: Internal implementation was called\n", __func__);
    internal_function_called = true;
    return crypto_sign_ed25519_open(m, mlen, sm, smlen, pk);
}

bool internal_ed25519_function_called(void)
{
    return internal_function_called;
}

void reset_ed25519_function_called(void)
{
    internal_function_called = false;
}
