/* $Id: VBoxServiceCpuHotPlug.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxService - Guest Additions CPU Hot-Plugging Service.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

/** @page pg_vgsvc_cpuhotplug VBoxService - CPU Hot-Plugging
 *
 * The CPU Hot-Plugging subservice helps execute and coordinate CPU hot-plugging
 * between the guest OS and the VMM.
 *
 * CPU Hot-Plugging is useful for reallocating CPU resources from one VM to
 * other VMs or/and the host.  It talks to the VMM via VMMDev, new hot-plugging
 * events being signalled with an interrupt (no polling).
 *
 * Currently only supported for linux guests.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <VBox/VBoxGuestLib.h>
#include "VBoxServiceInternal.h"

#ifdef RT_OS_LINUX
# include <iprt/linux/sysfs.h>
# include <errno.h> /* For the sysfs API */
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef RT_OS_LINUX

/** @name Paths to access the CPU device
 * @{
 */
# define SYSFS_ACPI_CPU_PATH    "/sys/devices"
# define SYSFS_CPU_PATH         "/sys/devices/system/cpu"
/** @} */

/** Path component for the ACPI CPU path. */
typedef struct SYSFSCPUPATHCOMP
{
    /** Flag whether the name is suffixed with a number */
    bool        fNumberedSuffix;
    /** Name of the component */
    const char *pcszName;
} SYSFSCPUPATHCOMP, *PSYSFSCPUPATHCOMP;
/** Pointer to a const component. */
typedef const SYSFSCPUPATHCOMP *PCSYSFSCPUPATHCOMP;

/**
 * Structure which defines how the entries are assembled.
 */
typedef struct SYSFSCPUPATH
{
    /** Id when probing for the correct path. */
    uint32_t           uId;
    /** Array holding the possible components. */
    PCSYSFSCPUPATHCOMP aComponentsPossible;
    /** Number of entries in the array, excluding the terminator. */
    unsigned           cComponents;
    /** Directory handle */
    RTDIR              hDir;
    /** Current directory to try. */
    char              *pszPath;
} SYSFSCPUPATH, *PSYSFSCPUPATH;

/** Content of uId if the path wasn't probed yet. */
# define ACPI_CPU_PATH_NOT_PROBED UINT32_MAX
#endif /* RT_OS_LINUX*/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef RT_OS_LINUX
/** Possible combinations of all path components for level 1. */
static const SYSFSCPUPATHCOMP g_aAcpiCpuPathLvl1[] =
{
    /** LNXSYSTEM:<id> */
    { true, "LNXSYSTM:*" }
};

/** Possible combinations of all path components for level 2. */
static const SYSFSCPUPATHCOMP g_aAcpiCpuPathLvl2[] =
{
    /** device:<id> */
    { true, "device:*" },
    /** LNXSYBUS:<id> */
    { true, "LNXSYBUS:*" }
};

/** Possible combinations of all path components for level 3 */
static const SYSFSCPUPATHCOMP g_aAcpiCpuPathLvl3[] =
{
    /** ACPI0004:<id> */
    { true, "ACPI0004:*" }
};

/** Possible combinations of all path components for level 4 */
static const SYSFSCPUPATHCOMP g_aAcpiCpuPathLvl4[] =
{
    /** LNXCPU:<id> */
    { true, "LNXCPU:*" },
    /** ACPI_CPU:<id> */
    { true, "ACPI_CPU:*" }
};

/** All possible combinations. */
static SYSFSCPUPATH g_aAcpiCpuPath[] =
{
    /** Level 1 */
    { ACPI_CPU_PATH_NOT_PROBED, g_aAcpiCpuPathLvl1, RT_ELEMENTS(g_aAcpiCpuPathLvl1), NULL, NULL },
    /** Level 2 */
    { ACPI_CPU_PATH_NOT_PROBED, g_aAcpiCpuPathLvl2, RT_ELEMENTS(g_aAcpiCpuPathLvl2), NULL, NULL },
    /** Level 3 */
    { ACPI_CPU_PATH_NOT_PROBED, g_aAcpiCpuPathLvl3, RT_ELEMENTS(g_aAcpiCpuPathLvl3), NULL, NULL },
    /** Level 4 */
    { ACPI_CPU_PATH_NOT_PROBED, g_aAcpiCpuPathLvl4, RT_ELEMENTS(g_aAcpiCpuPathLvl4), NULL, NULL },
};

/**
 * Possible directories to get to the topology directory for reading core and package id.
 *
 * @remark: This is not part of the path above because the eject file is not in one of the directories
 *          below and would make the hot unplug code fail.
 */
static const char *g_apszTopologyPath[] =
{
    "sysdev",
    "physical_node"
};

#endif /* RT_OS_LINUX*/


#ifdef RT_OS_LINUX

/**
 * Probes for the correct path to the ACPI CPU object in sysfs for the
 * various different kernel versions and distro's.
 *
 * @returns VBox status code.
 */
static int vgsvcCpuHotPlugProbePath(void)
{
    int rc = VINF_SUCCESS;

    /* Probe for the correct path if we didn't already. */
    if (RT_UNLIKELY(g_aAcpiCpuPath[0].uId == ACPI_CPU_PATH_NOT_PROBED))
    {
        char *pszPath = NULL;   /** < Current path, increasing while we dig deeper. */

        pszPath = RTStrDup(SYSFS_ACPI_CPU_PATH);
        if (!pszPath)
            return VERR_NO_MEMORY;

        /*
         * Simple algorithm to find the path.
         * Performance is not a real problem because it is
         * only executed once.
         */
        for (unsigned iLvlCurr = 0; iLvlCurr < RT_ELEMENTS(g_aAcpiCpuPath); iLvlCurr++)
        {
            PSYSFSCPUPATH pAcpiCpuPathLvl = &g_aAcpiCpuPath[iLvlCurr];

            for (unsigned iCompCurr = 0; iCompCurr < pAcpiCpuPathLvl->cComponents; iCompCurr++)
            {
                PCSYSFSCPUPATHCOMP pPathComponent = &pAcpiCpuPathLvl->aComponentsPossible[iCompCurr];

                /* Open the directory */
                RTDIR hDirCurr = NIL_RTDIR;
                char *pszPathTmp = RTPathJoinA(pszPath, pPathComponent->pcszName);
                if (pszPathTmp)
                {
                    rc = RTDirOpenFiltered(&hDirCurr, pszPathTmp, RTDIRFILTER_WINNT, 0 /*fFlags*/);
                    RTStrFree(pszPathTmp);
                }
                else
                    rc = VERR_NO_STR_MEMORY;
                if (RT_FAILURE(rc))
                    break;

                /* Search if the current directory contains one of the possible parts. */
                size_t cchName = strlen(pPathComponent->pcszName);
                RTDIRENTRY DirFolderContent;
                bool fFound = false;

                /* Get rid of the * filter which is in the path component. */
                if (pPathComponent->fNumberedSuffix)
                    cchName--;

                while (RT_SUCCESS(RTDirRead(hDirCurr, &DirFolderContent, NULL))) /* Assumption that szName has always enough space */
                {
                    if (   DirFolderContent.cbName >= cchName
                        && !strncmp(DirFolderContent.szName, pPathComponent->pcszName, cchName))
                    {
                        /* Found, use the complete name to dig deeper. */
                        fFound = true;
                        pAcpiCpuPathLvl->uId = iCompCurr;
                        char *pszPathLvl = RTPathJoinA(pszPath, DirFolderContent.szName);
                        if (pszPathLvl)
                        {
                            RTStrFree(pszPath);
                            pszPath = pszPathLvl;
                        }
                        else
                            rc = VERR_NO_STR_MEMORY;
                        break;
                    }
                }
                RTDirClose(hDirCurr);

                if (fFound)
                    break;
            } /* For every possible component. */

            /* No matching component for this part, no need to continue */
            if (RT_FAILURE(rc))
                break;
        } /* For every level */

        VGSvcVerbose(1, "Final path after probing %s rc=%Rrc\n", pszPath, rc);
        RTStrFree(pszPath);
    }

    return rc;
}


/**
 * Returns the path of the ACPI CPU device with the given core and package ID.
 *
 * @returns VBox status code.
 * @param   ppszPath     Where to store the path.
 * @param   idCpuCore    The core ID of the CPU.
 * @param   idCpuPackage The package ID of the CPU.
 */
static int vgsvcCpuHotPlugGetACPIDevicePath(char **ppszPath, uint32_t idCpuCore, uint32_t idCpuPackage)
{
    int rc = VINF_SUCCESS;

    AssertPtrReturn(ppszPath, VERR_INVALID_PARAMETER);

    rc = vgsvcCpuHotPlugProbePath();
    if (RT_SUCCESS(rc))
    {
        /* Build the path from all components. */
        bool fFound = false;
        unsigned iLvlCurr = 0;
        char *pszPath = NULL;
        char *pszPathDir = NULL;
        PSYSFSCPUPATH pAcpiCpuPathLvl = &g_aAcpiCpuPath[iLvlCurr];

        /* Init everything. */
        Assert(pAcpiCpuPathLvl->uId != ACPI_CPU_PATH_NOT_PROBED);
        pszPath = RTPathJoinA(SYSFS_ACPI_CPU_PATH, pAcpiCpuPathLvl->aComponentsPossible[pAcpiCpuPathLvl->uId].pcszName);
        if (!pszPath)
            return VERR_NO_STR_MEMORY;

        pAcpiCpuPathLvl->pszPath = RTStrDup(SYSFS_ACPI_CPU_PATH);
        if (!pAcpiCpuPathLvl->pszPath)
        {
            RTStrFree(pszPath);
            return VERR_NO_STR_MEMORY;
        }

        /* Open the directory */
        rc = RTDirOpenFiltered(&pAcpiCpuPathLvl->hDir, pszPath, RTDIRFILTER_WINNT, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            RTStrFree(pszPath);
            pszPath = NULL;

            /* Search for CPU */
            while (!fFound)
            {
                /* Get the next directory. */
                RTDIRENTRY DirFolderContent;
                rc = RTDirRead(pAcpiCpuPathLvl->hDir, &DirFolderContent, NULL);
                if (RT_SUCCESS(rc))
                {
                    /* Create the new path. */
                    char *pszPathCurr = RTPathJoinA(pAcpiCpuPathLvl->pszPath, DirFolderContent.szName);
                    if (!pszPathCurr)
                    {
                        rc = VERR_NO_STR_MEMORY;
                        break;
                    }

                    /* If this is the last level check for the given core and package id. */
                    if (iLvlCurr == RT_ELEMENTS(g_aAcpiCpuPath) - 1)
                    {
                        /* Get the sysdev */
                        uint32_t idCore = 0;
                        uint32_t idPackage = 0;

                        for (unsigned i = 0; i < RT_ELEMENTS(g_apszTopologyPath); i++)
                        {
                            int64_t i64Core    = 0;
                            int64_t i64Package = 0;

                            int rc2 = RTLinuxSysFsReadIntFile(10, &i64Core, "%s/%s/topology/core_id",
                                                              pszPathCurr, g_apszTopologyPath[i]);
                            if (RT_SUCCESS(rc2))
                                rc2 = RTLinuxSysFsReadIntFile(10, &i64Package, "%s/%s/topology/physical_package_id",
                                                              pszPathCurr, g_apszTopologyPath[i]);

                            if (RT_SUCCESS(rc2))
                            {
                                idCore = (uint32_t)i64Core;
                                idPackage = (uint32_t)i64Package;
                                break;
                            }
                        }

                        if (   idCore    == idCpuCore
                            && idPackage == idCpuPackage)
                        {
                            /* Return the path */
                            pszPath = pszPathCurr;
                            fFound = true;
                            VGSvcVerbose(3, "CPU found\n");
                            break;
                        }
                        else
                        {
                            /* Get the next directory. */
                            RTStrFree(pszPathCurr);
                            pszPathCurr = NULL;
                            VGSvcVerbose(3, "CPU doesn't match, next directory\n");
                        }
                    }
                    else
                    {
                        /* Go deeper */
                        iLvlCurr++;

                        VGSvcVerbose(3, "Going deeper (iLvlCurr=%u)\n", iLvlCurr);

                        pAcpiCpuPathLvl = &g_aAcpiCpuPath[iLvlCurr];

                        Assert(pAcpiCpuPathLvl->hDir == NIL_RTDIR);
                        Assert(!pAcpiCpuPathLvl->pszPath);
                        pAcpiCpuPathLvl->pszPath = pszPathCurr;
                        PCSYSFSCPUPATHCOMP pPathComponent = &pAcpiCpuPathLvl->aComponentsPossible[pAcpiCpuPathLvl->uId];

                        Assert(pAcpiCpuPathLvl->uId != ACPI_CPU_PATH_NOT_PROBED);

                        pszPathDir = RTPathJoinA(pszPathCurr, pPathComponent->pcszName);
                        if (!pszPathDir)
                        {
                            rc = VERR_NO_STR_MEMORY;
                            break;
                        }

                        VGSvcVerbose(3, "New path %s\n", pszPathDir);

                        /* Open the directory */
                        rc = RTDirOpenFiltered(&pAcpiCpuPathLvl->hDir, pszPathDir, RTDIRFILTER_WINNT, 0 /*fFlags*/);
                        RTStrFree(pszPathDir);
                        pszPathDir = NULL;
                        if (RT_FAILURE(rc))
                            break;
                    }
                }
                else
                {
                    RTDirClose(pAcpiCpuPathLvl->hDir);
                    RTStrFree(pAcpiCpuPathLvl->pszPath);
                    pAcpiCpuPathLvl->hDir = NIL_RTDIR;
                    pAcpiCpuPathLvl->pszPath = NULL;

                    /*
                     * If we reached the end we didn't find the matching path
                     * meaning the CPU is already offline.
                     */
                    if (!iLvlCurr)
                    {
                        rc = VERR_NOT_FOUND;
                        break;
                    }

                    iLvlCurr--;
                    pAcpiCpuPathLvl = &g_aAcpiCpuPath[iLvlCurr];
                    VGSvcVerbose(3, "Directory not found, going back (iLvlCurr=%u)\n", iLvlCurr);
                }
            } /* while not found */
        } /* Successful init */

        /* Cleanup */
        for (unsigned i = 0; i < RT_ELEMENTS(g_aAcpiCpuPath); i++)
        {
            if (g_aAcpiCpuPath[i].hDir)
                RTDirClose(g_aAcpiCpuPath[i].hDir);
            if (g_aAcpiCpuPath[i].pszPath)
                RTStrFree(g_aAcpiCpuPath[i].pszPath);
            g_aAcpiCpuPath[i].hDir = NIL_RTDIR;
            g_aAcpiCpuPath[i].pszPath = NULL;
        }
        if (pszPathDir)
            RTStrFree(pszPathDir);
        if (RT_FAILURE(rc) && pszPath)
            RTStrFree(pszPath);

        if (RT_SUCCESS(rc))
            *ppszPath = pszPath;
    }

    return rc;
}

#endif /* RT_OS_LINUX */

/**
 * Handles VMMDevCpuEventType_Plug.
 *
 * @param   idCpuCore       The CPU core ID.
 * @param   idCpuPackage    The CPU package ID.
 */
static void vgsvcCpuHotPlugHandlePlugEvent(uint32_t idCpuCore, uint32_t idCpuPackage)
{
#ifdef RT_OS_LINUX
    /*
     * The topology directory (containing the physical and core id properties)
     * is not available until the CPU is online. So we just iterate over all directories
     * and enable the first CPU which is not online already.
     * Because the directory might not be available immediately we try a few times.
     *
     */
    /** @todo Maybe use udev to monitor hot-add events from the kernel */
    bool fCpuOnline = false;
    unsigned cTries = 5;

    do
    {
        RTDIR hDirDevices = NULL;
        int rc = RTDirOpen(&hDirDevices, SYSFS_CPU_PATH);
        if (RT_SUCCESS(rc))
        {
            RTDIRENTRY DirFolderContent;
            while (RT_SUCCESS(RTDirRead(hDirDevices, &DirFolderContent, NULL))) /* Assumption that szName has always enough space */
            {
                /* Check if this is a CPU object which can be brought online. */
                if (RTLinuxSysFsExists("%s/%s/online", SYSFS_CPU_PATH, DirFolderContent.szName))
                {
                    /* Check the status of the CPU by reading the online flag. */
                    int64_t i64Status = 0;
                    rc = RTLinuxSysFsReadIntFile(10 /*uBase*/, &i64Status, "%s/%s/online", SYSFS_CPU_PATH, DirFolderContent.szName);
                    if (   RT_SUCCESS(rc)
                        && i64Status == 0)
                    {
                        /* CPU is offline, turn it on. */
                        rc = RTLinuxSysFsWriteU8File(10 /*uBase*/, 1, "%s/%s/online", SYSFS_CPU_PATH, DirFolderContent.szName);
                        if (RT_SUCCESS(rc))
                        {
                            VGSvcVerbose(1, "CpuHotPlug: CPU %u/%u was brought online\n", idCpuPackage, idCpuCore);
                            fCpuOnline = true;
                            break;
                        }
                    }
                    else if (RT_FAILURE(rc))
                        VGSvcError("CpuHotPlug: Failed to open '%s/%s/online' rc=%Rrc\n",
                                   SYSFS_CPU_PATH, DirFolderContent.szName, rc);
                    else
                    {
                        /*
                         * Check whether the topology matches what we got (which means someone raced us and brought the CPU
                         * online already).
                         */
                        int64_t i64Core    = 0;
                        int64_t i64Package = 0;

                        int rc2 = RTLinuxSysFsReadIntFile(10, &i64Core, "%s/%s/topology/core_id",
                                                          SYSFS_CPU_PATH, DirFolderContent.szName);
                        if (RT_SUCCESS(rc2))
                            rc2 = RTLinuxSysFsReadIntFile(10, &i64Package, "%s/%s/topology/physical_package_id",
                                                          SYSFS_CPU_PATH, DirFolderContent.szName);
                        if (   RT_SUCCESS(rc2)
                            && idCpuPackage == i64Package
                            && idCpuCore == i64Core)
                        {
                            VGSvcVerbose(1, "CpuHotPlug: '%s' is already online\n", DirFolderContent.szName);
                            fCpuOnline = true;
                            break;
                        }
                    }
                }
            }
            RTDirClose(hDirDevices);
        }
        else
            VGSvcError("CpuHotPlug: Failed to open path %s rc=%Rrc\n", SYSFS_CPU_PATH, rc);

        /* Sleep a bit */
        if (!fCpuOnline)
            RTThreadSleep(100);

    } while (   !fCpuOnline
             && cTries-- > 0);
#else
# error "Port me"
#endif
}


/**
 * Handles VMMDevCpuEventType_Unplug.
 *
 * @param   idCpuCore       The CPU core ID.
 * @param   idCpuPackage    The CPU package ID.
 */
static void vgsvcCpuHotPlugHandleUnplugEvent(uint32_t idCpuCore, uint32_t idCpuPackage)
{
#ifdef RT_OS_LINUX
    char *pszCpuDevicePath = NULL;
    int rc = vgsvcCpuHotPlugGetACPIDevicePath(&pszCpuDevicePath, idCpuCore, idCpuPackage);
    if (RT_SUCCESS(rc))
    {
        RTFILE hFileCpuEject;
        rc = RTFileOpenF(&hFileCpuEject, RTFILE_O_WRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE, "%s/eject", pszCpuDevicePath);
        if (RT_SUCCESS(rc))
        {
            /* Write a 1 to eject the CPU */
            rc = RTFileWrite(hFileCpuEject, "1", 1, NULL);
            if (RT_SUCCESS(rc))
                VGSvcVerbose(1, "CpuHotPlug: CPU %u/%u was ejected\n", idCpuPackage, idCpuCore);
            else
                VGSvcError("CpuHotPlug: Failed to eject CPU %u/%u rc=%Rrc\n", idCpuPackage, idCpuCore, rc);

            RTFileClose(hFileCpuEject);
        }
        else
            VGSvcError("CpuHotPlug: Failed to open '%s/eject' rc=%Rrc\n", pszCpuDevicePath, rc);
        RTStrFree(pszCpuDevicePath);
    }
    else if (rc == VERR_NOT_FOUND)
        VGSvcVerbose(1, "CpuHotPlug: CPU %u/%u was aleady ejected by someone else!\n", idCpuPackage, idCpuCore);
    else
        VGSvcError("CpuHotPlug: Failed to get CPU device path rc=%Rrc\n", rc);
#else
# error "Port me"
#endif
}


/** @interface_method_impl{VBOXSERVICE,pfnWorker} */
static DECLCALLBACK(int) vgsvcCpuHotPlugWorker(bool volatile *pfShutdown)
{
    /*
     * Tell the control thread that it can continue spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

    /*
     * Enable the CPU hotplug notifier.
     */
    int rc = VbglR3CpuHotPlugInit();
    if (RT_FAILURE(rc))
        return rc;

    /*
     * The Work Loop.
     */
    for (;;)
    {
        /* Wait for CPU hot-plugging event. */
        uint32_t            idCpuCore;
        uint32_t            idCpuPackage;
        VMMDevCpuEventType  enmEventType;
        rc = VbglR3CpuHotPlugWaitForEvent(&enmEventType, &idCpuCore, &idCpuPackage);
        if (RT_SUCCESS(rc))
        {
            VGSvcVerbose(3, "CpuHotPlug: Event happened idCpuCore=%u idCpuPackage=%u enmEventType=%d\n",
                         idCpuCore, idCpuPackage, enmEventType);
            switch (enmEventType)
            {
                case VMMDevCpuEventType_Plug:
                    vgsvcCpuHotPlugHandlePlugEvent(idCpuCore, idCpuPackage);
                    break;

                case VMMDevCpuEventType_Unplug:
                    vgsvcCpuHotPlugHandleUnplugEvent(idCpuCore, idCpuPackage);
                    break;

                default:
                {
                    static uint32_t s_iErrors = 0;
                    if (s_iErrors++ < 10)
                        VGSvcError("CpuHotPlug: Unknown event: idCpuCore=%u idCpuPackage=%u enmEventType=%d\n",
                                   idCpuCore, idCpuPackage, enmEventType);
                    break;
                }
            }
        }
        else if (rc != VERR_INTERRUPTED && rc != VERR_TRY_AGAIN)
        {
            VGSvcError("CpuHotPlug: VbglR3CpuHotPlugWaitForEvent returned %Rrc\n", rc);
            break;
        }

        if (*pfShutdown)
            break;
    }

    VbglR3CpuHotPlugTerm();
    return rc;
}


/** @interface_method_impl{VBOXSERVICE,pfnStop} */
static DECLCALLBACK(void) vgsvcCpuHotPlugStop(void)
{
    VbglR3InterruptEventWaits();
    return;
}


/**
 * The 'CpuHotPlug' service description.
 */
VBOXSERVICE g_CpuHotPlug =
{
    /* pszName. */
    "cpuhotplug",
    /* pszDescription. */
    "CPU hot-plugging monitor",
    /* pszUsage. */
    NULL,
    /* pszOptions. */
    NULL,
    /* methods */
    VGSvcDefaultPreInit,
    VGSvcDefaultOption,
    VGSvcDefaultInit,
    vgsvcCpuHotPlugWorker,
    vgsvcCpuHotPlugStop,
    VGSvcDefaultTerm
};

