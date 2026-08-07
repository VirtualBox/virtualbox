/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2025 by Red Hat, Inc.
 *
 * Author: Sahana Prasad <sahana@redhat.com>
 * Author: Pavol Žáčik <pzacik@redhat.com>
 * Author: Claude (Anthropic)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef HYBRID_MLKEM_H_
#define HYBRID_MLKEM_H_

#include "libssh/mlkem.h"
#include "libssh/wrapper.h"

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NISTP256_SHARED_SECRET_SIZE 32
#define NISTP384_SHARED_SECRET_SIZE 48

int ssh_client_hybrid_mlkem_init(ssh_session session);
void ssh_client_hybrid_mlkem_remove_callbacks(ssh_session session);

#ifdef WITH_SERVER
void ssh_server_hybrid_mlkem_init(ssh_session session);
#endif /* WITH_SERVER */

#ifdef __cplusplus
}
#endif

#endif /* HYBRID_MLKEM_H_ */
