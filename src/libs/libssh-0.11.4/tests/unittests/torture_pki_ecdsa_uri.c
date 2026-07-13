
#include "config.h"

#define LIBSSH_STATIC

#include <sys/stat.h>
#include <fcntl.h>

#include "pki.c"
#include "torture.h"
#include "torture_pki.h"
#include "torture_key.h"

#define LIBSSH_ECDSA_TESTKEY "libssh_testkey.id_"
#define LIBSSH_ECDSA_TESTKEY_PEM "libssh_testkey_pem.id_"
#define LABEL_256 "ecdsa256"
#define LABEL_384 "ecdsa384"
#define LABEL_521 "ecdsa521"
#define PUB_URI_FMT "pkcs11:token=%s;object=%s;type=public"
#define PRIV_URI_FMT "pkcs11:token=%s;object=%s;type=private?pin-value=1234"
#define PRIV_URI_NO_PUB_FMT "pkcs11:token=%s_no_pub_uri;object=%s_no_pub_uri;type=private?pin-value=1234"

/** PKCS#11 URIs with invalid fields**/

#define PRIV_URI_FMT_384_INVALID_TOKEN "pkcs11:token=ecdsa521;object=ecdsa384;type=private?pin-value=1234"
#define PRIV_URI_FMT_521_INVALID_OBJECT "pkcs11:token=ecdsa521;object=ecdsa384;type=private?pin-value=1234"
#define PUB_URI_FMT_384_INVALID_TOKEN "pkcs11:token=ecdsa521;object=ecdsa384;type=public"
#define PUB_URI_FMT_521_INVALID_OBJECT "pkcs11:token=ecdsa521;object=ecdsa384;type=public"

const char template[] = "/tmp/temp_dir_XXXXXX";
const unsigned char INPUT[] = "1234567890123456789012345678901234567890"
                              "123456789012345678901234";
struct pki_st {
    char *orig_dir;
    char *temp_dir;
    enum ssh_keytypes_e type;
};

static int setup_tokens_ecdsa(void **state, int ecdsa_bits, const char *obj_tempname, const char *load_public)
{

    struct pki_st *test_state = *state;
    char priv_filename[1024];
    char pub_filename[1024];
    char *cwd = NULL;

    cwd = test_state->temp_dir;
    assert_non_null(cwd);

    snprintf(priv_filename, sizeof(priv_filename), "%s%s%s%s", cwd, "/", LIBSSH_ECDSA_TESTKEY, obj_tempname);
    snprintf(pub_filename, sizeof(pub_filename), "%s%s%s%s%s", cwd, "/", LIBSSH_ECDSA_TESTKEY, obj_tempname, ".pub");

    switch (ecdsa_bits) {
        case 521:
            test_state->type = SSH_KEYTYPE_ECDSA_P521;
            break;
        case 384:
            test_state->type = SSH_KEYTYPE_ECDSA_P384;
            break;
        default:
            test_state->type = SSH_KEYTYPE_ECDSA_P256;
            break;
    }

    torture_write_file(priv_filename,
                       torture_get_testkey(test_state->type, 0));
    torture_write_file(pub_filename,
                       torture_get_testkey_pub_pem(test_state->type));
    torture_setup_tokens(cwd, priv_filename, obj_tempname, load_public);

    return 0;
}

static int setup_directory_structure(void **state)
{
    struct pki_st *test_state = NULL;
    char *temp_dir;
    int rc;

    test_state = (struct pki_st *)malloc(sizeof(struct pki_st));
    assert_non_null(test_state);

    test_state->orig_dir = torture_get_current_working_dir();
    assert_non_null(test_state->orig_dir);

    temp_dir = torture_make_temp_dir(template);
    assert_non_null(temp_dir);

    rc = torture_change_dir(temp_dir);
    assert_int_equal(rc, 0);
    SAFE_FREE(temp_dir);

    test_state->temp_dir = torture_get_current_working_dir();
    assert_non_null(test_state->temp_dir);

    *state = test_state;

    setup_tokens_ecdsa(state, 256, "ecdsa256", "1");
    setup_tokens_ecdsa(state, 384, "ecdsa384", "1");
    setup_tokens_ecdsa(state, 521, "ecdsa521", "1");
    setup_tokens_ecdsa(state, 256, "ecdsa256_no_pub_uri", "0");
    setup_tokens_ecdsa(state, 384, "ecdsa384_no_pub_uri", "0");
    setup_tokens_ecdsa(state, 521, "ecdsa521_no_pub_uri", "0");

    return 0;
}

static int teardown_directory_structure(void **state)
{
    struct pki_st *test_state = *state;
    int rc;

    torture_cleanup_tokens(test_state->temp_dir);

    rc = torture_change_dir(test_state->orig_dir);
    assert_int_equal(rc, 0);

    rc = torture_rmdirs(test_state->temp_dir);
    assert_int_equal(rc, 0);

    SAFE_FREE(test_state->temp_dir);
    SAFE_FREE(test_state->orig_dir);
    SAFE_FREE(test_state);

    return 0;
}

static void torture_pki_ecdsa_import_pubkey_uri(void **state, const char *label)
{
    char uri[128] = {0};
    ssh_key pubkey = NULL;
    int rc;

    rc = snprintf(uri, sizeof(uri), PUB_URI_FMT, label, label);
    assert_in_range(rc, 0, sizeof(uri) - 1);

    rc = ssh_pki_import_pubkey_file(uri, &pubkey);
    assert_return_code(rc, errno);
    assert_non_null(pubkey);

    rc = ssh_key_is_public(pubkey);
    assert_int_equal(rc, 1);

    SSH_KEY_FREE(pubkey);
}

static void torture_pki_ecdsa_import_pubkey_uri_256(void **state)
{
    torture_pki_ecdsa_import_pubkey_uri(state, LABEL_256);
}

static void torture_pki_ecdsa_import_pubkey_uri_384(void **state)
{
    torture_pki_ecdsa_import_pubkey_uri(state, LABEL_384);
}

static void torture_pki_ecdsa_import_pubkey_uri_521(void **state)
{
    torture_pki_ecdsa_import_pubkey_uri(state, LABEL_521);
}

static void
torture_pki_ecdsa_publickey_from_privatekey_uri(void **state,
                                                const char *label,
                                                const char *type)
{
    int rc;
    char uri[128] = {0};
    ssh_key privkey = NULL;
    ssh_key pubkey = NULL;
    ssh_string pblob = NULL;
    char pubkey_original[4096] = {0};
    char pubkey_generated[4096] = {0};
    char convert_key_to_pem[4096];
    char pub_filename[1024];
    char pub_filename_generated[1024];
    char pub_filename_pem[1024];

    rc = snprintf(uri, sizeof(uri), PRIV_URI_FMT, label, label);
    assert_in_range(rc, 0, sizeof(uri) - 1);

    rc = ssh_pki_import_privkey_file(uri,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_return_code(rc, errno);
    assert_non_null(privkey);

    rc = ssh_pki_export_pubkey_blob(privkey, &pblob);
    assert_return_code(rc, errno);
    assert_non_null(pblob);
    ssh_string_free(pblob);

    rc = ssh_pki_export_privkey_to_pubkey(privkey, &pubkey);
    assert_return_code(rc, errno);
    assert_non_null(pubkey);

    snprintf(pub_filename, sizeof(pub_filename), "%s%s%s", LIBSSH_ECDSA_TESTKEY, type, ".pub");
    snprintf(pub_filename_generated, sizeof(pub_filename_generated), "%s%s%s",
             LIBSSH_ECDSA_TESTKEY_PEM, type, "generated.pub");
    snprintf(pub_filename_pem, sizeof(pub_filename_pem), "%s%s%s", LIBSSH_ECDSA_TESTKEY_PEM, type, ".pub");

    rc = torture_read_one_line(pub_filename,
                               pubkey_original,
                               sizeof(pubkey_original));
    assert_return_code(rc, errno);

    rc = ssh_pki_export_pubkey_file(pubkey, pub_filename_generated);
    assert_return_code(rc, errno);

    /* remove the public key, generate it from the private key and write it. */
    unlink(pub_filename);

    snprintf(convert_key_to_pem, sizeof(convert_key_to_pem), "ssh-keygen -e -f %s -m PKCS8 > %s ",
            pub_filename_generated, pub_filename_pem);

    system(convert_key_to_pem);

    rc = torture_read_one_line(pub_filename_pem,
                               pubkey_generated,
                               sizeof(pubkey_generated));
    assert_return_code(rc, errno);

    assert_memory_equal(pubkey_original, pubkey_generated, strlen(pubkey_original));

    SSH_KEY_FREE(privkey);
    SSH_KEY_FREE(pubkey);
}

static void torture_pki_ecdsa_publickey_from_privatekey_uri_256(void **state)
{
    torture_pki_ecdsa_publickey_from_privatekey_uri(state, LABEL_256, "ecdsa256");
}

static void torture_pki_ecdsa_publickey_from_privatekey_uri_384(void **state)
{
    torture_pki_ecdsa_publickey_from_privatekey_uri(state, LABEL_384, "ecdsa384");
}

static void torture_pki_ecdsa_publickey_from_privatekey_uri_521(void **state)
{
    torture_pki_ecdsa_publickey_from_privatekey_uri(state, LABEL_521, "ecdsa521");
}

static void
import_pubkey_without_loading_public_uri(void **state, const char *label)
{
    int rc;
    char uri[128] = {0};
    ssh_key privkey = NULL;
    ssh_key pubkey = NULL;
    ssh_string pblob = NULL;

    rc = snprintf(uri, sizeof(uri), PRIV_URI_NO_PUB_FMT, label, label);
    assert_in_range(rc, 0, sizeof(uri) - 1);

    rc = ssh_pki_import_privkey_file(uri,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_return_code(rc, errno);
    assert_non_null(privkey);

    rc = ssh_pki_export_pubkey_blob(privkey, &pblob);
    assert_int_not_equal(rc, 0);
    assert_null(pblob);

    rc = ssh_pki_export_privkey_to_pubkey(privkey, &pubkey);
    assert_int_not_equal(rc, 0);
    assert_null(pubkey);

    SSH_KEY_FREE(privkey);
}

static void torture_pki_ecdsa_import_pubkey_without_loading_public_uri_256(void **state)
{
    import_pubkey_without_loading_public_uri(state, LABEL_256);
}

static void torture_pki_ecdsa_import_pubkey_without_loading_public_uri_384(void **state)
{
    import_pubkey_without_loading_public_uri(state, LABEL_384);
}

static void torture_pki_ecdsa_import_pubkey_without_loading_public_uri_521(void **state)
{
    import_pubkey_without_loading_public_uri(state, LABEL_521);
}

static void
torture_ecdsa_sign_verify_uri(void **state,
                              const char *label,
                              enum ssh_digest_e dig_type)
{
    int rc;
    char uri[128] = {0};
    ssh_key privkey = NULL, pubkey = NULL;
    ssh_signature sign = NULL;
    enum ssh_keytypes_e type = SSH_KEYTYPE_UNKNOWN;
    const char *type_char = NULL;
    const char *etype_char = NULL;
    ssh_session session = ssh_new();

    assert_non_null(session);

    rc = snprintf(uri, sizeof(uri), PRIV_URI_FMT, label, label);
    assert_in_range(rc, 0, sizeof(uri) - 1);

    rc = ssh_pki_import_privkey_file(uri,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_return_code(rc, errno);
    assert_non_null(privkey);

    rc = ssh_pki_export_privkey_to_pubkey(privkey, &pubkey);
    assert_return_code(rc, errno);
    assert_non_null(pubkey);

    sign = pki_do_sign(privkey, INPUT, sizeof(INPUT), dig_type);
    assert_non_null(sign);

    rc = ssh_pki_signature_verify(session, sign, pubkey, INPUT, sizeof(INPUT));
    assert_return_code(rc, errno);

    type = ssh_key_type(privkey);
    type_char = ssh_key_type_to_char(type);
    etype_char = ssh_pki_key_ecdsa_name(privkey);

    switch (dig_type) {
    case SSH_DIGEST_SHA256:
        assert_true(type == SSH_KEYTYPE_ECDSA_P256);
        assert_string_equal(type_char, "ecdsa-sha2-nistp256");
        assert_string_equal(etype_char, "ecdsa-sha2-nistp256");
        break;
    case SSH_DIGEST_SHA384:
        assert_true(type == SSH_KEYTYPE_ECDSA_P384);
        assert_string_equal(type_char, "ecdsa-sha2-nistp384");
        assert_string_equal(etype_char, "ecdsa-sha2-nistp384");
        break;
    case SSH_DIGEST_SHA512:
        assert_true(type == SSH_KEYTYPE_ECDSA_P521);
        assert_string_equal(type_char, "ecdsa-sha2-nistp521");
        assert_string_equal(etype_char, "ecdsa-sha2-nistp521");
        break;
    default:
        printf("Invalid hash type: %d\n", dig_type);
    }

    ssh_free(session);
    ssh_signature_free(sign);
    SSH_KEY_FREE(privkey);
    SSH_KEY_FREE(pubkey);
}

static void torture_ecdsa_sign_verify_uri_256(void **state)
{
    torture_ecdsa_sign_verify_uri(state, LABEL_256, SSH_DIGEST_SHA256);
}

static void torture_ecdsa_sign_verify_uri_384(void **state)
{
    torture_ecdsa_sign_verify_uri(state, LABEL_384, SSH_DIGEST_SHA384);
}

static void torture_ecdsa_sign_verify_uri_521(void **state)
{
    torture_ecdsa_sign_verify_uri(state, LABEL_521, SSH_DIGEST_SHA512);
}

static void torture_pki_ecdsa_duplicate_key_uri(void **state, const char *label)
{
    int rc;
    char pub_uri[128] = {0};
    char priv_uri[128] = {0};
    char *b64_key = NULL;
    char *b64_key_gen = NULL;
    ssh_key pubkey = NULL;
    ssh_key pubkey_dup = NULL;
    ssh_key privkey = NULL;
    ssh_key privkey_dup = NULL;

    (void) state;

    rc = snprintf(pub_uri, sizeof(pub_uri), PUB_URI_FMT, label, label);
    assert_in_range(rc, 0, sizeof(pub_uri) - 1);
    rc = snprintf(priv_uri, sizeof(priv_uri), PRIV_URI_FMT, label, label);
    assert_in_range(rc, 0, sizeof(priv_uri) - 1);

    rc = ssh_pki_import_pubkey_file(pub_uri, &pubkey);
    assert_return_code(rc, errno);
    assert_non_null(pubkey);

    rc = ssh_pki_export_pubkey_base64(pubkey, &b64_key);
    assert_return_code(rc, errno);
    assert_non_null(b64_key);

    rc = ssh_pki_import_privkey_file(priv_uri,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_return_code(rc, errno);
    assert_non_null(privkey);

    privkey_dup = ssh_key_dup(privkey);
    assert_non_null(privkey_dup);

    rc = ssh_pki_export_privkey_to_pubkey(privkey, &pubkey_dup);
    assert_return_code(rc, errno);
    assert_non_null(pubkey_dup);

    rc = ssh_pki_export_pubkey_base64(pubkey_dup, &b64_key_gen);
    assert_return_code(rc, errno);
    assert_non_null(b64_key_gen);

    assert_string_equal(b64_key, b64_key_gen);

    rc = ssh_key_cmp(privkey, privkey_dup, SSH_KEY_CMP_PRIVATE);
    assert_return_code(rc, errno);

    rc = ssh_key_cmp(pubkey, pubkey_dup, SSH_KEY_CMP_PUBLIC);
    assert_return_code(rc, errno);

    SSH_KEY_FREE(pubkey);
    SSH_KEY_FREE(pubkey_dup);
    SSH_KEY_FREE(privkey);
    SSH_KEY_FREE(privkey_dup);
    SSH_STRING_FREE_CHAR(b64_key);
    SSH_STRING_FREE_CHAR(b64_key_gen);
}

static void torture_pki_ecdsa_duplicate_key_uri_256(void **state)
{
    torture_pki_ecdsa_duplicate_key_uri(state, LABEL_256);
}

static void torture_pki_ecdsa_duplicate_key_uri_384(void **state)
{
    torture_pki_ecdsa_duplicate_key_uri(state, LABEL_384);
}

static void torture_pki_ecdsa_duplicate_key_uri_521(void **state)
{
    torture_pki_ecdsa_duplicate_key_uri(state, LABEL_521);
}

static void
torture_pki_ecdsa_duplicate_then_demote_uri(void **state, const char *label)
{
    char priv_uri[128] = {0};
    ssh_key pubkey = NULL;
    ssh_key privkey = NULL;
    ssh_key privkey_dup = NULL;
    int rc;

    (void) state;

    rc = snprintf(priv_uri, sizeof(priv_uri), PRIV_URI_FMT, label, label);
    assert_in_range(rc, 0, sizeof(priv_uri) - 1);

    rc = ssh_pki_import_privkey_file(priv_uri,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_return_code(rc, errno);
    assert_non_null(privkey);

    privkey_dup = ssh_key_dup(privkey);
    assert_non_null(privkey_dup);
    assert_int_equal(privkey->ecdsa_nid, privkey_dup->ecdsa_nid);

    rc = ssh_pki_export_privkey_to_pubkey(privkey_dup, &pubkey);
    assert_return_code(rc, errno);
    assert_non_null(pubkey);
    assert_int_equal(pubkey->ecdsa_nid, privkey->ecdsa_nid);

    SSH_KEY_FREE(pubkey);
    SSH_KEY_FREE(privkey);
    SSH_KEY_FREE(privkey_dup);
}

static void torture_pki_ecdsa_duplicate_then_demote_uri_256(void **state)
{
    torture_pki_ecdsa_duplicate_then_demote_uri(state, LABEL_256);
}

static void torture_pki_ecdsa_duplicate_then_demote_uri_384(void **state)
{
    torture_pki_ecdsa_duplicate_then_demote_uri(state, LABEL_384);
}

static void torture_pki_ecdsa_duplicate_then_demote_uri_521(void **state)
{
    torture_pki_ecdsa_duplicate_then_demote_uri(state, LABEL_521);
}

static void torture_pki_ecdsa_import_pubkey_uri_invalid_configurations(void **state)
{
    ssh_key privkey = NULL;
    ssh_key pubkey = NULL;
    int rc;

    /** invalid token for already setup Private PKCS #11 URI */
    rc = ssh_pki_import_privkey_file(PRIV_URI_FMT_384_INVALID_TOKEN,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_int_not_equal(rc, 0);
    assert_null(privkey);

    /** invalid object for already setup Private PKCS #11 URI */
    rc = ssh_pki_import_privkey_file(PRIV_URI_FMT_521_INVALID_OBJECT,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_int_not_equal(rc, 0);
    assert_null(privkey);
    /** invalid token for already setup Public PKCS #11 URI */
    rc = ssh_pki_import_pubkey_file(PUB_URI_FMT_384_INVALID_TOKEN,
                                     &pubkey);
    assert_int_not_equal(rc, 0);
    assert_null(pubkey);

    /** invalid object for already setup Public PKCS #11 URI */
    rc = ssh_pki_import_pubkey_file(PUB_URI_FMT_521_INVALID_OBJECT,
                                     &pubkey);
    assert_int_not_equal(rc, 0);
    assert_null(pubkey);
}

int torture_run_tests(void) {
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_pki_ecdsa_import_pubkey_uri_256),
        cmocka_unit_test(torture_pki_ecdsa_import_pubkey_uri_384),
        cmocka_unit_test(torture_pki_ecdsa_import_pubkey_uri_521),
        cmocka_unit_test(torture_pki_ecdsa_publickey_from_privatekey_uri_256),
        cmocka_unit_test(torture_pki_ecdsa_publickey_from_privatekey_uri_384),
        cmocka_unit_test(torture_pki_ecdsa_publickey_from_privatekey_uri_521),
        cmocka_unit_test(torture_ecdsa_sign_verify_uri_256),
        cmocka_unit_test(torture_ecdsa_sign_verify_uri_384),
        cmocka_unit_test(torture_ecdsa_sign_verify_uri_521),
        cmocka_unit_test(torture_pki_ecdsa_duplicate_key_uri_256),
        cmocka_unit_test(torture_pki_ecdsa_duplicate_key_uri_384),
        cmocka_unit_test(torture_pki_ecdsa_duplicate_key_uri_521),
        cmocka_unit_test(torture_pki_ecdsa_duplicate_then_demote_uri_256),
        cmocka_unit_test(torture_pki_ecdsa_duplicate_then_demote_uri_384),
        cmocka_unit_test(torture_pki_ecdsa_duplicate_then_demote_uri_521),

        /** Expect fail on these negative test cases **/
        cmocka_unit_test(torture_pki_ecdsa_import_pubkey_uri_invalid_configurations),
        cmocka_unit_test(torture_pki_ecdsa_import_pubkey_without_loading_public_uri_256),
        cmocka_unit_test(torture_pki_ecdsa_import_pubkey_without_loading_public_uri_384),
        cmocka_unit_test(torture_pki_ecdsa_import_pubkey_without_loading_public_uri_521),
    };
    ssh_session session = ssh_new();
    int verbosity = SSH_LOG_FUNCTIONS;

    /* Do not use system openssl.cnf for the pkcs11 uri tests.
     * It can load a pkcs11 provider too early before we will set up environment
     * variables that are needed for the pkcs11 provider to access correct
     * tokens, causing unexpected failures.
     * Make sure this comes before ssh_init(), which initializes OpenSSL!
     */
    setenv("OPENSSL_CONF", "/dev/null", 1);

    ssh_init();
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, setup_directory_structure, teardown_directory_structure);

    ssh_free(session);
    ssh_finalize();

    return rc;
}
