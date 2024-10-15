/* $Id: UINotificationProgressTask.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINotificationProgressTask class implementation.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#include "UIErrorString.h"
#include "UINotificationObject.h"
#include "UINotificationProgressTask.h"


UINotificationProgressTask::UINotificationProgressTask(UINotificationProgress *pParent)
    : UIProgressTask(pParent)
    , m_pParent(pParent)
{
}

QString UINotificationProgressTask::errorMessage() const
{
    return m_strErrorMessage;
}

CProgress UINotificationProgressTask::createProgress()
{
    /* Call to sub-class to create progress-wrapper: */
    COMResult comResult;
    CProgress comProgress = m_pParent->createProgress(comResult);
    if (!comResult.isOk())
    {
        m_strErrorMessage = UIErrorString::formatErrorInfo(comResult);
        return CProgress();
    }
    /* Return progress-wrapper: */
    return comProgress;
}

void UINotificationProgressTask::handleProgressFinished(CProgress &comProgress)
{
    /* Handle progress-wrapper errors: */
    if (comProgress.isNotNull() && !comProgress.GetCanceled() && (!comProgress.isOk() || comProgress.GetResultCode() != 0))
        m_strErrorMessage = UIErrorString::formatErrorInfo(comProgress);
}
