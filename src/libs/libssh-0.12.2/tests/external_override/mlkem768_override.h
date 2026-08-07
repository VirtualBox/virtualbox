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

#include "libssh/mlkem_native.h"

libcrux_ml_kem_mlkem768_MlKem768KeyPair
__wrap_libcrux_ml_kem_mlkem768_portable_generate_key_pair(
    uint8_t randomness[64U]);

bool __wrap_libcrux_ml_kem_mlkem768_portable_validate_public_key(
    libcrux_ml_kem_types_MlKemPublicKey_30 *public_key);

tuple_c2 __wrap_libcrux_ml_kem_mlkem768_portable_encapsulate(
    libcrux_ml_kem_types_MlKemPublicKey_30 *public_key,
    uint8_t randomness[32U]);

void __wrap_libcrux_ml_kem_mlkem768_portable_decapsulate(
    libcrux_ml_kem_types_MlKemPrivateKey_d9 *private_key,
    libcrux_ml_kem_mlkem768_MlKem768Ciphertext *ciphertext,
    uint8_t ret[32U]);

bool internal_mlkem768_function_called(void);
void reset_mlkem768_function_called(void);
