/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2010 by Aris Adamantiadis
 * Copyright (c) 2023 by Jakub Jelen
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

#define LIBSSH_STATIC

#include "torture.h"
#include "libssh/libssh.h"
#include "libssh/priv.h"
#include "libssh/session.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <pwd.h>

#include "torture_auth_common.c"

static int sshd_setup(void **state)
{
    torture_setup_sshd_server(state, true);

    return 0;
}

static int sshd_teardown(void **state) {
    torture_teardown_sshd_server(state);

    return 0;
}

static int session_setup(void **state)
{
    struct torture_state *s = *state;
    int verbosity = torture_libssh_verbosity();
    const char *all_keytypes = NULL;
    struct passwd *pwd = NULL;
    bool b = false;
    int rc;

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    rc = setuid(pwd->pw_uid);
    assert_return_code(rc, errno);

    s->ssh.session = ssh_new();
    assert_non_null(s->ssh.session);

    ssh_options_set(s->ssh.session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    ssh_options_set(s->ssh.session, SSH_OPTIONS_HOST, TORTURE_SSH_SERVER);
    /* Make sure no other configuration options from system will get used */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_PROCESS_CONFIG, &b);
    assert_ssh_return_code(s->ssh.session, rc);

    /* Enable all hostkeys */
    all_keytypes = ssh_kex_get_supported_method(SSH_HOSTKEYS);
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_PUBLICKEY_ACCEPTED_TYPES, all_keytypes);
    assert_ssh_return_code(s->ssh.session, rc);

    /* certs have been signed for login as alice */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_USER, TORTURE_SSH_USER_ALICE);
    assert_int_equal(rc, SSH_OK);

    /* Make sure we do not interfere with another ssh-agent */
    unsetenv("SSH_AUTH_SOCK");
    unsetenv("SSH_AGENT_PID");

    return 0;
}

/* This sets up the ssh session in the directory without the default
 * certificates that are used for authentication, requiring them to be provided
 * as configuration options or from agent explicitly. */
static int session_setup_ssh_dir(void **state)
{
    struct torture_state *s = *state;
    const char *no_home = "~/.no_ssh";
    int rc;

    session_setup(state);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_SSH_DIR, no_home);
    assert_ssh_return_code(s->ssh.session, rc);

    return 0;
}

static int session_teardown(void **state)
{
    struct torture_state *s = *state;

    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    return 0;
}

static int agent_setup(void **state)
{
    struct torture_state *s = *state;
    char ssh_agent_cmd[4096];
    char ssh_agent_sock[1024];
    char ssh_agent_pidfile[1024];
    char ssh_key_add[1024];
    struct passwd *pwd;
    int rc;

    rc = session_setup(state);
    if (rc != 0) {
        return rc;
    }

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(ssh_agent_sock,
             sizeof(ssh_agent_sock),
             "%s/agent.sock",
             s->socket_dir);

    snprintf(ssh_agent_pidfile,
             sizeof(ssh_agent_pidfile),
             "%s/agent.pid",
             s->socket_dir);

    /* Production ready code!!! */
    snprintf(ssh_agent_cmd,
             sizeof(ssh_agent_cmd),
             "eval `ssh-agent -a %s`; echo $SSH_AGENT_PID > %s",
             ssh_agent_sock, ssh_agent_pidfile);

    /* run ssh-agent and ssh-add as the normal user */
    unsetenv("UID_WRAPPER_ROOT");

    rc = system(ssh_agent_cmd);
    assert_return_code(rc, errno);

    setenv("SSH_AUTH_SOCK", ssh_agent_sock, 1);
    setenv("TORTURE_SSH_AGENT_PIDFILE", ssh_agent_pidfile, 1);

    snprintf(ssh_key_add,
             sizeof(ssh_key_add),
             "ssh-add %s/.ssh/id_rsa",
             pwd->pw_dir);

    rc = system(ssh_key_add);
    assert_return_code(rc, errno);

    return 0;
}

static int agent_cert_setup(void **state)
{
    char doe_alt_ssh_key[1024];
    struct passwd *pwd;
    int rc;

    rc = agent_setup(state);
    if (rc != 0) {
        return rc;
    }

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    /* remove all keys, load alternative key + cert */
    snprintf(doe_alt_ssh_key,
             sizeof(doe_alt_ssh_key),
             "ssh-add -D && ssh-add %s/.ssh/id_rsa",
             pwd->pw_dir);

    rc = system(doe_alt_ssh_key);
    assert_return_code(rc, errno);

    return 0;
}

static int agent_teardown(void **state)
{
    const char *ssh_agent_pidfile;
    int rc;

    rc = session_teardown(state);
    if (rc != 0) {
        return rc;
    }

    ssh_agent_pidfile = getenv("TORTURE_SSH_AGENT_PIDFILE");
    assert_non_null(ssh_agent_pidfile);

    /* kill agent pid */
    rc = torture_terminate_process(ssh_agent_pidfile);
    assert_return_code(rc, errno);

    unlink(ssh_agent_pidfile);

    unsetenv("TORTURE_SSH_AGENT_PIDFILE");
    unsetenv("SSH_AUTH_SOCK");

    return 0;
}

static void torture_auth_cert(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_key privkey = NULL;
    ssh_key cert = NULL;
    char doe_ssh_key[1024];
    char doe_ssh_cert[2048];
    struct passwd *pwd;
    int rc;

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_key,
             sizeof(doe_ssh_key),
             "%s/.ssh/id_rsa",
             pwd->pw_dir);
    snprintf(doe_ssh_cert,
             sizeof(doe_ssh_cert),
             "%s-cert.pub",
             doe_ssh_key);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_pki_import_privkey_file(doe_ssh_key, NULL, NULL, NULL, &privkey);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_pki_import_cert_file(doe_ssh_cert, &cert);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_pki_copy_cert_to_privkey(cert, privkey);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_try_publickey(session, NULL, cert);
    assert_ssh_return_code(session, rc);

    rc = ssh_userauth_publickey(session, NULL, privkey);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    SSH_KEY_FREE(privkey);
    SSH_KEY_FREE(cert);
}

static void torture_auth_cert_nonblocking(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_key privkey = NULL;
    ssh_key cert = NULL;
    char doe_ssh_key[1024];
    char doe_ssh_cert[2048];
    struct passwd *pwd;
    int rc;

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_key,
             sizeof(doe_ssh_key),
             "%s/.ssh/id_rsa",
             pwd->pw_dir);
    snprintf(doe_ssh_cert,
             sizeof(doe_ssh_cert),
             "%s-cert.pub",
             doe_ssh_key);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    ssh_set_blocking(session, 0);

    rc = ssh_pki_import_privkey_file(doe_ssh_key, NULL, NULL, NULL, &privkey);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_pki_import_cert_file(doe_ssh_cert, &cert);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_pki_copy_cert_to_privkey(cert, privkey);
    assert_int_equal(rc, SSH_OK);

    do {
        rc = ssh_userauth_try_publickey(session, NULL, cert);
    } while (rc == SSH_AUTH_AGAIN);
    assert_ssh_return_code(session, rc);

    do {
        rc = ssh_userauth_publickey(session, NULL, privkey);
    } while (rc == SSH_AUTH_AGAIN);

    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    SSH_KEY_FREE(privkey);
    SSH_KEY_FREE(cert);
}

/* Same as torture_auth_cert, but without explicitly loading certificate to the
 * private key file, keeping libssh to use default cert path when done with
 * _auto(). */
static void torture_auth_cert_default_non_explicit(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;

    /* the cert is in the default location (~/.ssh/id_rsa-cert.pub) */

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
}

/* Same as torture_auth_cert_nonblocking, but without explicitly loading
 * certificate to the private key file, keeping libssh to use default cert path
 * when done with  _auto().
 * Non-blocking version */
static void torture_auth_cert_default_non_explicit_nonblocking(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;

    /* the cert is in the default location (~/.ssh/id_rsa-cert.pub) */

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    ssh_set_blocking(session, 0);

    do {
        rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
}

/* Sanity test that there are no default identities available and the automatic
 * pubkey authentication fails without any explicit identities */
static void torture_auth_auto_fail(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_DENIED);
}

/* Sanity test that there are no default identities available and the automatic
 * pubkey authentication fails without any explicit identities
 * Non-blocking version */
static void torture_auth_auto_fail_nonblocking(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    ssh_set_blocking(session, 0);

    do {
        rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    assert_int_equal(rc, SSH_AUTH_DENIED);
}

/* Same as torture_auth_cert, but the home SSH dir does not have any default
 * identities and they are loaded through the options, only through the private
 * key path. */
static void torture_auth_cert_options_private(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char doe_ssh_key[1024];
    struct passwd *pwd;
    int rc;

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_key,
             sizeof(doe_ssh_key),
             "%s/.ssh/id_rsa",
             pwd->pw_dir);

    /* the cert has default naming relative to the private key (*-cert.pub) */
    rc = ssh_options_set(session, SSH_OPTIONS_IDENTITY, doe_ssh_key);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
}

/* Same as torture_auth_cert, but the home SSH dir does not have any default
 * identities and they are loaded through the options, only through the private
 * key path.
 * Non-blocking version */
static void torture_auth_cert_options_private_nonblocking(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char doe_ssh_key[1024];
    struct passwd *pwd;
    int rc;

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_key,
             sizeof(doe_ssh_key),
             "%s/.ssh/id_rsa",
             pwd->pw_dir);

    /* the cert has default naming relative to the private key (*-cert.pub) */
    rc = ssh_options_set(session, SSH_OPTIONS_IDENTITY, doe_ssh_key);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    ssh_set_blocking(session, 0);

    do {
        rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
}

/* Same as torture_auth_cert, but the home SSH dir does not have any default
 * identities and they are loaded through the options, also the certificate file
 */
static void torture_auth_cert_options_cert(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char doe_ssh_key[1024];
    char doe_ssh_cert[2048];
    struct passwd *pwd;
    int rc;

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_key,
             sizeof(doe_ssh_key),
             "%s/.ssh/id_rsa",
             pwd->pw_dir);
    snprintf(doe_ssh_cert,
             sizeof(doe_ssh_cert),
             "%s-cert.pub",
             doe_ssh_key);

    /* Explicit private key and cert */
    rc = ssh_options_set(session, SSH_OPTIONS_IDENTITY, doe_ssh_key);
    assert_int_equal(rc, SSH_OK);
    rc = ssh_options_set(session, SSH_OPTIONS_CERTIFICATE, doe_ssh_cert);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
}

/* Same as torture_auth_cert, but the home SSH dir does not have any default
 * identities and they are loaded through the options, only through the private
 * key path.
 * Non-blocking version */
static void torture_auth_cert_options_cert_nonblocking(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char doe_ssh_key[1024];
    char doe_ssh_cert[2048];
    struct passwd *pwd;
    int rc;

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_key,
             sizeof(doe_ssh_key),
             "%s/.ssh/id_rsa",
             pwd->pw_dir);
    snprintf(doe_ssh_cert,
             sizeof(doe_ssh_cert),
             "%s-cert.pub",
             doe_ssh_key);

    /* Explicit private key and cert */
    rc = ssh_options_set(session, SSH_OPTIONS_IDENTITY, doe_ssh_key);
    assert_int_equal(rc, SSH_OK);
    rc = ssh_options_set(session, SSH_OPTIONS_CERTIFICATE, doe_ssh_cert);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    ssh_set_blocking(session, 0);

    do {
        rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
}

static void workaround_old_openssh_bug(void **state)
{
#if OPENSSH_VERSION_MAJOR < 8 || (OPENSSH_VERSION_MAJOR == 8 && OPENSSH_VERSION_MINOR == 0)
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;

    /* Skip this test if in FIPS mode.
     *
     * OpenSSH agent has a bug which makes it to not use SHA2 in signatures when
     * using certificates. It always uses SHA1.
     *
     * This should be removed as soon as OpenSSH agent bug is fixed.
     * (see https://gitlab.com/libssh/libssh-mirror/merge_requests/34) */
    if (ssh_fips_mode()) {
        skip();
    } else {
        /* After the bug is solved, this also should be removed */
        rc = ssh_options_set(session, SSH_OPTIONS_PUBLICKEY_ACCEPTED_TYPES,
                             "ssh-rsa-cert-v01@openssh.com");
        assert_int_equal(rc, SSH_OK);
    }
#else
    (void)state;
#endif /* OPENSSH_VERSION_MAJOR < 8.1 */
}

static void torture_auth_agent_cert(void **state)
{
    workaround_old_openssh_bug(state);

    /* Setup loads a different key, tests are exactly the same. */
    torture_auth_agent(state);
}

static void torture_auth_agent_cert_nonblocking(void **state)
{
    workaround_old_openssh_bug(state);

    torture_auth_agent_nonblocking(state);
}

static void
torture_auth_agent_cert_identities_only(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char doe_ssh_key[1024];
    struct passwd *pwd = NULL;
    bool identities_only = true;
    char *id = NULL;
    int rc;

    workaround_old_openssh_bug(state);

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_key,
             sizeof(doe_ssh_key),
             "%s/.ssh/id_rsa",
             pwd->pw_dir);

    if (!ssh_agent_is_running(session)){
        print_message("*** Agent not running. Test ignored\n");
        return;
    }

    rc = ssh_options_set(session, SSH_OPTIONS_IDENTITIES_ONLY, &identities_only);
    assert_int_equal(rc, SSH_OK);

    /* Remove the default identities */
    while ((id = ssh_list_pop_head(char *, session->opts.identity_non_exp)) != NULL) {
        SAFE_FREE(id);
    }

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_none(session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    /* Should fail as key is not in config */
    rc = ssh_userauth_agent(session, NULL);
    assert_ssh_return_code_equal(session, rc, SSH_AUTH_DENIED);

    /* Re-add a key -- the cert in default location should be loaded
     * automatically */
    rc = ssh_list_append(session->opts.identity, strdup(doe_ssh_key));
    assert_int_equal(rc, SSH_OK);

    /* Should succeed as key now in config/options */
    rc = ssh_userauth_agent(session, NULL);
    assert_ssh_return_code(session, rc);
}

static void
torture_auth_agent_cert_identities_only_nonblocking(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char doe_ssh_key[1024];
    struct passwd *pwd = NULL;
    bool identities_only = true;
    char *id = NULL;
    int rc;

    workaround_old_openssh_bug(state);

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_key,
             sizeof(doe_ssh_key),
             "%s/.ssh/id_rsa",
             pwd->pw_dir);

    if (!ssh_agent_is_running(session)){
        print_message("*** Agent not running. Test ignored\n");
        return;
    }

    rc = ssh_options_set(session, SSH_OPTIONS_IDENTITIES_ONLY, &identities_only);
    assert_int_equal(rc, SSH_OK);

    /* Remove the default identities */
    while ((id = ssh_list_pop_head(char *, session->opts.identity_non_exp)) != NULL) {
        SAFE_FREE(id);
    }

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    ssh_set_blocking(session, 0);
    do {
        rc = ssh_userauth_none(session, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    /* Should fail as key is not in config */
    do {
        rc = ssh_userauth_agent(session, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    assert_ssh_return_code_equal(session, rc, SSH_AUTH_DENIED);

    /* Re-add a key -- the cert in default location should be loaded
     * automatically */
    rc = ssh_list_append(session->opts.identity, strdup(doe_ssh_key));
    assert_int_equal(rc, SSH_OK);

    /* Should succeed as key now in config/options */
    do {
        rc = ssh_userauth_agent(session, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    assert_ssh_return_code(session, rc);
}

static int agent_cert_setup_explicit(void **state)
{
    char orig_doe_ssh_key[1024];
    char doe_ssh_key[1024];
    char keydata[2048];
    struct passwd *pwd = NULL;
    int fd ;
    int rc;

    agent_cert_setup(state);

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(orig_doe_ssh_key,
             sizeof(orig_doe_ssh_key),
             "%s/.ssh/id_rsa",
             pwd->pw_dir);

    snprintf(doe_ssh_key,
             sizeof(doe_ssh_key),
             "%s/.ssh/my_rsa",
             pwd->pw_dir);

    /* move the private key away from the default location the certificate can
     * not be loaded automatically */
    fd = open(orig_doe_ssh_key, O_RDONLY);
    assert_true(fd > 0);
    rc = read(fd, keydata, sizeof(keydata));
    assert_true(rc > 0);
    keydata[rc] = '\0';
    close(fd);
    torture_write_file(doe_ssh_key, keydata);

    return 0;
}

static void
torture_auth_agent_cert_identities_only_explicit(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char doe_ssh_key[1024];
    char doe_ssh_cert[1024];
    struct passwd *pwd = NULL;
    bool identities_only = true;
    char *id = NULL;
    int rc;

    workaround_old_openssh_bug(state);

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_key,
             sizeof(doe_ssh_key),
             "%s/.ssh/my_rsa",
             pwd->pw_dir);
    snprintf(doe_ssh_cert,
             sizeof(doe_ssh_cert),
             "%s/.ssh/id_rsa-cert.pub",
             pwd->pw_dir);

    if (!ssh_agent_is_running(session)){
        print_message("*** Agent not running. Test ignored\n");
        skip();
    }

    rc = ssh_options_set(session, SSH_OPTIONS_IDENTITIES_ONLY, &identities_only);
    assert_int_equal(rc, SSH_OK);

    /* Remove the default identities */
    while ((id = ssh_list_pop_head(char *, session->opts.identity_non_exp)) != NULL) {
        SAFE_FREE(id);
    }

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_none(session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    /* Should fail as key is not in config */
    rc = ssh_userauth_agent(session, NULL);
    assert_ssh_return_code_equal(session, rc, SSH_AUTH_DENIED);

    /* Re-add a key and cert  */
    rc = ssh_list_append(session->opts.identity, strdup(doe_ssh_key));
    assert_int_equal(rc, SSH_OK);
    rc = ssh_list_append(session->opts.certificate, strdup(doe_ssh_cert));
    assert_int_equal(rc, SSH_OK);

    /* Should succeed as key now in config/options */
    rc = ssh_userauth_agent(session, NULL);
    assert_ssh_return_code(session, rc);
}

static void
torture_auth_agent_cert_identities_only_nonblocking_explicit(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char doe_ssh_key[1024];
    char doe_ssh_cert[1024];
    struct passwd *pwd = NULL;
    bool identities_only = true;
    char *id = NULL;
    int rc;

    workaround_old_openssh_bug(state);

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_key,
             sizeof(doe_ssh_key),
             "%s/.ssh/my_rsa",
             pwd->pw_dir);
    snprintf(doe_ssh_cert,
             sizeof(doe_ssh_cert),
             "%s/.ssh/id_rsa-cert.pub",
             pwd->pw_dir);

    if (!ssh_agent_is_running(session)){
        print_message("*** Agent not running. Test ignored\n");
        skip();
    }

    rc = ssh_options_set(session, SSH_OPTIONS_IDENTITIES_ONLY, &identities_only);
    assert_int_equal(rc, SSH_OK);

    /* Remove the default identities */
    while ((id = ssh_list_pop_head(char *, session->opts.identity_non_exp)) != NULL) {
        SAFE_FREE(id);
    }

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    ssh_set_blocking(session, 0);

    do {
        rc = ssh_userauth_none(session, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    /* Should fail as key is not in config */
    do {
        rc = ssh_userauth_agent(session, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    assert_ssh_return_code_equal(session, rc, SSH_AUTH_DENIED);

    /* Re-add a key and cert  */
    rc = ssh_list_append(session->opts.identity, strdup(doe_ssh_key));
    assert_int_equal(rc, SSH_OK);
    rc = ssh_list_append(session->opts.certificate, strdup(doe_ssh_cert));
    assert_int_equal(rc, SSH_OK);

    /* Should succeed as key now in config/options */
    do {
        rc = ssh_userauth_agent(session, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    assert_ssh_return_code(session, rc);
}

static void
torture_auth_agent_cert_only_identities_only(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char doe_ssh_cert[1024];
    struct passwd *pwd = NULL;
    bool identities_only = true;
    char *id = NULL;
    int rc;

    workaround_old_openssh_bug(state);

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_cert,
             sizeof(doe_ssh_cert),
             "%s/.ssh/id_rsa-cert.pub",
             pwd->pw_dir);

    if (!ssh_agent_is_running(session)){
        print_message("*** Agent not running. Test ignored\n");
        skip();
    }

    rc = ssh_options_set(session, SSH_OPTIONS_IDENTITIES_ONLY, &identities_only);
    assert_int_equal(rc, SSH_OK);

    /* Remove the default identities */
    while ((id = ssh_list_pop_head(char *, session->opts.identity_non_exp)) != NULL) {
        SAFE_FREE(id);
    }

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_none(session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    /* Should fail as key is not in config */
    rc = ssh_userauth_agent(session, NULL);
    assert_ssh_return_code_equal(session, rc, SSH_AUTH_DENIED);

    /* Re-add a cert: key is in the agent  */
    rc = ssh_list_append(session->opts.certificate, strdup(doe_ssh_cert));
    assert_int_equal(rc, SSH_OK);

    /* Should succeed as key now in config/options */
    rc = ssh_userauth_agent(session, NULL);
    assert_ssh_return_code(session, rc);
}

static void
torture_auth_agent_cert_only_identities_only_nonblocking(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char doe_ssh_cert[1024];
    struct passwd *pwd = NULL;
    bool identities_only = true;
    char *id = NULL;
    int rc;

    workaround_old_openssh_bug(state);

    pwd = getpwnam("doe");
    assert_non_null(pwd);

    snprintf(doe_ssh_cert,
             sizeof(doe_ssh_cert),
             "%s/.ssh/id_rsa-cert.pub",
             pwd->pw_dir);

    if (!ssh_agent_is_running(session)){
        print_message("*** Agent not running. Test ignored\n");
        skip();
    }

    rc = ssh_options_set(session, SSH_OPTIONS_IDENTITIES_ONLY, &identities_only);
    assert_int_equal(rc, SSH_OK);

    /* Remove the default identities */
    while ((id = ssh_list_pop_head(char *, session->opts.identity_non_exp)) != NULL) {
        SAFE_FREE(id);
    }

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    ssh_set_blocking(session, 0);

    do {
        rc = ssh_userauth_none(session, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    /* Should fail as key is not in config */
    do {
        rc = ssh_userauth_agent(session, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    assert_ssh_return_code_equal(session, rc, SSH_AUTH_DENIED);

    /* Re-add a cert: key is in the agent  */
    rc = ssh_list_append(session->opts.certificate, strdup(doe_ssh_cert));
    assert_int_equal(rc, SSH_OK);

    /* Should succeed as key now in config/options */
    do {
        rc = ssh_userauth_agent(session, NULL);
    } while (rc == SSH_AUTH_AGAIN);
    assert_ssh_return_code(session, rc);
}

int torture_run_tests(void) {
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_auth_cert,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_cert_nonblocking,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_cert_default_non_explicit,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_cert_default_non_explicit_nonblocking,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_auto_fail,
                                        session_setup_ssh_dir,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_auto_fail_nonblocking,
                                        session_setup_ssh_dir,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_cert_options_private,
                                        session_setup_ssh_dir,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_cert_options_private_nonblocking,
                                        session_setup_ssh_dir,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_cert_options_cert,
                                        session_setup_ssh_dir,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_cert_options_cert_nonblocking,
                                        session_setup_ssh_dir,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_agent_cert,
                                        agent_cert_setup,
                                        agent_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_agent_cert_nonblocking,
                                        agent_cert_setup,
                                        agent_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_agent_cert_identities_only,
                                        agent_cert_setup,
                                        agent_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_agent_cert_identities_only_nonblocking,
                                        agent_cert_setup,
                                        agent_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_agent_cert_identities_only_explicit,
                                        agent_cert_setup_explicit,
                                        agent_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_agent_cert_identities_only_nonblocking_explicit,
                                        agent_cert_setup_explicit,
                                        agent_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_agent_cert_only_identities_only,
                                        agent_cert_setup,
                                        agent_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_agent_cert_only_identities_only_nonblocking,
                                        agent_cert_setup,
                                        agent_teardown),
    };

    ssh_init();
    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
