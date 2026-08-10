/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2025 Praneeth Sarode <praneethsarode@gmail.com>
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 2.1 of the License.
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

#include "libssh/callbacks.h"
#include "libssh/misc.h"
#include "libssh/pki.h"
#include "libssh/sk_api.h"
#include "libssh/sk_common.h"
#include "libssh/sk_usbhid.h"

#include <fido.h>
#include <fido/credman.h>
#include <fido/err.h>

#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define SK_USBHID_API_VERSION 0x000a0000

#define ECDSA_P256_PUBKEY_LEN 64

/* Maximum number of FIDO2/U2F devices that can be connected */
#define MAX_FIDO_DEVICES 8

/* Timeout for touch detection on single FIDO2/U2F device during each polling */
#define FIDO_POLL_MS 50

/* Sleep between each consecutive polling */
#define POLL_SLEEP_NS 200000000

/* The entire timeout for user to touch any of the connected devices */
#define SELECT_MS 15000

/* DER encoding constants */
#define DER_SEQUENCE_TAG  0x30
#define DER_INTEGER_TAG   0x02
#define DER_MAX_LEN_BYTES 2

struct sk_device {
    char *path;
    fido_dev_t *fido_device;
};

/**
 * libfido2 log handler that prints libfido2 debug messages.
 *
 * @param msg The log message from libfido2
 */
static void fido_log_handler(const char *msg)
{
    if (msg == NULL) {
        return;
    }

    SSH_LOG(SSH_LOG_TRACE, "libfido2: %s", msg);
}

/**
 * Initialize libfido2 with appropriate logging settings based on
 * current libssh log level.
 */
static void sk_fido_init(void)
{
    int fido_flags = 0;
    int log_level = ssh_get_log_level();

    /* Enable libfido2 debug output if libssh is at TRACE level */
    if (log_level == SSH_LOG_TRACE) {
        fido_flags |= FIDO_DEBUG;
        fido_set_log_handler(fido_log_handler);
    }

    fido_init(fido_flags);
}

/**
 * Convert a libfido2 error code to a libssh security key error code.
 *
 * @param fido_err The FIDO error code to convert
 *
 * @return The corresponding SSH_SK_ERR_* error code
 */
static int fido_err_to_ssh_sk_err(int fido_err)
{
    switch (fido_err) {
    case FIDO_ERR_UNSUPPORTED_OPTION:
    case FIDO_ERR_UNSUPPORTED_ALGORITHM:
    case FIDO_ERR_UNSUPPORTED_EXTENSION:
        return SSH_SK_ERR_UNSUPPORTED;
    case FIDO_ERR_PIN_REQUIRED:
    case FIDO_ERR_PIN_INVALID:
        return SSH_SK_ERR_PIN_REQUIRED;
    default:
        return SSH_SK_ERR_GENERAL;
    }
}

static void sk_device_close(struct sk_device *device)
{
    int rc;

    if (device == NULL) {
        return;
    }

    if (device->fido_device != NULL) {
        rc = fido_dev_cancel(device->fido_device);
        if (rc != FIDO_OK) {
            SSH_LOG(SSH_LOG_WARN,
                    "Failed to cancel device operations: %s",
                    fido_strerr(rc));
        }

        rc = fido_dev_close(device->fido_device);
        if (rc != FIDO_OK) {
            SSH_LOG(SSH_LOG_WARN,
                    "Failed to close device: %s",
                    fido_strerr(rc));
        }

        fido_dev_free(&device->fido_device);
    }

    SAFE_FREE(device->path);
    SAFE_FREE(device);
}

static struct sk_device *sk_device_open(const char *device_path)
{
    int rc;
    struct sk_device *device = NULL;

    if (device_path == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Device path cannot be NULL");
        goto error;
    }

    device = calloc(1, sizeof(struct sk_device));
    if (device == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for sk_device");
        goto error;
    }

    device->fido_device = fido_dev_new();
    if (device->fido_device == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create new fido device instance");
        goto error;
    }

    device->path = strdup(device_path);
    if (device->path == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for device path");
        goto error;
    }

    rc = fido_dev_open(device->fido_device, device->path);
    if (rc != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to open FIDO2/U2F device at %s: %s",
                device->path,
                fido_strerr(rc));
        goto error;
    }

    return device;

error:
    sk_device_close(device);
    return NULL;
}

static void sk_device_close_list(struct sk_device **devices, size_t num_devices)
{
    size_t i;

    if (devices == NULL) {
        return;
    }

    for (i = 0; i < num_devices; i++) {
        sk_device_close(devices[i]);
    }
    SAFE_FREE(devices);
}

static struct sk_device **
sk_device_open_list(const fido_dev_info_t *device_list,
                    size_t num_devices,
                    size_t *num_opened)
{
    size_t i;
    const char *device_path = NULL;
    struct sk_device **devices = NULL;
    const fido_dev_info_t *device_info = NULL;

    *num_opened = 0;

    devices = calloc(num_devices, sizeof(struct sk_device *));
    if (devices == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for device list");
        return NULL;
    }

    for (i = 0; i < num_devices; i++) {
        device_info = fido_dev_info_ptr(device_list, i);
        if (device_info == NULL) {
            SSH_LOG(SSH_LOG_INFO, "Failed to get device info for index %zu", i);
            continue;
        }

        device_path = fido_dev_info_path(device_info);
        devices[*num_opened] = sk_device_open(device_path);
        if (devices[*num_opened] == NULL) {
            SSH_LOG(SSH_LOG_INFO,
                    "Failed to open device %zu at %s",
                    *num_opened,
                    device_path);
        } else {
            (*num_opened)++;
        }
    }

    if (*num_opened == 0) {
        sk_device_close_list(devices, num_devices);
        devices = NULL;
    }

    return devices;
}

/**
 * Check if given device has the credentials corresponding to the given
 * key_handle.
 *
 * @param device        The security key device to check
 * @param application   The application identifier (relying party ID)
 * @param key_handle    The key handle to check for
 * @param key_handle_len The length of the key handle in bytes
 *
 * @return FIDO_OK if resident key exists, FIDO_ERR_NO_CREDENTIALS if it
 *         doesn't, other FIDO_ERR_* codes on failure
 */
static int sk_device_check_key_handle(const struct sk_device *device,
                                      const char *application,
                                      const uint8_t *key_handle,
                                      size_t key_handle_len)
{
    int ret = FIDO_ERR_INTERNAL;
    uint8_t dummy_data[32] = {0};
    fido_assert_t *assert = NULL;
    bool is_dev_fido2 = false;

    /*
     * We make use of the pre-flight checking as described in
     * https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-20210615.html#pre-flight
     * to identify whether the device knows of the passed key_handle.
     */

    assert = fido_assert_new();
    if (assert == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create new FIDO assertion");
        return FIDO_ERR_INTERNAL;
    }

    ret = fido_assert_set_clientdata(assert, dummy_data, sizeof(dummy_data));
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set client data for assertion: %s",
                fido_strerr(ret));
        goto out;
    }

    ret = fido_assert_set_rp(assert, application);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set Relying Party for assertion: %s",
                fido_strerr(ret));
        goto out;
    }

    ret = fido_assert_set_up(assert, FIDO_OPT_FALSE);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set user presence for assertion: %s",
                fido_strerr(ret));
        goto out;
    }

    /* Allow assertions only from this particular key_handle */
    ret = fido_assert_allow_cred(assert, key_handle, key_handle_len);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to allow credential for assertion: %s",
                fido_strerr(ret));
        goto out;
    }

    is_dev_fido2 = fido_dev_is_fido2(device->fido_device);

    ret = fido_dev_get_assert(device->fido_device, assert, NULL);

    if (!is_dev_fido2 && ret == FIDO_ERR_USER_PRESENCE_REQUIRED) {
        /* U2F devices might return this */
        ret = FIDO_OK;
    } else if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_INFO,
                "Failed to get assertion from device: %s",
                fido_strerr(ret));
    }

out:
    fido_assert_free(&assert);
    return ret;
}

/**
 * Check if given device has the resident key with given user_id and
 * application.
 *
 * @param device        The security key device to check
 * @param application   The application identifier (relying party ID)
 * @param user_id       The binary user ID to search for in resident keys
 * @param user_id_len   Length of binary user_id
 * @param pin           The PIN for the device (can be NULL if not required)
 *
 * @return FIDO_OK if resident key exists, FIDO_ERR_NO_CREDENTIALS if it
 *         doesn't, other FIDO_ERR_* codes on failure
 */
static int sk_device_check_resident_key(const struct sk_device *device,
                                        const char *application,
                                        const uint8_t *user_id,
                                        size_t user_id_len,
                                        const char *pin)
{
    int rc, ret = FIDO_ERR_INTERNAL;
    bool supports_uv = false;
    size_t i, num_asserts = 0, len;
    const uint8_t *ptr = NULL;
    uint8_t dummy_data[32] = {0};
    fido_opt_t user_verification = FIDO_OPT_OMIT;
    fido_assert_t *assert = NULL;

    /* If no user_id or zero length provided, nothing to compare */
    if (user_id == NULL || user_id_len == 0) {
        return FIDO_ERR_NO_CREDENTIALS;
    }

    assert = fido_assert_new();
    if (assert == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create new FIDO assertion");
        goto out;
    }

    ret = fido_assert_set_clientdata(assert, dummy_data, sizeof(dummy_data));
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set client data for assertion: %s",
                fido_strerr(ret));
        goto out;
    }

    ret = fido_assert_set_rp(assert, application);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set Relying Party for assertion: %s",
                fido_strerr(ret));
        goto out;
    }

    /* Check if device supports internal user verification methods such as
     * biometric */
    supports_uv = fido_dev_supports_uv(device->fido_device);

    /*
     * Determine user verification strategy for resident key enumeration:
     * - If PIN is provided, rely on PIN-based authentication (UV = OMIT)
     *
     * - If no PIN is provided but device supports internal UV (biometric/etc),
     * enable UV to ensure we can access all resident keys regardless of their
     * credential protection while minimising user friction.
     *
     * - If no PIN is provided and device does not support internal UV, we will
     * only be able to access resident keys without user-verification
     * protection.
     *
     * Read about credential protection and resident keys:
     * (https://developers.yubico.com/WebAuthn/WebAuthn_Developer_Guide/Resident_Keys.html)
     */
    user_verification =
        (pin == NULL && supports_uv) ? FIDO_OPT_TRUE : FIDO_OPT_OMIT;
    ret = fido_assert_set_uv(assert, user_verification);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set user verification for assertion: %s",
                fido_strerr(ret));
        goto out;
    }

    ret = fido_dev_get_assert(device->fido_device, assert, pin);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to get assertion from device: %s",
                fido_strerr(ret));
        goto out;
    }

    ret = FIDO_ERR_NO_CREDENTIALS;

    num_asserts = fido_assert_count(assert);
    for (i = 0; i < num_asserts; i++) {
        ptr = fido_assert_user_id_ptr(assert, i);
        len = fido_assert_user_id_len(assert, i);

        if (len != user_id_len) {
            continue;
        }

        rc = memcmp(ptr, user_id, user_id_len);
        if (rc == 0) {
            SSH_LOG(SSH_LOG_INFO, "Resident key with given user ID exists");
            ret = FIDO_OK;
            break;
        }
    }

out:
    fido_assert_free(&assert);
    return ret;
}

/**
 * Begin touch detection on all devices in the provided list.
 *
 * @param devices Array of device pointers
 * @param num_devices Number of devices in the array
 *
 * @return SSH_OK if at least one device started touch detection successfully,
 *         SSH_ERROR if all devices failed
 */
static int sk_device_touch_begin(struct sk_device **devices, size_t num_devices)
{
    int rc;
    size_t i, num_success = 0;

    for (i = 0; i < num_devices; i++) {
        rc = fido_dev_get_touch_begin(devices[i]->fido_device);
        if (rc != FIDO_OK) {
            SSH_LOG(SSH_LOG_INFO,
                    "Failed to begin touch on device %s: %s",
                    devices[i]->path,
                    fido_strerr(rc));
        } else {
            num_success++;
        }
    }

    return (num_success > 0) ? SSH_OK : SSH_ERROR;
}

/**
 * Poll the touch status on all devices and return the index of the device
 * on which touch was detected.
 *
 * @param devices Array of device pointers to poll for touch status
 * @param num_devices Number of devices in the array
 * @param touch_detected Pointer to store whether touch was detected (0 or 1)
 * @param chosen_idx Pointer to store the index of the device that was touched
 *
 * @return SSH_OK on successful polling (regardless of whether touch was
 *         detected), SSH_ERROR if no devices left to poll.
 *
 * @warning Automatically closes the device if any error occurs
 *          while detecting if it was touched.
 */
static int sk_device_touch_poll(struct sk_device **devices,
                                size_t num_devices,
                                int *touch_detected,
                                size_t *chosen_idx)
{
    int rc;
    size_t i, n_failed = 0;

    for (i = 0; i < num_devices; i++) {
        if (devices[i] == NULL) {
            continue;
        }

        SSH_LOG(SSH_LOG_DEBUG,
                "Polling touch status on device %s",
                devices[i]->path);

        rc = fido_dev_get_touch_status(devices[i]->fido_device,
                                       touch_detected,
                                       FIDO_POLL_MS);
        if (rc != FIDO_OK) {
            SSH_LOG(SSH_LOG_INFO,
                    "Failed to get touch status on device %s: %s",
                    devices[i]->path,
                    fido_strerr(rc));
            sk_device_close(devices[i]);
            devices[i] = NULL;

            n_failed++;
            if (n_failed == num_devices) {
                SSH_LOG(SSH_LOG_WARN, "No devices left to poll");
                return SSH_ERROR;
            }
        } else if (*touch_detected) {
            *chosen_idx = i;
            return SSH_OK;
        }
    }

    *touch_detected = 0;
    return SSH_OK;
}

/**
 * Select a device from the list of devices which has the given
 * application and key handle.
 *
 * @param device_list Array of device information structures
 * @param num_devices Number of devices in the device_list array
 * @param application The application identifier (relying party ID)
 * @param key_handle The key handle to look for
 * @param key_handle_len The length of the key handle in bytes
 *
 * @return The selected device on success, NULL if no device found or on error.
 */
static struct sk_device *
sk_device_select_by_credential(const fido_dev_info_t *device_list,
                               size_t num_devices,
                               const char *application,
                               const uint8_t *key_handle,
                               size_t key_handle_len)
{
    int rc;
    size_t num_opened = 0, i;
    struct sk_device **devices = NULL, *selected_device = NULL;

    devices = sk_device_open_list(device_list, num_devices, &num_opened);
    if (devices == NULL) {
        SSH_LOG(SSH_LOG_WARN, "No FIDO2/U2F devices opened");
        return NULL;
    }

    selected_device = NULL;
    for (i = 0; i < num_opened; i++) {
        rc = sk_device_check_key_handle(devices[i],
                                        application,
                                        key_handle,
                                        key_handle_len);
        if (rc == FIDO_OK) {
            selected_device = devices[i];
            devices[i] = NULL;
            SSH_LOG(SSH_LOG_DEBUG,
                    "Selected device %s for key handle",
                    selected_device->path);
            break;
        }
    }

    sk_device_close_list(devices, num_opened);
    return selected_device;
}

/**
 * Select a device by touch, where the user touches the key they want to use.
 * The function will block until a touch is detected or the timeout is reached.
 *
 * @param device_list Array of device information structures
 * @param num_devices Number of devices in the device_list array
 *
 * @return The selected device on success, NULL if no device found or on error.
 */
static struct sk_device *
sk_device_select_by_touch(const fido_dev_info_t *device_list,
                          size_t num_devices)
{
    int rc, touch = 0;
    size_t num_opened = 0, chosen_idx;
    struct sk_device **devices = NULL, *selected_device = NULL;
    struct ssh_timestamp ts;

#ifndef _WIN32
    struct timespec poll_sleep = {.tv_sec = 0, .tv_nsec = POLL_SLEEP_NS};
#endif

    devices = sk_device_open_list(device_list, num_devices, &num_opened);
    if (devices == NULL) {
        SSH_LOG(SSH_LOG_WARN, "No FIDO2/U2F devices opened");
        return NULL;
    }

    if (num_opened == 1) {
        selected_device = devices[0];
        devices[0] = NULL;
        SSH_LOG(SSH_LOG_DEBUG,
                "Only one device opened, automatically selected %s",
                selected_device->path);
        goto out;
    }

    SSH_LOG(SSH_LOG_DEBUG, "%zu FIDO2/U2F device(s) opened", num_opened);

    rc = sk_device_touch_begin(devices, num_opened);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to begin touch on any device");
        goto out;
    }

    ssh_timestamp_init(&ts);
    do {
        rc = sk_device_touch_poll(devices, num_opened, &touch, &chosen_idx);
        if (rc != SSH_OK) {
            SSH_LOG(SSH_LOG_WARN, "Failed to poll touch status");
            goto out;
        } else if (touch) {
            selected_device = devices[chosen_idx];
            devices[chosen_idx] = NULL;
            goto out;
        }

        if (ssh_timeout_elapsed(&ts, SELECT_MS)) {
            SSH_LOG(SSH_LOG_WARN, "Touch selection timed out");
            break;
        }

#ifdef _WIN32
        /* Sleep expects milliseconds; convert nanoseconds (round down). */
        Sleep((DWORD)(POLL_SLEEP_NS / 1000000));
#else
        nanosleep(&poll_sleep, NULL);
#endif /* _WIN32 */

    } while (true);

out:
    sk_device_close_list(devices, num_opened);
    return selected_device;
}

/**
 * Probe for FIDO2/U2F devices and choose one based on the provided application
 * and key handle. If application or key handle are NULL, the user will be
 * prompted to touch the key they want to use.
 *
 * @param application The application identifier (relying party ID), can be NULL
 * @param key_handle The key handle to look for, can be NULL
 * @param key_handle_len The length of the key handle in bytes
 * @param probe_resident Whether to probe for resident keys
 *
 * @return The selected device on success, NULL if no device found or on error.
 */
static struct sk_device *sk_device_probe(const char *application,
                                         const uint8_t *key_handle,
                                         size_t key_handle_len,
                                         bool probe_resident)
{
    int rc;
    size_t num_devices = 0;
    struct sk_device *device = NULL;
    fido_dev_info_t *device_list = NULL;

#ifdef _WIN32
    if (!probe_resident) {
        device = sk_device_open("windows://hello");
        if (device == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Failed to open Windows Hello device");
            return NULL;
        }

        SSH_LOG(SSH_LOG_DEBUG, "Using Windows Hello device");
        return device;
    }
#endif /* _WIN32 */

    device_list = fido_dev_info_new(MAX_FIDO_DEVICES);
    if (device_list == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create device info list");
        return NULL;
    }

    rc = fido_dev_info_manifest(device_list, MAX_FIDO_DEVICES, &num_devices);
    if (rc != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to get device info manifest: %s",
                fido_strerr(rc));
        goto out;
    }
    if (num_devices == 0) {
        SSH_LOG(SSH_LOG_WARN, "No FIDO2/U2F devices found");
        goto out;
    }

    SSH_LOG(SSH_LOG_DEBUG, "%zu FIDO2/U2F device(s) detected", num_devices);

    /*
     * If key_handle and application are specified, then we find the key which
     * has the corresponding credentials, otherwise, we rely on the user to
     * touch the key that they want to use.
     */
    if (application != NULL && key_handle != NULL) {
        SSH_LOG(SSH_LOG_DEBUG, "Selecting device by credential");
        device = sk_device_select_by_credential(device_list,
                                                num_devices,
                                                application,
                                                key_handle,
                                                key_handle_len);
    } else {
        SSH_LOG(SSH_LOG_DEBUG, "Selecting device by touch");
        device = sk_device_select_by_touch(device_list, num_devices);
    }

out:
    fido_dev_info_free(&device_list, MAX_FIDO_DEVICES);
    return device;
}

/**
 * Export an ECDSA public key from a FIDO2/U2F credential.
 *
 * The format returned by libfido2 is different from the expected SEC1 octet
 * string representation, so this function performs the necessary conversion.
 *
 * @param credential The FIDO2/U2F credential containing the ECDSA public key
 * @param response The enrollment response structure to fill with the public key
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int export_public_key_ecdsa(const fido_cred_t *credential,
                                   struct sk_enroll_response *response)
{
    size_t len;
    const uint8_t *ptr = NULL;

    response->public_key = NULL;
    response->public_key_len = 0;

    ptr = fido_cred_pubkey_ptr(credential);
    if (ptr == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to get FIDO2/U2F credential public key");
        return SSH_ERROR;
    }

    len = fido_cred_pubkey_len(credential);
    if (len != ECDSA_P256_PUBKEY_LEN) {
        SSH_LOG(SSH_LOG_WARN,
                "Bad FIDO2/U2F credential public key length %zu"
                "(expected ecdsa public key length %d)",
                len,
                ECDSA_P256_PUBKEY_LEN);
        return SSH_ERROR;
    }

    /*
     * Convert from libfido2's raw coordinate format to SEC1 octet string
     * format.
     *
     * libfido2 returns: x_coordinate (32 bytes) + y_coordinate (32
     * bytes)
     *
     * SEC1 format expects: 0x04 + x_coordinate (32 bytes) + y_coordinate (32
     * bytes)
     */
    response->public_key_len = 1 + ECDSA_P256_PUBKEY_LEN;
    response->public_key = calloc(response->public_key_len, sizeof(uint8_t));
    if (response->public_key == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for public key");
        return SSH_ERROR;
    }

    /* SEC1 uncompressed point format: 0x04 prefix + raw coordinates */
    response->public_key[0] = 0x04;
    memcpy(response->public_key + 1, ptr, ECDSA_P256_PUBKEY_LEN);

    return SSH_OK;
}

/**
 * Export an Ed25519 public key from a FIDO2 credential.
 *
 * @param credential The FIDO2 credential containing the Ed25519 public key
 * @param response The enrollment response structure to fill with the public key
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int export_public_key_ed25519(const fido_cred_t *credential,
                                     struct sk_enroll_response *response)
{
    size_t len;
    const uint8_t *ptr = NULL;

    response->public_key = NULL;
    response->public_key_len = 0;

    ptr = fido_cred_pubkey_ptr(credential);
    if (ptr == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to get FIDO2 credential public key");
        return SSH_ERROR;
    }

    len = fido_cred_pubkey_len(credential);
    if (len != ED25519_KEY_LEN) {
        SSH_LOG(SSH_LOG_WARN,
                "Bad FIDO2 credential public key length %zu"
                " (expected ed25519 public key length %d)",
                len,
                ED25519_KEY_LEN);
        return SSH_ERROR;
    }

    response->public_key_len = len;
    response->public_key = calloc(response->public_key_len, sizeof(uint8_t));
    if (response->public_key == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for public key");
        return SSH_ERROR;
    }

    memcpy(response->public_key, ptr, len);
    return SSH_OK;
}

/**
 * Export a public key from a FIDO2/U2F credential based on the specified
 * algorithm.
 *
 * @param algorithm The key algorithm (SSH_SK_ECDSA or SSH_SK_ED25519)
 * @param credential The FIDO2/U2F credential containing the public key
 * @param response The enrollment response structure to fill with the public key
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int export_public_key(int algorithm,
                             const fido_cred_t *credential,
                             struct sk_enroll_response *response)
{
    int ret;

    switch (algorithm) {
    case SSH_SK_ECDSA:
        ret = export_public_key_ecdsa(credential, response);
        break;
    case SSH_SK_ED25519:
        ret = export_public_key_ed25519(credential, response);
        break;
    default:
        SSH_LOG(SSH_LOG_WARN, "Unsupported algorithm: %d", algorithm);
        ret = SSH_ERROR;
    }

    return ret;
}

/**
 * Parse DER length encoding.
 *
 * @param p Pointer to the current position in DER data (updated on success)
 * @param end Pointer to the end of DER data
 * @param length Pointer to store the parsed length
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int
parse_der_length(const uint8_t **p, const uint8_t *end, size_t *length)
{
    int len_bytes = 0;

    if (*p >= end) {
        SSH_LOG(SSH_LOG_WARN, "Insufficient data for DER length");
        return SSH_ERROR;
    }

    /* If the MSB is set, it indicates a long form length
     * where the lower 7 bits indicate the number of
     * subsequent bytes that represent the length.
     *
     * If the MSB is not set, it indicates a short form
     * length where the length is directly represented
     * in the the byte itself.
     */
    if (**p & 0x80) {
        /* Long form length */
        len_bytes = **p & 0x7f;
        (*p)++;

        if (len_bytes > DER_MAX_LEN_BYTES) {
            SSH_LOG(
                SSH_LOG_WARN,
                "Invalid DER length bytes: %d. Should not be greater than %d",
                len_bytes,
                DER_MAX_LEN_BYTES);
            return SSH_ERROR;
        }

        if (*p + len_bytes > end) {
            SSH_LOG(SSH_LOG_WARN, "Insufficient data for length bytes");
            return SSH_ERROR;
        }

        *length = 0;
        while (len_bytes--) {
            *length = (*length << 8) | **p;
            (*p)++;
        }
    } else {
        /* Short form length */
        *length = **p;
        (*p)++;
    }

    return SSH_OK;
}

/**
 * Parse a single DER-encoded INTEGER.
 *
 * @param p Pointer to the current position in DER data (updated on success)
 * @param end Pointer to the end of DER data
 * @param component_name Name of the component for error messages
 * @param int_ptr Pointer to store the integer data
 * @param int_len Pointer to store the integer length
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int parse_der_integer(const uint8_t **p,
                             const uint8_t *end,
                             const char *component_name,
                             uint8_t **int_ptr,
                             size_t *int_len)
{
    size_t length = 0;
    const uint8_t *data_ptr = NULL;
    int rc = SSH_ERROR;

    if (int_ptr == NULL || int_len == NULL) {
        SSH_LOG(SSH_LOG_WARN,
                "Invalid arguments provided for %s component",
                component_name);
        return SSH_ERROR;
    }

    *int_ptr = NULL;
    *int_len = 0;

    /* Check for INTEGER tag */
    if (*p >= end || **p != DER_INTEGER_TAG) {
        SSH_LOG(SSH_LOG_WARN,
                "Expected INTEGER tag for %s component",
                component_name);
        return SSH_ERROR;
    }
    (*p)++;

    /* Parse length */
    rc = parse_der_length(p, end, &length);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Invalid %s component length", component_name);
        return SSH_ERROR;
    }

    /* Verify we have enough data */
    if (*p + length > end) {
        SSH_LOG(SSH_LOG_WARN,
                "%s component extends beyond signature",
                component_name);
        return SSH_ERROR;
    }

    /* Skip leading zero if present (The leading zero is placed when the MSB of
     * the actual number is 1, so that it is not confused as a negative number
     * in 2's complement) */
    data_ptr = *p;
    if (length > 0 && **p == 0x00) {
        data_ptr++;
        length--;
    }

    /* Allocate memory for the integer data */
    if (length > 0) {
        *int_ptr = calloc(length, sizeof(uint8_t));
        if (*int_ptr == NULL) {
            SSH_LOG(SSH_LOG_WARN,
                    "Failed to allocate memory for %s component",
                    component_name);
            return SSH_ERROR;
        }
        memcpy(*int_ptr, data_ptr, length);
    }

    *int_len = length;
    *p = data_ptr + length;

    return SSH_OK;
}

/**
 * Parse DER-encoded ECDSA signature and extract r and s components.
 *
 * @param der_sig DER-encoded signature data
 * @param der_len Length of DER data
 * @param r_ptr Pointer to store r component
 * @param r_len Pointer to store r length
 * @param s_ptr Pointer to store s component
 * @param s_len Pointer to store s length
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int parse_ecdsa_der_signature(const uint8_t *der_sig,
                                     size_t der_len,
                                     uint8_t **r_ptr,
                                     size_t *r_len,
                                     uint8_t **s_ptr,
                                     size_t *s_len)
{
    const uint8_t *p = der_sig;
    const uint8_t *end = der_sig + der_len;
    size_t seq_len = 0;
    int rc;

    if (r_ptr == NULL || r_len == NULL || s_ptr == NULL || s_len == NULL ||
        der_sig == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Invalid arguments provided");
        return SSH_ERROR;
    }

    *r_ptr = NULL;
    *r_len = 0;
    *s_ptr = NULL;
    *s_len = 0;

    /* Parse SEQUENCE tag */
    if (p >= end || *(p++) != DER_SEQUENCE_TAG) {
        SSH_LOG(SSH_LOG_WARN, "Expected SEQUENCE tag in DER signature");
        return SSH_ERROR;
    }

    /* Parse sequence length */
    rc = parse_der_length(&p, end, &seq_len);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Invalid DER sequence length");
        return SSH_ERROR;
    }

    /* Verify sequence length matches remaining data */
    if (p + seq_len != end) {
        SSH_LOG(SSH_LOG_WARN, "DER sequence length mismatch");
        return SSH_ERROR;
    }

    /* Parse first INTEGER (r component) */
    rc = parse_der_integer(&p, end, "r", r_ptr, r_len);
    if (rc != SSH_OK) {
        goto error;
    }

    /* Parse second INTEGER (s component) */
    rc = parse_der_integer(&p, end, "s", s_ptr, s_len);
    if (rc != SSH_OK) {
        goto error;
    }

    /* Verify we consumed all data */
    if (p != end) {
        SSH_LOG(SSH_LOG_WARN, "Unexpected data after s component");
        goto error;
    }

    return SSH_OK;

error:
    SAFE_FREE(*r_ptr);
    *r_len = 0;
    SAFE_FREE(*s_ptr);
    *s_len = 0;
    return SSH_ERROR;
}

/**
 * Export an ECDSA signature from a FIDO2/U2F assertion.
 *
 * @param assert The FIDO2/U2F assertion containing the signature
 * @param response The sign response structure to fill with the signature
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int export_signature_ecdsa(fido_assert_t *assert,
                                  struct sk_sign_response *response)
{
    size_t len = 0;
    const uint8_t *ptr = NULL;
    int rc;

    len = fido_assert_sig_len(assert, 0);
    ptr = fido_assert_sig_ptr(assert, 0);

    if (ptr == NULL || len == 0) {
        SSH_LOG(SSH_LOG_WARN,
                "Invalid signature data from FIDO2/U2F assertion");
        return SSH_ERROR;
    }

    /* This will allocate and populate response->sig_r/_s (+ lengths) */
    rc = parse_ecdsa_der_signature(ptr,
                                   len,
                                   &response->sig_r,
                                   &response->sig_r_len,
                                   &response->sig_s,
                                   &response->sig_s_len);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to parse DER ECDSA signature");
        return SSH_ERROR;
    }

    return SSH_OK;
}

/**
 * Export an Ed25519 signature from a FIDO2 assertion.
 *
 * @param assert The FIDO2 assertion containing the signature
 * @param response The sign response structure to fill with the signature
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int export_signature_ed25519(fido_assert_t *assert,
                                    struct sk_sign_response *response)
{
    const uint8_t *ptr = NULL;
    size_t len;

    ptr = fido_assert_sig_ptr(assert, 0);
    len = fido_assert_sig_len(assert, 0);
    if (len != ED25519_SIG_LEN) {
        SSH_LOG(SSH_LOG_WARN, "Bad ED25519 signature length %zu", len);
        return SSH_ERROR;
    }

    response->sig_r_len = len;
    response->sig_r = calloc(response->sig_r_len, sizeof(uint8_t));
    if (response->sig_r == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for signature");
        return SSH_ERROR;
    }

    memcpy(response->sig_r, ptr, len);
    response->sig_s = NULL;
    response->sig_s_len = 0;
    return SSH_OK;
}

/**
 * Export a signature from a FIDO2/U2F assertion based on the specified
 * algorithm.
 *
 * @param algorithm The signature algorithm (SSH_SK_ECDSA or SSH_SK_ED25519)
 * @param assert The FIDO2/U2F assertion containing the signature
 * @param response The sign response structure to fill with the signature
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int export_signature(int algorithm,
                            fido_assert_t *assert,
                            struct sk_sign_response *response)
{
    int ret;

    switch (algorithm) {
    case SSH_SK_ECDSA:
        ret = export_signature_ecdsa(assert, response);
        break;
    case SSH_SK_ED25519:
        ret = export_signature_ed25519(assert, response);
        break;
    default:
        SSH_LOG(SSH_LOG_WARN, "Unsupported algorithm: %d", algorithm);
        ret = SSH_ERROR;
    }

    return ret;
}

static uint32_t ssh_sk_usbhid_api_version(void)
{
    return SK_USBHID_API_VERSION;
}

/**
 * Create and configure a new FIDO2/U2F credential for enrollment.
 *
 * @param device The FIDO2/U2F device to use
 * @param alg The algorithm to use (SSH_SK_ECDSA or SSH_SK_ED25519)
 * @param challenge The challenge data
 * @param challenge_len The length of the challenge data
 * @param application The application identifier (relying party ID)
 * @param flags The enrollment flags
 * @param pin The PIN for the device (can be NULL)
 * @param user_id The binary user ID buffer (can be NULL)
 * @param user_id_len Length of user_id buffer in bytes (0 if none)
 * @param credential_ptr Pointer to store the created credential
 *
 * @return FIDO_OK on success, FIDO_ERR_* codes on failure
 */
static int create_new_fido_credential(struct sk_device *device,
                                      uint32_t alg,
                                      const uint8_t *challenge,
                                      size_t challenge_len,
                                      const char *application,
                                      uint8_t flags,
                                      const char *pin,
                                      const uint8_t *user_id,
                                      size_t user_id_len,
                                      fido_cred_t **credential_ptr)
{
    int ret = FIDO_ERR_INTERNAL;
    int cose_algorithm, cred_protection;
    bool cred_prot_support = false;
    fido_opt_t set_resident_key = FIDO_OPT_OMIT;
    fido_cred_t *credential = NULL;

    /* Set the COSE algorithm based on the requested algorithm */
    switch (alg) {
    case SSH_SK_ECDSA:
        cose_algorithm = COSE_ES256;
        break;
    case SSH_SK_ED25519:
        cose_algorithm = COSE_EDDSA;
        break;
    default:
        SSH_LOG(SSH_LOG_WARN, "Unsupported algorithm: %u", alg);
        return FIDO_ERR_UNSUPPORTED_ALGORITHM;
    }

    credential = fido_cred_new();
    if (credential == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create new FIDO2/U2F credential");
        goto error;
    }

    ret = fido_cred_set_type(credential, cose_algorithm);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set credential type: %s",
                fido_strerr(ret));
        goto error;
    }

    ret = fido_cred_set_clientdata(credential, challenge, challenge_len);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set client data: %s",
                fido_strerr(ret));
        goto error;
    }

    if (flags & SSH_SK_RESIDENT_KEY) {
        set_resident_key = FIDO_OPT_TRUE;
    }
    ret = fido_cred_set_rk(credential, set_resident_key);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set resident key option: %s",
                fido_strerr(ret));
        goto error;
    }

    /* TODO: Add an additional option to set display_name, icon ..etc */
    ret =
        fido_cred_set_user(credential, user_id, user_id_len, NULL, NULL, NULL);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set user information: %s",
                fido_strerr(ret));
        goto error;
    }

    ret = fido_cred_set_rp(credential, application, NULL);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set Relying Party: %s",
                fido_strerr(ret));
        goto error;
    }

    if (flags & (SSH_SK_USER_VERIFICATION_REQD | SSH_SK_RESIDENT_KEY)) {
        cred_prot_support = fido_dev_supports_cred_prot(device->fido_device);
        if (!cred_prot_support) {
            SSH_LOG(SSH_LOG_WARN,
                    "Device does not support credential protection");
            ret = FIDO_ERR_UNSUPPORTED_EXTENSION;
            goto error;
        }

        if (flags & SSH_SK_USER_VERIFICATION_REQD) {
            cred_protection = FIDO_CRED_PROT_UV_REQUIRED;
        } else {
            cred_protection = FIDO_CRED_PROT_UV_OPTIONAL_WITH_ID;
        }

        ret = fido_cred_set_prot(credential, cred_protection);
        if (ret != FIDO_OK) {
            SSH_LOG(SSH_LOG_WARN,
                    "Failed to set credential protection: %s",
                    fido_strerr(ret));
            goto error;
        }
    }

    ret = fido_dev_make_cred(device->fido_device, credential, pin);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to make credential: %s",
                fido_strerr(ret));
        goto error;
    }

    *credential_ptr = credential;
    return FIDO_OK;

error:
    fido_cred_free(&credential);
    return ret;
}

/**
 * Construct an enrollment response from a FIDO2/U2F credential.
 * This function extracts and copies all necessary data from the fido_cred_t
 * into the response structure.
 *
 * @param alg The algorithm used (SSH_SK_ECDSA or SSH_SK_ED25519)
 * @param credential The FIDO2/U2F credential containing the enrollment data
 * @param flags The enrollment flags
 * @param response_ptr Pointer to store the constructed enrollment response
 *
 * @return SSH_OK on success, SSH_ERROR on failure
 */
static int
fido_cred_export_sk_enroll_response(uint32_t alg,
                                    const fido_cred_t *credential,
                                    uint8_t flags,
                                    struct sk_enroll_response **response_ptr)
{
    const uint8_t *ptr = NULL;
    const char *fmt = NULL;
    struct sk_enroll_response *response = NULL;
    int rc;

    response = calloc(1, sizeof(struct sk_enroll_response));
    if (response == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for enroll response");
        return SSH_ERROR;
    }

    response->flags = flags;

    /* Export public key */
    rc = export_public_key(alg, credential, response);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to export public key from credential");
        goto error;
    }

    /* Export the key handle */
    ptr = fido_cred_id_ptr(credential);
    if (ptr == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to get key handle");
        goto error;
    }

    response->key_handle_len = fido_cred_id_len(credential);
    response->key_handle = calloc(response->key_handle_len, sizeof(uint8_t));
    if (response->key_handle == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for key handle");
        goto error;
    }
    memcpy(response->key_handle, ptr, response->key_handle_len);

    /* Export challenge signature */
    fmt = fido_cred_fmt(credential);
    if (fmt == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to get attestation format");
        goto error;
    }

    ptr = fido_cred_sig_ptr(credential);
    if (ptr != NULL) {
        response->signature_len = fido_cred_sig_len(credential);
        response->signature = calloc(response->signature_len, sizeof(uint8_t));
        if (response->signature == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for signature");
            goto error;
        }
        memcpy(response->signature, ptr, response->signature_len);
    } else if (strcmp(fmt, "none") == 0) {
        /* No signature for "none" attestation format */
        response->signature = NULL;
        response->signature_len = 0;
    } else {
        SSH_LOG(SSH_LOG_WARN, "Failed to get signature");
        goto error;
    }

    /* Export attestation information if available */
    ptr = fido_cred_x5c_ptr(credential);
    if (ptr != NULL) {
        response->attestation_cert_len = fido_cred_x5c_len(credential);
        response->attestation_cert =
            calloc(response->attestation_cert_len, sizeof(uint8_t));
        if (response->attestation_cert == NULL) {
            SSH_LOG(SSH_LOG_WARN,
                    "Failed to allocate memory for attestation cert");
            goto error;
        }
        memcpy(response->attestation_cert, ptr, response->attestation_cert_len);
    }

    /* Export authdata */
    ptr = fido_cred_authdata_ptr(credential);
    if (ptr == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to get authdata");
        goto error;
    }

    response->authdata_len = fido_cred_authdata_len(credential);
    response->authdata = calloc(response->authdata_len, sizeof(uint8_t));
    if (response->authdata == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for authdata");
        goto error;
    }
    memcpy(response->authdata, ptr, response->authdata_len);

    *response_ptr = response;
    response = NULL;
    return SSH_OK;

error:
    sk_enroll_response_free(response);
    return SSH_ERROR;
}

static int ssh_sk_usbhid_enroll(uint32_t alg,
                                const uint8_t *challenge,
                                size_t challenge_len,
                                const char *application,
                                uint8_t flags,
                                const char *pin,
                                struct sk_option **options,
                                struct sk_enroll_response **enroll_response)
{
    int rc, ret = SSH_SK_ERR_GENERAL;
    const uint8_t *ptr = NULL;
    const char *attestation_format = NULL;
    const char *supported_options[] = {SSH_SK_OPTION_NAME_DEVICE_PATH,
                                       SSH_SK_OPTION_NAME_USER_ID,
                                       NULL};
    char **option_values = NULL;
    const char *device_path = NULL;
    uint8_t user_id[SK_MAX_USER_ID_LEN] = {0};
    size_t user_id_len = 0;
    size_t j;

    struct sk_device *device = NULL;
    struct sk_enroll_response *response = NULL;

    fido_cred_t *credential = NULL;

    if (enroll_response == NULL) {
        SSH_LOG(SSH_LOG_WARN, "enroll_response cannot be NULL");
        goto out;
    }
    *enroll_response = NULL;

    switch (alg) {
    case SSH_SK_ECDSA:
    case SSH_SK_ED25519:
        break;
    default:
        SSH_LOG(SSH_LOG_WARN, "Unsupported algorithm: %u", alg);
        ret = SSH_SK_ERR_UNSUPPORTED;
        goto out;
    }

    if (challenge == NULL || challenge_len == 0) {
        SSH_LOG(SSH_LOG_WARN, "challenge cannot be NULL or empty");
        goto out;
    }

    if (application == NULL || application[0] == '\0') {
        SSH_LOG(SSH_LOG_WARN, "application cannot be NULL or empty");
        goto out;
    }

    /* Extract device path from options if provided */
    rc = sk_options_validate_get((const struct sk_option **)options,
                                 supported_options,
                                 &option_values);
    if (rc == SSH_OK && option_values != NULL) {
        device_path = option_values[0]; /* device path is first in the array */

        /*
         * The user id is actually binary data according to the FIDO2
         * specification, but since we want to remain compatible with OpenSSH
         * sk-api, so we are restricted to only obtain the user_id as a char *
         * from the sk_option struct.
         */
        if (option_values[1] != NULL) {
            user_id_len = strlen(option_values[1]);

            if (user_id_len > SK_MAX_USER_ID_LEN) {
                SSH_LOG(SSH_LOG_WARN,
                        "user_id length exceeds maximum of %d characters",
                        SK_MAX_USER_ID_LEN);
                goto out;
            }

            memcpy((char *)user_id, option_values[1], user_id_len);
        }
    }

    sk_fido_init();

    if (device_path != NULL) {
        device = sk_device_open(device_path);
    } else {
        device = sk_device_probe(NULL, NULL, 0, false);
    }

    if (device == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to open FIDO2/U2F device");
        ret = SSH_SK_ERR_DEVICE_NOT_FOUND;
        goto out;
    }

    SSH_LOG(SSH_LOG_DEBUG, "Using FIDO2/U2F device: %s", device->path);

    /*
     * Check whether a resident key with same user_id exists to avoid
     * overwriting, unless operation is marked as forceful.
     */
    if ((flags & SSH_SK_RESIDENT_KEY) != 0 &&
        (flags & SSH_SK_FORCE_OPERATION) == 0) {

        rc = sk_device_check_resident_key(device,
                                          application,
                                          (uint8_t *)user_id,
                                          SK_MAX_USER_ID_LEN,
                                          pin);
        if (rc == FIDO_OK) {
            SSH_LOG(SSH_LOG_INFO, "Resident key already exists");
            ret = SSH_SK_ERR_CREDENTIAL_EXISTS;
            goto out;
        } else if (rc != FIDO_ERR_NO_CREDENTIALS) {
            SSH_LOG(SSH_LOG_WARN,
                    "Failed to check for resident key: %s",
                    fido_strerr(rc));
            ret = fido_err_to_ssh_sk_err(rc);
            goto out;
        }
    }

    /* Create and configure the FIDO2/U2F credential */
    ret = create_new_fido_credential(device,
                                     alg,
                                     challenge,
                                     challenge_len,
                                     application,
                                     flags,
                                     pin,
                                     (uint8_t *)user_id,
                                     SK_MAX_USER_ID_LEN,
                                     &credential);
    if (ret != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create new FIDO2/U2F credential");
        ret = fido_err_to_ssh_sk_err(ret);
        goto out;
    }

    ptr = fido_cred_x5c_ptr(credential);
    attestation_format = fido_cred_fmt(credential);

    if (attestation_format == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to get attestation format");
        goto out;
    }

    rc = strcmp(attestation_format, "none");

    /*
     * If the x509 certificate is available, we can assume attestation type to
     * be Basic Attestation and verify the attestation using the
     * fido_cred_verify function, which checks the attestation signature using
     * the attestation key mentioned in the x509 certificate.
     *
     * If the x509 certificate is not available, we check the attestation format
     * to see whether it's type is Self attestation or None. If it
     * is Self Attestation, we use fido_cred_verify_self to verify the
     * credential, which checks the attestation signature against the public key
     * of the credential itself.
     *
     * For more details, refer:
     * https://developers.yubico.com/libfido2/Manuals/fido_cred_verify.html
     * https://www.w3.org/TR/webauthn-2/#sctn-attestation
     */
    if (ptr != NULL) {
        SSH_LOG(SSH_LOG_DEBUG,
                "Verifying attestation (type: Basic Attestation)");
        rc = fido_cred_verify(credential);
    } else if (rc != 0) {
        SSH_LOG(SSH_LOG_DEBUG,
                "Verifying attestation (type: Self attestation)");
        rc = fido_cred_verify_self(credential);
    } else {
        SSH_LOG(SSH_LOG_DEBUG, "No attestation data available");
        rc = FIDO_OK;
    }
    if (rc != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to verify credential: %s",
                fido_strerr(rc));
        ret = fido_err_to_ssh_sk_err(rc);
        goto out;
    }

    /* Construct the enrollment response from the credential data */
    rc = fido_cred_export_sk_enroll_response(alg, credential, flags, &response);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to export public key from credential");
        goto out;
    }

    *enroll_response = response;
    response = NULL;
    ret = SSH_OK;

out:

    /* Clean up extracted values */
    if (option_values != NULL) {
        for (j = 0; supported_options[j] != NULL; j++) {
            SAFE_FREE(option_values[j]);
        }
        SAFE_FREE(option_values);
    }
    sk_enroll_response_free(response);
    sk_device_close(device);
    fido_cred_free(&credential);

    return ret;
}

static int ssh_sk_usbhid_sign(uint32_t alg,
                              const uint8_t *data,
                              size_t data_len,
                              const char *application,
                              const uint8_t *key_handle,
                              size_t key_handle_len,
                              uint8_t flags,
                              const char *pin,
                              struct sk_option **options,
                              struct sk_sign_response **sign_response)
{
    int rc, ret = SSH_SK_ERR_GENERAL;
    size_t i;

    const char *supported_options[] = {SSH_SK_OPTION_NAME_DEVICE_PATH, NULL};
    char **option_values = NULL;
    const char *device_path = NULL;

    struct sk_device *device = NULL;
    struct sk_sign_response *response = NULL;

    bool has_internal_uv = false, is_winhello = false;
    fido_opt_t user_presence = FIDO_OPT_FALSE;
    fido_assert_t *assert = NULL;

    if (sign_response == NULL) {
        SSH_LOG(SSH_LOG_WARN, "sign_response cannot be NULL");
        goto out;
    }
    *sign_response = NULL;

    switch (alg) {
    case SSH_SK_ECDSA:
    case SSH_SK_ED25519:
        break;
    default:
        SSH_LOG(SSH_LOG_WARN, "Unsupported algorithm: %u", alg);
        ret = SSH_SK_ERR_UNSUPPORTED;
        goto out;
    }

    if (data == NULL || data_len == 0) {
        SSH_LOG(SSH_LOG_WARN, "data to sign cannot be NULL or empty");
        goto out;
    }

    if (application == NULL || application[0] == '\0') {
        SSH_LOG(SSH_LOG_WARN, "application cannot be NULL or empty");
        goto out;
    }

    if (key_handle == NULL || key_handle_len == 0) {
        SSH_LOG(SSH_LOG_WARN, "key_handle cannot be NULL or empty");
        goto out;
    }

    /* Extract device path from options if provided */
    rc = sk_options_validate_get((const struct sk_option **)options,
                                 supported_options,
                                 &option_values);
    if (rc == SSH_OK && option_values != NULL) {
        device_path = option_values[0];
    }

    sk_fido_init();

    /*
     * We directly open the device if path is given.
     *
     * Otherwise, If PIN supplied or UV required, we avoid credential probing
     * across multiple devices (which could trigger multiple UV prompts).
     * Instead, we select by user touch first.
     *
     * For presence-only (UP) cases, credential-based probing is silent (see the
     * comment in the sk_device_check_key_handle function about pre-flight
     * checking), so we keep it to reduce touches.
     */
    if (device_path != NULL) {
        device = sk_device_open(device_path);
    } else if (pin != NULL || (flags & SSH_SK_USER_VERIFICATION_REQD)) {
        /* Touch based selection */
        device = sk_device_probe(NULL, NULL, 0, false);
    } else {
        /* Credential based selection */
        device =
            sk_device_probe(application, key_handle, key_handle_len, false);
    }

    if (device == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to open FIDO2/U2F device");
        ret = SSH_SK_ERR_DEVICE_NOT_FOUND;
        goto out;
    }

    assert = fido_assert_new();
    if (assert == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create new FIDO2/U2F assertion");
        goto out;
    }

    rc = fido_assert_set_clientdata(assert, data, data_len);
    if (rc != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to set client data: %s", fido_strerr(rc));
        ret = fido_err_to_ssh_sk_err(rc);
        goto out;
    }

    rc = fido_assert_set_rp(assert, application);
    if (rc != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set relying party: %s",
                fido_strerr(rc));
        ret = fido_err_to_ssh_sk_err(rc);
        goto out;
    }

    rc = fido_assert_allow_cred(assert, key_handle, key_handle_len);
    if (rc != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to allow credential: %s",
                fido_strerr(rc));
        ret = fido_err_to_ssh_sk_err(rc);
        goto out;
    }

    user_presence =
        (flags & SSH_SK_USER_PRESENCE_REQD) ? FIDO_OPT_TRUE : FIDO_OPT_FALSE;
    rc = fido_assert_set_up(assert, user_presence);
    if (rc != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to set user presence: %s",
                fido_strerr(rc));
        ret = fido_err_to_ssh_sk_err(rc);
        goto out;
    }

    /*
     * WinHello always requests the pin, unless we explicitly specify that we
     * don't expect user verification.
     */
    is_winhello = fido_dev_is_winhello(device->fido_device);
    if (pin == NULL && is_winhello) {
        rc = fido_assert_set_uv(assert, FIDO_OPT_FALSE);
        if (rc != FIDO_OK) {
            SSH_LOG(SSH_LOG_WARN,
                    "Failed to set user verification: %s",
                    fido_strerr(rc));
            ret = fido_err_to_ssh_sk_err(rc);
        }
    }

    /*
     * pin can be NULL if device internally has user verification capabilities
     * such as biometric.
     */
    if (pin == NULL && (flags & SSH_SK_USER_VERIFICATION_REQD)) {
        has_internal_uv = fido_dev_has_uv(device->fido_device);
        if (!has_internal_uv) {
            SSH_LOG(SSH_LOG_WARN,
                    "User Verification requirement cannot be satisfied as "
                    "device lacks internal user verification and PIN is also "
                    "not provided");
            ret = SSH_SK_ERR_PIN_REQUIRED;
            goto out;
        }

        rc = fido_assert_set_uv(assert, FIDO_OPT_TRUE);
        if (rc != FIDO_OK) {
            SSH_LOG(SSH_LOG_WARN,
                    "Failed to set user verification: %s",
                    fido_strerr(rc));
            ret = fido_err_to_ssh_sk_err(rc);
            goto out;
        }
    }

    rc = fido_dev_get_assert(device->fido_device, assert, pin);
    if (rc != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to get assertion: %s", fido_strerr(rc));
        ret = fido_err_to_ssh_sk_err(rc);
        goto out;
    }

    response = calloc(1, sizeof(struct sk_sign_response));
    if (response == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate sign response");
        goto out;
    }

    response->flags = fido_assert_flags(assert, 0);
    response->counter = fido_assert_sigcount(assert, 0);

    rc = export_signature(alg, assert, response);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to export signature");
        goto out;
    }

    *sign_response = response;
    response = NULL;
    ret = SSH_OK;

out:
    if (option_values != NULL) {
        for (i = 0; supported_options[i] != NULL; i++) {
            SAFE_FREE(option_values[i]);
        }
        SAFE_FREE(option_values);
    }

    fido_assert_free(&assert);
    sk_device_close(device);
    sk_sign_response_free(response);

    return ret;
}

/**
 * Export a single resident credential into an allocated sk_resident_key.
 */
static int fido_cred_export_sk_resident_key(const fido_cred_t *credential,
                                            const char *relying_party_id,
                                            bool has_internal_uv,
                                            struct sk_resident_key **out_key)
{
    struct sk_resident_key *resident_key = NULL;
    const uint8_t *ptr = NULL;
    size_t len;
    int algorithm;
    int rc;

    resident_key = calloc(1, sizeof(struct sk_resident_key));
    if (resident_key == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for resident key");
        goto error;
    }

    /* application */
    resident_key->application = strdup(relying_party_id);
    if (resident_key->application == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for application");
        goto error;
    }

    /* key handle */
    len = fido_cred_id_len(credential);
    ptr = fido_cred_id_ptr(credential);
    resident_key->key.key_handle_len = len;
    resident_key->key.key_handle = calloc(len, sizeof(uint8_t));
    if (resident_key->key.key_handle == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for key handle");
        goto error;
    }
    memcpy(resident_key->key.key_handle, ptr, len);

    /* user id */
    len = fido_cred_user_id_len(credential);
    ptr = fido_cred_user_id_ptr(credential);
    resident_key->user_id_len = len;
    resident_key->user_id = calloc(len, sizeof(uint8_t));
    if (resident_key->user_id == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to allocate memory for user ID");
        goto error;
    }
    memcpy(resident_key->user_id, ptr, len);

    /* algorithm */
    algorithm = fido_cred_type(credential);
    switch (algorithm) {
    case COSE_ES256:
        resident_key->alg = SSH_SK_ECDSA;
        break;
    case COSE_EDDSA:
        resident_key->alg = SSH_SK_ED25519;
        break;
    default:
        SSH_LOG(SSH_LOG_WARN, "Unsupported algorithm %d", algorithm);
        goto error;
    }

    rc = fido_cred_prot(credential);
    if (rc == FIDO_CRED_PROT_UV_REQUIRED && !has_internal_uv) {
        resident_key->flags |= SSH_SK_USER_VERIFICATION_REQD;
    }

    rc = export_public_key(resident_key->alg, credential, &resident_key->key);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to export public key for credential: %d",
                rc);
        goto error;
    }

    *out_key = resident_key;
    return SSH_OK;

error:
    SK_RESIDENT_KEY_FREE(resident_key);
    return SSH_ERROR;
}

/**
 * Load resident keys from a specific security key device.
 *
 * @param device The security key device to load keys from
 * @param pin The PIN for the device (required for loading resident keys)
 * @param resident_keys_ptr Pointer to store the array of loaded resident keys
 * @param num_keys_found_ptr Pointer to store the number of keys found
 *
 * @return SSH_SK_ERR_* error code (SSH_OK on success)
 *
 * @note This function only considers resident keys that belong to
 *       relying parties starting with "ssh:".
 */
static int
sk_device_load_resident_keys(struct sk_device *device,
                             const char *pin,
                             struct sk_resident_key ***resident_keys_ptr,
                             size_t *num_keys_found_ptr)
{
    int ret = SSH_SK_ERR_GENERAL, rc;
    bool has_internal_uv = false;
    size_t i, j, keys_count, num_relying_parties;
    const char *relying_party_id = NULL;

    struct sk_resident_key *cur_resident_key = NULL, **temp_ptr = NULL;
    fido_credman_metadata_t *metadata = NULL;
    fido_credman_rp_t *relying_parties = NULL;
    fido_credman_rk_t *resident_keys = NULL;
    const fido_cred_t *credential = NULL;

    has_internal_uv = fido_dev_has_uv(device->fido_device);

    metadata = fido_credman_metadata_new();
    if (metadata == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create FIDO2/U2F metadata");
        goto out;
    }

    rc = fido_credman_get_dev_metadata(device->fido_device, metadata, pin);
    if (rc != FIDO_OK) {
        if (rc == FIDO_ERR_INVALID_COMMAND) {
            SSH_LOG(SSH_LOG_WARN, "Device does not support resident keys");
            ret = SSH_SK_ERR_UNSUPPORTED;
        } else {
            SSH_LOG(SSH_LOG_WARN,
                    "Failed to get device metadata: %s for device at %s",
                    fido_strerr(rc),
                    device->path);
            ret = fido_err_to_ssh_sk_err(rc);
        }
        goto out;
    }

    relying_parties = fido_credman_rp_new();
    if (relying_parties == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to create relying parties list");
        goto out;
    }

    rc = fido_credman_get_dev_rp(device->fido_device, relying_parties, pin);
    if (rc != FIDO_OK) {
        SSH_LOG(SSH_LOG_WARN,
                "Failed to get relying party: %s",
                fido_strerr(rc));
        ret = fido_err_to_ssh_sk_err(rc);
        goto out;
    }

    num_relying_parties = fido_credman_rp_count(relying_parties);

    SSH_LOG(SSH_LOG_DEBUG,
            "Device %s has key(s) for %zu relying party(ies).",
            device->path,
            num_relying_parties);

    /*
     * Check all resident keys belonging to relying parties starting with "ssh:"
     */
    for (i = 0; i < num_relying_parties; i++) {
        relying_party_id = fido_credman_rp_id(relying_parties, i);
        if (relying_party_id != NULL) {
            rc = strncasecmp(relying_party_id, "ssh:", 4);
            if (rc != 0) {
                SSH_LOG(SSH_LOG_DEBUG,
                        "Skipping non-SSH relying party: %s",
                        relying_party_id);
                continue;
            }
        } else {
            SSH_LOG(SSH_LOG_DEBUG,
                    "Relying party ID is NULL, skipping RP %zu",
                    i);
            continue;
        }

        fido_credman_rk_free(&resident_keys);
        resident_keys = fido_credman_rk_new();
        if (resident_keys == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Failed to create FIDO2 resident key");
            goto out;
        }

        rc = fido_credman_get_dev_rk(device->fido_device,
                                     relying_party_id,
                                     resident_keys,
                                     pin);
        if (rc != FIDO_OK) {
            SSH_LOG(SSH_LOG_INFO,
                    "Failed to get resident key for RP %s: %s",
                    relying_party_id,
                    fido_strerr(rc));
            ret = fido_err_to_ssh_sk_err(rc);
            continue;
        }

        keys_count = fido_credman_rk_count(resident_keys);
        if (keys_count == 0) {
            SSH_LOG(SSH_LOG_INFO,
                    "No resident keys found for RP %s",
                    relying_party_id);
            continue;
        }

        SSH_LOG(SSH_LOG_DEBUG,
                "Found %zu resident key(s) for RP %s",
                keys_count,
                relying_party_id);

        for (j = 0; j < keys_count; j++) {
            credential = fido_credman_rk(resident_keys, j);
            if (credential == NULL) {
                SSH_LOG(SSH_LOG_INFO, "No resident key in slot %zu", j);
                continue;
            }

            rc = fido_cred_export_sk_resident_key(credential,
                                                  relying_party_id,
                                                  has_internal_uv,
                                                  &cur_resident_key);
            if (rc != SSH_OK) {
                goto out;
            }

            temp_ptr = realloc(*resident_keys_ptr,
                               sizeof(struct sk_resident_key *) *
                                   (*num_keys_found_ptr + 1));
            if (temp_ptr == NULL) {
                SSH_LOG(SSH_LOG_WARN,
                        "Failed to allocate memory for resident keys list");
                goto out;
            }

            *resident_keys_ptr = temp_ptr;
            (*resident_keys_ptr)[*num_keys_found_ptr] = cur_resident_key;
            (*num_keys_found_ptr)++;
            cur_resident_key = NULL;
        }
    }

    ret = SSH_OK;

out:
    SK_RESIDENT_KEY_FREE(cur_resident_key);
    fido_credman_rp_free(&relying_parties);
    fido_credman_rk_free(&resident_keys);
    fido_credman_metadata_free(&metadata);
    return ret;
}

static int
ssh_sk_usbhid_load_resident_keys(const char *pin,
                                 struct sk_option **options,
                                 struct sk_resident_key ***resident_keys_ptr,
                                 size_t *num_keys_found_ptr)
{
    int rc, ret = SSH_SK_ERR_GENERAL;
    size_t i, j, keys_count = 0;
    const char *supported_options[] = {SSH_SK_OPTION_NAME_DEVICE_PATH, NULL};
    char **option_values = NULL;
    const char *device_path = NULL;

    struct sk_resident_key **resident_keys = NULL;
    struct sk_device *device = NULL;

    if (resident_keys_ptr == NULL || num_keys_found_ptr == NULL) {
        SSH_LOG(SSH_LOG_WARN,
                "resident_keys_ptr and num_keys_found_ptr cannot be NULL");
        return SSH_SK_ERR_GENERAL;
    }

    /*
     * To load device metadata and resident keys, a valid pin must be provided
     * regardless of internal uv support.
     */
    if (pin == NULL) {
        SSH_LOG(SSH_LOG_WARN, "PIN cannot be NULL for loading resident keys");
        return SSH_SK_ERR_PIN_REQUIRED;
    }

    *resident_keys_ptr = NULL;
    *num_keys_found_ptr = 0;

    sk_fido_init();

    rc = sk_options_validate_get((const struct sk_option **)options,
                                 supported_options,
                                 &option_values);
    if (rc == SSH_OK && option_values != NULL) {
        device_path = option_values[0];
    }

    if (device_path != NULL) {
        device = sk_device_open(device_path);
    } else {
        device = sk_device_probe(NULL, NULL, 0, 1);
    }

    if (device == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to open FIDO2 device");
        ret = SSH_SK_ERR_DEVICE_NOT_FOUND;
        goto out;
    }

    rc = sk_device_load_resident_keys(device, pin, &resident_keys, &keys_count);
    if (rc != SSH_OK) {
        SSH_LOG(SSH_LOG_WARN, "Failed to load resident keys: %d", rc);
        ret = rc;
        goto out;
    }

    *resident_keys_ptr = resident_keys;
    *num_keys_found_ptr = keys_count;
    resident_keys = NULL;
    keys_count = 0;
    ret = SSH_OK;

out:
    if (option_values != NULL) {
        for (j = 0; supported_options[j] != NULL; j++) {
            SAFE_FREE(option_values[j]);
        }
        SAFE_FREE(option_values);
    }

    sk_device_close(device);

    for (i = 0; i < keys_count; i++) {
        SK_RESIDENT_KEY_FREE(resident_keys[i]);
    }
    SAFE_FREE(resident_keys);
    return ret;
}

static struct ssh_sk_callbacks_struct sk_usbhid_callbacks = {
    .api_version = ssh_sk_usbhid_api_version,
    .enroll = ssh_sk_usbhid_enroll,
    .sign = ssh_sk_usbhid_sign,
    .load_resident_keys = ssh_sk_usbhid_load_resident_keys,
};

const struct ssh_sk_callbacks_struct *ssh_sk_get_usbhid_callbacks(void)
{
    ssh_callbacks_init(&sk_usbhid_callbacks);
    return &sk_usbhid_callbacks;
}
