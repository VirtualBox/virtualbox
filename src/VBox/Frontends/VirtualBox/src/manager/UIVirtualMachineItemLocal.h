/* $Id: UIVirtualMachineItemLocal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualMachineItemLocal class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIVirtualMachineItemLocal_h
#define FEQT_INCLUDED_SRC_manager_UIVirtualMachineItemLocal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDateTime>

/* GUI includes: */
#include "UIVirtualMachineItem.h"

/* COM includes: */
#include "CMachine.h"

/** UIVirtualMachineItem sub-class used as local Virtual Machine item interface. */
class UIVirtualMachineItemLocal : public UIVirtualMachineItem
{
    Q_OBJECT;

public:

    /** Constructs local VM item on the basis of taken @a comMachine. */
    UIVirtualMachineItemLocal(const CMachine &comMachine);
    /** Destructs local VM item. */
    virtual ~UIVirtualMachineItemLocal();

    /** @name Arguments.
      * @{ */
        /** Returns cached virtual machine object. */
        CMachine machine() const { return m_comMachine; }
    /** @} */

    /** @name Basic attributes.
      * @{ */
        /** Returns cached machine settings file name. */
        QString settingsFile() const { return m_strSettingsFile; }
        /** Returns cached machine group list. */
        const QStringList &groups() { return m_groups; }
    /** @} */

    /** @name Snapshot attributes.
      * @{ */
        /** Returns cached snapshot name. */
        QString snapshotName() const { return m_strSnapshotName; }
        /** Returns cached snapshot children count. */
        ULONG snapshotCount() const { return m_cSnaphot; }
    /** @} */

    /** @name State attributes.
      * @{ */
        /** Returns cached machine state. */
        KMachineState machineState() const { return m_enmMachineState; }
        /** Returns cached session state. */
        KSessionState sessionState() const { return m_enmSessionState; }
        /** Returns cached session state name. */
        QString sessionStateName() const { return m_strSessionStateName; }
    /** @} */

    /** @name Update stuff.
      * @{ */
        /** Recaches machine data. */
        virtual void recache() RT_OVERRIDE;
        /** Recaches machine item pixmap. */
        virtual void recachePixmap() RT_OVERRIDE;
    /** @} */

    /** @name Validation stuff.
      * @{ */
        /** Returns whether this item is editable. */
        virtual bool isItemEditable() const RT_OVERRIDE;
        /** Returns whether this item is removable. */
        virtual bool isItemRemovable() const RT_OVERRIDE;
        /** Returns whether this item is saved. */
        virtual bool isItemSaved() const RT_OVERRIDE;
        /** Returns whether this item is powered off. */
        virtual bool isItemPoweredOff() const RT_OVERRIDE;
        /** Returns whether this item is started. */
        virtual bool isItemStarted() const RT_OVERRIDE;
        /** Returns whether this item is running. */
        virtual bool isItemRunning() const RT_OVERRIDE;
        /** Returns whether this item is running headless. */
        virtual bool isItemRunningHeadless() const RT_OVERRIDE;
        /** Returns whether this item is paused. */
        virtual bool isItemPaused() const RT_OVERRIDE;
        /** Returns whether this item is stuck. */
        virtual bool isItemStuck() const RT_OVERRIDE;
        /** Returns whether this item can be switched to. */
        virtual bool isItemCanBeSwitchedTo() const RT_OVERRIDE;
    /** @} */

private slots:

    /** @name Event handling.
      * @{ */
        /** Handles translation event. */
        void sltRetranslateUI();
    /** @} */

private:

    /** @name Arguments.
      * @{ */
        /** Holds cached machine object reference. */
        CMachine  m_comMachine;
    /** @} */

    /** @name Basic attributes.
      * @{ */
        /** Holds cached machine settings file name. */
        QString      m_strSettingsFile;
        /** Holds cached machine group list. */
        QStringList  m_groups;
    /** @} */

    /** @name Snapshot attributes.
      * @{ */
        /** Holds cached snapshot name. */
        QString    m_strSnapshotName;
        /** Holds cached last state change date/time. */
        QDateTime  m_lastStateChange;
        /** Holds cached snapshot children count. */
        ULONG      m_cSnaphot;
    /** @} */

    /** @name State attributes.
      * @{ */
        /** Holds cached machine state. */
        KMachineState  m_enmMachineState;
        /** Holds cached session state. */
        KSessionState  m_enmSessionState;
        /** Holds cached session state name. */
        QString        m_strSessionStateName;
    /** @} */

    /** @name Console attributes.
      * @{ */
        /** Holds machine PID. */
        ULONG  m_pid;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIVirtualMachineItemLocal_h */
