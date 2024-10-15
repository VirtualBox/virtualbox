/* $Id: Display.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox frontends: Basic Frontend (BFE):
 * Declaration of Display class
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

#ifndef VBOX_INCLUDED_SRC_VBoxBFE_Display_h
#define VBOX_INCLUDED_SRC_VBoxBFE_Display_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/semaphore.h>
#include <VBox/vmm/pdm.h>

#include <VBox/com/defs.h>
#include "Framebuffer.h"

class Display
{

public:

    Display();
    ~Display();

    // public methods only for internal purposes
    static const PDMDRVREG  DrvReg;

    uint32_t getWidth();
    uint32_t getHeight();
    uint32_t getBitsPerPixel();

    int SetFramebuffer(unsigned iScreenID, Framebuffer *pFramebuffer);
    void getFramebufferDimensions(int32_t *px1, int32_t *py1, int32_t *px2,
                                  int32_t *py2);

    void resetFramebuffer();

    void setRunning(void) { mfMachineRunning = true; };

    int i_handleDisplayResize(unsigned uScreenId, uint32_t bpp, void *pvVRAM,
                              uint32_t cbLine, uint32_t w, uint32_t h, uint16_t flags,
                              int32_t xOrigin, int32_t yOrigin, bool fVGAResize);
    void i_handleDisplayUpdate (int x, int y, int w, int h);

    void i_invalidateAndUpdateScreen(uint32_t aScreenId);


private:

    void updateDisplayData();

    static DECLCALLBACK(void*) i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID);
    static DECLCALLBACK(int)   i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
    static DECLCALLBACK(void)  i_drvDestruct(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(void)  i_drvPowerOff(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(int)   i_displayResizeCallback(PPDMIDISPLAYCONNECTOR pInterface, uint32_t bpp, void *pvVRAM, uint32_t cbLine, uint32_t cx, uint32_t cy);
    static DECLCALLBACK(void)  i_displayUpdateCallback(PPDMIDISPLAYCONNECTOR pInterface,
                                                       uint32_t x, uint32_t y, uint32_t cx, uint32_t cy);
    static DECLCALLBACK(void)  i_displayRefreshCallback(PPDMIDISPLAYCONNECTOR pInterface);
    static DECLCALLBACK(void)  i_displayResetCallback(PPDMIDISPLAYCONNECTOR pInterface);
    static DECLCALLBACK(void)  i_displayLFBModeChangeCallback(PPDMIDISPLAYCONNECTOR pInterface, bool fEnabled);
    static DECLCALLBACK(void)  i_displayProcessAdapterDataCallback(PPDMIDISPLAYCONNECTOR pInterface, void *pvVRAM, uint32_t u32VRAMSize);
    static DECLCALLBACK(void)  i_displayProcessDisplayDataCallback(PPDMIDISPLAYCONNECTOR pInterface, void *pvVRAM, unsigned uScreenId);

    static DECLCALLBACK(void) i_doInvalidateAndUpdate(struct DRVMAINDISPLAY  *mpDrv);
    /** Pointer to the associated display driver. */
    struct DRVMAINDISPLAY  *mpDrv;

    Framebuffer *m_pFramebuffer;
    bool mFramebufferOpened;

    bool        mfMachineRunning;
    volatile bool fVGAResizing;

    void handleResizeCompletedEMT (void);
    volatile uint32_t mu32ResizeStatus;

    enum {
        ResizeStatus_Void,
        ResizeStatus_InProgress,
        ResizeStatus_UpdateDisplayData
    };
};

#define DISPLAY_OID                          "e2ff0c7b-c02b-46d0-aa90-b9caf0f60561"

#endif /* !VBOX_INCLUDED_SRC_VBoxBFE_Display_h */
