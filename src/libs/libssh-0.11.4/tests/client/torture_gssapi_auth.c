#include "config.h"

#define LIBSSH_STATIC

#include "torture.h"
#include <libssh/libssh.h>

#include <errno.h>
#include <fcntl.h>
#include <gssapi.h>
#include <pwd.h>

static int
sshd_setup(void **state)
{
    torture_setup_sshd_server(state, false);
    torture_update_sshd_config(state,
                               "GSSAPIAuthentication yes\n"
                               "GSSAPICleanupCredentials yes\n"
                               "GSSAPIStrictAcceptorCheck yes\n");

    return 0;
}

static int
sshd_teardown(void **state)
{
    assert_non_null(state);

    torture_teardown_sshd_server(state);

    return 0;
}

static int
session_setup(void **state)
{
    struct torture_state *s = *state;
    int verbosity = torture_libssh_verbosity();
    struct passwd *pwd = NULL;
    int rc;

    pwd = getpwnam("bob");
    assert_non_null(pwd);

    rc = setuid(pwd->pw_uid);
    assert_return_code(rc, errno);

    s->ssh.session = ssh_new();
    assert_non_null(s->ssh.session);

    ssh_options_set(s->ssh.session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    ssh_options_set(s->ssh.session, SSH_OPTIONS_HOST, TORTURE_SSH_SERVER);

    ssh_options_set(s->ssh.session, SSH_OPTIONS_USER, TORTURE_SSH_USER_ALICE);

    return 0;
}

static int
session_teardown(void **state)
{
    struct torture_state *s = *state;

    assert_non_null(s);

    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    return 0;
}

static void
torture_gssapi_auth(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    /* No client credential */
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        /* No TGT */
        "");
    rc = ssh_userauth_gssapi(session);
    assert_int_equal(rc, SSH_AUTH_DENIED);
    torture_teardown_kdc_server(state);
    /* Invalid host principal */
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/invalid.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/invalid.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");
    rc = ssh_userauth_gssapi(session);
    assert_int_equal(rc, SSH_AUTH_DENIED);
    torture_teardown_kdc_server(state);
    /* Valid */
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");
    rc = ssh_userauth_gssapi(session);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
    torture_teardown_kdc_server(state);
}

static void
torture_gssapi_auth_client_identity(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    /* Invalid client identity option */
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");
    ssh_options_set(session, SSH_OPTIONS_GSSAPI_CLIENT_IDENTITY, "bob");
    rc = ssh_userauth_gssapi(session);
    assert_int_equal(rc, SSH_AUTH_DENIED);
    torture_teardown_kdc_server(state);

    /* Valid client identity option*/
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");
    ssh_options_set(session, SSH_OPTIONS_GSSAPI_CLIENT_IDENTITY, "alice");
    rc = ssh_userauth_gssapi(session);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
    torture_teardown_kdc_server(state);
}

static void
torture_gssapi_auth_server_identity(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    /* Invalid server identity option */
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");
    ssh_options_set(session,
                    SSH_OPTIONS_GSSAPI_SERVER_IDENTITY,
                    "invalid.libssh.site");
    rc = ssh_userauth_gssapi(session);
    assert_int_equal(rc, SSH_AUTH_ERROR);
    torture_teardown_kdc_server(state);

    /* Valid server identity option*/
    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");
    ssh_options_set(session,
                    SSH_OPTIONS_GSSAPI_SERVER_IDENTITY,
                    "server.libssh.site");
    rc = ssh_userauth_gssapi(session);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
    torture_teardown_kdc_server(state);
}

static void
torture_gssapi_auth_delegate_creds(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    int rc;
    OM_uint32 maj_stat, min_stat;
    gss_cred_id_t client_creds = GSS_C_NO_CREDENTIAL;
    gss_OID_set no_mechs = GSS_C_NO_OID_SET;
    int t = 1;

    ssh_options_set(session, SSH_OPTIONS_GSSAPI_DELEGATE_CREDENTIALS, &t);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    torture_setup_kdc_server(
        state,
        "kadmin.local addprinc -randkey host/server.libssh.site \n"
        "kadmin.local ktadd -k $(dirname $0)/d/ssh.keytab host/server.libssh.site \n"
        "kadmin.local addprinc -pw bar alice \n"
        "kadmin.local list_principals",

        "echo bar | kinit alice");

    maj_stat = gss_acquire_cred(&min_stat,
                                GSS_C_NO_NAME,
                                GSS_C_INDEFINITE,
                                GSS_C_NO_OID_SET,
                                GSS_C_INITIATE,
                                &client_creds,
                                &no_mechs,
                                NULL);
    assert_int_equal(GSS_ERROR(maj_stat), 0);

    ssh_gssapi_set_creds(session, client_creds);

    rc = ssh_userauth_gssapi(session);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    gss_release_cred(&min_stat, &client_creds);
    gss_release_oid_set(&min_stat, &no_mechs);

    torture_teardown_kdc_server(state);
}

int
torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_gssapi_auth,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_gssapi_auth_client_identity,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_gssapi_auth_server_identity,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_gssapi_auth_delegate_creds,
                                        session_setup,
                                        session_teardown),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
