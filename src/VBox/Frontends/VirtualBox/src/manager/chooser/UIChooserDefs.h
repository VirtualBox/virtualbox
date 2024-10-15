/* $Id: UIChooserDefs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserDefs class declaration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_chooser_UIChooserDefs_h
#define FEQT_INCLUDED_SRC_manager_chooser_UIChooserDefs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QGraphicsItem>

/* Other VBox includes: */
#include <iprt/cdefs.h> // for RT_BIT stuff


/** UIChooserNode types. */
enum UIChooserNodeType
{
    UIChooserNodeType_Any     = QGraphicsItem::UserType,
    UIChooserNodeType_Group,
    UIChooserNodeType_Global,
    UIChooserNodeType_Machine
};


/** UIChooserNodeGroup types. */
enum UIChooserNodeGroupType
{
    UIChooserNodeGroupType_Invalid,
    UIChooserNodeGroupType_Local,
    UIChooserNodeGroupType_Provider,
    UIChooserNodeGroupType_Profile
};


/** UIChooserNode extra-data prefix types. */
enum UIChooserNodeDataPrefixType
{
    UIChooserNodeDataPrefixType_Global,
    UIChooserNodeDataPrefixType_Machine,
    UIChooserNodeDataPrefixType_Local,
    UIChooserNodeDataPrefixType_Provider,
    UIChooserNodeDataPrefixType_Profile
};


/** UIChooserNode extra-data option types. */
enum UIChooserNodeDataOptionType
{
    UIChooserNodeDataOptionType_GlobalFavorite,
    UIChooserNodeDataOptionType_GroupOpened
};


/** UIChooserNode extra-data value types. */
enum UIChooserNodeDataValueType
{
    UIChooserNodeDataValueType_GlobalDefault
};


/** UIChooserItem search flags. */
enum UIChooserItemSearchFlag
{
    UIChooserItemSearchFlag_Global        = RT_BIT(0),
    UIChooserItemSearchFlag_Machine       = RT_BIT(1),
    UIChooserItemSearchFlag_LocalGroup    = RT_BIT(2),
    UIChooserItemSearchFlag_CloudProvider = RT_BIT(3),
    UIChooserItemSearchFlag_CloudProfile  = RT_BIT(4),
    UIChooserItemSearchFlag_ExactId       = RT_BIT(5),
    UIChooserItemSearchFlag_ExactName     = RT_BIT(6),
    UIChooserItemSearchFlag_FullName      = RT_BIT(7),
};


/** UIChooserItem drag token types. */
enum UIChooserItemDragToken
{
    UIChooserItemDragToken_Off,
    UIChooserItemDragToken_Up,
    UIChooserItemDragToken_Down
};


/** UIChooserItemMachine enumeration flags. */
enum UIChooserItemMachineEnumerationFlag
{
    UIChooserItemMachineEnumerationFlag_Unique       = RT_BIT(0),
    UIChooserItemMachineEnumerationFlag_Inaccessible = RT_BIT(1)
};


#endif /* !FEQT_INCLUDED_SRC_manager_chooser_UIChooserDefs_h */
