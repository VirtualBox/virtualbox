/* $Id: UITaskCloudGetSettingsForm.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITaskCloudGetSettingsForm class implementation.
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

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UICommon.h"
#include "UICloudNetworkingStuff.h"
#include "UINotificationCenter.h"
#include "UITaskCloudGetSettingsForm.h"
#include "UIThreadPool.h"


/*********************************************************************************************************************************
*   Class UITaskCloudGetSettingsForm implementation.                                                                             *
*********************************************************************************************************************************/

UITaskCloudGetSettingsForm::UITaskCloudGetSettingsForm(const CCloudMachine &comCloudMachine)
    : UITask(Type_CloudGetSettingsForm)
    , m_comCloudMachine(comCloudMachine)
{
}

CForm UITaskCloudGetSettingsForm::result() const
{
    m_mutex.lock();
    const CForm comResult = m_comResult;
    m_mutex.unlock();
    return comResult;
}

QString UITaskCloudGetSettingsForm::errorInfo() const
{
    m_mutex.lock();
    QString strErrorInfo = m_strErrorInfo;
    m_mutex.unlock();
    return strErrorInfo;
}

void UITaskCloudGetSettingsForm::run()
{
    m_mutex.lock();
    cloudMachineSettingsForm(m_comCloudMachine, m_comResult, m_strErrorInfo);
    m_mutex.unlock();
}


/*********************************************************************************************************************************
*   Class UIReceiverCloudGetSettingsForm implementation.                                                                         *
*********************************************************************************************************************************/

UIReceiverCloudGetSettingsForm::UIReceiverCloudGetSettingsForm(QWidget *pParent)
    : QObject(pParent)
    , m_pParent(pParent)
{
    /* Connect receiver: */
    connect(uiCommon().threadPoolCloud(), &UIThreadPool::sigTaskComplete,
            this, &UIReceiverCloudGetSettingsForm::sltHandleTaskComplete);
}

void UIReceiverCloudGetSettingsForm::sltHandleTaskComplete(UITask *pTask)
{
    /* Skip unrelated tasks: */
    if (!pTask || pTask->type() != UITask::Type_CloudGetSettingsForm)
        return;

    /* Cast task to corresponding sub-class: */
    UITaskCloudGetSettingsForm *pSettingsTask = static_cast<UITaskCloudGetSettingsForm*>(pTask);

    /* Redirect to another listeners: */
    if (pSettingsTask->errorInfo().isNull())
        emit sigTaskComplete(pSettingsTask->result());
    else
    {
        UINotificationMessage::cannotAcquireCloudMachineSettings(pSettingsTask->errorInfo());
        emit sigTaskFailed(pSettingsTask->errorInfo());
    }
}
