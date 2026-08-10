/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2015 by Aris Adamantiadis <aris@badcode.be>
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

#include "libssh/priv.h"
#include "libssh/poll.h"
#include "libssh/callbacks.h"
#include "libssh/session.h"
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>

#ifndef CHUNKSIZE
#define CHUNKSIZE 4096
#endif

#ifndef _WIN32
# include <sys/types.h>
# include <sys/socket.h>
#endif

struct ssh_connector_struct {
    ssh_session session;

    ssh_channel in_channel;
    ssh_channel out_channel;

    socket_t in_fd;
    socket_t out_fd;

    bool fd_is_socket;

    ssh_poll_handle in_poll;
    ssh_poll_handle out_poll;

    ssh_event event;

    int in_available;
    int out_wontblock;

    struct ssh_channel_callbacks_struct in_channel_cb;
    struct ssh_channel_callbacks_struct out_channel_cb;

    enum ssh_connector_flags_e in_flags;
    enum ssh_connector_flags_e out_flags;
};

static int ssh_connector_channel_data_cb(ssh_session session,
                                         ssh_channel channel,
                                         void *data,
                                         uint32_t len,
                                         int is_stderr,
                                         void *userdata);
static int ssh_connector_channel_write_wontblock_cb(ssh_session session,
                                                    ssh_channel channel,
                                                    uint32_t bytes,
                                                    void *userdata);
static ssize_t ssh_connector_fd_read(ssh_connector connector,
                                     void *buffer,
                                     uint32_t len);
static ssize_t ssh_connector_fd_write(ssh_connector connector,
                                      const void *buffer,
                                      uint32_t len);
static bool ssh_connector_fd_is_socket(socket_t socket);

/**
 * @brief Create a new SSH connector.
 *
 * Allocates and initializes a new connector object for moving data between
 * an SSH session and file descriptors. The connector is created with invalid
 * file descriptors and callback structures initialized, but not yet attached
 * to any channels or sockets.
 *
 * @param[in]  session   The SSH session to associate with the connector.
 *
 * @return               A newly allocated connector on success, or NULL if an
 *                       error occurred. On error, an out-of-memory error is
 *                       set on the session.
 */
ssh_connector ssh_connector_new(ssh_session session)
{
    ssh_connector connector;

    connector = calloc(1, sizeof(struct ssh_connector_struct));
    if (connector == NULL){
        ssh_set_error_oom(session);
        return NULL;
    }

    connector->session = session;
    connector->in_fd = SSH_INVALID_SOCKET;
    connector->out_fd = SSH_INVALID_SOCKET;

    connector->fd_is_socket = false;

    ssh_callbacks_init(&connector->in_channel_cb);
    ssh_callbacks_init(&connector->out_channel_cb);

    connector->in_channel_cb.userdata = connector;
    connector->in_channel_cb.channel_data_function = ssh_connector_channel_data_cb;

    connector->out_channel_cb.userdata = connector;
    connector->out_channel_cb.channel_write_wontblock_function =
            ssh_connector_channel_write_wontblock_cb;

    return connector;
}

/**
 * @brief Free an SSH connector.
 *
 * Cleans up and deallocates a connector created by ssh_connector_new().
 * Any channel callbacks and poll objects associated with the @p connector
 * are removed and freed before the connector structure itself is released.
 *
 * @param[in] connector  The connector to free.
 */
void ssh_connector_free (ssh_connector connector)
{
    if (connector == NULL) {
        return;
    }
    if (connector->in_channel != NULL) {
        ssh_remove_channel_callbacks(connector->in_channel,
                                     &connector->in_channel_cb);
    }
    if (connector->out_channel != NULL) {
        ssh_remove_channel_callbacks(connector->out_channel,
                                     &connector->out_channel_cb);
    }

    if (connector->event != NULL){
        ssh_connector_remove_event(connector);
    }

    if (connector->in_poll != NULL) {
        ssh_poll_free(connector->in_poll);
        connector->in_poll = NULL;
    }

    if (connector->out_poll != NULL) {
        ssh_poll_free(connector->out_poll);
        connector->out_poll = NULL;
    }

    free(connector);
}

/**
 * @brief Set the input channel for a connector.
 *
 * Associates an SSH channel with the @p connector as its input source and
 * installs the internal channel callbacks used for reading data. Any
 * configured input file descriptor is disabled and the connector will
 * receive data from the given channel only.
 *
 * If neither `SSH_CONNECTOR_STDOUT` nor `SSH_CONNECTOR_STDERR` is specified
 * in @p flags, `SSH_CONNECTOR_STDOUT` is used as the default.
 *
 * @param[in] connector  The connector to configure.
 * @param[in] channel    The SSH channel to use as input.
 * @param[in] flags      A combination of ssh_connector_flags_e values
 *                       selecting which channel streams to read from.
 *
 * @return               `SSH_OK` on success, `SSH_ERROR` on failure.
 */
int ssh_connector_set_in_channel(ssh_connector connector,
                                  ssh_channel channel,
                                  enum ssh_connector_flags_e flags)
{
    connector->in_channel = channel;
    connector->in_fd = SSH_INVALID_SOCKET;
    connector->in_flags = flags;

    /* Fallback to default value for invalid flags */
    if (!(flags & SSH_CONNECTOR_STDOUT) && !(flags & SSH_CONNECTOR_STDERR)) {
        connector->in_flags = SSH_CONNECTOR_STDOUT;
    }

    return ssh_add_channel_callbacks(channel, &connector->in_channel_cb);
}

/**
 * @brief Set the output channel for a connector.
 *
 * Associates an SSH channel with the @p connector as its output target and
 * installs the internal channel callbacks used for writing data. Any
 * configured output file descriptor is disabled and the connector will
 * send data to the given channel only.
 *
 * If neither `SSH_CONNECTOR_STDOUT` nor `SSH_CONNECTOR_STDERR` is specified
 * in @p flags, `SSH_CONNECTOR_STDOUT` is used as the default.
 *
 * @param[in] connector  The connector to configure.
 * @param[in] channel    The SSH channel to use as output.
 * @param[in] flags      A combination of ssh_connector_flags_e values
 *                       selecting which channel streams to write to.
 *
 * @return               `SSH_OK` on success, `SSH_ERROR` on failure.
 */
int ssh_connector_set_out_channel(ssh_connector connector,
                                  ssh_channel channel,
                                  enum ssh_connector_flags_e flags)
{
    connector->out_channel = channel;
    connector->out_fd = SSH_INVALID_SOCKET;
    connector->out_flags = flags;

    /* Fallback to default value for invalid flags */
    if (!(flags & SSH_CONNECTOR_STDOUT) && !(flags & SSH_CONNECTOR_STDERR)) {
        connector->out_flags = SSH_CONNECTOR_STDOUT;
    }

    return ssh_add_channel_callbacks(channel, &connector->out_channel_cb);
}

/**
 * @brief Set the connector's input file descriptor.
 *
 * Sets the @p fd (file descriptor) to be used as the input source for the
 * @p connector , replacing any previously configured input channel.
 *
 * @param[in] connector  The connector to configure.
 * @param[in] fd         The file descriptor (socket or regular).
 */
void ssh_connector_set_in_fd(ssh_connector connector, socket_t fd)
{
    connector->in_fd = fd;
    connector->fd_is_socket = ssh_connector_fd_is_socket(fd);
    connector->in_channel = NULL;
}

/**
 * @brief Set the connector's output file descriptor.
 *
 * Sets the @p fd (file descriptor) to be used as the output target for the
 * @p connector , replacing any previously configured output channel.
 *
 * @param[in] connector  The connector to configure.
 * @param[in] fd         The file descriptor (socket or regular).
 */
void ssh_connector_set_out_fd(ssh_connector connector, socket_t fd)
{
    connector->out_fd = fd;
    connector->fd_is_socket = ssh_connector_fd_is_socket(fd);
    connector->out_channel = NULL;
}

/* TODO */
static void ssh_connector_except(ssh_connector connector, socket_t fd)
{
    (void) connector;
    (void) fd;
}

/* TODO */
static void ssh_connector_except_channel(ssh_connector connector,
                                         ssh_channel channel)
{
    (void) connector;
    (void) channel;
}

/**
 * @internal
 *
 * @brief Reset the poll events to be followed for each file descriptor.
 */
static void ssh_connector_reset_pollevents(ssh_connector connector)
{
    if (connector->in_fd != SSH_INVALID_SOCKET) {
        if (connector->in_available) {
            ssh_poll_remove_events(connector->in_poll, POLLIN);
        } else {
            ssh_poll_add_events(connector->in_poll, POLLIN);
        }
    }

    if (connector->out_fd != SSH_INVALID_SOCKET) {
        if (connector->out_wontblock) {
            ssh_poll_remove_events(connector->out_poll, POLLOUT);
        } else {
            ssh_poll_add_events(connector->out_poll, POLLOUT);
        }
    }
}

/**
 * @internal
 *
 * @brief Update the connector's flags after a read-write io
 *        operation
 *
 * This should be called after some data is successfully read from
 * connector's input and written to connector's output.
 *
 * @param[in, out] connector Connector for which the io operation occurred.
 *
 * @warning This does not consider the case when the io indicated failure
 *
 * @warning This does not consider the case when the input indicated that
 *          EOF was encountered.
 */
static void ssh_connector_update_flags_after_io(ssh_connector connector)
{
    /*
     * With fds we can afford to mark:
     * - in_available as 0 after an fd read (even if more pending data can be
     *   immediately read from the fd)
     *
     * - out_wontblock as 0 after an fd write (even if more data can
     *   be written to the fd without blocking)
     *
     * since poll events set on the fd will get raised to indicate
     * possibility of read/write in case existing situation is apt
     * (i.e can read/write occur right now) or if situation becomes
     * apt in future (read data becomes available, write becomes
     * possible)
     */

    /*
     * On the other hand, with channels we need to be more careful
     * before claiming read/write not possible because channel callbacks
     * are called in limited scenarios.
     *
     * (e.g. connector callback to indicate read data available on input
     * channel is called only when new data is received on channel. It is
     * not called when we have some pending data in channel's buffers but
     * don't receive any new data on the channel)
     *
     * Hence, in case of channels, blindly setting flag associated with
     * read/write input/output to 0 after a read/write may not be a good
     * idea as the callback that sets it back to 1 again may not be ever
     * called again.
     */

    uint32_t window_size;

    /* update in_available based on input source (fd or channel) */
    if (connector->in_fd != SSH_INVALID_SOCKET) {
        connector->in_available = 0;
    } else if (connector->in_channel != NULL) {
        if (ssh_channel_poll_timeout(connector->in_channel, 0, 0) > 0) {
            connector->in_available = 1;
        } else {
            connector->in_available = 0;
        }
    } else {
        /* connector input is invalid ! */
        return;
    }

    /* update out_wontblock based on output source (fd or channel) */
    if (connector->out_fd != SSH_INVALID_SOCKET) {
        connector->out_wontblock = 0;
    } else if (connector->out_channel != NULL) {
        window_size = ssh_channel_window_size(connector->out_channel);
        if (window_size > 0) {
            connector->out_wontblock = 1;
        } else {
            connector->out_wontblock = 0;
        }
    } else {
        /* connector output is invalid ! */
        return;
    }
}

/**
 * @internal
 *
 * @brief Callback called when a poll event is received on an input fd.
 */
static void ssh_connector_fd_in_cb(ssh_connector connector)
{
    unsigned char buffer[CHUNKSIZE];
    uint32_t toread = CHUNKSIZE;
    ssize_t r;
    ssize_t w;
    ssize_t total = 0;
    int rc;

    SSH_LOG(SSH_LOG_TRACE, "connector POLLIN event for fd %d", connector->in_fd);

    if (connector->out_wontblock) {
        if (connector->out_channel != NULL) {
            uint32_t size = ssh_channel_window_size(connector->out_channel);

            /* Don't attempt reading more than the window */
            toread = MIN(size, CHUNKSIZE);
        }

        r = ssh_connector_fd_read(connector, buffer, toread);
        /* Sanity: Make sure we do not get too large return value to make static
         * analysis tools happy */
        if (r < 0 || r > (ssize_t)toread) {
            ssh_connector_except(connector, connector->in_fd);
            return;
        }

        if (connector->out_channel != NULL) {
            if (r == 0) {
                SSH_LOG(SSH_LOG_TRACE, "input fd %d is EOF", connector->in_fd);
                if (connector->out_channel->local_eof == 0) {
                    rc = ssh_channel_send_eof(connector->out_channel);
                    (void)rc; /* TODO Handle rc? */
                }
                connector->in_available = 1; /* Don't poll on it */
                return;
            } else if (r > 0) {
                /* loop around ssh_channel_write in case our window reduced due to a race */
                while (total != r){
                    if (connector->out_flags & SSH_CONNECTOR_STDOUT) {
                        w = ssh_channel_write(connector->out_channel,
                                              buffer + total,
                                              (uint32_t)(r - total));
                    } else {
                        w = ssh_channel_write_stderr(connector->out_channel,
                                                     buffer + total,
                                                     (uint32_t)(r - total));
                    }
                    if (w == SSH_ERROR) {
                        return;
                    }
                    total += w;
                }
            }
        } else if (connector->out_fd != SSH_INVALID_SOCKET) {
            if (r == 0){
                close(connector->out_fd);
                connector->out_fd = SSH_INVALID_SOCKET;
            } else {
                /*
                 * Loop around write in case the write blocks even for CHUNKSIZE
                 * bytes
                 */
                while (total < r) {
                    w = ssh_connector_fd_write(connector,
                                               buffer + total,
                                               (uint32_t)(r - total));
                    /* Sanity: Make sure we do not get too large return value
                     * to make static analysis tools happy */
                    if (w < 0 || w > (r - total)) {
                        ssh_connector_except(connector, connector->out_fd);
                        return;
                    }
                    total += w;
                }
            }
        } else {
            ssh_set_error(connector->session, SSH_FATAL, "output socket or channel closed");
            return;
        }

        ssh_connector_update_flags_after_io(connector);
    } else {
        connector->in_available = 1;
    }
}

/** @internal
 * @brief Callback called when a poll event is received on an output fd
 */
static void
ssh_connector_fd_out_cb(ssh_connector connector)
{
    unsigned char buffer[CHUNKSIZE];
    ssize_t r;
    ssize_t w;
    ssize_t total = 0;
    SSH_LOG(SSH_LOG_TRACE, "connector POLLOUT event for fd %d",
            connector->out_fd);

    if (connector->in_available) {
        if (connector->in_channel != NULL) {
            r = ssh_channel_read_nonblocking(connector->in_channel, buffer,
                                             CHUNKSIZE, 0);
            if (r == SSH_ERROR) {
                ssh_connector_except_channel(connector, connector->in_channel);
                return;
            } else if (r == 0 && ssh_channel_is_eof(connector->in_channel)) {
                close(connector->out_fd);
                connector->out_fd = SSH_INVALID_SOCKET;
                return;
            } else if (r > 0) {
                /* loop around write in case the write blocks even for CHUNKSIZE bytes */
                while (total != r) {
                    w = ssh_connector_fd_write(connector,
                                               buffer + total,
                                               (uint32_t)(r - total));
                    if (w < 0) {
                        ssh_connector_except(connector, connector->out_fd);
                        return;
                    }
                    total += w;
                }
            }
        } else if (connector->in_fd != SSH_INVALID_SOCKET) {
            /* fallback on the socket input callback */
            connector->out_wontblock = 1;
            ssh_connector_fd_in_cb(connector);
        } else {
            ssh_set_error(connector->session,
                          SSH_FATAL,
                          "Output socket or channel closed");
            return;
        }

        ssh_connector_update_flags_after_io(connector);
    } else {
        connector->out_wontblock = 1;
    }
}

/**
 * @internal
 *
 * @brief Callback called when a poll event is received on a file descriptor.
 *
 * This is for input or output.
 *
 * @param[in] fd file descriptor receiving the event
 *
 * @param[in] revents received Poll(2) events
 *
 * @param[in] userdata connector
 *
 * @returns 0
 */
static int ssh_connector_fd_cb(UNUSED_PARAM(ssh_poll_handle p),
                               socket_t fd,
                               int revents,
                               void *userdata)
{
    ssh_connector connector = userdata;

    if (revents & POLLERR) {
        ssh_connector_except(connector, fd);
    } else if((revents & (POLLIN|POLLHUP)) && fd == connector->in_fd) {
        ssh_connector_fd_in_cb(connector);
    } else if(((revents & POLLOUT) || (revents & POLLHUP)) &&
              fd == connector->out_fd) {
        ssh_connector_fd_out_cb(connector);
    }
    ssh_connector_reset_pollevents(connector);

    return 0;
}

/**
 * @internal
 *
 * @brief Callback called when data is received on channel.
 *
 * @param[in] session The SSH session
 *
 * @param[in] channel The channel data came from
 *
 * @param[in] data Pointer to the data
 *
 * @param[in] len Length of data
 *
 * @param[in] is_stderr Set to 1 if the data are out of band
 *
 * @param[in] userdata The ssh connector
 *
 * @returns Amount of data bytes consumed
 */
static int ssh_connector_channel_data_cb(ssh_session session,
                                         UNUSED_PARAM(ssh_channel channel),
                                         void *data,
                                         uint32_t len,
                                         int is_stderr,
                                         void *userdata)
{
    ssh_connector connector = userdata;
    int w;
    uint32_t window;

    SSH_LOG(SSH_LOG_TRACE,
            "Received data (%" PRIu32 ") on channel (%" PRIu32 ":%" PRIu32 ")",
            len,
            channel->local_channel,
            channel->remote_channel);

    if (is_stderr && !(connector->in_flags & SSH_CONNECTOR_STDERR)) {
        /* ignore stderr */
        return 0;
    } else if (!is_stderr && !(connector->in_flags & SSH_CONNECTOR_STDOUT)) {
        /* ignore stdout */
        return 0;
    } else if (len == 0) {
        /* ignore empty data */
        return 0;
    }

    if (connector->out_wontblock) {
        SSH_LOG(SSH_LOG_TRACE, "Writing won't block");
        if (connector->out_channel != NULL) {
            uint32_t window_len;

            window = ssh_channel_window_size(connector->out_channel);
            window_len = MIN(window, len);

            /* Route the data to the right exception channel */
            if (connector->out_flags & SSH_CONNECTOR_STDOUT &&
                !(is_stderr && (connector->out_flags & SSH_CONNECTOR_STDERR))) {
                w = ssh_channel_write(connector->out_channel,
                                      data,
                                      window_len);
            } else {
                w = ssh_channel_write_stderr(connector->out_channel,
                                             data,
                                             window_len);
            }
            if (w == SSH_ERROR) {
                ssh_connector_except_channel(connector, connector->out_channel);
            }
        } else if (connector->out_fd != SSH_INVALID_SOCKET) {
            ssize_t ws = ssh_connector_fd_write(connector, data, len);
            if (ws < 0) {
                ssh_connector_except(connector, connector->out_fd);
            }
            w = (int)ws;
        } else {
            ssh_set_error(session, SSH_FATAL, "output socket or channel closed");
            return SSH_ERROR;
        }

        ssh_connector_update_flags_after_io(connector);
        ssh_connector_reset_pollevents(connector);

        return w;
    } else {
        SSH_LOG(SSH_LOG_TRACE, "Writing would block: wait?");
        connector->in_available = 1;

        return 0;
    }
}

/**
 * @internal
 *
 * @brief Callback called when the channel is free to write.
 *
 * @param[in] bytes Amount of bytes that can be written without blocking
 *
 * @param[in] userdata The ssh connector
 *
 * @returns Amount of data bytes consumed
 */
static int
ssh_connector_channel_write_wontblock_cb(ssh_session session,
                                         UNUSED_PARAM(ssh_channel channel),
                                         uint32_t bytes,
                                         void *userdata)
{
    ssh_connector connector = userdata;
    uint8_t buffer[CHUNKSIZE];
    int r, w;

    (void) channel;

    SSH_LOG(SSH_LOG_TRACE,
            "Write won't block (%" PRIu32 ") on channel (%" PRIu32 ":%" PRIu32 ")",
            bytes,
            channel->local_channel,
            channel->remote_channel);

    if (connector->in_available) {
        if (connector->in_channel != NULL) {
            uint32_t len = MIN(CHUNKSIZE, bytes);

            r = ssh_channel_read_nonblocking(connector->in_channel,
                                             buffer,
                                             len,
                                             0);
            if (r == SSH_ERROR) {
                ssh_connector_except_channel(connector, connector->in_channel);
            } else if (r == 0 && ssh_channel_is_eof(connector->in_channel)) {
                ssh_channel_send_eof(connector->out_channel);
            } else if (r > 0) {
                w = ssh_channel_write(connector->out_channel, buffer, r);
                if (w == SSH_ERROR) {
                    ssh_connector_except_channel(connector,
                                                 connector->out_channel);
                }
            }
        } else if (connector->in_fd != SSH_INVALID_SOCKET) {
            /* fallback on on the socket input callback */
            connector->out_wontblock = 1;
            ssh_connector_fd_in_cb(connector);
            ssh_connector_reset_pollevents(connector);
        } else {
            ssh_set_error(session,
                          SSH_FATAL,
                          "Output socket or channel closed");

            return 0;
        }

        ssh_connector_update_flags_after_io(connector);
    } else {
        connector->out_wontblock = 1;
    }

    return 0;
}

int ssh_connector_set_event(ssh_connector connector, ssh_event event)
{
    int rc = SSH_OK;

    if ((connector->in_fd == SSH_INVALID_SOCKET &&
         connector->in_channel == NULL)
        || (connector->out_fd == SSH_INVALID_SOCKET &&
            connector->out_channel == NULL)) {
        rc = SSH_ERROR;
        ssh_set_error(connector->session,SSH_FATAL,"Connector not complete");
        goto error;
    }

    connector->event = event;
    if (connector->in_fd != SSH_INVALID_SOCKET) {
        if (connector->in_poll == NULL) {
            connector->in_poll = ssh_poll_new(connector->in_fd,
                                              POLLIN|POLLERR,
                                              ssh_connector_fd_cb,
                                              connector);
        }
        rc = ssh_event_add_poll(event, connector->in_poll);
        if (rc != SSH_OK) {
            goto error;
        }
    }

    if (connector->out_fd != SSH_INVALID_SOCKET) {
        if (connector->out_poll == NULL) {
            connector->out_poll = ssh_poll_new(connector->out_fd,
                                               POLLOUT|POLLERR,
                                               ssh_connector_fd_cb,
                                               connector);
        }

        rc = ssh_event_add_poll(event, connector->out_poll);
        if (rc != SSH_OK) {
            goto error;
        }
    }
    if (connector->in_channel != NULL) {
        ssh_session session = ssh_channel_get_session(connector->in_channel);
        rc = ssh_event_add_session(event, session);
        if (rc != SSH_OK)
            goto error;
        if (ssh_channel_poll_timeout(connector->in_channel, 0, 0) > 0){
            connector->in_available = 1;
        }
    }
    if (connector->out_channel != NULL) {
        ssh_session session = ssh_channel_get_session(connector->out_channel);

        rc =  ssh_event_add_session(event, session);
        if (rc != SSH_OK) {
            goto error;
        }
        if (ssh_channel_window_size(connector->out_channel) > 0) {
            connector->out_wontblock = 1;
        }
    }

error:
    return rc;
}

int ssh_connector_remove_event(ssh_connector connector)
{
    ssh_session session = NULL;

    if (connector->in_poll != NULL) {
        ssh_event_remove_poll(connector->event, connector->in_poll);
        ssh_poll_free(connector->in_poll);
        connector->in_poll = NULL;
    }

    if (connector->out_poll != NULL) {
        ssh_event_remove_poll(connector->event, connector->out_poll);
        ssh_poll_free(connector->out_poll);
        connector->out_poll = NULL;
    }

    if (connector->in_channel != NULL) {
        session = ssh_channel_get_session(connector->in_channel);

        ssh_event_remove_session(connector->event, session);
    }

    if (connector->out_channel != NULL) {
        session = ssh_channel_get_session(connector->out_channel);

        ssh_event_remove_session(connector->event, session);
    }
    connector->event = NULL;

    return SSH_OK;
}

/**
 * @internal
 *
 * @brief Check the file descriptor to check if it is a Windows socket handle.
 *
 */
static bool ssh_connector_fd_is_socket(socket_t s)
{
#ifdef _WIN32
    struct sockaddr_storage ss;
    int len = sizeof(struct sockaddr_storage);
    int rc;

    rc = getsockname(s, (struct sockaddr *)&ss, &len);
    if (rc == 0) {
        return true;
    }

    SSH_LOG(SSH_LOG_TRACE,
            "Error %i in getsockname() for fd %d",
            WSAGetLastError(),
            s);

    return false;
#else
    struct stat sb;
    int rc;

    rc = fstat(s, &sb);
    if (rc != 0) {
        SSH_LOG(SSH_LOG_TRACE,
                "error %i in fstat() for fd %d",
                errno,
                s);
        return false;
    }

    /* The descriptor is a socket */
    if (S_ISSOCK(sb.st_mode)) {
          return true;
    }

    return false;
#endif /* _WIN32 */
}

/**
 * @internal
 *
 * @brief read len bytes from socket into buffer
 *
 */
static ssize_t ssh_connector_fd_read(ssh_connector connector,
                                     void *buffer,
                                     uint32_t len)
{
    ssize_t nread = -1;

    if (connector->fd_is_socket) {
        nread = recv(connector->in_fd,buffer, len, 0);
    } else {
        nread = read(connector->in_fd,buffer, len);
    }

    return nread;
}

/**
 * @internal
 *
 * @brief brief writes len bytes from buffer to socket
 *
 */
static ssize_t ssh_connector_fd_write(ssh_connector connector,
                                      const void *buffer,
                                      uint32_t len)
{
    ssize_t bwritten = -1;
    int flags = 0;

#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif

    if (connector->fd_is_socket) {
        bwritten = send(connector->out_fd,buffer, len, flags);
    } else {
        bwritten = write(connector->out_fd, buffer, len);
    }

    return bwritten;
}
