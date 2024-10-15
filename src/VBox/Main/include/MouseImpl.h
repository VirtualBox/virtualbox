/* $Id: MouseImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef MAIN_INCLUDED_MouseImpl_h
#define MAIN_INCLUDED_MouseImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "MouseWrap.h"
#include "ConsoleImpl.h"
#include "EventImpl.h"
#include <VBox/vmm/pdmdrv.h>

/**
 * Structure for keeping mouse pointer data.
 */
struct MousePointerData
{
    MousePointerData()
        : fVisible(false)
        , hotX(0)
        , hotY(0)
        , width(0)
        , height(0)
        , pu8Shape(NULL)
        , cbShape(0) { }

    virtual ~MousePointerData()
    {
        Destroy();
    }

    /**
     * Initialize pointer shape data.
     *
     * @returns VBox status code.
     * @param   afVisible           Whether the mouse cursor actually is visible or not.
     * @param   afAlpha             Whether the pixel data contains an alpha mask or not.
     * @param   auHotX              X hot position (in pixel) of the new cursor.
     * @param   auHotY              Y hot position (in pixel) of the new cursor.
     * @param   auWidth             Width (in pixel) of the new cursor.
     * @param   auHeight            Height (in pixel) of the new cursor.
     * @param   apu8Shape           Pixel data of the new cursor.
     * @param   acbShape            Size of \a apu8Shape (in bytes).
     */
    int Init(bool afVisible, bool afAlpha, uint32_t auHotX, uint32_t auHotY, uint32_t auWidth, uint32_t auHeight,
             const uint8_t *apu8Shape, uint32_t acbShape)
    {
        AssertMsgReturn(pu8Shape == NULL, ("Already initialized!\n"), VERR_WRONG_ORDER);

        fVisible = afVisible;
        fAlpha   = afAlpha;
        hotX     = auHotX;
        hotY     = auHotY;
        width    = auWidth;
        height   = auHeight;
        if (acbShape)
        {
            pu8Shape = (uint8_t *)RTMemDup(apu8Shape, acbShape);
            AssertPtrReturn(pu8Shape, VERR_NO_MEMORY);
            cbShape = acbShape;
        }

        return VINF_SUCCESS;
    }

    /**
     * Initialize pointer shape data with another pointer shape data instance.
     *
     * @returns VBox status code.
     * @param   aThat           Pointer shape data instance to use for initialization.
     */
    int Init(const MousePointerData &aThat)
    {
        return Init(aThat.fVisible, aThat.fAlpha, aThat.hotX, aThat.hotY, aThat.width, aThat.height,
                    aThat.pu8Shape, aThat.cbShape);
    }

    /**
     * Destroys a pointer shape.
     */
    void Destroy(void)
    {
        if (pu8Shape)
        {
            Assert(cbShape);
            RTMemFree(pu8Shape);
            pu8Shape = NULL;
        }
        cbShape = 0;
    }

    bool fVisible;
    bool fAlpha;
    uint32_t hotX;
    uint32_t hotY;
    uint32_t width;
    uint32_t height;
    uint8_t *pu8Shape;
    uint32_t cbShape;
};

/** Maximum number of devices supported */
enum { MOUSE_MAX_DEVICES = 4 };
/** Mouse driver instance data. */
typedef struct DRVMAINMOUSE DRVMAINMOUSE, *PDRVMAINMOUSE;

class ATL_NO_VTABLE Mouse :
    public MouseWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS (Mouse)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(ConsoleMouseInterface *parent);
    void uninit();

    static const PDMDRVREG  DrvReg;

    ConsoleMouseInterface *i_getParent() const
    {
        return mParent;
    }

    /** notify the front-end of guest capability changes */
    void i_onVMMDevGuestCapsChange(uint32_t fCaps)
    {
        mfVMMDevGuestCaps = fCaps;
        i_sendMouseCapsNotifications();
    }

    int i_getPointerShape(MousePointerData &aData);
    int i_updatePointerShape(bool fVisible, bool fAlpha,
                             uint32_t xHot, uint32_t yHot,
                             uint32_t uWidth, uint32_t uHeight,
                             const uint8_t *pu8Shape, uint32_t cbShape);

    // Wrapped IMouse properties
    HRESULT getAbsoluteSupported(BOOL *aAbsoluteSupported);
    HRESULT getRelativeSupported(BOOL *aRelativeSupported);
    HRESULT getTouchScreenSupported(BOOL *aTouchScreenSupported);
    HRESULT getTouchPadSupported(BOOL *aTouchPadSupported);
    HRESULT getNeedsHostCursor(BOOL *aNeedsHostCursor);
    HRESULT getPointerShape(ComPtr<IMousePointerShape> &aPointerShape);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);

    // Wrapped IMouse methods
    HRESULT putMouseEvent(LONG aDx,
                          LONG aDy,
                          LONG aDz,
                          LONG aDw,
                          LONG aButtonState);
    HRESULT putMouseEventAbsolute(LONG aX,
                                  LONG aY,
                                  LONG aDz,
                                  LONG aDw,
                                  LONG aButtonState);
    HRESULT putEventMultiTouch(LONG aCount,
                               const std::vector<LONG64> &aContacts,
                               BOOL isTouchScreen,
                               ULONG aScanTime);
    HRESULT putEventMultiTouchString(LONG aCount,
                                     const com::Utf8Str &aContacts,
                                     BOOL isTouchScreen,
                                     ULONG aScanTime);
private:

    static DECLCALLBACK(void *) i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID);
    static DECLCALLBACK(void)   i_mouseReportModes(PPDMIMOUSECONNECTOR pInterface, bool fRel, bool fAbs, bool fMTAbs, bool fMTRel);
    static DECLCALLBACK(int)    i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
    static DECLCALLBACK(void)   i_drvDestruct(PPDMDRVINS pDrvIns);

    HRESULT i_updateVMMDevMouseCaps(uint32_t fCapsAdded, uint32_t fCapsRemoved);
    HRESULT i_reportRelEventToMouseDev(int32_t dx, int32_t dy, int32_t dz,
                                 int32_t dw, uint32_t fButtons);
    HRESULT i_reportAbsEventToMouseDev(int32_t x, int32_t y, int32_t dz,
                                     int32_t dw, uint32_t fButtons);
    HRESULT i_reportMTEventToMouseDev(int32_t x, int32_t z, uint32_t cContact,
                                    uint32_t fContact);
    HRESULT i_reportMultiTouchEventToDevice(uint8_t cContacts, const uint64_t *pau64Contacts, bool fTouchScreen, uint32_t u32ScanTime);
    HRESULT i_reportAbsEventToVMMDev(int32_t x, int32_t y, int32_t dz, int32_t dw, uint32_t fButtons);
    HRESULT i_reportAbsEventToInputDevices(int32_t x, int32_t y, int32_t dz, int32_t dw, uint32_t fButtons,
                                           bool fUsesVMMDevEvent);
    HRESULT i_reportAbsEventToDisplayDevice(int32_t x, int32_t y);
    HRESULT i_convertDisplayRes(LONG x, LONG y, int32_t *pxAdj, int32_t *pyAdj,
                                 bool *pfValid);
    HRESULT i_putEventMultiTouch(LONG aCount, const LONG64 *paContacts, BOOL isTouchScreen, ULONG aScanTime);

    uint32_t i_getDeviceCaps(void);
    void i_sendMouseCapsNotifications(void);
    bool i_guestNeedsHostCursor(void);
    bool i_vmmdevCanAbs(void);
    bool i_deviceCanAbs(void);
    bool i_supportsAbs(uint32_t fCaps) const;
    bool i_supportsAbs(void);
    bool i_supportsRel(void);
    bool i_supportsTS(void);
    bool i_supportsTP(void);

    ConsoleMouseInterface * const         mParent;
    /** Pointer to the associated mouse driver. */
    struct DRVMAINMOUSE    *mpDrv[MOUSE_MAX_DEVICES];

    uint32_t mfVMMDevGuestCaps;  /** We cache this to avoid access races */
    int32_t mcLastX;
    int32_t mcLastY;
    uint32_t mfLastButtons;

    ComPtr<IMousePointerShape> mPointerShape;
    /** Current mouse pointer data. */
    MousePointerData           mPointerData;

    const ComObjPtr<EventSource> mEventSource;
    VBoxEventDesc                mMouseEvent;

    void i_fireMouseEvent(bool fAbsolute, LONG x, LONG y, LONG dz, LONG dw,
                          LONG fButtons);

    void i_fireMultiTouchEvent(uint8_t cContacts,
                               const LONG64 *paContacts,
                               bool fTouchScreen,
                               uint32_t u32ScanTime);
};

#endif /* !MAIN_INCLUDED_MouseImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
