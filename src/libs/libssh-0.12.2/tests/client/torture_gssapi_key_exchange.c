#include "config.h"

#define LIBSSH_STATIC

#include "libssh/crypto.h"
#include "torture.h"
#include <libssh/libssh.h>

#include <errno.h>
#include <fcntl.h>
#include <gssapi.h>
#include <pwd.h>

static int sshd_setup(void **state)
{
    torture_setup_sshd_server(state, false);
    torture_update_sshd_config(state,
                               "GSSAPIAuthentication yes\n"
                               "GSSAPIKeyExchange yes\n");

    return 0;
}

static int sshd_teardown(void **state)
{
    assert_non_null(state);

    torture_teardown_sshd_server(state);

    return 0;
}

static int session_setup(void **state)
{
    struct torture_state *s = *state;
    int verbosity = torture_libssh_verbosity();
    struct passwd *pwd = NULL;
    int rc;
    bool b = false;

    pwd = getpwnam("bob");
    assert_non_null(pwd);

    rc = setuid(pwd->pw_uid);
    assert_return_code(rc, errno);

    s->ssh.session = ssh_new();
    assert_non_null(s->ssh.session);

    ssh_options_set(s->ssh.session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    ssh_options_set(s->ssh.session, SSH_OPTIONS_HOST, TORTURE_SSH_SERVER);

    ssh_options_set(s->ssh.session, SSH_OPTIONS_USER, TORTURE_SSH_USER_ALICE);

    /* Make sure no other configuration options from system will get used */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_PROCESS_CONFIG, &b);
    assert_ssh_return_code(s->ssh.session, rc);

    return 0;
}

static int session_teardown(void **state)
{
    struct torture_state *s = *state;

    assert_non_null(s);

    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    return 0;
}

static void torture_gssapi_key_exchange(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    enum ssh_known_hosts_e known_hosts_state;
    int rc;
    bool t = true;

    /* Valid */
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_GSSAPI_KEY_EXCHANGE, &t);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    assert_true(ssh_session_kex_is_gss(session));

    known_hosts_state = ssh_session_is_known_server(session);
    assert_int_equal(known_hosts_state, SSH_KNOWN_HOSTS_UNKNOWN);

    torture_teardown_kdc_server(state);
}

static void torture_gssapi_key_exchange_no_tgt(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;
    bool t = true;

    /* Don't run kinit */
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        /* No TGT */
        "");

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_GSSAPI_KEY_EXCHANGE, &t);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    assert_false(ssh_session_kex_is_gss(session));

    torture_teardown_kdc_server(state);
}

static void torture_gssapi_key_exchange_alg(void **state,
                                            const char *kex_string,
                                            enum ssh_key_exchange_e kex_type)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    enum ssh_known_hosts_e known_hosts_state;
    int rc;
    bool t = true;

    /* Valid */
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_GSSAPI_KEY_EXCHANGE, &t);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_options_set(s->ssh.session,
                         SSH_OPTIONS_GSSAPI_KEY_EXCHANGE_ALGS,
                         kex_string);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    assert_int_equal(session->current_crypto->kex_type, kex_type);

    known_hosts_state = ssh_session_is_known_server(session);
    assert_int_equal(known_hosts_state, SSH_KNOWN_HOSTS_UNKNOWN);

    torture_teardown_kdc_server(state);
}

static void torture_gssapi_key_exchange_gss_group14_sha256(void **state)
{
    torture_gssapi_key_exchange_alg(state,
                                    "gss-group14-sha256-",
                                    SSH_GSS_KEX_DH_GROUP14_SHA256);
}

static void torture_gssapi_key_exchange_gss_group16_sha512(void **state)
{
    torture_gssapi_key_exchange_alg(state,
                                    "gss-group16-sha512-",
                                    SSH_GSS_KEX_DH_GROUP16_SHA512);
}

static void torture_gssapi_key_exchange_gss_nistp256_sha256(void **state)
{
    torture_gssapi_key_exchange_alg(state,
                                    "gss-nistp256-sha256-",
                                    SSH_GSS_KEX_ECDH_NISTP256_SHA256);
}

static void torture_gssapi_key_exchange_gss_curve25519_sha256(void **state)
{
    if (ssh_fips_mode()) {
        skip();
    }
    torture_gssapi_key_exchange_alg(state,
                                    "gss-curve25519-sha256-",
                                    SSH_GSS_KEX_CURVE25519_SHA256);
}

static void torture_gssapi_key_exchange_auth(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    enum ssh_known_hosts_e known_hosts_state;
    int rc;
    bool t = true;

    /* Valid */
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_GSSAPI_KEY_EXCHANGE, &t);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    rc = ssh_userauth_gssapi_keyex(session);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    known_hosts_state = ssh_session_is_known_server(session);
    assert_int_equal(known_hosts_state, SSH_KNOWN_HOSTS_UNKNOWN);

    torture_teardown_kdc_server(state);
}

static void torture_gssapi_key_exchange_auth_bad_user(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;
    bool t = true;

    /* Valid */
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_GSSAPI_KEY_EXCHANGE, &t);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_USER, TORTURE_SSH_USER_BOB);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    rc = ssh_userauth_gssapi_keyex(session);
    assert_int_equal(rc, SSH_AUTH_DENIED);

    torture_teardown_kdc_server(state);
}

static void torture_gssapi_key_exchange_no_auth(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;
    bool f = false;

    /* Valid */
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");

    /* Don't do GSSAPI Key Exchange */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_GSSAPI_KEY_EXCHANGE, &f);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    assert_false(ssh_session_kex_is_gss(session));

    /* Still try to do "gssapi-keyex" auth */
    rc = ssh_userauth_gssapi_keyex(session);
    assert_int_equal(rc, SSH_AUTH_ERROR);

    torture_teardown_kdc_server(state);
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_gssapi_key_exchange,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_gssapi_key_exchange_no_tgt,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(
            torture_gssapi_key_exchange_gss_group14_sha256,
            session_setup,
            session_teardown),
        cmocka_unit_test_setup_teardown(
            torture_gssapi_key_exchange_gss_group16_sha512,
            session_setup,
            session_teardown),
        cmocka_unit_test_setup_teardown(
            torture_gssapi_key_exchange_gss_nistp256_sha256,
            session_setup,
            session_teardown),
        cmocka_unit_test_setup_teardown(
            torture_gssapi_key_exchange_gss_curve25519_sha256,
            session_setup,
            session_teardown),
        cmocka_unit_test_setup_teardown(torture_gssapi_key_exchange_auth,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(
            torture_gssapi_key_exchange_auth_bad_user,
            session_setup,
            session_teardown),
        cmocka_unit_test_setup_teardown(torture_gssapi_key_exchange_no_auth,
                                        session_setup,
                                        session_teardown),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
