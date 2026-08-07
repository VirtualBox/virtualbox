#include "config.h"

#define LIBSSH_STATIC

#include "libssh/pki.h"
#include "pki.c"
#include "torture.h"
#include "torture_key.h"
#include "torture_pki.h"

#ifdef WITH_FIDO2
#include "libssh/libssh.h"
#include "torture_sk.h"
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

/**
 * The tests for the sk-type keys can also be configured to run with
 * the sk-usbhid callbacks instead of the default sk-dummy callbacks which can
 * run in a CI environment.
 *
 * To run these tests with the sk-usbhid callbacks, at least one FIDO2 device
 * must be connected and the environment variables TORTURE_SK_USBHID must be
 * set.
 */

static const char template[] = "tmp_XXXXXX";
static const char input[] = "Test input\0string with null byte";
static const size_t input_len = sizeof(input) - 1; /* -1 to exclude final \0 */
static const char *test_namespace = "file";

struct key_hash_combo {
    enum ssh_keytypes_e key_type;
    enum sshsig_digest_e hash_alg;
    const char *key_name;
};

struct sshsig_st {
    /*
     * The original current working directory at the start of the test.
     *
     * During setup, the current working directory is changed to a newly
     * created temporary directory (temp_dir).
     *
     * During cleanup, the current working directory is restored back
     * to original_cwd.
     */
    char *original_cwd;
    char *temp_dir;
    ssh_key rsa_key;
    ssh_key ed25519_key;
    ssh_key ecdsa_key;

#ifdef WITH_FIDO2
    ssh_pki_ctx pki_ctx;
    ssh_key sk_ecdsa_key;
    ssh_key sk_ed25519_key;
#endif

    const char *ssh_keygen_path;
    const struct key_hash_combo *test_combinations;
    size_t num_combinations;
};

static struct key_hash_combo test_combinations[] = {
    {SSH_KEYTYPE_RSA, SSHSIG_DIGEST_SHA2_256, "rsa"},
    {SSH_KEYTYPE_RSA, SSHSIG_DIGEST_SHA2_512, "rsa"},
    {SSH_KEYTYPE_ED25519, SSHSIG_DIGEST_SHA2_256, "ed25519"},
    {SSH_KEYTYPE_ED25519, SSHSIG_DIGEST_SHA2_512, "ed25519"},
#ifdef HAVE_ECC
    {SSH_KEYTYPE_ECDSA_P256, SSHSIG_DIGEST_SHA2_256, "ecdsa"},
    {SSH_KEYTYPE_ECDSA_P256, SSHSIG_DIGEST_SHA2_512, "ecdsa"},
# ifdef WITH_FIDO2
    {SSH_KEYTYPE_SK_ECDSA, SSHSIG_DIGEST_SHA2_256, "sk_ecdsa"},
    {SSH_KEYTYPE_SK_ECDSA, SSHSIG_DIGEST_SHA2_512, "sk_ecdsa"},
# endif /* WITH_FIDO2 */
#endif /* HAVE_ECC */

#ifdef WITH_FIDO2
    {SSH_KEYTYPE_SK_ED25519, SSHSIG_DIGEST_SHA2_256, "sk_ed25519"},
    {SSH_KEYTYPE_SK_ED25519, SSHSIG_DIGEST_SHA2_512, "sk_ed25519"},
#endif
};

static ssh_key get_test_key(struct sshsig_st *test_state,
                            enum ssh_keytypes_e type)
{
    switch (type) {
    case SSH_KEYTYPE_RSA:
        return test_state->rsa_key;
    case SSH_KEYTYPE_ED25519:
        if (ssh_fips_mode()) {
            return NULL;
        } else {
            return test_state->ed25519_key;
        }
#ifdef HAVE_ECC
    case SSH_KEYTYPE_ECDSA_P256:
        return test_state->ecdsa_key;
# ifdef WITH_FIDO2
    case SSH_KEYTYPE_SK_ECDSA:
        return test_state->sk_ecdsa_key;
# endif /* WITH_FIDO2 */
#endif /* HAVE_ECC */

#ifdef WITH_FIDO2
    case SSH_KEYTYPE_SK_ED25519:
        if (ssh_fips_mode()) {
            return NULL;
        } else {
            return test_state->sk_ed25519_key;
        }
#endif
    default:
        return NULL;
    }
}

static int setup_sshsig_compat(void **state)
{
    struct sshsig_st *test_state = NULL;
    char *original_cwd = NULL;
    char *temp_dir = NULL;
    int rc = 0;

#ifdef WITH_FIDO2
    const struct ssh_sk_callbacks_struct *sk_callbacks = NULL;
#endif

    test_state = calloc(1, sizeof(struct sshsig_st));
    assert_non_null(test_state);

    original_cwd = torture_get_current_working_dir();
    assert_non_null(original_cwd);

    temp_dir = torture_make_temp_dir(template);
    assert_non_null(temp_dir);

    test_state->original_cwd = original_cwd;
    test_state->temp_dir = temp_dir;
    test_state->test_combinations = test_combinations;
    test_state->num_combinations =
        sizeof(test_combinations) / sizeof(test_combinations[0]);

    *state = test_state;

    rc = torture_change_dir(temp_dir);
    assert_int_equal(rc, 0);

    /* Check if openssh is available and supports SSH signatures */
#ifdef OPENSSH_SUPPORTS_SSHSIG
    test_state->ssh_keygen_path = SSH_KEYGEN_EXECUTABLE;
#else
    test_state->ssh_keygen_path = NULL;
    printf("OpenSSH version does not support SSH signatures (requires "
           "8.1+), skipping compatibility tests\n");
#endif /* OPENSSH_SUPPORTS_SSHSIG */

    /* Load pre-generated test keys using torture functions */
    rc = ssh_pki_import_privkey_base64(torture_get_testkey(SSH_KEYTYPE_RSA, 0),
                                       NULL,
                                       NULL,
                                       NULL,
                                       &test_state->rsa_key);
    assert_int_equal(rc, SSH_OK);

    /* Skip ed25519 if in FIPS mode */
    if (!ssh_fips_mode()) {
        /* mbedtls and libgcrypt don't fully support PKCS#8 PEM */
        /* thus parse the key with OpenSSH */
        rc = ssh_pki_import_privkey_base64(
            torture_get_openssh_testkey(SSH_KEYTYPE_ED25519, 0),
            NULL,
            NULL,
            NULL,
            &test_state->ed25519_key);
        assert_int_equal(rc, SSH_OK);
    }

#ifdef HAVE_ECC
    rc = ssh_pki_import_privkey_base64(
        torture_get_testkey(SSH_KEYTYPE_ECDSA_P256, 0),
        NULL,
        NULL,
        NULL,
        &test_state->ecdsa_key);
    assert_int_equal(rc, SSH_OK);
#endif

#ifdef WITH_FIDO2
    /* Create and configure PKI context for SK operations */
    sk_callbacks = torture_get_sk_callbacks();
    if (sk_callbacks != NULL) {
        test_state->pki_ctx = ssh_pki_ctx_new();
        assert_non_null(test_state->pki_ctx);

        rc = ssh_pki_ctx_options_set(test_state->pki_ctx,
                                     SSH_PKI_OPTION_SK_CALLBACKS,
                                     sk_callbacks);
        assert_int_equal(rc, SSH_OK);

# ifdef HAVE_ECC
        rc = ssh_pki_generate_key(SSH_KEYTYPE_SK_ECDSA,
                                  test_state->pki_ctx,
                                  &test_state->sk_ecdsa_key);
        assert_int_equal(rc, SSH_OK);
# endif /* HAVE_ECC */

        if (!ssh_fips_mode()) {
            rc = ssh_pki_generate_key(SSH_KEYTYPE_SK_ED25519,
                                      test_state->pki_ctx,
                                      &test_state->sk_ed25519_key);
            assert_int_equal(rc, SSH_OK);
        }
    }

#endif /* WITH_FIDO2 */

    /* Write keys to files for openssh compatibility testing */
    if (test_state->ssh_keygen_path != NULL) {
        torture_write_file("test_rsa", torture_get_testkey(SSH_KEYTYPE_RSA, 0));
        torture_write_file("test_rsa.pub",
                           torture_get_testkey_pub(SSH_KEYTYPE_RSA));

        if (!ssh_fips_mode()) {
            torture_write_file(
                "test_ed25519",
                torture_get_openssh_testkey(SSH_KEYTYPE_ED25519, 0));
            torture_write_file("test_ed25519.pub",
                               torture_get_testkey_pub(SSH_KEYTYPE_ED25519));
        }

#ifdef HAVE_ECC
        torture_write_file("test_ecdsa",
                           torture_get_testkey(SSH_KEYTYPE_ECDSA_P256, 0));
        torture_write_file("test_ecdsa.pub",
                           torture_get_testkey_pub(SSH_KEYTYPE_ECDSA_P256));
#endif /* HAVE_ECC */

#ifdef WITH_FIDO2
# ifdef HAVE_ECC
        /* Write SK keys to files if they were successfully generated */
        if (test_state->sk_ecdsa_key != NULL) {
            char *sk_ecdsa_priv = NULL;
            char *sk_ecdsa_pub = NULL;

            rc = ssh_pki_export_privkey_base64(test_state->sk_ecdsa_key,
                                               NULL,
                                               NULL,
                                               NULL,
                                               &sk_ecdsa_priv);
            assert_int_equal(rc, SSH_OK);

            rc = ssh_pki_export_pubkey_base64(test_state->sk_ecdsa_key,
                                              &sk_ecdsa_pub);
            assert_int_equal(rc, SSH_OK);

            torture_write_file("test_sk_ecdsa", sk_ecdsa_priv);
            torture_write_file("test_sk_ecdsa.pub", sk_ecdsa_pub);

            SAFE_FREE(sk_ecdsa_priv);
            SAFE_FREE(sk_ecdsa_pub);
        }
# endif /* HAVE_ECC */

        if (!ssh_fips_mode() && test_state->sk_ed25519_key != NULL) {
            char *sk_ed25519_priv = NULL;
            char *sk_ed25519_pub = NULL;

            rc = ssh_pki_export_privkey_base64(test_state->sk_ed25519_key,
                                               NULL,
                                               NULL,
                                               NULL,
                                               &sk_ed25519_priv);
            assert_int_equal(rc, SSH_OK);

            rc = ssh_pki_export_pubkey_base64(test_state->sk_ed25519_key,
                                              &sk_ed25519_pub);
            assert_int_equal(rc, SSH_OK);

            torture_write_file("test_sk_ed25519", sk_ed25519_priv);
            torture_write_file("test_sk_ed25519.pub", sk_ed25519_pub);

            SAFE_FREE(sk_ed25519_priv);
            SAFE_FREE(sk_ed25519_pub);
        }
#endif /* WITH_FIDO2 */

        rc = chmod("test_rsa", 0600);
        assert_return_code(rc, errno);
        if (!ssh_fips_mode()) {
            rc = chmod("test_ed25519", 0600);
            assert_return_code(rc, errno);
        }
#ifdef HAVE_ECC
        rc = chmod("test_ecdsa", 0600);
        assert_return_code(rc, errno);
#endif

#ifdef WITH_FIDO2
        /* Set permissions for SK key files */
# ifdef HAVE_ECC
        if (test_state->sk_ecdsa_key != NULL) {
            rc = chmod("test_sk_ecdsa", 0600);
            assert_return_code(rc, errno);
        }
# endif /* HAVE_ECC */
        if (!ssh_fips_mode() && test_state->sk_ed25519_key != NULL) {
            rc = chmod("test_sk_ed25519", 0600);
            assert_return_code(rc, errno);
        }
#endif /* WITH_FIDO2 */
    }

    return 0;
}

static int teardown_sshsig_compat(void **state)
{
    struct sshsig_st *test_state = *state;
    int rc = 0;

    assert_non_null(test_state);

    ssh_key_free(test_state->rsa_key);
    ssh_key_free(test_state->ed25519_key);
    ssh_key_free(test_state->ecdsa_key);

#ifdef WITH_FIDO2
    SSH_PKI_CTX_FREE(test_state->pki_ctx);
    ssh_key_free(test_state->sk_ecdsa_key);
    ssh_key_free(test_state->sk_ed25519_key);
#endif

    rc = torture_change_dir(test_state->original_cwd);
    assert_int_equal(rc, 0);

    rc = torture_rmdirs(test_state->temp_dir);
    assert_int_equal(rc, 0);

    SAFE_FREE(test_state->temp_dir);
    SAFE_FREE(test_state->original_cwd);
    SAFE_FREE(test_state);

    return 0;
}

static int run_openssh_command(const char *cmd)
{
    char full_cmd[2048];
    int rc;

#if defined(WITH_FIDO2) && defined(SK_DUMMY_LIBRARY_PATH)
    /* Set SSH_SK_PROVIDER to sk-dummy library when using sk-dummy callbacks */
    if (torture_sk_is_using_sk_dummy()) {
        snprintf(full_cmd,
                 sizeof(full_cmd),
                 "SSH_SK_PROVIDER=\"%s\" %s",
                 SK_DUMMY_LIBRARY_PATH,
                 cmd);
    } else {
        snprintf(full_cmd, sizeof(full_cmd), "%s", cmd);
    }
#else
    snprintf(full_cmd, sizeof(full_cmd), "%s", cmd);
#endif

    rc = system(full_cmd);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

static void torture_pki_sshsig_armor_dearmor(UNUSED_PARAM(void **state))
{
    ssh_buffer test_buffer = NULL;
    ssh_buffer dearmored_buffer = NULL;
    char *armored_sig = NULL;
    const char test_data[] = "test signature data";
    int rc;

    test_buffer = ssh_buffer_new();
    assert_non_null(test_buffer);

    rc = ssh_buffer_add_data(test_buffer, test_data, strlen(test_data));
    assert_int_equal(rc, SSH_OK);

    rc = sshsig_armor(test_buffer, &armored_sig);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(armored_sig);

    /* Test with NULL armored_sig */
    rc = sshsig_armor(test_buffer, NULL);
    assert_int_equal(rc, SSH_ERROR);

    assert_non_null(strstr(armored_sig, SSHSIG_BEGIN_SIGNATURE));
    assert_non_null(strstr(armored_sig, SSHSIG_END_SIGNATURE));

    /* Test with NULL dearmored_buffer */
    rc = sshsig_dearmor(armored_sig, NULL);
    assert_int_equal(rc, SSH_ERROR);

    rc = sshsig_dearmor(armored_sig, &dearmored_buffer);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(dearmored_buffer);

    assert_int_equal(ssh_buffer_get_len(test_buffer),
                     ssh_buffer_get_len(dearmored_buffer));
    assert_memory_equal(ssh_buffer_get(test_buffer),
                        ssh_buffer_get(dearmored_buffer),
                        ssh_buffer_get_len(test_buffer));

    ssh_buffer_free(test_buffer);
    ssh_buffer_free(dearmored_buffer);
    free(armored_sig);
}

static void torture_pki_sshsig_armor_dearmor_invalid(UNUSED_PARAM(void **state))
{
    ssh_buffer dearmored_buffer = NULL;
    char *armored_sig = NULL;
    int rc;
    const char *invalid_sig = "-----BEGIN INVALID SIGNATURE-----\n"
                              "data\n"
                              "-----END INVALID SIGNATURE-----\n";

    const char *incomplete_sig = "-----BEGIN SSH SIGNATURE----\n"
                                 "U1NIU0lH\n";

    /* Test with NULL buffer */
    rc = sshsig_armor(NULL, &armored_sig);
    assert_int_equal(rc, SSH_ERROR);

    /* Test dearmoring with invalid signature */
    rc = sshsig_dearmor(invalid_sig, &dearmored_buffer);
    assert_int_equal(rc, SSH_ERROR);

    /* Test dearmoring with NULL input */
    rc = sshsig_dearmor(NULL, &dearmored_buffer);
    assert_int_equal(rc, SSH_ERROR);

    /* Test dearmoring with missing end marker */
    rc = sshsig_dearmor(incomplete_sig, &dearmored_buffer);
    assert_int_equal(rc, SSH_ERROR);
}

static void test_libssh_sign_verify_combo(struct sshsig_st *test_state,
                                          const struct key_hash_combo *combo)
{
    char *signature = NULL;
    ssh_key verify_key = NULL;
    ssh_key test_key = NULL;
    ssh_pki_ctx pki_context = NULL;
    int rc;

    if ((combo->key_type == SSH_KEYTYPE_ED25519 ||
         combo->key_type == SSH_KEYTYPE_SK_ED25519) &&
        ssh_fips_mode()) {
        skip();
    }

    test_key = get_test_key(test_state, combo->key_type);
    if (is_sk_key_type(combo->key_type) && test_key == NULL) {
        /* Skip if SK key type is requested but SK callbacks are not available
         */
        skip();
    }

    assert_non_null(test_key);

#ifdef WITH_FIDO2
    /* Use PKI context for SK keys */
    if (is_sk_key_type(combo->key_type)) {
        pki_context = test_state->pki_ctx;
    }
#endif

    rc = sshsig_sign(input,
                     input_len,
                     test_key,
                     pki_context,
                     test_namespace,
                     combo->hash_alg,
                     &signature);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(signature);

    rc =
        sshsig_verify(input, input_len, signature, test_namespace, &verify_key);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(verify_key);

    rc = ssh_key_cmp(test_key, verify_key, SSH_KEY_CMP_PUBLIC);
    assert_int_equal(rc, 0);

    ssh_key_free(verify_key);
    free(signature);
}

static void
test_openssh_sign_libssh_verify_combo(struct sshsig_st *test_state,
                                      const struct key_hash_combo *combo)
{
    char cmd[1024];
    char *openssh_sig = NULL;
    ssh_key verify_key = NULL;
    ssh_key test_key = NULL;
    FILE *fp = NULL;
    int rc;

    if ((combo->key_type == SSH_KEYTYPE_ED25519 ||
         combo->key_type == SSH_KEYTYPE_SK_ED25519) &&
        ssh_fips_mode()) {
        skip();
    }

    test_key = get_test_key(test_state, combo->key_type);
    if (is_sk_key_type(combo->key_type) && test_key == NULL) {
        /* Skip if SK key type is requested but SK callbacks are not available
         */
        skip();
    }

    fp = fopen("test_message.txt", "wb");
    assert_non_null(fp);
    /* Write binary data including null byte */
    rc = fwrite(input, input_len, 1, fp);
    assert_return_code(rc, errno);
    rc = fclose(fp);
    assert_return_code(rc, errno);

    snprintf(cmd,
             sizeof(cmd),
             "%s -Y sign -f test_%s -n %s test_message.txt",
             test_state->ssh_keygen_path,
             combo->key_name,
             test_namespace);
    rc = run_openssh_command(cmd);

    assert_int_equal(rc, 0);
    openssh_sig = torture_pki_read_file("test_message.txt.sig");
    assert_non_null(openssh_sig);

    rc = sshsig_verify(input,
                       input_len,
                       openssh_sig,
                       test_namespace,
                       &verify_key);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(verify_key);

    ssh_key_free(verify_key);
    free(openssh_sig);
    rc = unlink("test_message.txt.sig");
    assert_return_code(rc, errno);
    rc = unlink("test_message.txt");
    assert_return_code(rc, errno);
}

static void
test_libssh_sign_openssh_verify_combo(struct sshsig_st *test_state,
                                      const struct key_hash_combo *combo)
{
    char *libssh_sig = NULL;
    char cmd[1024];
    FILE *fp = NULL;
    int rc;
    char *pubkey_b64 = NULL;
    ssh_key test_key = NULL;
    ssh_pki_ctx pki_context = NULL;

    if ((combo->key_type == SSH_KEYTYPE_ED25519 ||
         combo->key_type == SSH_KEYTYPE_SK_ED25519) &&
        ssh_fips_mode()) {
        skip();
    }

    printf("Testing key type: %s\n", combo->key_name);
    test_key = get_test_key(test_state, combo->key_type);
    if (is_sk_key_type(combo->key_type) && test_key == NULL) {
        /* Skip if SK key type is requested but SK callbacks are not available
         */
        skip();
    }
    assert_non_null(test_key);

#ifdef WITH_FIDO2
    /* Use PKI context for SK keys */
    if (is_sk_key_type(combo->key_type)) {
        pki_context = test_state->pki_ctx;
    }
#endif

    fp = fopen("test_message.txt", "wb");
    assert_non_null(fp);
    /* Write binary data including null byte */
    rc = fwrite(input, input_len, 1, fp);
    assert_return_code(rc, errno);
    rc = fclose(fp);
    assert_return_code(rc, errno);

    rc = sshsig_sign(input,
                     input_len,
                     test_key,
                     pki_context,
                     test_namespace,
                     combo->hash_alg,
                     &libssh_sig);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(libssh_sig);

    fp = fopen("test_message.txt.sig", "w");
    assert_non_null(fp);
    rc = fputs(libssh_sig, fp);
    assert_return_code(rc, errno);
    rc = fclose(fp);
    assert_return_code(rc, errno);

    rc = ssh_pki_export_pubkey_base64(test_key, &pubkey_b64);
    assert_int_equal(rc, SSH_OK);

    fp = fopen("allowed_signers", "w");
    assert_non_null(fp);
    rc = fprintf(fp, "test %s %s\n", test_key->type_c, pubkey_b64);
    assert_return_code(rc, errno);
    rc = fclose(fp);
    assert_return_code(rc, errno);

    snprintf(cmd,
             sizeof(cmd),
             "%s -Y verify -f allowed_signers -I test -n %s -s "
             "test_message.txt.sig < test_message.txt",
             test_state->ssh_keygen_path,
             test_namespace);
    rc = run_openssh_command(cmd);
    assert_int_equal(rc, 0);

    free(libssh_sig);
    free(pubkey_b64);
    rc = unlink("test_message.txt.sig");
    assert_return_code(rc, errno);
    rc = unlink("allowed_signers");
    assert_return_code(rc, errno);
    rc = unlink("test_message.txt");
    assert_return_code(rc, errno);
}

static void torture_sshsig_libssh_all_combinations(void **state)
{
    struct sshsig_st *test_state = *state;
    size_t i;

    for (i = 0; i < test_state->num_combinations; i++) {
        test_libssh_sign_verify_combo(test_state,
                                      &test_state->test_combinations[i]);
    }
}

static void torture_sshsig_openssh_libssh_all_combinations(void **state)
{
    struct sshsig_st *test_state = *state;
    size_t i;

    if (test_state->ssh_keygen_path == NULL) {
        skip();
    }

    for (i = 0; i < test_state->num_combinations; i++) {
        test_openssh_sign_libssh_verify_combo(
            test_state,
            &test_state->test_combinations[i]);
    }
}

static void torture_sshsig_libssh_openssh_all_combinations(void **state)
{
    struct sshsig_st *test_state = *state;
    size_t i;

    if (test_state->ssh_keygen_path == NULL) {
        skip();
    }

    for (i = 0; i < test_state->num_combinations; i++) {
        test_libssh_sign_openssh_verify_combo(
            test_state,
            &test_state->test_combinations[i]);
    }
}

static void torture_sshsig_error_cases_all_combinations(void **state)
{
    struct sshsig_st *test_state = *state;
    char *signature = NULL;
    ssh_key verify_key = NULL;
    int rc;
    size_t i;
    char tampered_data[] = "Tampered\0data";

    for (i = 0; i < test_state->num_combinations; i++) {
        const struct key_hash_combo *combo = &test_state->test_combinations[i];
        ssh_key test_key = NULL;
        ssh_pki_ctx pki_context = NULL;

        if ((combo->key_type == SSH_KEYTYPE_ED25519 ||
             combo->key_type == SSH_KEYTYPE_SK_ED25519) &&
            ssh_fips_mode()) {
            continue;
        }

        test_key = get_test_key(test_state, combo->key_type);
        if (is_sk_key_type(combo->key_type) && test_key == NULL) {
            /* Skip if SK key type is requested but SK callbacks are not
             * available */
            continue;
        }
        assert_non_null(test_key);

#ifdef WITH_FIDO2
        if (is_sk_key_type(combo->key_type)) {
            pki_context = test_state->pki_ctx;
        }
#endif

        rc = sshsig_sign(input,
                         input_len,
                         test_key,
                         pki_context,
                         "", /* Test empty string namespace */
                         combo->hash_alg,
                         &signature);
        assert_int_equal(rc, SSH_ERROR);
        assert_null(signature);

        rc = sshsig_sign(input,
                         input_len,
                         test_key,
                         pki_context,
                         test_namespace,
                         combo->hash_alg,
                         &signature);
        assert_int_equal(rc, SSH_OK);
        assert_non_null(signature);

        rc = sshsig_verify(input,
                           input_len,
                           signature,
                           "wrong_namespace",
                           &verify_key);
        assert_int_equal(rc, SSH_ERROR);
        assert_null(verify_key);

        rc = sshsig_verify(input,
                           input_len,
                           signature,
                           "", /* Test empty string namespace */
                           &verify_key);
        assert_int_equal(rc, SSH_ERROR);
        assert_null(verify_key);

        rc = sshsig_verify(tampered_data,
                           sizeof(tampered_data) - 1,
                           signature,
                           test_namespace,
                           &verify_key);
        assert_int_equal(rc, SSH_ERROR);
        assert_null(verify_key);

        free(signature);
        signature = NULL;
    }

    /* Test invalid hash algorithm */
    rc = sshsig_sign(input,
                     input_len,
                     test_state->rsa_key,
                     NULL, /* pki_context */
                     test_namespace,
                     2,
                     &signature);
    assert_int_equal(rc, SSH_ERROR);

    /* Test NULL parameters */
    rc = sshsig_sign(input,
                     input_len,
                     NULL,
                     NULL, /* pki_context */
                     test_namespace,
                     SSHSIG_DIGEST_SHA2_256,
                     &signature);
    assert_int_equal(rc, SSH_ERROR);

    rc =
        sshsig_verify(input, input_len, "invalid", test_namespace, &verify_key);
    assert_int_equal(rc, SSH_ERROR);
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_pki_sshsig_armor_dearmor),
        cmocka_unit_test(torture_pki_sshsig_armor_dearmor_invalid),
        /* Comprehensive combination tests */
        cmocka_unit_test_setup_teardown(torture_sshsig_libssh_all_combinations,
                                        setup_sshsig_compat,
                                        teardown_sshsig_compat),
        cmocka_unit_test_setup_teardown(
            torture_sshsig_openssh_libssh_all_combinations,
            setup_sshsig_compat,
            teardown_sshsig_compat),
        cmocka_unit_test_setup_teardown(
            torture_sshsig_libssh_openssh_all_combinations,
            setup_sshsig_compat,
            teardown_sshsig_compat),

        /* Comprehensive error case testing */
        cmocka_unit_test_setup_teardown(
            torture_sshsig_error_cases_all_combinations,
            setup_sshsig_compat,
            teardown_sshsig_compat),
    };

    ssh_init();
    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, NULL, NULL);
    ssh_finalize();

    return rc;
}
