/* $Id: VBoxManage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - VirtualBox command-line interface, internal header file.
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

#ifndef VBOX_INCLUDED_SRC_VBoxManage_VBoxManage_h
#define VBOX_INCLUDED_SRC_VBoxManage_VBoxManage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/com.h>
#include <VBox/com/ptr.h>
#include <VBox/com/VirtualBox.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>

#include <iprt/types.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/getopt.h>

#include "VBoxManageBuiltInHelp.h"
#include "PasswordInput.h"

#ifdef VBOX_WITH_VBOXMANAGE_NLS
# include "VirtualBoxTranslator.h"
#endif


////////////////////////////////////////////////////////////////////////////////
//
// definitions
//
////////////////////////////////////////////////////////////////////////////////

/**
 * This defines a a_CtxName::tr function that gives the translator context as
 * well as providing a shorter way to call VirtualBoxTranslator::translate.
 */
#ifdef VBOX_WITH_VBOXMANAGE_NLS
# define DECLARE_TRANSLATION_CONTEXT(a_CtxName) \
struct a_CtxName \
{ \
   static const char *tr(const char *pszSource, const char *pszComment = NULL, const size_t uNum = ~(size_t)0) \
   { \
       return VirtualBoxTranslator::translate(NULL, #a_CtxName, pszSource, pszComment, uNum); \
   } \
}
#else
# define DECLARE_TRANSLATION_CONTEXT(a_CtxName) \
struct a_CtxName \
{ \
   static const char *tr(const char *pszSource, const char *pszComment = NULL, const size_t uNum = ~(size_t)0) \
   { \
       RT_NOREF(pszComment, uNum); \
       return pszSource;  \
   } \
}
#endif

/**
 * Defines an option with two variants, producing two RTGETOPTDEF entries.
 *
 * This is mainly for replacing character-soup option names like
 * --natlocalhostreachable and --biossystemtimeoffset with more easily parsed
 * ones, like --nat-localhost-reachable and --bios-system-time-offset, without
 * removing the legacy name.
 */
#define OPT2(a_pszWordDashWord, a_pszWordSoup, a_chOptOrValue, a_fFlags) \
    { a_pszWordDashWord, a_chOptOrValue, a_fFlags }, \
    { a_pszWordSoup,     a_chOptOrValue, a_fFlags }

/** A single option variant of OPT2 for better looking tables. */
#define OPT1(a_pszOption, a_chOptOrValue, a_fFlags) \
    { a_pszOption, a_chOptOrValue, a_fFlags }


/** command handler argument */
struct HandlerArg
{
    int argc;
    char **argv;

    ComPtr<IVirtualBox> virtualBox;
    ComPtr<ISession> session;
};


/** showVMInfo details */
typedef enum
{
    VMINFO_NONE             = 0,
    VMINFO_STANDARD         = 1,    /**< standard details */
    VMINFO_FULL             = 2,    /**< both */
    VMINFO_MACHINEREADABLE  = 3,    /**< both, and make it machine readable */
    VMINFO_COMPACT          = 4
} VMINFO_DETAILS;


////////////////////////////////////////////////////////////////////////////////
//
// global variables
//
////////////////////////////////////////////////////////////////////////////////

extern bool g_fDetailedProgress;        // in VBoxManage.cpp


////////////////////////////////////////////////////////////////////////////////
//
// prototypes
//
////////////////////////////////////////////////////////////////////////////////

/* VBoxManageHelp.cpp */
void        setCurrentCommand(enum HELP_CMD_VBOXMANAGE enmCommand);
void        setCurrentSubcommand(uint64_t fCurSubcommandScope);

void        printUsage(PRTSTREAM pStrm);
void        printHelp(PRTSTREAM pStrm);
RTEXITCODE  errorNoSubcommand(void);
RTEXITCODE  errorUnknownSubcommand(const char *pszSubCmd);
RTEXITCODE  errorTooManyParameters(char **papszArgs);
RTEXITCODE  errorGetOpt(int rcGetOpt, union RTGETOPTUNION const *pValueUnion);
RTEXITCODE  errorFetchValue(int iValueNo, const char *pszOption, int rcGetOptFetchValue, union RTGETOPTUNION const *pValueUnion);
RTEXITCODE  errorSyntax(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);
RTEXITCODE  errorSyntaxV(const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);
HRESULT     errorSyntaxHr(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);
RTEXITCODE  errorArgument(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);
HRESULT     errorArgumentHr(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

# define SHOW_PROGRESS_NONE     0
# define SHOW_PROGRESS_DESC     RT_BIT_32(0)
# define SHOW_PROGRESS          RT_BIT_32(1)
/** Shows detailed information of the progress.
 *  Mutually exclusive with SHOW_PROGRESS_OPS. */
# define SHOW_PROGRESS_DETAILS  RT_BIT_32(2)
/** Only shows the operation descriptions without other details.
 *  Mutually exclusive with SHOW_PROGRESS_DETAILS. */
# define SHOW_PROGRESS_OPS      RT_BIT_32(3)
HRESULT showProgress(ComPtr<IProgress> progress, uint32_t fFlags = SHOW_PROGRESS);

/* VBoxManage.cpp */
void showLogo(PRTSTREAM pStrm);

/* VBoxInternalManage.cpp */
DECLHIDDEN(void) printUsageInternalCmds(PRTSTREAM pStrm);
RTEXITCODE handleInternalCommands(HandlerArg *a);

/* VBoxManageControlVM.cpp */
RTEXITCODE handleControlVM(HandlerArg *a);

/* VBoxManageModifyVM.cpp */
void parseGroups(const char *pcszGroups, com::SafeArray<BSTR> *pGroups);
#ifdef VBOX_WITH_RECORDING
int parseScreens(const char *pcszScreens, com::SafeArray<BOOL> *pScreens);
#endif
RTEXITCODE handleModifyVM(HandlerArg *a);
VMProcPriority_T nameToVMProcPriority(const char *pszName);

/* VBoxManageDebugVM.cpp */
RTEXITCODE handleDebugVM(HandlerArg *a);

/* VBoxManageGuestProp.cpp */
RTEXITCODE handleGuestProperty(HandlerArg *a);

/* VBoxManageGuestCtrl.cpp */
RTEXITCODE handleGuestControl(HandlerArg *a);

/* VBoxManageVMInfo.cpp */
HRESULT showSnapshots(ComPtr<ISnapshot> &rootSnapshot,
                      ComPtr<ISnapshot> &currentSnapshot,
                      VMINFO_DETAILS details,
                      const com::Utf8Str &prefix = "",
                      int level = 0);
RTEXITCODE handleShowVMInfo(HandlerArg *a);
HRESULT showVMInfo(ComPtr<IVirtualBox> pVirtualBox,
                   ComPtr<IMachine> pMachine,
                   ComPtr<ISession> pSession,
                   VMINFO_DETAILS details = VMINFO_NONE);
const char *machineStateToName(MachineState_T machineState, bool fShort);
HRESULT showBandwidthGroups(ComPtr<IBandwidthControl> &bwCtrl,
                            VMINFO_DETAILS details);
void outputMachineReadableString(const char *pszName, const char *pszValue, bool fQuoteName = false, bool fNewline = true);
void outputMachineReadableString(const char *pszName, com::Bstr const *pbstrValue, bool fQuoteName = false, bool fNewline = true);
void outputMachineReadableStringWithFmtName(const char *pszValue, bool fQuoteName, const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR(3, 4);
void outputMachineReadableStringWithFmtName(com::Bstr const *pbstrValue, bool fQuoteName, const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR(3, 4);
void outputMachineReadableBool(const char *pszName, BOOL const *pfValue);
void outputMachineReadableBool(const char *pszName, bool const *pfValue);
void outputMachineReadableULong(const char *pszName, ULONG *uValue);
void outputMachineReadableLong64(const char *pszName, LONG64 *uValue);

/* VBoxManageList.cpp */
RTEXITCODE handleList(HandlerArg *a);

/* VBoxManageMetrics.cpp */
RTEXITCODE handleMetrics(HandlerArg *a);

/* VBoxManageMisc.cpp */
RTEXITCODE handleRegisterVM(HandlerArg *a);
RTEXITCODE handleUnregisterVM(HandlerArg *a);
RTEXITCODE handleCreateVM(HandlerArg *a);
RTEXITCODE handleCloneVM(HandlerArg *a);
RTEXITCODE handleStartVM(HandlerArg *a);
#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
RTEXITCODE handleEncryptVM(HandlerArg *a);
#endif
RTEXITCODE handleDiscardState(HandlerArg *a);
RTEXITCODE handleAdoptState(HandlerArg *a);
RTEXITCODE handleGetExtraData(HandlerArg *a);
RTEXITCODE handleSetExtraData(HandlerArg *a);
RTEXITCODE handleSetProperty(HandlerArg *a);
RTEXITCODE handleSharedFolder(HandlerArg *a);
RTEXITCODE handleExtPack(HandlerArg *a);
RTEXITCODE handleUnattended(HandlerArg *a);
RTEXITCODE handleMoveVM(HandlerArg *a);
RTEXITCODE handleCloudProfile(HandlerArg *a);

/* VBoxManageDisk.cpp */
HRESULT openMedium(HandlerArg *a, const char *pszFilenameOrUuid,
                   DeviceType_T enmDevType, AccessMode_T enmAccessMode,
                   ComPtr<IMedium> &pMedium, bool fForceNewUuidOnOpen,
                   bool fSilent);
RTEXITCODE handleCreateMedium(HandlerArg *a);
RTEXITCODE handleModifyMedium(HandlerArg *a);
RTEXITCODE handleCloneMedium(HandlerArg *a);
RTEXITCODE handleMediumProperty(HandlerArg *a);
RTEXITCODE handleEncryptMedium(HandlerArg *a);
RTEXITCODE handleCheckMediumPassword(HandlerArg *a);
RTEXITCODE handleConvertFromRaw(HandlerArg *a);
HRESULT showMediumInfo(const ComPtr<IVirtualBox> &pVirtualBox,
                       const ComPtr<IMedium> &pMedium,
                       const char *pszParentUUID,
                       bool fOptLong);
RTEXITCODE handleShowMediumInfo(HandlerArg *a);
RTEXITCODE handleCloseMedium(HandlerArg *a);
RTEXITCODE handleMediumIO(HandlerArg *a);
int parseMediumType(const char *psz, MediumType_T *penmMediumType);
int parseBool(const char *psz, bool *pb);

/* VBoxManageStorageController.cpp */
RTEXITCODE handleStorageAttach(HandlerArg *a);
RTEXITCODE handleStorageController(HandlerArg *a);

// VBoxManageAppliance.cpp
RTEXITCODE handleImportAppliance(HandlerArg *a);
RTEXITCODE handleExportAppliance(HandlerArg *a);
RTEXITCODE handleSignAppliance(HandlerArg *a);

// VBoxManageSnapshot.cpp
RTEXITCODE handleSnapshot(HandlerArg *a);

/* VBoxManageUSB.cpp */
RTEXITCODE handleUSBFilter(HandlerArg *a);
RTEXITCODE handleUSBDevSource(HandlerArg *a);

/* VBoxManageHostonly.cpp */
RTEXITCODE handleHostonlyIf(HandlerArg *a);
#ifdef VBOX_WITH_VMNET
RTEXITCODE handleHostonlyNet(HandlerArg *a);
#endif /* VBOX_WITH_VMNET */

/* VBoxManageDHCPServer.cpp */
RTEXITCODE handleDHCPServer(HandlerArg *a);

/* VBoxManageNATNetwork.cpp */
RTEXITCODE handleNATNetwork(HandlerArg *a);
RTEXITCODE listNATNetworks(bool fLong, bool fSorted,
                           const ComPtr<IVirtualBox> &pVirtualBox);

/* VBoxManageBandwidthControl.cpp */
RTEXITCODE handleBandwidthControl(HandlerArg *a);

/* VBoxManageCloud.cpp */
RTEXITCODE handleCloud(HandlerArg *a);

/* VBoxManageCloudMachine.cpp */
RTEXITCODE handleCloudMachine(HandlerArg *a, int iFirst,
                              const char *pcszProviderName,
                              const char *pcszProfileName);
RTEXITCODE listCloudMachines(HandlerArg *a, int iFirst,
                              const char *pcszProviderName,
                              const char *pcszProfileName);
RTEXITCODE handleCloudShowVMInfo(HandlerArg *a, int iFirst,
                                 const char *pcszProviderName,
                                 const char *pcszProfileName);

#ifdef VBOX_WITH_UPDATE_AGENT
/* VBoxManageUpdateCheck.cpp */
RTEXITCODE handleUpdateCheck(HandlerArg *a);
#endif

/* VBoxManageModifyNvram.cpp */
RTEXITCODE handleModifyNvram(HandlerArg *a);

#endif /* !VBOX_INCLUDED_SRC_VBoxManage_VBoxManage_h */
