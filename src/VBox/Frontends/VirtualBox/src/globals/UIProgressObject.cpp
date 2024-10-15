/* $Id: UIProgressObject.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIProgressObject class implementation.
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

/* GUI includes: */
#include "UIErrorString.h"
#include "UIProgressEventHandler.h"
#include "UIProgressObject.h"

/* COM includes: */
#include "CProgress.h"


UIProgressObject::UIProgressObject(CProgress &comProgress, QObject *pParent /* = 0 */)
    : QObject(pParent)
    , m_comProgress(comProgress)
    , m_fCancelable(false)
    , m_pEventHandler(0)
{
    prepare();
}

UIProgressObject::~UIProgressObject()
{
    cleanup();
}

void UIProgressObject::exec()
{
    /* Make sure progress hasn't aborted/finished already: */
    if (!m_comProgress.isOk() || m_comProgress.GetCompleted())
        return;

    /* We are creating a locally-scoped event-loop object,
     * but holding a pointer to it for a control needs: */
    QEventLoop eventLoop;
    m_pEventLoopExec = &eventLoop;

    /* Guard ourself for the case
     * we self-destroyed in our event-loop: */
    QPointer<UIProgressObject> guard = this;

    /* Start the blocking event-loop: */
    eventLoop.exec();

    /* Event-loop object unblocked,
     * Are we still valid? */
    if (guard.isNull())
        return;

    /* Cleanup the pointer finally: */
    m_pEventLoopExec = 0;
}

void UIProgressObject::cancel()
{
    /* Make sure progress hasn't aborted/finished already: */
    if (!m_comProgress.isOk() || m_comProgress.GetCompleted())
        return;

    /* Cancel progress first of all: */
    m_comProgress.Cancel();

    /* We are creating a locally-scoped event-loop object,
     * but holding a pointer to it for a control needs: */
    QEventLoop eventLoop;
    m_pEventLoopCancel = &eventLoop;

    /* Guard ourself for the case
     * we self-destroyed in our event-loop: */
    QPointer<UIProgressObject> guard = this;

    /* Start the blocking event-loop: */
    eventLoop.exec();

    /* Event-loop object unblocked,
     * Are we still valid? */
    if (guard.isNull())
        return;

    /* Cleanup the pointer finally: */
    m_pEventLoopCancel = 0;
}

void UIProgressObject::sltHandleProgressPercentageChange(const QUuid &, const int iPercent)
{
    /* Update cancelable value: */
    m_fCancelable = m_comProgress.GetCancelable();

    /* Notify listeners: */
    emit sigProgressChange(m_comProgress.GetOperationCount(),
                           m_comProgress.GetOperationDescription(),
                           m_comProgress.GetOperation(),
                           iPercent);
}

void UIProgressObject::sltHandleProgressTaskComplete(const QUuid &)
{
    /* Notify listeners about the operation progress error: */
    if (!m_comProgress.isOk() || m_comProgress.GetResultCode() != 0)
        emit sigProgressError(UIErrorString::formatErrorInfo(m_comProgress));

    /* Exit from the exec event-loop if there is any: */
    if (m_pEventLoopExec)
        m_pEventLoopExec->exit();
    /* Exit from the cancel event-loop if there is any: */
    if (m_pEventLoopCancel)
        m_pEventLoopCancel->exit();

    emit sigProgressComplete();
}

void UIProgressObject::prepare()
{
    /* Init cancelable value: */
    m_fCancelable = m_comProgress.GetCancelable();

    /* Create CProgress event handler: */
    m_pEventHandler = new UIProgressEventHandler(this, m_comProgress);
    if (m_pEventHandler)
    {
        connect(m_pEventHandler, &UIProgressEventHandler::sigProgressPercentageChange,
                this, &UIProgressObject::sltHandleProgressPercentageChange);
        connect(m_pEventHandler, &UIProgressEventHandler::sigProgressTaskComplete,
                this, &UIProgressObject::sltHandleProgressTaskComplete);
        connect(m_pEventHandler, &UIProgressEventHandler::sigHandlingFinished,
                this, &UIProgressObject::sigProgressEventHandlingFinished);
    }
}

void UIProgressObject::cleanup()
{
    /* Destroy CProgress event handler: */
    disconnect(m_pEventHandler, &UIProgressEventHandler::sigProgressPercentageChange,
               this, &UIProgressObject::sltHandleProgressPercentageChange);
    disconnect(m_pEventHandler, &UIProgressEventHandler::sigProgressTaskComplete,
               this, &UIProgressObject::sltHandleProgressTaskComplete);
    disconnect(m_pEventHandler, &UIProgressEventHandler::sigHandlingFinished,
               this, &UIProgressObject::sigProgressEventHandlingFinished);
    delete m_pEventHandler;
    m_pEventHandler = 0;
}
