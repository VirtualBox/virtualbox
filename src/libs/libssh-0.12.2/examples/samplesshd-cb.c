/* This is a sample implementation of a libssh based SSH server */
/*
Copyright 2003-2009 Aris Adamantiadis

This file is part of the SSH Library

You are free to copy this file, modify it in any way, consider it being public
domain. This does not apply to the rest of the library though, but it is
allowed to cut-and-paste working code from this file to any license of
program.
The goal is to show the API in action. It's not a reference on how terminal
clients must be made or how a client should react.
*/

#include "config.h"

#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>

#ifdef HAVE_ARGP_H
#include <argp.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <io.h>
#endif

#ifndef BUF_SIZE
#define BUF_SIZE 2049
#endif

#ifndef KEYS_FOLDER
#ifdef _WIN32
#define KEYS_FOLDER
#else
#define KEYS_FOLDER "/etc/ssh/"
#endif
#endif

#define USER "myuser"
#define PASSWORD "mypassword"

static int authenticated=0;
static int tries = 0;
static int error = 0;
static ssh_channel chan=NULL;

static int auth_none(ssh_session session,
                     const char *user,
                     void *userdata)
{
    ssh_string banner = NULL;

    (void)user; /* unused */
    (void)userdata; /* unused */

    ssh_set_auth_methods(session,
                         SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_GSSAPI_MIC);

    banner = ssh_string_from_char("Banner Example\n");
    if (banner != NULL) {
        ssh_send_issue_banner(session, banner);
    }
    ssh_string_free(banner);

    return SSH_AUTH_DENIED;
}

static int auth_password(ssh_session session, const char *user,
        const char *password, void *userdata){
    (void)userdata;
    printf("Authenticating user %s pwd %s\n",user, password);
    if(strcmp(user,USER) == 0 && strcmp(password, PASSWORD) == 0){
        authenticated = 1;
        printf("Authenticated\n");
        return SSH_AUTH_SUCCESS;
    }
    if (tries >= 3){
        printf("Too many authentication tries\n");
        ssh_disconnect(session);
        error = 1;
        return SSH_AUTH_DENIED;
    }
    tries++;
    return SSH_AUTH_DENIED;
}

#ifdef WITH_GSSAPI
static int auth_gssapi_mic(ssh_session session, const char *user, const char *principal, void *userdata){
    ssh_gssapi_creds creds = ssh_gssapi_get_creds(session);
    (void)userdata;
    printf("Authenticating user %s with gssapi principal %s\n",user, principal);
    if (creds != NULL)
        printf("Received some gssapi credentials\n");
    else
        printf("Not received any forwardable creds\n");
    printf("authenticated\n");
    authenticated = 1;
    return SSH_AUTH_SUCCESS;
}
#endif

static int pty_request(ssh_session session, ssh_channel channel, const char *term,
        int x,int y, int px, int py, void *userdata){
    (void) session;
    (void) channel;
    (void) term;
    (void) x;
    (void) y;
    (void) px;
    (void) py;
    (void) userdata;
    printf("Allocated terminal\n");
    return 0;
}

static int shell_request(ssh_session session, ssh_channel channel, void *userdata){
    (void)session;
    (void)channel;
    (void)userdata;
    printf("Allocated shell\n");
    return 0;
}
struct ssh_channel_callbacks_struct channel_cb = {
    .channel_pty_request_function = pty_request,
    .channel_shell_request_function = shell_request
};

static ssh_channel new_session_channel(ssh_session session, void *userdata){
    (void) session;
    (void) userdata;
    if(chan != NULL)
        return NULL;
    printf("Allocated session channel\n");
    chan = ssh_channel_new(session);
    ssh_callbacks_init(&channel_cb);
    ssh_set_channel_callbacks(chan, &channel_cb);
    return chan;
}


#ifdef HAVE_ARGP_H
const char *argp_program_version = "libssh server example "
SSH_STRINGIFY(LIBSSH_VERSION);
const char *argp_program_bug_address = "<libssh@libssh.org>";

/* Program documentation. */
static char doc[] = "libssh -- a Secure Shell protocol implementation";

/* A description of the arguments we accept. */
static char args_doc[] = "BINDADDR";

/* The options we understand. */
static struct argp_option options[] = {
    {
        .name  = "port",
        .key   = 'p',
        .arg   = "PORT",
        .flags = 0,
        .doc   = "Set the port to bind.",
        .group = 0
    },
    {
        .name  = "hostkey",
        .key   = 'k',
        .arg   = "FILE",
        .flags = 0,
        .doc   = "Set the host key.",
        .group = 0
    },
    {
        .name  = "rsakey",
        .key   = 'r',
        .arg   = "FILE",
        .flags = 0,
        .doc   = "Set the rsa key (deprecated alias for 'k').",
        .group = 0
    },
    {
        .name  = "verbose",
        .key   = 'v',
        .arg   = NULL,
        .flags = 0,
        .doc   = "Get verbose output.",
        .group = 0
    },
    {
        .name  = "config",
        .key   = 'f',
        .arg   = "FILE",
        .flags = 0,
        .doc   = "Configuration file to use.",
        .group = 0
    },
    {NULL, 0, NULL, 0, NULL, 0}
};

/* Parse a single option. */
static error_t parse_opt (int key, char *arg, struct argp_state *state) {
    /* Get the input argument from argp_parse, which we
     * know is a pointer to our arguments structure.
     */
    ssh_bind sshbind = state->input;

    switch (key) {
        case 'p':
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, arg);
            break;
        case 'r':
        case 'k':
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY, arg);
            break;
        case 'v':
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_LOG_VERBOSITY_STR, "3");
            break;
        case 'f':
            ssh_bind_options_parse_config(sshbind, arg);
            break;
        case ARGP_KEY_ARG:
            if (state->arg_num >= 1) {
                /* Too many arguments. */
                argp_usage (state);
            }
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDADDR, arg);
            break;
        case ARGP_KEY_END:
            if (state->arg_num < 1) {
                /* Not enough arguments. */
                argp_usage (state);
            }
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

/* Our argp parser. */
static struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};
#endif /* HAVE_ARGP_H */

int main(int argc, char **argv)
{
    ssh_session session = NULL;
    ssh_bind sshbind = NULL;
    ssh_event mainloop = NULL;
    struct ssh_server_callbacks_struct cb = {
        .userdata = NULL,
        .auth_none_function = auth_none,
        .auth_password_function = auth_password,
#ifdef WITH_GSSAPI
        .auth_gssapi_mic_function = auth_gssapi_mic,
#endif
        .channel_open_request_session_function = new_session_channel
    };

    char buf[BUF_SIZE];
    int i;
    int r;

    sshbind=ssh_bind_new();
    session=ssh_new();

    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY, KEYS_FOLDER "ssh_host_rsa_key");

#ifdef HAVE_ARGP_H
    /*
     * Parse our arguments; every option seen by parse_opt will
     * be reflected in arguments.
     */
    argp_parse (&argp, argc, argv, 0, 0, sshbind);
#else
    (void) argc;
    (void) argv;
#endif

    if(ssh_bind_listen(sshbind)<0){
        printf("Error listening to socket: %s\n",ssh_get_error(sshbind));
        return 1;
    }
    r=ssh_bind_accept(sshbind,session);
    if(r==SSH_ERROR){
        printf("error accepting a connection : %s\n",ssh_get_error(sshbind));
        return 1;
    }
    ssh_callbacks_init(&cb);
    ssh_set_server_callbacks(session, &cb);

    if (ssh_handle_key_exchange(session)) {
        printf("ssh_handle_key_exchange: %s\n", ssh_get_error(session));
        return 1;
    }
    ssh_set_auth_methods(session,SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_GSSAPI_MIC);
    mainloop = ssh_event_new();
    ssh_event_add_session(mainloop, session);

    while (!(authenticated && chan != NULL)){
        if(error)
            break;
        r = ssh_event_dopoll(mainloop, -1);
        if (r == SSH_ERROR){
            printf("Error : %s\n",ssh_get_error(session));
            ssh_disconnect(session);
            return 1;
        }
    }
    if(error){
        printf("Error, exiting loop\n");
    } else
        printf("Authenticated and got a channel\n");
    do{
        i=ssh_channel_read(chan, buf, sizeof(buf) - 1, 0);
        if(i>0) {
            if (ssh_channel_write(chan, buf, i) == SSH_ERROR) {
                printf("error writing to channel\n");
                return 1;
            }

            buf[i] = '\0';
            printf("%s", buf);
            fflush(stdout);

            if (buf[0] == '\x0d') {
                if (ssh_channel_write(chan, "\n", 1) == SSH_ERROR) {
                    printf("error writing to channel\n");
                    return 1;
                }

                printf("\n");
            }
        }
    } while (i>0);
    ssh_disconnect(session);
    ssh_bind_free(sshbind);
    ssh_finalize();
    return 0;
}
