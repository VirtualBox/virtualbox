/* $Id: UIFDCreationDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFDCreationDialog class declaration.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_medium_UIFDCreationDialog_h
#define FEQT_INCLUDED_SRC_medium_UIFDCreationDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDialog>
#include <QUuid>

/* COM includes: */
#include "CMedium.h"
#include "CMediumFormat.h"

/* Forward declarations: */
class QCheckBox;
class QComboBox;
class QIDialogButtonBox;
class QLabel;
class UIFilePathSelector;

/* A QDialog extension to get necessary setting from the user for floppy disk creation. */
class SHARED_LIBRARY_STUFF UIFDCreationDialog : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs the floppy disc creation dialog passing @a pParent to the base-class.
      * @param  strDefaultFolder  Brings the default folder.
      * @param  strMachineName    Brings the machine name. */
    UIFDCreationDialog(QWidget *pParent,
                       const QString &strDefaultFolder,
                       const QString &strMachineName = QString());

    /** Return the medium ID. */
    QUuid mediumID() const;

    /** Creates and shows a dialog thru which user can create a new floppy disk a VISO using the file-open dialog.
      * @param  parent            Passes the parent of the dialog,
      * @param  strDefaultFolder  Passes the default folder,
      * @param  strMachineName    Passes the name of the machine,
      * returns the UUID of the newly created medium if successful, a null QUuid otherwise.*/
    static QUuid createFloppyDisk(QWidget *pParent, const QString &strDefaultFolder = QString(),
                                  const QString &strMachineName = QString());

public slots:

    /** Creates the floppy disc image, asynchronously. */
    virtual void accept() RT_OVERRIDE;

private slots:

    /** Handles signal about @a comMedium was created. */
    void sltHandleMediumCreated(const CMedium &comMedium);
    void sltPathChanged(const QString &strPath);
    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Floppy disc sizes. */
    enum FDSize
    {
        FDSize_2_88M,
        FDSize_1_44M,
        FDSize_1_2M,
        FDSize_720K,
        FDSize_360K
    };

    /** Prepares all. */
    void prepare();

    /** Returns default file-path. */
    QString getDefaultFilePath() const;
    /** Returns false if the file is already exists. */
    bool checkFilePath(const QString &strPath) const;

    /** Returns a list of formats for certain @a enmDeviceType. */
    static QVector<CMediumFormat> getFormatsForDeviceType(KDeviceType enmDeviceType);

    /** Holds the default folder. */
    QString  m_strDefaultFolder;
    /** Holds the machine name. */
    QString  m_strMachineName;

    /** Holds the path label instance. */
    QLabel             *m_pPathLabel;
    /** Holds the file path selector instance. */
    UIFilePathSelector *m_pFilePathSelector;
    /** Holds the size label instance. */
    QLabel             *m_pSizeLabel;
    /** Holds the size combo instance. */
    QComboBox          *m_pSizeCombo;
    /** Holds the format check-box instance. */
    QCheckBox          *m_pFormatCheckBox;
    /** holds the button-box instance. */
    QIDialogButtonBox  *m_pButtonBox;

    /** Holds the created medium ID. */
    QUuid  m_uMediumID;
};

#endif /* !FEQT_INCLUDED_SRC_medium_UIFDCreationDialog_h */
