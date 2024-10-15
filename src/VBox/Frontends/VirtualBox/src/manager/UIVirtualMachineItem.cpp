/* $Id: UIVirtualMachineItem.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualMachineItem class implementation.
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

/* GUI includes: */
#include "UIVirtualMachineItemCloud.h"
#include "UIVirtualMachineItemLocal.h"


/*********************************************************************************************************************************
*   Class UIVirtualMachineItem implementation.                                                                                   *
*********************************************************************************************************************************/

UIVirtualMachineItem::UIVirtualMachineItem(UIVirtualMachineItemType enmType)
    : m_enmType(enmType)
    , m_fAccessible(false)
    , m_enmConfigurationAccessLevel(ConfigurationAccessLevel_Null)
    , m_fHasDetails(false)
{
}

UIVirtualMachineItem::~UIVirtualMachineItem()
{
}

UIVirtualMachineItemLocal *UIVirtualMachineItem::toLocal()
{
    return   itemType() == UIVirtualMachineItemType_Local
           ? static_cast<UIVirtualMachineItemLocal*>(this)
           : 0;
}

UIVirtualMachineItemCloud *UIVirtualMachineItem::toCloud()
{
    return   (   itemType() == UIVirtualMachineItemType_CloudFake
              || itemType() == UIVirtualMachineItemType_CloudReal)
           ? static_cast<UIVirtualMachineItemCloud*>(this)
           : 0;
}

QPixmap UIVirtualMachineItem::osPixmap(QSize *pLogicalSize /* = 0 */) const
{
    if (pLogicalSize)
        *pLogicalSize = m_logicalPixmapSize;
    return m_pixmap;
}


/*********************************************************************************************************************************
*   Class UIVirtualMachineItemMimeData implementation.                                                                           *
*********************************************************************************************************************************/

QString UIVirtualMachineItemMimeData::m_type = "application/org.virtualbox.gui.vmselector.UIVirtualMachineItem";

UIVirtualMachineItemMimeData::UIVirtualMachineItemMimeData(UIVirtualMachineItem *pItem)
    : m_pItem(pItem)
{
}

QStringList UIVirtualMachineItemMimeData::formats() const
{
    QStringList types;
    types << type();
    return types;
}
