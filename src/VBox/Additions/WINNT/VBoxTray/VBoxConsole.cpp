/* $Id: VBoxConsole.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxConsole.cpp - Console APIs.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#include "VBoxTray.h"
#include "VBoxTrayInternal.h"


/** @todo r=andy Lacks documentation / Doxygen headers. What does this, and why? */

BOOL VBoxConsoleIsAllowed()
{
    return vboxDtIsInputDesktop() && vboxStIsActiveConsole();
}

void VBoxConsoleEnable(BOOL fEnable)
{
    if (fEnable)
        VBoxCapsAcquireAllSupported();
    else
        VBoxCapsReleaseAll();
}

void VBoxConsoleCapSetSupported(uint32_t iCap, BOOL fSupported)
{
    if (fSupported)
    {
        VBoxCapsEntryFuncStateSet(iCap, VBOXCAPS_ENTRY_FUNCSTATE_SUPPORTED);

        if (VBoxConsoleIsAllowed())
            VBoxCapsEntryAcquire(iCap);
    }
    else
    {
        VBoxCapsEntryFuncStateSet(iCap, VBOXCAPS_ENTRY_FUNCSTATE_UNSUPPORTED);

        VBoxCapsEntryRelease(iCap);
    }
}

