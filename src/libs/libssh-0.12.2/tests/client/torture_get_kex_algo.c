#include "config.h"
#include "libssh/libcrypto.h"
#include <string.h>
#define LIBSSH_STATIC
#include "torture.h"
#include <errno.h>
#include <pwd.h>

#define ECDH_SHA2_NISTP256 "ecdh-sha2-nistp256"
#define CURVE25519_SHA256 "curve25519-sha256"
#define DIFFIE_HELLMAN_GROUP_14_SHA_1 "diffie-hellman-group14-sha1"
#define KEX_DH_GEX_SHA1 "diffie-hellman-group-exchange-sha1"
#define KEX_DH_GEX_SHA256 "diffie-hellman-group-exchange-sha256"
#define SNTRUP761X25519 "sntrup761x25519-sha512"
#define SNTRUP761X25519_OPENSSH "sntrup761x25519-sha512@openssh.com"
#define MLKEM768X25519 "mlkem768x25519-sha256"
#define MLKEM768NISTP256 "mlkem768nistp256-sha256"

static int sshd_setup(void **state)
{
    torture_setup_sshd_server(state, false);

    return 0;
}

static int sshd_teardown(void **state)
{
    torture_teardown_sshd_server(state);

    return 0;
}

static int session_setup(void **state)
{
    struct torture_state *s = *state;
    int verbosity = torture_libssh_verbosity();
    struct passwd *pwd = NULL;
    bool false_v = false;
    int rc;

    pwd = getpwnam("bob");
    assert_non_null(pwd);

    rc = setuid(pwd->pw_uid);
    assert_return_code(rc, errno);

    s->ssh.session = ssh_new();
    assert_non_null(s->ssh.session);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_PROCESS_CONFIG, &false_v);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_HOST, TORTURE_SSH_SERVER);
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

static void torture_kex_basic_functionality(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = NULL;
    const char *kex_algo = NULL;
    const char *valid_algorithms[] = {
        SNTRUP761X25519,
        SNTRUP761X25519_OPENSSH,
        MLKEM768X25519,
        MLKEM768NISTP256,
        CURVE25519_SHA256,
        ECDH_SHA2_NISTP256,
        DIFFIE_HELLMAN_GROUP_14_SHA_1,
    };
    size_t valid_algorithms_count, i;
    int rc;
    bool is_valid_algo;

    session = s->ssh.session;

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    kex_algo = ssh_get_kex_algo(session);
    assert_non_null(kex_algo);

    is_valid_algo = false;
    valid_algorithms_count =
        sizeof(valid_algorithms) / sizeof(valid_algorithms[0]);
    for (i = 0; i < valid_algorithms_count; i++) {
        if (strcmp(kex_algo, valid_algorithms[i]) == 0) {
            is_valid_algo = true;
            break;
        }
    }
    assert_true(is_valid_algo);
}

static void torture_kex_algo_preference(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    const char *expected_kex = NULL;
    const char *actual_kex = NULL;
    int rc;

    if (ssh_fips_mode()) {
        expected_kex = ECDH_SHA2_NISTP256;
    } else {
        expected_kex = CURVE25519_SHA256;
    }

    rc = ssh_options_set(session, SSH_OPTIONS_KEY_EXCHANGE, expected_kex);
    assert_ssh_return_code(session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    actual_kex = ssh_get_kex_algo(session);
    assert_non_null(actual_kex);
    assert_string_equal(actual_kex, expected_kex);
}

static void torture_kex_algo_negotiation(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    const char *kex_list =
        "non-existent-algo,not-supported-kex," CURVE25519_SHA256
        "," ECDH_SHA2_NISTP256 "," DIFFIE_HELLMAN_GROUP_14_SHA_1;
    int rc, cmp;
    const char *negotiated_kex = NULL;
    bool found;
    char *temp_list = NULL;
    char *token = NULL;

    rc = ssh_options_set(session, SSH_OPTIONS_KEY_EXCHANGE, kex_list);
    assert_ssh_return_code(session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    negotiated_kex = ssh_get_kex_algo(session);
    assert_non_null(negotiated_kex);

    assert_string_not_equal(negotiated_kex, "non-existent-algo");
    assert_string_not_equal(negotiated_kex, "not-supported-kex");

    found = false;
    temp_list = strdup(kex_list);

    for (token = strtok(temp_list, ","); token != NULL;
         token = strtok(NULL, ",")) {
        cmp = strcmp(token, negotiated_kex);
        if (cmp == 0) {
            found = true;
            break;
        }
    }

    free(temp_list);
    assert_true(found);
}

static void torture_kex_algo_before_connect(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    const char *kex_algo = NULL;

    kex_algo = ssh_get_kex_algo(session);
    assert_null(kex_algo);
}

#ifdef WITH_GEX
static void torture_dgex_algo(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    const char *kex_list = KEX_DH_GEX_SHA1 "," KEX_DH_GEX_SHA256;
    int rc, cmp;
    const char *negotiated_kex = NULL;
    bool found;
    char *temp_list = NULL;
    char *token = NULL;
    rc = ssh_options_set(session, SSH_OPTIONS_KEY_EXCHANGE, kex_list);
    assert_ssh_return_code(session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    negotiated_kex = ssh_get_kex_algo(session);
    assert_non_null(negotiated_kex);

    found = false;
    temp_list = strdup(kex_list);

    for (token = strtok(temp_list, ","); token != NULL;
         token = strtok(NULL, ",")) {
        cmp = strcmp(token, negotiated_kex);
        if (cmp == 0) {
            found = true;
            break;
        }
    }

    free(temp_list);
    assert_true(found);
}
#endif /* WITH_GEX */

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_kex_basic_functionality,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_kex_algo_preference,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_kex_algo_negotiation,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_kex_algo_before_connect,
                                        session_setup,
                                        session_teardown),
#ifdef WITH_GEX
        cmocka_unit_test_setup_teardown(torture_dgex_algo,
                                        session_setup,
                                        session_teardown),
#endif /* WITH_GEX */
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);

    ssh_finalize();
    return rc;
}
