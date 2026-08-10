/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2013 by Aris Adamantiadis <aris@badcode.be>
 * Copyright (c) 2023 Simon Josefsson <simon@josefsson.org>
 * Copyright (c) 2025 Jakub Jelen <jjelen@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation,
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef SNTRUP761_H_
#define SNTRUP761_H_

#include "config.h"
#include "curve25519.h"
#include "libssh.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CURVE25519
#define HAVE_SNTRUP761 1
#endif

/*
 * Derived from public domain source, written by (in alphabetical order):
 * - Daniel J. Bernstein
 * - Chitchanok Chuengsatiansup
 * - Tanja Lange
 * - Christine van Vredendaal
 */

#include <stdint.h>
#include <string.h>

#define SNTRUP761_SECRETKEY_SIZE 1763
#define SNTRUP761_PUBLICKEY_SIZE 1158
#define SNTRUP761_CIPHERTEXT_SIZE 1039
#define SNTRUP761_SIZE 32

typedef void sntrup761_random_func(void *ctx, size_t length, uint8_t *dst);

void sntrup761_keypair(uint8_t *pk,
                       uint8_t *sk,
                       void *random_ctx,
                       sntrup761_random_func *random);
void sntrup761_enc(uint8_t *c,
                   uint8_t *k,
                   const uint8_t *pk,
                   void *random_ctx,
                   sntrup761_random_func *random);
void sntrup761_dec(uint8_t *k, const uint8_t *c, const uint8_t *sk);

typedef unsigned char ssh_sntrup761_pubkey[SNTRUP761_PUBLICKEY_SIZE];
typedef unsigned char ssh_sntrup761_privkey[SNTRUP761_SECRETKEY_SIZE];
typedef unsigned char ssh_sntrup761_ciphertext[SNTRUP761_CIPHERTEXT_SIZE];

int ssh_client_sntrup761x25519_init(ssh_session session);
void ssh_client_sntrup761x25519_remove_callbacks(ssh_session session);

#ifdef WITH_SERVER
void ssh_server_sntrup761x25519_init(ssh_session session);
#endif /* WITH_SERVER */

#ifdef __cplusplus
}
#endif

#endif /* SNTRUP761_H_ */
