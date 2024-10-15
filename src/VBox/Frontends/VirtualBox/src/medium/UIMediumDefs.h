/* $Id: UIMediumDefs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMedium related declarations.
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

#ifndef FEQT_INCLUDED_SRC_medium_UIMediumDefs_h
#define FEQT_INCLUDED_SRC_medium_UIMediumDefs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QtGlobal>
#include <QString>

/* GUI includes: */
#include "UILibraryDefs.h"

/* COM includes: */
#include "KDeviceType.h"
#include "KStorageBus.h"

/* Forward declarations: */
class CVirtualBox;


/** Medium formats. */
enum UIMediumFormat
{
    UIMediumFormat_VDI,
    UIMediumFormat_VMDK,
    UIMediumFormat_VHD,
    UIMediumFormat_Parallels,
    UIMediumFormat_QED,
    UIMediumFormat_QCOW,
};

/** UIMediumDefs namespace. */
namespace UIMediumDefs
{
    /** UIMedium types. */
    enum UIMediumDeviceType
    {
        UIMediumDeviceType_HardDisk,
        UIMediumDeviceType_DVD,
        UIMediumDeviceType_Floppy,
        UIMediumDeviceType_All,
        UIMediumDeviceType_Invalid
    };

    /** Converts global medium type (KDeviceType) to local (UIMediumDeviceType). */
    SHARED_LIBRARY_STUFF UIMediumDeviceType mediumTypeToLocal(KDeviceType globalType);
    /** Convert local medium type (UIMediumDeviceType) to global (KDeviceType). */
    SHARED_LIBRARY_STUFF KDeviceType mediumTypeToGlobal(UIMediumDeviceType localType);

    /** Returns medium formats which are currently supported by @a comVBox for the given @a enmDeviceType. */
    QList<QPair<QString, QString> > MediumBackends(const CVirtualBox &comVBox, KDeviceType enmDeviceType);
    /** Returns which hard disk formats are currently supported by @a comVBox. */
    QList<QPair<QString, QString> > HDDBackends(const CVirtualBox &comVBox);
    /** Returns which optical disk formats are currently supported by @a comVBox. */
    QList<QPair<QString, QString> > DVDBackends(const CVirtualBox &comVBox);
    /** Returns which floppy disk formats are currently supported by @a comVBox. */
    QList<QPair<QString, QString> > FloppyBackends(const CVirtualBox &comVBox);

    /** Returns the first file extension of the list of file extension support for the @a enmDeviceType. */
   QString getPreferredExtensionForMedium(KDeviceType enmDeviceType);
}
/* Using this namespace globally: */
using namespace UIMediumDefs;

/** Medium-target. */
struct UIMediumTarget
{
    /** Medium-target types. */
    enum UIMediumTargetType
    {
        UIMediumTargetType_WithID,
        UIMediumTargetType_WithLocation,
        UIMediumTargetType_WithFileDialog,
        UIMediumTargetType_CreateAdHocVISO,
        UIMediumTargetType_CreateFloppyDisk
    };

    /** Medium-target constructor. */
    UIMediumTarget(const QString &strName = QString(), qint32 iPort = 0, qint32 iDevice = 0,
                   UIMediumDeviceType aMediumType = UIMediumDeviceType_Invalid,
                   UIMediumTargetType aType = UIMediumTargetType_WithID, const QString &strData = QString())
        : name(strName), port(iPort), device(iDevice)
        , mediumType(aMediumType)
        , type(aType), data(strData)
    {}

    /** Determines controller name. */
    QString name;
    /** Determines controller port. */
    qint32 port;
    /** Determines controller device. */
    qint32 device;

    /** Determines medium-target medium-type. */
    UIMediumDeviceType mediumType;

    /** Determines medium-target type. */
    UIMediumTargetType type;
    /** Depending on medium-target type holds <i>ID</i> or <i>location</i>. */
    QString data;
};

/** Storage-slot struct. */
struct StorageSlot
{
    StorageSlot() : bus(KStorageBus_Null), port(0), device(0) {}
    StorageSlot(const StorageSlot &other) : bus(other.bus), port(other.port), device(other.device) {}
    StorageSlot(KStorageBus otherBus, qint32 iPort, qint32 iDevice) : bus(otherBus), port(iPort), device(iDevice) {}
    StorageSlot& operator=(const StorageSlot &other) { bus = other.bus; port = other.port; device = other.device; return *this; }
    bool operator==(const StorageSlot &other) const { return bus == other.bus && port == other.port && device == other.device; }
    bool operator!=(const StorageSlot &other) const { return bus != other.bus || port != other.port || device != other.device; }
    bool operator<(const StorageSlot &other) const { return (bus <  other.bus) ||
                                                            (bus == other.bus && port <  other.port) ||
                                                            (bus == other.bus && port == other.port && device < other.device); }
    bool operator>(const StorageSlot &other) const { return (bus >  other.bus) ||
                                                            (bus == other.bus && port >  other.port) ||
                                                            (bus == other.bus && port == other.port && device > other.device); }
    bool isNull() const { return bus == KStorageBus_Null; }
    KStorageBus bus; qint32 port; qint32 device;
};

/** Storage-slot struct extension with exact controller name. */
struct ExactStorageSlot : public StorageSlot
{
    ExactStorageSlot(const QString &strController,
                     KStorageBus enmBus, qint32 iPort, qint32 iDevice)
        : StorageSlot(enmBus, iPort, iDevice)
        , controller(strController)
    {}
    QString controller;
};

/* Let QMetaType know about our types: */
Q_DECLARE_METATYPE(UIMediumDeviceType);
Q_DECLARE_METATYPE(UIMediumTarget);
Q_DECLARE_METATYPE(StorageSlot);

#endif /* !FEQT_INCLUDED_SRC_medium_UIMediumDefs_h */
