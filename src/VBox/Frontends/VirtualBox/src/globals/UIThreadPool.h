/* $Id: UIThreadPool.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIThreadPool class declaration.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIThreadPool_h
#define FEQT_INCLUDED_SRC_globals_UIThreadPool_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QVector>
#include <QWaitCondition>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class UITask;
class UIThreadWorker;

/** QObject extension used as worker-thread pool.
  * Schedules COM-related GUI tasks to multiple worker-threads. */
class SHARED_LIBRARY_STUFF UIThreadPool : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about @a pTask complete. */
    void sigTaskComplete(UITask *pTask);

public:

    /** Constructs worker-thread pool.
      * @param  cMaxWorkers           Brings the maximum amount of worker-threads.
      * @param  cMsWorkerIdleTimeout  Brings the maximum amount of time (in ms) which
      *                               pool will wait for the worker-thread on cleanup. */
    UIThreadPool(ulong cMaxWorkers = 3, ulong cMsWorkerIdleTimeout = 5000);
    /** Destructs worker-thread pool. */
    virtual ~UIThreadPool() RT_OVERRIDE;

    /** Returns whether the 'termination sequence' is started. */
    bool isTerminating() const;
    /** Defines that the 'termination sequence' is started. */
    void setTerminating();

    /** Enqueues @a pTask into the task-queue. */
    void enqueueTask(UITask *pTask);
    /** Returns dequeued top-most task from the task-queue. */
    UITask *dequeueTask(UIThreadWorker *pWorker);

private slots:

    /** Handles @a pTask complete signal. */
    void sltHandleTaskComplete(UITask *pTask);

    /** Handles @a pWorker finished signal. */
    void sltHandleWorkerFinished(UIThreadWorker *pWorker);

private:

    /** @name Worker-thread stuff.
     * @{ */
        /** Holds the maximum amount of time (in ms) which
          * pool will wait for the worker-thread on cleanup. */
        const ulong               m_cMsIdleTimeout;
        /** Holds the vector of worker-threads. */
        QVector<UIThreadWorker*>  m_workers;
        /** Holds the number of worker-threads.
          * @remarks We cannot use the vector size since it may contain 0 pointers. */
        int                       m_cWorkers;
        /** Holds the number of idle worker-threads. */
        int                       m_cIdleWorkers;
        /** Holds whether the 'termination sequence' is started
          * and all worker-threads should terminate ASAP. */
        bool                      m_fTerminating;
    /** @} */

    /** @name Task stuff
     * @{ */
        /** Holds the queue of pending tasks. */
        QQueue<UITask*>  m_pendingTasks;
        /** Holds the set of executing tasks. */
        QSet<UITask*>    m_executingTasks;
        /** Holds the condition variable that gets signalled when
          * queuing a new task and there are idle worker threads around.
          * @remarks Idle threads sits in dequeueTask waiting for this.
          *          Thus on thermination, setTerminating() will send a
          *          broadcast signal to wake up all workers (after
          *          setting m_fTerminating of course). */
        QWaitCondition   m_taskCondition;
    /** @} */

    /** Holds the guard mutex object protecting
      * all the inter-thread variables. */
    mutable QMutex  m_everythingLocker;
};

#endif /* !FEQT_INCLUDED_SRC_globals_UIThreadPool_h */
