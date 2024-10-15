/* $Id: WinKeyboard.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef FEQT_INCLUDED_SRC_platform_win_WinKeyboard_h
#define FEQT_INCLUDED_SRC_platform_win_WinKeyboard_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UILibraryDefs.h"

/* Other VBox includes: */
#include <iprt/win/windows.h>

SHARED_LIBRARY_STUFF void * WinHidDevicesKeepLedsState(void);
SHARED_LIBRARY_STUFF void   WinHidDevicesApplyAndReleaseLedsState(void *pData);
SHARED_LIBRARY_STUFF void   WinHidDevicesBroadcastLeds(bool fNumLockOn, bool fCapsLockOn, bool fScrollLockOn);

SHARED_LIBRARY_STUFF bool winHidLedsInSync(bool fNumLockOn, bool fCapsLockOn, bool fScrollLockOn);

/** Helper class to deal with Windows AltGr handling.
  *
  * Background: Windows sends AltGr key down and up events as two events: a
  * left control event and a right alt one.  Since the left control event does
  * not correspond to actually pressing or releasing the left control key we
  * would like to detect it and handle it.  This class monitors all key down and
  * up events and if it detects that a left control down event has been sendt
  * although left control should be up it tells us to insert a left control up
  * event into the event stream.  While this does not let us filter out the
  * unwanted event at source, it should still make guest system keyboard handling
  * work correctly. */
class SHARED_LIBRARY_STUFF WinAltGrMonitor
{
public:

    /** Constructor. */
    WinAltGrMonitor() : m_enmFakeControlDetectionState(NONE), m_timeOfLastKeyEvent(0) {}

    /** All key events should be fed to this method.
      * @param iDownScanCode the scan code stripped of the make/break bit
      * @param fKeyDown      is this a key down event?
      * @param fExtended     is this an extended scan code? */
    void updateStateFromKeyEvent(unsigned iDownScanCode, bool fKeyDown, bool fExtended);

    /** Do we need to insert a left control up into the stream? */
    bool isLeftControlReleaseNeeded() const;

    /** Can we tell for sure at this point that the current message is a fake
     * control event?  This method might fail to recognise a fake event, but
     * should never incorrectly flag a non-fake one.
     * @note We deliberately do not call this from the host combination editor
     *       in an attempt to ensure that the other code path also gets enough
     *       test coverage.
     */
    bool isCurrentEventDefinitelyFake(unsigned iDownScanCode,
                                      bool fKeyDown,
                                      bool fExtendedKey) const;

private:

    /** State detection for fake control events which we may have missed. */
    enum
    {
        /** No interesting state. */
        NONE,
        /** The last keypress might be a fake control. */
        LAST_EVENT_WAS_LEFT_CONTROL_DOWN,
        /** Left control is down, so we ignore fake control events. */
        LEFT_CONTROL_DOWN,
        /** A fake control down event and no up was passed to the guest. */
        FAKE_CONTROL_DOWN
    } m_enmFakeControlDetectionState;
    LONG m_timeOfLastKeyEvent;
};

#endif /* !FEQT_INCLUDED_SRC_platform_win_WinKeyboard_h */

