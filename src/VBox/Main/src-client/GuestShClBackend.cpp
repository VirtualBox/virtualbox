/* $Id: GuestShClBackend.cpp 115050 2026-08-17 15:20:35Z andreas.loeffler@oracle.com $ */
/** @file
 * Main Shared Clipboard - Native backend dispatcher implementation.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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

#include "GuestShClBackend.h"
#include "GuestShClBackendPrivate.h"

#include <VBox/err.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>


ShClBackend::ShClBackend(void)
    : m_pOps(ShClBackendGetOps())
    , m_pCtx(NULL)
{
    AssertPtr(m_pOps);
}


ShClBackend::~ShClBackend(void)
{
    Assert(m_pCtx == NULL);
}


int ShClBackend::init(void)
{
    AssertPtrReturn(m_pOps, VERR_INVALID_STATE);
    AssertPtrReturn(m_pOps->pfnInit, VERR_INVALID_STATE);
    return m_pOps->pfnInit();
}


void ShClBackend::destroy(void)
{
    AssertPtrReturnVoid(m_pOps);
    AssertPtrReturnVoid(m_pOps->pfnDestroy);
    m_pOps->pfnDestroy();
}


void ShClBackend::setCallbacks(PSHCLCALLBACKS pCallbacks)
{
    AssertPtrReturnVoid(m_pOps);
    if (m_pOps->pfnSetCallbacks)
        m_pOps->pfnSetCallbacks(pCallbacks);
}


int ShClBackend::connect(GuestShClConn *pConn)
{
    AssertPtrReturn(pConn, VERR_INVALID_POINTER);
    AssertPtrReturn(m_pOps, VERR_INVALID_STATE);
    AssertPtrReturn(m_pOps->pfnConnect, VERR_INVALID_STATE);
    AssertReturn(m_pCtx == NULL, VERR_RESOURCE_BUSY);

    PSHCLCONTEXT pCtx = NULL;
    int vrc = m_pOps->pfnConnect(pConn, &pCtx);
    if (RT_SUCCESS(vrc))
    {
        if (pCtx)
            m_pCtx = pCtx;
        else
            vrc = VERR_INTERNAL_ERROR;
    }
    return vrc;
}


int ShClBackend::disconnect(void)
{
    AssertPtrReturn(m_pOps, VERR_INVALID_STATE);
    AssertPtrReturn(m_pOps->pfnDisconnect, VERR_INVALID_STATE);
    AssertPtrReturn(m_pCtx, VERR_INVALID_STATE);

    PSHCLCONTEXT const pCtx = m_pCtx;
    int const vrc = m_pOps->pfnDisconnect(pCtx);
    m_pCtx = NULL;
    return vrc;
}


int ShClBackend::reportFormats(SHCLFORMATS fFormats)
{
    AssertPtrReturn(m_pCtx, VERR_INVALID_STATE);
    return m_pOps->pfnReportFormats(m_pCtx, fFormats);
}


int ShClBackend::readData(SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    AssertPtrReturn(m_pCtx, VERR_INVALID_STATE);
    return m_pOps->pfnReadData(m_pCtx, uFormat, pvData, cbData, pcbActual);
}


int ShClBackend::writeData(SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    AssertPtrReturn(m_pCtx, VERR_INVALID_STATE);
    return m_pOps->pfnWriteData(m_pCtx, uFormat, pvData, cbData);
}


int ShClBackend::sync(void)
{
    AssertPtrReturn(m_pCtx, VERR_INVALID_STATE);
    return m_pOps->pfnSync(m_pCtx);
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
void ShClBackend::transferGetCallbacks(PSHCLTRANSFERCALLBACKS pCallbacks)
{
    AssertPtrReturnVoid(pCallbacks);
    AssertPtrReturnVoid(m_pCtx);
    m_pOps->pfnTransferGetCallbacks(m_pCtx, pCallbacks);
}


int ShClBackend::transferHandleStatusReply(PSHCLTRANSFER pTransfer, SHCLSOURCE enmSource,
                                            SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(m_pCtx, VERR_INVALID_STATE);
    return m_pOps->pfnTransferHandleStatusReply(m_pCtx, pTransfer, enmSource, enmStatus, rcStatus);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
