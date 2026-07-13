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

#include "test_server.h"
#include "testserver_common.h"

#include <libssh/priv.h>
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <sys/wait.h>

void free_server_state(struct server_state_st *state)
{
    if (state == NULL) {
        return;
    }

    SAFE_FREE(state->address);

    SAFE_FREE(state->ecdsa_key);
    SAFE_FREE(state->ed25519_key);
    SAFE_FREE(state->rsa_key);
    SAFE_FREE(state->host_key);

    SAFE_FREE(state->pcap_file);

    SAFE_FREE(state->expected_username);
    SAFE_FREE(state->expected_password);
    SAFE_FREE(state->config_file);
    SAFE_FREE(state->log_file);
    SAFE_FREE(state->server_cb);
    SAFE_FREE(state->channel_cb);
}

/* SIGCHLD handler for cleaning up dead children. */
static void sigchld_handler(int signo) {
    (void) signo;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

bool done = false;

static void sigterm_handler(int signo)
{
    (void) signo;
    fprintf(stderr, "Received SIGTERM. Gracefully exiting ...\n");
    done = true;
}

int run_server(struct server_state_st *state)
{
    ssh_session session = NULL;
    ssh_bind sshbind = NULL;
    ssh_event event = NULL;

    struct sigaction sa = {
        .sa_flags = 0
    };

    int rc = SSH_ERROR;

    /* Check provided state */
    if (state == NULL) {
        fprintf(stderr, "Invalid state\n");
        goto out;
    }

    /* Set up SIGCHLD handler. */
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, NULL) != 0) {
        fprintf(stderr, "Failed to register SIGCHLD handler\n");
        goto out;
    }

    /* Set up SIGTERM handler. */
    sa.sa_handler = sigterm_handler;
    sa.sa_flags = 0;

    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        fprintf(stderr, "Failed to register SIGTERM handler\n");
        goto out;
    }

    /* Redirect all the output and errors to the file to avoid mixing up with
     * the output from the client */
    if (state->log_file != NULL) {
        int fd;
        FILE *f = fopen(state->log_file, "a");
        if (f == NULL) {
            fprintf(stderr, "Failed to open the log file: %s\n", strerror(errno));
            goto out;
        }
        fd = dup2(fileno(f), STDERR_FILENO);
        if (fd == -1) {
            fprintf(stderr, "dup2 of log file to stderr failed: %s\n",
                    strerror(errno));
            fclose(f);
            goto out;
        }
        fd = dup2(fileno(f), STDOUT_FILENO);
        if (fd == -1) {
            fprintf(stderr, "dup2 of log file to stdout failed: %s\n",
                    strerror(errno));
            fclose(f);
            goto out;
        }
        fclose(f);
    }

    if (state->address == NULL) {
        fprintf(stderr, "Missing bind address\n");
        goto out;
    }

    if (state->host_key == NULL && state->rsa_key == NULL &&
        state->ecdsa_key == NULL && state->ed25519_key) {
        fprintf(stderr, "Missing host key\n");
        goto out;
    }

    sshbind = ssh_bind_new();
    if (sshbind == NULL) {
        fprintf(stderr, "Out of memory\n");
        goto out;
    }

    if (state->verbosity) {
        rc = ssh_bind_options_set(sshbind,
                                  SSH_BIND_OPTIONS_LOG_VERBOSITY,
                                  &state->verbosity);
        if (rc != 0) {
            fprintf(stderr,
                    "Error setting verbosity level: %s\n",
                    ssh_get_error(sshbind));
            goto out;
        }
    }

    if (!state->parse_global_config) {
        rc = ssh_bind_options_set(sshbind,
                                  SSH_BIND_OPTIONS_PROCESS_CONFIG,
                                  &(state->parse_global_config));
        if (rc != 0) {
            goto out;
        }
    }

    if (state->config_file) {
        rc = ssh_bind_options_parse_config(sshbind, state->config_file);
        if (rc != 0) {
            goto out;
        }
    }

    rc = ssh_bind_options_set(sshbind,
                              SSH_BIND_OPTIONS_BINDADDR,
                              state->address);
    if (rc != 0) {
        fprintf(stderr,
                "Error setting bind address: %s\n",
                ssh_get_error(sshbind));
        goto out;
    }

    rc = ssh_bind_options_set(sshbind,
                              SSH_BIND_OPTIONS_BINDPORT,
                              &(state->port));
    if (rc != 0) {
        fprintf(stderr,
                "Error setting bind port: %s\n",
                ssh_get_error(sshbind));
        goto out;
    }

    if (state->rsa_key != NULL) {
        rc = ssh_bind_options_set(sshbind,
                                  SSH_BIND_OPTIONS_HOSTKEY,
                                  state->rsa_key);
        if (rc != 0) {
            fprintf(stderr,
                    "Error setting RSA key: %s\n",
                    ssh_get_error(sshbind));
            goto out;
        }
    }

    if (state->ecdsa_key != NULL) {
        rc = ssh_bind_options_set(sshbind,
                                  SSH_BIND_OPTIONS_HOSTKEY,
                                  state->ecdsa_key);
        if (rc != 0) {
            fprintf(stderr,
                    "Error setting ECDSA key: %s\n",
                    ssh_get_error(sshbind));
            goto out;
        }
    }

    if (state->host_key) {
        rc = ssh_bind_options_set(sshbind,
                                  SSH_BIND_OPTIONS_HOSTKEY,
                                  state->host_key);
        if (rc) {
            fprintf(stderr,
                    "Error setting hostkey: %s\n",
                    ssh_get_error(sshbind));
            goto out;
        }
    }

    rc = ssh_bind_listen(sshbind);
    if (rc != 0) {
        fprintf(stderr,
                "Error listening to socket: %s\n",
                ssh_get_error(sshbind));
        goto out;
    }

    printf("%d: Started libssh test server on port %d\n", getpid(), state->port);

    while (done == false) {
        session = ssh_new();
        if (session == NULL) {
            fprintf(stderr, "Out of memory\n");
            rc = SSH_ERROR;
            goto out;
        }

        /* Blocks until there is a new incoming connection. */
        rc = ssh_bind_accept(sshbind, session);
        if (rc != SSH_ERROR) {
            pid_t pid = fork();

            switch(pid) {
            case 0:
                /* Remove the SIGCHLD handler inherited from parent. */
                sa.sa_handler = SIG_DFL;
                sigaction(SIGCHLD, &sa, NULL);
                /* Remove the SIGTERM handler inherited from parent. */
                sa.sa_handler = SIG_DFL;
                sigaction(SIGTERM, &sa, NULL);
                /* Remove socket binding, which allows us to restart the
                 * parent process, without terminating existing sessions. */
                ssh_bind_free(sshbind);

                event = ssh_event_new();
                if (event != NULL) {
                    /* Blocks until the SSH session ends by either
                     * child process exiting, or client disconnecting. */
                    state->handle_session(event, session, state);
                    ssh_event_free(event);
                } else {
                    fprintf(stderr, "Could not create polling context\n");
                }
                ssh_disconnect(session);
                ssh_free(session);

                free_server_state(state);
                SAFE_FREE(state);
                finalize_openssl();
                exit(0);
            case -1:
                fprintf(stderr, "Failed to fork\n");
            }
            fprintf(stderr, "Forked process PID %d\n", pid);
        } else {
            fprintf(stderr,
                    "Error accepting a connection: %s\n",
                    ssh_get_error(sshbind));
        }

        /* Since the session has been passed to a child fork, do some cleaning
         * up at the parent process. */
        ssh_disconnect(session);
        ssh_free(session);
    }

    rc = 0;

out:
    free_server_state(state);
    SAFE_FREE(state);
    ssh_bind_free(sshbind);
    return rc;
}

pid_t
fork_run_server(struct server_state_st *state,
                void (*free_test_state) (void **userdata),
                void *userdata)
{
    pid_t pid;
    int rc;

    char err_str[1024] = {0};

    struct sigaction sa;

    /* Check provided state */
    if (state == NULL) {
        fprintf(stderr, "Invalid state\n");
        return -1;
    }

    /* Set up SIGCHLD handler. */
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, NULL) != 0) {
        strerror_r(errno, err_str, 1024);
        fprintf(stderr, "Failed to register SIGCHLD handler: %s\n",
                err_str);
        return -1;
    }

    pid = fork();
    switch(pid) {
    case 0:
        /* no longer needed */
        free_test_state(userdata);
        /* Remove the SIGCHLD handler inherited from parent. */
        sa.sa_handler = SIG_DFL;
        sigaction(SIGCHLD, &sa, NULL);

        /* The child process starts a server which will listen for connections */
        rc = run_server(state);
        finalize_openssl();
        exit(rc);
    case -1:
        strerror_r(errno, err_str, 1024);
        fprintf(stderr, "Failed to fork: %s\n",
                err_str);
        return -1;
    default:
        /* Return the child pid  */
        fprintf(stderr, "Forked process PID %d\n", pid);
        return pid;
    }
}
