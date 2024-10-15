/* $Id: DarwinKeyboard.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef FEQT_INCLUDED_SRC_platform_darwin_DarwinKeyboard_h
#define FEQT_INCLUDED_SRC_platform_darwin_DarwinKeyboard_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UILibraryDefs.h"

/* External includes: */
#include <CoreFoundation/CFBase.h>


RT_C_DECLS_BEGIN

/** Private hack for missing rightCmdKey enum. */
#define kEventKeyModifierRightCmdKeyMask (1<<27)

/** The scancode mask. */
#define VBOXKEY_SCANCODE_MASK       0x007f
/** Extended key. */
#define VBOXKEY_EXTENDED            0x0080
/** Modifier key. */
#define VBOXKEY_MODIFIER            0x0400
/** Lock key (like num lock and caps lock). */
#define VBOXKEY_LOCK                0x0800

/** Converts a darwin (virtual) key code to a set 1 scan code. */
SHARED_LIBRARY_STUFF unsigned DarwinKeycodeToSet1Scancode(unsigned uKeyCode);
/** Adjusts the modifier mask left / right using the current keyboard state. */
SHARED_LIBRARY_STUFF UInt32   DarwinAdjustModifierMask(UInt32 fModifiers, const void *pvCocoaEvent);
/** Converts a single modifier to a set 1 scan code. */
SHARED_LIBRARY_STUFF unsigned DarwinModifierMaskToSet1Scancode(UInt32 fModifiers);
/** Converts a single modifier to a darwin keycode. */
SHARED_LIBRARY_STUFF unsigned DarwinModifierMaskToDarwinKeycode(UInt32 fModifiers);
/** Converts a darwin keycode to a modifier mask. */
SHARED_LIBRARY_STUFF UInt32   DarwinKeyCodeToDarwinModifierMask(unsigned uKeyCode);

/** Disables or enabled global hot keys. */
SHARED_LIBRARY_STUFF void     DarwinDisableGlobalHotKeys(bool fDisable);

/** Start grabbing keyboard events.
  * @param   fGlobalHotkeys  Brings whether to disable global hotkeys or not. */
SHARED_LIBRARY_STUFF void     DarwinGrabKeyboard(bool fGlobalHotkeys);
/** Reverses the actions taken by DarwinGrabKeyboard. */
SHARED_LIBRARY_STUFF void     DarwinReleaseKeyboard();

/** Saves the states of leds for all HID devices attached to the system and return it. */
SHARED_LIBRARY_STUFF void    *DarwinHidDevicesKeepLedsState();

/** Applies LEDs @a pState release its resources afterwards. */
SHARED_LIBRARY_STUFF int      DarwinHidDevicesApplyAndReleaseLedsState(void *pState);
/** Set states for host keyboard LEDs.
  * @note This function will set led values for all
  *       keyboard devices attached to the system.
  * @param pState         Brings the pointer to saved LEDs state.
  * @param fNumLockOn     Turns on NumLock led if TRUE, off otherwise
  * @param fCapsLockOn    Turns on CapsLock led if TRUE, off otherwise
  * @param fScrollLockOn  Turns on ScrollLock led if TRUE, off otherwise */
SHARED_LIBRARY_STUFF void     DarwinHidDevicesBroadcastLeds(void *pState, bool fNumLockOn, bool fCapsLockOn, bool fScrollLockOn);

RT_C_DECLS_END


#endif /* !FEQT_INCLUDED_SRC_platform_darwin_DarwinKeyboard_h */

