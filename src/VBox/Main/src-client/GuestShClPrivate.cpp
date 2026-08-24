/* $Id: GuestShClPrivate.cpp 115106 2026-08-24 17:32:24Z andreas.loeffler@oracle.com $ */
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
# include "GuestShClConn.h"
# include "ProgressImpl.h"

# include <iprt/asm.h>
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



/** Init-once state for the singleton publication lock. */
RTONCE GuestShCl::s_InstanceOnce = RTONCE_INITIALIZER;
/** Process-lifetime singleton publication lock. */
RTCRITSECTRW GuestShCl::s_InstanceLock;
/** Static (Singleton) instance of the Shared Clipboard management object. */
GuestShCl *GuestShCl::s_pInstance = NULL;
/** Singleton published to asynchronous external callers. */
GuestShCl * volatile GuestShCl::s_pExternalInstance = NULL;


/** @callback_method_impl{FNRTONCE} */
DECLCALLBACK(int32_t) GuestShCl::s_initInstanceLock(void *pvUser)
{
    RT_NOREF(pvUser);
    return RTCritSectRwInit(&s_InstanceLock);
}


/**
 * Creates the singleton without publishing it to asynchronous external calls.
 *
 * @returns Newly created singleton on success.
 * @retval  NULL if @a pConsole is invalid, synchronization initialization or
 *          locking fails, or a singleton already exists.
 * @throws  VBox status code if constructing the singleton fails.
 * @param   pConsole            Pointer to the parent console.
 */
GuestShCl *GuestShCl::CreateInstance(Console *pConsole)
{
    AssertPtrReturn(pConsole, NULL);

    int vrc = RTOnce(&s_InstanceOnce, s_initInstanceLock, NULL);
    AssertRCReturn(vrc, NULL);

    GuestShCl *pInstance = new GuestShCl(pConsole);
    vrc = RTCritSectRwEnterExcl(&s_InstanceLock);
    if (RT_FAILURE(vrc))
    {
        delete pInstance;
        AssertRC(vrc);
        return NULL;
    }

    Assert(s_pInstance == NULL);
    if (s_pInstance == NULL)
        s_pInstance = pInstance;
    else
    {
        RTCritSectRwLeaveExcl(&s_InstanceLock);
        delete pInstance;
        return NULL;
    }

    vrc = RTCritSectRwLeaveExcl(&s_InstanceLock);
    AssertRC(vrc);
    return pInstance;
}


/** Destroys the singleton after draining asynchronous external calls. */
void GuestShCl::DestroyInstance(void)
{
    int vrc = RTOnce(&s_InstanceOnce, s_initInstanceLock, NULL);
    AssertRCReturnVoid(vrc);

    ASMAtomicWriteNullPtr(&s_pExternalInstance);
    vrc = RTCritSectRwEnterExcl(&s_InstanceLock);
    AssertRCReturnVoid(vrc);
    GuestShCl *pInstance = s_pInstance;
    ASMAtomicWriteNullPtr(&s_pExternalInstance);
    s_pInstance = NULL;
    vrc = RTCritSectRwLeaveExcl(&s_InstanceLock);
    AssertRC(vrc);

    delete pInstance;
}


/**
 * Publishes the singleton to asynchronous external callers.
 *
 * @retval  VERR_WRONG_ORDER if no singleton has been created.
 */
int GuestShCl::EnableExternalCalls(void)
{
    int vrc = RTOnce(&s_InstanceOnce, s_initInstanceLock, NULL);
    if (RT_FAILURE(vrc))
        return vrc;

    vrc = RTCritSectRwEnterExcl(&s_InstanceLock);
    if (RT_FAILURE(vrc))
        return vrc;
    if (s_pInstance)
        ASMAtomicWritePtr(&s_pExternalInstance, s_pInstance);
    else
        vrc = VERR_WRONG_ORDER;
    int const vrcLeave = RTCritSectRwLeaveExcl(&s_InstanceLock);
    AssertRC(vrcLeave);
    return vrc;
}


/** Withdraws the singleton and drains asynchronous external calls. */
void GuestShCl::DisableExternalCalls(void)
{
    int vrc = RTOnce(&s_InstanceOnce, s_initInstanceLock, NULL);
    AssertRCReturnVoid(vrc);

    ASMAtomicWriteNullPtr(&s_pExternalInstance);
    vrc = RTCritSectRwEnterExcl(&s_InstanceLock);
    AssertRCReturnVoid(vrc);
    ASMAtomicWriteNullPtr(&s_pExternalInstance);
    vrc = RTCritSectRwLeaveExcl(&s_InstanceLock);
    AssertRC(vrc);
}


/**
 * Acquires the singleton for an asynchronous external call.
 *
 * @returns The acquired singleton on success.
 * @retval  NULL if external calls are disabled, the singleton is being
 *          withdrawn, its publication lock is busy, or lock initialization
 *          fails.
 */
GuestShCl *GuestShCl::Acquire(void)
{
    int vrc = RTOnce(&s_InstanceOnce, s_initInstanceLock, NULL);
    AssertRCReturn(vrc, NULL);

    GuestShCl *pInstance = ASMAtomicReadPtrT(&s_pExternalInstance, GuestShCl *);
    if (pInstance)
    {
        vrc = RTCritSectRwTryEnterShared(&s_InstanceLock);
        if (RT_SUCCESS(vrc))
        {
            if (pInstance == ASMAtomicReadPtrT(&s_pExternalInstance, GuestShCl *))
                return pInstance;
            vrc = RTCritSectRwLeaveShared(&s_InstanceLock);
            AssertRC(vrc);
        }
        else
            Assert(vrc == VERR_SEM_BUSY);
    }
    return NULL;
}


/** Releases an asynchronous external-call reference. */
void GuestShCl::Release(void)
{
    int const vrc = RTCritSectRwLeaveShared(&s_InstanceLock);
    AssertRC(vrc);
}


GuestShCl::GuestShCl(Console *pConsole)
    : m_pConsole(pConsole)
    , m_pConn(NULL)
    , m_fVrdeEnabled(false)
    , m_fRemoteDataReadActive(false)
    , m_fRemoteFormatsPending(false)
    , m_fPendingRemoteFormats(VBOX_SHCL_FMT_NONE)
    , m_uHostDataSeq(0)
    , m_uGuestDataSeq(0)
{
    LogFlowFuncEnter();

    int vrc = RTCritSectInit(&m_CritSect);
    if (RT_FAILURE(vrc))
        throw vrc;

    vrc = RTCritSectInit(&m_RemoteFormatsCritSect);
    if (RT_FAILURE(vrc))
    {
        RTCritSectDelete(&m_CritSect);
        throw vrc;
    }

    try
    {
        m_pConn = new GuestShClConn(this);
    }
    catch (...)
    {
        RTCritSectDelete(&m_RemoteFormatsCritSect);
        RTCritSectDelete(&m_CritSect);
        throw;
    }
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

    if (m_pConn)
    {
        int const vrc = m_pConn->destroyBackend();
        AssertRC(vrc);
        delete m_pConn;
        m_pConn = NULL;
    }

    if (RTCritSectIsInitialized(&m_RemoteFormatsCritSect))
        RTCritSectDelete(&m_RemoteFormatsCritSect);
    if (RTCritSectIsInitialized(&m_CritSect))
        RTCritSectDelete(&m_CritSect);

    m_fVrdeEnabled = false;
    m_fRemoteDataReadActive = false;
    m_fRemoteFormatsPending = false;
    m_fPendingRemoteFormats = VBOX_SHCL_FMT_NONE;
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
 * @retval  VERR_INVALID_POINTER if @a ppvData or @a pcbData is invalid.
 * @retval  VERR_SHCLPB_NO_DATA if the guest clipboard is disconnected or no
 *          data is available.
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

    return m_pConn->i_readDataFromGuest(uFormat, ppvData, pcbData);
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

    return m_pConn->readDataFromBackend(uFormat, pvData, cbData, pcbActual);
}

/**
 * Reports guest clipboard formats to the native host clipboard backend.
 *
 * @retval  VERR_RESOURCE_BUSY if VRDE currently owns the host clipboard route.
 * @param   fFormats    Formats to report to the host.
 */
int GuestShCl::ReportFormatsToHost(SHCLFORMATS fFormats)
{
    if (!IsNativeBackendActive())
        return VERR_RESOURCE_BUSY;

    int vrc = lock();
    if (RT_FAILURE(vrc))
        return vrc;

    ++m_uGuestDataSeq;
    unlock();

    vrc = m_pConn->reportFormatsToBackend(fFormats);
    return vrc == VERR_SHCLPB_NO_DATA ? VINF_SUCCESS : vrc;
}

/**
 * Writes guest clipboard data to the native host clipboard backend.
 *
 * @retval  VERR_INVALID_PARAMETER if @a uFormat is VBOX_SHCL_FMT_NONE.
 * @retval  VERR_INVALID_POINTER if @a pvData is invalid for a non-empty buffer.
 * @retval  VERR_RESOURCE_BUSY if VRDE currently owns the host clipboard route.
 * @param   uFormat     Format of the data buffer.
 * @param   pvData      Data buffer. Optional when @a cbData is zero.
 * @param   cbData      Size of the data buffer in bytes.
 */
int GuestShCl::WriteDataToHost(SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    AssertReturn(uFormat != VBOX_SHCL_FMT_NONE, VERR_INVALID_PARAMETER);
    if (cbData)
        AssertPtrReturn(pvData, VERR_INVALID_POINTER);

    if (!IsNativeBackendActive())
        return VERR_RESOURCE_BUSY;

    int const vrc = m_pConn->writeDataToBackend(uFormat, pvData, cbData);
    return vrc == VERR_SHCLPB_NO_DATA ? VINF_SUCCESS : vrc;
}

/**
 * Reports host clipboard formats to the active guest clipboard client.
 *
 * @retval  VERR_RESOURCE_BUSY if VRDE currently owns the host clipboard route.
 * @param   fFormats    Formats to report to the guest.
 */
int GuestShCl::ReportFormatsToGuest(SHCLFORMATS fFormats)
{
    if (!IsNativeBackendActive())
        return VERR_RESOURCE_BUSY;

    int const vrc = m_pConn->reportFormatsToGuest(fFormats);
    if (vrc == VERR_SHCLPB_NO_DATA || vrc == VINF_NO_CHANGE)
        return VINF_SUCCESS;
    if (RT_SUCCESS(vrc))
        i_incHostDataSeq();
    return vrc;
}

/**
 * Checks whether native clipboard callbacks may use the guest connection.
 *
 * @returns true when the native backend is selected, otherwise false.
 */
bool GuestShCl::IsNativeBackendActive(void)
{
    return !ASMAtomicReadBool(&m_fVrdeEnabled);
}

/**
 * Selects or deselects VRDE as the host clipboard provider.
 *
 * The transition first withdraws the formats owned by the old provider.  When
 * returning to the native backend, it then republishes the native formats.
 *
 * @retval  VINF_NO_CHANGE if @a fEnable already matches the selected route.
 * @param   fEnable             Whether VRDE should own the clipboard route.
 */
int GuestShCl::VrdeEnable(bool fEnable)
{
    int vrc = RTCritSectEnter(&m_RemoteFormatsCritSect);
    AssertRCReturn(vrc, vrc);

    bool const fWasEnabled = ASMAtomicReadBool(&m_fVrdeEnabled);
    if (fWasEnabled == fEnable)
    {
        RTCritSectLeave(&m_RemoteFormatsCritSect);
        return VINF_NO_CHANGE;
    }

    /* Block the native backend before withdrawing its formats. */
    if (fEnable)
        ASMAtomicWriteBool(&m_fVrdeEnabled, true);

    m_fRemoteFormatsPending = false;
    m_fPendingRemoteFormats = VBOX_SHCL_FMT_NONE;
    vrc = i_reportRemoteFormatsToGuestNow(VBOX_SHCL_FMT_NONE);

    /* Keep VRDE selected until its formats have been withdrawn. */
    if (!fEnable)
        ASMAtomicWriteBool(&m_fVrdeEnabled, false);

    int const vrcLeave = RTCritSectLeave(&m_RemoteFormatsCritSect);
    AssertRC(vrcLeave);

    if (fEnable)
        return vrc;

    int vrcSync = m_pConn->syncBackend();
    if (   vrcSync == VERR_INVALID_STATE
        || vrcSync == VERR_SHCLPB_NO_DATA
        || vrcSync == VINF_NO_CHANGE)
        vrcSync = VINF_SUCCESS;
    if (RT_FAILURE(vrc))
        return vrc;
    return vrcSync;
}

/**
 * Reports remote clipboard formats to the active guest clipboard client.
 *
 * @retval  VINF_NO_CHANGE if the native backend became selected before the
 *          remote formats could be reported.
 * @retval  VERR_INVALID_PARAMETER if @a fFormats is invalid.
 * @param   fFormats            Formats reported by the remote clipboard peer.
 */
int GuestShCl::ReportRemoteFormatsToGuest(SHCLFORMATS fFormats)
{
    AssertReturn(ShClFormatsAreValid(fFormats), VERR_INVALID_PARAMETER);
    /* VRDE deliberately handles ordinary clipboard data only. */
    fFormats &= ~VBOX_SHCL_FMT_URI_LIST;

    if (IsNativeBackendActive())
        return VINF_NO_CHANGE;

    int vrc = RTCritSectEnter(&m_RemoteFormatsCritSect);
    AssertRCReturn(vrc, vrc);

    if (IsNativeBackendActive())
    {
        RTCritSectLeave(&m_RemoteFormatsCritSect);
        return VINF_NO_CHANGE;
    }

    if (m_fRemoteDataReadActive)
    {
        m_fRemoteFormatsPending = true;
        m_fPendingRemoteFormats = fFormats;
        RTCritSectLeave(&m_RemoteFormatsCritSect);
        return VINF_SUCCESS;
    }

    vrc = i_reportRemoteFormatsToGuestNow(fFormats);
    int const vrcLeave = RTCritSectLeave(&m_RemoteFormatsCritSect);
    AssertRC(vrcLeave);
    return vrc;
}

/**
 * Reports remote clipboard formats immediately.
 *
 * @retval  VERR_INVALID_PARAMETER if @a fFormats is invalid.
 * @param   fFormats            Remote formats, VBOX_SHCL_FMT_XXX.
 *
 * @note    The caller must serialize remote reports and handle deferral while
 *          a remote-data read is active.
 */
int GuestShCl::i_reportRemoteFormatsToGuestNow(SHCLFORMATS fFormats)
{
    AssertReturn(ShClFormatsAreValid(fFormats), VERR_INVALID_PARAMETER);
    Assert(RTCritSectIsOwner(&m_RemoteFormatsCritSect));

    int const vrc = m_pConn->reportFormatsToGuest(fFormats, &fFormats);
    if (vrc == VERR_SHCLPB_NO_DATA || vrc == VINF_NO_CHANGE)
        return VINF_SUCCESS;
    if (RT_SUCCESS(vrc))
    {
        i_incHostDataSeq();
        Clipboard *pClipboard = m_pConsole->i_getClipboard();
        if (pClipboard)
            pClipboard->i_reportFormats(VBOX_SHCL_MAIN_CLIENT_NONE, fFormats, ClipboardSource_Remote,
                                        true /* fForceNotify */);
    }
    return vrc;
}

/**
 * Reports clipboard formats to the guest via the service backend and mirrors
 * successful reports to the console clipboard event source.
 *
 * @retval  VERR_INVALID_POINTER if @a pConn is invalid.
 * @retval  VERR_INVALID_HANDLE if @a pConn is not this object's connection.
 * @retval  VERR_INVALID_PARAMETER if @a enmSource is invalid.
 * @param   pConn       Connection to report through.
 * @param   fFormats    Formats to report to the guest.
 * @param   enmSource   Source of the format report.
 */
int GuestShCl::ReportFormatsToGuest(GuestShClConn *pConn, SHCLFORMATS fFormats, SHCLSOURCE enmSource)
{
    AssertPtrReturn(pConn, VERR_INVALID_POINTER);
    AssertReturn(pConn == m_pConn, VERR_INVALID_HANDLE);

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

    if (   enmSource == SHCLSOURCE_LOCAL
        && !IsNativeBackendActive())
        return VINF_SUCCESS;

    int const vrc = pConn->reportFormatsToGuest(fFormats, &fFormats);
    if (vrc == VINF_NO_CHANGE)
        return VINF_SUCCESS;
    if (RT_SUCCESS(vrc))
    {
        if (enmSource == SHCLSOURCE_LOCAL)
            i_incHostDataSeq();
        else
            i_incGuestDataSeq();

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

    int vrc = pThis->i_svcExtParmsValidate(u32Function, pvParms, cbParms);
    if (RT_FAILURE(vrc))
    {
        LogFlowFuncLeaveRC(vrc);
        return vrc;
    }

    vrc = VERR_NOT_SUPPORTED;

    switch (u32Function)
    {
        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST:
            vrc = pThis->i_svcExtReportFormatsToHostCallback((PSHCLEXTPARMS)pvParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_DATA_READ:
            vrc = pThis->i_svcExtDataReadCallback((PSHCLEXTPARMS)pvParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_DATA_WRITE:
            vrc = pThis->i_svcExtDataWriteCallback((PSHCLEXTPARMS)pvParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT:
            vrc = pThis->i_svcExtBackendInitCallback((PSHCLEXTPARMS)pvParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY:
            vrc = pThis->i_svcExtBackendDestroyCallback((PSHCLEXTPARMS)pvParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT:
            vrc = pThis->i_svcExtBackendConnectCallback((PSHCLEXTPARMS)pvParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT:
            vrc = pThis->i_svcExtBackendDisconnectCallback((PSHCLEXTPARMS)pvParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC:
            vrc = pThis->i_svcExtBackendSyncCallback((PSHCLEXTPARMS)pvParms, pvParms, cbParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_ERROR:
            vrc = pThis->i_svcExtErrorCallback((PSHCLEXTPARMS)pvParms);
            break;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        case VBOX_CLIPBOARD_EXT_FN_TRANSFER_CALLBACKS:
            vrc = pThis->i_svcExtTransferGetCallbacksCallback((PSHCLEXTPARMS)pvParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER:
            vrc = pThis->i_svcExtFileTransferCallback((PSHCLEXTPARMS)pvParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_PROGRESS:
            vrc = pThis->i_svcExtTransferProgressCallback((PSHCLEXTPARMS)pvParms);
            break;

        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_RESET:
            vrc = pThis->i_svcExtTransferResetCallback((PSHCLEXTPARMS)pvParms);
            break;
#endif

        default:
            vrc = VERR_NOT_SUPPORTED;
            break;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc; /* Goes back to host service. */
}

#endif /* VBOX_WITH_SHARED_CLIPBOARD */
