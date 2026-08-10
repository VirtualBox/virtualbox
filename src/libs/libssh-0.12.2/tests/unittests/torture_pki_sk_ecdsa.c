/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2025 Praneeth Sarode <praneethsarode@gmail.com>
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
#include "torture_sk.h"

/* Test constants */
#define LIBSSH_SK_ECDSA_TESTKEY "libssh_testkey.id_ecdsa_sk"
#define LIBSSH_SK_ECDSA_TESTKEY_PASSPHRASE \
    "libssh_testkey_passphrase.id_ecdsa_sk"

const char template[] = "temp_dir_XXXXXX";

struct pki_st {
    char *cwd;
    char *temp_dir;
};

static int setup_sk_ecdsa_key(void **state)
{
    const char *keystring = NULL;
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

    keystring = torture_get_openssh_testkey(SSH_KEYTYPE_SK_ECDSA, 0);
    torture_write_file(LIBSSH_SK_ECDSA_TESTKEY, keystring);

    keystring = torture_get_openssh_testkey(SSH_KEYTYPE_SK_ECDSA, 1);
    torture_write_file(LIBSSH_SK_ECDSA_TESTKEY_PASSPHRASE, keystring);

    keystring = torture_get_testkey_pub(SSH_KEYTYPE_SK_ECDSA);
    torture_write_file(LIBSSH_SK_ECDSA_TESTKEY ".pub", keystring);

    return 0;
}

static int teardown(void **state)
{
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

static void torture_pki_sk_ecdsa_import_pubkey_file(void **state)
{
    ssh_key pubkey = NULL;
    int rc;

    (void)state; /* unused */

    rc = ssh_pki_import_pubkey_file(LIBSSH_SK_ECDSA_TESTKEY ".pub", &pubkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(pubkey, SSH_KEYTYPE_SK_ECDSA, false);

    SSH_KEY_FREE(pubkey);
}

static void
torture_pki_sk_ecdsa_import_pubkey_from_openssh_privkey(void **state)
{
    ssh_key pubkey = NULL;
    int rc;

    (void)state; /* unused */

    rc =
        ssh_pki_import_pubkey_file(LIBSSH_SK_ECDSA_TESTKEY_PASSPHRASE, &pubkey);
    assert_return_code(rc, errno);
    assert_non_null(pubkey);
    assert_sk_key_valid(pubkey, SSH_KEYTYPE_SK_ECDSA, false);

    SSH_KEY_FREE(pubkey);
}

static void torture_pki_sk_ecdsa_import_privkey_base64(void **state)
{
    ssh_key privkey = NULL;
    char *keystring = NULL;
    int rc;

    (void)state; /* unused */

    keystring = torture_pki_read_file(LIBSSH_SK_ECDSA_TESTKEY);
    assert_non_null(keystring);

    rc = ssh_pki_import_privkey_base64(keystring,
                                       NULL, /* no passphrase */
                                       NULL, /* no auth callback */
                                       NULL, /* no auth data */
                                       &privkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(privkey, SSH_KEYTYPE_SK_ECDSA, true);

    SAFE_FREE(keystring);
    SSH_KEY_FREE(privkey);
}

static void torture_pki_sk_ecdsa_import_privkey_base64_comment(void **state)
{
    int rc, file_str_len;
    const char *comment_str = "#this is line-comment\n#this is another\n";
    char *file_str = NULL;
    ssh_key key = NULL;
    char *keystring = NULL;

    (void)state; /* unused */

    keystring = torture_pki_read_file(LIBSSH_SK_ECDSA_TESTKEY);
    assert_non_null(keystring);

    file_str_len = strlen(comment_str) + strlen(keystring) + 1;
    file_str = malloc(file_str_len);
    assert_non_null(file_str);
    rc = snprintf(file_str, file_str_len, "%s%s", comment_str, keystring);
    assert_int_equal(rc, file_str_len - 1);

    rc = ssh_pki_import_privkey_base64(file_str, NULL, NULL, NULL, &key);
    assert_return_code(rc, errno);
    assert_sk_key_valid(key, SSH_KEYTYPE_SK_ECDSA, true);

    SAFE_FREE(keystring);
    SAFE_FREE(file_str);
    SSH_KEY_FREE(key);
}

static void torture_pki_sk_ecdsa_import_privkey_base64_whitespace(void **state)
{
    int rc, file_str_len;
    const char *whitespace_str = "  \t\t\t\n\n\n";
    char *file_str = NULL;
    ssh_key key = NULL;
    char *keystring = NULL;

    (void)state; /* unused */

    keystring = torture_pki_read_file(LIBSSH_SK_ECDSA_TESTKEY);
    assert_non_null(keystring);

    file_str_len = 2 * strlen(whitespace_str) + strlen(keystring) + 1;
    file_str = malloc(file_str_len);
    assert_non_null(file_str);
    rc = snprintf(file_str,
                  file_str_len,
                  "%s%s%s",
                  whitespace_str,
                  keystring,
                  whitespace_str);
    assert_int_equal(rc, file_str_len - 1);

    rc = ssh_pki_import_privkey_base64(file_str, NULL, NULL, NULL, &key);
    assert_return_code(rc, errno);
    assert_sk_key_valid(key, SSH_KEYTYPE_SK_ECDSA, true);

    SAFE_FREE(keystring);
    SAFE_FREE(file_str);
    SSH_KEY_FREE(key);
}

static void torture_pki_sk_ecdsa_import_export_privkey_base64(void **state)
{
    ssh_key origkey = NULL;
    ssh_key privkey = NULL;
    char *key_buf = NULL;
    const char *keystring = NULL;
    int rc;

    (void)state; /* unused */

    keystring = torture_get_openssh_testkey(SSH_KEYTYPE_SK_ECDSA, 0);
    assert_non_null(keystring);

    rc = ssh_pki_import_privkey_base64(keystring, NULL, NULL, NULL, &origkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(origkey, SSH_KEYTYPE_SK_ECDSA, true);

    rc = ssh_pki_export_privkey_base64(origkey, NULL, NULL, NULL, &key_buf);
    assert_return_code(rc, errno);
    assert_non_null(key_buf);

    rc = ssh_pki_import_privkey_base64(key_buf, NULL, NULL, NULL, &privkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(privkey, SSH_KEYTYPE_SK_ECDSA, true);

    rc = ssh_key_cmp(origkey, privkey, SSH_KEY_CMP_PRIVATE);
    assert_int_equal(rc, 0);

    SSH_KEY_FREE(origkey);
    SSH_KEY_FREE(privkey);
    SSH_STRING_FREE_CHAR(key_buf);
}

static void torture_pki_sk_ecdsa_publickey_from_privatekey(void **state)
{
    ssh_key privkey = NULL;
    ssh_key pubkey = NULL;
    const char *keystring = NULL;
    int rc;

    (void)state; /* unused */

    keystring = torture_get_openssh_testkey(SSH_KEYTYPE_SK_ECDSA, 0);
    assert_non_null(keystring);

    rc = ssh_pki_import_privkey_base64(keystring,
                                       NULL, /* no passphrase */
                                       NULL, /* no auth callback */
                                       NULL, /* no auth data */
                                       &privkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(privkey, SSH_KEYTYPE_SK_ECDSA, true);

    rc = ssh_pki_export_privkey_to_pubkey(privkey, &pubkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(pubkey, SSH_KEYTYPE_SK_ECDSA, false);

    rc = ssh_key_cmp(privkey, pubkey, SSH_KEY_CMP_PUBLIC);
    assert_int_equal(rc, 0);

    SSH_KEY_FREE(privkey);
    SSH_KEY_FREE(pubkey);
}

static void torture_pki_sk_ecdsa_import_privkey_base64_passphrase(void **state)
{
    ssh_key privkey = NULL;
    const char *keystring = NULL;
    const char *passphrase = NULL;
    int rc;

    (void)state; /* unused */

    keystring = torture_get_openssh_testkey(SSH_KEYTYPE_SK_ECDSA, 1);
    assert_non_null(keystring);

    passphrase = torture_get_testkey_passphrase();
    assert_non_null(passphrase);

    /* Import with a passphrase */
    rc = ssh_pki_import_privkey_base64(keystring,
                                       passphrase,
                                       NULL,
                                       NULL,
                                       &privkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(privkey, SSH_KEYTYPE_SK_ECDSA, true);
    SSH_KEY_FREE(privkey);

    rc = ssh_pki_import_privkey_base64(keystring,
                                       "wrong passphrase",
                                       NULL,
                                       NULL,
                                       &privkey);
    assert_int_equal(rc, SSH_ERROR);
    assert_null(privkey);
}

static void torture_pki_sk_ecdsa_duplicate_key(void **state)
{
    ssh_key privkey = NULL;
    ssh_key duplicated = NULL;
    const char *keystring = NULL;
    int rc;

    (void)state; /* unused */

    keystring = torture_get_openssh_testkey(SSH_KEYTYPE_SK_ECDSA, 0);
    assert_non_null(keystring);

    rc = ssh_pki_import_privkey_base64(keystring,
                                       NULL, /* no passphrase */
                                       NULL, /* no auth callback */
                                       NULL, /* no auth data */
                                       &privkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(privkey, SSH_KEYTYPE_SK_ECDSA, true);

    duplicated = ssh_key_dup(privkey);
    assert_sk_key_valid(duplicated, SSH_KEYTYPE_SK_ECDSA, true);

    rc = ssh_key_cmp(privkey, duplicated, SSH_KEY_CMP_PRIVATE);
    assert_int_equal(rc, 0);

    SSH_KEY_FREE(privkey);
    SSH_KEY_FREE(duplicated);
}

static void torture_pki_sk_ecdsa_import_pubkey_base64(void **state)
{
    ssh_key key = NULL;
    ssh_key pubkey = NULL;
    char *b64_key = NULL;
    const char *keystring = NULL;
    int rc;

    (void)state; /* unused */

    keystring = torture_get_openssh_testkey(SSH_KEYTYPE_SK_ECDSA, 0);
    assert_non_null(keystring);

    /* Import private key to extract public key */
    rc = ssh_pki_import_privkey_base64(keystring, NULL, NULL, NULL, &key);
    assert_return_code(rc, errno);
    assert_sk_key_valid(key, SSH_KEYTYPE_SK_ECDSA, true);

    rc = ssh_pki_export_privkey_to_pubkey(key, &pubkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(pubkey, SSH_KEYTYPE_SK_ECDSA, false);

    /* Export public key to base64 */
    rc = ssh_pki_export_pubkey_base64(pubkey, &b64_key);
    assert_return_code(rc, errno);
    assert_non_null(b64_key);

    SSH_KEY_FREE(key);
    SSH_KEY_FREE(pubkey);

    /* Import public key from base64 */
    rc = ssh_pki_import_pubkey_base64(b64_key, SSH_KEYTYPE_SK_ECDSA, &pubkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(pubkey, SSH_KEYTYPE_SK_ECDSA, false);

    SSH_KEY_FREE(pubkey);
    SSH_STRING_FREE_CHAR(b64_key);
}

static void torture_pki_sk_ecdsa_pubkey_blob(void **state)
{
    ssh_key privkey = NULL;
    ssh_key pubkey = NULL;
    ssh_key imported_pubkey = NULL;
    ssh_string pub_blob = NULL;
    const char *keystring = NULL;
    int rc;

    (void)state; /* unused */

    keystring = torture_get_openssh_testkey(SSH_KEYTYPE_SK_ECDSA, 0);
    assert_non_null(keystring);

    rc = ssh_pki_import_privkey_base64(keystring, NULL, NULL, NULL, &privkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(privkey, SSH_KEYTYPE_SK_ECDSA, true);

    rc = ssh_pki_export_privkey_to_pubkey(privkey, &pubkey);
    assert_return_code(rc, errno);
    assert_sk_key_valid(pubkey, SSH_KEYTYPE_SK_ECDSA, false);

    /* Export public key to blob */
    rc = ssh_pki_export_pubkey_blob(pubkey, &pub_blob);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(pub_blob);

    /* Import public key from blob */
    rc = ssh_pki_import_pubkey_blob(pub_blob, &imported_pubkey);
    assert_int_equal(rc, SSH_OK);
    assert_sk_key_valid(imported_pubkey, SSH_KEYTYPE_SK_ECDSA, false);

    /* Compare keys */
    rc = ssh_key_cmp(pubkey, imported_pubkey, SSH_KEY_CMP_PUBLIC);
    assert_int_equal(rc, 0);

    ssh_string_free(pub_blob);
    SSH_KEY_FREE(privkey);
    SSH_KEY_FREE(pubkey);
    SSH_KEY_FREE(imported_pubkey);
}

int torture_run_tests(void)
{
    int rc;

    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_pki_sk_ecdsa_import_pubkey_file,
                                        setup_sk_ecdsa_key,
                                        teardown),
        cmocka_unit_test_setup_teardown(
            torture_pki_sk_ecdsa_import_pubkey_from_openssh_privkey,
            setup_sk_ecdsa_key,
            teardown),
        cmocka_unit_test_setup_teardown(
            torture_pki_sk_ecdsa_import_privkey_base64,
            setup_sk_ecdsa_key,
            teardown),
        cmocka_unit_test_setup_teardown(
            torture_pki_sk_ecdsa_import_privkey_base64_comment,
            setup_sk_ecdsa_key,
            teardown),
        cmocka_unit_test_setup_teardown(
            torture_pki_sk_ecdsa_import_privkey_base64_whitespace,
            setup_sk_ecdsa_key,
            teardown),
        cmocka_unit_test_setup_teardown(
            torture_pki_sk_ecdsa_import_export_privkey_base64,
            setup_sk_ecdsa_key,
            teardown),
        cmocka_unit_test_setup_teardown(
            torture_pki_sk_ecdsa_publickey_from_privatekey,
            setup_sk_ecdsa_key,
            teardown),
        cmocka_unit_test_setup_teardown(
            torture_pki_sk_ecdsa_import_pubkey_base64,
            setup_sk_ecdsa_key,
            teardown),
        cmocka_unit_test_setup_teardown(
            torture_pki_sk_ecdsa_import_privkey_base64_passphrase,
            setup_sk_ecdsa_key,
            teardown),
        cmocka_unit_test_setup_teardown(torture_pki_sk_ecdsa_duplicate_key,
                                        setup_sk_ecdsa_key,
                                        teardown),

        cmocka_unit_test_setup_teardown(torture_pki_sk_ecdsa_pubkey_blob,
                                        setup_sk_ecdsa_key,
                                        teardown),

    };

    ssh_init();
    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, NULL, NULL);
    ssh_finalize();

    return rc;
}
