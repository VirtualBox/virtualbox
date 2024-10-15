/* $Id: UIAdvancedSettingsDialogSpecific.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIAdvancedSettingsDialogSpecific class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_UIAdvancedSettingsDialogSpecific_h
#define FEQT_INCLUDED_SRC_settings_UIAdvancedSettingsDialogSpecific_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIAdvancedSettingsDialog.h"

/* COM includes: */
#include "CConsole.h"
#include "CMachine.h"
#include "CSession.h"

/* Forward declarations: */
class UIActionPool;

/** UIAdvancedSettingsDialog extension encapsulating all the specific functionality of the Global Preferences. */
class SHARED_LIBRARY_STUFF UIAdvancedSettingsDialogGlobal : public UIAdvancedSettingsDialog
{
    Q_OBJECT;

public:

    /** Constructs settings dialog passing @a pParent to the base-class.
      * @param  strCategory  Brings the name of category to be opened.
      * @param  strControl   Brings the name of control to be focused. */
    UIAdvancedSettingsDialogGlobal(QWidget *pParent,
                                   const QString &strCategory = QString(),
                                   const QString &strControl = QString());

protected:

    /** Loads the dialog data. */
    virtual bool load() RT_OVERRIDE;
    /** Saves the dialog data. */
    virtual void save() RT_OVERRIDE;

    /** Returns the dialog title. */
    virtual QString title() const RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();

    /** Returns whether page with certain @a iPageId is available. */
    bool isPageAvailable(int iPageId) const;
};


/** UIAdvancedSettingsDialog extension encapsulating all the specific functionality of the Machine Settings. */
class SHARED_LIBRARY_STUFF UIAdvancedSettingsDialogMachine : public UIAdvancedSettingsDialog
{
    Q_OBJECT;

public:

    /** Constructs settings dialog passing @a pParent to the base-class.
      * @param  uMachineId   Brings the machine ID.
      * @param  pActionPool  Brings the action pool instance.
      * @param  strCategory  Brings the name of category to be opened.
      * @param  strControl   Brings the name of control to be focused. */
    UIAdvancedSettingsDialogMachine(QWidget *pParent,
                                    const QUuid &uMachineId,
                                    UIActionPool *pActionPool,
                                    const QString &strCategory = QString(),
                                    const QString &strControl = QString());

    /** Update machine stuff.
      * @param  uMachineId   Brings the machine ID.
      * @param  strCategory  Brings the name of category to be opened.
      * @param  strControl   Brings the name of control to be focused. */
    void setNewMachineId(const QUuid &uMachineId,
                         const QString &strCategory = QString(),
                         const QString &strControl = QString());

protected:

    /** Loads the dialog data. */
    virtual bool load() RT_OVERRIDE;
    /** Saves the dialog data. */
    virtual void save() RT_OVERRIDE;

    /** Returns the dialog title. */
    virtual QString title() const RT_OVERRIDE;

    /** Verifies data integrity between certain @a pSettingsPage and other pages. */
    virtual void recorrelate(UISettingsPage *pSettingsPage) RT_OVERRIDE;

protected slots:

    /** Handles category change to @a cId. */
    virtual void sltCategoryChanged(int cId) RT_OVERRIDE;

    /** Handle serializartion finished. */
    virtual void sltHandleSerializationFinished() RT_OVERRIDE;

private slots:

    /** Handles session state change for machine with certain @a uMachineId to @a enmSessionState. */
    void sltSessionStateChanged(const QUuid &uMachineId, const KSessionState enmSessionState);
    /** Handles machine state change for machine with certain @a uMachineId to @a enmMachineState. */
    void sltMachineStateChanged(const QUuid &uMachineId, const KMachineState enmMachineState);
    /** Handles machine data change for machine with certain @a uMachineId. */
    void sltMachineDataChanged(const QUuid &uMachineId);
    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();

    /** Returns whether page with certain @a iPageId is available. */
    bool isPageAvailable(int iPageId) const;

    /** Recalculates configuration access level. */
    void updateConfigurationAccessLevel();

    /** Holds the machine ID. */
    QUuid         m_uMachineId;
    /** Holds the action-pool reference. */
    UIActionPool *m_pActionPool;

    /** Holds the session state. */
    KSessionState  m_enmSessionState;
    /** Holds the machine state. */
    KMachineState  m_enmMachineState;

    /** Holds the session reference. */
    CSession  m_session;
    /** Holds the machine reference. */
    CMachine  m_machine;
    /** Holds the console reference. */
    CConsole  m_console;
};


#endif /* !FEQT_INCLUDED_SRC_settings_UIAdvancedSettingsDialogSpecific_h */
