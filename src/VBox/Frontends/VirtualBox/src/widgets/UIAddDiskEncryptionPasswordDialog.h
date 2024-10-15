/* $Id: UIAddDiskEncryptionPasswordDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIAddDiskEncryptionPasswordDialog class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIAddDiskEncryptionPasswordDialog_h
#define FEQT_INCLUDED_SRC_widgets_UIAddDiskEncryptionPasswordDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDialog>
#include <QMap>
#include <QMultiMap>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QLabel;
class QIDialogButtonBox;
class UIEncryptionDataTable;

/* Type definitions: */
typedef QMultiMap<QString, QUuid> EncryptedMediumMap;
typedef QMap<QString, QString> EncryptionPasswordMap;
typedef QMap<QString, bool> EncryptionPasswordStatusMap;

/** QDialog subclass used to
  * allow the user to enter disk encryption passwords for particular password IDs. */
class SHARED_LIBRARY_STUFF UIAddDiskEncryptionPasswordDialog : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs dialog passing @a pParent to the base-class.
      * @param  strMachineName    Brings the name of the machine we show this dialog for.
      * @param  encryptedMedia  Brings the lists of medium ids (values) encrypted with passwords with ids (keys). */
    UIAddDiskEncryptionPasswordDialog(QWidget *pParent, const QString &strMachineName, const EncryptedMediumMap &encryptedMedia);

    /** Returns the shallow copy of the encryption password map
      * acquired from the UIEncryptionDataTable instance. */
    EncryptionPasswordMap encryptionPasswords() const;

private slots:

    /** Translation routine. */
    void sltRetranslateUI();

    /** Handles editor's Enter/Return key triggering. */
    void sltEditorEnterKeyTriggered() { accept(); }

    /** Performs passwords validation.
      * If all passwords are valid,
      * this slot calls to base-class. */
    void accept() RT_OVERRIDE;

private:

    /** Prepares all. */
    void prepare();

    /** Returns whether passed @a strPassword is valid for medium with passed @a uMediumId. */
    static bool isPasswordValid(const QUuid &uMediumId, const QString strPassword);

    /** Holds the name of the machine we show this dialog for. */
    const QString  m_strMachineName;

    /** Holds the encrypted medium map reference. */
    const EncryptedMediumMap &m_encryptedMedia;

    /** Holds the description label instance. */
    QLabel                *m_pLabelDescription;
    /** Holds the encryption-data table instance. */
    UIEncryptionDataTable *m_pTableEncryptionData;
    /** Holds the button-box instance. */
    QIDialogButtonBox     *m_pButtonBox;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIAddDiskEncryptionPasswordDialog_h */
