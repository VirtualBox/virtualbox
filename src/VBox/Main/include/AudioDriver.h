/* $Id: AudioDriver.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef MAIN_INCLUDED_AudioDriver_h
#define MAIN_INCLUDED_AudioDriver_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/ptr.h>
#include <VBox/com/string.h>
#include <VBox/com/AutoLock.h>

using namespace com;

/**
 * Audio driver configuration for audio drivers implemented
 * in Main.
 */
struct AudioDriverCfg
{
    AudioDriverCfg(Utf8Str a_strDev = "", unsigned a_uInst = 0, unsigned a_uLUN = 0, Utf8Str a_strName = "",
                   bool a_fEnabledIn = false, bool a_fEnabledOut = false)
        : strDev(a_strDev)
        , uInst(a_uInst)
        , uLUN(a_uLUN)
        , strName(a_strName)
        , fEnabledIn(a_fEnabledIn)
        , fEnabledOut(a_fEnabledOut)
    { }

    /** Copy assignment operator. */
    AudioDriverCfg& operator=(AudioDriverCfg const &a_rThat) RT_NOEXCEPT
    {
        this->strDev      = a_rThat.strDev;
        this->uInst       = a_rThat.uInst;
        this->uLUN        = a_rThat.uLUN;
        this->strName     = a_rThat.strName;
        this->fEnabledIn  = a_rThat.fEnabledIn;
        this->fEnabledOut = a_rThat.fEnabledOut;

        return *this;
    }

    /** The device name. */
    Utf8Str             strDev;
    /** The device instance. */
    unsigned            uInst;
    /** The LUN the driver is attached to.
     *  Set the UINT8_MAX if not attached. */
    unsigned            uLUN;
    /** The driver name. */
    Utf8Str             strName;
    /** Whether input is enabled. */
    bool                fEnabledIn;
    /** Whether output is enabled. */
    bool                fEnabledOut;
};

class Console;

/**
 * Base class for all audio drivers implemented in Main.
 */
class AudioDriver
{

public:
    AudioDriver(Console *pConsole);
    virtual ~AudioDriver();

    /** Copy assignment operator. */
    AudioDriver &operator=(AudioDriver const &a_rThat) RT_NOEXCEPT;

    Console *GetParent(void) { return mpConsole; }

    AudioDriverCfg *GetConfig(void) { return &mCfg; }
    int InitializeConfig(AudioDriverCfg *pCfg);

    /** Checks if audio is configured or not. */
    bool isConfigured() const { return mCfg.strName.isNotEmpty(); }

    bool IsAttached(void) { return mfAttached; }

    int doAttachDriverViaEmt(PUVM pUVM, PCVMMR3VTABLE pVMM, util::AutoWriteLock *pAutoLock);
    int doDetachDriverViaEmt(PUVM pUVM, PCVMMR3VTABLE pVMM, util::AutoWriteLock *pAutoLock);

protected:
    static DECLCALLBACK(int) attachDriverOnEmt(AudioDriver *pThis);
    static DECLCALLBACK(int) detachDriverOnEmt(AudioDriver *pThis);

    int configure(unsigned uLUN, bool fAttach);

    /**
     * Virtual function for child specific driver configuration.
     *
     * This is called at the end of AudioDriver::configure().
     *
     * @returns VBox status code.
     * @param   pLunCfg          CFGM configuration node of the driver.
     * @param   pVMM            The VMM ring-3 vtable.
     */
    virtual int configureDriver(PCFGMNODE pLunCfg, PCVMMR3VTABLE pVMM)
    {
        RT_NOREF(pLunCfg, pVMM);
        return VINF_SUCCESS;
    }

protected:

    /** Pointer to parent. */
    Console             *mpConsole;
    /** The driver's configuration. */
    AudioDriverCfg       mCfg;
    /** Whether the driver is attached or not. */
    bool                 mfAttached;
};

#endif /* !MAIN_INCLUDED_AudioDriver_h */

