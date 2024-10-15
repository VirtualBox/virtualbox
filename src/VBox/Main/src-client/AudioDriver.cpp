/* $Id: AudioDriver.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox audio base class for Main audio drivers.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include "LoggingNew.h"

#include <VBox/log.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/vmmr3vtable.h>

#include "AudioDriver.h"
#include "ConsoleImpl.h"

AudioDriver::AudioDriver(Console *pConsole)
    : mpConsole(pConsole)
    , mfAttached(false)
{
}


AudioDriver::~AudioDriver(void)
{
}


AudioDriver &AudioDriver::operator=(AudioDriver const &a_rThat) RT_NOEXCEPT
{
    mpConsole  = a_rThat.mpConsole;
    mCfg       = a_rThat.mCfg;
    mfAttached = a_rThat.mfAttached;

    return *this;
}


/**
 * Initializes the audio driver with a certain (device) configuration.
 *
 * @returns VBox status code.
 * @param   pCfg                Audio driver configuration to use.
 */
int AudioDriver::InitializeConfig(AudioDriverCfg *pCfg)
{
    AssertPtrReturn(pCfg, VERR_INVALID_POINTER);

    /* Sanity. */
    AssertReturn(pCfg->strDev.isNotEmpty(),  VERR_INVALID_PARAMETER);
    AssertReturn(pCfg->uLUN != UINT8_MAX,    VERR_INVALID_PARAMETER);
    AssertReturn(pCfg->strName.isNotEmpty(), VERR_INVALID_PARAMETER);

    /* Apply configuration. */
    mCfg = *pCfg;

    return VINF_SUCCESS;
}


/**
 * Attaches the driver via EMT, if configured.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle for talking to EMT.
 * @param   pVMM        The VMM ring-3 vtable.
 * @param   pAutoLock   The callers auto lock instance.  Can be NULL if not
 *                      locked.
 */
int AudioDriver::doAttachDriverViaEmt(PUVM pUVM, PCVMMR3VTABLE pVMM, util::AutoWriteLock *pAutoLock)
{
    if (!isConfigured())
        return VINF_SUCCESS;

    PVMREQ pReq;
    int vrc = pVMM->pfnVMR3ReqCallU(pUVM, VMCPUID_ANY, &pReq, 0 /* no wait! */, VMREQFLAGS_VBOX_STATUS,
                                    (PFNRT)attachDriverOnEmt, 1, this);
    if (vrc == VERR_TIMEOUT)
    {
        /* Release the lock before a blocking VMR3* call (EMT might wait for it, @bugref{7648})! */
        if (pAutoLock)
            pAutoLock->release();

        vrc = pVMM->pfnVMR3ReqWait(pReq, RT_INDEFINITE_WAIT);

        if (pAutoLock)
            pAutoLock->acquire();
    }

    AssertRC(vrc);
    pVMM->pfnVMR3ReqFree(pReq);

    return vrc;
}


/**
 * Configures the audio driver (to CFGM) and attaches it to the audio chain.
 * Does nothing if the audio driver already is attached.
 *
 * @returns VBox status code.
 * @param   pThis               Audio driver to detach.
 */
/* static */
DECLCALLBACK(int) AudioDriver::attachDriverOnEmt(AudioDriver *pThis)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    Console::SafeVMPtrQuiet ptrVM(pThis->mpConsole);
    Assert(ptrVM.isOk());

    if (pThis->mfAttached) /* Already attached? Bail out. */
    {
        LogFunc(("%s: Already attached\n", pThis->mCfg.strName.c_str()));
        return VINF_SUCCESS;
    }

    AudioDriverCfg *pCfg = &pThis->mCfg;

    LogFunc(("strName=%s, strDevice=%s, uInst=%u, uLUN=%u\n",
             pCfg->strName.c_str(), pCfg->strDev.c_str(), pCfg->uInst, pCfg->uLUN));

    /* Detach the driver chain from the audio device first. */
    int vrc = ptrVM.vtable()->pfnPDMR3DeviceDetach(ptrVM.rawUVM(), pCfg->strDev.c_str(), pCfg->uInst, pCfg->uLUN, 0 /* fFlags */);
    if (RT_SUCCESS(vrc))
    {
        vrc = pThis->configure(pCfg->uLUN, true /* Attach */);
        if (RT_SUCCESS(vrc))
            vrc = ptrVM.vtable()->pfnPDMR3DriverAttach(ptrVM.rawUVM(), pCfg->strDev.c_str(), pCfg->uInst, pCfg->uLUN,
                                                       0 /* fFlags */, NULL /* ppBase */);
    }

    if (RT_SUCCESS(vrc))
    {
        pThis->mfAttached = true;
        LogRel2(("%s: Driver attached (LUN #%u)\n", pCfg->strName.c_str(), pCfg->uLUN));
    }
    else
        LogRel(("%s: Failed to attach audio driver, vrc=%Rrc\n", pCfg->strName.c_str(), vrc));

    LogFunc(("Returning %Rrc\n", vrc));
    return vrc;
}


/**
 * Detatches the driver via EMT, if configured.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle for talking to EMT.
 * @param   pVMM        The VMM ring-3 vtable.
 * @param   pAutoLock   The callers auto lock instance.  Can be NULL if not
 *                      locked.
 */
int AudioDriver::doDetachDriverViaEmt(PUVM pUVM, PCVMMR3VTABLE pVMM, util::AutoWriteLock *pAutoLock)
{
    if (!isConfigured())
        return VINF_SUCCESS;

    PVMREQ pReq;
    int vrc = pVMM->pfnVMR3ReqCallU(pUVM, VMCPUID_ANY, &pReq, 0 /* no wait! */, VMREQFLAGS_VBOX_STATUS,
                                    (PFNRT)detachDriverOnEmt, 1, this);
    if (vrc == VERR_TIMEOUT)
    {
        /* Release the lock before a blocking VMR3* call (EMT might wait for it, @bugref{7648})! */
        if (pAutoLock)
            pAutoLock->release();

        vrc = pVMM->pfnVMR3ReqWait(pReq, RT_INDEFINITE_WAIT);

        if (pAutoLock)
            pAutoLock->acquire();
    }

    AssertRC(vrc);
    pVMM->pfnVMR3ReqFree(pReq);

    return vrc;
}


/**
 * Detaches an already attached audio driver from the audio chain.
 * Does nothing if the audio driver already is detached or not attached.
 *
 * @returns VBox status code.
 * @param   pThis               Audio driver to detach.
 */
/* static */
DECLCALLBACK(int) AudioDriver::detachDriverOnEmt(AudioDriver *pThis)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    if (!pThis->mfAttached) /* Not attached? Bail out. */
    {
        LogFunc(("%s: Not attached\n", pThis->mCfg.strName.c_str()));
        return VINF_SUCCESS;
    }

    Console::SafeVMPtrQuiet ptrVM(pThis->mpConsole);
    Assert(ptrVM.isOk());

    AudioDriverCfg *pCfg = &pThis->mCfg;

    Assert(pCfg->uLUN != UINT8_MAX);

    LogFunc(("strName=%s, strDevice=%s, uInst=%u, uLUN=%u\n",
             pCfg->strName.c_str(), pCfg->strDev.c_str(), pCfg->uInst, pCfg->uLUN));

    /* Destroy the entire driver chain for the specified LUN.
     *
     * Start with the "AUDIO" driver, as this driver serves as the audio connector between
     * the device emulation and the select backend(s). */
    int vrc = ptrVM.vtable()->pfnPDMR3DriverDetach(ptrVM.rawUVM(), pCfg->strDev.c_str(), pCfg->uInst, pCfg->uLUN,
                                                   "AUDIO", 0 /* iOccurrence */,  0 /* fFlags */);
    if (RT_SUCCESS(vrc))
        vrc = pThis->configure(pCfg->uLUN, false /* Detach */);/** @todo r=bird: Illogical and from what I can tell pointless! */

    if (RT_SUCCESS(vrc))
    {
        pThis->mfAttached = false;
        LogRel2(("%s: Driver detached\n", pCfg->strName.c_str()));
    }
    else
        LogRel(("%s: Failed to detach audio driver, vrc=%Rrc\n", pCfg->strName.c_str(), vrc));

    LogFunc(("Returning %Rrc\n", vrc));
    return vrc;
}

/**
 * Configures the audio driver via CFGM.
 *
 * @returns VBox status code.
 * @param   uLUN                LUN to attach driver to.
 * @param   fAttach             Whether to attach or detach the driver configuration to CFGM.
 *
 * @thread EMT
 */
int AudioDriver::configure(unsigned uLUN, bool fAttach)
{
    Console::SafeVMPtrQuiet ptrVM(mpConsole);
    AssertReturn(ptrVM.isOk(), VERR_INVALID_STATE);

    PCFGMNODE pRoot = ptrVM.vtable()->pfnCFGMR3GetRootU(ptrVM.rawUVM());
    AssertPtr(pRoot);
    PCFGMNODE pDev0 = ptrVM.vtable()->pfnCFGMR3GetChildF(pRoot, "Devices/%s/%u/", mCfg.strDev.c_str(), mCfg.uInst);

    if (!pDev0) /* No audio device configured? Bail out. */
    {
        LogRel2(("%s: No audio device configured, skipping to attach driver\n", mCfg.strName.c_str()));
        return VINF_SUCCESS;
    }

    int vrc = VINF_SUCCESS;

    PCFGMNODE pDevLun = ptrVM.vtable()->pfnCFGMR3GetChildF(pDev0, "LUN#%u/", uLUN);

    if (fAttach)
    {
        do  /* break "loop" */
        {
            AssertMsgBreakStmt(pDevLun, ("%s: Device LUN #%u not found\n", mCfg.strName.c_str(), uLUN), vrc = VERR_NOT_FOUND);

            LogRel2(("%s: Configuring audio driver (to LUN #%u)\n", mCfg.strName.c_str(), uLUN));

            ptrVM.vtable()->pfnCFGMR3RemoveNode(pDevLun); /* Remove LUN completely first. */

            /* Insert new LUN configuration and build up the new driver chain. */
            vrc = ptrVM.vtable()->pfnCFGMR3InsertNodeF(pDev0, &pDevLun, "LUN#%u/", uLUN);                        AssertRCBreak(vrc);
            vrc = ptrVM.vtable()->pfnCFGMR3InsertString(pDevLun, "Driver", "AUDIO");                             AssertRCBreak(vrc);

            PCFGMNODE pLunCfg;
            vrc = ptrVM.vtable()->pfnCFGMR3InsertNode(pDevLun, "Config", &pLunCfg);                              AssertRCBreak(vrc);

            vrc = ptrVM.vtable()->pfnCFGMR3InsertStringF(pLunCfg, "DriverName",    "%s", mCfg.strName.c_str());  AssertRCBreak(vrc);
            vrc = ptrVM.vtable()->pfnCFGMR3InsertInteger(pLunCfg, "InputEnabled",  mCfg.fEnabledIn);             AssertRCBreak(vrc);
            vrc = ptrVM.vtable()->pfnCFGMR3InsertInteger(pLunCfg, "OutputEnabled", mCfg.fEnabledOut);            AssertRCBreak(vrc);

            PCFGMNODE pAttachedDriver;
            vrc = ptrVM.vtable()->pfnCFGMR3InsertNode(pDevLun, "AttachedDriver", &pAttachedDriver);              AssertRCBreak(vrc);
            vrc = ptrVM.vtable()->pfnCFGMR3InsertStringF(pAttachedDriver, "Driver", "%s", mCfg.strName.c_str()); AssertRCBreak(vrc);
            PCFGMNODE pAttachedDriverCfg;
            vrc = ptrVM.vtable()->pfnCFGMR3InsertNode(pAttachedDriver, "Config", &pAttachedDriverCfg);           AssertRCBreak(vrc);

            /* Call the (virtual) method for driver-specific configuration. */
            vrc = configureDriver(pAttachedDriverCfg, ptrVM.vtable());                                           AssertRCBreak(vrc);

        } while (0);
    }
    else /* Detach */
    {
        LogRel2(("%s: Unconfiguring audio driver\n", mCfg.strName.c_str()));
    }

    if (RT_SUCCESS(vrc))
    {
#ifdef LOG_ENABLED
        LogFunc(("%s: fAttach=%RTbool\n", mCfg.strName.c_str(), fAttach));
        ptrVM.vtable()->pfnCFGMR3Dump(pDevLun);
#endif
    }
    else
        LogRel(("%s: %s audio driver failed with vrc=%Rrc\n", mCfg.strName.c_str(), fAttach ? "Configuring" : "Unconfiguring", vrc));

    LogFunc(("Returning %Rrc\n", vrc));
    return vrc;
}

