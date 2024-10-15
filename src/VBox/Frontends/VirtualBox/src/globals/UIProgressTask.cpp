/* $Id: UIProgressTask.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIProgressTask class implementation.
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
#include <QTimer>

/* GUI includes: */
#include "UIProgressTask.h"


UIProgressTask::UIProgressTask(QObject *pParent)
    : QObject(pParent)
    , m_pTimer(0)
{
    prepare();
}

UIProgressTask::~UIProgressTask()
{
    cleanup();
}

bool UIProgressTask::isScheduled() const
{
    AssertPtrReturn(m_pTimer, false);
    return m_pTimer->isActive();
}

bool UIProgressTask::isRunning() const
{
    return m_pProgressObject;
}

bool UIProgressTask::isCancelable() const
{
    return m_pProgressObject ? m_pProgressObject->isCancelable() : false;
}

void UIProgressTask::schedule(int iMsec)
{
    AssertPtrReturnVoid(m_pTimer);
    m_pTimer->setInterval(iMsec);
    m_pTimer->start();
}

void UIProgressTask::start()
{
    /* Ignore request if already running: */
    if (isRunning())
        return;

    /* Call for a virtual stuff to create progress-wrapper itself: */
    m_comProgress = createProgress();

    /* Make sure progress valid: */
    if (   m_comProgress.isNull()
        || m_comProgress.GetCompleted())
    {
        /* Notify external listeners: */
        emit sigProgressStarted();
        sltHandleProgressEventHandlingFinished();
    }
    else
    {
        /* Prepare progress-object: */
        m_pProgressObject = new UIProgressObject(m_comProgress, this);
        if (m_pProgressObject)
        {
            /* Setup connections: */
            connect(m_pProgressObject.data(), &UIProgressObject::sigProgressChange,
                    this, &UIProgressTask::sltHandleProgressChange);
            connect(m_pProgressObject.data(), &UIProgressObject::sigProgressEventHandlingFinished,
                    this, &UIProgressTask::sltHandleProgressEventHandlingFinished);

            /* Notify external listeners: */
            emit sigProgressStarted();
            if (m_comProgress.GetCompleted())
                sltHandleProgressEventHandlingFinished();
        }
    }
}

void UIProgressTask::cancel()
{
    if (m_pProgressObject)
    {
        m_pProgressObject->cancel();
        /* Notify external listeners: */
        emit sigProgressCanceled();
    }
}

void UIProgressTask::sltHandleProgressChange(ulong /*uOperations*/, QString /*strOperation*/,
                                             ulong /*uOperation*/, ulong uPercent)
{
    /* Notify external listeners: */
    emit sigProgressChange(uPercent);
}

void UIProgressTask::sltHandleProgressEventHandlingFinished()
{
    /* Call for a virtual stuff to let sub-class handle result: */
    handleProgressFinished(m_comProgress);

    /* Cleanup progress-object and progress-wrapper: */
    delete m_pProgressObject;
    m_comProgress = CProgress();

    /* Notify external listeners: */
    emit sigProgressFinished();
}

void UIProgressTask::prepare()
{
    /* Prepare schedule-timer: */
    m_pTimer = new QTimer(this);
    if (m_pTimer)
    {
        m_pTimer->setSingleShot(true);
        connect(m_pTimer, &QTimer::timeout,
                this, &UIProgressTask::start);
    }
}

void UIProgressTask::cleanup()
{
    /* Cleanup progress-object and progress-wrapper: */
    delete m_pProgressObject;
    m_comProgress = CProgress();

    /* Cleanup schedule-timer: */
    delete m_pTimer;
    m_pTimer = 0;
}
