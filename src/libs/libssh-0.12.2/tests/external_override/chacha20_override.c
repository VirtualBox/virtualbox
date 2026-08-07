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
#include <libssh/chacha.h>
#include <libssh/priv.h>

#include "chacha20_override.h"

static bool internal_function_called = false;

void __wrap_chacha_keysetup(struct chacha_ctx *x,
                            const uint8_t *k,
                            uint32_t kbits)
#ifdef HAVE_GCC_BOUNDED_ATTRIBUTE
    __attribute__((__bounded__(__minbytes__, 2, CHACHA_MINKEYLEN)))
#endif
{
    fprintf(stderr, "%s: Internal implementation was called\n", __func__);
    internal_function_called = true;
    chacha_keysetup(x, k, kbits);
}

void __wrap_chacha_ivsetup(struct chacha_ctx *x,
                           const uint8_t *iv,
                           const uint8_t *ctr)
#ifdef HAVE_GCC_BOUNDED_ATTRIBUTE
    __attribute__((__bounded__(__minbytes__, 2, CHACHA_NONCELEN)))
    __attribute__((__bounded__(__minbytes__, 3, CHACHA_CTRLEN)))
#endif
{
    fprintf(stderr, "%s: Internal implementation was called\n", __func__);
    internal_function_called = true;
    chacha_ivsetup(x, iv, ctr);
}

void __wrap_chacha_encrypt_bytes(struct chacha_ctx *x,
                                 const uint8_t *m,
                                 uint8_t *c,
                                 uint32_t bytes)
#ifdef HAVE_GCC_BOUNDED_ATTRIBUTE
    __attribute__((__bounded__(__buffer__, 2, 4)))
    __attribute__((__bounded__(__buffer__, 3, 4)))
#endif
{
    fprintf(stderr, "%s: Internal implementation was called\n", __func__);
    internal_function_called = true;
    chacha_encrypt_bytes(x, m, c, bytes);
}

bool internal_chacha20_function_called(void)
{
    return internal_function_called;
}

void reset_chacha20_function_called(void)
{
    internal_function_called = false;
}
