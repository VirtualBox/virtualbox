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
#include <libssh/poly1305.h>
#include <libssh/priv.h>

static bool internal_function_called = false;

void __wrap_poly1305_auth(uint8_t out[POLY1305_TAGLEN],
                          const uint8_t *m,
                          size_t inlen,
                          const uint8_t key[POLY1305_KEYLEN])
#ifdef HAVE_GCC_BOUNDED_ATTRIBUTE
    __attribute__((__bounded__(__minbytes__, 1, POLY1305_TAGLEN)))
    __attribute__((__bounded__(__buffer__, 2, 3)))
    __attribute__((__bounded__(__minbytes__, 4, POLY1305_KEYLEN)))
#endif
{
    fprintf(stderr, "%s: Internal implementation was called\n", __func__);
    internal_function_called = true;
    poly1305_auth(out, m, inlen, key);
}

bool internal_poly1305_function_called(void)
{
    return internal_function_called;
}

void reset_poly1305_function_called(void)
{
    internal_function_called = false;
}
