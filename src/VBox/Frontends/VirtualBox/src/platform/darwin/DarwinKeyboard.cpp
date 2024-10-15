/* $Id: DarwinKeyboard.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Declarations of utility functions for handling Darwin Keyboard specific tasks.
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

/* Defines: */
#define VBOX_WITH_KBD_LEDS_SYNC
//#define VBOX_WITHOUT_KBD_LEDS_SYNC_FILTERING

/* GUI includes: */
#include "UILoggingDefs.h"
#include "DarwinKeyboard.h"
#ifndef USE_HID_FOR_MODIFIERS
# include "CocoaEventHelper.h"
#endif

/* Other VBox includes: */
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/time.h>
#ifdef DEBUG_PRINTF
# include <iprt/stream.h>
#endif
#ifdef VBOX_WITH_KBD_LEDS_SYNC
# include <iprt/errcore.h>
# include <iprt/semaphore.h>
# include <VBox/sup.h>
#endif

/* External includes: */
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/usb/USB.h>
#ifdef USE_HID_FOR_MODIFIERS
# include <CoreFoundation/CoreFoundation.h>
# include <IOKit/hid/IOHIDUsageTables.h>
# include <mach/mach.h>
# include <mach/mach_error.h>
#endif
#ifdef VBOX_WITH_KBD_LEDS_SYNC
# include <IOKit/IOMessage.h>
# include <IOKit/usb/IOUSBLib.h>
#endif


RT_C_DECLS_BEGIN
/* Private interface in 10.3 and later. */
typedef int CGSConnection;
typedef enum
{
    kCGSGlobalHotKeyInvalid = -1 /* bird */,
    kCGSGlobalHotKeyEnable = 0,
    kCGSGlobalHotKeyDisable = 1,
    kCGSGlobalHotKeyDisableExceptUniversalAccess = 2,
    kCGSGlobalHotKeySleep = 4 /* dsen */,
    kCGSGlobalHotKeyScreenSaver = 6 /* dsen */,
} CGSGlobalHotKeyOperatingMode;
extern CGSConnection _CGSDefaultConnection(void);
extern CGError CGSGetGlobalHotKeyOperatingMode(CGSConnection Connection, CGSGlobalHotKeyOperatingMode *enmMode);
extern CGError CGSSetGlobalHotKeyOperatingMode(CGSConnection Connection, CGSGlobalHotKeyOperatingMode enmMode);
RT_C_DECLS_END


/* Defined Constants And Macros: */
#define QZ_RMETA        0x36
#define QZ_LMETA        0x37
#define QZ_LSHIFT       0x38
#define QZ_CAPSLOCK     0x39
#define QZ_LALT         0x3A
#define QZ_LCTRL        0x3B
#define QZ_RSHIFT       0x3C
#define QZ_RALT         0x3D
#define QZ_RCTRL        0x3E
// Found the definition of the fn-key in:
// http://stuff.mit.edu/afs/sipb/project/darwin/src/modules/IOHIDFamily/IOHIDSystem/IOHIKeyboardMapper.cpp &
// http://stuff.mit.edu/afs/sipb/project/darwin/src/modules/AppleADBKeyboard/AppleADBKeyboard.cpp
// Maybe we need this in the future.
#define QZ_FN           0x3F
#define QZ_NUMLOCK      0x47
/** Short hand for an extended key. */
#define K_EX            VBOXKEY_EXTENDED
/** Short hand for a modifier key. */
#define K_MOD           VBOXKEY_MODIFIER
/** Short hand for a lock key. */
#define K_LOCK          VBOXKEY_LOCK
#ifdef USE_HID_FOR_MODIFIERS
/** An attempt at catching reference leaks. */
# define MY_CHECK_CREFS(cRefs)   do { AssertMsg(cRefs < 25, ("%ld\n", cRefs)); NOREF(cRefs); } while (0)
#endif


/** This is derived partially from SDL_QuartzKeys.h and partially from testing.
  * (The funny thing about the virtual scan codes on the mac is that they aren't
  * offically documented, which is rather silly to say the least. Thus, the need
  * for looking at SDL and other odd places for docs.) */
static const uint16_t g_aDarwinToSet1[] =
{
    /* set-1                           SDL_QuartzKeys.h */
    0x1e,                       /* QZ_a            0x00 */
    0x1f,                       /* QZ_s            0x01 */
    0x20,                       /* QZ_d            0x02 */
    0x21,                       /* QZ_f            0x03 */
    0x23,                       /* QZ_h            0x04 */
    0x22,                       /* QZ_g            0x05 */
    0x2c,                       /* QZ_z            0x06 */
    0x2d,                       /* QZ_x            0x07 */
    0x2e,                       /* QZ_c            0x08 */
    0x2f,                       /* QZ_v            0x09 */
    0x56,                       /* between lshift and z. 'INT 1'? */
    0x30,                       /* QZ_b            0x0B */
    0x10,                       /* QZ_q            0x0C */
    0x11,                       /* QZ_w            0x0D */
    0x12,                       /* QZ_e            0x0E */
    0x13,                       /* QZ_r            0x0F */
    0x15,                       /* QZ_y            0x10 */
    0x14,                       /* QZ_t            0x11 */
    0x02,                       /* QZ_1            0x12 */
    0x03,                       /* QZ_2            0x13 */
    0x04,                       /* QZ_3            0x14 */
    0x05,                       /* QZ_4            0x15 */
    0x07,                       /* QZ_6            0x16 */
    0x06,                       /* QZ_5            0x17 */
    0x0d,                       /* QZ_EQUALS       0x18 */
    0x0a,                       /* QZ_9            0x19 */
    0x08,                       /* QZ_7            0x1A */
    0x0c,                       /* QZ_MINUS        0x1B */
    0x09,                       /* QZ_8            0x1C */
    0x0b,                       /* QZ_0            0x1D */
    0x1b,                       /* QZ_RIGHTBRACKET 0x1E */
    0x18,                       /* QZ_o            0x1F */
    0x16,                       /* QZ_u            0x20 */
    0x1a,                       /* QZ_LEFTBRACKET  0x21 */
    0x17,                       /* QZ_i            0x22 */
    0x19,                       /* QZ_p            0x23 */
    0x1c,                       /* QZ_RETURN       0x24 */
    0x26,                       /* QZ_l            0x25 */
    0x24,                       /* QZ_j            0x26 */
    0x28,                       /* QZ_QUOTE        0x27 */
    0x25,                       /* QZ_k            0x28 */
    0x27,                       /* QZ_SEMICOLON    0x29 */
    0x2b,                       /* QZ_BACKSLASH    0x2A */
    0x33,                       /* QZ_COMMA        0x2B */
    0x35,                       /* QZ_SLASH        0x2C */
    0x31,                       /* QZ_n            0x2D */
    0x32,                       /* QZ_m            0x2E */
    0x34,                       /* QZ_PERIOD       0x2F */
    0x0f,                       /* QZ_TAB          0x30 */
    0x39,                       /* QZ_SPACE        0x31 */
    0x29,                       /* QZ_BACKQUOTE    0x32 */
    0x0e,                       /* QZ_BACKSPACE    0x33 */
    0x9c,                       /* QZ_IBOOK_ENTER  0x34 */
    0x01,                       /* QZ_ESCAPE       0x35 */
    0x5c|K_EX|K_MOD,            /* QZ_RMETA        0x36 */
    0x5b|K_EX|K_MOD,            /* QZ_LMETA        0x37 */
    0x2a|K_MOD,                 /* QZ_LSHIFT       0x38 */
    0x3a|K_LOCK,                /* QZ_CAPSLOCK     0x39 */
    0x38|K_MOD,                 /* QZ_LALT         0x3A */
    0x1d|K_MOD,                 /* QZ_LCTRL        0x3B */
    0x36|K_MOD,                 /* QZ_RSHIFT       0x3C */
    0x38|K_EX|K_MOD,            /* QZ_RALT         0x3D */
    0x1d|K_EX|K_MOD,            /* QZ_RCTRL        0x3E */
       0,                       /*                      */
       0,                       /*                      */
    0x53,                       /* QZ_KP_PERIOD    0x41 */
       0,                       /*                      */
    0x37,                       /* QZ_KP_MULTIPLY  0x43 */
       0,                       /*                      */
    0x4e,                       /* QZ_KP_PLUS      0x45 */
       0,                       /*                      */
    0x45|K_LOCK,                /* QZ_NUMLOCK      0x47 */
       0,                       /*                      */
       0,                       /*                      */
       0,                       /*                      */
    0x35|K_EX,                  /* QZ_KP_DIVIDE    0x4B */
    0x1c|K_EX,                  /* QZ_KP_ENTER     0x4C */
       0,                       /*                      */
    0x4a,                       /* QZ_KP_MINUS     0x4E */
       0,                       /*                      */
       0,                       /*                      */
    0x0d/*?*/,                  /* QZ_KP_EQUALS    0x51 */
    0x52,                       /* QZ_KP0          0x52 */
    0x4f,                       /* QZ_KP1          0x53 */
    0x50,                       /* QZ_KP2          0x54 */
    0x51,                       /* QZ_KP3          0x55 */
    0x4b,                       /* QZ_KP4          0x56 */
    0x4c,                       /* QZ_KP5          0x57 */
    0x4d,                       /* QZ_KP6          0x58 */
    0x47,                       /* QZ_KP7          0x59 */
       0,                       /*                      */
    0x48,                       /* QZ_KP8          0x5B */
    0x49,                       /* QZ_KP9          0x5C */
    0x7d,                       /* yen, | (JIS)    0x5D */
    0x73,                       /* _, ro (JIS)     0x5E */
       0,                       /*                      */
    0x3f,                       /* QZ_F5           0x60 */
    0x40,                       /* QZ_F6           0x61 */
    0x41,                       /* QZ_F7           0x62 */
    0x3d,                       /* QZ_F3           0x63 */
    0x42,                       /* QZ_F8           0x64 */
    0x43,                       /* QZ_F9           0x65 */
    0x29,                       /* Zen/Han (JIS)   0x66 */
    0x57,                       /* QZ_F11          0x67 */
    0x29,                       /* Zen/Han (JIS)   0x68 */
    0x37|K_EX,                  /* QZ_PRINT / F13  0x69 */
    0x63,                       /* QZ_F16          0x6A */
    0x46|K_LOCK,                /* QZ_SCROLLOCK    0x6B */
       0,                       /*                      */
    0x44,                       /* QZ_F10          0x6D */
    0x5d|K_EX,                  /*                      */
    0x58,                       /* QZ_F12          0x6F */
       0,                       /*                      */
       0/* 0xe1,0x1d,0x45*/,    /* QZ_PAUSE        0x71 */
    0x52|K_EX,                  /* QZ_INSERT / HELP 0x72 */
    0x47|K_EX,                  /* QZ_HOME         0x73 */
    0x49|K_EX,                  /* QZ_PAGEUP       0x74 */
    0x53|K_EX,                  /* QZ_DELETE       0x75 */
    0x3e,                       /* QZ_F4           0x76 */
    0x4f|K_EX,                  /* QZ_END          0x77 */
    0x3c,                       /* QZ_F2           0x78 */
    0x51|K_EX,                  /* QZ_PAGEDOWN     0x79 */
    0x3b,                       /* QZ_F1           0x7A */
    0x4b|K_EX,                  /* QZ_LEFT         0x7B */
    0x4d|K_EX,                  /* QZ_RIGHT        0x7C */
    0x50|K_EX,                  /* QZ_DOWN         0x7D */
    0x48|K_EX,                  /* QZ_UP           0x7E */
       0,/*0x5e|K_EX*/          /* QZ_POWER        0x7F */ /* have different break key! */
                                                           /* do NEVER deliver the Power
                                                            * scancode as e.g. Windows will
                                                            * handle it, @bugref{7692}. */
};


/** Holds whether we've connected or not. */
static bool g_fConnectedToCGS = false;
/** Holds the cached connection. */
static CGSConnection g_CGSConnection;


#ifdef USE_HID_FOR_MODIFIERS

/** Holds the IO Master Port. */
static mach_port_t g_MasterPort = NULL;

/** Holds the amount of keyboards in the cache. */
static unsigned g_cKeyboards = 0;
/** Array of cached keyboard data. */
static struct KeyboardCacheData
{
    /** The device interface. */
    IOHIDDeviceInterface  **ppHidDeviceInterface;
    /** The queue interface. */
    IOHIDQueueInterface   **ppHidQueueInterface;

    /** Cookie translation array. */
    struct KeyboardCacheCookie
    {
        /** The cookie. */
        IOHIDElementCookie  Cookie;
        /** The corresponding modifier mask. */
        uint32_t            fMask;
    }                       aCookies[64];
    /** Number of cookies in the array. */
    unsigned                cCookies;
}                   g_aKeyboards[128];
/** Holds the keyboard cache creation timestamp. */
static uint64_t     g_u64KeyboardTS = 0;

/** Holds the HID queue status. */
static bool         g_fHIDQueueEnabled;
/** Holds the current modifier mask. */
static uint32_t     g_fHIDModifierMask;
/** Holds the old modifier mask. */
static uint32_t     g_fOldHIDModifierMask;

#endif /* USE_HID_FOR_MODIFIERS */


#ifdef VBOX_WITH_KBD_LEDS_SYNC

#define VBOX_BOOL_TO_STR_STATE(x) (x) ? "ON" : "OFF"
/** HID LEDs synchronization data: LED states. */
typedef struct VBoxLedState_t
{
    /** Holds the state of NUM LOCK. */
    bool fNumLockOn;
    /** Holds the  state of CAPS LOCK. */
    bool fCapsLockOn;
    /** Holds the  state of SCROLL LOCK. */
    bool fScrollLockOn;
} VBoxLedState_t;

/** HID LEDs synchronization data: keyboard states. */
typedef struct VBoxKbdState_t
{
    /** Holds the reference to IOKit HID device. */
    IOHIDDeviceRef    pDevice;
    /** Holds the LED states. */
    VBoxLedState_t    LED;
    /** Holds the  pointer to a VBoxHidsState_t instance where VBoxKbdState_t instance is stored. */
    void             *pParentContainer;
    /** Holds the position in global storage (used to simplify CFArray navigation when removing detached device). */
    CFIndex           idxPosition;
    /** Holds the KBD CAPS LOCK key hold timeout (some Apple keyboards only). */
    uint64_t          cCapsLockTimeout;
    /** Holds the HID Location ID: unique for an USB device registered in the system. */
    uint32_t          idLocation;
} VBoxKbdState_t;

/** A struct that used to pass input event info from IOKit callback to a Carbon one */
typedef struct VBoxKbdEvent_t
{
    VBoxKbdState_t *pKbd;
    uint32_t        iKeyCode;
    uint64_t        tsKeyDown;
} VBoxKbdEvent_t;

/** HID LEDs synchronization data: IOKit specific data. */
typedef struct VBoxHidsState_t
{
    /** Holds the IOKit HID manager reference. */
    IOHIDManagerRef     hidManagerRef;
    /** Holds the array which consists of VBoxKbdState_t elements. */
    CFMutableArrayRef   pDeviceCollection;
    /** Holds the LED states that were stored during last broadcast and reflect a guest LED states. */
    VBoxLedState_t      guestState;

    /** Holds the queue which will be appended in IOKit input callback. Carbon input callback will extract data from it. */
    CFMutableArrayRef   pFifoEventQueue;
    /** Holds the lock for pFifoEventQueue. */
    RTSEMMUTEX          fifoEventQueueLock;

    /** Holds the IOService notification reference: USB HID device matching. */
    io_iterator_t         pUsbHidDeviceMatchNotify;
    /** Holds the IOService notification reference: USB HID general interest notifications (IOService messages). */
    io_iterator_t         pUsbHidGeneralInterestNotify;
    /** Holds the IOService notification port reference: device match and device general interest message. */
    IONotificationPortRef pNotificationPrortRef;

    CFMachPortRef       pTapRef;
    CFRunLoopSourceRef  pLoopSourceRef;
} VBoxHidsState_t;

#endif /* VBOX_WITH_KBD_LEDS_SYNC */


unsigned DarwinKeycodeToSet1Scancode(unsigned uKeyCode)
{
    if (uKeyCode >= RT_ELEMENTS(g_aDarwinToSet1))
        return 0;
    return g_aDarwinToSet1[uKeyCode];
}

UInt32 DarwinAdjustModifierMask(UInt32 fModifiers, const void *pvCocoaEvent)
{
    /* Check if there is anything to adjust and perform the adjustment. */
    if (fModifiers & (shiftKey | rightShiftKey | controlKey | rightControlKey | optionKey | rightOptionKey | cmdKey | kEventKeyModifierRightCmdKeyMask))
    {
#ifndef USE_HID_FOR_MODIFIERS
        // WORKAROUND:
        // Convert the Cocoa modifiers to Carbon ones (the Cocoa modifier
        // definitions are tucked away in Objective-C headers, unfortunately).
        //
        // Update: CGEventTypes.h includes what looks like the Cocoa modifiers
        //         and the NX_* defines should be available as well. We should look
        //         into ways to intercept the CG (core graphics) events in the Carbon
        //         based setup and get rid of all this HID mess. */
        AssertPtr(pvCocoaEvent);
        //::darwinPrintEvent("dbg-adjMods: ", pvCocoaEvent);
        uint32_t fAltModifiers = ::darwinEventModifierFlagsXlated(pvCocoaEvent);
#else  /* USE_HID_FOR_MODIFIERS */
        /* Update the keyboard cache. */
        darwinHIDKeyboardCacheUpdate();
        const UInt32 fAltModifiers = g_fHIDModifierMask;
#endif /* USE_HID_FOR_MODIFIERS */

#ifdef DEBUG_PRINTF
        RTPrintf("dbg-fAltModifiers=%#x fModifiers=%#x", fAltModifiers, fModifiers);
#endif
        if (   (fModifiers    & (rightShiftKey | shiftKey))
            && (fAltModifiers & (rightShiftKey | shiftKey)))
        {
            fModifiers &= ~(rightShiftKey | shiftKey);
            fModifiers |= fAltModifiers & (rightShiftKey | shiftKey);
        }

        if (   (fModifiers    & (rightControlKey | controlKey))
            && (fAltModifiers & (rightControlKey | controlKey)))
        {
            fModifiers &= ~(rightControlKey | controlKey);
            fModifiers |= fAltModifiers & (rightControlKey | controlKey);
        }

        if (   (fModifiers    & (optionKey | rightOptionKey))
            && (fAltModifiers & (optionKey | rightOptionKey)))
        {
            fModifiers &= ~(optionKey | rightOptionKey);
            fModifiers |= fAltModifiers & (optionKey | rightOptionKey);
        }

        if (   (fModifiers    & (cmdKey | kEventKeyModifierRightCmdKeyMask))
            && (fAltModifiers & (cmdKey | kEventKeyModifierRightCmdKeyMask)))
        {
            fModifiers &= ~(cmdKey | kEventKeyModifierRightCmdKeyMask);
            fModifiers |= fAltModifiers & (cmdKey | kEventKeyModifierRightCmdKeyMask);
        }
#ifdef DEBUG_PRINTF
        RTPrintf(" -> %#x\n", fModifiers);
#endif
    }
    return fModifiers;
}

unsigned DarwinModifierMaskToSet1Scancode(UInt32 fModifiers)
{
    unsigned uScanCode = DarwinModifierMaskToDarwinKeycode(fModifiers);
    if (uScanCode < RT_ELEMENTS(g_aDarwinToSet1))
        uScanCode = g_aDarwinToSet1[uScanCode];
    else
        Assert(uScanCode == ~0U);
    return uScanCode;
}

unsigned DarwinModifierMaskToDarwinKeycode(UInt32 fModifiers)
{
    unsigned uKeyCode;

    /** @todo find symbols for these keycodes... */
    fModifiers &= shiftKey | rightShiftKey | controlKey | rightControlKey | optionKey | rightOptionKey | cmdKey
                | kEventKeyModifierRightCmdKeyMask | kEventKeyModifierNumLockMask | alphaLock | kEventKeyModifierFnMask;
    if (fModifiers == shiftKey)
        uKeyCode = QZ_LSHIFT;
    else if (fModifiers == rightShiftKey)
        uKeyCode = QZ_RSHIFT;
    else if (fModifiers == controlKey)
        uKeyCode = QZ_LCTRL;
    else if (fModifiers == rightControlKey)
        uKeyCode = QZ_RCTRL;
    else if (fModifiers == optionKey)
        uKeyCode = QZ_LALT;
    else if (fModifiers == rightOptionKey)
        uKeyCode = QZ_RALT;
    else if (fModifiers == cmdKey)
        uKeyCode = QZ_LMETA;
    else if (fModifiers == kEventKeyModifierRightCmdKeyMask /* hack */)
        uKeyCode = QZ_RMETA;
    else if (fModifiers == alphaLock)
        uKeyCode = QZ_CAPSLOCK;
    else if (fModifiers == kEventKeyModifierNumLockMask)
        uKeyCode = QZ_NUMLOCK;
    else if (fModifiers == kEventKeyModifierFnMask)
        uKeyCode = QZ_FN;
    else if (fModifiers == 0)
        uKeyCode = 0;
    else
        uKeyCode = ~0U; /* multiple */
    return uKeyCode;
}

UInt32 DarwinKeyCodeToDarwinModifierMask(unsigned uKeyCode)
{
    UInt32 fModifiers;

    /** @todo find symbols for these keycodes... */
    if (uKeyCode == QZ_LSHIFT)
        fModifiers = shiftKey;
    else if (uKeyCode == QZ_RSHIFT)
        fModifiers = rightShiftKey;
    else if (uKeyCode == QZ_LCTRL)
        fModifiers = controlKey;
    else if (uKeyCode == QZ_RCTRL)
        fModifiers = rightControlKey;
    else if (uKeyCode == QZ_LALT)
        fModifiers = optionKey;
    else if (uKeyCode == QZ_RALT)
        fModifiers = rightOptionKey;
    else if (uKeyCode == QZ_LMETA)
        fModifiers = cmdKey;
    else if (uKeyCode == QZ_RMETA)
        fModifiers = kEventKeyModifierRightCmdKeyMask; /* hack */
    else if (uKeyCode == QZ_CAPSLOCK)
        fModifiers = alphaLock;
    else if (uKeyCode == QZ_NUMLOCK)
        fModifiers = kEventKeyModifierNumLockMask;
    else if (uKeyCode == QZ_FN)
        fModifiers = kEventKeyModifierFnMask;
    else
        fModifiers = 0;
    return fModifiers;
}


void DarwinDisableGlobalHotKeys(bool fDisable)
{
    /* Prevent LogRel clogging: */
    static unsigned s_cComplaints = 0;

    /* Lazy connect to the core graphics service. */
    if (!g_fConnectedToCGS)
    {
        g_CGSConnection = _CGSDefaultConnection();
        g_fConnectedToCGS = true;
    }

    /* Get the current mode. */
    CGSGlobalHotKeyOperatingMode enmMode = kCGSGlobalHotKeyInvalid;
    CGSGetGlobalHotKeyOperatingMode(g_CGSConnection, &enmMode);
    if (    enmMode != kCGSGlobalHotKeyEnable
        &&  enmMode != kCGSGlobalHotKeyDisable
        &&  enmMode != kCGSGlobalHotKeyDisableExceptUniversalAccess)
    {
        /* We are silently ignoring case when
         * host is in sleep or screensaver mode: */
        if (   enmMode != kCGSGlobalHotKeySleep
            && enmMode != kCGSGlobalHotKeyScreenSaver)
        {
            /* Otherwise we should warn about the unknown mode we met: */
            AssertMsgFailed(("%d\n", enmMode));
        }
        if (s_cComplaints++ < 32)
            LogRel(("DarwinDisableGlobalHotKeys: Unexpected enmMode=%d\n", enmMode));
        return;
    }

    /* Calc the new mode. */
    if (fDisable)
    {
        if (enmMode != kCGSGlobalHotKeyEnable)
            return;
        enmMode = kCGSGlobalHotKeyDisableExceptUniversalAccess;
    }
    else
    {
        if (enmMode != kCGSGlobalHotKeyDisableExceptUniversalAccess)
            return;
        enmMode = kCGSGlobalHotKeyEnable;
    }

    /* Try set it and check the actual result. */
    CGSSetGlobalHotKeyOperatingMode(g_CGSConnection, enmMode);
    CGSGlobalHotKeyOperatingMode enmNewMode = kCGSGlobalHotKeyInvalid;
    CGSGetGlobalHotKeyOperatingMode(g_CGSConnection, &enmNewMode);
    if (enmNewMode != enmMode)
    {
        /* We are silently ignoring case when
         * host is in sleep or screensaver mode: */
        if (   enmNewMode != kCGSGlobalHotKeySleep
            && enmNewMode != kCGSGlobalHotKeyScreenSaver)
        {
            /* Otherwise we should warn about the unknown mode we met while trying to enable hot keys: */
            AssertMsg(enmMode == kCGSGlobalHotKeyEnable, ("enmNewMode=%d enmMode=%d\n", enmNewMode, enmMode));
        }
        if (s_cComplaints++ < 32)
            LogRel(("DarwinDisableGlobalHotKeys: Failed to change mode; enmNewMode=%d enmMode=%d\n", enmNewMode, enmMode));
    }
}


#ifdef USE_HID_FOR_MODIFIERS

/** Callback function for consuming queued events.
  * @param   pvTarget  Brings the queue?
  * @param   rcIn      Brings what?
  * @param   pvRefcon  Brings the pointer to the keyboard cache entry.
  * @param   pvSender  Brings what? */
static void darwinQueueCallback(void *pvTarget, IOReturn rcIn, void *pvRefcon, void *pvSender)
{
    struct KeyboardCacheData *pKeyboardEntry = (struct KeyboardCacheData *)pvRefcon;
    if (!pKeyboardEntry->ppHidQueueInterface)
        return;
    NOREF(pvTarget);
    NOREF(rcIn);
    NOREF(pvSender);

    /* Consume the events. */
    g_fOldHIDModifierMask = g_fHIDModifierMask;
    for (;;)
    {
#ifdef DEBUG_PRINTF
        RTPrintf("dbg-ev: "); RTStrmFlush(g_pStdOut);
#endif
        IOHIDEventStruct Event;
        AbsoluteTime ZeroTime = {0,0};
        IOReturn rc = (*pKeyboardEntry->ppHidQueueInterface)->getNextEvent(pKeyboardEntry->ppHidQueueInterface,
                                                                           &Event, ZeroTime, 0);
        if (rc != kIOReturnSuccess)
            break;

        /* Translate the cookie value to a modifier mask. */
        uint32_t fMask = 0;
        unsigned i = pKeyboardEntry->cCookies;
        while (i-- > 0)
        {
            if (pKeyboardEntry->aCookies[i].Cookie == Event.elementCookie)
            {
                fMask = pKeyboardEntry->aCookies[i].fMask;
                break;
            }
        }

        /* Adjust the modifier mask. */
        if (Event.value)
            g_fHIDModifierMask |= fMask;
        else
            g_fHIDModifierMask &= ~fMask;
#ifdef DEBUG_PRINTF
        RTPrintf("t=%d c=%#x v=%#x cblv=%d lv=%p m=%#X\n", Event.type, Event.elementCookie, Event.value, Event.longValueSize, Event.value, fMask); RTStrmFlush(g_pStdOut);
#endif
    }
#ifdef DEBUG_PRINTF
    RTPrintf("dbg-ev: done\n"); RTStrmFlush(g_pStdOut);
#endif
}

/* Forward declaration for darwinBruteForcePropertySearch. */
static void darwinBruteForcePropertySearch(CFDictionaryRef DictRef, struct KeyboardCacheData *pKeyboardEntry);

/** Element enumeration callback. */
static void darwinBruteForcePropertySearchApplier(const void *pvValue, void *pvCacheEntry)
{
    if (CFGetTypeID(pvValue) == CFDictionaryGetTypeID())
        darwinBruteForcePropertySearch((CFMutableDictionaryRef)pvValue, (struct KeyboardCacheData *)pvCacheEntry);
}

/** Recurses through the keyboard properties looking for certain keys. */
static void darwinBruteForcePropertySearch(CFDictionaryRef DictRef, struct KeyboardCacheData *pKeyboardEntry)
{
    CFTypeRef ObjRef;

    /* Check for the usage page and usage key we want. */
    long lUsage;
    ObjRef = CFDictionaryGetValue(DictRef, CFSTR(kIOHIDElementUsageKey));
    if (    ObjRef
        &&  CFGetTypeID(ObjRef) == CFNumberGetTypeID()
        &&  CFNumberGetValue((CFNumberRef)ObjRef, kCFNumberLongType, &lUsage))
    {
        switch (lUsage)
        {
            case kHIDUsage_KeyboardLeftControl:
            case kHIDUsage_KeyboardLeftShift:
            case kHIDUsage_KeyboardLeftAlt:
            case kHIDUsage_KeyboardLeftGUI:
            case kHIDUsage_KeyboardRightControl:
            case kHIDUsage_KeyboardRightShift:
            case kHIDUsage_KeyboardRightAlt:
            case kHIDUsage_KeyboardRightGUI:
            {
                long lPage;
                ObjRef = CFDictionaryGetValue(DictRef, CFSTR(kIOHIDElementUsagePageKey));
                if (    !ObjRef
                    ||  CFGetTypeID(ObjRef) != CFNumberGetTypeID()
                    ||  !CFNumberGetValue((CFNumberRef)ObjRef, kCFNumberLongType, &lPage)
                    ||  lPage != kHIDPage_KeyboardOrKeypad)
                    break;

                if (pKeyboardEntry->cCookies >= RT_ELEMENTS(pKeyboardEntry->aCookies))
                {
                    AssertMsgFailed(("too many cookies!\n"));
                    break;
                }

                /* Get the cookie and modifier mask. */
                long lCookie;
                ObjRef = CFDictionaryGetValue(DictRef, CFSTR(kIOHIDElementCookieKey));
                if (    !ObjRef
                    ||  CFGetTypeID(ObjRef) != CFNumberGetTypeID()
                    ||  !CFNumberGetValue((CFNumberRef)ObjRef, kCFNumberLongType, &lCookie))
                    break;

                uint32_t fMask;
                switch (lUsage)
                {
                    case kHIDUsage_KeyboardLeftControl : fMask = controlKey; break;
                    case kHIDUsage_KeyboardLeftShift   : fMask = shiftKey; break;
                    case kHIDUsage_KeyboardLeftAlt     : fMask = optionKey; break;
                    case kHIDUsage_KeyboardLeftGUI     : fMask = cmdKey; break;
                    case kHIDUsage_KeyboardRightControl: fMask = rightControlKey; break;
                    case kHIDUsage_KeyboardRightShift  : fMask = rightShiftKey; break;
                    case kHIDUsage_KeyboardRightAlt    : fMask = rightOptionKey; break;
                    case kHIDUsage_KeyboardRightGUI    : fMask = kEventKeyModifierRightCmdKeyMask; break;
                    default: AssertMsgFailed(("%ld\n",lUsage)); fMask = 0; break;
                }

                /* If we've got a queue, add the cookie to the queue. */
                if (pKeyboardEntry->ppHidQueueInterface)
                {
                    IOReturn rc = (*pKeyboardEntry->ppHidQueueInterface)->addElement(pKeyboardEntry->ppHidQueueInterface, (IOHIDElementCookie)lCookie, 0);
                    AssertMsg(rc == kIOReturnSuccess, ("rc=%d\n", rc));
#ifdef DEBUG_PRINTF
                    RTPrintf("dbg-add: u=%#lx c=%#lx\n", lUsage, lCookie);
#endif
                }

                /* Add the cookie to the keyboard entry. */
                pKeyboardEntry->aCookies[pKeyboardEntry->cCookies].Cookie = (IOHIDElementCookie)lCookie;
                pKeyboardEntry->aCookies[pKeyboardEntry->cCookies].fMask = fMask;
                ++pKeyboardEntry->cCookies;
                break;
            }
        }
    }


    /* Get the elements key and recursively iterate the elements looking for they key cookies. */
    ObjRef = CFDictionaryGetValue(DictRef, CFSTR(kIOHIDElementKey));
    if (    ObjRef
        &&  CFGetTypeID(ObjRef) == CFArrayGetTypeID())
    {
        CFArrayRef ArrayObjRef = (CFArrayRef)ObjRef;
        CFRange Range = {0, CFArrayGetCount(ArrayObjRef)};
        CFArrayApplyFunction(ArrayObjRef, Range, darwinBruteForcePropertySearchApplier, pKeyboardEntry);
    }
}

/** Creates a keyboard cache entry.
  * @param  pKeyboardEntry  Brings the pointer to the entry.
  * @param  KeyboardDevice  Brings the keyboard device to create the entry for. */
static bool darwinHIDKeyboardCacheCreateEntry(struct KeyboardCacheData *pKeyboardEntry, io_object_t KeyboardDevice)
{
    unsigned long cRefs = 0;
    memset(pKeyboardEntry, 0, sizeof(*pKeyboardEntry));

    /* Query the HIDDeviceInterface for this HID (keyboard) object. */
    SInt32 Score = 0;
    IOCFPlugInInterface **ppPlugInInterface = NULL;
    IOReturn rc = IOCreatePlugInInterfaceForService(KeyboardDevice, kIOHIDDeviceUserClientTypeID,
                                                    kIOCFPlugInInterfaceID, &ppPlugInInterface, &Score);
    if (rc == kIOReturnSuccess)
    {
        IOHIDDeviceInterface **ppHidDeviceInterface = NULL;
        HRESULT hrc = (*ppPlugInInterface)->QueryInterface(ppPlugInInterface,
                                                           CFUUIDGetUUIDBytes(kIOHIDDeviceInterfaceID),
                                                           (LPVOID *)&ppHidDeviceInterface);
        cRefs = (*ppPlugInInterface)->Release(ppPlugInInterface); MY_CHECK_CREFS(cRefs);
        ppPlugInInterface = NULL;
        if (hrc == S_OK)
        {
            rc = (*ppHidDeviceInterface)->open(ppHidDeviceInterface, 0);
            if (rc == kIOReturnSuccess)
            {
                /* Create a removal callback. */
                /** @todo */

                /* Create the queue so we can insert elements while searching the properties. */
                IOHIDQueueInterface   **ppHidQueueInterface = (*ppHidDeviceInterface)->allocQueue(ppHidDeviceInterface);
                if (ppHidQueueInterface)
                {
                    rc = (*ppHidQueueInterface)->create(ppHidQueueInterface, 0, 32);
                    if (rc != kIOReturnSuccess)
                    {
                        AssertMsgFailed(("rc=%d\n", rc));
                        cRefs = (*ppHidQueueInterface)->Release(ppHidQueueInterface); MY_CHECK_CREFS(cRefs);
                        ppHidQueueInterface = NULL;
                    }
                }
                else
                    AssertFailed();
                pKeyboardEntry->ppHidQueueInterface = ppHidQueueInterface;

                /* Brute force getting of attributes. */
                /** @todo read up on how to do this in a less resource intensive way! Suggestions are welcome! */
                CFMutableDictionaryRef PropertiesRef = 0;
                kern_return_t krc = IORegistryEntryCreateCFProperties(KeyboardDevice, &PropertiesRef, kCFAllocatorDefault, kNilOptions);
                if (krc == KERN_SUCCESS)
                {
                    darwinBruteForcePropertySearch(PropertiesRef, pKeyboardEntry);
                    CFRelease(PropertiesRef);
                }
                else
                    AssertMsgFailed(("krc=%#x\n", krc));

                if (ppHidQueueInterface)
                {
                    /* Now install our queue callback. */
                    CFRunLoopSourceRef RunLoopSrcRef = NULL;
                    rc = (*ppHidQueueInterface)->createAsyncEventSource(ppHidQueueInterface, &RunLoopSrcRef);
                    if (rc == kIOReturnSuccess)
                    {
                        CFRunLoopRef RunLoopRef = (CFRunLoopRef)GetCFRunLoopFromEventLoop(GetMainEventLoop());
                        CFRunLoopAddSource(RunLoopRef, RunLoopSrcRef, kCFRunLoopDefaultMode);
                    }

                    /* Now install our queue callback. */
                    rc = (*ppHidQueueInterface)->setEventCallout(ppHidQueueInterface, darwinQueueCallback, ppHidQueueInterface, pKeyboardEntry);
                    if (rc != kIOReturnSuccess)
                        AssertMsgFailed(("rc=%d\n", rc));
                }

                /* Complete the new keyboard cache entry. */
                pKeyboardEntry->ppHidDeviceInterface = ppHidDeviceInterface;
                pKeyboardEntry->ppHidQueueInterface = ppHidQueueInterface;
                return true;
            }

            AssertMsgFailed(("rc=%d\n", rc));
            cRefs = (*ppHidDeviceInterface)->Release(ppHidDeviceInterface); MY_CHECK_CREFS(cRefs);
        }
        else
            AssertMsgFailed(("hrc=%#x\n", hrc));
    }
    else
        AssertMsgFailed(("rc=%d\n", rc));

    return false;
}

/** Destroys a keyboard cache entry. */
static void darwinHIDKeyboardCacheDestroyEntry(struct KeyboardCacheData *pKeyboardEntry)
{
    unsigned long cRefs;

    /* Destroy the queue. */
    if (pKeyboardEntry->ppHidQueueInterface)
    {
        IOHIDQueueInterface **ppHidQueueInterface = pKeyboardEntry->ppHidQueueInterface;
        pKeyboardEntry->ppHidQueueInterface = NULL;

        /* Stop it just in case we haven't done so. doesn't really matter I think. */
        (*ppHidQueueInterface)->stop(ppHidQueueInterface);

        /* Deal with the run loop source. */
        CFRunLoopSourceRef RunLoopSrcRef = (*ppHidQueueInterface)->getAsyncEventSource(ppHidQueueInterface);
        if (RunLoopSrcRef)
        {
            CFRunLoopRef RunLoopRef = (CFRunLoopRef)GetCFRunLoopFromEventLoop(GetMainEventLoop());
            CFRunLoopRemoveSource(RunLoopRef, RunLoopSrcRef, kCFRunLoopDefaultMode);

            CFRelease(RunLoopSrcRef);
        }

        /* Dispose of and release the queue. */
        (*ppHidQueueInterface)->dispose(ppHidQueueInterface);
        cRefs = (*ppHidQueueInterface)->Release(ppHidQueueInterface); MY_CHECK_CREFS(cRefs);
    }

    /* Release the removal hook? */
    /** @todo */

    /* Close and release the device interface. */
    if (pKeyboardEntry->ppHidDeviceInterface)
    {
        IOHIDDeviceInterface **ppHidDeviceInterface = pKeyboardEntry->ppHidDeviceInterface;
        pKeyboardEntry->ppHidDeviceInterface = NULL;

        (*ppHidDeviceInterface)->close(ppHidDeviceInterface);
        cRefs = (*ppHidDeviceInterface)->Release(ppHidDeviceInterface); MY_CHECK_CREFS(cRefs);
    }
}

/** Zap the keyboard cache. */
static void darwinHIDKeyboardCacheZap(void)
{
    /* Release the old cache data first. */
    while (g_cKeyboards > 0)
    {
        unsigned i = --g_cKeyboards;
        darwinHIDKeyboardCacheDestroyEntry(&g_aKeyboards[i]);
    }
}

/** Updates the cached keyboard data.
  * @todo The current implementation is very brute force...
  *       Rewrite it so that it doesn't flush the cache completely but simply checks whether
  *       anything has changed in the HID config. With any luck, there might even be a callback
  *       or something we can poll for HID config changes...
  *       setRemovalCallback() is a start... */
static void darwinHIDKeyboardCacheDoUpdate(void)
{
    g_u64KeyboardTS = RTTimeMilliTS();

    /* Dispense with the old cache data. */
    darwinHIDKeyboardCacheZap();

    /* Open the master port on the first invocation. */
    if (!g_MasterPort)
    {
        kern_return_t krc = IOMasterPort(MACH_PORT_NULL, &g_MasterPort);
        AssertReturnVoid(krc == KERN_SUCCESS);
    }

    /* Create a matching dictionary for searching for keyboards devices. */
    static const UInt32 s_Page = kHIDPage_GenericDesktop;
    static const UInt32 s_Usage = kHIDUsage_GD_Keyboard;
    CFMutableDictionaryRef RefMatchingDict = IOServiceMatching(kIOHIDDeviceKey);
    AssertReturnVoid(RefMatchingDict);
    CFDictionarySetValue(RefMatchingDict, CFSTR(kIOHIDPrimaryUsagePageKey),
                         CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &s_Page));
    CFDictionarySetValue(RefMatchingDict, CFSTR(kIOHIDPrimaryUsageKey),
                         CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &s_Usage));

    /* Perform the search and get a collection of keyboard devices. */
    io_iterator_t Keyboards = NULL;
    IOReturn rc = IOServiceGetMatchingServices(g_MasterPort, RefMatchingDict, &Keyboards);
    AssertMsgReturnVoid(rc == kIOReturnSuccess, ("rc=%d\n", rc));
    RefMatchingDict = NULL; /* the reference is consumed by IOServiceGetMatchingServices. */

    /* Enumerate the keyboards and query the cache data. */
    unsigned i = 0;
    io_object_t KeyboardDevice;
    while (   i < RT_ELEMENTS(g_aKeyboards)
           && (KeyboardDevice = IOIteratorNext(Keyboards)) != 0)
    {
        if (darwinHIDKeyboardCacheCreateEntry(&g_aKeyboards[i], KeyboardDevice))
            i++;
        IOObjectRelease(KeyboardDevice);
    }
    g_cKeyboards = i;

    IOObjectRelease(Keyboards);
}

/** Updates the keyboard cache if it's time to do it again. */
static void darwinHIDKeyboardCacheUpdate(void)
{
    if (    !g_cKeyboards
        /*||  g_u64KeyboardTS - RTTimeMilliTS() > 7500*/ /* 7.5sec */)
        darwinHIDKeyboardCacheDoUpdate();
}

/** Queries the modifier keys from the (IOKit) HID Manager. */
static UInt32 darwinQueryHIDModifiers(void)
{
    /* Iterate thru the keyboards collecting their modifier masks. */
    UInt32 fHIDModifiers = 0;
    unsigned i = g_cKeyboards;
    while (i-- > 0)
    {
        IOHIDDeviceInterface **ppHidDeviceInterface = g_aKeyboards[i].ppHidDeviceInterface;
        if (!ppHidDeviceInterface)
            continue;

        unsigned j = g_aKeyboards[i].cCookies;
        while (j-- > 0)
        {
            IOHIDEventStruct HidEvent;
            IOReturn rc = (*ppHidDeviceInterface)->getElementValue(ppHidDeviceInterface,
                                                                   g_aKeyboards[i].aCookies[j].Cookie,
                                                                   &HidEvent);
            if (rc == kIOReturnSuccess)
            {
                if (HidEvent.value)
                    fHIDModifiers |= g_aKeyboards[i].aCookies[j].fMask;
            }
            else
                AssertMsgFailed(("rc=%#x\n", rc));
        }
    }

    return fHIDModifiers;
}

#endif /* USE_HID_FOR_MODIFIERS */


void DarwinGrabKeyboard(bool fGlobalHotkeys)
{
    LogFlow(("DarwinGrabKeyboard: fGlobalHotkeys=%RTbool\n", fGlobalHotkeys));

#ifdef USE_HID_FOR_MODIFIERS
    /* Update the keyboard cache. */
    darwinHIDKeyboardCacheUpdate();

    /* Start the keyboard queues and query the current mask. */
    g_fHIDQueueEnabled = true;

    unsigned i = g_cKeyboards;
    while (i-- > 0)
    {
        if (g_aKeyboards[i].ppHidQueueInterface)
            (*g_aKeyboards[i].ppHidQueueInterface)->start(g_aKeyboards[i].ppHidQueueInterface);
    }

    g_fHIDModifierMask = darwinQueryHIDModifiers();
#endif /* USE_HID_FOR_MODIFIERS */

    /* Disable hotkeys if requested. */
    if (fGlobalHotkeys)
        DarwinDisableGlobalHotKeys(true);
}

void DarwinReleaseKeyboard()
{
    LogFlow(("DarwinReleaseKeyboard\n"));

    /* Re-enable hotkeys. */
    DarwinDisableGlobalHotKeys(false);

#ifdef USE_HID_FOR_MODIFIERS
    /* Stop and drain the keyboard queues. */
    g_fHIDQueueEnabled = false;

#if 0
    unsigned i = g_cKeyboards;
    while (i-- > 0)
    {
        if (g_aKeyboards[i].ppHidQueueInterface)
        {

            (*g_aKeyboards[i].ppHidQueueInterface)->stop(g_aKeyboards[i].ppHidQueueInterface);

            /* drain it */
            IOReturn rc;
            unsigned c = 0;
            do
            {
                IOHIDEventStruct Event;
                AbsoluteTime MaxTime = {0,0};
                rc = (*g_aKeyboards[i].ppHidQueueInterface)->getNextEvent(g_aKeyboards[i].ppHidQueueInterface,
                                                                          &Event, MaxTime, 0);
            } while (   rc == kIOReturnSuccess
                     && c++ < 32);
        }
    }
#else
    /* Kill the keyboard cache. */
    darwinHIDKeyboardCacheZap();
#endif

    /* Clear the modifier mask. */
    g_fHIDModifierMask = 0;
#endif /* USE_HID_FOR_MODIFIERS */
}


#ifdef VBOX_WITH_KBD_LEDS_SYNC

/** Prepares dictionary that will be used to match HID LED device(s) while discovering. */
static CFDictionaryRef darwinQueryLedDeviceMatchingDictionary()
{
    CFDictionaryRef deviceMatchingDictRef;

    // Use two (key, value) pairs:
    //      - (kIOHIDDeviceUsagePageKey, kHIDPage_GenericDesktop),
    //      - (kIOHIDDeviceUsageKey,     kHIDUsage_GD_Keyboard). */

    CFNumberRef usagePageKeyCFNumberRef; int usagePageKeyCFNumberValue = kHIDPage_GenericDesktop;
    CFNumberRef usageKeyCFNumberRef;     int usageKeyCFNumberValue     = kHIDUsage_GD_Keyboard;

    usagePageKeyCFNumberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usagePageKeyCFNumberValue);
    if (usagePageKeyCFNumberRef)
    {
        usageKeyCFNumberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usageKeyCFNumberValue);
        if (usageKeyCFNumberRef)
        {
            CFStringRef dictionaryKeys[2] = { CFSTR(kIOHIDDeviceUsagePageKey), CFSTR(kIOHIDDeviceUsageKey) };
            CFNumberRef dictionaryVals[2] = { usagePageKeyCFNumberRef,         usageKeyCFNumberRef         };

            deviceMatchingDictRef = CFDictionaryCreate(kCFAllocatorDefault,
                                                       (const void **)dictionaryKeys,
                                                       (const void **)dictionaryVals,
                                                       2, /** two (key, value) pairs */
                                                       &kCFTypeDictionaryKeyCallBacks,
                                                       &kCFTypeDictionaryValueCallBacks);

            if (deviceMatchingDictRef)
            {
                CFRelease(usageKeyCFNumberRef);
                CFRelease(usagePageKeyCFNumberRef);

                return deviceMatchingDictRef;
            }

            CFRelease(usageKeyCFNumberRef);
        }

        CFRelease(usagePageKeyCFNumberRef);
    }

    return NULL;
}

/** Prepare dictionary that will be used to match HID LED device element(s) while discovering. */
static CFDictionaryRef darwinQueryLedElementMatchingDictionary()
{
    CFDictionaryRef elementMatchingDictRef;

    // Use only one (key, value) pair to match LED device element:
    //      - (kIOHIDElementUsagePageKey, kHIDPage_LEDs).  */

    CFNumberRef usagePageKeyCFNumberRef; int usagePageKeyCFNumberValue = kHIDPage_LEDs;

    usagePageKeyCFNumberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usagePageKeyCFNumberValue);
    if (usagePageKeyCFNumberRef)
    {
        CFStringRef dictionaryKeys[1] = { CFSTR(kIOHIDElementUsagePageKey), };
        CFNumberRef dictionaryVals[1] = { usagePageKeyCFNumberRef,          };

        elementMatchingDictRef = CFDictionaryCreate(kCFAllocatorDefault,
                                                    (const void **)dictionaryKeys,
                                                    (const void **)dictionaryVals,
                                                    1, /** one (key, value) pair */
                                                    &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);

        if (elementMatchingDictRef)
        {
            CFRelease(usagePageKeyCFNumberRef);
            return elementMatchingDictRef;
        }

        CFRelease(usagePageKeyCFNumberRef);
    }

    return NULL;
}

/** Turn ON or OFF a particular LED. */
static int darwinLedElementSetValue(IOHIDDeviceRef hidDevice, IOHIDElementRef element, bool fEnabled)
{
    IOHIDValueRef valueRef;
    IOReturn      rc = kIOReturnError;

    /* Try to resume suspended keyboard devices. Abort if failed in order to avoid GUI freezes. */
    int rc1 = SUPR3ResumeSuspendedKeyboards();
    if (RT_FAILURE(rc1))
        return rc1;

    valueRef = IOHIDValueCreateWithIntegerValue(kCFAllocatorDefault, element, 0, (fEnabled) ? 1 : 0);
    if (valueRef)
    {
        rc = IOHIDDeviceSetValue(hidDevice, element, valueRef);
        if (rc != kIOReturnSuccess)
            LogRel2(("Warning! Something went wrong in attempt to turn %s HID device led (error %d)!\n", ((fEnabled) ? "on" : "off"), rc));
        else
            LogRel2(("Led (%d) is turned %s\n", (int)IOHIDElementGetUsage(element), ((fEnabled) ? "on" : "off")));

        CFRelease(valueRef);
    }

    return rc;
}

/** Get state of a particular led. */
static int darwinLedElementGetValue(IOHIDDeviceRef hidDevice, IOHIDElementRef element, bool *fEnabled)
{
    /* Try to resume suspended keyboard devices. Abort if failed in order to avoid GUI freezes. */
    int rc1 = SUPR3ResumeSuspendedKeyboards();
    if (RT_FAILURE(rc1))
        return rc1;

    IOHIDValueRef valueRef;
    IOReturn rc = IOHIDDeviceGetValue(hidDevice, element, &valueRef);
    if (rc == kIOReturnSuccess)
    {
        CFIndex integerValue = IOHIDValueGetIntegerValue(valueRef);
        switch (integerValue)
        {
            case 0:
                *fEnabled = false;
                break;
            case 1:
                *fEnabled = true;
                break;
            default:
                rc = kIOReturnError;
        }

        /*CFRelease(valueRef); - IOHIDDeviceGetValue does not return a reference, so no need to release it. */
    }

    return rc;
}

/** Set corresponding states from NumLock, CapsLock and ScrollLock leds. */
static int darwinSetDeviceLedsState(IOHIDDeviceRef hidDevice, CFDictionaryRef elementMatchingDict,
                                    bool fNumLockOn, bool fCapsLockOn, bool fScrollLockOn)
{
    CFArrayRef matchingElementsArrayRef;
    int        rc2 = 0;

    matchingElementsArrayRef = IOHIDDeviceCopyMatchingElements(hidDevice, elementMatchingDict, kIOHIDOptionsTypeNone);
    if (matchingElementsArrayRef)
    {
        CFIndex cElements = CFArrayGetCount(matchingElementsArrayRef);

        /* Cycle though all the elements we found */
        for (CFIndex i = 0; i < cElements; i++)
        {
            IOHIDElementRef element = (IOHIDElementRef)CFArrayGetValueAtIndex(matchingElementsArrayRef, i);
            uint32_t        usage   = IOHIDElementGetUsage(element);
            int             rc = 0;

            switch (usage)
            {
                case kHIDUsage_LED_NumLock:
                    rc = darwinLedElementSetValue(hidDevice, element, fNumLockOn);
                    break;

                case kHIDUsage_LED_CapsLock:
                    rc = darwinLedElementSetValue(hidDevice, element, fCapsLockOn);
                    break;
                case kHIDUsage_LED_ScrollLock:
                    rc = darwinLedElementSetValue(hidDevice, element, fScrollLockOn);
                    break;
            }
            if (rc != 0)
            {
                LogRel2(("Failed to set led (%d) state\n", (int)IOHIDElementGetUsage(element)));
                rc2 = kIOReturnError;
            }
        }

        CFRelease(matchingElementsArrayRef);
    }

    return rc2;
}

/** Get corresponding states for NumLock, CapsLock and ScrollLock leds. */
static int darwinGetDeviceLedsState(IOHIDDeviceRef hidDevice, CFDictionaryRef elementMatchingDict,
                                    bool *fNumLockOn, bool *fCapsLockOn, bool *fScrollLockOn)
{
    CFArrayRef matchingElementsArrayRef;
    int        rc2 = 0;

    matchingElementsArrayRef = IOHIDDeviceCopyMatchingElements(hidDevice, elementMatchingDict, kIOHIDOptionsTypeNone);
    if (matchingElementsArrayRef)
    {
        CFIndex cElements = CFArrayGetCount(matchingElementsArrayRef);

        /* Cycle though all the elements we found */
        for (CFIndex i = 0; i < cElements; i++)
        {
            IOHIDElementRef element = (IOHIDElementRef)CFArrayGetValueAtIndex(matchingElementsArrayRef, i);
            uint32_t        usage   = IOHIDElementGetUsage(element);
            int             rc = 0;

            switch (usage)
            {
                case kHIDUsage_LED_NumLock:
                    rc = darwinLedElementGetValue(hidDevice, element, fNumLockOn);
                    break;

                case kHIDUsage_LED_CapsLock:
                    rc = darwinLedElementGetValue(hidDevice, element, fCapsLockOn);
                    break;
                case kHIDUsage_LED_ScrollLock:
                    rc = darwinLedElementGetValue(hidDevice, element, fScrollLockOn);
                    break;
            }
            if (rc != 0)
            {
                LogRel2(("Failed to get led (%d) state\n", (int)IOHIDElementGetUsage(element)));
                rc2 = kIOReturnError;
            }
        }

        CFRelease(matchingElementsArrayRef);
    }

    return rc2;
}

/** Get integer property of HID device */
static uint32_t darwinQueryIntProperty(IOHIDDeviceRef pHidDeviceRef, CFStringRef pProperty)
{
    CFTypeRef pNumberRef;
    uint32_t  value = 0;

    AssertReturn(pHidDeviceRef, 0);
    AssertReturn(pProperty, 0);

    pNumberRef = IOHIDDeviceGetProperty(pHidDeviceRef, pProperty);
    if (pNumberRef)
    {
        if (CFGetTypeID(pNumberRef) == CFNumberGetTypeID())
        {
            if (CFNumberGetValue((CFNumberRef)pNumberRef, kCFNumberSInt32Type, &value))
                return value;
        }
    }

    return 0;
}

/** Get HID Vendor ID */
static uint32_t darwinHidVendorId(IOHIDDeviceRef pHidDeviceRef)
{
    return darwinQueryIntProperty(pHidDeviceRef, CFSTR(kIOHIDVendorIDKey));
}

/** Get HID Product ID */
static uint32_t darwinHidProductId(IOHIDDeviceRef pHidDeviceRef)
{
    return darwinQueryIntProperty(pHidDeviceRef, CFSTR(kIOHIDProductIDKey));
}

/** Get HID Location ID */
static uint32_t darwinHidLocationId(IOHIDDeviceRef pHidDeviceRef)
{
    return darwinQueryIntProperty(pHidDeviceRef, CFSTR(kIOHIDLocationIDKey));
}

/** Some keyboard devices might freeze after LEDs manipulation. We filter out such devices here.
  * In the list below, devices that known to have such issues. If you want to add new device,
  * then add it here. Currently, we only filter devices by Vendor ID.
  * In future it might make sense to take Product ID into account as well. */
static bool darwinHidDeviceSupported(IOHIDDeviceRef pHidDeviceRef)
{
#ifndef VBOX_WITHOUT_KBD_LEDS_SYNC_FILTERING
    bool     fSupported = true;
    uint32_t vendorId = darwinHidVendorId(pHidDeviceRef);
    uint32_t productId = darwinHidProductId(pHidDeviceRef);

    if (vendorId == 0x05D5)      /* Genius */
    {
        if (productId == 0x8001) /* GK-04008/C keyboard */
            fSupported = false;
    }
    if (vendorId == 0xE6A)       /* Megawin Technology */
    {
        if (productId == 0x6001) /* Japanese flexible keyboard */
            fSupported = false;
    }

    LogRel2(("HID device [VendorID=0x%X, ProductId=0x%X] %s in the list of supported devices.\n", vendorId, productId, (fSupported ? "is" : "is not")));

    return fSupported;
#else /* !VBOX_WITH_KBD_LEDS_SYNC_FILTERING */
    return true;
#endif
}

/** IOKit key press callback helper: take care about key-down event.
  * This code should be executed within a critical section under pHidState->fifoEventQueueLock. */
static void darwinHidInputCbKeyDown(VBoxKbdState_t *pKbd, uint32_t iKeyCode, VBoxHidsState_t *pHidState)
{
    VBoxKbdEvent_t *pEvent = (VBoxKbdEvent_t *)malloc(sizeof(VBoxKbdEvent_t));

    if (pEvent)
    {
        /* Queue Key-Down event. */
        pEvent->tsKeyDown = RTTimeSystemMilliTS();
        pEvent->pKbd      = pKbd;
        pEvent->iKeyCode  = iKeyCode;

        CFArrayAppendValue(pHidState->pFifoEventQueue, (void *)pEvent);

        LogRel2(("IOHID: KBD %d: Modifier Key-Down event\n", (int)pKbd->idxPosition));
    }
    else
        LogRel2(("IOHID: Modifier Key-Up event. Unable to find memory for KBD %d event\n", (int)pKbd->idxPosition));
}

/** IOkit and Carbon key press callbacks helper: CapsLock timeout checker.
  *
  * Returns FALSE if CAPS LOCK timeout not occurred and its state still was not switched (Apple kbd).
  * Returns TRUE if CAPS LOCK timeout occurred and its state was switched (Apple kbd).
  * Returns TRUE for non-Apple kbd. */
static bool darwinKbdCapsEventMatches(VBoxKbdEvent_t *pEvent, bool fCapsLed)
{
    // CapsLock timeout is only applicable if conditions
    // below are satisfied:
    //
    // a) Key pressed on Apple keyboard
    // b) CapsLed is OFF at the moment when CapsLock key is pressed

    bool fAppleKeyboard = (pEvent->pKbd->cCapsLockTimeout > 0);

    /* Apple keyboard */
    if (fAppleKeyboard && !fCapsLed)
    {
        uint64_t tsDiff = RTTimeSystemMilliTS() - pEvent->tsKeyDown;
        if (tsDiff < pEvent->pKbd->cCapsLockTimeout)
            return false;
    }

    return true;
}

/** IOKit key press callback helper: take care about key-up event.
  * This code should be executed within a critical section under pHidState->fifoEventQueueLock. */
static void darwinHidInputCbKeyUp(VBoxKbdState_t *pKbd, uint32_t iKeyCode, VBoxHidsState_t *pHidState)
{
    CFIndex         iQueue = 0;
    VBoxKbdEvent_t *pEvent = NULL;

    // Key-up event assumes that key-down event occured previously. If so, an event
    // data should be in event queue. Attempt to find it.
    for (CFIndex i = 0; i < CFArrayGetCount(pHidState->pFifoEventQueue); i++)
    {
        VBoxKbdEvent_t *pCachedEvent = (VBoxKbdEvent_t *)CFArrayGetValueAtIndex(pHidState->pFifoEventQueue, i);

        if (pCachedEvent && pCachedEvent->pKbd == pKbd && pCachedEvent->iKeyCode == iKeyCode)
        {
            pEvent = pCachedEvent;
            iQueue = i;
            break;
        }
    }

    /* Event found. */
    if (pEvent)
    {
        // NUM LOCK should not have timeout and its press should immidiately trigger Carbon callback.
        // Therefore, if it is still in queue this is a problem because it was not handled by Carbon callback.
        // This mean that NUM LOCK is most likely out of sync.
        if (iKeyCode == kHIDUsage_KeypadNumLock)
        {
            LogRel2(("IOHID: KBD %d: Modifier Key-Up event. Key-Down event was not habdled by Carbon callback. "
                "NUM LOCK is most likely out of sync\n", (int)pKbd->idxPosition));
        }
        else if (iKeyCode == kHIDUsage_KeyboardCapsLock)
        {
            // If CAPS LOCK key-press event still not match CAPS LOCK timeout criteria, Carbon callback
            // should not be triggered for this event at all. Threfore, event should be removed from queue.
            if (!darwinKbdCapsEventMatches(pEvent, pHidState->guestState.fCapsLockOn))
            {
                CFArrayRemoveValueAtIndex(pHidState->pFifoEventQueue, iQueue);

                LogRel2(("IOHID: KBD %d: Modifier Key-Up event on Apple keyboard. Key-Down event was triggered %llu ms "
                    "ago. Carbon event should not be triggered, removed from queue\n", (int)pKbd->idxPosition,
                    RTTimeSystemMilliTS() - pEvent->tsKeyDown));
                free(pEvent);
            }
            else
            {
                // CAPS LOCK key-press event matches to CAPS LOCK timeout criteria and still present in queue.
                // This might mean that Carbon callback was triggered for this event, but cached keyboard state was not updated.
                // It also might mean that Carbon callback still was not triggered, but it will be soon.
                // Threfore, CAPS LOCK might be out of sync.
                LogRel2(("IOHID: KBD %d: Modifier Key-Up event. Key-Down event was triggered %llu ms "
                    "ago and still was not handled by Carbon callback. CAPS LOCK might out of sync if "
                    "Carbon will not handle this\n", (int)pKbd->idxPosition, RTTimeSystemMilliTS() - pEvent->tsKeyDown));
            }
        }
    }
    else
        LogRel2(("IOHID: KBD %d: Modifier Key-Up event. Modifier state change was "
            "successfully handled by Carbon callback\n", (int)pKbd->idxPosition));
}

/** IOKit key press callback. Triggered before Carbon callback. We remember which keyboard produced a keypress here. */
static void darwinHidInputCallback(void *pData, IOReturn unused, void *unused1, IOHIDValueRef pValueRef)
{
    (void)unused;
    (void)unused1;

    AssertReturnVoid(pValueRef);

    IOHIDElementRef pElementRef = IOHIDValueGetElement(pValueRef);
    AssertReturnVoid(pElementRef);

    uint32_t usage = IOHIDElementGetUsage(pElementRef);

    if (IOHIDElementGetUsagePage(pElementRef) == kHIDPage_KeyboardOrKeypad)    /* Keyboard or keypad event */
        if (usage == kHIDUsage_KeyboardCapsLock ||                             /* CapsLock key has been pressed */
            usage == kHIDUsage_KeypadNumLock)                                  /* ... or NumLock key has been pressed */
        {
            VBoxKbdState_t *pKbd = (VBoxKbdState_t *)pData;

            if (pKbd && pKbd->pParentContainer)
            {
                bool             fKeyDown  = (IOHIDValueGetIntegerValue(pValueRef) == 1);
                VBoxHidsState_t *pHidState = (VBoxHidsState_t *)pKbd->pParentContainer;

                AssertReturnVoid(pHidState);

                if (RT_FAILURE(RTSemMutexRequest(pHidState->fifoEventQueueLock, RT_INDEFINITE_WAIT)))
                    return ;

                /* Handle corresponding event. */
                if (fKeyDown)
                    darwinHidInputCbKeyDown(pKbd, usage, pHidState);
                else
                    darwinHidInputCbKeyUp(pKbd, usage, pHidState);

                RTSemMutexRelease(pHidState->fifoEventQueueLock);
            }
            else
                LogRel2(("IOHID: No KBD: A modifier key has been pressed\n"));
        }
}

/** Carbon key press callback helper: find last occured KBD event in queue
 * (ignoring those events which do not match CAPS LOCK timeout criteria).
 * Once event found, it is removed from queue. This code should be executed
 * within a critical section under pHidState->fifoEventQueueLock. */
static VBoxKbdEvent_t *darwinCarbonCbFindEvent(VBoxHidsState_t *pHidState)
{
    VBoxKbdEvent_t *pEvent = NULL;

    for (CFIndex i = 0; i < CFArrayGetCount(pHidState->pFifoEventQueue); i++)
    {
        pEvent = (VBoxKbdEvent_t *)CFArrayGetValueAtIndex(pHidState->pFifoEventQueue, i);

        /* Paranoia: skip potentially dangerous data items. */
        if (!pEvent || !pEvent->pKbd) continue;

        if ( pEvent->iKeyCode == kHIDUsage_KeypadNumLock
         || (pEvent->iKeyCode == kHIDUsage_KeyboardCapsLock && darwinKbdCapsEventMatches(pEvent, pHidState->guestState.fCapsLockOn)))
        {
            /* Found one. Remove it from queue. */
            CFArrayRemoveValueAtIndex(pHidState->pFifoEventQueue, i);

            LogRel2(("CARBON: Found event in queue: %d (KBD %d, tsKeyDown=%llu, pressed %llu ms ago)\n", (int)i,
                (int)pEvent->pKbd->idxPosition, pEvent->tsKeyDown, RTTimeSystemMilliTS() - pEvent->tsKeyDown));

            break;
        }
        else
            LogRel2(("CARBON: Skip keyboard event from KBD %d, key pressed %llu ms ago\n",
                (int)pEvent->pKbd->idxPosition, RTTimeSystemMilliTS() - pEvent->tsKeyDown));

        pEvent = NULL;
    }

    return pEvent;
}

/** Carbon key press callback. Triggered after IOKit callback. */
static CGEventRef darwinCarbonCallback(CGEventTapProxy unused, CGEventType unused1, CGEventRef pEventRef, void *pData)
{
    (void)unused;
    (void)unused1;

    CGEventFlags fMask = CGEventGetFlags(pEventRef);
    bool         fCaps = (bool)(fMask & NX_ALPHASHIFTMASK);
    bool         fNum  = (bool)(fMask & NX_NUMERICPADMASK);
    CGKeyCode    key   = CGEventGetIntegerValueField(pEventRef, kCGKeyboardEventKeycode);

    VBoxHidsState_t *pHidState = (VBoxHidsState_t *)pData;
    AssertReturn(pHidState, pEventRef);

    if (RT_FAILURE(RTSemMutexRequest(pHidState->fifoEventQueueLock, RT_INDEFINITE_WAIT)))
        return pEventRef;

    if (key == kHIDUsage_KeyboardCapsLock ||
        key == kHIDUsage_KeypadNumLock)
    {
        /* Attempt to find an event queued by IOKit callback. */
        VBoxKbdEvent_t *pEvent = darwinCarbonCbFindEvent(pHidState);
        if (pEvent)
        {
            VBoxKbdState_t *pKbd = pEvent->pKbd;

            LogRel2(("CARBON: KBD %d: caps=%s, num=%s. tsKeyDown=%llu, tsKeyUp=%llu [tsDiff=%llu ms]. %d events in queue.\n",
                (int)pKbd->idxPosition, VBOX_BOOL_TO_STR_STATE(fCaps), VBOX_BOOL_TO_STR_STATE(fNum),
                pEvent->tsKeyDown, RTTimeSystemMilliTS(), RTTimeSystemMilliTS() - pEvent->tsKeyDown,
                CFArrayGetCount(pHidState->pFifoEventQueue)));

            pKbd->LED.fCapsLockOn = fCaps;
            pKbd->LED.fNumLockOn  = fNum;

            /* Silently resync last touched KBD device */
            if (pHidState)
            {
                CFDictionaryRef elementMatchingDict = darwinQueryLedElementMatchingDictionary();
                if (elementMatchingDict)
                {
                    (void)darwinSetDeviceLedsState(pKbd->pDevice,
                                                   elementMatchingDict,
                                                   pHidState->guestState.fNumLockOn,
                                                   pHidState->guestState.fCapsLockOn,
                                                   pHidState->guestState.fScrollLockOn);

                    CFRelease(elementMatchingDict);
                }
            }

            free(pEvent);
        }
        else
            LogRel2(("CARBON: No KBD to take care when modifier key has been pressed: caps=%s, num=%s (%d events in queue)\n",
                VBOX_BOOL_TO_STR_STATE(fCaps), VBOX_BOOL_TO_STR_STATE(fNum), CFArrayGetCount(pHidState->pFifoEventQueue)));
    }

    RTSemMutexRelease(pHidState->fifoEventQueueLock);

    return pEventRef;
}

/** Helper function to obtain interface for IOUSBInterface IOService. */
static IOUSBDeviceInterface ** darwinQueryUsbHidInterfaceInterface(io_service_t service)
{
    kern_return_t         rc;
    IOCFPlugInInterface **ppPluginInterface = NULL;
    SInt32                iScore;

    rc = IOCreatePlugInInterfaceForService(service, kIOUSBInterfaceUserClientTypeID,
                                           kIOCFPlugInInterfaceID, &ppPluginInterface, &iScore);

    if (rc == kIOReturnSuccess && ppPluginInterface != NULL)
    {
        IOUSBDeviceInterface **ppUsbDeviceInterface = NULL;

        rc = (*ppPluginInterface)->QueryInterface(ppPluginInterface, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID),
                                                  (LPVOID *)&ppUsbDeviceInterface);
        IODestroyPlugInInterface(ppPluginInterface);

        if (rc == kIOReturnSuccess && ppUsbDeviceInterface != NULL)
            return ppUsbDeviceInterface;
        else
            LogRel2(("Failed to query plugin interface for USB device\n"));

    }
    else
        LogRel2(("Failed to create plugin interface for USB device\n"));

    return NULL;
}

/** Helper function for IOUSBInterface IOService general interest notification callback: resync LEDs. */
static void darwinUsbHidResyncLeds(VBoxKbdState_t *pKbd)
{
    AssertReturnVoid(pKbd);

    VBoxHidsState_t *pHidState = (VBoxHidsState_t *)pKbd->pParentContainer;
    CFDictionaryRef  elementMatchingDict = darwinQueryLedElementMatchingDictionary();
    if (elementMatchingDict)
    {
        LogRel2(("Do HID device resync at location 0x%X \n", pKbd->idLocation));
        (void)darwinSetDeviceLedsState(pKbd->pDevice, elementMatchingDict,
            pHidState->guestState.fNumLockOn, pHidState->guestState.fCapsLockOn, pHidState->guestState.fScrollLockOn);
        CFRelease(elementMatchingDict);
    }
}

/** IOUSBInterface IOService general interest notification callback. When we receive it, we do
 * silently resync kbd which has just changed its state. */
static void darwinUsbHidGeneralInterestCb(void *pData, io_service_t unused1, natural_t msg, void *unused2)
{
    NOREF(unused1);
    NOREF(unused2);

    AssertReturnVoid(pData);
    VBoxKbdState_t *pKbd = (VBoxKbdState_t *)pData;

    switch (msg)
    {
        case kIOUSBMessagePortHasBeenSuspended:
            {
                LogRel2(("IOUSBInterface IOService general interest notification kIOUSBMessagePortHasBeenSuspended for KBD %d (Location ID: 0x%X)\n",
                         (int)(pKbd->idxPosition), pKbd->idLocation));
                break;
            }

        case kIOUSBMessagePortHasBeenResumed:
            {
                LogRel2(("IOUSBInterface IOService general interest notification kIOUSBMessagePortHasBeenResumed for KBD %d (Location ID: 0x%X)\n",
                         (int)(pKbd->idxPosition), pKbd->idLocation));
                break;
            }

        case kIOUSBMessagePortHasBeenReset:
            {
                LogRel2(("IOUSBInterface IOService general interest notification kIOUSBMessagePortHasBeenReset for KBD %d (Location ID: 0x%X)\n",
                         (int)(pKbd->idxPosition), pKbd->idLocation));
                darwinUsbHidResyncLeds(pKbd);
                break;
            }

        case kIOUSBMessageCompositeDriverReconfigured:
            {
                LogRel2(("IOUSBInterface IOService general interest notification kIOUSBMessageCompositeDriverReconfigured for KBD %d (Location ID: 0x%X)\n",
                         (int)(pKbd->idxPosition), pKbd->idLocation));
                break;
            }

        case kIOMessageServiceWasClosed:
            {
                LogRel2(("IOUSBInterface IOService general interest notification kIOMessageServiceWasClosed for KBD %d (Location ID: 0x%X)\n",
                         (int)(pKbd->idxPosition), pKbd->idLocation));
                break;
            }

        default:
            LogRel2(("IOUSBInterface IOService general interest notification 0x%X for KBD %d (Location ID: 0x%X)\n",
                     msg, (int)(pKbd->idxPosition), pKbd->idLocation));
    }
}

/** Get pre-cached KBD device by its Location ID. */
static VBoxKbdState_t *darwinUsbHidQueryKbdByLocationId(uint32_t idLocation, VBoxHidsState_t *pHidState)
{
    AssertReturn(pHidState, NULL);

    for (CFIndex i = 0; i < CFArrayGetCount(pHidState->pDeviceCollection); i++)
    {
        VBoxKbdState_t *pKbd = (VBoxKbdState_t *)CFArrayGetValueAtIndex(pHidState->pDeviceCollection, i);
        if (pKbd && pKbd->idLocation == idLocation)
        {
            LogRel2(("Lookup USB HID Device by location ID 0x%X: found match\n", idLocation));
            return pKbd;
        }
    }

    LogRel2(("Lookup USB HID Device by location ID 0x%X: no matches found:\n", idLocation));

    return NULL;
}

/** IOUSBInterface IOService match notification callback: issued when IOService instantinates.
 * We subscribe to general interest notifications for available IOServices here. */
static void darwinUsbHidDeviceMatchCb(void *pData, io_iterator_t iter)
{
    AssertReturnVoid(pData);

    io_service_t     service;
    VBoxHidsState_t *pHidState = (VBoxHidsState_t *)pData;

    while ((service = IOIteratorNext(iter)))
    {
        kern_return_t         rc;

        IOUSBDeviceInterface **ppUsbDeviceInterface = darwinQueryUsbHidInterfaceInterface(service);

        if (ppUsbDeviceInterface)
        {
            uint8_t  idDeviceClass, idDeviceSubClass;
            UInt32   idLocation;

            rc = (*ppUsbDeviceInterface)->GetLocationID    (ppUsbDeviceInterface,  &idLocation);       AssertMsg(rc == 0, ("Failed to get Location ID"));
            rc = (*ppUsbDeviceInterface)->GetDeviceClass   (ppUsbDeviceInterface,  &idDeviceClass);    AssertMsg(rc == 0, ("Failed to get Device Class"));
            rc = (*ppUsbDeviceInterface)->GetDeviceSubClass(ppUsbDeviceInterface,  &idDeviceSubClass); AssertMsg(rc == 0, ("Failed to get Device Subclass"));
            RT_NOREF(rc);

            if (idDeviceClass == kUSBHIDInterfaceClass && idDeviceSubClass == kUSBHIDBootInterfaceSubClass)
            {
                VBoxKbdState_t *pKbd = darwinUsbHidQueryKbdByLocationId((uint32_t)idLocation, pHidState);

                if (pKbd)
                {
                    rc = IOServiceAddInterestNotification(pHidState->pNotificationPrortRef, service, kIOGeneralInterest,
                        darwinUsbHidGeneralInterestCb, pKbd, &pHidState->pUsbHidGeneralInterestNotify);

                    AssertMsg(rc == 0, ("Failed to add general interest notification"));

                    LogRel2(("Found HID device at location 0x%X: class 0x%X, subclass 0x%X\n", idLocation, idDeviceClass, idDeviceSubClass));
                }
            }

            rc = (*ppUsbDeviceInterface)->Release(ppUsbDeviceInterface); AssertMsg(rc == 0, ("Failed to release USB device interface"));
        }

        IOObjectRelease(service);
    }
}

/** Register IOUSBInterface IOService match notification callback in order to recync KBD
 * device when it reports state change. */
static int darwinUsbHidSubscribeInterestNotifications(VBoxHidsState_t *pHidState)
{
    AssertReturn(pHidState, kIOReturnBadArgument);

    int rc = kIOReturnNoMemory;
    CFDictionaryRef pDictionary = IOServiceMatching(kIOUSBInterfaceClassName);

    if (pDictionary)
    {
        RT_GCC_NO_WARN_DEPRECATED_BEGIN
        pHidState->pNotificationPrortRef = IONotificationPortCreate(kIOMasterPortDefault); /* kIOMasterPortDefault: Deprecated since 12.0. */
        RT_GCC_NO_WARN_DEPRECATED_END
        if (pHidState->pNotificationPrortRef)
        {
            CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(pHidState->pNotificationPrortRef), kCFRunLoopDefaultMode);

            rc = IOServiceAddMatchingNotification(pHidState->pNotificationPrortRef, kIOMatchedNotification,
                                                  pDictionary, darwinUsbHidDeviceMatchCb, pHidState,
                                                  &pHidState->pUsbHidDeviceMatchNotify);

            if (rc == kIOReturnSuccess && pHidState->pUsbHidDeviceMatchNotify != IO_OBJECT_NULL)
            {
                darwinUsbHidDeviceMatchCb(pHidState, pHidState->pUsbHidDeviceMatchNotify);
                LogRel2(("Successfully subscribed to IOUSBInterface IOService match notifications\n"));
            }
            else
                LogRel2(("Failed to subscribe to IOUSBInterface IOService match notifications: subscription error 0x%X\n", rc));
        }
        else
            LogRel2(("Failed to subscribe to IOUSBInterface IOService match notifications: unable to create notification port\n"));
    }
    else
        LogRel2(("Failed to subscribe to IOUSBInterface IOService match notifications: no memory\n"));

    return rc;
}

/** Remove IOUSBInterface IOService match notification subscription. */
static void darwinUsbHidUnsubscribeInterestNotifications(VBoxHidsState_t *pHidState)
{
    AssertReturnVoid(pHidState);

    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(pHidState->pNotificationPrortRef), kCFRunLoopDefaultMode);
    IONotificationPortDestroy(pHidState->pNotificationPrortRef);
    pHidState->pNotificationPrortRef = NULL;

    LogRel2(("Successfully un-subscribed from IOUSBInterface IOService match notifications\n"));
}

/** This callback is called when user physically removes HID device. We remove device from cache here. */
static void darwinHidRemovalCallback(void *pData, IOReturn unused, void *unused1)
{
    (void)unused;
    (void)unused1;

    VBoxKbdState_t  *pKbd      = (VBoxKbdState_t  *)pData;                  AssertReturnVoid(pKbd);
    VBoxHidsState_t *pHidState = (VBoxHidsState_t *)pKbd->pParentContainer; AssertReturnVoid(pHidState);

    AssertReturnVoid(pHidState->pDeviceCollection);

    LogRel2(("Forget KBD %d\n", (int)pKbd->idxPosition));

    //if (RT_FAILURE(RTSemMutexRequest(pHidState->fifoEventQueueLock, RT_INDEFINITE_WAIT)))
    //    return ;

    CFArrayRemoveValueAtIndex(pHidState->pDeviceCollection, pKbd->idxPosition);
    free(pKbd);

    //RTSemMutexRelease(pHidState->fifoEventQueueLock);
}

/** Check if we already cached given device */
static bool darwinIsDeviceInCache(VBoxHidsState_t *pState, IOHIDDeviceRef pDevice)
{
    AssertReturn(pState, false);
    AssertReturn(pState->pDeviceCollection, false);

    for (CFIndex i = 0; i < CFArrayGetCount(pState->pDeviceCollection); i++)
    {
        VBoxKbdState_t *pKbd = (VBoxKbdState_t *)CFArrayGetValueAtIndex(pState->pDeviceCollection, i);
        if (pKbd && pKbd->pDevice == pDevice)
            return true;
    }

    return false;
}

/** Add device to cache. */
static void darwinHidAddDevice(VBoxHidsState_t *pHidState, IOHIDDeviceRef pDevice, bool fApplyLedState)
{
    int rc;

    if (!darwinIsDeviceInCache(pHidState, pDevice))
    {
        if (IOHIDDeviceConformsTo(pDevice, kHIDPage_GenericDesktop, kHIDUsage_GD_Keyboard)
         && darwinHidDeviceSupported(pDevice))
        {
            VBoxKbdState_t *pKbd = (VBoxKbdState_t *)malloc(sizeof(VBoxKbdState_t));
            if (pKbd)
            {
                pKbd->pDevice = pDevice;
                pKbd->pParentContainer = (void *)pHidState;
                pKbd->idxPosition = CFArrayGetCount(pHidState->pDeviceCollection);
                pKbd->idLocation = darwinHidLocationId(pDevice);

                // Some Apple keyboards have CAPS LOCK key timeout. According to corresponding
                // kext plist files, it is equals to 75 ms. For such devices we only add info into our FIFO event
                // queue if the time between Key-Down and Key-Up events >= 75 ms.
                pKbd->cCapsLockTimeout = (darwinHidVendorId(pKbd->pDevice) == kIOUSBVendorIDAppleComputer) ? 75 : 0;

                CFDictionaryRef elementMatchingDict = darwinQueryLedElementMatchingDictionary();
                if (elementMatchingDict)
                {
                    rc = darwinGetDeviceLedsState(pKbd->pDevice,
                                                  elementMatchingDict,
                                                  &pKbd->LED.fNumLockOn,
                                                  &pKbd->LED.fCapsLockOn,
                                                  &pKbd->LED.fScrollLockOn);

                    // This should never happen, but if happened -- mark all the leds of current
                    // device as turned OFF.
                    if (rc != 0)
                    {
                        LogRel2(("Unable to get leds state for device %d. Mark leds as turned off\n", (int)(pKbd->idxPosition)));
                        pKbd->LED.fNumLockOn    =
                        pKbd->LED.fCapsLockOn   =
                        pKbd->LED.fScrollLockOn = false;
                    }

                    /* Register per-device removal callback */
                    IOHIDDeviceRegisterRemovalCallback(pKbd->pDevice, darwinHidRemovalCallback, (void *)pKbd);

                    /* Register per-device input callback */
                    IOHIDDeviceRegisterInputValueCallback(pKbd->pDevice, darwinHidInputCallback, (void *)pKbd);
                    IOHIDDeviceScheduleWithRunLoop(pKbd->pDevice, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

                    CFArrayAppendValue(pHidState->pDeviceCollection, (void *)pKbd);

                    LogRel2(("Saved LEDs for KBD %d (%p): fNumLockOn=%s, fCapsLockOn=%s, fScrollLockOn=%s\n",
                        (int)pKbd->idxPosition, pKbd, VBOX_BOOL_TO_STR_STATE(pKbd->LED.fNumLockOn), VBOX_BOOL_TO_STR_STATE(pKbd->LED.fCapsLockOn),
                        VBOX_BOOL_TO_STR_STATE(pKbd->LED.fScrollLockOn)));

                    if (fApplyLedState)
                    {
                        rc = darwinSetDeviceLedsState(pKbd->pDevice, elementMatchingDict, pHidState->guestState.fNumLockOn,
                                                      pHidState->guestState.fCapsLockOn, pHidState->guestState.fScrollLockOn);
                        if (rc != 0)
                            LogRel2(("Unable to apply guest state to newly attached device\n"));
                    }

                    CFRelease(elementMatchingDict);
                    return;
                }

                free(pKbd);
            }
        }
    }
}

/** This callback is called when new HID device discovered by IOHIDManager. We add devices to cache here and only here! */
static void darwinHidMatchingCallback(void *pData, IOReturn unused, void *unused1, IOHIDDeviceRef pDevice)
{
    (void)unused;
    (void)unused1;

    VBoxHidsState_t *pHidState = (VBoxHidsState_t *)pData;

    AssertReturnVoid(pHidState);
    AssertReturnVoid(pHidState->pDeviceCollection);
    AssertReturnVoid(pDevice);

    darwinHidAddDevice(pHidState, pDevice, true);
}

/** Register Carbon key press callback. */
static int darwinAddCarbonHandler(VBoxHidsState_t *pHidState)
{
    CFMachPortRef pTapRef;
    CGEventMask   fMask = CGEventMaskBit(kCGEventFlagsChanged);

    AssertReturn(pHidState, kIOReturnError);

    /* Create FIFO event queue for keyboard events */
    pHidState->pFifoEventQueue = CFArrayCreateMutable(kCFAllocatorDefault, 0, NULL);
    AssertReturn(pHidState->pFifoEventQueue, kIOReturnError);

    /* Create Lock for FIFO event queue */
    if (RT_FAILURE(RTSemMutexCreate(&pHidState->fifoEventQueueLock)))
    {
        LogRel2(("Unable to create Lock for FIFO event queue\n"));
        CFRelease(pHidState->pFifoEventQueue);
        pHidState->pFifoEventQueue = NULL;
        return kIOReturnError;
    }

    pTapRef = CGEventTapCreate(kCGSessionEventTap, kCGTailAppendEventTap, kCGEventTapOptionDefault, fMask,
                               darwinCarbonCallback, (void *)pHidState);
    if (pTapRef)
    {
        CFRunLoopSourceRef pLoopSourceRef;
        pLoopSourceRef = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, pTapRef, 0);
        if (pLoopSourceRef)
        {
            CFRunLoopAddSource(CFRunLoopGetCurrent(), pLoopSourceRef, kCFRunLoopDefaultMode);
            CGEventTapEnable(pTapRef, true);

            pHidState->pTapRef = pTapRef;
            pHidState->pLoopSourceRef = pLoopSourceRef;

            return 0;
        }
        else
            LogRel2(("Unable to create a loop source\n"));

        CFRelease(pTapRef);
    }
    else
        LogRel2(("Unable to create an event tap\n"));

    return kIOReturnError;
}

/** Remove Carbon key press callback. */
static void darwinRemoveCarbonHandler(VBoxHidsState_t *pHidState)
{
    AssertReturnVoid(pHidState);
    AssertReturnVoid(pHidState->pTapRef);
    AssertReturnVoid(pHidState->pLoopSourceRef);
    AssertReturnVoid(pHidState->pFifoEventQueue);

    CGEventTapEnable(pHidState->pTapRef, false);
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), pHidState->pLoopSourceRef, kCFRunLoopDefaultMode);
    CFRelease(pHidState->pLoopSourceRef);
    CFRelease(pHidState->pTapRef);

    RTSemMutexRequest(pHidState->fifoEventQueueLock, RT_INDEFINITE_WAIT);
    CFRelease(pHidState->pFifoEventQueue);
    pHidState->pFifoEventQueue = NULL;
    RTSemMutexRelease(pHidState->fifoEventQueueLock);

    RTSemMutexDestroy(pHidState->fifoEventQueueLock);
}

#endif /* !VBOX_WITH_KBD_LEDS_SYNC */


void *DarwinHidDevicesKeepLedsState()
{
#ifdef VBOX_WITH_KBD_LEDS_SYNC
    IOReturn         rc;
    VBoxHidsState_t *pHidState;

    pHidState = (VBoxHidsState_t *)malloc(sizeof(VBoxHidsState_t));
    AssertReturn(pHidState, NULL);

    pHidState->hidManagerRef = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (pHidState->hidManagerRef)
    {
        CFDictionaryRef deviceMatchingDictRef = darwinQueryLedDeviceMatchingDictionary();
        if (deviceMatchingDictRef)
        {
            IOHIDManagerScheduleWithRunLoop(pHidState->hidManagerRef, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            IOHIDManagerSetDeviceMatching(pHidState->hidManagerRef, deviceMatchingDictRef);

            rc = IOHIDManagerOpen(pHidState->hidManagerRef, kIOHIDOptionsTypeNone);
            if (rc == kIOReturnSuccess)
            {
                pHidState->pDeviceCollection = CFArrayCreateMutable(kCFAllocatorDefault, 0, NULL);
                if (pHidState->pDeviceCollection)
                {
                    if (darwinAddCarbonHandler(pHidState) == 0)
                    {
                        /* Populate cache with HID devices */
                        CFSetRef pDevicesSet = IOHIDManagerCopyDevices(pHidState->hidManagerRef);
                        if (pDevicesSet)
                        {
                            CFIndex cDevices = CFSetGetCount(pDevicesSet);

                            IOHIDDeviceRef *ppDevices = (IOHIDDeviceRef *)malloc((size_t)cDevices * sizeof(IOHIDDeviceRef));
                            if (ppDevices)
                            {
                                CFSetGetValues(pDevicesSet, (const void **)ppDevices);
                                for (CFIndex i= 0; i < cDevices; i++)
                                    darwinHidAddDevice(pHidState, (IOHIDDeviceRef)ppDevices[i], false);

                                free(ppDevices);
                            }

                            CFRelease(pDevicesSet);
                        }

                        IOHIDManagerRegisterDeviceMatchingCallback(pHidState->hidManagerRef, darwinHidMatchingCallback, (void *)pHidState);

                        CFRelease(deviceMatchingDictRef);

                        /* This states should be set on broadcast */
                        pHidState->guestState.fNumLockOn =
                        pHidState->guestState.fCapsLockOn =
                        pHidState->guestState.fScrollLockOn = false;

                        /* Finally, subscribe to USB HID notifications in order to prevent LED artifacts on
                           automatic power management */
                        if (darwinUsbHidSubscribeInterestNotifications(pHidState) == 0)
                            return pHidState;
                    }
                }

                rc = IOHIDManagerClose(pHidState->hidManagerRef, 0);
                if (rc != kIOReturnSuccess)
                    LogRel2(("Warning! Something went wrong in attempt to close HID device manager!\n"));
            }

            CFRelease(deviceMatchingDictRef);
        }

        CFRelease(pHidState->hidManagerRef);
    }

    free(pHidState);

    return NULL;
#else /* !VBOX_WITH_KBD_LEDS_SYNC */
    return NULL;
#endif
}


int DarwinHidDevicesApplyAndReleaseLedsState(void *pState)
{
#ifdef VBOX_WITH_KBD_LEDS_SYNC
    VBoxHidsState_t *pHidState = (VBoxHidsState_t *)pState;
    IOReturn         rc, rc2 = 0;

    AssertReturn(pHidState, kIOReturnError);

    darwinUsbHidUnsubscribeInterestNotifications(pHidState);

    /* Need to unregister Carbon stuff first: */
    darwinRemoveCarbonHandler(pHidState);

    CFDictionaryRef elementMatchingDict = darwinQueryLedElementMatchingDictionary();
    if (elementMatchingDict)
    {
        /* Restore LEDs: */
        for (CFIndex i = 0; i < CFArrayGetCount(pHidState->pDeviceCollection); i++)
        {
            /* Cycle through supported devices only. */
            VBoxKbdState_t *pKbd;
            pKbd = (VBoxKbdState_t *)CFArrayGetValueAtIndex(pHidState->pDeviceCollection, i);

            if (pKbd)
            {
                rc = darwinSetDeviceLedsState(pKbd->pDevice,
                                              elementMatchingDict,
                                              pKbd->LED.fNumLockOn,
                                              pKbd->LED.fCapsLockOn,
                                              pKbd->LED.fScrollLockOn);
                if (rc != 0)
                {
                    LogRel2(("Unable to restore led states for device (%d)!\n", (int)i));
                    rc2 = kIOReturnError;
                }

                IOHIDDeviceUnscheduleFromRunLoop(pKbd->pDevice, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

                LogRel2(("Restored LEDs for KBD %d (%p): fNumLockOn=%s, fCapsLockOn=%s, fScrollLockOn=%s\n",
                     (int)i, pKbd, VBOX_BOOL_TO_STR_STATE(pKbd->LED.fNumLockOn), VBOX_BOOL_TO_STR_STATE(pKbd->LED.fCapsLockOn),
                     VBOX_BOOL_TO_STR_STATE(pKbd->LED.fScrollLockOn)));

                free(pKbd);
            }
        }

        CFRelease(elementMatchingDict);
    }

    /* Free resources: */
    CFRelease(pHidState->pDeviceCollection);

    rc = IOHIDManagerClose(pHidState->hidManagerRef, 0);
    if (rc != kIOReturnSuccess)
    {
        LogRel2(("Warning! Something went wrong in attempt to close HID device manager!\n"));
        rc2 = kIOReturnError;
    }

    IOHIDManagerUnscheduleFromRunLoop(pHidState->hidManagerRef, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    CFRelease(pHidState->hidManagerRef);

    free(pHidState);

    return rc2;
#else /* !VBOX_WITH_KBD_LEDS_SYNC */
    (void)pState;
    return 0;
#endif /* !VBOX_WITH_KBD_LEDS_SYNC */
}

void DarwinHidDevicesBroadcastLeds(void *pState, bool fNumLockOn, bool fCapsLockOn, bool fScrollLockOn)
{
#ifdef VBOX_WITH_KBD_LEDS_SYNC
    VBoxHidsState_t *pHidState = (VBoxHidsState_t *)pState;
    IOReturn         rc;

    AssertReturnVoid(pHidState);
    AssertReturnVoid(pHidState->pDeviceCollection);

    CFDictionaryRef elementMatchingDict = darwinQueryLedElementMatchingDictionary();
    if (elementMatchingDict)
    {
        LogRel2(("Start LEDs broadcast: fNumLockOn=%s, fCapsLockOn=%s, fScrollLockOn=%s\n",
            VBOX_BOOL_TO_STR_STATE(fNumLockOn), VBOX_BOOL_TO_STR_STATE(fCapsLockOn), VBOX_BOOL_TO_STR_STATE(fScrollLockOn)));

        for (CFIndex i = 0; i < CFArrayGetCount(pHidState->pDeviceCollection); i++)
        {
            /* Cycle through supported devices only. */
            VBoxKbdState_t *pKbd;
            pKbd = (VBoxKbdState_t *)CFArrayGetValueAtIndex(pHidState->pDeviceCollection, i);

            if (pKbd && darwinHidDeviceSupported(pKbd->pDevice))
            {
                rc = darwinSetDeviceLedsState(pKbd->pDevice,
                                              elementMatchingDict,
                                              fNumLockOn,
                                              fCapsLockOn,
                                              fScrollLockOn);
                if (rc != 0)
                    LogRel2(("Unable to restore led states for device (%d)!\n", (int)i));
            }
        }

        LogRel2(("LEDs broadcast completed\n"));

        CFRelease(elementMatchingDict);
    }

    /* Dynamically attached device will use these states: */
    pHidState->guestState.fNumLockOn    = fNumLockOn;
    pHidState->guestState.fCapsLockOn   = fCapsLockOn;
    pHidState->guestState.fScrollLockOn = fScrollLockOn;
#else /* !VBOX_WITH_KBD_LEDS_SYNC */
    (void)fNumLockOn;
    (void)fCapsLockOn;
    (void)fScrollLockOn;
#endif /* !VBOX_WITH_KBD_LEDS_SYNC */
}

