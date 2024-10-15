/* $Id: QIFileDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIFileDialog class declarations.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIFileDialog_h
#define FEQT_INCLUDED_SRC_extensions_QIFileDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QFileDialog>

/* GUI includes: */
#include "UILibraryDefs.h"

/** QFileDialog subclass simplifying access to it's static stuff. */
class SHARED_LIBRARY_STUFF QIFileDialog : public QFileDialog
{
    Q_OBJECT;

    /** Constructs our own file-dialog passing @a pParent and enmFlags to the base-class.
      * Doesn't mean to be used directly, cause this subclass is a bunch of statics. */
    QIFileDialog(QWidget *pParent, Qt::WindowFlags enmFlags);

public:

    /** Returns an existing directory selected by the user.
      * @param  strDir            Brings the dir to start from.
      * @param  pParent           Brings the parent.
      * @param  strCaption        Brings the dialog caption.
      * @param  fDirOnly          Brings whether dialog should show dirs only.
      * @param  fResolveSymLinks  Brings whether dialog should resolve sym-links. */
    static QString getExistingDirectory(const QString &strDir, QWidget *pParent,
                                        const QString &strCaption = QString(),
                                        bool fDirOnly = true,
                                        bool fResolveSymLinks = true);

    /** Returns a file name selected by the user. The file does not have to exist.
      * @param  strStartWith        Brings the full file path to start from.
      * @param  strFilters          Brings the filters.
      * @param  pParent             Brings the parent.
      * @param  strCaption          Brings the dialog caption.
      * @param  pStrSelectedFilter  Brings the selected filter.
      * @param  fResolveSymLinks    Brings whether dialog should resolve sym-links.
      * @param  fConfirmOverwrite   Brings whether dialog should confirm overwrite. */
    static QString getSaveFileName(const QString &strStartWith, const QString &strFilters, QWidget *pParent,
                                   const QString &strCaption, QString *pStrSelectedFilter = 0,
                                   bool fResolveSymLinks = true, bool fConfirmOverwrite = false);

    /** Returns an existing file selected by the user. If the user presses Cancel, it returns a null string.
      * @param  strStartWith        Brings the full file path to start from.
      * @param  strFilters          Brings the filters.
      * @param  pParent             Brings the parent.
      * @param  strCaption          Brings the dialog caption.
      * @param  pStrSelectedFilter  Brings the selected filter.
      * @param  fResolveSymLinks    Brings whether dialog should resolve sym-links. */
    static QString getOpenFileName(const QString &strStartWith, const QString &strFilters, QWidget *pParent,
                                   const QString &strCaption, QString *pStrSelectedFilter = 0,
                                   bool fResolveSymLinks = true);

    /** Returns one or more existing files selected by the user.
      * @param  strStartWith        Brings the full file path to start from.
      * @param  strFilters          Brings the filters.
      * @param  pParent             Brings the parent.
      * @param  strCaption          Brings the dialog caption.
      * @param  pStrSelectedFilter  Brings the selected filter.
      * @param  fResolveSymLinks    Brings whether dialog should resolve sym-links.
      * @param  fSingleFile         Brings whether dialog should allow chosing single file only. */
    static QStringList getOpenFileNames(const QString &strStartWith, const QString &strFilters, QWidget *pParent,
                                        const QString &strCaption, QString *pStrSelectedFilter = 0,
                                        bool fResolveSymLinks = true,
                                        bool fSingleFile = false);

    /** Search for the first directory that exists starting from the
      * passed one @a strStartDir and going up through its parents. */
    static QString getFirstExistingDir(const QString &strStartDir);
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIFileDialog_h */
