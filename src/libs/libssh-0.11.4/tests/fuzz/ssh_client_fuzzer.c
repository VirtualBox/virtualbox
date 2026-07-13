/*
 * Copyright 2019 Andreas Schneider <asn@cryptomilk.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>

#define LIBSSH_STATIC 1
#include <libssh/libssh.h>
#include <libssh/callbacks.h>

static int auth_callback(const char *prompt,
                         char *buf,
                         size_t len,
                         int echo,
                         int verify,
                         void *userdata)
{
    (void)prompt;   /* unused */
    (void)echo;     /* unused */
    (void)verify;   /* unused */
    (void)userdata; /* unused */

    snprintf(buf, len, "secret");

    return 0;
}

struct ssh_callbacks_struct cb = {
    .userdata = NULL,
    .auth_function = auth_callback,
};

static void select_loop(ssh_session session, ssh_channel channel)
{
    ssh_connector connector_in, connector_out, connector_err;

    ssh_event event = ssh_event_new();

    /* stdin */
    connector_in = ssh_connector_new(session);
    ssh_connector_set_out_channel(connector_in, channel, SSH_CONNECTOR_STDINOUT);
    ssh_connector_set_in_fd(connector_in, 0);
    ssh_event_add_connector(event, connector_in);

    /* stdout */
    connector_out = ssh_connector_new(session);
    ssh_connector_set_out_fd(connector_out, 1);
    ssh_connector_set_in_channel(connector_out, channel, SSH_CONNECTOR_STDINOUT);
    ssh_event_add_connector(event, connector_out);

    /* stderr */
    connector_err = ssh_connector_new(session);
    ssh_connector_set_out_fd(connector_err, 2);
    ssh_connector_set_in_channel(connector_err, channel, SSH_CONNECTOR_STDERR);
    ssh_event_add_connector(event, connector_err);

    while (ssh_channel_is_open(channel)) {
        ssh_event_dopoll(event, 60000);
    }
    ssh_event_remove_connector(event, connector_in);
    ssh_event_remove_connector(event, connector_out);
    ssh_event_remove_connector(event, connector_err);

    ssh_connector_free(connector_in);
    ssh_connector_free(connector_out);
    ssh_connector_free(connector_err);

    ssh_event_free(event);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ssh_session session = NULL;
    ssh_channel channel = NULL;
    const char *env = NULL;
    int socket_fds[2] = {-1, -1};
    ssize_t nwritten;
    bool no = false;
    int rc;
    long timeout = 1; /* use short timeout to avoid timeouts during fuzzing */

    /* This is the maximum that can be handled by the socket buffer before the
     * other side will read some data. Other option would be feeding the socket
     * from different thread which would not mind if it would be blocked, but I
     * believe all the important inputs should fit into this size */
    if (size > 219264) {
        return -1;
    }

    /* Set up the socket to send data */
    rc = socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds);
    assert(rc == 0);

    nwritten = send(socket_fds[1], data, size, 0);
    assert((size_t)nwritten == size);

    rc = shutdown(socket_fds[1], SHUT_WR);
    assert(rc == 0);

    ssh_init();

    session = ssh_new();
    assert(session != NULL);

    env = getenv("LIBSSH_VERBOSITY");
    if (env != NULL && strlen(env) > 0) {
        ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY_STR, env);
    }
    rc = ssh_options_set(session, SSH_OPTIONS_FD, &socket_fds[0]);
    assert(rc == 0);
    rc = ssh_options_set(session, SSH_OPTIONS_HOST, "127.0.0.1");
    assert(rc == 0);
    rc = ssh_options_set(session, SSH_OPTIONS_USER, "alice");
    assert(rc == 0);
    rc = ssh_options_set(session, SSH_OPTIONS_CIPHERS_C_S, "none");
    assert(rc == 0);
    rc = ssh_options_set(session, SSH_OPTIONS_CIPHERS_S_C, "none");
    assert(rc == 0);
    rc = ssh_options_set(session, SSH_OPTIONS_HMAC_C_S, "none");
    assert(rc == 0);
    rc = ssh_options_set(session, SSH_OPTIONS_HMAC_S_C, "none");
    assert(rc == 0);
    rc = ssh_options_set(session, SSH_OPTIONS_PROCESS_CONFIG, &no);
    assert(rc == 0);
    rc = ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);
    assert(rc == 0);

    ssh_callbacks_init(&cb);
    ssh_set_callbacks(session, &cb);

    rc = ssh_connect(session);
    if (rc != SSH_OK) {
        goto out;
    }

    rc = ssh_userauth_none(session, NULL);
    if (rc != SSH_OK) {
        goto out;
    }

    channel = ssh_channel_new(session);
    if (channel == NULL) {
        goto out;
    }

    rc = ssh_channel_open_session(channel);
    if (rc != SSH_OK) {
        goto out;
    }

    rc = ssh_channel_request_exec(channel, "ls");
    if (rc != SSH_OK) {
        goto out;
    }

    select_loop(session, channel);

out:
    ssh_channel_free(channel);
    ssh_disconnect(session);
    ssh_free(session);

    ssh_finalize();

    close(socket_fds[0]);
    close(socket_fds[1]);

    return 0;
}
