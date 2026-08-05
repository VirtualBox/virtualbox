/* $Id: GuestShClPrivate.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * Private Shared Clipboard code.
 */

/*
 * Copyright (C) 2023-2026 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include "LoggingNew.h"

#ifdef VBOX_WITH_SHARED_CLIPBOARD
# include "ClipboardImpl.h"
# include "ConsoleImpl.h"
# include "GuestShClPrivate.h"
# include "ProgressImpl.h"

# include <iprt/cpp/utils.h>

# include <VMMDev.h>
# include <VBox/VMMDevCoreTypes.h>

# include <VBox/GuestHost/SharedClipboard.h>
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
#  include <VBox/GuestHost/SharedClipboard-transfers.h>
# endif
# include <VBox/HostServices/VBoxClipboardSvc.h>
# include <VBox/HostServices/VBoxClipboardExt.h>
# include <VBox/GuestHost/clipboard-helper.h>
# include <VBox/version.h>


/*********************************************************************************************************************************
 * GuestShCl implementation.                                                                                                     *
 ********************************************************************************************************************************/



/** Static (Singleton) instance of the Shared Clipboard management object. */
GuestShCl* GuestShCl::s_pInstance = NULL;




GuestShCl::GuestShCl(Console *pConsole)
    : m_pConsole(pConsole)
    , m_pfnExtCallback(NULL)
    , m_pClient(NULL)
    , m_fGuestReadsBlocked(false)
    , m_cGuestReads(0)
    , m_hGuestReadsDone(NIL_RTSEMEVENTMULTI)
    , m_uHostDataSeq(0)
    , m_uGuestDataSeq(0)
{
    LogFlowFuncEnter();

    RT_ZERO(m_SvcExtVRDP);

    int vrc = RTCritSectInit(&m_CritSect);
    if (RT_FAILURE(vrc))
        throw vrc;

    vrc = RTSemEventMultiCreate(&m_hGuestReadsDone);
    if (RT_FAILURE(vrc))
    {
        RTCritSectDelete(&m_CritSect);
        throw vrc;
    }
    RTSemEventMultiSignal(m_hGuestReadsDone);
}

GuestShCl::~GuestShCl(void)
{
    uninit();
}

/**
 * Uninitializes the Shared Clipboard management object.
 */
void GuestShCl::uninit(void)
{
    LogFlowFuncEnter();

    if (m_hGuestReadsDone != NIL_RTSEMEVENTMULTI)
    {
        int vrc = lock();
        if (RT_SUCCESS(vrc))
        {
            m_fGuestReadsBlocked = true;
            unlock();
        }
        i_waitForGuestReads();
        RTSemEventMultiDestroy(m_hGuestReadsDone);
        m_hGuestReadsDone = NIL_RTSEMEVENTMULTI;
    }

    if (RTCritSectIsInitialized(&m_CritSect))
        RTCritSectDelete(&m_CritSect);

    RT_ZERO(m_SvcExtVRDP);

    m_pfnExtCallback = NULL;
    m_pClient = NULL;
    m_fGuestReadsBlocked = false;
    m_cGuestReads = 0;
    m_uHostDataSeq = 0;
    m_uGuestDataSeq = 0;
}

/**
 * Locks the Shared Clipboard management object.
 *
 * @returns VBox status code.
 */
int GuestShCl::lock(void)
{
    int vrc = RTCritSectEnter(&m_CritSect);
    AssertRC(vrc);
    return vrc;
}

/**
 * Unlocks the Shared Clipboard management object.
 *
 * @returns VBox status code.
 */
int GuestShCl::unlock(void)
{
    int vrc = RTCritSectLeave(&m_CritSect);
    AssertRC(vrc);
    return vrc;
}

/**
 * Increments the host data sequence counter.
 *
 * @returns Incremented host data sequence counter, or 0 if the counter cannot be incremented.
 */
uint64_t GuestShCl::i_incHostDataSeq(void)
{
    uint64_t uSeq = 0;
    int const vrc = lock();
    if (RT_SUCCESS(vrc))
    {
        uSeq = i_incHostDataSeqLocked();
        unlock();
    }
    else
        AssertMsgFailed(("Incrementing host data sequence counter failed with %Rrc\n", vrc));
    return uSeq;
}

/**
 * Increments the host data sequence counter while the caller owns the object lock.
 *
 * @returns Incremented host data sequence counter.
 */
uint64_t GuestShCl::i_incHostDataSeqLocked(void)
{
    return ++m_uHostDataSeq;
}

/**
 * Increments the guest data sequence counter.
 *
 * @returns Incremented guest data sequence counter, or 0 if the counter cannot be incremented.
 */
uint64_t GuestShCl::i_incGuestDataSeq(void)
{
    uint64_t uSeq = 0;
    int const vrc = lock();
    if (RT_SUCCESS(vrc))
    {
        uSeq = ++m_uGuestDataSeq;
        unlock();
    }
    else
        AssertMsgFailed(("Incrementing guest data sequence counter failed with %Rrc\n", vrc));
    return uSeq;
}

/**
 * Gets the current host data sequence counter.
 *
 * The returned value can be passed to i_isHostDataSeqCurrent() later to check whether the host data
 * observed by the caller is still current.
 *
 * @returns Current host data sequence counter, or 0 if the counter cannot be read.
 */
uint64_t GuestShCl::i_getHostDataSeq(void)
{
    uint64_t uSeq = 0;
    int const vrc = lock();
    if (RT_SUCCESS(vrc))
    {
        uSeq = m_uHostDataSeq;
        unlock();
    }
    else
        AssertMsgFailed(("Getting host data sequence counter failed with %Rrc\n", vrc));
    return uSeq;
}


/**
 * Checks whether a previously read host data sequence counter is still current.
 *
 * @returns true if \a uSeq matches the current host data sequence counter, false otherwise.
 * @param   uSeq                Host data sequence counter value to check.
 */
bool GuestShCl::i_isHostDataSeqCurrent(uint64_t uSeq)
{
    bool fIsCurrent = false;
    int const vrc = lock();
    if (RT_SUCCESS(vrc))
    {
        fIsCurrent = i_isHostDataSeqCurrentLocked(uSeq);
        unlock();
    }
    else
        AssertMsgFailed(("Checking host data sequence counter failed with %Rrc\n", vrc));
    return fIsCurrent;
}


/**
 * Checks whether a previously read host data sequence counter is still current while the caller owns the object lock.
 *
 * @returns true if \a uSeq matches the current host data sequence counter, false otherwise.
 * @param   uSeq                Host data sequence counter value to check.
 */
bool GuestShCl::i_isHostDataSeqCurrentLocked(uint64_t uSeq)
{
    return m_uHostDataSeq == uSeq;
}


/**
 * Gets the current guest data sequence counter.
 *
 * The returned value can be passed to i_isGuestDataSeqCurrent() later to check whether the guest data
 * observed by the caller is still current.
 *
 * @returns Current guest data sequence counter, or 0 if the counter cannot be read.
 */
uint64_t GuestShCl::i_getGuestDataSeq(void)
{
    uint64_t uSeq = 0;
    int const vrc = lock();
    if (RT_SUCCESS(vrc))
    {
        uSeq = m_uGuestDataSeq;
        unlock();
    }
    else
        AssertMsgFailed(("Getting guest data sequence counter failed with %Rrc\n", vrc));
    return uSeq;
}


/**
 * Checks whether a previously read guest data sequence counter is still current.
 *
 * @returns true if \a uSeq matches the current guest data sequence counter, false otherwise.
 * @param   uSeq                Guest data sequence counter value to check.
 */
bool GuestShCl::i_isGuestDataSeqCurrent(uint64_t uSeq)
{
    bool fIsCurrent = false;
    int const vrc = lock();
    if (RT_SUCCESS(vrc))
    {
        fIsCurrent = m_uGuestDataSeq == uSeq;
        unlock();
    }
    else
        AssertMsgFailed(("Checking guest data sequence counter failed with %Rrc\n", vrc));
    return fIsCurrent;
}


/**
 * Starts a guest data read that will use the active service client outside m_CritSect.
 *
 * @returns VBox status code.
 * @param   ppClient        Where to return the active client.
 */
int GuestShCl::i_beginGuestRead(PSHCLCLIENT *ppClient)
{
    AssertPtrReturn(ppClient, VERR_INVALID_POINTER);
    *ppClient = NULL;

    int vrc = lock();
    if (RT_FAILURE(vrc))
        return vrc;

    if (   m_pClient
        && !m_fGuestReadsBlocked)
    {
        *ppClient = m_pClient;
        if (m_cGuestReads++ == 0)
            RTSemEventMultiReset(m_hGuestReadsDone);
        vrc = VINF_SUCCESS;
    }
    else
        vrc = VERR_SHCLPB_NO_DATA;

    unlock();
    return vrc;
}


/** Ends a guest data read started by i_beginGuestRead(). */
void GuestShCl::i_endGuestRead(void)
{
    int const vrc = lock();
    if (RT_SUCCESS(vrc))
    {
        Assert(m_cGuestReads > 0);
        if (m_cGuestReads > 0 && --m_cGuestReads == 0)
            RTSemEventMultiSignal(m_hGuestReadsDone);
        unlock();
    }
}


/** Waits until all guest data reads using the active client have completed. */
void GuestShCl::i_waitForGuestReads(void)
{
    for (;;)
    {
        int vrc = lock();
        if (RT_FAILURE(vrc))
            return;
        bool const fDone = m_cGuestReads == 0;
        unlock();
        if (fDone)
            return;
        vrc = RTSemEventMultiWait(m_hGuestReadsDone, RT_INDEFINITE_WAIT);
        AssertRC(vrc);
        if (RT_FAILURE(vrc))
            return;
    }
}


/**
 * Registers a Shared Clipboard service extension.
 *
 * @returns VBox status code.
 * @param   pfnExtension        Service extension to register.
 * @param   pvExtension         User-supplied data pointer. Optional.
 */
int GuestShCl::RegisterServiceExtension(PFNHGCMSVCEXT pfnExtension, void *pvExtension)
{
    AssertPtrReturn(pfnExtension, VERR_INVALID_POINTER);
    /* pvExtension is optional. */

    lock();

    LogFlowFunc(("m_pfnExtCallback=%p\n", this->m_pfnExtCallback));

    PSHCLSVCEXT pExt = &this->m_SvcExtVRDP; /* Currently we only have one extension only. */

    Assert(pExt->pfnExt == NULL);

    pExt->pfnExt         = pfnExtension;
    pExt->pvExt          = pvExtension;
    pExt->pfnExtCallback = this->m_pfnExtCallback; /* Assign callback function. Optional and can be NULL. */

    if (pExt->pfnExtCallback)
    {
        /* Make sure to also give the extension the ability to use the callback. */
        SHCLEXTPARMS parms;
        RT_ZERO(parms);

        parms.u.SetCallback.pfnCallback = pExt->pfnExtCallback;

        /* ignore rc, callback is optional */ pExt->pfnExt(pExt->pvExt,
                                                           VBOX_CLIPBOARD_EXT_FN_SET_CALLBACK, &parms, sizeof(parms));
    }

    unlock();

    return VINF_SUCCESS;
}

/**
 * Unregisters a Shared Clipboard service extension.
 *
 * @returns VBox status code.
 * @param   pfnExtension        Service extension to unregister.
 */
int GuestShCl::UnregisterServiceExtension(PFNHGCMSVCEXT pfnExtension)
{
    AssertPtrReturn(pfnExtension, VERR_INVALID_POINTER);

    lock();

    PSHCLSVCEXT pExt = &this->m_SvcExtVRDP; /* Currently we only have one extension only. */

    AssertReturnStmt(pExt->pfnExt == pfnExtension, unlock(), VERR_INVALID_PARAMETER);
    AssertPtr(pExt->pfnExt);

    /* Unregister the callback (setting to NULL). */
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    /* ignore rc, callback is optional */ pExt->pfnExt(pExt->pvExt,
                                                       VBOX_CLIPBOARD_EXT_FN_SET_CALLBACK, &parms, sizeof(parms));

    RT_BZERO(pExt, sizeof(SHCLSVCEXT));

    unlock();

    return VINF_SUCCESS;
}

/**
 * Sends a (blocking) message to the host side of the host service.
 *
 * @returns VBox status code.
 * @param   u32Function         HGCM message ID to send.
 * @param   cParms              Number of parameters to send.
 * @param   paParms             Array of parameters to send. Must match \c cParms.
 */
int GuestShCl::HostCall(uint32_t u32Function, uint32_t cParms, PVBOXHGCMSVCPARM paParms) const
{
    /* Forward the information to the VMM device. */
    AssertPtr(m_pConsole);
    VMMDev *pVMMDev = m_pConsole->i_getVMMDev();
    if (!pVMMDev)
        return VERR_COM_OBJECT_NOT_FOUND;

    return pVMMDev->hgcmHostCall("VBoxSharedClipboard", u32Function, cParms, paParms);
}

/**
 * Reads clipboard data from the active guest clipboard client.
 *
 * @returns VBox status code.
 * @param   uFormat     Format to request from the guest.
 * @param   ppvData     Where to return the allocated data buffer.
 * @param   pcbData     Where to return the data size.
 */
int GuestShCl::ReadDataFromGuest(SHCLFORMAT uFormat, void **ppvData, uint32_t *pcbData)
{
    AssertPtrReturn(ppvData, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbData, VERR_INVALID_POINTER);
    *ppvData = NULL;
    *pcbData = 0;

    PSHCLCLIENT pClient = NULL;
    int vrc = i_beginGuestRead(&pClient);
    if (RT_FAILURE(vrc))
        return vrc;

    /* Do not hold m_CritSect while waiting for the guest reply: the reply callback
     * validates the active client under the same lock before signalling the event. */
    vrc = ShClSvcReadDataFromGuest(pClient, uFormat, ppvData, pcbData);

    i_endGuestRead();
    return vrc;
}

/**
 * Reads clipboard data from the native host clipboard backend.
 *
 * @returns VBox status code.
 * @param   uFormat     Format to request from the host backend.
 * @param   pvData      Destination buffer.
 * @param   cbData      Size of the destination buffer.
 * @param   pcbActual   Where to return the required data size.
 */
int GuestShCl::ReadDataFromHost(SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    AssertReturn(uFormat != VBOX_SHCL_FMT_NONE, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbActual, VERR_INVALID_POINTER);
    *pcbActual = 0;

    SHCLCLIENTCMDCTX cmdCtx;
    RT_ZERO(cmdCtx);

    PSHCLCLIENT pClient = NULL;
    int vrc = i_beginGuestRead(&pClient);
    if (RT_FAILURE(vrc))
        return vrc;

    if (pClient->pBackend)
        vrc = ShClBackendReadData(pClient->pBackend, pClient, &cmdCtx, uFormat, pvData, cbData, pcbActual);
    else
        vrc = VERR_SHCLPB_NO_DATA;

    i_endGuestRead();
    return vrc;
}

/**
 * Reports guest clipboard formats to the native host clipboard backend.
 *
 * @returns VBox status code.
 * @param   fFormats    Formats to report to the host.
 */
int GuestShCl::ReportFormatsToHost(SHCLFORMATS fFormats)
{
    int vrc = lock();
    if (RT_FAILURE(vrc))
        return vrc;

    ++m_uGuestDataSeq;
    unlock();

    PSHCLCLIENT pClient = NULL;
    vrc = i_beginGuestRead(&pClient);
    if (RT_FAILURE(vrc))
        return vrc == VERR_SHCLPB_NO_DATA ? VINF_SUCCESS : vrc;

    if (pClient->pBackend)
    {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        fFormats = shClSvcHandleFormats(false /* fHostToGuest */, pClient, fFormats);
#endif
        vrc = ShClBackendReportFormats(pClient->pBackend, pClient, fFormats);
    }
    else
        vrc = VINF_SUCCESS;

    i_endGuestRead();
    return vrc;
}

/**
 * Writes guest clipboard data to the native host clipboard backend.
 *
 * @returns VBox status code.
 * @param   uFormat     Format of the data buffer.
 * @param   pvData      Data buffer. Optional when @a cbData is zero.
 * @param   cbData      Size of the data buffer in bytes.
 */
int GuestShCl::WriteDataToHost(SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    AssertReturn(uFormat != VBOX_SHCL_FMT_NONE, VERR_INVALID_PARAMETER);
    if (cbData)
        AssertPtrReturn(pvData, VERR_INVALID_POINTER);

    SHCLCLIENTCMDCTX cmdCtx;
    RT_ZERO(cmdCtx);

    PSHCLCLIENT pClient = NULL;
    int vrc = i_beginGuestRead(&pClient);
    if (RT_FAILURE(vrc))
        return vrc == VERR_SHCLPB_NO_DATA ? VINF_SUCCESS : vrc;

    if (pClient->pBackend)
        vrc = ShClBackendWriteData(pClient->pBackend, pClient, &cmdCtx, uFormat, pvData, cbData);
    else
        vrc = VINF_SUCCESS;

    i_endGuestRead();
    return vrc;
}

/**
 * Reports host clipboard formats to the active guest clipboard client.
 *
 * @returns VBox status code.
 * @param   fFormats    Formats to report to the guest.
 */
int GuestShCl::ReportFormatsToGuest(SHCLFORMATS fFormats)
{
    int vrc = lock();
    if (RT_FAILURE(vrc))
        return vrc;

    i_incHostDataSeqLocked();

    PSHCLCLIENT pClient = m_pClient;
    if (   pClient
        && pClient->pBackend)
    {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        fFormats = shClSvcHandleFormats(true /* fHostToGuest */, pClient, fFormats);
#endif
        vrc = ShClBackendReportFormatsToGuest(pClient->pBackend, pClient, fFormats);
    }
    else
        vrc = VINF_SUCCESS;

    unlock();
    return vrc;
}

/**
 * Reports clipboard formats to the guest via the service backend and mirrors
 * successful reports to the console clipboard event source.
 *
 * @returns VBox status code.
 * @param   pClient     Clipboard client to report to.
 * @param   fFormats    Formats to report to the guest.
 * @param   enmSource   Source of the format report.
 */
int GuestShCl::ReportFormatsToGuest(PSHCLCLIENT pClient, SHCLFORMATS fFormats, SHCLSOURCE enmSource)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    ClipboardSource_T enmClipboardSource = ClipboardSource_Custom;
    switch (enmSource)
    {
        case SHCLSOURCE_LOCAL:
            enmClipboardSource = ClipboardSource_Host;
            break;

        case SHCLSOURCE_REMOTE:
            enmClipboardSource = ClipboardSource_Guest;
            break;

        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    /* Reuse the guest-read lifetime guard to keep the weak service client valid
     * while the platform backend reports the formats. */
    PSHCLCLIENT pActiveClient = NULL;
    int vrc = i_beginGuestRead(&pActiveClient);
    if (RT_FAILURE(vrc))
        return vrc;
    if (pClient != pActiveClient)
    {
        i_endGuestRead();
        return VERR_SHCLPB_NO_DATA;
    }

    if (enmSource == SHCLSOURCE_LOCAL)
        i_incHostDataSeq();
    else
        i_incGuestDataSeq();

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    fFormats = shClSvcHandleFormats(true /* fHostToGuest */, pClient, fFormats);
#endif

    if (pClient->pBackend)
        vrc = ShClBackendReportFormatsToGuest(pClient->pBackend, pClient, fFormats);
    else
        vrc = VINF_SUCCESS;

    i_endGuestRead();

    if (RT_SUCCESS(vrc))
    {
        AssertPtr(m_pConsole->i_getClipboard());
        if (m_pConsole->i_getClipboard())
            m_pConsole->i_getClipboard()->i_reportFormats(VBOX_SHCL_MAIN_CLIENT_NONE,
                                                          fFormats, enmClipboardSource, true /* fForceNotify */);
    }
    return vrc;
}



/**
 * Reports an error by setting the error info and also informs subscribed listeners.
 *
 * @returns VBox status code.
 * @param   pcszId              ID (name) of the clipboard. Can be NULL if not being used.
 * @param   vrc                 Result code (IPRT-style) to report.
 * @param   pcszMsgFmt          Error message to report.
 * @param   ...                 Format string for \a pcszMsgFmt.
 */
int GuestShCl::ReportError(const char *pcszId, int vrc, const char *pcszMsgFmt, ...)
{
    /* pcszId can be NULL. */
    AssertReturn(pcszMsgFmt && *pcszMsgFmt != '\0', E_INVALIDARG);

    va_list va;
    va_start(va, pcszMsgFmt);

    Utf8Str strMsg;
    int const vrc2 = strMsg.printfVNoThrow(pcszMsgFmt, va);
    if (RT_FAILURE(vrc2))
    {
        va_end(va);
        return vrc2;
    }

    va_end(va);

    if (pcszId)
        LogRel(("Shared Clipboard (%s): %s (%Rrc)\n", pcszId, strMsg.c_str(), vrc));
    else
        LogRel(("Shared Clipboard: %s (%Rrc)\n", strMsg.c_str(), vrc));

    m_pConsole->i_onClipboardError(pcszId, strMsg.c_str(), vrc);

    return VINF_SUCCESS;
}


/**
 * Static main dispatcher function to handle callbacks from the Shared Clipboard host service.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if the extension did not handle the requested function. This will invoke the regular backend then.
 * @param   pvExtension         Pointer to service extension.
 * @param   u32Function         Callback HGCM message ID.
 * @param   pvParms             Pointer to optional data provided for a particular message. Optional.
 * @param   cbParms             Size (in bytes) of \a pvParms.
 */
/* static */
DECLCALLBACK(int) GuestShCl::s_HgcmDispatcher(void *pvExtension, uint32_t u32Function,
                                               void *pvParms, uint32_t cbParms)
{
    LogFlowFunc(("pvExtension=%p, u32Function=%RU32, pvParms=%p, cbParms=%RU32\n",
                 pvExtension, u32Function, pvParms, cbParms));

    GuestShCl *pThis = reinterpret_cast<GuestShCl*>(pvExtension);
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    int vrc = pThis->i_validateSvcExtParms(u32Function, pvParms, cbParms);
    if (RT_FAILURE(vrc))
    {
        LogFlowFuncLeaveRC(vrc);
        return vrc;
    }

    PSHCLEXTPARMS pParms = (PSHCLEXTPARMS)pvParms; /* pParms might be NULL for unknown messages. */
    vrc = VERR_NOT_SUPPORTED;

    switch (u32Function)
    {
        case VBOX_CLIPBOARD_EXT_FN_SET_CALLBACK:
            vrc = pThis->i_handleSvcExtSetCallback(pParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST:
            vrc = pThis->i_handleSvcExtReportFormatsToHost(pParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_GUEST:
            vrc = pThis->i_handleSvcExtReportFormatsToGuest(pParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_DATA_READ:
            vrc = pThis->i_handleSvcExtDataRead(pParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_DATA_READ_VRDE:
            vrc = pThis->i_handleSvcExtDataReadVrde(pParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_DATA_WRITE:
            vrc = pThis->i_handleSvcExtDataWrite(pParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT:
            vrc = pThis->i_handleSvcExtBackendInit(pParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY:
            vrc = pThis->i_handleSvcExtBackendDestroy(pParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT:
            vrc = pThis->i_handleSvcExtBackendConnect(pParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT:
            vrc = pThis->i_handleSvcExtBackendDisconnect(pParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC:
            vrc = pThis->i_handleSvcExtBackendSync(pParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_ERROR:
            vrc = pThis->i_handleSvcExtError(pParms);
            break;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER:
            vrc = pThis->i_handleSvcExtFileTransfer(pParms, pvParms, cbParms);
            break;
#endif

        default:
            vrc = pThis->i_forwardToSvcExt(u32Function, pvParms, cbParms);
            break;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc; /* Goes back to host service. */
}

#endif /* VBOX_WITH_SHARED_CLIPBOARD */
