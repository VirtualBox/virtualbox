/* $Id: VBoxManageDebugVM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - Implementation of the debugvm command.
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

#include <VBox/types.h>
#include <iprt/ctype.h>
#include <iprt/getopt.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <VBox/log.h>

#include "VBoxManage.h"

DECLARE_TRANSLATION_CONTEXT(DebugVM);


/**
 * Handles the getregisters sub-command.
 *
 * @returns Suitable exit code.
 * @param   pArgs               The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_GetRegisters(HandlerArg *pArgs, IMachineDebugger *pDebugger)
{
    /*
     * We take a list of register names (case insensitive).  If 'all' is
     * encountered we'll dump all registers.
     */
    ULONG                       idCpu = 0;
    unsigned                    cRegisters = 0;

    RTGETOPTSTATE               GetState;
    RTGETOPTUNION               ValueUnion;
    static const RTGETOPTDEF    s_aOptions[] =
    {
        { "--cpu", 'c', RTGETOPT_REQ_UINT32 },
    };
    int vrc = RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            case 'c':
                idCpu = ValueUnion.u32;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!RTStrICmp(ValueUnion.psz, "all"))
                {
                    com::SafeArray<BSTR> aBstrNames;
                    com::SafeArray<BSTR> aBstrValues;
                    CHECK_ERROR2I_RET(pDebugger, GetRegisters(idCpu, ComSafeArrayAsOutParam(aBstrNames),
                                                              ComSafeArrayAsOutParam(aBstrValues)),
                                      RTEXITCODE_FAILURE);
                    Assert(aBstrNames.size() == aBstrValues.size());

                    size_t cchMaxName  = 8;
                    for (size_t i = 0; i < aBstrNames.size(); i++)
                    {
                        size_t cchName = RTUtf16Len(aBstrNames[i]);
                        if (cchName > cchMaxName)
                            cchMaxName = cchName;
                    }

                    for (size_t i = 0; i < aBstrNames.size(); i++)
                        RTPrintf("%-*ls = %ls\n", cchMaxName, aBstrNames[i], aBstrValues[i]);
                }
                else
                {
                    com::Bstr bstrName = ValueUnion.psz;
                    com::Bstr bstrValue;
                    CHECK_ERROR2I_RET(pDebugger, GetRegister(idCpu, bstrName.raw(), bstrValue.asOutParam()), RTEXITCODE_FAILURE);
                    RTPrintf("%s = %ls\n", ValueUnion.psz, bstrValue.raw());
                }
                cRegisters++;
                break;

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    if (!cRegisters)
        return errorSyntax(DebugVM::tr("The getregisters sub-command takes at least one register name"));
    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the info sub-command.
 *
 * @returns Suitable exit code.
 * @param   pArgs               The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_Info(HandlerArg *pArgs, IMachineDebugger *pDebugger)
{
    /*
     * Parse arguments.
     */
    const char    *pszInfo = NULL;
    const char    *pszArgs = NULL;
    RTGETOPTSTATE  GetState;
    RTGETOPTUNION  ValueUnion;
    int vrc = RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, NULL, 0, 2, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            case VINF_GETOPT_NOT_OPTION:
                if (!pszInfo)
                    pszInfo = ValueUnion.psz;
                else if (!pszArgs)
                    pszArgs = ValueUnion.psz;
                else
                    return errorTooManyParameters(&pArgs->argv[GetState.iNext - 1]);
                break;
            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    if (!pszInfo)
        return errorSyntax(DebugVM::tr("Must specify info item to display"));

    /*
     * Do the work.
     */
    com::Bstr bstrName(pszInfo);
    com::Bstr bstrArgs(pszArgs);
    com::Bstr bstrInfo;
    CHECK_ERROR2I_RET(pDebugger, Info(bstrName.raw(), bstrArgs.raw(), bstrInfo.asOutParam()), RTEXITCODE_FAILURE);
    RTPrintf("%ls", bstrInfo.raw());
    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the inject sub-command.
 *
 * @returns Suitable exit code.
 * @param   a                   The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_InjectNMI(HandlerArg *a, IMachineDebugger *pDebugger)
{
    if (a->argc != 2)
        return errorTooManyParameters(&a->argv[1]);
    CHECK_ERROR2I_RET(pDebugger, InjectNMI(), RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the log sub-command.
 *
 * @returns Suitable exit code.
 * @param   pArgs               The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 * @param   pszSubCmd           The sub command.
 */
static RTEXITCODE handleDebugVM_LogXXXX(HandlerArg *pArgs, IMachineDebugger *pDebugger, const char *pszSubCmd)
{
    /*
     * Parse arguments.
     */
    bool                        fRelease = false;
    com::Utf8Str                strSettings;

    RTGETOPTSTATE               GetState;
    RTGETOPTUNION               ValueUnion;

    /*
     * NB: don't use short options to prevent log specifications like
     * "-drv_foo" from being interpreted as options.
     */
#   define DEBUGVM_LOG_DEBUG       (VINF_GETOPT_NOT_OPTION + 'd')
#   define DEBUGVM_LOG_RELEASE     (VINF_GETOPT_NOT_OPTION + 'r')

    static const RTGETOPTDEF    s_aOptions[] =
    {
        { "--debug",        DEBUGVM_LOG_DEBUG,   RTGETOPT_REQ_NOTHING },
        { "--release",      DEBUGVM_LOG_RELEASE, RTGETOPT_REQ_NOTHING }
    };
    /*
     * Note: RTGETOPTINIT_FLAGS_NO_STD_OPTS is needed to not get into an infinite hang in the following
     *       while-loop when processing log groups starting with "h",
     *       e.g. "VBoxManage debugvm <VM Name> log --debug -hex".
     */
    int vrc = RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST | RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            case DEBUGVM_LOG_RELEASE:
                fRelease = true;
                break;

            case DEBUGVM_LOG_DEBUG:
                fRelease = false;
                break;

            /* Because log strings can start with "-" (like "-all+dev_foo")
             * we have to take everything we got as a setting and apply it.
             * IPRT will take care of the validation afterwards. */
            default:
                if (strSettings.length() == 0)
                    strSettings = ValueUnion.psz;
                else
                {
                    strSettings.append(' ');
                    strSettings.append(ValueUnion.psz);
                }
                break;
        }
    }

    if (fRelease)
    {
        com::Utf8Str strTmp(strSettings);
        strSettings = "release:";
        strSettings.append(strTmp);
    }

    com::Bstr bstrSettings(strSettings);
    if (!strcmp(pszSubCmd, "log"))
        CHECK_ERROR2I_RET(pDebugger, ModifyLogGroups(bstrSettings.raw()), RTEXITCODE_FAILURE);
    else if (!strcmp(pszSubCmd, "logdest"))
        CHECK_ERROR2I_RET(pDebugger, ModifyLogDestinations(bstrSettings.raw()), RTEXITCODE_FAILURE);
    else if (!strcmp(pszSubCmd, "logflags"))
        CHECK_ERROR2I_RET(pDebugger, ModifyLogFlags(bstrSettings.raw()), RTEXITCODE_FAILURE);
    else
        AssertFailedReturn(RTEXITCODE_FAILURE);

    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the inject sub-command.
 *
 * @returns Suitable exit code.
 * @param   pArgs               The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_DumpVMCore(HandlerArg *pArgs, IMachineDebugger *pDebugger)
{
    /*
     * Parse arguments.
     */
    const char                 *pszFilename = NULL;
    const char                 *pszCompression = NULL;

    RTGETOPTSTATE               GetState;
    RTGETOPTUNION               ValueUnion;
    static const RTGETOPTDEF    s_aOptions[] =
    {
        { "--filename",     'f', RTGETOPT_REQ_STRING },
        { "--compression",  'c', RTGETOPT_REQ_STRING }
    };
    int vrc = RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, 0 /*fFlags*/);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            case 'c':
                if (pszCompression)
                    return errorSyntax(DebugVM::tr("The --compression option has already been given"));
                pszCompression = ValueUnion.psz;
                break;
            case 'f':
                if (pszFilename)
                    return errorSyntax(DebugVM::tr("The --filename option has already been given"));
                pszFilename = ValueUnion.psz;
                break;
            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    if (!pszFilename)
        return errorSyntax(DebugVM::tr("The --filename option is required"));

    /*
     * Make the filename absolute before handing it on to the API.
     */
    char szAbsFilename[RTPATH_MAX];
    vrc = RTPathAbs(pszFilename, szAbsFilename, sizeof(szAbsFilename));
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, DebugVM::tr("RTPathAbs failed on '%s': %Rrc"), pszFilename, vrc);

    com::Bstr bstrFilename(szAbsFilename);
    com::Bstr bstrCompression(pszCompression);
    CHECK_ERROR2I_RET(pDebugger, DumpGuestCore(bstrFilename.raw(), bstrCompression.raw()), RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the osdetect sub-command.
 *
 * @returns Suitable exit code.
 * @param   a                   The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_OSDetect(HandlerArg *a, IMachineDebugger *pDebugger)
{
    if (a->argc != 2)
        return errorTooManyParameters(&a->argv[1]);

    com::Bstr bstrIgnore;
    com::Bstr bstrAll("all");
    CHECK_ERROR2I_RET(pDebugger, LoadPlugIn(bstrAll.raw(), bstrIgnore.asOutParam()), RTEXITCODE_FAILURE);

    com::Bstr bstrName;
    CHECK_ERROR2I_RET(pDebugger, DetectOS(bstrName.asOutParam()), RTEXITCODE_FAILURE);
    RTPrintf(DebugVM::tr("Detected: %ls\n"), bstrName.raw());
    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the osinfo sub-command.
 *
 * @returns Suitable exit code.
 * @param   a                   The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_OSInfo(HandlerArg *a, IMachineDebugger *pDebugger)
{
    if (a->argc != 2)
        return errorTooManyParameters(&a->argv[1]);

    com::Bstr bstrName;
    CHECK_ERROR2I_RET(pDebugger, COMGETTER(OSName)(bstrName.asOutParam()), RTEXITCODE_FAILURE);
    com::Bstr bstrVersion;
    CHECK_ERROR2I_RET(pDebugger, COMGETTER(OSVersion)(bstrVersion.asOutParam()), RTEXITCODE_FAILURE);
    RTPrintf(DebugVM::tr("Name:    %ls\n"), bstrName.raw());
    RTPrintf(DebugVM::tr("Version: %ls\n"), bstrVersion.raw());
    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the osdmsg sub-command.
 *
 * @returns Suitable exit code.
 * @param   pArgs               The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_OSDmesg(HandlerArg *pArgs, IMachineDebugger *pDebugger)
{
    /*
     * Parse argument.
     */
    uint32_t                    uMaxMessages = 0;
    RTGETOPTSTATE               GetState;
    RTGETOPTUNION               ValueUnion;
    static const RTGETOPTDEF    s_aOptions[] =
    {
        { "--lines", 'n', RTGETOPT_REQ_UINT32 },
    };
    int vrc = RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
        switch (vrc)
        {
            case 'n': uMaxMessages = ValueUnion.u32; break;
            default: return errorGetOpt(vrc, &ValueUnion);
        }

    /*
     * Do it.
     */
    com::Bstr bstrDmesg;
    CHECK_ERROR2I_RET(pDebugger, QueryOSKernelLog(uMaxMessages, bstrDmesg.asOutParam()), RTEXITCODE_FAILURE);
    RTPrintf("%ls\n", bstrDmesg.raw());
    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the setregisters sub-command.
 *
 * @returns Suitable exit code.
 * @param   pArgs               The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_SetRegisters(HandlerArg *pArgs, IMachineDebugger *pDebugger)
{
    /*
     * We take a list of register assignments, that is register=value.
     */
    ULONG                       idCpu = 0;
    com::SafeArray<IN_BSTR>     aBstrNames;
    com::SafeArray<IN_BSTR>     aBstrValues;

    RTGETOPTSTATE               GetState;
    RTGETOPTUNION               ValueUnion;
    static const RTGETOPTDEF    s_aOptions[] =
    {
        { "--cpu", 'c', RTGETOPT_REQ_UINT32 },
    };
    int vrc = RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            case 'c':
                idCpu = ValueUnion.u32;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                const char *pszEqual = strchr(ValueUnion.psz, '=');
                if (!pszEqual)
                    return errorSyntax(DebugVM::tr("setregisters expects input on the form 'register=value' got '%s'"),
                                       ValueUnion.psz);
                try
                {
                    com::Bstr bstrName(ValueUnion.psz, pszEqual - ValueUnion.psz);
                    com::Bstr bstrValue(pszEqual + 1);
                    if (   !aBstrNames.push_back(bstrName.raw())
                        || !aBstrValues.push_back(bstrValue.raw()))
                        throw std::bad_alloc();
                }
                catch (std::bad_alloc &)
                {
                    RTMsgError(DebugVM::tr("Out of memory\n"));
                    return RTEXITCODE_FAILURE;
                }
                break;
            }

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    if (!aBstrNames.size())
        return errorSyntax(DebugVM::tr("The setregisters sub-command takes at least one register name"));

    /*
     * If it is only one register, use the single register method just so
     * we expose it and can test it from the command line.
     */
    if (aBstrNames.size() == 1)
    {
        CHECK_ERROR2I_RET(pDebugger, SetRegister(idCpu, aBstrNames[0], aBstrValues[0]), RTEXITCODE_FAILURE);
        RTPrintf(DebugVM::tr("Successfully set %ls\n"), aBstrNames[0]);
    }
    else
    {
        CHECK_ERROR2I_RET(pDebugger, SetRegisters(idCpu, ComSafeArrayAsInParam(aBstrNames), ComSafeArrayAsInParam(aBstrValues)),
                          RTEXITCODE_FAILURE);
        RTPrintf(DebugVM::tr("Successfully set %u registers\n", "", aBstrNames.size()), aBstrNames.size());
    }

    return RTEXITCODE_SUCCESS;
}

/** @name debugvm show flags
 * @{ */
#define DEBUGVM_SHOW_FLAGS_HUMAN_READABLE   UINT32_C(0x00000000)
#define DEBUGVM_SHOW_FLAGS_SH_EXPORT        UINT32_C(0x00000001)
#define DEBUGVM_SHOW_FLAGS_SH_EVAL          UINT32_C(0x00000002)
#define DEBUGVM_SHOW_FLAGS_CMD_SET          UINT32_C(0x00000003)
#define DEBUGVM_SHOW_FLAGS_FMT_MASK         UINT32_C(0x00000003)
/** @} */

/**
 * Prints a variable according to the @a fFlags.
 *
 * @param   pszVar              The variable name.
 * @param   pbstrValue          The variable value.
 * @param   fFlags              The debugvm show flags.
 */
static void handleDebugVM_Show_PrintVar(const char *pszVar, com::Bstr const *pbstrValue, uint32_t fFlags)
{
    switch (fFlags & DEBUGVM_SHOW_FLAGS_FMT_MASK)
    {
        case DEBUGVM_SHOW_FLAGS_HUMAN_READABLE: RTPrintf(" %27s=%ls\n", pszVar, pbstrValue->raw()); break;
        case DEBUGVM_SHOW_FLAGS_SH_EXPORT:      RTPrintf(DebugVM::tr("export %s='%ls'\n"), pszVar, pbstrValue->raw()); break;
        case DEBUGVM_SHOW_FLAGS_SH_EVAL:        RTPrintf("%s='%ls'\n", pszVar, pbstrValue->raw()); break;
        case DEBUGVM_SHOW_FLAGS_CMD_SET:        RTPrintf(DebugVM::tr("set %s=%ls\n"), pszVar, pbstrValue->raw()); break;
        default: AssertFailed();
    }
}

/**
 * Handles logdbg-settings.
 *
 * @returns Exit code.
 * @param   pDebugger           The debugger interface.
 * @param   fFlags              The debugvm show flags.
 */
static RTEXITCODE handleDebugVM_Show_LogDbgSettings(IMachineDebugger *pDebugger, uint32_t fFlags)
{
    if ((fFlags & DEBUGVM_SHOW_FLAGS_FMT_MASK) == DEBUGVM_SHOW_FLAGS_HUMAN_READABLE)
        RTPrintf(DebugVM::tr("Debug logger settings:\n"));

    com::Bstr bstr;
    CHECK_ERROR2I_RET(pDebugger, COMGETTER(LogDbgGroups)(bstr.asOutParam()), RTEXITCODE_FAILURE);
    handleDebugVM_Show_PrintVar("VBOX_LOG", &bstr, fFlags);

    CHECK_ERROR2I_RET(pDebugger, COMGETTER(LogDbgFlags)(bstr.asOutParam()), RTEXITCODE_FAILURE);
    handleDebugVM_Show_PrintVar("VBOX_LOG_FLAGS", &bstr, fFlags);

    CHECK_ERROR2I_RET(pDebugger, COMGETTER(LogDbgDestinations)(bstr.asOutParam()), RTEXITCODE_FAILURE);
    handleDebugVM_Show_PrintVar("VBOX_LOG_DEST", &bstr, fFlags);
    return RTEXITCODE_SUCCESS;
}

/**
 * Handles logrel-settings.
 *
 * @returns Exit code.
 * @param   pDebugger           The debugger interface.
 * @param   fFlags              The debugvm show flags.
 */
static RTEXITCODE handleDebugVM_Show_LogRelSettings(IMachineDebugger *pDebugger, uint32_t fFlags)
{
    if ((fFlags & DEBUGVM_SHOW_FLAGS_FMT_MASK) == DEBUGVM_SHOW_FLAGS_HUMAN_READABLE)
        RTPrintf(DebugVM::tr("Release logger settings:\n"));

    com::Bstr bstr;
    CHECK_ERROR2I_RET(pDebugger, COMGETTER(LogRelGroups)(bstr.asOutParam()), RTEXITCODE_FAILURE);
    handleDebugVM_Show_PrintVar("VBOX_RELEASE_LOG", &bstr, fFlags);

    CHECK_ERROR2I_RET(pDebugger, COMGETTER(LogRelFlags)(bstr.asOutParam()), RTEXITCODE_FAILURE);
    handleDebugVM_Show_PrintVar("VBOX_RELEASE_LOG_FLAGS", &bstr, fFlags);

    CHECK_ERROR2I_RET(pDebugger, COMGETTER(LogRelDestinations)(bstr.asOutParam()), RTEXITCODE_FAILURE);
    handleDebugVM_Show_PrintVar("VBOX_RELEASE_LOG_DEST", &bstr, fFlags);
    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the show sub-command.
 *
 * @returns Suitable exit code.
 * @param   pArgs               The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_Show(HandlerArg *pArgs, IMachineDebugger *pDebugger)
{
    /*
     * Parse arguments and what to show.  Order dependent.
     */
    uint32_t                    fFlags = DEBUGVM_SHOW_FLAGS_HUMAN_READABLE;

    RTGETOPTSTATE               GetState;
    RTGETOPTUNION               ValueUnion;
    static const RTGETOPTDEF    s_aOptions[] =
    {
        { "--human-readable", 'H', RTGETOPT_REQ_NOTHING },
        { "--sh-export",      'e', RTGETOPT_REQ_NOTHING },
        { "--sh-eval",        'E', RTGETOPT_REQ_NOTHING },
        { "--cmd-set",        's', RTGETOPT_REQ_NOTHING  },
    };
    int vrc = RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, 0 /*fFlags*/);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            case 'H':
                fFlags = (fFlags & ~DEBUGVM_SHOW_FLAGS_FMT_MASK) | DEBUGVM_SHOW_FLAGS_HUMAN_READABLE;
                break;

            case 'e':
                fFlags = (fFlags & ~DEBUGVM_SHOW_FLAGS_FMT_MASK) | DEBUGVM_SHOW_FLAGS_SH_EXPORT;
                break;

            case 'E':
                fFlags = (fFlags & ~DEBUGVM_SHOW_FLAGS_FMT_MASK) | DEBUGVM_SHOW_FLAGS_SH_EVAL;
                break;

            case 's':
                fFlags = (fFlags & ~DEBUGVM_SHOW_FLAGS_FMT_MASK) | DEBUGVM_SHOW_FLAGS_CMD_SET;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                RTEXITCODE rcExit;
                if (!strcmp(ValueUnion.psz, "log-settings"))
                {
                    rcExit = handleDebugVM_Show_LogDbgSettings(pDebugger, fFlags);
                    if (rcExit == RTEXITCODE_SUCCESS)
                        rcExit = handleDebugVM_Show_LogRelSettings(pDebugger, fFlags);
                }
                else if (!strcmp(ValueUnion.psz, "logdbg-settings"))
                    rcExit = handleDebugVM_Show_LogDbgSettings(pDebugger, fFlags);
                else if (!strcmp(ValueUnion.psz, "logrel-settings"))
                    rcExit = handleDebugVM_Show_LogRelSettings(pDebugger, fFlags);
                else
                    rcExit = errorSyntax(DebugVM::tr("The show sub-command has no idea what '%s' might be"), ValueUnion.psz);
                if (rcExit != RTEXITCODE_SUCCESS)
                    return rcExit;
                break;
            }

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }
    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the stack sub-command.
 *
 * @returns Suitable exit code.
 * @param   pArgs               The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_Stack(HandlerArg *pArgs, IMachineDebugger *pDebugger)
{
    /*
     * Parse arguments.
     */
    VMCPUID                     idCpu = VMCPUID_ALL;

    RTGETOPTSTATE               GetState;
    RTGETOPTUNION               ValueUnion;
    static const RTGETOPTDEF    s_aOptions[] =
    {
        { "--cpu", 'c', RTGETOPT_REQ_UINT32 },
    };
    int vrc = RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            case 'c':
                idCpu = ValueUnion.u32;
                break;

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    /*
     * Dump stack.
     */
    com::Bstr bstrGuestStack;
    if (idCpu != VMCPUID_ALL)
    {
        /* Single CPU */
        CHECK_ERROR2I_RET(pDebugger, DumpGuestStack(idCpu, bstrGuestStack.asOutParam()), RTEXITCODE_FAILURE);
        RTPrintf("%ls\n", bstrGuestStack.raw());
    }
    else
    {
        /* All CPUs. */
        ComPtr<IMachine> ptrMachine;
        CHECK_ERROR2I_RET(pArgs->session, COMGETTER(Machine)(ptrMachine.asOutParam()), RTEXITCODE_FAILURE);
        ULONG cCpus;
        CHECK_ERROR2I_RET(ptrMachine, COMGETTER(CPUCount)(&cCpus), RTEXITCODE_FAILURE);

        for (idCpu = 0; idCpu < (VMCPUID)cCpus; idCpu++)
        {
            CHECK_ERROR2I_RET(pDebugger, DumpGuestStack(idCpu, bstrGuestStack.asOutParam()), RTEXITCODE_FAILURE);
            if (cCpus > 1)
            {
                if (idCpu > 0)
                    RTPrintf("\n");
                RTPrintf(DebugVM::tr("====================== CPU #%u ======================\n"), idCpu);
            }
            RTPrintf("%ls\n", bstrGuestStack.raw());
        }
    }


    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the statistics sub-command.
 *
 * @returns Suitable exit code.
 * @param   pArgs               The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_Statistics(HandlerArg *pArgs, IMachineDebugger *pDebugger)
{
    /*
     * Parse arguments.
     */
    bool                        fWithDescriptions   = false;
    const char                 *pszPattern          = NULL; /* all */
    bool                        fReset              = false;

    RTGETOPTSTATE               GetState;
    RTGETOPTUNION               ValueUnion;
    static const RTGETOPTDEF    s_aOptions[] =
    {
        { "--descriptions", 'd', RTGETOPT_REQ_NOTHING },
        { "--pattern",      'p', RTGETOPT_REQ_STRING  },
        { "--reset",        'r', RTGETOPT_REQ_NOTHING  },
    };
    int vrc = RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, 0 /*fFlags*/);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            case 'd':
                fWithDescriptions = true;
                break;

            case 'p':
                if (pszPattern)
                    return errorSyntax(DebugVM::tr("Multiple --pattern options are not permitted"));
                pszPattern = ValueUnion.psz;
                break;

            case 'r':
                fReset = true;
                break;

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    if (fReset && fWithDescriptions)
        return errorSyntax(DebugVM::tr("The --reset and --descriptions options does not mix"));

    /*
     * Execute the order.
     */
    com::Bstr bstrPattern(pszPattern);
    if (fReset)
        CHECK_ERROR2I_RET(pDebugger, ResetStats(bstrPattern.raw()), RTEXITCODE_FAILURE);
    else
    {
        com::Bstr bstrStats;
        CHECK_ERROR2I_RET(pDebugger, GetStats(bstrPattern.raw(), fWithDescriptions, bstrStats.asOutParam()),
                          RTEXITCODE_FAILURE);
        /* if (fFormatted)
         { big mess }
         else
         */
        RTPrintf("%ls\n", bstrStats.raw());
    }

    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the guestsample sub-command.
 *
 * @returns Suitable exit code.
 * @param   pArgs               The handler arguments.
 * @param   pDebugger           Pointer to the debugger interface.
 */
static RTEXITCODE handleDebugVM_GuestSample(HandlerArg *pArgs, IMachineDebugger *pDebugger)
{
    /*
     * Parse arguments.
     */
    const char                 *pszFilename         = NULL;
    uint32_t                   cSampleIntervalUs    = 1000;
    uint64_t                   cSampleTimeUs        = 1000*1000;

    RTGETOPTSTATE               GetState;
    RTGETOPTUNION               ValueUnion;
    static const RTGETOPTDEF    s_aOptions[] =
    {
        { "--filename",           'f', RTGETOPT_REQ_STRING },
        { "--sample-interval-us", 'i', RTGETOPT_REQ_UINT32 },
        { "--sample-time-us",     't', RTGETOPT_REQ_UINT64 },
    };
    int vrc = RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, 0 /*fFlags*/);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            case 'f':
                pszFilename = ValueUnion.psz;
                break;
            case 'i':
                cSampleIntervalUs = ValueUnion.u32;
                break;
            case 't':
                cSampleTimeUs = ValueUnion.u64;
                break;

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    if (!pszFilename)
        return errorSyntax(DebugVM::tr("The --filename is missing"));

    /*
     * Execute the order.
     */
    ComPtr<IProgress> ptrProgress;
    com::Bstr bstrFilename(pszFilename);
    CHECK_ERROR2I_RET(pDebugger, TakeGuestSample(bstrFilename.raw(), cSampleIntervalUs, cSampleTimeUs, ptrProgress.asOutParam()), RTEXITCODE_FAILURE);
    showProgress(ptrProgress);

    return RTEXITCODE_SUCCESS;
}

RTEXITCODE handleDebugVM(HandlerArg *pArgs)
{
    RTEXITCODE rcExit = RTEXITCODE_FAILURE;

    /*
     * The first argument is the VM name or UUID.  Open a session to it.
     */
    if (pArgs->argc < 2)
        return errorNoSubcommand();
    ComPtr<IMachine> ptrMachine;
    CHECK_ERROR2I_RET(pArgs->virtualBox, FindMachine(com::Bstr(pArgs->argv[0]).raw(), ptrMachine.asOutParam()), RTEXITCODE_FAILURE);
    CHECK_ERROR2I_RET(ptrMachine, LockMachine(pArgs->session, LockType_Shared), RTEXITCODE_FAILURE);

    /*
     * Get the associated console and machine debugger.
     */
    HRESULT hrc;
    ComPtr<IConsole> ptrConsole;
    CHECK_ERROR2(hrc, pArgs->session, COMGETTER(Console)(ptrConsole.asOutParam()));
    if (SUCCEEDED(hrc))
    {
        if (ptrConsole.isNotNull())
        {
            ComPtr<IMachineDebugger> ptrDebugger;
            CHECK_ERROR2(hrc, ptrConsole, COMGETTER(Debugger)(ptrDebugger.asOutParam()));
            if (SUCCEEDED(hrc))
            {
                /*
                 * String switch on the sub-command.
                 */
                const char *pszSubCmd = pArgs->argv[1];
                if (!strcmp(pszSubCmd, "dumpvmcore"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_DUMPVMCORE);
                    rcExit = handleDebugVM_DumpVMCore(pArgs, ptrDebugger);
                }
                else if (!strcmp(pszSubCmd, "getregisters"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_GETREGISTERS);
                    rcExit = handleDebugVM_GetRegisters(pArgs, ptrDebugger);
                }
                else if (!strcmp(pszSubCmd, "info"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_INFO);
                    rcExit = handleDebugVM_Info(pArgs, ptrDebugger);
                }
                else if (!strcmp(pszSubCmd, "injectnmi"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_INJECTNMI);
                    rcExit = handleDebugVM_InjectNMI(pArgs, ptrDebugger);
                }
                else if (!strcmp(pszSubCmd, "log"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_LOG);
                    rcExit = handleDebugVM_LogXXXX(pArgs, ptrDebugger, pszSubCmd);
                }
                else if (!strcmp(pszSubCmd, "logdest"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_LOGDEST);
                    rcExit = handleDebugVM_LogXXXX(pArgs, ptrDebugger, pszSubCmd);
                }
                else if (!strcmp(pszSubCmd, "logflags"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_LOGFLAGS);
                    rcExit = handleDebugVM_LogXXXX(pArgs, ptrDebugger, pszSubCmd);
                }
                else if (!strcmp(pszSubCmd, "osdetect"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_OSDETECT);
                    rcExit = handleDebugVM_OSDetect(pArgs, ptrDebugger);
                }
                else if (!strcmp(pszSubCmd, "osinfo"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_OSINFO);
                    rcExit = handleDebugVM_OSInfo(pArgs, ptrDebugger);
                }
                else if (!strcmp(pszSubCmd, "osdmesg"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_OSDMESG);
                    rcExit = handleDebugVM_OSDmesg(pArgs, ptrDebugger);
                }
                else if (!strcmp(pszSubCmd, "setregisters"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_SETREGISTERS);
                    rcExit = handleDebugVM_SetRegisters(pArgs, ptrDebugger);
                }
                else if (!strcmp(pszSubCmd, "show"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_SHOW);
                    rcExit = handleDebugVM_Show(pArgs, ptrDebugger);
                }
                else if (!strcmp(pszSubCmd, "stack"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_STACK);
                    rcExit = handleDebugVM_Stack(pArgs, ptrDebugger);
                }
                else if (!strcmp(pszSubCmd, "statistics"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_STATISTICS);
                    rcExit = handleDebugVM_Statistics(pArgs, ptrDebugger);
                }
                else if (!strcmp(pszSubCmd, "guestsample"))
                {
                    setCurrentSubcommand(HELP_SCOPE_DEBUGVM_GUESTSAMPLE);
                    rcExit = handleDebugVM_GuestSample(pArgs, ptrDebugger);
                }
                else
                    errorUnknownSubcommand(pszSubCmd);
            }
        }
        else
            RTMsgError(DebugVM::tr("Machine '%s' is not currently running.\n"), pArgs->argv[0]);
    }

    pArgs->session->UnlockMachine();

    return rcExit;
}
