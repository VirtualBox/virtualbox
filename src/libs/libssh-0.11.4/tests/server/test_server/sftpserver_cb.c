/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2018 by Red Hat, Inc.
 *
 * Author: Anderson Toshiyuki Sasaki <ansasaki@redhat.com>
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
#include "test_server.h"
#include "default_cb.h"

#include <libssh/callbacks.h>
#include <libssh/server.h>
#include <libssh/priv.h>
#include <libssh/sftp.h>
#include <libssh/sftpserver.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#ifdef HAVE_PTY_H
#include <pty.h>
#endif
#ifdef HAVE_UTMP_H
#include <utmp.h>
#endif
#ifdef HAVE_UTIL_H
#include <util.h>
#endif

/* below are for sftp */
#include <sys/types.h>
#include <sys/statvfs.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>


#define BUF_SIZE 1048576
#define SESSION_END (SSH_CLOSED | SSH_CLOSED_ERROR)


/* TODO implement proper pam authentication cb */
static int sftp_auth_password_cb(UNUSED_PARAM(ssh_session session),
                     const char *user,
                     const char *password,
                     void *userdata)
{
    bool known_user = false;
    bool valid_password = false;

    struct session_data_st *sdata;

    sdata = (struct session_data_st *)userdata;

    if (sdata == NULL) {
        fprintf(stderr, "Error: NULL userdata\n");
        goto null_userdata;
    }

    if (sdata->username == NULL) {
        fprintf(stderr, "Error: expected username not set\n");
        goto denied;
    }

    if (sdata->password == NULL) {
        fprintf(stderr, "Error: expected password not set\n");
        goto denied;
    }

    printf("Password authentication of user %s\n", user);

    known_user = !(strcmp(user, sdata->username));
    valid_password = !(strcmp(password, sdata->password));

    if (known_user && valid_password) {
        sdata->authenticated = 1;
        sdata->auth_attempts = 0;
        printf("Authenticated\n");
        return SSH_AUTH_SUCCESS;
    }

denied:
    sdata->auth_attempts++;
null_userdata:
    return SSH_AUTH_DENIED;
}

static ssh_channel sftp_channel_new_session_cb(ssh_session session, void *userdata)
{
    struct session_data_st *sdata = NULL;
    ssh_channel chan = NULL;

    sdata = (struct session_data_st *)userdata;

    if (sdata == NULL) {
        fprintf(stderr, "NULL userdata");
        goto end;
    }

    chan = ssh_channel_new(session);
    if (chan == NULL) {
        fprintf(stderr, "Error creating channel: %s\n",
                ssh_get_error(session));
        goto end;
    }

    sdata->channel = chan;

end:
    return chan;
}

#ifdef WITH_PCAP
static void set_pcap(struct session_data_st *sdata,
                     ssh_session session,
                     char *pcap_file)
{
    int rc = 0;

    if (sdata == NULL) {
        return;
    }

    if (pcap_file == NULL) {
        return;
    }

    sdata->pcap = ssh_pcap_file_new();
    if (sdata->pcap == NULL) {
        return;
    }

    rc = ssh_pcap_file_open(sdata->pcap, pcap_file);
    if (rc == SSH_ERROR) {
        fprintf(stderr, "Error opening pcap file\n");
        ssh_pcap_file_free(sdata->pcap);
        sdata->pcap = NULL;
        return;
    }
    ssh_set_pcap_file(session, sdata->pcap);
}

static void cleanup_pcap(struct session_data_st *sdata)
{
    if (sdata == NULL) {
        return;
    }

    if (sdata->pcap == NULL) {
        return;
    }

    ssh_pcap_file_free(sdata->pcap);
    sdata->pcap = NULL;
}
#endif


/* The caller is responsible to set the userdata to be provided to the callback
 * The caller is responsible to free the allocated structure
 */
struct ssh_server_callbacks_struct *get_sftp_server_cb(void)
{

    struct ssh_server_callbacks_struct *cb;

    cb = (struct ssh_server_callbacks_struct *)calloc(1,
            sizeof(struct ssh_server_callbacks_struct));

    if (cb == NULL) {
        fprintf(stderr, "Out of memory\n");
        goto end;
    }

    cb->auth_password_function = sftp_auth_password_cb;
    cb->channel_open_request_session_function = sftp_channel_new_session_cb;

end:
    return cb;
}

/* The caller is responsible to set the userdata to be provided to the callback
 * The caller is responsible to free the allocated structure
 * */
struct ssh_channel_callbacks_struct *get_sftp_channel_cb(void)
{
    struct ssh_channel_callbacks_struct *cb;

    cb = (struct ssh_channel_callbacks_struct *)calloc(1,
            sizeof(struct ssh_channel_callbacks_struct));
    if (cb == NULL) {
        fprintf(stderr, "Out of memory\n");
        goto end;
    }

    cb->channel_data_function = sftp_channel_default_data_callback;
    cb->channel_subsystem_request_function = sftp_channel_default_subsystem_request;

end:
    return cb;
};

void sftp_handle_session_cb(ssh_event event,
                            ssh_session session,
                            struct server_state_st *state)
{
    int n;
    int rc = 0;

    /* Structure for storing the pty size. */
    struct winsize wsize = {
        .ws_row = 0,
        .ws_col = 0,
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    /* Our struct holding information about the channel. */
    struct channel_data_st cdata = {
        .event = NULL,
        .winsize = &wsize,
        .sftp = NULL
    };

    /* Our struct holding information about the session. */
    struct session_data_st sdata = {
        .channel = NULL,
        .auth_attempts = 0,
        .authenticated = 0,
        .username = SSHD_DEFAULT_USER,
        .password = SSHD_DEFAULT_PASSWORD
    };

    struct ssh_channel_callbacks_struct *channel_cb = NULL;
    struct ssh_server_callbacks_struct *server_cb = NULL;

    if (state == NULL) {
        fprintf(stderr, "NULL server state provided\n");
        goto end;
    }

    /* If callbacks were provided use them. Otherwise, use default callbacks */
    if (state->server_cb != NULL) {
        /* This is a macro, it does not return a value */
        ssh_callbacks_init(state->server_cb);

        rc = ssh_set_server_callbacks(session, state->server_cb);
        if (rc) {
            goto end;
        }
    } else {
        server_cb = get_sftp_server_cb();
        if (server_cb == NULL) {
            goto end;
        }

        server_cb->userdata = &sdata;

        /* This is a macro, it does not return a value */
        ssh_callbacks_init(server_cb);

        rc = ssh_set_server_callbacks(session, server_cb);
        if (rc) {
            goto end;
        }
    }

    sdata.server_state = (void *)state;

#ifdef WITH_PCAP
    set_pcap(&sdata, session, state->pcap_file);
#endif

    if (state->expected_username != NULL) {
        sdata.username = state->expected_username;
    }

    if (state->expected_password != NULL) {
        sdata.password = state->expected_password;
    }

    if (ssh_handle_key_exchange(session) != SSH_OK) {
        fprintf(stderr, "%s\n", ssh_get_error(session));
        goto end;
    }

    /* Set the supported authentication methods */
    if (state->auth_methods) {
        ssh_set_auth_methods(session, state->auth_methods);
    } else {
        ssh_set_auth_methods(session,
                SSH_AUTH_METHOD_PASSWORD |
                SSH_AUTH_METHOD_PUBLICKEY);
    }

    ssh_event_add_session(event, session);

    n = 0;
    while (sdata.authenticated == 0 || sdata.channel == NULL) {
        /* If the user has used up all attempts, or if he hasn't been able to
         * authenticate in 10 seconds (n * 100ms), disconnect. */
        if (sdata.auth_attempts >= state->max_tries || n >= 100) {
            goto end;
        }

        if (ssh_event_dopoll(event, 100) == SSH_ERROR) {
            fprintf(stderr, "do_poll error: %s\n", ssh_get_error(session));
            goto end;
        }
        n++;
    }

    /* TODO check return values */
    if (state->channel_cb != NULL) {
        ssh_callbacks_init(state->channel_cb);

        rc = ssh_set_channel_callbacks(sdata.channel, state->channel_cb);
        if (rc) {
            goto end;
        }
    } else {
        channel_cb = get_sftp_channel_cb();
        if (channel_cb == NULL) {
            goto end;
        }

        channel_cb->userdata = &(cdata.sftp);

        ssh_callbacks_init(channel_cb);
        rc = ssh_set_channel_callbacks(sdata.channel, channel_cb);
        if (rc) {
            goto end;
        }
    }

    do {
        /* Poll the main event which takes care of the session, the channel and
         * even our child process's stdout/stderr (once it's started). */
        if (ssh_event_dopoll(event, -1) == SSH_ERROR) {
            ssh_channel_close(sdata.channel);
        }

        /* If child process's stdout/stderr has been registered with the event,
         * or the child process hasn't started yet, continue. */
        if (cdata.event != NULL) {
            continue;
        }

    } while (ssh_channel_is_open(sdata.channel));

    ssh_channel_send_eof(sdata.channel);
    ssh_channel_close(sdata.channel);
    sftp_server_free(cdata.sftp);

    /* Wait up to 5 seconds for the client to terminate the session. */
    for (n = 0; n < 50 && (ssh_get_status(session) & SESSION_END) == 0; n++) {
        ssh_event_dopoll(event, 100);
    }

end:
#ifdef WITH_PCAP
    cleanup_pcap(&sdata);
#endif
    if (channel_cb != NULL) {
        free(channel_cb);
    }
    if (server_cb != NULL) {
        free(server_cb);
    }
    return;
}
