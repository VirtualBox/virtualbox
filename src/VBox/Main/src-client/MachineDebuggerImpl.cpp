/* $Id: MachineDebuggerImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox IMachineDebugger COM class implementation (VBoxC).
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN_MACHINEDEBUGGER
#include "LoggingNew.h"

#include "MachineDebuggerImpl.h"

#include "Global.h"
#include "ConsoleImpl.h"
#include "ProgressImpl.h"

#include "AutoCaller.h"

#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/tm.h>
#include <VBox/vmm/hm.h>
#include <VBox/err.h>
#include <iprt/cpp/utils.h>


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

MachineDebugger::MachineDebugger()
    : mParent(NULL)
{
}

MachineDebugger::~MachineDebugger()
{
}

HRESULT MachineDebugger::FinalConstruct()
{
    unconst(mParent) = NULL;
    return BaseFinalConstruct();
}

void MachineDebugger::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the machine debugger object.
 *
 * @returns COM result indicator
 * @param aParent handle of our parent object
 */
HRESULT MachineDebugger::init(Console *aParent)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    for (unsigned i = 0; i < RT_ELEMENTS(maiQueuedEmExecPolicyParams); i++)
        maiQueuedEmExecPolicyParams[i] = UINT8_MAX;
    mSingleStepQueued = -1;
    mLogEnabledQueued = -1;
    mVirtualTimeRateQueued = UINT32_MAX;
    mFlushMode = false;

    m_hSampleReport = NULL;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void MachineDebugger::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(mParent) = NULL;
    mFlushMode = false;
}

/**
 * @callback_method_impl{FNDBGFPROGRESS}
 */
/*static*/ DECLCALLBACK(int) MachineDebugger::i_dbgfProgressCallback(void *pvUser, unsigned uPercentage)
{
    MachineDebugger *pThis = (MachineDebugger *)pvUser;

    int vrc = pThis->m_Progress->i_iprtProgressCallback(uPercentage, static_cast<Progress *>(pThis->m_Progress));
    if (   RT_SUCCESS(vrc)
        && uPercentage == 100)
    {
        PCVMMR3VTABLE const pVMM = pThis->mParent->i_getVMMVTable();
        AssertPtrReturn(pVMM, VERR_INTERNAL_ERROR_3);

        vrc = pVMM->pfnDBGFR3SampleReportDumpToFile(pThis->m_hSampleReport, pThis->m_strFilename.c_str());
        pVMM->pfnDBGFR3SampleReportRelease(pThis->m_hSampleReport);
        pThis->m_hSampleReport = NULL;
        if (RT_SUCCESS(vrc))
            pThis->m_Progress->i_notifyComplete(S_OK);
        else
        {
            HRESULT hrc = pThis->setError(VBOX_E_IPRT_ERROR,
                                          tr("Writing the sample report to '%s' failed with %Rrc"),
                                          pThis->m_strFilename.c_str(), vrc);
            pThis->m_Progress->i_notifyComplete(hrc);
        }
        pThis->m_Progress.setNull();
    }
    else if (vrc == VERR_CANCELLED)
        vrc = VERR_DBGF_CANCELLED;

    return vrc;
}

// IMachineDebugger properties
/////////////////////////////////////////////////////////////////////////////

/**
 * Returns the current singlestepping flag.
 *
 * @returns COM status code
 * @param   aSingleStep     Where to store the result.
 */
HRESULT MachineDebugger::getSingleStep(BOOL *aSingleStep)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        RT_NOREF(aSingleStep); /** @todo */
        ReturnComNotImplemented();
    }
    return hrc;
}

/**
 * Sets the singlestepping flag.
 *
 * @returns COM status code
 * @param   aSingleStep     The new state.
 */
HRESULT MachineDebugger::setSingleStep(BOOL aSingleStep)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        NOREF(aSingleStep); /** @todo */
        ReturnComNotImplemented();
    }
    return hrc;
}

/**
 * Internal worker for getting an EM executable policy setting.
 *
 * @returns COM status code.
 * @param   enmPolicy           Which EM policy.
 * @param   pfEnforced          Where to return the policy setting.
 */
HRESULT MachineDebugger::i_getEmExecPolicyProperty(EMEXECPOLICY enmPolicy, BOOL *pfEnforced)
{
    CheckComArgOutPointerValid(pfEnforced);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (i_queueSettings())
            *pfEnforced = maiQueuedEmExecPolicyParams[enmPolicy] == 1;
        else
        {
            bool fEnforced = false;
            Console::SafeVMPtrQuiet ptrVM(mParent);
            hrc = ptrVM.hrc();
            if (SUCCEEDED(hrc))
                ptrVM.vtable()->pfnEMR3QueryExecutionPolicy(ptrVM.rawUVM(), enmPolicy, &fEnforced);
            *pfEnforced = fEnforced;
        }
    }
    return hrc;
}

/**
 * Internal worker for setting an EM executable policy.
 *
 * @returns COM status code.
 * @param   enmPolicy           Which policy to change.
 * @param   fEnforce            Whether to enforce the policy or not.
 */
HRESULT MachineDebugger::i_setEmExecPolicyProperty(EMEXECPOLICY enmPolicy, BOOL fEnforce)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (i_queueSettings())
            maiQueuedEmExecPolicyParams[enmPolicy] = fEnforce ? 1 : 0;
        else
        {
            Console::SafeVMPtrQuiet ptrVM(mParent);
            hrc = ptrVM.hrc();
            if (SUCCEEDED(hrc))
            {
                int vrc = ptrVM.vtable()->pfnEMR3SetExecutionPolicy(ptrVM.rawUVM(), enmPolicy, fEnforce != FALSE);
                if (RT_FAILURE(vrc))
                    hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("EMR3SetExecutionPolicy failed with %Rrc"), vrc);
            }
        }
    }
    return hrc;
}

/**
 * Returns the current execute-all-in-IEM setting.
 *
 * @returns COM status code
 * @param   aExecuteAllInIEM    Address of result variable.
 */
HRESULT MachineDebugger::getExecuteAllInIEM(BOOL *aExecuteAllInIEM)
{
    return i_getEmExecPolicyProperty(EMEXECPOLICY_IEM_ALL, aExecuteAllInIEM);
}

/**
 * Changes the execute-all-in-IEM setting.
 *
 * @returns COM status code
 * @param   aExecuteAllInIEM    New setting.
 */
HRESULT MachineDebugger::setExecuteAllInIEM(BOOL aExecuteAllInIEM)
{
    LogFlowThisFunc(("enable=%d\n", aExecuteAllInIEM));
    return i_setEmExecPolicyProperty(EMEXECPOLICY_IEM_ALL, aExecuteAllInIEM);
}

/**
 * Returns the log enabled / disabled status.
 *
 * @returns COM status code
 * @param   aLogEnabled     address of result variable
 */
HRESULT MachineDebugger::getLogEnabled(BOOL *aLogEnabled)
{
#ifdef LOG_ENABLED
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    const PRTLOGGER pLogInstance = RTLogDefaultInstance();
    *aLogEnabled = pLogInstance && !(RTLogGetFlags(pLogInstance) & RTLOGFLAGS_DISABLED);
#else
    *aLogEnabled = false;
#endif

    return S_OK;
}

/**
 * Enables or disables logging.
 *
 * @returns COM status code
 * @param   aLogEnabled    The new code log state.
 */
HRESULT MachineDebugger::setLogEnabled(BOOL aLogEnabled)
{
    LogFlowThisFunc(("aLogEnabled=%d\n", aLogEnabled));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (i_queueSettings())
    {
        // queue the request
        mLogEnabledQueued = aLogEnabled;
        return S_OK;
    }

    Console::SafeVMPtr ptrVM(mParent);
    if (FAILED(ptrVM.hrc())) return ptrVM.hrc();

#ifdef LOG_ENABLED
    int vrc = ptrVM.vtable()->pfnDBGFR3LogModifyFlags(ptrVM.rawUVM(), aLogEnabled ? "enabled" : "disabled");
    if (RT_FAILURE(vrc))
    {
        /** @todo handle error code. */
    }
#endif

    return S_OK;
}

HRESULT MachineDebugger::i_logStringProps(PRTLOGGER pLogger, PFNLOGGETSTR pfnLogGetStr,
                                          const char *pszLogGetStr, Utf8Str *pstrSettings)
{
    /* Make sure the VM is powered up. */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (FAILED(hrc))
        return hrc;

    /* Make sure we've got a logger. */
    if (!pLogger)
    {
        *pstrSettings = "";
        return S_OK;
    }

    /* Do the job. */
    size_t cbBuf = _1K;
    for (;;)
    {
        char *pszBuf = (char *)RTMemTmpAlloc(cbBuf);
        AssertReturn(pszBuf, E_OUTOFMEMORY);
        int vrc = pstrSettings->reserveNoThrow(cbBuf);
        if (RT_SUCCESS(vrc))
        {
            vrc = pfnLogGetStr(pLogger, pstrSettings->mutableRaw(), cbBuf);
            if (RT_SUCCESS(vrc))
            {
                pstrSettings->jolt();
                return S_OK;
            }
            *pstrSettings = "";
            AssertReturn(vrc == VERR_BUFFER_OVERFLOW,
                         setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("%s returned %Rrc"), pszLogGetStr, vrc));
        }
        else
            return E_OUTOFMEMORY;

        /* try again with a bigger buffer. */
        cbBuf *= 2;
        AssertReturn(cbBuf <= _256K, setError(E_FAIL, tr("%s returns too much data"), pszLogGetStr));
    }
}

HRESULT MachineDebugger::getLogDbgFlags(com::Utf8Str &aLogDbgFlags)
{
    return i_logStringProps(RTLogGetDefaultInstance(), RTLogQueryFlags, "RTLogQueryFlags", &aLogDbgFlags);
}

HRESULT MachineDebugger::getLogDbgGroups(com::Utf8Str &aLogDbgGroups)
{
    return i_logStringProps(RTLogGetDefaultInstance(), RTLogQueryGroupSettings, "RTLogQueryGroupSettings", &aLogDbgGroups);
}

HRESULT MachineDebugger::getLogDbgDestinations(com::Utf8Str &aLogDbgDestinations)
{
    return i_logStringProps(RTLogGetDefaultInstance(), RTLogQueryDestinations, "RTLogQueryDestinations", &aLogDbgDestinations);
}

HRESULT MachineDebugger::getLogRelFlags(com::Utf8Str &aLogRelFlags)
{
    return i_logStringProps(RTLogRelGetDefaultInstance(), RTLogQueryFlags, "RTLogQueryFlags", &aLogRelFlags);
}

HRESULT MachineDebugger::getLogRelGroups(com::Utf8Str &aLogRelGroups)
{
    return i_logStringProps(RTLogRelGetDefaultInstance(), RTLogQueryGroupSettings, "RTLogQueryGroupSettings", &aLogRelGroups);
}

HRESULT MachineDebugger::getLogRelDestinations(com::Utf8Str &aLogRelDestinations)
{
    return i_logStringProps(RTLogRelGetDefaultInstance(), RTLogQueryDestinations, "RTLogQueryDestinations", &aLogRelDestinations);
}

/**
 * Return the main execution engine of the VM.
 *
 * @returns COM status code
 * @param   apenmEngine     Address of the result variable.
 */
HRESULT MachineDebugger::getExecutionEngine(VMExecutionEngine_T *apenmEngine)
{
    *apenmEngine = VMExecutionEngine_NotSet;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (ptrVM.isOk())
    {
        uint8_t bEngine = UINT8_MAX;
        int vrc = ptrVM.vtable()->pfnEMR3QueryMainExecutionEngine(ptrVM.rawUVM(), &bEngine);
        if (RT_SUCCESS(vrc))
            switch (bEngine)
            {
                case VM_EXEC_ENGINE_NOT_SET:    *apenmEngine = VMExecutionEngine_NotSet; break;
                case VM_EXEC_ENGINE_HW_VIRT:    *apenmEngine = VMExecutionEngine_HwVirt; break;
                case VM_EXEC_ENGINE_NATIVE_API: *apenmEngine = VMExecutionEngine_NativeApi; break;
                case VM_EXEC_ENGINE_IEM:
                {
                    bool fForced = false;
                    vrc = ptrVM.vtable()->pfnEMR3QueryExecutionPolicy(ptrVM.rawUVM(), EMEXECPOLICY_IEM_RECOMPILED, &fForced);
                    if (RT_SUCCESS(vrc) && fForced)
                        *apenmEngine = VMExecutionEngine_Recompiler;
                    else
                        *apenmEngine = VMExecutionEngine_Interpreter;
                    break;
                }
                default: AssertMsgFailed(("bEngine=%d\n", bEngine));
            }
    }

    return S_OK;
}

/**
 * Returns the current nested paging flag.
 *
 * @returns COM status code
 * @param   aHWVirtExNestedPagingEnabled    address of result variable
 */
HRESULT MachineDebugger::getHWVirtExNestedPagingEnabled(BOOL *aHWVirtExNestedPagingEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (ptrVM.isOk())
        *aHWVirtExNestedPagingEnabled = ptrVM.vtable()->pfnHMR3IsNestedPagingActive(ptrVM.rawUVM());
    else
        *aHWVirtExNestedPagingEnabled = false;

    return S_OK;
}

/**
 * Returns the current VPID flag.
 *
 * @returns COM status code
 * @param   aHWVirtExVPIDEnabled address of result variable
 */
HRESULT MachineDebugger::getHWVirtExVPIDEnabled(BOOL *aHWVirtExVPIDEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (ptrVM.isOk())
        *aHWVirtExVPIDEnabled = ptrVM.vtable()->pfnHMR3IsVpidActive(ptrVM.rawUVM());
    else
        *aHWVirtExVPIDEnabled = false;

    return S_OK;
}

/**
 * Returns the current unrestricted execution setting.
 *
 * @returns COM status code
 * @param   aHWVirtExUXEnabled  address of result variable
 */
HRESULT MachineDebugger::getHWVirtExUXEnabled(BOOL *aHWVirtExUXEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (ptrVM.isOk())
        *aHWVirtExUXEnabled = ptrVM.vtable()->pfnHMR3IsUXActive(ptrVM.rawUVM());
    else
        *aHWVirtExUXEnabled = false;

    return S_OK;
}

HRESULT MachineDebugger::getOSName(com::Utf8Str &aOSName)
{
    LogFlowThisFunc(("\n"));
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        /*
         * Do the job and try convert the name.
         */
        char szName[64];
        int vrc = ptrVM.vtable()->pfnDBGFR3OSQueryNameAndVersion(ptrVM.rawUVM(), szName, sizeof(szName), NULL, 0);
        if (RT_SUCCESS(vrc))
            hrc = aOSName.assignEx(szName);
        else
            hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("DBGFR3OSQueryNameAndVersion failed with %Rrc"), vrc);
    }
    return hrc;
}

HRESULT MachineDebugger::getOSVersion(com::Utf8Str &aOSVersion)
{
    LogFlowThisFunc(("\n"));
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        /*
         * Do the job and try convert the name.
         */
        char szVersion[256];
        int vrc = ptrVM.vtable()->pfnDBGFR3OSQueryNameAndVersion(ptrVM.rawUVM(), NULL, 0, szVersion, sizeof(szVersion));
        if (RT_SUCCESS(vrc))
            hrc = aOSVersion.assignEx(szVersion);
        else
            hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("DBGFR3OSQueryNameAndVersion failed with %Rrc"), vrc);
    }
    return hrc;
}

/**
 * Returns the current PAE flag.
 *
 * @returns COM status code
 * @param   aPAEEnabled     address of result variable.
 */
HRESULT MachineDebugger::getPAEEnabled(BOOL *aPAEEnabled)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (ptrVM.isOk())
    {
        uint32_t cr4;
        int vrc = ptrVM.vtable()->pfnDBGFR3RegCpuQueryU32(ptrVM.rawUVM(), 0 /*idCpu*/,  DBGFREG_CR4, &cr4); AssertRC(vrc);
        *aPAEEnabled = RT_BOOL(cr4 & X86_CR4_PAE);
    }
    else
        *aPAEEnabled = false;

    return S_OK;
}

/**
 * Returns the current virtual time rate.
 *
 * @returns COM status code.
 * @param   aVirtualTimeRate    Where to store the rate.
 */
HRESULT MachineDebugger::getVirtualTimeRate(ULONG *aVirtualTimeRate)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
        *aVirtualTimeRate = ptrVM.vtable()->pfnTMR3GetWarpDrive(ptrVM.rawUVM());

    return hrc;
}

/**
 * Set the virtual time rate.
 *
 * @returns COM status code.
 * @param   aVirtualTimeRate    The new rate.
 */
HRESULT MachineDebugger::setVirtualTimeRate(ULONG aVirtualTimeRate)
{
    HRESULT hrc = S_OK;

    if (aVirtualTimeRate < 2 || aVirtualTimeRate > 20000)
        return setError(E_INVALIDARG, tr("%u is out of range [2..20000]"), aVirtualTimeRate);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (i_queueSettings())
        mVirtualTimeRateQueued = aVirtualTimeRate;
    else
    {
        Console::SafeVMPtr ptrVM(mParent);
        hrc = ptrVM.hrc();
        if (SUCCEEDED(hrc))
        {
            int vrc = ptrVM.vtable()->pfnTMR3SetWarpDrive(ptrVM.rawUVM(), aVirtualTimeRate);
            if (RT_FAILURE(vrc))
                hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("TMR3SetWarpDrive(, %u) failed with vrc=%Rrc"), aVirtualTimeRate, vrc);
        }
    }

    return hrc;
}

/**
 * Get the VM uptime in milliseconds.
 *
 * @returns COM status code
 * @param   aUptime     Where to store the uptime.
 */
HRESULT MachineDebugger::getUptime(LONG64 *aUptime)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
        *aUptime = (int64_t)ptrVM.vtable()->pfnTMR3TimeVirtGetMilli(ptrVM.rawUVM());

    return hrc;
}

// IMachineDebugger methods
/////////////////////////////////////////////////////////////////////////////

HRESULT MachineDebugger::dumpGuestCore(const com::Utf8Str &aFilename, const com::Utf8Str &aCompression)
{
    if (aCompression.length())
        return setError(E_INVALIDARG, tr("The compression parameter must be empty"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        int vrc = ptrVM.vtable()->pfnDBGFR3CoreWrite(ptrVM.rawUVM(), aFilename.c_str(), false /*fReplaceFile*/);
        if (RT_SUCCESS(vrc))
            hrc = S_OK;
        else
            hrc = setErrorBoth(E_FAIL, vrc, tr("DBGFR3CoreWrite failed with %Rrc"), vrc);
    }

    return hrc;
}

HRESULT MachineDebugger::dumpHostProcessCore(const com::Utf8Str &aFilename, const com::Utf8Str &aCompression)
{
    RT_NOREF(aFilename, aCompression);
    ReturnComNotImplemented();
}

/**
 * Debug info string buffer formatter.
 */
typedef struct MACHINEDEBUGGERINOFHLP
{
    /** The core info helper structure. */
    DBGFINFOHLP Core;
    /** Pointer to the buffer. */
    char       *pszBuf;
    /** The size of the buffer. */
    size_t      cbBuf;
    /** The offset into the buffer */
    size_t      offBuf;
    /** Indicates an out-of-memory condition. */
    bool        fOutOfMemory;
} MACHINEDEBUGGERINOFHLP;
/** Pointer to a Debug info string buffer formatter. */
typedef MACHINEDEBUGGERINOFHLP *PMACHINEDEBUGGERINOFHLP;


/**
 * @callback_method_impl{FNRTSTROUTPUT}
 */
static DECLCALLBACK(size_t) MachineDebuggerInfoOutput(void *pvArg, const char *pachChars, size_t cbChars)
{
    PMACHINEDEBUGGERINOFHLP pHlp = (PMACHINEDEBUGGERINOFHLP)pvArg;

    /*
     * Grow the buffer if required.
     */
    size_t const cbRequired  = cbChars + pHlp->offBuf + 1;
    if (cbRequired > pHlp->cbBuf)
    {
        if (RT_UNLIKELY(pHlp->fOutOfMemory))
            return 0;

        size_t cbBufNew = pHlp->cbBuf * 2;
        if (cbRequired > cbBufNew)
            cbBufNew = RT_ALIGN_Z(cbRequired, 256);
        void *pvBufNew = RTMemRealloc(pHlp->pszBuf, cbBufNew);
        if (RT_UNLIKELY(!pvBufNew))
        {
            pHlp->fOutOfMemory = true;
            RTMemFree(pHlp->pszBuf);
            pHlp->pszBuf = NULL;
            pHlp->cbBuf  = 0;
            pHlp->offBuf = 0;
            return 0;
        }

        pHlp->pszBuf = (char *)pvBufNew;
        pHlp->cbBuf  = cbBufNew;
    }

    /*
     * Copy the bytes into the buffer and terminate it.
     */
    if (cbChars)
    {
        memcpy(&pHlp->pszBuf[pHlp->offBuf], pachChars, cbChars);
        pHlp->offBuf += cbChars;
    }
    pHlp->pszBuf[pHlp->offBuf] = '\0';
    Assert(pHlp->offBuf < pHlp->cbBuf);
    return cbChars;
}

/**
 * @interface_method_impl{DBGFINFOHLP,pfnPrintfV}
 */
static DECLCALLBACK(void) MachineDebuggerInfoPrintfV(PCDBGFINFOHLP pHlp, const char *pszFormat, va_list args)
{
    RTStrFormatV(MachineDebuggerInfoOutput, (void *)pHlp, NULL,  NULL, pszFormat, args);
}

/**
 * @interface_method_impl{DBGFINFOHLP,pfnPrintf}
 */
static DECLCALLBACK(void) MachineDebuggerInfoPrintf(PCDBGFINFOHLP pHlp, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    MachineDebuggerInfoPrintfV(pHlp, pszFormat, va);
    va_end(va);
}

/**
 * Initializes the debug info string buffer formatter
 *
 * @param   pHlp    The help structure to init.
 * @param   pVMM    The VMM vtable.
 */
static void MachineDebuggerInfoInit(PMACHINEDEBUGGERINOFHLP pHlp, PCVMMR3VTABLE pVMM)
{
    pHlp->Core.pfnPrintf        = MachineDebuggerInfoPrintf;
    pHlp->Core.pfnPrintfV       = MachineDebuggerInfoPrintfV;
    pHlp->Core.pfnGetOptError   = pVMM->pfnDBGFR3InfoGenericGetOptError;
    pHlp->pszBuf                = NULL;
    pHlp->cbBuf                 = 0;
    pHlp->offBuf                = 0;
    pHlp->fOutOfMemory          = false;
}

/**
 * Deletes the debug info string buffer formatter.
 * @param   pHlp                The helper structure to delete.
 */
static void MachineDebuggerInfoDelete(PMACHINEDEBUGGERINOFHLP pHlp)
{
    RTMemFree(pHlp->pszBuf);
    pHlp->pszBuf = NULL;
}

HRESULT MachineDebugger::info(const com::Utf8Str &aName, const com::Utf8Str &aArgs, com::Utf8Str &aInfo)
{
    LogFlowThisFunc(("\n"));

    /*
     * Do the autocaller and lock bits.
     */
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Console::SafeVMPtr ptrVM(mParent);
        hrc = ptrVM.hrc();
        if (SUCCEEDED(hrc))
        {
            /*
             * Create a helper and call DBGFR3Info.
             */
            MACHINEDEBUGGERINOFHLP Hlp;
            MachineDebuggerInfoInit(&Hlp, ptrVM.vtable());
            int vrc = ptrVM.vtable()->pfnDBGFR3Info(ptrVM.rawUVM(),  aName.c_str(),  aArgs.c_str(), &Hlp.Core);
            if (RT_SUCCESS(vrc))
            {
                if (!Hlp.fOutOfMemory)
                    hrc = aInfo.assignEx(Hlp.pszBuf);
                else
                    hrc = E_OUTOFMEMORY;
            }
            else
                hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("DBGFR3Info failed with %Rrc"), vrc);
            MachineDebuggerInfoDelete(&Hlp);
        }
    }
    return hrc;
}

HRESULT MachineDebugger::injectNMI()
{
    LogFlowThisFunc(("\n"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        int vrc = ptrVM.vtable()->pfnDBGFR3InjectNMI(ptrVM.rawUVM(), 0);
        if (RT_SUCCESS(vrc))
            hrc = S_OK;
        else
            hrc = setErrorBoth(E_FAIL, vrc, tr("DBGFR3InjectNMI failed with %Rrc"), vrc);
    }
    return hrc;
}

HRESULT MachineDebugger::modifyLogFlags(const com::Utf8Str &aSettings)
{
    LogFlowThisFunc(("aSettings=%s\n", aSettings.c_str()));
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        int vrc = ptrVM.vtable()->pfnDBGFR3LogModifyFlags(ptrVM.rawUVM(), aSettings.c_str());
        if (RT_SUCCESS(vrc))
            hrc = S_OK;
        else
            hrc = setErrorBoth(E_FAIL, vrc, tr("DBGFR3LogModifyFlags failed with %Rrc"), vrc);
    }
    return hrc;
}

HRESULT MachineDebugger::modifyLogGroups(const com::Utf8Str &aSettings)
{
    LogFlowThisFunc(("aSettings=%s\n", aSettings.c_str()));
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        int vrc = ptrVM.vtable()->pfnDBGFR3LogModifyGroups(ptrVM.rawUVM(), aSettings.c_str());
        if (RT_SUCCESS(vrc))
            hrc = S_OK;
        else
            hrc = setErrorBoth(E_FAIL, vrc, tr("DBGFR3LogModifyGroups failed with %Rrc"), vrc);
    }
    return hrc;
}

HRESULT MachineDebugger::modifyLogDestinations(const com::Utf8Str &aSettings)
{
    LogFlowThisFunc(("aSettings=%s\n", aSettings.c_str()));
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        int vrc = ptrVM.vtable()->pfnDBGFR3LogModifyDestinations(ptrVM.rawUVM(), aSettings.c_str());
        if (RT_SUCCESS(vrc))
            hrc = S_OK;
        else
            hrc = setErrorBoth(E_FAIL, vrc, tr("DBGFR3LogModifyDestinations failed with %Rrc"), vrc);
    }
    return hrc;
}

HRESULT MachineDebugger::readPhysicalMemory(LONG64 aAddress, ULONG aSize, std::vector<BYTE> &aBytes)
{
    RT_NOREF(aAddress, aSize, aBytes);
    ReturnComNotImplemented();
}

HRESULT MachineDebugger::writePhysicalMemory(LONG64 aAddress, ULONG aSize, const std::vector<BYTE> &aBytes)
{
    RT_NOREF(aAddress, aSize, aBytes);
    ReturnComNotImplemented();
}

HRESULT MachineDebugger::readVirtualMemory(ULONG aCpuId, LONG64 aAddress, ULONG aSize, std::vector<BYTE> &aBytes)
{
    RT_NOREF(aCpuId, aAddress, aSize, aBytes);
    ReturnComNotImplemented();
}

HRESULT MachineDebugger::writeVirtualMemory(ULONG aCpuId, LONG64 aAddress, ULONG aSize, const std::vector<BYTE> &aBytes)
{
    RT_NOREF(aCpuId, aAddress, aSize, aBytes);
    ReturnComNotImplemented();
}

HRESULT MachineDebugger::loadPlugIn(const com::Utf8Str &aName, com::Utf8Str &aPlugInName)
{
    /*
     * Lock the debugger and get the VM pointer
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        /*
         * Do the job and try convert the name.
         */
        if (aName.equals("all"))
        {
            ptrVM.vtable()->pfnDBGFR3PlugInLoadAll(ptrVM.rawUVM());
            hrc = aPlugInName.assignEx("all");
        }
        else
        {
            RTERRINFOSTATIC ErrInfo;
            char            szName[80];
            int vrc = ptrVM.vtable()->pfnDBGFR3PlugInLoad(ptrVM.rawUVM(), aName.c_str(), szName, sizeof(szName), RTErrInfoInitStatic(&ErrInfo));
            if (RT_SUCCESS(vrc))
                hrc = aPlugInName.assignEx(szName);
            else
                hrc = setErrorVrc(vrc, "%s", ErrInfo.szMsg);
        }
    }
    return hrc;

}

HRESULT MachineDebugger::unloadPlugIn(const com::Utf8Str &aName)
{
    /*
     * Lock the debugger and get the VM pointer
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        /*
         * Do the job and try convert the name.
         */
        if (aName.equals("all"))
        {
            ptrVM.vtable()->pfnDBGFR3PlugInUnloadAll(ptrVM.rawUVM());
            hrc = S_OK;
        }
        else
        {
            int vrc = ptrVM.vtable()->pfnDBGFR3PlugInUnload(ptrVM.rawUVM(), aName.c_str());
            if (RT_SUCCESS(vrc))
                hrc = S_OK;
            else if (vrc == VERR_NOT_FOUND)
                hrc = setErrorBoth(E_FAIL, vrc, tr("Plug-in '%s' was not found"), aName.c_str());
            else
                hrc = setErrorVrc(vrc, tr("Error unloading '%s': %Rrc"), aName.c_str(), vrc);
        }
    }
    return hrc;

}

HRESULT MachineDebugger::detectOS(com::Utf8Str &aOs)
{
    LogFlowThisFunc(("\n"));

    /*
     * Lock the debugger and get the VM pointer
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        /*
         * Do the job.
         */
        char szName[64];
        int vrc = ptrVM.vtable()->pfnDBGFR3OSDetect(ptrVM.rawUVM(), szName, sizeof(szName));
        if (RT_SUCCESS(vrc) && vrc != VINF_DBGF_OS_NOT_DETCTED)
            hrc = aOs.assignEx(szName);
        else
            hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("DBGFR3OSDetect failed with %Rrc"), vrc);
    }
    return hrc;
}

HRESULT MachineDebugger::queryOSKernelLog(ULONG aMaxMessages, com::Utf8Str &aDmesg)
{
    /*
     * Lock the debugger and get the VM pointer
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        PDBGFOSIDMESG pDmesg = (PDBGFOSIDMESG)ptrVM.vtable()->pfnDBGFR3OSQueryInterface(ptrVM.rawUVM(), DBGFOSINTERFACE_DMESG);
        if (pDmesg)
        {
            size_t   cbActual;
            size_t   cbBuf  = _512K;
            int vrc = aDmesg.reserveNoThrow(cbBuf);
            if (RT_SUCCESS(vrc))
            {
                uint32_t cMessages = aMaxMessages == 0 ? UINT32_MAX : aMaxMessages;
                vrc = pDmesg->pfnQueryKernelLog(pDmesg, ptrVM.rawUVM(), ptrVM.vtable(), 0 /*fFlags*/, cMessages,
                                                aDmesg.mutableRaw(), cbBuf, &cbActual);

                uint32_t cTries = 10;
                while (vrc == VERR_BUFFER_OVERFLOW && cbBuf < 16*_1M && cTries-- > 0)
                {
                    cbBuf = RT_ALIGN_Z(cbActual + _4K, _4K);
                    vrc = aDmesg.reserveNoThrow(cbBuf);
                    if (RT_SUCCESS(vrc))
                        vrc = pDmesg->pfnQueryKernelLog(pDmesg, ptrVM.rawUVM(), ptrVM.vtable(), 0 /*fFlags*/, cMessages,
                                                        aDmesg.mutableRaw(), cbBuf, &cbActual);
                }
                if (RT_SUCCESS(vrc))
                    aDmesg.jolt();
                else if (vrc == VERR_BUFFER_OVERFLOW)
                    hrc = setError(E_FAIL, tr("Too much log available, must use the maxMessages parameter to restrict."));
                else
                    hrc = setErrorVrc(vrc);
            }
            else
                hrc = setErrorBoth(E_OUTOFMEMORY, vrc);
        }
        else
            hrc = setError(E_FAIL, tr("The dmesg interface isn't implemented by guest OS digger, or detectOS() has not been called."));
    }
    return hrc;
}

HRESULT MachineDebugger::getRegister(ULONG aCpuId, const com::Utf8Str &aName, com::Utf8Str &aValue)
{
    /*
     * The prologue.
     */
    LogFlowThisFunc(("\n"));
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        /*
         * Real work.
         */
        DBGFREGVAL      Value;
        DBGFREGVALTYPE  enmType;
        int vrc = ptrVM.vtable()->pfnDBGFR3RegNmQuery(ptrVM.rawUVM(), aCpuId, aName.c_str(), &Value, &enmType);
        if (RT_SUCCESS(vrc))
        {
            char szHex[160];
            ssize_t cch = ptrVM.vtable()->pfnDBGFR3RegFormatValue(szHex, sizeof(szHex), &Value, enmType, true /*fSpecial*/);
            if (cch > 0)
                hrc = aValue.assignEx(szHex);
            else
                hrc = E_UNEXPECTED;
        }
        else if (vrc == VERR_DBGF_REGISTER_NOT_FOUND)
            hrc = setErrorBoth(E_FAIL, vrc, tr("Register '%s' was not found"), aName.c_str());
        else if (vrc == VERR_INVALID_CPU_ID)
            hrc = setErrorBoth(E_FAIL, vrc, tr("Invalid CPU ID: %u"), aCpuId);
        else
            hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc,
                               tr("DBGFR3RegNmQuery failed with vrc=%Rrc querying register '%s' with default cpu set to %u"),
                               vrc, aName.c_str(), aCpuId);
    }

    return hrc;
}

HRESULT MachineDebugger::getRegisters(ULONG aCpuId, std::vector<com::Utf8Str> &aNames, std::vector<com::Utf8Str> &aValues)
{
    RT_NOREF(aCpuId); /** @todo fix missing aCpuId usage! */

    /*
     * The prologue.
     */
    LogFlowThisFunc(("\n"));
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        /*
         * Real work.
         */
        size_t cRegs;
        int vrc = ptrVM.vtable()->pfnDBGFR3RegNmQueryAllCount(ptrVM.rawUVM(), &cRegs);
        if (RT_SUCCESS(vrc))
        {
            PDBGFREGENTRYNM paRegs = (PDBGFREGENTRYNM)RTMemAllocZ(sizeof(paRegs[0]) * cRegs);
            if (paRegs)
            {
                vrc = ptrVM.vtable()->pfnDBGFR3RegNmQueryAll(ptrVM.rawUVM(), paRegs, cRegs);
                if (RT_SUCCESS(vrc))
                {
                    try
                    {
                        aValues.resize(cRegs);
                        aNames.resize(cRegs);
                        uint32_t iDst = 0;
                        for (uint32_t iSrc = 0; iSrc < cRegs; iSrc++)
                            if (paRegs[iSrc].pszName) /* skip padding entries */
                            {
                                char szHex[160];
                                szHex[159] = szHex[0] = '\0';
                                ssize_t cch = ptrVM.vtable()->pfnDBGFR3RegFormatValue(szHex, sizeof(szHex), &paRegs[iSrc].Val,
                                                                                      paRegs[iSrc].enmType, true /*fSpecial*/);
                                Assert(cch > 0); NOREF(cch);
                                aNames[iDst]  = paRegs[iSrc].pszName;
                                aValues[iDst] = szHex;
                                iDst++;
                            }

                        /* If we skipped padding entries, resize the return arrays to the actual return size. */
                        if (iDst < cRegs)
                        {
                            aValues.resize(iDst);
                            aNames.resize(iDst);
                        }
                    }
                    catch (std::bad_alloc &)
                    {
                        hrc = E_OUTOFMEMORY;
                    }
                }
                else
                    hrc = setErrorBoth(E_FAIL, vrc, tr("DBGFR3RegNmQueryAll failed with %Rrc"), vrc);

                RTMemFree(paRegs);
            }
            else
                hrc = E_OUTOFMEMORY;
        }
        else
            hrc = setErrorBoth(E_FAIL, vrc, tr("DBGFR3RegNmQueryAllCount failed with %Rrc"), vrc);
    }
    return hrc;
}

HRESULT MachineDebugger::setRegister(ULONG aCpuId, const com::Utf8Str &aName, const com::Utf8Str &aValue)
{
    RT_NOREF(aCpuId, aName, aValue);
    ReturnComNotImplemented();
}

HRESULT MachineDebugger::setRegisters(ULONG aCpuId, const std::vector<com::Utf8Str> &aNames,
                                      const std::vector<com::Utf8Str> &aValues)
{
    RT_NOREF(aCpuId, aNames, aValues);
    ReturnComNotImplemented();
}

HRESULT MachineDebugger::dumpGuestStack(ULONG aCpuId, com::Utf8Str &aStack)
{
    /*
     * The prologue.
     */
    LogFlowThisFunc(("\n"));
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        /*
         * There is currently a problem with the windows diggers and SMP, where
         * guest driver memory is being read from CPU zero in order to ensure that
         * we've got a consisten virtual memory view.  If one of the other CPUs
         * initiates a rendezvous while we're unwinding the stack and trying to
         * read guest driver memory, we will deadlock.
         *
         * So, check the VM state and maybe suspend the VM before we continue.
         */
        int  vrc     = VINF_SUCCESS;
        bool fPaused = false;
        if (aCpuId != 0)
        {
            VMSTATE enmVmState = ptrVM.vtable()->pfnVMR3GetStateU(ptrVM.rawUVM());
            if (   enmVmState == VMSTATE_RUNNING
                || enmVmState == VMSTATE_RUNNING_LS)
            {
                alock.release();
                vrc = ptrVM.vtable()->pfnVMR3Suspend(ptrVM.rawUVM(), VMSUSPENDREASON_USER);
                alock.acquire();
                fPaused = RT_SUCCESS(vrc);
            }
        }
        if (RT_SUCCESS(vrc))
        {
            PCDBGFSTACKFRAME pFirstFrame;
            vrc = ptrVM.vtable()->pfnDBGFR3StackWalkBegin(ptrVM.rawUVM(), aCpuId, DBGFCODETYPE_GUEST, &pFirstFrame);
            if (RT_SUCCESS(vrc))
            {
                /*
                 * Print header.
                 */
                try
                {
                    uint32_t fBitFlags = 0;
                    for (PCDBGFSTACKFRAME pFrame = pFirstFrame;
                         pFrame;
                         pFrame = ptrVM.vtable()->pfnDBGFR3StackWalkNext(pFrame))
                    {
                        uint32_t const fCurBitFlags = pFrame->fFlags & (DBGFSTACKFRAME_FLAGS_16BIT | DBGFSTACKFRAME_FLAGS_32BIT | DBGFSTACKFRAME_FLAGS_64BIT);
                        if (fCurBitFlags & DBGFSTACKFRAME_FLAGS_16BIT)
                        {
                            if (fCurBitFlags != fBitFlags)
                                aStack.append("SS:BP     Ret SS:BP Ret CS:EIP    Arg0     Arg1     Arg2     Arg3     CS:EIP / Symbol [line]\n");
                            aStack.appendPrintf("%04RX16:%04RX16 %04RX16:%04RX16 %04RX32:%08RX32 %08RX32 %08RX32 %08RX32 %08RX32",
                                                pFrame->AddrFrame.Sel,
                                                (uint16_t)pFrame->AddrFrame.off,
                                                pFrame->AddrReturnFrame.Sel,
                                                (uint16_t)pFrame->AddrReturnFrame.off,
                                                (uint32_t)pFrame->AddrReturnPC.Sel,
                                                (uint32_t)pFrame->AddrReturnPC.off,
                                                pFrame->Args.au32[0],
                                                pFrame->Args.au32[1],
                                                pFrame->Args.au32[2],
                                                pFrame->Args.au32[3]);
                        }
                        else if (fCurBitFlags & DBGFSTACKFRAME_FLAGS_32BIT)
                        {
                            if (fCurBitFlags != fBitFlags)
                                aStack.append("EBP      Ret EBP  Ret CS:EIP    Arg0     Arg1     Arg2     Arg3     CS:EIP / Symbol [line]\n");
                            aStack.appendPrintf("%08RX32 %08RX32 %04RX32:%08RX32 %08RX32 %08RX32 %08RX32 %08RX32",
                                                (uint32_t)pFrame->AddrFrame.off,
                                                (uint32_t)pFrame->AddrReturnFrame.off,
                                                (uint32_t)pFrame->AddrReturnPC.Sel,
                                                (uint32_t)pFrame->AddrReturnPC.off,
                                                pFrame->Args.au32[0],
                                                pFrame->Args.au32[1],
                                                pFrame->Args.au32[2],
                                                pFrame->Args.au32[3]);
                        }
                        else if (fCurBitFlags & DBGFSTACKFRAME_FLAGS_64BIT)
                        {
                            if (fCurBitFlags != fBitFlags)
                                aStack.append("RBP              Ret SS:RBP            Ret RIP          CS:RIP / Symbol [line]\n");
                            aStack.appendPrintf("%016RX64 %04RX16:%016RX64 %016RX64",
                                                (uint64_t)pFrame->AddrFrame.off,
                                                pFrame->AddrReturnFrame.Sel,
                                                (uint64_t)pFrame->AddrReturnFrame.off,
                                                (uint64_t)pFrame->AddrReturnPC.off);
                        }

                        if (!pFrame->pSymPC)
                            aStack.appendPrintf(fCurBitFlags & DBGFSTACKFRAME_FLAGS_64BIT
                                                ? " %RTsel:%016RGv"
                                                : fCurBitFlags & DBGFSTACKFRAME_FLAGS_32BIT
                                                ? " %RTsel:%08RGv"
                                                : " %RTsel:%04RGv"
                                                , pFrame->AddrPC.Sel, pFrame->AddrPC.off);
                        else
                        {
                            RTGCINTPTR offDisp = pFrame->AddrPC.FlatPtr - pFrame->pSymPC->Value; /** @todo this isn't 100% correct for segmented stuff. */
                            if (offDisp > 0)
                                aStack.appendPrintf(" %s+%llx", pFrame->pSymPC->szName, (int64_t)offDisp);
                            else if (offDisp < 0)
                                aStack.appendPrintf(" %s-%llx", pFrame->pSymPC->szName, -(int64_t)offDisp);
                            else
                                aStack.appendPrintf(" %s", pFrame->pSymPC->szName);
                        }
                        if (pFrame->pLinePC)
                            aStack.appendPrintf(" [%s @ 0i%d]", pFrame->pLinePC->szFilename, pFrame->pLinePC->uLineNo);
                        aStack.append("\n");

                        fBitFlags = fCurBitFlags;
                    }
                }
                catch (std::bad_alloc &)
                {
                    hrc = E_OUTOFMEMORY;
                }

                ptrVM.vtable()->pfnDBGFR3StackWalkEnd(pFirstFrame);
            }
            else
                hrc = setErrorBoth(E_FAIL, vrc, tr("DBGFR3StackWalkBegin failed with %Rrc"), vrc);

            /*
             * Resume the VM if we suspended it.
             */
            if (fPaused)
            {
                alock.release();
                ptrVM.vtable()->pfnVMR3Resume(ptrVM.rawUVM(), VMRESUMEREASON_USER);
            }
        }
        else
            hrc = setErrorBoth(E_FAIL, vrc, tr("Suspending the VM failed with %Rrc\n"), vrc);
    }

    return hrc;
}

/**
 * Resets VM statistics.
 *
 * @returns COM status code.
 * @param   aPattern            The selection pattern. A bit similar to filename globbing.
 */
HRESULT MachineDebugger::resetStats(const com::Utf8Str &aPattern)
{
    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (!ptrVM.isOk())
        return setError(VBOX_E_INVALID_VM_STATE, tr("Machine is not running"));

    ptrVM.vtable()->pfnSTAMR3Reset(ptrVM.rawUVM(), aPattern.c_str());

    return S_OK;
}

/**
 * Dumps VM statistics to the log.
 *
 * @returns COM status code.
 * @param   aPattern            The selection pattern. A bit similar to filename globbing.
 */
HRESULT MachineDebugger::dumpStats(const com::Utf8Str &aPattern)
{
    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (!ptrVM.isOk())
        return setError(VBOX_E_INVALID_VM_STATE, tr("Machine is not running"));

    ptrVM.vtable()->pfnSTAMR3Dump(ptrVM.rawUVM(), aPattern.c_str());

    return S_OK;
}

/**
 * Get the VM statistics in an XML format.
 *
 * @returns COM status code.
 * @param   aPattern            The selection pattern. A bit similar to filename globbing.
 * @param   aWithDescriptions   Whether to include the descriptions.
 * @param   aStats              The XML document containing the statistics.
 */
HRESULT MachineDebugger::getStats(const com::Utf8Str &aPattern, BOOL aWithDescriptions, com::Utf8Str &aStats)
{
    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (!ptrVM.isOk())
        return setError(VBOX_E_INVALID_VM_STATE, tr("Machine is not running"));

    char *pszSnapshot;
    int vrc = ptrVM.vtable()->pfnSTAMR3Snapshot(ptrVM.rawUVM(), aPattern.c_str(), &pszSnapshot, NULL, !!aWithDescriptions);
    if (RT_FAILURE(vrc))
        return vrc == VERR_NO_MEMORY ? E_OUTOFMEMORY : E_FAIL;

    /** @todo this is horribly inefficient! And it's kinda difficult to tell whether it failed...
     * Must use UTF-8 or ASCII here and completely avoid these two extra copy operations.
     * Until that's done, this method is kind of useless for debugger statistics GUI because
     * of the amount statistics in a debug build. */
    HRESULT hrc = aStats.assignEx(pszSnapshot);
    ptrVM.vtable()->pfnSTAMR3SnapshotFree(ptrVM.rawUVM(), pszSnapshot);

    return hrc;
}


/** Wrapper around TMR3GetCpuLoadPercents. */
HRESULT MachineDebugger::getCPULoad(ULONG aCpuId, ULONG *aPctExecuting, ULONG *aPctHalted, ULONG *aPctOther, LONG64 *aMsInterval)
{
    HRESULT hrc;
    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (ptrVM.isOk())
    {
        uint8_t uPctExecuting = 0;
        uint8_t uPctHalted    = 0;
        uint8_t uPctOther     = 0;
        uint64_t msInterval   = 0;
        int vrc = ptrVM.vtable()->pfnTMR3GetCpuLoadPercents(ptrVM.rawUVM(), aCpuId >= UINT32_MAX / 2 ? VMCPUID_ALL : aCpuId,
                                                            &msInterval, &uPctExecuting, &uPctHalted, &uPctOther);
        if (RT_SUCCESS(vrc))
        {
            *aPctExecuting = uPctExecuting;
            *aPctHalted    = uPctHalted;
            *aPctOther     = uPctOther;
            *aMsInterval   = msInterval;
            hrc = S_OK;
        }
        else
            hrc = setErrorVrc(vrc);
    }
    else
        hrc = setError(VBOX_E_INVALID_VM_STATE, tr("Machine is not running"));
    return hrc;
}


HRESULT MachineDebugger::takeGuestSample(const com::Utf8Str &aFilename, ULONG aUsInterval, LONG64 aUsSampleTime, ComPtr<IProgress> &pProgress)
{
    /*
     * The prologue.
     */
    LogFlowThisFunc(("\n"));
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        if (!m_hSampleReport)
        {
            m_strFilename = aFilename;

            int vrc = ptrVM.vtable()->pfnDBGFR3SampleReportCreate(ptrVM.rawUVM(), aUsInterval,
                                                                  DBGF_SAMPLE_REPORT_F_STACK_REVERSE, &m_hSampleReport);
            if (RT_SUCCESS(vrc))
            {
                hrc = m_Progress.createObject();
                if (SUCCEEDED(hrc))
                {
                    hrc = m_Progress->init(static_cast<IMachineDebugger*>(this),
                                           tr("Creating guest sample report..."),
                                           TRUE /* aCancelable */);
                    if (SUCCEEDED(hrc))
                    {
                        vrc = ptrVM.vtable()->pfnDBGFR3SampleReportStart(m_hSampleReport, aUsSampleTime, i_dbgfProgressCallback,
                                                                         static_cast<MachineDebugger*>(this));
                        if (RT_SUCCESS(vrc))
                            hrc = m_Progress.queryInterfaceTo(pProgress.asOutParam());
                        else
                            hrc = setErrorVrc(vrc);
                    }
                }

                if (FAILED(hrc))
                {
                    ptrVM.vtable()->pfnDBGFR3SampleReportRelease(m_hSampleReport);
                    m_hSampleReport = NULL;
                }
            }
            else
                hrc = setErrorVrc(vrc);
        }
        else
            hrc = setError(VBOX_E_INVALID_VM_STATE, tr("A sample report is already in progress"));
    }

    return hrc;
}

/**
 * Hack for getting the user mode VM handle (UVM) and VMM function table.
 *
 * @returns COM status code
 * @param   aMagicVersion       The VMMR3VTABLE_MAGIC_VERSION value of the
 *                              caller so we can check that the function table
 *                              is compatible.  (Otherwise, the caller can't
 *                              safely release the UVM reference.)
 * @param   aUVM                Where to store the vm handle. Since there is no
 *                              uintptr_t in COM, we're using the max integer. (No,
 *                              ULONG is not pointer sized!)
 * @param   aVMMFunctionTable   Where to store the vm handle.
 *
 * @remarks The returned handle must be passed to VMR3ReleaseUVM()!
 */
HRESULT MachineDebugger::getUVMAndVMMFunctionTable(LONG64 aMagicVersion, LONG64 *aVMMFunctionTable, LONG64 *aUVM)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Make sure it is a local call.
     */
    RTTHREAD hThread = RTThreadSelf();
    if (hThread != NIL_RTTHREAD)
    {
        const char *pszName = RTThreadGetName(hThread);
        if (   !RTStrStartsWith(pszName, "ALIEN-") /* COM worker threads are aliens */
            && !RTStrStartsWith(pszName, "nspr-")  /* XPCOM worker threads are nspr-X */ )
        {
            /*
             * Use safe VM pointer to get both the UVM and VMM function table.
             */
            Console::SafeVMPtr ptrVM(mParent);
            HRESULT hrc = ptrVM.hrc();
            if (SUCCEEDED(hrc))
            {
                if (VMMR3VTABLE_IS_COMPATIBLE_EX(ptrVM.vtable()->uMagicVersion, (uint64_t)aMagicVersion))
                {
                    ptrVM.vtable()->pfnVMR3RetainUVM(ptrVM.rawUVM());
                    *aUVM              = (intptr_t)ptrVM.rawUVM();
                    *aVMMFunctionTable = (intptr_t)ptrVM.vtable();
                    hrc = S_OK;
                }
                else
                    hrc = setError(E_FAIL, tr("Incompatible VMM function table: %RX64 vs %RX64 (caller)"),
                                   ptrVM.vtable()->uMagicVersion, (uint64_t)aMagicVersion);
            }
            return hrc;
        }
    }

    return setError(E_ACCESSDENIED, tr("The method getUVMAndVMMFunctionTable is only for local calls"));
}



// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

void MachineDebugger::i_flushQueuedSettings()
{
    mFlushMode = true;
    if (mSingleStepQueued != -1)
    {
        COMSETTER(SingleStep)(mSingleStepQueued);
        mSingleStepQueued = -1;
    }
    for (unsigned i = 0; i < EMEXECPOLICY_END; i++)
        if (maiQueuedEmExecPolicyParams[i] != UINT8_MAX)
        {
            i_setEmExecPolicyProperty((EMEXECPOLICY)i, RT_BOOL(maiQueuedEmExecPolicyParams[i]));
            maiQueuedEmExecPolicyParams[i] = UINT8_MAX;
        }
    if (mLogEnabledQueued != -1)
    {
        COMSETTER(LogEnabled)(mLogEnabledQueued);
        mLogEnabledQueued = -1;
    }
    if (mVirtualTimeRateQueued != UINT32_MAX)
    {
        COMSETTER(VirtualTimeRate)(mVirtualTimeRateQueued);
        mVirtualTimeRateQueued = UINT32_MAX;
    }
    mFlushMode = false;
}

// private methods
/////////////////////////////////////////////////////////////////////////////

bool MachineDebugger::i_queueSettings() const
{
    if (!mFlushMode)
    {
        // check if the machine is running
        MachineState_T machineState;
        mParent->COMGETTER(State)(&machineState);
        switch (machineState)
        {
            // queue the request
            default:
                return true;

            case MachineState_Running:
            case MachineState_Paused:
            case MachineState_Stuck:
            case MachineState_LiveSnapshotting:
            case MachineState_Teleporting:
                break;
        }
    }
    return false;
}
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
