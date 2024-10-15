/* $Id: UIMachineAttributeSetter.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineAttributeSetter namespace declaration.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIMachineAttributeSetter_h
#define FEQT_INCLUDED_SRC_globals_UIMachineAttributeSetter_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* COM includes: */
#include "CMachine.h"
#include "KNetworkAttachmentType.h"

/** Known machine attributes. */
enum MachineAttribute
{
    MachineAttribute_Invalid,
    MachineAttribute_Name,
    MachineAttribute_OSType,
    MachineAttribute_BaseMemory,
    MachineAttribute_BootOrder,
    MachineAttribute_VideoMemory,
    MachineAttribute_GraphicsControllerType,
    MachineAttribute_AudioHostDriverType,
    MachineAttribute_AudioControllerType,
    MachineAttribute_NetworkAttachmentType,
    MachineAttribute_USBControllerType,
};

/** Contains short network adapter description. */
struct UINetworkAdapterDescriptor
{
    /** Composes network adapter descriptor for certain @a iSlot, @a enmType and @a strName. */
    UINetworkAdapterDescriptor(int iSlot = -1,
                               KNetworkAttachmentType enmType = KNetworkAttachmentType_Null,
                               const QString &strName = QString())
        : m_iSlot(iSlot), m_enmType(enmType), m_strName(strName)
    {}

    /** Holds the slot of described network adapter. */
    int                     m_iSlot;
    /** Holds the attachment type of described network adapter. */
    KNetworkAttachmentType  m_enmType;
    /** Holds the adapter name of described network adapter. */
    QString                 m_strName;
};
Q_DECLARE_METATYPE(UINetworkAdapterDescriptor);

/** A set of USB controller types. */
typedef QSet<KUSBControllerType> UIUSBControllerTypeSet;
Q_DECLARE_METATYPE(UIUSBControllerTypeSet);

/** Namespace used to assign CMachine attributes on more convenient basis. */
namespace UIMachineAttributeSetter
{
    /** Assigns @a comMachine @a guiAttribute of specified @a enmType. */
    SHARED_LIBRARY_STUFF void setMachineAttribute(const CMachine &comMachine,
                                                  const MachineAttribute &enmType,
                                                  const QVariant &guiAttribute);

    /** Assigns @a comMachine @a strLocation. */
    SHARED_LIBRARY_STUFF void setMachineLocation(const QUuid &uMachineId,
                                                 const QString &strLocation);
}
using namespace UIMachineAttributeSetter /* if header included */;

#endif /* !FEQT_INCLUDED_SRC_globals_UIMachineAttributeSetter_h */
