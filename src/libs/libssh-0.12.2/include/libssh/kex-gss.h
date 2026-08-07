/*
 * kex-gss.h - GSSAPI key exchange
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2024 by Gauravsingh Sisodia <xaerru@gmail.com>
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
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
#ifndef KEX_GSS_H_
#define KEX_GSS_H_

#include "config.h"
#ifdef WITH_GSSAPI

int ssh_client_gss_kex_init(ssh_session session);
void ssh_server_gss_kex_init(ssh_session session);
int ssh_server_gss_kex_process_init(ssh_session session, ssh_buffer packet);
void ssh_client_gss_kex_remove_callbacks(ssh_session session);
void ssh_client_gss_kex_remove_callback_hostkey(ssh_session session);

#endif /* WITH_GSSAPI */
#endif /* KEX_GSS_H_ */
