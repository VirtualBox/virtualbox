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

#include "libssh/curve25519.h"

int __wrap_crypto_scalarmult_base(unsigned char *q,
                                  const unsigned char *n);

int __wrap_crypto_scalarmult(unsigned char *q,
                             const unsigned char *n,
                             const unsigned char *p);

bool internal_curve25519_function_called(void);
void reset_curve25519_function_called(void);
