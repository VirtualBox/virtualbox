/* $Id: KeyboardImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
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

#ifndef MAIN_INCLUDED_KeyboardImpl_h
#define MAIN_INCLUDED_KeyboardImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "KeyboardWrap.h"
#include "EventImpl.h"

#include <VBox/vmm/pdmdrv.h>

/** Limit of simultaneously attached devices (just USB and/or PS/2). */
enum { KEYBOARD_MAX_DEVICES = 2 };

/** Simple keyboard event class. */
class KeyboardEvent
{
public:
    KeyboardEvent() : scan(-1) {}
    KeyboardEvent(int _scan) : scan(_scan) {}
    bool i_isValid()
    {
        return (scan & ~0x80) && !(scan & ~0xFF);
    }
    int scan;
};
class Console;

class ATL_NO_VTABLE Keyboard :
    public KeyboardWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(Keyboard)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Console *aParent);
    void uninit();

    static const PDMDRVREG  DrvReg;

    Console *i_getParent() const
    {
        return mParent;
    }

private:

    // Wrapped Keyboard properties
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getKeyboardLEDs(std::vector<KeyboardLED_T> &aKeyboardLEDs);

    // Wrapped Keyboard members
    HRESULT putScancode(LONG aScancode);
    HRESULT putScancodes(const std::vector<LONG> &aScancodes,
                         ULONG *aCodesStored);
    HRESULT putCAD();
    HRESULT releaseKeys();
    HRESULT putUsageCode(LONG aUsageCode, LONG aUsagePage, BOOL fKeyRelease);

    static DECLCALLBACK(void)   i_keyboardLedStatusChange(PPDMIKEYBOARDCONNECTOR pInterface, PDMKEYBLEDS enmLeds);
    static DECLCALLBACK(void)   i_keyboardSetActive(PPDMIKEYBOARDCONNECTOR pInterface, bool fActive);
    static DECLCALLBACK(void *) i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID);
    static DECLCALLBACK(int)    i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
    static DECLCALLBACK(void)   i_drvDestruct(PPDMDRVINS pDrvIns);

    void onKeyboardLedsChange(PDMKEYBLEDS enmLeds);

    Console * const         mParent;
    /** Pointer to the associated keyboard driver(s). */
    struct DRVMAINKEYBOARD *mpDrv[KEYBOARD_MAX_DEVICES];

    /* The current guest keyboard LED status. */
    PDMKEYBLEDS menmLeds;

    const ComObjPtr<EventSource> mEventSource;
};

#endif /* !MAIN_INCLUDED_KeyboardImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
