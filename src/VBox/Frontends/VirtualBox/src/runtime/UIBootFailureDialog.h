/* $Id: UIBootFailureDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIBootFailureDialog class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIBootFailureDialog_h
#define FEQT_INCLUDED_SRC_runtime_UIBootFailureDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIMainDialog.h"
#include "UIMedium.h"
#include "UIMediumDefs.h"


/* Forward declarations: */
class QCheckBox;
class QLabel;
class QVBoxLayout;
class QIDialogButtonBox;
class QIRichTextLabel;
class UIFilePathSelector;

/** QIDialog extension providing GUI with a dialog to select an existing medium. */
class UIBootFailureDialog : public QIMainDialog
{

    Q_OBJECT;

signals:

public:

    enum ReturnCode
    {
        ReturnCode_Close = 0,
        ReturnCode_Reset,
        ReturnCode_Max
    };

    UIBootFailureDialog(QWidget *pParent);
    ~UIBootFailureDialog();
    QString bootMediumPath() const;

protected:

    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

private slots:

    void sltCancel();
    void sltReset();
    void sltFileSelectorPathChanged(const QString &strPath);
    /** @name Event-handling stuff.
      * @{ */
        /** Handles translation event. */
        void sltRetranslateUI();
    /** @} */

private:

    QPixmap iconPixmap();
    /* Checks if selected iso exists and readable. Returns false if not. Returns true if nothing is selected. */
    bool checkISOImage() const;

    /** @name Prepare/cleanup cascade.
      * @{ */
        /** Configures all. */
            void configure();
            void prepareWidgets();
            void prepareConnections();
    /** @} */

    void          setTitle();
    QWidget              *m_pParent;

    QWidget              *m_pCentralWidget;
    QVBoxLayout          *m_pMainLayout;
    QIDialogButtonBox    *m_pButtonBox;
    QPushButton          *m_pCloseButton;
    QPushButton          *m_pResetButton;
    QIRichTextLabel      *m_pLabel;
    UIFilePathSelector   *m_pBootImageSelector;
    QLabel               *m_pBootImageLabel;
    QLabel               *m_pIconLabel;
    QCheckBox            *m_pSuppressDialogCheckBox;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIBootFailureDialog_h */
