/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2021 by Anderson Toshiyuki Sasaki - Red Hat, Inc.
 *
 * The SSH Library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the SSH Library; see the file COPYING. If not,
 * see <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "torture.h"
#include "libssh/libssh.h"
#include "libssh/priv.h"
#include "libssh/session.h"

#include <errno.h>
#include <sys/types.h>
#include <pwd.h>

#include "chacha20_override.h"
#include "poly1305_override.h"
#include "curve25519_override.h"
#include "ed25519_override.h"

const char template[] = "temp_dir_XXXXXX";

struct test_st {
    char *temp_dir;
    char *orig_dir;
};

static int sshd_setup(void **state)
{
    struct torture_state *s;
    struct test_st *test_state = NULL;
    char *temp_dir;
    int rc;

    torture_setup_sshd_server(state, false);

    test_state = malloc(sizeof(struct test_st));
    assert_non_null(test_state);

    s = *((struct torture_state **)state);
    s->private_data = test_state;

    test_state->orig_dir = torture_get_current_working_dir();
    assert_non_null(test_state->orig_dir);

    temp_dir = torture_make_temp_dir(template);
    assert_non_null(temp_dir);

    rc = torture_change_dir(temp_dir);
    assert_int_equal(rc, 0);

    test_state->temp_dir = temp_dir;

    return 0;
}

static int sshd_teardown(void **state)
{
    struct torture_state *s = *state;
    struct test_st *test_state = s->private_data;
    int rc;

    rc = torture_change_dir(test_state->orig_dir);
    assert_int_equal(rc, 0);

    rc = torture_rmdirs(test_state->temp_dir);
    assert_int_equal(rc, 0);

    SAFE_FREE(test_state->temp_dir);
    SAFE_FREE(test_state->orig_dir);
    SAFE_FREE(test_state);

    torture_teardown_sshd_server(state);

    return 0;
}

static int session_setup(void **state)
{
    struct torture_state *s = *state;
    int verbosity = torture_libssh_verbosity();
    struct passwd *pwd;
    bool false_v = false;
    int rc;

    pwd = getpwnam("bob");
    assert_non_null(pwd);

    rc = setuid(pwd->pw_uid);
    assert_return_code(rc, errno);

    s->ssh.session = ssh_new();
    assert_non_null(s->ssh.session);

    ssh_options_set(s->ssh.session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    ssh_options_set(s->ssh.session, SSH_OPTIONS_HOST, TORTURE_SSH_SERVER);
    /* Prevent parsing configuration files that can introduce different
     * algorithms then we want to test */
    ssh_options_set(s->ssh.session, SSH_OPTIONS_PROCESS_CONFIG, &false_v);

    reset_chacha20_function_called();
    reset_poly1305_function_called();
    reset_curve25519_function_called();
    reset_ed25519_function_called();

    return 0;
}

static int session_teardown(void **state)
{
    struct torture_state *s = *state;

    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    return 0;
}

static void test_algorithm(ssh_session session,
                           const char *kex,
                           const char *cipher,
                           const char *hostkey)
{
    char data[256] = {0};
    int rc;

    if (kex != NULL) {
        rc = ssh_options_set(session, SSH_OPTIONS_KEY_EXCHANGE, kex);
        assert_ssh_return_code(session, rc);
    }

    if (cipher != NULL) {
        rc = ssh_options_set(session, SSH_OPTIONS_CIPHERS_C_S, cipher);
        assert_ssh_return_code(session, rc);
        rc = ssh_options_set(session, SSH_OPTIONS_CIPHERS_S_C, cipher);
        assert_ssh_return_code(session, rc);
    }

    if (hostkey != NULL) {
        rc = ssh_options_set(session, SSH_OPTIONS_HOSTKEYS, hostkey);
        assert_ssh_return_code(session, rc);
    }

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    /* send ignore packets of all sizes */
    memset(data, 'A', sizeof(data) - 1);
    ssh_send_ignore(session, data);
    ssh_handle_packets(session, 50);

    rc = ssh_userauth_none(session, NULL);
    if (rc != SSH_OK) {
        rc = ssh_get_error_code(session);
        assert_int_equal(rc, SSH_REQUEST_DENIED);
    }

    ssh_disconnect(session);
}

#ifdef OPENSSH_CHACHA20_POLY1305_OPENSSH_COM
static void torture_override_chacha20_poly1305(void **state)
{
    struct torture_state *s = *state;

    bool internal_chacha20_called;
    bool internal_poly1305_called;

    if (ssh_fips_mode()) {
        skip();
    }

    test_algorithm(s->ssh.session,
                   NULL, /* kex */
                   "chacha20-poly1305@openssh.com",
                   NULL  /* hostkey */);

    internal_chacha20_called = internal_chacha20_function_called();
    internal_poly1305_called = internal_poly1305_function_called();

#if SHOULD_CALL_INTERNAL_CHACHAPOLY
    assert_true(internal_chacha20_called);
    assert_true(internal_poly1305_called);
#else
    assert_false(internal_chacha20_called ||
                 internal_poly1305_called);
#endif

}
#endif /* OPENSSH_CHACHA20_POLY1305_OPENSSH_COM */

#ifdef OPENSSH_CURVE25519_SHA256
static void torture_override_ecdh_curve25519_sha256(void **state)
{
    struct torture_state *s = *state;
    bool internal_curve25519_called;

    if (ssh_fips_mode()) {
        skip();
    }

    test_algorithm(s->ssh.session,
                   "curve25519-sha256",
                   NULL, /* cipher */
                   NULL  /* hostkey */);

    internal_curve25519_called = internal_curve25519_function_called();

#if SHOULD_CALL_INTERNAL_CURVE25519
    assert_true(internal_curve25519_called);
#else
    assert_false(internal_curve25519_called);
#endif
}
#endif /* OPENSSH_CURVE25519_SHA256 */

#ifdef OPENSSH_CURVE25519_SHA256_LIBSSH_ORG
static void torture_override_ecdh_curve25519_sha256_libssh_org(void **state)
{
    struct torture_state *s = *state;
    bool internal_curve25519_called;

    if (ssh_fips_mode()) {
        skip();
    }

    test_algorithm(s->ssh.session,
                   "curve25519-sha256@libssh.org",
                   NULL, /* cipher */
                   NULL  /* hostkey */);

    internal_curve25519_called = internal_curve25519_function_called();

#if SHOULD_CALL_INTERNAL_CURVE25519
    assert_true(internal_curve25519_called);
#else
    assert_false(internal_curve25519_called);
#endif
}
#endif /* OPENSSH_CURVE25519_SHA256_LIBSSH_ORG */

#ifdef OPENSSH_SSH_ED25519
static void torture_override_ed25519(void **state)
{
    struct torture_state *s = *state;
    bool internal_ed25519_called;

    if (ssh_fips_mode()) {
        skip();
    }

    test_algorithm(s->ssh.session,
                   NULL, /* kex */
                   NULL, /* cipher */
                   "ssh-ed25519");

    internal_ed25519_called = internal_ed25519_function_called();

#if SHOULD_CALL_INTERNAL_ED25519
    assert_true(internal_ed25519_called);
#else
    assert_false(internal_ed25519_called);
#endif
}
#endif /* OPENSSH_SSH_ED25519 */

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
#ifdef OPENSSH_CHACHA20_POLY1305_OPENSSH_COM
        cmocka_unit_test_setup_teardown(torture_override_chacha20_poly1305,
                                        session_setup,
                                        session_teardown),
#endif /* OPENSSH_CHACHA20_POLY1305_OPENSSH_COM */
#ifdef OPENSSH_CURVE25519_SHA256
        cmocka_unit_test_setup_teardown(torture_override_ecdh_curve25519_sha256,
                                        session_setup,
                                        session_teardown),
#endif /* OPENSSH_CURVE25519_SHA256 */
#ifdef OPENSSH_CURVE25519_SHA256_LIBSSH_ORG
        cmocka_unit_test_setup_teardown(torture_override_ecdh_curve25519_sha256_libssh_org,
                                        session_setup,
                                        session_teardown),
#endif /* OPENSSH_CURVE25519_SHA256_LIBSSH_ORG */
#ifdef OPENSSH_SSH_ED25519
        cmocka_unit_test_setup_teardown(torture_override_ed25519,
                                        session_setup,
                                        session_teardown),
#endif /* OPENSSH_SSH_ED25519 */
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    if (rc != 0) {
        return rc;
    }

    ssh_finalize();

    return rc;
}
