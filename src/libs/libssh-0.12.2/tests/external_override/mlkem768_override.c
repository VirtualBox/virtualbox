/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2021 - 2025 Red Hat, Inc.
 *
 * Authors: Anderson Toshiyuki Sasaki
 *          Jakub Jelen <jjelen@redhat.com>
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <libssh/priv.h>

#include "libssh/mlkem_native.h"
#include "mlkem768_override.h"

static bool internal_function_called = false;

libcrux_ml_kem_mlkem768_MlKem768KeyPair
__wrap_libcrux_ml_kem_mlkem768_portable_generate_key_pair(
    uint8_t randomness[64U])
{
    fprintf(stderr, "%s: Internal implementation was called\n", __func__);
    internal_function_called = true;
    return libcrux_ml_kem_mlkem768_portable_generate_key_pair(randomness);
}

bool __wrap_libcrux_ml_kem_mlkem768_portable_validate_public_key(
    libcrux_ml_kem_types_MlKemPublicKey_30 *public_key)
{
    fprintf(stderr, "%s: Internal implementation was called\n", __func__);
    internal_function_called = true;
    return libcrux_ml_kem_mlkem768_portable_validate_public_key(public_key);
}

tuple_c2 __wrap_libcrux_ml_kem_mlkem768_portable_encapsulate(
    libcrux_ml_kem_types_MlKemPublicKey_30 *public_key,
    uint8_t randomness[32U])
{
    fprintf(stderr, "%s: Internal implementation was called\n", __func__);
    internal_function_called = true;
    return libcrux_ml_kem_mlkem768_portable_encapsulate(public_key, randomness);
}

void __wrap_libcrux_ml_kem_mlkem768_portable_decapsulate(
    libcrux_ml_kem_types_MlKemPrivateKey_d9 *private_key,
    libcrux_ml_kem_mlkem768_MlKem768Ciphertext *ciphertext,
    uint8_t ret[32U])
{
    fprintf(stderr, "%s: Internal implementation was called\n", __func__);
    internal_function_called = true;
    return libcrux_ml_kem_mlkem768_portable_decapsulate(private_key,
                                                        ciphertext,
                                                        ret);
}

bool internal_mlkem768_function_called(void)
{
    return internal_function_called;
}

void reset_mlkem768_function_called(void)
{
    internal_function_called = false;
}
