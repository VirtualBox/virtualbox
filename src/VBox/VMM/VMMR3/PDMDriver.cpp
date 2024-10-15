/* $Id: PDMDriver.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Pluggable Device and Driver Manager, Driver parts.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PDM_DRIVER
#include "PDMInternal.h"
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/sup.h>
#include <VBox/vmm/vmcc.h>

#include <VBox/version.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/thread.h>
#include <iprt/path.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Internal callback structure pointer.
 *
 * The main purpose is to define the extra data we associate
 * with PDMDRVREGCB so we can find the VM instance and so on.
 */
typedef struct PDMDRVREGCBINT
{
    /** The callback structure. */
    PDMDRVREGCB     Core;
    /** A bit of padding. */
    uint32_t        u32[4];
    /** VM Handle. */
    PVM             pVM;
    /** Pointer to the configuration node the registrations should be
     * associated with.  Can be NULL. */
    PCFGMNODE       pCfgNode;
} PDMDRVREGCBINT, *PPDMDRVREGCBINT;
typedef const PDMDRVREGCBINT *PCPDMDRVREGCBINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) pdmR3DrvRegister(PCPDMDRVREGCB pCallbacks, PCPDMDRVREG pReg);
static int pdmR3DrvLoad(PVM pVM, PPDMDRVREGCBINT pRegCB, const char *pszFilename, const char *pszName);


/**
 * Register drivers in a statically linked environment.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pfnCallback Driver registration callback
 */
VMMR3DECL(int) PDMR3DrvStaticRegistration(PVM pVM, FNPDMVBOXDRIVERSREGISTER pfnCallback)
{
    /*
     * The registration callbacks.
     */
    PDMDRVREGCBINT  RegCB;
    RegCB.Core.u32Version   = PDM_DRVREG_CB_VERSION;
    RegCB.Core.pfnRegister  = pdmR3DrvRegister;
    RegCB.pVM               = pVM;
    RegCB.pCfgNode          = NULL;

    int rc = pfnCallback(&RegCB.Core, VBOX_VERSION);
    if (RT_FAILURE(rc))
        AssertMsgFailed(("VBoxDriversRegister failed with rc=%Rrc\n", rc));

    return rc;
}


/**
 * This function will initialize the drivers for this VM instance.
 *
 * First of all this mean loading the builtin drivers and letting them
 * register themselves. Beyond that any additional driver modules are
 * loaded and called for registration.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
int pdmR3DrvInit(PVM pVM)
{
    LogFlow(("pdmR3DrvInit:\n"));

    AssertRelease(!(RT_UOFFSETOF(PDMDRVINS, achInstanceData) & 15));
    PPDMDRVINS pDrvInsAssert; NOREF(pDrvInsAssert);
    AssertCompile(sizeof(pDrvInsAssert->Internal.s) <= sizeof(pDrvInsAssert->Internal.padding));
    AssertRelease(sizeof(pDrvInsAssert->Internal.s) <= sizeof(pDrvInsAssert->Internal.padding));

    /*
     * The registration callbacks.
     */
    PDMDRVREGCBINT  RegCB;
    RegCB.Core.u32Version   = PDM_DRVREG_CB_VERSION;
    RegCB.Core.pfnRegister  = pdmR3DrvRegister;
    RegCB.pVM               = pVM;
    RegCB.pCfgNode          = NULL;

    /*
     * Load the builtin module
     */
    PCFGMNODE pDriversNode = CFGMR3GetChild(CFGMR3GetRoot(pVM), "PDM/Drivers");
    bool fLoadBuiltin;
    int rc = CFGMR3QueryBool(pDriversNode, "LoadBuiltin", &fLoadBuiltin);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
        fLoadBuiltin = true;
    else if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: Querying boolean \"LoadBuiltin\" failed with %Rrc\n", rc));
        return rc;
    }
    if (fLoadBuiltin)
    {
        /* make filename */
        char *pszFilename = pdmR3FileR3("VBoxDD", true /*fShared*/);
        if (!pszFilename)
            return VERR_NO_TMP_MEMORY;
        rc = pdmR3DrvLoad(pVM, &RegCB, pszFilename, "VBoxDD");
        RTMemTmpFree(pszFilename);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Load additional driver modules.
     */
    for (PCFGMNODE pCur = CFGMR3GetFirstChild(pDriversNode); pCur; pCur = CFGMR3GetNextChild(pCur))
    {
        /*
         * Get the name and path.
         */
        char szName[PDMMOD_NAME_LEN];
        rc = CFGMR3GetName(pCur, &szName[0], sizeof(szName));
        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
        {
            AssertMsgFailed(("configuration error: The module name is too long, cchName=%zu.\n", CFGMR3GetNameLen(pCur)));
            return VERR_PDM_MODULE_NAME_TOO_LONG;
        }
        else if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("CFGMR3GetName -> %Rrc.\n", rc));
            return rc;
        }

        /* the path is optional, if no path the module name + path is used. */
        char szFilename[RTPATH_MAX];
        rc = CFGMR3QueryString(pCur, "Path", &szFilename[0], sizeof(szFilename));
        if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
            strcpy(szFilename, szName);
        else if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("configuration error: Failure to query the module path, rc=%Rrc.\n", rc));
            return rc;
        }

        /* prepend path? */
        if (!RTPathHavePath(szFilename))
        {
            char *psz = pdmR3FileR3(szFilename, false /*fShared*/);
            if (!psz)
                return VERR_NO_TMP_MEMORY;
            size_t cch = strlen(psz) + 1;
            if (cch > sizeof(szFilename))
            {
                RTMemTmpFree(psz);
                AssertMsgFailed(("Filename too long! cch=%d '%s'\n", cch, psz));
                return VERR_FILENAME_TOO_LONG;
            }
            memcpy(szFilename, psz, cch);
            RTMemTmpFree(psz);
        }

        /*
         * Load the module and register it's drivers.
         */
        RegCB.pCfgNode = pCur;
        rc = pdmR3DrvLoad(pVM, &RegCB, szFilename, szName);
        if (RT_FAILURE(rc))
            return rc;
    }

    LogFlow(("pdmR3DrvInit: returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


/**
 * Loads one driver module and call the registration entry point.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pRegCB          The registration callback stuff.
 * @param   pszFilename     Module filename.
 * @param   pszName         Module name.
 */
static int pdmR3DrvLoad(PVM pVM, PPDMDRVREGCBINT pRegCB, const char *pszFilename, const char *pszName)
{
    /*
     * Load it.
     */
    int rc = pdmR3LoadR3U(pVM->pUVM, pszFilename, pszName);
    if (RT_SUCCESS(rc))
    {
        /*
         * Get the registration export and call it.
         */
        FNPDMVBOXDRIVERSREGISTER *pfnVBoxDriversRegister;
        rc = PDMR3LdrGetSymbolR3(pVM, pszName, "VBoxDriversRegister", (void **)&pfnVBoxDriversRegister);
        if (RT_SUCCESS(rc))
        {
            Log(("PDM: Calling VBoxDriversRegister (%p) of %s (%s)\n", pfnVBoxDriversRegister, pszName, pszFilename));
            rc = pfnVBoxDriversRegister(&pRegCB->Core, VBOX_VERSION);
            if (RT_SUCCESS(rc))
                Log(("PDM: Successfully loaded driver module %s (%s).\n", pszName, pszFilename));
            else
                AssertMsgFailed(("VBoxDriversRegister failed with rc=%Rrc\n", rc));
        }
        else
        {
            AssertMsgFailed(("Failed to locate 'VBoxDriversRegister' in %s (%s) rc=%Rrc\n", pszName, pszFilename, rc));
            if (rc == VERR_SYMBOL_NOT_FOUND)
                rc = VERR_PDM_NO_REGISTRATION_EXPORT;
        }
    }
    else
        AssertMsgFailed(("Failed to load %s (%s) rc=%Rrc!\n", pszName, pszFilename, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVREGCB,pfnRegister} */
static DECLCALLBACK(int) pdmR3DrvRegister(PCPDMDRVREGCB pCallbacks, PCPDMDRVREG pReg)
{
    /*
     * Validate the registration structure.
     */
    AssertPtrReturn(pReg, VERR_INVALID_POINTER);
    AssertMsgReturn(pReg->u32Version == PDM_DRVREG_VERSION,
                    ("%#x\n", pReg->u32Version),
                    VERR_PDM_UNKNOWN_DRVREG_VERSION);
    AssertReturn(pReg->szName[0], VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn(RTStrEnd(pReg->szName, sizeof(pReg->szName)),
                    ("%.*s\n", sizeof(pReg->szName), pReg->szName),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn(pdmR3IsValidName(pReg->szName), ("%.*s\n", sizeof(pReg->szName), pReg->szName),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn(    !(pReg->fFlags & PDM_DRVREG_FLAGS_R0)
                    ||  (   pReg->szR0Mod[0]
                         && RTStrEnd(pReg->szR0Mod, sizeof(pReg->szR0Mod))),
                    ("%s: %.*s\n", pReg->szName, sizeof(pReg->szR0Mod), pReg->szR0Mod),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn(    !(pReg->fFlags & PDM_DRVREG_FLAGS_RC)
                    ||  (   pReg->szRCMod[0]
                         && RTStrEnd(pReg->szRCMod, sizeof(pReg->szRCMod))),
                    ("%s: %.*s\n", pReg->szName, sizeof(pReg->szRCMod), pReg->szRCMod),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn(RT_VALID_PTR(pReg->pszDescription),
                    ("%s: %p\n", pReg->szName, pReg->pszDescription),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn(!(pReg->fFlags & ~(PDM_DRVREG_FLAGS_HOST_BITS_MASK | PDM_DRVREG_FLAGS_R0 | PDM_DRVREG_FLAGS_RC)),
                    ("%s: %#x\n", pReg->szName, pReg->fFlags),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn((pReg->fFlags & PDM_DRVREG_FLAGS_HOST_BITS_MASK) == PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
                    ("%s: %#x\n", pReg->szName, pReg->fFlags),
                    VERR_PDM_INVALID_DRIVER_HOST_BITS);
    AssertMsgReturn(pReg->cMaxInstances > 0,
                    ("%s: %#x\n", pReg->szName, pReg->cMaxInstances),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn(pReg->cbInstance <= _1M,
                    ("%s: %#x\n", pReg->szName, pReg->cbInstance),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn(RT_VALID_PTR(pReg->pfnConstruct),
                    ("%s: %p\n", pReg->szName, pReg->pfnConstruct),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn(RT_VALID_PTR(pReg->pfnRelocate) || !(pReg->fFlags & PDM_DRVREG_FLAGS_RC),
                    ("%s: %#x\n", pReg->szName, pReg->cbInstance),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn(pReg->pfnSoftReset == NULL,
                    ("%s: %p\n", pReg->szName, pReg->pfnSoftReset),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);
    AssertMsgReturn(pReg->u32VersionEnd == PDM_DRVREG_VERSION,
                    ("%s: %#x\n", pReg->szName, pReg->u32VersionEnd),
                    VERR_PDM_INVALID_DRIVER_REGISTRATION);

    /*
     * Check for duplicate and find FIFO entry at the same time.
     */
    PCPDMDRVREGCBINT const  pRegCB = (PCPDMDRVREGCBINT)pCallbacks;
    PVM const               pVM    = pRegCB->pVM;
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    PPDMDRV pDrvPrev = NULL;
    PPDMDRV pDrv = pRegCB->pVM->pdm.s.pDrvs;
    for (; pDrv; pDrvPrev = pDrv, pDrv = pDrv->pNext)
    {
        if (!strcmp(pDrv->pReg->szName, pReg->szName))
        {
            RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
            AssertMsgFailed(("Driver '%s' already exists\n", pReg->szName));
            return VERR_PDM_DRIVER_NAME_CLASH;
        }
    }

    /*
     * Allocate new driver structure and insert it into the list.
     */
    int rc;
    pDrv = (PPDMDRV)MMR3HeapAlloc(pRegCB->pVM, MM_TAG_PDM_DRIVER, sizeof(*pDrv));
    if (pDrv)
    {
        pDrv->pNext         = NULL;
        pDrv->cInstances    = 0;
        pDrv->iNextInstance = 0;
        pDrv->pReg          = pReg;
        pDrv->pInstances    = NULL;
        rc = CFGMR3QueryStringAllocDef(    pRegCB->pCfgNode, "RCSearchPath", &pDrv->pszRCSearchPath, NULL);
        if (RT_SUCCESS(rc))
            rc = CFGMR3QueryStringAllocDef(pRegCB->pCfgNode, "R0SearchPath", &pDrv->pszR0SearchPath, NULL);
        if (RT_SUCCESS(rc))
        {
            if (pDrvPrev)
                pDrvPrev->pNext = pDrv;
            else
                pRegCB->pVM->pdm.s.pDrvs = pDrv;
            RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

            Log(("PDM: Registered driver '%s'\n", pReg->szName));
            return VINF_SUCCESS;
        }
        MMR3HeapFree(pDrv);
    }
    else
        rc = VERR_NO_MEMORY;
    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
    return rc;
}


/**
 * Lookups a driver structure by name.
 * @internal
 */
PPDMDRV pdmR3DrvLookup(PVM pVM, const char *pszName)
{
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    for (PPDMDRV pDrv = pVM->pdm.s.pDrvs; pDrv; pDrv = pDrv->pNext)
        if (!strcmp(pDrv->pReg->szName, pszName))
        {
            RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
            return pDrv;
        }
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
    return NULL;
}


/**
 * Transforms the driver chain as it's being instantiated.
 *
 * Worker for pdmR3DrvInstantiate.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pDrvAbove           The driver above, NULL if top.
 * @param   pLun                The LUN.
 * @param   ppNode              The AttachedDriver node, replaced if any
 *                              morphing took place.
 * @note    Caller owns CoreListCritSectRw exclusivly.
 */
static int pdmR3DrvMaybeTransformChain(PVM pVM, PPDMDRVINS pDrvAbove, PPDMLUN pLun, PCFGMNODE *ppNode)
{
    /*
     * The typical state of affairs is that there are no injections.
     */
    PCFGMNODE pCurTrans = CFGMR3GetFirstChild(CFGMR3GetChild(CFGMR3GetRoot(pVM), "PDM/DriverTransformations"));
    if (!pCurTrans)
        return VINF_SUCCESS;

    /*
     * Gather the attributes used in the matching process.
     */
    const char *pszDevice = pLun->pDevIns
                          ? pLun->pDevIns->Internal.s.pDevR3->pReg->szName
                          : pLun->pUsbIns->Internal.s.pUsbDev->pReg->szName;
    char        szLun[32];
    RTStrPrintf(szLun, sizeof(szLun), "%u", pLun->iLun);
    const char *pszAbove  = pDrvAbove ? pDrvAbove->Internal.s.pDrv->pReg->szName : "<top>";
    char       *pszThisDrv;
    int rc = CFGMR3QueryStringAlloc(*ppNode, "Driver", &pszThisDrv);
    AssertMsgRCReturn(rc,  ("Query for string value of \"Driver\" -> %Rrc\n", rc),
                      rc == VERR_CFGM_VALUE_NOT_FOUND ? VERR_PDM_CFG_MISSING_DRIVER_NAME : rc);

    uint64_t    uInjectTransformationAbove = 0;
    if (pDrvAbove)
    {
        rc = CFGMR3QueryIntegerDef(CFGMR3GetParent(*ppNode), "InjectTransformationPtr", &uInjectTransformationAbove, 0);
        AssertLogRelRCReturn(rc, rc);
    }


    /*
     * Enumerate possible driver chain transformations.
     */
    unsigned cTransformations = 0;
    for (; pCurTrans != NULL; pCurTrans = CFGMR3GetNextChild(pCurTrans))
    {
        char szCurTransNm[256];
        rc = CFGMR3GetName(pCurTrans, szCurTransNm, sizeof(szCurTransNm));
        AssertLogRelRCReturn(rc, rc);

        /** @cfgm{/PDM/DriverTransformations/&lt;name&gt;/Device,string,*}
         * One or more simple wildcard patters separated by '|' for matching
         * the devices this transformation rule applies to. */
        char *pszMultiPat;
        rc = CFGMR3QueryStringAllocDef(pCurTrans, "Device", &pszMultiPat, "*");
        AssertLogRelRCReturn(rc, rc);
        bool fMatch = RTStrSimplePatternMultiMatch(pszMultiPat, RTSTR_MAX, pszDevice, RTSTR_MAX, NULL);
        MMR3HeapFree(pszMultiPat);
        if (!fMatch)
            continue;

        /** @cfgm{/PDM/DriverTransformations/&lt;name&gt;/LUN,string,*}
         * One or more simple wildcard patters separated by '|' for matching
         * the LUNs this transformation rule applies to. */
        rc = CFGMR3QueryStringAllocDef(pCurTrans, "LUN", &pszMultiPat, "*");
        AssertLogRelRCReturn(rc, rc);
        fMatch = RTStrSimplePatternMultiMatch(pszMultiPat, RTSTR_MAX, szLun, RTSTR_MAX, NULL);
        MMR3HeapFree(pszMultiPat);
        if (!fMatch)
            continue;

        /** @cfgm{/PDM/DriverTransformations/&lt;name&gt;/BelowDriver,string,*}
         * One or more simple wildcard patters separated by '|' for matching the
         * drivers the transformation should be applied below.  This means, that
         * when the drivers matched here attached another driver below them, the
         * transformation will be applied.  To represent the device, '&lt;top&gt;'
         * is used. */
        rc = CFGMR3QueryStringAllocDef(pCurTrans, "BelowDriver", &pszMultiPat, "*");
        AssertLogRelRCReturn(rc, rc);
        fMatch = RTStrSimplePatternMultiMatch(pszMultiPat, RTSTR_MAX, pszAbove, RTSTR_MAX, NULL);
        MMR3HeapFree(pszMultiPat);
        if (!fMatch)
            continue;

        /** @cfgm{/PDM/DriverTransformations/&lt;name&gt;/AboveDriver,string,*}
         * One or more simple wildcard patters separated by '|' for matching the
         * drivers the transformation should be applie above or at (depending on
         * the action).  The value being matched against here is the driver that
         * is in the process of being attached, so for mergeconfig actions this is
         * usually what you need to match on. */
        rc = CFGMR3QueryStringAlloc(pCurTrans, "AboveDriver", &pszMultiPat);
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
            rc = VINF_SUCCESS;
        else
        {
            AssertLogRelRCReturn(rc, rc);
            fMatch = RTStrSimplePatternMultiMatch(pszMultiPat, RTSTR_MAX, pszThisDrv, RTSTR_MAX, NULL);
            MMR3HeapFree(pszMultiPat);
            if (!fMatch)
                continue;
            if (uInjectTransformationAbove == (uintptr_t)pCurTrans)
                continue;
        }

        /*
         * We've got a match! Now, what are we supposed to do?
         */
        /** @cfgm{/PDM/DriverTransformations/&lt;name&gt;/Action,string,inject}
         * The action that the transformation takes.  Possible values are:
         *      - inject
         *      - mergeconfig: This merges and the content of the 'Config' key under the
         *        transformation into the driver's own 'Config' key, replacing any
         *        duplicates.
         *      - remove
         *      - removetree
         *      - replace
         *      - replacetree
         */
        char szAction[16];
        rc = CFGMR3QueryStringDef(pCurTrans, "Action", szAction, sizeof(szAction), "inject");
        AssertLogRelRCReturn(rc, rc);
        AssertLogRelMsgReturn(   !strcmp(szAction, "inject")
                              || !strcmp(szAction, "mergeconfig")
                              || !strcmp(szAction, "remove")
                              || !strcmp(szAction, "removetree")
                              || !strcmp(szAction, "replace")
                              || !strcmp(szAction, "replacetree")
                              ,
                              ("Action='%s', valid values are 'inject', 'mergeconfig', 'replace', 'replacetree', 'remove', 'removetree'.\n", szAction),
                              VERR_PDM_MISCONFIGURED_DRV_TRANSFORMATION);
        LogRel(("PDMDriver: Applying '%s' to '%s'::[%s]...'%s': %s\n", szCurTransNm, pszDevice, szLun, pszThisDrv, szAction));
        CFGMR3Dump(*ppNode);
        CFGMR3Dump(pCurTrans);

        /* Get the attached driver to inject. */
        PCFGMNODE pTransAttDrv = NULL;
        if (!strcmp(szAction, "inject") || !strcmp(szAction, "replace") || !strcmp(szAction, "replacetree"))
        {
            pTransAttDrv = CFGMR3GetChild(pCurTrans, "AttachedDriver");
            AssertLogRelMsgReturn(pTransAttDrv,
                                  ("An %s transformation requires an AttachedDriver child node!\n", szAction),
                                  VERR_PDM_MISCONFIGURED_DRV_TRANSFORMATION);
        }


        /*
         * Remove the node.
         */
        if (!strcmp(szAction, "remove") || !strcmp(szAction, "removetree"))
        {
            PCFGMNODE pBelowThis = CFGMR3GetChild(*ppNode, "AttachedDriver");
            if (!pBelowThis || !strcmp(szAction, "removetree"))
            {
                CFGMR3RemoveNode(*ppNode);
                *ppNode = NULL;
            }
            else
            {
                PCFGMNODE pBelowThisCopy;
                rc = CFGMR3DuplicateSubTree(pBelowThis, &pBelowThisCopy);
                AssertLogRelRCReturn(rc, rc);

                rc = CFGMR3ReplaceSubTree(*ppNode, pBelowThisCopy);
                AssertLogRelRCReturnStmt(rc, CFGMR3RemoveNode(pBelowThis), rc);
            }
        }
        /*
         * Replace the driver about to be instantiated.
         */
        else if (!strcmp(szAction, "replace") || !strcmp(szAction, "replacetree"))
        {
            PCFGMNODE pTransCopy;
            rc = CFGMR3DuplicateSubTree(pTransAttDrv, &pTransCopy);
            AssertLogRelRCReturn(rc, rc);

            PCFGMNODE pBelowThis = CFGMR3GetChild(*ppNode, "AttachedDriver");
            if (!pBelowThis || !strcmp(szAction, "replacetree"))
                rc = VINF_SUCCESS;
            else
            {
                PCFGMNODE pBelowThisCopy;
                rc = CFGMR3DuplicateSubTree(pBelowThis, &pBelowThisCopy);
                if (RT_SUCCESS(rc))
                {
                    rc = CFGMR3InsertSubTree(pTransCopy, "AttachedDriver", pBelowThisCopy, NULL);
                    AssertLogRelRC(rc);
                    if (RT_FAILURE(rc))
                        CFGMR3RemoveNode(pBelowThisCopy);
                }
            }
            if (RT_SUCCESS(rc))
                rc = CFGMR3ReplaceSubTree(*ppNode, pTransCopy);
            if (RT_FAILURE(rc))
                CFGMR3RemoveNode(pTransCopy);
        }
        /*
         * Inject a driver before the driver about to be instantiated.
         */
        else if (!strcmp(szAction, "inject"))
        {
            PCFGMNODE pTransCopy;
            rc = CFGMR3DuplicateSubTree(pTransAttDrv, &pTransCopy);
            AssertLogRelRCReturn(rc, rc);

            PCFGMNODE pThisCopy;
            rc = CFGMR3DuplicateSubTree(*ppNode, &pThisCopy);
            if (RT_SUCCESS(rc))
            {
                rc = CFGMR3InsertSubTree(pTransCopy, "AttachedDriver", pThisCopy, NULL);
                if (RT_SUCCESS(rc))
                {
                    rc = CFGMR3InsertInteger(pTransCopy, "InjectTransformationPtr", (uintptr_t)pCurTrans);
                    AssertLogRelRC(rc);
                    rc = CFGMR3InsertString(pTransCopy, "InjectTransformationNm", szCurTransNm);
                    AssertLogRelRC(rc);
                    if (RT_SUCCESS(rc))
                        rc = CFGMR3ReplaceSubTree(*ppNode, pTransCopy);
                }
                else
                {
                    AssertLogRelRC(rc);
                    CFGMR3RemoveNode(pThisCopy);
                }
            }
            if (RT_FAILURE(rc))
                CFGMR3RemoveNode(pTransCopy);
        }
        /*
         * Merge the Config node of the transformation with the one of the
         * current driver.
         */
        else if (!strcmp(szAction, "mergeconfig"))
        {
            PCFGMNODE pTransConfig = CFGMR3GetChild(pCurTrans, "Config");
            AssertLogRelReturn(pTransConfig, VERR_PDM_MISCONFIGURED_DRV_TRANSFORMATION);

            PCFGMNODE pDrvConfig = CFGMR3GetChild(*ppNode, "Config");
            if (*ppNode)
                CFGMR3InsertNode(*ppNode, "Config", &pDrvConfig);
            AssertLogRelReturn(pDrvConfig, VERR_PDM_CANNOT_TRANSFORM_REMOVED_DRIVER);

            rc = CFGMR3CopyTree(pDrvConfig, pTransConfig, CFGM_COPY_FLAGS_REPLACE_VALUES | CFGM_COPY_FLAGS_MERGE_KEYS);
            AssertLogRelRCReturn(rc, rc);
        }
        else
            AssertFailed();

        cTransformations++;
        if (*ppNode)
            CFGMR3Dump(*ppNode);
        else
            LogRel(("PDMDriver: The transformation removed the driver.\n"));
    }

    /*
     * Note what happened in the release log.
     */
    if (cTransformations > 0)
        LogRel(("PDMDriver: Transformations done. Applied %u driver transformations.\n", cTransformations));

    return rc;
}


/**
 * Instantiate a driver.
 *
 * @note    The caller must enter CoreListCritSectRw with exclusive access
 *          rights before the call.  This function _will_ release the lock for
 *          doing callbacks (and failure cleanups), but it will still own it
 *          upon return.
 *
 * @returns VBox status code, including informational statuses.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pNode               The CFGM node for the driver.
 * @param   pBaseInterface      The base interface.
 * @param   pDrvAbove           The driver above it.  NULL if it's the top-most
 *                              driver.
 * @param   pLun                The LUN the driver is being attached to.  NULL
 *                              if we're instantiating a driver chain before
 *                              attaching it - untested.
 * @param   ppBaseInterface     Where to return the pointer to the base
 *                              interface of the newly created driver.
 *
 * @remarks Recursive calls to this function is normal as the drivers will
 *          attach to anything below them during the pfnContruct call.
 *
 * @todo    Need to extend this interface a bit so that the driver
 *          transformation feature can attach drivers to unconfigured LUNs and
 *          at the end of chains.
 */
int pdmR3DrvInstantiate(PVM pVM, PCFGMNODE pNode, PPDMIBASE pBaseInterface, PPDMDRVINS pDrvAbove,
                        PPDMLUN pLun, PPDMIBASE *ppBaseInterface)
{
    Assert(!pDrvAbove || !pDrvAbove->Internal.s.pDown);
    Assert(!pDrvAbove || !pDrvAbove->pDownBase);

    Assert(pBaseInterface->pfnQueryInterface(pBaseInterface, PDMIBASE_IID) == pBaseInterface);

    /* Exclusive lock ownership w/o any extra recursions. */
    Assert(RTCritSectRwIsWriteOwner(&pVM->pdm.s.CoreListCritSectRw));
    Assert(RTCritSectRwGetWriteRecursion(&pVM->pdm.s.CoreListCritSectRw) == 1);

    /*
     * Do driver chain injections
     */
    int rc = pdmR3DrvMaybeTransformChain(pVM, pDrvAbove, pLun, &pNode);
    if (RT_FAILURE(rc))
        return rc;
    if (!pNode)
        return VERR_PDM_NO_ATTACHED_DRIVER;

    /*
     * Find the driver.
     */
    char *pszName;
    rc = CFGMR3QueryStringAlloc(pNode, "Driver", &pszName);
    if (RT_SUCCESS(rc))
    {
        PPDMDRV pDrv = pdmR3DrvLookup(pVM, pszName);
        if (    pDrv
            &&  pDrv->cInstances < pDrv->pReg->cMaxInstances)
        {
            /* config node */
            PCFGMNODE pConfigNode = CFGMR3GetChild(pNode, "Config");
            if (!pConfigNode)
                rc = CFGMR3InsertNode(pNode, "Config", &pConfigNode);
            if (RT_SUCCESS(rc))
            {
                CFGMR3SetRestrictedRoot(pConfigNode);

                /*
                 * Allocate the driver instance.
                 */
                size_t cb = RT_UOFFSETOF_DYN(PDMDRVINS, achInstanceData[pDrv->pReg->cbInstance]);
                cb = RT_ALIGN_Z(cb, 16);
                PPDMDRVINS pNew;
#undef PDM_WITH_RING0_DRIVERS
#ifdef PDM_WITH_RING0_DRIVERS
                bool const fHyperHeap = !!(pDrv->pReg->fFlags & (PDM_DRVREG_FLAGS_R0 | PDM_DRVREG_FLAGS_RC));
                if (fHyperHeap)
                    rc = MMHyperAlloc(pVM, cb, 64, MM_TAG_PDM_DRIVER, (void **)&pNew);
                else
#endif
                    rc = MMR3HeapAllocZEx(pVM, MM_TAG_PDM_DRIVER, cb, (void **)&pNew);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Initialize the instance structure (declaration order).
                     */
                    pNew->u32Version                = PDM_DRVINS_VERSION;
                    pNew->iInstance                 = pDrv->iNextInstance;
                    pNew->Internal.s.pUp            = pDrvAbove ? pDrvAbove : NULL;
                    //pNew->Internal.s.pDown          = NULL;
                    pNew->Internal.s.pLun           = pLun;
                    pNew->Internal.s.pDrv           = pDrv;
                    pNew->Internal.s.pVMR3          = pVM;
#ifdef PDM_WITH_RING0_DRIVERS
                    pNew->Internal.s.pVMR0          = pDrv->pReg->fFlags & PDM_DRVREG_FLAGS_R0 ? pVM->pVMR0ForCall : NIL_RTR0PTR;
                    pNew->Internal.s.pVMRC          = pDrv->pReg->fFlags & PDM_DRVREG_FLAGS_RC ? pVM->pVMRC : NIL_RTRCPTR;
#endif
                    //pNew->Internal.s.fDetaching     = false;
                    pNew->Internal.s.fVMSuspended   = true; /** @todo should be 'false', if driver is attached at runtime. */
                    //pNew->Internal.s.fVMReset       = false;
#ifdef PDM_WITH_RING0_DRIVERS
                    pNew->Internal.s.fHyperHeap     = fHyperHeap;
#endif
                    //pNew->Internal.s.pfnAsyncNotify = NULL;
                    pNew->Internal.s.pCfgHandle     = pNode;
                    //pNew->Internal.s.pNext          = NULL;
                    pNew->pReg                      = pDrv->pReg;
                    pNew->pCfg                      = pConfigNode;
                    pNew->pUpBase                   = pBaseInterface;
                    Assert(!pDrvAbove || pBaseInterface == &pDrvAbove->IBase);
                    //pNew->pDownBase                 = NULL;
                    //pNew->IBase.pfnQueryInterface   = NULL;
                    //pNew->fTracing                  = 0;
                    pNew->idTracing                 = ++pVM->pdm.s.idTracingOther;
                    pNew->pHlpR3                    = &g_pdmR3DrvHlp;
                    pNew->pvInstanceDataR3          = &pNew->achInstanceData[0];
#ifdef PDM_WITH_RING0_DRIVERS
                    if (pDrv->pReg->fFlags & PDM_DRVREG_FLAGS_R0)
                    {
                        pNew->pvInstanceDataR0      = MMHyperR3ToR0(pVM, &pNew->achInstanceData[0]);
                        rc = PDMR3LdrGetSymbolR0(pVM, NULL, "g_pdmR0DrvHlp", &pNew->pHlpR0);
                        AssertReleaseRCReturn(rc, rc);
                    }
# ifdef VBOX_WITH_RAW_MODE_KEEP
                    if (   (pDrv->pReg->fFlags & PDM_DRVREG_FLAGS_RC)
                        && VM_IS_RAW_MODE_ENABLED(pVM))
                    {
                        pNew->pvInstanceDataR0      = MMHyperR3ToRC(pVM, &pNew->achInstanceData[0]);
                        rc = PDMR3LdrGetSymbolRC(pVM, NULL, "g_pdmRCDrvHlp", &pNew->pHlpRC);
                        AssertReleaseRCReturn(rc, rc);
                    }
# endif
#endif

                    /*
                     * Update PDMDRV members and link it into the instance list.
                     */
                    pDrv->iNextInstance++;
                    pDrv->cInstances++;

                    PPDMDRVINS pPrevDrvIns = pDrv->pInstances;
                    if (!pPrevDrvIns)
                        pDrv->pInstances = pNew;
                    else
                    {
                        while (pPrevDrvIns->Internal.s.pNext)
                            pPrevDrvIns = pPrevDrvIns->Internal.s.pNext;
                        pPrevDrvIns->Internal.s.pNext = pNew;
                    }

                    /*
                     * Link with it with the driver above / LUN.
                     */
                    if (pDrvAbove)
                    {
                        pDrvAbove->pDownBase        = &pNew->IBase;
                        pDrvAbove->Internal.s.pDown = pNew;
                    }
                    else if (pLun)
                        pLun->pTop                  = pNew;
                    if (pLun)
                        pLun->pBottom               = pNew;

                    /*
                     * Invoke the constructor.
                     *
                     * We have to leave the exclusive CoreListCritSectRw ownership here, as
                     * the constructor may call us back recursively to attach drivers down
                     * the stack, which would cause trouble in pdmR3DrvDestroyChain since
                     * it assumes there is no lock recursion.  There is also the possibility
                     * of the constructor engaging with PDM asynchronously via another thread.
                     */
                    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
                    rc = pDrv->pReg->pfnConstruct(pNew, pNew->pCfg, 0 /*fFlags*/);
                    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
                    if (RT_SUCCESS(rc))
                    {
                        AssertPtr(pNew->IBase.pfnQueryInterface);
                        Assert(pNew->IBase.pfnQueryInterface(&pNew->IBase, PDMIBASE_IID) == &pNew->IBase);

                        /* Success! */
                        *ppBaseInterface = &pNew->IBase;
                        if (pLun)
                            Log(("PDM: Attached driver %p:'%s'/%d to LUN#%d on device '%s'/%d, pDrvAbove=%p:'%s'/%d\n",
                                 pNew, pDrv->pReg->szName, pNew->iInstance,
                                 pLun->iLun,
                                 pLun->pDevIns ? pLun->pDevIns->pReg->szName : pLun->pUsbIns->pReg->szName,
                                 pLun->pDevIns ? pLun->pDevIns->iInstance    : pLun->pUsbIns->iInstance,
                                 pDrvAbove, pDrvAbove ? pDrvAbove->pReg->szName : "", pDrvAbove ? pDrvAbove->iInstance : UINT32_MAX));
                        else
                            Log(("PDM: Attached driver %p:'%s'/%d, pDrvAbove=%p:'%s'/%d\n",
                                 pNew, pDrv->pReg->szName, pNew->iInstance,
                                 pDrvAbove, pDrvAbove ? pDrvAbove->pReg->szName : "", pDrvAbove ? pDrvAbove->iInstance : UINT32_MAX));
                    }
                    else
                    {
                        pdmR3DrvDestroyChain(pVM, pNew, PDM_TACH_FLAGS_NO_CALLBACKS);
                        if (rc == VERR_VERSION_MISMATCH)
                            rc = VERR_PDM_DRIVER_VERSION_MISMATCH;
                    }
                }
                else
                    AssertMsgFailed(("Failed to allocate %d bytes for instantiating driver '%s'! rc=%Rrc\n", cb, pszName, rc));
            }
            else
                AssertMsgFailed(("Failed to create Config node! rc=%Rrc\n", rc));
        }
        else if (pDrv)
        {
            AssertMsgFailed(("Too many instances of driver '%s', max is %u\n", pszName, pDrv->pReg->cMaxInstances));
            rc = VERR_PDM_TOO_MANY_DRIVER_INSTANCES;
        }
        else
        {
            AssertMsgFailed(("Driver '%s' wasn't found!\n", pszName));
            rc = VERR_PDM_DRIVER_NOT_FOUND;
        }
        MMR3HeapFree(pszName);
    }
    else
    {
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
            rc = VERR_PDM_CFG_MISSING_DRIVER_NAME;
        else
            AssertMsgFailed(("Query for string value of \"Driver\" -> %Rrc\n", rc));
    }
    return rc;
}


/**
 * Detaches a driver from whatever it's attached to.
 * This will of course lead to the destruction of the driver and all drivers below it in the chain.
 *
 * @note    The caller must enter CoreListCritSectRw with exclusive access
 *          rights before the call.  This function _will_ release the lock for
 *          doing callbacks and cleaning up resources associated with the
 *          drivers being destroyed, but it will still own it upon return.
 *
 * @returns VINF_SUCCESS
 * @param   pVM         The cross context VM structure.
 * @param   pDrvIns     The driver instance to detach.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 */
int pdmR3DrvDetach(PVM pVM, PPDMDRVINS pDrvIns, uint32_t fFlags)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvDetach: pDrvIns=%p '%s'/%d\n", pDrvIns, pDrvIns->pReg->szName, pDrvIns->iInstance));
    VM_ASSERT_EMT(pVM);

    /*
     * Check that we're not doing this recursively, that could have unwanted sideeffects!
     */
    if (pDrvIns->Internal.s.fDetaching)
    {
        AssertMsgFailed(("Recursive detach! '%s'/%d\n", pDrvIns->pReg->szName, pDrvIns->iInstance));
        return VINF_SUCCESS;
    }

    /*
     * Check that we actually can detach this instance.
     * The requirement is that the driver/device above has a detach method.
     */
    if (  pDrvIns->Internal.s.pUp
        ? !pDrvIns->Internal.s.pUp->pReg->pfnDetach
        :   pDrvIns->Internal.s.pLun->pDevIns
          ? !pDrvIns->Internal.s.pLun->pDevIns->pReg->pfnDetach
          : !pDrvIns->Internal.s.pLun->pUsbIns->pReg->pfnDriverDetach
       )
    {
        AssertMsgFailed(("Cannot detach driver instance because the driver/device above doesn't support it!\n"));
        return VERR_PDM_DRIVER_DETACH_NOT_POSSIBLE;
    }

    /*
     * Join paths with pdmR3DrvDestroyChain.
     */
    pdmR3DrvDestroyChain(pVM, pDrvIns, fFlags);

    return VINF_SUCCESS;
}


/**
 * Destroys a driver chain starting with the specified driver.
 *
 * This is used when unplugging a device at run time.
 *
 * @note    The caller must enter CoreListCritSectRw with exclusive access
 *          rights before the call.  This function _will_ release the lock for
 *          doing callbacks and cleaning up resources associated with the
 *          drivers being destroyed, but it will still own it upon return.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pDrvIns     Pointer to the driver instance to start with.
 * @param   fFlags      PDM_TACH_FLAGS_NOT_HOT_PLUG, PDM_TACH_FLAGS_NO_CALLBACKS
 *                      or 0.
 */
void pdmR3DrvDestroyChain(PVM pVM, PPDMDRVINS pDrvIns, uint32_t fFlags)
{
    Assert(pDrvIns->Internal.s.pVMR3 == pVM);
    VM_ASSERT_EMT(pVM);

    /* Exclusive lock ownership w/o any extra recursions. */
    Assert(RTCritSectRwIsWriteOwner(&pVM->pdm.s.CoreListCritSectRw));
    Assert(RTCritSectRwGetWriteRecursion(&pVM->pdm.s.CoreListCritSectRw) == 1);

    /*
     * Detach the bottommost driver until we've detached pDrvIns.
     */
    Assert(!pDrvIns->Internal.s.fDetaching);
    pDrvIns->Internal.s.fDetaching = true;

    PPDMDRVINS pCur;
    do
    {
        /* find the driver to detach. */
        pCur = pDrvIns;
        while (pCur->Internal.s.pDown)
            pCur = pCur->Internal.s.pDown;
        LogFlow(("pdmR3DrvDestroyChain: pCur=%p '%s'/%d\n", pCur, pCur->pReg->szName, pCur->iInstance));

        /*
         * Unlink it and notify parent.
         */
        pCur->Internal.s.fDetaching = true;

        PPDMLUN pLun = pCur->Internal.s.pLun;
        Assert(pLun->pBottom == pCur);
        pLun->pBottom = pCur->Internal.s.pUp;

        if (pCur->Internal.s.pUp)
        {
            /* driver parent */
            PPDMDRVINS pParent = pCur->Internal.s.pUp;
            pCur->Internal.s.pUp = NULL;
            pParent->Internal.s.pDown = NULL;

            if (!(fFlags & PDM_TACH_FLAGS_NO_CALLBACKS) && pParent->pReg->pfnDetach)
            {
                RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
                pParent->pReg->pfnDetach(pParent, fFlags);
                RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
            }

            pParent->pDownBase = NULL;
        }
        else
        {
            /* device parent */
            Assert(pLun->pTop == pCur);
            pLun->pTop = NULL;
            if (!(fFlags & PDM_TACH_FLAGS_NO_CALLBACKS))
            {
                if (pLun->pDevIns)
                {
                    if (pLun->pDevIns->pReg->pfnDetach)
                    {
                        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
                        PDMCritSectEnter(pVM, pLun->pDevIns->pCritSectRoR3, VERR_IGNORED);
                        pLun->pDevIns->pReg->pfnDetach(pLun->pDevIns, pLun->iLun, fFlags);
                        PDMCritSectLeave(pVM, pLun->pDevIns->pCritSectRoR3);
                        RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
                    }
                }
                else
                {
                    if (pLun->pUsbIns->pReg->pfnDriverDetach)
                    {
                        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
                        /** @todo USB device locking? */
                        pLun->pUsbIns->pReg->pfnDriverDetach(pLun->pUsbIns, pLun->iLun, fFlags);
                        RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
                    }
                }
            }
        }

        /*
         * Unlink the instance and call the destructor.
         */
        PPDMDRVINS pPrevEntry = pCur->Internal.s.pDrv->pInstances;
        if (pCur == pPrevEntry)
            pCur->Internal.s.pDrv->pInstances = pCur->Internal.s.pNext;
        else if (pPrevEntry)
        {
            PPDMDRVINS pCurEntry = pPrevEntry->Internal.s.pNext;
            while (pCurEntry != pCur && pCurEntry)
            {
                pPrevEntry = pCurEntry;
                pCurEntry = pCurEntry->Internal.s.pNext;
            }
            AssertLogRelMsg(pCurEntry == pCur, ("%s", pCur->pReg->szName));
            if (pCurEntry == pCur)
                pPrevEntry->Internal.s.pNext = pCur->Internal.s.pNext;
        }
        else
            AssertLogRelMsg(pPrevEntry != NULL, ("%s", pCur->pReg->szName));

        pCur->pUpBase = NULL;
        pCur->Internal.s.pDrv->cInstances--;

        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

        if (pCur->pReg->pfnDestruct)
            pCur->pReg->pfnDestruct(pCur);

        /*
         * Free all resources allocated by the driver.
         */
        /* Queues. */
        int rc = PDMR3QueueDestroyDriver(pVM, pCur);
        AssertRC(rc);

        /* Timers. */
        rc = TMR3TimerDestroyDriver(pVM, pCur);
        AssertRC(rc);

        /* SSM data units. */
        rc = SSMR3DeregisterDriver(pVM, pCur, NULL, 0);
        AssertRC(rc);

        /* PDM threads. */
        rc = pdmR3ThreadDestroyDriver(pVM, pCur);
        AssertRC(rc);

        /* Info handlers. */
        rc = DBGFR3InfoDeregisterDriver(pVM, pCur, NULL);
        AssertRC(rc);

        /* PDM critsects. */
        rc = pdmR3CritSectBothDeleteDriver(pVM, pCur);
        AssertRC(rc);

        /* Block caches. */
        PDMR3BlkCacheReleaseDriver(pVM, pCur);

#ifdef VBOX_WITH_PDM_ASYNC_COMPLETION
        /* Completion templates.*/
        pdmR3AsyncCompletionTemplateDestroyDriver(pVM, pCur);
#endif

        /* Finally, the driver it self. */
#ifdef PDM_WITH_RING0_DRIVERS
        bool const fHyperHeap = pCur->Internal.s.fHyperHeap;
#endif
        ASMMemFill32(pCur, RT_UOFFSETOF_DYN(PDMDRVINS, achInstanceData[pCur->pReg->cbInstance]), 0xdeadd0d0);
#ifdef PDM_WITH_RING0_DRIVERS
        if (fHyperHeap)
            MMHyperFree(pVM, pCur);
        else
#endif
            MMR3HeapFree(pCur);

        RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
    } while (pCur != pDrvIns);
}


/**
 * Enumerates driver instances via a callback.
 *
 * @returns VBox status code.
 * @retval  VERR_PDM_DRIVER_NOT_FOUND if @a pszDriver isn't registered.
 * @param   pUVM            The user mode VM handle.
 * @param   pszDriver       The name of the driver which instances should be
 *                          enumerated.
 * @param   pfnCallback     The callback function.
 * @param   pvUser          User argument.
 */
VMMR3DECL(int) PDMR3DriverEnumInstances(PUVM pUVM, const char *pszDriver, PFNPDMENUMDRVINS pfnCallback, void *pvUser)
{
    LogFlow(("PDMR3DriverEnumInstances: pszDriver=%p:{%s} pfnCallback=%p pvUser=%p\n", pszDriver, pszDriver, pfnCallback, pvUser));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM const pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * First find the PDMDRV entry for the driver.
     */
    int           rc   = VINF_SUCCESS;
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    PPDMDRV const pDrv = pdmR3DrvLookup(pVM, pszDriver);
    if (pDrv)
    {
        /*
         * Walk the list of instances.
         */
        for (PPDMDRVINSR3 pDrvIns = pDrv->pInstances; pDrvIns; pDrvIns = pDrvIns->Internal.s.pNext)
        {
            if (!pDrvIns->Internal.s.fDetaching) /* paranoia */
            {
                PPDMLUN const       pLun         = pDrvIns->Internal.s.pLun;
                PPDMDEVINS const    pDevIns      = pLun->pDevIns;
                const char * const  pszDevName   = pDevIns ? pDevIns->pReg->szName : pLun->pUsbIns->pReg->szName;
                uint32_t const      uDevInstance = pDevIns ? pDevIns->iInstance    : pLun->pUsbIns->iInstance;
                int rc2 = pfnCallback(&pDrvIns->IBase, pDrvIns->iInstance, pDevIns == NULL,
                                      pszDevName, uDevInstance, pLun->iLun, pvUser);
                if (RT_FAILURE(rc2))
                {
                    rc = rc2;
                    break;
                }
            }
        }
    }
    else
        AssertFailedStmt(rc = VERR_PDM_DRIVER_NOT_FOUND);
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
    return rc;
}



/** @name Driver Helpers
 * @{
 */

/** @interface_method_impl{PDMDRVHLPR3,pfnAttach} */
static DECLCALLBACK(int) pdmR3DrvHlp_Attach(PPDMDRVINS pDrvIns, uint32_t fFlags, PPDMIBASE *ppBaseInterface)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    PVM pVM = pDrvIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DrvHlp_Attach: caller='%s'/%d: fFlags=%#x\n", pDrvIns->pReg->szName, pDrvIns->iInstance, fFlags));
    Assert(!(fFlags & ~(PDM_TACH_FLAGS_NOT_HOT_PLUG)));
    RT_NOREF_PV(fFlags);

    /*
     * Check that there isn't anything attached already.
     */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
    int rc;
    if (!pDrvIns->Internal.s.pDown)
    {
        Assert(pDrvIns->Internal.s.pLun->pBottom == pDrvIns);

        /*
         * Get the attached driver configuration.
         */
        PCFGMNODE pNode = CFGMR3GetChild(pDrvIns->Internal.s.pCfgHandle, "AttachedDriver");
        if (pNode)
            rc = pdmR3DrvInstantiate(pVM, pNode, &pDrvIns->IBase, pDrvIns, pDrvIns->Internal.s.pLun, ppBaseInterface);
        else
            rc = VERR_PDM_NO_ATTACHED_DRIVER;
    }
    else
    {
        AssertMsgFailed(("Already got a driver attached. The driver should keep track of such things!\n"));
        rc = VERR_PDM_DRIVER_ALREADY_ATTACHED;
    }
    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    LogFlow(("pdmR3DrvHlp_Attach: caller='%s'/%d: return %Rrc\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnDetach} */
static DECLCALLBACK(int) pdmR3DrvHlp_Detach(PPDMDRVINS pDrvIns, uint32_t fFlags)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_Detach: caller='%s'/%d: fFlags=%#x\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, fFlags));
    PVM const pVM =  pDrvIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    /*
     * Anything attached?
     */
    int rc;
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
    if (pDrvIns->Internal.s.pDown)
        rc = pdmR3DrvDetach(pVM, pDrvIns->Internal.s.pDown, fFlags);
    else
    {
        AssertMsgFailed(("Nothing attached!\n"));
        rc = VERR_PDM_NO_DRIVER_ATTACHED;
    }
    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    LogFlow(("pdmR3DrvHlp_Detach: caller='%s'/%d: returns %Rrc\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnDetachSelf} */
static DECLCALLBACK(int) pdmR3DrvHlp_DetachSelf(PPDMDRVINS pDrvIns, uint32_t fFlags)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_DetachSelf: caller='%s'/%d: fFlags=%#x\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, fFlags));
    PVM const pVM = pDrvIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
    int rc = pdmR3DrvDetach(pVM, pDrvIns, fFlags);
    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    LogFlow(("pdmR3DrvHlp_Detach: returns %Rrc\n", rc)); /* pDrvIns is freed by now. */
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnMountPrepare} */
static DECLCALLBACK(int) pdmR3DrvHlp_MountPrepare(PPDMDRVINS pDrvIns, const char *pszFilename, const char *pszCoreDriver)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_MountPrepare: caller='%s'/%d: pszFilename=%p:{%s} pszCoreDriver=%p:{%s}\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pszFilename, pszFilename, pszCoreDriver, pszCoreDriver));
    PVM const pVM = pDrvIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    /* We're using CoreListCritSectRw for some setup & CFGM serialization here. */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    /*
     * Do the caller have anything attached below itself?
     */
    if (pDrvIns->Internal.s.pDown)
    {
        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
        AssertMsgFailed(("Cannot prepare a mount when something's attached to you!\n"));
        return VERR_PDM_DRIVER_ALREADY_ATTACHED;
    }

    /*
     * We're asked to prepare, so we'll start off by nuking the
     * attached configuration tree.
     */
    PCFGMNODE pNode = CFGMR3GetChild(pDrvIns->Internal.s.pCfgHandle, "AttachedDriver");
    if (pNode)
        CFGMR3RemoveNode(pNode);

    /*
     * If there is no core driver, we'll have to probe for it.
     */
    if (!pszCoreDriver)
    {
        /** @todo implement image probing. */
        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
        AssertReleaseMsgFailed(("Not implemented!\n"));
        return VERR_NOT_IMPLEMENTED;
    }

    /*
     * Construct the basic attached driver configuration.
     */
    int rc = CFGMR3InsertNode(pDrvIns->Internal.s.pCfgHandle, "AttachedDriver", &pNode);
    if (RT_SUCCESS(rc))
    {
        rc = CFGMR3InsertString(pNode, "Driver", pszCoreDriver);
        if (RT_SUCCESS(rc))
        {
            PCFGMNODE pCfg;
            rc = CFGMR3InsertNode(pNode, "Config", &pCfg);
            if (RT_SUCCESS(rc))
            {
                rc = CFGMR3InsertString(pCfg, "Path", pszFilename);
                if (RT_SUCCESS(rc))
                {
                    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
                    LogFlow(("pdmR3DrvHlp_MountPrepare: caller='%s'/%d: returns %Rrc (Driver=%s)\n",
                             pDrvIns->pReg->szName, pDrvIns->iInstance, rc, pszCoreDriver));
                    return rc;
                }
                AssertMsgFailed(("Path string insert failed, rc=%Rrc\n", rc));
            }
            else
                AssertMsgFailed(("Config node failed, rc=%Rrc\n", rc));
        }
        else
            AssertMsgFailed(("Driver string insert failed, rc=%Rrc\n", rc));
        CFGMR3RemoveNode(pNode);
    }
    else
        AssertMsgFailed(("AttachedDriver node insert failed, rc=%Rrc\n", rc));

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
    LogFlow(("pdmR3DrvHlp_MountPrepare: caller='%s'/%d: returns %Rrc\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnAssertEMT} */
static DECLCALLBACK(bool) pdmR3DrvHlp_AssertEMT(PPDMDRVINS pDrvIns, const char *pszFile, unsigned iLine, const char *pszFunction)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    if (VM_IS_EMT(pDrvIns->Internal.s.pVMR3))
        return true;

    char szMsg[100];
    RTStrPrintf(szMsg, sizeof(szMsg), "AssertEMT '%s'/%d\n", pDrvIns->pReg->szName, pDrvIns->iInstance);
    RTAssertMsg1Weak(szMsg, iLine, pszFile, pszFunction);
    AssertBreakpoint();
    VM_ASSERT_EMT(pDrvIns->Internal.s.pVMR3);
    return false;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnAssertOther} */
static DECLCALLBACK(bool) pdmR3DrvHlp_AssertOther(PPDMDRVINS pDrvIns, const char *pszFile, unsigned iLine, const char *pszFunction)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    if (!VM_IS_EMT(pDrvIns->Internal.s.pVMR3))
        return true;

    char szMsg[100];
    RTStrPrintf(szMsg, sizeof(szMsg), "AssertOther '%s'/%d\n", pDrvIns->pReg->szName, pDrvIns->iInstance);
    RTAssertMsg1Weak(szMsg, iLine, pszFile, pszFunction);
    AssertBreakpoint();
    VM_ASSERT_EMT(pDrvIns->Internal.s.pVMR3);
    return false;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnVMSetErrorV} */
static DECLCALLBACK(int) pdmR3DrvHlp_VMSetErrorV(PPDMDRVINS pDrvIns, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    int const rc2 = VMSetErrorV(pDrvIns->Internal.s.pVMR3, rc, RT_SRC_POS_ARGS, pszFormat, va);
    Assert(rc2 == rc); RT_NOREF_PV(rc2);
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnVMSetRuntimeErrorV} */
static DECLCALLBACK(int) pdmR3DrvHlp_VMSetRuntimeErrorV(PPDMDRVINS pDrvIns, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, va_list va)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    int rc = VMSetRuntimeErrorV(pDrvIns->Internal.s.pVMR3, fFlags, pszErrorId, pszFormat, va);
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnVMState} */
static DECLCALLBACK(VMSTATE) pdmR3DrvHlp_VMState(PPDMDRVINS pDrvIns)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);

    VMSTATE enmVMState = VMR3GetState(pDrvIns->Internal.s.pVMR3);

    LogFlow(("pdmR3DrvHlp_VMState: caller='%s'/%d: returns %d (%s)\n", pDrvIns->pReg->szName, pDrvIns->iInstance,
             enmVMState, VMR3GetStateName(enmVMState)));
    return enmVMState;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnVMTeleportedAndNotFullyResumedYet} */
static DECLCALLBACK(bool) pdmR3DrvHlp_VMTeleportedAndNotFullyResumedYet(PPDMDRVINS pDrvIns)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);

    bool fRc = VMR3TeleportedAndNotFullyResumedYet(pDrvIns->Internal.s.pVMR3);

    LogFlow(("pdmR3DrvHlp_VMState: caller='%s'/%d: returns %RTbool)\n", pDrvIns->pReg->szName, pDrvIns->iInstance,
             fRc));
    return fRc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnGetSupDrvSession} */
static DECLCALLBACK(PSUPDRVSESSION) pdmR3DrvHlp_GetSupDrvSession(PPDMDRVINS pDrvIns)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);

    PSUPDRVSESSION pSession = pDrvIns->Internal.s.pVMR3->pSession;
    LogFlow(("pdmR3DrvHlp_GetSupDrvSession: caller='%s'/%d: returns %p)\n", pDrvIns->pReg->szName, pDrvIns->iInstance,
             pSession));
    return pSession;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnQueueCreate} */
static DECLCALLBACK(int) pdmR3DrvHlp_QueueCreate(PPDMDRVINS pDrvIns, uint32_t cbItem, uint32_t cItems, uint32_t cMilliesInterval,
                                                 PFNPDMQUEUEDRV pfnCallback, const char *pszName, PDMQUEUEHANDLE *phQueue)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_PDMQueueCreate: caller='%s'/%d: cbItem=%d cItems=%d cMilliesInterval=%d pfnCallback=%p pszName=%p:{%s} phQueue=%p\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, cbItem, cItems, cMilliesInterval, pfnCallback, pszName, pszName, phQueue));
    PVM pVM = pDrvIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    if (pDrvIns->iInstance > 0)
    {
        pszName = MMR3HeapAPrintf(pVM, MM_TAG_PDM_DRIVER_DESC, "%s_%u", pszName, pDrvIns->iInstance);
        AssertLogRelReturn(pszName, VERR_NO_MEMORY);
    }

    int rc = PDMR3QueueCreateDriver(pVM, pDrvIns, cbItem, cItems, cMilliesInterval, pfnCallback, pszName, phQueue);

    LogFlow(("pdmR3DrvHlp_PDMQueueCreate: caller='%s'/%d: returns %Rrc *phQueue=%p\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc, *phQueue));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnQueueAlloc} */
static DECLCALLBACK(PPDMQUEUEITEMCORE) pdmR3DrvHlp_QueueAlloc(PPDMDRVINS pDrvIns, PDMQUEUEHANDLE hQueue)
{
    return PDMQueueAlloc(pDrvIns->Internal.s.pVMR3, hQueue, pDrvIns);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnQueueInsert} */
static DECLCALLBACK(int) pdmR3DrvHlp_QueueInsert(PPDMDRVINS pDrvIns, PDMQUEUEHANDLE hQueue, PPDMQUEUEITEMCORE pItem)
{
    return PDMQueueInsert(pDrvIns->Internal.s.pVMR3, hQueue, pDrvIns, pItem);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnQueueFlushIfNecessary} */
static DECLCALLBACK(bool) pdmR3DrvHlp_QueueFlushIfNecessary(PPDMDRVINS pDrvIns, PDMQUEUEHANDLE hQueue)
{
    return PDMQueueFlushIfNecessary(pDrvIns->Internal.s.pVMR3, hQueue, pDrvIns) == VINF_SUCCESS;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnTMGetVirtualFreq} */
static DECLCALLBACK(uint64_t) pdmR3DrvHlp_TMGetVirtualFreq(PPDMDRVINS pDrvIns)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);

    return TMVirtualGetFreq(pDrvIns->Internal.s.pVMR3);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnTMGetVirtualTime} */
static DECLCALLBACK(uint64_t) pdmR3DrvHlp_TMGetVirtualTime(PPDMDRVINS pDrvIns)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);

    return TMVirtualGet(pDrvIns->Internal.s.pVMR3);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnTimerCreate} */
static DECLCALLBACK(int) pdmR3DrvHlp_TimerCreate(PPDMDRVINS pDrvIns, TMCLOCK enmClock, PFNTMTIMERDRV pfnCallback, void *pvUser,
                                                 uint32_t fFlags, const char *pszDesc, PTMTIMERHANDLE phTimer)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_TimerCreate: caller='%s'/%d: enmClock=%d pfnCallback=%p pvUser=%p fFlags=%#x pszDesc=%p:{%s} phTimer=%p\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, enmClock, pfnCallback, pvUser, fFlags, pszDesc, pszDesc, phTimer));

    /* Mangle the timer name if there are more than once instance of this driver. */
    char szName[32];
    AssertReturn(strlen(pszDesc) < sizeof(szName) - 3, VERR_INVALID_NAME);
    if (pDrvIns->iInstance > 0)
    {
        RTStrPrintf(szName, sizeof(szName), "%s[%u]", pszDesc, pDrvIns->iInstance);
        pszDesc = szName;
    }

    /* Clear the ring-0 flag if the driver isn't configured for ring-0. */
    if (fFlags & TMTIMER_FLAGS_RING0)
    {
        AssertReturn(!(fFlags & TMTIMER_FLAGS_NO_RING0), VERR_INVALID_FLAGS);
        Assert(pDrvIns->Internal.s.pDrv->pReg->fFlags & PDM_DRVREG_FLAGS_R0);
#ifdef PDM_WITH_RING0_DRIVERS
        if (!(pDrvIns->Internal.s.fIntFlags & PDMDRVINSINT_FLAGS_R0_ENABLED)) /** @todo PDMDRVINSINT_FLAGS_R0_ENABLED? */
#endif
            fFlags = (fFlags & ~TMTIMER_FLAGS_RING0) | TMTIMER_FLAGS_NO_RING0;
    }
    else
        fFlags |= TMTIMER_FLAGS_NO_RING0;

    int rc = TMR3TimerCreateDriver(pDrvIns->Internal.s.pVMR3, pDrvIns, enmClock, pfnCallback, pvUser, fFlags, pszDesc, phTimer);

    LogFlow(("pdmR3DrvHlp_TMTimerCreate: caller='%s'/%d: returns %Rrc *phTimer=%p\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc, *phTimer));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnTimerDestroy} */
static DECLCALLBACK(int) pdmR3DrvHlp_TimerDestroy(PPDMDRVINS pDrvIns, TMTIMERHANDLE hTimer)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_TimerDestroy: caller='%s'/%d: hTimer=%RX64\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, hTimer));

    int rc = TMR3TimerDestroy(pDrvIns->Internal.s.pVMR3, hTimer);

    LogFlow(("pdmR3DrvHlp_TimerDestroy: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnTimerSetMillies} */
static DECLCALLBACK(int) pdmR3DrvHlp_TimerSetMillies(PPDMDRVINS pDrvIns, TMTIMERHANDLE hTimer, uint64_t cMilliesToNext)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return TMTimerSetMillies(pDrvIns->Internal.s.pVMR3, hTimer, cMilliesToNext);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnSSMRegister} */
static DECLCALLBACK(int) pdmR3DrvHlp_SSMRegister(PPDMDRVINS pDrvIns, uint32_t uVersion, size_t cbGuess,
                                                 PFNSSMDRVLIVEPREP pfnLivePrep, PFNSSMDRVLIVEEXEC pfnLiveExec, PFNSSMDRVLIVEVOTE pfnLiveVote,
                                                 PFNSSMDRVSAVEPREP pfnSavePrep, PFNSSMDRVSAVEEXEC pfnSaveExec, PFNSSMDRVSAVEDONE pfnSaveDone,
                                                 PFNSSMDRVLOADPREP pfnLoadPrep, PFNSSMDRVLOADEXEC pfnLoadExec, PFNSSMDRVLOADDONE pfnLoadDone)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    VM_ASSERT_EMT(pDrvIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DrvHlp_SSMRegister: caller='%s'/%d: uVersion=%#x cbGuess=%#x\n"
             "    pfnLivePrep=%p pfnLiveExec=%p pfnLiveVote=%p  pfnSavePrep=%p pfnSaveExec=%p pfnSaveDone=%p pszLoadPrep=%p pfnLoadExec=%p pfnLoaddone=%p\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, uVersion, cbGuess,
             pfnLivePrep, pfnLiveExec, pfnLiveVote,
             pfnSavePrep, pfnSaveExec, pfnSaveDone, pfnLoadPrep, pfnLoadExec, pfnLoadDone));

    int rc = SSMR3RegisterDriver(pDrvIns->Internal.s.pVMR3, pDrvIns, pDrvIns->pReg->szName, pDrvIns->iInstance,
                                 uVersion, cbGuess,
                                 pfnLivePrep, pfnLiveExec, pfnLiveVote,
                                 pfnSavePrep, pfnSaveExec, pfnSaveDone,
                                 pfnLoadPrep, pfnLoadExec, pfnLoadDone);

    LogFlow(("pdmR3DrvHlp_SSMRegister: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnSSMDeregister} */
static DECLCALLBACK(int) pdmR3DrvHlp_SSMDeregister(PPDMDRVINS pDrvIns, const char *pszName, uint32_t uInstance)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    VM_ASSERT_EMT(pDrvIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DrvHlp_SSMDeregister: caller='%s'/%d: pszName=%p:{%s} uInstance=%#x\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pszName, pszName, uInstance));

    int rc = SSMR3DeregisterDriver(pDrvIns->Internal.s.pVMR3, pDrvIns, pszName, uInstance);

    LogFlow(("pdmR3DrvHlp_SSMDeregister: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnMMHeapFree} */
static DECLCALLBACK(void) pdmR3DrvHlp_MMHeapFree(PPDMDRVINS pDrvIns, void *pv)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns); RT_NOREF(pDrvIns);
    LogFlow(("pdmR3DrvHlp_MMHeapFree: caller='%s'/%d: pv=%p\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pv));

    MMR3HeapFree(pv);

    LogFlow(("pdmR3DrvHlp_MMHeapFree: caller='%s'/%d: returns\n", pDrvIns->pReg->szName, pDrvIns->iInstance));
}


/** @interface_method_impl{PDMDRVHLPR3,pfnDBGFInfoRegister} */
static DECLCALLBACK(int) pdmR3DrvHlp_DBGFInfoRegister(PPDMDRVINS pDrvIns, const char *pszName, const char *pszDesc,
                                                      PFNDBGFHANDLERDRV pfnHandler)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_DBGFInfoRegister: caller='%s'/%d: pszName=%p:{%s} pszDesc=%p:{%s} pfnHandler=%p\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pszName, pszName, pszDesc, pszDesc, pfnHandler));

    int rc = DBGFR3InfoRegisterDriver(pDrvIns->Internal.s.pVMR3, pszName, pszDesc, pfnHandler, pDrvIns);

    LogFlow(("pdmR3DrvHlp_DBGFInfoRegister: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnDBGFInfoRegisterArgv} */
static DECLCALLBACK(int) pdmR3DrvHlp_DBGFInfoRegisterArgv(PPDMDRVINS pDrvIns, const char *pszName, const char *pszDesc,
                                                          PFNDBGFINFOARGVDRV pfnHandler)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_DBGFInfoRegisterArgv: caller='%s'/%d: pszName=%p:{%s} pszDesc=%p:{%s} pfnHandler=%p\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pszName, pszName, pszDesc, pszDesc, pfnHandler));

    int rc = DBGFR3InfoRegisterDriverArgv(pDrvIns->Internal.s.pVMR3, pszName, pszDesc, pfnHandler, pDrvIns);

    LogFlow(("pdmR3DrvHlp_DBGFInfoRegisterArgv: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnDBGFInfoDeregister} */
static DECLCALLBACK(int) pdmR3DrvHlp_DBGFInfoDeregister(PPDMDRVINS pDrvIns, const char *pszName)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_DBGFInfoDeregister: caller='%s'/%d: pszName=%p:{%s}\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pszName, pszName));

    int rc = DBGFR3InfoDeregisterDriver(pDrvIns->Internal.s.pVMR3, pDrvIns, pszName);

    LogFlow(("pdmR3DrvHlp_DBGFInfoDeregister: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc));

    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnSTAMRegister} */
static DECLCALLBACK(void) pdmR3DrvHlp_STAMRegister(PPDMDRVINS pDrvIns, void *pvSample, STAMTYPE enmType, const char *pszName,
                                                   STAMUNIT enmUnit, const char *pszDesc)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    PVM pVM = pDrvIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

#ifdef VBOX_WITH_STATISTICS /** @todo rework this to always be compiled in */
    if (*pszName == '/')
        STAM_REG(pDrvIns->Internal.s.pVMR3, pvSample, enmType, pszName, enmUnit, pszDesc);
    else
        STAMR3RegisterF(pVM, pvSample, enmType, STAMVISIBILITY_ALWAYS, enmUnit, pszDesc,
                        "/Drivers/%s-%u/%s", pDrvIns->pReg->szName, pDrvIns->iInstance, pszName);
#else
    RT_NOREF(pDrvIns, pvSample, enmType, pszName, enmUnit, pszDesc, pVM);
#endif
}


/** @interface_method_impl{PDMDRVHLPR3,pfnSTAMRegisterV} */
static DECLCALLBACK(void) pdmR3DrvHlp_STAMRegisterV(PPDMDRVINS pDrvIns, void *pvSample, STAMTYPE enmType,
                                                    STAMVISIBILITY enmVisibility, STAMUNIT enmUnit, const char *pszDesc,
                                                    const char *pszName, va_list args)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    PVM pVM = pDrvIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);

    int rc;
    if (*pszName == '/')
        rc = STAMR3RegisterV(pVM, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, args);
    else
    {
        /* We need to format it to check whether it starts with a
           slash or not (will rework this later). */
        char szFormatted[2048];
        ssize_t cchBase = RTStrPrintf2(szFormatted, sizeof(szFormatted) - 1024, "/Drivers/%s-%u/",
                                       pDrvIns->pReg->szName, pDrvIns->iInstance);
        AssertReturnVoid(cchBase > 0);

        ssize_t cch2 = RTStrPrintf2V(&szFormatted[cchBase], sizeof(szFormatted) - cchBase, pszName, args);
        AssertReturnVoid(cch2 > 0);

        rc = STAMR3Register(pVM, pvSample, enmType, enmVisibility,
                            &szFormatted[szFormatted[cchBase] == '/' ? cchBase : 0], enmUnit, pszDesc);
    }
    AssertRC(rc);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnSTAMRegisterF} */
static DECLCALLBACK(void) pdmR3DrvHlp_STAMRegisterF(PPDMDRVINS pDrvIns, void *pvSample, STAMTYPE enmType,
                                                    STAMVISIBILITY enmVisibility, STAMUNIT enmUnit, const char *pszDesc,
                                                    const char *pszName, ...)
{
    va_list va;
    va_start(va, pszName);
    pdmR3DrvHlp_STAMRegisterV(pDrvIns, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, va);
    va_end(va);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnSTAMDeregister} */
static DECLCALLBACK(int) pdmR3DrvHlp_STAMDeregister(PPDMDRVINS pDrvIns, void *pvSample)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    VM_ASSERT_EMT(pDrvIns->Internal.s.pVMR3);

    return STAMR3DeregisterByAddr(pDrvIns->Internal.s.pVMR3->pUVM, pvSample);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnSTAMDeregisterByPrefix} */
static DECLCALLBACK(int) pdmR3DrvHlp_STAMDeregisterByPrefix(PPDMDRVINS pDrvIns, const char *pszPrefix)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);

    if (*pszPrefix == '/')
        return STAMR3DeregisterByPrefix(pDrvIns->Internal.s.pVMR3->pUVM, pszPrefix);

    char szTmp[2048];
    ssize_t cch = RTStrPrintf2(szTmp, sizeof(szTmp), "/Drivers/%s-%u/%s", pDrvIns->pReg->szName, pDrvIns->iInstance, pszPrefix);
    AssertReturn(cch > 0, VERR_BUFFER_OVERFLOW);
    return STAMR3DeregisterByPrefix(pDrvIns->Internal.s.pVMR3->pUVM, szTmp);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnSUPCallVMMR0Ex} */
static DECLCALLBACK(int) pdmR3DrvHlp_SUPCallVMMR0Ex(PPDMDRVINS pDrvIns, unsigned uOperation, void *pvArg, unsigned cbArg)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_SSMCallVMMR0Ex: caller='%s'/%d: uOperation=%u pvArg=%p cbArg=%d\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, uOperation, pvArg, cbArg));
    RT_NOREF_PV(cbArg);

    int rc;
    if (    uOperation >= VMMR0_DO_SRV_START
        &&  uOperation <  VMMR0_DO_SRV_END)
        rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pDrvIns->Internal.s.pVMR3), NIL_VMCPUID,
                              uOperation, 0, (PSUPVMMR0REQHDR)pvArg);
    else
    {
        AssertMsgFailed(("Invalid uOperation=%u\n", uOperation));
        rc = VERR_INVALID_PARAMETER;
    }

    LogFlow(("pdmR3DrvHlp_SUPCallVMMR0Ex: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnUSBRegisterHub} */
static DECLCALLBACK(int) pdmR3DrvHlp_USBRegisterHub(PPDMDRVINS pDrvIns, uint32_t fVersions, uint32_t cPorts,
                                                    PCPDMUSBHUBREG pUsbHubReg, PPCPDMUSBHUBHLP ppUsbHubHlp)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    VM_ASSERT_EMT(pDrvIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DrvHlp_USBRegisterHub: caller='%s'/%d: fVersions=%#x cPorts=%#x pUsbHubReg=%p ppUsbHubHlp=%p\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, fVersions, cPorts, pUsbHubReg, ppUsbHubHlp));

#ifdef VBOX_WITH_USB
    int rc = pdmR3UsbRegisterHub(pDrvIns->Internal.s.pVMR3, pDrvIns, fVersions, cPorts, pUsbHubReg, ppUsbHubHlp);
#else
    int rc = VERR_NOT_SUPPORTED;
#endif

    LogFlow(("pdmR3DrvHlp_USBRegisterHub: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnSetAsyncNotification} */
static DECLCALLBACK(int) pdmR3DrvHlp_SetAsyncNotification(PPDMDRVINS pDrvIns, PFNPDMDRVASYNCNOTIFY pfnAsyncNotify)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    VM_ASSERT_EMT0(pDrvIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DrvHlp_SetAsyncNotification: caller='%s'/%d: pfnAsyncNotify=%p\n", pDrvIns->pReg->szName, pDrvIns->iInstance, pfnAsyncNotify));

    int rc = VINF_SUCCESS;
    AssertStmt(pfnAsyncNotify, rc = VERR_INVALID_PARAMETER);
    AssertStmt(!pDrvIns->Internal.s.pfnAsyncNotify, rc = VERR_WRONG_ORDER);
    AssertStmt(pDrvIns->Internal.s.fVMSuspended || pDrvIns->Internal.s.fVMReset, rc = VERR_WRONG_ORDER);
    VMSTATE enmVMState = VMR3GetState(pDrvIns->Internal.s.pVMR3);
    AssertStmt(   enmVMState == VMSTATE_SUSPENDING
               || enmVMState == VMSTATE_SUSPENDING_EXT_LS
               || enmVMState == VMSTATE_SUSPENDING_LS
               || enmVMState == VMSTATE_RESETTING
               || enmVMState == VMSTATE_RESETTING_LS
               || enmVMState == VMSTATE_POWERING_OFF
               || enmVMState == VMSTATE_POWERING_OFF_LS,
               rc = VERR_INVALID_STATE);

    if (RT_SUCCESS(rc))
        pDrvIns->Internal.s.pfnAsyncNotify = pfnAsyncNotify;

    LogFlow(("pdmR3DrvHlp_SetAsyncNotification: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnAsyncNotificationCompleted} */
static DECLCALLBACK(void) pdmR3DrvHlp_AsyncNotificationCompleted(PPDMDRVINS pDrvIns)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    PVM pVM = pDrvIns->Internal.s.pVMR3;

    VMSTATE enmVMState = VMR3GetState(pVM);
    if (   enmVMState == VMSTATE_SUSPENDING
        || enmVMState == VMSTATE_SUSPENDING_EXT_LS
        || enmVMState == VMSTATE_SUSPENDING_LS
        || enmVMState == VMSTATE_RESETTING
        || enmVMState == VMSTATE_RESETTING_LS
        || enmVMState == VMSTATE_POWERING_OFF
        || enmVMState == VMSTATE_POWERING_OFF_LS)
    {
        LogFlow(("pdmR3DrvHlp_AsyncNotificationCompleted: caller='%s'/%d:\n", pDrvIns->pReg->szName, pDrvIns->iInstance));
        VMR3AsyncPdmNotificationWakeupU(pVM->pUVM);
    }
    else
        LogFlow(("pdmR3DrvHlp_AsyncNotificationCompleted: caller='%s'/%d: enmVMState=%d\n", pDrvIns->pReg->szName, pDrvIns->iInstance, enmVMState));
}


/** @interface_method_impl{PDMDRVHLPR3,pfnThreadCreate} */
static DECLCALLBACK(int) pdmR3DrvHlp_ThreadCreate(PPDMDRVINS pDrvIns, PPPDMTHREAD ppThread, void *pvUser,
                                                  PFNPDMTHREADDRV pfnThread, PFNPDMTHREADWAKEUPDRV pfnWakeup, size_t cbStack,
                                                  RTTHREADTYPE enmType, const char *pszName)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    VM_ASSERT_EMT(pDrvIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DrvHlp_ThreadCreate: caller='%s'/%d: ppThread=%p pvUser=%p pfnThread=%p pfnWakeup=%p cbStack=%#zx enmType=%d pszName=%p:{%s}\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, ppThread, pvUser, pfnThread, pfnWakeup, cbStack, enmType, pszName, pszName));

    int rc = pdmR3ThreadCreateDriver(pDrvIns->Internal.s.pVMR3, pDrvIns, ppThread, pvUser, pfnThread, pfnWakeup, cbStack, enmType, pszName);

    LogFlow(("pdmR3DrvHlp_ThreadCreate: caller='%s'/%d: returns %Rrc *ppThread=%RTthrd\n", pDrvIns->pReg->szName, pDrvIns->iInstance,
            rc, *ppThread));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnAsyncCompletionTemplateCreate} */
static DECLCALLBACK(int) pdmR3DrvHlp_AsyncCompletionTemplateCreate(PPDMDRVINS pDrvIns, PPPDMASYNCCOMPLETIONTEMPLATE ppTemplate,
                                                                   PFNPDMASYNCCOMPLETEDRV pfnCompleted, void *pvTemplateUser,
                                                                   const char *pszDesc)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_AsyncCompletionTemplateCreate: caller='%s'/%d: ppTemplate=%p pfnCompleted=%p pszDesc=%p:{%s}\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, ppTemplate, pfnCompleted, pszDesc, pszDesc));

    int rc = pdmR3AsyncCompletionTemplateCreateDriver(pDrvIns->Internal.s.pVMR3, pDrvIns, ppTemplate, pfnCompleted, pvTemplateUser, pszDesc);

    LogFlow(("pdmR3DrvHlp_AsyncCompletionTemplateCreate: caller='%s'/%d: returns %Rrc *ppThread=%p\n", pDrvIns->pReg->szName,
             pDrvIns->iInstance, rc, *ppTemplate));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnNetShaperAttach} */
static DECLCALLBACK(int) pdmR3DrvHlp_NetShaperAttach(PPDMDRVINS pDrvIns, const char *pszBwGroup, PPDMNSFILTER pFilter)
{
#ifdef VBOX_WITH_NETSHAPER
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_NetShaperAttach: caller='%s'/%d: pFilter=%p pszBwGroup=%p:{%s}\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pFilter, pszBwGroup, pszBwGroup));

    int rc = PDMR3NsAttach(pDrvIns->Internal.s.pVMR3, pDrvIns, pszBwGroup, pFilter);

    LogFlow(("pdmR3DrvHlp_NetShaperAttach: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName,
             pDrvIns->iInstance, rc));
    return rc;
#else
    RT_NOREF(pDrvIns, pszBwGroup, pFilter);
    return VERR_NOT_IMPLEMENTED;
#endif
}


/** @interface_method_impl{PDMDRVHLPR3,pfnNetShaperDetach} */
static DECLCALLBACK(int) pdmR3DrvHlp_NetShaperDetach(PPDMDRVINS pDrvIns, PPDMNSFILTER pFilter)
{
#ifdef VBOX_WITH_NETSHAPER
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_NetShaperDetach: caller='%s'/%d: pFilter=%p\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pFilter));

    int rc = PDMR3NsDetach(pDrvIns->Internal.s.pVMR3, pDrvIns, pFilter);

    LogFlow(("pdmR3DrvHlp_NetShaperDetach: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName,
             pDrvIns->iInstance, rc));
    return rc;
#else
    RT_NOREF(pDrvIns, pFilter);
    return VERR_NOT_IMPLEMENTED;
#endif
}


/** @interface_method_impl{PDMDRVHLPR3,pfnNetShaperAllocateBandwidth} */
static DECLCALLBACK(bool) pdmR3DrvHlp_NetShaperAllocateBandwidth(PPDMDRVINS pDrvIns, PPDMNSFILTER pFilter, size_t cbTransfer)
{
#ifdef VBOX_WITH_NETSHAPER
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_NetShaperDetach: caller='%s'/%d: pFilter=%p cbTransfer=%#zx\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pFilter, cbTransfer));

    bool const fRc = PDMNetShaperAllocateBandwidth(pDrvIns->Internal.s.pVMR3, pFilter, cbTransfer);

    LogFlow(("pdmR3DrvHlp_NetShaperDetach: caller='%s'/%d: returns %RTbool\n", pDrvIns->pReg->szName, pDrvIns->iInstance, fRc));
    return fRc;
#else
    RT_NOREF(pDrvIns, pFilter, cbTransfer);
    return true;
#endif
}


/** @interface_method_impl{PDMDRVHLPR3,pfnLdrGetRCInterfaceSymbols} */
static DECLCALLBACK(int) pdmR3DrvHlp_LdrGetRCInterfaceSymbols(PPDMDRVINS pDrvIns, void *pvInterface, size_t cbInterface,
                                                              const char *pszSymPrefix, const char *pszSymList)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    VM_ASSERT_EMT(pDrvIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DrvHlp_LdrGetRCInterfaceSymbols: caller='%s'/%d: pvInterface=%p cbInterface=%zu pszSymPrefix=%p:{%s} pszSymList=%p:{%s}\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pvInterface, cbInterface, pszSymPrefix, pszSymPrefix, pszSymList, pszSymList));

    int rc;
    if (   strncmp(pszSymPrefix, "drv", 3) == 0
        && RTStrIStr(pszSymPrefix + 3, pDrvIns->pReg->szName) != NULL)
    {
        if (pDrvIns->pReg->fFlags & PDM_DRVREG_FLAGS_RC)
#ifdef PDM_WITH_RING0_DRIVERS
            rc = PDMR3LdrGetInterfaceSymbols(pDrvIns->Internal.s.pVMR3,
                                             pvInterface, cbInterface,
                                             pDrvIns->pReg->szRCMod, pDrvIns->Internal.s.pDrv->pszRCSearchPath,
                                             pszSymPrefix, pszSymList,
                                             false /*fRing0OrRC*/);
#else
        {
            AssertLogRelMsgFailed(("ring-0 drivers are not supported in this VBox version!\n"));
            RT_NOREF(pvInterface, cbInterface, pszSymList);
            rc = VERR_NOT_SUPPORTED;
        }
#endif
        else
        {
            AssertMsgFailed(("Not a raw-mode enabled driver\n"));
            rc = VERR_PERMISSION_DENIED;
        }
    }
    else
    {
        AssertMsgFailed(("Invalid prefix '%s' for '%s'; must start with 'drv' and contain the driver name!\n",
                         pszSymPrefix, pDrvIns->pReg->szName));
        rc = VERR_INVALID_NAME;
    }

    LogFlow(("pdmR3DrvHlp_LdrGetRCInterfaceSymbols: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName,
             pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnLdrGetR0InterfaceSymbols} */
static DECLCALLBACK(int) pdmR3DrvHlp_LdrGetR0InterfaceSymbols(PPDMDRVINS pDrvIns, void *pvInterface, size_t cbInterface,
                                                              const char *pszSymPrefix, const char *pszSymList)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    VM_ASSERT_EMT(pDrvIns->Internal.s.pVMR3);
    LogFlow(("pdmR3DrvHlp_LdrGetR0InterfaceSymbols: caller='%s'/%d: pvInterface=%p cbInterface=%zu pszSymPrefix=%p:{%s} pszSymList=%p:{%s}\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pvInterface, cbInterface, pszSymPrefix, pszSymPrefix, pszSymList, pszSymList));

    int rc;
    if (   strncmp(pszSymPrefix, "drv", 3) == 0
        && RTStrIStr(pszSymPrefix + 3, pDrvIns->pReg->szName) != NULL)
    {
        if (pDrvIns->pReg->fFlags & PDM_DRVREG_FLAGS_R0)
#ifdef PDM_WITH_RING0_DRIVERS
            rc = PDMR3LdrGetInterfaceSymbols(pDrvIns->Internal.s.pVMR3,
                                             pvInterface, cbInterface,
                                             pDrvIns->pReg->szR0Mod, pDrvIns->Internal.s.pDrv->pszR0SearchPath,
                                             pszSymPrefix, pszSymList,
                                             true /*fRing0OrRC*/);
#else
        {
            AssertLogRelMsgFailed(("ring-0 drivers are not supported in this VBox version!\n"));
            RT_NOREF(pvInterface, cbInterface, pszSymList);
            rc = VERR_NOT_SUPPORTED;
        }
#endif
        else
        {
            AssertMsgFailed(("Not a ring-0 enabled driver\n"));
            rc = VERR_PERMISSION_DENIED;
        }
    }
    else
    {
        AssertMsgFailed(("Invalid prefix '%s' for '%s'; must start with 'drv' and contain the driver name!\n",
                         pszSymPrefix, pDrvIns->pReg->szName));
        rc = VERR_INVALID_NAME;
    }

    LogFlow(("pdmR3DrvHlp_LdrGetR0InterfaceSymbols: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName,
             pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectInit} */
static DECLCALLBACK(int) pdmR3DrvHlp_CritSectInit(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect,
                                                  RT_SRC_POS_DECL, const char *pszName)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    PVM pVM = pDrvIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3DrvHlp_CritSectInit: caller='%s'/%d: pCritSect=%p pszName=%s\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pCritSect, pszName));

    int rc = pdmR3CritSectInitDriver(pVM, pDrvIns, pCritSect, RT_SRC_POS_ARGS, "%s_%u", pszName, pDrvIns->iInstance);

    LogFlow(("pdmR3DrvHlp_CritSectInit: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName,
             pDrvIns->iInstance, rc));
    return rc;
}

/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectYield} */
static DECLCALLBACK(bool)     pdmR3DrvHlp_CritSectYield(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    RT_NOREF(pDrvIns);
    return PDMR3CritSectYield(pDrvIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectEnter} */
static DECLCALLBACK(int)      pdmR3DrvHlp_CritSectEnter(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect, int rcBusy)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectEnter(pDrvIns->Internal.s.pVMR3, pCritSect, rcBusy);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectEnterDebug} */
static DECLCALLBACK(int)      pdmR3DrvHlp_CritSectEnterDebug(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect, int rcBusy,
                                                             RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectEnterDebug(pDrvIns->Internal.s.pVMR3, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectTryEnter} */
static DECLCALLBACK(int)      pdmR3DrvHlp_CritSectTryEnter(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectTryEnter(pDrvIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectTryEnterDebug} */
static DECLCALLBACK(int)      pdmR3DrvHlp_CritSectTryEnterDebug(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect,
                                                                RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectTryEnterDebug(pDrvIns->Internal.s.pVMR3, pCritSect, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectLeave} */
static DECLCALLBACK(int)      pdmR3DrvHlp_CritSectLeave(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectLeave(pDrvIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectIsOwner} */
static DECLCALLBACK(bool)     pdmR3DrvHlp_CritSectIsOwner(PPDMDRVINS pDrvIns, PCPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectIsOwner(pDrvIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectIsInitialized} */
static DECLCALLBACK(bool)     pdmR3DrvHlp_CritSectIsInitialized(PPDMDRVINS pDrvIns, PCPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    RT_NOREF(pDrvIns);
    return PDMCritSectIsInitialized(pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectHasWaiters} */
static DECLCALLBACK(bool)     pdmR3DrvHlp_CritSectHasWaiters(PPDMDRVINS pDrvIns, PCPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectHasWaiters(pDrvIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectGetRecursion} */
static DECLCALLBACK(uint32_t) pdmR3DrvHlp_CritSectGetRecursion(PPDMDRVINS pDrvIns, PCPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    RT_NOREF(pDrvIns);
    return PDMCritSectGetRecursion(pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectScheduleExitEvent} */
static DECLCALLBACK(int)      pdmR3DrvHlp_CritSectScheduleExitEvent(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect,
                                                                    SUPSEMEVENT hEventToSignal)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    RT_NOREF(pDrvIns);
    return PDMHCCritSectScheduleExitEvent(pCritSect, hEventToSignal);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCritSectDelete} */
static DECLCALLBACK(int)      pdmR3DrvHlp_CritSectDelete(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMR3CritSectDelete(pDrvIns->Internal.s.pVMR3, pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR3,pfnCallR0} */
static DECLCALLBACK(int) pdmR3DrvHlp_CallR0(PPDMDRVINS pDrvIns, uint32_t uOperation, uint64_t u64Arg)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
#ifdef PDM_WITH_RING0_DRIVERS
    PVM pVM = pDrvIns->Internal.s.pVMR3;
#endif
    LogFlow(("pdmR3DrvHlp_CallR0: caller='%s'/%d: uOperation=%#x u64Arg=%#RX64\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, uOperation, u64Arg));

    /*
     * Lazy resolve the ring-0 entry point.
     */
    int rc = VINF_SUCCESS;
    PFNPDMDRVREQHANDLERR0 pfnReqHandlerR0 = pDrvIns->Internal.s.pfnReqHandlerR0;
    if (RT_UNLIKELY(pfnReqHandlerR0 == NIL_RTR0PTR))
    {
        if (pDrvIns->pReg->fFlags & PDM_DRVREG_FLAGS_R0)
        {
#ifdef PDM_WITH_RING0_DRIVERS
            char szSymbol[          sizeof("drvR0") + sizeof(pDrvIns->pReg->szName) + sizeof("ReqHandler")];
            strcat(strcat(strcpy(szSymbol, "drvR0"),         pDrvIns->pReg->szName),         "ReqHandler");
            szSymbol[sizeof("drvR0") - 1] = RT_C_TO_UPPER(szSymbol[sizeof("drvR0") - 1]);

            rc = PDMR3LdrGetSymbolR0Lazy(pVM, pDrvIns->pReg->szR0Mod, pDrvIns->Internal.s.pDrv->pszR0SearchPath, szSymbol,
                                         &pfnReqHandlerR0);
            if (RT_SUCCESS(rc))
                pDrvIns->Internal.s.pfnReqHandlerR0 = pfnReqHandlerR0;
            else
                pfnReqHandlerR0 = NIL_RTR0PTR;
#else
            RT_NOREF(uOperation, u64Arg);
            rc = VERR_NOT_SUPPORTED;
#endif
        }
        else
            rc = VERR_ACCESS_DENIED;
    }
    if (RT_LIKELY(pfnReqHandlerR0 != NIL_RTR0PTR && RT_SUCCESS(rc)))
    {
#ifdef PDM_WITH_RING0_DRIVERS
        /*
         * Make the ring-0 call.
         */
        PDMDRIVERCALLREQHANDLERREQ Req;
        Req.Hdr.u32Magic    = SUPVMMR0REQHDR_MAGIC;
        Req.Hdr.cbReq       = sizeof(Req);
        Req.pDrvInsR0       = PDMDRVINS_2_R0PTR(pDrvIns);
        Req.uOperation      = uOperation;
        Req.u32Alignment    = 0;
        Req.u64Arg          = u64Arg;
        rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), NIL_VMCPUID, VMMR0_DO_PDM_DRIVER_CALL_REQ_HANDLER, 0, &Req.Hdr);
#else
        rc = VERR_NOT_SUPPORTED;
#endif
    }

    LogFlow(("pdmR3DrvHlp_CallR0: caller='%s'/%d: returns %Rrc\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnBlkCacheRetain} */
static DECLCALLBACK(int) pdmR3DrvHlp_BlkCacheRetain(PPDMDRVINS pDrvIns, PPPDMBLKCACHE ppBlkCache,
                                                    PFNPDMBLKCACHEXFERCOMPLETEDRV pfnXferComplete,
                                                    PFNPDMBLKCACHEXFERENQUEUEDRV pfnXferEnqueue,
                                                    PFNPDMBLKCACHEXFERENQUEUEDISCARDDRV pfnXferEnqueueDiscard,
                                                    const char *pcszId)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMR3BlkCacheRetainDriver(pDrvIns->Internal.s.pVMR3, pDrvIns, ppBlkCache,
                                     pfnXferComplete, pfnXferEnqueue, pfnXferEnqueueDiscard, pcszId);
}



/** @interface_method_impl{PDMDRVHLPR3,pfnVMGetSuspendReason} */
static DECLCALLBACK(VMSUSPENDREASON) pdmR3DrvHlp_VMGetSuspendReason(PPDMDRVINS pDrvIns)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    PVM pVM = pDrvIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    VMSUSPENDREASON enmReason = VMR3GetSuspendReason(pVM->pUVM);
    LogFlow(("pdmR3DrvHlp_VMGetSuspendReason: caller='%s'/%d: returns %d\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, enmReason));
    return enmReason;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnVMGetResumeReason} */
static DECLCALLBACK(VMRESUMEREASON) pdmR3DrvHlp_VMGetResumeReason(PPDMDRVINS pDrvIns)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    PVM pVM = pDrvIns->Internal.s.pVMR3;
    VM_ASSERT_EMT(pVM);
    VMRESUMEREASON enmReason = VMR3GetResumeReason(pVM->pUVM);
    LogFlow(("pdmR3DrvHlp_VMGetResumeReason: caller='%s'/%d: returns %d\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, enmReason));
    return enmReason;
}


/** @interface_method_impl{PDMDRVHLPR3,pfnQueryGenericUserObject} */
static DECLCALLBACK(void *) pdmR3DrvHlp_QueryGenericUserObject(PPDMDRVINS pDrvIns, PCRTUUID pUuid)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR3DrvHlp_QueryGenericUserObject: caller='%s'/%d: pUuid=%p:%RTuuid\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pUuid, pUuid));

    void *pvRet;
    PUVM  pUVM = pDrvIns->Internal.s.pVMR3->pUVM;
    if (pUVM->pVmm2UserMethods->pfnQueryGenericObject)
        pvRet = pUVM->pVmm2UserMethods->pfnQueryGenericObject(pUVM->pVmm2UserMethods, pUVM, pUuid);
    else
        pvRet = NULL;

    LogFlow(("pdmR3DrvHlp_QueryGenericUserObject: caller='%s'/%d: returns %#p for %RTuuid\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pvRet, pUuid));
    return pvRet;
}


/**
 * The driver helper structure.
 */
const PDMDRVHLPR3 g_pdmR3DrvHlp =
{
    PDM_DRVHLPR3_VERSION,
    pdmR3DrvHlp_Attach,
    pdmR3DrvHlp_Detach,
    pdmR3DrvHlp_DetachSelf,
    pdmR3DrvHlp_MountPrepare,
    pdmR3DrvHlp_AssertEMT,
    pdmR3DrvHlp_AssertOther,
    pdmR3DrvHlp_VMSetErrorV,
    pdmR3DrvHlp_VMSetRuntimeErrorV,
    pdmR3DrvHlp_VMState,
    pdmR3DrvHlp_VMTeleportedAndNotFullyResumedYet,
    pdmR3DrvHlp_GetSupDrvSession,
    pdmR3DrvHlp_QueueCreate,
    pdmR3DrvHlp_QueueAlloc,
    pdmR3DrvHlp_QueueInsert,
    pdmR3DrvHlp_QueueFlushIfNecessary,
    pdmR3DrvHlp_TMGetVirtualFreq,
    pdmR3DrvHlp_TMGetVirtualTime,
    pdmR3DrvHlp_TimerCreate,
    pdmR3DrvHlp_TimerDestroy,
    pdmR3DrvHlp_SSMRegister,
    pdmR3DrvHlp_SSMDeregister,
    SSMR3PutStruct,
    SSMR3PutStructEx,
    SSMR3PutBool,
    SSMR3PutU8,
    SSMR3PutS8,
    SSMR3PutU16,
    SSMR3PutS16,
    SSMR3PutU32,
    SSMR3PutS32,
    SSMR3PutU64,
    SSMR3PutS64,
    SSMR3PutU128,
    SSMR3PutS128,
    SSMR3PutUInt,
    SSMR3PutSInt,
    SSMR3PutGCUInt,
    SSMR3PutGCUIntReg,
    SSMR3PutGCPhys32,
    SSMR3PutGCPhys64,
    SSMR3PutGCPhys,
    SSMR3PutGCPtr,
    SSMR3PutGCUIntPtr,
    SSMR3PutRCPtr,
    SSMR3PutIOPort,
    SSMR3PutSel,
    SSMR3PutMem,
    SSMR3PutStrZ,
    SSMR3GetStruct,
    SSMR3GetStructEx,
    SSMR3GetBool,
    SSMR3GetBoolV,
    SSMR3GetU8,
    SSMR3GetU8V,
    SSMR3GetS8,
    SSMR3GetS8V,
    SSMR3GetU16,
    SSMR3GetU16V,
    SSMR3GetS16,
    SSMR3GetS16V,
    SSMR3GetU32,
    SSMR3GetU32V,
    SSMR3GetS32,
    SSMR3GetS32V,
    SSMR3GetU64,
    SSMR3GetU64V,
    SSMR3GetS64,
    SSMR3GetS64V,
    SSMR3GetU128,
    SSMR3GetU128V,
    SSMR3GetS128,
    SSMR3GetS128V,
    SSMR3GetGCPhys32,
    SSMR3GetGCPhys32V,
    SSMR3GetGCPhys64,
    SSMR3GetGCPhys64V,
    SSMR3GetGCPhys,
    SSMR3GetGCPhysV,
    SSMR3GetUInt,
    SSMR3GetSInt,
    SSMR3GetGCUInt,
    SSMR3GetGCUIntReg,
    SSMR3GetGCPtr,
    SSMR3GetGCUIntPtr,
    SSMR3GetRCPtr,
    SSMR3GetIOPort,
    SSMR3GetSel,
    SSMR3GetMem,
    SSMR3GetStrZ,
    SSMR3GetStrZEx,
    SSMR3Skip,
    SSMR3SkipToEndOfUnit,
    SSMR3SetLoadError,
    SSMR3SetLoadErrorV,
    SSMR3SetCfgError,
    SSMR3SetCfgErrorV,
    SSMR3HandleGetStatus,
    SSMR3HandleGetAfter,
    SSMR3HandleIsLiveSave,
    SSMR3HandleMaxDowntime,
    SSMR3HandleHostBits,
    SSMR3HandleRevision,
    SSMR3HandleVersion,
    SSMR3HandleHostOSAndArch,
    CFGMR3Exists,
    CFGMR3QueryType,
    CFGMR3QuerySize,
    CFGMR3QueryInteger,
    CFGMR3QueryIntegerDef,
    CFGMR3QueryString,
    CFGMR3QueryStringDef,
    CFGMR3QueryPassword,
    CFGMR3QueryPasswordDef,
    CFGMR3QueryBytes,
    CFGMR3QueryU64,
    CFGMR3QueryU64Def,
    CFGMR3QueryS64,
    CFGMR3QueryS64Def,
    CFGMR3QueryU32,
    CFGMR3QueryU32Def,
    CFGMR3QueryS32,
    CFGMR3QueryS32Def,
    CFGMR3QueryU16,
    CFGMR3QueryU16Def,
    CFGMR3QueryS16,
    CFGMR3QueryS16Def,
    CFGMR3QueryU8,
    CFGMR3QueryU8Def,
    CFGMR3QueryS8,
    CFGMR3QueryS8Def,
    CFGMR3QueryBool,
    CFGMR3QueryBoolDef,
    CFGMR3QueryPort,
    CFGMR3QueryPortDef,
    CFGMR3QueryUInt,
    CFGMR3QueryUIntDef,
    CFGMR3QuerySInt,
    CFGMR3QuerySIntDef,
    CFGMR3QueryGCPtr,
    CFGMR3QueryGCPtrDef,
    CFGMR3QueryGCPtrU,
    CFGMR3QueryGCPtrUDef,
    CFGMR3QueryGCPtrS,
    CFGMR3QueryGCPtrSDef,
    CFGMR3QueryStringAlloc,
    CFGMR3QueryStringAllocDef,
    CFGMR3GetParent,
    CFGMR3GetChild,
    CFGMR3GetChildF,
    CFGMR3GetChildFV,
    CFGMR3GetFirstChild,
    CFGMR3GetNextChild,
    CFGMR3GetName,
    CFGMR3GetNameLen,
    CFGMR3AreChildrenValid,
    CFGMR3GetFirstValue,
    CFGMR3GetNextValue,
    CFGMR3GetValueName,
    CFGMR3GetValueNameLen,
    CFGMR3GetValueType,
    CFGMR3AreValuesValid,
    CFGMR3ValidateConfig,
    pdmR3DrvHlp_MMHeapFree,
    pdmR3DrvHlp_DBGFInfoRegister,
    pdmR3DrvHlp_DBGFInfoRegisterArgv,
    pdmR3DrvHlp_DBGFInfoDeregister,
    pdmR3DrvHlp_STAMRegister,
    pdmR3DrvHlp_STAMRegisterF,
    pdmR3DrvHlp_STAMRegisterV,
    pdmR3DrvHlp_STAMDeregister,
    pdmR3DrvHlp_SUPCallVMMR0Ex,
    pdmR3DrvHlp_USBRegisterHub,
    pdmR3DrvHlp_SetAsyncNotification,
    pdmR3DrvHlp_AsyncNotificationCompleted,
    pdmR3DrvHlp_ThreadCreate,
    PDMR3ThreadDestroy,
    PDMR3ThreadIAmSuspending,
    PDMR3ThreadIAmRunning,
    PDMR3ThreadSleep,
    PDMR3ThreadSuspend,
    PDMR3ThreadResume,
    pdmR3DrvHlp_AsyncCompletionTemplateCreate,
    PDMR3AsyncCompletionTemplateDestroy,
    PDMR3AsyncCompletionEpCreateForFile,
    PDMR3AsyncCompletionEpClose,
    PDMR3AsyncCompletionEpGetSize,
    PDMR3AsyncCompletionEpSetSize,
    PDMR3AsyncCompletionEpSetBwMgr,
    PDMR3AsyncCompletionEpFlush,
    PDMR3AsyncCompletionEpRead,
    PDMR3AsyncCompletionEpWrite,
    pdmR3DrvHlp_NetShaperAttach,
    pdmR3DrvHlp_NetShaperDetach,
    pdmR3DrvHlp_NetShaperAllocateBandwidth,
    pdmR3DrvHlp_LdrGetRCInterfaceSymbols,
    pdmR3DrvHlp_LdrGetR0InterfaceSymbols,
    pdmR3DrvHlp_CritSectInit,
    pdmR3DrvHlp_CritSectYield,
    pdmR3DrvHlp_CritSectEnter,
    pdmR3DrvHlp_CritSectEnterDebug,
    pdmR3DrvHlp_CritSectTryEnter,
    pdmR3DrvHlp_CritSectTryEnterDebug,
    pdmR3DrvHlp_CritSectLeave,
    pdmR3DrvHlp_CritSectIsOwner,
    pdmR3DrvHlp_CritSectIsInitialized,
    pdmR3DrvHlp_CritSectHasWaiters,
    pdmR3DrvHlp_CritSectGetRecursion,
    pdmR3DrvHlp_CritSectScheduleExitEvent,
    pdmR3DrvHlp_CritSectDelete,
    pdmR3DrvHlp_CallR0,
    pdmR3DrvHlp_BlkCacheRetain,
    PDMR3BlkCacheRelease,
    PDMR3BlkCacheClear,
    PDMR3BlkCacheSuspend,
    PDMR3BlkCacheResume,
    PDMR3BlkCacheIoXferComplete,
    PDMR3BlkCacheRead,
    PDMR3BlkCacheWrite,
    PDMR3BlkCacheFlush,
    PDMR3BlkCacheDiscard,
    pdmR3DrvHlp_VMGetSuspendReason,
    pdmR3DrvHlp_VMGetResumeReason,
    pdmR3DrvHlp_TimerSetMillies,
    pdmR3DrvHlp_STAMDeregisterByPrefix,
    pdmR3DrvHlp_QueryGenericUserObject,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    PDM_DRVHLPR3_VERSION /* u32TheEnd */
};

/** @} */
