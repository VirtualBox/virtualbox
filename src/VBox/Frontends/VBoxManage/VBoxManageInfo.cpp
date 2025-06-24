/* $Id: VBoxManageInfo.cpp 109948 2025-06-24 19:59:46Z jack.doherty@oracle.com $ */
/** @file
 * VBoxManage - The 'showvminfo' command and helper routines.
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
#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <VBox/com/VirtualBox.h>

#ifdef VBOX_WITH_PCI_PASSTHROUGH
#include <VBox/pci.h>
#endif

#include <VBox/log.h>
#include <VBox/version.h>
#include <iprt/stream.h>
#include <iprt/time.h>
#include <iprt/string.h>
#include <iprt/getopt.h>
#include <iprt/ctype.h>

#include "VBoxManage.h"
#include "VBoxManageUtils.h"

using namespace com;

DECLARE_TRANSLATION_CONTEXT(Info);


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define SHOW_UTF8_STRING(a_pszMachine, a_pszHuman, a_szValue) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        if (details == VMINFO_MACHINEREADABLE) \
            outputMachineReadableString(a_pszMachine, a_szValue); \
        else \
            RTPrintf("%-28s %s\n", a_pszHuman, a_szValue); \
    } while (0)

#define SHOW_BSTR_STRING(a_pszMachine, a_pszHuman, a_bstrValue) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        if (details == VMINFO_MACHINEREADABLE) \
            outputMachineReadableString(a_pszMachine, &a_bstrValue); \
        else \
            RTPrintf("%-28s %ls\n", a_pszHuman, a_bstrValue.raw()); \
    } while (0)

#define SHOW_BOOL_VALUE_EX(a_pszMachine, a_pszHuman, a_fValue, a_szTrue, a_szFalse) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        if (details == VMINFO_MACHINEREADABLE) \
            outputMachineReadableString(a_pszMachine, a_fValue ? "on" : "off"); \
        else \
            RTPrintf("%-28s %s\n", a_pszHuman, a_fValue ? a_szTrue: a_szFalse); \
    } while (0)

#define SHOW_BOOL_VALUE(a_pszMachine, a_pszHuman, a_fValue) \
    SHOW_BOOL_VALUE_EX(a_pszMachine, a_pszHuman, a_fValue, Info::tr("enabled"), Info::tr("disabled"))

#define SHOW_ULONG_VALUE(a_pszMachine, a_pszHuman, a_uValue, a_pszUnit) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        if (details == VMINFO_MACHINEREADABLE) \
            RTPrintf("%s=%u\n", a_pszMachine, a_uValue); \
        else \
            RTPrintf("%-28s %u%s\n", a_pszHuman, a_uValue, a_pszUnit); \
    } while (0)

#define SHOW_LONG64_VALUE(a_pszMachine, a_pszHuman, a_llValue, a_pszUnit) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        if (details == VMINFO_MACHINEREADABLE) \
            RTPrintf("%s=%lld\n", a_pszMachine, a_llValue); \
        else \
            RTPrintf("%-28s %lld%s\n", a_pszHuman, a_llValue, a_pszUnit); \
    } while (0)

#define SHOW_BOOLEAN_PROP(a_pObj, a_Prop, a_pszMachine, a_pszHuman) \
    SHOW_BOOLEAN_PROP_EX(a_pObj, a_Prop, a_pszMachine, a_pszHuman, Info::tr("enabled"), Info::tr("disabled"))

#define SHOW_BOOLEAN_PROP_EX(a_pObj, a_Prop, a_pszMachine, a_pszHuman, a_szTrue, a_szFalse) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        BOOL f; \
        CHECK_ERROR2I_RET(a_pObj, COMGETTER(a_Prop)(&f), hrcCheck); \
        if (details == VMINFO_MACHINEREADABLE) \
            outputMachineReadableString(a_pszMachine, f ? "on" : "off"); \
        else \
            RTPrintf("%-28s %s\n", a_pszHuman, f ? a_szTrue : a_szFalse); \
    } while (0)

#define SHOW_BOOLEAN_METHOD(a_pObj, a_Invocation, a_pszMachine, a_pszHuman) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        BOOL f; \
        CHECK_ERROR2I_RET(a_pObj, a_Invocation, hrcCheck); \
        if (details == VMINFO_MACHINEREADABLE) \
            outputMachineReadableString(a_pszMachine, f ? "on" : "off"); \
        else \
            RTPrintf("%-28s %s\n", a_pszHuman, f ? Info::tr("enabled") : Info::tr("disabled")); \
    } while (0)

#define SHOW_STRING_PROP(a_pObj, a_Prop, a_pszMachine, a_pszHuman) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        Bstr bstr; \
        CHECK_ERROR2I_RET(a_pObj, COMGETTER(a_Prop)(bstr.asOutParam()), hrcCheck); \
        if (details == VMINFO_MACHINEREADABLE) \
            outputMachineReadableString(a_pszMachine, &bstr); \
        else \
            RTPrintf("%-28s %ls\n", a_pszHuman, bstr.raw()); \
    } while (0)

#define SHOW_STRING_PROP_NOT_EMPTY(a_pObj, a_Prop, a_pszMachine, a_pszHuman) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        Bstr bstr; \
        CHECK_ERROR2I_RET(a_pObj, COMGETTER(a_Prop)(bstr.asOutParam()), hrcCheck); \
        if (bstr.isNotEmpty()) \
        { \
            if (details == VMINFO_MACHINEREADABLE) \
                outputMachineReadableString(a_pszMachine, &bstr); \
            else \
                RTPrintf("%-28s %ls\n", a_pszHuman, bstr.raw()); \
        } \
    } while (0)

    /** @def SHOW_STRING_PROP_MAJ
     * For not breaking the output in a dot release we don't show default values. */
#define SHOW_STRING_PROP_MAJ(a_pObj, a_Prop, a_pszMachine, a_pszHuman, a_pszUnless, a_uMajorVer) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        Bstr bstr; \
        CHECK_ERROR2I_RET(a_pObj, COMGETTER(a_Prop)(bstr.asOutParam()), hrcCheck); \
        if ((a_uMajorVer) <= VBOX_VERSION_MAJOR || !bstr.equals(a_pszUnless)) \
        { \
            if (details == VMINFO_MACHINEREADABLE)\
                outputMachineReadableString(a_pszMachine, &bstr); \
            else \
                RTPrintf("%-28s %ls\n", a_pszHuman, bstr.raw()); \
        } \
    } while (0)

#define SHOW_STRINGARRAY_PROP(a_pObj, a_Prop, a_pszMachine, a_pszHuman) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        SafeArray<BSTR> array; \
        CHECK_ERROR2I_RET(a_pObj, COMGETTER(a_Prop)(ComSafeArrayAsOutParam(array)), hrcCheck); \
        Utf8Str str; \
        for (size_t i = 0; i < array.size(); i++) \
        { \
            if (i != 0) \
                str.append(","); \
            str.append(Utf8Str(array[i]).c_str()); \
        } \
        Bstr bstr(str); \
        if (details == VMINFO_MACHINEREADABLE) \
            outputMachineReadableString(a_pszMachine, &bstr); \
        else \
            RTPrintf("%-28s %ls\n", a_pszHuman, bstr.raw()); \
    } while (0)

#define SHOW_UUID_PROP(a_pObj, a_Prop, a_pszMachine, a_pszHuman) \
    SHOW_STRING_PROP(a_pObj, a_Prop, a_pszMachine, a_pszHuman)

#define SHOW_USHORT_PROP_EX2(a_pObj, a_Prop, a_pszMachine, a_pszHuman, a_pszUnit, a_szFmtMachine, a_szFmtHuman) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        USHORT u16 = 0; \
        CHECK_ERROR2I_RET(a_pObj, COMGETTER(a_Prop)(&u16), hrcCheck); \
        if (details == VMINFO_MACHINEREADABLE) \
            RTPrintf("%s=" a_szFmtMachine "\n", a_pszMachine, u16); \
        else \
            RTPrintf("%-28s " a_szFmtHuman "%s\n", a_pszHuman, u16, u16, a_pszUnit); \
    } while (0)

#define SHOW_ULONG_PROP(a_pObj, a_Prop, a_pszMachine, a_pszHuman, a_pszUnit) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        ULONG u32 = 0; \
        CHECK_ERROR2I_RET(a_pObj, COMGETTER(a_Prop)(&u32), hrcCheck); \
        if (details == VMINFO_MACHINEREADABLE) \
            RTPrintf("%s=%u\n", a_pszMachine, u32); \
        else \
            RTPrintf("%-28s %u%s\n", a_pszHuman, u32, a_pszUnit); \
    } while (0)

#define SHOW_LONG64_PROP(a_pObj, a_Prop, a_pszMachine, a_pszHuman, a_pszUnit) \
    do \
    { \
        Assert(a_pszHuman[strlen(a_pszHuman) - 1] == ':'); \
        LONG64 i64 = 0; \
        CHECK_ERROR2I_RET(a_pObj, COMGETTER(a_Prop)(&i64), hrcCheck); \
        if (details == VMINFO_MACHINEREADABLE) \
            RTPrintf("%s=%lld\n", a_pszMachine, i64); \
        else \
            RTPrintf("%-28s %'lld%s\n", a_pszHuman, i64, a_pszUnit); \
    } while (0)


// funcs
///////////////////////////////////////////////////////////////////////////////

/**
 * Helper for formatting an indexed name or some such thing.
 */
static const char *FmtNm(char psz[80], const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTStrPrintfV(psz, 80, pszFormat, va);
    va_end(va);
    return psz;
}

HRESULT showSnapshots(ComPtr<ISnapshot> &rootSnapshot,
                      ComPtr<ISnapshot> &currentSnapshot,
                      VMINFO_DETAILS details,
                      const Utf8Str &prefix /* = ""*/,
                      int level /*= 0*/)
{
    /* start with the root */
    Bstr name;
    Bstr uuid;
    Bstr description;
    CHECK_ERROR2I_RET(rootSnapshot, COMGETTER(Name)(name.asOutParam()), hrcCheck);
    CHECK_ERROR2I_RET(rootSnapshot, COMGETTER(Id)(uuid.asOutParam()), hrcCheck);
    CHECK_ERROR2I_RET(rootSnapshot, COMGETTER(Description)(description.asOutParam()), hrcCheck);
    bool fCurrent = (rootSnapshot == currentSnapshot);
    if (details == VMINFO_MACHINEREADABLE)
    {
        /* print with hierarchical numbering */
        RTPrintf("SnapshotName%s=\"%ls\"\n", prefix.c_str(), name.raw());
        RTPrintf("SnapshotUUID%s=\"%s\"\n", prefix.c_str(), Utf8Str(uuid).c_str());
        if (!description.isEmpty())
            RTPrintf("SnapshotDescription%s=\"%ls\"\n", prefix.c_str(), description.raw());
        if (fCurrent)
        {
            RTPrintf("CurrentSnapshotName=\"%ls\"\n", name.raw());
            RTPrintf("CurrentSnapshotUUID=\"%s\"\n", Utf8Str(uuid).c_str());
            RTPrintf("CurrentSnapshotNode=\"SnapshotName%s\"\n", prefix.c_str());
        }
    }
    else
    {
        /* print with indentation */
        RTPrintf(Info::tr("   %sName: %ls (UUID: %s)%s\n"),
                 prefix.c_str(),
                 name.raw(),
                 Utf8Str(uuid).c_str(),
                 (fCurrent) ? " *" : "");
        if (!description.isEmpty() && RTUtf16Chr(description.raw(), '\n') == NULL)
            RTPrintf(Info::tr("   %sDescription: %ls\n"), prefix.c_str(), description.raw());
        else if (!description.isEmpty())
            RTPrintf(Info::tr("   %sDescription:\n%ls\n"), prefix.c_str(), description.raw());
    }

    /* get the children */
    HRESULT hrc = S_OK;
    SafeIfaceArray <ISnapshot> coll;
    CHECK_ERROR2I_RET(rootSnapshot,COMGETTER(Children)(ComSafeArrayAsOutParam(coll)), hrcCheck);
    if (!coll.isNull())
    {
        for (size_t index = 0; index < coll.size(); ++index)
        {
            ComPtr<ISnapshot> snapshot = coll[index];
            if (snapshot)
            {
                Utf8Str newPrefix;
                if (details == VMINFO_MACHINEREADABLE)
                    newPrefix.printf("%s-%d", prefix.c_str(), index + 1);
                else
                    newPrefix.printf("%s   ", prefix.c_str());

                /* recursive call */
                HRESULT hrc2 = showSnapshots(snapshot, currentSnapshot, details, newPrefix, level + 1);
                if (FAILED(hrc2))
                    hrc = hrc2;
            }
        }
    }
    return hrc;
}

static void makeTimeStr(char *s, int cb, int64_t millies)
{
    RTTIME t;
    RTTIMESPEC ts;

    RTTimeSpecSetMilli(&ts, millies);

    RTTimeExplode(&t, &ts);

    RTStrPrintf(s, cb, "%04d/%02d/%02d %02d:%02d:%02d UTC",
                        t.i32Year, t.u8Month, t.u8MonthDay,
                        t.u8Hour, t.u8Minute, t.u8Second);
}

const char *machineStateToName(MachineState_T machineState, bool fShort)
{
    switch (machineState)
    {
        case MachineState_PoweredOff:
            return fShort ? "poweroff"             : Info::tr("powered off");
        case MachineState_Saved:
            return fShort ? "saved"                : Info::tr("saved");
        case MachineState_Teleported:
            return fShort ? "teleported"           : Info::tr("teleported");
        case MachineState_Aborted:
            return fShort ? "aborted"              : Info::tr("aborted");
        case MachineState_AbortedSaved:
            return fShort ? "aborted-saved"        : Info::tr("aborted-saved");
        case MachineState_Running:
            return fShort ? "running"              : Info::tr("running");
        case MachineState_Paused:
            return fShort ? "paused"               : Info::tr("paused");
        case MachineState_Stuck:
            return fShort ? "gurumeditation"       : Info::tr("guru meditation");
        case MachineState_Teleporting:
            return fShort ? "teleporting"          : Info::tr("teleporting");
        case MachineState_LiveSnapshotting:
            return fShort ? "livesnapshotting"     : Info::tr("live snapshotting");
        case MachineState_Starting:
            return fShort ? "starting"             : Info::tr("starting");
        case MachineState_Stopping:
            return fShort ? "stopping"             : Info::tr("stopping");
        case MachineState_Saving:
            return fShort ? "saving"               : Info::tr("saving");
        case MachineState_Restoring:
            return fShort ? "restoring"            : Info::tr("restoring");
        case MachineState_TeleportingPausedVM:
            return fShort ? "teleportingpausedvm"  : Info::tr("teleporting paused vm");
        case MachineState_TeleportingIn:
            return fShort ? "teleportingin"        : Info::tr("teleporting (incoming)");
        case MachineState_DeletingSnapshotOnline:
            return fShort ? "deletingsnapshotlive" : Info::tr("deleting snapshot live");
        case MachineState_DeletingSnapshotPaused:
            return fShort ? "deletingsnapshotlivepaused" : Info::tr("deleting snapshot live paused");
        case MachineState_OnlineSnapshotting:
            return fShort ? "onlinesnapshotting"   : Info::tr("online snapshotting");
        case MachineState_RestoringSnapshot:
            return fShort ? "restoringsnapshot"    : Info::tr("restoring snapshot");
        case MachineState_DeletingSnapshot:
            return fShort ? "deletingsnapshot"     : Info::tr("deleting snapshot");
        case MachineState_SettingUp:
            return fShort ? "settingup"            : Info::tr("setting up");
        case MachineState_Snapshotting:
            return fShort ? "snapshotting"         : Info::tr("offline snapshotting");
        default:
            break;
    }
    return Info::tr("unknown");
}

static const char *facilityStateToName(AdditionsFacilityStatus_T faStatus, bool fShort)
{
    switch (faStatus)
    {
        case AdditionsFacilityStatus_Inactive:
            return fShort ? "inactive"    : Info::tr("not active");
        case AdditionsFacilityStatus_Paused:
            return fShort ? "paused"      : Info::tr("paused");
        case AdditionsFacilityStatus_PreInit:
            return fShort ? "preinit"     : Info::tr("pre-initializing");
        case AdditionsFacilityStatus_Init:
            return fShort ? "init"        : Info::tr("initializing");
        case AdditionsFacilityStatus_Active:
            return fShort ? "active"      : Info::tr("active/running");
        case AdditionsFacilityStatus_Terminating:
            return fShort ? "terminating" : Info::tr("terminating");
        case AdditionsFacilityStatus_Terminated:
            return fShort ? "terminated"  : Info::tr("terminated");
        case AdditionsFacilityStatus_Failed:
            return fShort ? "failed"      : Info::tr("failed");
        case AdditionsFacilityStatus_Unknown:
        default:
            break;
    }
    return Info::tr("unknown");
}

static const char *storageControllerTypeToName(StorageControllerType_T enmCtlType, bool fMachineReadable = false)
{
    switch (enmCtlType)
    {
        case StorageControllerType_LsiLogic:
            return "LsiLogic";
        case StorageControllerType_LsiLogicSas:
            return "LsiLogicSas";
        case StorageControllerType_BusLogic:
            return "BusLogic";
        case StorageControllerType_IntelAhci:
            return "IntelAhci";
        case StorageControllerType_PIIX3:
            return "PIIX3";
        case StorageControllerType_PIIX4:
            return "PIIX4";
        case StorageControllerType_ICH6:
            return "ICH6";
        case StorageControllerType_I82078:
            return "I82078";
        case StorageControllerType_USB:
            return "USB";
        case StorageControllerType_NVMe:
            return "NVMe";
        case StorageControllerType_VirtioSCSI:
            return "VirtioSCSI";
        default:
            return fMachineReadable ? "unknown" : Info::tr("unknown");
    }
}


DECLINLINE(bool) doesMachineReadableStringNeedEscaping(const char *psz)
{
    return psz == NULL
        || *psz == '\0'
        || strchr(psz, '"') != NULL
        || strchr(psz, '\\') != NULL;
}


/**
 * This simply outputs the string adding necessary escaping and nothing else.
 */
static void outputMachineReadableStringWorker(const char *psz)
{
    for (;;)
    {
        const char *pszDoubleQuote = strchr(psz, '"');
        const char *pszSlash       = strchr(psz, '\\');
        const char *pszNext;
        if (pszSlash)
            pszNext = !pszDoubleQuote || (uintptr_t)pszSlash < (uintptr_t)pszDoubleQuote ? pszSlash : pszDoubleQuote;
        else if (pszDoubleQuote)
            pszNext = pszDoubleQuote;
        else
        {
            RTStrmWrite(g_pStdOut, psz, strlen(psz));
            break;
        }
        RTStrmWrite(g_pStdOut, psz, pszNext - psz);
        char const szTmp[2] = { '\\', *pszNext };
        RTStrmWrite(g_pStdOut, szTmp, sizeof(szTmp));

        psz = pszNext + 1;
    }
}


/**
 * This takes care of escaping double quotes and slashes that the string might
 * contain.
 *
 * @param   pszName     The variable name.
 * @param   pszValue    The value.
 * @param   fQuoteName  Whether to unconditionally quote the name or not.
 * @param   fNewline    Whether to automatically add a newline after the value.
 */
void outputMachineReadableString(const char *pszName, const char *pszValue, bool fQuoteName /*=false*/, bool fNewline /*=true*/)
{
    if (!fQuoteName)
        fQuoteName = strchr(pszName, '=') != NULL;
    bool const fEscapeName  = doesMachineReadableStringNeedEscaping(pszName);
    bool const fEscapeValue = doesMachineReadableStringNeedEscaping(pszValue);
    if (!fEscapeName && !fEscapeValue)
    {
        if (fNewline)
            RTPrintf(!fQuoteName ? "%s=\"%s\"\n" : "\"%s\"=\"%s\"\n", pszName, pszValue);
        else
            RTPrintf(!fQuoteName ? "%s=\"%s\""   : "\"%s\"=\"%s\"",   pszName, pszValue);
    }
    else
    {
        /* The name and string quotation: */
        if (!fEscapeName)
            RTPrintf(fQuoteName ? "\"%s\"=\"" : "%s=\"", pszName);
        else
        {
            if (fQuoteName)
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE("\""));
            outputMachineReadableStringWorker(pszName);
            if (fQuoteName)
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE("\"=\""));
            else
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE("=\""));
        }

        /* the value and the closing quotation */
        outputMachineReadableStringWorker(pszValue);
        if (fNewline)
            RTStrmWrite(g_pStdOut, RT_STR_TUPLE("\"\n"));
        else
            RTStrmWrite(g_pStdOut, RT_STR_TUPLE("\""));
    }
}


/**
 * This takes care of escaping double quotes and slashes that the string might
 * contain.
 *
 * @param   pszName     The variable name.
 * @param   pbstrValue  The value.
 * @param   fQuoteName  Whether to unconditionally quote the name or not.
 * @param   fNewline    Whether to automatically add a newline after the value.
 */
void outputMachineReadableString(const char *pszName, Bstr const *pbstrValue, bool fQuoteName /*=false*/, bool fNewline /*=true*/)
{
    com::Utf8Str strValue(*pbstrValue);
    outputMachineReadableString(pszName, strValue.c_str(), fQuoteName, fNewline);
}


/**
 * Variant that allows formatting the name string, C string value.
 *
 * @param   pszValue    The value.
 * @param   fQuoteName  Whether to unconditionally quote the name or not.
 * @param   pszNameFmt  The variable name.
 */
void outputMachineReadableStringWithFmtName(const char *pszValue, bool fQuoteName, const char *pszNameFmt, ...)
{
    com::Utf8Str strName;
    va_list va;
    va_start(va, pszNameFmt);
    strName.printfV(pszNameFmt, va);
    va_end(va);

    outputMachineReadableString(strName.c_str(), pszValue, fQuoteName);
}


/**
 * Variant that allows formatting the name string, Bstr value.
 *
 * @param   pbstrValue  The value.
 * @param   fQuoteName  Whether to unconditionally quote the name or not.
 * @param   pszNameFmt  The variable name.
 */
void outputMachineReadableStringWithFmtName(com::Bstr const *pbstrValue, bool fQuoteName, const char *pszNameFmt, ...)
{
    com::Utf8Str strName;
    va_list va;
    va_start(va, pszNameFmt);
    strName.printfV(pszNameFmt, va);
    va_end(va);

    outputMachineReadableString(strName.c_str(), pbstrValue, fQuoteName);
}


/**
 * Machine readable outputting of a boolean value.
 */
void outputMachineReadableBool(const char *pszName, BOOL const *pfValue)
{
    RTPrintf("%s=\"%s\"\n", pszName, *pfValue ? "on" : "off");
}


/**
 * Machine readable outputting of a boolean value.
 */
void outputMachineReadableBool(const char *pszName, bool const *pfValue)
{
    RTPrintf("%s=\"%s\"\n", pszName, *pfValue ? "on" : "off");
}


/**
 * Machine readable outputting of a ULONG value.
 */
void outputMachineReadableULong(const char *pszName, ULONG *puValue)
{
    RTPrintf("%s=\"%u\"\n", pszName, *puValue);
}


/**
 * Machine readable outputting of a LONG64 value.
 */
void outputMachineReadableLong64(const char *pszName, LONG64 *puValue)
{
    RTPrintf("%s=\"%llu\"\n", pszName, *puValue);
}


/**
 * Helper for parsing extra data config.
 * @returns true, false, or -1 if invalid.
 */
static int parseCfgmBool(Bstr const *pbstr)
{
    /* GetExtraData returns empty strings if the requested data wasn't
       found, so fend that off first: */
    size_t cwcLeft = pbstr->length();
    if (!cwcLeft)
        return false;
    PCRTUTF16 pwch = pbstr->raw();

    /* Skip type prefix: */
    if (   cwcLeft >= 8
        && pwch[0] == 'i'
        && pwch[1] == 'n'
        && pwch[2] == 't'
        && pwch[3] == 'e'
        && pwch[4] == 'g'
        && pwch[5] == 'e'
        && pwch[6] == 'r'
        && pwch[7] == ':')
    {
        pwch    += 8;
        cwcLeft -= 8;
    }

    /* Hex prefix? */
    bool fHex = false;
    if (   cwcLeft >= 2
        && pwch[0] == '0'
        && (pwch[1] == 'x' || pwch[1] == 'X'))
    {
        pwch    += 2;
        cwcLeft -= 2;
        fHex     = true;
    }

    /* Empty string is wrong: */
    if (cwcLeft == 0)
        return -1;

    /* Check that it's all digits and return when we find a non-zero
       one or reaches the end: */
    do
    {
        RTUTF16 const wc = *pwch++;
        if (!RT_C_IS_DIGIT(wc) && (!fHex || !RT_C_IS_XDIGIT(wc)))
            return -1;
        if (wc != '0')
            return true;
    } while (--cwcLeft > 0);
    return false;
}


/**
 * Converts bandwidth group type to a string.
 * @returns String representation.
 * @param   enmType         Bandwidth control group type.
 */
static const char * bwGroupTypeToString(BandwidthGroupType_T enmType)
{
    switch (enmType)
    {
        case BandwidthGroupType_Null:    return Info::tr("Null");
        case BandwidthGroupType_Disk:    return Info::tr("Disk");
        case BandwidthGroupType_Network: return Info::tr("Network");
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
        case BandwidthGroupType_32BitHack: break; /* Shut up compiler warnings. */
#endif
    }
    return Info::tr("unknown");
}

HRESULT showBandwidthGroups(ComPtr<IBandwidthControl> &bwCtrl,
                            VMINFO_DETAILS details)
{
    SafeIfaceArray<IBandwidthGroup> bwGroups;
    CHECK_ERROR2I_RET(bwCtrl, GetAllBandwidthGroups(ComSafeArrayAsOutParam(bwGroups)), hrcCheck);

    if (details != VMINFO_MACHINEREADABLE)
        RTPrintf(bwGroups.size() != 0 ? "\n" : Info::tr("<none>\n"));
    for (size_t i = 0; i < bwGroups.size(); i++)
    {
        Bstr strName;
        CHECK_ERROR2I_RET(bwGroups[i], COMGETTER(Name)(strName.asOutParam()), hrcCheck);
        BandwidthGroupType_T enmType;
        CHECK_ERROR2I_RET(bwGroups[i], COMGETTER(Type)(&enmType), hrcCheck);
        LONG64 cbMaxPerSec;
        CHECK_ERROR2I_RET(bwGroups[i], COMGETTER(MaxBytesPerSec)(&cbMaxPerSec), hrcCheck);

        const char *pszType = bwGroupTypeToString(enmType);
        if (details == VMINFO_MACHINEREADABLE)
        {
            /* Complicated condensed format. */
            char szName[64];
            RTStrPrintf(szName, sizeof(szName), "BandwidthGroup%zu", i);
            outputMachineReadableString(szName, &strName, false /*fQuoteName*/, false /*fNewline*/);
            RTPrintf(",%s,%RI64\n", pszType, cbMaxPerSec);
        }
        else
        {
            if (cbMaxPerSec == 0)
            {
                RTPrintf(Info::tr("#%zu: Name: '%ls', Type: %s, Limit: none (disabled)\n"), i, strName.raw(), pszType);
                continue;
            }

            /* translate to human readable units.*/
            const char *pszUnit;
            LONG64      cUnits;
            if (!(cbMaxPerSec % _1G))
            {
                cUnits  = cbMaxPerSec / _1G;
                pszUnit = "GiB/s";
            }
            else if (!(cbMaxPerSec % _1M))
            {
                cUnits  = cbMaxPerSec / _1M;
                pszUnit = "MiB/s";
            }
            else if (!(cbMaxPerSec % _1K))
            {
                cUnits  = cbMaxPerSec / _1K;
                pszUnit = "KiB/s";
            }
            else
            {
                cUnits  = cbMaxPerSec;
                pszUnit = "bytes/s";
            }

            /*
             * We want to report network rate limit in bits/s, not bytes.
             * Only if it cannot be express it in kilobits we will fall
             * back to reporting it in bytes.
             */
            if (   enmType == BandwidthGroupType_Network
                && !(cbMaxPerSec % 125) )
            {
                LONG64      cNetUnits  = cbMaxPerSec / 125;
                const char *pszNetUnit = "kbps";
                if (!(cNetUnits % 1000000))
                {
                    cNetUnits /= 1000000;
                    pszNetUnit = "Gbps";
                }
                else if (!(cNetUnits % 1000))
                {
                    cNetUnits /= 1000;
                    pszNetUnit = "Mbps";
                }
                RTPrintf(Info::tr("#%zu: Name: '%ls', Type: %s, Limit: %RI64 %s (%RI64 %s)\n"),
                         i, strName.raw(), pszType, cNetUnits, pszNetUnit, cUnits, pszUnit);
            }
            else
                RTPrintf(Info::tr("#%zu: Name: '%ls', Type: %s, Limit: %RI64 %s\n"), i, strName.raw(), pszType, cUnits, pszUnit);
        }
    }

    return VINF_SUCCESS;
}

static const char *symlinkPolicyToName(SymlinkPolicy_T enmSymlinkPolicy)
{
    switch (enmSymlinkPolicy)
    {
        case SymlinkPolicy_AllowedToAnyTarget:
            return ("any");
        case SymlinkPolicy_AllowedInShareSubtree:
            return ("subtree");
        case SymlinkPolicy_AllowedToRelativeTargets:
            return ("relative");
        case SymlinkPolicy_Forbidden:
            return("forbidden");
        default:
            return("none");
    }
}

/** Shows a shared folder.   */
static HRESULT showSharedFolder(ComPtr<ISharedFolder> &sf, VMINFO_DETAILS details, const char *pszDesc,
                                const char *pszMrInfix, size_t idxMr, bool fFirst)
{
    Bstr name, hostPath, bstrAutoMountPoint;
    BOOL writable = FALSE, fAutoMount = FALSE;
    SymlinkPolicy_T enmSymlinkPolicy = SymlinkPolicy_None;
    CHECK_ERROR2I_RET(sf, COMGETTER(Name)(name.asOutParam()), hrcCheck);
    CHECK_ERROR2I_RET(sf, COMGETTER(HostPath)(hostPath.asOutParam()), hrcCheck);
    CHECK_ERROR2I_RET(sf, COMGETTER(Writable)(&writable), hrcCheck);
    CHECK_ERROR2I_RET(sf, COMGETTER(AutoMount)(&fAutoMount), hrcCheck);
    CHECK_ERROR2I_RET(sf, COMGETTER(AutoMountPoint)(bstrAutoMountPoint.asOutParam()), hrcCheck);
    CHECK_ERROR2I_RET(sf, COMGETTER(SymlinkPolicy)(&enmSymlinkPolicy), hrcCheck);

    if (fFirst && details != VMINFO_MACHINEREADABLE)
        RTPrintf("\n\n");
    if (details == VMINFO_MACHINEREADABLE)
    {
        char szNm[80];
        outputMachineReadableString(FmtNm(szNm, "SharedFolderName%s%zu", pszMrInfix, idxMr), &name);
        outputMachineReadableString(FmtNm(szNm, "SharedFolderPath%s%zu", pszMrInfix, idxMr), &hostPath);
    }
    else
    {
        RTPrintf(Info::tr("Name: '%ls', Host path: '%ls' (%s), %s%s"),
                 name.raw(), hostPath.raw(), pszDesc, writable ? Info::tr("writable") : Info::tr("readonly"),
                 fAutoMount ? Info::tr(", auto-mount") : "");
        if (enmSymlinkPolicy != SymlinkPolicy_None)
            RTPrintf(Info::tr(", symlink-policy: %s"), symlinkPolicyToName(enmSymlinkPolicy));
        if (bstrAutoMountPoint.isNotEmpty())
            RTPrintf(Info::tr(", mount-point: '%ls'\n"), bstrAutoMountPoint.raw());
        else
            RTPrintf("\n");
    }
    return S_OK;
}

/** Displays a list of IUSBDevices or IHostUSBDevices. */
template <class IUSBDeviceType>
static HRESULT showUsbDevices(SafeIfaceArray<IUSBDeviceType> &coll, const char *pszPfx,
                              const char *pszName, VMINFO_DETAILS details)
{
    if (coll.size() > 0)
    {
        if (details != VMINFO_MACHINEREADABLE)
            RTPrintf("%-28s\n\n", pszName);
        for (size_t i = 0; i < coll.size(); ++i)
        {
            ComPtr<IUSBDeviceType> dev = coll[i];
            char szValue[128];
            char szNm[80];

            SHOW_STRING_PROP(dev, Id, FmtNm(szNm, "%sActive%zu", pszPfx, i + 1), "UUID:");
            SHOW_USHORT_PROP_EX2(dev, VendorId,  FmtNm(szNm, "%sVendorId%zu", pszPfx, i + 1),  Info::tr("VendorId:"),  "", "%#06x", "%#06x (%04X)");
            SHOW_USHORT_PROP_EX2(dev, ProductId, FmtNm(szNm, "%sProductId%zu", pszPfx, i + 1), Info::tr("ProductId:"), "", "%#06x", "%#06x (%04X)");

            USHORT bcdRevision;
            CHECK_ERROR2I_RET(dev, COMGETTER(Revision)(&bcdRevision), hrcCheck);
            if (details == VMINFO_MACHINEREADABLE)
                RTStrPrintf(szValue, sizeof(szValue), "%#04x%02x", bcdRevision >> 8, bcdRevision & 0xff);
            else
                RTStrPrintf(szValue, sizeof(szValue), "%u.%u (%02u%02u)\n",
                            bcdRevision >> 8, bcdRevision & 0xff, bcdRevision >> 8, bcdRevision & 0xff);
            SHOW_UTF8_STRING(FmtNm(szNm, "%sRevision%zu", pszPfx, i + 1), Info::tr("Revision:"), szValue);

            SHOW_STRING_PROP_NOT_EMPTY(dev, Manufacturer, FmtNm(szNm, "%sManufacturer%zu", pszPfx, i + 1), Info::tr("Manufacturer:"));
            SHOW_STRING_PROP_NOT_EMPTY(dev, Product,      FmtNm(szNm, "%sProduct%zu", pszPfx, i + 1),      Info::tr("Product:"));
            SHOW_STRING_PROP_NOT_EMPTY(dev, SerialNumber, FmtNm(szNm, "%sSerialNumber%zu", pszPfx, i + 1), Info::tr("SerialNumber:"));
            SHOW_STRING_PROP_NOT_EMPTY(dev, Address,      FmtNm(szNm, "%sAddress%zu", pszPfx, i + 1),      Info::tr("Address:"));

            if (details != VMINFO_MACHINEREADABLE)
                RTPrintf("\n");
        }
    }
    else if (details != VMINFO_MACHINEREADABLE)
        RTPrintf("%-28s %s\n", pszName, Info::tr("<none>"));
    return S_OK;
}

/** Displays the medium attachments of the given controller. */
static HRESULT showMediumAttachments(ComPtr<IMachine> &machine, ComPtr<IStorageController> ptrStorageCtl, VMINFO_DETAILS details)
{
    Bstr bstrStorageCtlName;
    CHECK_ERROR2I_RET(ptrStorageCtl, COMGETTER(Name)(bstrStorageCtlName.asOutParam()), hrcCheck);
    ULONG cDevices;
    CHECK_ERROR2I_RET(ptrStorageCtl, COMGETTER(MaxDevicesPerPortCount)(&cDevices), hrcCheck);
    ULONG cPorts;
    CHECK_ERROR2I_RET(ptrStorageCtl, COMGETTER(PortCount)(&cPorts), hrcCheck);

    for (ULONG i = 0; i < cPorts; ++ i)
    {
        for (ULONG k = 0; k < cDevices; ++ k)
        {
            ComPtr<IMediumAttachment> mediumAttach;
            HRESULT hrc = machine->GetMediumAttachment(bstrStorageCtlName.raw(), i, k, mediumAttach.asOutParam());
            if (!SUCCEEDED(hrc) && hrc != VBOX_E_OBJECT_NOT_FOUND)
            {
                com::GlueHandleComError(machine, "GetMediumAttachment", hrc, __FILE__, __LINE__);
                return hrc;
            }

            BOOL fIsEjected = FALSE;
            BOOL fTempEject = FALSE;
            BOOL fHotPlug = FALSE;
            BOOL fNonRotational = FALSE;
            BOOL fDiscard = FALSE;
            DeviceType_T devType = DeviceType_Null;
            if (mediumAttach)
            {
                CHECK_ERROR2I_RET(mediumAttach, COMGETTER(TemporaryEject)(&fTempEject), hrcCheck);
                CHECK_ERROR2I_RET(mediumAttach, COMGETTER(IsEjected)(&fIsEjected), hrcCheck);
                CHECK_ERROR2I_RET(mediumAttach, COMGETTER(Type)(&devType), hrcCheck);
                CHECK_ERROR2I_RET(mediumAttach, COMGETTER(HotPluggable)(&fHotPlug), hrcCheck);
                CHECK_ERROR2I_RET(mediumAttach, COMGETTER(NonRotational)(&fNonRotational), hrcCheck);
                CHECK_ERROR2I_RET(mediumAttach, COMGETTER(Discard)(&fDiscard), hrcCheck);
            }

            ComPtr<IMedium> medium;
            hrc = machine->GetMedium(bstrStorageCtlName.raw(), i, k, medium.asOutParam());
            if (SUCCEEDED(hrc) && medium)
            {
                BOOL fPassthrough = FALSE;
                if (mediumAttach)
                {
                    CHECK_ERROR2I_RET(mediumAttach, COMGETTER(Passthrough)(&fPassthrough), hrcCheck);
                }

                Bstr bstrFilePath;
                CHECK_ERROR2I_RET(medium, COMGETTER(Location)(bstrFilePath.asOutParam()), hrcCheck);
                Bstr bstrUuid;
                CHECK_ERROR2I_RET(medium, COMGETTER(Id)(bstrUuid.asOutParam()), hrcCheck);

                if (details != VMINFO_MACHINEREADABLE)
                    RTPrintf(Info::tr("  Port %u, Unit %u: UUID: %ls%s%s%s%s%s%s\n    Location: \"%ls\"\n"),
                             i, k, bstrUuid.raw(),
                             fPassthrough   ? Info::tr(", passthrough enabled") : "",
                             fTempEject     ? Info::tr(", temp eject") : "",
                             fIsEjected     ? Info::tr(", ejected") : "",
                             fHotPlug       ? Info::tr(", hot-pluggable") : "",
                             fNonRotational ? Info::tr(", non-rotational (SSD)") : "",
                             fDiscard       ? Info::tr(", discards unused blocks") : "",
                             bstrFilePath.raw());
                else
                {
                    /* Note! dvdpassthough, tempeject and IsEjected was all missed the port
                             and unit bits prior to VBox 7.0.  */
                    /** @todo This would look better on the "%ls-%d-%d-{tag}" form! */
                    outputMachineReadableStringWithFmtName(&bstrFilePath,
                                                           true, "%ls-%d-%d", bstrStorageCtlName.raw(), i, k);
                    outputMachineReadableStringWithFmtName(&bstrUuid,
                                                           true,  "%ls-ImageUUID-%d-%d", bstrStorageCtlName.raw(), i, k);

                    if (fPassthrough)
                        outputMachineReadableStringWithFmtName("on",
                                                               true, "%ls-dvdpassthrough-%d-%d", bstrStorageCtlName.raw(), i, k);
                    if (devType == DeviceType_DVD)
                    {
                        outputMachineReadableStringWithFmtName(fTempEject ? "on" : "off",
                                                               true, "%ls-tempeject-%d-%d", bstrStorageCtlName.raw(), i, k);
                        outputMachineReadableStringWithFmtName(fIsEjected ? "on" : "off",
                                                               true, "%ls-IsEjected-%d-%d", bstrStorageCtlName.raw(), i, k);
                    }

                    if (   bstrStorageCtlName.compare(Bstr("SATA"), Bstr::CaseInsensitive)== 0
                        || bstrStorageCtlName.compare(Bstr("USB"), Bstr::CaseInsensitive)== 0)
                        outputMachineReadableStringWithFmtName(fHotPlug ? "on" : "off",
                                                               true, "%ls-hot-pluggable-%d-%d", bstrStorageCtlName.raw(),
                                                               i, k);

                    outputMachineReadableStringWithFmtName(fNonRotational ? "on" : "off",
                                                           true, "%ls-nonrotational-%d-%d", bstrStorageCtlName.raw(), i, k);
                    outputMachineReadableStringWithFmtName(fDiscard ? "on" : "off",
                                                           true, "%ls-discard-%d-%d", bstrStorageCtlName.raw(), i, k);
                }
            }
            else if (SUCCEEDED(hrc))
            {
                if (details != VMINFO_MACHINEREADABLE)
                    RTPrintf(Info::tr("  Port %u, Unit %u: Empty%s%s\n"), i, k,
                             fTempEject ? Info::tr(", temp eject") : "",
                             fIsEjected ? Info::tr(", ejected") : "");
                else
                {
                    outputMachineReadableStringWithFmtName("emptydrive", true, "%ls-%d-%d", bstrStorageCtlName.raw(), i, k);
                    if (devType == DeviceType_DVD)
                        outputMachineReadableStringWithFmtName(fIsEjected ? "on" : "off",
                                                               true, "%ls-IsEjected-%d-%d", bstrStorageCtlName.raw(), i, k);
                }
            }
            else if (details == VMINFO_MACHINEREADABLE)
                outputMachineReadableStringWithFmtName("none", true, "%ls-%d-%d", bstrStorageCtlName.raw(), i, k);
            else if (hrc != VBOX_E_OBJECT_NOT_FOUND)
                RTPrintf(Info::tr("  Port %u, Unit %u: GetMedium failed: %Rhrc\n"), i, k, hrc);

        }
    }
    return S_OK;
}


#ifdef VBOX_WITH_IOMMU_AMD
static const char *iommuTypeToString(IommuType_T iommuType, VMINFO_DETAILS details)
{
    switch (iommuType)
    {
        case IommuType_None:
            if (details == VMINFO_MACHINEREADABLE)
                return "none";
            return Info::tr("None");

        case IommuType_Automatic:
            if (details == VMINFO_MACHINEREADABLE)
                return "automatic";
            return Info::tr("Automatic");

        case IommuType_AMD:
            if (details == VMINFO_MACHINEREADABLE)
                return "amd";
            return "AMD";

        case IommuType_Intel:
            if (details == VMINFO_MACHINEREADABLE)
                return "intel";
            return "Intel";

        default:
            if (details == VMINFO_MACHINEREADABLE)
                return "unknown";
            return Info::tr("Unknown");
    }
}
#endif

static const char *paravirtProviderToString(ParavirtProvider_T provider, VMINFO_DETAILS details)
{
    switch (provider)
    {
        case ParavirtProvider_None:
            if (details == VMINFO_MACHINEREADABLE)
                return "none";
            return Info::tr("None");

        case ParavirtProvider_Default:
            if (details == VMINFO_MACHINEREADABLE)
                return "default";
            return Info::tr("Default");

        case ParavirtProvider_Legacy:
            if (details == VMINFO_MACHINEREADABLE)
                return "legacy";
            return Info::tr("Legacy");

        case ParavirtProvider_Minimal:
            if (details == VMINFO_MACHINEREADABLE)
                return "minimal";
            return Info::tr("Minimal");

        case ParavirtProvider_HyperV:
            if (details == VMINFO_MACHINEREADABLE)
                return "hyperv";
            return "HyperV";

        case ParavirtProvider_KVM:
            if (details == VMINFO_MACHINEREADABLE)
                return "kvm";
            return "KVM";

        default:
            if (details == VMINFO_MACHINEREADABLE)
                return "unknown";
            return Info::tr("Unknown");
    }
}


/* Disable global optimizations for MSC 8.0/64 to make it compile in reasonable
   time. MSC 7.1/32 doesn't have quite as much trouble with it, but still
   sufficient to qualify for this hack as well since this code isn't performance
   critical and probably won't gain much from the extra optimizing in real life. */
#if defined(_MSC_VER)
# pragma optimize("g", off)
# pragma warning(push)
# if _MSC_VER < RT_MSC_VER_VC120
#  pragma warning(disable: 4748)
# endif
#endif

HRESULT showVMInfo(ComPtr<IVirtualBox> pVirtualBox,
                   ComPtr<IMachine> machine,
                   ComPtr<ISession> pSession,
                   VMINFO_DETAILS details /*= VMINFO_NONE*/)
{
    HRESULT hrc;
    ComPtr<IConsole> pConsole;
    if (pSession)
        pSession->COMGETTER(Console)(pConsole.asOutParam());

    char szNm[80];
    char szValue[256];

    /*
     * The rules for output in -argdump format:
     * 1) the key part (the [0-9a-zA-Z_\-]+ string before the '=' delimiter)
     *    is all lowercase for "VBoxManage modifyvm" parameters. Any
     *    other values printed are in CamelCase.
     * 2) strings (anything non-decimal) are printed surrounded by
     *    double quotes '"'. If the strings themselves contain double
     *    quotes, these characters are escaped by '\'. Any '\' character
     *    in the original string is also escaped by '\'.
     * 3) numbers (containing just [0-9\-]) are written out unchanged.
     */

    BOOL fAccessible;
    CHECK_ERROR2I_RET(machine, COMGETTER(Accessible)(&fAccessible), hrcCheck);
    if (!fAccessible)
    {
        Bstr uuid;
        machine->COMGETTER(Id)(uuid.asOutParam());
        if (details == VMINFO_COMPACT)
            RTPrintf(Info::tr("\"<inaccessible>\" {%s}\n"), Utf8Str(uuid).c_str());
        else
        {
            if (details == VMINFO_MACHINEREADABLE)
                RTPrintf("name=\"<inaccessible>\"\n");
            else
                RTPrintf(Info::tr("Name:            <inaccessible!>\n"));
            if (details == VMINFO_MACHINEREADABLE)
                RTPrintf("UUID=\"%s\"\n", Utf8Str(uuid).c_str());
            else
                RTPrintf("UUID:            %s\n", Utf8Str(uuid).c_str());
            if (details != VMINFO_MACHINEREADABLE)
            {
                Bstr settingsFilePath;
                hrc = machine->COMGETTER(SettingsFilePath)(settingsFilePath.asOutParam());
                RTPrintf(Info::tr("Config file:     %ls\n"), settingsFilePath.raw());

                Bstr strCipher;
                Bstr strPasswordId;
                HRESULT hrc2 = machine->GetEncryptionSettings(strCipher.asOutParam(), strPasswordId.asOutParam());
                if (SUCCEEDED(hrc2))
                {
                    RTPrintf("Encryption:     enabled\n");
                    RTPrintf("Cipher:         %ls\n", strCipher.raw());
                    RTPrintf("Password ID:    %ls\n", strPasswordId.raw());
                }
                else
                    RTPrintf("Encryption:     disabled\n");

                ComPtr<IVirtualBoxErrorInfo> accessError;
                hrc = machine->COMGETTER(AccessError)(accessError.asOutParam());
                RTPrintf(Info::tr("Access error details:\n"));
                ErrorInfo ei(accessError);
                GluePrintErrorInfo(ei);
                RTPrintf("\n");
            }
        }
        return S_OK;
    }

    ComPtr<IPlatform> platform;
    CHECK_ERROR_RET(machine, COMGETTER(Platform)(platform.asOutParam()), hrc);
    ComPtr<IPlatformProperties> platformProperties;
    CHECK_ERROR_RET(platform, COMGETTER(Properties)(platformProperties.asOutParam()), hrc);

    PlatformArchitecture_T platformArch;
    CHECK_ERROR_RET(platform, COMGETTER(Architecture)(&platformArch), hrc);

    ComPtr<IFirmwareSettings> firmwareSettings;
    CHECK_ERROR_RET(machine, COMGETTER(FirmwareSettings)(firmwareSettings.asOutParam()), hrc);

    if (details == VMINFO_COMPACT)
    {
        Bstr machineName;
        machine->COMGETTER(Name)(machineName.asOutParam());
        Bstr uuid;
        machine->COMGETTER(Id)(uuid.asOutParam());

        RTPrintf("\"%ls\" {%s}\n", machineName.raw(), Utf8Str(uuid).c_str());
        return S_OK;
    }

    SHOW_STRING_PROP(      machine, Name,                       "name",                 Info::tr("Name:"));
    {
        Bstr strCipher;
        Bstr strPasswordId;
        HRESULT hrc2 = machine->GetEncryptionSettings(strCipher.asOutParam(), strPasswordId.asOutParam());

        SHOW_UTF8_STRING(      "encryption",              Info::tr("Encryption:"),
                                                          SUCCEEDED(hrc2) ? "enabled" : "disabled");
        if (SUCCEEDED(hrc2))
        {
            SHOW_BSTR_STRING(      "enc_cipher",          Info::tr("Cipher:"), strCipher);
            SHOW_BSTR_STRING(      "enc_password_id",     Info::tr("Password ID:"), strPasswordId);
        }
    }
    SHOW_STRINGARRAY_PROP( machine, Groups,                     "groups",               Info::tr("Groups:"));
    SHOW_UTF8_STRING(      "platformArchitecture",              Info::tr("Platform Architecture:"),
                                                                                        platformArch == PlatformArchitecture_x86 ? "x86" : "ARM");
    Bstr osTypeId;
    CHECK_ERROR2I_RET(machine, COMGETTER(OSTypeId)(osTypeId.asOutParam()), hrcCheck);
    ComPtr<IGuestOSType> osType;
    pVirtualBox->GetGuestOSType(osTypeId.raw(), osType.asOutParam());
    if (!osType.isNull())
        SHOW_STRING_PROP(       osType, Description,                "ostype",               Info::tr("Guest OS:"));
    else
        SHOW_STRING_PROP(      machine, OSTypeId,                   "ostype",               Info::tr("Guest OS:"));
    SHOW_UUID_PROP(        machine, Id,                         "UUID",                 "UUID:");
    SHOW_STRING_PROP(      machine, SettingsFilePath,           "CfgFile",              Info::tr("Config file:"));
    SHOW_STRING_PROP(      machine, SnapshotFolder,             "SnapFldr",             Info::tr("Snapshot folder:"));
    SHOW_STRING_PROP(      machine, LogFolder,                  "LogFldr",              Info::tr("Log folder:"));
    SHOW_UUID_PROP(        machine, HardwareUUID,               "hardwareuuid",         Info::tr("Hardware UUID:"));
    SHOW_ULONG_PROP(       machine, MemorySize,                 "memory",               Info::tr("Memory size:"),     "MB");
    SHOW_BOOLEAN_PROP(     machine, PageFusionEnabled,          "pagefusion",           Info::tr("Page Fusion:"));
    ComPtr<IGraphicsAdapter> pGraphicsAdapter;
    machine->COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam());
    SHOW_ULONG_PROP(pGraphicsAdapter, VRAMSize,                 "vram",                 Info::tr("VRAM size:"),       "MB");
    SHOW_ULONG_PROP(       machine, CPUExecutionCap,            "cpuexecutioncap",      Info::tr("CPU exec cap:"),    "%");
    SHOW_STRING_PROP_MAJ(  machine, CPUProfile,                 "cpu-profile",          Info::tr("CPUProfile:"),      "host", 6);

    ChipsetType_T chipsetType;
    CHECK_ERROR2I_RET(platform, COMGETTER(ChipsetType)(&chipsetType), hrcCheck);
    const char *pszChipsetType;
    switch (chipsetType)
    {
        case ChipsetType_Null:
            if (details == VMINFO_MACHINEREADABLE)
                pszChipsetType = "invalid";
            else
                pszChipsetType = Info::tr("invalid");
            break;
        case ChipsetType_PIIX3:        pszChipsetType = "piix3";        break;
        case ChipsetType_ICH9:         pszChipsetType = "ich9";         break;
        case ChipsetType_ARMv8Virtual: pszChipsetType = "armv8virtual"; break;
        default:
            AssertFailed();
            if (details == VMINFO_MACHINEREADABLE)
                pszChipsetType = "unknown";
            else
                pszChipsetType = Info::tr("unknown");
            break;
    }
    SHOW_UTF8_STRING("chipset", Info::tr("Chipset:"), pszChipsetType);

    FirmwareType_T firmwareType;
    CHECK_ERROR2I_RET(firmwareSettings, COMGETTER(FirmwareType)(&firmwareType), hrcCheck);
    const char *pszFirmwareType;
    switch (firmwareType)
    {
        case FirmwareType_BIOS:     pszFirmwareType = "BIOS"; break;
        case FirmwareType_EFI:      pszFirmwareType = "EFI"; break;
        case FirmwareType_EFI32:    pszFirmwareType = "EFI32"; break;
        case FirmwareType_EFI64:    pszFirmwareType = "EFI64"; break;
        case FirmwareType_EFIDUAL:  pszFirmwareType = "EFIDUAL"; break;
        default:
            AssertFailed();
            if (details == VMINFO_MACHINEREADABLE)
                pszFirmwareType = "unknown";
            else
                pszFirmwareType = Info::tr("unknown");
            break;
    }
    SHOW_UTF8_STRING("firmware", Info::tr("Firmware:"), pszFirmwareType);

    SHOW_ULONG_PROP(       machine, CPUCount, "cpus", Info::tr("Number of CPUs:"), "");

    switch (platformArch)
    {
        case PlatformArchitecture_x86:
        {
            ComPtr<IPlatformX86> platformX86;
            CHECK_ERROR_RET(platform, COMGETTER(X86)(platformX86.asOutParam()), hrc);

            SHOW_BOOLEAN_PROP(     platformX86, HPETEnabled, "hpet", Info::tr("HPET:"));
            SHOW_BOOLEAN_METHOD(   platformX86, GetCPUProperty(CPUPropertyTypeX86_PAE, &f), "pae", "PAE:");
            SHOW_BOOLEAN_METHOD(   platformX86, GetCPUProperty(CPUPropertyTypeX86_LongMode, &f), "longmode", Info::tr("Long Mode:"));
            SHOW_BOOLEAN_METHOD(   platformX86, GetCPUProperty(CPUPropertyTypeX86_TripleFaultReset, &f), "triplefaultreset", Info::tr("Triple Fault Reset:"));
            SHOW_BOOLEAN_METHOD(   platformX86, GetCPUProperty(CPUPropertyTypeX86_APIC, &f), "apic", "APIC:");
            SHOW_BOOLEAN_METHOD(   platformX86, GetCPUProperty(CPUPropertyTypeX86_X2APIC, &f), "x2apic", "X2APIC:");
            SHOW_BOOLEAN_METHOD(   platformX86, GetCPUProperty(CPUPropertyTypeX86_HWVirt, &f), "nested-hw-virt", Info::tr("Nested VT-x/AMD-V:"));

            if (details != VMINFO_MACHINEREADABLE)
                RTPrintf("%-28s ", Info::tr("CPUID overrides:"));
            ULONG uOrdinal = 0;
            for (uOrdinal = 0; uOrdinal < _4K; uOrdinal++)
            {
                ULONG uLeaf, uSubLeaf, uEAX, uEBX, uECX, uEDX;
                hrc = platformX86->GetCPUIDLeafByOrdinal(uOrdinal, &uLeaf, &uSubLeaf, &uEAX, &uEBX, &uECX, &uEDX);
                if (SUCCEEDED(hrc))
                {
                    if (details == VMINFO_MACHINEREADABLE)
                        RTPrintf("cpuid=%08x,%08x,%08x,%08x,%08x,%08x", uLeaf, uSubLeaf, uEAX, uEBX, uECX, uEDX);
                    else
                    {
                        if (!uOrdinal)
                            RTPrintf(Info::tr("Leaf no.      EAX      EBX      ECX      EDX\n"));
                        RTPrintf("%-28s %08x/%03x  %08x %08x %08x %08x\n", "", uLeaf, uSubLeaf, uEAX, uEBX, uECX, uEDX);
                    }
                }
                else
                {
                    if (hrc != E_INVALIDARG)
                        com::GlueHandleComError(machine, "GetCPUIDLeaf", hrc, __FILE__, __LINE__);
                    break;
                }
            }
            if (!uOrdinal && details != VMINFO_MACHINEREADABLE)
                RTPrintf(Info::tr("None\n"));

            SHOW_BOOLEAN_METHOD(platformX86, GetHWVirtExProperty(HWVirtExPropertyType_Enabled,   &f),   "hwvirtex",     Info::tr("Hardware Virtualization:"));
            SHOW_BOOLEAN_METHOD(platformX86, GetHWVirtExProperty(HWVirtExPropertyType_NestedPaging, &f),"nestedpaging", Info::tr("Nested Paging:"));
            SHOW_BOOLEAN_METHOD(platformX86, GetHWVirtExProperty(HWVirtExPropertyType_LargePages, &f),  "largepages",   Info::tr("Large Pages:"));
            SHOW_BOOLEAN_METHOD(platformX86, GetHWVirtExProperty(HWVirtExPropertyType_VPID, &f),        "vtxvpid",      "VT-x VPID:");
            SHOW_BOOLEAN_METHOD(platformX86, GetHWVirtExProperty(HWVirtExPropertyType_UnrestrictedExecution, &f), "vtxux", Info::tr("VT-x Unrestricted Exec.:"));
            SHOW_BOOLEAN_METHOD(platformX86, GetHWVirtExProperty(HWVirtExPropertyType_VirtVmsaveVmload, &f),      "virtvmsavevmload", Info::tr("AMD-V Virt. Vmsave/Vmload:"));
            break;
        }

        case PlatformArchitecture_ARM:
        {
            /** @todo BUGBUG ARM stuff here */
            break;
        }

        default:
            AssertFailed();
            break;
    }

    SHOW_ULONG_PROP(       machine, CPUIDPortabilityLevel, "cpuid-portability-level", Info::tr("CPUID Portability Level:"), "");

    ComPtr<INvramStore> nvramStore;
    CHECK_ERROR2I_RET(machine, COMGETTER(NonVolatileStore)(nvramStore.asOutParam()), hrcCheck);

    FirmwareBootMenuMode_T enmBootMenuMode;
    CHECK_ERROR2I_RET(firmwareSettings, COMGETTER(BootMenuMode)(&enmBootMenuMode), hrcCheck);
    const char *pszBootMenu;
    switch (enmBootMenuMode)
    {
        case FirmwareBootMenuMode_Disabled:
            if (details == VMINFO_MACHINEREADABLE)
                pszBootMenu = "disabled";
            else
                pszBootMenu = Info::tr("disabled");
            break;
        case FirmwareBootMenuMode_MenuOnly:
            if (details == VMINFO_MACHINEREADABLE)
                pszBootMenu = "menuonly";
            else
                pszBootMenu = Info::tr("menu only");
            break;
        default:
            if (details == VMINFO_MACHINEREADABLE)
                pszBootMenu = "messageandmenu";
            else
                pszBootMenu = Info::tr("message and menu");
    }
    SHOW_UTF8_STRING("bootmenu", Info::tr("Boot menu mode:"), pszBootMenu);

    ComPtr<ISystemProperties> systemProperties;
    CHECK_ERROR2I_RET(pVirtualBox, COMGETTER(SystemProperties)(systemProperties.asOutParam()), hrcCheck);
    ULONG maxBootPosition = 0;
    CHECK_ERROR2I_RET(platformProperties, COMGETTER(MaxBootPosition)(&maxBootPosition), hrcCheck);
    for (ULONG i = 1; i <= maxBootPosition; i++)
    {
        DeviceType_T bootOrder;
        CHECK_ERROR2I_RET(machine, GetBootOrder(i, &bootOrder), hrcCheck);
        const char *pszDevice;
        if (bootOrder == DeviceType_Floppy)
            pszDevice = details == VMINFO_MACHINEREADABLE ? "floppy"        : Info::tr("Floppy");
        else if (bootOrder == DeviceType_DVD)
            pszDevice = details == VMINFO_MACHINEREADABLE ? "dvd"           : "DVD";
        else if (bootOrder == DeviceType_HardDisk)
            pszDevice = details == VMINFO_MACHINEREADABLE ? "disk"          : Info::tr("HardDisk");
        else if (bootOrder == DeviceType_Network)
            pszDevice = details == VMINFO_MACHINEREADABLE ? "net"           : Info::tr("Network");
        else if (bootOrder == DeviceType_USB)
            pszDevice = details == VMINFO_MACHINEREADABLE ? "usb"           : "USB";
        else if (bootOrder == DeviceType_SharedFolder)
            pszDevice = details == VMINFO_MACHINEREADABLE ? "sharedfolder"  : Info::tr("Shared Folder");
        else
            pszDevice = details == VMINFO_MACHINEREADABLE ? "none"          : Info::tr("Not Assigned");
        SHOW_UTF8_STRING(FmtNm(szNm, "boot%u", i), FmtNm(szNm, Info::tr("Boot Device %u:"), i), pszDevice);
    }

    SHOW_BOOLEAN_PROP(firmwareSettings, ACPIEnabled,                "acpi",                 "ACPI:");
    SHOW_BOOLEAN_PROP(firmwareSettings, IOAPICEnabled,              "ioapic",               "IOAPIC:");

    APICMode_T apicMode;
    CHECK_ERROR2I_RET(firmwareSettings, COMGETTER(APICMode)(&apicMode), hrcCheck);
    const char *pszAPIC;
    switch (apicMode)
    {
        case APICMode_Disabled:
            if (details == VMINFO_MACHINEREADABLE)
                pszAPIC = "disabled";
            else
                pszAPIC = Info::tr("disabled");
            break;
        case APICMode_APIC:
        default:
            if (details == VMINFO_MACHINEREADABLE)
                pszAPIC = "apic";
            else
                pszAPIC = "APIC";
            break;
        case APICMode_X2APIC:
            if (details == VMINFO_MACHINEREADABLE)
                pszAPIC = "x2apic";
            else
                pszAPIC = "x2APIC";
            break;
    }
    SHOW_UTF8_STRING("biosapic", Info::tr("BIOS APIC mode:"), pszAPIC);

    SHOW_LONG64_PROP(firmwareSettings,  TimeOffset, "biossystemtimeoffset", Info::tr("Time offset:"),  Info::tr("ms"));
    Bstr bstrNVRAMFile;
    CHECK_ERROR2I_RET(nvramStore, COMGETTER(NonVolatileStorageFile)(bstrNVRAMFile.asOutParam()), hrcCheck);
    if (bstrNVRAMFile.isNotEmpty())
        SHOW_BSTR_STRING("NvramFile", Info::tr("BIOS NVRAM File:"), bstrNVRAMFile);
    if (   firmwareType == FirmwareType_EFI || firmwareType == FirmwareType_EFI32
        || firmwareType == FirmwareType_EFI64 || firmwareType == FirmwareType_EFIDUAL)
    {
        ComPtr<IUefiVariableStore> uefiVarStore;
        hrc = nvramStore->COMGETTER(UefiVariableStore)(uefiVarStore.asOutParam());
        if (SUCCEEDED(hrc))
            SHOW_BOOLEAN_PROP(uefiVarStore, SecureBootEnabled, "SecureBoot", Info::tr("UEFI Secure Boot:"));
    }
    SHOW_BOOLEAN_PROP_EX(platform,   RTCUseUTC, "rtcuseutc", Info::tr("RTC:"), "UTC", Info::tr("local time"));

#ifdef VBOX_WITH_IOMMU_AMD /** @todo BUGBUG Do we set / needs this for ARM as well? */
    IommuType_T iommuType;
    CHECK_ERROR2I_RET(platform, COMGETTER(IommuType)(&iommuType), hrcCheck);
    const char *pszIommuType = iommuTypeToString(iommuType, details);
    SHOW_UTF8_STRING("iommu", "IOMMU:", pszIommuType);
#endif

    ParavirtProvider_T paravirtProvider;
    CHECK_ERROR2I_RET(machine, COMGETTER(ParavirtProvider)(&paravirtProvider), hrcCheck);
    const char *pszParavirtProvider = paravirtProviderToString(paravirtProvider, details);
    SHOW_UTF8_STRING("paravirtprovider", Info::tr("Paravirt. Provider:"), pszParavirtProvider);

    ParavirtProvider_T effParavirtProvider;
    CHECK_ERROR2I_RET(machine, GetEffectiveParavirtProvider(&effParavirtProvider), hrcCheck);
    const char *pszEffParavirtProvider = paravirtProviderToString(effParavirtProvider, details);
    SHOW_UTF8_STRING("effparavirtprovider", Info::tr("Effective Paravirt. Prov.:"), pszEffParavirtProvider);

    Bstr paravirtDebug;
    CHECK_ERROR2I_RET(machine, COMGETTER(ParavirtDebug)(paravirtDebug.asOutParam()), hrcCheck);
    if (paravirtDebug.isNotEmpty())
        SHOW_BSTR_STRING("paravirtdebug", Info::tr("Paravirt. Debug:"), paravirtDebug);

    MachineState_T machineState;
    CHECK_ERROR2I_RET(machine, COMGETTER(State)(&machineState), hrcCheck);
    const char *pszState = machineStateToName(machineState, details == VMINFO_MACHINEREADABLE /*=fShort*/);

    LONG64 stateSince;
    machine->COMGETTER(LastStateChange)(&stateSince);
    RTTIMESPEC timeSpec;
    RTTimeSpecSetMilli(&timeSpec, stateSince);
    char pszTime[30] = {0};
    RTTimeSpecToString(&timeSpec, pszTime, sizeof(pszTime));
    if (details == VMINFO_MACHINEREADABLE)
    {
        RTPrintf("VMState=\"%s\"\n", pszState);
        RTPrintf("VMStateChangeTime=\"%s\"\n", pszTime);

        Bstr stateFile;
        machine->COMGETTER(StateFilePath)(stateFile.asOutParam());
        if (!stateFile.isEmpty())
            RTPrintf("VMStateFile=\"%ls\"\n", stateFile.raw());
    }
    else
        RTPrintf(Info::tr("%-28s %s (since %s)\n"), Info::tr("State:"), pszState, pszTime);

    GraphicsControllerType_T enmGraphics;
    hrc = pGraphicsAdapter->COMGETTER(GraphicsControllerType)(&enmGraphics);
    if (SUCCEEDED(hrc))
    {
        const char *pszCtrl;
        switch (enmGraphics)
        {
            case GraphicsControllerType_Null:
                if (details == VMINFO_MACHINEREADABLE)
                    pszCtrl = "null";
                else
                    pszCtrl = Info::tr("Null");
                break;
            case GraphicsControllerType_VBoxVGA:
                if (details == VMINFO_MACHINEREADABLE)
                    pszCtrl = "vboxvga";
                else
                    pszCtrl = "VBoxVGA";
                break;
            case GraphicsControllerType_VMSVGA:
                if (details == VMINFO_MACHINEREADABLE)
                    pszCtrl = "vmsvga";
                else
                    pszCtrl = "VMSVGA";
                break;
            case GraphicsControllerType_VBoxSVGA:
                if (details == VMINFO_MACHINEREADABLE)
                    pszCtrl = "vboxsvga";
                else
                    pszCtrl = "VBoxSVGA";
                break;
            case GraphicsControllerType_QemuRamFB:
                if (details == VMINFO_MACHINEREADABLE)
                    pszCtrl = "qemuramfb";
                else
                    pszCtrl = "QemuRamFB";
                break;

            default:
                if (details == VMINFO_MACHINEREADABLE)
                    pszCtrl = "unknown";
                else
                    pszCtrl = Info::tr("Unknown");
                break;
        }

        if (details == VMINFO_MACHINEREADABLE)
            RTPrintf("graphicscontroller=\"%s\"\n", pszCtrl);
        else
            RTPrintf("%-28s %s\n", Info::tr("Graphics Controller:"), pszCtrl);
    }

    SHOW_ULONG_PROP(pGraphicsAdapter, MonitorCount,             "monitorcount",             Info::tr("Monitor count:"), "");
    SHOW_BOOLEAN_METHOD(pGraphicsAdapter, IsFeatureEnabled(GraphicsFeature_Acceleration3D, &f), "accelerate3d", "3D Acceleration:");
#ifdef VBOX_WITH_VIDEOHWACCEL
    SHOW_BOOLEAN_METHOD(pGraphicsAdapter, IsFeatureEnabled(GraphicsFeature_Acceleration2DVideo, &f), "accelerate2dvideo", "2D Video Acceleration:");
#endif
    SHOW_BOOLEAN_PROP(    machine,  TeleporterEnabled,          "teleporterenabled",        Info::tr("Teleporter Enabled:"));
    SHOW_ULONG_PROP(      machine,  TeleporterPort,             "teleporterport",           Info::tr("Teleporter Port:"), "");
    SHOW_STRING_PROP(     machine,  TeleporterAddress,          "teleporteraddress",        Info::tr("Teleporter Address:"));
    SHOW_STRING_PROP(     machine,  TeleporterPassword,         "teleporterpassword",       Info::tr("Teleporter Password:"));
    SHOW_BOOLEAN_PROP(    machine,  TracingEnabled,             "tracing-enabled",          Info::tr("Tracing Enabled:"));
    SHOW_BOOLEAN_PROP(    machine,  AllowTracingToAccessVM,     "tracing-allow-vm-access",  Info::tr("Allow Tracing to Access VM:"));
    SHOW_STRING_PROP(     machine,  TracingConfig,              "tracing-config",           Info::tr("Tracing Configuration:"));
    SHOW_BOOLEAN_PROP(    machine,  AutostartEnabled,           "autostart-enabled",        Info::tr("Autostart Enabled:"));
    SHOW_ULONG_PROP(      machine,  AutostartDelay,             "autostart-delay",          Info::tr("Autostart Delay:"), "");
    SHOW_STRING_PROP(     machine,  DefaultFrontend,            "defaultfrontend",          Info::tr("Default Frontend:"));

    VMProcPriority_T enmVMProcPriority;
    CHECK_ERROR2I_RET(machine, COMGETTER(VMProcessPriority)(&enmVMProcPriority), hrcCheck);
    const char *pszVMProcPriority;
    switch (enmVMProcPriority)
    {
        case VMProcPriority_Flat:
            if (details == VMINFO_MACHINEREADABLE)
                pszVMProcPriority = "flat";
            else
                pszVMProcPriority = Info::tr("flat");
            break;
        case VMProcPriority_Low:
            if (details == VMINFO_MACHINEREADABLE)
                pszVMProcPriority = "low";
            else
                pszVMProcPriority = Info::tr("low");
            break;
        case VMProcPriority_Normal:
            if (details == VMINFO_MACHINEREADABLE)
                pszVMProcPriority = "normal";
            else
                pszVMProcPriority = Info::tr("normal");
            break;
        case VMProcPriority_High:
            if (details == VMINFO_MACHINEREADABLE)
                pszVMProcPriority = "high";
            else
                pszVMProcPriority = Info::tr("high");
            break;
        default:
            if (details == VMINFO_MACHINEREADABLE)
                pszVMProcPriority = "default";
            else
                pszVMProcPriority = Info::tr("default");
            break;
    }
    SHOW_UTF8_STRING("vmprocpriority", Info::tr("VM process priority:"), pszVMProcPriority);

/** @todo Convert the remainder of the function to SHOW_XXX macros and add error
 *        checking where missing. */
    /*
     * Storage Controllers and their attached Mediums.
     */
    com::SafeIfaceArray<IStorageController> storageCtls;
    CHECK_ERROR(machine, COMGETTER(StorageControllers)(ComSafeArrayAsOutParam(storageCtls)));
    if (storageCtls.size() > 0)
    {
        if (details != VMINFO_MACHINEREADABLE)
            RTPrintf("%s\n", Info::tr("Storage Controllers:"));

        for (size_t i = 0; i < storageCtls.size(); ++i)
        {
            ComPtr<IStorageController> storageCtl = storageCtls[i];

            Bstr bstrName;
            CHECK_ERROR2I_RET(storageCtl, COMGETTER(Name)(bstrName.asOutParam()), hrcCheck);
            StorageControllerType_T enmCtlType = StorageControllerType_Null;
            CHECK_ERROR2I_RET(storageCtl, COMGETTER(ControllerType)(&enmCtlType), hrcCheck);
            ULONG uInstance = 0;
            CHECK_ERROR2I_RET(storageCtl, COMGETTER(Instance)(&uInstance), hrcCheck);
            ULONG cMaxPorts = 0;
            CHECK_ERROR2I_RET(storageCtl, COMGETTER(MaxPortCount)(&cMaxPorts), hrcCheck);
            ULONG cPorts = 0;
            CHECK_ERROR2I_RET(storageCtl, COMGETTER(PortCount)(&cPorts), hrcCheck);
            BOOL fBootable = FALSE;
            CHECK_ERROR2I_RET(storageCtl, COMGETTER(Bootable)(&fBootable), hrcCheck);
            if (details == VMINFO_MACHINEREADABLE)
            {
                outputMachineReadableString(FmtNm(szNm, "storagecontrollername%u", i), &bstrName);
                outputMachineReadableString(FmtNm(szNm, "storagecontrollertype%u", i),
                                            storageControllerTypeToName(enmCtlType, true));
                RTPrintf("storagecontrollerinstance%u=\"%u\"\n", i, uInstance);
                RTPrintf("storagecontrollermaxportcount%u=\"%u\"\n", i, cMaxPorts);
                RTPrintf("storagecontrollerportcount%u=\"%u\"\n", i, cPorts);
                RTPrintf("storagecontrollerbootable%u=\"%s\"\n", i, fBootable ? "on" : "off");
            }
            else
            {
                RTPrintf(Info::tr("#%u: '%ls', Type: %s, Instance: %u, Ports: %u (max %u), %s\n"), i, bstrName.raw(),
                         storageControllerTypeToName(enmCtlType, false), uInstance, cPorts, cMaxPorts,
                         fBootable ? Info::tr("Bootable") : Info::tr("Not bootable"));
                hrc = showMediumAttachments(machine, storageCtl, details);
                if (FAILED(hrc))
                    return hrc;
            }
        }
    }
    else if (details != VMINFO_MACHINEREADABLE)
        RTPrintf("%-28s %s\n", Info::tr("Storage Controllers:"), Info::tr("<none>"));

    if (details == VMINFO_MACHINEREADABLE)
        for (size_t j = 0; j < storageCtls.size(); ++ j)
        {
            hrc = showMediumAttachments(machine, storageCtls[j], details);
            if (FAILED(hrc))
                return hrc;
        }

    /* get the maximum amount of NICS */
    ULONG maxNICs = getMaxNics(machine);

    for (ULONG currentNIC = 0; currentNIC < maxNICs; currentNIC++)
    {
        ComPtr<INetworkAdapter> nic;
        hrc = machine->GetNetworkAdapter(currentNIC, nic.asOutParam());
        if (SUCCEEDED(hrc) && nic)
        {
            FmtNm(szNm, details == VMINFO_MACHINEREADABLE ? "nic%u" : Info::tr("NIC %u:"), currentNIC + 1);

            BOOL fEnabled;
            nic->COMGETTER(Enabled)(&fEnabled);
            if (!fEnabled)
            {
                if (details == VMINFO_MACHINEREADABLE)
                    RTPrintf("%s=\"none\"\n", szNm);
                else
                    RTPrintf(Info::tr("%-28s disabled\n"), szNm);
            }
            else
            {
                Bstr strMACAddress;
                nic->COMGETTER(MACAddress)(strMACAddress.asOutParam());
                Utf8Str strAttachment;
                Utf8Str strNatSettings;
                Utf8Str strNatForwardings;
                NetworkAttachmentType_T attachment;
                nic->COMGETTER(AttachmentType)(&attachment);
                switch (attachment)
                {
                    case NetworkAttachmentType_Null:
                        if (details == VMINFO_MACHINEREADABLE)
                            strAttachment = "null";
                        else
                            strAttachment = Info::tr("none");
                        break;

                    case NetworkAttachmentType_NAT:
                    {
                        Bstr strNetwork;
                        ComPtr<INATEngine> engine;
                        nic->COMGETTER(NATEngine)(engine.asOutParam());
                        engine->COMGETTER(Network)(strNetwork.asOutParam());
                        com::SafeArray<BSTR> forwardings;
                        engine->COMGETTER(Redirects)(ComSafeArrayAsOutParam(forwardings));
                        strNatForwardings = "";
                        for (size_t i = 0; i < forwardings.size(); ++i)
                        {
                            bool fSkip = false;
                            BSTR r = forwardings[i];
                            Utf8Str utf = Utf8Str(r);
                            Utf8Str strName;
                            Utf8Str strProto;
                            Utf8Str strHostPort;
                            Utf8Str strHostIP;
                            Utf8Str strGuestPort;
                            Utf8Str strGuestIP;
                            size_t pos, ppos;
                            pos = ppos = 0;
#define ITERATE_TO_NEXT_TERM(res, str, pos, ppos)   \
                            do {                                                \
                                pos = str.find(",", ppos);                      \
                                if (pos == Utf8Str::npos)                       \
                                {                                               \
                                    Log(( #res " extracting from %s is failed\n", str.c_str())); \
                                    fSkip = true;                               \
                                }                                               \
                                res = str.substr(ppos, pos - ppos);             \
                                Log2((#res " %s pos:%d, ppos:%d\n", res.c_str(), pos, ppos)); \
                                ppos = pos + 1;                                 \
                            } while (0)
                            ITERATE_TO_NEXT_TERM(strName, utf, pos, ppos);
                            if (fSkip) continue;
                            ITERATE_TO_NEXT_TERM(strProto, utf, pos, ppos);
                            if (fSkip) continue;
                            ITERATE_TO_NEXT_TERM(strHostIP, utf, pos, ppos);
                            if (fSkip) continue;
                            ITERATE_TO_NEXT_TERM(strHostPort, utf, pos, ppos);
                            if (fSkip) continue;
                            ITERATE_TO_NEXT_TERM(strGuestIP, utf, pos, ppos);
                            if (fSkip) continue;
                            strGuestPort = utf.substr(ppos, utf.length() - ppos);
#undef ITERATE_TO_NEXT_TERM
                            switch (strProto.toUInt32())
                            {
                                case NATProtocol_TCP:
                                    strProto = "tcp";
                                    break;
                                case NATProtocol_UDP:
                                    strProto = "udp";
                                    break;
                                default:
                                    strProto = "unk";
                                    break;
                            }
                            if (details == VMINFO_MACHINEREADABLE)
                                /** @todo r=bird: This probably isn't good enough wrt escaping. */
                                strNatForwardings.appendPrintf("Forwarding(%d)=\"%s,%s,%s,%s,%s,%s\"\n",
                                                               i, strName.c_str(), strProto.c_str(),
                                                               strHostIP.c_str(), strHostPort.c_str(),
                                                               strGuestIP.c_str(), strGuestPort.c_str());
                            else
                                strNatForwardings.appendPrintf(Info::tr("NIC %d Rule(%d):   name = %s, protocol = %s, host ip = %s, host port = %s, guest ip = %s, guest port = %s\n"),
                                                               currentNIC + 1, i, strName.c_str(),
                                                               strProto.c_str(), strHostIP.c_str(), strHostPort.c_str(),
                                                               strGuestIP.c_str(), strGuestPort.c_str());
                        }
                        ULONG mtu = 0;
                        ULONG sockSnd = 0;
                        ULONG sockRcv = 0;
                        ULONG tcpSnd = 0;
                        ULONG tcpRcv = 0;
                        engine->GetNetworkSettings(&mtu, &sockSnd, &sockRcv, &tcpSnd, &tcpRcv);

/** @todo r=klaus dnsproxy etc needs to be dumped, too */
                        if (details == VMINFO_MACHINEREADABLE)
                        {
                            RTPrintf("natnet%d=\"%ls\"\n", currentNIC + 1, strNetwork.length() ? strNetwork.raw(): Bstr("nat").raw());
                            strAttachment = "nat";
                            strNatSettings.printf("mtu=\"%d\"\nsockSnd=\"%d\"\nsockRcv=\"%d\"\ntcpWndSnd=\"%d\"\ntcpWndRcv=\"%d\"\n",
                                                  mtu, sockSnd ? sockSnd : 64, sockRcv ? sockRcv : 64, tcpSnd ? tcpSnd : 64, tcpRcv ? tcpRcv : 64);
                        }
                        else
                        {
                            strAttachment = "NAT";
                            strNatSettings.printf(Info::tr("NIC %d Settings:  MTU: %d, Socket (send: %d, receive: %d), TCP Window (send:%d, receive: %d)\n"),
                                                  currentNIC + 1, mtu, sockSnd ? sockSnd : 64, sockRcv ? sockRcv : 64, tcpSnd ? tcpSnd : 64, tcpRcv ? tcpRcv : 64);
                        }
                        break;
                    }

                    case NetworkAttachmentType_Bridged:
                    {
                        Bstr strBridgeAdp;
                        nic->COMGETTER(BridgedInterface)(strBridgeAdp.asOutParam());
                        if (details == VMINFO_MACHINEREADABLE)
                        {
                            RTPrintf("bridgeadapter%d=\"%ls\"\n", currentNIC + 1, strBridgeAdp.raw());
                            strAttachment = "bridged";
                        }
                        else
                            strAttachment.printf(Info::tr("Bridged Interface '%ls'"), strBridgeAdp.raw());
                        break;
                    }

                    case NetworkAttachmentType_Internal:
                    {
                        Bstr strNetwork;
                        nic->COMGETTER(InternalNetwork)(strNetwork.asOutParam());
                        if (details == VMINFO_MACHINEREADABLE)
                        {
                            RTPrintf("intnet%d=\"%ls\"\n", currentNIC + 1, strNetwork.raw());
                            strAttachment = "intnet";
                        }
                        else
                            strAttachment.printf(Info::tr("Internal Network '%s'"), Utf8Str(strNetwork).c_str());
                        break;
                    }

                    case NetworkAttachmentType_HostOnly:
                    {
                        Bstr strHostonlyAdp;
                        nic->COMGETTER(HostOnlyInterface)(strHostonlyAdp.asOutParam());
                        if (details == VMINFO_MACHINEREADABLE)
                        {
                            RTPrintf("hostonlyadapter%d=\"%ls\"\n", currentNIC + 1, strHostonlyAdp.raw());
                            strAttachment = "hostonly";
                        }
                        else
                            strAttachment.printf(Info::tr("Host-only Interface '%ls'"), strHostonlyAdp.raw());
                        break;
                    }

                    case NetworkAttachmentType_Generic:
                    {
                        Bstr strGenericDriver;
                        nic->COMGETTER(GenericDriver)(strGenericDriver.asOutParam());
                        if (details == VMINFO_MACHINEREADABLE)
                        {
                            RTPrintf("generic%d=\"%ls\"\n", currentNIC + 1, strGenericDriver.raw());
                            strAttachment = "Generic";
                        }
                        else
                        {
                            strAttachment.printf(Info::tr("Generic '%ls'"), strGenericDriver.raw());

                            // show the generic properties
                            com::SafeArray<BSTR> aProperties;
                            com::SafeArray<BSTR> aValues;
                            hrc = nic->GetProperties(NULL,
                                                     ComSafeArrayAsOutParam(aProperties),
                                                     ComSafeArrayAsOutParam(aValues));
                            if (SUCCEEDED(hrc))
                            {
                                strAttachment += " { ";
                                for (unsigned i = 0; i < aProperties.size(); ++i)
                                    strAttachment.appendPrintf(!i ? "%ls='%ls'" : ", %ls='%ls'", aProperties[i], aValues[i]);
                                strAttachment += " }";
                            }
                        }
                        break;
                    }

                    case NetworkAttachmentType_NATNetwork:
                    {
                        Bstr strNetwork;
                        nic->COMGETTER(NATNetwork)(strNetwork.asOutParam());
                        if (details == VMINFO_MACHINEREADABLE)
                        {
                            RTPrintf("nat-network%d=\"%ls\"\n", currentNIC + 1, strNetwork.raw());
                            strAttachment = "natnetwork";
                        }
                        else
                            strAttachment.printf(Info::tr("NAT Network '%s'"), Utf8Str(strNetwork).c_str());
                        break;
                    }

#ifdef VBOX_WITH_VMNET
                    case NetworkAttachmentType_HostOnlyNetwork:
                    {
                        Bstr strNetwork;
                        nic->COMGETTER(HostOnlyNetwork)(strNetwork.asOutParam());
                        if (details == VMINFO_MACHINEREADABLE)
                        {
                            RTPrintf("hostonly-network%d=\"%ls\"\n", currentNIC + 1, strNetwork.raw());
                            strAttachment = "hostonlynetwork";
                        }
                        else
                            strAttachment.printf(Info::tr("Host Only Network '%s'"), Utf8Str(strNetwork).c_str());
                        break;
                    }
#endif /* VBOX_WITH_VMNET */

#ifdef VBOX_WITH_CLOUD_NET
                    case NetworkAttachmentType_Cloud:
                    {
                        Bstr strNetwork;
                        nic->COMGETTER(CloudNetwork)(strNetwork.asOutParam());
                        if (details == VMINFO_MACHINEREADABLE)
                        {
                            RTPrintf("cloud-network%d=\"%ls\"\n", currentNIC + 1, strNetwork.raw());
                            strAttachment = "cloudnetwork";
                        }
                        else
                            strAttachment.printf(Info::tr("Cloud Network '%s'"), Utf8Str(strNetwork).c_str());
                        break;
                    }
#endif /* VBOX_WITH_CLOUD_NET */

                    default:
                        if (details == VMINFO_MACHINEREADABLE)
                            strAttachment = "unknown";
                        else
                            strAttachment = Info::tr("unknown");
                        break;
                }

                /* cable connected */
                BOOL fConnected;
                nic->COMGETTER(CableConnected)(&fConnected);

                /* promisc policy */
                NetworkAdapterPromiscModePolicy_T enmPromiscModePolicy;
                CHECK_ERROR2I_RET(nic, COMGETTER(PromiscModePolicy)(&enmPromiscModePolicy), hrcCheck);
                const char *pszPromiscuousGuestPolicy;
                switch (enmPromiscModePolicy)
                {
                    case NetworkAdapterPromiscModePolicy_Deny:          pszPromiscuousGuestPolicy = Info::tr("deny"); break;
                    case NetworkAdapterPromiscModePolicy_AllowNetwork:  pszPromiscuousGuestPolicy = Info::tr("allow-vms"); break;
                    case NetworkAdapterPromiscModePolicy_AllowAll:      pszPromiscuousGuestPolicy = Info::tr("allow-all"); break;
                    default: AssertFailedReturn(E_INVALIDARG);
                }

                /* trace stuff */
                BOOL fTraceEnabled;
                nic->COMGETTER(TraceEnabled)(&fTraceEnabled);
                Bstr traceFile;
                nic->COMGETTER(TraceFile)(traceFile.asOutParam());

                /* NIC type */
                NetworkAdapterType_T NICType;
                nic->COMGETTER(AdapterType)(&NICType);
                const char *pszNICType;
                switch (NICType)
                {
                    case NetworkAdapterType_Am79C970A:  pszNICType = "Am79C970A";   break;
                    case NetworkAdapterType_Am79C973:   pszNICType = "Am79C973";    break;
                    case NetworkAdapterType_Am79C960:   pszNICType = "Am79C960";    break;
#ifdef VBOX_WITH_E1000
                    case NetworkAdapterType_I82540EM:   pszNICType = "82540EM";     break;
                    case NetworkAdapterType_I82543GC:   pszNICType = "82543GC";     break;
                    case NetworkAdapterType_I82545EM:   pszNICType = "82545EM";     break;
#endif
#ifdef VBOX_WITH_VIRTIO
                    case NetworkAdapterType_Virtio:     pszNICType = "virtio";      break;
#endif
                    case NetworkAdapterType_NE1000:     pszNICType = "NE1000";      break;
                    case NetworkAdapterType_NE2000:     pszNICType = "NE2000";      break;
                    case NetworkAdapterType_WD8003:     pszNICType = "WD8003";      break;
                    case NetworkAdapterType_WD8013:     pszNICType = "WD8013";      break;
                    case NetworkAdapterType_ELNK2:      pszNICType = "3C503";       break;
                    case NetworkAdapterType_ELNK1:      pszNICType = "3C501";       break;
                    default:
                        AssertFailed();
                        if (details == VMINFO_MACHINEREADABLE)
                            pszNICType = "unknown";
                        else
                            pszNICType = Info::tr("unknown");
                        break;
                }

                /* reported line speed */
                ULONG ulLineSpeed;
                nic->COMGETTER(LineSpeed)(&ulLineSpeed);

                /* boot priority of the adapter */
                ULONG ulBootPriority;
                nic->COMGETTER(BootPriority)(&ulBootPriority);

                /* bandwidth group */
                ComObjPtr<IBandwidthGroup> pBwGroup;
                Bstr strBwGroup;
                nic->COMGETTER(BandwidthGroup)(pBwGroup.asOutParam());
                if (!pBwGroup.isNull())
                    pBwGroup->COMGETTER(Name)(strBwGroup.asOutParam());

                if (details == VMINFO_MACHINEREADABLE)
                {
                    RTPrintf("macaddress%d=\"%ls\"\n", currentNIC + 1, strMACAddress.raw());
                    RTPrintf("cableconnected%d=\"%s\"\n", currentNIC + 1, fConnected ? "on" : "off");
                    RTPrintf("nic%d=\"%s\"\n", currentNIC + 1, strAttachment.c_str());
                    RTPrintf("nictype%d=\"%s\"\n", currentNIC + 1, pszNICType);
                    RTPrintf("nicspeed%d=\"%d\"\n", currentNIC + 1, ulLineSpeed);
                }
                else
                    RTPrintf(Info::tr("%-28s MAC: %ls, Attachment: %s, Cable connected: %s, Trace: %s (file: %ls), Type: %s, Reported speed: %d Mbps, Boot priority: %d, Promisc Policy: %s, Bandwidth group: %ls\n"),
                             szNm, strMACAddress.raw(), strAttachment.c_str(),
                             fConnected ? Info::tr("on") : Info::tr("off"),
                             fTraceEnabled ? Info::tr("on") : Info::tr("off"),
                             traceFile.isEmpty() ? Bstr(Info::tr("none")).raw() : traceFile.raw(),
                             pszNICType,
                             ulLineSpeed / 1000,
                             (int)ulBootPriority,
                             pszPromiscuousGuestPolicy,
                             strBwGroup.isEmpty() ? Bstr(Info::tr("none")).raw() : strBwGroup.raw());
                if (strNatSettings.length())
                    RTPrintf(strNatSettings.c_str());
                if (strNatForwardings.length())
                    RTPrintf(strNatForwardings.c_str());
            }
        }
    }

    /* Pointing device information */
    PointingHIDType_T aPointingHID;
    const char *pszHID = Info::tr("Unknown");
    const char *pszMrHID = "unknown";
    machine->COMGETTER(PointingHIDType)(&aPointingHID);
    switch (aPointingHID)
    {
        case PointingHIDType_None:
            pszHID = Info::tr("None");
            pszMrHID = "none";
            break;
        case PointingHIDType_PS2Mouse:
            pszHID = Info::tr("PS/2 Mouse");
            pszMrHID = "ps2mouse";
            break;
        case PointingHIDType_USBMouse:
            pszHID = Info::tr("USB Mouse");
            pszMrHID = "usbmouse";
            break;
        case PointingHIDType_USBTablet:
            pszHID = Info::tr("USB Tablet");
            pszMrHID = "usbtablet";
            break;
        case PointingHIDType_ComboMouse:
            pszHID = Info::tr("USB Tablet and PS/2 Mouse");
            pszMrHID = "combomouse";
            break;
        case PointingHIDType_USBMultiTouch:
            pszHID = Info::tr("USB Multi-Touch");
            pszMrHID = "usbmultitouch";
            break;
        default:
            break;
    }
    SHOW_UTF8_STRING("hidpointing", Info::tr("Pointing Device:"), details == VMINFO_MACHINEREADABLE ? pszMrHID : pszHID);

    /* Keyboard device information */
    KeyboardHIDType_T aKeyboardHID;
    machine->COMGETTER(KeyboardHIDType)(&aKeyboardHID);
    pszHID = Info::tr("Unknown");
    pszMrHID = "unknown";
    switch (aKeyboardHID)
    {
        case KeyboardHIDType_None:
            pszHID = Info::tr("None");
            pszMrHID = "none";
            break;
        case KeyboardHIDType_PS2Keyboard:
            pszHID = Info::tr("PS/2 Keyboard");
            pszMrHID = "ps2kbd";
            break;
        case KeyboardHIDType_USBKeyboard:
            pszHID = Info::tr("USB Keyboard");
            pszMrHID = "usbkbd";
            break;
        case KeyboardHIDType_ComboKeyboard:
            pszHID = Info::tr("USB and PS/2 Keyboard");
            pszMrHID = "combokbd";
            break;
        default:
            break;
    }
    SHOW_UTF8_STRING("hidkeyboard", Info::tr("Keyboard Device:"), details == VMINFO_MACHINEREADABLE ? pszMrHID : pszHID);

    ComPtr<ISystemProperties> sysProps;
    pVirtualBox->COMGETTER(SystemProperties)(sysProps.asOutParam());

    /* get the maximum amount of UARTs */
    ULONG maxUARTs = 0;
    platformProperties->COMGETTER(SerialPortCount)(&maxUARTs);
    for (ULONG currentUART = 0; currentUART < maxUARTs; currentUART++)
    {
        ComPtr<ISerialPort> uart;
        hrc = machine->GetSerialPort(currentUART, uart.asOutParam());
        if (SUCCEEDED(hrc) && uart)
        {
            FmtNm(szNm, details == VMINFO_MACHINEREADABLE ? "uart%u" : Info::tr("UART %u:"), currentUART + 1);

            /* show the config of this UART */
            BOOL fEnabled;
            uart->COMGETTER(Enabled)(&fEnabled);
            if (!fEnabled)
            {
                if (details == VMINFO_MACHINEREADABLE)
                    RTPrintf("%s=\"off\"\n", szNm);
                else
                    RTPrintf(Info::tr("%-28s disabled\n"), szNm);
            }
            else
            {
                ULONG ulIRQ, ulIOAddress;
                PortMode_T HostMode;
                Bstr path;
                BOOL fServer;
                UartType_T UartType;
                uart->COMGETTER(IRQ)(&ulIRQ);
                uart->COMGETTER(IOAddress)(&ulIOAddress);
                uart->COMGETTER(Path)(path.asOutParam());
                uart->COMGETTER(Server)(&fServer);
                uart->COMGETTER(HostMode)(&HostMode);
                uart->COMGETTER(UartType)(&UartType);

                if (details == VMINFO_MACHINEREADABLE)
                    RTPrintf("%s=\"%#06x,%d\"\n", szNm, ulIOAddress, ulIRQ);
                else
                    RTPrintf(Info::tr("%-28s I/O address: %#06x, IRQ: %d"), szNm, ulIOAddress, ulIRQ);
                switch (HostMode)
                {
                    default:
                    case PortMode_Disconnected:
                        if (details == VMINFO_MACHINEREADABLE)
                            RTPrintf("uartmode%d=\"disconnected\"\n", currentUART + 1);
                        else
                            RTPrintf(Info::tr(", disconnected"));
                        break;
                    case PortMode_RawFile:
                        if (details == VMINFO_MACHINEREADABLE)
                            RTPrintf("uartmode%d=\"file,%ls\"\n", currentUART + 1,
                                     path.raw());
                        else
                            RTPrintf(Info::tr(", attached to raw file '%ls'\n"),
                                     path.raw());
                        break;
                    case PortMode_TCP:
                        if (details == VMINFO_MACHINEREADABLE)
                            RTPrintf("uartmode%d=\"%s,%ls\"\n", currentUART + 1,
                                     fServer ? "tcpserver" : "tcpclient", path.raw());
                        else
                            RTPrintf(Info::tr(", attached to tcp (%s) '%ls'"),
                                     fServer ? Info::tr("server") : Info::tr("client"), path.raw());
                        break;
                    case PortMode_HostPipe:
                        if (details == VMINFO_MACHINEREADABLE)
                            RTPrintf("uartmode%d=\"%s,%ls\"\n", currentUART + 1,
                                     fServer ? "server" : "client", path.raw());
                        else
                            RTPrintf(Info::tr(", attached to pipe (%s) '%ls'"),
                                     fServer ? Info::tr("server") : Info::tr("client"), path.raw());
                        break;
                    case PortMode_HostDevice:
                        if (details == VMINFO_MACHINEREADABLE)
                            RTPrintf("uartmode%d=\"%ls\"\n", currentUART + 1,
                                     path.raw());
                        else
                            RTPrintf(Info::tr(", attached to device '%ls'"), path.raw());
                        break;
                }
                switch (UartType)
                {
                    default:
                    case UartType_U16450:
                        if (details == VMINFO_MACHINEREADABLE)
                            RTPrintf("uarttype%d=\"16450\"\n", currentUART + 1);
                        else
                            RTPrintf(", 16450\n");
                        break;
                    case UartType_U16550A:
                        if (details == VMINFO_MACHINEREADABLE)
                            RTPrintf("uarttype%d=\"16550A\"\n", currentUART + 1);
                        else
                            RTPrintf(", 16550A\n");
                        break;
                    case UartType_U16750:
                        if (details == VMINFO_MACHINEREADABLE)
                            RTPrintf("uarttype%d=\"16750\"\n", currentUART + 1);
                        else
                            RTPrintf(", 16750\n");
                        break;
                }
            }
        }
    }

    /* get the maximum amount of LPTs */
    ULONG maxLPTs = 0;
    platformProperties->COMGETTER(ParallelPortCount)(&maxLPTs);
    for (ULONG currentLPT = 0; currentLPT < maxLPTs; currentLPT++)
    {
        ComPtr<IParallelPort> lpt;
        hrc = machine->GetParallelPort(currentLPT, lpt.asOutParam());
        if (SUCCEEDED(hrc) && lpt)
        {
            FmtNm(szNm, details == VMINFO_MACHINEREADABLE ? "lpt%u" : Info::tr("LPT %u:"), currentLPT + 1);

            /* show the config of this LPT */
            BOOL fEnabled;
            lpt->COMGETTER(Enabled)(&fEnabled);
            if (!fEnabled)
            {
                if (details == VMINFO_MACHINEREADABLE)
                    RTPrintf("%s=\"off\"\n", szNm);
                else
                    RTPrintf(Info::tr("%-28s disabled\n"), szNm);
            }
            else
            {
                ULONG ulIRQ, ulIOBase;
                Bstr path;
                lpt->COMGETTER(IRQ)(&ulIRQ);
                lpt->COMGETTER(IOBase)(&ulIOBase);
                lpt->COMGETTER(Path)(path.asOutParam());

                if (details == VMINFO_MACHINEREADABLE)
                    RTPrintf("%s=\"%#06x,%d\"\n", szNm, ulIOBase, ulIRQ);
                else
                    RTPrintf(Info::tr("%-28s I/O base: %#06x, IRQ: %d"), szNm, ulIOBase, ulIRQ);
                if (details == VMINFO_MACHINEREADABLE)
                    RTPrintf("lptmode%d=\"%ls\"\n", currentLPT + 1, path.raw());
                else
                    RTPrintf(Info::tr(", attached to device '%ls'\n"), path.raw());
            }
        }
    }

    ComPtr<IAudioSettings> audioSettings;
    ComPtr<IAudioAdapter>  audioAdapter;
    hrc = machine->COMGETTER(AudioSettings)(audioSettings.asOutParam());
    if (SUCCEEDED(hrc))
        hrc = audioSettings->COMGETTER(Adapter)(audioAdapter.asOutParam());
    if (SUCCEEDED(hrc))
    {
        const char *pszDrv   = Info::tr("Unknown");
        const char *pszCtrl  = Info::tr("Unknown");
        const char *pszCodec = Info::tr("Unknown");
        BOOL fEnabled;
        hrc = audioAdapter->COMGETTER(Enabled)(&fEnabled);
        if (SUCCEEDED(hrc) && fEnabled)
        {
            AudioDriverType_T enmDrvType;
            hrc = audioAdapter->COMGETTER(AudioDriver)(&enmDrvType);
            switch (enmDrvType)
            {
                case AudioDriverType_Default:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszDrv = "default";
                    else
                        pszDrv = Info::tr("Default");
                    break;
                case AudioDriverType_Null:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszDrv = "null";
                    else
                        pszDrv = Info::tr("Null");
                    break;
                case AudioDriverType_OSS:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszDrv = "oss";
                    else
                        pszDrv = "OSS";
                    break;
                case AudioDriverType_ALSA:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszDrv = "alsa";
                    else
                        pszDrv = "ALSA";
                    break;
                case AudioDriverType_Pulse:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszDrv = "pulse";
                    else
                        pszDrv = "PulseAudio";
                    break;
                case AudioDriverType_WinMM:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszDrv = "winmm";
                    else
                        pszDrv = "WINMM";
                    break;
                case AudioDriverType_DirectSound:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszDrv = "dsound";
                    else
                        pszDrv = "DirectSound";
                    break;
                case AudioDriverType_WAS:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszDrv = "was";
                    else
                        pszDrv = "Windows Audio Session (WAS)";
                    break;
                case AudioDriverType_CoreAudio:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszDrv = "coreaudio";
                    else
                        pszDrv = "CoreAudio";
                    break;
                case AudioDriverType_SolAudio:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszDrv = "solaudio";
                    else
                        pszDrv = "SolAudio";
                    break;
                default:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszDrv = "unknown";
                    break;
            }
            AudioControllerType_T enmCtrlType;
            hrc = audioAdapter->COMGETTER(AudioController)(&enmCtrlType);
            switch (enmCtrlType)
            {
                case AudioControllerType_AC97:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszCtrl = "ac97";
                    else
                        pszCtrl = "AC97";
                    break;
                case AudioControllerType_SB16:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszCtrl = "sb16";
                    else
                        pszCtrl = "SB16";
                    break;
                case AudioControllerType_HDA:
                    if (details == VMINFO_MACHINEREADABLE)
                        pszCtrl = "hda";
                    else
                        pszCtrl = "HDA";
                    break;
                default:
                    break;
            }
            AudioCodecType_T enmCodecType;
            hrc = audioAdapter->COMGETTER(AudioCodec)(&enmCodecType);
            switch (enmCodecType)
            {
                case AudioCodecType_SB16:
                    pszCodec = "SB16";
                    break;
                case AudioCodecType_STAC9700:
                    pszCodec = "STAC9700";
                    break;
                case AudioCodecType_AD1980:
                    pszCodec = "AD1980";
                    break;
                case AudioCodecType_STAC9221:
                    pszCodec = "STAC9221";
                    break;
                case AudioCodecType_Null: break; /* Shut up MSC. */
                default:                  break;
            }
        }
        else
            fEnabled = FALSE;

        if (details == VMINFO_MACHINEREADABLE)
            RTPrintf("audio=\"%s\"\n", fEnabled ? pszDrv : "none");
        else
        {
            RTPrintf("%-28s %s", Info::tr("Audio:"), fEnabled ? Info::tr("enabled") : Info::tr("disabled"));
            if (fEnabled)
                RTPrintf(Info::tr(" (Driver: %s, Controller: %s, Codec: %s)"), pszDrv, pszCtrl, pszCodec);
            RTPrintf("\n");
        }
        SHOW_BOOLEAN_PROP(audioAdapter, EnabledOut,  "audio_out",  Info::tr("Audio playback:"));
        SHOW_BOOLEAN_PROP(audioAdapter, EnabledIn, "audio_in", Info::tr("Audio capture:"));

        /** @todo Add printing run-time host audio device selection(s) here. */
    }

    /* Shared clipboard */
    {
        const char *psz;
        ClipboardMode_T enmMode = (ClipboardMode_T)0;
        hrc = machine->COMGETTER(ClipboardMode)(&enmMode);
        switch (enmMode)
        {
            case ClipboardMode_Disabled:
                psz = "disabled";
                break;
            case ClipboardMode_HostToGuest:
                psz = details == VMINFO_MACHINEREADABLE ? "hosttoguest" : Info::tr("HostToGuest");
                break;
            case ClipboardMode_GuestToHost:
                psz = details == VMINFO_MACHINEREADABLE ? "guesttohost" : Info::tr("GuestToHost");
                break;
            case ClipboardMode_Bidirectional:
                psz = details == VMINFO_MACHINEREADABLE ? "bidirectional" : Info::tr("Bidirectional");
                break;
            default:
                psz = details == VMINFO_MACHINEREADABLE ? "unknown" : Info::tr("Unknown");
                break;
        }
        SHOW_UTF8_STRING("clipboard", Info::tr("Clipboard Mode:"), psz);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        SHOW_BOOLEAN_PROP(machine, ClipboardFileTransfersEnabled, "clipboard_file_transfers", Info::tr("Clipboard file transfers:"));
#endif
    }

    /* Drag and drop */
    {
        const char *psz;
        DnDMode_T enmMode;
        hrc = machine->COMGETTER(DnDMode)(&enmMode);
        switch (enmMode)
        {
            case DnDMode_Disabled:
                psz = "disabled";
                break;
            case DnDMode_HostToGuest:
                psz = details == VMINFO_MACHINEREADABLE ? "hosttoguest" : Info::tr("HostToGuest");
                break;
            case DnDMode_GuestToHost:
                psz = details == VMINFO_MACHINEREADABLE ? "guesttohost" : Info::tr("GuestToHost");
                break;
            case DnDMode_Bidirectional:
                psz = details == VMINFO_MACHINEREADABLE ? "bidirectional" : Info::tr("Bidirectional");
                break;
            default:
                psz = details == VMINFO_MACHINEREADABLE ? "unknown" : Info::tr("Unknown");
                break;
        }
        SHOW_UTF8_STRING("draganddrop", Info::tr("Drag and drop Mode:"), psz);
    }

    {
        SessionState_T sessState;
        hrc = machine->COMGETTER(SessionState)(&sessState);
        if (SUCCEEDED(hrc) && sessState != SessionState_Unlocked)
        {
            Bstr sessName;
            hrc = machine->COMGETTER(SessionName)(sessName.asOutParam());
            if (SUCCEEDED(hrc) && !sessName.isEmpty())
                SHOW_BSTR_STRING("SessionName", Info::tr("Session name:"), sessName);
        }
    }

    if (pConsole)
    {
        do
        {
            ComPtr<IDisplay> display;
            hrc = pConsole->COMGETTER(Display)(display.asOutParam());
            if (hrc == E_ACCESSDENIED || display.isNull())
                break; /* VM not powered up */
            if (FAILED(hrc))
            {
                com::GlueHandleComError(pConsole, "COMGETTER(Display)(display.asOutParam())", hrc, __FILE__, __LINE__);
                return hrc;
            }
            ULONG xRes, yRes, bpp;
            LONG xOrigin, yOrigin;
            GuestMonitorStatus_T monitorStatus;
            hrc = display->GetScreenResolution(0, &xRes, &yRes, &bpp, &xOrigin, &yOrigin, &monitorStatus);
            if (hrc == E_ACCESSDENIED)
                break; /* VM not powered up */
            if (FAILED(hrc))
            {
                com::ErrorInfo info(display, COM_IIDOF(IDisplay));
                GluePrintErrorInfo(info);
                return hrc;
            }
            if (details == VMINFO_MACHINEREADABLE)
                RTPrintf("VideoMode=\"%d,%d,%d\"@%d,%d %d\n", xRes, yRes, bpp, xOrigin, yOrigin, monitorStatus);
            else
            {
                const char *pszMonitorStatus = Info::tr("unknown status");
                switch (monitorStatus)
                {
                    case GuestMonitorStatus_Blank:    pszMonitorStatus = Info::tr("blank"); break;
                    case GuestMonitorStatus_Enabled:  pszMonitorStatus = Info::tr("enabled"); break;
                    case GuestMonitorStatus_Disabled: pszMonitorStatus = Info::tr("disabled"); break;
                    default: break;
                }
                RTPrintf("%-28s %dx%dx%d at %d,%d %s\n", Info::tr("Video mode:"), xRes, yRes, bpp, xOrigin, yOrigin, pszMonitorStatus);
            }
        }
        while (0);
    }

    /*
     * Remote Desktop
     */
    ComPtr<IVRDEServer> vrdeServer;
    hrc = machine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
    if (SUCCEEDED(hrc) && vrdeServer)
    {
        BOOL fEnabled = false;
        vrdeServer->COMGETTER(Enabled)(&fEnabled);
        if (fEnabled)
        {
            LONG currentPort = -1;
            Bstr ports;
            vrdeServer->GetVRDEProperty(Bstr("TCP/Ports").raw(), ports.asOutParam());
            Bstr address;
            vrdeServer->GetVRDEProperty(Bstr("TCP/Address").raw(), address.asOutParam());
            BOOL fMultiCon;
            vrdeServer->COMGETTER(AllowMultiConnection)(&fMultiCon);
            BOOL fReuseCon;
            vrdeServer->COMGETTER(ReuseSingleConnection)(&fReuseCon);
            Bstr videoChannel;
            vrdeServer->GetVRDEProperty(Bstr("VideoChannel/Enabled").raw(), videoChannel.asOutParam());
            BOOL fVideoChannel =    (videoChannel.compare(Bstr("true"), Bstr::CaseInsensitive)== 0)
                                 || (videoChannel == "1");
            Bstr videoChannelQuality;
            vrdeServer->GetVRDEProperty(Bstr("VideoChannel/Quality").raw(), videoChannelQuality.asOutParam());
            AuthType_T authType = (AuthType_T)0;
            const char *strAuthType;
            vrdeServer->COMGETTER(AuthType)(&authType);
            switch (authType)
            {
                case AuthType_Null:
                    if (details == VMINFO_MACHINEREADABLE)
                        strAuthType = "null";
                    else
                        strAuthType = Info::tr("null");
                    break;
                case AuthType_External:
                    if (details == VMINFO_MACHINEREADABLE)
                        strAuthType = "external";
                    else
                        strAuthType = Info::tr("external");
                    break;
                case AuthType_Guest:
                    if (details == VMINFO_MACHINEREADABLE)
                        strAuthType = "guest";
                    else
                        strAuthType = Info::tr("guest");
                    break;
                default:
                    if (details == VMINFO_MACHINEREADABLE)
                        strAuthType = "unknown";
                    else
                        strAuthType = Info::tr("unknown");
                    break;
            }
            if (pConsole)
            {
                ComPtr<IVRDEServerInfo> vrdeServerInfo;
                CHECK_ERROR_RET(pConsole, COMGETTER(VRDEServerInfo)(vrdeServerInfo.asOutParam()), hrc);
                if (!vrdeServerInfo.isNull())
                {
                    hrc = vrdeServerInfo->COMGETTER(Port)(&currentPort);
                    if (hrc == E_ACCESSDENIED)
                    {
                        currentPort = -1; /* VM not powered up */
                    }
                    else if (FAILED(hrc))
                    {
                        com::ErrorInfo info(vrdeServerInfo, COM_IIDOF(IVRDEServerInfo));
                        GluePrintErrorInfo(info);
                        return hrc;
                    }
                }
            }
            if (details == VMINFO_MACHINEREADABLE)
            {
                RTPrintf("vrde=\"on\"\n");
                RTPrintf("vrdeport=%d\n", currentPort);
                RTPrintf("vrdeports=\"%ls\"\n", ports.raw());
                RTPrintf("vrdeaddress=\"%ls\"\n", address.raw());
                RTPrintf("vrdeauthtype=\"%s\"\n", strAuthType);
                RTPrintf("vrdemulticon=\"%s\"\n", fMultiCon ? "on" : "off");
                RTPrintf("vrdereusecon=\"%s\"\n", fReuseCon ? "on" : "off");
                RTPrintf("vrdevideochannel=\"%s\"\n", fVideoChannel ? "on" : "off");
                if (fVideoChannel)
                    RTPrintf("vrdevideochannelquality=\"%ls\"\n", videoChannelQuality.raw());
            }
            else
            {
                if (address.isEmpty())
                    address = "0.0.0.0";
                RTPrintf(Info::tr("%-28s enabled (Address %ls, Ports %ls, MultiConn: %s, ReuseSingleConn: %s, Authentication type: %s)\n"),
                         "VRDE:", address.raw(), ports.raw(), fMultiCon ? Info::tr("on") : Info::tr("off"),
                         fReuseCon ? Info::tr("on") : Info::tr("off"), strAuthType);
                if (pConsole && currentPort != -1 && currentPort != 0)
                   RTPrintf("%-28s %d\n", Info::tr("VRDE port:"), currentPort);
                if (fVideoChannel)
                    RTPrintf(Info::tr("%-28s enabled (Quality %ls)\n"), Info::tr("Video redirection:"), videoChannelQuality.raw());
                else
                    RTPrintf(Info::tr("%-28s disabled\n"), Info::tr("Video redirection:"));
            }
            com::SafeArray<BSTR> aProperties;
            if (SUCCEEDED(vrdeServer->COMGETTER(VRDEProperties)(ComSafeArrayAsOutParam(aProperties))))
            {
                unsigned i;
                for (i = 0; i < aProperties.size(); ++i)
                {
                    Bstr value;
                    vrdeServer->GetVRDEProperty(aProperties[i], value.asOutParam());
                    if (details == VMINFO_MACHINEREADABLE)
                    {
                        if (value.isEmpty())
                            RTPrintf("vrdeproperty[%ls]=<not set>\n", aProperties[i]);
                        else
                            RTPrintf("vrdeproperty[%ls]=\"%ls\"\n", aProperties[i], value.raw());
                    }
                    else
                    {
                        if (value.isEmpty())
                            RTPrintf(Info::tr("%-28s: %-10lS = <not set>\n"), Info::tr("VRDE property"), aProperties[i]);
                        else
                            RTPrintf("%-28s: %-10lS = \"%ls\"\n", Info::tr("VRDE property"), aProperties[i], value.raw());
                    }
                }
            }
        }
        else
        {
            if (details == VMINFO_MACHINEREADABLE)
                RTPrintf("vrde=\"off\"\n");
            else
                RTPrintf(Info::tr("%-28s disabled\n"), "VRDE:");
        }
    }

    /*
     * USB.
     */
    SafeIfaceArray<IUSBController> USBCtlColl;
    hrc = machine->COMGETTER(USBControllers)(ComSafeArrayAsOutParam(USBCtlColl));
    if (SUCCEEDED(hrc))
    {
        bool fOhciEnabled = false;
        bool fEhciEnabled = false;
        bool fXhciEnabled = false;

        for (unsigned i = 0; i < USBCtlColl.size(); i++)
        {
            USBControllerType_T enmType;

            hrc = USBCtlColl[i]->COMGETTER(Type)(&enmType);
            if (SUCCEEDED(hrc))
            {
                switch (enmType)
                {
                    case USBControllerType_OHCI:
                        fOhciEnabled = true;
                        break;
                    case USBControllerType_EHCI:
                        fEhciEnabled = true;
                        break;
                    case USBControllerType_XHCI:
                        fXhciEnabled = true;
                        break;
                    default:
                        break;
                }
            }
        }

        SHOW_BOOL_VALUE("usb",  "OHCI USB:", fOhciEnabled);
        SHOW_BOOL_VALUE("ehci", "EHCI USB:", fEhciEnabled);
        SHOW_BOOL_VALUE("xhci", "xHCI USB:", fXhciEnabled);
    }

    ComPtr<IUSBDeviceFilters> USBFlts;
    hrc = machine->COMGETTER(USBDeviceFilters)(USBFlts.asOutParam());
    if (SUCCEEDED(hrc))
    {
        SafeIfaceArray <IUSBDeviceFilter> Coll;
        hrc = USBFlts->COMGETTER(DeviceFilters)(ComSafeArrayAsOutParam(Coll));
        if (SUCCEEDED(hrc))
        {
            if (Coll.size() > 0)
            {
                if (details != VMINFO_MACHINEREADABLE)
                    RTPrintf(Info::tr("USB Device Filters:\n"));
                for (size_t index = 0; index < Coll.size(); ++index)
                {
                    ComPtr<IUSBDeviceFilter> DevPtr = Coll[index];

                    if (details != VMINFO_MACHINEREADABLE)
                        SHOW_UTF8_STRING("index", Info::tr("Index:"), FmtNm(szNm, "%zu", index));
                    SHOW_BOOLEAN_PROP_EX(DevPtr, Active,   FmtNm(szNm, "USBFilterActive%zu", index + 1),       Info::tr("  Active:"), Info::tr("yes"), Info::tr("no"));
                    SHOW_STRING_PROP(DevPtr, Name,         FmtNm(szNm, "USBFilterName%zu", index + 1),         Info::tr("  Name:"));
                    SHOW_STRING_PROP(DevPtr, VendorId,     FmtNm(szNm, "USBFilterVendorId%zu", index + 1),     Info::tr("  VendorId:"));
                    SHOW_STRING_PROP(DevPtr, ProductId,    FmtNm(szNm, "USBFilterProductId%zu", index + 1),    Info::tr("  ProductId:"));
                    SHOW_STRING_PROP(DevPtr, Revision,     FmtNm(szNm, "USBFilterRevision%zu", index + 1),     Info::tr("  Revision:"));
                    SHOW_STRING_PROP(DevPtr, Manufacturer, FmtNm(szNm, "USBFilterManufacturer%zu", index + 1), Info::tr("  Manufacturer:"));
                    SHOW_STRING_PROP(DevPtr, Product,      FmtNm(szNm, "USBFilterProduct%zu", index + 1),      Info::tr("  Product:"));
                    SHOW_STRING_PROP(DevPtr, Remote,       FmtNm(szNm, "USBFilterRemote%zu", index + 1),       Info::tr("  Remote:"));
                    SHOW_STRING_PROP(DevPtr, SerialNumber, FmtNm(szNm, "USBFilterSerialNumber%zu", index + 1), Info::tr("  Serial Number:"));
                    if (details != VMINFO_MACHINEREADABLE)
                    {
                        ULONG fMaskedIfs;
                        CHECK_ERROR_RET(DevPtr, COMGETTER(MaskedInterfaces)(&fMaskedIfs), hrc);
                        if (fMaskedIfs)
                            RTPrintf("%-28s %#010x\n", Info::tr("Masked Interfaces:"), fMaskedIfs);
                    }
                }
            }
            else if (details != VMINFO_MACHINEREADABLE)
                RTPrintf("%-28s %s\n", Info::tr("USB Device Filters:"), Info::tr("<none>"));
        }

        if (pConsole)
        {
            {
                SafeIfaceArray<IHostUSBDevice> coll;
                CHECK_ERROR_RET(pConsole, COMGETTER(RemoteUSBDevices)(ComSafeArrayAsOutParam(coll)), hrc);
                hrc = showUsbDevices(coll, "USBRemote", Info::tr("Available remote USB devices:"), details);
                if (FAILED(hrc))
                    return hrc;
            }

            {
                SafeIfaceArray<IUSBDevice> coll;
                CHECK_ERROR_RET(pConsole, COMGETTER(USBDevices)(ComSafeArrayAsOutParam(coll)), hrc);
                showUsbDevices(coll, "USBAttach", Info::tr("Currently attached USB devices:"), details);
                if (FAILED(hrc))
                    return hrc;
            }
        }
    } /* USB */

#ifdef VBOX_WITH_PCI_PASSTHROUGH
    /* Host PCI passthrough devices */
    {
         SafeIfaceArray <IPCIDeviceAttachment> assignments;
         hrc = machine->COMGETTER(PCIDeviceAssignments)(ComSafeArrayAsOutParam(assignments));
         if (SUCCEEDED(hrc))
         {
             if (assignments.size() > 0 && (details != VMINFO_MACHINEREADABLE))
             {
                 RTPrintf(Info::tr("\nAttached physical PCI devices:\n\n"));
             }

             for (size_t index = 0; index < assignments.size(); ++index)
             {
                 ComPtr<IPCIDeviceAttachment> Assignment = assignments[index];
                 char szHostPCIAddress[32], szGuestPCIAddress[32];
                 LONG iHostPCIAddress = -1, iGuestPCIAddress = -1;
                 Bstr DevName;

                 Assignment->COMGETTER(Name)(DevName.asOutParam());
                 Assignment->COMGETTER(HostAddress)(&iHostPCIAddress);
                 Assignment->COMGETTER(GuestAddress)(&iGuestPCIAddress);
                 PCIBusAddress().fromLong(iHostPCIAddress).format(szHostPCIAddress, sizeof(szHostPCIAddress));
                 PCIBusAddress().fromLong(iGuestPCIAddress).format(szGuestPCIAddress, sizeof(szGuestPCIAddress));

                 if (details == VMINFO_MACHINEREADABLE)
                     RTPrintf("AttachedHostPCI=%s,%s\n", szHostPCIAddress, szGuestPCIAddress);
                 else
                     RTPrintf(Info::tr("   Host device %ls at %s attached as %s\n"), DevName.raw(), szHostPCIAddress, szGuestPCIAddress);
             }

             if (assignments.size() > 0 && (details != VMINFO_MACHINEREADABLE))
             {
                 RTPrintf("\n");
             }
         }
    }
    /* Host PCI passthrough devices */
#endif

    /*
     * Bandwidth groups
     */
    if (details != VMINFO_MACHINEREADABLE)
        RTPrintf("%-28s ", Info::tr("Bandwidth groups:"));
    {
        ComPtr<IBandwidthControl> bwCtrl;
        CHECK_ERROR_RET(machine, COMGETTER(BandwidthControl)(bwCtrl.asOutParam()), hrc);

        hrc = showBandwidthGroups(bwCtrl, details);
    }


    /*
     * Shared folders
     */
    if (details != VMINFO_MACHINEREADABLE)
        RTPrintf("%-28s ", Info::tr("Shared folders:"));
    uint32_t numSharedFolders = 0;
#if 0 // not yet implemented
    /* globally shared folders first */
    {
        SafeIfaceArray <ISharedFolder> sfColl;
        CHECK_ERROR_RET(pVirtualBox, COMGETTER(SharedFolders)(ComSafeArrayAsOutParam(sfColl)), rc);
        for (size_t i = 0; i < sfColl.size(); ++i)
        {
            ComPtr<ISharedFolder> sf = sfColl[i];
            showSharedFolder(sf, details, Info::tr("global mapping"), "GlobalMapping", i + 1, numSharedFolders == 0);
            ++numSharedFolders;
        }
    }
#endif
    /* now VM mappings */
    {
        com::SafeIfaceArray <ISharedFolder> folders;
        CHECK_ERROR_RET(machine, COMGETTER(SharedFolders)(ComSafeArrayAsOutParam(folders)), hrc);
        for (size_t i = 0; i < folders.size(); ++i)
        {
            ComPtr<ISharedFolder> sf = folders[i];
            showSharedFolder(sf, details, Info::tr("machine mapping"), "MachineMapping", i + 1, numSharedFolders == 0);
            ++numSharedFolders;
        }
    }
    /* transient mappings */
    if (pConsole)
    {
        com::SafeIfaceArray <ISharedFolder> folders;
        CHECK_ERROR_RET(pConsole, COMGETTER(SharedFolders)(ComSafeArrayAsOutParam(folders)), hrc);
        for (size_t i = 0; i < folders.size(); ++i)
        {
            ComPtr<ISharedFolder> sf = folders[i];
            showSharedFolder(sf, details, Info::tr("transient mapping"), "TransientMapping", i + 1, numSharedFolders == 0);
            ++numSharedFolders;
        }
    }
    if (details != VMINFO_MACHINEREADABLE)
    {
        if (!numSharedFolders)
            RTPrintf(Info::tr("<none>\n"));
        else
            RTPrintf("\n");
    }

    if (pConsole)
    {
        /*
         * Live VRDE info.
         */
        ComPtr<IVRDEServerInfo> vrdeServerInfo;
        CHECK_ERROR_RET(pConsole, COMGETTER(VRDEServerInfo)(vrdeServerInfo.asOutParam()), hrc);
        BOOL    fActive = FALSE;
        ULONG   cNumberOfClients = 0;
        LONG64  BeginTime = 0;
        LONG64  EndTime = 0;
        LONG64  BytesSent = 0;
        LONG64  BytesSentTotal = 0;
        LONG64  BytesReceived = 0;
        LONG64  BytesReceivedTotal = 0;
        Bstr    User;
        Bstr    Domain;
        Bstr    ClientName;
        Bstr    ClientIP;
        ULONG   ClientVersion = 0;
        ULONG   EncryptionStyle = 0;

        if (!vrdeServerInfo.isNull())
        {
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(Active)(&fActive), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(NumberOfClients)(&cNumberOfClients), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(BeginTime)(&BeginTime), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(EndTime)(&EndTime), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(BytesSent)(&BytesSent), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(BytesSentTotal)(&BytesSentTotal), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(BytesReceived)(&BytesReceived), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(BytesReceivedTotal)(&BytesReceivedTotal), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(User)(User.asOutParam()), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(Domain)(Domain.asOutParam()), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(ClientName)(ClientName.asOutParam()), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(ClientIP)(ClientIP.asOutParam()), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(ClientVersion)(&ClientVersion), hrc);
            CHECK_ERROR_RET(vrdeServerInfo, COMGETTER(EncryptionStyle)(&EncryptionStyle), hrc);
        }

        SHOW_BOOL_VALUE_EX("VRDEActiveConnection", Info::tr("VRDE Connection:"), fActive, Info::tr("active"), Info::tr("not active"));
        SHOW_ULONG_VALUE("VRDEClients=", Info::tr("Clients so far:"), cNumberOfClients, "");

        if (cNumberOfClients > 0)
        {
            char szTimeValue[128];
            makeTimeStr(szTimeValue, sizeof(szTimeValue), BeginTime);
            if (fActive)
                SHOW_UTF8_STRING("VRDEStartTime", Info::tr("Start time:"), szTimeValue);
            else
            {
                SHOW_UTF8_STRING("VRDELastStartTime", Info::tr("Last started:"), szTimeValue);
                makeTimeStr(szTimeValue, sizeof(szTimeValue), EndTime);
                SHOW_UTF8_STRING("VRDELastEndTime", Info::tr("Last ended:"), szTimeValue);
            }

            int64_t ThroughputSend = 0;
            int64_t ThroughputReceive = 0;
            if (EndTime != BeginTime)
            {
                ThroughputSend = (BytesSent * 1000) / (EndTime - BeginTime);
                ThroughputReceive = (BytesReceived * 1000) / (EndTime - BeginTime);
            }
            SHOW_LONG64_VALUE("VRDEBytesSent", Info::tr("Sent:"), BytesSent, Info::tr("Bytes"));
            SHOW_LONG64_VALUE("VRDEThroughputSend", Info::tr("Average speed:"), ThroughputSend, Info::tr("B/s"));
            SHOW_LONG64_VALUE("VRDEBytesSentTotal", Info::tr("Sent total:"), BytesSentTotal, Info::tr("Bytes"));

            SHOW_LONG64_VALUE("VRDEBytesReceived", Info::tr("Received:"), BytesReceived, Info::tr("Bytes"));
            SHOW_LONG64_VALUE("VRDEThroughputReceive", Info::tr("Speed:"), ThroughputReceive, Info::tr("B/s"));
            SHOW_LONG64_VALUE("VRDEBytesReceivedTotal", Info::tr("Received total:"), BytesReceivedTotal, Info::tr("Bytes"));

            if (fActive)
            {
                SHOW_BSTR_STRING("VRDEUserName", Info::tr("User name:"), User);
                SHOW_BSTR_STRING("VRDEDomain", Info::tr("Domain:"), Domain);
                SHOW_BSTR_STRING("VRDEClientName", Info::tr("Client name:"), ClientName);
                SHOW_BSTR_STRING("VRDEClientIP", Info::tr("Client IP:"), ClientIP);
                SHOW_ULONG_VALUE("VRDEClientVersion", Info::tr("Client version:"), ClientVersion, "");
                SHOW_UTF8_STRING("VRDEEncryption", Info::tr("Encryption:"), EncryptionStyle == 0 ? "RDP4" : "RDP5 (X.509)");
            }
        }
    }

#ifdef VBOX_WITH_RECORDING
    {
        ComPtr<IRecordingSettings> recordingSettings;
        CHECK_ERROR_RET(machine, COMGETTER(RecordingSettings)(recordingSettings.asOutParam()), hrc);

        BOOL fStarted = FALSE;
        ComPtr<IProgress> progress;
        hrc = recordingSettings->COMGETTER(Progress)(progress.asOutParam());
        if (SUCCEEDED(hrc))
        {
            hrc = progress->COMGETTER(Completed)(&fStarted);
            fStarted = !fStarted;
        }
        SHOW_BOOL_VALUE_EX("recording_started", Info::tr("Recording status:"), fStarted, Info::tr("started"), Info::tr("stopped"));

        BOOL fEnabled;
        CHECK_ERROR_RET(recordingSettings, COMGETTER(Enabled)(&fEnabled), hrc);
        SHOW_BOOL_VALUE_EX("recording_enabled", Info::tr("Recording enabled:"), fEnabled, Info::tr("yes"), Info::tr("no"));

        SafeIfaceArray <IRecordingScreenSettings> saScreenSettings;
        CHECK_ERROR_RET(recordingSettings, COMGETTER(Screens)(ComSafeArrayAsOutParam(saScreenSettings)), hrc);

        SHOW_ULONG_VALUE("recording_screens", Info::tr("Recording screens:"), saScreenSettings.size(), "");

        for (size_t i = 0; i < saScreenSettings.size(); ++i)
        {
            ComPtr<IRecordingScreenSettings> screenSettings = saScreenSettings[i];

            FmtNm(szNm, details == VMINFO_MACHINEREADABLE ? "rec_screen%zu" : Info::tr("Screen %u:"), i);
            RTPrintf(Info::tr(" %s\n"), szNm);

            CHECK_ERROR_RET(screenSettings, COMGETTER(Enabled)(&fEnabled), hrc);
            ULONG idScreen;
            CHECK_ERROR_RET(screenSettings, COMGETTER(Id)(&idScreen), hrc);
            com::SafeArray<RecordingFeature_T> vecFeatures;
            CHECK_ERROR_RET(screenSettings, COMGETTER(Features)(ComSafeArrayAsOutParam(vecFeatures)), hrc);
            ULONG Width;
            CHECK_ERROR_RET(screenSettings, COMGETTER(VideoWidth)(&Width), hrc);
            ULONG Height;
            CHECK_ERROR_RET(screenSettings, COMGETTER(VideoHeight)(&Height), hrc);
            ULONG Rate;
            CHECK_ERROR_RET(screenSettings, COMGETTER(VideoRate)(&Rate), hrc);
            ULONG Fps;
            CHECK_ERROR_RET(screenSettings, COMGETTER(VideoFPS)(&Fps), hrc);
            RecordingDestination_T enmDst;
            CHECK_ERROR_RET(screenSettings, COMGETTER(Destination)(&enmDst), hrc);
            Bstr  bstrFile;
            CHECK_ERROR_RET(screenSettings, COMGETTER(Filename)(bstrFile.asOutParam()), hrc);
            Bstr  bstrOptions;
            CHECK_ERROR_RET(screenSettings, COMGETTER(Options)(bstrOptions.asOutParam()), hrc);

            BOOL fRecordVideo = FALSE;
# ifdef VBOX_WITH_AUDIO_RECORDING
            BOOL fRecordAudio = FALSE;
# endif
            for (size_t f = 0; f < vecFeatures.size(); ++f)
            {
                if (vecFeatures[f] == RecordingFeature_Video)
                    fRecordVideo = TRUE;
# ifdef VBOX_WITH_AUDIO_RECORDING
                else if (vecFeatures[f] == RecordingFeature_Audio)
                    fRecordAudio = TRUE;
# endif
            }

            SHOW_BOOL_VALUE_EX("rec_screen_enabled",         Info::tr("    Enabled:"), fEnabled,
                                                             Info::tr("yes"), Info::tr("no"));
            SHOW_ULONG_VALUE  ("rec_screen_id",              Info::tr("    ID:"), idScreen, "");
            SHOW_BOOL_VALUE_EX("rec_screen_video_enabled",   Info::tr("    Record video:"), fRecordVideo,
                                                             Info::tr("yes"), Info::tr("no"));
# ifdef VBOX_WITH_AUDIO_RECORDING
            SHOW_BOOL_VALUE_EX("rec_screen_audio_enabled",   Info::tr("    Record audio:"), fRecordAudio,
                                                             Info::tr("yes"), Info::tr("no"));
# endif
            SHOW_UTF8_STRING("rec_screen_dest",              Info::tr("    Destination:"),
                                                               enmDst == RecordingDestination_File
                                                             ? Info::tr("File") : Info::tr("Unknown"));
            /** @todo Implement other destinations. */
            if (enmDst == RecordingDestination_File)
                SHOW_BSTR_STRING("rec_screen_dest_filename", Info::tr("    File:"), bstrFile);

            SHOW_BSTR_STRING  ("rec_screen_opts",            Info::tr("    Options:"), bstrOptions);

            /* Video properties. */
            RTStrPrintf(szValue, sizeof(szValue), "%ux%u", Width, Height);
            SHOW_UTF8_STRING  ("rec_screen_video_res_xy",    Info::tr("    Video dimensions:"), szValue);
            SHOW_ULONG_VALUE  ("rec_screen_video_rate_kbps", Info::tr("    Video rate:"), Rate, Info::tr("kbps"));
            SHOW_ULONG_VALUE  ("rec_screen_video_fps",       Info::tr("    Video FPS:"), Fps, Info::tr("fps"));

            /** @todo Add more audio capturing profile / information here. */
        }
    }
#endif /* VBOX_WITH_RECORDING */

    if (    details == VMINFO_STANDARD
        ||  details == VMINFO_FULL
        ||  details == VMINFO_MACHINEREADABLE)
    {
        Bstr description;
        machine->COMGETTER(Description)(description.asOutParam());
        if (!description.isEmpty())
        {
            if (details == VMINFO_MACHINEREADABLE)
                outputMachineReadableString("description", &description);
            else
                RTPrintf(Info::tr("Description:\n%ls\n"), description.raw());
        }
    }

    /* VMMDev testing config (extra data) */
    if (details != VMINFO_MACHINEREADABLE)
    {
        Bstr bstr;
        CHECK_ERROR2I_RET(machine, GetExtraData(Bstr("VBoxInternal/Devices/VMMDev/0/Config/TestingEnabled").raw(),
                                                bstr.asOutParam()), hrcCheck);
        int const fEnabled = parseCfgmBool(&bstr);

        CHECK_ERROR2I_RET(machine, GetExtraData(Bstr("VBoxInternal/Devices/VMMDev/0/Config/TestingMMIO").raw(),
                                                bstr.asOutParam()), hrcCheck);
        int const fMmio = parseCfgmBool(&bstr);
        if (fEnabled || fMmio)
        {
            RTPrintf("%-28s %s, %s %s\n",
                     Info::tr("VMMDev Testing"),
                     fEnabled > 0 ? Info::tr("enabled") : fEnabled == 0 ? Info::tr("disabled") : Info::tr("misconfigured"),
                     "MMIO:",
                     fMmio    > 0 ? Info::tr("enabled") : fMmio    == 0 ? Info::tr("disabled") : Info::tr("misconfigured"));
            for (uint32_t i = 0; i < 10; i++)
            {
                BstrFmt bstrName("VBoxInternal/Devices/VMMDev/0/Config/TestingCfgDword%u", i);
                CHECK_ERROR2I_RET(machine, GetExtraData(bstrName.raw(), bstr.asOutParam()), hrcCheck);
                if (bstr.isNotEmpty())
                    RTPrintf("%-28s %ls\n", FmtNm(szNm, "VMMDev Testing Cfg Dword%u:", i), bstr.raw());
            }
        }
    }

    /*
     * Snapshots.
     */
    ComPtr<ISnapshot> snapshot;
    hrc = machine->FindSnapshot(Bstr().raw(), snapshot.asOutParam());
    if (SUCCEEDED(hrc) && snapshot)
    {
        ComPtr<ISnapshot> currentSnapshot;
        hrc = machine->COMGETTER(CurrentSnapshot)(currentSnapshot.asOutParam());
        if (SUCCEEDED(hrc))
        {
            if (details != VMINFO_MACHINEREADABLE)
                RTPrintf(Info::tr("* Snapshots:\n"));
            showSnapshots(snapshot, currentSnapshot, details);
        }
    }

    /*
     * Guest stuff (mainly interesting when running).
     */
    if (details != VMINFO_MACHINEREADABLE)
        RTPrintf(Info::tr("* Guest:\n"));

    SHOW_ULONG_PROP(machine, MemoryBalloonSize, "GuestMemoryBalloon",
                    Info::tr("Configured memory balloon:"), Info::tr("MB"));

    if (pConsole)
    {
        ComPtr<IGuest> guest;
        hrc = pConsole->COMGETTER(Guest)(guest.asOutParam());
        if (SUCCEEDED(hrc) && !guest.isNull())
        {
            SHOW_STRING_PROP_NOT_EMPTY(guest, OSTypeId, "GuestOSType", Info::tr("OS type:"));

            AdditionsRunLevelType_T guestRunLevel; /** @todo Add a runlevel-to-string (e.g. 0 = "None") method? */
            hrc = guest->COMGETTER(AdditionsRunLevel)(&guestRunLevel);
            if (SUCCEEDED(hrc))
                SHOW_ULONG_VALUE("GuestAdditionsRunLevel", Info::tr("Additions run level:"), (ULONG)guestRunLevel, "");

            Bstr guestString;
            hrc = guest->COMGETTER(AdditionsVersion)(guestString.asOutParam());
            if (   SUCCEEDED(hrc)
                && !guestString.isEmpty())
            {
                ULONG uRevision;
                hrc = guest->COMGETTER(AdditionsRevision)(&uRevision);
                if (FAILED(hrc))
                    uRevision = 0;
                RTStrPrintf(szValue, sizeof(szValue), "%ls r%u", guestString.raw(), uRevision);
                SHOW_UTF8_STRING("GuestAdditionsVersion", Info::tr("Additions version:"), szValue);
            }

            /* Print information about known Guest Additions facilities: */
            SafeIfaceArray <IAdditionsFacility> collFac;
            CHECK_ERROR_RET(guest, COMGETTER(Facilities)(ComSafeArrayAsOutParam(collFac)), hrc);
            if (collFac.size() > 0)
            {
                if (details != VMINFO_MACHINEREADABLE)
                    RTPrintf("%s\n", Info::tr("Guest Facilities:"));
                LONG64 lLastUpdatedMS;
                char szLastUpdated[32];
                AdditionsFacilityStatus_T curStatus;
                for (size_t index = 0; index < collFac.size(); ++index)
                {
                    ComPtr<IAdditionsFacility> fac = collFac[index];
                    if (fac)
                    {
                        CHECK_ERROR_RET(fac, COMGETTER(Name)(guestString.asOutParam()), hrc);
                        if (!guestString.isEmpty())
                        {
                            CHECK_ERROR_RET(fac, COMGETTER(Status)(&curStatus), hrc);
                            CHECK_ERROR_RET(fac, COMGETTER(LastUpdated)(&lLastUpdatedMS), hrc);
                            if (details == VMINFO_MACHINEREADABLE)
                                RTPrintf("GuestAdditionsFacility_%ls=%u,%lld\n",
                                         guestString.raw(), curStatus, lLastUpdatedMS);
                            else
                            {
                                makeTimeStr(szLastUpdated, sizeof(szLastUpdated), lLastUpdatedMS);
                                RTPrintf(Info::tr("Facility \"%ls\": %s (last update: %s)\n"),
                                         guestString.raw(), facilityStateToName(curStatus, false /* No short naming */), szLastUpdated);
                            }
                        }
                        else
                            AssertMsgFailed(("Facility with undefined name retrieved!\n"));
                    }
                    else
                        AssertMsgFailed(("Invalid facility returned!\n"));
                }
            }
            else if (details != VMINFO_MACHINEREADABLE)
                RTPrintf("%-28s %s\n", Info::tr("Guest Facilities:"), Info::tr("<none>"));
        }
    }

    if (details != VMINFO_MACHINEREADABLE)
        RTPrintf("\n");
    return S_OK;
}

#if defined(_MSC_VER)
# pragma optimize("", on)
# pragma warning(pop)
#endif

static const RTGETOPTDEF g_aShowVMInfoOptions[] =
{
    { "--details",          'D', RTGETOPT_REQ_NOTHING },
    { "-details",           'D', RTGETOPT_REQ_NOTHING },    // deprecated
    { "--machinereadable",  'M', RTGETOPT_REQ_NOTHING },
    { "-machinereadable",   'M', RTGETOPT_REQ_NOTHING },    // deprecated
    { "--log",              'l', RTGETOPT_REQ_UINT32 },
    { "--password-id",      'i', RTGETOPT_REQ_STRING },
    { "-password-id",       'i', RTGETOPT_REQ_STRING },
    { "--password",         'w', RTGETOPT_REQ_STRING },
    { "-password",          'w', RTGETOPT_REQ_STRING },
};

RTEXITCODE handleShowVMInfo(HandlerArg *a)
{
    HRESULT hrc;
    const char *VMNameOrUuid = NULL;
    bool fLog = false;
    uint32_t uLogIdx = 0;
    bool fDetails = false;
    bool fMachinereadable = false;
    Bstr bstrPasswordId;
    const char *pszPassword = NULL;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aShowVMInfoOptions, RT_ELEMENTS(g_aShowVMInfoOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'D':   // --details
                fDetails = true;
                break;

            case 'M':   // --machinereadable
                fMachinereadable = true;
                break;

            case 'l':   // --log
                fLog = true;
                uLogIdx = ValueUnion.u32;
                break;

            case 'i':   // --password-id
                bstrPasswordId = ValueUnion.psz;
                break;

            case 'w':   // --password
                pszPassword = ValueUnion.psz;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!VMNameOrUuid)
                    VMNameOrUuid = ValueUnion.psz;
                else
                    return errorSyntax(Info::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* check for required options */
    if (!VMNameOrUuid)
        return errorSyntax(Info::tr("VM name or UUID required"));

    /* try to find the given machine */
    ComPtr<IMachine> machine;
    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(VMNameOrUuid).raw(),
                                           machine.asOutParam()));
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    /* Printing the log is exclusive. */
    if (fLog && (fMachinereadable || fDetails))
        return errorSyntax(Info::tr("Option --log is exclusive"));

    /* add VM password if required */
    if (pszPassword && bstrPasswordId.isNotEmpty())
    {
        Utf8Str strPassword;
        if (!RTStrCmp(pszPassword, "-"))
        {
            /* Get password from console. */
            RTEXITCODE rcExit = readPasswordFromConsole(&strPassword, "Enter the password:");
            if (rcExit == RTEXITCODE_FAILURE)
                return rcExit;
        }
        else
        {
            RTEXITCODE rcExit = readPasswordFile(pszPassword, &strPassword);
            if (rcExit == RTEXITCODE_FAILURE)
            {
                RTMsgError("Failed to read new password from file");
                return rcExit;
            }
        }
        CHECK_ERROR(machine, AddEncryptionPassword(bstrPasswordId.raw(), Bstr(strPassword).raw()));
    }

    if (fLog)
    {
        ULONG64 uOffset = 0;
        SafeArray<BYTE> aLogData;
        size_t cbLogData;
        while (true)
        {
            /* Reset the array */
            aLogData.setNull();
            /* Fetch a chunk of the log file */
            CHECK_ERROR_BREAK(machine, ReadLog(uLogIdx, uOffset, _1M,
                                               ComSafeArrayAsOutParam(aLogData)));
            cbLogData = aLogData.size();
            if (cbLogData == 0)
                break;
            /* aLogData has a platform dependent line ending, standardize on
             * Unix style, as RTStrmWrite does the LF -> CR/LF replacement on
             * Windows. Otherwise we end up with CR/CR/LF on Windows. */
            size_t cbLogDataPrint = cbLogData;
            for (BYTE *s = aLogData.raw(), *d = s;
                 s - aLogData.raw() < (ssize_t)cbLogData;
                 s++, d++)
            {
                if (*s == '\r')
                {
                    /* skip over CR, adjust destination */
                    d--;
                    cbLogDataPrint--;
                }
                else if (s != d)
                    *d = *s;
            }
            RTStrmWrite(g_pStdOut, aLogData.raw(), cbLogDataPrint);
            uOffset += cbLogData;
        }
    }
    else
    {
        /* 2nd option can be -details or -argdump */
        VMINFO_DETAILS details = VMINFO_NONE;
        if (fMachinereadable)
            details = VMINFO_MACHINEREADABLE;
        else if (fDetails)
            details = VMINFO_FULL;
        else
            details = VMINFO_STANDARD;

        /* open an existing session for the VM */
        hrc = machine->LockMachine(a->session, LockType_Shared);
        if (SUCCEEDED(hrc))
            /* get the session machine */
            hrc = a->session->COMGETTER(Machine)(machine.asOutParam());

        hrc = showVMInfo(a->virtualBox, machine, a->session, details);

        a->session->UnlockMachine();
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
