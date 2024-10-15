/* $Id: UIProgressTaskReadCloudMachineList.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIProgressTaskReadCloudMachineList class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIProgressTaskReadCloudMachineList_h
#define FEQT_INCLUDED_SRC_manager_UIProgressTaskReadCloudMachineList_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UICloudEntityKey.h"
#include "UIProgressTask.h"

/* COM includes: */
#include "CCloudClient.h"
#include "CCloudMachine.h"

/** UIProgressTask extension performing read cloud machine list task. */
class UIProgressTaskReadCloudMachineList : public UIProgressTask
{
    Q_OBJECT;

public:

    /** Constructs read cloud machine list task passing @a pParent to the base-class.
      * @param  guiCloudProfileKey  Brings cloud profile description key.
      * @param  fWithRefresh        Brings whether cloud machine should be refreshed as well. */
    UIProgressTaskReadCloudMachineList(QObject *pParent, const UICloudEntityKey &guiCloudProfileKey, bool fWithRefresh);

    /** Returns cloud profile description key. */
    UICloudEntityKey cloudProfileKey() const;

    /** Returns resulting cloud machine-wrapper vector. */
    QVector<CCloudMachine> machines() const;

    /** Returns error message. */
    QString errorMessage() const;

protected:

    /** Creates and returns started progress-wrapper required to init UIProgressObject. */
    virtual CProgress createProgress() RT_OVERRIDE;
    /** Handles finished @a comProgress wrapper. */
    virtual void handleProgressFinished(CProgress &comProgress) RT_OVERRIDE;

private:

    /** Holds the cloud profile description key. */
    UICloudEntityKey  m_guiCloudProfileKey;
    /** Holds whether cloud machine should be refreshed as well. */
    bool              m_fWithRefresh;

    /** Holds the cloud client-wrapper. */
    CCloudClient            m_comCloudClient;
    /** Holds the resulting cloud machine-wrapper vector. */
    QVector<CCloudMachine>  m_machines;

    /** Holds the error message. */
    QString  m_strErrorMessage;
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIProgressTaskReadCloudMachineList_h */
