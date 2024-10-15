/* $Id: UIProgressTaskReadCloudMachineList.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIProgressTaskReadCloudMachineList class implementation.
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

/* GUI includes: */
#include "UICloudNetworkingStuff.h"
#include "UIErrorString.h"
#include "UIProgressTaskReadCloudMachineList.h"


UIProgressTaskReadCloudMachineList::UIProgressTaskReadCloudMachineList(QObject *pParent,
                                                                       const UICloudEntityKey &guiCloudProfileKey,
                                                                       bool fWithRefresh)
    : UIProgressTask(pParent)
    , m_guiCloudProfileKey(guiCloudProfileKey)
    , m_fWithRefresh(fWithRefresh)
{
}

UICloudEntityKey UIProgressTaskReadCloudMachineList::cloudProfileKey() const
{
    return m_guiCloudProfileKey;
}

QVector<CCloudMachine> UIProgressTaskReadCloudMachineList::machines() const
{
    return m_machines;
}

QString UIProgressTaskReadCloudMachineList::errorMessage() const
{
    return m_strErrorMessage;
}

CProgress UIProgressTaskReadCloudMachineList::createProgress()
{
    /* Create cloud client: */
    m_comCloudClient = cloudClientByName(m_guiCloudProfileKey.m_strProviderShortName,
                                         m_guiCloudProfileKey.m_strProfileName,
                                         m_strErrorMessage);
    if (m_comCloudClient.isNull())
        return CProgress();

    /* Initialize progress-wrapper: */
    CProgress comProgress = m_fWithRefresh
                          ? m_comCloudClient.ReadCloudMachineList()
                          : m_comCloudClient.ReadCloudMachineStubList();
    if (!m_comCloudClient.isOk())
    {
        m_strErrorMessage = UIErrorString::formatErrorInfo(m_comCloudClient);
        return CProgress();
    }

    /* Return progress-wrapper: */
    return comProgress;
}

void UIProgressTaskReadCloudMachineList::handleProgressFinished(CProgress &comProgress)
{
    /* Check if we already have error-mesage: */
    if (!m_strErrorMessage.isEmpty())
        return;

    /* Handle progress-wrapper errors: */
    if (comProgress.isNotNull() && !comProgress.GetCanceled() && (!comProgress.isOk() || comProgress.GetResultCode() != 0))
    {
        m_strErrorMessage = UIErrorString::formatErrorInfo(comProgress);
        return;
    }

    /* Fill the result: */
    m_machines = m_fWithRefresh
               ? m_comCloudClient.GetCloudMachineList()
               : m_comCloudClient.GetCloudMachineStubList();
    if (!m_comCloudClient.isOk())
        m_strErrorMessage = UIErrorString::formatErrorInfo(m_comCloudClient);
}
