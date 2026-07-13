/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2022 Zeyu Sheng <shengzeyu19_98@163.com>
 * Copyright (c) 2023 Red Hat, Inc.
 *
 * Authors: Jakub Jelen <jjelen@redhat.com>
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

#ifndef SFTP_SERVER_H
#define SFTP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "libssh/libssh.h"
#include "libssh/sftp.h"

/**
 * @defgroup libssh_sftp_server The libssh SFTP server API
 *
 * @brief SFTP server handling functions
 *
 * TODO
 *
 * @{
 */

#define SSH_SFTP_CALLBACK(name) \
    static int name(sftp_client_message message)

typedef int (*sftp_server_message_callback)(sftp_client_message message);

struct sftp_message_handler
{
    const char *name;
    const char *extended_name;
    uint8_t type;

    sftp_server_message_callback cb;
};

LIBSSH_API int sftp_channel_default_subsystem_request(ssh_session session,
                                                      ssh_channel channel,
                                                      const char *subsystem,
                                                      void *userdata);
LIBSSH_API int sftp_channel_default_data_callback(ssh_session session,
                                                  ssh_channel channel,
                                                  void *data,
                                                  uint32_t len,
                                                  int is_stderr,
                                                  void *userdata);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* SFTP_SERVER_H */
