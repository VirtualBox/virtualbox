/* $Id: VBoxLogRelCreate.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MS COM / XPCOM Abstraction Layer - VBoxLogRelCreate.
 */

/*
 * Copyright (C) 2005-2024 Oracle and/or its affiliates.
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
#include <VBox/com/utils.h>

#include <iprt/buildconfig.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/process.h>
#include <iprt/time.h>

#include <iprt/errcore.h>
#include <VBox/log.h>
#include <VBox/version.h>
#include "package-generated.h"



namespace com
{

static const char *g_pszLogEntity = NULL;

static DECLCALLBACK(void) vboxHeaderFooter(PRTLOGGER pReleaseLogger, RTLOGPHASE enmPhase, PFNRTLOGPHASEMSG pfnLog)
{
    /* some introductory information */
    static RTTIMESPEC s_TimeSpec;
    char szTmp[256];
    if (enmPhase == RTLOGPHASE_BEGIN)
        RTTimeNow(&s_TimeSpec);
    RTTimeSpecToString(&s_TimeSpec, szTmp, sizeof(szTmp));

    switch (enmPhase)
    {
        case RTLOGPHASE_BEGIN:
        {
            bool fOldBuffered = RTLogSetBuffering(pReleaseLogger, true /*fBuffered*/);
            pfnLog(pReleaseLogger,
                   "VirtualBox %s %s r%u %s (%s %s) release log\n"
#ifdef VBOX_BLEEDING_EDGE
                   "EXPERIMENTAL build " VBOX_BLEEDING_EDGE "\n"
#endif
                   "Log opened %s\n",
                   g_pszLogEntity, VBOX_VERSION_STRING, RTBldCfgRevision(),
                   RTBldCfgTargetDotArch(), __DATE__, __TIME__, szTmp);

            pfnLog(pReleaseLogger, "Build Type: %s\n", KBUILD_TYPE);
            int vrc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pReleaseLogger, "OS Product: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pReleaseLogger, "OS Release: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pReleaseLogger, "OS Version: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_SERVICE_PACK, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pReleaseLogger, "OS Service Pack: %s\n", szTmp);

            vrc = RTSystemQueryDmiString(RTSYSDMISTR_PRODUCT_NAME, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pReleaseLogger, "DMI Product Name: %s\n", szTmp);
            vrc = RTSystemQueryDmiString(RTSYSDMISTR_PRODUCT_VERSION, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pReleaseLogger, "DMI Product Version: %s\n", szTmp);

            RTSYSFWTYPE enmType;
            vrc = RTSystemQueryFirmwareType(&enmType);
            if (RT_SUCCESS(vrc))
            {
                pfnLog(pReleaseLogger, "Firmware type: %s\n", RTSystemFirmwareTypeName(enmType));
                if (enmType == RTSYSFWTYPE_UEFI)
                {
                     bool fValue;
                     vrc = RTSystemQueryFirmwareBoolean(RTSYSFWBOOL_SECURE_BOOT, &fValue);
                     if (RT_SUCCESS(vrc))
                         pfnLog(pReleaseLogger, "Secure Boot: %s\n", fValue ? "Enabled" : "Disabled");
                     else
                         pfnLog(pReleaseLogger, "Secure Boot: %Rrc\n", vrc);
                }
            }
            else
                pfnLog(pReleaseLogger, "Firmware type: failed - %Rrc\n", vrc);

            uint64_t cbHostRam = 0, cbHostRamAvail = 0;
            vrc = RTSystemQueryTotalRam(&cbHostRam);
            if (RT_SUCCESS(vrc))
                vrc = RTSystemQueryAvailableRam(&cbHostRamAvail);
            if (RT_SUCCESS(vrc))
            {
                pfnLog(pReleaseLogger, "Host RAM: %lluMB", cbHostRam / _1M);
                if (cbHostRam > _2G)
                    pfnLog(pReleaseLogger, " (%lld.%lldGB)",
                           cbHostRam / _1G, (cbHostRam % _1G) / (_1G / 10));
                pfnLog(pReleaseLogger, " total, %lluMB", cbHostRamAvail / _1M);
                if (cbHostRamAvail > _2G)
                    pfnLog(pReleaseLogger, " (%lld.%lldGB)",
                           cbHostRamAvail / _1G, (cbHostRamAvail % _1G) / (_1G / 10));
                pfnLog(pReleaseLogger, " available\n");
            }

            /* the package type is interesting for Linux distributions */
            char szExecName[RTPATH_MAX];
            char *pszExecName = RTProcGetExecutablePath(szExecName, sizeof(szExecName));
            pfnLog(pReleaseLogger,
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

#ifdef RT_OS_WINDOWS
            static struct
            {
                const char    *pszDesc;
                RTSYSNTFEATURE enmFeature;
            } s_aNtFeatures[] =
            {
                { "Core Isolation (Memory Integrity)", RTSYSNTFEATURE_CORE_ISOLATION_MEMORY_INTEGRITY }
            };
            pfnLog(pReleaseLogger, "Windows Features:\n");
            for (size_t i = 0; i < RT_ELEMENTS(s_aNtFeatures); i++)
            {
                pfnLog(pReleaseLogger, "  %s: ", s_aNtFeatures[i].pszDesc);
                bool fEnabled;
                vrc = RTSystemQueryNtFeatureEnabled(s_aNtFeatures[i].enmFeature, &fEnabled);
                if (RT_SUCCESS(vrc))
                    pfnLog(pReleaseLogger, "%s", fEnabled ? "ENABLED\n" : "DISABLED\n");
                else if (vrc == VERR_NOT_SUPPORTED)
                    pfnLog(pReleaseLogger, "Not supported\n");
                else
                    pfnLog(pReleaseLogger, "Failed to query (%Rrc)\n", vrc);
            }
#endif
            RTLogSetBuffering(pReleaseLogger, fOldBuffered);
            break;
        }

        case RTLOGPHASE_PREROTATE:
            pfnLog(pReleaseLogger, "Log rotated - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_POSTROTATE:
            pfnLog(pReleaseLogger, "Log continuation - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_END:
            pfnLog(pReleaseLogger, "End of log file - Log started %s\n", szTmp);
            break;

        default:
            /* nothing */;
    }
}

int VBoxLogRelCreate(const char *pcszEntity, const char *pcszLogFile,
                     uint32_t fFlags, const char *pcszGroupSettings,
                     const char *pcszEnvVarBase, uint32_t fDestFlags,
                     uint32_t cMaxEntriesPerGroup, uint32_t cHistory,
                     uint32_t uHistoryFileTime, uint64_t uHistoryFileSize,
                     PRTERRINFO pErrInfo)
{
    return VBoxLogRelCreateEx(pcszEntity, pcszLogFile,
                              fFlags, pcszGroupSettings,
                              pcszEnvVarBase, fDestFlags,
                              cMaxEntriesPerGroup, cHistory,
                              uHistoryFileTime, uHistoryFileSize,
                              NULL /*pOutputIf*/, NULL /*pvOutputIfUser*/,
                              pErrInfo);
}

int VBoxLogRelCreateEx(const char *pcszEntity, const char *pcszLogFile,
                       uint32_t fFlags, const char *pcszGroupSettings,
                       const char *pcszEnvVarBase, uint32_t fDestFlags,
                       uint32_t cMaxEntriesPerGroup, uint32_t cHistory,
                       uint32_t uHistoryFileTime, uint64_t uHistoryFileSize,
                       const void *pOutputIf, void *pvOutputIfUser,
                       PRTERRINFO pErrInfo)
{
    /* create release logger */
    PRTLOGGER pReleaseLogger;
    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    fFlags |= RTLOGFLAGS_USECRLF;
#endif
    g_pszLogEntity = pcszEntity;
    int vrc = RTLogCreateEx(&pReleaseLogger, pcszEnvVarBase, fFlags, pcszGroupSettings, RT_ELEMENTS(s_apszGroups), s_apszGroups,
                            cMaxEntriesPerGroup, 0 /*cBufDescs*/, NULL /*paBufDescs*/, fDestFlags,
                            vboxHeaderFooter, cHistory, uHistoryFileSize, uHistoryFileTime,
                            (PCRTLOGOUTPUTIF)pOutputIf, pvOutputIfUser,
                            pErrInfo, pcszLogFile ? "%s" : NULL, pcszLogFile);
    if (RT_SUCCESS(vrc))
    {
        /* explicitly flush the log, to have some info when buffering */
        RTLogFlush(pReleaseLogger);

        /* register this logger as the release logger */
        RTLogRelSetDefaultInstance(pReleaseLogger);
    }
    return vrc;
}

} /* namespace com */
