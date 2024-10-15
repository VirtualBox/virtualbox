/* $Id: UILocalMachineStuff.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UILocalMachineStuff namespace declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UILocalMachineStuff_h
#define FEQT_INCLUDED_SRC_globals_UILocalMachineStuff_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QUuid>

/* GUI includes: */
#include "UIDefs.h"

/* COM includes: */
#include "KLockType.h"
#include "CSession.h"

/* Forward declarations: */
class CMachine;

/** Cloud networking stuff namespace. */
namespace UILocalMachineStuff
{
    /** Switches to certain @a comMachine. */
    SHARED_LIBRARY_STUFF bool switchToMachine(CMachine &comMachine);
    /** Launches certain @a comMachine in specified @a enmLaunchMode. */
    SHARED_LIBRARY_STUFF bool launchMachine(CMachine &comMachine, UILaunchMode enmLaunchMode = UILaunchMode_Default);

    /** Opens session of certain @a enmLockType for VM with certain @a uId. */
    SHARED_LIBRARY_STUFF CSession openSession(QUuid uId, KLockType enmLockType = KLockType_Write);
    /** Opens session of certain @a enmLockType for currently chosen VM. */
    SHARED_LIBRARY_STUFF CSession openSession(KLockType enmLockType = KLockType_Write);
    /** Opens session of KLockType_Shared type for VM with certain @a uId. */
    SHARED_LIBRARY_STUFF CSession openExistingSession(const QUuid &uId);
    /** Tries to guess if new @a comSession needs to be opened for certain @a comMachine,
      * if yes, new session of required type will be opened and machine will be updated,
      * otherwise, no session will be created and machine will be left unchanged. */
    SHARED_LIBRARY_STUFF CSession tryToOpenSessionFor(CMachine &comMachine);
}

/* Using across any module who included us: */
using namespace UILocalMachineStuff;

#endif /* !FEQT_INCLUDED_SRC_globals_UILocalMachineStuff_h */
