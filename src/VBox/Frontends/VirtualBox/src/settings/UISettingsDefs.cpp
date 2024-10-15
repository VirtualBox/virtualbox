/* $Id: UISettingsDefs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISettingsDefs implementation
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include "UISettingsDefs.h"


/* Using declarations: */
using namespace UISettingsDefs;

ConfigurationAccessLevel UISettingsDefs::configurationAccessLevel(KSessionState enmSessionState, KMachineState enmMachineState)
{
    /* Depending on passed arguments: */
    switch (enmMachineState)
    {
        case KMachineState_PoweredOff:
        case KMachineState_Teleported:
        case KMachineState_Aborted:    return enmSessionState == KSessionState_Unlocked ?
                                              ConfigurationAccessLevel_Full :
                                              ConfigurationAccessLevel_Partial_PoweredOff;
        case KMachineState_AbortedSaved:
        case KMachineState_Saved:      return ConfigurationAccessLevel_Partial_Saved;
        case KMachineState_Running:
        case KMachineState_Paused:     return ConfigurationAccessLevel_Partial_Running;
        default: break;
    }
    /* Null by default: */
    return ConfigurationAccessLevel_Null;
}

