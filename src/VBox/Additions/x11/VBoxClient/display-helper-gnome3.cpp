/* $Id: display-helper-gnome3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - Gnome3 Desktop Environment helper.
 *
 * A helper for X11/Wayland Client which performs Gnome Desktop
 * Environment specific actions.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * This helper implements communication protocol between gnome-settings-daemon
 * and itself using interface defined in (revision e88467f9):
 *
 * https://gitlab.gnome.org/GNOME/mutter/-/blob/main/src/org.gnome.Mutter.DisplayConfig.xml
 */

#include "VBoxClient.h"
#include "display-helper.h"

#include <stdio.h>
#include <stdlib.h>

#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>

#include <iprt/env.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>

/** Load libDbus symbols needed for us. */
#include <VBox/dbus.h>
/* Declarations of the functions that we need from libXrandr. */
#define VBOX_DBUS_GENERATE_BODY
#include <VBox/dbus-calls.h>

/** D-bus parameters for connecting to Gnome display service. */
#define VBOXCLIENT_HELPER_DBUS_DESTINATION      "org.gnome.Mutter.DisplayConfig"
#define VBOXCLIENT_HELPER_DBUS_PATH             "/org/gnome/Mutter/DisplayConfig"
#define VBOXCLIENT_HELPER_DBUS_IFACE            "org.gnome.Mutter.DisplayConfig"
#define VBOXCLIENT_HELPER_DBUS_GET_METHOD       "GetCurrentState"
#define VBOXCLIENT_HELPER_DBUS_APPLY_METHOD     "ApplyMonitorsConfig"

/** D-bus communication timeout value, milliseconds.*/
#define VBOXCLIENT_HELPER_DBUS_TIMEOUT_MS       (1 * 1000)

/** gnome-settings-daemon ApplyMonitorsConfig method:
 *  0: verify       - test if configuration can be applied and do not change anything,
 *  1: temporary    - apply configuration temporary, all will be reverted after re-login,
 *  2: persistent   - apply configuration permanently (asks for user confirmation).
 */
#define VBOXCLIENT_APPLY_DISPLAY_CONFIG_METHOD  (1)

/**
 * Helper macro which is used in order to simplify code when batch of
 * values needed to be parsed out of D-bus. Macro prevents execution
 * of the 'next' command if 'previous' one was failed (tracked via
 * local variable _ret). It is required that '_ret' should be initialized
 * to TRUE before batch started.
 *
 * @param _ret      Local variable which is used in order to track execution flow.
 * @param _call     A function (with full arguments) which returns 'dbus_bool_t'.
 */
#define VBCL_HLP_GNOME3_NEXT(_ret, _call) \
    { _ret &= _ret ? _call : _ret; if (!ret) VBClLogError(__FILE__ ":%d: check fail here!\n", __LINE__); }

/**
 * This structure describes sub-part of physical monitor state
 * required to compose a payload for calling ApplyMonitorsConfig method. */
struct vbcl_hlp_gnome3_physical_display_state
{
    /** Physical display connector name string. */
    char *connector;
    /** Current mode name string for physical display. */
    char *mode;
};

/**
 * Verify if data represented by D-bus message iteration corresponds to given data type.
 *
 * @return  True if D-bus message iteration corresponds to given data type, False otherwise.
 * @param   iter    D-bus message iteration.
 * @param   type    D-bus data type.
 */
static dbus_bool_t vbcl_hlp_gnome3_verify_data_type(DBusMessageIter *iter, int type)
{
    if (!iter)
        return false;

    if (dbus_message_iter_get_arg_type(iter) != type)
        return false;

    return true;
}

/**
 * Verifies D-bus iterator signature.
 *
 * @return  True if iterator signature matches to given one.
 * @param   iter        D-bus iterator to check.
 * @param   signature   Expected iterator signature.
 */
static dbus_bool_t vbcl_hlp_gnome3_check_iter_signature(DBusMessageIter *iter, const char *signature)
{
    char        *iter_signature;
    dbus_bool_t match;

    if (   !iter
        || !signature)
    {
        return false;
    }

    /* In case of dbus_message_iter_get_signature() returned memory should be freed by us. */
    iter_signature = dbus_message_iter_get_signature(iter);
    match = (strcmp(iter_signature, signature) == 0);

    if (!match)
        VBClLogError("iter signature mismatch: '%s' vs. '%s'\n", signature, iter_signature);

    if (iter_signature)
        dbus_free(iter_signature);

    return match;
}

/**
 * Verifies D-bus message signature.
 *
 * @return  True if message signature matches to given one.
 * @param   message     D-bus message to check.
 * @param   signature   Expected message signature.
 */
static dbus_bool_t vbcl_hlp_gnome3_check_message_signature(DBusMessage *message, const char *signature)
{
    char *message_signature;
    dbus_bool_t match;

    if (   !message
        || !signature)
    {
        return false;
    }

    /* In case of dbus_message_get_signature() returned memory need NOT be freed by us. */
    message_signature = dbus_message_get_signature(message);
    match = (strcmp(message_signature, signature) == 0);

    if (!match)
        VBClLogError("message signature mismatch: '%s' vs. '%s'\n", signature, message_signature);

    return match;
}

/**
 * Jump into DBUS_TYPE_ARRAY iter container and initialize sub-iterator
 * aimed to traverse container child nodes.
 *
 * @return  True if operation was successful, False otherwise.
 * @param   iter    D-bus iter of type DBUS_TYPE_ARRAY.
 * @param   array   Returned sub-iterator.
 */
static dbus_bool_t vbcl_hlp_gnome3_iter_get_array(DBusMessageIter *iter, DBusMessageIter *array)
{
    if (!iter || !array)
        return false;

    if (vbcl_hlp_gnome3_verify_data_type(iter, DBUS_TYPE_ARRAY))
    {
        dbus_message_iter_recurse(iter, array);
        /* Move to the next iter, returned value not important. */
        dbus_message_iter_next(iter);
        return true;
    }
    else
    {
        VBClLogError(
            "cannot get array: argument signature '%s' does not match to type of array\n",
            dbus_message_iter_get_signature(iter));
    }

    return false;
}

/**
 * Get value of D-bus iter of specified simple type (numerals, strings).
 *
 * @return  True if operation was successful, False otherwise.
 * @param   iter    D-bus iter of type simple type.
 * @param   type    D-bus data type.
 * @param   value   Returned value.
 */
static dbus_bool_t vbcl_hlp_gnome3_iter_get_basic(DBusMessageIter *iter, int type, void *value)
{
    if (!iter || !value)
        return false;

    if (vbcl_hlp_gnome3_verify_data_type(iter, type))
    {
        dbus_message_iter_get_basic(iter, value);
        /* Move to the next iter, returned value not important. */
        dbus_message_iter_next(iter);
        return true;
    }
    else
    {
        VBClLogError(
            "cannot get value: argument signature '%s' does not match to specified type\n",
            dbus_message_iter_get_signature(iter));
    }

    return false;
}

/**
 * Lookup simple value (numeral, string, bool etc) in D-bus dictionary
 * by given key and type.
 *
 * @return  True value is found, False otherwise.
 * @param   dict        D-bus iterator which represents dictionary.
 * @param   key_match   Dictionary key.
 * @param   type        Type of value.
 * @param   value       Returning value.
 */
static dbus_bool_t vbcl_hlp_gnome3_lookup_dict(DBusMessageIter *dict, const char *key_match, int type, void *value)
{
    dbus_bool_t found = false;

    if (!dict || !key_match)
        return false;

    if (!vbcl_hlp_gnome3_check_iter_signature(dict, "{sv}"))
        return false;

    do
    {
        dbus_bool_t ret = true;
        DBusMessageIter iter;
        char *key = NULL;

        /* Proceed to part a{ > sv < } of a{sv}. */
        dbus_message_iter_recurse(dict, &iter);

        /* Should be TRUE in order to satisfy VBCL_HLP_GNOME3_NEXT() requirements. */
        AssertReturn(ret, false);

        /* Proceed to part a{ > s < v} of a{sv}. */
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&iter, DBUS_TYPE_STRING, &key));

        /* Check if key matches. */
        if (strcmp(key_match, key) == 0)
        {
            DBusMessageIter value_iter;

            /* Proceed to part a{s > v < } of a{sv}. */
            dbus_message_iter_recurse(&iter, &value_iter);
            VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&value_iter, type, value));

            /* Make sure there are no more arguments. */
            VBCL_HLP_GNOME3_NEXT(ret, !dbus_message_iter_has_next(&value_iter));

            if (ret)
            {
                found = true;
                break;
            }
        }
    }
    while (dbus_message_iter_next(dict));

    return found;
}

/**
 * Go through available modes and pick up the one which has property 'is-current' set.
 * See GetCurrentState interface documentation for more details. Returned string memory
 * must be freed by calling function.
 *
 * @return  Mode name as a string if found, NULL otherwise.
 * @param   modes   List of monitor modes.
 */
static char *vbcl_hlp_gnome3_lookup_monitor_current_mode(DBusMessageIter *modes)
{
    char            *szCurrentMode = NULL;
    DBusMessageIter modes_iter;

    /* De-serialization parameters for 'modes': (siiddada{sv}). */
    char            *id = NULL;
    int32_t         width = 0;
    int32_t         height = 0;
    double          refresh_rate = 0;
    double          preferred_scale = 0;
    DBusMessageIter supported_scales;
    DBusMessageIter properties;

    if (!modes)
        return NULL;

    if(!vbcl_hlp_gnome3_check_iter_signature(modes, "(siiddada{sv})"))
        return NULL;

    do
    {
        static const char *key_match = "is-current";
        dbus_bool_t default_mode_found = false;
        dbus_bool_t ret = true;

        /* Should be TRUE in order to satisfy VBCL_HLP_GNOME3_NEXT() requirements. */
        AssertReturn(ret, NULL);

        /* Proceed to part a( > siiddada{sv} < ) of a(siiddada{sv}). */
        dbus_message_iter_recurse(modes, &modes_iter);
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&modes_iter, DBUS_TYPE_STRING, &id));
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&modes_iter, DBUS_TYPE_INT32, &width));
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&modes_iter, DBUS_TYPE_INT32, &height));
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&modes_iter, DBUS_TYPE_DOUBLE, &refresh_rate));
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&modes_iter, DBUS_TYPE_DOUBLE, &preferred_scale));
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_array(&modes_iter, &supported_scales));
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_array(&modes_iter, &properties));

        ret = vbcl_hlp_gnome3_lookup_dict(&properties, key_match, DBUS_TYPE_BOOLEAN, &default_mode_found);
        if (ret && default_mode_found)
        {
            szCurrentMode = strdup(id);
            break;
        }
    }
    while (dbus_message_iter_next(modes));

    return szCurrentMode;
}

/**
 * Parse physical monitors list entry. See GetCurrentState interface documentation for more details.
 *
 * @return  True if monitors list entry has been successfully parsed, False otherwise.
 * @param   physical_monitors_in            D-bus iterator representing list of physical monitors.
 * @param   connector                       Connector name (out).
 * @param   vendor                          Vendor name (out).
 * @param   product                         Product name (out).
 * @param   physical_monitor_serial         Serial number (out).
 * @param   modes                           List of monitor modes (out).
 * @param   physical_monitor_properties     A D-bus dictionary containing monitor properties (out).
 */
static dbus_bool_t vbcl_hlp_gnome3_parse_physical_monitor_record(
    DBusMessageIter *physical_monitors_in,
    char **connector,
    char **vendor,
    char **product,
    char **physical_monitor_serial,
    DBusMessageIter *modes,
    DBusMessageIter *physical_monitor_properties)
{
    dbus_bool_t ret = true;

    DBusMessageIter physical_monitors_in_iter;
    DBusMessageIter physical_monitors_in_description_iter;

    if (   !physical_monitors_in
        || !connector
        || !vendor
        || !product
        || !physical_monitor_serial
        || !modes
        || !physical_monitor_properties)
    {
        return false;
    }

    /* Validate signature. */
    if (!vbcl_hlp_gnome3_check_iter_signature(physical_monitors_in, "((ssss)a(siiddada{sv})a{sv})"))
        return false;

    /* Proceed to part ( > (ssss)a(siiddada{sv})a{sv} < ) of ((ssss)a(siiddada{sv})a{sv}). */
    dbus_message_iter_recurse(physical_monitors_in, &physical_monitors_in_iter);

    /* Should be TRUE in order to satisfy VBCL_HLP_GNOME3_NEXT() requirements. */
    AssertReturn(ret, false);

    /* Proceed to part ( > (ssss) < a(siiddada{sv})a{sv}) of ((ssss)a(siiddada{sv})a{sv}). */
    dbus_message_iter_recurse(&physical_monitors_in_iter, &physical_monitors_in_description_iter);
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&physical_monitors_in_description_iter, DBUS_TYPE_STRING, connector));
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&physical_monitors_in_description_iter, DBUS_TYPE_STRING, vendor));
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&physical_monitors_in_description_iter, DBUS_TYPE_STRING, product));
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&physical_monitors_in_description_iter, DBUS_TYPE_STRING, physical_monitor_serial));

    /* Proceed to part ((ssss) > a(siiddada{sv}) < a{sv}) of ((ssss)a(siiddada{sv})a{sv}). */
    if (ret)
        dbus_message_iter_next(&physical_monitors_in_iter);

    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_array(&physical_monitors_in_iter, modes));

    /* Proceed to part ((ssss)a(siiddada{sv}) > a{sv} < ) of ((ssss)a(siiddada{sv})a{sv}). */
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_array(&physical_monitors_in_iter, physical_monitor_properties));

    /* Make sure there are no more arguments. */
    VBCL_HLP_GNOME3_NEXT(ret, !dbus_message_iter_has_next(&physical_monitors_in_iter));

    return ret;
}

/**
 * Parse logical monitors list entry. See GetCurrentState interface documentation for more details.
 *
 * @return  True if monitors list entry has been successfully parsed, False otherwise.
 * @param   logical_monitors_in     D-bus iterator representing list of logical monitors.
 * @param   x                       Monitor X position (out).
 * @param   y                       Monitor Y position (out).
 * @param   scale                   Monitor scale factor (out).
 * @param   transform               Current monitor transform (rotation) (out).
 * @param   primary                 A flag which indicates if monitor is set as primary (out).
 * @param   monitors                List of physical monitors which are displaying this logical monitor (out).
 * @param   properties              List of monitor properties (out).
 */
static dbus_bool_t vbcl_hlp_gnome3_parse_logical_monitor_record(
    DBusMessageIter *logical_monitors_in,
    int32_t *x,
    int32_t *y,
    double *scale,
    uint32_t *transform,
    dbus_bool_t *primary,
    DBusMessageIter *monitors,
    DBusMessageIter *properties)
{
    dbus_bool_t ret = true;

    /* Iter used to traverse logical monitor parameters: @a(iiduba(ssss)a{sv}). */
    DBusMessageIter logical_monitors_in_iter;

    if (   !logical_monitors_in
        || !x
        || !y
        || !scale
        || !transform
        || !primary
        || !monitors
        || !properties)

    /* Should be TRUE in order to satisfy VBCL_HLP_GNOME3_NEXT() requirements. */
    AssertReturn(ret, false);

    /* Proceed to part @a( > iiduba(ssss)a{sv} < ) of @a(iiduba(ssss)a{sv}). */
    dbus_message_iter_recurse(logical_monitors_in, &logical_monitors_in_iter);
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&logical_monitors_in_iter, DBUS_TYPE_INT32,   x));
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&logical_monitors_in_iter, DBUS_TYPE_INT32,   y));
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&logical_monitors_in_iter, DBUS_TYPE_DOUBLE,  scale));
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&logical_monitors_in_iter, DBUS_TYPE_UINT32,  transform));
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&logical_monitors_in_iter, DBUS_TYPE_BOOLEAN, primary));
    /* Proceed to part @a(iidub > a(ssss) < a{sv}) of @a(iiduba(ssss)a{sv}). */
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_array(&logical_monitors_in_iter, monitors));
    /* Proceed to part @a(iiduba(ssss) > a{sv} < ) of @a(iiduba(ssss)a{sv}). */
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_array(&logical_monitors_in_iter, properties));

    /* Make sure there are no more arguments. */
    VBCL_HLP_GNOME3_NEXT(ret, !dbus_message_iter_has_next(&logical_monitors_in_iter));

    return ret;
}

/**
 * Get list of physical monitors parameters from D-bus iterator.
 *
 * Once this function was traversed 'physical_monitors_in' iterator, we are in the
 * end of the list of physical monitors parameters. So, it is important to do it once.
 *
 * @return  True if monitors parameters were successfully discovered, False otherwise.
 * @param   physical_monitors_in    D-bus iterator representing list of physical monitors.
 * @param   state                   Storage to put monitors state to.
 * @param   state_records_max       Size of state storage.
 * @param   cPhysicalMonitors       Actual number of physical displays parsed.
 */
static dbus_bool_t vbcl_hlp_gnome3_get_physical_monitors_state(
    DBusMessageIter *physical_monitors_in,
    vbcl_hlp_gnome3_physical_display_state *state,
    uint32_t state_records_max,
    uint32_t *cPhysicalMonitors)
{
    dbus_bool_t ret = true;
    uint32_t    iMonitor = 0;

    if (   !physical_monitors_in
        || !state
        || !cPhysicalMonitors)
    {
        return false;
    }

    /* Validate signature. */
    if (!vbcl_hlp_gnome3_check_iter_signature(physical_monitors_in, "((ssss)a(siiddada{sv})a{sv})"))
        return false;

    /* Should be TRUE in order to satisfy VBCL_HLP_GNOME3_NEXT() requirements. */
    AssertReturn(ret, false);

    do
    {
        char            *connector                 = NULL;
        char            *vendor                    = NULL;
        char            *product                   = NULL;
        char            *physical_monitor_serial   = NULL;
        DBusMessageIter modes;
        DBusMessageIter physical_monitor_properties;

        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_parse_physical_monitor_record(
            physical_monitors_in, &connector, &vendor, &product, &physical_monitor_serial,
            &modes, &physical_monitor_properties));

        if (iMonitor < state_records_max)
        {
            state[iMonitor].connector = connector;
            state[iMonitor].mode = vbcl_hlp_gnome3_lookup_monitor_current_mode(&modes);

            /* Check if both parameters were discovered successfully. */
            VBCL_HLP_GNOME3_NEXT(ret, state[iMonitor].connector && state[iMonitor].mode);
        }

        iMonitor++;

    }
    while (ret && dbus_message_iter_next(physical_monitors_in));

    if (iMonitor >= state_records_max)
    {
        VBClLogError("physical monitors list is too big (%u)\n", iMonitor);
        ret = false;
    }

    *cPhysicalMonitors = iMonitor;

    return ret;
}

/**
 * Release monitors state resources.
 *
 * @param   state               Array of monitor states.
 * @param   cPhysicalMonitors   Number of elements in array.
 */
static void vbcl_hlp_gnome3_free_physical_monitors_state(
    vbcl_hlp_gnome3_physical_display_state *state,
    uint32_t cPhysicalMonitors)
{
    if (!state || !cPhysicalMonitors)
        return;

    for (uint32_t i = 0; i < cPhysicalMonitors; i++)
    {
        /* Only free() what we allocated ourselves. */
        if (state[i].mode)
            free(state[i].mode);
    }
}

/**
 * Add dictionary element with boolean value into an array.
 *
 * @return  True on success, False otherwise.
 * @param   parent_iter         Array to add dictionary element into.
 * @param   key                 Dictionary key.
 * @param   value               Boolean value for given key.
 */
static dbus_bool_t vbcl_hlp_gnome3_add_dict_bool_entry(
    DBusMessageIter *parent_iter, const char *key, const dbus_bool_t value)
{
    dbus_bool_t ret = true;

    DBusMessageIter sub_iter_key;
    DBusMessageIter sub_iter_value;

    RT_ZERO(sub_iter_key);
    RT_ZERO(sub_iter_value);

    /* Should be TRUE in order to satisfy VBCL_HLP_GNOME3_NEXT() requirements. */
    AssertReturn(ret, false);

    VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_open_container(parent_iter, DBUS_TYPE_DICT_ENTRY, NULL, &sub_iter_key));
    {
        VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_append_basic(&sub_iter_key, DBUS_TYPE_STRING, &key));

        VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_open_container(&sub_iter_key, ((int) 'v'), "b", &sub_iter_value));
        {
            VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_append_basic(&sub_iter_value, DBUS_TYPE_BOOLEAN, &value));
        }

        VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_close_container(&sub_iter_key, &sub_iter_value));
    }
    VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_close_container(parent_iter, &sub_iter_key));

    return ret;
}

/**
 * This function is responsible for gathering current display
 * information (via its helper functions), compose a payload
 * for ApplyMonitorsConfig method and finally send configuration
 * change to gnome-settings-daemon over D-bus.
 *
 * @return  IPRT status code.
 * @param   connection              Handle to D-bus connection.
 * @param   serial                  Serial number obtained from GetCurrentState interface,
 *                                  needs to be passed to ApplyMonitorsConfig.
 * @param   physical_monitors_in    List of physical monitors (see GetCurrentState).
 * @param   logical_monitors_in     List of logical monitors (see GetCurrentState).
 * @param   idPrimaryDisplay        ID (number) of display which is requested to be set as primary.
 */
static int vbcl_hlp_gnome3_convert_and_apply_display_settings(
    DBusConnection *connection,
    uint32_t serial,
    DBusMessageIter *physical_monitors_in,
    DBusMessageIter *logical_monitors_in,
    uint32_t idPrimaryDisplay)
{
    int             rc = VERR_INVALID_PARAMETER;
    uint32_t        iLogicalMonitor = 0;
    uint32_t        cPhysicalMonitors = 0;
    int32_t         method = VBOXCLIENT_APPLY_DISPLAY_CONFIG_METHOD;

    dbus_bool_t     ret = true;
    DBusError       error;
    DBusMessage     *reply = NULL;;
    DBusMessage     *message = NULL;
    DBusMessageIter message_iter;
    DBusMessageIter logical_monitors_out_iter;
    DBusMessageIter properties_out_iter;

    struct vbcl_hlp_gnome3_physical_display_state
        physical_monitors_state[VBOX_DRMIPC_MONITORS_MAX];

    if (   !connection
        || !physical_monitors_in
        || !logical_monitors_in)
    {
        return VERR_INVALID_PARAMETER;
    }

    /* Important for error handling code path when dbus_message_iter_abandon_container_if_open() is in place. */
    RT_ZERO(message_iter);
    RT_ZERO(logical_monitors_out_iter);
    RT_ZERO(properties_out_iter);

    message = dbus_message_new_method_call(
        VBOXCLIENT_HELPER_DBUS_DESTINATION,
        VBOXCLIENT_HELPER_DBUS_PATH,
        VBOXCLIENT_HELPER_DBUS_IFACE,
        VBOXCLIENT_HELPER_DBUS_APPLY_METHOD);
    if (!message)
    {
        VBClLogError("unable to apply monitors config: no memory\n");
        return VERR_NO_MEMORY;
    }

    /* Start composing payload for ApplyMonitorsConfig method: (uu@a(iiduba(ssa{sv}))@a{sv}). */
    dbus_message_iter_init_append(message, &message_iter);

    /* Should be TRUE in order to satisfy VBCL_HLP_GNOME3_NEXT() requirements. */
    AssertReturn(ret, VERR_INVALID_PARAMETER);

    /* Get list of physical monitors parameters. */
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_get_physical_monitors_state(
        physical_monitors_in, physical_monitors_state, VBOX_DRMIPC_MONITORS_MAX, &cPhysicalMonitors));

    /* ( >u< u@a(iiduba(ssa{sv}))@a{sv}) of (uu@a(iiduba(ssa{sv}))@a{sv}). */
    VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_append_basic(&message_iter, DBUS_TYPE_UINT32, &serial));
    /* (u >u< @a(iiduba(ssa{sv}))@a{sv}) of (uu@a(iiduba(ssa{sv}))@a{sv}). */
    VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_append_basic(&message_iter, DBUS_TYPE_UINT32, &method));

    /* Parameter "monitors" of method ApplyMonitorsConfig.
     * Part (uu >@a(iiduba(ssa{sv}))< @a{sv}) of (uu@a(iiduba(ssa{sv}))@a{sv}). */
    VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_open_container(&message_iter, DBUS_TYPE_ARRAY, "(iiduba(ssa{sv}))", &logical_monitors_out_iter));

    /* Iterate over current configuration monitors (@logical_monitors
     * parameter of GetCurrentState interface) and compose the rest part of message. */
    do
    {
        /* De-serialization parameters for @logical_monitors data (see GetCurrentState interface documentation). */
        int32_t          x          = 0;
        int32_t          y          = 0;
        double           scale      = 0;
        uint32_t         transform  = 0;
        dbus_bool_t      primary    = false;
        dbus_bool_t      isPrimary  = false;
        DBusMessageIter  monitors;
        DBusMessageIter  properties;

        /* These iterators are used in order to compose sub-containers of the message. */
        DBusMessageIter  sub_iter0;
        DBusMessageIter  sub_iter1;
        DBusMessageIter  sub_iter2;
        DBusMessageIter  sub_iter3;
        /* Important for error handling code path when dbus_message_iter_abandon_container_if_open() is in place. */
        RT_ZERO(sub_iter0);
        RT_ZERO(sub_iter1);
        RT_ZERO(sub_iter2);
        RT_ZERO(sub_iter3);

        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_parse_logical_monitor_record(
            logical_monitors_in, &x, &y, &scale, &transform, &primary, &monitors, &properties));

        if (ret)
        {
            /* Whether current display supposed to be set as primary. */
            isPrimary = (iLogicalMonitor == idPrimaryDisplay);

            /* Compose part (uu@a( > iiduba(ssa{sv}) < )@a{sv}) of (uu@a(iiduba(ssa{sv}))@a{sv}). */
            VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_open_container(&logical_monitors_out_iter, DBUS_TYPE_STRUCT, NULL, &sub_iter0));
            {
                VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_append_basic(&sub_iter0, DBUS_TYPE_INT32,   &x));
                VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_append_basic(&sub_iter0, DBUS_TYPE_INT32,   &y));
                VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_append_basic(&sub_iter0, DBUS_TYPE_DOUBLE,  &scale));
                VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_append_basic(&sub_iter0, DBUS_TYPE_UINT32,  &transform));
                VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_append_basic(&sub_iter0, DBUS_TYPE_BOOLEAN, &isPrimary));

                /* Compose part (uu@a(iidub > a(ssa{sv}) < )@a{sv}) of (uu@a(iiduba(ssa{sv}))@a{sv}). */
                VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_open_container(&sub_iter0, DBUS_TYPE_ARRAY, "(ssa{sv})", &sub_iter1));
                {
                    /* Compose part (uu@a(iiduba > (ssa{sv}) < )@a{sv}) of (uu@a(iiduba(ssa{sv}))@a{sv}). */
                    VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_open_container(&sub_iter1, DBUS_TYPE_STRUCT, NULL, &sub_iter2));
                    {
                        VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_append_basic(&sub_iter2, DBUS_TYPE_STRING, &physical_monitors_state[iLogicalMonitor].connector));
                        VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_append_basic(&sub_iter2, DBUS_TYPE_STRING, &physical_monitors_state[iLogicalMonitor].mode));

                        /* Compose part (uu@a(iiduba(ss > a{sv} < ))@a{sv}) of (uu@a(iiduba(ssa{sv}))@a{sv}). */
                        VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_open_container(&sub_iter2, DBUS_TYPE_ARRAY, "{sv}", &sub_iter3));
                        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_add_dict_bool_entry(&sub_iter3, "is-current", true));
                        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_add_dict_bool_entry(&sub_iter3, "is-preferred", true));
                        VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_close_container(&sub_iter2, &sub_iter3));
                    }
                    VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_close_container(&sub_iter1, &sub_iter2));
                }
                VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_close_container(&sub_iter0, &sub_iter1));
            }
            VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_close_container(&logical_monitors_out_iter, &sub_iter0));

            iLogicalMonitor++;

            if (!ret)
            {
                dbus_message_iter_abandon_container_if_open(&sub_iter2, &sub_iter3);
                dbus_message_iter_abandon_container_if_open(&sub_iter1, &sub_iter2);
                dbus_message_iter_abandon_container_if_open(&sub_iter0, &sub_iter1);
                dbus_message_iter_abandon_container_if_open(&logical_monitors_out_iter, &sub_iter0);
            }
        }
        else
        {
            break;
        }

    }
    while (ret && dbus_message_iter_next(logical_monitors_in));

    /* Finish with parameter "monitors" of method ApplyMonitorsConfig. */
    VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_close_container(&message_iter, &logical_monitors_out_iter));

    /* Parameter "properties" of method ApplyMonitorsConfig (empty dict).
     * Part (uu@a(iiduba(ssa{sv})) >@a{sv}< ) of (uu@a(iiduba(ssa{sv}))@a{sv}).*/
    VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_open_container(&message_iter, DBUS_TYPE_ARRAY, "{sv}", &properties_out_iter));
    VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_close_container(&message_iter, &properties_out_iter));

    if (ret)
    {
        dbus_error_init(&error);

        reply = dbus_connection_send_with_reply_and_block(connection, message, VBOXCLIENT_HELPER_DBUS_TIMEOUT_MS, &error);
        if (reply)
        {
            VBClLogInfo("display %d has been set as primary\n", idPrimaryDisplay);
            dbus_message_unref(reply);
            rc = VINF_SUCCESS;
        }
        else
        {
            VBClLogError("unable to apply monitors config: %s\n",
                dbus_error_is_set(&error) ? error.message : "unknown error");
            dbus_error_free(&error);
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else
    {
        VBClLogError("unable to apply monitors config: cannot compose monitors config\n");

        dbus_message_iter_abandon_container_if_open(&message_iter, &logical_monitors_out_iter);
        dbus_message_iter_abandon_container_if_open(&message_iter, &properties_out_iter);

        rc = VERR_INVALID_PARAMETER;
    }

    /* Clean physical monitors state. */
    vbcl_hlp_gnome3_free_physical_monitors_state(physical_monitors_state, cPhysicalMonitors);

    dbus_message_unref(message);

    return rc;
}

/**
 * This function parses GetCurrentState interface call reply and passes it for further processing.
 *
 * @return  IPRT status code.
 * @param   connection          Handle to D-bus connection.
 * @param   idPrimaryDisplay    ID (number) of display which is requested to be set as primary.
 * @param   reply               Reply message of GetCurrentState call.
 */
static int vbcl_hlp_gnome3_process_current_display_layout(
    DBusConnection *connection, uint32_t idPrimaryDisplay, DBusMessage *reply)
{
    static const char *expected_signature = "ua((ssss)a(siiddada{sv})a{sv})a(iiduba(ssss)a{sv})a{sv}";

    dbus_bool_t     ret = true;
    DBusMessageIter iter;
    int             rc = VERR_GENERAL_FAILURE;

    uint32_t serial = 0;
    DBusMessageIter monitors;
    DBusMessageIter logical_monitors_in;
    DBusMessageIter properties;

    if (!reply)
    {
        return VERR_INVALID_PARAMETER;
    }

    /* Parse VBOXCLIENT_HELPER_DBUS_GET_METHOD reply payload:
     *
     * (u@a((ssss)a(siiddada{sv})a{sv})@a(iiduba(ssss)a{sv})@a{sv}).
     *
     * Method return the following arguments:  monitors, logical_monitors, properties.
     */

    /* Should be TRUE in order to satisfy VBCL_HLP_GNOME3_NEXT() requirements. */
    AssertReturn(ret, VERR_INVALID_PARAMETER);

    /* Important: in order to avoid libdbus asserts during parsing, its signature should be verified at first.  */
    VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_check_message_signature(reply, expected_signature));

    VBCL_HLP_GNOME3_NEXT(ret, dbus_message_iter_init(reply, &iter));
    if (ret)
    {
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_basic(&iter, DBUS_TYPE_UINT32, &serial));
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_array(&iter, &monitors));
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_array(&iter, &logical_monitors_in));
        VBCL_HLP_GNOME3_NEXT(ret, vbcl_hlp_gnome3_iter_get_array(&iter, &properties));

        /* Make sure there are no more arguments. */
        if (ret && !dbus_message_iter_has_next(&iter))
        {
            rc = vbcl_hlp_gnome3_convert_and_apply_display_settings(
                connection, serial, &monitors, &logical_monitors_in, idPrimaryDisplay);
        }
        else
        {
            VBClLogError("cannot fetch current displays configuration: incorrect number of arguments\n");
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else
    {
        VBClLogError("cannot fetch current displays configuration: no data\n");
        rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

/**
 * This function establishes D-bus connection, requests gnome-settings-daemon
 * to provide current display configuration via GetCurrentState interface call
 * and passes this information further to helper functions in order to set
 * requested display as primary.
 *
 * @return  IPRT status code.
 * @param   idPrimaryDisplay    A display ID which is requested to be set as primary.
 */
static DECLCALLBACK(int) vbcl_hlp_gnome3_set_primary_display(uint32_t idPrimaryDisplay)
{
    int rc = VERR_GENERAL_FAILURE;

    DBusConnection  *connection = NULL;
    DBusMessage     *message = NULL;
    DBusError       error;

    rc = RTDBusLoadLib();
    if (RT_FAILURE(rc))
    {
        VBClLogError("unable to load D-bus library\n");
        return VERR_SYMBOL_NOT_FOUND;
    }

    dbus_error_init(&error);
    connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (!dbus_error_is_set(&error))
    {
        message = dbus_message_new_method_call(
            VBOXCLIENT_HELPER_DBUS_DESTINATION,
            VBOXCLIENT_HELPER_DBUS_PATH,
            VBOXCLIENT_HELPER_DBUS_IFACE,
            VBOXCLIENT_HELPER_DBUS_GET_METHOD);

        if (message)
        {
            DBusMessage *reply;

            reply = dbus_connection_send_with_reply_and_block(connection, message, VBOXCLIENT_HELPER_DBUS_TIMEOUT_MS, &error);
            if (!dbus_error_is_set(&error))
            {
                rc = vbcl_hlp_gnome3_process_current_display_layout(connection, idPrimaryDisplay, reply);
                dbus_message_unref(reply);
            }
            else
            {
                VBClLogError("unable to get current display configuration: %s\n", error.message);
                dbus_error_free(&error);
                rc = VERR_INVALID_PARAMETER;
            }

            dbus_message_unref(message);
        }
        else
        {
            VBClLogError("unable to get current display configuration: no memory\n");
            rc = VERR_NO_MEMORY;
        }

        dbus_connection_flush(connection);
    }
    else
    {
        VBClLogError("unable to establish dbus connection: %s\n", error.message);
        dbus_error_free(&error);
        rc = VERR_INVALID_HANDLE;
    }

    return rc;
}

/**
 * @interface_method_impl{VBCLDISPLAYHELPER,pfnProbe}
 */
static DECLCALLBACK(int) vbcl_hlp_gnome3_probe(void)
{
    const char *pszCurrentDesktop = RTEnvGet(VBGH_ENV_XDG_CURRENT_DESKTOP);

    /* GNOME3 identifies itself by XDG_CURRENT_DESKTOP environment variable.
     * It can slightly vary for different distributions, but we assume that this
     * variable should at least contain sub-string 'GNOME' in its value. */
    if (pszCurrentDesktop && RTStrStr(pszCurrentDesktop, "GNOME"))
        return VINF_SUCCESS;

    return VERR_NOT_FOUND;
}

/**
 * @interface_method_impl{VBCLDISPLAYHELPER,pfnInit}
 */
static DECLCALLBACK(int) vbcl_hlp_gnome3_init(void)
{
    int rc;

    if (VBClGetDisplayServerType() == VBGHDISPLAYSERVERTYPE_X11)
    {
        rc = vbcl_hlp_generic_init();
        VBClLogInfo("attempt to start generic helper routines, rc=%Rrc\n", rc);
    }

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBCLDISPLAYHELPER,pfnTerm}
 */
static DECLCALLBACK(int) vbcl_hlp_gnome3_term(void)
{
    int rc;

    if (VBClGetDisplayServerType() == VBGHDISPLAYSERVERTYPE_X11)
    {
        rc = vbcl_hlp_generic_term();
        VBClLogInfo("attempt to stop generic helper routines, rc=%Rrc\n", rc);
    }

    return VINF_SUCCESS;
}

/* Helper callbacks. */
const VBCLDISPLAYHELPER g_DisplayHelperGnome3 =
{
    "GNOME3",                                               /* .pszName */
    vbcl_hlp_gnome3_probe,                                  /* .pfnProbe */
    vbcl_hlp_gnome3_init,                                   /* .pfnInit */
    vbcl_hlp_gnome3_term,                                   /* .pfnTerm */
    vbcl_hlp_gnome3_set_primary_display,                    /* .pfnSetPrimaryDisplay */
    vbcl_hlp_generic_subscribe_display_offset_changed,      /* .pfnSubscribeDisplayOffsetChangeNotification */
    vbcl_hlp_generic_unsubscribe_display_offset_changed,    /* .pfnUnsubscribeDisplayOffsetChangeNotification */
};
