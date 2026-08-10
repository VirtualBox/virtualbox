#include "config.h"

#define LIBSSH_STATIC

#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "pki.c"
#include "torture.h"
#include "torture_key.h"
#include "torture_pki.h"

#define LIBSSH_DSA_TESTKEY "libssh_testkey.id_dsa"
#define LIBSSH_DSA_TESTKEY_PASSPHRASE "libssh_testkey_passphrase.id_dsa"

const char template[] = "temp_dir_XXXXXX";
const unsigned char INPUT[] = "12345678901234567890";

struct pki_st {
    char *cwd;
    char *temp_dir;
};

static int setup_dsa_key(void **state)
{
    struct pki_st *test_state = NULL;
    char *cwd = NULL;
    char *tmp_dir = NULL;
    int rc = 0;

    test_state = (struct pki_st *)malloc(sizeof(struct pki_st));
    assert_non_null(test_state);

    cwd = torture_get_current_working_dir();
    assert_non_null(cwd);

    tmp_dir = torture_make_temp_dir(template);
    assert_non_null(tmp_dir);

    test_state->cwd = cwd;
    test_state->temp_dir = tmp_dir;

    *state = test_state;

    rc = torture_change_dir(tmp_dir);
    assert_int_equal(rc, 0);

    printf("Changed directory to: %s\n", tmp_dir);

    torture_write_file(LIBSSH_DSA_TESTKEY,
                       torture_get_testkey(SSH_KEYTYPE_DSS, 0));
    torture_write_file(LIBSSH_DSA_TESTKEY_PASSPHRASE,
                       torture_get_testkey(SSH_KEYTYPE_DSS, 1));
    torture_write_file(LIBSSH_DSA_TESTKEY ".pub",
                       torture_get_testkey_pub(SSH_KEYTYPE_DSS));
    torture_write_file(LIBSSH_DSA_TESTKEY "-cert.pub",
                       torture_get_testkey_pub(SSH_KEYTYPE_DSS_CERT01));

    return 0;
}

static int setup_openssh_dsa_key(void **state)
{
    struct pki_st *test_state = NULL;
    char *cwd = NULL;
    char *tmp_dir = NULL;
    int rc = 0;

    test_state = (struct pki_st *)malloc(sizeof(struct pki_st));
    assert_non_null(test_state);

    cwd = torture_get_current_working_dir();
    assert_non_null(cwd);

    tmp_dir = torture_make_temp_dir(template);
    assert_non_null(tmp_dir);

    test_state->cwd = cwd;
    test_state->temp_dir = tmp_dir;

    *state = test_state;

    rc = torture_change_dir(tmp_dir);
    assert_int_equal(rc, 0);

    torture_write_file(LIBSSH_DSA_TESTKEY,
                       torture_get_openssh_testkey(SSH_KEYTYPE_DSS, 0));
    torture_write_file(LIBSSH_DSA_TESTKEY_PASSPHRASE,
                       torture_get_openssh_testkey(SSH_KEYTYPE_DSS, 1));
    torture_write_file(LIBSSH_DSA_TESTKEY ".pub",
                       torture_get_testkey_pub(SSH_KEYTYPE_DSS));
    torture_write_file(LIBSSH_DSA_TESTKEY "-cert.pub",
                       torture_get_testkey_pub(SSH_KEYTYPE_DSS_CERT01));

    return 0;
}

static int teardown(void **state) {

    struct pki_st *test_state = NULL;
    int rc = 0;

    test_state = *((struct pki_st **)state);

    assert_non_null(test_state);
    assert_non_null(test_state->cwd);
    assert_non_null(test_state->temp_dir);

    rc = torture_change_dir(test_state->cwd);
    assert_int_equal(rc, 0);

    rc = torture_rmdirs(test_state->temp_dir);
    assert_int_equal(rc, 0);

    SAFE_FREE(test_state->temp_dir);
    SAFE_FREE(test_state->cwd);
    SAFE_FREE(test_state);

    return 0;
}

static void torture_pki_dsa_import_pubkey_file(void **state)
{
    ssh_key pubkey = NULL;
    int rc;

    (void)state;

    /* The key doesn't have the hostname as comment after the key */
    rc = ssh_pki_import_pubkey_file(LIBSSH_DSA_TESTKEY ".pub", &pubkey);
    assert_int_equal(rc, SSH_ERROR);
    assert_null(pubkey);
}

static void torture_pki_dsa_import_pubkey_from_openssh_privkey(void **state)
{
    ssh_key pubkey = NULL;
    int rc;

    (void)state;

    /* The key doesn't have the hostname as comment after the key */
    rc = ssh_pki_import_pubkey_file(LIBSSH_DSA_TESTKEY_PASSPHRASE, &pubkey);
    assert_int_equal(rc, SSH_ERROR);
    assert_null(pubkey);
}

static void torture_pki_dsa_import_privkey_base64(void **state)
{
    int rc;
    ssh_key key = NULL;
    const char *passphrase = torture_get_testkey_passphrase();

    (void) state; /* unused */

    rc = ssh_pki_import_privkey_base64(torture_get_testkey(SSH_KEYTYPE_DSS, 0),
                                       passphrase,
                                       NULL,
                                       NULL,
                                       &key);
    assert_int_equal(rc, SSH_ERROR);
    assert_null(key);
}

static void torture_pki_generate_dsa(void **state)
{
    int rc;
    ssh_key key = NULL;

    (void) state;

    /* Setup */
    rc = ssh_pki_generate(SSH_KEYTYPE_DSS, 2048, &key);
    assert_int_equal(rc, SSH_ERROR);
    assert_null(key);
}

static void torture_pki_dsa_import_cert_file(void **state)
{
    int rc;
    ssh_key cert = NULL;

    (void) state; /* unused */

    rc = ssh_pki_import_cert_file(LIBSSH_DSA_TESTKEY "-cert.pub", &cert);
    assert_int_equal(rc, SSH_ERROR);
    assert_null(cert);
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_pki_dsa_import_pubkey_file,
                                 setup_dsa_key,
                                 teardown),
        cmocka_unit_test_setup_teardown(torture_pki_dsa_import_pubkey_from_openssh_privkey,
                                 setup_openssh_dsa_key,
                                 teardown),
        cmocka_unit_test_setup_teardown(torture_pki_dsa_import_privkey_base64,
                                 setup_dsa_key,
                                 teardown),
        cmocka_unit_test_setup_teardown(torture_pki_dsa_import_privkey_base64,
                                 setup_openssh_dsa_key,
                                 teardown),
        cmocka_unit_test_setup_teardown(torture_pki_dsa_import_cert_file,
                                        setup_dsa_key,
                                        teardown),
        cmocka_unit_test_setup_teardown(torture_pki_generate_dsa,
                                        setup_dsa_key,
                                        teardown),
    };

    ssh_init();
    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, NULL, NULL);
    ssh_finalize();
    return rc;
}
