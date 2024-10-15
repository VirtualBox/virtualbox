/* $Id: UIVMLogViewerDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMLogViewerDialog class declaration.
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

#ifndef FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerDialog_h
#define FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QString>
#include <QUuid>

/* GUI includes: */
#include "QIManagerDialog.h"

/* Forward declarations: */
class QAbstractButton;
class UIActionPool;
class UIVirtualMachineItem;


/** QIManagerDialogFactory extension used as a factory for Log Viewer dialog. */
class SHARED_LIBRARY_STUFF UIVMLogViewerDialogFactory : public QIManagerDialogFactory
{
public:

    /** Constructs Log Viewer factory acquiring additional arguments.
      * @param  pActionPool  Brings the action-pool reference.
      * @param  machineIDs   Brings the list of machine IDs. */
    UIVMLogViewerDialogFactory(UIActionPool *pActionPool = 0,
                               const QList<QUuid> &machineIDs = QList<QUuid>(),
                               const QString &strMachineName = QString());

protected:

    /** Creates derived @a pDialog instance.
      * @param  pCenterWidget  Brings the widget to center wrt. pCenterWidget. */
    virtual void create(QIManagerDialog *&pDialog, QWidget *pCenterWidget) RT_OVERRIDE;

    /** Holds the action-pool reference. */
    UIActionPool *m_pActionPool;
    /** Holds the list of machine IDs. */
    QList<QUuid>  m_machineIDs;
    QString       m_strMachineName;
};


/** QIManagerDialog extension providing GUI with the dialog displaying machine logs. */
class SHARED_LIBRARY_STUFF UIVMLogViewerDialog : public QIManagerDialog
{
    Q_OBJECT;

public:

    /** Constructs Log Viewer dialog.
      * @param  pCenterWidget  Brings the widget reference to center according to.
      * @param  pActionPool    Brings the action-pool reference.
      * @param  machineIDs     Brings the list of machine IDs. */
    UIVMLogViewerDialog(QWidget *pCenterWidget, UIActionPool *pActionPool,
                        const QList<QUuid> &machineIDs = QList<QUuid>(),
                        const QString &strMachineName = QString());
    ~UIVMLogViewerDialog();
    void setSelectedVMListItems(const QList<UIVirtualMachineItem*> &items);

protected:

    /** @name Event-handling stuff.
      * @{ */
        /** Handles translation event. */
        virtual bool event(QEvent *pEvent) RT_OVERRIDE;
    /** @} */

    /** @name Prepare/cleanup cascade.
     * @{ */
        /** Configures all. */
        virtual void configure() RT_OVERRIDE;
        /** Configures central-widget. */
        virtual void configureCentralWidget() RT_OVERRIDE;
        /** Configures button-box. */
        virtual void configureButtonBox() RT_OVERRIDE;
        /** Perform final preparations. */
        virtual void finalize() RT_OVERRIDE;
        /** Loads dialog geometry from extradata. */
        virtual void loadDialogGeometry();

        /** Saves dialog geometry into extradata. */
        virtual void saveDialogGeometry();
    /** @} */

    /** @name Functions related to geometry restoration.
     * @{ */
        /** Returns whether the window should be maximized when geometry being restored. */
        virtual bool shouldBeMaximized() const RT_OVERRIDE;
    /** @} */

private slots:

    /** Must be handles soemthing related to close @a shortcut. */
    void sltSetCloseButtonShortCut(QKeySequence shortcut);

    /** Handles button-box button click. */
    void sltHandleButtonBoxClick(QAbstractButton *pButton);
    void sltRetranslateUI();

private:

    /** Holds the action-pool reference. */
    UIActionPool *m_pActionPool;
    /** Holds the list of machine IDs. */
    QList<QUuid>  m_machineIDs;
    int           m_iGeometrySaveTimerId;
    QString       m_strMachineName;
};


#endif /* !FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerDialog_h */
