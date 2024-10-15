/* $Id: client.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Base class for a host-guest service.
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

#include <VBox/log.h>
#include <VBox/hgcmsvc.h>

#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/cpp/utils.h>

#include <VBox/HostServices/Service.h>

using namespace HGCM;

Client::Client(uint32_t idClient)
    : m_idClient(idClient)
    , m_fDeferred(false)
{
    RT_ZERO(m_Deferred);
    RT_ZERO(m_SvcCtx);
}

Client::~Client(void)
{

}

/**
 * Completes a guest call by returning the control back to the guest side,
 * together with a status code, internal version.
 *
 * @returns IPRT status code.
 * @param   hHandle             Call handle to complete guest call for.
 * @param   rcOp                Return code to return to the guest side.
 */
int Client::completeInternal(VBOXHGCMCALLHANDLE hHandle, int rcOp) RT_NOEXCEPT
{
    LogFlowThisFunc(("idClient=%RU32\n", m_idClient));

    if (   m_SvcCtx.pHelpers
        && m_SvcCtx.pHelpers->pfnCallComplete)
    {
        m_SvcCtx.pHelpers->pfnCallComplete(hHandle, rcOp);

        reset();
        return VINF_SUCCESS;
    }

    return VERR_NOT_AVAILABLE;
}

/**
 * Resets the client's internal state.
 */
void Client::reset(void) RT_NOEXCEPT
{
   m_fDeferred = false;

   RT_ZERO(m_Deferred);
}

/**
 * Completes a guest call by returning the control back to the guest side,
 * together with a status code.
 *
 * @returns IPRT status code.
 * @param   hHandle             Call handle to complete guest call for.
 * @param   rcOp                Return code to return to the guest side.
 */
int Client::Complete(VBOXHGCMCALLHANDLE hHandle, int rcOp /* = VINF_SUCCESS */) RT_NOEXCEPT
{
    return completeInternal(hHandle, rcOp);
}

/**
 * Completes a deferred guest call by returning the control back to the guest side,
 * together with a status code.
 *
 * @returns IPRT status code. VERR_INVALID_STATE if the client is not in deferred mode.
 * @param   rcOp                Return code to return to the guest side.
 */
int Client::CompleteDeferred(int rcOp) RT_NOEXCEPT
{
    if (m_fDeferred)
    {
        Assert(m_Deferred.hHandle != NULL);

        int rc = completeInternal(m_Deferred.hHandle, rcOp);
        if (RT_SUCCESS(rc))
            m_fDeferred = false;

        return rc;
    }

    AssertMsg(m_fDeferred, ("Client %RU32 is not in deferred mode\n", m_idClient));
    return VERR_INVALID_STATE;
}

/**
 * Returns the HGCM call handle of the client.
 *
 * @returns HGCM handle.
 */
VBOXHGCMCALLHANDLE Client::GetHandle(void) const RT_NOEXCEPT
{
    return m_Deferred.hHandle;
}

/**
 * Returns the HGCM call handle of the client.
 *
 * @returns HGCM handle.
 */
uint32_t Client::GetMsgType(void) const RT_NOEXCEPT
{
    return m_Deferred.uType;
}

uint32_t Client::GetMsgParamCount(void) const RT_NOEXCEPT
{
    return m_Deferred.cParms;
}

/**
 * Returns the client's (HGCM) ID.
 *
 * @returns The client's (HGCM) ID.
 */
uint32_t Client::GetClientID(void) const RT_NOEXCEPT
{
    return m_idClient;
}

/**
 * Returns whether the client currently is in deferred mode or not.
 *
 * @returns \c True if in deferred mode, \c False if not.
 */
bool Client::IsDeferred(void) const RT_NOEXCEPT
{
    return m_fDeferred;
}

/**
 * Set the client's status to deferred, meaning that it does not return to the caller
 * until CompleteDeferred() has been called.
 *
 * @returns VBox status code.
 * @param   hHandle             Call handle to save.
 * @param   u32Function         Function number to save.
 * @param   cParms              Number of HGCM parameters to save.
 * @param   paParms             HGCM parameters to save.
 */
void Client::SetDeferred(VBOXHGCMCALLHANDLE hHandle, uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM paParms[]) RT_NOEXCEPT
{
    LogFlowThisFunc(("uClient=%RU32\n", m_idClient));

    m_fDeferred = true;

    m_Deferred.hHandle = hHandle;
    m_Deferred.uType   = u32Function;
    m_Deferred.cParms  = cParms;
    m_Deferred.paParms = paParms;
}

/**
 * Sets the HGCM service context.
 *
 * @param   SvcCtx              Service context to set.
 */
void Client::SetSvcContext(const VBOXHGCMSVCTX &SvcCtx) RT_NOEXCEPT
{
    m_SvcCtx = SvcCtx;
}

/**
 * Sets the deferred parameters to a specific message type and
 * required parameters. That way the client can re-request that message with
 * the right amount of parameters from the service.
 *
 * @returns IPRT status code.
 * @param   uMsg                Message type (number) to set.
 * @param   cParms              Number of parameters the message needs.
 */
int Client::SetDeferredMsgInfo(uint32_t uMsg, uint32_t cParms) RT_NOEXCEPT
{
    if (m_fDeferred)
    {
        if (m_Deferred.cParms < 2)
            return VERR_INVALID_PARAMETER;

        AssertPtrReturn(m_Deferred.paParms, VERR_BUFFER_OVERFLOW);

        HGCMSvcSetU32(&m_Deferred.paParms[0], uMsg);
        HGCMSvcSetU32(&m_Deferred.paParms[1], cParms);

        return VINF_SUCCESS;
    }

    AssertFailed();
    return VERR_INVALID_STATE;
}

/**
 * Sets the deferred parameters to a specific message type and
 * required parameters. That way the client can re-request that message with
 * the right amount of parameters from the service.
 *
 * @returns IPRT status code.
 * @param   pMessage            Message to get message type and required parameters from.
 */
int Client::SetDeferredMsgInfo(const Message *pMessage) RT_NOEXCEPT
{
    AssertPtrReturn(pMessage, VERR_INVALID_POINTER);
    return SetDeferredMsgInfo(pMessage->GetType(), pMessage->GetParamCount());
}

