/*
 * torture_sk_usbhid.c - Torture tests for security key USB-HID
 * callbacks.
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

#include "libssh/sk_common.h"
#include "torture.h"
#include "torture_sk.h"

/**
 * These tests require at least one FIDO2 device to be connected
 * and the environment variables TORTURE_SK_USBHID and TORTURE_SK_PIN to be set.
 *
 * If TORTURE_SK_USBHID is not set, these tests will be skipped.
 * To enable these tests, set both environment variables before running:
 *
 *     export TORTURE_SK_USBHID=1
 *     export TORTURE_SK_PIN=your_device_pin
 *
 * The TORTURE_SK_PIN environment variable should contain the PIN used to
 * unlock the FIDO2 device for operations.
 *
 * Note that these tests must be run in the order that they are defined in, as
 * the signing tests rely on the output of the enrollment tests.
 */

static const char *test_pin = NULL;
static const char *test_application = "ssh:test@example.com";

static const uint8_t dummy_data[] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};

/* Global variables to store key handles for signing tests */
static uint8_t *ecdsa_key_handle = NULL;
static size_t ecdsa_key_handle_len = 0;
static uint8_t *ed25519_key_handle = NULL;
static size_t ed25519_key_handle_len = 0;

/* Check if tests should run */
static bool should_run_tests(void)
{
    char *env = getenv("TORTURE_SK_USBHID");
    return (env != NULL && env[0] != '\0');
}

static struct sk_option **create_user_id_option(const char *user_id)
{
    struct sk_option **array = NULL, *option = NULL;

    array = calloc(2, sizeof(struct sk_option *));
    assert_non_null(array);

    option = calloc(1, sizeof(struct sk_option));
    assert_non_null(option);

    option->name = strdup(SSH_SK_OPTION_NAME_USER_ID);
    assert_non_null(option->name);
    option->value = strdup(user_id);
    assert_non_null(option->value);
    option->required = 0;

    array[0] = option;
    array[1] = NULL;

    return array;
}

static void torture_sk_usbhid_enroll_generic_key(enum ssh_keytypes_e key_type)
{
    const struct ssh_sk_callbacks_struct *callbacks = NULL;
    struct sk_enroll_response *response = NULL;
    struct sk_option **options = NULL;
    const char *user_id = NULL;
    uint8_t **key_handle_out = NULL;
    size_t *key_handle_len_out = NULL;
    int rc, flags;

    callbacks = ssh_sk_get_default_callbacks();
    assert_non_null(callbacks);
    assert_true(ssh_callbacks_exists(callbacks, enroll));

    /* Setup based on key type */
    switch (key_type) {
    case SSH_SK_ECDSA:
        user_id = "libssh_test_ecdsa_sk";
        key_handle_out = &ecdsa_key_handle;
        key_handle_len_out = &ecdsa_key_handle_len;
        break;
    case SSH_SK_ED25519:
        user_id = "libssh_test_ed25519_sk";
        key_handle_out = &ed25519_key_handle;
        key_handle_len_out = &ed25519_key_handle_len;
        break;
    default:
        /* Should never reach here */
        assert_true(0);
        return;
    }

    options = create_user_id_option(user_id);

    /* Enroll non-resident key */
    flags = SSH_SK_USER_PRESENCE_REQD;
    rc = callbacks->enroll(key_type,
                           dummy_data,
                           sizeof(dummy_data),
                           test_application,
                           flags,
                           test_pin,
                           options,
                           &response);
    assert_int_equal(rc, SSH_OK);
    assert_sk_enroll_response(response, flags);

    /* Store the non-resident key handle for signing tests */
    *key_handle_out = calloc(response->key_handle_len, 1);
    assert_non_null(*key_handle_out);
    memcpy(*key_handle_out, response->key_handle, response->key_handle_len);
    *key_handle_len_out = response->key_handle_len;

    SK_ENROLL_RESPONSE_FREE(response);
    SK_OPTIONS_FREE(options);
}

static void
torture_sk_usbhid_enroll_generic_resident_key(enum ssh_keytypes_e key_type)
{
    const struct ssh_sk_callbacks_struct *callbacks = NULL;
    struct sk_enroll_response *response = NULL;
    struct sk_option **options = NULL;
    const char *user_id = NULL;
    int rc, flags;

    callbacks = ssh_sk_get_default_callbacks();
    assert_non_null(callbacks);
    assert_true(ssh_callbacks_exists(callbacks, enroll));

    /* Setup based on key type */
    switch (key_type) {
    case SSH_SK_ECDSA:
        user_id = "libssh_test_ecdsa_sk";
        break;
    case SSH_SK_ED25519:
        user_id = "libssh_test_ed25519_sk";
        break;
    default:
        /* Should never reach here */
        assert_true(0);
        return;
    }

    options = create_user_id_option(user_id);

    /* Enroll first resident key */
    flags = SSH_SK_USER_PRESENCE_REQD | SSH_SK_RESIDENT_KEY |
            SSH_SK_FORCE_OPERATION;
    rc = callbacks->enroll(key_type,
                           dummy_data,
                           sizeof(dummy_data),
                           test_application,
                           flags,
                           test_pin,
                           options,
                           &response);
    assert_int_equal(rc, SSH_OK);
    assert_sk_enroll_response(response, flags);
    SK_ENROLL_RESPONSE_FREE(response);

    /* Try to enroll same resident key again - should fail with
     * SSH_SK_ERR_CREDENTIAL_EXISTS */
    flags = SSH_SK_USER_PRESENCE_REQD | SSH_SK_RESIDENT_KEY;
    rc = callbacks->enroll(key_type,
                           dummy_data,
                           sizeof(dummy_data),
                           test_application,
                           flags,
                           test_pin,
                           options,
                           &response);
    assert_int_equal(rc, SSH_SK_ERR_CREDENTIAL_EXISTS);
    SK_ENROLL_RESPONSE_FREE(response);

    /* The force operation flag should overwrite the existing resident key with
     * new one */
    flags = SSH_SK_USER_PRESENCE_REQD | SSH_SK_RESIDENT_KEY |
            SSH_SK_FORCE_OPERATION;
    rc = callbacks->enroll(key_type,
                           dummy_data,
                           sizeof(dummy_data),
                           test_application,
                           flags,
                           test_pin,
                           options,
                           &response);
    assert_int_equal(rc, SSH_OK);
    assert_sk_enroll_response(response, flags);
    SK_ENROLL_RESPONSE_FREE(response);
    SK_OPTIONS_FREE(options);
}

static void torture_sk_usbhid_enroll_ecdsa_key(UNUSED_PARAM(void **state))
{
    torture_sk_usbhid_enroll_generic_key(SSH_SK_ECDSA);
}

static void torture_sk_usbhid_enroll_ed25519_key(UNUSED_PARAM(void **state))
{
    torture_sk_usbhid_enroll_generic_key(SSH_SK_ED25519);
}

static void
torture_sk_usbhid_enroll_ecdsa_resident_key(UNUSED_PARAM(void **state))
{
    torture_sk_usbhid_enroll_generic_resident_key(SSH_SK_ECDSA);
}

static void
torture_sk_usbhid_enroll_ed25519_resident_key(UNUSED_PARAM(void **state))
{
    torture_sk_usbhid_enroll_generic_resident_key(SSH_SK_ED25519);
}

static void torture_sk_usbhid_sign_generic(enum ssh_keytypes_e key_type)
{
    const struct ssh_sk_callbacks_struct *callbacks;
    struct sk_sign_response *response = NULL;
    uint8_t *key_handle = NULL;
    size_t key_handle_len = 0;
    int rc, flags;

    /* Setup based on key type */
    switch (key_type) {
    case SSH_SK_ECDSA:
        key_handle = ecdsa_key_handle;
        key_handle_len = ecdsa_key_handle_len;
        break;
    case SSH_SK_ED25519:
        key_handle = ed25519_key_handle;
        key_handle_len = ed25519_key_handle_len;
        break;
    default:
        /* Should never reach here */
        assert_true(0);
        return;
    }

    assert_non_null(key_handle);
    assert_true(key_handle_len > 0);

    callbacks = ssh_sk_get_default_callbacks();
    assert_non_null(callbacks);
    assert_true(ssh_callbacks_exists(callbacks, sign));

    flags = SSH_SK_USER_PRESENCE_REQD;
    rc = callbacks->sign(key_type,
                         dummy_data,
                         sizeof(dummy_data),
                         test_application,
                         key_handle,
                         key_handle_len,
                         flags,
                         test_pin,
                         NULL,
                         &response);
    assert_int_equal(rc, SSH_OK);
    assert_sk_sign_response(response, key_type);

    SK_SIGN_RESPONSE_FREE(response);
}

static void torture_sk_usbhid_sign_ecdsa(UNUSED_PARAM(void **state))
{
    torture_sk_usbhid_sign_generic(SSH_SK_ECDSA);
}

static void torture_sk_usbhid_sign_ed25519(UNUSED_PARAM(void **state))
{
    torture_sk_usbhid_sign_generic(SSH_SK_ED25519);
}

static void torture_sk_usbhid_load_resident_keys(UNUSED_PARAM(void **state))
{
    const struct ssh_sk_callbacks_struct *callbacks;
    struct sk_resident_key **resident_keys = NULL;
    size_t num_keys = 0;
    int rc;

    callbacks = ssh_sk_get_default_callbacks();
    assert_non_null(callbacks);
    assert_true(ssh_callbacks_exists(callbacks, load_resident_keys));

    rc = callbacks->load_resident_keys(test_pin,
                                       NULL,
                                       &resident_keys,
                                       &num_keys);
    assert_int_equal(rc, SSH_OK);
    assert_non_null(resident_keys);
    assert_true(num_keys > 0);

    for (size_t i = 0; i < num_keys; i++) {
        assert_sk_resident_key(resident_keys[i]);
        SK_RESIDENT_KEY_FREE(resident_keys[i]);
    }

    free(resident_keys);
}

static int setup(UNUSED_PARAM(void **state))
{
    const char *test_pin_env = NULL;

    test_pin_env = torture_get_sk_pin();
    if (test_pin_env != NULL) {
        test_pin = test_pin_env;
    }

    return 0;
}

static int cleanup(UNUSED_PARAM(void **state))
{
    SAFE_FREE(ecdsa_key_handle);
    SAFE_FREE(ed25519_key_handle);

    return 0;
}

int torture_run_tests(void)
{
    int rc;
    bool should_run;

    struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_sk_usbhid_enroll_ecdsa_key),
        cmocka_unit_test(torture_sk_usbhid_enroll_ed25519_key),
        cmocka_unit_test(torture_sk_usbhid_enroll_ecdsa_resident_key),
        cmocka_unit_test(torture_sk_usbhid_enroll_ed25519_resident_key),
        cmocka_unit_test(torture_sk_usbhid_sign_ecdsa),
        cmocka_unit_test(torture_sk_usbhid_sign_ed25519),
        cmocka_unit_test(torture_sk_usbhid_load_resident_keys),
    };

    /*
     * Only run tests if TORTURE_SK_USBHID environment variable is set
     * and we expect a FIDO2 device to be available.
     */
    should_run = should_run_tests();
    if (!should_run) {
        printf("Skipping sk_usbhid tests: TORTURE_SK_USBHID not set\n");
        return 0; /* Success, but no tests run */
    }

    ssh_init();
    rc = cmocka_run_group_tests(tests, setup, cleanup);
    ssh_finalize();

    return rc;
}
