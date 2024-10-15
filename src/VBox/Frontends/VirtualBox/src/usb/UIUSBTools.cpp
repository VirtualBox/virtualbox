/* $Id: UIUSBTools.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUSBTools namespace implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QVector>

/* GUI includes: */
#include "UIConverter.h"
#include "UIUSBTools.h"

/* COM includes: */
#include "CHostUSBDevice.h"
#include "CHostVideoInputDevice.h"
#include "CUSBDevice.h"
#include "CUSBDeviceFilter.h"

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h>


QString UIUSBTools::usbDetails(const CUSBDevice &comDevice)
{
    QString strDetails;
    if (comDevice.isNull())
        strDetails = QApplication::translate("UIUSBTools", "Unknown device", "USB device details");
    else
    {
        QVector<QString> devInfoVector = comDevice.GetDeviceInfo();
        QString strManufacturer;
        QString strProduct;

        if (devInfoVector.size() >= 1)
            strManufacturer = devInfoVector[0].trimmed();
        if (devInfoVector.size() >= 2)
            strProduct = devInfoVector[1].trimmed();

        if (strManufacturer.isEmpty() && strProduct.isEmpty())
        {
            strDetails =
                QApplication::translate("UIUSBTools", "Unknown device %1:%2", "USB device details")
                   .arg(QString::number(comDevice.GetVendorId(),  16).toUpper().rightJustified(4, '0'))
                   .arg(QString::number(comDevice.GetProductId(), 16).toUpper().rightJustified(4, '0'));
        }
        else
        {
            if (strProduct.toUpper().startsWith(strManufacturer.toUpper()))
                strDetails = strProduct;
            else
                strDetails = strManufacturer + " " + strProduct;
        }
        ushort iRev = comDevice.GetRevision();
        if (iRev != 0)
        {
            strDetails += " [";
            strDetails += QString::number(iRev, 16).toUpper().rightJustified(4, '0');
            strDetails += "]";
        }
    }

    return strDetails.trimmed();
}

QString UIUSBTools::usbToolTip(const CUSBDevice &comDevice)
{
    QString strTip =
        QApplication::translate("UIUSBTools", "<nobr>Vendor ID: %1</nobr><br>"
                                "<nobr>Product ID: %2</nobr><br>"
                                "<nobr>Revision: %3</nobr>", "USB device tooltip")
           .arg(QString::number(comDevice.GetVendorId(),  16).toUpper().rightJustified(4, '0'))
           .arg(QString::number(comDevice.GetProductId(), 16).toUpper().rightJustified(4, '0'))
           .arg(QString::number(comDevice.GetRevision(),  16).toUpper().rightJustified(4, '0'));

    const QString strSerial = comDevice.GetSerialNumber();
    if (!strSerial.isEmpty())
        strTip += QApplication::translate("UIUSBTools",
                                          "<br><nobr>Serial No. %1</nobr>",
                                          "USB device tooltip").arg(strSerial);

    /* Add the state field if it's a host USB device: */
    CHostUSBDevice hostDev(comDevice);
    if (!hostDev.isNull())
    {
        strTip += QString(QApplication::translate("UIUSBTools",
                                                  "<br><nobr>State: %1</nobr>",
                                                  "USB device tooltip"))
                             .arg(gpConverter->toString(hostDev.GetState()));
    }

    return strTip;
}

QString UIUSBTools::usbToolTip(const CUSBDeviceFilter &comFilter)
{
    QString strTip;

    const QString strVendorId = comFilter.GetVendorId();
    if (!strVendorId.isEmpty())
        strTip += QApplication::translate("UIUSBTools",
                                          "<nobr>Vendor ID: %1</nobr>",
                                          "USB filter tooltip").arg(strVendorId);

    const QString strProductId = comFilter.GetProductId();
    if (!strProductId.isEmpty())
        strTip += strTip.isEmpty() ? "" : "<br/>" + QApplication::translate("UIUSBTools",
                                                                            "<nobr>Product ID: %2</nobr>",
                                                                            "USB filter tooltip").arg(strProductId);

    const QString strRevision = comFilter.GetRevision();
    if (!strRevision.isEmpty())
        strTip += strTip.isEmpty() ? "" : "<br/>" + QApplication::translate("UIUSBTools",
                                                                            "<nobr>Revision: %3</nobr>",
                                                                            "USB filter tooltip").arg(strRevision);

    const QString strProduct = comFilter.GetProduct();
    if (!strProduct.isEmpty())
        strTip += strTip.isEmpty() ? "" : "<br/>" + QApplication::translate("UIUSBTools",
                                                                            "<nobr>Product: %4</nobr>",
                                                                            "USB filter tooltip").arg(strProduct);

    const QString strManufacturer = comFilter.GetManufacturer();
    if (!strManufacturer.isEmpty())
        strTip += strTip.isEmpty() ? "" : "<br/>" + QApplication::translate("UIUSBTools",
                                                                            "<nobr>Manufacturer: %5</nobr>",
                                                                            "USB filter tooltip").arg(strManufacturer);

    const QString strSerial = comFilter.GetSerialNumber();
    if (!strSerial.isEmpty())
        strTip += strTip.isEmpty() ? "" : "<br/>" + QApplication::translate("UIUSBTools",
                                                                            "<nobr>Serial No.: %1</nobr>",
                                                                            "USB filter tooltip").arg(strSerial);

    const QString strPort = comFilter.GetPort();
    if (!strPort.isEmpty())
        strTip += strTip.isEmpty() ? "" : "<br/>" + QApplication::translate("UIUSBTools",
                                                                            "<nobr>Port: %1</nobr>",
                                                                            "USB filter tooltip").arg(strPort);

    /* Add the state field if it's a host USB device: */
    CHostUSBDevice hostDev(comFilter);
    if (!hostDev.isNull())
    {
        strTip += strTip.isEmpty() ? "":"<br/>" + QApplication::translate("UIUSBTools",
                                                                          "<nobr>State: %1</nobr>",
                                                                          "USB filter tooltip")
                                                     .arg(gpConverter->toString(hostDev.GetState()));
    }

    return strTip;
}

QString UIUSBTools::usbToolTip(const CHostVideoInputDevice &comWebcam)
{
    QStringList records;

    const QString strName = comWebcam.GetName();
    if (!strName.isEmpty())
        records << strName;

    const QString strPath = comWebcam.GetPath();
    if (!strPath.isEmpty())
        records << strPath;

    return records.join("<br>");
}
