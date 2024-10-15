/* $Id: GuestProcessImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest process handling implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_GuestProcessImpl_h
#define MAIN_INCLUDED_GuestProcessImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestCtrlImplPrivate.h"
#include "GuestProcessWrap.h"

#include <iprt/cpp/utils.h>

class Console;
class GuestSession;
class GuestProcessStartTask;

/**
 * Class for handling a guest process.
 */
class ATL_NO_VTABLE GuestProcess :
    public GuestProcessWrap,
    public GuestObject
{
public:
    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(GuestProcess)

    int     init(Console *aConsole, GuestSession *aSession, ULONG aObjectID,
                 const GuestProcessStartupInfo &aProcInfo, const GuestEnvironment *pBaseEnv);
    void    uninit(void);
    HRESULT FinalConstruct(void);
    void    FinalRelease(void);
    /** @}  */

public:
    /** @name Implemented virtual methods from GuestObject.
     * @{ */
    int i_callbackDispatcher(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb);
    int i_onUnregister(void);
    int i_onSessionStatusChange(GuestSessionStatus_T enmSessionStatus);
    /** @}  */

public:
    /** @name Public internal methods.
     * @{ */
    inline int i_checkPID(uint32_t uPID);
    ProcessStatus_T i_getStatus(void);
    int i_readData(uint32_t uHandle, uint32_t uSize, uint32_t uTimeoutMS, void *pvData, size_t cbData, uint32_t *pcbRead, int *pvrcGuest);
    int i_startProcess(uint32_t cMsTimeout, int *pvrcGuest);
    int i_startProcessInner(uint32_t cMsTimeout, AutoWriteLock &rLock, GuestWaitEvent *pEvent, int *pvrcGuest);
    int i_startProcessAsync(void);
    int i_terminateProcess(uint32_t uTimeoutMS, int *pvrcGuest);
    ProcessWaitResult_T i_waitFlagsToResult(uint32_t fWaitFlags);
    int i_waitFor(uint32_t fWaitFlags, ULONG uTimeoutMS, ProcessWaitResult_T &waitResult, int *pvrcGuest);
    int i_waitForInputNotify(GuestWaitEvent *pEvent, uint32_t uHandle, uint32_t uTimeoutMS, ProcessInputStatus_T *pInputStatus, uint32_t *pcbProcessed);
    int i_waitForOutput(GuestWaitEvent *pEvent, uint32_t uHandle, uint32_t uTimeoutMS, void* pvData, size_t cbData, uint32_t *pcbRead);
    int i_waitForStatusChange(GuestWaitEvent *pEvent, uint32_t uTimeoutMS, ProcessStatus_T *pProcessStatus, int *pvrcGuest);
    int i_writeData(uint32_t uHandle, uint32_t uFlags, void *pvData, size_t cbData, uint32_t uTimeoutMS, uint32_t *puWritten, int *pvrcGuest);
    /** @}  */

    /** @name Static internal methods.
     * @{ */
    static Utf8Str i_guestErrorToString(int vrcGuest, const char *pcszWhat);
    static Utf8Str i_statusToString(ProcessStatus_T enmStatus);
    static bool i_isGuestError(int guestRc);
    static ProcessWaitResult_T i_waitFlagsToResultEx(uint32_t fWaitFlags, ProcessStatus_T oldStatus, ProcessStatus_T newStatus, uint32_t uProcFlags, uint32_t uProtocol);
#if 0 /* unused */
    static bool i_waitResultImpliesEx(ProcessWaitResult_T waitResult, ProcessStatus_T procStatus, uint32_t uProtocol);
#endif
    /** @}  */

protected:
    /** @name Protected internal methods.
     * @{ */
    inline bool i_isAlive(void);
    inline bool i_hasEnded(void);
    int i_onGuestDisconnected(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData);
    int i_onProcessInputStatus(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData);
    int i_onProcessNotifyIO(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData);
    int i_onProcessStatusChange(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData);
    int i_onProcessOutput(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData);
    int i_prepareExecuteEnv(const char *pszEnv, void **ppvList, ULONG *pcbList, ULONG *pcEnvVars);
    int i_setProcessStatus(ProcessStatus_T procStatus, int vrcProc);
    static int i_startProcessThreadTask(GuestProcessStartTask *pTask);
    /** @}  */

private:
    /** Wrapped @name IProcess properties.
     * @{ */
    HRESULT getArguments(std::vector<com::Utf8Str> &aArguments);
    HRESULT getEnvironment(std::vector<com::Utf8Str> &aEnvironment);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getExecutablePath(com::Utf8Str &aExecutablePath);
    HRESULT getExitCode(LONG *aExitCode);
    HRESULT getName(com::Utf8Str &aName);
    HRESULT getPID(ULONG *aPID);
    HRESULT getStatus(ProcessStatus_T *aStatus);
    /** @}  */

    /** Wrapped @name IProcess methods.
     * @{ */
    HRESULT waitFor(ULONG aWaitFor,
                    ULONG aTimeoutMS,
                    ProcessWaitResult_T *aReason);
    HRESULT waitForArray(const std::vector<ProcessWaitForFlag_T> &aWaitFor,
                         ULONG aTimeoutMS,
                         ProcessWaitResult_T *aReason);
    HRESULT read(ULONG aHandle,
                 ULONG aToRead,
                 ULONG aTimeoutMS,
                 std::vector<BYTE> &aData);
    HRESULT write(ULONG aHandle,
                  ULONG aFlags,
                  const std::vector<BYTE> &aData,
                  ULONG aTimeoutMS,
                  ULONG *aWritten);
    HRESULT writeArray(ULONG aHandle,
                       const std::vector<ProcessInputFlag_T> &aFlags,
                       const std::vector<BYTE> &aData,
                       ULONG aTimeoutMS,
                       ULONG *aWritten);
    HRESULT terminate(void);
    /** @}  */

    /**
     * This can safely be used without holding any locks.
     * An AutoCaller suffices to prevent it being destroy while in use and
     * internally there is a lock providing the necessary serialization.
     */
    const ComObjPtr<EventSource> mEventSource;

    struct Data
    {
        /** The process startup information. */
        GuestProcessStartupInfo  mProcess;
        /** Reference to the immutable session base environment. NULL if the
         * environment feature isn't supported.
         * @remarks If there is proof that the uninit order of GuestSession and
         *          this class is what GuestObjectBase claims, then this isn't
         *          strictly necessary. */
        GuestEnvironment const  *mpSessionBaseEnv;
        /** Exit code if process has been terminated. */
        LONG                     mExitCode;
        /** PID reported from the guest.
         *  Note: This is *not* the internal object ID! */
        ULONG                    mPID;
        /** The current process status. */
        ProcessStatus_T          mStatus;
        /** The last returned process status
         *  returned from the guest side. */
        int                      mLastError;

        Data(void) : mpSessionBaseEnv(NULL)
        { }
        ~Data(void)
        {
            if (mpSessionBaseEnv)
            {
                mpSessionBaseEnv->releaseConst();
                mpSessionBaseEnv = NULL;
            }
        }
    } mData;

    friend class GuestProcessStartTask;
};

/**
 * Guest process tool wait flags.
 */
/** No wait flags specified; wait until process terminates.
 *  The maximum waiting time is set in the process' startup
 *  info. */
#define GUESTPROCESSTOOL_WAIT_FLAG_NONE            0
/** Wait until next stream block from stdout has been
 *  read in completely, then return.
 */
#define GUESTPROCESSTOOL_WAIT_FLAG_STDOUT_BLOCK    RT_BIT(0)

/**
 * Structure for keeping a VBoxService toolbox tool's error info around.
 */
struct GuestProcessToolErrorInfo
{
    /** Return (VBox status) code from the guest side for executing the process tool. */
    int     vrcGuest;
    /** The process tool's returned exit code. */
    int32_t iExitCode;
};

/**
 * Wrapper class for running guest processes.
 */
class GuestProcessWrapper
{
public:
    DECLARE_TRANSLATE_METHODS(GuestProcessWrapper)

    GuestProcessWrapper(void);

    virtual ~GuestProcessWrapper(void);

public:

    int init(GuestSession *pGuestSession, const GuestProcessStartupInfo &startupInfo, bool fAsync, int *pvrcGuest);

    void uninit(void);

    /** Returns the stdout output from the guest process. */
    GuestProcessOutputStream &getStdOut(void) { return mStdOut; }

    /** Returns the stderr output from the guest proces. */
    GuestProcessOutputStream &getStdErr(void) { return mStdErr; }

    bool isRunning(void);

    bool isTerminatedOk(void);

    int getTerminationStatus(int32_t *piExitCode = NULL);

    int terminate(uint32_t uTimeoutMS, int *pvrcGuest);

    int wait(int *pvrcGuest);

public:

#if 0 /* unused */
    virtual int onOutputCallback(uint32_t uHandle, const std::vector<Utf8Str> &vecData);
#endif

    virtual int onOutputCallback(uint32_t uHandle, const BYTE *pbData, size_t cbData);

    virtual int onInputCallback(uint32_t uHandle, std::vector<Utf8Str> &vecData);

protected:

    /** Pointer to session this toolbox object is bound to. */
    ComObjPtr<GuestSession>     pSession;
    /** Pointer to process object this object is bound to. */
    ComObjPtr<GuestProcess>     pProcess;
    /** The process startup info. */
    GuestProcessStartupInfo     mStartupInfo;
    /** Stream object for handling stdout data. */
    GuestProcessOutputStream    mStdOut;
    /** Stream object for handling stderr data. */
    GuestProcessOutputStream    mStdErr;
};

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
/**
 * Internal class for handling the BusyBox-like tools built into VBoxService
 * on the guest side. It's also called the VBoxService Toolbox (tm).
 *
 * Those initially were necessary to guarantee execution of commands (like "ls", "cat")
 * under the behalf of a certain guest user.
 *
 * This class essentially helps to wrap all the gory details like process creation,
 * information extraction and maintaining the overall status.
 *
 * Note! When implementing new functionality / commands, do *not* use this approach anymore!
 *       This class has to be kept to guarantee backwards-compatibility.
 *
 * Deprecated, do not use anymore.
 */
class GuestProcessToolbox : public GuestProcessWrapper
{
public:
    DECLARE_TRANSLATE_METHODS(GuestProcessToolbox)

    GuestProcessToolbox(void);

    virtual ~GuestProcessToolbox(void);

public:

    int wait(uint32_t fToolWaitFlags, int *pvrcGuest);

    int waitEx(uint32_t fToolWaitFlags, GuestToolboxStreamBlock *strmBlockOut, int *pvrcGuest);

    int getCurrentBlock(uint32_t uHandle, GuestToolboxStreamBlock &strmBlock);

    int getRc(void) const;

    /** Returns the stdout output from the guest process tool. */
    GuestToolboxStream &getStdOut(void) { return mStdOut; }

    /** Returns the stderr output from the guest process tool. */
    GuestToolboxStream &getStdErr(void) { return mStdErr; }

public:

    /** Wrapped @name Static run methods.
     * @note Do not call anything 'run' here, it's way too common.
     * @{ */
    static int runTool(GuestSession *pGuestSession, const GuestProcessStartupInfo &startupInfo, int *pvrcGuest,
                       GuestCtrlStreamObjects *pLstStdOutObjPairs = NULL, uint32_t cStdOutObjectsToRead = 1);

    static int runToolWithErrorInfo(GuestSession *pGuestSession, const GuestProcessStartupInfo &startupInfo,
                                    GuestProcessToolErrorInfo &errorInfo,
                                    GuestCtrlStreamObjects *pLstStdOutObjPairs = NULL, uint32_t cStdOutObjectsToRead = 1);
    /** @}  */

    /** Wrapped @name Static exit code conversion methods.
     * @{ */
    static int exitCodeToRc(const GuestProcessStartupInfo &startupInfo, int32_t iExitCode);
    static int exitCodeToRc(const char *pszTool, int32_t iExitCode);
    /** @}  */

    /** Wrapped @name Static guest error conversion methods.
     * @{ */
    static Utf8Str guestErrorToString(const char *pszTool, const GuestErrorInfo& guestErrorInfo);
    /** @}  */

protected:

    /** Stream object for handling the toolbox' stdout data. */
    GuestToolboxStream          mStdOut;
    /** Stream object for handling the toolbox' stderr data. */
    GuestToolboxStream          mStdErr;
};
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

#endif /* !MAIN_INCLUDED_GuestProcessImpl_h */

