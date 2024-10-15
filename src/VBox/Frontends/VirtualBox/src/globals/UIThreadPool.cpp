/* $Id: UIThreadPool.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIThreadPool class implementation.
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

/* Qt includes: */
#include <QThread>

/* GUI includes: */
#include "COMDefs.h"
#include "UIDefs.h"
#include "UITask.h"
#include "UIThreadPool.h"

/* Other VBox includes: */
#include <iprt/assert.h>


/** QThread extension used as worker-thread.
  * Capable of executing COM-related tasks. */
class UIThreadWorker : public QThread
{
    Q_OBJECT;

signals:

    /** Notifies listeners about @a pWorker finished. */
    void sigFinished(UIThreadWorker *pWorker);

public:

    /** Constructs worker-thread for parent worker-thread @a pPool.
      * @param  iIndex  Brings worker-thread index within the worker-thread pool registry. */
    UIThreadWorker(UIThreadPool *pPool, int iIndex);

    /** Returns worker-thread index within the worker-thread pool registry. */
    int index() const { return m_iIndex; }

    /** Disables sigFinished signal, for optimizing worker-thread pool termination. */
    void setNoFinishedSignal() { m_fNoFinishedSignal = true; }

private:

    /** Contains the worker-thread body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the worker-thread pool reference. */
    UIThreadPool *m_pPool;

    /** Holds the worker-thread index within the worker-thread pool registry. */
    int  m_iIndex;

    /** Holds whether sigFinished signal should be emitted or not. */
    bool  m_fNoFinishedSignal;
};


/*********************************************************************************************************************************
*   Class UIThreadPool implementation.                                                                                           *
*********************************************************************************************************************************/

UIThreadPool::UIThreadPool(ulong cMaxWorkers /* = 3 */, ulong cMsWorkerIdleTimeout /* = 5000 */)
    : m_cMsIdleTimeout(cMsWorkerIdleTimeout)
    , m_workers(cMaxWorkers)
    , m_cWorkers(0)
    , m_cIdleWorkers(0)
    , m_fTerminating(false)
{
}

UIThreadPool::~UIThreadPool()
{
    /* Set termination status: */
    setTerminating();

    /* Lock initially: */
    m_everythingLocker.lock();

    /* Cleanup all the workers: */
    for (int idxWorker = 0; idxWorker < m_workers.size(); ++idxWorker)
    {
        /* Acquire the worker: */
        UIThreadWorker *pWorker = m_workers.at(idxWorker);
        /* Remove it from the registry: */
        m_workers[idxWorker] = 0;

        /* Clean up the worker, if there was one: */
        if (pWorker)
        {
            /* Decrease the number of workers: */
            --m_cWorkers;
            /* Unlock temporary to let the worker finish: */
            m_everythingLocker.unlock();
            /* Wait for the worker to finish: */
            pWorker->wait();
            /* Lock again: */
            m_everythingLocker.lock();
            /* Delete the worker finally: */
            delete pWorker;
        }
    }

    /* Cleanup all the tasks: */
    qDeleteAll(m_pendingTasks);
    qDeleteAll(m_executingTasks);
    m_pendingTasks.clear();
    m_executingTasks.clear();

    /* Unlock finally: */
    m_everythingLocker.unlock();
}

bool UIThreadPool::isTerminating() const
{
    /* Lock initially: */
    m_everythingLocker.lock();

    /* Acquire termination-flag: */
    bool fTerminating = m_fTerminating;

    /* Unlock finally: */
    m_everythingLocker.unlock();

    /* Return termination-flag: */
    return fTerminating;
}

void UIThreadPool::setTerminating()
{
    /* Lock initially: */
    m_everythingLocker.lock();

    /* Assign termination-flag: */
    m_fTerminating = true;

    /* Tell all threads to NOT queue any termination signals: */
    for (int idxWorker = 0; idxWorker < m_workers.size(); ++idxWorker)
    {
        UIThreadWorker *pWorker = m_workers.at(idxWorker);
        if (pWorker)
            pWorker->setNoFinishedSignal();
    }

    /* Wake up all idle worker threads: */
    m_taskCondition.wakeAll();

    /* Unlock finally: */
    m_everythingLocker.unlock();
}

void UIThreadPool::enqueueTask(UITask *pTask)
{
    /* Do nothing if terminating: */
    AssertReturnVoid(!isTerminating());

    /* Prepare task: */
    connect(pTask, &UITask::sigComplete,
            this, &UIThreadPool::sltHandleTaskComplete, Qt::QueuedConnection);

    /* Lock initially: */
    m_everythingLocker.lock();

    /* Put the task into the queue: */
    m_pendingTasks.enqueue(pTask);

    /* Wake up an idle worker if we got one: */
    if (m_cIdleWorkers > 0)
    {
        m_taskCondition.wakeOne();
    }
    /* No idle worker threads, should we create a new one? */
    else if (m_cWorkers < m_workers.size())
    {
        /* Find free slot: */
        int idxFirstUnused = m_workers.size();
        while (idxFirstUnused-- > 0)
            if (m_workers.at(idxFirstUnused) == 0)
            {
                /* Prepare the new worker: */
                UIThreadWorker *pWorker = new UIThreadWorker(this, idxFirstUnused);
                connect(pWorker, &UIThreadWorker::sigFinished,
                        this, &UIThreadPool::sltHandleWorkerFinished, Qt::QueuedConnection);
                m_workers[idxFirstUnused] = pWorker;
                ++m_cWorkers;

                /* And start it: */
                pWorker->start();
                break;
            }
    }
    /* else: wait for some worker to complete
     * whatever it's busy with and jump to it. */

    /* Unlock finally: */
    m_everythingLocker.unlock();
}

UITask *UIThreadPool::dequeueTask(UIThreadWorker *pWorker)
{
    /* Lock initially: */
    m_everythingLocker.lock();

    /* Dequeue a task, watching out for terminations.
     * For optimal efficiency in enqueueTask() we keep count of idle threads.
     * If the wait times out, we'll return 0 and terminate the thread. */
    bool fIdleTimedOut = false;
    while (!m_fTerminating)
    {
        /* Make sure that worker has proper index: */
        Assert(m_workers.at(pWorker->index()) == pWorker);

        /* Dequeue task if there is one: */
        if (!m_pendingTasks.isEmpty())
        {
            UITask *pTask = m_pendingTasks.dequeue();
            if (pTask)
            {
                /* Put into the set of executing tasks: */
                m_executingTasks << pTask;

                /* Unlock finally: */
                m_everythingLocker.unlock();

                /* Return dequeued task: */
                return pTask;
            }
        }

        /* If we timed out already, then quit the worker thread. To prevent a
         * race between enqueueTask and the queue removal of the thread from
         * the workers vector, we remove it here already. (This does not apply
         * to the termination scenario.) */
        if (fIdleTimedOut)
        {
            m_workers[pWorker->index()] = 0;
            --m_cWorkers;
            break;
        }

        /* Wait for a task or timeout: */
        ++m_cIdleWorkers;
        fIdleTimedOut = !m_taskCondition.wait(&m_everythingLocker, m_cMsIdleTimeout);
        --m_cIdleWorkers;
    }

    /* Unlock finally: */
    m_everythingLocker.unlock();

    /* Return 0 by default: */
    return 0;
}

void UIThreadPool::sltHandleTaskComplete(UITask *pTask)
{
    /* Skip on termination: */
    if (isTerminating())
        return;

    /* Notify listeners: */
    emit sigTaskComplete(pTask);

    /* Lock initially: */
    m_everythingLocker.lock();

    /* Delete task finally: */
    if (   !m_executingTasks.contains(pTask)
        || !m_executingTasks.remove(pTask))
        AssertMsgFailed(("Unable to find or remove complete task!"));
    delete pTask;

    /* Unlock finally: */
    m_everythingLocker.unlock();
}

void UIThreadPool::sltHandleWorkerFinished(UIThreadWorker *pWorker)
{
    /* Wait for the thread to finish completely, then delete the thread
     * object. We have already removed the thread from the workers vector.
     * Note! We don't want to use 'this' here, in case it's invalid. */
    pWorker->wait();
    delete pWorker;
}


/*********************************************************************************************************************************
*   Class UIThreadWorker implementation.                                                                                         *
*********************************************************************************************************************************/

UIThreadWorker::UIThreadWorker(UIThreadPool *pPool, int iIndex)
    : m_pPool(pPool)
    , m_iIndex(iIndex)
    , m_fNoFinishedSignal(false)
{
}

void UIThreadWorker::run()
{
    /* Initialize COM: */
    COMBase::InitializeCOM(false);

    /* Try get a task from the pool queue: */
    while (UITask *pTask = m_pPool->dequeueTask(this))
    {
        /* Process the task if we are not terminating.
         * Please take into account tasks are cleared by the UIThreadPool
         * after all listeners notified about task is complete and handled it. */
        if (!m_pPool->isTerminating())
            pTask->start();
    }

    /* Cleanup COM: */
    COMBase::CleanupCOM();

    /* Queue a signal for the pool to do thread cleanup, unless the pool is
       already terminating and doesn't need the signal. */
    if (!m_fNoFinishedSignal)
        emit sigFinished(this);
}


#include "UIThreadPool.moc"
