/* $Id: dndmanager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Drag and Drop manager: Handling of DnD messages on the host side.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/

#ifdef LOG_GROUP
 #undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_GUEST_DND

#include "dndmanager.h"

#include <VBox/log.h>
#include <iprt/file.h>
#include <iprt/dir.h>
#include <iprt/path.h>
#include <iprt/uri.h>


/*********************************************************************************************************************************
*   DnDManager                                                                                                                   *
*********************************************************************************************************************************/

/**
 * Adds a DnD message to the manager's queue.
 *
 * @returns IPRT status code.
 * @param   pMsg                Pointer to DnD message to add. The queue then owns the pointer.
 * @param   fAppend             Whether to append or prepend the message to the queue.
 */
int DnDManager::AddMsg(DnDMessage *pMsg, bool fAppend /* = true */)
{
    AssertPtrReturn(pMsg, VERR_INVALID_POINTER);

    LogFlowFunc(("uMsg=%s (%#x), cParms=%RU32, fAppend=%RTbool\n",
                 DnDHostMsgToStr(pMsg->GetType()), pMsg->GetType(), pMsg->GetParamCount(), fAppend));

    if (fAppend)
        m_queueMsg.append(pMsg);
    else
        m_queueMsg.prepend(pMsg);

#ifdef DEBUG
    DumpQueue();
#endif

    /** @todo Catch / handle OOM? */

    return VINF_SUCCESS;
}

/**
 * Adds a DnD message to the manager's queue.
 *
 * @returns IPRT status code.
 * @param   uMsg                Type (function number) of message to add.
 * @param   cParms              Number of parameters of message to add.
 * @param   paParms             Array of parameters of message to add.
 * @param   fAppend             Whether to append or prepend the message to the queue.
 */
int DnDManager::AddMsg(uint32_t uMsg, uint32_t cParms, VBOXHGCMSVCPARM paParms[], bool fAppend /* = true */)
{
    int rc;

    try
    {
        DnDMessage *pMsg = new DnDGenericMessage(uMsg, cParms, paParms);
        rc = AddMsg(pMsg, fAppend);
    }
    catch(std::bad_alloc &)
    {
        rc = VERR_NO_MEMORY;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#ifdef DEBUG
void DnDManager::DumpQueue(void)
{
    LogFunc(("Current queue (%zu items, FIFO) is: %s", m_queueMsg.size(), m_queueMsg.isEmpty() ? "<Empty>" : ""));
    for (size_t i = 0; i < m_queueMsg.size(); ++i)
    {
        if (i > 0)
            Log((" - "));
        DnDMessage const *pMsg = m_queueMsg[i];
        uint32_t const uType = pMsg->GetType();
        Log(("%s (%d / %#x) cRefS=%RU32", DnDHostMsgToStr(uType), uType, uType, pMsg->RefCount()));
    }
    Log(("\n"));
}
#endif /* DEBUG */

/**
 * Retrieves information about the next message in the queue.
 *
 * @returns IPRT status code. VERR_NO_DATA if no next message is available.
 * @param   fAddRef             Set to \c true to increase the message's reference count, or \c false if not.
 * @param   puType              Where to store the message type.
 * @param   pcParms             Where to store the message parameter count.
 */
int DnDManager::GetNextMsgInfo(bool fAddRef, uint32_t *puType, uint32_t *pcParms)
{
    AssertPtrReturn(puType, VERR_INVALID_POINTER);
    AssertPtrReturn(pcParms, VERR_INVALID_POINTER);

    int rc;

    if (m_queueMsg.isEmpty())
    {
        rc = VERR_NO_DATA;
    }
    else
    {
        DnDMessage *pMsg = m_queueMsg.first();
        AssertPtr(pMsg);

        *puType  = pMsg->GetType();
        *pcParms = pMsg->GetParamCount();

        if (fAddRef)
            pMsg->AddRef();

        rc = VINF_SUCCESS;
    }

#ifdef DEBUG
    DumpQueue();
#endif

    LogFlowFunc(("Returning uMsg=%s (%#x), cParms=%RU32, fAddRef=%RTbool, rc=%Rrc\n",
                 DnDHostMsgToStr(*puType), *puType, *pcParms, fAddRef, rc));
    return rc;
}

/**
 * Retrieves the next queued up message and removes it from the queue on success.
 *
 * @returns VBox status code.
 * @retval  VERR_NO_DATA if no next message is available.
 * @param   uMsg                Message type to retrieve.
 * @param   cParms              Number of parameters the \@a paParms array can store.
 * @param   paParms             Where to store the message parameters.
 */
int DnDManager::GetNextMsg(uint32_t uMsg, uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    LogFlowFunc(("uMsg=%s (%#x), cParms=%RU32\n", DnDHostMsgToStr(uMsg), uMsg, cParms));

    /* Check for pending messages in our queue. */
    if (m_queueMsg.isEmpty())
        return VERR_NO_DATA;

#ifdef DEBUG
    DumpQueue();
#endif

    /* Get the current message. */
    DnDMessage *pMsg = m_queueMsg.first();
    AssertPtr(pMsg);

    if (pMsg->Release() == 0)     /* Not referenced by any client anymore? */
        m_queueMsg.removeFirst(); /* Remove the current message from the queue. */

    /* Fetch the current message info. */
    int rc = pMsg->GetData(uMsg, cParms, paParms);

    /*
     * If there was an error handling the current message or the user has canceled
     * the operation, we need to cleanup all pending events.
     */
    if (RT_FAILURE(rc))
    {
        /* Clear any pending messages. */
        Reset(true /* fForce */);
    }

    LogFlowFunc(("Message processed with rc=%Rrc\n", rc));
    return rc;
}

/**
 * Resets the manager by clearing the message queue and internal state.
 *
 * @param   fForce              Set to \c true to forcefully also remove still referenced messages, or \c false to only
 *                              remove non-referenced messages.
 */
void DnDManager::Reset(bool fForce)
{
    LogFlowFuncEnter();

#ifdef DEBUG
    DumpQueue();
#endif

    for (size_t i = 0; i < m_queueMsg.size(); i++)
    {
        if (   fForce
            || m_queueMsg[i]->RefCount() == 0)
        {
            m_queueMsg.removeAt(i);
            i = i > 0 ? i - 1 : 0;
        }
    }
}

