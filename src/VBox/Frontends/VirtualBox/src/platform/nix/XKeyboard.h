/* $Id: XKeyboard.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Declarations of Linux-specific keyboard functions.
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

#ifndef FEQT_INCLUDED_SRC_platform_nix_XKeyboard_h
#define FEQT_INCLUDED_SRC_platform_nix_XKeyboard_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QString;
typedef struct _XDisplay Display;

/** Initializes the X keyboard subsystem. */
SHARED_LIBRARY_STUFF void initMappedX11Keyboard(Display *pDisplay, const QString &remapScancodes);

/** Handles native XKey events. */
SHARED_LIBRARY_STUFF unsigned handleXKeyEvent(Display *pDisplay, unsigned int iDetail);

/** Handles log requests from initXKeyboard after release logging is started. */
SHARED_LIBRARY_STUFF void doXKeyboardLogging(Display *pDisplay);

/** Wraps for the XkbKeycodeToKeysym(3) API which falls back to the deprecated XKeycodeToKeysym(3) if it is unavailable. */
SHARED_LIBRARY_STUFF unsigned long wrapXkbKeycodeToKeysym(Display *pDisplay, unsigned char cCode,
                                                          unsigned int cGroup, unsigned int cIndex);

#endif /* !FEQT_INCLUDED_SRC_platform_nix_XKeyboard_h */

