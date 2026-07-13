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

#define LIBSSH_STATIC

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>

#include "torture.h"
#include "torture_key.h"
#include "libssh/libssh.h"
#include "libssh/priv.h"
#include "libssh/session.h"

#include "test_server.h"
#include "default_cb.h"

#define TORTURE_KNOWN_HOSTS_FILE "libssh_torture_knownhosts"

const char template[] = "temp_dir_XXXXXX";

struct test_server_st {
    struct torture_state *state;
    char *cwd;
    char *temp_dir;
};

static int libssh_server_setup(void **state)
{
    struct test_server_st *tss = NULL;
    struct torture_state *s = NULL;

    char log_file[1024];

    assert_non_null(state);

    tss = (struct test_server_st*)calloc(1, sizeof(struct test_server_st));
    assert_non_null(tss);

    torture_setup_socket_dir((void **)&s);
    torture_setup_create_libssh_config((void **)&s);

    snprintf(log_file,
             sizeof(log_file),
             "%s/sshd/log",
             s->socket_dir);

    s->log_file = strdup(log_file);

    /* The second argument is the relative path to the "server" directory binary
     */
    torture_setup_libssh_server((void **)&s, "./test_server/test_server");
    assert_non_null(s);

    tss->state = s;

    *state = tss;

    return 0;
}

static int sshd_teardown(void **state) {

    struct test_server_st *tss = NULL;
    struct torture_state *s = NULL;

    assert_non_null(state);

    tss = *state;
    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    /* This function can be reused to teardown the server */
    torture_teardown_sshd_server((void **)&s);

    SAFE_FREE(tss);

    return 0;
}

static int session_setup(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    int verbosity = torture_libssh_verbosity();
    struct passwd *pwd;
    char *cwd = NULL;
    char *tmp_dir = NULL;
    bool b = false;
    int rc;

    assert_non_null(tss);

    /* Make sure we do not test the agent */
    unsetenv("SSH_AUTH_SOCK");

    cwd = torture_get_current_working_dir();
    assert_non_null(cwd);

    tmp_dir = torture_make_temp_dir(template);
    assert_non_null(tmp_dir);

    tss->cwd = cwd;
    tss->temp_dir = tmp_dir;

    s = tss->state;
    assert_non_null(s);

    pwd = getpwnam("bob");
    assert_non_null(pwd);

    rc = setuid(pwd->pw_uid);
    assert_return_code(rc, errno);

    s->ssh.session = ssh_new();
    assert_non_null(s->ssh.session);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    assert_ssh_return_code(s->ssh.session, rc);
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_HOST, TORTURE_SSH_SERVER);
    assert_ssh_return_code(s->ssh.session, rc);
    /* Make sure no other configuration options from system will get used */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_PROCESS_CONFIG, &b);
    assert_ssh_return_code(s->ssh.session, rc);

    return 0;
}

static int session_teardown(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    int rc = 0;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    rc = torture_change_dir(tss->cwd);
    assert_int_equal(rc, 0);

    rc = torture_rmdirs(tss->temp_dir);
    assert_int_equal(rc, 0);

    SAFE_FREE(tss->temp_dir);
    SAFE_FREE(tss->cwd);

    return 0;
}

static void torture_server_auth_none(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s = NULL;
    ssh_session session = NULL;
    char *banner = NULL;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    rc = ssh_options_set(session, SSH_OPTIONS_USER, TORTURE_SSH_USER_BOB);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_none(session, NULL);
    assert_int_equal(rc, SSH_AUTH_DENIED);

    banner = ssh_get_issue_banner(session);
    assert_string_equal(banner, SSHD_BANNER_MESSAGE);
    free(banner);
    banner = NULL;

    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
}

static void torture_server_auth_password(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    ssh_session session;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    /* TODO: implement proper pam authentication in callback */
    /* Using the default user for the server */
    rc = ssh_options_set(session, SSH_OPTIONS_USER, SSHD_DEFAULT_USER);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_none(session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_AUTH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PASSWORD);

    /* TODO: implement proper pam authentication in callback */
    /* Using the default password for the server */
    rc = ssh_userauth_password(session, NULL, SSHD_DEFAULT_PASSWORD);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
}

static void torture_server_auth_pubkey(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    ssh_session session;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    /* Authenticate as alice with bob's pubkey */
    rc = ssh_options_set(session, SSH_OPTIONS_USER, TORTURE_SSH_USER_ALICE);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_none(session,NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
}

static void torture_server_hostkey_mismatch(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s = NULL;
    ssh_session session = NULL;
    char known_hosts_file[1024] = {0};
    FILE *file = NULL;
    enum ssh_known_hosts_e found;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    /* Store the testkey in the knownhosts file */
    snprintf(known_hosts_file,
             sizeof(known_hosts_file),
             "%s/%s",
             s->socket_dir,
             TORTURE_KNOWN_HOSTS_FILE);

    file = fopen(known_hosts_file, "w");
    assert_non_null(file);
    fprintf(file,
            "127.0.0.10 %s\n",
            torture_get_testkey_pub(SSH_KEYTYPE_RSA));
    fclose(file);

    rc = ssh_options_set(session, SSH_OPTIONS_KNOWNHOSTS, known_hosts_file);
    assert_ssh_return_code(session, rc);
    /* Using the default user for the server */
    rc = ssh_options_set(session, SSH_OPTIONS_USER, SSHD_DEFAULT_USER);
    assert_ssh_return_code(session, rc);

    /* Configure the client to offer only rsa-sha2-256 hostkey algorithm */
    rc = ssh_options_set(session, SSH_OPTIONS_HOSTKEYS, "rsa-sha2-256");
    assert_ssh_return_code(session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    /* Make sure we can verify the signature */
    found = ssh_session_is_known_server(session);
    assert_int_equal(found, SSH_KNOWN_HOSTS_OK);
}

static void torture_server_unknown_global_request(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s = NULL;
    ssh_session session = NULL;
    ssh_channel channel;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    rc = ssh_options_set(session, SSH_OPTIONS_USER, SSHD_DEFAULT_USER);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    /* Using the default password for the server */
    rc = ssh_userauth_password(session, NULL, SSHD_DEFAULT_PASSWORD);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    /* Request asking for reply */
    rc = ssh_global_request(session, "unknown-request-00@test.com", NULL, 1);
    assert_ssh_return_code_equal(session, rc, SSH_ERROR);

    /* Request and don't ask for reply */
    rc = ssh_global_request(session, "another-bad-req-00@test.com", NULL, 0);
    assert_ssh_return_code(session, rc);

    /* Open channel to make sure the session is still working */
    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    ssh_channel_close(channel);
}

static void torture_server_no_more_sessions(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s = NULL;
    ssh_session session = NULL;
    ssh_channel channels[2];
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    rc = ssh_options_set(session, SSH_OPTIONS_USER, SSHD_DEFAULT_USER);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    /* Using the default password for the server */
    rc = ssh_userauth_password(session, NULL, SSHD_DEFAULT_PASSWORD);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    /* Open a channel session */
    channels[0] = ssh_channel_new(session);
    assert_non_null(channels[0]);

    rc = ssh_channel_open_session(channels[0]);
    assert_ssh_return_code(session, rc);

    /* Send no-more-sessions@openssh.com global request */
    rc = ssh_request_no_more_sessions(session);
    assert_ssh_return_code(session, rc);

    /* Try to open an extra session and expect failure */
    channels[1] = ssh_channel_new(session);
    assert_non_null(channels[1]);

    rc = ssh_channel_open_session(channels[1]);
    assert_int_equal(rc, SSH_ERROR);

    /* Free the unused channel */
    ssh_channel_close(channels[1]);
    ssh_channel_free(channels[1]);

    /* Close and free open channel */
    ssh_channel_close(channels[0]);
    ssh_channel_free(channels[0]);
}

static void torture_server_set_disconnect_message(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s = NULL;
    ssh_session session;
    int rc;
    const char *message = "Goodbye";

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    rc = ssh_session_set_disconnect_message(session,message);
    assert_ssh_return_code(session, rc);
    assert_string_equal(session->disconnect_message,message);
}

static void torture_null_server_set_disconnect_message(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s = NULL;
    ssh_session session;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    rc = ssh_session_set_disconnect_message(NULL,"Goodbye");
    assert_int_equal(rc, SSH_ERROR);
}

static void torture_server_set_null_disconnect_message(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s = NULL;
    ssh_session session;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    rc = ssh_session_set_disconnect_message(session,NULL);
    assert_int_equal(rc, SSH_OK);
    assert_string_equal(session->disconnect_message,"Bye Bye");
}

int torture_run_tests(void) {
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_server_auth_none,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_auth_password,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_auth_pubkey,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_hostkey_mismatch,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_unknown_global_request,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_no_more_sessions,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_set_disconnect_message,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_null_server_set_disconnect_message,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_set_null_disconnect_message,
                                        session_setup,
                                        session_teardown),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, libssh_server_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
