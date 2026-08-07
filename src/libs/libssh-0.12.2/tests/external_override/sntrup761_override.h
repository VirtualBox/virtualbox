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

#include "libssh/sntrup761.h"

void __wrap_sntrup761_keypair(uint8_t *pk,
                              uint8_t *sk,
                              void *random_ctx,
                              sntrup761_random_func *random);

void __wrap_sntrup761_enc(uint8_t *c,
                          uint8_t *k,
                          const uint8_t *pk,
                          void *random_ctx,
                          sntrup761_random_func *random);

void __wrap_sntrup761_dec(uint8_t *k, const uint8_t *c, const uint8_t *sk);

bool internal_sntrup761_function_called(void);
void reset_sntrup761_function_called(void);
