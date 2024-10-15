/* $Id: Keyboard.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox frontends: Basic Frontend (BFE):
 * Declaration of Keyboard class
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_VBoxBFE_Keyboard_h
#define VBOX_INCLUDED_SRC_VBoxBFE_Keyboard_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/semaphore.h>
#include <VBox/vmm/pdm.h>

#include <VBox/com/defs.h>

class Keyboard
{

public:

    Keyboard();
    ~Keyboard();

    // public methods only for internal purposes
    static const PDMDRVREG  DrvReg;

    int PutScancode(long aScancode);
    int PutScancodes(const long *paScancodes,
                     uint32_t cScancodes,
                     unsigned long *aCodesStored);
    int PutUsageCode(long aUsageCode, long aUsagePage, bool fKeyRelease);

private:

    int releaseKeys();

    static DECLCALLBACK(void) i_keyboardLedStatusChange(PPDMIKEYBOARDCONNECTOR pInterface, PDMKEYBLEDS enmLeds);
    static DECLCALLBACK(void) i_keyboardSetActive(PPDMIKEYBOARDCONNECTOR pInterface, bool fActive);

    static DECLCALLBACK(void*) i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID);
    static DECLCALLBACK(int)   i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
    static DECLCALLBACK(void)  i_drvDestruct(PPDMDRVINS pDrvIns);

    /** Pointer to the associated display driver. */
    struct DRVMAINKEYBOARD  *mpDrv;

    bool        mfMachineRunning;
};

#define KEYBOARD_OID                          "d0cfbc0f-67ae-49db-ac4b-e8dc314f5a5c"

#endif /* !VBOX_INCLUDED_SRC_VBoxBFE_Keyboard_h */
