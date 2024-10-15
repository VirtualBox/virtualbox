/* $Id: DisplayServerType.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest / Host common code - Display server type detection + handling.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_GuestHost_DisplayServerType_h
#define VBOX_INCLUDED_GuestHost_DisplayServerType_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

/** Environment variable which is exported when in Wayland Desktop Environment. */
#define VBGH_ENV_WAYLAND_DISPLAY        "WAYLAND_DISPLAY"
/** Environment variable which contains information about currently running Desktop Environment. */
#define VBGH_ENV_XDG_CURRENT_DESKTOP    "XDG_CURRENT_DESKTOP"
/** Environment variable which contains information about currently running session (X11, Wayland, etc). */
#define VBGH_ENV_XDG_SESSION_TYPE       "XDG_SESSION_TYPE"
/** Another environment variable which may contain information about currently running session. */
#define VBGH_ENV_DESKTOP_SESSION        "DESKTOP_SESSION"

/**
 * Enumeration holding a guest / host desktop display server type.
 */
typedef enum
{
    /** No display server detected or (explicitly) not set. */
    VBGHDISPLAYSERVERTYPE_NONE = 0,
    /** Automatic detection -- might not work reliably on all systems. */
    VBGHDISPLAYSERVERTYPE_AUTO,
    /** X11 (X.org). */
    VBGHDISPLAYSERVERTYPE_X11,
    /** Wayland. */
    VBGHDISPLAYSERVERTYPE_PURE_WAYLAND,
    /** XWayland; Wayland is running, but some (older) apps need X as a bridge as well. */
    VBGHDISPLAYSERVERTYPE_XWAYLAND
} VBGHDISPLAYSERVERTYPE;

const char *VBGHDisplayServerTypeToStr(VBGHDISPLAYSERVERTYPE enmType);
VBGHDISPLAYSERVERTYPE VBGHDisplayServerTypeDetect(void);
bool VBGHDisplayServerTypeIsGtkAvailable(void);
bool VBGHDisplayServerTypeIsXAvailable(VBGHDISPLAYSERVERTYPE enmType);
bool VBGHDisplayServerTypeIsWaylandAvailable(VBGHDISPLAYSERVERTYPE enmType);

#endif /* !VBOX_INCLUDED_GuestHost_DisplayServerType_h */

