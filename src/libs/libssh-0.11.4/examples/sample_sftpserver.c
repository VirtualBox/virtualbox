/* This is a sample implementation of a libssh based SSH server */
/*
Copyright 2014 Audrius Butkevicius

This file is part of the SSH Library

You are free to copy this file, modify it in any way, consider it being public
domain. This does not apply to the rest of the library though, but it is
allowed to cut-and-paste working code from this file to any license of
program.
The goal is to show the API in action.
*/

#include "config.h"

#include <libssh/callbacks.h>
#include <libssh/server.h>
#include <libssh/sftp.h>
#include <libssh/sftpserver.h>

#include <poll.h>
#ifdef HAVE_ARGP_H
#include <argp.h>
#endif
#include <fcntl.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#ifdef HAVE_PTY_H
#include <pty.h>
#endif
#include <signal.h>
#include <stdlib.h>
#ifdef HAVE_UTMP_H
#include <utmp.h>
#endif
#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdbool.h>

/* below are for sftp */
#include <sys/statvfs.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <inttypes.h>

#ifndef KEYS_FOLDER
#ifdef _WIN32
#define KEYS_FOLDER
#else
#define KEYS_FOLDER "/etc/ssh/"
#endif
#endif

#define USER "myuser"
#define PASS "mypassword"
#define BUF_SIZE 1048576
#define SESSION_END (SSH_CLOSED | SSH_CLOSED_ERROR)

static void set_default_keys(ssh_bind sshbind,
                             int rsa_already_set,
                             int ecdsa_already_set)
{
    if (!rsa_already_set)
    {
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY,
                             KEYS_FOLDER "ssh_host_rsa_key");
    }
    if (!ecdsa_already_set)
    {
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY,
                             KEYS_FOLDER "ssh_host_ecdsa_key");
    }
    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY,
                         KEYS_FOLDER "ssh_host_ed25519_key");
}
#define DEF_STR_SIZE 1024
char authorizedkeys[DEF_STR_SIZE] = {0};
#ifdef HAVE_ARGP_H
const char *argp_program_version = "libssh sftp server example " SSH_STRINGIFY(LIBSSH_VERSION);
const char *argp_program_bug_address = "<libssh@libssh.org>";

/* Program documentation. */
static char doc[] = "Sftp server implemented with libssh -- a Secure Shell protocol implementation";

/* A description of the arguments we accept. */
static char args_doc[] = "BINDADDR";

/* The options we understand. */
static struct argp_option options[] = {
    {.name = "port",
     .key = 'p',
     .arg = "PORT",
     .flags = 0,
     .doc = "Set the port to bind.",
     .group = 0},
    {.name = "hostkey",
     .key = 'k',
     .arg = "FILE",
     .flags = 0,
     .doc = "Set a host key.  Can be used multiple times.  "
            "Implies no default keys.",
     .group = 0},
    {.name = "rsakey",
     .key = 'r',
     .arg = "FILE",
     .flags = 0,
     .doc = "Set the rsa key.",
     .group = 0},
    {.name = "ecdsakey",
     .key = 'e',
     .arg = "FILE",
     .flags = 0,
     .doc = "Set the ecdsa key.",
     .group = 0},
    {.name = "authorizedkeys",
     .key = 'a',
     .arg = "FILE",
     .flags = 0,
     .doc = "Set the authorized keys file.",
     .group = 0},
    {.name = "no-default-keys",
     .key = 'n',
     .arg = NULL,
     .flags = 0,
     .doc = "Do not set default key locations.",
     .group = 0},
    {.name = "verbose",
     .key = 'v',
     .arg = NULL,
     .flags = 0,
     .doc = "Get verbose output.",
     .group = 0},
    {NULL, 0, NULL, 0, NULL, 0}};

/* Parse a single option. */
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    /* Get the input argument from argp_parse, which we
     * know is a pointer to our arguments structure. */
    ssh_bind sshbind = state->input;
    static int no_default_keys = 0;
    static int rsa_already_set = 0, ecdsa_already_set = 0;

    switch (key)
    {
    case 'n':
        no_default_keys = 1;
        break;
    case 'p':
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, arg);
        break;
    case 'k':
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY, arg);
        /* We can't track the types of keys being added with this
           option, so let's ensure we keep the keys we're adding
           by just not setting the default keys */
        no_default_keys = 1;
        break;
    case 'r':
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY, arg);
        rsa_already_set = 1;
        break;
    case 'e':
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY, arg);
        ecdsa_already_set = 1;
        break;
    case 'a':
        strncpy(authorizedkeys, arg, DEF_STR_SIZE - 1);
        break;
    case 'v':
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_LOG_VERBOSITY_STR,
                             "3");
        break;
    case ARGP_KEY_ARG:
        if (state->arg_num >= 1)
        {
            /* Too many arguments. */
            argp_usage(state);
        }
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDADDR, arg);
        break;
    case ARGP_KEY_END:
        if (state->arg_num < 1)
        {
            /* Not enough arguments. */
            argp_usage(state);
        }

        if (!no_default_keys)
        {
            set_default_keys(sshbind,
                             rsa_already_set,
                             ecdsa_already_set);
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

/* A userdata struct for channel. */
struct channel_data_struct
{
    /* Event which is used to poll the above descriptors. */
    ssh_event event;
    sftp_session sftp;
};

/* A userdata struct for session. */
struct session_data_struct
{
    /* Pointer to the channel the session will allocate. */
    ssh_channel channel;
    int auth_attempts;
    int authenticated;
};

static int auth_password(ssh_session session, const char *user,
                         const char *pass, void *userdata)
{
    struct session_data_struct *sdata = (struct session_data_struct *)userdata;

    (void)session;

    if (strcmp(user, USER) == 0 && strcmp(pass, PASS) == 0)
    {
        sdata->authenticated = 1;
        return SSH_AUTH_SUCCESS;
    }

    sdata->auth_attempts++;
    return SSH_AUTH_DENIED;
}

static int auth_publickey(ssh_session session,
                          const char *user,
                          struct ssh_key_struct *pubkey,
                          char signature_state,
                          void *userdata)
{
    struct session_data_struct *sdata = (struct session_data_struct *)userdata;

    (void)session;
    (void)user;

    if (signature_state == SSH_PUBLICKEY_STATE_NONE)
    {
        return SSH_AUTH_SUCCESS;
    }

    if (signature_state != SSH_PUBLICKEY_STATE_VALID)
    {
        return SSH_AUTH_DENIED;
    }

    // valid so far.  Now look through authorized keys for a match
    if (authorizedkeys[0])
    {
        ssh_key key = NULL;
        int result;
        struct stat buf;

        if (stat(authorizedkeys, &buf) == 0)
        {
            result = ssh_pki_import_pubkey_file(authorizedkeys, &key);
            if ((result != SSH_OK) || (key == NULL))
            {
                fprintf(stderr,
                        "Unable to import public key file %s\n",
                        authorizedkeys);
            }
            else
            {
                result = ssh_key_cmp(key, pubkey, SSH_KEY_CMP_PUBLIC);
                ssh_key_free(key);
                if (result == 0)
                {
                    sdata->authenticated = 1;
                    return SSH_AUTH_SUCCESS;
                }
            }
        }
    }

    // no matches
    sdata->authenticated = 0;
    return SSH_AUTH_DENIED;
}

static ssh_channel channel_open(ssh_session session, void *userdata)
{
    struct session_data_struct *sdata = (struct session_data_struct *)userdata;

    sdata->channel = ssh_channel_new(session);
    return sdata->channel;
}

static void handle_session(ssh_event event, ssh_session session)
{
    int n;

    /* Our struct holding information about the channel. */
    struct channel_data_struct cdata = {
        .sftp = NULL,
    };

    /* Our struct holding information about the session. */
    struct session_data_struct sdata = {
        .channel = NULL,
        .auth_attempts = 0,
        .authenticated = 0,
    };

    struct ssh_channel_callbacks_struct channel_cb = {
        .userdata = &(cdata.sftp),
        .channel_data_function = sftp_channel_default_data_callback,
        .channel_subsystem_request_function = sftp_channel_default_subsystem_request,
    };

    struct ssh_server_callbacks_struct server_cb = {
        .userdata = &sdata,
        .auth_password_function = auth_password,
        .channel_open_request_session_function = channel_open,
    };

    if (authorizedkeys[0])
    {
        server_cb.auth_pubkey_function = auth_publickey;
        ssh_set_auth_methods(session, SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_PUBLICKEY);
    }
    else
        ssh_set_auth_methods(session, SSH_AUTH_METHOD_PASSWORD);

    ssh_callbacks_init(&server_cb);
    ssh_callbacks_init(&channel_cb);

    ssh_set_server_callbacks(session, &server_cb);

    if (ssh_handle_key_exchange(session) != SSH_OK)
    {
        fprintf(stderr, "%s\n", ssh_get_error(session));
        return;
    }

    ssh_event_add_session(event, session);

    n = 0;
    while (sdata.authenticated == 0 || sdata.channel == NULL) {
        /* If the user has used up all attempts, or if he hasn't been able to
         * authenticate in 10 seconds (n * 100ms), disconnect. */
        if (sdata.auth_attempts >= 3 || n >= 100) {
            return;
        }

        if (ssh_event_dopoll(event, 100) == SSH_ERROR) {
            fprintf(stderr, "%s\n", ssh_get_error(session));
            return;
        }
        n++;
    }

    ssh_set_channel_callbacks(sdata.channel, &channel_cb);

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
        /* FIXME The server keeps hanging in the poll above when the client
         * closes the channel */
    } while (ssh_channel_is_open(sdata.channel));

    ssh_channel_send_eof(sdata.channel);
    ssh_channel_close(sdata.channel);

    /* Wait up to 5 seconds for the client to terminate the session. */
    for (n = 0; n < 50 && (ssh_get_status(session) & SESSION_END) == 0; n++) {
        ssh_event_dopoll(event, 100);
    }
}

/* SIGCHLD handler for cleaning up dead children. */
static void sigchld_handler(int signo)
{
    (void)signo;

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

int main(int argc, char **argv)
{
    ssh_bind sshbind = NULL;
    ssh_session session = NULL;
    ssh_event event = NULL;
    struct sigaction sa;
    int rc;

    /* Set up SIGCHLD handler. */
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) != 0)
    {
        fprintf(stderr, "Failed to register SIGCHLD handler\n");
        return 1;
    }

    rc = ssh_init();
    if (rc < 0)
    {
        fprintf(stderr, "ssh_init failed\n");
        goto exit;
    }

    sshbind = ssh_bind_new();
    if (sshbind == NULL)
    {
        fprintf(stderr, "ssh_bind_new failed\n");
        goto exit;
    }

#ifdef HAVE_ARGP_H
    argp_parse(&argp, argc, argv, 0, 0, sshbind);
#else
    (void)argc;
    (void)argv;

    set_default_keys(sshbind, 0, 0);
#endif /* HAVE_ARGP_H */

    if (ssh_bind_listen(sshbind) < 0)
    {
        fprintf(stderr, "%s\n", ssh_get_error(sshbind));
        goto exit;
    }

    while (1)
    {
        session = ssh_new();
        if (session == NULL)
        {
            fprintf(stderr, "Failed to allocate session\n");
            continue;
        }

        /* Blocks until there is a new incoming connection. */
        if (ssh_bind_accept(sshbind, session) != SSH_ERROR)
        {
            switch (fork())
            {
            case 0:
                /* Remove the SIGCHLD handler inherited from parent. */
                sa.sa_handler = SIG_DFL;
                sigaction(SIGCHLD, &sa, NULL);
                /* Remove socket binding, which allows us to restart the
                 * parent process, without terminating existing sessions. */
                ssh_bind_free(sshbind);

                event = ssh_event_new();
                if (event != NULL)
                {
                    /* Blocks until the SSH session ends by either
                     * child process exiting, or client disconnecting. */
                    handle_session(event, session);
                    ssh_event_free(event);
                }
                else
                {
                    fprintf(stderr, "Could not create polling context\n");
                }
                ssh_disconnect(session);
                ssh_free(session);

                exit(0);
            case -1:
                fprintf(stderr, "Failed to fork\n");
            }
        }
        else
        {
            fprintf(stderr, "%s\n", ssh_get_error(sshbind));
        }
        /* Since the session has been passed to a child fork, do some cleaning
         * up at the parent process. */
        ssh_disconnect(session);
        ssh_free(session);
    }

exit:
    ssh_bind_free(sshbind);
    ssh_finalize();
    return 0;
}
