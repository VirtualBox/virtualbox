/* $Id: UICloudMachineSettingsDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudMachineSettingsDialog class declaration.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_cloud_machinesettings_UICloudMachineSettingsDialog_h
#define FEQT_INCLUDED_SRC_cloud_machinesettings_UICloudMachineSettingsDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QPointer>
#include <QWidget>

/* GUI includes: */
#include "UICloudMachineSettingsDialogPage.h"

/* COM includes: */
#include "CCloudMachine.h"
#include "CForm.h"

/* Forward declarations: */
class QIDialogButtonBox;
class UINotificationCenter;

/** Cloud machine settings window. */
class UICloudMachineSettingsDialog : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about dialog should be closed. */
    void sigClose();

public:

    /** Constructs @a comCloudMachine settings dialog passing @a pParent to the base-class. */
    UICloudMachineSettingsDialog(QWidget *pParent, const CCloudMachine &comCloudMachine);
    /** Destructs cloud machine settings dialog. */
    virtual ~UICloudMachineSettingsDialog() /* override final */;

    /** Returns local notification-center reference. */
    UINotificationCenter *notificationCenter() const { return m_pNotificationCenter; }

    /** Defines @a comCloudMachine */
    void setCloudMachine(const CCloudMachine &comCloudMachine);

protected:

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles first show @a pEvent. */
    virtual void polishEvent(QShowEvent*);
    /** Handles close @a pEvent. */
    virtual void closeEvent(QCloseEvent *pEvent) RT_OVERRIDE RT_FINAL;

private slots:

    /** Sets Ok button to be @a fEnabled. */
    void setOkButtonEnabled(bool fEnabled);
    /** Inits the dialog. */
    void init() { load(); }
    /** Accepts the dialog. */
    void accept() { save(); }
    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();

    /** Loads the data. */
    void load();
    /** Saves the data and closes the dialog. */
    void save();

    /** Holds whether dialog is polished. */
    bool  m_fPolished;
    /** Holds whether the dialod can really be closed. */
    bool  m_fClosable;
    /** Holds whether the dialod had emitted signal to be closed. */
    bool  m_fClosed;

    /** Holds the cloud machine object reference. */
    CCloudMachine  m_comCloudMachine;
    /** Holds the cloud machine settings form object reference. */
    CForm          m_comForm;
    /** Holds the cloud machine name. */
    QString        m_strName;

    /** Holds the cloud machine settings dialog page instance. */
    UISafePointerCloudMachineSettingsDialogPage  m_pPage;
    /** Holds the dialog button-box instance. */
    QIDialogButtonBox                           *m_pButtonBox;

    /** Holds the local notification-center instance. */
    UINotificationCenter *m_pNotificationCenter;
};

/** Safe pointer to cloud machine settings dialog. */
typedef QPointer<UICloudMachineSettingsDialog> UISafePointerCloudMachineSettingsDialog;

#endif /* !FEQT_INCLUDED_SRC_cloud_machinesettings_UICloudMachineSettingsDialog_h */
