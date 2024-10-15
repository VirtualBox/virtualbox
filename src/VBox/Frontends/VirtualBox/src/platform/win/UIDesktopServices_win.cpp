/* $Id: UIDesktopServices_win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt GUI - Utility Classes and Functions specific to Windows..
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
#include <QDir>
#include <QCoreApplication>
#include <QUuid>

/* System includes */
#include <iprt/win/shlobj.h>


bool UIDesktopServices::createMachineShortcut(const QString & /* strSrcFile */, const QString &strDstPath, const QString &strName, const QUuid &uUuid)
{
    IShellLink *pShl = NULL;
    IPersistFile *pPPF = NULL;
    const QString strVBox = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/" + VBOX_GUI_VMRUNNER_IMAGE);
    QFileInfo fi(strVBox);
    QString strVBoxDir = QDir::toNativeSeparators(fi.absolutePath());
    HRESULT rc = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (void**)(&pShl));
    if (FAILED(rc))
        return false;
    do
    {
/** @todo Not really BSTR, so using PCWSTR, but whatever...   */
        rc = pShl->SetPath((PCWSTR)strVBox.utf16());
        if (FAILED(rc))
            break;
        rc = pShl->SetWorkingDirectory((PCWSTR)strVBoxDir.utf16());
        if (FAILED(rc))
            break;
        QString strArgs = QString("--comment \"%1\" --startvm \"%2\"").arg(strName).arg(uUuid.toString());
        rc = pShl->SetArguments((PCWSTR)strArgs.utf16());
        if (FAILED(rc))
            break;
        QString strDesc = QString("Starts the VirtualBox machine %1").arg(strName);
        rc = pShl->SetDescription((PCWSTR)strDesc.utf16());
        if (FAILED(rc))
            break;
        rc = pShl->QueryInterface(IID_IPersistFile, (void**)&pPPF);
        if (FAILED(rc))
            break;
        QString strLink = QString("%1\\%2.lnk").arg(strDstPath).arg(strName);
        rc = pPPF->Save((PCWSTR)strLink.utf16(), TRUE);
    } while(0);
    if (pPPF)
        pPPF->Release();
    if (pShl)
        pShl->Release();
    return SUCCEEDED(rc);
}

bool UIDesktopServices::openInFileManager(const QString &strFile)
{
    QFileInfo fi(strFile);
    QString strTmp = QDir::toNativeSeparators(fi.absolutePath());

    intptr_t rc = (intptr_t)ShellExecute(NULL, L"explore", (PCWSTR)strTmp.utf16(), NULL, NULL, SW_SHOWNORMAL);

    return rc > 32 ? true : false;
}

