/* $Id: UIVirtualMachineItemCloud.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualMachineItemCloud class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIVirtualMachineItemCloud_h
#define FEQT_INCLUDED_SRC_manager_UIVirtualMachineItemCloud_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIVirtualMachineItem.h"

/* COM includes: */
#include "CCloudMachine.h"

/* Forward declarations: */
class UIProgressTask;

/** UIVirtualMachineItem sub-class used as cloud Virtual Machine item interface. */
class UIVirtualMachineItemCloud : public UIVirtualMachineItem
{
    Q_OBJECT;

signals:

    /** Notifies listeners about refresh started. */
    void sigRefreshStarted();
    /** Notifies listeners about refresh finished. */
    void sigRefreshFinished();

public:

    /** Constructs fake cloud VM item of certain @a enmState. */
    UIVirtualMachineItemCloud(UIFakeCloudVirtualMachineItemState enmState);
    /** Constructs real cloud VM item on the basis of taken @a comCloudMachine. */
    UIVirtualMachineItemCloud(const CCloudMachine &comCloudMachine);
    /** Destructs cloud VM item. */
    virtual ~UIVirtualMachineItemCloud() RT_OVERRIDE;

    /** @name Arguments.
      * @{ */
        /** Returns cached cloud machine object. */
        CCloudMachine machine() const { return m_comCloudMachine; }
    /** @} */

    /** @name Data attributes.
      * @{ */
        /** Returns cached machine state. */
        KCloudMachineState machineState() const { return m_enmMachineState; }
        QUuid machineId() const { return m_comCloudMachine.isOk() ? m_comCloudMachine.GetId() : QUuid(); }

        /** Defines fake cloud item @a enmState. */
        void setFakeCloudItemState(UIFakeCloudVirtualMachineItemState enmState);
        /** Returns fake cloud item state. */
        UIFakeCloudVirtualMachineItemState fakeCloudItemState() const { return m_enmFakeCloudItemState; }

        /** Defines fake cloud item @a strErrorMessage. */
        void setFakeCloudItemErrorMessage(const QString &strErrorMessage);
        /** Returns fake cloud item error message. */
        QString fakeCloudItemErrorMessage() const { return m_strFakeCloudItemErrorMessage; }

        /** Defines whether update is @a fRequired by global reason. */
        void setUpdateRequiredByGlobalReason(bool fRequired);
        /** Defines whether update is @a fRequired by local reason. */
        void setUpdateRequiredByLocalReason(bool fRequired);
        /** Updates cloud VM info async way, @a fDelayed if requested or instantly otherwise. */
        void updateInfoAsync(bool fDelayed);
        /** Makes sure async info update is finished.
          * @note  This method creates own event-loop to avoid blocking calling thread event processing,
          *        so it's safe to call it from the GUI thread, ofc the method itself will be blocked. */
        void waitForAsyncInfoUpdateFinished();
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

    /** Handles signal about cloud VM info refresh progress is done. */
    void sltHandleRefreshCloudMachineInfoDone();

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Cleanups all. */
        void cleanup();
    /** @} */

    /** @name Arguments.
      * @{ */
        /** Holds cached cloud machine object. */
        CCloudMachine  m_comCloudMachine;
    /** @} */

    /** @name Data attributes.
      * @{ */
        /** Holds cached machine state. */
        KCloudMachineState  m_enmMachineState;

        /** Holds fake cloud item state. */
        UIFakeCloudVirtualMachineItemState  m_enmFakeCloudItemState;
        /** Holds fake cloud item error message. */
        QString                             m_strFakeCloudItemErrorMessage;

        /** Holds whether update is required by global reason. */
        bool            m_fUpdateRequiredByGlobalReason;
        /** Holds whether update is required by local reason. */
        bool            m_fUpdateRequiredByLocalReason;
        /** Holds the refresh progress-task instance. */
        UIProgressTask *m_pProgressTaskRefresh;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIVirtualMachineItemCloud_h */
