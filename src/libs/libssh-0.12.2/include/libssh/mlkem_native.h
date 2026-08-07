/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2025 by Red Hat, Inc.
 *
 * Author: Jakub Jelen <jjelen@redhat.com>
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

#ifndef MLKEM_NATIVE_H_
#define MLKEM_NATIVE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
A monomorphic instance of libcrux_ml_kem.types.MlKemPrivateKey
with const generics
- $2400size_t
*/
typedef struct libcrux_ml_kem_types_MlKemPrivateKey_d9_s {
    uint8_t value[2400U];
} libcrux_ml_kem_types_MlKemPrivateKey_d9;

/**
A monomorphic instance of libcrux_ml_kem.types.MlKemPublicKey
with const generics
- $1184size_t
*/
typedef struct libcrux_ml_kem_types_MlKemPublicKey_30_s {
    uint8_t value[1184U];
} libcrux_ml_kem_types_MlKemPublicKey_30;

typedef struct libcrux_ml_kem_mlkem768_MlKem768KeyPair_s {
    libcrux_ml_kem_types_MlKemPrivateKey_d9 sk;
    libcrux_ml_kem_types_MlKemPublicKey_30 pk;
} libcrux_ml_kem_mlkem768_MlKem768KeyPair;

typedef struct libcrux_ml_kem_mlkem768_MlKem768Ciphertext_s {
    uint8_t value[1088U];
} libcrux_ml_kem_mlkem768_MlKem768Ciphertext;

/**
A monomorphic instance of K.
with types libcrux_ml_kem_types_MlKemCiphertext[[$1088size_t]],
uint8_t[32size_t]

*/
typedef struct tuple_c2_s {
    libcrux_ml_kem_mlkem768_MlKem768Ciphertext fst;
    uint8_t snd[32U];
} tuple_c2;

/**
 Generate ML-KEM 768 Key Pair
*/
libcrux_ml_kem_mlkem768_MlKem768KeyPair
libcrux_ml_kem_mlkem768_portable_generate_key_pair(uint8_t randomness[64U]);

/**
 Validate a public key.

 Returns `true` if valid, and `false` otherwise.
*/
bool libcrux_ml_kem_mlkem768_portable_validate_public_key(
    libcrux_ml_kem_types_MlKemPublicKey_30 *public_key);

/**
 Encapsulate ML-KEM 768

 Generates an ([`MlKem768Ciphertext`], [`MlKemSharedSecret`]) tuple.
 The input is a reference to an [`MlKem768PublicKey`] and [`SHARED_SECRET_SIZE`]
 bytes of `randomness`.
*/
tuple_c2 libcrux_ml_kem_mlkem768_portable_encapsulate(
    libcrux_ml_kem_types_MlKemPublicKey_30 *public_key,
    uint8_t randomness[32U]);

/**
 Decapsulate ML-KEM 768

 Generates an [`MlKemSharedSecret`].
 The input is a reference to an [`MlKem768PrivateKey`] and an
 [`MlKem768Ciphertext`].
*/
void libcrux_ml_kem_mlkem768_portable_decapsulate(
    libcrux_ml_kem_types_MlKemPrivateKey_d9 *private_key,
    libcrux_ml_kem_mlkem768_MlKem768Ciphertext *ciphertext,
    uint8_t ret[32U]);

/* rename some types to be a bit more ergonomic */
#define libcrux_mlkem768_keypair    libcrux_ml_kem_mlkem768_MlKem768KeyPair_s
#define libcrux_mlkem768_pk         libcrux_ml_kem_types_MlKemPublicKey_30_s
#define libcrux_mlkem768_sk         libcrux_ml_kem_types_MlKemPrivateKey_d9_s
#define libcrux_mlkem768_ciphertext libcrux_ml_kem_mlkem768_MlKem768Ciphertext_s
#define libcrux_mlkem768_enc_result tuple_c2_s
/* defines for PRNG inputs */
#define LIBCRUX_ML_KEM_KEY_PAIR_PRNG_LEN 64U
#define LIBCRUX_ML_KEM_ENC_PRNG_LEN      32

#ifdef __cplusplus
}
#endif

#endif /* MLKEM_NATIVE_H_ */
