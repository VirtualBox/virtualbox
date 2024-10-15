/* $Id: MachineDebuggerImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
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

#ifndef MAIN_INCLUDED_MachineDebuggerImpl_h
#define MAIN_INCLUDED_MachineDebuggerImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "MachineDebuggerWrap.h"
#include <iprt/log.h>
#include <VBox/vmm/em.h>

class Console;
class Progress;

class ATL_NO_VTABLE MachineDebugger :
    public MachineDebuggerWrap
{

public:

    DECLARE_COMMON_CLASS_METHODS (MachineDebugger)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init (Console *aParent);
    void uninit() RT_OVERRIDE;

    // "public-private methods"
    void i_flushQueuedSettings();

private:

    // wrapped IMachineDeugger properties
    HRESULT getSingleStep(BOOL *aSingleStep) RT_OVERRIDE;
    HRESULT setSingleStep(BOOL aSingleStep) RT_OVERRIDE;
    HRESULT getExecuteAllInIEM(BOOL *aExecuteAllInIEM) RT_OVERRIDE;
    HRESULT setExecuteAllInIEM(BOOL aExecuteAllInIEM) RT_OVERRIDE;
    HRESULT getLogEnabled(BOOL *aLogEnabled) RT_OVERRIDE;
    HRESULT setLogEnabled(BOOL aLogEnabled) RT_OVERRIDE;
    HRESULT getLogDbgFlags(com::Utf8Str &aLogDbgFlags) RT_OVERRIDE;
    HRESULT getLogDbgGroups(com::Utf8Str &aLogDbgGroups) RT_OVERRIDE;
    HRESULT getLogDbgDestinations(com::Utf8Str &aLogDbgDestinations) RT_OVERRIDE;
    HRESULT getLogRelFlags(com::Utf8Str &aLogRelFlags) RT_OVERRIDE;
    HRESULT getLogRelGroups(com::Utf8Str &aLogRelGroups) RT_OVERRIDE;
    HRESULT getLogRelDestinations(com::Utf8Str &aLogRelDestinations) RT_OVERRIDE;
    HRESULT getExecutionEngine(VMExecutionEngine_T *apenmEngine) RT_OVERRIDE;
    HRESULT getHWVirtExNestedPagingEnabled(BOOL *aHWVirtExNestedPagingEnabled) RT_OVERRIDE;
    HRESULT getHWVirtExVPIDEnabled(BOOL *aHWVirtExVPIDEnabled) RT_OVERRIDE;
    HRESULT getHWVirtExUXEnabled(BOOL *aHWVirtExUXEnabled) RT_OVERRIDE;
    HRESULT getOSName(com::Utf8Str &aOSName) RT_OVERRIDE;
    HRESULT getOSVersion(com::Utf8Str &aOSVersion) RT_OVERRIDE;
    HRESULT getPAEEnabled(BOOL *aPAEEnabled) RT_OVERRIDE;
    HRESULT getVirtualTimeRate(ULONG *aVirtualTimeRate) RT_OVERRIDE;
    HRESULT setVirtualTimeRate(ULONG aVirtualTimeRate) RT_OVERRIDE;
    HRESULT getUptime(LONG64 *aUptime) RT_OVERRIDE;

    // wrapped IMachineDeugger methods
    HRESULT dumpGuestCore(const com::Utf8Str &aFilename,
                          const com::Utf8Str &aCompression) RT_OVERRIDE;
    HRESULT dumpHostProcessCore(const com::Utf8Str &aFilename,
                                const com::Utf8Str &aCompression) RT_OVERRIDE;
    HRESULT info(const com::Utf8Str &aName,
                 const com::Utf8Str &aArgs,
                 com::Utf8Str &aInfo) RT_OVERRIDE;
    HRESULT injectNMI() RT_OVERRIDE;
    HRESULT modifyLogGroups(const com::Utf8Str &aSettings) RT_OVERRIDE;
    HRESULT modifyLogFlags(const com::Utf8Str &aSettings) RT_OVERRIDE;
    HRESULT modifyLogDestinations(const com::Utf8Str &aSettings) RT_OVERRIDE;
    HRESULT readPhysicalMemory(LONG64 aAddress,
                               ULONG aSize,
                               std::vector<BYTE> &aBytes) RT_OVERRIDE;
    HRESULT writePhysicalMemory(LONG64 aAddress,
                                ULONG aSize,
                                const std::vector<BYTE> &aBytes) RT_OVERRIDE;
    HRESULT readVirtualMemory(ULONG aCpuId,
                              LONG64 aAddress,
                              ULONG aSize,
                              std::vector<BYTE> &aBytes) RT_OVERRIDE;
    HRESULT writeVirtualMemory(ULONG aCpuId,
                               LONG64 aAddress,
                               ULONG aSize,
                               const std::vector<BYTE> &aBytes) RT_OVERRIDE;
    HRESULT loadPlugIn(const com::Utf8Str &aName,
                       com::Utf8Str &aPlugInName) RT_OVERRIDE;
    HRESULT unloadPlugIn(const com::Utf8Str &aName) RT_OVERRIDE;
    HRESULT detectOS(com::Utf8Str &aOs) RT_OVERRIDE;
    HRESULT queryOSKernelLog(ULONG aMaxMessages,
                             com::Utf8Str &aDmesg) RT_OVERRIDE;
    HRESULT getRegister(ULONG aCpuId,
                        const com::Utf8Str &aName,
                        com::Utf8Str &aValue) RT_OVERRIDE;
    HRESULT getRegisters(ULONG aCpuId,
                         std::vector<com::Utf8Str> &aNames,
                         std::vector<com::Utf8Str> &aValues) RT_OVERRIDE;
    HRESULT setRegister(ULONG aCpuId,
                        const com::Utf8Str &aName,
                        const com::Utf8Str &aValue) RT_OVERRIDE;
    HRESULT setRegisters(ULONG aCpuId,
                         const std::vector<com::Utf8Str> &aNames,
                         const std::vector<com::Utf8Str> &aValues) RT_OVERRIDE;
    HRESULT dumpGuestStack(ULONG aCpuId,
                           com::Utf8Str &aStack) RT_OVERRIDE;
    HRESULT resetStats(const com::Utf8Str &aPattern) RT_OVERRIDE;
    HRESULT dumpStats(const com::Utf8Str &aPattern) RT_OVERRIDE;
    HRESULT getStats(const com::Utf8Str &aPattern,
                     BOOL aWithDescriptions,
                     com::Utf8Str &aStats) RT_OVERRIDE;
    HRESULT getCPULoad(ULONG aCpuId, ULONG *aPctExecuting, ULONG *aPctHalted, ULONG *aPctOther, LONG64 *aMsInterval) RT_OVERRIDE;
    HRESULT takeGuestSample(const com::Utf8Str &aFilename, ULONG aUsInterval, LONG64 aUsSampleTime, ComPtr<IProgress> &pProgress) RT_OVERRIDE;
    HRESULT getUVMAndVMMFunctionTable(LONG64 aMagicVersion, LONG64 *aVMMFunctionTable, LONG64 *aUVM) RT_OVERRIDE;

    // private methods
    bool i_queueSettings() const;
    HRESULT i_getEmExecPolicyProperty(EMEXECPOLICY enmPolicy, BOOL *pfEnforced);
    HRESULT i_setEmExecPolicyProperty(EMEXECPOLICY enmPolicy, BOOL fEnforce);

    /** RTLogGetFlags, RTLogGetGroupSettings and RTLogGetDestinations function. */
    typedef DECLCALLBACKTYPE(int, FNLOGGETSTR,(PRTLOGGER, char *, size_t));
    /** Function pointer.  */
    typedef FNLOGGETSTR *PFNLOGGETSTR;
    HRESULT i_logStringProps(PRTLOGGER pLogger, PFNLOGGETSTR pfnLogGetStr, const char *pszLogGetStr, Utf8Str *pstrSettings);

    static DECLCALLBACK(int) i_dbgfProgressCallback(void *pvUser, unsigned uPercentage);

    Console * const mParent;
    /** @name Flags whether settings have been queued because they could not be sent
     *        to the VM (not up yet, etc.)
     * @{ */
    uint8_t maiQueuedEmExecPolicyParams[EMEXECPOLICY_END];
    int mSingleStepQueued;
    int mLogEnabledQueued;
    uint32_t mVirtualTimeRateQueued;
    bool mFlushMode;
    /** @}  */

    /** @name Sample report related things.
     * @{ */
    /** Sample report handle. */
    DBGFSAMPLEREPORT        m_hSampleReport;
    /** Progress object for the currently taken guest sample. */
    ComObjPtr<Progress>     m_Progress;
    /** Filename to dump the report to. */
    com::Utf8Str            m_strFilename;
    /** @} */
};

#endif /* !MAIN_INCLUDED_MachineDebuggerImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
