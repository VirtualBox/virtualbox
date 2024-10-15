/* $Id: UsbTestServicePlatform-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestServ - Remote USB test configuration and execution server, Platform
 *               specific helpers - Linux version.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/string.h>

#include <iprt/linux/sysfs.h>

#include "UsbTestServicePlatform.h"


/*********************************************************************************************************************************
*   Constants And Macros, Structures and Typedefs                                                                                *
*********************************************************************************************************************************/

/** Where the dummy_hcd.* and dummy_udc.* entries are stored. */
#define UTS_PLATFORM_LNX_DUMMY_HCD_PATH "/sys/devices/platform"

/**
 * A USB bus provided by the dummy HCD.
 */
typedef struct UTSPLATFORMLNXDUMMYHCDBUS
{
    /** The bus ID on the host the dummy HCD is serving. */
    uint32_t                  uBusId;
    /** Flag whether this is a super speed bus. */
    bool                      fSuperSpeed;
} UTSPLATFORMLNXDUMMYHCDBUS;
/** Pointer to a Dummy HCD bus. */
typedef UTSPLATFORMLNXDUMMYHCDBUS *PUTSPLATFORMLNXDUMMYHCDBUS;

/**
 * A dummy UDC descriptor.
 */
typedef struct UTSPLATFORMLNXDUMMYHCD
{
    /** Index of the dummy hcd entry. */
    uint32_t                   idxDummyHcd;
    /** Name for the dummy HCD. */
    const char                 *pszHcdName;
    /** Name for the accompanying dummy HCD. */
    const char                 *pszUdcName;
    /** Flag whether this HCD is free for use. */
    bool                       fAvailable;
    /** Flag whether this HCD contains a super speed capable bus. */
    bool                       fSuperSpeed;
    /** Number of busses this HCD instance serves. */
    unsigned                   cBusses;
    /** Bus structures the HCD serves.*/
    PUTSPLATFORMLNXDUMMYHCDBUS paBusses;
} UTSPLATFORMLNXDUMMYHCD;
/** Pointer to a dummy HCD entry. */
typedef UTSPLATFORMLNXDUMMYHCD *PUTSPLATFORMLNXDUMMYHCD;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/** Array of dummy HCD entries. */
static PUTSPLATFORMLNXDUMMYHCD g_paDummyHcd = NULL;
/** Number of Dummy hCD entries in the array. */
static unsigned                g_cDummyHcd = 0;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Queries the assigned busses for the given dummy HCD instance.
 *
 * @returns IPRT status code.
 * @param   pHcd              The dummy HCD bus instance.
 * @param   pszHcdName        The base HCD name.
 */
static int utsPlatformLnxDummyHcdQueryBusses(PUTSPLATFORMLNXDUMMYHCD pHcd, const char *pszHcdName)
{
    int rc = VINF_SUCCESS;
    char aszPath[RTPATH_MAX + 1];
    unsigned idxBusCur = 0;
    unsigned idxBusMax = 0;

    size_t cchPath = RTStrPrintf(&aszPath[0], RT_ELEMENTS(aszPath), UTS_PLATFORM_LNX_DUMMY_HCD_PATH "/%s.%u/usb*",
                                 pszHcdName, pHcd->idxDummyHcd);
    if (cchPath == RT_ELEMENTS(aszPath))
        return VERR_BUFFER_OVERFLOW;

    RTDIR hDir = NULL;
    rc = RTDirOpenFiltered(&hDir, aszPath, RTDIRFILTER_WINNT, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        do
        {
            RTDIRENTRY DirFolderContent;
            rc = RTDirRead(hDir, &DirFolderContent, NULL);
            if (RT_SUCCESS(rc))
            {
                uint32_t uBusId = 0;

                /* Extract the bus number - it is after "usb", i.e. "usb9" indicates a bus ID of 9. */
                rc = RTStrToUInt32Ex(&DirFolderContent.szName[3], NULL, 10, &uBusId);
                if (RT_SUCCESS(rc))
                {
                    /* Check whether this is a super speed bus. */
                    int64_t iSpeed = 0;
                    bool fSuperSpeed = false;
                    rc = RTLinuxSysFsReadIntFile(10, &iSpeed, UTS_PLATFORM_LNX_DUMMY_HCD_PATH "/%s.%u/%s/speed",
                                                 pszHcdName, pHcd->idxDummyHcd, DirFolderContent.szName);
                    if (   RT_SUCCESS(rc)
                        && (iSpeed == 5000 || iSpeed == 10000))
                    {
                        fSuperSpeed = true;
                        pHcd->fSuperSpeed = true;
                    }

                    /* Add to array of available busses for this HCD. */
                    if (idxBusCur == idxBusMax)
                    {
                        size_t cbNew = (idxBusMax + 10) * sizeof(UTSPLATFORMLNXDUMMYHCDBUS);
                        PUTSPLATFORMLNXDUMMYHCDBUS pNew = (PUTSPLATFORMLNXDUMMYHCDBUS)RTMemRealloc(pHcd->paBusses, cbNew);
                        if (pNew)
                        {
                            idxBusMax += 10;
                            pHcd->paBusses = pNew;
                        }
                    }

                    if (idxBusCur < idxBusMax)
                    {
                        pHcd->paBusses[idxBusCur].uBusId      = uBusId;
                        pHcd->paBusses[idxBusCur].fSuperSpeed = fSuperSpeed;
                        idxBusCur++;
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }
            }
        } while (RT_SUCCESS(rc));

        pHcd->cBusses = idxBusCur;

        if (rc == VERR_NO_MORE_FILES)
            rc = VINF_SUCCESS;

        RTDirClose(hDir);
    }

    return rc;
}


/**
 * Scans all available HCDs with the given name.
 *
 * @returns IPRT status code.
 * @param   pszHcdName        The base HCD name.
 * @param   pszUdcName        The base UDC name.
 */
static int utsPlatformLnxHcdScanByName(const char *pszHcdName, const char *pszUdcName)
{
    char aszPath[RTPATH_MAX + 1];
    size_t cchPath = RTStrPrintf(&aszPath[0], RT_ELEMENTS(aszPath),
                                 UTS_PLATFORM_LNX_DUMMY_HCD_PATH "/%s.*", pszHcdName);
    if (cchPath == RT_ELEMENTS(aszPath))
        return VERR_BUFFER_OVERFLOW;

    /* Enumerate the available HCD and their bus numbers. */
    RTDIR hDir = NULL;
    int rc = RTDirOpenFiltered(&hDir, aszPath, RTDIRFILTER_WINNT, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        unsigned idxHcdCur = g_cDummyHcd;
        unsigned idxHcdMax = g_cDummyHcd;

        do
        {
            RTDIRENTRY DirFolderContent;
            rc = RTDirRead(hDir, &DirFolderContent, NULL);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Get the HCD index and assigned bus number form the sysfs entries,
                 * Any error here is silently ignored and results in the HCD not being
                 * added to the list of available controllers.
                 */
                const char *pszIdx = RTStrStr(DirFolderContent.szName, ".");
                if (pszIdx)
                {
                    /* Skip the separator and convert number to index. */
                    pszIdx++;

                    uint32_t idxHcd = 0;
                    rc = RTStrToUInt32Ex(pszIdx, NULL, 10, &idxHcd);
                    if (RT_SUCCESS(rc))
                    {
                        /* Add to array of available HCDs. */
                        if (idxHcdCur == idxHcdMax)
                        {
                            size_t cbNew = (idxHcdMax + 10) * sizeof(UTSPLATFORMLNXDUMMYHCD);
                            PUTSPLATFORMLNXDUMMYHCD pNew = (PUTSPLATFORMLNXDUMMYHCD)RTMemRealloc(g_paDummyHcd, cbNew);
                            if (pNew)
                            {
                                idxHcdMax += 10;
                                g_paDummyHcd = pNew;
                            }
                        }

                        if (idxHcdCur < idxHcdMax)
                        {
                            g_paDummyHcd[idxHcdCur].idxDummyHcd = idxHcd;
                            g_paDummyHcd[idxHcdCur].pszHcdName  = pszHcdName;
                            g_paDummyHcd[idxHcdCur].pszUdcName  = pszUdcName;
                            g_paDummyHcd[idxHcdCur].fAvailable  = true;
                            g_paDummyHcd[idxHcdCur].fSuperSpeed = false;
                            g_paDummyHcd[idxHcdCur].cBusses     = 0;
                            g_paDummyHcd[idxHcdCur].paBusses    = NULL;
                            rc = utsPlatformLnxDummyHcdQueryBusses(&g_paDummyHcd[idxHcdCur], pszHcdName);
                            if (RT_SUCCESS(rc))
                                idxHcdCur++;
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }
                }
            }
        } while (RT_SUCCESS(rc));

        g_cDummyHcd = idxHcdCur;

        if (rc == VERR_NO_MORE_FILES)
            rc = VINF_SUCCESS;

        RTDirClose(hDir);
    }

    return rc;
}

DECLHIDDEN(int) utsPlatformInit(void)
{
    /* Load the modules required for setting up USB/IP testing. */
    int rc = utsPlatformModuleLoad("libcomposite", NULL, 0);
    if (RT_SUCCESS(rc))
    {
        const char *apszArg[] = { "num=20" }; /** @todo Make configurable from config. */
        rc = utsPlatformModuleLoad("dummy_hcd", &apszArg[0], RT_ELEMENTS(apszArg));
        if (RT_SUCCESS(rc))
            rc = utsPlatformModuleLoad("dummy_hcd_ss", &apszArg[0], RT_ELEMENTS(apszArg));
        if (RT_SUCCESS(rc))
            rc = utsPlatformLnxHcdScanByName("dummy_hcd", "dummy_udc");
        if (RT_SUCCESS(rc))
            rc = utsPlatformLnxHcdScanByName("dummy_hcd_ss", "dummy_udc_ss");
    }

    return rc;
}


DECLHIDDEN(void) utsPlatformTerm(void)
{
    /* Unload dummy HCD. */
    utsPlatformModuleUnload("dummy_hcd");
    utsPlatformModuleUnload("dummy_hcd_ss");

    RTMemFree(g_paDummyHcd);
}


DECLHIDDEN(int) utsPlatformModuleLoad(const char *pszModule, const char **papszArgv,
                                      unsigned cArgv)
{
    RTPROCESS hProcModprobe = NIL_RTPROCESS;
    const char **papszArgs = (const char **)RTMemAllocZ((3 + cArgv) * sizeof(const char *));
    if (RT_UNLIKELY(!papszArgs))
        return VERR_NO_MEMORY;

    papszArgs[0] = "modprobe";
    papszArgs[1] = pszModule;

    unsigned idx;
    for (idx = 0; idx < cArgv; idx++)
        papszArgs[2+idx] = papszArgv[idx];
    papszArgs[2+idx] = NULL;

    int rc = RTProcCreate("modprobe", papszArgs, RTENV_DEFAULT, RTPROC_FLAGS_SEARCH_PATH, &hProcModprobe);
    if (RT_SUCCESS(rc))
    {
        RTPROCSTATUS ProcSts;
        rc = RTProcWait(hProcModprobe, RTPROCWAIT_FLAGS_BLOCK, &ProcSts);
        if (RT_SUCCESS(rc))
        {
            /* Evaluate the process status. */
            if (   ProcSts.enmReason != RTPROCEXITREASON_NORMAL
                || ProcSts.iStatus != 0)
                rc = VERR_UNRESOLVED_ERROR; /** @todo Log and give finer grained status code. */
        }
    }

    RTMemFree(papszArgs);
    return rc;
}


DECLHIDDEN(int) utsPlatformModuleUnload(const char *pszModule)
{
    RTPROCESS hProcModprobe = NIL_RTPROCESS;
    const char *apszArgv[3];

    apszArgv[0] = "rmmod";
    apszArgv[1] = pszModule;
    apszArgv[2] = NULL;

    int rc = RTProcCreate("rmmod", apszArgv, RTENV_DEFAULT, RTPROC_FLAGS_SEARCH_PATH, &hProcModprobe);
    if (RT_SUCCESS(rc))
    {
        RTPROCSTATUS ProcSts;
        rc = RTProcWait(hProcModprobe, RTPROCWAIT_FLAGS_BLOCK, &ProcSts);
        if (RT_SUCCESS(rc))
        {
            /* Evaluate the process status. */
            if (   ProcSts.enmReason != RTPROCEXITREASON_NORMAL
                || ProcSts.iStatus != 0)
                rc = VERR_UNRESOLVED_ERROR; /** @todo Log and give finer grained status code. */
        }
    }

    return rc;
}


DECLHIDDEN(int) utsPlatformLnxAcquireUDC(bool fSuperSpeed, char **ppszUdc, uint32_t *puBusId)
{
    int rc = VERR_NOT_FOUND;

    for (unsigned i = 0; i < g_cDummyHcd; i++)
    {
        PUTSPLATFORMLNXDUMMYHCD pHcd = &g_paDummyHcd[i];

        /*
         * We can't use a super speed capable UDC for gadgets with lower speeds
         * because they hardcode the maximum speed to SuperSpeed most of the time
         * which will make it unusable for lower speeds.
         */
        if (   pHcd->fAvailable
            && pHcd->fSuperSpeed == fSuperSpeed)
        {
            /* Check all assigned busses for a speed match. */
            for (unsigned idxBus = 0; idxBus < pHcd->cBusses; idxBus++)
            {
                if (pHcd->paBusses[idxBus].fSuperSpeed == fSuperSpeed)
                {
                    rc = VINF_SUCCESS;
                    int cbRet = RTStrAPrintf(ppszUdc, "%s.%u", pHcd->pszUdcName, pHcd->idxDummyHcd);
                    if (cbRet == -1)
                        rc = VERR_NO_STR_MEMORY;
                    *puBusId = pHcd->paBusses[idxBus].uBusId;
                    pHcd->fAvailable = false;
                    break;
                }
            }

            if (rc != VERR_NOT_FOUND)
                break;
        }
    }

    return rc;
}


DECLHIDDEN(int) utsPlatformLnxReleaseUDC(const char *pszUdc)
{
    int rc = VERR_INVALID_PARAMETER;
    const char *pszIdx = RTStrStr(pszUdc, ".");
    if (pszIdx)
    {
        size_t cchUdcName = pszIdx - pszUdc;
        pszIdx++;
        uint32_t idxHcd = 0;
        rc = RTStrToUInt32Ex(pszIdx, NULL, 10, &idxHcd);
        if (RT_SUCCESS(rc))
        {
            rc = VERR_NOT_FOUND;

            for (unsigned i = 0; i < g_cDummyHcd; i++)
            {
                if (   g_paDummyHcd[i].idxDummyHcd == idxHcd
                    && !RTStrNCmp(g_paDummyHcd[i].pszUdcName, pszUdc, cchUdcName))
                {
                    AssertReturn(!g_paDummyHcd[i].fAvailable, VERR_INVALID_PARAMETER);
                    g_paDummyHcd[i].fAvailable = true;
                    rc = VINF_SUCCESS;
                    break;
                }
            }
        }
    }

    return rc;
}

