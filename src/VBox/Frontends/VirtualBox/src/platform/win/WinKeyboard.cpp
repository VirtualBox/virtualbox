/* $Id: WinKeyboard.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Declarations of utility functions for handling Windows Keyboard specific tasks.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

/* GUI includes: */
#include "UILoggingDefs.h"
#include "WinKeyboard.h"

/* Other VBox includes: */
#include <iprt/assert.h>

/* External includes: */
#include <stdio.h>


/* Beautification of log output */
#define VBOX_BOOL_TO_STR_STATE(x)   (x) ? "ON" : "OFF"
#define VBOX_CONTROL_TO_STR_NAME(x) ((x == VK_CAPITAL) ? "CAPS" : (x == VK_SCROLL ? "SCROLL" : ((x == VK_NUMLOCK) ? "NUM" : "UNKNOWN")))

/* A structure that contains internal control state representation */
typedef struct VBoxModifiersState_t {
    bool fNumLockOn;                        /** A state of NUM LOCK */
    bool fCapsLockOn;                       /** A state of CAPS LOCK */
    bool fScrollLockOn;                     /** A state of SCROLL LOCK */
} VBoxModifiersState_t;

/**
 * Get current state of a keyboard modifier.
 *
 * @param   idModifier        Modifier to examine (VK_CAPITAL, VK_SCROLL or VK_NUMLOCK)
 *
 * @returns modifier state or asserts if wrong modifier is specified.
 */
static bool winGetModifierState(int idModifier)
{
    AssertReturn((idModifier == VK_CAPITAL) || (idModifier == VK_SCROLL) || (idModifier == VK_NUMLOCK), false);
    return (GetKeyState(idModifier) & 0x0001) != 0;
}

/**
 * Set current state of a keyboard modifier.
 *
 * @param   idModifier        Modifier to set (VK_CAPITAL, VK_SCROLL or VK_NUMLOCK)
 * @param   fState            State to set
 */
static void winSetModifierState(int idModifier, bool fState)
{
    AssertReturnVoid((idModifier == VK_CAPITAL) || (idModifier == VK_SCROLL) || (idModifier == VK_NUMLOCK));

    /* If the modifier is already in desired state, just do nothing. Otherwise, toggle it. */
    if (winGetModifierState(idModifier) != fState)
    {
        /* Simulate KeyUp+KeyDown keystroke */
        keybd_event(idModifier, 0, KEYEVENTF_EXTENDEDKEY, 0);
        keybd_event(idModifier, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);

        /* Process posted above keyboard events immediately: */
        MSG msg;
        while (::PeekMessage(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
            ::DispatchMessage(&msg);

        LogRel2(("HID LEDs sync: setting %s state to %s (0x%X).\n",
                 VBOX_CONTROL_TO_STR_NAME(idModifier), VBOX_BOOL_TO_STR_STATE(fState), MapVirtualKey(idModifier, MAPVK_VK_TO_VSC)));
    }
    else
    {
        LogRel2(("HID LEDs sync: setting %s state: skipped: state is already %s (0x%X).\n",
                 VBOX_CONTROL_TO_STR_NAME(idModifier), VBOX_BOOL_TO_STR_STATE(fState), MapVirtualKey(idModifier, MAPVK_VK_TO_VSC)));
    }
}

/** Set all HID LEDs at once. */
static bool winSetHidLeds(bool fNumLockOn, bool fCapsLockOn, bool fScrollLockOn)
{
    winSetModifierState(VK_NUMLOCK, fNumLockOn);
    winSetModifierState(VK_CAPITAL, fCapsLockOn);
    winSetModifierState(VK_SCROLL,  fScrollLockOn);
    return true;
}

/** Check if specified LED states correspond to the system modifier states. */
bool winHidLedsInSync(bool fNumLockOn, bool fCapsLockOn, bool fScrollLockOn)
{
    if (winGetModifierState(VK_NUMLOCK) != fNumLockOn)
        return false;

    if (winGetModifierState(VK_CAPITAL) != fCapsLockOn)
        return false;

    if (winGetModifierState(VK_SCROLL) != fScrollLockOn)
        return false;

    return true;
}

/**
 * Allocate memory and store modifier states there.
 *
 * @returns allocated memory witch contains modifier states or NULL.
 */
void * WinHidDevicesKeepLedsState(void)
{
    VBoxModifiersState_t *pState;

    pState = (VBoxModifiersState_t *)malloc(sizeof(VBoxModifiersState_t));
    if (pState)
    {
        pState->fNumLockOn    = winGetModifierState(VK_NUMLOCK);
        pState->fCapsLockOn   = winGetModifierState(VK_CAPITAL);
        pState->fScrollLockOn = winGetModifierState(VK_SCROLL);

        LogRel2(("HID LEDs sync: host state captured: NUM(%s) CAPS(%s) SCROLL(%s)\n",
                 VBOX_BOOL_TO_STR_STATE(pState->fNumLockOn),
                 VBOX_BOOL_TO_STR_STATE(pState->fCapsLockOn),
                 VBOX_BOOL_TO_STR_STATE(pState->fScrollLockOn)));

        return (void *)pState;
    }

    LogRel2(("HID LEDs sync: unable to allocate memory for HID LEDs synchronization data. LEDs sync will be disabled.\n"));

    return NULL;
}

/**
 * Restore host modifier states and free memory.
 */
void WinHidDevicesApplyAndReleaseLedsState(void *pData)
{
    VBoxModifiersState_t *pState = (VBoxModifiersState_t *)pData;

    if (pState)
    {
        LogRel2(("HID LEDs sync: attempt to restore host state: NUM(%s) CAPS(%s) SCROLL(%s)\n",
                 VBOX_BOOL_TO_STR_STATE(pState->fNumLockOn),
                 VBOX_BOOL_TO_STR_STATE(pState->fCapsLockOn),
                 VBOX_BOOL_TO_STR_STATE(pState->fScrollLockOn)));

        if (winSetHidLeds(pState->fNumLockOn, pState->fCapsLockOn, pState->fScrollLockOn))
            LogRel2(("HID LEDs sync: host state restored\n"));
        else
            LogRel2(("HID LEDs sync: host state restore failed\n"));

        free(pState);
    }
}

/**
 * Broadcast HID modifier states.
 *
 * @param   fNumLockOn        NUM LOCK state
 * @param   fCapsLockOn       CAPS LOCK state
 * @param   fScrollLockOn     SCROLL LOCK state
 */
void WinHidDevicesBroadcastLeds(bool fNumLockOn, bool fCapsLockOn, bool fScrollLockOn)
{
    LogRel2(("HID LEDs sync: start broadcast guest modifier states: NUM(%s) CAPS(%s) SCROLL(%s)\n",
             VBOX_BOOL_TO_STR_STATE(fNumLockOn),
             VBOX_BOOL_TO_STR_STATE(fCapsLockOn),
             VBOX_BOOL_TO_STR_STATE(fScrollLockOn)));

    if (winSetHidLeds(fNumLockOn, fCapsLockOn, fScrollLockOn))
        LogRel2(("HID LEDs sync: broadcast completed\n"));
    else
        LogRel2(("HID LEDs sync: broadcast failed\n"));
}

/** @brief doesCurrentLayoutHaveAltGr
  *
  * @return true if this keyboard layout has an AltGr key, false otherwise
  * Check to see whether the current keyboard layout actually has an AltGr key
  * by checking whether any of the keys which might do produce a symbol when
  * AltGr (Control + Alt) is depressed. Generally this loop will exit pretty
  * early (it exits on the first iteration for my German layout). If there is
  * no AltGr key in the layout then it will run right through, but that should
  * hopefully not happen very often.
  *
  * In theory we could do this once and cache the result, but that involves
  * tracking layout switches to invalidate the cache, and I don't think that the
  * added complexity is worth the price. */
static bool doesCurrentLayoutHaveAltGr()
{
    /* Keyboard state array with VK_CONTROL and VK_MENU depressed. */
    static const BYTE s_auKeyStates[256] =
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x80 };
    WORD ach;
    unsigned i;

    for (i = '0'; i <= VK_OEM_102; ++i)
    {
        if (ToAscii(i, 0, s_auKeyStates, &ach, 0))
            break;
        /* Skip ranges of virtual keys which are undefined or not relevant. */
        if (i == '9')
            i = 'A' - 1;
        if (i == 'Z')
            i = VK_OEM_1 - 1;
        if (i == VK_OEM_3)
            i = VK_OEM_4 - 1;
        if (i == VK_OEM_8)
            i = VK_OEM_102 - 1;
    }
    if (i > VK_OEM_102)
        return false;
    return true;
}

void WinAltGrMonitor::updateStateFromKeyEvent(unsigned iDownScanCode,
                                              bool fKeyDown, bool fExtendedKey)
{
    LONG messageTime = GetMessageTime();
    /* We do not want the make/break: */
    AssertRelease(~iDownScanCode & 0x80);
    /* Depending on m_enmFakeControlDetectionState: */
    switch (m_enmFakeControlDetectionState)
    {
        case NONE:
        case FAKE_CONTROL_DOWN:
            if (   iDownScanCode == 0x1D /* left control */
                && fKeyDown
                && !fExtendedKey)
                m_enmFakeControlDetectionState = LAST_EVENT_WAS_LEFT_CONTROL_DOWN;
            else
                m_enmFakeControlDetectionState = NONE;
            break;
        case LAST_EVENT_WAS_LEFT_CONTROL_DOWN:
            if (   iDownScanCode == 0x38 /* Alt */
                && fKeyDown
                && fExtendedKey
                && m_timeOfLastKeyEvent == messageTime
                && doesCurrentLayoutHaveAltGr())
            {
                m_enmFakeControlDetectionState = FAKE_CONTROL_DOWN;
                break;
            }
            else
                m_enmFakeControlDetectionState = LEFT_CONTROL_DOWN;
            RT_FALL_THRU();
        case LEFT_CONTROL_DOWN:
            if (   iDownScanCode == 0x1D /* left control */
                && !fKeyDown
                && !fExtendedKey)
                m_enmFakeControlDetectionState = NONE;
            break;
        default:
            AssertReleaseMsgFailed(("Unknown AltGr detection state.\n"));
    }
    m_timeOfLastKeyEvent = messageTime;
}

bool WinAltGrMonitor::isLeftControlReleaseNeeded() const
{
    return m_enmFakeControlDetectionState == FAKE_CONTROL_DOWN;
}

bool WinAltGrMonitor::isCurrentEventDefinitelyFake(unsigned iDownScanCode,
                                                   bool fKeyDown,
                                                   bool fExtendedKey) const
{
    if (iDownScanCode != 0x1d /* scan code: Control */ || fExtendedKey)
        return false;

    LONG messageTime = GetMessageTime();
    MSG peekMsg;
    if (!PeekMessage(&peekMsg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_NOREMOVE))
        return false;
    if (messageTime != (LONG)peekMsg.time)
        return false;

    if (   fKeyDown
        && peekMsg.message != WM_KEYDOWN
        && peekMsg.message != WM_SYSKEYDOWN)
        return false;
    if (   !fKeyDown
        && peekMsg.message != WM_KEYUP
        && peekMsg.message != WM_SYSKEYUP)
        return false;
    if (   (RT_HIWORD(peekMsg.lParam) & 0xFF) != 0x38 /* scan code: Alt */
        || !(RT_HIWORD(peekMsg.lParam) & KF_EXTENDED))
        return false;
    if (!doesCurrentLayoutHaveAltGr())
        return false;
    return true;
}
