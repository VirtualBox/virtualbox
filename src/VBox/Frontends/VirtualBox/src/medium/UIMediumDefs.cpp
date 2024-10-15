/* $Id: UIMediumDefs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMedium related implementations.
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
#include "UIGlobalSession.h"
#include "UIMediumDefs.h"

/* COM includes: */
#include "CMediumFormat.h"
#include "CSystemProperties.h"

/* COM includes: */
#include "CMediumFormat.h"
#include "CSystemProperties.h"
#include "CVirtualBox.h"


/* Convert global medium type (KDeviceType) to local (UIMediumDeviceType): */
UIMediumDeviceType UIMediumDefs::mediumTypeToLocal(KDeviceType globalType)
{
    switch (globalType)
    {
        case KDeviceType_HardDisk:
            return UIMediumDeviceType_HardDisk;
        case KDeviceType_DVD:
            return UIMediumDeviceType_DVD;
        case KDeviceType_Floppy:
            return UIMediumDeviceType_Floppy;
        default:
            break;
    }
    return UIMediumDeviceType_Invalid;
}

/* Convert local medium type (UIMediumDeviceType) to global (KDeviceType): */
KDeviceType UIMediumDefs::mediumTypeToGlobal(UIMediumDeviceType localType)
{
    switch (localType)
    {
        case UIMediumDeviceType_HardDisk:
            return KDeviceType_HardDisk;
        case UIMediumDeviceType_DVD:
            return KDeviceType_DVD;
        case UIMediumDeviceType_Floppy:
            return KDeviceType_Floppy;
        default:
            break;
    }
    return KDeviceType_Null;
}

QList<QPair<QString, QString> > UIMediumDefs::MediumBackends(const CVirtualBox &comVBox, KDeviceType enmType)
{
    /* Prepare a list of pairs with the form <tt>{"Backend Name", "*.suffix1 .suffix2 ..."}</tt>. */
    const CSystemProperties comSystemProperties = comVBox.GetSystemProperties();
    QVector<CMediumFormat> mediumFormats = comSystemProperties.GetMediumFormats();
    QList<QPair<QString, QString> > backendPropList;
    for (int i = 0; i < mediumFormats.size(); ++i)
    {
        /* Acquire file extensions & device types: */
        QVector<QString> fileExtensions;
        QVector<KDeviceType> deviceTypes;
        mediumFormats[i].DescribeFileExtensions(fileExtensions, deviceTypes);

        /* Compose filters list: */
        QStringList filters;
        for (int iExtensionIndex = 0; iExtensionIndex < fileExtensions.size(); ++iExtensionIndex)
            if (deviceTypes[iExtensionIndex] == enmType)
                filters << QString("*.%1").arg(fileExtensions[iExtensionIndex]);
        /* Create a pair out of the backend description and all suffix's. */
        if (!filters.isEmpty())
            backendPropList << QPair<QString, QString>(mediumFormats[i].GetName(), filters.join(" "));
    }
    return backendPropList;
}

QList<QPair<QString, QString> > UIMediumDefs::HDDBackends(const CVirtualBox &comVBox)
{
    return MediumBackends(comVBox, KDeviceType_HardDisk);
}

QList<QPair<QString, QString> > UIMediumDefs::DVDBackends(const CVirtualBox &comVBox)
{
    return MediumBackends(comVBox, KDeviceType_DVD);
}

QList<QPair<QString, QString> > UIMediumDefs::FloppyBackends(const CVirtualBox &comVBox)
{
    return MediumBackends(comVBox, KDeviceType_Floppy);
}

QString UIMediumDefs::getPreferredExtensionForMedium(KDeviceType enmDeviceType)
{
    CSystemProperties comSystemProperties = gpGlobalSession->virtualBox().GetSystemProperties();
    QVector<CMediumFormat> mediumFormats = comSystemProperties.GetMediumFormats();
    for (int i = 0; i < mediumFormats.size(); ++i)
    {
        /* File extensions */
        QVector <QString> fileExtensions;
        QVector <KDeviceType> deviceTypes;

        mediumFormats[i].DescribeFileExtensions(fileExtensions, deviceTypes);
        if (fileExtensions.size() != deviceTypes.size())
            continue;
        for (int a = 0; a < fileExtensions.size(); ++a)
        {
            if (deviceTypes[a] == enmDeviceType)
                return fileExtensions[a];
        }
    }
    return QString();
}
