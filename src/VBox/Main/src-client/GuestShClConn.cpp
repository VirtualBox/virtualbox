/* $Id: GuestShClConn.cpp 115050 2026-08-17 15:20:35Z andreas.loeffler@oracle.com $ */
/** @file
 * Main Shared Clipboard - Service connection management implementation.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/log.h>

#include "GuestShClConn.h"
#include "GuestShClBackend.h"
#ifdef VBOX_COM_INPROC
# include "GuestShClPrivate.h"
#endif

#include <VBox/err.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/string.h>


GuestShClConn::GuestShClConn(GuestShCl *pOwner)
    : m_pOwner(pOwner)
    , m_hCallsDone(NIL_RTSEMEVENTMULTI)
    , m_enmState(State_Disconnected)
    , m_fBackendInitialized(false)
    , m_cCalls(0)
{
    RT_ZERO(m_Transport);

    int vrc = RTCritSectInit(&m_CritSect);
    if (RT_FAILURE(vrc))
        throw vrc;

    vrc = RTSemEventMultiCreate(&m_hCallsDone);
    if (RT_FAILURE(vrc))
    {
        RTCritSectDelete(&m_CritSect);
        throw vrc;
    }
    vrc = RTSemEventMultiSignal(m_hCallsDone);
    if (RT_FAILURE(vrc))
    {
        RTSemEventMultiDestroy(m_hCallsDone);
        m_hCallsDone = NIL_RTSEMEVENTMULTI;
        RTCritSectDelete(&m_CritSect);
        throw vrc;
    }
}


GuestShClConn::~GuestShClConn(void)
{
    Assert(m_enmState == State_Disconnected);
    Assert(m_cCalls == 0);
    Assert(!m_fBackendInitialized);

    if (m_hCallsDone != NIL_RTSEMEVENTMULTI)
    {
        RTSemEventMultiDestroy(m_hCallsDone);
        m_hCallsDone = NIL_RTSEMEVENTMULTI;
    }
    if (RTCritSectIsInitialized(&m_CritSect))
        RTCritSectDelete(&m_CritSect);
}


int GuestShClConn::i_callBegin(PSHCLTRANSPORT pTransport)
{
    AssertPtrReturn(pTransport, VERR_INVALID_POINTER);
    RT_ZERO(*pTransport);

    int vrc = RTCritSectEnter(&m_CritSect);
    if (RT_FAILURE(vrc))
        return vrc;

    if (   (m_enmState == State_Connecting || m_enmState == State_Connected)
        && ShClTransportIsValid(&m_Transport))
    {
        *pTransport = m_Transport;
        if (m_cCalls++ == 0)
        {
            int const vrcReset = RTSemEventMultiReset(m_hCallsDone);
            AssertFatalMsgRC(vrcReset, ("Resetting the Shared Clipboard connection call-drain event failed with %Rrc\n",
                                        vrcReset));
        }
        vrc = VINF_SUCCESS;
    }
    else
        vrc = VERR_SHCLPB_NO_DATA;

    int const vrcLeave = RTCritSectLeave(&m_CritSect);
    AssertFatalMsgRC(vrcLeave, ("Releasing the Shared Clipboard connection lock failed with %Rrc\n", vrcLeave));
    return vrc;
}


void GuestShClConn::i_callEnd(void)
{
    int const vrc = RTCritSectEnter(&m_CritSect);
    AssertFatalMsgRC(vrc, ("Taking the Shared Clipboard connection lock while ending a call failed with %Rrc\n", vrc));

    Assert(m_cCalls > 0);
    if (m_cCalls > 0 && --m_cCalls == 0)
    {
        int const vrcSignal = RTSemEventMultiSignal(m_hCallsDone);
        AssertFatalMsgRC(vrcSignal, ("Signalling the Shared Clipboard connection call-drain event failed with %Rrc\n",
                                     vrcSignal));
    }

    int const vrcLeave = RTCritSectLeave(&m_CritSect);
    AssertFatalMsgRC(vrcLeave, ("Releasing the Shared Clipboard connection lock while ending a call failed with %Rrc\n",
                                vrcLeave));
}


void GuestShClConn::i_waitForCalls(void)
{
    for (;;)
    {
        int vrc = RTCritSectEnter(&m_CritSect);
        AssertFatalMsgRC(vrc, ("Taking the Shared Clipboard connection lock while draining calls failed with %Rrc\n", vrc));
        bool const fDone = m_cCalls == 0;
        vrc = RTCritSectLeave(&m_CritSect);
        AssertFatalMsgRC(vrc, ("Releasing the Shared Clipboard connection lock while draining calls failed with %Rrc\n", vrc));
        if (fDone)
            return;

        vrc = RTSemEventMultiWait(m_hCallsDone, RT_INDEFINITE_WAIT);
        AssertFatalMsgRC(vrc, ("Draining Shared Clipboard connection calls failed with %Rrc\n", vrc));
    }
}


int GuestShClConn::initBackend(void)
{
    int vrc = RTCritSectEnter(&m_CritSect);
    if (RT_FAILURE(vrc))
        return vrc;
    bool const fInitialized = m_fBackendInitialized;
    RTCritSectLeave(&m_CritSect);
    if (fInitialized)
        return VINF_SUCCESS;

    vrc = m_Backend.init();
    if (RT_SUCCESS(vrc))
    {
        int const vrcLock = RTCritSectEnter(&m_CritSect);
        AssertFatalMsgRC(vrcLock, ("Taking the Shared Clipboard connection lock after backend initialization failed with %Rrc\n",
                                   vrcLock));
        m_fBackendInitialized = true;
        int const vrcLeave = RTCritSectLeave(&m_CritSect);
        AssertFatalMsgRC(vrcLeave,
                         ("Releasing the Shared Clipboard connection lock after backend initialization failed "
                          "with %Rrc\n", vrcLeave));
    }
    return vrc;
}


int GuestShClConn::destroyBackend(void)
{
    SHCLTRANSPORT Transport;
    bool fConnected;
    bool fInitialized;
    int vrc = RTCritSectEnter(&m_CritSect);
    if (RT_FAILURE(vrc))
        return vrc;
    Transport = m_Transport;
    fConnected = m_enmState == State_Connected;
    fInitialized = m_fBackendInitialized;
    RTCritSectLeave(&m_CritSect);

    int vrcDisconnect = VINF_SUCCESS;
    if (fConnected)
        vrcDisconnect = disconnect(&Transport);

    if (fInitialized)
    {
        m_Backend.destroy();
        vrc = RTCritSectEnter(&m_CritSect);
        AssertFatalMsgRC(vrc, ("Taking the Shared Clipboard connection lock after backend destruction failed with %Rrc\n", vrc));
        m_fBackendInitialized = false;
        int const vrcLeave = RTCritSectLeave(&m_CritSect);
        AssertFatalMsgRC(vrcLeave, ("Releasing the Shared Clipboard connection lock after backend destruction failed with %Rrc\n",
                                    vrcLeave));
    }
    return RT_FAILURE(vrcDisconnect) ? vrcDisconnect : vrc;
}


void GuestShClConn::setBackendCallbacks(PSHCLCALLBACKS pCallbacks)
{
    m_Backend.setCallbacks(pCallbacks);
}


int GuestShClConn::connect(PCSHCLTRANSPORT pTransport)
{
    AssertReturn(ShClTransportIsValid(pTransport), VERR_INVALID_HANDLE);

    int vrc = RTCritSectEnter(&m_CritSect);
    if (RT_FAILURE(vrc))
        return vrc;
    if (m_enmState != State_Disconnected)
    {
        RTCritSectLeave(&m_CritSect);
        return VERR_RESOURCE_BUSY;
    }
    m_Transport = *pTransport;
    m_enmState = State_Connecting;
    RTCritSectLeave(&m_CritSect);

    vrc = m_Backend.connect(this);

    int const vrcLock = RTCritSectEnter(&m_CritSect);
    AssertFatalMsgRC(vrcLock, ("Taking the Shared Clipboard connection lock after backend connection failed with %Rrc\n",
                               vrcLock));
    if (RT_SUCCESS(vrc))
        m_enmState = State_Connected;
    else
    {
        m_enmState = State_Disconnected;
        RT_ZERO(m_Transport);
    }
    int const vrcLeave = RTCritSectLeave(&m_CritSect);
    AssertFatalMsgRC(vrcLeave, ("Releasing the Shared Clipboard connection lock after backend connection failed with %Rrc\n",
                                vrcLeave));
    return vrc;
}


int GuestShClConn::disconnect(PCSHCLTRANSPORT pTransport)
{
    AssertReturn(ShClTransportIsValid(pTransport), VERR_INVALID_HANDLE);

    int vrc = RTCritSectEnter(&m_CritSect);
    if (RT_FAILURE(vrc))
        return vrc;
    if (   m_enmState != State_Connected
        || !ShClTransportIsEqual(&m_Transport, pTransport))
    {
        RTCritSectLeave(&m_CritSect);
        return VERR_INVALID_HANDLE;
    }
    m_enmState = State_Closing;
    RTCritSectLeave(&m_CritSect);

    i_waitForCalls();
    vrc = m_Backend.disconnect();

    int const vrcLock = RTCritSectEnter(&m_CritSect);
    AssertFatalMsgRC(vrcLock, ("Taking the Shared Clipboard connection lock after backend disconnection failed with %Rrc\n",
                               vrcLock));
    RT_ZERO(m_Transport);
    m_enmState = State_Disconnected;
    int const vrcLeave = RTCritSectLeave(&m_CritSect);
    AssertFatalMsgRC(vrcLeave, ("Releasing the Shared Clipboard connection lock after backend disconnection failed with %Rrc\n",
                                vrcLeave));
    return vrc;
}


bool GuestShClConn::matches(PCSHCLTRANSPORT pTransport) const
{
    bool fMatches = false;
    int const vrc = RTCritSectEnter(&m_CritSect);
    if (RT_SUCCESS(vrc))
    {
        fMatches = (m_enmState == State_Connecting || m_enmState == State_Connected)
                && ShClTransportIsEqual(&m_Transport, pTransport);
        RTCritSectLeave(&m_CritSect);
    }
    return fMatches;
}


bool GuestShClConn::isConnected(void) const
{
    bool fConnected = false;
    int const vrc = RTCritSectEnter(&m_CritSect);
    if (RT_SUCCESS(vrc))
    {
        fConnected = m_enmState == State_Connected;
        RTCritSectLeave(&m_CritSect);
    }
    return fConnected;
}


#define SHCL_CONN_SVC_CALL_BEGIN(a_Transport) \
    SHCLTRANSPORT a_Transport; \
    int vrc = i_callBegin(&(a_Transport)); \
    if (RT_FAILURE(vrc)) \
        return vrc

#define SHCL_CONN_BACKEND_CALL_BEGIN(a_Transport) \
    SHCLTRANSPORT a_Transport; \
    int vrc = i_callBegin(&(a_Transport)); \
    if (RT_FAILURE(vrc)) \
        return vrc


int GuestShClConn::reportFormatsToGuest(SHCLFORMATS fFormats, SHCLFORMATS *pfReported)
{
    SHCL_CONN_SVC_CALL_BEGIN(Transport);
    vrc = Transport.pOps->pfnReportFormatsToGuest(Transport.hClient, fFormats, pfReported);
    i_callEnd();
    return vrc;
}


int GuestShClConn::reportLocalFormats(SHCLFORMATS fFormats)
{
#ifdef VBOX_COM_INPROC
    if (m_pOwner)
        return m_pOwner->ReportFormatsToGuest(this, fFormats, SHCLSOURCE_LOCAL);
#endif
    return reportFormatsToGuest(fFormats);
}


int GuestShClConn::readDataFromGuestAsync(SHCLFORMATS fFormats, PSHCLEVENT *ppEvent)
{
    SHCL_CONN_SVC_CALL_BEGIN(Transport);
    vrc = Transport.pOps->pfnReadDataFromGuestAsync(Transport.hClient, fFormats, ppEvent);
    i_callEnd();
    return vrc;
}


int GuestShClConn::readDataFromGuest(SHCLFORMAT uFormat, void **ppvData, uint32_t *pcbData)
{
    AssertPtrReturn(ppvData, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbData, VERR_INVALID_POINTER);
    SHCL_CONN_SVC_CALL_BEGIN(Transport);
    vrc = Transport.pOps->pfnReadDataFromGuest(Transport.hClient, uFormat, ppvData, pcbData);
    i_callEnd();
    return vrc;
}


int GuestShClConn::guestDataBegin(PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT uFormat, PSHCLGUESTDATATOKEN phToken)
{
    AssertPtrReturn(pCmdCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(phToken, VERR_INVALID_POINTER);
    *phToken = NULL;
    SHCL_CONN_SVC_CALL_BEGIN(Transport);
    vrc = Transport.pOps->pfnGuestDataBegin(Transport.hClient, pCmdCtx, uFormat, phToken);
    if (   RT_FAILURE(vrc)
        || !*phToken)
        i_callEnd();
    return vrc;
}


int GuestShClConn::guestDataComplete(SHCLGUESTDATATOKEN hToken, void const *pvData, uint32_t cbData)
{
    AssertPtrReturn(hToken, VERR_INVALID_HANDLE);

    SHCLTRANSPORT Transport;
    int vrc = RTCritSectEnter(&m_CritSect);
    if (RT_FAILURE(vrc))
        return vrc;
    if (m_cCalls > 0 && ShClTransportIsValid(&m_Transport))
        Transport = m_Transport;
    else
    {
        RT_ZERO(Transport);
        vrc = VERR_INVALID_STATE;
    }
    RTCritSectLeave(&m_CritSect);

    if (RT_SUCCESS(vrc))
    {
        vrc = Transport.pOps->pfnGuestDataComplete(Transport.hClient, hToken, pvData, cbData);
        i_callEnd();
    }
    return vrc;
}


void GuestShClConn::guestDataCancel(SHCLGUESTDATATOKEN hToken)
{
    AssertPtrReturnVoid(hToken);

    SHCLTRANSPORT Transport;
    int const vrc = RTCritSectEnter(&m_CritSect);
    if (RT_SUCCESS(vrc))
    {
        if (m_cCalls > 0 && ShClTransportIsValid(&m_Transport))
            Transport = m_Transport;
        else
            RT_ZERO(Transport);
        RTCritSectLeave(&m_CritSect);

        if (ShClTransportIsValid(&Transport))
        {
            Transport.pOps->pfnGuestDataCancel(Transport.hClient, hToken);
            i_callEnd();
        }
    }
}


int GuestShClConn::syncBackend(void)
{
    SHCL_CONN_BACKEND_CALL_BEGIN(Transport);
    vrc = m_Backend.sync();
    i_callEnd();
    return vrc;
}


int GuestShClConn::reportFormatsToBackend(SHCLFORMATS fFormats)
{
    SHCL_CONN_BACKEND_CALL_BEGIN(Transport);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    SHCLFORMATS fFiltered = VBOX_SHCL_FMT_NONE;
    vrc = Transport.pOps->pfnFilterFormats(Transport.hClient, false /* fHostToGuest */, fFormats, &fFiltered);
    if (RT_SUCCESS(vrc))
        fFormats = fFiltered;
#endif
    if (RT_SUCCESS(vrc))
        vrc = m_Backend.reportFormats(fFormats);
    i_callEnd();
    return vrc;
}


int GuestShClConn::readDataFromBackend(SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    SHCL_CONN_BACKEND_CALL_BEGIN(Transport);
    vrc = m_Backend.readData(uFormat, pvData, cbData, pcbActual);
    i_callEnd();
    return vrc;
}


int GuestShClConn::writeDataToBackend(SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    SHCL_CONN_BACKEND_CALL_BEGIN(Transport);
    vrc = m_Backend.writeData(uFormat, pvData, cbData);
    i_callEnd();
    return vrc;
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
int GuestShClConn::transferGetCallbacks(PSHCLTRANSFERCALLBACKS pCallbacks)
{
    AssertPtrReturn(pCallbacks, VERR_INVALID_POINTER);
    SHCL_CONN_BACKEND_CALL_BEGIN(Transport);
    m_Backend.transferGetCallbacks(pCallbacks);
    i_callEnd();
    return VINF_SUCCESS;
}


int GuestShClConn::transferHandleStatusReply(PSHCLTRANSFER pTransfer, SHCLSOURCE enmSource,
                                              SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    SHCL_CONN_BACKEND_CALL_BEGIN(Transport);
    vrc = m_Backend.transferHandleStatusReply(pTransfer, enmSource, enmStatus, rcStatus);
    i_callEnd();
    return vrc;
}


PSHCLTRANSFER GuestShClConn::transferGetByIdRetained(SHCLTRANSFERID idTransfer)
{
    SHCLTRANSPORT Transport;
    int const vrc = i_callBegin(&Transport);
    if (RT_FAILURE(vrc))
        return NULL;
    PSHCLTRANSFER const pTransfer = Transport.pOps->pfnTransferGetByIdRetained(Transport.hClient, idTransfer);
    i_callEnd();
    return pTransfer;
}


PSHCLTRANSFER GuestShClConn::transferGetByKeyRetained(SHCLSESSIONID idSession, SHCLTRANSFERID idTransfer,
                                                       SHCLTRANSFERGEN uGeneration)
{
    SHCLTRANSPORT Transport;
    int const vrc = i_callBegin(&Transport);
    if (RT_FAILURE(vrc))
        return NULL;
    PSHCLTRANSFER const pTransfer = Transport.pOps->pfnTransferGetByKeyRetained(Transport.hClient,
                                                                               idSession, idTransfer, uGeneration);
    i_callEnd();
    return pTransfer;
}


int GuestShClConn::transferCreate(SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource,
                                   PSHCLTRANSFERCALLBACKS pCallbacks, SHCLTRANSFERID idTransfer,
                                   PSHCLTRANSFER *ppTransfer)
{
    SHCL_CONN_SVC_CALL_BEGIN(Transport);
    vrc = Transport.pOps->pfnTransferCreate(Transport.hClient, enmDir, enmSource, pCallbacks,
                                            idTransfer, ppTransfer);
    i_callEnd();
    return vrc;
}


int GuestShClConn::transferInit(PSHCLTRANSFER pTransfer)
{
    SHCL_CONN_SVC_CALL_BEGIN(Transport);
    vrc = Transport.pOps->pfnTransferInit(Transport.hClient, pTransfer);
    i_callEnd();
    return vrc;
}


void GuestShClConn::transferDestroyById(SHCLTRANSFERID idTransfer)
{
    SHCLTRANSPORT Transport;
    int const vrc = i_callBegin(&Transport);
    if (RT_SUCCESS(vrc))
    {
        Transport.pOps->pfnTransferDestroyById(Transport.hClient, idTransfer);
        i_callEnd();
    }
}


void GuestShClConn::transferDestroyAll(void)
{
    SHCLTRANSPORT Transport;
    int const vrc = RTCritSectEnter(&m_CritSect);
    AssertFatalMsgRC(vrc, ("Taking the Shared Clipboard connection lock while destroying transfers failed with %Rrc\n", vrc));
    Transport = m_Transport;
    int const vrcLeave = RTCritSectLeave(&m_CritSect);
    AssertFatalMsgRC(vrcLeave, ("Releasing the Shared Clipboard connection lock while destroying transfers failed with %Rrc\n",
                                vrcLeave));
    if (ShClTransportIsValid(&Transport))
        Transport.pOps->pfnTransferDestroyAll(Transport.hClient);
}


int GuestShClConn::transferProviderInitGuest(PSHCLTXPROVIDER pProvider)
{
    AssertPtrReturn(pProvider, VERR_INVALID_POINTER);
    SHCL_CONN_SVC_CALL_BEGIN(Transport);
    vrc = Transport.pOps->pfnTransferProviderInitGuest(Transport.hClient, pProvider);
    i_callEnd();
    return vrc;
}


#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


#undef SHCL_CONN_BACKEND_CALL_BEGIN
#undef SHCL_CONN_SVC_CALL_BEGIN
