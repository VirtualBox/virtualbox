/* $Id: GuestShClPrivate.h 115105 2026-08-24 16:57:58Z andreas.loeffler@oracle.com $ */
/** @file
 * Private Shared Clipboard code for the Main API.
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

#ifndef MAIN_INCLUDED_GuestShClPrivate_h
#define MAIN_INCLUDED_GuestShClPrivate_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/HostServices/VBoxClipboardExt.h>
#include <VBox/hgcmsvc.h>
#include <iprt/critsect.h>
#include <iprt/once.h>

/**
 * Forward prototype declarations.
 */
class Console;
class GuestShClConn;

/**
 * Private singleton class for managing the Shared Clipboard implementation within Main.
 *
 * Can't be instanciated directly, only via the factory pattern via GuestShCl::CreateInstance().
 */
class GuestShCl
{
public:

    /**
     * Creates the Singleton GuestShCl object.
     *
     * @returns Newly created Singleton object, or NULL on failure.
     * @param   pConsole        Pointer to parent console.
     */
    static GuestShCl *CreateInstance(Console *pConsole);

    /**
     * Destroys the Singleton GuestShCl object.
     */
    static void DestroyInstance(void);

    /**
     * Publishes the singleton to asynchronous external callers.
     *
     * @returns VBox status code.
     */
    static int EnableExternalCalls(void);

    /**
     * Withdraws the singleton from asynchronous external callers and waits for
     * all calls which previously acquired it to return.
     */
    static void DisableExternalCalls(void);

    /**
     * Acquires the singleton for an asynchronous external call.
     *
     * The returned reference must be released with Release() on the
     * same thread.
     *
     * @returns Acquired singleton, or NULL if external calls are disabled.
     */
    static GuestShCl *Acquire(void);

    /**
     * Releases a singleton reference returned by Acquire().
     *
     * @thread  The thread which called Acquire().
     */
    static void Release(void);

    /**
     * Returns the Singleton GuestShCl object.
     *
     * @returns Pointer to Singleton GuestShCl object.
     */
    static inline GuestShCl *GetInst(void)
    {
        AssertPtr(GuestShCl::s_pInstance);
        return GuestShCl::s_pInstance;
    }

protected:

    /** Constructor; will throw vrc on failure. */
    GuestShCl(Console *pConsole);
    virtual ~GuestShCl(void);

    /** @name Object state helpers.
     * @{ */
    void uninit(void);
    int lock(void);
    int unlock(void);
    uint64_t i_incHostDataSeq(void);
    uint64_t i_incHostDataSeqLocked(void);
    uint64_t i_incGuestDataSeq(void);
    uint64_t i_getHostDataSeq(void);
    bool i_isHostDataSeqCurrent(uint64_t uSeq);
    bool i_isHostDataSeqCurrentLocked(uint64_t uSeq);
    uint64_t i_getGuestDataSeq(void);
    bool i_isGuestDataSeqCurrent(uint64_t uSeq);
    int i_reportRemoteFormatsToGuestNow(SHCLFORMATS fFormats);
    /** @}  */

public:

    /** @name Public helper functions.
     * @{ */
    int HostCall(uint32_t u32Function, uint32_t cParms, PVBOXHGCMSVCPARM paParms) const;
    int ReadDataFromGuest(SHCLFORMAT uFormat, void **ppvData, uint32_t *pcbData);
    int ReadDataFromHost(SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual);
    int ReportFormatsToHost(SHCLFORMATS fFormats);
    int WriteDataToHost(SHCLFORMAT uFormat, void *pvData, uint32_t cbData);
    int ReportFormatsToGuest(SHCLFORMATS fFormats);
    bool IsNativeBackendActive(void);
    int VrdeEnable(bool fEnable);
    int ReportRemoteFormatsToGuest(SHCLFORMATS fFormats);
    int ReportFormatsToGuest(GuestShClConn *pConn, SHCLFORMATS fFormats, SHCLSOURCE enmSource);
    int ReportError(const char *pcszId, int vrc, const char *pcszMsgFmt, ...);
    /** @}  */

public:

    /** @name Static low-level HGCM callback handler.
     * @{ */
    static DECLCALLBACK(int) s_HgcmDispatcher(void *pvExtension, uint32_t u32Function, void *pvParms, uint32_t cbParms);
    /** @}  */

protected:

    int i_svcExtParmsValidate(uint32_t u32Function, void *pvParms, uint32_t cbParms);

protected:

    /** @name Service extension callback handlers.
     * @{ */
    int i_svcExtReportFormatsToHostCallback(PSHCLEXTPARMS pParms);
    int i_svcExtDataReadCallback(PSHCLEXTPARMS pParms);
    int i_svcExtDataWriteCallback(PSHCLEXTPARMS pParms);
    int i_svcExtBackendInitCallback(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms);
    int i_svcExtBackendDestroyCallback(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms);
    int i_svcExtBackendConnectCallback(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms);
    int i_svcExtBackendDisconnectCallback(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms);
    int i_svcExtBackendSyncCallback(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms);
    int i_svcExtErrorCallback(PSHCLEXTPARMS pParms);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    int i_svcExtTransferGetCallbacksCallback(PSHCLEXTPARMS pParms);
    int i_svcExtFileTransferCallback(PSHCLEXTPARMS pParms);
    int i_svcExtTransferProgressCallback(PSHCLEXTPARMS pParms);
    int i_svcExtTransferResetCallback(PSHCLEXTPARMS pParms);
#endif
    /** @}  */

    /** @name Singleton properties.
     * @{ */
    /** Pointer to console.
     *  Does not need any locking, as this object is a member of the console itself. */
    Console                    *m_pConsole;
    /** Critical section to serialize access. */
    RTCRITSECT                  m_CritSect;
    /** Serializes remote format publication and the handoff after a remote data read. */
    RTCRITSECT                  m_RemoteFormatsCritSect;
    /** Main-owned connection encapsulating the service endpoint and native backend context. */
    GuestShClConn              *m_pConn;
    /** Whether VRDE currently owns the host clipboard route. */
    volatile bool              m_fVrdeEnabled;
    /** Whether Main is synchronously reading from the remote clipboard provider. */
    bool                        m_fRemoteDataReadActive;
    /** Whether a remote format announcement arrived during that read. */
    bool                        m_fRemoteFormatsPending;
    /** Latest remote formats deferred until the active read completes. */
    SHCLFORMATS                 m_fPendingRemoteFormats;
    /** Host data sequence counter, protected by m_CritSect. */
    uint64_t                    m_uHostDataSeq;
    /** Guest data sequence counter, protected by m_CritSect. */
    uint64_t                    m_uGuestDataSeq;
    /** @}  */

private:

    /** @callback_method_impl{FNRTONCE} */
    static DECLCALLBACK(int32_t) s_initInstanceLock(void *pvUser);

    /** Init-once state for the process-lifetime singleton publication lock. */
    static RTONCE                s_InstanceOnce;
    /** Serializes singleton publication and protects external references. */
    static RTCRITSECTRW          s_InstanceLock;
    /** Static pointer to singleton instance. */
    static GuestShCl           *s_pInstance;
    /** Singleton published to asynchronous external callers. */
    static GuestShCl * volatile s_pExternalInstance;
};

/** Access to the GuestShCl's singleton instance. */
#define GuestShClInst() GuestShCl::GetInst()

#endif /* !MAIN_INCLUDED_GuestShClPrivate_h */
