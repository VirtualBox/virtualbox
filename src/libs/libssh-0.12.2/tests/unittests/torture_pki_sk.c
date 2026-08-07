/*
 * torture_pki_sk.c - Torture tests for PKI security key functions
 *
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
#include "sk_common.c"
#include "torture.h"
#include "torture_pki.h"
#include "torture_sk.h"

#include <string.h>

/**
 * These tests can also be configured to run with the sk-usbhid callbacks
 * instead of the default sk-dummy callbacks which can run in a CI
 * environment.
 *
 * To run these tests with the sk-usbhid callbacks, at least one FIDO2 device
 * must be connected and the environment variables TORTURE_SK_USBHID and
 * TORTURE_SK_PIN must be set.
 *
 * The TORTURE_SK_PIN environment variable should contain the PIN used to
 * unlock the FIDO2 device for operations.
 *
 * Note that these tests must be run in the order that they are defined in, as
 * the signing tests rely on the output of the enrollment tests.
 */

/* Test constants */

/* Default PIN value which will be overridden with the PIN set in the
 * environment variable. */
static const char *test_pin = NULL;
static const char *test_application = "ssh:test@example.com";
static const unsigned char test_message[] = "Test signing data for SK keys";

static const char test_challenge[] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};

/* Global keys for testing */
static ssh_key g_ecdsa_key = NULL;
static ssh_key g_ed25519_key = NULL;

static const struct ssh_sk_callbacks_struct *g_sk_callbacks = NULL;
bool valid_sk_callbacks = false;

static int test_pin_callback(UNUSED_PARAM(const char *prompt),
                             char *buf,
                             size_t len,
                             UNUSED_PARAM(int echo),
                             UNUSED_PARAM(int verify),
                             UNUSED_PARAM(void *userdata))
{
    size_t pin_len;

    if (test_pin == NULL) {
        return SSH_ERROR;
    }

    pin_len = strlen(test_pin);
    if (pin_len + 1 > len) {
        return -1; /* buffer too small */
    }

    memcpy(buf, test_pin, pin_len);
    buf[pin_len] = '\0';
    return SSH_OK;
}

static void torture_pki_sk_enroll_generic_key(enum ssh_keytypes_e key_type)
{
    ssh_key pubkey = NULL, reimported_privkey = NULL, reimported_pubkey = NULL;
    ssh_pki_ctx enroll_ctx = NULL;
    const char *privkey_filename = NULL;
    const char *pubkey_filename = NULL;
    const char *test_user_id = NULL;
    ssh_key *ptr_to_g_key = NULL;
    ssh_auth_callback pin_callback = NULL;
    int rc;

    /* Conditions to skip the test */
    if (!valid_sk_callbacks) {
        skip();
    }

    if (key_type == SSH_KEYTYPE_SK_ED25519 && ssh_fips_mode()) {
        skip();
    }

    /* Setup based on key type */
    switch (key_type) {
    case SSH_KEYTYPE_SK_ECDSA:
        privkey_filename = "test_sk_ecdsa_private.key";
        pubkey_filename = "test_sk_ecdsa_public.pub";
        test_user_id = "libssh_test_ecdsa_sk";
        ptr_to_g_key = &g_ecdsa_key;
        break;

    case SSH_KEYTYPE_SK_ED25519:
        privkey_filename = "test_sk_ed25519_private.key";
        pubkey_filename = "test_sk_ed25519_public.pub";
        test_user_id = "libssh_test_ed25519_sk";
        ptr_to_g_key = &g_ed25519_key;
        break;

    default:
        /* Should never reach here */
        assert_true(0);
        return;
    }

    if (test_pin != NULL) {
        pin_callback = test_pin_callback;
    }

    enroll_ctx = torture_create_sk_pki_ctx(test_application,
                                           SSH_SK_USER_PRESENCE_REQD,
                                           test_challenge,
                                           sizeof(test_challenge),
                                           pin_callback,
                                           NULL,
                                           test_user_id,
                                           g_sk_callbacks);
    assert_non_null(enroll_ctx);

    rc = ssh_pki_generate_key(key_type, enroll_ctx, ptr_to_g_key);
    assert_int_equal(rc, SSH_OK);
    assert_sk_key_valid(*ptr_to_g_key, key_type, true);

    /* Export private key to file */
    rc = ssh_pki_export_privkey_file(*ptr_to_g_key,
                                     NULL, /* no passphrase */
                                     NULL, /* no auth callback */
                                     NULL, /* no auth data */
                                     privkey_filename);
    assert_int_equal(rc, SSH_OK);

    /* Extract public key from private key */
    rc = ssh_pki_export_privkey_to_pubkey(*ptr_to_g_key, &pubkey);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(pubkey);

    /* Export public key to file */
    rc = ssh_pki_export_pubkey_file(pubkey, pubkey_filename);
    assert_int_equal(rc, SSH_OK);

    /* Verify exported files by importing them back */
    rc = ssh_pki_import_privkey_file(privkey_filename,
                                     NULL, /* no passphrase */
                                     NULL, /* no auth callback */
                                     NULL, /* no auth data */
                                     &reimported_privkey);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(reimported_privkey);

    rc = ssh_pki_import_pubkey_file(pubkey_filename, &reimported_pubkey);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(reimported_pubkey);

    /* Verify keys match */
    rc = ssh_key_cmp(*ptr_to_g_key, reimported_privkey, SSH_KEY_CMP_PRIVATE);
    assert_int_equal(rc, 0);

    rc = ssh_key_cmp(pubkey, reimported_pubkey, SSH_KEY_CMP_PUBLIC);
    assert_int_equal(rc, 0);

    rc = ssh_key_cmp(*ptr_to_g_key, reimported_pubkey, SSH_KEY_CMP_PUBLIC);
    assert_int_equal(rc, 0);

    rc = ssh_key_cmp(reimported_privkey, pubkey, SSH_KEY_CMP_PUBLIC);
    assert_int_equal(rc, 0);

    /* Cleanup */
    unlink(privkey_filename);
    unlink(pubkey_filename);

    SSH_KEY_FREE(pubkey);
    SSH_KEY_FREE(reimported_privkey);
    SSH_KEY_FREE(reimported_pubkey);
    SSH_PKI_CTX_FREE(enroll_ctx);
}

static void torture_pki_sk_enroll_ecdsa_key(UNUSED_PARAM(void **state))
{
    torture_pki_sk_enroll_generic_key(SSH_KEYTYPE_SK_ECDSA);
}

static void torture_pki_sk_enroll_ed25519_key(UNUSED_PARAM(void **state))
{
    torture_pki_sk_enroll_generic_key(SSH_KEYTYPE_SK_ED25519);
}

static void
torture_pki_sk_enroll_generic_resident_key(enum ssh_keytypes_e key_type)
{
    ssh_key resident_key = NULL;
    ssh_pki_ctx enroll_ctx = NULL;
    const char *test_user_id = NULL;
    ssh_auth_callback pin_callback = NULL;
    int rc, flags;

    /* Conditions to skip the test */
    if (!valid_sk_callbacks) {
        skip();
    }

    if (key_type == SSH_KEYTYPE_SK_ED25519 && ssh_fips_mode()) {
        skip();
    }

    /* Setup based on key type */
    switch (key_type) {
    case SSH_KEYTYPE_SK_ECDSA:
        test_user_id = "libssh_test_ecdsa_sk";
        break;

    case SSH_KEYTYPE_SK_ED25519:
        test_user_id = "libssh_test_ed25519_sk";
        break;

    default:
        /* Should never reach here */
        assert_true(0);
        return;
    }

    flags = SSH_SK_USER_PRESENCE_REQD | SSH_SK_RESIDENT_KEY |
            SSH_SK_FORCE_OPERATION;

    if (test_pin != NULL) {
        pin_callback = test_pin_callback;
    }

    enroll_ctx = torture_create_sk_pki_ctx(test_application,
                                           flags,
                                           test_challenge,
                                           sizeof(test_challenge),
                                           pin_callback,
                                           NULL,
                                           test_user_id,
                                           g_sk_callbacks);
    assert_non_null(enroll_ctx);

    rc = ssh_pki_generate_key(key_type, enroll_ctx, &resident_key);
    assert_int_equal(rc, SSH_OK);
    assert_sk_key_valid(resident_key, key_type, true);

    SSH_KEY_FREE(resident_key);
    SSH_PKI_CTX_FREE(enroll_ctx);
}

static void torture_pki_sk_enroll_ecdsa_resident_key(UNUSED_PARAM(void **state))
{
    torture_pki_sk_enroll_generic_resident_key(SSH_KEYTYPE_SK_ECDSA);
}

static void
torture_pki_sk_enroll_ed25519_resident_key(UNUSED_PARAM(void **state))
{
    torture_pki_sk_enroll_generic_resident_key(SSH_KEYTYPE_SK_ED25519);
}

static void torture_pki_sk_sign_generic_key(enum ssh_keytypes_e key_type)
{
    ssh_signature signature = NULL;
    ssh_key public_key = NULL;
    ssh_pki_ctx sign_ctx = NULL;
    ssh_key *ptr_to_g_key = NULL;
    ssh_auth_callback pin_callback = NULL;
    int rc;

    /* Conditions to skip the test */
    if (!valid_sk_callbacks) {
        skip();
    }

    if (key_type == SSH_KEYTYPE_SK_ED25519 && ssh_fips_mode()) {
        skip();
    }

    /* Select the appropriate global key based on key type */
    switch (key_type) {
    case SSH_KEYTYPE_SK_ECDSA:
        ptr_to_g_key = &g_ecdsa_key;
        break;

    case SSH_KEYTYPE_SK_ED25519:
        ptr_to_g_key = &g_ed25519_key;
        break;

    default:
        /* Should never reach here */
        assert_true(0);
        return;
    }

    assert_non_null(*ptr_to_g_key);

    rc = ssh_pki_export_privkey_to_pubkey(*ptr_to_g_key, &public_key);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(public_key);

    if (test_pin != NULL) {
        pin_callback = test_pin_callback;
    }

    sign_ctx = torture_create_sk_pki_ctx(test_application,
                                         SSH_SK_USER_PRESENCE_REQD,
                                         test_challenge,
                                         sizeof(test_challenge),
                                         pin_callback,
                                         NULL,
                                         NULL,
                                         g_sk_callbacks);
    assert_non_null(sign_ctx);

    signature = pki_sk_do_sign(sign_ctx,
                               *ptr_to_g_key,
                               test_message,
                               sizeof(test_message) - 1);
    assert_non_null(signature);
    assert_sk_signature_valid(signature,
                              key_type,
                              public_key,
                              test_message,
                              sizeof(test_message) - 1);

    SSH_SIGNATURE_FREE(signature);
    SSH_KEY_FREE(public_key);
    SSH_PKI_CTX_FREE(sign_ctx);
}

static void torture_pki_sk_sign_ecdsa_key(UNUSED_PARAM(void **state))
{
    torture_pki_sk_sign_generic_key(SSH_KEYTYPE_SK_ECDSA);
}

static void torture_pki_sk_sign_ed25519_key(UNUSED_PARAM(void **state))
{
    torture_pki_sk_sign_generic_key(SSH_KEYTYPE_SK_ED25519);
}

static void torture_pki_sk_load_resident_keys(UNUSED_PARAM(void **state))
{
    ssh_pki_ctx load_ctx = NULL;
    ssh_key *resident_keys = NULL;
    size_t num_keys = 0;
    size_t i;
    int rc;

    /* Conditions to skip the test */
    if (!valid_sk_callbacks || torture_sk_is_using_sk_dummy()) {
        skip();
    }

    load_ctx = ssh_pki_ctx_new();
    assert_non_null(load_ctx);

    assert_non_null(test_pin);
    rc = ssh_pki_ctx_set_sk_pin_callback(load_ctx, test_pin_callback, NULL);
    assert_int_equal(rc, SSH_OK);

    if (g_sk_callbacks != NULL) {
        rc = ssh_pki_ctx_options_set(load_ctx,
                                     SSH_PKI_OPTION_SK_CALLBACKS,
                                     g_sk_callbacks);
        assert_int_equal(rc, SSH_OK);
    }

    rc = ssh_sk_resident_keys_load(load_ctx, &resident_keys, &num_keys);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(resident_keys);
    assert_true(num_keys > 0);

    for (i = 0; i < num_keys; i++) {
        ssh_key key = resident_keys[i];
        assert_non_null(key);

        assert_true(key->type == SSH_KEYTYPE_SK_ECDSA ||
                    key->type == SSH_KEYTYPE_SK_ED25519);

        assert_true(key->sk_flags & SSH_SK_RESIDENT_KEY);

        assert_true(key->sk_flags & SSH_SK_USER_PRESENCE_REQD);

        if (key->type == SSH_KEYTYPE_SK_ECDSA) {
            assert_sk_key_valid(key, SSH_KEYTYPE_SK_ECDSA, true);
        } else if (key->type == SSH_KEYTYPE_SK_ED25519) {
            if (!ssh_fips_mode()) {
                assert_sk_key_valid(key, SSH_KEYTYPE_SK_ED25519, true);
            }
        }
    }

    for (i = 0; i < num_keys; i++) {
        SSH_KEY_FREE(resident_keys[i]);
    }
    SAFE_FREE(resident_keys);

    SSH_PKI_CTX_FREE(load_ctx);
}

static void
torture_pki_ctx_sk_callbacks_options_clear(UNUSED_PARAM(void **state))
{
    ssh_pki_ctx ctx = NULL;
    int rc;

    /* Test with NULL context - should return SSH_ERROR */
    rc = ssh_pki_ctx_sk_callbacks_options_clear(NULL);
    assert_int_equal(rc, SSH_ERROR);

    /* Create a new PKI context */
    ctx = ssh_pki_ctx_new();
    assert_non_null(ctx);

    /* Test clearing options on a context with no options set - should succeed
     */
    rc = ssh_pki_ctx_sk_callbacks_options_clear(ctx);
    assert_int_equal(rc, SSH_OK);

    /* Add some options to the context */
    rc = ssh_pki_ctx_sk_callbacks_option_set(ctx,
                                             SSH_SK_OPTION_NAME_DEVICE_PATH,
                                             "/dev/hidraw0",
                                             false);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_pki_ctx_sk_callbacks_option_set(ctx,
                                             SSH_SK_OPTION_NAME_USER_ID,
                                             "test_user",
                                             true);
    assert_int_equal(rc, SSH_OK);

    /* Clear all options - should succeed */
    rc = ssh_pki_ctx_sk_callbacks_options_clear(ctx);
    assert_int_equal(rc, SSH_OK);

    /* Verify that we can add options again after clearing */
    rc = ssh_pki_ctx_sk_callbacks_option_set(ctx,
                                             SSH_SK_OPTION_NAME_DEVICE_PATH,
                                             "/dev/hidraw1",
                                             false);
    assert_int_equal(rc, SSH_OK);

    /* Clear options again */
    rc = ssh_pki_ctx_sk_callbacks_options_clear(ctx);
    assert_int_equal(rc, SSH_OK);

    /* Test multiple clears on same context - should succeed */
    rc = ssh_pki_ctx_sk_callbacks_options_clear(ctx);
    assert_int_equal(rc, SSH_OK);

    SSH_PKI_CTX_FREE(ctx);
}

/* Setup function to run before all tests */
static int setup_global_state(UNUSED_PARAM(void **state))
{
    const struct ssh_sk_callbacks_struct *sk_callbacks = NULL;
    const char *test_pin_env = NULL;

    sk_callbacks = torture_get_sk_callbacks();
    if (sk_callbacks != NULL) {
        g_sk_callbacks = sk_callbacks;
        valid_sk_callbacks = true;
    }

    test_pin_env = torture_get_sk_pin();
    if (test_pin_env != NULL) {
        test_pin = test_pin_env;
    }

    return 0;
}

/* Teardown function to run after all tests */
static int teardown_global_state(UNUSED_PARAM(void **state))
{

    /* Clean up global keys */
    SSH_KEY_FREE(g_ecdsa_key);
    SSH_KEY_FREE(g_ed25519_key);

    return 0;
}

int torture_run_tests(void)
{
    int rc;

    struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_pki_sk_enroll_ecdsa_key),
        cmocka_unit_test(torture_pki_sk_enroll_ed25519_key),
        cmocka_unit_test(torture_pki_sk_enroll_ecdsa_resident_key),
        cmocka_unit_test(torture_pki_sk_enroll_ed25519_resident_key),
        cmocka_unit_test(torture_pki_sk_sign_ecdsa_key),
        cmocka_unit_test(torture_pki_sk_sign_ed25519_key),
        cmocka_unit_test(torture_pki_sk_load_resident_keys),
        cmocka_unit_test(torture_pki_ctx_sk_callbacks_options_clear),
    };

    ssh_init();
    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests,
                                setup_global_state,
                                teardown_global_state);
    ssh_finalize();

    return rc;
}
