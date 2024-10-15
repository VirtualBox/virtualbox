/* $Id: ThreadTask.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Implementation of ThreadTask
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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

#include <iprt/errcore.h>
#include <iprt/thread.h>

#include "VirtualBoxBase.h"
#include "ThreadTask.h"

#define LOG_GROUP LOG_GROUP_MAIN_THREAD_TASK
#include "LoggingNew.h"

/**
 * Starts the task (on separate thread), consuming @a this.
 *
 * The function takes ownership of "this" instance (object instance which calls
 * this function). And the function is responsible for deletion of "this"
 * pointer in all cases.
 *
 * Possible way of usage:
 * @code{.cpp}
        HRESULT hr;
        SomeTaskInheritedFromThreadTask *pTask = NULL;
        try
        {
            pTask = new SomeTaskInheritedFromThreadTask(this);
            if (!pTask->Init()) // some init procedure
                throw E_FAIL;
        }
        catch (...)
        {
            if (pTask);
                delete pTask;
            return E_FAIL;
        }
        return pTask->createThread(); // pTask is always consumed
   @endcode
 *
 * @sa createThreadWithType
 *
 * @note Always consumes @a this!
 */
HRESULT ThreadTask::createThread(void)
{
    return createThreadInternal(RTTHREADTYPE_MAIN_WORKER);
}


/**
 * Same ThreadTask::createThread(), except it takes a thread type parameter.
 *
 * @param   enmType     The thread type.
 *
 * @note Always consumes @a this!
 */
HRESULT ThreadTask::createThreadWithType(RTTHREADTYPE enmType)
{
    return createThreadInternal(enmType);
}


/**
 * Internal worker for ThreadTask::createThread,
 * ThreadTask::createThreadWithType.
 *
 * @note Always consumes @a this!
 */
HRESULT ThreadTask::createThreadInternal(RTTHREADTYPE enmType)
{
    LogThisFunc(("Created \"%s\"\n", m_strTaskName.c_str()));

    mAsync = true;
    int vrc = RTThreadCreate(NULL,
                             taskHandlerThreadProc,
                             (void *)this,
                             0,
                             enmType,
                             0,
                             m_strTaskName.c_str());
    if (RT_SUCCESS(vrc))
        return S_OK;

    mAsync = false;
    delete this;
    return E_FAIL;
}


/**
 * Static method that can get passed to RTThreadCreate to have a
 * thread started for a Task.
 */
/* static */ DECLCALLBACK(int) ThreadTask::taskHandlerThreadProc(RTTHREAD /* thread */, void *pvUser)
{
    if (pvUser == NULL)
        return VERR_INVALID_POINTER; /* nobody cares */

    ThreadTask *pTask = static_cast<ThreadTask *>(pvUser);

    LogFunc(("Started \"%s\"\n", pTask->m_strTaskName.c_str()));

    /*
     *  handler shall catch and process all possible cases as errors and exceptions.
     */
    pTask->handler();

    LogFunc(("Ended \"%s\"\n", pTask->m_strTaskName.c_str()));

    delete pTask;
    return VINF_SUCCESS;
}

