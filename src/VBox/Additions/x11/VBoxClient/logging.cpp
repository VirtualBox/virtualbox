/* $Id: logging.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Guest Additions - X11 Client.
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


#include <sys/wait.h>
#include <stdlib.h>

#include <iprt/buildconfig.h>
#include <iprt/file.h>
#include <iprt/message.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/system.h>

#ifdef VBOX_WITH_DBUS
# include <VBox/dbus.h>
#endif
#include <VBox/VBoxGuestLib.h>

#include <package-generated.h>
#include "VBoxClient.h"

/** Logging parameters. */
/** @todo Make this configurable later. */
static PRTLOGGER     g_pLoggerRelease = NULL;
static uint32_t      g_cHistory = 10;                   /* Enable log rotation, 10 files. */
static uint32_t      g_uHistoryFileTime = RT_SEC_1DAY;  /* Max 1 day per file. */
static uint64_t      g_uHistoryFileSize = 100 * _1M;    /* Max 100MB per file. */

/** Custom log prefix (to be set externally). */
static char          *g_pszCustomLogPrefix;

extern unsigned      g_cRespawn;


/**
 * Fallback notification helper using 'notify-send'.
 *
 * @returns VBox status code.
 * @returns VERR_NOT_SUPPORTED if 'notify-send' is not available, or there was an error while running 'notify-send'.
 * @param   pszMessage          Message to notify desktop environment with.
 */
int vbclNotifyFallbackNotifySend(const char *pszMessage)
{
    AssertPtrReturn(pszMessage, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    if (g_cRespawn == 0)
    {
        char *pszCommand = RTStrAPrintf2("notify-send \"VBoxClient: %s\"", pszMessage);
        if (pszCommand)
        {
            int status = system(pszCommand);

            RTStrFree(pszCommand);

            if (WEXITSTATUS(status) != 0)  /* Utility or extension not available. */
            {
                pszCommand = RTStrAPrintf2("xmessage -buttons OK:0 -center \"VBoxClient: %s\"",
                                           pszMessage);
                if (pszCommand)
                {
                    status = system(pszCommand);
                    if (WEXITSTATUS(status) != 0)  /* Utility or extension not available. */
                        rc = VERR_NOT_SUPPORTED;

                    RTStrFree(pszCommand);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}

/**
 * Shows a notification on the desktop.
 *
 * @returns VBox status code.
 * @returns VERR_NOT_SUPPORTED if the current desktop environment is not supported.
 * @param   pszHeader           Header text to show.
 * @param   pszBody             Body text to show.
 *
 * @note    How this notification will look like depends on the actual desktop environment implementing
 *          the actual notification service. Currently only D-BUS-compatible environments are supported.
 *
 *          Most notification implementations have length limits on their header / body texts, so keep
 *          the text(s) short.
 */
int VBClShowNotify(const char *pszHeader, const char *pszBody)
{
    AssertPtrReturn(pszHeader, VERR_INVALID_POINTER);
    AssertPtrReturn(pszBody,   VERR_INVALID_POINTER);

    int rc;
# ifdef VBOX_WITH_DBUS
    rc = RTDBusLoadLib(); /** @todo Does this init / load the lib only once? Need to check this. */
    if (RT_FAILURE(rc))
    {
        VBClLogError("D-Bus seems not to be installed; no desktop notifications available\n");
        return rc;
    }

    DBusConnection *conn;
    DBusMessage* msg = NULL;
    conn = dbus_bus_get(DBUS_BUS_SESSION, NULL);
    if (conn == NULL)
    {
        VBClLogError("Could not retrieve D-BUS session bus\n");
        rc = VERR_INVALID_HANDLE;
    }
    else
    {
        msg = dbus_message_new_method_call("org.freedesktop.Notifications",
                                           "/org/freedesktop/Notifications",
                                           "org.freedesktop.Notifications",
                                           "Notify");
        if (msg == NULL)
        {
            VBClLogError("Could not create D-BUS message!\n");
            rc = VERR_INVALID_HANDLE;
        }
        else
            rc = VINF_SUCCESS;
    }
    if (RT_SUCCESS(rc))
    {
        uint32_t msg_replace_id = 0;
        const char *msg_app = "VBoxClient";
        const char *msg_icon = "";
        const char *msg_summary = pszHeader;
        const char *msg_body = pszBody;
        int32_t msg_timeout = -1;           /* Let the notification server decide */

        DBusMessageIter iter;
        DBusMessageIter array;
        /*DBusMessageIter dict; - unused */
        /*DBusMessageIter value; - unused */
        /*DBusMessageIter variant; - unused */
        /*DBusMessageIter data; - unused */

        /* Format: UINT32 org.freedesktop.Notifications.Notify
         *         (STRING app_name, UINT32 replaces_id, STRING app_icon, STRING summary, STRING body,
         *          ARRAY actions, DICT hints, INT32 expire_timeout)
         */
        dbus_message_iter_init_append(msg,&iter);
        dbus_message_iter_append_basic(&iter,DBUS_TYPE_STRING,&msg_app);
        dbus_message_iter_append_basic(&iter,DBUS_TYPE_UINT32,&msg_replace_id);
        dbus_message_iter_append_basic(&iter,DBUS_TYPE_STRING,&msg_icon);
        dbus_message_iter_append_basic(&iter,DBUS_TYPE_STRING,&msg_summary);
        dbus_message_iter_append_basic(&iter,DBUS_TYPE_STRING,&msg_body);
        dbus_message_iter_open_container(&iter,DBUS_TYPE_ARRAY,DBUS_TYPE_STRING_AS_STRING,&array);
        dbus_message_iter_close_container(&iter,&array);
        dbus_message_iter_open_container(&iter,DBUS_TYPE_ARRAY,"{sv}",&array);
        dbus_message_iter_close_container(&iter,&array);
        dbus_message_iter_append_basic(&iter,DBUS_TYPE_INT32,&msg_timeout);

        DBusError err;
        dbus_error_init(&err);

        DBusMessage *reply;
        reply = dbus_connection_send_with_reply_and_block(conn, msg, 30 * 1000 /* 30 seconds timeout */, &err);
        if (dbus_error_is_set(&err))
            VBClLogError("D-BUS returned an error while sending the notification: %s", err.message);
        else if (reply)
        {
            dbus_connection_flush(conn);
            dbus_message_unref(reply);
        }
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
    }
    if (msg != NULL)
        dbus_message_unref(msg);
# else
    /** @todo Implement me */
    RT_NOREF(pszHeader, pszBody);
    rc = VERR_NOT_SUPPORTED;
# endif /* VBOX_WITH_DBUS */

    /* Try to use a fallback if the stuff above fails or is not available. */
    if (RT_FAILURE(rc))
        rc = vbclNotifyFallbackNotifySend(pszBody);

    /* If everything fails, still print out our notification to stdout, in the hope
     * someone still gets aware of it. */
    if (RT_FAILURE(rc))
        VBClLogInfo("*** Notification: %s - %s ***\n", pszHeader, pszBody);

    return rc;
}

/**
 * Logs a message with a given prefix, format string and a va_list.
 *
 * @param   pszPrefix           Log prefix to use.
 * @param   pszFormat           Format string to use.
 * @param   va                  va_list to use.
 */
static void vbclLogV(const char *pszPrefix, const char *pszFormat, va_list va)
{
    char *psz = NULL;
    RTStrAPrintfV(&psz, pszFormat, va);
    AssertPtrReturnVoid(psz);
    LogRel(("%s%s", pszPrefix ? pszPrefix : "", psz));
    RTStrFree(psz);
}

/**
 * Logs a fatal error, notifies the desktop environment via a message and
 * exits the application immediately.
 *
 * @param   pszFormat           Format string to log.
 * @param   ...                 Variable arguments for format string. Optional.
 */
void VBClLogFatalError(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    vbclLogV("Fatal Error: ", pszFormat, va);
    va_end(va);
}

/**
 * Logs an error message to the (release) logging instance.
 *
 * @param   pszFormat               Format string to log.
 */
void VBClLogError(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    vbclLogV("Error: ", pszFormat, va);
    va_end(va);
}

/**
 * Logs an info message to the (release) logging instance.
 *
 * @param   pszFormat               Format string to log.
 */
void  VBClLogInfo(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    vbclLogV("", pszFormat, va);
    va_end(va);
}

/**
 * Displays a verbose message based on the currently
 * set global verbosity level.
 *
 * @param   iLevel      Minimum log level required to display this message.
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
void VBClLogVerbose(unsigned iLevel, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    if (iLevel <= g_cVerbosity)
        vbclLogV("", pszFormat, va);
    va_end(va);
}

/**
 * @callback_method_impl{FNRTLOGPHASE, Release logger callback}
 */
static DECLCALLBACK(void) vbClLogHeaderFooter(PRTLOGGER pLoggerRelease, RTLOGPHASE enmPhase, PFNRTLOGPHASEMSG pfnLog)
{
    /* Some introductory information. */
    static RTTIMESPEC s_TimeSpec;
    char szTmp[256];
    if (enmPhase == RTLOGPHASE_BEGIN)
        RTTimeNow(&s_TimeSpec);
    RTTimeSpecToString(&s_TimeSpec, szTmp, sizeof(szTmp));

    switch (enmPhase)
    {
        case RTLOGPHASE_BEGIN:
        {
            pfnLog(pLoggerRelease,
                   "VBoxClient %s r%s (verbosity: %u) %s (%s %s) release log\n"
                   "Log opened %s\n",
                   RTBldCfgVersion(), RTBldCfgRevisionStr(), g_cVerbosity, VBOX_BUILD_TARGET,
                   __DATE__, __TIME__, szTmp);

            int vrc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Product: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Release: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Version: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_SERVICE_PACK, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Service Pack: %s\n", szTmp);

            /* the package type is interesting for Linux distributions */
            char szExecName[RTPATH_MAX];
            char *pszExecName = RTProcGetExecutablePath(szExecName, sizeof(szExecName));
            pfnLog(pLoggerRelease,
                   "Executable: %s\n"
                   "Process ID: %u\n"
                   "Package type: %s"
#ifdef VBOX_OSE
                   " (OSE)"
#endif
                   "\n",
                   pszExecName ? pszExecName : "unknown",
                   RTProcSelf(),
                   VBOX_PACKAGE_STRING);
            break;
        }

        case RTLOGPHASE_PREROTATE:
            pfnLog(pLoggerRelease, "Log rotated - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_POSTROTATE:
            pfnLog(pLoggerRelease, "Log continuation - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_END:
            pfnLog(pLoggerRelease, "End of log file - Log started %s\n", szTmp);
            break;

        default:
            /* nothing */
            break;
    }
}

static DECLCALLBACK(size_t) vbClLogPrefixCb(PRTLOGGER pLogger, char *pchBuf, size_t cchBuf, void *pvUser)
{
    size_t cbPrefix = 0;

    RT_NOREF(pLogger);
    RT_NOREF(pvUser);

    if (g_pszCustomLogPrefix)
    {
        cbPrefix = RT_MIN(strlen(g_pszCustomLogPrefix), cchBuf);
        memcpy(pchBuf, g_pszCustomLogPrefix, cbPrefix);
    }

    return cbPrefix;
}

/**
 * Creates the default release logger outputting to the specified file, extended version.
 *
 * @return  IPRT status code.
 * @param   pszLogFile      Filename for log output. Empty filename allowed.
 * @param   fPrintHeader    Whether to print the VBoxClient logging header or not.
 */
int VBClLogCreateEx(const char *pszLogFile, bool fPrintHeader)
{
    if (!pszLogFile)
        return VINF_SUCCESS;

    /* Create release logger (stdout + file). */
    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
    RTUINT fFlags = RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME | RTLOGFLAGS_PREFIX_CUSTOM;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    fFlags |= RTLOGFLAGS_USECRLF;
#endif
    int rc = RTLogCreateEx(&g_pLoggerRelease, "VBOXCLIENT_RELEASE_LOG", fFlags, "all",
                           RT_ELEMENTS(s_apszGroups), s_apszGroups, UINT32_MAX /*cMaxEntriesPerGroup*/,
                           0 /*cBufDescs*/, NULL /*paBufDescs*/, RTLOGDEST_STDOUT | RTLOGDEST_USER,
                           fPrintHeader ? vbClLogHeaderFooter : NULL, g_cHistory, g_uHistoryFileSize, g_uHistoryFileTime,
                           NULL /*pOutputIf*/, NULL /*pvOutputIfUser*/,
                           NULL /*pErrInfo*/, "%s", pszLogFile ? pszLogFile : "");
    if (RT_SUCCESS(rc))
    {
        /* register this logger as the release logger */
        RTLogRelSetDefaultInstance(g_pLoggerRelease);

        rc = RTLogSetCustomPrefixCallback(g_pLoggerRelease, vbClLogPrefixCb, NULL);
        if (RT_FAILURE(rc))
            RTMsgError("unable to register custom log prefix callback\n");

        /* Explicitly flush the log in case of VBOXSERVICE_RELEASE_LOG=buffered. */
        RTLogFlush(g_pLoggerRelease);
    }
    else
        RTMsgError("Failed to create release log '%s', rc=%Rrc\n", pszLogFile, rc);

    return rc;
}

/**
 * Creates the default release logger outputting to the specified file.
 *
 * @return  IPRT status code.
 * @param   pszLogFile      Filename for log output. Empty filename allowed.
 */
int VBClLogCreate(const char *pszLogFile)
{
    return VBClLogCreateEx(pszLogFile, true /* fPrintHeader */);
}

/**
 * Destroys the currently active logging instance.
 */
void VBClLogDestroy(void)
{
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
}

/**
 * Modifies the (release) log settings.
 *
 * @returns VBox status code.
 * @param   pszDest             Log destination string to set.
 * @param   uVerbosity          Verbosity level to set.
 *
 * @note    Errors will be logged to stderr.
 */
int VBClLogModify(const char *pszDest, unsigned uVerbosity)
{
    AssertPtrReturn(pszDest, VERR_INVALID_POINTER);
    AssertPtrReturn(g_pLoggerRelease, VERR_INVALID_POINTER);

    int rc = RTLogDestinations(g_pLoggerRelease, pszDest);
    if (RT_SUCCESS(rc))
    {
#define LOG_GROUP_SET_BREAK(a_Val) \
        rc = RTLogGroupSettings(g_pLoggerRelease, "all.e" a_Val); break;

        switch (uVerbosity)
        {
            case 0:  LOG_GROUP_SET_BREAK("");
            case 1:  LOG_GROUP_SET_BREAK(".l");
            case 2:  LOG_GROUP_SET_BREAK(".l.l2");
            case 3:  LOG_GROUP_SET_BREAK(".l.l2.l3");
            default: LOG_GROUP_SET_BREAK(".l.l2.l3.l4");
        }
#undef LOG_GROUP_SET_BREAK
    }

    if (RT_FAILURE(rc)) /* Print to stderr in the hope that anyone can read this. */
        RTMsgError("Failed to set/modify log output, rc=%Rrc", rc);

    return rc;
}

/**
 * Set custom log prefix.
 *
 * @param pszPrefix     Custom log prefix string.
 */
void VBClLogSetLogPrefix(const char *pszPrefix)
{
    g_pszCustomLogPrefix = (char *)pszPrefix;
}

