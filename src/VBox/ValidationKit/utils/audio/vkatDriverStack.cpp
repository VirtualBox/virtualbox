/* $Id: vkatDriverStack.cpp 108006 2025-01-22 14:24:01Z andreas.loeffler@oracle.com $ */
/** @file
 * Validation Kit Audio Test (VKAT) - Driver stack code.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_AUDIO_TEST
#include <iprt/log.h>

#include <iprt/errcore.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/test.h>


/**
 * Internal driver instance data
 * @note This must be put here as it's needed before pdmdrv.h is included.
 */
typedef struct PDMDRVINSINT
{
    /** The stack the drive belongs to. */
    struct AUDIOTESTDRVSTACK *pStack;
} PDMDRVINSINT;
#define PDMDRVINSINT_DECLARED

#include "vkatInternal.h"
#include "VBoxDD.h"



/*********************************************************************************************************************************
*   Fake PDM Driver Handling.                                                                                                    *
*********************************************************************************************************************************/

/** @name Driver Fakes/Stubs
 *
 * @{  */

static DECLCALLBACK(PCFGMNODE) audioTestDrvHlp_CFGMR3GetChild(PCFGMNODE pNode, const char *pszPath)
{
    RT_NOREF(pNode, pszPath);
    return NULL;
}


static DECLCALLBACK(int) audioTestDrvHlp_CFGMR3QueryString(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString)
{
    if (pNode != NULL)
    {
        PCPDMDRVREG pDrvReg = (PCPDMDRVREG)pNode;
        if (g_uVerbosity > 2)
            RTPrintf("debug: CFGMR3QueryString([%s], %s, %p, %#x)\n", pDrvReg->szName, pszName, pszString, cchString);

        if (   (   strcmp(pDrvReg->szName, "PulseAudio") == 0
                || strcmp(pDrvReg->szName, "HostAudioWas") == 0)
            && strcmp(pszName, "VmName") == 0)
            return RTStrCopy(pszString, cchString, "vkat");

        if (   strcmp(pDrvReg->szName, "HostAudioWas") == 0
            && strcmp(pszName, "VmUuid") == 0)
            return RTStrCopy(pszString, cchString, "794c9192-d045-4f28-91ed-46253ac9998e");
    }
    else if (g_uVerbosity > 2)
        RTPrintf("debug: CFGMR3QueryString(%p, %s, %p, %#x)\n", pNode, pszName, pszString, cchString);

    return VERR_CFGM_VALUE_NOT_FOUND;
}


static DECLCALLBACK(int) audioTestDrvHlp_CFGMR3QueryStringAlloc(PCFGMNODE pNode, const char *pszName, char **ppszString)
{
    char szStr[128];
    int rc = audioTestDrvHlp_CFGMR3QueryString(pNode, pszName, szStr, sizeof(szStr));
    if (RT_SUCCESS(rc))
        *ppszString = RTStrDup(szStr);

    return rc;
}


static DECLCALLBACK(void) audioTestDrvHlp_MMR3HeapFree(PPDMDRVINS pDrvIns, void *pv)
{
    RT_NOREF(pDrvIns);

    /* counterpart to CFGMR3QueryStringAlloc */
    RTStrFree((char *)pv);
}


static DECLCALLBACK(int) audioTestDrvHlp_CFGMR3QueryStringDef(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString, const char *pszDef)
{
    PCPDMDRVREG pDrvReg = (PCPDMDRVREG)pNode;
    if (RT_VALID_PTR(pDrvReg))
    {
        const char *pszRet = pszDef;
        if (   g_pszDrvAudioDebug
            && strcmp(pDrvReg->szName, "AUDIO") == 0
            && strcmp(pszName, "DebugPathOut") == 0)
            pszRet = g_pszDrvAudioDebug;

        int rc = RTStrCopy(pszString, cchString, pszRet);

        if (g_uVerbosity > 2)
            RTPrintf("debug: CFGMR3QueryStringDef([%s], %s, %p, %#x, %s) -> '%s' + %Rrc\n",
                     pDrvReg->szName, pszName, pszString, cchString, pszDef, pszRet, rc);
        return rc;
    }

    if (g_uVerbosity > 2)
        RTPrintf("debug: CFGMR3QueryStringDef(%p, %s, %p, %#x, %s)\n", pNode, pszName, pszString, cchString, pszDef);
    return RTStrCopy(pszString, cchString, pszDef);
}


static DECLCALLBACK(int) audioTestDrvHlp_CFGMR3QueryBoolDef(PCFGMNODE pNode, const char *pszName, bool *pf, bool fDef)
{
    PCPDMDRVREG pDrvReg = (PCPDMDRVREG)pNode;
    if (RT_VALID_PTR(pDrvReg))
    {
        *pf = fDef;
        if (   strcmp(pDrvReg->szName, "AUDIO") == 0
            && strcmp(pszName, "DebugEnabled") == 0)
            *pf = g_fDrvAudioDebug;

        if (g_uVerbosity > 2)
            RTPrintf("debug: CFGMR3QueryBoolDef([%s], %s, %p, %RTbool) -> %RTbool\n", pDrvReg->szName, pszName, pf, fDef, *pf);
        return VINF_SUCCESS;
    }
    *pf = fDef;
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) audioTestDrvHlp_CFGMR3QueryU8(PCFGMNODE pNode, const char *pszName, uint8_t *pu8)
{
    RT_NOREF(pNode, pszName, pu8);
    return VERR_CFGM_VALUE_NOT_FOUND;
}


static DECLCALLBACK(int) audioTestDrvHlp_CFGMR3QueryU32(PCFGMNODE pNode, const char *pszName, uint32_t *pu32)
{
    RT_NOREF(pNode, pszName, pu32);
    return VERR_CFGM_VALUE_NOT_FOUND;
}


static DECLCALLBACK(int) audioTestDrvHlp_CFGMR3ValidateConfig(PCFGMNODE pNode, const char *pszNode,
                                                              const char *pszValidValues, const char *pszValidNodes,
                                                              const char *pszWho, uint32_t uInstance)
{
    RT_NOREF(pNode, pszNode, pszValidValues, pszValidNodes, pszWho, uInstance);
    return VINF_SUCCESS;
}

/**
 * @copydoc PDMDRVHLPR3::pfnVMState
 */
static DECLCALLBACK(VMSTATE) audioTestDrvHlp_VMState(PPDMDRVINS pDrvIns)
{
    RT_NOREF(pDrvIns);
    return VMSTATE_RUNNING; /* For mocking we report the VM state as running here. */
}

/** @} */

/** @name Driver Helper Fakes
 * @{ */

static DECLCALLBACK(int) audioTestDrvHlp_Attach(PPDMDRVINS pDrvIns, uint32_t fFlags, PPDMIBASE *ppBaseInterface)
{
    /* DrvAudio must be allowed to attach the backend driver (paranoid
       backend drivers may call us to check that nothing is attached). */
    if (strcmp(pDrvIns->pReg->szName, "AUDIO") == 0)
    {
        PAUDIOTESTDRVSTACK pDrvStack = pDrvIns->Internal.s.pStack;
        AssertReturn(pDrvStack->pDrvBackendIns == NULL, VERR_PDM_DRIVER_ALREADY_ATTACHED);

        if (g_uVerbosity > 1)
            RTMsgInfo("Attaching backend '%s' to DrvAudio...\n", pDrvStack->pDrvReg->szName);
        int rc = audioTestDrvConstruct(pDrvStack, pDrvStack->pDrvReg, pDrvIns, &pDrvStack->pDrvBackendIns);
        if (RT_SUCCESS(rc))
        {
            if (ppBaseInterface)
                *ppBaseInterface = &pDrvStack->pDrvBackendIns->IBase;
        }
        else
            RTMsgError("Failed to attach backend: %Rrc", rc);
        return rc;
    }
    RT_NOREF(fFlags);
    return VERR_PDM_NO_ATTACHED_DRIVER;
}


static DECLCALLBACK(void) audioTestDrvHlp_STAMRegister(PPDMDRVINS pDrvIns, void *pvSample, STAMTYPE enmType, const char *pszName,
                                                       STAMUNIT enmUnit, const char *pszDesc)
{
    RT_NOREF(pDrvIns, pvSample, enmType, pszName, enmUnit, pszDesc);
}


static DECLCALLBACK(void) audioTestDrvHlp_STAMRegisterF(PPDMDRVINS pDrvIns, void *pvSample, STAMTYPE enmType,
                                                        STAMVISIBILITY enmVisibility, STAMUNIT enmUnit, const char *pszDesc,
                                                        const char *pszName, ...)
{
    RT_NOREF(pDrvIns, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName);
}


static DECLCALLBACK(void) audioTestDrvHlp_STAMRegisterV(PPDMDRVINS pDrvIns, void *pvSample, STAMTYPE enmType,
                                                        STAMVISIBILITY enmVisibility, STAMUNIT enmUnit, const char *pszDesc,
                                                        const char *pszName, va_list args)
{
    RT_NOREF(pDrvIns, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, args);
}


static DECLCALLBACK(int) audioTestDrvHlp_STAMDeregister(PPDMDRVINS pDrvIns, void *pvSample)
{
    RT_NOREF(pDrvIns, pvSample);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) audioTestDrvHlp_STAMDeregisterByPrefix(PPDMDRVINS pDrvIns, const char *pszPrefix)
{
    RT_NOREF(pDrvIns, pszPrefix);
    return VINF_SUCCESS;
}

/**
 * Get the driver helpers.
 */
static const PDMDRVHLPR3 *audioTestFakeGetDrvHlp(void)
{
    /*
     * Note! No initializer for s_DrvHlp (also why it's not a file global).
     *       We do not want to have to update this code every time PDMDRVHLPR3
     *       grows new entries or are otherwise modified.  Only when the
     *       entries used by the audio driver changes do we want to change
     *       our code.
     */
    static PDMDRVHLPR3 s_DrvHlp;
    if (s_DrvHlp.u32Version != PDM_DRVHLPR3_VERSION)
    {
        s_DrvHlp.u32Version                     = PDM_DRVHLPR3_VERSION;
        s_DrvHlp.u32TheEnd                      = PDM_DRVHLPR3_VERSION;
        s_DrvHlp.pfnAttach                      = audioTestDrvHlp_Attach;
        s_DrvHlp.pfnSTAMRegister                = audioTestDrvHlp_STAMRegister;
        s_DrvHlp.pfnSTAMRegisterF               = audioTestDrvHlp_STAMRegisterF;
        s_DrvHlp.pfnSTAMRegisterV               = audioTestDrvHlp_STAMRegisterV;
        s_DrvHlp.pfnSTAMDeregister              = audioTestDrvHlp_STAMDeregister;
        s_DrvHlp.pfnSTAMDeregisterByPrefix      = audioTestDrvHlp_STAMDeregisterByPrefix;
        s_DrvHlp.pfnCFGMGetChild                = audioTestDrvHlp_CFGMR3GetChild;
        s_DrvHlp.pfnCFGMQueryString             = audioTestDrvHlp_CFGMR3QueryString;
        s_DrvHlp.pfnCFGMQueryStringAlloc        = audioTestDrvHlp_CFGMR3QueryStringAlloc;
        s_DrvHlp.pfnMMHeapFree                  = audioTestDrvHlp_MMR3HeapFree;
        s_DrvHlp.pfnCFGMQueryStringDef          = audioTestDrvHlp_CFGMR3QueryStringDef;
        s_DrvHlp.pfnCFGMQueryBoolDef            = audioTestDrvHlp_CFGMR3QueryBoolDef;
        s_DrvHlp.pfnCFGMQueryU8                 = audioTestDrvHlp_CFGMR3QueryU8;
        s_DrvHlp.pfnCFGMQueryU32                = audioTestDrvHlp_CFGMR3QueryU32;
        s_DrvHlp.pfnCFGMValidateConfig          = audioTestDrvHlp_CFGMR3ValidateConfig;
        s_DrvHlp.pfnVMState                     = audioTestDrvHlp_VMState;
    }
    return &s_DrvHlp;
}

/** @} */


/**
 * Implementation of PDMIBASE::pfnQueryInterface for a fake device above
 * DrvAudio.
 */
static DECLCALLBACK(void *) audioTestFakeDeviceIBaseQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, pInterface);
    RTMsgWarning("audioTestFakeDeviceIBaseQueryInterface: Unknown interface: %s\n", pszIID);
    return NULL;
}

/** IBase interface for a fake device above DrvAudio. */
static PDMIBASE g_AudioTestFakeDeviceIBase =  { audioTestFakeDeviceIBaseQueryInterface };


static DECLCALLBACK(int) audioTestIHostAudioPort_DoOnWorkerThread(PPDMIHOSTAUDIOPORT pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                                  uintptr_t uUser, void *pvUser)
{
    RT_NOREF(pInterface, pStream, uUser, pvUser);
    RTMsgWarning("audioTestIHostAudioPort_DoOnWorkerThread was called\n");
    return VERR_NOT_IMPLEMENTED;
}


static DECLCALLBACK(void) audioTestIHostAudioPort_NotifyDeviceChanged(PPDMIHOSTAUDIOPORT pInterface, PDMAUDIODIR enmDir, void *pvUser)
{
    RT_NOREF(pInterface, enmDir, pvUser);
    RTMsgWarning("audioTestIHostAudioPort_NotifyDeviceChanged was called\n");
}


static DECLCALLBACK(void) audioTestIHostAudioPort_StreamNotifyPreparingDeviceSwitch(PPDMIHOSTAUDIOPORT pInterface,
                                                                                    PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    RTMsgWarning("audioTestIHostAudioPort_StreamNotifyPreparingDeviceSwitch was called\n");
}


static DECLCALLBACK(void) audioTestIHostAudioPort_StreamNotifyDeviceChanged(PPDMIHOSTAUDIOPORT pInterface,
                                                                            PPDMAUDIOBACKENDSTREAM pStream, bool fReInit)
{
    RT_NOREF(pInterface, pStream, fReInit);
    RTMsgWarning("audioTestIHostAudioPort_StreamNotifyDeviceChanged was called\n");
}


static DECLCALLBACK(void) audioTestIHostAudioPort_NotifyDevicesChanged(PPDMIHOSTAUDIOPORT pInterface)
{
    RT_NOREF(pInterface);
    RTMsgWarning("audioTestIHostAudioPort_NotifyDevicesChanged was called\n");
}


static PDMIHOSTAUDIOPORT g_AudioTestIHostAudioPort =
{
    audioTestIHostAudioPort_DoOnWorkerThread,
    audioTestIHostAudioPort_NotifyDeviceChanged,
    audioTestIHostAudioPort_StreamNotifyPreparingDeviceSwitch,
    audioTestIHostAudioPort_StreamNotifyDeviceChanged,
    audioTestIHostAudioPort_NotifyDevicesChanged,
};


/**
 * Implementation of PDMIBASE::pfnQueryInterface for a fake DrvAudio above a
 * backend.
 */
static DECLCALLBACK(void *) audioTestFakeDrvAudioIBaseQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, pInterface);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIOPORT, &g_AudioTestIHostAudioPort);
    RTMsgWarning("audioTestFakeDrvAudioIBaseQueryInterface: Unknown interface: %s\n", pszIID);
    return NULL;
}


/** IBase interface for a fake DrvAudio above a lonesome backend. */
static PDMIBASE g_AudioTestFakeDrvAudioIBase =  { audioTestFakeDrvAudioIBaseQueryInterface };



/**
 * Constructs a PDM audio driver instance.
 *
 * @returns VBox status code.
 * @param   pDrvStack       The stack this is associated with.
 * @param   pDrvReg         PDM driver registration record to use for construction.
 * @param   pParentDrvIns   The parent driver (if any).
 * @param   ppDrvIns        Where to return the driver instance structure.
 */
int audioTestDrvConstruct(PAUDIOTESTDRVSTACK pDrvStack, PCPDMDRVREG pDrvReg, PPDMDRVINS pParentDrvIns,
                          PPPDMDRVINS ppDrvIns)
{
    /* The destruct function must have valid data to work with. */
    *ppDrvIns   = NULL;

    /*
     * Check registration structure validation (doesn't need to be too
     * thorough, PDM check it in detail on every VM startup).
     */
    AssertPtrReturn(pDrvReg, VERR_INVALID_POINTER);
    RTMsgInfo("Initializing backend '%s' ...\n", pDrvReg->szName);
    AssertPtrReturn(pDrvReg->pfnConstruct, VERR_INVALID_PARAMETER);

    /*
     * Create the instance data structure.
     */
    PPDMDRVINS pDrvIns = (PPDMDRVINS)RTMemAllocZVar(RT_UOFFSETOF_DYN(PDMDRVINS, achInstanceData[pDrvReg->cbInstance]));
    RTTEST_CHECK_RET(g_hTest, pDrvIns, VERR_NO_MEMORY);

    pDrvIns->u32Version         = PDM_DRVINS_VERSION;
    pDrvIns->iInstance          = 0;
    pDrvIns->pHlpR3             = audioTestFakeGetDrvHlp();
    pDrvIns->pvInstanceDataR3   = &pDrvIns->achInstanceData[0];
    pDrvIns->pReg               = pDrvReg;
    pDrvIns->pCfg               = (PCFGMNODE)pDrvReg;
    pDrvIns->Internal.s.pStack  = pDrvStack;
    pDrvIns->pUpBase            = NULL;
    pDrvIns->pDownBase          = NULL;
    if (pParentDrvIns)
    {
        Assert(pParentDrvIns->pDownBase == NULL);
        pParentDrvIns->pDownBase = &pDrvIns->IBase;
        pDrvIns->pUpBase         = &pParentDrvIns->IBase;
    }
    else if (strcmp(pDrvReg->szName, "AUDIO") == 0)
        pDrvIns->pUpBase         = &g_AudioTestFakeDeviceIBase;
    else
        pDrvIns->pUpBase         = &g_AudioTestFakeDrvAudioIBase;

    /*
     * Invoke the constructor.
     */
    int rc = pDrvReg->pfnConstruct(pDrvIns, pDrvIns->pCfg, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        *ppDrvIns   = pDrvIns;
        return VINF_SUCCESS;
    }

    if (pDrvReg->pfnDestruct)
        pDrvReg->pfnDestruct(pDrvIns);
    RTMemFree(pDrvIns);
    return rc;
}


/**
 * Destructs a PDM audio driver instance.
 *
 * @param   pDrvIns             Driver instance to destruct.
 */
static void audioTestDrvDestruct(PPDMDRVINS pDrvIns)
{
    if (pDrvIns)
    {
        Assert(pDrvIns->u32Version == PDM_DRVINS_VERSION);

        if (pDrvIns->pReg->pfnDestruct)
            pDrvIns->pReg->pfnDestruct(pDrvIns);

        pDrvIns->u32Version = 0;
        pDrvIns->pReg = NULL;
        RTMemFree(pDrvIns);
    }
}


/**
 * Sends the PDM driver a power off notification.
 *
 * @param   pDrvIns             Driver instance to notify.
 */
static void audioTestDrvNotifyPowerOff(PPDMDRVINS pDrvIns)
{
    if (pDrvIns)
    {
        Assert(pDrvIns->u32Version == PDM_DRVINS_VERSION);
        if (pDrvIns->pReg->pfnPowerOff)
            pDrvIns->pReg->pfnPowerOff(pDrvIns);
    }
}


/**
 * Deletes a driver stack.
 *
 * This will power off and destroy the drivers.
 */
void audioTestDriverStackDelete(PAUDIOTESTDRVSTACK pDrvStack)
{
    /*
     * Do power off notifications (top to bottom).
     */
    audioTestDrvNotifyPowerOff(pDrvStack->pDrvAudioIns);
    audioTestDrvNotifyPowerOff(pDrvStack->pDrvBackendIns);

    /*
     * Drivers are destroyed from bottom to top (closest to the device).
     */
    audioTestDrvDestruct(pDrvStack->pDrvBackendIns);
    pDrvStack->pDrvBackendIns   = NULL;
    pDrvStack->pIHostAudio      = NULL;

    audioTestDrvDestruct(pDrvStack->pDrvAudioIns);
    pDrvStack->pDrvAudioIns     = NULL;
    pDrvStack->pIAudioConnector = NULL;

    PDMAudioHostEnumDelete(&pDrvStack->DevEnum);
}


/**
 * Initializes a driver stack, extended version.
 *
 * @returns VBox status code.
 * @param   pDrvStack       The driver stack to initialize.
 * @param   pDrvReg         The backend driver to use.
 * @param   fEnabledIn      Whether input is enabled or not on creation time.
 * @param   fEnabledOut     Whether output is enabled or not on creation time.
 * @param   fWithDrvAudio   Whether to include DrvAudio in the stack or not.
 */
int audioTestDriverStackInitEx(PAUDIOTESTDRVSTACK pDrvStack, PCPDMDRVREG pDrvReg, bool fEnabledIn, bool fEnabledOut, bool fWithDrvAudio)
{
    int rc;

    RT_ZERO(*pDrvStack);
    pDrvStack->pDrvReg = pDrvReg;

    PDMAudioHostEnumInit(&pDrvStack->DevEnum);

    if (!fWithDrvAudio)
        rc = audioTestDrvConstruct(pDrvStack, pDrvReg, NULL /*pParentDrvIns*/, &pDrvStack->pDrvBackendIns);
    else
    {
        rc = audioTestDrvConstruct(pDrvStack, &g_DrvAUDIO, NULL /*pParentDrvIns*/, &pDrvStack->pDrvAudioIns);
        if (RT_SUCCESS(rc))
        {
            Assert(pDrvStack->pDrvAudioIns);
            PPDMIBASE const pIBase = &pDrvStack->pDrvAudioIns->IBase;
            pDrvStack->pIAudioConnector = (PPDMIAUDIOCONNECTOR)pIBase->pfnQueryInterface(pIBase, PDMIAUDIOCONNECTOR_IID);
            if (pDrvStack->pIAudioConnector)
            {
                /* Both input and output is disabled by default. */
                if (fEnabledIn)
                    rc = pDrvStack->pIAudioConnector->pfnEnable(pDrvStack->pIAudioConnector, PDMAUDIODIR_IN, true);

                if (RT_SUCCESS(rc))
                {
                    if (fEnabledOut)
                        rc = pDrvStack->pIAudioConnector->pfnEnable(pDrvStack->pIAudioConnector, PDMAUDIODIR_OUT, true);
                }

                if (RT_FAILURE(rc))
                {
                    RTTestFailed(g_hTest, "Failed to enabled input and output: %Rrc", rc);
                    audioTestDriverStackDelete(pDrvStack);
                }
            }
            else
            {
                RTTestFailed(g_hTest, "Failed to query PDMIAUDIOCONNECTOR");
                audioTestDriverStackDelete(pDrvStack);
                rc = VERR_PDM_MISSING_INTERFACE;
            }
        }
    }

    /*
     * Get the IHostAudio interface and check that the host driver is working.
     */
    if (RT_SUCCESS(rc))
    {
        PPDMIBASE const pIBase = &pDrvStack->pDrvBackendIns->IBase;
        pDrvStack->pIHostAudio = (PPDMIHOSTAUDIO)pIBase->pfnQueryInterface(pIBase, PDMIHOSTAUDIO_IID);
        if (pDrvStack->pIHostAudio)
        {
            PDMAUDIOBACKENDSTS enmStatus = pDrvStack->pIHostAudio->pfnGetStatus(pDrvStack->pIHostAudio, PDMAUDIODIR_OUT);
            if (enmStatus == PDMAUDIOBACKENDSTS_RUNNING)
                return VINF_SUCCESS;

            RTTestFailed(g_hTest, "Expected backend status RUNNING, got %d instead", enmStatus);
        }
        else
            RTTestFailed(g_hTest, "Failed to query PDMIHOSTAUDIO for '%s'", pDrvReg->szName);
        audioTestDriverStackDelete(pDrvStack);
    }

    return rc;
}


/**
 * Initializes a driver stack.
 *
 * @returns VBox status code.
 * @param   pDrvStack       The driver stack to initialize.
 * @param   pDrvReg         The backend driver to use.
 * @param   fEnabledIn      Whether input is enabled or not on creation time.
 * @param   fEnabledOut     Whether output is enabled or not on creation time.
 * @param   fWithDrvAudio   Whether to include DrvAudio in the stack or not.
 */
int audioTestDriverStackInit(PAUDIOTESTDRVSTACK pDrvStack, PCPDMDRVREG pDrvReg, bool fWithDrvAudio)
{
    return audioTestDriverStackInitEx(pDrvStack, pDrvReg, true /* fEnabledIn */, true /* fEnabledOut */, fWithDrvAudio);
}

/**
 * Initializes a driver stack by probing all backends in the order of appearance
 * in the backends description table.
 *
 * @returns VBox status code.
 * @param   pDrvStack       The driver stack to initialize.
 * @param   fEnabledIn      Whether input is enabled or not on creation time.
 * @param   fEnabledOut     Whether output is enabled or not on creation time.
 * @param   fWithDrvAudio   Whether to include DrvAudio in the stack or not.
 */
int audioTestDriverStackProbe(PAUDIOTESTDRVSTACK pDrvStack, bool fEnabledIn, bool fEnabledOut, bool fWithDrvAudio)
{
    int rc = VERR_IPE_UNINITIALIZED_STATUS; /* Shut up MSVC. */

    PCPDMDRVREG pDrvLast = NULL; /* Last probed backend. */

    for (size_t i = 0; i < g_cBackends; i++)
    {
        PCPDMDRVREG pDrvReg = g_aBackends[i].pDrvReg;

        if (   pDrvLast
            && pDrvLast == pDrvReg) /* Check if we already probed the backend by another alias and skip if so. */
            continue;

        pDrvLast = pDrvReg;

        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Probing for backend '%s' ...\n", pDrvReg->szName);

        rc = audioTestDriverStackInitEx(pDrvStack, pDrvReg, fEnabledIn, fEnabledOut, fWithDrvAudio); /** @todo Make in/out configurable, too. */
        if (RT_SUCCESS(rc))
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Probing backend '%s' successful\n", pDrvReg->szName);
            return rc;
        }

        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Probing backend '%s' failed with %Rrc, trying next one\n", pDrvReg->szName, rc);
    }

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Probing all backends failed\n");
    return rc;
}

/**
 * Wrapper around PDMIHOSTAUDIO::pfnSetDevice.
 */
int audioTestDriverStackSetDevice(PAUDIOTESTDRVSTACK pDrvStack, PDMAUDIODIR enmDir, const char *pszDevId)
{
    int rc;
    if (   pDrvStack->pIHostAudio
        && pDrvStack->pIHostAudio->pfnSetDevice)
        rc = pDrvStack->pIHostAudio->pfnSetDevice(pDrvStack->pIHostAudio, enmDir, pszDevId);
    else if (!pszDevId || *pszDevId)
        rc = VINF_SUCCESS;
    else
        rc = VERR_INVALID_FUNCTION;
    return rc;
}


/**
 * Common stream creation code.
 *
 * @returns VBox status code.
 * @param   pDrvStack   The audio driver stack to create it via.
 * @param   pCfgReq     The requested config.
 * @param   ppStream    Where to return the stream pointer on success.
 * @param   pCfgAcq     Where to return the actual (well, not necessarily when
 *                      using DrvAudio, but probably the same) stream config on
 *                      success (not used as input).
 */
static int audioTestDriverStackStreamCreate(PAUDIOTESTDRVSTACK pDrvStack, PCPDMAUDIOSTREAMCFG pCfgReq,
                                            PPDMAUDIOSTREAM *ppStream, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    char szTmp[PDMAUDIOSTRMCFGTOSTRING_MAX + 16];
    int  rc;
    *ppStream = NULL;

    if (pDrvStack->pIAudioConnector)
    {
        /*
         * DrvAudio does most of the work here.
         */
        rc = pDrvStack->pIAudioConnector->pfnStreamCreate(pDrvStack->pIAudioConnector, 0 /*fFlags*/, pCfgReq, ppStream);
        if (RT_SUCCESS(rc))
        {
            *pCfgAcq = (*ppStream)->Cfg;
            RTMsgInfo("Created backend stream: %s\n", PDMAudioStrmCfgToString(pCfgReq, szTmp, sizeof(szTmp)));
            return rc;
        }
        /* else: Don't set RTTestFailed(...) here, as test boxes (servers) don't have any audio hardware.
         *       Caller has check the rc then. */
    }
    else
    {
        /*
         * Get the config so we can see how big the PDMAUDIOBACKENDSTREAM
         * structure actually is for this backend.
         */
        PDMAUDIOBACKENDCFG BackendCfg;
        rc = pDrvStack->pIHostAudio->pfnGetConfig(pDrvStack->pIHostAudio, &BackendCfg);
        if (RT_SUCCESS(rc))
        {
            if (BackendCfg.cbStream >= sizeof(PDMAUDIOBACKENDSTREAM))
            {
                /*
                 * Allocate and initialize the stream.
                 */
                uint32_t const cbStream = sizeof(AUDIOTESTDRVSTACKSTREAM) - sizeof(PDMAUDIOBACKENDSTREAM) + BackendCfg.cbStream;
                PAUDIOTESTDRVSTACKSTREAM pStreamAt = (PAUDIOTESTDRVSTACKSTREAM)RTMemAllocZVar(cbStream);
                if (pStreamAt)
                {
                    pStreamAt->Core.uMagic     = PDMAUDIOSTREAM_MAGIC;
                    pStreamAt->Core.Cfg        = *pCfgReq;
                    pStreamAt->Core.cbBackend  = cbStream;

                    pStreamAt->Backend.uMagic  = PDMAUDIOBACKENDSTREAM_MAGIC;
                    pStreamAt->Backend.pStream = &pStreamAt->Core;

                    /*
                     * Call the backend to create the stream.
                     */
                    rc = pDrvStack->pIHostAudio->pfnStreamCreate(pDrvStack->pIHostAudio, &pStreamAt->Backend,
                                                                 pCfgReq, &pStreamAt->Core.Cfg);
                    if (RT_SUCCESS(rc))
                    {
                        if (g_uVerbosity > 1)
                            RTMsgInfo("Created backend stream: %s\n",
                                      PDMAudioStrmCfgToString(&pStreamAt->Core.Cfg, szTmp, sizeof(szTmp)));

                        /* Return if stream is ready: */
                        if (rc == VINF_SUCCESS)
                        {
                            *ppStream = &pStreamAt->Core;
                            *pCfgAcq  = pStreamAt->Core.Cfg;
                            return VINF_SUCCESS;
                        }
                        if (rc == VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED)
                        {
                            /*
                             * Do async init right here and now.
                             */
                            rc = pDrvStack->pIHostAudio->pfnStreamInitAsync(pDrvStack->pIHostAudio, &pStreamAt->Backend,
                                                                            false /*fDestroyed*/);
                            if (RT_SUCCESS(rc))
                            {
                                *ppStream = &pStreamAt->Core;
                                *pCfgAcq  = pStreamAt->Core.Cfg;
                                return VINF_SUCCESS;
                            }

                            RTTestFailed(g_hTest, "pfnStreamInitAsync failed: %Rrc\n", rc);
                        }
                        else
                        {
                            RTTestFailed(g_hTest, "pfnStreamCreate returned unexpected info status: %Rrc", rc);
                            rc = VERR_IPE_UNEXPECTED_INFO_STATUS;
                        }
                        pDrvStack->pIHostAudio->pfnStreamDestroy(pDrvStack->pIHostAudio, &pStreamAt->Backend, true /*fImmediate*/);
                    }
                    /* else: Don't set RTTestFailed(...) here, as test boxes (servers) don't have any audio hardware.
                     *       Caller has check the rc then. */
                }
                else
                {
                    RTTestFailed(g_hTest, "Out of memory!\n");
                    rc = VERR_NO_MEMORY;
                }
                RTMemFree(pStreamAt);
            }
            else
            {
                RTTestFailed(g_hTest, "cbStream=%#x is too small, min %#zx!\n", BackendCfg.cbStream, sizeof(PDMAUDIOBACKENDSTREAM));
                rc = VERR_OUT_OF_RANGE;
            }
        }
        else
            RTTestFailed(g_hTest, "pfnGetConfig failed: %Rrc\n", rc);
    }
    return rc;
}


/**
 * Creates an output stream.
 *
 * @returns VBox status code.
 * @param   pDrvStack           The audio driver stack to create it via.
 * @param   pProps              The audio properties to use.
 * @param   cMsBufferSize       The buffer size in milliseconds.
 * @param   cMsPreBuffer        The pre-buffering amount in milliseconds.
 * @param   cMsSchedulingHint   The scheduling hint in milliseconds.
 * @param   ppStream            Where to return the stream pointer on success.
 * @param   pCfgAcq             Where to return the actual (well, not
 *                              necessarily when using DrvAudio, but probably
 *                              the same) stream config on success (not used as
 *                              input).
 */
int audioTestDriverStackStreamCreateOutput(PAUDIOTESTDRVSTACK pDrvStack, PCPDMAUDIOPCMPROPS pProps,
                                           uint32_t cMsBufferSize, uint32_t cMsPreBuffer, uint32_t cMsSchedulingHint,
                                           PPDMAUDIOSTREAM *ppStream, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    /*
     * Calculate the stream config.
     */
    PDMAUDIOSTREAMCFG CfgReq;
    int rc = PDMAudioStrmCfgInitWithProps(&CfgReq, pProps);
    AssertRC(rc);
    CfgReq.enmDir                       = PDMAUDIODIR_OUT;
    CfgReq.enmPath                      = PDMAUDIOPATH_OUT_FRONT;
    CfgReq.Device.cMsSchedulingHint     = cMsSchedulingHint == UINT32_MAX || cMsSchedulingHint == 0
                                        ? 10 : cMsSchedulingHint;
    if (pDrvStack->pIAudioConnector && (cMsBufferSize == UINT32_MAX || cMsBufferSize == 0))
        CfgReq.Backend.cFramesBufferSize = 0; /* DrvAudio picks the default */
    else
        CfgReq.Backend.cFramesBufferSize = PDMAudioPropsMilliToFrames(pProps,
                                                                      cMsBufferSize == UINT32_MAX || cMsBufferSize == 0
                                                                      ? 300 : cMsBufferSize);
    if (cMsPreBuffer == UINT32_MAX)
        CfgReq.Backend.cFramesPreBuffering = pDrvStack->pIAudioConnector ? UINT32_MAX /*DrvAudo picks the default */
                                           : CfgReq.Backend.cFramesBufferSize * 2 / 3;
    else
        CfgReq.Backend.cFramesPreBuffering = PDMAudioPropsMilliToFrames(pProps, cMsPreBuffer);
    if (   CfgReq.Backend.cFramesPreBuffering >= CfgReq.Backend.cFramesBufferSize + 16
        && !pDrvStack->pIAudioConnector /*DrvAudio deals with it*/ )
    {
        RTMsgWarning("Cannot pre-buffer %#x frames with only %#x frames of buffer!",
                     CfgReq.Backend.cFramesPreBuffering, CfgReq.Backend.cFramesBufferSize);
        CfgReq.Backend.cFramesPreBuffering = CfgReq.Backend.cFramesBufferSize > 16
            ? CfgReq.Backend.cFramesBufferSize - 16 : 0;
    }

    static uint32_t s_idxStream = 0;
    uint32_t const idxStream = s_idxStream++;
    RTStrPrintf(CfgReq.szName, sizeof(CfgReq.szName), "out-%u", idxStream);

    /*
     * Call common code to do the actual work.
     */
    return audioTestDriverStackStreamCreate(pDrvStack, &CfgReq, ppStream, pCfgAcq);
}


/**
 * Creates an input stream.
 *
 * @returns VBox status code.
 * @param   pDrvStack           The audio driver stack to create it via.
 * @param   pProps              The audio properties to use.
 * @param   cMsBufferSize       The buffer size in milliseconds.
 * @param   cMsPreBuffer        The pre-buffering amount in milliseconds.
 * @param   cMsSchedulingHint   The scheduling hint in milliseconds.
 * @param   ppStream            Where to return the stream pointer on success.
 * @param   pCfgAcq             Where to return the actual (well, not
 *                              necessarily when using DrvAudio, but probably
 *                              the same) stream config on success (not used as
 *                              input).
 */
int audioTestDriverStackStreamCreateInput(PAUDIOTESTDRVSTACK pDrvStack, PCPDMAUDIOPCMPROPS pProps,
                                          uint32_t cMsBufferSize, uint32_t cMsPreBuffer, uint32_t cMsSchedulingHint,
                                          PPDMAUDIOSTREAM *ppStream, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    /*
     * Calculate the stream config.
     */
    PDMAUDIOSTREAMCFG CfgReq;
    int rc = PDMAudioStrmCfgInitWithProps(&CfgReq, pProps);
    AssertRC(rc);
    CfgReq.enmDir                       = PDMAUDIODIR_IN;
    CfgReq.enmPath                      = PDMAUDIOPATH_IN_LINE;
    CfgReq.Device.cMsSchedulingHint     = cMsSchedulingHint == UINT32_MAX || cMsSchedulingHint == 0
                                        ? 10 : cMsSchedulingHint;
    if (pDrvStack->pIAudioConnector && (cMsBufferSize == UINT32_MAX || cMsBufferSize == 0))
        CfgReq.Backend.cFramesBufferSize = 0; /* DrvAudio picks the default */
    else
        CfgReq.Backend.cFramesBufferSize = PDMAudioPropsMilliToFrames(pProps,
                                                                      cMsBufferSize == UINT32_MAX || cMsBufferSize == 0
                                                                      ? 300 : cMsBufferSize);
    if (cMsPreBuffer == UINT32_MAX)
        CfgReq.Backend.cFramesPreBuffering = pDrvStack->pIAudioConnector ? UINT32_MAX /*DrvAudio picks the default */
                                           : CfgReq.Backend.cFramesBufferSize / 2;
    else
        CfgReq.Backend.cFramesPreBuffering = PDMAudioPropsMilliToFrames(pProps, cMsPreBuffer);
    if (   CfgReq.Backend.cFramesPreBuffering >= CfgReq.Backend.cFramesBufferSize + 16 /** @todo way to little */
        && !pDrvStack->pIAudioConnector /*DrvAudio deals with it*/ )
    {
        RTMsgWarning("Cannot pre-buffer %#x frames with only %#x frames of buffer!",
                     CfgReq.Backend.cFramesPreBuffering, CfgReq.Backend.cFramesBufferSize);
        CfgReq.Backend.cFramesPreBuffering = CfgReq.Backend.cFramesBufferSize > 16
            ? CfgReq.Backend.cFramesBufferSize - 16 : 0;
    }

    static uint32_t s_idxStream = 0;
    uint32_t const idxStream = s_idxStream++;
    RTStrPrintf(CfgReq.szName, sizeof(CfgReq.szName), "in-%u", idxStream);

    /*
     * Call common code to do the actual work.
     */
    return audioTestDriverStackStreamCreate(pDrvStack, &CfgReq, ppStream, pCfgAcq);
}


/**
 * Destroys a stream.
 *
 * @param   pDrvStack           Driver stack the stream to destroy is assigned to.
 * @param   pStream             Stream to destroy. Pointer will be NULL (invalid) after successful return.
 */
void audioTestDriverStackStreamDestroy(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream)
{
    if (!pStream)
        return;

    if (pDrvStack->pIAudioConnector)
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Destroying stream '%s' (IAudioConnector) ...\n", pStream->Cfg.szName);
        int rc = pDrvStack->pIAudioConnector->pfnStreamDestroy(pDrvStack->pIAudioConnector, pStream, true /*fImmediate*/);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "pfnStreamDestroy failed: %Rrc", rc);
    }
    else
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Destroying stream '%s' (IHostAudio) ...\n", pStream->Cfg.szName);
        PAUDIOTESTDRVSTACKSTREAM pStreamAt = (PAUDIOTESTDRVSTACKSTREAM)pStream;
        int rc = pDrvStack->pIHostAudio->pfnStreamDestroy(pDrvStack->pIHostAudio, &pStreamAt->Backend, true /*fImmediate*/);
        if (RT_SUCCESS(rc))
        {
            pStreamAt->Core.uMagic    = ~PDMAUDIOSTREAM_MAGIC;
            pStreamAt->Backend.uMagic = ~PDMAUDIOBACKENDSTREAM_MAGIC;

            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Destroying stream '%s' done\n", pStream->Cfg.szName);

            RTMemFree(pStreamAt);

            pStreamAt = NULL;
            pStream   = NULL;
        }
        else
            RTTestFailed(g_hTest, "PDMIHOSTAUDIO::pfnStreamDestroy failed: %Rrc", rc);
    }
}


/**
 * Enables a stream.
 */
int audioTestDriverStackStreamEnable(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream)
{
    int rc;
    if (pDrvStack->pIAudioConnector)
    {
        rc = pDrvStack->pIAudioConnector->pfnStreamControl(pDrvStack->pIAudioConnector, pStream, PDMAUDIOSTREAMCMD_ENABLE);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "pfnStreamControl/ENABLE failed: %Rrc", rc);
    }
    else
    {
        PAUDIOTESTDRVSTACKSTREAM pStreamAt = (PAUDIOTESTDRVSTACKSTREAM)pStream;
        rc = pDrvStack->pIHostAudio->pfnStreamEnable(pDrvStack->pIHostAudio, &pStreamAt->Backend);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "PDMIHOSTAUDIO::pfnStreamEnable failed: %Rrc", rc);
    }
    return rc;
}


/**
 * Disables a stream.
 */
int AudioTestDriverStackStreamDisable(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream)
{
    int rc;
    if (pDrvStack->pIAudioConnector)
    {
        rc = pDrvStack->pIAudioConnector->pfnStreamControl(pDrvStack->pIAudioConnector, pStream, PDMAUDIOSTREAMCMD_DISABLE);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "pfnStreamControl/DISABLE failed: %Rrc", rc);
    }
    else
    {
        PAUDIOTESTDRVSTACKSTREAM pStreamAt = (PAUDIOTESTDRVSTACKSTREAM)pStream;
        rc = pDrvStack->pIHostAudio->pfnStreamDisable(pDrvStack->pIHostAudio, &pStreamAt->Backend);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "PDMIHOSTAUDIO::pfnStreamDisable failed: %Rrc", rc);
    }
    return rc;
}


/**
 * Drains an output stream.
 */
int audioTestDriverStackStreamDrain(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream, bool fSync)
{
    int rc;
    if (pDrvStack->pIAudioConnector)
    {
        /*
         * Issue the drain request.
         */
        rc = pDrvStack->pIAudioConnector->pfnStreamControl(pDrvStack->pIAudioConnector, pStream, PDMAUDIOSTREAMCMD_DRAIN);
        if (RT_SUCCESS(rc) && fSync)
        {
            /*
             * This is a synchronous drain, so wait for the driver to change state to inactive.
             */
            PDMAUDIOSTREAMSTATE enmState;
            while (   (enmState = pDrvStack->pIAudioConnector->pfnStreamGetState(pDrvStack->pIAudioConnector, pStream))
                   >= PDMAUDIOSTREAMSTATE_ENABLED)
            {
                RTThreadSleep(2);
                rc = pDrvStack->pIAudioConnector->pfnStreamIterate(pDrvStack->pIAudioConnector, pStream);
                if (RT_FAILURE(rc))
                {
                    RTTestFailed(g_hTest, "pfnStreamIterate/DRAIN failed: %Rrc", rc);
                    break;
                }
            }
            if (enmState != PDMAUDIOSTREAMSTATE_INACTIVE)
            {
                RTTestFailed(g_hTest, "Stream state not INACTIVE after draining: %s", PDMAudioStreamStateGetName(enmState));
                rc = VERR_AUDIO_STREAM_NOT_READY;
            }
        }
        else if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "pfnStreamControl/ENABLE failed: %Rrc", rc);
    }
    else
    {
        /*
         * Issue the drain request.
         */
        PAUDIOTESTDRVSTACKSTREAM pStreamAt = (PAUDIOTESTDRVSTACKSTREAM)pStream;
        rc = pDrvStack->pIHostAudio->pfnStreamDrain(pDrvStack->pIHostAudio, &pStreamAt->Backend);
        if (RT_SUCCESS(rc) && fSync)
        {
            RTMSINTERVAL const msTimeout = RT_MS_5MIN; /* 5 minutes should be really enough for draining our stuff. */
            uint64_t const     tsStart   = RTTimeMilliTS();

            /*
             * This is a synchronous drain, so wait for the driver to change state to inactive.
             */
            PDMHOSTAUDIOSTREAMSTATE enmHostState;
            while (   (enmHostState = pDrvStack->pIHostAudio->pfnStreamGetState(pDrvStack->pIHostAudio, &pStreamAt->Backend))
                   == PDMHOSTAUDIOSTREAMSTATE_DRAINING)
            {
                RTThreadSleep(2);
                uint32_t cbWritten = UINT32_MAX;
                rc = pDrvStack->pIHostAudio->pfnStreamPlay(pDrvStack->pIHostAudio, &pStreamAt->Backend,
                                                           NULL /*pvBuf*/, 0 /*cbBuf*/, &cbWritten);
                if (RT_FAILURE(rc))
                {
                    RTTestFailed(g_hTest, "pfnStreamPlay/DRAIN failed: %Rrc", rc);
                    break;
                }
                if (cbWritten != 0)
                {
                    RTTestFailed(g_hTest, "pfnStreamPlay/DRAIN did not set cbWritten to zero: %#x", cbWritten);
                    rc = VERR_MISSING;
                    break;
                }

                /* Fail-safe for audio stacks and/or implementations which mess up draining.
                 *
                 * Note: On some testboxes draining never seems to finish and thus is getting aborted, no clue why.
                 *       The test result in the end still could be correct, although the actual draining problem
                 *       needs to be investigated further.
                 *
                 *       So don't make this (and the stream state check below) an error for now and just warn about it.
                 *
                 ** @todo Investigate draining issues on testboxes.
                 */
                if (RTTimeMilliTS() - tsStart > msTimeout)
                {
                    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                                 "Warning: Draining stream took too long (timeout is %RU32ms), giving up", msTimeout);
                    break;
                }
            }
            if (enmHostState != PDMHOSTAUDIOSTREAMSTATE_OKAY)
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                             "Warning: Stream state not OKAY after draining: %s", PDMHostAudioStreamStateGetName(enmHostState));
        }
        else if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "PDMIHOSTAUDIO::pfnStreamControl/ENABLE failed: %Rrc", rc);
    }
    return rc;
}


/**
 * Checks if the stream is okay.
 * @returns true if okay, false if not.
 */
bool audioTestDriverStackStreamIsOkay(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream)
{
    /*
     * Get the stream status and check if it means is okay or not.
     */
    bool fRc = false;
    if (pDrvStack->pIAudioConnector)
    {
        PDMAUDIOSTREAMSTATE enmState = pDrvStack->pIAudioConnector->pfnStreamGetState(pDrvStack->pIAudioConnector, pStream);
        switch (enmState)
        {
            case PDMAUDIOSTREAMSTATE_NOT_WORKING:
            case PDMAUDIOSTREAMSTATE_NEED_REINIT:
                break;
            case PDMAUDIOSTREAMSTATE_INACTIVE:
            case PDMAUDIOSTREAMSTATE_ENABLED:
            case PDMAUDIOSTREAMSTATE_ENABLED_READABLE:
            case PDMAUDIOSTREAMSTATE_ENABLED_WRITABLE:
                fRc = true;
                break;
            /* no default */
            case PDMAUDIOSTREAMSTATE_INVALID:
            case PDMAUDIOSTREAMSTATE_END:
            case PDMAUDIOSTREAMSTATE_32BIT_HACK:
                break;
        }
    }
    else
    {
        PAUDIOTESTDRVSTACKSTREAM pStreamAt    = (PAUDIOTESTDRVSTACKSTREAM)pStream;
        PDMHOSTAUDIOSTREAMSTATE  enmHostState = pDrvStack->pIHostAudio->pfnStreamGetState(pDrvStack->pIHostAudio,
                                                                                          &pStreamAt->Backend);
        switch (enmHostState)
        {
            case PDMHOSTAUDIOSTREAMSTATE_INITIALIZING:
            case PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING:
                break;
            case PDMHOSTAUDIOSTREAMSTATE_OKAY:
            case PDMHOSTAUDIOSTREAMSTATE_DRAINING:
            case PDMHOSTAUDIOSTREAMSTATE_INACTIVE:
                fRc = true;
                break;
            /* no default */
            case PDMHOSTAUDIOSTREAMSTATE_INVALID:
            case PDMHOSTAUDIOSTREAMSTATE_END:
            case PDMHOSTAUDIOSTREAMSTATE_32BIT_HACK:
                break;
        }
    }
    return fRc;
}


/**
 * Gets the number of bytes it's currently possible to write to the stream.
 */
uint32_t audioTestDriverStackStreamGetWritable(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream)
{
    uint32_t cbWritable;
    if (pDrvStack->pIAudioConnector)
        cbWritable = pDrvStack->pIAudioConnector->pfnStreamGetWritable(pDrvStack->pIAudioConnector, pStream);
    else
    {
        PAUDIOTESTDRVSTACKSTREAM pStreamAt = (PAUDIOTESTDRVSTACKSTREAM)pStream;
        cbWritable = pDrvStack->pIHostAudio->pfnStreamGetWritable(pDrvStack->pIHostAudio, &pStreamAt->Backend);
    }
    return cbWritable;
}


/**
 * Tries to play the @a cbBuf bytes of samples in @a pvBuf.
 */
int audioTestDriverStackStreamPlay(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream,
                                   void const *pvBuf, uint32_t cbBuf, uint32_t *pcbPlayed)
{
    int rc;
    if (pDrvStack->pIAudioConnector)
    {
        rc = pDrvStack->pIAudioConnector->pfnStreamPlay(pDrvStack->pIAudioConnector, pStream, pvBuf, cbBuf, pcbPlayed);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "pfnStreamPlay(,,,%#x,) failed: %Rrc", cbBuf, rc);
    }
    else
    {
        PAUDIOTESTDRVSTACKSTREAM pStreamAt = (PAUDIOTESTDRVSTACKSTREAM)pStream;
        rc = pDrvStack->pIHostAudio->pfnStreamPlay(pDrvStack->pIHostAudio, &pStreamAt->Backend, pvBuf, cbBuf, pcbPlayed);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "PDMIHOSTAUDIO::pfnStreamPlay(,,,%#x,) failed: %Rrc", cbBuf, rc);
    }
    return rc;
}


/**
 * Gets the number of bytes it's currently possible to write to the stream.
 */
uint32_t audioTestDriverStackStreamGetReadable(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream)
{
    uint32_t cbReadable;
    if (pDrvStack->pIAudioConnector)
        cbReadable = pDrvStack->pIAudioConnector->pfnStreamGetReadable(pDrvStack->pIAudioConnector, pStream);
    else
    {
        PAUDIOTESTDRVSTACKSTREAM pStreamAt = (PAUDIOTESTDRVSTACKSTREAM)pStream;
        cbReadable = pDrvStack->pIHostAudio->pfnStreamGetReadable(pDrvStack->pIHostAudio, &pStreamAt->Backend);
    }
    return cbReadable;
}


/**
 * Tries to capture @a cbBuf bytes of samples in @a pvBuf.
 */
int audioTestDriverStackStreamCapture(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream,
                                      void *pvBuf, uint32_t cbBuf, uint32_t *pcbCaptured)
{
    int rc;
    if (pDrvStack->pIAudioConnector)
    {
        rc = pDrvStack->pIAudioConnector->pfnStreamCapture(pDrvStack->pIAudioConnector, pStream, pvBuf, cbBuf, pcbCaptured);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "pfnStreamCapture(,,,%#x,) failed: %Rrc", cbBuf, rc);
    }
    else
    {
        PAUDIOTESTDRVSTACKSTREAM pStreamAt = (PAUDIOTESTDRVSTACKSTREAM)pStream;
        rc = pDrvStack->pIHostAudio->pfnStreamCapture(pDrvStack->pIHostAudio, &pStreamAt->Backend, pvBuf, cbBuf, pcbCaptured);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "PDMIHOSTAUDIO::pfnStreamCapture(,,,%#x,) failed: %Rrc", cbBuf, rc);
    }
    return rc;
}


/*********************************************************************************************************************************
*   Mixed streams                                                                                                                *
*********************************************************************************************************************************/

/**
 * Initializing mixing for a stream.
 *
 * This can be used as a do-nothing wrapper for the stack.
 *
 * @returns VBox status code.
 * @param   pMix        The mixing state.
 * @param   pStream     The stream to mix to/from.
 * @param   pProps      The mixer properties.  Pass NULL for no mixing, just
 *                      wrap the driver stack functionality.
 * @param   cMsBuffer   The buffer size.
 */
int AudioTestMixStreamInit(PAUDIOTESTDRVMIXSTREAM pMix, PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream,
                           PCPDMAUDIOPCMPROPS pProps, uint32_t cMsBuffer)
{
    RT_ZERO(*pMix);

    AssertReturn(pDrvStack, VERR_INVALID_PARAMETER);
    AssertReturn(pStream, VERR_INVALID_PARAMETER);

    pMix->pDrvStack = pDrvStack;
    pMix->pStream   = pStream;
    if (!pProps)
    {
        pMix->pProps = &pStream->Cfg.Props;
        return VINF_SUCCESS;
    }

    /*
     * Okay, we're doing mixing so we need to set up the mixer buffer
     * and associated states.
     */
    pMix->fDoMixing = true;
    int rc = AudioMixBufInit(&pMix->MixBuf, "mixer", pProps, PDMAudioPropsMilliToFrames(pProps, cMsBuffer));
    if (RT_SUCCESS(rc))
    {
        pMix->pProps = &pMix->MixBuf.Props;

        if (pStream->Cfg.enmDir == PDMAUDIODIR_IN)
        {
            rc = AudioMixBufInitPeekState(&pMix->MixBuf, &pMix->PeekState, &pMix->MixBuf.Props);
            if (RT_SUCCESS(rc))
            {
                rc = AudioMixBufInitWriteState(&pMix->MixBuf, &pMix->WriteState, &pStream->Cfg.Props);
                if (RT_SUCCESS(rc))
                    return rc;
            }
        }
        else if (pStream->Cfg.enmDir == PDMAUDIODIR_OUT)
        {
            rc = AudioMixBufInitWriteState(&pMix->MixBuf, &pMix->WriteState, &pMix->MixBuf.Props);
            if (RT_SUCCESS(rc))
            {
                rc = AudioMixBufInitPeekState(&pMix->MixBuf, &pMix->PeekState, &pStream->Cfg.Props);
                if (RT_SUCCESS(rc))
                    return rc;
            }
        }
        else
        {
            RTTestFailed(g_hTest, "Bogus stream direction!");
            rc = VERR_INVALID_STATE;
        }
    }
    else
        RTTestFailed(g_hTest, "AudioMixBufInit failed: %Rrc", rc);
    RT_ZERO(*pMix);
    return rc;
}


/**
 * Terminate mixing (leaves the stream untouched).
 *
 * @param   pMix        The mixing state.
 */
void AudioTestMixStreamTerm(PAUDIOTESTDRVMIXSTREAM pMix)
{
    if (pMix->fDoMixing)
    {
        AudioMixBufTerm(&pMix->MixBuf);
        pMix->pStream = NULL;
    }
    RT_ZERO(*pMix);
}


/**
 * Worker that transports data between the mixer buffer and the drivers.
 *
 * @returns VBox status code.
 * @param   pMix    The mixer stream setup to do transfers for.
 */
static int audioTestMixStreamTransfer(PAUDIOTESTDRVMIXSTREAM pMix)
{
    uint8_t abBuf[16384];
    if (pMix->pStream->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        /*
         * Try fill up the mixer buffer as much as possible.
         *
         * Slight fun part is that we have to calculate conversion
         * ratio and be rather pessimistic about it.
         */
        uint32_t const cbBuf = PDMAudioPropsFloorBytesToFrame(&pMix->pStream->Cfg.Props, sizeof(abBuf));
        for (;;)
        {
            /*
             * Figure out how much we can move in this iteration.
             */
            uint32_t cDstFrames = AudioMixBufFree(&pMix->MixBuf);
            if (!cDstFrames)
                break;

            uint32_t cbReadable = audioTestDriverStackStreamGetReadable(pMix->pDrvStack, pMix->pStream);
            if (!cbReadable)
                break;

            uint32_t cbToRead;
            if (PDMAudioPropsHz(&pMix->pStream->Cfg.Props) == PDMAudioPropsHz(&pMix->MixBuf.Props))
                cbToRead = PDMAudioPropsFramesToBytes(&pMix->pStream->Cfg.Props, cDstFrames);
            else
                cbToRead = PDMAudioPropsFramesToBytes(&pMix->pStream->Cfg.Props,
                                                        cDstFrames * PDMAudioPropsHz(&pMix->pStream->Cfg.Props)
                                                      / PDMAudioPropsHz(&pMix->MixBuf.Props));
            cbToRead = RT_MIN(cbToRead, RT_MIN(cbReadable, cbBuf));
            if (!cbToRead)
                break;

            /*
             * Get the data.
             */
            uint32_t cbCaptured = 0;
            int rc = audioTestDriverStackStreamCapture(pMix->pDrvStack, pMix->pStream, abBuf, cbToRead, &cbCaptured);
            if (RT_FAILURE(rc))
                return rc;
            Assert(cbCaptured == cbToRead);
            AssertBreak(cbCaptured > 0);

            /*
             * Feed it to the mixer.
             */
            uint32_t cDstFramesWritten = 0;
            if ((abBuf[0] >> 4) & 1) /* some cheap random */
                AudioMixBufWrite(&pMix->MixBuf, &pMix->WriteState, abBuf, cbCaptured,
                                 0 /*offDstFrame*/, cDstFrames, &cDstFramesWritten);
            else
            {
                AudioMixBufSilence(&pMix->MixBuf, &pMix->WriteState, 0 /*offFrame*/, cDstFrames);
                AudioMixBufBlend(&pMix->MixBuf, &pMix->WriteState, abBuf, cbCaptured,
                                 0 /*offDstFrame*/, cDstFrames, &cDstFramesWritten);
            }
            AudioMixBufCommit(&pMix->MixBuf, cDstFramesWritten);
        }
    }
    else
    {
        /*
         * The goal here is to empty the mixer buffer by transfering all
         * the data to the drivers.
         */
        uint32_t const cbBuf = PDMAudioPropsFloorBytesToFrame(&pMix->MixBuf.Props, sizeof(abBuf));
        for (;;)
        {
            uint32_t cFrames = AudioMixBufUsed(&pMix->MixBuf);
            if (!cFrames)
                break;

            uint32_t cbWritable = audioTestDriverStackStreamGetWritable(pMix->pDrvStack, pMix->pStream);
            if (!cbWritable)
                break;

            uint32_t cSrcFramesPeeked;
            uint32_t cbDstPeeked;
            AudioMixBufPeek(&pMix->MixBuf, 0 /*offSrcFrame*/, cFrames, &cSrcFramesPeeked,
                            &pMix->PeekState, abBuf, RT_MIN(cbBuf, cbWritable), &cbDstPeeked);
            AudioMixBufAdvance(&pMix->MixBuf, cSrcFramesPeeked);

            if (!cbDstPeeked)
                break;

            uint32_t offBuf = 0;
            while (offBuf < cbDstPeeked)
            {
                uint32_t cbPlayed = 0;
                int rc = audioTestDriverStackStreamPlay(pMix->pDrvStack, pMix->pStream,
                                                        &abBuf[offBuf], cbDstPeeked - offBuf, &cbPlayed);
                if (RT_FAILURE(rc))
                    return rc;
                if (!cbPlayed)
                    RTThreadSleep(1);
                offBuf += cbPlayed;
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * Same as audioTestDriverStackStreamEnable.
 */
int AudioTestMixStreamEnable(PAUDIOTESTDRVMIXSTREAM pMix)
{
    return audioTestDriverStackStreamEnable(pMix->pDrvStack, pMix->pStream);
}


/**
 * Same as audioTestDriverStackStreamDrain.
 */
int AudioTestMixStreamDrain(PAUDIOTESTDRVMIXSTREAM pMix, bool fSync)
{
    /*
     * If we're mixing, we must first make sure the buffer is empty.
     */
    if (pMix->fDoMixing)
    {
        audioTestMixStreamTransfer(pMix);
        while (AudioMixBufUsed(&pMix->MixBuf) > 0)
        {
            RTThreadSleep(1);
            audioTestMixStreamTransfer(pMix);
        }
    }

    /*
     * Then we do the regular work.
     */
    return audioTestDriverStackStreamDrain(pMix->pDrvStack, pMix->pStream, fSync);
}

/**
 * Same as audioTestDriverStackStreamDisable.
 */
int AudioTestMixStreamDisable(PAUDIOTESTDRVMIXSTREAM pMix)
{
    return AudioTestDriverStackStreamDisable(pMix->pDrvStack, pMix->pStream);
}


/**
 * Same as audioTestDriverStackStreamIsOkay.
 */
bool AudioTestMixStreamIsOkay(PAUDIOTESTDRVMIXSTREAM pMix)
{
    return audioTestDriverStackStreamIsOkay(pMix->pDrvStack, pMix->pStream);
}


/**
 * Same as audioTestDriverStackStreamGetWritable
 */
uint32_t AudioTestMixStreamGetWritable(PAUDIOTESTDRVMIXSTREAM pMix)
{
    if (!pMix->fDoMixing)
        return audioTestDriverStackStreamGetWritable(pMix->pDrvStack, pMix->pStream);
    uint32_t cbRet = AudioMixBufFreeBytes(&pMix->MixBuf);
    if (!cbRet)
    {
        audioTestMixStreamTransfer(pMix);
        cbRet = AudioMixBufFreeBytes(&pMix->MixBuf);
    }
    return cbRet;
}




/**
 * Same as audioTestDriverStackStreamPlay.
 */
int AudioTestMixStreamPlay(PAUDIOTESTDRVMIXSTREAM pMix, void const *pvBuf, uint32_t cbBuf, uint32_t *pcbPlayed)
{
    if (!pMix->fDoMixing)
        return audioTestDriverStackStreamPlay(pMix->pDrvStack, pMix->pStream, pvBuf, cbBuf, pcbPlayed);

    *pcbPlayed = 0;

    int rc = audioTestMixStreamTransfer(pMix);
    if (RT_FAILURE(rc))
        return rc;

    uint32_t const cbFrame = PDMAudioPropsFrameSize(&pMix->MixBuf.Props);
    while (cbBuf >= cbFrame)
    {
        uint32_t const  cFrames = AudioMixBufFree(&pMix->MixBuf);
        if (!cFrames)
            break;
        uint32_t cbToWrite = PDMAudioPropsFramesToBytes(&pMix->MixBuf.Props, cFrames);
        cbToWrite = RT_MIN(cbToWrite, cbBuf);
        cbToWrite = PDMAudioPropsFloorBytesToFrame(&pMix->MixBuf.Props, cbToWrite);

        uint32_t cFramesWritten = 0;
        AudioMixBufWrite(&pMix->MixBuf, &pMix->WriteState, pvBuf, cbToWrite, 0 /*offDstFrame*/, cFrames, &cFramesWritten);
        Assert(cFramesWritten == PDMAudioPropsBytesToFrames(&pMix->MixBuf.Props, cbToWrite));
        AudioMixBufCommit(&pMix->MixBuf, cFramesWritten);

        *pcbPlayed += cbToWrite;
        cbBuf      -= cbToWrite;
        pvBuf       = (uint8_t const *)pvBuf + cbToWrite;

        rc = audioTestMixStreamTransfer(pMix);
        if (RT_FAILURE(rc))
            return *pcbPlayed ? VINF_SUCCESS : rc;
    }

    return VINF_SUCCESS;
}


/**
 * Same as audioTestDriverStackStreamGetReadable
 */
uint32_t AudioTestMixStreamGetReadable(PAUDIOTESTDRVMIXSTREAM pMix)
{
    if (!pMix->fDoMixing)
        return audioTestDriverStackStreamGetReadable(pMix->pDrvStack, pMix->pStream);

    audioTestMixStreamTransfer(pMix);
    uint32_t cbRet = AudioMixBufUsedBytes(&pMix->MixBuf);
    return cbRet;
}




/**
 * Same as audioTestDriverStackStreamCapture.
 */
int AudioTestMixStreamCapture(PAUDIOTESTDRVMIXSTREAM pMix, void *pvBuf, uint32_t cbBuf, uint32_t *pcbCaptured)
{
    if (!pMix->fDoMixing)
        return audioTestDriverStackStreamCapture(pMix->pDrvStack, pMix->pStream, pvBuf, cbBuf, pcbCaptured);

    *pcbCaptured = 0;

    int rc = audioTestMixStreamTransfer(pMix);
    if (RT_FAILURE(rc))
        return rc;

    uint32_t const cbFrame = PDMAudioPropsFrameSize(&pMix->MixBuf.Props);
    while (cbBuf >= cbFrame)
    {
        uint32_t const cFrames = AudioMixBufUsed(&pMix->MixBuf);
        if (!cFrames)
            break;
        uint32_t cbToRead = PDMAudioPropsFramesToBytes(&pMix->MixBuf.Props, cFrames);
        cbToRead = RT_MIN(cbToRead, cbBuf);
        cbToRead = PDMAudioPropsFloorBytesToFrame(&pMix->MixBuf.Props, cbToRead);

        uint32_t cFramesPeeked = 0;
        uint32_t cbPeeked      = 0;
        AudioMixBufPeek(&pMix->MixBuf, 0 /*offSrcFrame*/, cFrames, &cFramesPeeked, &pMix->PeekState, pvBuf, cbToRead, &cbPeeked);
        Assert(cFramesPeeked == PDMAudioPropsBytesToFrames(&pMix->MixBuf.Props, cbPeeked));
        AudioMixBufAdvance(&pMix->MixBuf, cFramesPeeked);

        *pcbCaptured += cbToRead;
        cbBuf        -= cbToRead;
        pvBuf         = (uint8_t *)pvBuf + cbToRead;

        rc = audioTestMixStreamTransfer(pMix);
        if (RT_FAILURE(rc))
            return *pcbCaptured ? VINF_SUCCESS : rc;
    }

    return VINF_SUCCESS;
}

/**
 * Sets the volume of a mixing stream.
 *
 * @param   pMix                Mixing stream to set volume for.
 * @param   uVolumePercent      Volume to set (in percent, 0-100).
 */
void AudioTestMixStreamSetVolume(PAUDIOTESTDRVMIXSTREAM pMix, uint8_t uVolumePercent)
{
    AssertReturnVoid(pMix->fDoMixing);

    uint8_t const uVol = (PDMAUDIO_VOLUME_MAX / 100) * uVolumePercent;

    PDMAUDIOVOLUME Vol;
    RT_ZERO(Vol);
    for (size_t i = 0; i < RT_ELEMENTS(Vol.auChannels); i++)
        Vol.auChannels[i] = uVol;
    AudioMixBufSetVolume(&pMix->MixBuf, &Vol);
}

