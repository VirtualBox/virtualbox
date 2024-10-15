/* $Id: UIExecutionQueue.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIExecutionQueue class declaration.
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

#ifndef FEQT_INCLUDED_SRC_objects_UIExecutionQueue_h
#define FEQT_INCLUDED_SRC_objects_UIExecutionQueue_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QQueue>

/* GUI includes: */
#include "UILibraryDefs.h"

/** QObject subclass providing GUI with
  * interface for an execution step. */
class UIExecutionStep : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about step finished. */
    void sigStepFinished();

public:

    /** Constructs execution step. */
    UIExecutionStep();

    /** Executes the step. */
    virtual void exec() = 0;
};

/** QObject subclass providing GUI with
  * an object to process queue of execution steps. */
class UIExecutionQueue : public QObject
{
    Q_OBJECT;

signals:

    /** Starts the queue. */
    void sigStartQueue();

    /** Notifies about queue finished. */
    void sigQueueFinished();

public:

    /** Constructs execution queue passing @a pParent to the base-class. */
    UIExecutionQueue(QObject *pParent = 0);
    /** Destructs execution queue. */
    virtual ~UIExecutionQueue() RT_OVERRIDE RT_FINAL;

    /** Enqueues pStep into queue. */
    void enqueue(UIExecutionStep *pStep);

    /** Starts the queue. */
    void start() { emit sigStartQueue(); }

private slots:

    /** Starts subsequent step. */
    void sltStartsSubsequentStep();

private:

    /** Holds the execution step queue. */
    QQueue<UIExecutionStep*>  m_queue;
    /** Holds current step being executed. */
    UIExecutionStep          *m_pExecutedStep;
};

#endif /* !FEQT_INCLUDED_SRC_objects_UIExecutionQueue_h */
