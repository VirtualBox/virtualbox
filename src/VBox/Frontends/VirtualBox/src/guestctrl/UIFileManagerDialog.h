/* $Id: UIFileManagerDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFileManagerDialog class declaration.
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

#ifndef FEQT_INCLUDED_SRC_guestctrl_UIFileManagerDialog_h
#define FEQT_INCLUDED_SRC_guestctrl_UIFileManagerDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QString>
#include <QUuid>

/* GUI includes: */
#include "QIManagerDialog.h"


/* Forward declarations: */
class QDialogButtonBox;
class QVBoxLayout;
class UIActionPool;
class UIFileManagerDialog;


/** QIManagerDialogFactory extension used as a factory for the file manager dialog. */
class UIFileManagerDialogFactory : public QIManagerDialogFactory
{
public:

    UIFileManagerDialogFactory(UIActionPool *pActionPool, const QUuid &uMachineId, const QString &strMachineName);
    UIFileManagerDialogFactory();

protected:

    /** Creates derived @a pDialog instance.
      * @param  pCenterWidget  Passes the widget to center wrt. pCenterWidget. */
    virtual void create(QIManagerDialog *&pDialog, QWidget *pCenterWidget) RT_OVERRIDE;

    UIActionPool *m_pActionPool;
    QUuid      m_uMachineId;
    QString    m_strMachineName;
};

/** QIManagerDialog extension providing GUI with the dialog displaying file manager releated logs. */
class UIFileManagerDialog : public QIManagerDialog
{
    Q_OBJECT;

public:

    /** Constructs File Manager dialog.
      * @param  pCenterWidget  Passes the widget reference to center according to.
      * @param  pActionPool    Passes the action-pool reference.
      * @param  uMachineId     Passes the machine id. */
    UIFileManagerDialog(QWidget *pCenterWidget, UIActionPool *pActionPool, const QUuid &uMachineId, const QString &strMachineName);
    ~UIFileManagerDialog();

protected:

    /** @name Prepare/cleanup cascade.
     * @{ */
        /** Configures all. */
        virtual void configure() RT_OVERRIDE;
        /** Configures central-widget. */
        virtual void configureCentralWidget() RT_OVERRIDE;
        /** Perform final preparations. */
        virtual void finalize() RT_OVERRIDE;
        /** Loads dialog setting from extradata. */
        virtual void loadSettings() RT_OVERRIDE;

        /** Saves dialog setting into extradata. */
        virtual void saveSettings() RT_OVERRIDE;
    /** @} */

    /** @name Functions related to geometry restoration.
     * @{ */
        /** Returns whether the window should be maximized when geometry being restored. */
        virtual bool shouldBeMaximized() const RT_OVERRIDE;
    /** @} */

private slots:

    void sltSetCloseButtonShortCut(QKeySequence shortcut);
    /** @name Event-handling stuff.
      * @{ */
        /** Handles translation event. */
        void sltRetranslateUI();
    /** @} */

private:

    void manageEscapeShortCut();
    UIActionPool *m_pActionPool;
    QUuid    m_uMachineId;
    QString  m_strMachineName;
};


#endif /* !FEQT_INCLUDED_SRC_guestctrl_UIFileManagerDialog_h */
