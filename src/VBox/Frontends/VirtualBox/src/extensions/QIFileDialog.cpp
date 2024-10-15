/* $Id: QIFileDialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIFileDialog class implementation.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#ifdef VBOX_WS_MAC
# include <QEventLoop>
#endif

/* GUI includes: */
#include "QIFileDialog.h"
#include "UIModalWindowManager.h"


QIFileDialog::QIFileDialog(QWidget *pParent, Qt::WindowFlags enmFlags)
    : QFileDialog(pParent, enmFlags)
{
}

/* static */
QString QIFileDialog::getExistingDirectory(const QString &strDir,
                                           QWidget *pParent,
                                           const QString &strCaption,
                                           bool fDirOnly,
                                           bool fResolveSymLinks)
{
#ifdef VBOX_WS_MAC

    // WORKAROUND:
    // After 4.5 exec ignores the Qt::Sheet flag.
    // See "New Ways of Using Dialogs" in http://doc.trolltech.com/qq/QtQuarterly30.pdf why.
    // We want the old behavior for file-save dialog. Unfortunately there is a bug in Qt 4.5.x
    // which result in showing the native & the Qt dialog at the same time.
    QWidget *pRealParent = windowManager().realParentWindow(pParent);
    QFileDialog dlg(pRealParent);
    windowManager().registerNewParent(&dlg, pRealParent);
    dlg.setWindowTitle(strCaption);
    dlg.setDirectory(strDir);
    dlg.setOption(DontResolveSymlinks, !fResolveSymLinks);
    dlg.setFileMode(QFileDialog::Directory);
    if (fDirOnly)
        dlg.setOption(ShowDirsOnly, true);

    QEventLoop eventLoop;
    QObject::connect(&dlg, &QFileDialog::finished,
                     &eventLoop, &QEventLoop::quit);
    dlg.open();
    eventLoop.exec();

    return dlg.result() == QDialog::Accepted ? dlg.selectedFiles().value(0, QString()) : QString();

#else /* !VBOX_WS_MAC */

    QFileDialog::Options o;
    if (fDirOnly)
        o |= QFileDialog::ShowDirsOnly;
    if (!fResolveSymLinks)
        o |= QFileDialog::DontResolveSymlinks;
    return QFileDialog::getExistingDirectory(pParent, strCaption, strDir, o);

#endif /* !VBOX_WS_MAC */
}

/* static */
QString QIFileDialog::getSaveFileName(const QString &strStartWith,
                                      const QString &strFilters,
                                      QWidget       *pParent,
                                      const QString &strCaption,
                                      QString       *pStrSelectedFilter /* = 0 */,
                                      bool           fResolveSymLinks /* = true */,
                                      bool           fConfirmOverwrite /* = false */)
{
#ifdef VBOX_WS_MAC

    // WORKAROUND:
    // After 4.5 exec ignores the Qt::Sheet flag.
    // See "New Ways of Using Dialogs" in http://doc.trolltech.com/qq/QtQuarterly30.pdf why.
    // We want the old behavior for file-save dialog. Unfortunately there is a bug in Qt 4.5.x
    // which result in showing the native & the Qt dialog at the same time.
    QWidget *pRealParent = windowManager().realParentWindow(pParent);
    QFileDialog dlg(pRealParent);
    windowManager().registerNewParent(&dlg, pRealParent);
    dlg.setWindowTitle(strCaption);

    /* Some predictive algorithm which seems missed in native code. */
    QDir dir(strStartWith);
    while (!dir.isRoot() && !dir.exists())
        dir = QDir(QFileInfo(dir.absolutePath()).absolutePath());
    const QString strDirectory = dir.absolutePath();
    if (!strDirectory.isNull())
        dlg.setDirectory(strDirectory);
    if (strDirectory != strStartWith)
        dlg.selectFile(QFileInfo(strStartWith).absoluteFilePath());

    dlg.setNameFilter(strFilters);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    if (pStrSelectedFilter)
        dlg.selectNameFilter(*pStrSelectedFilter);
    dlg.setOption(DontResolveSymlinks, !fResolveSymLinks);
    dlg.setOption(DontConfirmOverwrite, !fConfirmOverwrite);

    QEventLoop eventLoop;
    QObject::connect(&dlg, &QFileDialog::finished,
                     &eventLoop, &QEventLoop::quit);
    dlg.open();
    eventLoop.exec();

    return dlg.result() == QDialog::Accepted ? dlg.selectedFiles().value(0, QString()) : QString();

#else /* !VBOX_WS_MAC */

    QFileDialog::Options o;
    if (!fResolveSymLinks)
        o |= QFileDialog::DontResolveSymlinks;
    if (!fConfirmOverwrite)
        o |= QFileDialog::DontConfirmOverwrite;
    return QFileDialog::getSaveFileName(pParent, strCaption, strStartWith,
                                        strFilters, pStrSelectedFilter, o);

#endif /* !VBOX_WS_MAC */
}

/* static */
QString QIFileDialog::getOpenFileName(const QString &strStartWith,
                                      const QString &strFilters,
                                      QWidget       *pParent,
                                      const QString &strCaption,
                                      QString       *pStrSelectedFilter /* = 0 */,
                                      bool           fResolveSymLinks /* = true */)
{
    return getOpenFileNames(strStartWith,
                            strFilters,
                            pParent,
                            strCaption,
                            pStrSelectedFilter,
                            fResolveSymLinks,
                            true /* fSingleFile */).value(0, "");
}

/* static */
QStringList QIFileDialog::getOpenFileNames(const QString &strStartWith,
                                           const QString &strFilters,
                                           QWidget       *pParent,
                                           const QString &strCaption,
                                           QString       *pStrSelectedFilter /* = 0 */,
                                           bool           fResolveSymLinks /* = true */,
                                           bool           fSingleFile /* = false */)
{
#ifdef VBOX_WS_MAC

    // WORKAROUND:
    // After 4.5 exec ignores the Qt::Sheet flag.
    // See "New Ways of Using Dialogs" in http://doc.trolltech.com/qq/QtQuarterly30.pdf why.
    // We want the old behavior for file-save dialog. Unfortunately there is a bug in Qt 4.5.x
    // which result in showing the native & the Qt dialog at the same time.
    QWidget *pRealParent = windowManager().realParentWindow(pParent);
    QFileDialog dlg(pRealParent);
    windowManager().registerNewParent(&dlg, pRealParent);
    dlg.setWindowTitle(strCaption);

    /* Some predictive algorithm which seems missed in native code. */
    QDir dir(strStartWith);
    while (!dir.isRoot() && !dir.exists())
        dir = QDir(QFileInfo(dir.absolutePath()).absolutePath());
    const QString strDirectory = dir.absolutePath();
    if (!strDirectory.isNull())
        dlg.setDirectory(strDirectory);
    if (strDirectory != strStartWith)
        dlg.selectFile(QFileInfo(strStartWith).absoluteFilePath());

    dlg.setNameFilter(strFilters);
    if (fSingleFile)
        dlg.setFileMode(QFileDialog::ExistingFile);
    else
        dlg.setFileMode(QFileDialog::ExistingFiles);
    if (pStrSelectedFilter)
        dlg.selectNameFilter(*pStrSelectedFilter);
    dlg.setOption(DontResolveSymlinks, !fResolveSymLinks);

    QEventLoop eventLoop;
    QObject::connect(&dlg, &QFileDialog::finished,
                     &eventLoop, &QEventLoop::quit);
    dlg.open();
    eventLoop.exec();

    return dlg.result() == QDialog::Accepted ? dlg.selectedFiles() : QStringList() << QString();

#else

    QFileDialog::Options o;
    if (!fResolveSymLinks)
        o |= QFileDialog::DontResolveSymlinks;

    if (fSingleFile)
        return QStringList() << QFileDialog::getOpenFileName(pParent, strCaption, strStartWith,
                                                             strFilters, pStrSelectedFilter, o);
    else
        return QFileDialog::getOpenFileNames(pParent, strCaption, strStartWith,
                                             strFilters, pStrSelectedFilter, o);

#endif
}

/* static */
QString QIFileDialog::getFirstExistingDir(const QString &strStartDir)
{
    QString strResult = QString();
    QDir dir(strStartDir);
    while (!dir.exists() && !dir.isRoot())
    {
        QFileInfo dirInfo(dir.absolutePath());
        if (dir == QDir(dirInfo.absolutePath()))
            break;
        dir = dirInfo.absolutePath();
    }
    if (dir.exists() && !dir.isRoot())
        strResult = dir.absolutePath();
    return strResult;
}
