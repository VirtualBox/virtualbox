/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2024 by Red Hat, Inc.
 *
 * Authors: Jakub Jelen <jjelen@redhat.com>
 *          Sahana Prasad <sahana@redhat.com>
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

#include "pki.c"
#include "torture.h"
#include "torture_key.h"
#include "torture_pki.h"

#define LIBSSH_EDDSA_TESTKEY            "libssh_testkey.id_ed25519"
#define LIBSSH_EDDSA_TESTKEY_PASSPHRASE "libssh_testkey_passphrase.id_ed25519"
#define PUB_URI_FMT  "pkcs11:token=%s;object=%s;type=public"
#define PRIV_URI_FMT "pkcs11:token=%s;object=%s;type=private?pin-value=%s"

const char template[] = "/tmp/temp_dir_XXXXXX";
const unsigned char INPUT[] = "1234567890123456789012345678901234567890"
                              "123456789012345678901234";
struct pki_st {
    char *orig_dir;
    char *temp_dir;
    char *pub_uri;
    char *priv_uri;
    char *priv_uri_invalid_object;
    char *priv_uri_invalid_token;
    char *pub_uri_invalid_object;
    char *pub_uri_invalid_token;
};

static int setup_tokens(void **state)
{
    char keys_path[1024] = {0};
    char keys_path_pub[1024] = {0};
    char *cwd = NULL;
    struct pki_st *test_state = *state;
    char obj_tempname[] = "label_XXXXXX";
    char pub_uri[1024] = {0};
    char priv_uri[1024] = {0};
    char pub_uri_invalid_object[1024] = {0};
    char priv_uri_invalid_object[1024] = {0};
    char pub_uri_invalid_token[1024] = {0};
    char priv_uri_invalid_token[1024] = {0};

    cwd = test_state->temp_dir;
    assert_non_null(cwd);

    ssh_tmpname(obj_tempname);

    snprintf(pub_uri, sizeof(pub_uri), PUB_URI_FMT, obj_tempname, obj_tempname);

    snprintf(priv_uri,
             sizeof(priv_uri),
             PRIV_URI_FMT,
             obj_tempname,
             obj_tempname,
             "1234");

    snprintf(pub_uri_invalid_token,
             sizeof(pub_uri_invalid_token),
             PUB_URI_FMT,
             "invalid",
             obj_tempname);

    snprintf(priv_uri_invalid_token,
             sizeof(priv_uri_invalid_token),
             PRIV_URI_FMT,
             "invalid",
             obj_tempname,
             "1234");

    snprintf(pub_uri_invalid_object,
             sizeof(pub_uri_invalid_object),
             PUB_URI_FMT,
             obj_tempname,
             "invalid");

    snprintf(priv_uri_invalid_object,
             sizeof(priv_uri_invalid_object),
             PRIV_URI_FMT,
             obj_tempname,
             "invalid",
             "1234");

    snprintf(keys_path, sizeof(keys_path), "%s/%s", cwd, LIBSSH_EDDSA_TESTKEY);

    snprintf(keys_path_pub,
             sizeof(keys_path_pub),
             "%s/%s.pub",
             cwd,
             LIBSSH_EDDSA_TESTKEY);

    test_state->pub_uri = strdup(pub_uri);
    test_state->priv_uri = strdup(priv_uri);
    test_state->pub_uri_invalid_token = strdup(pub_uri_invalid_token);
    test_state->pub_uri_invalid_object = strdup(pub_uri_invalid_object);
    test_state->priv_uri_invalid_token = strdup(priv_uri_invalid_token);
    test_state->priv_uri_invalid_object = strdup(priv_uri_invalid_object);

    torture_write_file(keys_path, torture_get_testkey(SSH_KEYTYPE_ED25519, 0));
    torture_write_file(keys_path_pub,
                       torture_get_testkey_pub_pem(SSH_KEYTYPE_ED25519));

    torture_setup_tokens(cwd, keys_path, obj_tempname, "1");

    return 0;
}

static int setup_directory_structure(void **state)
{
    struct pki_st *test_state = NULL;
    char *temp_dir = NULL;
    int rc;

    test_state = (struct pki_st *)malloc(sizeof(struct pki_st));
    assert_non_null(test_state);

    test_state->orig_dir = torture_get_current_working_dir();
    assert_non_null(test_state->orig_dir);

    temp_dir = torture_make_temp_dir(template);
    assert_non_null(temp_dir);

    rc = torture_change_dir(temp_dir);
    assert_int_equal(rc, 0);
    free(temp_dir);

    test_state->temp_dir = torture_get_current_working_dir();
    assert_non_null(test_state->temp_dir);

    *state = test_state;

    rc = setup_tokens(state);
    assert_int_equal(rc, 0);

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
    SAFE_FREE(test_state->priv_uri);
    SAFE_FREE(test_state->pub_uri);
    SAFE_FREE(test_state->priv_uri_invalid_object);
    SAFE_FREE(test_state->pub_uri_invalid_object);
    SAFE_FREE(test_state->priv_uri_invalid_token);
    SAFE_FREE(test_state->pub_uri_invalid_token);
    SAFE_FREE(test_state);

    return 0;
}

static void torture_pki_ed25519_import_pubkey_uri(void **state)
{
    ssh_key pubkey = NULL;
    int rc;
    struct pki_st *test_state = *state;

    rc = ssh_pki_import_pubkey_file(test_state->pub_uri, &pubkey);

    assert_return_code(rc, errno);
    assert_non_null(pubkey);

    rc = ssh_key_is_public(pubkey);
    assert_int_equal(rc, 1);

    SSH_KEY_FREE(pubkey);
}

static void torture_pki_ed25519_import_privkey_uri(void **state)
{
    int rc;
    ssh_key privkey = NULL;
    struct pki_st *test_state = *state;

    rc = ssh_pki_import_privkey_file(test_state->priv_uri,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_return_code(rc, errno);
    assert_non_null(privkey);

    rc = ssh_key_is_private(privkey);
    assert_int_equal(rc, 1);

    SSH_KEY_FREE(privkey);
}

static void torture_pki_sign_verify_uri(void **state)
{
    int rc;
    ssh_key privkey = NULL, pubkey = NULL;
    ssh_signature sign = NULL;
    ssh_session session = ssh_new();
    struct pki_st *test_state = *state;

    rc = ssh_pki_import_privkey_file(test_state->priv_uri,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_return_code(rc, errno);
    assert_non_null(privkey);

    rc = ssh_pki_import_pubkey_file(test_state->pub_uri, &pubkey);
    assert_return_code(rc, errno);
    assert_non_null(pubkey);

    sign = pki_do_sign(privkey, INPUT, sizeof(INPUT), SSH_DIGEST_AUTO);
    assert_non_null(sign);

    rc = ssh_pki_signature_verify(session, sign, pubkey, INPUT, sizeof(INPUT));
    assert_return_code(rc, errno);

    ssh_signature_free(sign);
    SSH_KEY_FREE(privkey);
    SSH_KEY_FREE(pubkey);

    ssh_free(session);
}

static void torture_pki_ed25519_publickey_from_privatekey_uri(void **state)
{
    int rc;
    ssh_key privkey = NULL;
    ssh_key pubkey = NULL;
    struct pki_st *test_state = *state;

    rc = ssh_pki_import_privkey_file(test_state->priv_uri,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_return_code(rc, errno);
    assert_non_null(privkey);

    rc = ssh_key_is_private(privkey);
    assert_int_equal(rc, 1);

    rc = ssh_pki_export_privkey_to_pubkey(privkey, &pubkey);
    assert_return_code(rc, errno);
    assert_non_null(pubkey);

    SSH_KEY_FREE(privkey);
    SSH_KEY_FREE(pubkey);
}

static void torture_pki_ed25519_uri_invalid_configurations(void **state)
{
    int rc;
    ssh_key pubkey = NULL;
    ssh_key privkey = NULL;

    struct pki_st *test_state = *state;

    rc = ssh_pki_import_pubkey_file(test_state->pub_uri_invalid_object, &pubkey);
    assert_int_not_equal(rc, 0);
    assert_null(pubkey);

    rc = ssh_pki_import_pubkey_file(test_state->pub_uri_invalid_token, &pubkey);
    assert_int_not_equal(rc, 0);
    assert_null(pubkey);

    rc = ssh_pki_import_privkey_file(test_state->priv_uri_invalid_object,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_int_not_equal(rc, 0);
    assert_null(privkey);

    rc = ssh_pki_import_privkey_file(test_state->priv_uri_invalid_token,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &privkey);
    assert_int_not_equal(rc, 0);
    assert_null(privkey);
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_pki_ed25519_import_pubkey_uri),
        cmocka_unit_test(torture_pki_ed25519_import_privkey_uri),
        cmocka_unit_test(torture_pki_sign_verify_uri),
        cmocka_unit_test(torture_pki_ed25519_publickey_from_privatekey_uri),
        cmocka_unit_test(torture_pki_ed25519_uri_invalid_configurations),
    };

    ssh_session session = ssh_new();
    int verbosity = torture_libssh_verbosity();

    /* Skip test FIPS mode altogether. */
    if (ssh_fips_mode()) {
        return 0;
    }

    /* Do not use system openssl.cnf for the pkcs11 uri tests.
     * It can load a pkcs11 provider too early before we will set up environment
     * variables that are needed for the pkcs11 provider to access correct
     * tokens, causing unexpected failures.
     * Make sure this comes before ssh_init(), which initializes OpenSSL!
     */
    setenv("OPENSSL_CONF", SOURCEDIR "/tests/etc/openssl.cnf", 1);

    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests,
                                setup_directory_structure,
                                teardown_directory_structure);

    ssh_free(session);

    ssh_finalize();

    return rc;
}
