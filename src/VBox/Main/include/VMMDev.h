/* $Id: VMMDev.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Driver interface to VMM device
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

#ifndef MAIN_INCLUDED_VMMDev_h
#define MAIN_INCLUDED_VMMDev_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VirtualBoxBase.h"
#include <VBox/vmm/pdmdrv.h>
#include <VBox/hgcmsvc.h>
#include <iprt/asm.h>

class Console;

class VMMDevMouseInterface
{
public:
    virtual ~VMMDevMouseInterface() { /* Make VC++ 19.2 happy. */ }
    virtual PPDMIVMMDEVPORT getVMMDevPort() = 0;
};

class VMMDev : public VMMDevMouseInterface
{
public:
    VMMDev(Console *console);
    virtual ~VMMDev();
    static const PDMDRVREG  DrvReg;
    /** Pointer to the associated VMMDev driver. */
    struct DRVMAINVMMDEV *mpDrv;

    bool fSharedFolderActive;
    bool isShFlActive()
    {
        return fSharedFolderActive;
    }

    Console *getParent()
    {
        return mParent;
    }

    int WaitCredentialsJudgement (uint32_t u32Timeout, uint32_t *pu32GuestFlags);
    int SetCredentialsJudgementResult (uint32_t u32Flags);

    PPDMIVMMDEVPORT getVMMDevPort();

#ifdef VBOX_WITH_HGCM
    int hgcmLoadService (const char *pszServiceLibrary, const char *pszServiceName);
    int hgcmHostCall (const char *pszServiceName, uint32_t u32Function, uint32_t cParms, PVBOXHGCMSVCPARM paParms);
    void hgcmShutdown(bool fUvmIsInvalid = false);

    bool hgcmIsActive (void) { return ASMAtomicReadBool(&m_fHGCMActive); }
#endif /* VBOX_WITH_HGCM */

private:
#ifdef VBOX_WITH_HGCM
# ifdef VBOX_WITH_GUEST_PROPS
    void i_guestPropSetMultiple(void *names, void *values, void *timestamps, void *flags);
    void i_guestPropSet(const char *pszName, const char *pszValue, const char *pszFlags);
    int  i_guestPropSetGlobalPropertyFlags(uint32_t fFlags);
    int  i_guestPropLoadAndConfigure();
# endif
#endif
    static DECLCALLBACK(void *) drvQueryInterface(PPDMIBASE pInterface, const char *pszIID);
    static DECLCALLBACK(int)    drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
    static DECLCALLBACK(void)   drvDestruct(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(void)   drvReset(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(void)   drvPowerOn(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(void)   drvPowerOff(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(void)   drvSuspend(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(void)   drvResume(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(int)    hgcmSave(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM);
    static DECLCALLBACK(int)    hgcmLoad(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);

    Console * const         mParent;

    RTSEMEVENT mCredentialsEvent;
    uint32_t mu32CredentialsFlags;

#ifdef VBOX_WITH_HGCM
    bool volatile m_fHGCMActive;
#endif /* VBOX_WITH_HGCM */
};

/** VMMDev object ID used by Console::i_vmm2User_QueryGenericObject and VMMDev::drvConstruct. */
#define VMMDEV_OID                          "e2ff0c7b-c02b-46d0-aa90-b9caf0f60561"

#endif /* !MAIN_INCLUDED_VMMDev_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
