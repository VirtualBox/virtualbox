/* $Id: MouseImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#define LOG_GROUP LOG_GROUP_MAIN_MOUSE
#include "LoggingNew.h"

#include <iprt/cpp/utils.h>

#include "MouseImpl.h"
#include "DisplayImpl.h"
#include "VMMDev.h"
#include "MousePointerShapeWrap.h"
#include "VBoxEvents.h"

#include <VBox/vmm/pdmdrv.h>
#include <VBox/VMMDev.h>
#include <VBox/err.h>


class ATL_NO_VTABLE MousePointerShape:
    public MousePointerShapeWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(MousePointerShape)

    HRESULT FinalConstruct();
    void FinalRelease();

    /* Public initializer/uninitializer for internal purposes only. */
    HRESULT init(ComObjPtr<Mouse> pMouse,
                 bool fVisible, bool fAlpha,
                 uint32_t hotX, uint32_t hotY,
                 uint32_t width, uint32_t height,
                 const uint8_t *pu8Shape, uint32_t cbShape);
    void uninit();

private:
    // wrapped IMousePointerShape properties
    virtual HRESULT getVisible(BOOL *aVisible);
    virtual HRESULT getAlpha(BOOL *aAlpha);
    virtual HRESULT getHotX(ULONG *aHotX);
    virtual HRESULT getHotY(ULONG *aHotY);
    virtual HRESULT getWidth(ULONG *aWidth);
    virtual HRESULT getHeight(ULONG *aHeight);
    virtual HRESULT getShape(std::vector<BYTE> &aShape);

    struct Data
    {
        ComObjPtr<Mouse> pMouse;
        bool fVisible;
        bool fAlpha;
        uint32_t hotX;
        uint32_t hotY;
        uint32_t width;
        uint32_t height;
        std::vector<BYTE> shape;
    };

    Data m;
};

/*
 * MousePointerShape implementation.
 */
DEFINE_EMPTY_CTOR_DTOR(MousePointerShape)

HRESULT MousePointerShape::FinalConstruct()
{
    return BaseFinalConstruct();
}

void MousePointerShape::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

HRESULT MousePointerShape::init(ComObjPtr<Mouse> pMouse,
                                bool fVisible, bool fAlpha,
                                uint32_t hotX, uint32_t hotY,
                                uint32_t width, uint32_t height,
                                const uint8_t *pu8Shape, uint32_t cbShape)
{
    LogFlowThisFunc(("v %d, a %d, h %d,%d, %dx%d, cb %d\n",
                     fVisible, fAlpha, hotX, hotY, width, height, cbShape));

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m.pMouse = pMouse;
    m.fVisible = fVisible;
    m.fAlpha   = fAlpha;
    m.hotX     = hotX;
    m.hotY     = hotY;
    m.width    = width;
    m.height   = height;
    m.shape.resize(cbShape);
    if (cbShape)
    {
        memcpy(&m.shape.front(), pu8Shape, cbShape);
    }

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

void MousePointerShape::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    m.pMouse.setNull();
}

HRESULT MousePointerShape::getVisible(BOOL *aVisible)
{
    *aVisible = m.fVisible;
    return S_OK;
}

HRESULT MousePointerShape::getAlpha(BOOL *aAlpha)
{
    *aAlpha = m.fAlpha;
    return S_OK;
}

HRESULT MousePointerShape::getHotX(ULONG *aHotX)
{
    *aHotX = m.hotX;
    return S_OK;
}

HRESULT MousePointerShape::getHotY(ULONG *aHotY)
{
    *aHotY = m.hotY;
    return S_OK;
}

HRESULT MousePointerShape::getWidth(ULONG *aWidth)
{
    *aWidth = m.width;
    return S_OK;
}

HRESULT MousePointerShape::getHeight(ULONG *aHeight)
{
    *aHeight = m.height;
    return S_OK;
}

HRESULT MousePointerShape::getShape(std::vector<BYTE> &aShape)
{
    aShape.resize(m.shape.size());
    if (m.shape.size())
        memcpy(&aShape.front(), &m.shape.front(), aShape.size());
    return S_OK;
}


/** @name Mouse device capabilities bitfield
 * @{ */
enum
{
    /** The mouse device can do relative reporting */
    MOUSE_DEVCAP_RELATIVE = 1,
    /** The mouse device can do absolute reporting */
    MOUSE_DEVCAP_ABSOLUTE = 2,
    /** The mouse device can do absolute multi-touch reporting */
    MOUSE_DEVCAP_MT_ABSOLUTE = 4,
    /** The mouse device can do relative multi-touch reporting */
    MOUSE_DEVCAP_MT_RELATIVE = 8,
};
/** @} */


/**
 * Mouse driver instance data.
 */
struct DRVMAINMOUSE
{
    /** Pointer to the mouse object. */
    Mouse                      *pMouse;
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Pointer to the mouse port interface of the driver/device above us. */
    PPDMIMOUSEPORT              pUpPort;
    /** Our mouse connector interface. */
    PDMIMOUSECONNECTOR          IConnector;
    /** The capabilities of this device. */
    uint32_t                    u32DevCaps;
};


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

Mouse::Mouse()
    : mParent(NULL)
{
}

Mouse::~Mouse()
{
}


HRESULT Mouse::FinalConstruct()
{
    RT_ZERO(mpDrv);
    mcLastX = 0x8000;
    mcLastY = 0x8000;
    mfLastButtons = 0;
    mfVMMDevGuestCaps = 0;
    return BaseFinalConstruct();
}

void Mouse::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the mouse object.
 *
 * @returns COM result indicator
 * @param parent handle of our parent object
 */
HRESULT Mouse::init (ConsoleMouseInterface *parent)
{
    LogFlowThisFunc(("\n"));

    ComAssertRet(parent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = parent;

    unconst(mEventSource).createObject();
    HRESULT hrc = mEventSource->init();
    AssertComRCReturnRC(hrc);

    ComPtr<IEvent> ptrEvent;
    hrc = ::CreateGuestMouseEvent(ptrEvent.asOutParam(), mEventSource,
                                  (GuestMouseEventMode_T)0, 0 /*x*/, 0 /*y*/, 0 /*z*/, 0 /*w*/, 0 /*buttons*/);
    AssertComRCReturnRC(hrc);
    mMouseEvent.init(ptrEvent, mEventSource);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void Mouse::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    for (unsigned i = 0; i < MOUSE_MAX_DEVICES; ++i)
    {
        if (mpDrv[i])
            mpDrv[i]->pMouse = NULL;
        mpDrv[i] = NULL;
    }

    mPointerShape.setNull();

    RTMemFree(mPointerData.pu8Shape);
    mPointerData.pu8Shape = NULL;
    mPointerData.cbShape = 0;

    mMouseEvent.uninit();
    unconst(mEventSource).setNull();
    unconst(mParent) = NULL;
}

/**
 * Updates the pointer shape data.
 *
 * @returns VBox status code.
 * @param   fVisible            Whether the mouse cursor actually is visible or not.
 * @param   fAlpha              Whether the pixel data contains an alpha mask or not.
 * @param   uHotX               X hot position (in pixel) of the new cursor.
 * @param   uHotY               Y hot position (in pixel) of the new cursor.
 * @param   uWidth              Width (in pixel) of the new cursor.
 * @param   uHeight             Height (in pixel) of the new cursor.
 * @param   pu8Shape            Pixel data of the new cursor.
 * @param   cbShape             Size of \a pu8Shape (in bytes).
 *
 * @note    Takes the write lock.
 */
int Mouse::i_updatePointerShape(bool fVisible, bool fAlpha,
                                uint32_t uHotX, uint32_t uHotY,
                                uint32_t uWidth, uint32_t uHeight,
                                const uint8_t *pu8Shape, uint32_t cbShape)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    mPointerData.Destroy(); /* Destroy old data first. */

    int vrc = mPointerData.Init(fVisible, fAlpha, uHotX, uHotY, uWidth, uHeight, pu8Shape, cbShape);
    if (RT_SUCCESS(vrc))
    {
        mPointerShape.setNull();
    }

    return vrc;
}

// IMouse properties
/////////////////////////////////////////////////////////////////////////////

/** Report the front-end's mouse handling capabilities to the VMM device and
 * thus to the guest.
 * @note all calls out of this object are made with no locks held! */
HRESULT Mouse::i_updateVMMDevMouseCaps(uint32_t fCapsAdded,
                                       uint32_t fCapsRemoved)
{
    VMMDevMouseInterface *pVMMDev = mParent->i_getVMMDevMouseInterface();
    if (!pVMMDev)
        return E_FAIL;  /* No assertion, as the front-ends can send events
                         * at all sorts of inconvenient times. */
    DisplayMouseInterface *pDisplay = mParent->i_getDisplayMouseInterface();
    if (pDisplay == NULL)
        return E_FAIL;
    PPDMIVMMDEVPORT pVMMDevPort = pVMMDev->getVMMDevPort();
    if (!pVMMDevPort)
        return E_FAIL;  /* same here */

    int vrc = pVMMDevPort->pfnUpdateMouseCapabilities(pVMMDevPort, fCapsAdded,
                                                      fCapsRemoved);
    if (RT_FAILURE(vrc))
        return E_FAIL;
    return pDisplay->i_reportHostCursorCapabilities(fCapsAdded, fCapsRemoved);
}

/**
 * Returns whether the currently active device portfolio can accept absolute
 * mouse events.
 *
 * @returns COM status code
 * @param aAbsoluteSupported address of result variable
 */
HRESULT Mouse::getAbsoluteSupported(BOOL *aAbsoluteSupported)
{
    *aAbsoluteSupported = i_supportsAbs();
    return S_OK;
}

/**
 * Returns whether the currently active device portfolio can accept relative
 * mouse events.
 *
 * @returns COM status code
 * @param aRelativeSupported address of result variable
 */
HRESULT Mouse::getRelativeSupported(BOOL *aRelativeSupported)
{
    *aRelativeSupported = i_supportsRel();
    return S_OK;
}

/**
 * Returns whether the currently active device portfolio can accept multi-touch
 * touchscreen events.
 *
 * @returns COM status code
 * @param aTouchScreenSupported address of result variable
 */
HRESULT Mouse::getTouchScreenSupported(BOOL *aTouchScreenSupported)
{
    *aTouchScreenSupported = i_supportsTS();
    return S_OK;
}

/**
 * Returns whether the currently active device portfolio can accept multi-touch
 * touchpad events.
 *
 * @returns COM status code
 * @param aTouchPadSupported address of result variable
 */
HRESULT Mouse::getTouchPadSupported(BOOL *aTouchPadSupported)
{
    *aTouchPadSupported = i_supportsTP();
    return S_OK;
}

/**
 * Returns whether the guest can currently switch to drawing the mouse cursor
 * itself if it is asked to by the front-end.
 *
 * @returns COM status code
 * @param aNeedsHostCursor address of result variable
 */
HRESULT Mouse::getNeedsHostCursor(BOOL *aNeedsHostCursor)
{
    *aNeedsHostCursor = i_guestNeedsHostCursor();
    return S_OK;
}

HRESULT Mouse::getPointerShape(ComPtr<IMousePointerShape> &aPointerShape)
{
    HRESULT hr = S_OK;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mPointerShape.isNull())
    {
        ComObjPtr<MousePointerShape> obj;
        hr = obj.createObject();
        if (SUCCEEDED(hr))
        {
            hr = obj->init(this, mPointerData.fVisible, mPointerData.fAlpha,
                           mPointerData.hotX, mPointerData.hotY,
                           mPointerData.width, mPointerData.height,
                           mPointerData.pu8Shape, mPointerData.cbShape);
        }

        if (SUCCEEDED(hr))
        {
            mPointerShape = obj;
        }
    }

    if (SUCCEEDED(hr))
    {
        aPointerShape = mPointerShape;
    }

    return hr;
}

// IMouse methods
/////////////////////////////////////////////////////////////////////////////

/** Converts a bitfield containing information about mouse buttons currently
 * held down from the format used by the front-end to the format used by PDM
 * and the emulated pointing devices. */
static uint32_t i_mouseButtonsToPDM(LONG buttonState)
{
    uint32_t fButtons = 0;
    if (buttonState & MouseButtonState_LeftButton)
        fButtons |= PDMIMOUSEPORT_BUTTON_LEFT;
    if (buttonState & MouseButtonState_RightButton)
        fButtons |= PDMIMOUSEPORT_BUTTON_RIGHT;
    if (buttonState & MouseButtonState_MiddleButton)
        fButtons |= PDMIMOUSEPORT_BUTTON_MIDDLE;
    if (buttonState & MouseButtonState_XButton1)
        fButtons |= PDMIMOUSEPORT_BUTTON_X1;
    if (buttonState & MouseButtonState_XButton2)
        fButtons |= PDMIMOUSEPORT_BUTTON_X2;
    return fButtons;
}

HRESULT Mouse::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    // no need to lock - lifetime constant
    mEventSource.queryInterfaceTo(aEventSource.asOutParam());
    return S_OK;
}

/**
 * Send a relative pointer event to the relative device we deem most
 * appropriate.
 *
 * @returns   COM status code
 */
HRESULT Mouse::i_reportRelEventToMouseDev(int32_t dx, int32_t dy, int32_t dz,
                                          int32_t dw, uint32_t fButtons)
{
    if (dx || dy || dz || dw || fButtons != mfLastButtons)
    {
        PPDMIMOUSEPORT pUpPort = NULL;
        {
            AutoReadLock aLock(this COMMA_LOCKVAL_SRC_POS);

            for (unsigned i = 0; !pUpPort && i < MOUSE_MAX_DEVICES; ++i)
            {
                if (mpDrv[i] && (mpDrv[i]->u32DevCaps & MOUSE_DEVCAP_RELATIVE))
                    pUpPort = mpDrv[i]->pUpPort;
            }
        }
        if (!pUpPort)
            return S_OK;

        int vrc = pUpPort->pfnPutEvent(pUpPort, dx, dy, dz, dw, fButtons);

        if (RT_FAILURE(vrc))
            return setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                tr("Could not send the mouse event to the virtual mouse (%Rrc)"),
                                vrc);
        mfLastButtons = fButtons;
    }
    return S_OK;
}


/**
 * Send an absolute pointer event to the emulated absolute device we deem most
 * appropriate.
 *
 * @returns   COM status code
 */
HRESULT Mouse::i_reportAbsEventToMouseDev(int32_t x, int32_t y,
                                          int32_t dz, int32_t dw, uint32_t fButtons)
{
    if (   x < VMMDEV_MOUSE_RANGE_MIN
        || x > VMMDEV_MOUSE_RANGE_MAX)
        return S_OK;
    if (   y < VMMDEV_MOUSE_RANGE_MIN
        || y > VMMDEV_MOUSE_RANGE_MAX)
        return S_OK;
    if (   x != mcLastX || y != mcLastY
        || dz || dw || fButtons != mfLastButtons)
    {
        PPDMIMOUSEPORT pUpPort = NULL;
        {
            AutoReadLock aLock(this COMMA_LOCKVAL_SRC_POS);

            for (unsigned i = 0; !pUpPort && i < MOUSE_MAX_DEVICES; ++i)
            {
                if (mpDrv[i] && (mpDrv[i]->u32DevCaps & MOUSE_DEVCAP_ABSOLUTE))
                    pUpPort = mpDrv[i]->pUpPort;
            }
        }
        if (!pUpPort)
            return S_OK;

        int vrc = pUpPort->pfnPutEventAbs(pUpPort, x, y, dz,
                                          dw, fButtons);
        if (RT_FAILURE(vrc))
            return setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                tr("Could not send the mouse event to the virtual mouse (%Rrc)"),
                                vrc);
        mfLastButtons = fButtons;

    }
    return S_OK;
}

HRESULT Mouse::i_reportMultiTouchEventToDevice(uint8_t cContacts,
                                               const uint64_t *pau64Contacts,
                                               bool fTouchScreen,
                                               uint32_t u32ScanTime)
{
    HRESULT hrc = S_OK;

    int match = fTouchScreen ? MOUSE_DEVCAP_MT_ABSOLUTE : MOUSE_DEVCAP_MT_RELATIVE;
    PPDMIMOUSEPORT pUpPort = NULL;
    {
        AutoReadLock aLock(this COMMA_LOCKVAL_SRC_POS);

        unsigned i;
        for (i = 0; i < MOUSE_MAX_DEVICES; ++i)
        {
            if (   mpDrv[i]
                && (mpDrv[i]->u32DevCaps & match))
            {
                pUpPort = mpDrv[i]->pUpPort;
                break;
            }
        }
    }

    if (pUpPort)
    {
        int vrc = pUpPort->pfnPutEventTouchScreen(pUpPort, cContacts, pau64Contacts, u32ScanTime);
        if (RT_FAILURE(vrc))
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                               tr("Could not send the multi-touch event to the virtual device (%Rrc)"),
                               vrc);
    }
    else
    {
        hrc = E_UNEXPECTED;
    }

    return hrc;
}


/**
 * Send an absolute position event to the VMM device.
 * @note all calls out of this object are made with no locks held!
 *
 * @returns   COM status code
 */
HRESULT Mouse::i_reportAbsEventToVMMDev(int32_t x, int32_t y, int32_t dz, int32_t dw, uint32_t fButtons)
{
    VMMDevMouseInterface *pVMMDev = mParent->i_getVMMDevMouseInterface();
    ComAssertRet(pVMMDev, E_FAIL);
    PPDMIVMMDEVPORT pVMMDevPort = pVMMDev->getVMMDevPort();
    ComAssertRet(pVMMDevPort, E_FAIL);

    if (x != mcLastX || y != mcLastY || dz || dw || fButtons != mfLastButtons)
    {
        int vrc = pVMMDevPort->pfnSetAbsoluteMouse(pVMMDevPort, x, y, dz, dw, fButtons);
        if (RT_FAILURE(vrc))
            return setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                tr("Could not send the mouse event to the virtual mouse (%Rrc)"),
                                vrc);
    }
    return S_OK;
}


/**
 * Send an absolute pointer event to a pointing device (the VMM device if
 * possible or whatever emulated absolute device seems best to us if not).
 *
 * @returns   COM status code
 */
HRESULT Mouse::i_reportAbsEventToInputDevices(int32_t x, int32_t y, int32_t dz, int32_t dw, uint32_t fButtons,
                                              bool fUsesVMMDevEvent)
{
    HRESULT hrc = S_OK;
    /** If we are using the VMMDev to report absolute position but without
     * VMMDev IRQ support then we need to send a small "jiggle" to the emulated
     * relative mouse device to alert the guest to changes. */
    LONG cJiggle = 0;

    if (i_vmmdevCanAbs())
    {
        /*
         * Send the absolute mouse position to the VMM device.
         */
        if (x != mcLastX || y != mcLastY || dz || dw || fButtons != mfLastButtons)
        {
            hrc = i_reportAbsEventToVMMDev(x, y, dz, dw, fButtons);
            cJiggle = !fUsesVMMDevEvent;
        }

        /* If guest cannot yet read full mouse state from DevVMM (i.e.,
         * only 'x' and 'y' coordinates will be read) we need to pass buttons
         * state as well as horizontal and vertical wheel movement over ever-present PS/2
         * emulated mouse device. */
        if (!(mfVMMDevGuestCaps & VMMDEV_MOUSE_GUEST_USES_FULL_STATE_PROTOCOL))
            hrc = i_reportRelEventToMouseDev(cJiggle, 0, dz, dw, fButtons);
    }
    else
        hrc = i_reportAbsEventToMouseDev(x, y, dz, dw, fButtons);

    mcLastX = x;
    mcLastY = y;
    mfLastButtons = fButtons;
    return hrc;
}


/**
 * Send an absolute position event to the display device.
 * @note all calls out of this object are made with no locks held!
 * @param  x  Cursor X position in pixels relative to the first screen, where
 *            (1, 1) is the upper left corner.
 * @param  y  Cursor Y position in pixels relative to the first screen, where
 *            (1, 1) is the upper left corner.
 */
HRESULT Mouse::i_reportAbsEventToDisplayDevice(int32_t x, int32_t y)
{
    DisplayMouseInterface *pDisplay = mParent->i_getDisplayMouseInterface();
    ComAssertRet(pDisplay, E_FAIL);

    if (x != mcLastX || y != mcLastY)
    {
        pDisplay->i_reportHostCursorPosition(x - 1, y - 1, false);
    }
    return S_OK;
}


void Mouse::i_fireMouseEvent(bool fAbsolute, LONG x, LONG y, LONG dz, LONG dw,
                             LONG fButtons)
{
    /* If mouse button is pressed, we generate new event, to avoid reusable events coalescing and thus
       dropping key press events */
    GuestMouseEventMode_T mode;
    if (fAbsolute)
        mode = GuestMouseEventMode_Absolute;
    else
        mode = GuestMouseEventMode_Relative;

    if (fButtons != 0)
        ::FireGuestMouseEvent(mEventSource, mode, x, y, dz, dw, fButtons);
    else
    {
        ComPtr<IEvent> ptrEvent;
        mMouseEvent.getEvent(ptrEvent.asOutParam());
        ReinitGuestMouseEvent(ptrEvent, mode, x, y, dz, dw, fButtons);
        mMouseEvent.fire(0);
    }
}

void Mouse::i_fireMultiTouchEvent(uint8_t cContacts,
                                  const LONG64 *paContacts,
                                  bool fTouchScreen,
                                  uint32_t u32ScanTime)
{
    com::SafeArray<SHORT> xPositions(cContacts);
    com::SafeArray<SHORT> yPositions(cContacts);
    com::SafeArray<USHORT> contactIds(cContacts);
    com::SafeArray<USHORT> contactFlags(cContacts);

    uint8_t i;
    for (i = 0; i < cContacts; i++)
    {
        uint32_t u32Lo = RT_LO_U32(paContacts[i]);
        uint32_t u32Hi = RT_HI_U32(paContacts[i]);
        xPositions[i] = (int16_t)u32Lo;
        yPositions[i] = (int16_t)(u32Lo >> 16);
        contactIds[i] = RT_BYTE1(u32Hi);
        contactFlags[i] = RT_BYTE2(u32Hi);
    }

    ::FireGuestMultiTouchEvent(mEventSource, cContacts, ComSafeArrayAsInParam(xPositions), ComSafeArrayAsInParam(yPositions),
                               ComSafeArrayAsInParam(contactIds), ComSafeArrayAsInParam(contactFlags), fTouchScreen, u32ScanTime);
}

/**
 * Send a relative mouse event to the guest.
 * @note the VMMDev capability change is so that the guest knows we are sending
 *       real events over the PS/2 device and not dummy events to signal the
 *       arrival of new absolute pointer data
 *
 * @returns COM status code
 * @param dx            X movement.
 * @param dy            Y movement.
 * @param dz            Z movement.
 * @param dw            Mouse wheel movement.
 * @param aButtonState  The mouse button state.
 */
HRESULT Mouse::putMouseEvent(LONG dx, LONG dy, LONG dz, LONG dw,
                             LONG aButtonState)
{
    LogRel3(("%s: dx=%d, dy=%d, dz=%d, dw=%d\n", __PRETTY_FUNCTION__,
                 dx, dy, dz, dw));

    uint32_t fButtonsAdj = i_mouseButtonsToPDM(aButtonState);
    /* Make sure that the guest knows that we are sending real movement
     * events to the PS/2 device and not just dummy wake-up ones. */
    i_updateVMMDevMouseCaps(0, VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE);
    HRESULT hrc = i_reportRelEventToMouseDev(dx, dy, dz, dw, fButtonsAdj);

    i_fireMouseEvent(false, dx, dy, dz, dw, aButtonState);

    return hrc;
}

/**
 * Convert an (X, Y) value pair in screen co-ordinates (starting from 1) to a
 * value from VMMDEV_MOUSE_RANGE_MIN to VMMDEV_MOUSE_RANGE_MAX.  Sets the
 * optional validity value to false if the pair is not on an active screen and
 * to true otherwise.
 * @note      since guests with recent versions of X.Org use a different method
 *            to everyone else to map the valuator value to a screen pixel (they
 *            multiply by the screen dimension, do a floating point divide by
 *            the valuator maximum and round the result, while everyone else
 *            does truncating integer operations) we adjust the value we send
 *            so that it maps to the right pixel both when the result is rounded
 *            and when it is truncated.
 *
 * @returns   COM status value
 */
HRESULT Mouse::i_convertDisplayRes(LONG x, LONG y, int32_t *pxAdj, int32_t *pyAdj,
                                   bool *pfValid)
{
    AssertPtrReturn(pxAdj, E_POINTER);
    AssertPtrReturn(pyAdj, E_POINTER);
    AssertPtrNullReturn(pfValid, E_POINTER);
    DisplayMouseInterface *pDisplay = mParent->i_getDisplayMouseInterface();
    ComAssertRet(pDisplay, E_FAIL);
    /** The amount to add to the result (multiplied by the screen width/height)
     * to compensate for differences in guest methods for mapping back to
     * pixels */
    enum { ADJUST_RANGE = - 3 * VMMDEV_MOUSE_RANGE / 4 };

    if (pfValid)
        *pfValid = true;
    if (!(mfVMMDevGuestCaps & VMMDEV_MOUSE_NEW_PROTOCOL) && !pDisplay->i_isInputMappingSet())
    {
        ULONG displayWidth, displayHeight;
        ULONG ulDummy;
        LONG lDummy;
        /* Takes the display lock */
        HRESULT hrc = pDisplay->i_getScreenResolution(0, &displayWidth,
                                                      &displayHeight, &ulDummy, &lDummy, &lDummy);
        if (FAILED(hrc))
            return hrc;

        *pxAdj = displayWidth ?   (x * VMMDEV_MOUSE_RANGE + ADJUST_RANGE)
                                / (LONG) displayWidth: 0;
        *pyAdj = displayHeight ?   (y * VMMDEV_MOUSE_RANGE + ADJUST_RANGE)
                                 / (LONG) displayHeight: 0;
    }
    else
    {
        int32_t x1, y1, x2, y2;
        /* Takes the display lock */
        pDisplay->i_getFramebufferDimensions(&x1, &y1, &x2, &y2);
        *pxAdj = x1 < x2 ?   ((x - x1) * VMMDEV_MOUSE_RANGE + ADJUST_RANGE)
                           / (x2 - x1) : 0;
        *pyAdj = y1 < y2 ?   ((y - y1) * VMMDEV_MOUSE_RANGE + ADJUST_RANGE)
                           / (y2 - y1) : 0;
        if (   *pxAdj < VMMDEV_MOUSE_RANGE_MIN
            || *pxAdj > VMMDEV_MOUSE_RANGE_MAX
            || *pyAdj < VMMDEV_MOUSE_RANGE_MIN
            || *pyAdj > VMMDEV_MOUSE_RANGE_MAX)
            if (pfValid)
                *pfValid = false;
    }
    return S_OK;
}


/**
 * Send an absolute mouse event to the VM. This requires either VirtualBox-
 * specific drivers installed in the guest or absolute pointing device
 * emulation.
 * @note the VMMDev capability change is so that the guest knows we are sending
 *       dummy events over the PS/2 device to signal the arrival of new
 *       absolute pointer data, and not pointer real movement data
 * @note all calls out of this object are made with no locks held!
 *
 * @returns COM status code
 * @param x         X position (pixel), starting from 1
 * @param y         Y position (pixel), starting from 1
 * @param dz        Z movement
 * @param dw        mouse wheel movement
 * @param aButtonState The mouse button state
 */
HRESULT Mouse::putMouseEventAbsolute(LONG x, LONG y, LONG dz, LONG dw,
                                     LONG aButtonState)
{
    LogRel3(("%s: x=%d, y=%d, dz=%d, dw=%d, fButtons=0x%x\n",
             __PRETTY_FUNCTION__, x, y, dz, dw, aButtonState));

    DisplayMouseInterface *pDisplay = mParent->i_getDisplayMouseInterface();
    ComAssertRet(pDisplay, E_FAIL);
    int32_t xAdj, yAdj;
    uint32_t fButtonsAdj;
    bool fValid;

    /* If we are doing old-style (IRQ-less) absolute reporting to the VMM
     * device then make sure the guest is aware of it, so that it knows to
     * ignore relative movement on the PS/2 device. */
    i_updateVMMDevMouseCaps(VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE, 0);
    /* Detect out-of-range. */
    if (x == 0x7FFFFFFF && y == 0x7FFFFFFF)
    {
        pDisplay->i_reportHostCursorPosition(0, 0, true);
        return S_OK;
    }
    /* Detect "report-only" (-1, -1).  This is not ideal, as in theory the
     * front-end could be sending negative values relative to the primary
     * screen. */
    if (x == -1 && y == -1)
        return S_OK;
    /** @todo the front end should do this conversion to avoid races */
    /** @note Or maybe not... races are pretty inherent in everything done in
     *        this object and not really bad as far as I can see. */
    HRESULT hrc = i_convertDisplayRes(x, y, &xAdj, &yAdj, &fValid);
    if (FAILED(hrc)) return hrc;

    fButtonsAdj = i_mouseButtonsToPDM(aButtonState);
    if (fValid)
    {
        hrc = i_reportAbsEventToInputDevices(xAdj, yAdj, dz, dw, fButtonsAdj,
                                             RT_BOOL(mfVMMDevGuestCaps & VMMDEV_MOUSE_NEW_PROTOCOL));
        if (FAILED(hrc)) return hrc;

        i_fireMouseEvent(true, x, y, dz, dw, aButtonState);
    }
    hrc = i_reportAbsEventToDisplayDevice(x, y);

    return hrc;
}

/**
 * Send a multi-touch event. This requires multi-touch pointing device emulation.
 * @note all calls out of this object are made with no locks held!
 *
 * @returns COM status code.
 * @param aCount     Number of contacts.
 * @param aContacts  Information about each contact.
 * @param aIsTouchscreen Distinguishes between touchscreen and touchpad events.
 * @param aScanTime  Timestamp.
 */
HRESULT Mouse::putEventMultiTouch(LONG aCount,
                                  const std::vector<LONG64> &aContacts,
                                  BOOL aIsTouchscreen,
                                  ULONG aScanTime)
{
    LogRel3(("%s: aCount %d(actual %d), aScanTime %u\n",
             __FUNCTION__, aCount, aContacts.size(), aScanTime));

    HRESULT hrc = S_OK;

    if ((LONG)aContacts.size() >= aCount)
    {
        const LONG64 *paContacts = aCount > 0? &aContacts.front(): NULL;

        hrc = i_putEventMultiTouch(aCount, paContacts, aIsTouchscreen, aScanTime);
    }
    else
    {
        hrc = E_INVALIDARG;
    }

    return hrc;
}

/**
 * Send a multi-touch event. Version for scripting languages.
 *
 * @returns COM status code.
 * @param aCount     Number of contacts.
 * @param aContacts  Information about each contact.
 * @param aIsTouchscreen Distinguishes between touchscreen and touchpad events.
 * @param aScanTime  Timestamp.
 */
HRESULT Mouse::putEventMultiTouchString(LONG aCount,
                                        const com::Utf8Str &aContacts,
                                        BOOL aIsTouchscreen,
                                        ULONG aScanTime)
{
    /** @todo implement: convert the string to LONG64 array and call putEventMultiTouch. */
    NOREF(aCount);
    NOREF(aContacts);
    NOREF(aIsTouchscreen);
    NOREF(aScanTime);
    return E_NOTIMPL;
}


// private methods
/////////////////////////////////////////////////////////////////////////////

/* Used by PutEventMultiTouch and PutEventMultiTouchString. */
HRESULT Mouse::i_putEventMultiTouch(LONG aCount,
                                    const LONG64 *paContacts,
                                    BOOL aIsTouchscreen,
                                    ULONG aScanTime)
{
    if (aCount >= 256)
        return E_INVALIDARG;

    HRESULT hrc = S_OK;

    /* Touch events in the touchscreen variant are currently mapped to the
     * primary monitor, because the emulated USB touchscreen device is
     * associated with one (normally the primary) screen in the guest.
     * In the future this could/should be extended to multi-screen support. */
    ULONG uScreenId = 0;

    ULONG cWidth  = 0;
    ULONG cHeight = 0;
    LONG  xOrigin = 0;
    LONG  yOrigin = 0;

    if (aIsTouchscreen)
    {
        DisplayMouseInterface *pDisplay = mParent->i_getDisplayMouseInterface();
        ComAssertRet(pDisplay, E_FAIL);
        ULONG cBPP    = 0;
        hrc = pDisplay->i_getScreenResolution(uScreenId, &cWidth, &cHeight, &cBPP, &xOrigin, &yOrigin);
        NOREF(cBPP);
        ComAssertComRCRetRC(hrc);
    }

    uint64_t* pau64Contacts = NULL;
    uint8_t cContacts = 0;

    /* Deliver 0 contacts too, touch device may use this to reset the state. */
    if (aCount > 0)
    {
        /* Create a copy with converted coords. */
        pau64Contacts = (uint64_t *)RTMemTmpAlloc(aCount * sizeof(uint64_t));
        if (pau64Contacts)
        {
            if (aIsTouchscreen)
            {
                int32_t x1 = xOrigin;
                int32_t y1 = yOrigin;
                int32_t x2 = x1 + cWidth;
                int32_t y2 = y1 + cHeight;

                LogRel3(("%s: screen [%d] %d,%d %d,%d\n",
                         __FUNCTION__, uScreenId, x1, y1, x2, y2));

                LONG i;
                for (i = 0; i < aCount; i++)
                {
                    uint32_t u32Lo = RT_LO_U32(paContacts[i]);
                    uint32_t u32Hi = RT_HI_U32(paContacts[i]);
                    int32_t x = (int16_t)u32Lo;
                    int32_t y = (int16_t)(u32Lo >> 16);
                    uint8_t contactId =  RT_BYTE1(u32Hi);
                    bool fInContact   = (RT_BYTE2(u32Hi) & 0x1) != 0;
                    bool fInRange     = (RT_BYTE2(u32Hi) & 0x2) != 0;

                    LogRel3(("%s: touchscreen [%d] %d,%d id %d, inContact %d, inRange %d\n",
                             __FUNCTION__, i, x, y, contactId, fInContact, fInRange));

                    /* x1,y1 are inclusive and x2,y2 are exclusive,
                     * while x,y start from 1 and are inclusive.
                     */
                    if (x <= x1 || x > x2 || y <= y1 || y > y2)
                    {
                        /* Out of range. Skip the contact. */
                        continue;
                    }

                    int32_t xAdj = x1 < x2? ((x - 1 - x1) * VMMDEV_MOUSE_RANGE) / (x2 - x1) : 0;
                    int32_t yAdj = y1 < y2? ((y - 1 - y1) * VMMDEV_MOUSE_RANGE) / (y2 - y1) : 0;

                    bool fValid = (   xAdj >= VMMDEV_MOUSE_RANGE_MIN
                                   && xAdj <= VMMDEV_MOUSE_RANGE_MAX
                                   && yAdj >= VMMDEV_MOUSE_RANGE_MIN
                                   && yAdj <= VMMDEV_MOUSE_RANGE_MAX);

                    if (fValid)
                    {
                        uint8_t fu8 = (uint8_t)(  (fInContact? 0x01: 0x00)
                                                | (fInRange?   0x02: 0x00));
                        pau64Contacts[cContacts] = RT_MAKE_U64_FROM_U16((uint16_t)xAdj,
                                                                        (uint16_t)yAdj,
                                                                        RT_MAKE_U16(contactId, fu8),
                                                                        0);
                        cContacts++;
                    }
                }
            }
            else
            {
                LONG i;
                for (i = 0; i < aCount; i++)
                {
                    uint32_t u32Lo = RT_LO_U32(paContacts[i]);
                    uint32_t u32Hi = RT_HI_U32(paContacts[i]);
                    uint16_t x = (uint16_t)u32Lo;
                    uint16_t y = (uint16_t)(u32Lo >> 16);
                    uint8_t contactId =  RT_BYTE1(u32Hi);
                    bool fInContact   = (RT_BYTE2(u32Hi) & 0x1) != 0;

                    LogRel3(("%s: touchpad [%d] %#04x,%#04x id %d, inContact %d\n",
                             __FUNCTION__, i, x, y, contactId, fInContact));

                    uint8_t fu8 = (uint8_t)(fInContact? 0x01: 0x00);

                    pau64Contacts[cContacts] = RT_MAKE_U64_FROM_U16(x, y,
                                                                    RT_MAKE_U16(contactId, fu8),
                                                                     0);
                    cContacts++;
                }
            }
        }
        else
        {
            hrc = E_OUTOFMEMORY;
        }
    }

    if (SUCCEEDED(hrc))
    {
        hrc = i_reportMultiTouchEventToDevice(cContacts, cContacts? pau64Contacts: NULL, !!aIsTouchscreen, (uint32_t)aScanTime);

        /* Send the original contact information. */
        i_fireMultiTouchEvent(cContacts, cContacts? paContacts: NULL, !!aIsTouchscreen, (uint32_t)aScanTime);
    }

    RTMemTmpFree(pau64Contacts);

    return hrc;
}


/** Does the guest currently rely on the host to draw the mouse cursor or
 * can it switch to doing it itself in software? */
bool Mouse::i_guestNeedsHostCursor(void)
{
    return RT_BOOL(mfVMMDevGuestCaps & VMMDEV_MOUSE_GUEST_NEEDS_HOST_CURSOR);
}


/**
 * Returns the current mouse pointer data.
 *
 * @returns VBox status code.
 * @param   aData               Where to return the current mouse pointer data.
 */
int Mouse::i_getPointerShape(MousePointerData &aData)
{
    AutoReadLock aLock(this COMMA_LOCKVAL_SRC_POS);

    return aData.Init(mPointerData);
}


/**
 * Gets the combined capabilities of all currently enabled devices.
 *
 * @returns Combination of MOUSE_DEVCAP_XXX
 */
uint32_t Mouse::i_getDeviceCaps(void)
{
    uint32_t fCaps = 0;
    AutoReadLock aLock(this COMMA_LOCKVAL_SRC_POS);
    for (unsigned i = 0; i < MOUSE_MAX_DEVICES; ++i)
        if (mpDrv[i])
            fCaps |= mpDrv[i]->u32DevCaps;
    return fCaps;
}


/** Does the VMM device currently support absolute reporting? */
bool Mouse::i_vmmdevCanAbs(void)
{
    /* This requires the VMMDev cap and a relative device, which supposedly
       consumes these. As seen in @bugref{10285} this isn't quite as clear cut. */
    return (mfVMMDevGuestCaps & VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE)
        && (i_getDeviceCaps() & MOUSE_DEVCAP_RELATIVE);
}


/** Does any device currently support absolute reporting w/o help from VMMDev? */
bool Mouse::i_deviceCanAbs(void)
{
    return RT_BOOL(i_getDeviceCaps() & MOUSE_DEVCAP_ABSOLUTE);
}


/** Can we currently send relative events to the guest? */
bool Mouse::i_supportsRel(void)
{
    return RT_BOOL(i_getDeviceCaps() & MOUSE_DEVCAP_RELATIVE);
}


/** Can we currently send absolute events to the guest (including via VMMDev)? */
bool Mouse::i_supportsAbs(uint32_t fCaps) const
{
    return (fCaps & MOUSE_DEVCAP_ABSOLUTE)
        || /* inlined i_vmmdevCanAbs() to avoid unnecessary i_getDeviceCaps call: */
           (   (mfVMMDevGuestCaps & VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE)
            && (fCaps & MOUSE_DEVCAP_RELATIVE));
}


/** Can we currently send absolute events to the guest? */
bool Mouse::i_supportsAbs(void)
{
    return Mouse::i_supportsAbs(i_getDeviceCaps());
}


/** Can we currently send multi-touch events (touchscreen variant) to the guest? */
bool Mouse::i_supportsTS(void)
{
    return RT_BOOL(i_getDeviceCaps() & MOUSE_DEVCAP_MT_ABSOLUTE);
}


/** Can we currently send multi-touch events (touchpad variant) to the guest? */
bool Mouse::i_supportsTP(void)
{
    return RT_BOOL(i_getDeviceCaps() & MOUSE_DEVCAP_MT_RELATIVE);
}


/** Check what sort of reporting can be done using the devices currently
 * enabled (including the VMM device) and notify the guest and the front-end.
 */
void Mouse::i_sendMouseCapsNotifications(void)
{
    bool fRelDev, fTSDev, fTPDev, fCanAbs, fNeedsHostCursor;
    {
        AutoReadLock aLock(this COMMA_LOCKVAL_SRC_POS);

        uint32_t const fCaps = i_getDeviceCaps();
        fRelDev = RT_BOOL(fCaps & MOUSE_DEVCAP_RELATIVE);
        fTSDev  = RT_BOOL(fCaps & MOUSE_DEVCAP_MT_ABSOLUTE);
        fTPDev  = RT_BOOL(fCaps & MOUSE_DEVCAP_MT_RELATIVE);
        fCanAbs = i_supportsAbs(fCaps);
        fNeedsHostCursor = i_guestNeedsHostCursor();
    }
    mParent->i_onMouseCapabilityChange(fCanAbs, fRelDev, fTSDev, fTPDev, fNeedsHostCursor);
}


/**
 * @interface_method_impl{PDMIMOUSECONNECTOR,pfnReportModes}
 */
DECLCALLBACK(void) Mouse::i_mouseReportModes(PPDMIMOUSECONNECTOR pInterface, bool fRelative,
                                             bool fAbsolute, bool fMTAbsolute, bool fMTRelative)
{
    PDRVMAINMOUSE pDrv = RT_FROM_MEMBER(pInterface, DRVMAINMOUSE, IConnector);
    if (fRelative)
        pDrv->u32DevCaps |= MOUSE_DEVCAP_RELATIVE;
    else
        pDrv->u32DevCaps &= ~MOUSE_DEVCAP_RELATIVE;
    if (fAbsolute)
        pDrv->u32DevCaps |= MOUSE_DEVCAP_ABSOLUTE;
    else
        pDrv->u32DevCaps &= ~MOUSE_DEVCAP_ABSOLUTE;
    if (fMTAbsolute)
        pDrv->u32DevCaps |= MOUSE_DEVCAP_MT_ABSOLUTE;
    else
        pDrv->u32DevCaps &= ~MOUSE_DEVCAP_MT_ABSOLUTE;
    if (fMTRelative)
        pDrv->u32DevCaps |= MOUSE_DEVCAP_MT_RELATIVE;
    else
        pDrv->u32DevCaps &= ~MOUSE_DEVCAP_MT_RELATIVE;

    pDrv->pMouse->i_sendMouseCapsNotifications();
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
DECLCALLBACK(void *)  Mouse::i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS      pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVMAINMOUSE   pDrv    = PDMINS_2_DATA(pDrvIns, PDRVMAINMOUSE);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUSECONNECTOR, &pDrv->IConnector);
    return NULL;
}


/**
 * Destruct a mouse driver instance.
 *
 * @param   pDrvIns     The driver instance data.
 */
DECLCALLBACK(void) Mouse::i_drvDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVMAINMOUSE pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINMOUSE);
    LogFlow(("Mouse::drvDestruct: iInstance=%d\n", pDrvIns->iInstance));

    if (pThis->pMouse)
    {
        AutoWriteLock mouseLock(pThis->pMouse COMMA_LOCKVAL_SRC_POS);
        for (unsigned cDev = 0; cDev < MOUSE_MAX_DEVICES; ++cDev)
            if (pThis->pMouse->mpDrv[cDev] == pThis)
            {
                pThis->pMouse->mpDrv[cDev] = NULL;
                break;
            }
    }
}


/**
 * Construct a mouse driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
DECLCALLBACK(int) Mouse::i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    RT_NOREF(fFlags, pCfg);
    PDRVMAINMOUSE pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINMOUSE);
    LogFlow(("drvMainMouse_Construct: iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Validate configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "", "");
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * IBase.
     */
    pDrvIns->IBase.pfnQueryInterface        = Mouse::i_drvQueryInterface;

    pThis->IConnector.pfnReportModes        = Mouse::i_mouseReportModes;

    /*
     * Get the IMousePort interface of the above driver/device.
     */
    pThis->pUpPort = (PPDMIMOUSEPORT)pDrvIns->pUpBase->pfnQueryInterface(pDrvIns->pUpBase, PDMIMOUSEPORT_IID);
    if (!pThis->pUpPort)
    {
        AssertMsgFailed(("Configuration error: No mouse port interface above!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }

    /*
     * Get the Mouse object pointer and update the mpDrv member.
     */
    com::Guid uuid(COM_IIDOF(IMouse));
    IMouse *pIMouse = (IMouse *)PDMDrvHlpQueryGenericUserObject(pDrvIns, uuid.raw());
    if (!pIMouse)
    {
        AssertMsgFailed(("Configuration error: No/bad Mouse object!\n"));
        return VERR_NOT_FOUND;
    }
    pThis->pMouse = static_cast<Mouse *>(pIMouse);

    unsigned cDev;
    {
        AutoWriteLock mouseLock(pThis->pMouse COMMA_LOCKVAL_SRC_POS);

        for (cDev = 0; cDev < MOUSE_MAX_DEVICES; ++cDev)
            if (!pThis->pMouse->mpDrv[cDev])
            {
                pThis->pMouse->mpDrv[cDev] = pThis;
                break;
            }
    }
    if (cDev == MOUSE_MAX_DEVICES)
        return VERR_NO_MORE_HANDLES;

    return VINF_SUCCESS;
}


/**
 * Main mouse driver registration record.
 */
const PDMDRVREG Mouse::DrvReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "MainMouse",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Main mouse driver (Main as in the API).",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_MOUSE,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVMAINMOUSE),
    /* pfnConstruct */
    Mouse::i_drvConstruct,
    /* pfnDestruct */
    Mouse::i_drvDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
