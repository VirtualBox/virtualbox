/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2010 by Aris Adamantiadis
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
#include <sys/types.h>
#include <pwd.h>

/* agent_is_running */
#include "agent.c"

#define LIBSSH_RSA_TESTKEY  "id_pkcs11_rsa"
#define LIBSSH_ECDSA_256_TESTKEY  "id_pkcs11_ecdsa_256"
#define LIBSSH_ECDSA_384_TESTKEY  "id_pkcs11_ecdsa_384"
#define LIBSSH_ECDSA_521_TESTKEY  "id_pkcs11_ecdsa_521"

const char template[] = "/tmp/temp_dir_XXXXXX";

struct pki_st {
    char *temp_dir;
    char *orig_dir;
    char *keys_dir;
};

static int setup_tokens(void **state, const char *type, const char *obj_name)
{
    struct torture_state *s = *state;
    struct pki_st *test_state = s->private_data;
    char priv_filename[1024];
    char *cwd = NULL;

    cwd = test_state->temp_dir;
    assert_non_null(cwd);

    snprintf(priv_filename, sizeof(priv_filename), "%s%s", test_state->keys_dir, type);

    torture_setup_tokens(cwd, priv_filename, obj_name, "1");

    return 0;
}
static int session_setup(void **state)
{
    int verbosity = torture_libssh_verbosity();
    struct torture_state *s = *state;
    struct passwd *pwd;
    bool b = false;
    int rc;

    pwd = getpwnam("bob");
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

    /* Make sure we do not interfere with another ssh-agent */
    unsetenv("SSH_AUTH_SOCK");
    unsetenv("SSH_AGENT_PID");

    return 0;
}

static int session_teardown(void **state)
{
    struct torture_state *s = *state;

    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    return 0;
}

static int setup_pkcs11(void **state)
{
    struct torture_state *s = *state;
    struct pki_st *test_state = NULL;
    int rc;
    char keys_dir[1024] = {0};
    char *temp_dir;

    test_state = malloc(sizeof(struct pki_st));
    assert_non_null(test_state);

    s->private_data = test_state;

    test_state->orig_dir = torture_get_current_working_dir();
    assert_non_null(test_state->orig_dir);

    temp_dir = torture_make_temp_dir(template);
    assert_non_null(temp_dir);

    rc = torture_change_dir(temp_dir);
    assert_int_equal(rc, 0);

    test_state->temp_dir = torture_get_current_working_dir();
    assert_non_null(test_state->temp_dir);

    snprintf(keys_dir, sizeof(keys_dir), "%s/tests/keys/pkcs11/", SOURCEDIR);

    test_state->keys_dir = strdup(keys_dir);

    setup_tokens(state, LIBSSH_RSA_TESTKEY, "rsa");
    setup_tokens(state, LIBSSH_ECDSA_256_TESTKEY, "ecdsa256");
    setup_tokens(state, LIBSSH_ECDSA_384_TESTKEY, "ecdsa384");
    setup_tokens(state, LIBSSH_ECDSA_521_TESTKEY, "ecdsa521");

    return 0;
}

static int sshd_setup(void **state)
{

    torture_setup_sshd_server(state, true);
    setup_pkcs11(state);

    return 0;
}

static int sshd_teardown(void **state) {

    struct torture_state *s = *state;
    struct pki_st *test_state = s->private_data;
    int rc;

    if (test_state != NULL) {
        torture_cleanup_tokens(test_state->temp_dir);

        rc = torture_change_dir(test_state->orig_dir);
        assert_int_equal(rc, 0);

        rc = torture_rmdirs(test_state->temp_dir);
        assert_int_equal(rc, 0);

        SAFE_FREE(test_state->temp_dir);
        SAFE_FREE(test_state->orig_dir);
        SAFE_FREE(test_state->keys_dir);
        SAFE_FREE(test_state);
    }

    torture_teardown_sshd_server(state);

    return 0;
}

static void torture_auth_autopubkey(void **state, const char *obj_name, const char *pin) {
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;
    char priv_uri[1042];

    /* Authenticate as charlie with bob his pubkey */
    rc = ssh_options_set(session, SSH_OPTIONS_USER, TORTURE_SSH_USER_CHARLIE);
    assert_int_equal(rc, SSH_OK);

    snprintf(priv_uri, sizeof(priv_uri), "pkcs11:token=%s;object=%s;type=private?pin-value=%s",
            obj_name, obj_name, pin);

    rc = ssh_options_set(session, SSH_OPTIONS_IDENTITY, priv_uri);
    assert_int_equal(rc, SSH_OK);
    assert_string_equal(session->opts.identity_non_exp->root->data, priv_uri);

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

static void torture_auth_autopubkey_rsa(void **state) {
    torture_auth_autopubkey(state, "rsa", "1234");
}

static void torture_auth_autopubkey_ecdsa_key_256(void **state) {
    torture_auth_autopubkey(state, "ecdsa256", "1234");
}

static void torture_auth_autopubkey_ecdsa_key_384(void **state) {
    torture_auth_autopubkey(state, "ecdsa384", "1234");
}

static void torture_auth_autopubkey_ecdsa_key_521(void **state) {
    torture_auth_autopubkey(state, "ecdsa521", "1234");
}

int torture_run_tests(void) {
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_auth_autopubkey_rsa,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_autopubkey_ecdsa_key_256,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_autopubkey_ecdsa_key_384,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_auth_autopubkey_ecdsa_key_521,
                                        session_setup,
                                        session_teardown),
    };

    /* Do not use system openssl.cnf for the pkcs11 uri tests.
     * It can load a pkcs11 provider too early before we will set up environment
     * variables that are needed for the pkcs11 provider to access correct
     * tokens, causing unexpected failures.
     * Make sure this comes before ssh_init(), which initializes OpenSSL!
     */
    setenv("OPENSSL_CONF", "/dev/null", 1);

    ssh_init();
    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
