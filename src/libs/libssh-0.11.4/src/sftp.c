/*
 * sftp.c - Secure FTP functions
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2005-2008 by Aris Adamantiadis
 * Copyright (c) 2008-2018 by Andreas Schneider <asn@cryptomilk.org>
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

/* This file contains code written by Nick Zitzmann */

#include "config.h"

#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>

#ifndef _WIN32
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "libssh/priv.h"
#include "libssh/ssh2.h"
#include "libssh/sftp.h"
#include "libssh/sftp_priv.h"
#include "libssh/buffer.h"
#include "libssh/channels.h"
#include "libssh/session.h"
#include "libssh/misc.h"
#include "libssh/bytearray.h"

#ifdef WITH_SFTP

struct sftp_ext_struct {
  uint32_t count;
  char **name;
  char **data;
};

static sftp_ext sftp_ext_new(void) {
  sftp_ext ext;

  ext = calloc(1, sizeof(struct sftp_ext_struct));
  if (ext == NULL) {
    return NULL;
  }

  return ext;
}

static void sftp_ext_free(sftp_ext ext)
{
    size_t i;

    if (ext == NULL) {
        return;
    }

    if (ext->count > 0) {
        if (ext->name != NULL) {
            for (i = 0; i < ext->count; i++) {
                SAFE_FREE(ext->name[i]);
            }
            SAFE_FREE(ext->name);
        }

        if (ext->data != NULL) {
            for (i = 0; i < ext->count; i++) {
                SAFE_FREE(ext->data[i]);
            }
            SAFE_FREE(ext->data);
        }
    }

    SAFE_FREE(ext);
}

sftp_session sftp_new(ssh_session session)
{
    sftp_session sftp;

    if (session == NULL) {
        return NULL;
    }

    sftp = calloc(1, sizeof(struct sftp_session_struct));
    if (sftp == NULL) {
        ssh_set_error_oom(session);

        return NULL;
    }

    sftp->ext = sftp_ext_new();
    if (sftp->ext == NULL) {
        ssh_set_error_oom(session);
        goto error;
    }

    sftp->read_packet = calloc(1, sizeof(struct sftp_packet_struct));
    if (sftp->read_packet == NULL) {
        ssh_set_error_oom(session);
        goto error;
    }

    sftp->read_packet->payload = ssh_buffer_new();
    if (sftp->read_packet->payload == NULL) {
        ssh_set_error_oom(session);
        goto error;
    }

    sftp->session = session;
    sftp->channel = ssh_channel_new(session);
    if (sftp->channel == NULL) {
        ssh_set_error_oom(session);
        goto error;
    }

    if (ssh_channel_open_session(sftp->channel)) {
        goto error;
    }

    if (ssh_channel_request_sftp(sftp->channel)) {
        goto error;
    }

    return sftp;
error:
    if (sftp->ext != NULL) {
        sftp_ext_free(sftp->ext);
    }
    if (sftp->channel != NULL) {
        ssh_channel_free(sftp->channel);
    }
    if (sftp->read_packet != NULL) {
        if (sftp->read_packet->payload != NULL) {
            SSH_BUFFER_FREE(sftp->read_packet->payload);
        }
        SAFE_FREE(sftp->read_packet);
    }
    SAFE_FREE(sftp);
    return NULL;
}

sftp_session
sftp_new_channel(ssh_session session, ssh_channel channel)
{
    sftp_session sftp = NULL;

    if (session == NULL) {
        return NULL;
    }

    sftp = calloc(1, sizeof(struct sftp_session_struct));
    if (sftp == NULL) {
        ssh_set_error_oom(session);
        return NULL;
    }

    sftp->ext = sftp_ext_new();
    if (sftp->ext == NULL) {
        ssh_set_error_oom(session);
        goto error;
    }

    sftp->read_packet = calloc(1, sizeof(struct sftp_packet_struct));
    if (sftp->read_packet == NULL) {
        ssh_set_error_oom(session);
        goto error;
    }

    sftp->read_packet->payload = ssh_buffer_new();
    if (sftp->read_packet->payload == NULL) {
        ssh_set_error_oom(session);
        goto error;
    }

    sftp->session = session;
    sftp->channel = channel;

    return sftp;

error:
    if (sftp->ext != NULL) {
        sftp_ext_free(sftp->ext);
    }
    if (sftp->read_packet != NULL) {
        if (sftp->read_packet->payload != NULL) {
            SSH_BUFFER_FREE(sftp->read_packet->payload);
        }
        SAFE_FREE(sftp->read_packet);
    }
    SAFE_FREE(sftp);
    return NULL;
}

#ifdef WITH_SERVER
sftp_session
sftp_server_new(ssh_session session, ssh_channel chan)
{
    sftp_session sftp = NULL;

    sftp = calloc(1, sizeof(struct sftp_session_struct));
    if (sftp == NULL) {
        ssh_set_error_oom(session);
        return NULL;
    }

    sftp->read_packet = calloc(1, sizeof(struct sftp_packet_struct));
    if (sftp->read_packet == NULL) {
        goto error;
    }

    sftp->read_packet->payload = ssh_buffer_new();
    if (sftp->read_packet->payload == NULL) {
        goto error;
    }

    sftp->session = session;
    sftp->channel = chan;

    return sftp;

error:
    ssh_set_error_oom(session);
    if (sftp->read_packet != NULL) {
        if (sftp->read_packet->payload != NULL) {
            SSH_BUFFER_FREE(sftp->read_packet->payload);
        }
        SAFE_FREE(sftp->read_packet);
    }
    SAFE_FREE(sftp);
    return NULL;
}

/* @deprecated in favor of sftp_server_new() and callbacks based sftp server */
int sftp_server_init(sftp_session sftp)
{
    ssh_session session = sftp->session;
    sftp_client_message msg = NULL;
    int rc;

    /* handles setting the sftp->client_version */
    msg = sftp_get_client_message(sftp);
    if (msg == NULL) {
        return -1;
    }

    if (msg->type != SSH_FXP_INIT) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Packet read of type %d instead of SSH_FXP_INIT",
                      msg->type);
        return -1;
    }

    SSH_LOG(SSH_LOG_PACKET, "Received SSH_FXP_INIT");

    rc = sftp_reply_version(msg);
    if (rc != SSH_OK) {
        ssh_set_error(session,
                      SSH_FATAL,
                      "Failed to process the SSH_FXP_INIT message");
        return -1;
    }

    return 0;
}

void sftp_server_free(sftp_session sftp)
{
    sftp_request_queue ptr;

    if (sftp == NULL) {
        return;
    }

    ptr = sftp->queue;
    while(ptr) {
        sftp_request_queue old;
        sftp_message_free(ptr->message);
        old = ptr->next;
        SAFE_FREE(ptr);
        ptr = old;
    }

    SAFE_FREE(sftp->handles);
    SSH_BUFFER_FREE(sftp->read_packet->payload);
    SAFE_FREE(sftp->read_packet);

    sftp_ext_free(sftp->ext);

    SAFE_FREE(sftp);
}

#endif /* WITH_SERVER */

void sftp_free(sftp_session sftp)
{
    sftp_request_queue ptr;

    if (sftp == NULL) {
        return;
    }

    if (sftp->channel != NULL) {
        ssh_channel_send_eof(sftp->channel);
        ptr = sftp->queue;
        while(ptr) {
            sftp_request_queue old;
            sftp_message_free(ptr->message);
            old = ptr->next;
            SAFE_FREE(ptr);
            ptr = old;
        }

        ssh_channel_free(sftp->channel);
        sftp->channel = NULL;
    }

    SAFE_FREE(sftp->handles);
    SSH_BUFFER_FREE(sftp->read_packet->payload);
    SAFE_FREE(sftp->read_packet);

    sftp_ext_free(sftp->ext);
    sftp_limits_free(sftp->limits);

    SAFE_FREE(sftp);
}

/* @internal
 * Process the incoming data and copy them from the SSH packet buffer to the
 * SFTP packet buffer.
 * @returns number of decoded bytes.
 */
int
sftp_decode_channel_data_to_packet(sftp_session sftp, void *data, uint32_t len)
{
    sftp_packet packet = sftp->read_packet;
    size_t nread;
    size_t payload_len;
    size_t data_offset;
    size_t to_read, rc;

    if (packet->sftp == NULL) {
        packet->sftp = sftp;
    }

    data_offset = sizeof(uint32_t) + sizeof(uint8_t);
    /* not enough bytes to read */
    if (len < data_offset) {
        return SSH_ERROR;
    }

    payload_len = PULL_BE_U32(data, 0);
    packet->type = PULL_BE_U8(data, 4);

    /* We should check the legality of payload length */
    if (payload_len > len - sizeof(uint32_t) || payload_len < sizeof(uint8_t)) {
        return SSH_ERROR;
    }

    to_read = payload_len - sizeof(uint8_t);
    rc = ssh_buffer_add_data(packet->payload,
                             (void*)((uint8_t *)data + data_offset),
                             to_read);
    if (rc != 0) {
        return SSH_ERROR;
    }
    nread = ssh_buffer_get_len(packet->payload);

    /* We should check if we copied the whole data */
    if (nread != to_read) {
        return SSH_ERROR;
    }

    /*
     * We should return how many bytes we decoded, including packet length
     * header and the payload length.
     * This can't overflow as we pulled this from unit32_t and checked this fits
     * into the buffer's max size of 0x10000000 (256MB).
     */
    return (int)(payload_len + sizeof(uint32_t));
}

/* Get the last sftp error */
int sftp_get_error(sftp_session sftp) {
  if (sftp == NULL) {
    return -1;
  }

  return sftp->errnum;
}

static sftp_limits_t sftp_limits_use_extension(sftp_session sftp);
static sftp_limits_t sftp_limits_use_default(sftp_session sftp);

/* Initialize the sftp session with the server. */
int sftp_init(sftp_session sftp)
{
    sftp_packet packet = NULL;
    ssh_buffer buffer = NULL;
    char *ext_name = NULL;
    char *ext_data = NULL;
    uint32_t version;
    int rc;

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    rc = ssh_buffer_pack(buffer, "d", LIBSFTP_VERSION);
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_INIT, buffer);
    if (rc == SSH_ERROR) {
        SSH_BUFFER_FREE(buffer);
        return -1;
    }

    SSH_BUFFER_FREE(buffer);

    packet = sftp_packet_read(sftp);
    if (packet == NULL) {
        return -1;
    }

    if (packet->type != SSH_FXP_VERSION) {
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Received a %d messages instead of SSH_FXP_VERSION",
                      packet->type);
        return -1;
    }

    /* TODO: are we sure there are 4 bytes ready? */
    rc = ssh_buffer_unpack(packet->payload, "d", &version);
    if (rc != SSH_OK) {
        ssh_set_error(sftp->session,
                      SSH_FATAL,
                      "Unable to unpack SSH_FXP_VERSION packet");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    SSH_LOG(SSH_LOG_DEBUG,
            "SFTP server version %" PRIu32,
            version);
    rc = ssh_buffer_unpack(packet->payload, "s", &ext_name);
    while (rc == SSH_OK) {
        uint32_t count = sftp->ext->count;
        char **tmp;

        rc = ssh_buffer_unpack(packet->payload, "s", &ext_data);
        if (rc == SSH_ERROR) {
            break;
        }

        SSH_LOG(SSH_LOG_DEBUG,
                "SFTP server extension: %s, version: %s",
                ext_name, ext_data);

        count++;
        tmp = realloc(sftp->ext->name, count * sizeof(char *));
        if (tmp == NULL) {
            ssh_set_error_oom(sftp->session);
            SAFE_FREE(ext_name);
            SAFE_FREE(ext_data);
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return -1;
        }

        tmp[count - 1] = ext_name;
        sftp->ext->name = tmp;

        tmp = realloc(sftp->ext->data, count * sizeof(char *));
        if (tmp == NULL) {
            ssh_set_error_oom(sftp->session);
            SAFE_FREE(ext_name);
            SAFE_FREE(ext_data);
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return -1;
        }

        tmp[count - 1] = ext_data;
        sftp->ext->data = tmp;

        sftp->ext->count = count;

        rc = ssh_buffer_unpack(packet->payload, "s", &ext_name);
    }

    sftp->version = sftp->server_version = (int)version;

    /* Set the limits */
    rc = sftp_extension_supported(sftp, "limits@openssh.com", "1");
    if (rc == 1) {
        /* Get the ssh and sftp errors */
        const char *static_ssh_err_msg = ssh_get_error(sftp->session);
        int ssh_err_code = ssh_get_error_code(sftp->session);
        int sftp_err_code = sftp_get_error(sftp);
        char *ssh_err_msg = strdup(static_ssh_err_msg);
        if (ssh_err_msg == NULL) {
            ssh_set_error_oom(sftp->session);
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return -1;
        }

        sftp->limits = sftp_limits_use_extension(sftp);
        if (sftp->limits == NULL) {
            /* fallback and use the default limits on failure */
            SSH_LOG(SSH_LOG_TRACE,
                    "Failed to get the limits from a server claiming to "
                    "support the limits@openssh.com extension, falling back "
                    "and using the default limits");

            /* Restore the sftp and ssh errors to their previous state */
            ssh_set_error(sftp->session, ssh_err_code, "%s", ssh_err_msg);
            sftp_set_error(sftp, sftp_err_code);
            SAFE_FREE(ssh_err_msg);

            sftp->limits = sftp_limits_use_default(sftp);
            if (sftp->limits == NULL) {
                return -1;
            }
        } else {
            SAFE_FREE(ssh_err_msg);
        }
    } else {
        sftp->limits = sftp_limits_use_default(sftp);
        if (sftp->limits == NULL) {
            return -1;
        }
    }

    return 0;
}

unsigned int sftp_extensions_get_count(sftp_session sftp)
{
    if (sftp == NULL || sftp->ext == NULL) {
        return 0;
    }

    return sftp->ext->count;
}

const char *sftp_extensions_get_name(sftp_session sftp, unsigned int idx)
{
    if (sftp == NULL) {
        return NULL;
    }

    if (sftp->ext == NULL || sftp->ext->name == NULL) {
        ssh_set_error_invalid(sftp->session);
        return NULL;
    }

    if (idx >= sftp->ext->count) {
        ssh_set_error_invalid(sftp->session);
        return NULL;
    }

    return sftp->ext->name[idx];
}

const char *sftp_extensions_get_data(sftp_session sftp, unsigned int idx)
{
    if (sftp == NULL) {
        return NULL;
    }

    if (sftp->ext == NULL || sftp->ext->name == NULL) {
        ssh_set_error_invalid(sftp->session);
        return NULL;
    }

    if (idx >= sftp->ext->count) {
        ssh_set_error_invalid(sftp->session);
        return NULL;
    }

    return sftp->ext->data[idx];
}

int sftp_extension_supported(sftp_session sftp, const char *name,
    const char *data) {
  size_t i, n;

  if (sftp == NULL || name == NULL || data == NULL) {
    return 0;
  }

  n = sftp_extensions_get_count(sftp);
  for (i = 0; i < n; i++) {
    const char *ext_name = sftp_extensions_get_name(sftp, i);
    const char *ext_data = sftp_extensions_get_data(sftp, i);

    if (ext_name != NULL && ext_data != NULL &&
        strcmp(ext_name, name) == 0 &&
        strcmp(ext_data, data) == 0) {
      return 1;
    }
  }

  return 0;
}

static sftp_file parse_handle_msg(sftp_message msg){
  sftp_file file;

  if(msg->packet_type != SSH_FXP_HANDLE) {
    ssh_set_error(msg->sftp->session, SSH_FATAL,
        "Not a ssh_fxp_handle message passed in!");
    return NULL;
  }

  file = calloc(1, sizeof(struct sftp_file_struct));
  if (file == NULL) {
    ssh_set_error_oom(msg->sftp->session);
    sftp_set_error(msg->sftp, SSH_FX_FAILURE);
    return NULL;
  }

  file->handle = ssh_buffer_get_ssh_string(msg->payload);
  if (file->handle == NULL) {
    ssh_set_error(msg->sftp->session, SSH_FATAL,
        "Invalid SSH_FXP_HANDLE message");
    SAFE_FREE(file);
    sftp_set_error(msg->sftp, SSH_FX_FAILURE);
    return NULL;
  }

  file->sftp = msg->sftp;
  file->offset = 0;
  file->eof = 0;

  return file;
}

/* Open a directory */
sftp_dir sftp_opendir(sftp_session sftp, const char *path)
{
    sftp_message msg = NULL;
    sftp_file file = NULL;
    sftp_dir dir = NULL;
    sftp_status_message status;
    ssh_buffer payload = NULL;
    uint32_t id;
    int rc;

    if (sftp == NULL) {
        return NULL;
    }

    payload = ssh_buffer_new();
    if (payload == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(payload,
                         "ds",
                         id,
                         path);
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(payload);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_OPENDIR, payload);
    SSH_BUFFER_FREE(payload);
    if (rc < 0) {
        return NULL;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            /* something nasty has happened */
            return NULL;
        }
        msg = sftp_dequeue(sftp, id);
    }

    switch (msg->packet_type) {
        case SSH_FXP_STATUS:
            status = parse_status_msg(msg);
            sftp_message_free(msg);
            if (status == NULL) {
                return NULL;
            }
            sftp_set_error(sftp, status->status);
            ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                          "SFTP server: %s", status->errormsg);
            status_msg_free(status);
            return NULL;
        case SSH_FXP_HANDLE:
            file = parse_handle_msg(msg);
            sftp_message_free(msg);
            if (file != NULL) {
                dir = calloc(1, sizeof(struct sftp_dir_struct));
                if (dir == NULL) {
                    ssh_set_error_oom(sftp->session);
                    free(file);
                    return NULL;
                }

                dir->sftp = sftp;
                dir->name = strdup(path);
                if (dir->name == NULL) {
                    SAFE_FREE(dir);
                    SAFE_FREE(file);
                    return NULL;
                }
                dir->handle = file->handle;
                SAFE_FREE(file);
            }
            return dir;
        default:
            ssh_set_error(sftp->session, SSH_FATAL,
                          "Received message %d during opendir!",
                          msg->packet_type);
            sftp_message_free(msg);
    }

    return NULL;
}

/* Get the version of the SFTP protocol supported by the server */
int sftp_server_version(sftp_session sftp) {
  return sftp->server_version;
}

/* Get a single file attributes structure of a directory. */
sftp_attributes sftp_readdir(sftp_session sftp, sftp_dir dir)
{
    sftp_message msg = NULL;
    sftp_status_message status;
    sftp_attributes attr;
    ssh_buffer payload;
    uint32_t id;
    int rc;

    if (dir->buffer == NULL) {
        payload = ssh_buffer_new();
        if (payload == NULL) {
            ssh_set_error_oom(sftp->session);
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return NULL;
        }

        id = sftp_get_new_id(sftp);

        rc = ssh_buffer_pack(payload,
                             "dS",
                             id,
                             dir->handle);
        if (rc != 0) {
            ssh_set_error_oom(sftp->session);
            sftp_set_error(sftp, SSH_FX_FAILURE);
            SSH_BUFFER_FREE(payload);
            return NULL;
        }

        rc = sftp_packet_write(sftp, SSH_FXP_READDIR, payload);
        SSH_BUFFER_FREE(payload);
        if (rc < 0) {
            return NULL;
        }

        SSH_LOG(SSH_LOG_PACKET,
                "Sent a ssh_fxp_readdir with id %" PRIu32, id);

        while (msg == NULL) {
            if (sftp_read_and_dispatch(sftp) < 0) {
                /* something nasty has happened */
                return NULL;
            }
            msg = sftp_dequeue(sftp, id);
        }

        switch (msg->packet_type){
            case SSH_FXP_STATUS:
                status = parse_status_msg(msg);
                sftp_message_free(msg);
                if (status == NULL) {
                    return NULL;
                }
                sftp_set_error(sftp, status->status);
                switch (status->status) {
                    case SSH_FX_EOF:
                        dir->eof = 1;
                        status_msg_free(status);
                        return NULL;
                    default:
                        break;
                }

                ssh_set_error(sftp->session, SSH_FATAL,
                        "Unknown error status: %" PRIu32, status->status);
                status_msg_free(status);

                return NULL;
            case SSH_FXP_NAME:
                ssh_buffer_get_u32(msg->payload, &dir->count);
                dir->count = ntohl(dir->count);
                dir->buffer = msg->payload;
                msg->payload = NULL;
                sftp_message_free(msg);
                break;
            default:
                ssh_set_error(sftp->session, SSH_FATAL,
                        "Unsupported message back %d", msg->packet_type);
                sftp_message_free(msg);
                sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);

                return NULL;
        }
    }

    /* now dir->buffer contains a buffer and dir->count != 0 */
    if (dir->count == 0) {
        ssh_set_error(sftp->session, SSH_FATAL,
                "Count of files sent by the server is zero, which is invalid, or "
                "libsftp bug");
        return NULL;
    }

    SSH_LOG(SSH_LOG_DEBUG, "Count is %" PRIu32, dir->count);

    attr = sftp_parse_attr(sftp, dir->buffer, 1);
    if (attr == NULL) {
        ssh_set_error(sftp->session, SSH_FATAL,
                "Couldn't parse the SFTP attributes");
        return NULL;
    }

    dir->count--;
    if (dir->count == 0) {
        SSH_BUFFER_FREE(dir->buffer);
        dir->buffer = NULL;
    }

    return attr;
}

/* Tell if the directory has reached EOF (End Of File). */
int sftp_dir_eof(sftp_dir dir) {
  return dir->eof;
}

/* Free a SFTP_ATTRIBUTE handle */
void sftp_attributes_free(sftp_attributes file){
  if (file == NULL) {
    return;
  }

  SSH_STRING_FREE(file->acl);
  SSH_STRING_FREE(file->extended_data);
  SSH_STRING_FREE(file->extended_type);

  SAFE_FREE(file->name);
  SAFE_FREE(file->longname);
  SAFE_FREE(file->group);
  SAFE_FREE(file->owner);

  SAFE_FREE(file);
}

static int sftp_handle_close(sftp_session sftp, ssh_string handle)
{
    sftp_status_message status;
    sftp_message msg = NULL;
    ssh_buffer buffer = NULL;
    uint32_t id;
    int rc;

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "dS",
                         id,
                         handle);
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_CLOSE, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return -1;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            /* something nasty has happened */
            return -1;
        }
        msg = sftp_dequeue(sftp,id);
    }

    switch (msg->packet_type) {
        case SSH_FXP_STATUS:
            status = parse_status_msg(msg);
            sftp_message_free(msg);
            if(status == NULL) {
                return -1;
            }
            sftp_set_error(sftp, status->status);
            switch (status->status) {
                case SSH_FX_OK:
                    status_msg_free(status);
                    return 0;
                    break;
                default:
                    break;
            }
            ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                    "SFTP server: %s", status->errormsg);
            status_msg_free(status);
            return -1;
        default:
            ssh_set_error(sftp->session, SSH_FATAL,
                    "Received message %d during sftp_handle_close!", msg->packet_type);
            sftp_message_free(msg);
            sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return -1;
}

/* Close an open file handle. */
int sftp_close(sftp_file file){
  int err = SSH_NO_ERROR;

  if (file == NULL) {
      return err;
  }

  SAFE_FREE(file->name);
  if (file->handle){
    err = sftp_handle_close(file->sftp,file->handle);
    SSH_STRING_FREE(file->handle);
  }
  /* FIXME: check server response and implement errno */
  SAFE_FREE(file);

  return err;
}

/* Close an open directory. */
int sftp_closedir(sftp_dir dir){
  int err = SSH_NO_ERROR;

  SAFE_FREE(dir->name);
  if (dir->handle) {
    err = sftp_handle_close(dir->sftp, dir->handle);
    SSH_STRING_FREE(dir->handle);
  }
  /* FIXME: check server response and implement errno */
  SSH_BUFFER_FREE(dir->buffer);
  SAFE_FREE(dir);

  return err;
}

/* Open a file on the server. */
sftp_file sftp_open(sftp_session sftp,
                    const char *file,
                    int flags,
                    mode_t mode)
{
    sftp_message msg = NULL;
    sftp_status_message status;
    struct sftp_attributes_struct attr;
    sftp_file handle;
    ssh_buffer buffer = NULL;
    sftp_attributes stat_data;
    uint32_t sftp_flags = 0;
    uint32_t id;
    int rc;

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        return NULL;
    }

    ZERO_STRUCT(attr);
    attr.permissions = mode;
    attr.flags = SSH_FILEXFER_ATTR_PERMISSIONS;

    if ((flags & O_RDWR) == O_RDWR) {
        sftp_flags |= (SSH_FXF_WRITE | SSH_FXF_READ);
    } else if ((flags & O_WRONLY) == O_WRONLY) {
        sftp_flags |= SSH_FXF_WRITE;
    } else {
        sftp_flags |= SSH_FXF_READ;
    }
    if ((flags & O_CREAT) == O_CREAT)
        sftp_flags |= SSH_FXF_CREAT;
    if ((flags & O_TRUNC) == O_TRUNC)
        sftp_flags |= SSH_FXF_TRUNC;
    if ((flags & O_EXCL) == O_EXCL)
        sftp_flags |= SSH_FXF_EXCL;
    if ((flags & O_APPEND) == O_APPEND) {
        sftp_flags |= SSH_FXF_APPEND;
    }
    SSH_LOG(SSH_LOG_PACKET, "Opening file %s with sftp flags %" PRIx32,
            file, sftp_flags);
    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "dsd",
                         id,
                         file,
                         sftp_flags);
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = buffer_add_attributes(buffer, &attr);
    if (rc < 0) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_OPEN, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return NULL;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            /* something nasty has happened */
            return NULL;
        }
        msg = sftp_dequeue(sftp, id);
    }

    switch (msg->packet_type) {
    case SSH_FXP_STATUS:
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return NULL;
        }
        sftp_set_error(sftp, status->status);
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                      "SFTP server: %s", status->errormsg);
        status_msg_free(status);

        return NULL;
    case SSH_FXP_HANDLE:
        handle = parse_handle_msg(msg);
        if (handle == NULL) {
            return NULL;
        }
        sftp_message_free(msg);
        if ((flags & O_APPEND) == O_APPEND) {
            stat_data = sftp_stat(sftp, file);
            if (stat_data == NULL) {
                sftp_close(handle);
                return NULL;
            }
            if ((stat_data->flags & SSH_FILEXFER_ATTR_SIZE) != SSH_FILEXFER_ATTR_SIZE) {
                ssh_set_error(sftp->session,
                              SSH_FATAL,
                              "Cannot open in append mode. Unknown file size.");
                sftp_attributes_free(stat_data);
                sftp_close(handle);
                sftp_set_error(sftp, SSH_FX_FAILURE);
                return NULL;
            }

            handle->offset = stat_data->size;
            sftp_attributes_free(stat_data);
        }
        return handle;
    default:
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Received message %d during open!", msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return NULL;
}

void sftp_file_set_nonblocking(sftp_file handle)
{
    handle->nonblocking = 1;
}
void sftp_file_set_blocking(sftp_file handle)
{
    handle->nonblocking = 0;
}

/* Read from a file using an opened sftp file handle. */
ssize_t sftp_read(sftp_file handle, void *buf, size_t count) {
  sftp_session sftp;
  sftp_message msg = NULL;
  sftp_status_message status;
  ssh_string datastring;
  size_t datalen;
  ssh_buffer buffer;
  uint32_t id;
  int rc;

  if (handle == NULL) {
      return -1;
  }
  sftp = handle->sftp;

  if (handle->eof) {
    return 0;
  }

  /*
   * limit the reads to the maximum specified in Section 3 of
   * https://datatracker.ietf.org/doc/html/draft-ietf-secsh-filexfer-02
   * or to the values provided by the limits@openssh.com extension.
   *
   * TODO: We should iterate over the blocks rather than writing less than
   * requested to provide less surprises to the calling applications.
   */
  if (count > sftp->limits->max_read_length) {
      count = sftp->limits->max_read_length;
  }

  buffer = ssh_buffer_new();
  if (buffer == NULL) {
    ssh_set_error_oom(sftp->session);
    return -1;
  }

  id = sftp_get_new_id(handle->sftp);

  rc = ssh_buffer_pack(buffer,
                       "dSqd",
                       id,
                       handle->handle,
                       handle->offset,
                       count);
  if (rc != SSH_OK){
    ssh_set_error_oom(sftp->session);
    SSH_BUFFER_FREE(buffer);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return -1;
  }
  if (sftp_packet_write(handle->sftp, SSH_FXP_READ, buffer) < 0) {
    SSH_BUFFER_FREE(buffer);
    return -1;
  }
  SSH_BUFFER_FREE(buffer);

  while (msg == NULL) {
    if (handle->nonblocking) {
      if (ssh_channel_poll(handle->sftp->channel, 0) == 0) {
        /* we cannot block */
        return 0;
      }
    }
    if (sftp_read_and_dispatch(handle->sftp) < 0) {
      /* something nasty has happened */
      return -1;
    }
    msg = sftp_dequeue(handle->sftp, id);
  }

  switch (msg->packet_type) {
    case SSH_FXP_STATUS:
      status = parse_status_msg(msg);
      sftp_message_free(msg);
      if (status == NULL) {
        return -1;
      }
      sftp_set_error(sftp, status->status);
      switch (status->status) {
        case SSH_FX_EOF:
          handle->eof = 1;
          status_msg_free(status);
          return 0;
        default:
          break;
      }
      ssh_set_error(sftp->session,SSH_REQUEST_DENIED,
          "SFTP server: %s", status->errormsg);
      status_msg_free(status);
      return -1;
    case SSH_FXP_DATA:
      datastring = ssh_buffer_get_ssh_string(msg->payload);
      sftp_message_free(msg);
      if (datastring == NULL) {
        ssh_set_error(sftp->session, SSH_FATAL,
            "Received invalid DATA packet from sftp server");
        return -1;
      }

      datalen = ssh_string_len(datastring);
      if (datalen > count) {
        ssh_set_error(sftp->session, SSH_FATAL,
            "Received a too big DATA packet from sftp server: "
            "%zu and asked for %zu",
            datalen, count);
        SSH_STRING_FREE(datastring);
        return -1;
      }
      handle->offset += (uint64_t)datalen;
      memcpy(buf, ssh_string_data(datastring), datalen);
      SSH_STRING_FREE(datastring);
      return datalen;
    default:
      ssh_set_error(sftp->session, SSH_FATAL,
          "Received message %d during read!", msg->packet_type);
      sftp_message_free(msg);
      sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
      return -1;
  }

  return -1; /* not reached */
}

/* Start an asynchronous read from a file using an opened sftp file handle. */
int sftp_async_read_begin(sftp_file file, uint32_t len){
  sftp_session sftp = file->sftp;
  ssh_buffer buffer;
  uint32_t id;
  int rc;

  buffer = ssh_buffer_new();
  if (buffer == NULL) {
    ssh_set_error_oom(sftp->session);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return -1;
  }

  id = sftp_get_new_id(sftp);

  rc = ssh_buffer_pack(buffer,
                       "dSqd",
                       id,
                       file->handle,
                       file->offset,
                       len);
  if (rc != SSH_OK) {
    ssh_set_error_oom(sftp->session);
    SSH_BUFFER_FREE(buffer);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return -1;
  }
  if (sftp_packet_write(sftp, SSH_FXP_READ, buffer) < 0) {
    SSH_BUFFER_FREE(buffer);
    return -1;
  }
  SSH_BUFFER_FREE(buffer);

  file->offset += len; /* assume we'll read len bytes */

  return id;
}

/* Wait for an asynchronous read to complete and save the data. */
int sftp_async_read(sftp_file file, void *data, uint32_t size, uint32_t id){
  sftp_session sftp;
  sftp_message msg = NULL;
  sftp_status_message status;
  ssh_string datastring;
  int err = SSH_OK;
  uint32_t len;

  if (file == NULL) {
    return SSH_ERROR;
  }
  sftp = file->sftp;

  if (file->eof) {
    return 0;
  }

  /* handle an existing request */
  while (msg == NULL) {
    if (file->nonblocking){
      if (ssh_channel_poll(sftp->channel, 0) == 0) {
        /* we cannot block */
        return SSH_AGAIN;
      }
    }

    if (sftp_read_and_dispatch(sftp) < 0) {
      /* something nasty has happened */
      return SSH_ERROR;
    }

    msg = sftp_dequeue(sftp,id);
  }

  switch (msg->packet_type) {
    case SSH_FXP_STATUS:
      status = parse_status_msg(msg);
      sftp_message_free(msg);
      if (status == NULL) {
        return -1;
      }
      sftp_set_error(sftp, status->status);
      if (status->status != SSH_FX_EOF) {
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
            "SFTP server : %s", status->errormsg);
        err = SSH_ERROR;
      } else {
        file->eof = 1;
      }
      status_msg_free(status);
      return err;
    case SSH_FXP_DATA:
      datastring = ssh_buffer_get_ssh_string(msg->payload);
      sftp_message_free(msg);
      if (datastring == NULL) {
        ssh_set_error(sftp->session, SSH_FATAL,
            "Received invalid DATA packet from sftp server");
        return SSH_ERROR;
      }
      if (ssh_string_len(datastring) > size) {
        ssh_set_error(sftp->session, SSH_FATAL,
            "Received a too big DATA packet from sftp server: "
            "%zu and asked for %" PRIu32,
            ssh_string_len(datastring), size);
        SSH_STRING_FREE(datastring);
        return SSH_ERROR;
      }
      len = ssh_string_len(datastring);
      /* Update the offset with the correct value */
      file->offset = file->offset - (size - len);
      memcpy(data, ssh_string_data(datastring), len);
      SSH_STRING_FREE(datastring);
      return len;
    default:
      ssh_set_error(sftp->session,SSH_FATAL,"Received message %d during read!",msg->packet_type);
      sftp_message_free(msg);
      sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
      return SSH_ERROR;
  }

  return SSH_ERROR;
}

ssize_t sftp_write(sftp_file file, const void *buf, size_t count) {
  sftp_session sftp;
  sftp_message msg = NULL;
  sftp_status_message status;
  ssh_buffer buffer;
  uint32_t id;
  ssize_t len;
  size_t packetlen;
  int rc;

  if (file == NULL) {
      return -1;
  }
  sftp = file->sftp;

  buffer = ssh_buffer_new();
  if (buffer == NULL) {
    ssh_set_error_oom(sftp->session);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return -1;
  }

  id = sftp_get_new_id(file->sftp);

  /*
   * limit the writes to the maximum specified in Section 3 of
   * https://datatracker.ietf.org/doc/html/draft-ietf-secsh-filexfer-02
   * or to the values provided by the limits@openssh.com extension.
   *
   * TODO: We should iterate over the blocks rather than writing less than
   * requested to provide less surprises to the calling applications.
   */
  if (count > sftp->limits->max_write_length) {
      count = sftp->limits->max_write_length;
  }

  rc = ssh_buffer_pack(buffer,
                       "dSqdP",
                       id,
                       file->handle,
                       file->offset,
                       count, /* len of datastring */
                       (size_t)count, buf);
  if (rc != SSH_OK){
    ssh_set_error_oom(sftp->session);
    SSH_BUFFER_FREE(buffer);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return -1;
  }
  len = sftp_packet_write(file->sftp, SSH_FXP_WRITE, buffer);
  packetlen=ssh_buffer_get_len(buffer);
  SSH_BUFFER_FREE(buffer);
  if (len < 0) {
    return -1;
  } else  if ((size_t)len != packetlen) {
    SSH_LOG(SSH_LOG_PACKET,
        "Could not write as much data as expected");
  }

  while (msg == NULL) {
    if (sftp_read_and_dispatch(file->sftp) < 0) {
      /* something nasty has happened */
      return -1;
    }
    msg = sftp_dequeue(file->sftp, id);
  }

  switch (msg->packet_type) {
    case SSH_FXP_STATUS:
      status = parse_status_msg(msg);
      sftp_message_free(msg);
      if (status == NULL) {
        return -1;
      }
      sftp_set_error(sftp, status->status);
      switch (status->status) {
        case SSH_FX_OK:
          file->offset += count;
          status_msg_free(status);
          return count;
        default:
          break;
      }
      ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
          "SFTP server: %s", status->errormsg);
      file->offset += count;
      status_msg_free(status);
      return -1;
    default:
      ssh_set_error(sftp->session, SSH_FATAL,
          "Received message %d during write!", msg->packet_type);
      sftp_message_free(msg);
      sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
      return -1;
  }

  return -1; /* not reached */
}

/* Seek to a specific location in a file. */
int sftp_seek(sftp_file file, uint32_t new_offset) {
  if (file == NULL) {
    return -1;
  }

  file->offset = new_offset;
  file->eof = 0;

  return 0;
}

int sftp_seek64(sftp_file file, uint64_t new_offset) {
  if (file == NULL) {
    return -1;
  }

  file->offset = new_offset;
  file->eof = 0;

  return 0;
}

/* Report current byte position in file. */
unsigned long sftp_tell(sftp_file file) {
  return (unsigned long)file->offset;
}
/* Report current byte position in file. */
uint64_t sftp_tell64(sftp_file file) {
  return (uint64_t) file->offset;
}

/* Rewinds the position of the file pointer to the beginning of the file.*/
void sftp_rewind(sftp_file file) {
  file->offset = 0;
  file->eof = 0;
}

/* code written by Nick */
int sftp_unlink(sftp_session sftp, const char *file) {
  sftp_status_message status = NULL;
  sftp_message msg = NULL;
  ssh_buffer buffer;
  uint32_t id;
  int rc;

  buffer = ssh_buffer_new();
  if (buffer == NULL) {
    ssh_set_error_oom(sftp->session);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return -1;
  }

  id = sftp_get_new_id(sftp);

  rc = ssh_buffer_pack(buffer,
                       "ds",
                       id,
                       file);
  if (rc != SSH_OK) {
    ssh_set_error_oom(sftp->session);
    SSH_BUFFER_FREE(buffer);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return -1;
  }

  if (sftp_packet_write(sftp, SSH_FXP_REMOVE, buffer) < 0) {
    SSH_BUFFER_FREE(buffer);
    return -1;
  }
  SSH_BUFFER_FREE(buffer);

  while (msg == NULL) {
    if (sftp_read_and_dispatch(sftp)) {
      return -1;
    }
    msg = sftp_dequeue(sftp, id);
  }

  if (msg->packet_type == SSH_FXP_STATUS) {
    /* by specification, this command's only supposed to return SSH_FXP_STATUS */
    status = parse_status_msg(msg);
    sftp_message_free(msg);
    if (status == NULL) {
      return -1;
    }
    sftp_set_error(sftp, status->status);
    switch (status->status) {
      case SSH_FX_OK:
        status_msg_free(status);
        return 0;
      default:
        break;
    }

    /*
     * The status should be SSH_FX_OK if the command was successful, if it
     * didn't, then there was an error
     */
    ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
        "SFTP server: %s", status->errormsg);
    status_msg_free(status);
    return -1;
  } else {
    ssh_set_error(sftp->session,SSH_FATAL,
        "Received message %d when attempting to remove file", msg->packet_type);
    sftp_message_free(msg);
    sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
  }

  return -1;
}

/* code written by Nick */
int sftp_rmdir(sftp_session sftp, const char *directory) {
  sftp_status_message status = NULL;
  sftp_message msg = NULL;
  ssh_buffer buffer;
  uint32_t id;
  int rc;

  buffer = ssh_buffer_new();
  if (buffer == NULL) {
    ssh_set_error_oom(sftp->session);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return -1;
  }

  id = sftp_get_new_id(sftp);

  rc = ssh_buffer_pack(buffer,
                       "ds",
                       id,
                       directory);
  if (rc != SSH_OK) {
    ssh_set_error_oom(sftp->session);
    SSH_BUFFER_FREE(buffer);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return -1;
  }
  if (sftp_packet_write(sftp, SSH_FXP_RMDIR, buffer) < 0) {
    SSH_BUFFER_FREE(buffer);
    return -1;
  }
  SSH_BUFFER_FREE(buffer);

  while (msg == NULL) {
    if (sftp_read_and_dispatch(sftp) < 0) {
      return -1;
    }
    msg = sftp_dequeue(sftp, id);
  }

  /* By specification, this command returns SSH_FXP_STATUS */
  if (msg->packet_type == SSH_FXP_STATUS) {
    status = parse_status_msg(msg);
    sftp_message_free(msg);
    if (status == NULL) {
      return -1;
    }
    sftp_set_error(sftp, status->status);
    switch (status->status) {
      case SSH_FX_OK:
        status_msg_free(status);
        return 0;
      default:
        break;
    }
    ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
        "SFTP server: %s", status->errormsg);
    status_msg_free(status);
    return -1;
  } else {
    ssh_set_error(sftp->session, SSH_FATAL,
        "Received message %d when attempting to remove directory",
        msg->packet_type);
    sftp_message_free(msg);
    sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
  }

  return -1;
}

/* Code written by Nick */
int sftp_mkdir(sftp_session sftp, const char *directory, mode_t mode)
{
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    sftp_attributes errno_attr = NULL;
    struct sftp_attributes_struct attr;
    ssh_buffer buffer;
    uint32_t id;
    int rc;

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    ZERO_STRUCT(attr);
    attr.permissions = mode;
    attr.flags = SSH_FILEXFER_ATTR_PERMISSIONS;

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "ds",
                         id,
                         directory);
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    rc = buffer_add_attributes(buffer, &attr);
    if (rc < 0) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_MKDIR, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return -1;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            return -1;
        }
        msg = sftp_dequeue(sftp, id);
    }

    /* By specification, this command only returns SSH_FXP_STATUS */
    if (msg->packet_type == SSH_FXP_STATUS) {
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return -1;
        }
        sftp_set_error(sftp, status->status);
        switch (status->status) {
            case SSH_FX_FAILURE:
                /*
                 * mkdir always returns a failure, even if the path already exists.
                 * To be POSIX conform and to be able to map it to EEXIST a stat
                 * call is needed here.
                 */
                errno_attr = sftp_lstat(sftp, directory);
                if (errno_attr != NULL) {
                    SAFE_FREE(errno_attr);
                    sftp_set_error(sftp, SSH_FX_FILE_ALREADY_EXISTS);
                }
                break;
            case SSH_FX_OK:
                status_msg_free(status);
                return 0;
            default:
                break;
        }
        /*
         * The status should be SSH_FX_OK if the command was successful, if it
         * didn't, then there was an error
         */
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                "SFTP server: %s", status->errormsg);
        status_msg_free(status);
        return -1;
    } else {
        ssh_set_error(sftp->session, SSH_FATAL,
                "Received message %d when attempting to make directory",
                msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return -1;
}

/* code written by nick */
int sftp_rename(sftp_session sftp, const char *original, const char *newname)
{
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    ssh_buffer buffer = NULL;
    uint32_t id;
    const char *extension_name = "posix-rename@openssh.com";
    int request_type;
    int rc;

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    id = sftp_get_new_id(sftp);

    /*
     * posix-rename@openssh.com extension will be used
     * if it is supported by sftp
     */
    if (sftp_extension_supported(sftp,
                                 extension_name,
                                 "1")) {
        rc = ssh_buffer_pack(buffer,
                             "dsss",
                             id,
                             extension_name,
                             original,
                             newname);
        if (rc != SSH_OK) {
            ssh_set_error_oom(sftp->session);
            SSH_BUFFER_FREE(buffer);
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return -1;
        }

        request_type = SSH_FXP_EXTENDED;
    } else {
        rc = ssh_buffer_pack(buffer,
                             "dss",
                             id,
                             original,
                             newname);
        if (rc != SSH_OK) {
            ssh_set_error_oom(sftp->session);
            SSH_BUFFER_FREE(buffer);
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return -1;
        }

        if (sftp->version >= 4) {
            /*
             * POSIX rename atomically replaces newpath,
             * we should do the same only available on >=v4
             */
            ssh_buffer_add_u32(buffer, SSH_FXF_RENAME_OVERWRITE);
        }

        request_type = SSH_FXP_RENAME;
    }

    rc = sftp_packet_write(sftp, request_type, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return -1;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            return -1;
        }
        msg = sftp_dequeue(sftp, id);
    }

    /* By specification, this command only returns SSH_FXP_STATUS */
    if (msg->packet_type == SSH_FXP_STATUS) {
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return -1;
        }
        sftp_set_error(sftp, status->status);
        switch (status->status) {
            case SSH_FX_OK:
                status_msg_free(status);
                return 0;
            default:
                break;
        }
        /*
         * Status should be SSH_FX_OK if the command was successful,
         * if it didn't, then there was an error
         */
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                      "SFTP server: %s", status->errormsg);
        status_msg_free(status);
        return -1;
    } else {
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Received message %d when attempting to rename",
                      msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return -1;
}

/* Code written by Nick */
/* Set file attributes on a file, directory or symbolic link. */
int sftp_setstat(sftp_session sftp, const char *file, sftp_attributes attr)
{
    uint32_t id;
    ssh_buffer buffer;
    sftp_message msg = NULL;
    sftp_status_message status = NULL;
    int rc;

    if (sftp == NULL || file == NULL || attr == NULL) {
        return -1;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "ds",
                         id,
                         file);
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    rc = buffer_add_attributes(buffer, attr);
    if (rc != 0) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_SETSTAT, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return -1;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            return -1;
        }
        msg = sftp_dequeue(sftp, id);
    }

    /* By specification, this command only returns SSH_FXP_STATUS */
    if (msg->packet_type == SSH_FXP_STATUS) {
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return -1;
        }
        sftp_set_error(sftp, status->status);
        switch (status->status) {
            case SSH_FX_OK:
                status_msg_free(status);
                return 0;
            default:
                break;
        }
        /*
         * The status should be SSH_FX_OK if the command was successful, if it
         * didn't, then there was an error
         */
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                "SFTP server: %s", status->errormsg);
        status_msg_free(status);
        return -1;
    } else {
        ssh_set_error(sftp->session, SSH_FATAL,
                "Received message %d when attempting to set stats", msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return -1;
}

int
sftp_lsetstat(sftp_session sftp, const char *file, sftp_attributes attr)
{
    uint32_t id;
    ssh_buffer buffer = NULL;
    sftp_message msg = NULL;
    sftp_status_message status = NULL;
    const char *extension_name = "lsetstat@openssh.com";
    int rc;

    if (sftp == NULL || file == NULL || attr == NULL) {
        return -1;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer, "dss", id, extension_name, file);
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    rc = buffer_add_attributes(buffer, attr);
    if (rc != 0) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_EXTENDED, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return -1;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            return -1;
        }
        msg = sftp_dequeue(sftp, id);
    }

    /* By specification, this command only returns SSH_FXP_STATUS */
    if (msg->packet_type == SSH_FXP_STATUS) {
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return -1;
        }
        sftp_set_error(sftp, status->status);
        switch (status->status) {
        case SSH_FX_OK:
            status_msg_free(status);
            return 0;
        default:
            break;
        }
        /*
         * The status should be SSH_FX_OK if the command was successful, if it
         * didn't, then there was an error
         */
        ssh_set_error(sftp->session,
                      SSH_REQUEST_DENIED,
                      "SFTP server: %s",
                      status->errormsg);
        status_msg_free(status);
        return -1;
    } else {
        ssh_set_error(sftp->session,
                      SSH_FATAL,
                      "Received message %d when attempting to lsetstat",
                      msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return -1;
}

/* Change the file owner and group */
int sftp_chown(sftp_session sftp, const char *file, uid_t owner, gid_t group) {
	struct sftp_attributes_struct attr;
  ZERO_STRUCT(attr);

  attr.uid = owner;
  attr.gid = group;

  attr.flags = SSH_FILEXFER_ATTR_UIDGID;

  return sftp_setstat(sftp, file, &attr);
}

/* Change permissions of a file */
int sftp_chmod(sftp_session sftp, const char *file, mode_t mode) {
	struct sftp_attributes_struct attr;
  ZERO_STRUCT(attr);
  attr.permissions = mode;
  attr.flags = SSH_FILEXFER_ATTR_PERMISSIONS;

  return sftp_setstat(sftp, file, &attr);
}

/* Change the last modification and access time of a file. */
int sftp_utimes(sftp_session sftp, const char *file,
    const struct timeval *times) {
	struct sftp_attributes_struct attr;
  ZERO_STRUCT(attr);

  attr.atime = times[0].tv_sec;
  attr.atime_nseconds = times[0].tv_usec;

  attr.mtime = times[1].tv_sec;
  attr.mtime_nseconds = times[1].tv_usec;

  attr.flags |= SSH_FILEXFER_ATTR_ACCESSTIME | SSH_FILEXFER_ATTR_MODIFYTIME |
    SSH_FILEXFER_ATTR_SUBSECOND_TIMES;

  return sftp_setstat(sftp, file, &attr);
}

int sftp_symlink(sftp_session sftp, const char *target, const char *dest)
{
  sftp_status_message status = NULL;
  sftp_message msg = NULL;
  ssh_buffer buffer;
  uint32_t id;
  int rc;

  if (sftp == NULL)
    return -1;
  if (target == NULL || dest == NULL) {
    ssh_set_error_invalid(sftp->session);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return -1;
  }

  buffer = ssh_buffer_new();
  if (buffer == NULL) {
    ssh_set_error_oom(sftp->session);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return -1;
  }

  id = sftp_get_new_id(sftp);

  /* The OpenSSH sftp server has order of the arguments reversed, see the
   * section "4.1 sftp: Reversal of arguments to SSH_FXP_SYMLINK' in
   * https://github.com/openssh/openssh-portable/blob/master/PROTOCOL
   * for more information */
  if (ssh_get_openssh_version(sftp->session)) {
      rc = ssh_buffer_pack(buffer,
                           "dss",
                           id,
                           target,
                           dest);
  } else {
      rc = ssh_buffer_pack(buffer,
                           "dss",
                           id,
                           dest,
                           target);
  }
  if (rc != SSH_OK){
      ssh_set_error_oom(sftp->session);
      SSH_BUFFER_FREE(buffer);
      sftp_set_error(sftp, SSH_FX_FAILURE);
      return -1;
  }

  if (sftp_packet_write(sftp, SSH_FXP_SYMLINK, buffer) < 0) {
    SSH_BUFFER_FREE(buffer);
    return -1;
  }
  SSH_BUFFER_FREE(buffer);

  while (msg == NULL) {
    if (sftp_read_and_dispatch(sftp) < 0) {
      return -1;
    }
    msg = sftp_dequeue(sftp, id);
  }

  /* By specification, this command only returns SSH_FXP_STATUS */
  if (msg->packet_type == SSH_FXP_STATUS) {
    status = parse_status_msg(msg);
    sftp_message_free(msg);
    if (status == NULL) {
      return -1;
    }
    sftp_set_error(sftp, status->status);
    switch (status->status) {
      case SSH_FX_OK:
        status_msg_free(status);
        return 0;
      default:
        break;
    }
    /*
     * The status should be SSH_FX_OK if the command was successful, if it
     * didn't, then there was an error
     */
    ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
        "SFTP server: %s", status->errormsg);
    status_msg_free(status);
    return -1;
  } else {
    ssh_set_error(sftp->session, SSH_FATAL,
        "Received message %d when attempting to set stats", msg->packet_type);
    sftp_message_free(msg);
    sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
  }

  return -1;
}

char *sftp_readlink(sftp_session sftp, const char *path)
{
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    ssh_buffer buffer;
    uint32_t id;
    int rc;

    if (sftp == NULL) {
        return NULL;
    }

    if (path == NULL) {
        ssh_set_error_invalid(sftp);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }
    if (sftp->version < 3){
        ssh_set_error(sftp,SSH_REQUEST_DENIED,"sftp version %d does not support sftp_readlink",sftp->version);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }
    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "ds",
                         id,
                         path);
    if (rc < 0) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_READLINK, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return NULL;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            return NULL;
        }
        msg = sftp_dequeue(sftp, id);
    }

    if (msg->packet_type == SSH_FXP_NAME) {
        uint32_t ignored = 0;
        char *lnk = NULL;

        rc = ssh_buffer_unpack(msg->payload,
                               "ds",
                               &ignored,
                               &lnk);
        sftp_message_free(msg);
        if (rc != SSH_OK) {
            ssh_set_error(sftp->session,
                          SSH_ERROR,
                          "Failed to retrieve link");
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return NULL;
        }

        return lnk;
    } else if (msg->packet_type == SSH_FXP_STATUS) { /* bad response (error) */
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return NULL;
        }
        sftp_set_error(sftp, status->status);
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                "SFTP server: %s", status->errormsg);
        status_msg_free(status);
    } else { /* this shouldn't happen */
        ssh_set_error(sftp->session, SSH_FATAL,
                "Received message %d when attempting to set stats", msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return NULL;
}

int sftp_hardlink(sftp_session sftp, const char *oldpath, const char *newpath)
{
    ssh_buffer buffer = NULL;
    uint32_t id;
    const char *extension_name = "hardlink@openssh.com";
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    int rc;

    if (sftp == NULL) {
        return -1;
    }

    if (oldpath == NULL || newpath == NULL) {
        ssh_set_error_invalid(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "dsss",
                         id,
                         extension_name,
                         oldpath,
                         newpath);
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_EXTENDED, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return -1;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            return -1;
        }
        msg = sftp_dequeue(sftp, id);
    }

    /* By specification, this command only returns SSH_FXP_STATUS */
    if (msg->packet_type == SSH_FXP_STATUS) {
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return -1;
        }
        sftp_set_error(sftp, status->status);
        switch (status->status) {
            case SSH_FX_OK:
                status_msg_free(status);
                return 0;
            default:
                break;
        }
        /*
         * Status should be SSH_FX_OK if the command was successful,
         * if it didn't, then there was an error
         */
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                      "SFTP server: %s", status->errormsg);
        status_msg_free(status);
        return -1;
    } else {
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Received message %d when attempting to create hardlink",
                      msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return -1;
}

static sftp_statvfs_t sftp_parse_statvfs(sftp_session sftp, ssh_buffer buf) {
  sftp_statvfs_t  statvfs;
  int rc;

  statvfs = calloc(1, sizeof(struct sftp_statvfs_struct));
  if (statvfs == NULL) {
    ssh_set_error_oom(sftp->session);
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return NULL;
  }

  rc = ssh_buffer_unpack(buf, "qqqqqqqqqqq",
          &statvfs->f_bsize,  /* file system block size */
          &statvfs->f_frsize, /* fundamental fs block size */
          &statvfs->f_blocks, /* number of blocks (unit f_frsize) */
          &statvfs->f_bfree,  /* free blocks in file system */
          &statvfs->f_bavail, /* free blocks for non-root */
          &statvfs->f_files,  /* total file inodes */
          &statvfs->f_ffree,  /* free file inodes */
          &statvfs->f_favail, /* free file inodes for to non-root */
          &statvfs->f_fsid,   /* file system id */
          &statvfs->f_flag,   /* bit mask of f_flag values */
          &statvfs->f_namemax/* maximum filename length */
          );
  if (rc != SSH_OK) {
    SAFE_FREE(statvfs);
    ssh_set_error(sftp->session, SSH_FATAL, "Invalid statvfs structure");
    sftp_set_error(sftp, SSH_FX_FAILURE);
    return NULL;
  }

  return statvfs;
}

sftp_statvfs_t sftp_statvfs(sftp_session sftp, const char *path)
{
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    ssh_buffer buffer;
    uint32_t id;
    int rc;

    if (sftp == NULL)
        return NULL;
    if (path == NULL) {
        ssh_set_error_invalid(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }
    if (sftp->version < 3){
        ssh_set_error(sftp,SSH_REQUEST_DENIED,"sftp version %d does not support sftp_statvfs",sftp->version);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "dss",
                         id,
                         "statvfs@openssh.com",
                         path);
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_EXTENDED, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return NULL;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            return NULL;
        }
        msg = sftp_dequeue(sftp, id);
    }

    if (msg->packet_type == SSH_FXP_EXTENDED_REPLY) {
        sftp_statvfs_t  buf = sftp_parse_statvfs(sftp, msg->payload);
        sftp_message_free(msg);
        if (buf == NULL) {
            return NULL;
        }

        return buf;
    } else if (msg->packet_type == SSH_FXP_STATUS) { /* bad response (error) */
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return NULL;
        }
        sftp_set_error(sftp, status->status);
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                "SFTP server: %s", status->errormsg);
        status_msg_free(status);
    } else { /* this shouldn't happen */
        ssh_set_error(sftp->session, SSH_FATAL,
                "Received message %d when attempting to get statvfs", msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return NULL;
}

int sftp_fsync(sftp_file file)
{
    sftp_session sftp;
    sftp_message msg = NULL;
    ssh_buffer buffer;
    uint32_t id;
    int rc;

    if (file == NULL) {
        return -1;
    }
    sftp = file->sftp;

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return -1;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "dsS",
                         id,
                         "fsync@openssh.com",
                         file->handle);
    if (rc < 0) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        goto done;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_EXTENDED, buffer);
    if (rc < 0) {
        ssh_set_error_oom(sftp->session);
        goto done;
    }

    do {
        rc = sftp_read_and_dispatch(sftp);
        if (rc < 0) {
            ssh_set_error_oom(sftp->session);
            rc = -1;
            goto done;
        }
        msg = sftp_dequeue(sftp, id);
    } while (msg == NULL);

    /* By specification, this command only returns SSH_FXP_STATUS */
    if (msg->packet_type == SSH_FXP_STATUS) {
        sftp_status_message status;

        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            rc = -1;
            goto done;
        }

        sftp_set_error(sftp, status->status);
        switch (status->status) {
            case SSH_FX_OK:
                /* SUCCESS, LEAVE */
                status_msg_free(status);
                rc = 0;
                goto done;
            default:
                break;
        }

        /*
         * The status should be SSH_FX_OK if the command was successful, if it
         * didn't, then there was an error
         */
        ssh_set_error(sftp->session,
                      SSH_REQUEST_DENIED,
                      "SFTP server: %s",
                      status->errormsg);
        status_msg_free(status);

        rc = -1;
        goto done;
    } else {
        ssh_set_error(sftp->session,
                      SSH_FATAL,
                      "Received message %d when attempting to set stats",
                      msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    rc = -1;
done:
    SSH_BUFFER_FREE(buffer);

    return rc;
}

sftp_statvfs_t sftp_fstatvfs(sftp_file file)
{
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    sftp_session sftp;
    ssh_buffer buffer;
    uint32_t id;
    int rc;

    if (file == NULL) {
        return NULL;
    }
    sftp = file->sftp;

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "dsS",
                         id,
                         "fstatvfs@openssh.com",
                         file->handle);
    if (rc < 0) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_EXTENDED, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return NULL;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            return NULL;
        }
        msg = sftp_dequeue(sftp, id);
    }

    if (msg->packet_type == SSH_FXP_EXTENDED_REPLY) {
        sftp_statvfs_t buf = sftp_parse_statvfs(sftp, msg->payload);
        sftp_message_free(msg);
        if (buf == NULL) {
            return NULL;
        }

        return buf;
    } else if (msg->packet_type == SSH_FXP_STATUS) { /* bad response (error) */
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return NULL;
        }
        sftp_set_error(sftp, status->status);
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                "SFTP server: %s", status->errormsg);
        status_msg_free(status);
    } else { /* this shouldn't happen */
        ssh_set_error(sftp->session, SSH_FATAL,
                "Received message %d when attempting to set stats", msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return NULL;
}

void sftp_statvfs_free(sftp_statvfs_t statvfs) {
    if (statvfs == NULL) {
        return;
    }

    SAFE_FREE(statvfs);
}

static sftp_limits_t sftp_limits_new(void)
{
    return calloc(1, sizeof(struct sftp_limits_struct));
}

static sftp_limits_t sftp_parse_limits(sftp_session sftp, ssh_buffer buf)
{
    sftp_limits_t limits = NULL;
    int rc;

    limits = sftp_limits_new();
    if (limits == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = ssh_buffer_unpack(buf, "qqqq",
            &limits->max_packet_length,  /** maximum number of bytes in a single sftp packet */
            &limits->max_read_length,    /** maximum length in a SSH_FXP_READ packet */
            &limits->max_write_length,   /** maximum length in a SSH_FXP_WRITE packet */
            &limits->max_open_handles    /** maximum number of active handles allowed by server */
            );
    if (rc != SSH_OK) {
        SAFE_FREE(limits);
        ssh_set_error(sftp->session, SSH_FATAL, "Invalid limits structure");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    return limits;
}

static sftp_limits_t sftp_limits_use_extension(sftp_session sftp)
{
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    ssh_buffer buffer;
    uint32_t id;
    int rc;

    if (sftp == NULL)
        return NULL;

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "ds",
                         id,
                         "limits@openssh.com");
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_EXTENDED, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return NULL;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            return NULL;
        }
        msg = sftp_dequeue(sftp, id);
    }

    if (msg->packet_type == SSH_FXP_EXTENDED_REPLY) {
        sftp_limits_t limits = sftp_parse_limits(sftp, msg->payload);
        sftp_message_free(msg);
        if (limits == NULL) {
            return NULL;
        }

        return limits;
    } else if (msg->packet_type == SSH_FXP_STATUS) { /* bad response (error) */
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return NULL;
        }
        sftp_set_error(sftp, status->status);
        ssh_set_error(sftp->session,
                      SSH_REQUEST_DENIED,
                      "SFTP server: %s",
                      status->errormsg);
        status_msg_free(status);
    } else { /* this shouldn't happen */
        ssh_set_error(sftp->session,
                      SSH_FATAL,
                      "Received message %d when attempting to get limits",
                      msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return NULL;
}

static sftp_limits_t sftp_limits_use_default(sftp_session sftp)
{
    sftp_limits_t limits = NULL;

    if (sftp == NULL) {
        return NULL;
    }

    limits = sftp_limits_new();
    if (limits == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    limits->max_packet_length = 34000;
    limits->max_read_length = 32768;
    limits->max_write_length = 32768;

    /*
     * For max-open-handles field openssh says :
     * If the server doesn't enforce a specific limit, then the field may
     * be set to 0.  This implies the server relies on the OS to enforce
     * limits (e.g. available memory or file handles), and such limits
     * might be dynamic.  The client SHOULD take care to not try to exceed
     * reasonable limits.
     */
    limits->max_open_handles = 0;

    return limits;
}

sftp_limits_t sftp_limits(sftp_session sftp)
{
    sftp_limits_t limits = NULL;

    if (sftp == NULL) {
        return NULL;
    }

    if (sftp->limits == NULL) {
        ssh_set_error(sftp, SSH_FATAL,
                      "Uninitialized sftp session, "
                      "sftp_init() was not called or failed");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    limits = sftp_limits_new();
    if (limits == NULL) {
        ssh_set_error_oom(sftp);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    memcpy(limits, sftp->limits, sizeof(struct sftp_limits_struct));
    return limits;
}

void sftp_limits_free(sftp_limits_t limits)
{
    if (limits == NULL) {
        return;
    }

    SAFE_FREE(limits);
}

/* another code written by Nick */
char *sftp_canonicalize_path(sftp_session sftp, const char *path)
{
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    ssh_buffer buffer;
    uint32_t id;
    int rc;

    if (sftp == NULL)
        return NULL;
    if (path == NULL) {
        ssh_set_error_invalid(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "ds",
                         id,
                         path);
    if (rc < 0) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_REALPATH, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return NULL;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            return NULL;
        }
        msg = sftp_dequeue(sftp, id);
    }

    if (msg->packet_type == SSH_FXP_NAME) {
        uint32_t ignored = 0;
        char *cname = NULL;

        rc = ssh_buffer_unpack(msg->payload,
                               "ds",
                               &ignored,
                               &cname);
        sftp_message_free(msg);
        if (rc != SSH_OK) {
            ssh_set_error(sftp->session,
                          SSH_ERROR,
                          "Failed to parse canonicalized path");
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return NULL;
        }

        return cname;
    } else if (msg->packet_type == SSH_FXP_STATUS) { /* bad response (error) */
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return NULL;
        }
        sftp_set_error(sftp, status->status);
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                "SFTP server: %s", status->errormsg);
        status_msg_free(status);
    } else { /* this shouldn't happen */
        ssh_set_error(sftp->session, SSH_FATAL,
                "Received message %d when attempting to set stats", msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return NULL;
}

static sftp_attributes sftp_xstat(sftp_session sftp,
                                  const char *path,
                                  int param)
{
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    ssh_buffer buffer;
    uint32_t id;
    int rc;

    if (sftp == NULL) {
        return NULL;
    }

    if (path == NULL) {
        ssh_set_error_invalid(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "ds",
                         id,
                         path);
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = sftp_packet_write(sftp, param, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return NULL;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(sftp) < 0) {
            return NULL;
        }
        msg = sftp_dequeue(sftp, id);
    }

    if (msg->packet_type == SSH_FXP_ATTRS) {
        sftp_attributes attr = sftp_parse_attr(sftp, msg->payload, 0);
        sftp_message_free(msg);

        return attr;
    } else if (msg->packet_type == SSH_FXP_STATUS) {
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return NULL;
        }
        sftp_set_error(sftp, status->status);
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                "SFTP server: %s", status->errormsg);
        status_msg_free(status);
        return NULL;
    }
    ssh_set_error(sftp->session, SSH_FATAL,
            "Received mesg %d during stat()", msg->packet_type);
    sftp_message_free(msg);
    sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);

    return NULL;
}

sftp_attributes sftp_stat(sftp_session session, const char *path) {
  return sftp_xstat(session, path, SSH_FXP_STAT);
}

sftp_attributes sftp_lstat(sftp_session session, const char *path) {
  return sftp_xstat(session, path, SSH_FXP_LSTAT);
}

sftp_attributes sftp_fstat(sftp_file file)
{
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    ssh_buffer buffer;
    uint32_t id;
    int rc;

    if (file == NULL) {
        return NULL;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(file->sftp->session);
        sftp_set_error(file->sftp, SSH_FX_FAILURE);
        return NULL;
    }

    id = sftp_get_new_id(file->sftp);

    rc = ssh_buffer_pack(buffer,
                         "dS",
                         id,
                         file->handle);
    if (rc != SSH_OK) {
        ssh_set_error_oom(file->sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(file->sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = sftp_packet_write(file->sftp, SSH_FXP_FSTAT, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return NULL;
    }

    while (msg == NULL) {
        if (sftp_read_and_dispatch(file->sftp) < 0) {
            return NULL;
        }
        msg = sftp_dequeue(file->sftp, id);
    }

    if (msg->packet_type == SSH_FXP_ATTRS){
        sftp_attributes attr = sftp_parse_attr(file->sftp, msg->payload, 0);
        sftp_message_free(msg);

        return attr;
    } else if (msg->packet_type == SSH_FXP_STATUS) {
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return NULL;
        }
        sftp_set_error(file->sftp, status->status);
        ssh_set_error(file->sftp->session, SSH_REQUEST_DENIED,
                "SFTP server: %s", status->errormsg);
        status_msg_free(status);

        return NULL;
    }
    ssh_set_error(file->sftp->session, SSH_FATAL,
            "Received msg %d during fstat()", msg->packet_type);
    sftp_message_free(msg);
    sftp_set_error(file->sftp, SSH_FX_BAD_MESSAGE);

    return NULL;
}

char *sftp_expand_path(sftp_session sftp, const char *path)
{
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    ssh_buffer buffer = NULL;
    uint32_t id;
    int rc;

    if (sftp == NULL) {
        return NULL;
    }

    if (path == NULL) {
        ssh_set_error(sftp->session,
                      SSH_FATAL,
                      "NULL received as an argument instead of the path to expand");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "dss",
                         id,
                         "expand-path@openssh.com",
                         path);
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_EXTENDED, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return NULL;
    }

    while (msg == NULL) {
        rc = sftp_read_and_dispatch(sftp);
        if (rc < 0) {
            return NULL;
        }
        msg = sftp_dequeue(sftp, id);
    }

    if (msg->packet_type == SSH_FXP_NAME) {
        uint32_t ignored = 0;
        char *cname = NULL;

        rc = ssh_buffer_unpack(msg->payload,
                               "ds",
                               &ignored,
                               &cname);
        sftp_message_free(msg);
        if (rc != SSH_OK) {
            ssh_set_error(sftp->session,
                          SSH_ERROR,
                          "Failed to parse expanded path");
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return NULL;
        }

        return cname;
    } else if (msg->packet_type == SSH_FXP_STATUS) {
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return NULL;
        }
        sftp_set_error(sftp, status->status);
        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                      "SFTP server: %s", status->errormsg);
        status_msg_free(status);
    } else {
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Received message %d when attempting to expand path",
                      msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return NULL;
}

char *
sftp_home_directory(sftp_session sftp, const char *username)
{
    sftp_status_message status = NULL;
    sftp_message msg = NULL;
    ssh_buffer buffer = NULL;
    uint32_t id;
    int rc;

    if (sftp == NULL) {
        return NULL;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    id = sftp_get_new_id(sftp);

    rc = ssh_buffer_pack(buffer,
                         "dss",
                         id,
                         "home-directory",
                         username ? username : "");
    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        SSH_BUFFER_FREE(buffer);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return NULL;
    }

    rc = sftp_packet_write(sftp, SSH_FXP_EXTENDED, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc < 0) {
        return NULL;
    }

    while (msg == NULL) {
        rc = sftp_read_and_dispatch(sftp);
        if (rc < 0) {
            return NULL;
        }
        msg = sftp_dequeue(sftp, id);
    }

    if (msg->packet_type == SSH_FXP_NAME) {
        uint32_t count = 0;
        char *homepath = NULL;
        char *longpath = NULL;
        sftp_attributes attr = NULL;

        rc = ssh_buffer_unpack(msg->payload, "ds", &count, &homepath);
        if (rc != SSH_OK) {
            ssh_set_error(sftp->session,
                          SSH_ERROR,
                          "Failed to query user home directory");
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return NULL;
        }
        /*
          for SFTP version > 3, longname field in SSH_FXP_NAME is omitted.
        */
        if (sftp->version <= 3) {
            rc = ssh_buffer_unpack(msg->payload, "s", &longpath);
            if (rc != SSH_OK) {
                ssh_set_error(sftp->session,
                              SSH_ERROR,
                              "Failed to extract longname from payload");
                sftp_set_error(sftp, SSH_FX_FAILURE);
                return NULL;
            }
        }
        attr = sftp_parse_attr(sftp, msg->payload, 0);
        if (attr == NULL) {
            ssh_set_error(sftp->session,
                          SSH_FATAL,
                          "Couldn't parse the SFTP attributes");
            return NULL;
        }
        sftp_message_free(msg);

        if (count != 1) {
            if (count > 1) {
                ssh_set_error(sftp->session,
                              SSH_ERROR,
                              "Multiple results returned");
            } else {
                ssh_set_error(sftp->session, SSH_ERROR, "No result returned");
            }
            sftp_set_error(sftp, SSH_FX_FAILURE);
            return NULL;
        }

        if (longpath) {
            free(longpath);
        }
        sftp_attributes_free(attr);
        return homepath;
    } else if (msg->packet_type == SSH_FXP_STATUS) {
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return NULL;
        }

        sftp_set_error(sftp, status->status);
        ssh_set_error(sftp->session,
                      SSH_REQUEST_DENIED,
                      "SFTP server: %s",
                      status->errormsg);
        status_msg_free(status);
    } else {
        ssh_set_error(
            sftp->session,
            SSH_FATAL,
            "Received message %d when attempting to query user home directory",
            msg->packet_type);
        sftp_message_free(msg);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    }

    return NULL;
}

#endif /* WITH_SFTP */
