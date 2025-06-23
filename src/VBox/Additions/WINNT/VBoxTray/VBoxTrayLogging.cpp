/* $Id: VBoxTrayLogging.cpp 109932 2025-06-23 17:37:16Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxTrayLogging.cpp - Logging.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <package-generated.h>
#include "product-generated.h"

#include "VBoxTray.h"
#include "VBoxTrayInternal.h"

#include <iprt/buildconfig.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/system.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static PRTLOGGER      g_pLoggerRelease = NULL;           /**< This is actually the debug logger in DEBUG builds! */
/** Note: The following parameters are not yet modifiable via command line or some such, but keep them here for later. */
static uint32_t       g_cHistory = 10;                   /**< Enable log rotation, 10 files. */
static uint32_t       g_uHistoryFileTime = RT_SEC_1DAY;  /**< Max 1 day per file. */
static uint64_t       g_uHistoryFileSize = 100 * _1M;    /**< Max 100MB per file. */


/**
 * Header/footer callback for the release logger.
 *
 * @param   pLoggerRelease
 * @param   enmPhase
 * @param   pfnLog
 */
static DECLCALLBACK(void) vboxTrayLogHeaderFooter(PRTLOGGER pLoggerRelease, RTLOGPHASE enmPhase, PFNRTLOGPHASEMSG pfnLog)
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
                   "VBoxTray %s r%s %s (%s %s) release log\n"
                   "Log opened %s\n",
                   RTBldCfgVersion(), RTBldCfgRevisionStr(), VBOX_BUILD_TARGET,
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
            /* nothing */;
    }
}

RTDECL(void) RTLogWriteUser(const char* pachChars, size_t cbChars)
{
    if (pachChars)
        RTPrintf("%s: %.*s", VBOX_VBOXTRAY_TITLE, cbChars, pachChars);
}

/**
 * Creates the default release logger outputting to the specified file.
 *
 * @return  VBox status code.
 * @param   pszLogFile          Path to log file to use. Can be NULL if not needed.
 */
int VBoxTrayLogCreate(const char *pszLogFile)
{
    /* Create release (or debug) logger (stdout + file). */
    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
#ifdef DEBUG
    static const char s_szEnvVarPfx[] = "VBOXTRAY_LOG";
    static const char s_szGroupSettings[] = "all.e.l.f";
#else
    static const char s_szEnvVarPfx[] = "VBOXTRAY_RELEASE_LOG";
    static const char s_szGroupSettings[] = "all";
#endif
    RTERRINFOSTATIC ErrInfo;
    int rc = RTLogCreateEx(&g_pLoggerRelease, s_szEnvVarPfx,
                           RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG | RTLOGFLAGS_USECRLF,
                           s_szGroupSettings, RT_ELEMENTS(s_apszGroups), s_apszGroups, UINT32_MAX,
                           0 /*cBufDescs*/, NULL /*paBufDescs*/, RTLOGDEST_STDOUT | RTLOGDEST_USER,
                           vboxTrayLogHeaderFooter, g_cHistory, g_uHistoryFileSize, g_uHistoryFileTime,
                           NULL /*pOutputIf*/, NULL /*pvOutputIfUser*/,
                           RTErrInfoInitStatic(&ErrInfo), "%s", pszLogFile ? pszLogFile : "");
    if (RT_SUCCESS(rc))
    {
        char szGroupSettings[_1K];

#ifdef DEBUG
        /* Register this logger as the _debug_ logger. */
        RTLogSetDefaultInstance(g_pLoggerRelease);
#else
        /* Register this logger as the release logger. */
        RTLogRelSetDefaultInstance(g_pLoggerRelease);
#endif
        /* If verbosity is explicitly set, make sure to increase the logging levels for
         * the logging groups we offer functionality for in VBoxTray. */
        if (g_cVerbosity)
        {
            /* All groups we want to enable logging for VBoxTray. */
#ifdef DEBUG
            const char *apszGroups[] = { "guest_dnd", "shared_clipboard" };
#else /* For release builds we always want all groups being logged in verbose mode. Don't change this! */
            const char *apszGroups[] = { "all" };
#endif
            szGroupSettings[0] = '\0';

            for (size_t i = 0; i < RT_ELEMENTS(apszGroups); i++)
            {
                if (i > 0)
                    rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), "+");
                if (RT_SUCCESS(rc))
                    rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), apszGroups[i]);
                if (RT_FAILURE(rc))
                    break;

                switch (g_cVerbosity)
                {
                    case 1:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2");
                        break;

                    case 2:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2.l3");
                        break;

                    case 3:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2.l3.l4");
                        break;

                    case 4:
                        RT_FALL_THROUGH();
                    default:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2.l3.l4.f");
                        break;
                }

                if (RT_FAILURE(rc))
                    break;
            }

            if (RT_SUCCESS(rc))
            {
                rc = RTLogGroupSettings(g_pLoggerRelease, szGroupSettings);
                if (RT_FAILURE(rc))
                    VBoxTrayShowError("Setting log group settings failed, rc=%Rrc\n", rc);
            }
        }

        /* Explicitly flush the log in case of VBOXTRAY_RELEASE_LOG=buffered. */
        RTLogFlush(g_pLoggerRelease);

        VBoxTrayInfo("Verbosity level: %d\n", g_cVerbosity);

        int const rc2 = RTLogQueryGroupSettings(g_pLoggerRelease, szGroupSettings, sizeof(szGroupSettings));
        if (RT_SUCCESS(rc2))
            VBoxTrayInfo("Log group settings are: %s\n", szGroupSettings);
    }
    else
        VBoxTrayShowError(ErrInfo.szMsg);

    return rc;
}

/**
 * Destroys the logging.
 */
void VBoxTrayLogDestroy(void)
{
    /* Only want to destroy the release logger before calling exit(). The debug
       logger can be useful after that point... */
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
}

