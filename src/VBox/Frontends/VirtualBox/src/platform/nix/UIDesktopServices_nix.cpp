/* $Id: UIDesktopServices_nix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt GUI - Utility Classes and Functions specific to X11..
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

/* VBox includes */
#include "UIDesktopServices.h"

/* Qt includes */
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QUrl>


bool UIDesktopServices::createMachineShortcut(const QString & /* strSrcFile */, const QString &strDstPath, const QString &strName, const QUuid &uUuid)
{
    QFile link(strDstPath + QDir::separator() + strName + ".desktop");
    if (link.open(QFile::WriteOnly | QFile::Truncate))
    {
        const QString strVBox = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/" + VBOX_GUI_VMRUNNER_IMAGE);
        QTextStream out(&link);
        /* Create a link which starts VirtualBox with the machine uuid. */
        out << "[Desktop Entry]" << Qt::endl
            << "Encoding=UTF-8" << Qt::endl
            << "Version=1.0" << Qt::endl
            << "Name=" << strName << Qt::endl
            << "Comment=Starts the VirtualBox machine " << strName << Qt::endl
            << "Type=Application" << Qt::endl
            << "Exec=" << strVBox << " --comment \"" << strName << "\" --startvm \"" << uUuid.toString() << "\"" << Qt::endl
            << "Icon=virtualbox-vbox.png" << Qt::endl;
        /* This would be a real file link entry, but then we could also simply
         * use a soft link (on most UNIX fs):
        out << "[Desktop Entry]" << Qt::endl
            << "Encoding=UTF-8" << Qt::endl
            << "Version=1.0" << Qt::endl
            << "Name=" << strName << Qt::endl
            << "Type=Link" << Qt::endl
            << "Icon=virtualbox-vbox.png" << Qt::endl
        */
        link.setPermissions(link.permissions() | QFile::ExeOwner);
        /** @todo r=bird: check status here perhaps, might've run out of disk space or
         *        some such thing... */
        return true;
    }
    return false;
}

bool UIDesktopServices::openInFileManager(const QString &strFile)
{
    QFileInfo fi(strFile);
    return QDesktopServices::openUrl(QUrl("file://" + QDir::toNativeSeparators(fi.absolutePath()), QUrl::TolerantMode));
}

