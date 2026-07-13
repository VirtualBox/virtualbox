/*
 * sftp_aio.c - Secure FTP functions for asynchronous i/o
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2005-2008 by Aris Adamantiadis
 * Copyright (c) 2008-2018 by Andreas Schneider <asn@cryptomilk.org>
 * Copyright (c) 2023 by Eshan Kelkar <eshankelkar@galorithm.com>
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

#include "config.h"

#include "libssh/sftp.h"
#include "libssh/sftp_priv.h"
#include "libssh/buffer.h"
#include "libssh/session.h"

#ifdef WITH_SFTP

struct sftp_aio_struct {
    sftp_file file;
    uint32_t id;
    size_t len;
};

static sftp_aio sftp_aio_new(void)
{
    sftp_aio aio = NULL;
    aio = calloc(1, sizeof(struct sftp_aio_struct));
    return aio;
}

void sftp_aio_free(sftp_aio aio)
{
    SAFE_FREE(aio);
}

ssize_t sftp_aio_begin_read(sftp_file file, size_t len, sftp_aio *aio)
{
    sftp_session sftp = NULL;
    ssh_buffer buffer = NULL;
    sftp_aio aio_handle = NULL;
    uint32_t id;
    int rc;

    if (file == NULL ||
        file->sftp == NULL ||
        file->sftp->session == NULL) {
        return SSH_ERROR;
    }

    sftp = file->sftp;
    if (len == 0) {
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Invalid argument, 0 passed as the number of "
                      "bytes to read");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return SSH_ERROR;
    }

    /* Apply a cap on the length a user is allowed to read */
    if (len > sftp->limits->max_read_length) {
        len = sftp->limits->max_read_length;
    }

    if (aio == NULL) {
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Invalid argument, NULL passed instead of a pointer to "
                      "a location to store an sftp aio handle");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return SSH_ERROR;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return SSH_ERROR;
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
        sftp_set_error(sftp, SSH_FX_FAILURE);
        SSH_BUFFER_FREE(buffer);
        return SSH_ERROR;
    }

    aio_handle = sftp_aio_new();
    if (aio_handle == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        SSH_BUFFER_FREE(buffer);
        return SSH_ERROR;
    }

    aio_handle->file = file;
    aio_handle->id = id;
    aio_handle->len = len;

    rc = sftp_packet_write(sftp, SSH_FXP_READ, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc == SSH_ERROR) {
        SFTP_AIO_FREE(aio_handle);
        return SSH_ERROR;
    }

    /* Assume we read len bytes from the file */
    file->offset += len;
    *aio = aio_handle;
    return len;
}

ssize_t sftp_aio_wait_read(sftp_aio *aio,
                           void *buf,
                           size_t buf_size)
{
    sftp_file file = NULL;
    size_t bytes_requested;
    sftp_session sftp = NULL;
    sftp_message msg = NULL;
    sftp_status_message status = NULL;
    uint32_t string_len, host_len;
    int rc, err;

    /*
     * This function releases the memory of the structure
     * that (*aio) points to in all cases except when the
     * return value is SSH_AGAIN.
     *
     * If the return value is SSH_AGAIN, the user should call this
     * function again to get the response for the request corresponding
     * to the structure that (*aio) points to, hence we don't release the
     * structure's memory when SSH_AGAIN is returned.
     */

    if (aio == NULL || *aio == NULL) {
        return SSH_ERROR;
    }

    file = (*aio)->file;
    bytes_requested = (*aio)->len;

    if (file == NULL ||
        file->sftp == NULL ||
        file->sftp->session == NULL) {
        SFTP_AIO_FREE(*aio);
        return SSH_ERROR;
    }

    sftp = file->sftp;
    if (bytes_requested == 0) {
        /* should never happen */
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Invalid sftp aio, len for requested i/o is 0");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        SFTP_AIO_FREE(*aio);
        return SSH_ERROR;
    }

    if (buf == NULL) {
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Invalid argument, NULL passed "
                      "instead of a buffer's address");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        SFTP_AIO_FREE(*aio);
        return SSH_ERROR;
    }

    if (buf_size < bytes_requested) {
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Buffer size (%zu, passed by the caller) is "
                      "smaller than the number of bytes requested "
                      "to read (%zu, as per the supplied sftp aio)",
                      buf_size, bytes_requested);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        SFTP_AIO_FREE(*aio);
        return SSH_ERROR;
    }

    /* handle an existing request */
    while (msg == NULL) {
        if (file->nonblocking) {
            if (ssh_channel_poll(sftp->channel, 0) == 0) {
                /* we cannot block */
                return SSH_AGAIN;
            }
        }

        if (sftp_read_and_dispatch(sftp) < 0) {
            /* something nasty has happened */
            SFTP_AIO_FREE(*aio);
            return SSH_ERROR;
        }

        msg = sftp_dequeue(sftp, (*aio)->id);
    }

    /*
     * Release memory for the structure that (*aio) points to
     * as all further points of return are for success or
     * failure.
     */
    SFTP_AIO_FREE(*aio);

    switch (msg->packet_type) {
    case SSH_FXP_STATUS:
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return SSH_ERROR;
        }

        sftp_set_error(sftp, status->status);
        if (status->status != SSH_FX_EOF) {
            ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                          "SFTP server : %s", status->errormsg);
            err = SSH_ERROR;
        } else {
            file->eof = 1;
            /* Update the offset correctly */
            file->offset = file->offset - bytes_requested;
            err = SSH_OK;
        }

        status_msg_free(status);
        return err;

    case SSH_FXP_DATA:
        rc = ssh_buffer_get_u32(msg->payload, &string_len);
        if (rc == 0) {
            /* Insufficient data in the buffer */
            ssh_set_error(sftp->session, SSH_FATAL,
                          "Received invalid DATA packet from sftp server");
            sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
            sftp_message_free(msg);
            return SSH_ERROR;
        }

        host_len = ntohl(string_len);
        if (host_len > buf_size) {
            /*
             * This should never happen, as according to the
             * SFTP protocol the server reads bytes less than
             * or equal to the number of bytes requested to read.
             *
             * And we have checked before that the buffer size is
             * greater than or equal to the number of bytes requested
             * to read, hence code of this if block should never
             * get executed.
             */
            ssh_set_error(sftp->session, SSH_FATAL,
                          "DATA packet (%u bytes) received from sftp server "
                          "cannot fit into the supplied buffer (%zu bytes)",
                          host_len, buf_size);
            sftp_set_error(sftp, SSH_FX_FAILURE);
            sftp_message_free(msg);
            return SSH_ERROR;
        }

        string_len = ssh_buffer_get_data(msg->payload, buf, host_len);
        if (string_len != host_len) {
            /* should never happen */
            ssh_set_error(sftp->session, SSH_FATAL,
                          "Received invalid DATA packet from sftp server");
            sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
            sftp_message_free(msg);
            return SSH_ERROR;
        }

        /* Update the offset with the correct value */
        file->offset = file->offset - (bytes_requested - string_len);
        sftp_message_free(msg);
        return string_len;

    default:
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Received message %d during read!", msg->packet_type);
        sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
        sftp_message_free(msg);
        return SSH_ERROR;
    }

    return SSH_ERROR; /* not reached */
}

ssize_t sftp_aio_begin_write(sftp_file file,
                             const void *buf,
                             size_t len,
                             sftp_aio *aio)
{
    sftp_session sftp = NULL;
    ssh_buffer buffer = NULL;
    sftp_aio aio_handle = NULL;
    uint32_t id;
    int rc;

    if (file == NULL ||
        file->sftp == NULL ||
        file->sftp->session == NULL) {
        return SSH_ERROR;
    }

    sftp = file->sftp;
    if (buf == NULL) {
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Invalid argument, NULL passed instead "
                      "of a buffer's address");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return SSH_ERROR;
    }

    if (len == 0) {
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Invalid argument, 0 passed as the number "
                      "of bytes to write");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return SSH_ERROR;
    }

    /* Apply a cap on the length a user is allowed to write */
    if (len > sftp->limits->max_write_length) {
        len = sftp->limits->max_write_length;
    }

    if (aio == NULL) {
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Invalid argument, NULL passed instead of a pointer to "
                      "a location to store an sftp aio handle");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return SSH_ERROR;
    }

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        return SSH_ERROR;
    }

    id = sftp_get_new_id(sftp);
    rc = ssh_buffer_pack(buffer,
                         "dSqdP",
                         id,
                         file->handle,
                         file->offset,
                         len, /* len of datastring */
                         len, buf);

    if (rc != SSH_OK) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        SSH_BUFFER_FREE(buffer);
        return SSH_ERROR;
    }

    aio_handle = sftp_aio_new();
    if (aio_handle == NULL) {
        ssh_set_error_oom(sftp->session);
        sftp_set_error(sftp, SSH_FX_FAILURE);
        SSH_BUFFER_FREE(buffer);
        return SSH_ERROR;
    }

    aio_handle->file = file;
    aio_handle->id = id;
    aio_handle->len = len;

    rc = sftp_packet_write(sftp, SSH_FXP_WRITE, buffer);
    SSH_BUFFER_FREE(buffer);
    if (rc == SSH_ERROR) {
        SFTP_AIO_FREE(aio_handle);
        return SSH_ERROR;
    }

    /* Assume we wrote len bytes to the file */
    file->offset += len;
    *aio = aio_handle;
    return len;
}

ssize_t sftp_aio_wait_write(sftp_aio *aio)
{
    sftp_file file = NULL;
    size_t bytes_requested;

    sftp_session sftp = NULL;
    sftp_message msg = NULL;
    sftp_status_message status = NULL;

    /*
     * This function releases the memory of the structure
     * that (*aio) points to in all cases except when the
     * return value is SSH_AGAIN.
     *
     * If the return value is SSH_AGAIN, the user should call this
     * function again to get the response for the request corresponding
     * to the structure that (*aio) points to, hence we don't release the
     * structure's memory when SSH_AGAIN is returned.
     */

    if (aio == NULL || *aio == NULL) {
        return SSH_ERROR;
    }

    file = (*aio)->file;
    bytes_requested = (*aio)->len;

    if (file == NULL ||
        file->sftp == NULL ||
        file->sftp->session == NULL) {
        SFTP_AIO_FREE(*aio);
        return SSH_ERROR;
    }

    sftp = file->sftp;
    if (bytes_requested == 0) {
        /* This should never happen */
        ssh_set_error(sftp->session, SSH_FATAL,
                      "Invalid sftp aio, len for requested i/o is 0");
        sftp_set_error(sftp, SSH_FX_FAILURE);
        SFTP_AIO_FREE(*aio);
        return SSH_ERROR;
    }

    while (msg == NULL) {
        if (file->nonblocking) {
            if (ssh_channel_poll(sftp->channel, 0) == 0) {
                /* we cannot block */
                return SSH_AGAIN;
            }
        }

        if (sftp_read_and_dispatch(sftp) < 0) {
            /* something nasty has happened */
            SFTP_AIO_FREE(*aio);
            return SSH_ERROR;
        }

        msg = sftp_dequeue(sftp, (*aio)->id);
    }

    /*
     * Release memory for the structure that (*aio) points to
     * as all further points of return are for success or
     * failure.
     */
    SFTP_AIO_FREE(*aio);

    if (msg->packet_type == SSH_FXP_STATUS) {
        status = parse_status_msg(msg);
        sftp_message_free(msg);
        if (status == NULL) {
            return SSH_ERROR;
        }

        sftp_set_error(sftp, status->status);
        if (status->status == SSH_FX_OK) {
            status_msg_free(status);
            return bytes_requested;
        }

        ssh_set_error(sftp->session, SSH_REQUEST_DENIED,
                      "SFTP server: %s", status->errormsg);
        status_msg_free(status);
        return SSH_ERROR;
    }

    ssh_set_error(sftp->session, SSH_FATAL,
                  "Received message %d during write!",
                  msg->packet_type);
    sftp_message_free(msg);
    sftp_set_error(sftp, SSH_FX_BAD_MESSAGE);
    return SSH_ERROR;
}

#endif /* WITH_SFTP */
