/* $Id: UIExecutionQueue.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIExecutionQueue class implementation.
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
#include "UIExecutionQueue.h"


/*********************************************************************************************************************************
*   Class UIExecutionStep implementation.                                                                                        *
*********************************************************************************************************************************/

UIExecutionStep::UIExecutionStep()
{
}


/*********************************************************************************************************************************
*   Class UIExecutionQueue implementation.                                                                                       *
*********************************************************************************************************************************/

UIExecutionQueue::UIExecutionQueue(QObject *pParent /* = 0 */)
    : QObject(pParent)
    , m_pExecutedStep(0)
{
    /* Listen for the queue start signal: */
    connect(this, &UIExecutionQueue::sigStartQueue,
            this, &UIExecutionQueue::sltStartsSubsequentStep,
            Qt::QueuedConnection);
}

UIExecutionQueue::~UIExecutionQueue()
{
    /* Cleanup current step: */
    delete m_pExecutedStep;
    m_pExecutedStep = 0;

    /* Dequeue steps one-by-one: */
    while (!m_queue.isEmpty())
        delete m_queue.dequeue();
}

void UIExecutionQueue::enqueue(UIExecutionStep *pUpdateStep)
{
    m_queue.enqueue(pUpdateStep);
}

void UIExecutionQueue::sltStartsSubsequentStep()
{
    /* Cleanup current step: */
    delete m_pExecutedStep;
    m_pExecutedStep = 0;

    /* If queue is empty, we are finished: */
    if (m_queue.isEmpty())
        emit sigQueueFinished();
    else
    {
        /* Otherwise dequeue first step and start it: */
        m_pExecutedStep = m_queue.dequeue();
        connect(m_pExecutedStep, &UIExecutionStep::sigStepFinished,
                this, &UIExecutionQueue::sltStartsSubsequentStep,
                Qt::QueuedConnection);
        m_pExecutedStep->exec();
    }
}
