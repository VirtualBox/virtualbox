/* $Id: VBoxManageMisc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - VirtualBox's command-line interface.
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
#include <VBox/com/NativeEventQueue.h>

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/cidr.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/sha.h>
#include <iprt/initterm.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/cpp/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>
#include <iprt/getopt.h>
#include <iprt/ctype.h>
#include <VBox/version.h>
#include <VBox/log.h>

#include "VBoxManage.h"

#include <list>

using namespace com;

DECLARE_TRANSLATION_CONTEXT(Misc);

static const RTGETOPTDEF g_aRegisterVMOptions[] =
{
    { "--password",       'p', RTGETOPT_REQ_STRING },
};

RTEXITCODE handleRegisterVM(HandlerArg *a)
{
    HRESULT hrc;

    const char *pszVmFile = NULL;
    const char *pszPasswordFile = NULL;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aRegisterVMOptions, RT_ELEMENTS(g_aRegisterVMOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'p':   // --password
                pszPasswordFile = ValueUnion.psz;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!pszVmFile)
                    pszVmFile = ValueUnion.psz;
                else
                    return errorSyntax(Misc::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_PRINT(c))
                        return errorSyntax(Misc::tr("Invalid option -%c"), c);
                    return errorSyntax(Misc::tr("Invalid option case %i"), c);
                }
                if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Misc::tr("unknown option: %s\n"), ValueUnion.psz);
                if (ValueUnion.pDef)
                    return errorSyntax("%s: %Rrs", ValueUnion.pDef->pszLong, c);
                return errorSyntax(Misc::tr("error: %Rrs"), c);
        }
    }

    Utf8Str strPassword;

    if (pszPasswordFile)
    {
        if (pszPasswordFile[0] == '-' && pszPasswordFile[1] == '\0')
        {
            /* Get password from console. */
            RTEXITCODE rcExit = readPasswordFromConsole(&strPassword, Misc::tr("Enter password:"));
            if (rcExit == RTEXITCODE_FAILURE)
                return rcExit;
        }
        else
        {
            RTEXITCODE rcExit = readPasswordFile(pszPasswordFile, &strPassword);
            if (rcExit == RTEXITCODE_FAILURE)
                return RTMsgErrorExitFailure(Misc::tr("Failed to read password from file"));
        }
    }

    ComPtr<IMachine> machine;
    /** @todo Ugly hack to get both the API interpretation of relative paths
     * and the client's interpretation of relative paths. Remove after the API
     * has been redesigned. */
    hrc = a->virtualBox->OpenMachine(Bstr(pszVmFile).raw(),
                                     Bstr(strPassword).raw(),
                                     machine.asOutParam());
    if (FAILED(hrc) && !RTPathStartsWithRoot(pszVmFile))
    {
        char szVMFileAbs[RTPATH_MAX] = "";
        int vrc = RTPathAbs(pszVmFile, szVMFileAbs, sizeof(szVMFileAbs));
        if (RT_FAILURE(vrc))
            return RTMsgErrorExitFailure(Misc::tr("Failed to convert \"%s\" to an absolute path: %Rrc"),
                                         pszVmFile, vrc);
        CHECK_ERROR(a->virtualBox, OpenMachine(Bstr(szVMFileAbs).raw(),
                                               Bstr(strPassword).raw(),
                                               machine.asOutParam()));
    }
    else if (FAILED(hrc))
        com::GlueHandleComError(a->virtualBox,
                                "OpenMachine(Bstr(a->argv[0]).raw(), Bstr(strPassword).raw(), machine.asOutParam()))",
                                hrc, __FILE__, __LINE__);
    if (SUCCEEDED(hrc))
    {
        ASSERT(machine);
        CHECK_ERROR(a->virtualBox, RegisterMachine(machine));
    }
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static const RTGETOPTDEF g_aUnregisterVMOptions[] =
{
    { "--delete",       'd', RTGETOPT_REQ_NOTHING },
    { "-delete",        'd', RTGETOPT_REQ_NOTHING },    // deprecated
    { "--delete-all",   'a', RTGETOPT_REQ_NOTHING },
    { "-delete-all",    'a', RTGETOPT_REQ_NOTHING },    // deprecated
};

RTEXITCODE handleUnregisterVM(HandlerArg *a)
{
    HRESULT hrc;
    const char *VMName = NULL;
    bool fDelete = false;
    bool fDeleteAll = false;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aUnregisterVMOptions, RT_ELEMENTS(g_aUnregisterVMOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'd':   // --delete
                fDelete = true;
                break;

            case 'a':   // --delete-all
                fDeleteAll = true;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!VMName)
                    VMName = ValueUnion.psz;
                else
                    return errorSyntax(Misc::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_PRINT(c))
                        return errorSyntax(Misc::tr("Invalid option -%c"), c);
                    return errorSyntax(Misc::tr("Invalid option case %i"), c);
                }
                if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Misc::tr("unknown option: %s\n"), ValueUnion.psz);
                if (ValueUnion.pDef)
                    return errorSyntax("%s: %Rrs", ValueUnion.pDef->pszLong, c);
                return errorSyntax(Misc::tr("error: %Rrs"), c);
        }
    }

    /* check for required options */
    if (!VMName)
        return errorSyntax(Misc::tr("VM name required"));

    ComPtr<IMachine> machine;
    CHECK_ERROR_RET(a->virtualBox, FindMachine(Bstr(VMName).raw(),
                                               machine.asOutParam()),
                    RTEXITCODE_FAILURE);
    SafeIfaceArray<IMedium> aMedia;
    CHECK_ERROR_RET(machine, Unregister(fDeleteAll ? CleanupMode_DetachAllReturnHardDisksAndVMRemovable
                                                   :CleanupMode_DetachAllReturnHardDisksOnly,
                                        ComSafeArrayAsOutParam(aMedia)),
                    RTEXITCODE_FAILURE);
    if (fDelete || fDeleteAll)
    {
        ComPtr<IProgress> pProgress;
        CHECK_ERROR_RET(machine, DeleteConfig(ComSafeArrayAsInParam(aMedia), pProgress.asOutParam()),
                        RTEXITCODE_FAILURE);

        hrc = showProgress(pProgress);
        CHECK_PROGRESS_ERROR_RET(pProgress, (Misc::tr("Machine delete failed")), RTEXITCODE_FAILURE);
    }
    else
    {
        /* Note that the IMachine::Unregister method will return the medium
         * reference in a sane order, which means that closing will normally
         * succeed, unless there is still another machine which uses the
         * medium. No harm done if we ignore the error. */
        for (size_t i = 0; i < aMedia.size(); i++)
        {
            IMedium *pMedium = aMedia[i];
            if (pMedium)
                hrc = pMedium->Close();
        }
        hrc = S_OK; /** @todo r=andy Why overwriting the result from closing the medium above? */
    }
    return RTEXITCODE_SUCCESS;
}

static const RTGETOPTDEF g_aCreateVMOptions[] =
{
    { "--name",                  'n', RTGETOPT_REQ_STRING },
    { "-name",                   'n', RTGETOPT_REQ_STRING },
    { "--groups",                'g', RTGETOPT_REQ_STRING },
    { "--basefolder",            'p', RTGETOPT_REQ_STRING },
    { "-basefolder",             'p', RTGETOPT_REQ_STRING },
    { "--ostype",                'o', RTGETOPT_REQ_STRING },
    { "-ostype",                 'o', RTGETOPT_REQ_STRING },
    { "--uuid",                  'u', RTGETOPT_REQ_UUID },
    { "-uuid",                   'u', RTGETOPT_REQ_UUID },
    { "--register",              'r', RTGETOPT_REQ_NOTHING },
    { "-register",               'r', RTGETOPT_REQ_NOTHING },
    { "--default",               'd', RTGETOPT_REQ_NOTHING },
    { "-default",                'd', RTGETOPT_REQ_NOTHING },
    { "--cipher",                'c', RTGETOPT_REQ_STRING },
    { "-cipher",                 'c', RTGETOPT_REQ_STRING },
    { "--password-id",           'i', RTGETOPT_REQ_STRING },
    { "-password-id",            'i', RTGETOPT_REQ_STRING },
    { "--password",              'w', RTGETOPT_REQ_STRING },
    { "-password",               'w', RTGETOPT_REQ_STRING },
    { "--platform-architecture", 'a', RTGETOPT_REQ_STRING },
    { "--platform-arch",         'a', RTGETOPT_REQ_STRING }, /* Shorter. */
};

RTEXITCODE handleCreateVM(HandlerArg *a)
{
    HRESULT hrc;
    PlatformArchitecture_T platformArch = PlatformArchitecture_None;
    Bstr bstrBaseFolder;
    Bstr bstrName;
    Bstr bstrOsTypeId;
    Bstr bstrUuid;
    bool fRegister = false;
    bool fDefault = false;
    /* TBD. Now not used */
    Bstr bstrDefaultFlags;
    com::SafeArray<BSTR> groups;
    Bstr bstrCipher;
    Bstr bstrPasswordId;
    const char *pszPassword = NULL;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aCreateVMOptions, RT_ELEMENTS(g_aCreateVMOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'n':   // --name
                bstrName = ValueUnion.psz;
                break;

            case 'g':   // --groups
                parseGroups(ValueUnion.psz, &groups);
                break;

            case 'p':   // --basefolder
                bstrBaseFolder = ValueUnion.psz;
                break;

            case 'o':   // --ostype
                bstrOsTypeId = ValueUnion.psz;
                break;

            case 'u':   // --uuid
                bstrUuid = Guid(ValueUnion.Uuid).toUtf16().raw();
                break;

            case 'r':   // --register
                fRegister = true;
                break;

            case 'd':   // --default
                fDefault = true;
                break;

            case 'c':   // --cipher
                bstrCipher = ValueUnion.psz;
                break;

            case 'i':   // --password-id
                bstrPasswordId = ValueUnion.psz;
                break;

            case 'w':   // --password
                pszPassword = ValueUnion.psz;
                break;

            case 'a':   // --platform-architecture
                if (!RTStrICmp(ValueUnion.psz, "x86"))
                    platformArch = PlatformArchitecture_x86;
                else if (!RTStrICmp(ValueUnion.psz, "arm"))
                    platformArch = PlatformArchitecture_ARM;
                else
                    return errorArgument(Misc::tr("Invalid --platform-architecture argument '%s'"), ValueUnion.psz);
                break;

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* check for required options */
    if (bstrName.isEmpty())
        return errorSyntax(Misc::tr("Parameter --name is required"));

    /* If the platform architecture is not specified explicitly ... */
    if (platformArch == PlatformArchitecture_None)
    {
        /* ... determine it from the guest OS type, if given. */
        if (bstrOsTypeId.isNotEmpty())
        {
            ComPtr<IGuestOSType> ptrGuestOsType;
            a->virtualBox->GetGuestOSType(bstrOsTypeId.raw(), ptrGuestOsType.asOutParam());
            if (ptrGuestOsType.isNull())
                return RTMsgErrorExit(RTEXITCODE_FAILURE, Misc::tr("Unknown or invalid guest OS type given."));
            CHECK_ERROR2I_RET(ptrGuestOsType, COMGETTER(PlatformArchitecture)(&platformArch), RTEXITCODE_FAILURE);
        }
        else /* use the host's platform type as the VM platform type. */
        {
            ComPtr<IHost> host;
            CHECK_ERROR2I_RET(a->virtualBox, COMGETTER(Host)(host.asOutParam()), RTEXITCODE_FAILURE);
            CHECK_ERROR2I_RET(host, COMGETTER(Architecture)(&platformArch), RTEXITCODE_FAILURE);
        }
    }

    do
    {
        Bstr createFlags;
        if (!bstrUuid.isEmpty())
            createFlags = BstrFmt("UUID=%ls", bstrUuid.raw());
        Bstr bstrPrimaryGroup;
        if (groups.size())
            bstrPrimaryGroup = groups[0];
        Bstr bstrSettingsFile;
        CHECK_ERROR_BREAK(a->virtualBox,
                          ComposeMachineFilename(bstrName.raw(),
                                                 bstrPrimaryGroup.raw(),
                                                 createFlags.raw(),
                                                 bstrBaseFolder.raw(),
                                                 bstrSettingsFile.asOutParam()));
        Utf8Str strPassword;
        if (pszPassword)
        {
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
        }
        ComPtr<IMachine> machine;
        CHECK_ERROR_BREAK(a->virtualBox,
                          CreateMachine(bstrSettingsFile.raw(),
                                        bstrName.raw(),
                                        platformArch,
                                        ComSafeArrayAsInParam(groups),
                                        bstrOsTypeId.raw(),
                                        createFlags.raw(),
                                        bstrCipher.raw(),
                                        bstrPasswordId.raw(),
                                        Bstr(strPassword).raw(),
                                        machine.asOutParam()));

        CHECK_ERROR_BREAK(machine, SaveSettings());
        if (fDefault)
        {
            /* ApplyDefaults assumes the machine is already registered */
            CHECK_ERROR_BREAK(machine, ApplyDefaults(bstrDefaultFlags.raw()));
            CHECK_ERROR_BREAK(machine, SaveSettings());
        }
        if (fRegister)
        {
            CHECK_ERROR_BREAK(a->virtualBox, RegisterMachine(machine));
        }

        Bstr uuid;
        CHECK_ERROR_BREAK(machine, COMGETTER(Id)(uuid.asOutParam()));
        Bstr settingsFile;
        CHECK_ERROR_BREAK(machine, COMGETTER(SettingsFilePath)(settingsFile.asOutParam()));
        RTPrintf(Misc::tr("Virtual machine '%ls' is created%s.\n"
                          "UUID: %s\n"
                          "Settings file: '%ls'\n"),
                 bstrName.raw(), fRegister ? Misc::tr(" and registered") : "",
                 Utf8Str(uuid).c_str(), settingsFile.raw());
    }
    while (0);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static const RTGETOPTDEF g_aMoveVMOptions[] =
{
    { "--type",           't', RTGETOPT_REQ_STRING },
    { "--folder",         'f', RTGETOPT_REQ_STRING },
};

RTEXITCODE handleMoveVM(HandlerArg *a)
{
    HRESULT                        hrc;
    const char                    *pszSrcName      = NULL;
    const char                    *pszType         = NULL;
    char                          szTargetFolder[RTPATH_MAX];

    int c;
    int vrc = VINF_SUCCESS;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;

    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aMoveVMOptions, RT_ELEMENTS(g_aMoveVMOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 't':   // --type
                pszType = ValueUnion.psz;
                break;

            case 'f':   // --target folder
                if (ValueUnion.psz && ValueUnion.psz[0] != '\0')
                {
                    vrc = RTPathAbs(ValueUnion.psz, szTargetFolder, sizeof(szTargetFolder));
                    if (RT_FAILURE(vrc))
                        return RTMsgErrorExit(RTEXITCODE_FAILURE, Misc::tr("RTPathAbs(%s,,) failed with vrc=%Rrc"),
                                              ValueUnion.psz, vrc);
                } else {
                    szTargetFolder[0] = '\0';
                }
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!pszSrcName)
                    pszSrcName = ValueUnion.psz;
                else
                    return errorSyntax(Misc::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }


    if (!pszType)
        pszType = "basic";

    /* Check for required options */
    if (!pszSrcName)
        return errorSyntax(Misc::tr("VM name required"));

    /* Get the machine object */
    ComPtr<IMachine> srcMachine;
    CHECK_ERROR_RET(a->virtualBox, FindMachine(Bstr(pszSrcName).raw(),
                                               srcMachine.asOutParam()),
                    RTEXITCODE_FAILURE);

    if (srcMachine)
    {
        /* Start the moving */
        ComPtr<IProgress> progress;

        /* we have to open a session for this task */
        CHECK_ERROR_RET(srcMachine, LockMachine(a->session, LockType_Write), RTEXITCODE_FAILURE);
        ComPtr<IMachine> sessionMachine;

        CHECK_ERROR_RET(a->session, COMGETTER(Machine)(sessionMachine.asOutParam()), RTEXITCODE_FAILURE);
        CHECK_ERROR_RET(sessionMachine,
                        MoveTo(Bstr(szTargetFolder).raw(),
                               Bstr(pszType).raw(),
                               progress.asOutParam()),
                        RTEXITCODE_FAILURE);
        hrc = showProgress(progress);
        CHECK_PROGRESS_ERROR_RET(progress, (Misc::tr("Move VM failed")), RTEXITCODE_FAILURE);

        sessionMachine.setNull();
        CHECK_ERROR_RET(a->session, UnlockMachine(), RTEXITCODE_FAILURE);

        RTPrintf(Misc::tr("Machine has been successfully moved into %s\n"),
                 szTargetFolder[0] != '\0' ? szTargetFolder : Misc::tr("the same location"));
    }

    return RTEXITCODE_SUCCESS;
}

static const RTGETOPTDEF g_aCloneVMOptions[] =
{
    { "--snapshot",       's', RTGETOPT_REQ_STRING },
    { "--name",           'n', RTGETOPT_REQ_STRING },
    { "--groups",         'g', RTGETOPT_REQ_STRING },
    { "--mode",           'm', RTGETOPT_REQ_STRING },
    { "--options",        'o', RTGETOPT_REQ_STRING },
    { "--register",       'r', RTGETOPT_REQ_NOTHING },
    { "--basefolder",     'p', RTGETOPT_REQ_STRING },
    { "--uuid",           'u', RTGETOPT_REQ_UUID },
};

static int parseCloneMode(const char *psz, CloneMode_T *pMode)
{
    if (!RTStrICmp(psz, "machine"))
        *pMode = CloneMode_MachineState;
    else if (!RTStrICmp(psz, "machineandchildren"))
        *pMode = CloneMode_MachineAndChildStates;
    else if (!RTStrICmp(psz, "all"))
        *pMode = CloneMode_AllStates;
    else
        return VERR_PARSE_ERROR;

    return VINF_SUCCESS;
}

static int parseCloneOptions(const char *psz, com::SafeArray<CloneOptions_T> *options)
{
    int vrc = VINF_SUCCESS;
    while (psz && *psz && RT_SUCCESS(vrc))
    {
        size_t len;
        const char *pszComma = strchr(psz, ',');
        if (pszComma)
            len = pszComma - psz;
        else
            len = strlen(psz);
        if (len > 0)
        {
            if (!RTStrNICmp(psz, "KeepAllMACs", len))
                options->push_back(CloneOptions_KeepAllMACs);
            else if (!RTStrNICmp(psz, "KeepNATMACs", len))
                options->push_back(CloneOptions_KeepNATMACs);
            else if (!RTStrNICmp(psz, "KeepDiskNames", len))
                options->push_back(CloneOptions_KeepDiskNames);
            else if (   !RTStrNICmp(psz, "Link", len)
                     || !RTStrNICmp(psz, "Linked", len))
                options->push_back(CloneOptions_Link);
            else if (   !RTStrNICmp(psz, "KeepHwUUIDs", len)
                     || !RTStrNICmp(psz, "KeepHwUUID", len))
                options->push_back(CloneOptions_KeepHwUUIDs);
            else
                vrc = VERR_PARSE_ERROR;
        }
        if (pszComma)
            psz += len + 1;
        else
            psz += len;
    }

    return vrc;
}

RTEXITCODE handleCloneVM(HandlerArg *a)
{
    HRESULT                        hrc;
    const char                    *pszSrcName       = NULL;
    const char                    *pszSnapshotName  = NULL;
    CloneMode_T                    mode             = CloneMode_MachineState;
    com::SafeArray<CloneOptions_T> options;
    const char                    *pszTrgName       = NULL;
    const char                    *pszTrgBaseFolder = NULL;
    bool                           fRegister        = false;
    Bstr                           bstrUuid;
    com::SafeArray<BSTR> groups;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aCloneVMOptions, RT_ELEMENTS(g_aCloneVMOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 's':   // --snapshot
                pszSnapshotName = ValueUnion.psz;
                break;

            case 'n':   // --name
                pszTrgName = ValueUnion.psz;
                break;

            case 'g':   // --groups
                parseGroups(ValueUnion.psz, &groups);
                break;

            case 'p':   // --basefolder
                pszTrgBaseFolder = ValueUnion.psz;
                break;

            case 'm':   // --mode
                if (RT_FAILURE(parseCloneMode(ValueUnion.psz, &mode)))
                    return errorArgument(Misc::tr("Invalid clone mode '%s'\n"), ValueUnion.psz);
                break;

            case 'o':   // --options
                if (RT_FAILURE(parseCloneOptions(ValueUnion.psz, &options)))
                    return errorArgument(Misc::tr("Invalid clone options '%s'\n"), ValueUnion.psz);
                break;

            case 'u':   // --uuid
                bstrUuid = Guid(ValueUnion.Uuid).toUtf16().raw();
                break;

            case 'r':   // --register
                fRegister = true;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!pszSrcName)
                    pszSrcName = ValueUnion.psz;
                else
                    return errorSyntax(Misc::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Check for required options */
    if (!pszSrcName)
        return errorSyntax(Misc::tr("VM name required"));

    /* Get the machine object */
    ComPtr<IMachine> srcMachine;
    CHECK_ERROR_RET(a->virtualBox, FindMachine(Bstr(pszSrcName).raw(),
                                               srcMachine.asOutParam()),
                    RTEXITCODE_FAILURE);

    /* Get the platform architecture, to clone a VM which has the same architecture. */
    ComPtr<IPlatform>      platform;
    CHECK_ERROR_RET(srcMachine, COMGETTER(Platform)(platform.asOutParam()), RTEXITCODE_FAILURE);
    PlatformArchitecture_T platformArch;
    CHECK_ERROR_RET(platform, COMGETTER(Architecture)(&platformArch), RTEXITCODE_FAILURE);

    /* If a snapshot name/uuid was given, get the particular machine of this
     * snapshot. */
    if (pszSnapshotName)
    {
        ComPtr<ISnapshot> srcSnapshot;
        CHECK_ERROR_RET(srcMachine, FindSnapshot(Bstr(pszSnapshotName).raw(),
                                                 srcSnapshot.asOutParam()),
                        RTEXITCODE_FAILURE);
        CHECK_ERROR_RET(srcSnapshot, COMGETTER(Machine)(srcMachine.asOutParam()),
                        RTEXITCODE_FAILURE);
    }

    /* Default name necessary? */
    if (!pszTrgName)
        pszTrgName = RTStrAPrintf2(Misc::tr("%s Clone"), pszSrcName);

    Bstr createFlags;
    if (!bstrUuid.isEmpty())
        createFlags = BstrFmt("UUID=%ls", bstrUuid.raw());
    Bstr bstrPrimaryGroup;
    if (groups.size())
        bstrPrimaryGroup = groups[0];
    Bstr bstrSettingsFile;
    CHECK_ERROR_RET(a->virtualBox,
                    ComposeMachineFilename(Bstr(pszTrgName).raw(),
                                           bstrPrimaryGroup.raw(),
                                           createFlags.raw(),
                                           Bstr(pszTrgBaseFolder).raw(),
                                           bstrSettingsFile.asOutParam()),
                    RTEXITCODE_FAILURE);

    ComPtr<IMachine> trgMachine;
    CHECK_ERROR_RET(a->virtualBox, CreateMachine(bstrSettingsFile.raw(),
                                                 Bstr(pszTrgName).raw(),
                                                 platformArch,
                                                 ComSafeArrayAsInParam(groups),
                                                 NULL,
                                                 createFlags.raw(),
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 trgMachine.asOutParam()),
                    RTEXITCODE_FAILURE);

    /* Start the cloning */
    ComPtr<IProgress> progress;
    CHECK_ERROR_RET(srcMachine, CloneTo(trgMachine,
                                        mode,
                                        ComSafeArrayAsInParam(options),
                                        progress.asOutParam()),
                    RTEXITCODE_FAILURE);
    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Misc::tr("Clone VM failed")), RTEXITCODE_FAILURE);

    if (fRegister)
        CHECK_ERROR_RET(a->virtualBox, RegisterMachine(trgMachine), RTEXITCODE_FAILURE);

    Bstr bstrNewName;
    CHECK_ERROR_RET(trgMachine, COMGETTER(Name)(bstrNewName.asOutParam()), RTEXITCODE_FAILURE);
    RTPrintf(Misc::tr("Machine has been successfully cloned as \"%ls\"\n"), bstrNewName.raw());

    return RTEXITCODE_SUCCESS;
}

RTEXITCODE handleStartVM(HandlerArg *a)
{
    HRESULT hrc = S_OK;
    std::list<const char *> VMs;
    Bstr sessionType;
    com::SafeArray<IN_BSTR> aBstrEnv;
    const char *pszPassword = NULL;
    const char *pszPasswordId = NULL;
    Utf8Str strPassword;

#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    /* make sure the VM process will by default start on the same display as VBoxManage */
    {
        const char *pszDisplay = RTEnvGet("DISPLAY");
        if (pszDisplay)
            aBstrEnv.push_back(BstrFmt("DISPLAY=%s", pszDisplay).raw());
        const char *pszXAuth = RTEnvGet("XAUTHORITY");
        if (pszXAuth)
            aBstrEnv.push_back(BstrFmt("XAUTHORITY=%s", pszXAuth).raw());
    }
#endif

    static const RTGETOPTDEF s_aStartVMOptions[] =
    {
        { "--type",         't', RTGETOPT_REQ_STRING },
        { "-type",          't', RTGETOPT_REQ_STRING },     // deprecated
        { "--putenv",       'E', RTGETOPT_REQ_STRING },
        { "--password",     'p', RTGETOPT_REQ_STRING },
        { "--password-id",  'i', RTGETOPT_REQ_STRING }
    };
    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, s_aStartVMOptions, RT_ELEMENTS(s_aStartVMOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 't':   // --type
                if (!RTStrICmp(ValueUnion.psz, "gui"))
                {
                    sessionType = "gui";
                }
#ifdef VBOX_WITH_VBOXSDL
                else if (!RTStrICmp(ValueUnion.psz, "sdl"))
                {
                    sessionType = "sdl";
                }
#endif
#ifdef VBOX_WITH_HEADLESS
                else if (!RTStrICmp(ValueUnion.psz, "capture"))
                {
                    sessionType = "capture";
                }
                else if (!RTStrICmp(ValueUnion.psz, "headless"))
                {
                    sessionType = "headless";
                }
#endif
                else
                    sessionType = ValueUnion.psz;
                break;

            case 'E':   // --putenv
                if (!RTStrStr(ValueUnion.psz, "\n"))
                    aBstrEnv.push_back(Bstr(ValueUnion.psz).raw());
                else
                    return errorSyntax(Misc::tr("Parameter to option --putenv must not contain any newline character"));
                break;

            case 'p':   // --password
                pszPassword = ValueUnion.psz;
                break;

            case 'i':   // --password-id
                pszPasswordId = ValueUnion.psz;
                break;

            case VINF_GETOPT_NOT_OPTION:
                VMs.push_back(ValueUnion.psz);
                break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_PRINT(c))
                        return errorSyntax(Misc::tr("Invalid option -%c"), c);
                    else
                        return errorSyntax(Misc::tr("Invalid option case %i"), c);
                }
                else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Misc::tr("unknown option: %s\n"), ValueUnion.psz);
                else if (ValueUnion.pDef)
                    return errorSyntax("%s: %Rrs", ValueUnion.pDef->pszLong, c);
                else
                    return errorSyntax(Misc::tr("error: %Rrs"), c);
        }
    }

    /* check for required options */
    if (VMs.empty())
        return errorSyntax(Misc::tr("at least one VM name or uuid required"));

    if (pszPassword)
    {
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
    }

    for (std::list<const char *>::const_iterator it = VMs.begin();
         it != VMs.end();
         ++it)
    {
        HRESULT hrc2 = hrc;
        const char *pszVM = *it;
        ComPtr<IMachine> machine;
        CHECK_ERROR(a->virtualBox, FindMachine(Bstr(pszVM).raw(),
                                               machine.asOutParam()));
        if (machine)
        {
            if (pszPasswordId && strPassword.isNotEmpty())
            {
                CHECK_ERROR(machine, AddEncryptionPassword(Bstr(pszPasswordId).raw(), Bstr(strPassword).raw()));
                if (hrc == VBOX_E_PASSWORD_INCORRECT)
                    RTMsgError("Password incorrect!");
            }
            if (SUCCEEDED(hrc))
            {
                ComPtr<IProgress> progress;
                CHECK_ERROR(machine, LaunchVMProcess(a->session, sessionType.raw(),
                                                     ComSafeArrayAsInParam(aBstrEnv), progress.asOutParam()));
                if (SUCCEEDED(hrc) && !progress.isNull())
                {
                    RTPrintf("Waiting for VM \"%s\" to power on...\n", pszVM);
                    CHECK_ERROR(progress, WaitForCompletion(-1));
                    if (SUCCEEDED(hrc))
                    {
                        BOOL completed = true;
                        CHECK_ERROR(progress, COMGETTER(Completed)(&completed));
                        if (SUCCEEDED(hrc))
                        {
                            ASSERT(completed);

                            LONG iRc;
                            CHECK_ERROR(progress, COMGETTER(ResultCode)(&iRc));
                            if (SUCCEEDED(hrc))
                            {
                                if (SUCCEEDED(iRc))
                                    RTPrintf("VM \"%s\" has been successfully started.\n", pszVM);
                                else
                                {
                                    ProgressErrorInfo info(progress);
                                    com::GluePrintErrorInfo(info);
                                }
                                hrc = iRc;
                            }
                        }
                    }
                }
            }
        }

        /* it's important to always close sessions */
        a->session->UnlockMachine();

        /* make sure that we remember the failed state */
        if (FAILED(hrc2))
            hrc = hrc2;
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
static const RTGETOPTDEF g_aSetVMEncryptionOptions[] =
{
    { "--new-password",    'n', RTGETOPT_REQ_STRING },
    { "--old-password",    'o', RTGETOPT_REQ_STRING },
    { "--cipher",          'c', RTGETOPT_REQ_STRING },
    { "--new-password-id", 'i', RTGETOPT_REQ_STRING },
    { "--force",           'f', RTGETOPT_REQ_NOTHING},
};

static RTEXITCODE handleSetVMEncryption(HandlerArg *a, const char *pszFilenameOrUuid)
{
    HRESULT           hrc;
    ComPtr<IMachine>  machine;
    const char       *pszPasswordNew = NULL;
    const char       *pszPasswordOld = NULL;
    const char       *pszCipher = NULL;
    const char       *pszNewPasswordId = NULL;
    BOOL              fForce = FALSE;
    Utf8Str           strPasswordNew;
    Utf8Str           strPasswordOld;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aSetVMEncryptionOptions, RT_ELEMENTS(g_aSetVMEncryptionOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'n':   // --new-password
                pszPasswordNew = ValueUnion.psz;
                break;

            case 'o':   // --old-password
                pszPasswordOld = ValueUnion.psz;
                break;

            case 'c':   // --cipher
                pszCipher = ValueUnion.psz;
                break;

            case 'i':   // --new-password-id
                pszNewPasswordId = ValueUnion.psz;
                break;

            case 'f':   // --force
                fForce = TRUE;
                break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_PRINT(c))
                        return errorSyntax(Misc::tr("Invalid option -%c"), c);
                    else
                        return errorSyntax(Misc::tr("Invalid option case %i"), c);
                }
                else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Misc::tr("unknown option: %s\n"), ValueUnion.psz);
                else if (ValueUnion.pDef)
                    return errorSyntax(Misc::tr("%s: %Rrs"), ValueUnion.pDef->pszLong, c);
                else
                    return errorSyntax(Misc::tr("error: %Rrs"), c);
        }
    }

    if (!pszFilenameOrUuid)
        return errorSyntax(Misc::tr("VM name or UUID required"));

    if (!pszPasswordNew && !pszPasswordOld)
        return errorSyntax(Misc::tr("No password specified"));

    if (   (pszPasswordNew && !pszNewPasswordId)
        || (!pszPasswordNew && pszNewPasswordId))
        return errorSyntax(Misc::tr("A new password must always have a valid identifier set at the same time"));

    if (pszPasswordOld)
    {
        if (!RTStrCmp(pszPasswordOld, "-"))
        {
            /* Get password from console. */
            RTEXITCODE rcExit = readPasswordFromConsole(&strPasswordOld, "Enter old password:");
            if (rcExit == RTEXITCODE_FAILURE)
                return rcExit;
        }
        else
        {
            RTEXITCODE rcExit = readPasswordFile(pszPasswordOld, &strPasswordOld);
            if (rcExit == RTEXITCODE_FAILURE)
            {
                RTMsgError("Failed to read old password from file");
                return rcExit;
            }
        }
    }
    if (pszPasswordNew)
    {
        if (!RTStrCmp(pszPasswordNew, "-"))
        {
            /* Get password from console. */
            RTEXITCODE rcExit = readPasswordFromConsole(&strPasswordNew, "Enter new password:");
            if (rcExit == RTEXITCODE_FAILURE)
                return rcExit;
        }
        else
        {
            RTEXITCODE rcExit = readPasswordFile(pszPasswordNew, &strPasswordNew);
            if (rcExit == RTEXITCODE_FAILURE)
            {
                RTMsgError("Failed to read new password from file");
                return rcExit;
            }
        }
    }

    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(pszFilenameOrUuid).raw(),
                                           machine.asOutParam()));
    if (machine)
    {
        ComPtr<IProgress> progress;
        CHECK_ERROR(machine, ChangeEncryption(Bstr(strPasswordOld).raw(), Bstr(pszCipher).raw(),
                                              Bstr(strPasswordNew).raw(), Bstr(pszNewPasswordId).raw(),
                                              fForce, progress.asOutParam()));
        if (SUCCEEDED(hrc))
            hrc = showProgress(progress);
        if (FAILED(hrc))
        {
            if (hrc == E_NOTIMPL)
                RTMsgError("Encrypt VM operation is not implemented!");
            else if (hrc == VBOX_E_NOT_SUPPORTED)
                RTMsgError("Encrypt VM operation for this cipher is not implemented yet!");
            else if (!progress.isNull())
                CHECK_PROGRESS_ERROR(progress, ("Failed to encrypt the VM"));
            else
                RTMsgError("Failed to encrypt the VM!");
        }
    }
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE handleCheckVMPassword(HandlerArg *a, const char *pszFilenameOrUuid)
{
    HRESULT hrc;
    ComPtr<IMachine> machine;
    Utf8Str strPassword;

    if (a->argc != 1)
        return errorSyntax(Misc::tr("Invalid number of arguments: %d"), a->argc);

    if (!RTStrCmp(a->argv[0], "-"))
    {
        /* Get password from console. */
        RTEXITCODE rcExit = readPasswordFromConsole(&strPassword, "Enter the password:");
        if (rcExit == RTEXITCODE_FAILURE)
            return rcExit;
    }
    else
    {
        RTEXITCODE rcExit = readPasswordFile(a->argv[0], &strPassword);
        if (rcExit == RTEXITCODE_FAILURE)
        {
            RTMsgError("Failed to read password from file");
            return rcExit;
        }
    }

    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(pszFilenameOrUuid).raw(),
                                           machine.asOutParam()));
    if (machine)
    {
        CHECK_ERROR(machine, CheckEncryptionPassword(Bstr(strPassword).raw()));
        if (SUCCEEDED(hrc))
            RTPrintf("The given password is correct\n");
    }
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static const RTGETOPTDEF g_aAddVMOptions[] =
{
    { "--password",    'p', RTGETOPT_REQ_STRING },
    { "--password-id", 'i', RTGETOPT_REQ_STRING }
};

static RTEXITCODE handleAddVMPassword(HandlerArg *a, const char *pszFilenameOrUuid)
{
    HRESULT hrc;
    ComPtr<IMachine> machine;
    const char *pszPassword = NULL;
    const char *pszPasswordId = NULL;
    Utf8Str strPassword;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aAddVMOptions, RT_ELEMENTS(g_aAddVMOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'p':   // --password
                pszPassword = ValueUnion.psz;
                break;

            case 'i':   // --password-id
                pszPasswordId = ValueUnion.psz;
                break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_PRINT(c))
                        return errorSyntax(Misc::tr("Invalid option -%c"), c);
                    else
                        return errorSyntax(Misc::tr("Invalid option case %i"), c);
                }
                else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Misc::tr("unknown option: %s\n"), ValueUnion.psz);
                else if (ValueUnion.pDef)
                    return errorSyntax(Misc::tr("%s: %Rrs"), ValueUnion.pDef->pszLong, c);
                else
                    return errorSyntax(Misc::tr("error: %Rrs"), c);
        }
    }

    if (!pszFilenameOrUuid)
        return errorSyntax(Misc::tr("VM name or UUID required"));

    if (!pszPassword)
        return errorSyntax(Misc::tr("No password specified"));

    if (!pszPasswordId)
        return errorSyntax(Misc::tr("No password identifier specified"));

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

    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(pszFilenameOrUuid).raw(),
                                           machine.asOutParam()));
    if (machine)
    {
        ComPtr<IProgress> progress;
        CHECK_ERROR(machine, AddEncryptionPassword(Bstr(pszPasswordId).raw(), Bstr(strPassword).raw()));
        if (hrc == VBOX_E_PASSWORD_INCORRECT)
            RTMsgError("Password incorrect!");
    }
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE handleRemoveVMPassword(HandlerArg *a, const char *pszFilenameOrUuid)
{
    HRESULT hrc;
    ComPtr<IMachine> machine;

    if (a->argc != 1)
        return errorSyntax(Misc::tr("Invalid number of arguments: %d"), a->argc);

    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(pszFilenameOrUuid).raw(),
                                           machine.asOutParam()));
    if (machine)
    {
        CHECK_ERROR(machine, RemoveEncryptionPassword(Bstr(a->argv[0]).raw()));
        if (hrc == VBOX_E_INVALID_VM_STATE)
            RTMsgError("The machine is in online or transient state\n");
    }
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

RTEXITCODE handleEncryptVM(HandlerArg *a)
{
    if (a->argc < 2)
        return errorSyntax(Misc::tr("subcommand required"));

    HandlerArg  handlerArg;
    handlerArg.argc = a->argc - 2;
    handlerArg.argv = &a->argv[2];
    handlerArg.virtualBox = a->virtualBox;
    handlerArg.session = a->session;
    if (!strcmp(a->argv[1], "setencryption"))
        return handleSetVMEncryption(&handlerArg, a->argv[0]);
    if (!strcmp(a->argv[1], "checkpassword"))
        return handleCheckVMPassword(&handlerArg, a->argv[0]);
    if (!strcmp(a->argv[1], "addpassword"))
        return handleAddVMPassword(&handlerArg, a->argv[0]);
    if (!strcmp(a->argv[1], "removepassword"))
        return handleRemoveVMPassword(&handlerArg, a->argv[0]);
    return errorSyntax(Misc::tr("unknown subcommand"));
}
#endif /* !VBOX_WITH_FULL_VM_ENCRYPTION */

RTEXITCODE handleDiscardState(HandlerArg *a)
{
    HRESULT hrc;

    if (a->argc != 1)
        return errorSyntax(Misc::tr("Incorrect number of parameters"));

    ComPtr<IMachine> machine;
    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                           machine.asOutParam()));
    if (machine)
    {
        do
        {
            /* we have to open a session for this task */
            CHECK_ERROR_BREAK(machine, LockMachine(a->session, LockType_Write));
            do
            {
                ComPtr<IMachine> sessionMachine;
                CHECK_ERROR_BREAK(a->session, COMGETTER(Machine)(sessionMachine.asOutParam()));
                CHECK_ERROR_BREAK(sessionMachine, DiscardSavedState(true /* fDeleteFile */));
            } while (0);
            CHECK_ERROR_BREAK(a->session, UnlockMachine());
        } while (0);
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

RTEXITCODE handleAdoptState(HandlerArg *a)
{
    HRESULT hrc;

    if (a->argc != 2)
        return errorSyntax(Misc::tr("Incorrect number of parameters"));

    ComPtr<IMachine> machine;
    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                           machine.asOutParam()));
    if (machine)
    {
        char szStateFileAbs[RTPATH_MAX] = "";
        int vrc = RTPathAbs(a->argv[1], szStateFileAbs, sizeof(szStateFileAbs));
        if (RT_FAILURE(vrc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Misc::tr("Cannot convert filename \"%s\" to absolute path: %Rrc"),
                                  a->argv[0], vrc);

        do
        {
            /* we have to open a session for this task */
            CHECK_ERROR_BREAK(machine, LockMachine(a->session, LockType_Write));
            do
            {
                ComPtr<IMachine> sessionMachine;
                CHECK_ERROR_BREAK(a->session, COMGETTER(Machine)(sessionMachine.asOutParam()));
                CHECK_ERROR_BREAK(sessionMachine, AdoptSavedState(Bstr(szStateFileAbs).raw()));
            } while (0);
            CHECK_ERROR_BREAK(a->session, UnlockMachine());
        } while (0);
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

RTEXITCODE handleGetExtraData(HandlerArg *a)
{
    HRESULT hrc = S_OK;

    if (a->argc > 2 || a->argc < 1)
        return errorSyntax(Misc::tr("Incorrect number of parameters"));

    /* global data? */
    if (!strcmp(a->argv[0], "global"))
    {
        /* enumeration? */
        if (a->argc < 2 || !strcmp(a->argv[1], "enumerate"))
        {
            SafeArray<BSTR> aKeys;
            CHECK_ERROR(a->virtualBox, GetExtraDataKeys(ComSafeArrayAsOutParam(aKeys)));

            for (size_t i = 0;
                 i < aKeys.size();
                 ++i)
            {
                Bstr bstrKey(aKeys[i]);
                Bstr bstrValue;
                CHECK_ERROR(a->virtualBox, GetExtraData(bstrKey.raw(),
                                                        bstrValue.asOutParam()));

                RTPrintf(Misc::tr("Key: %ls, Value: %ls\n"), bstrKey.raw(), bstrValue.raw());
            }
        }
        else
        {
            Bstr value;
            CHECK_ERROR(a->virtualBox, GetExtraData(Bstr(a->argv[1]).raw(),
                                                    value.asOutParam()));
            if (!value.isEmpty())
                RTPrintf(Misc::tr("Value: %ls\n"), value.raw());
            else
                RTPrintf(Misc::tr("No value set!\n"));
        }
    }
    else
    {
        ComPtr<IMachine> machine;
        CHECK_ERROR(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                               machine.asOutParam()));
        if (machine)
        {
            /* enumeration? */
            if (a->argc < 2 || !strcmp(a->argv[1], "enumerate"))
            {
                SafeArray<BSTR> aKeys;
                CHECK_ERROR(machine, GetExtraDataKeys(ComSafeArrayAsOutParam(aKeys)));

                for (size_t i = 0;
                    i < aKeys.size();
                    ++i)
                {
                    Bstr bstrKey(aKeys[i]);
                    Bstr bstrValue;
                    CHECK_ERROR(machine, GetExtraData(bstrKey.raw(),
                                                      bstrValue.asOutParam()));

                    RTPrintf(Misc::tr("Key: %ls, Value: %ls\n"), bstrKey.raw(), bstrValue.raw());
                }
            }
            else
            {
                Bstr value;
                CHECK_ERROR(machine, GetExtraData(Bstr(a->argv[1]).raw(),
                                                  value.asOutParam()));
                if (!value.isEmpty())
                    RTPrintf(Misc::tr("Value: %ls\n"), value.raw());
                else
                    RTPrintf(Misc::tr("No value set!\n"));
            }
        }
    }
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

RTEXITCODE handleSetExtraData(HandlerArg *a)
{
    HRESULT hrc = S_OK;

    if (a->argc < 2)
        return errorSyntax(Misc::tr("Not enough parameters"));

    /* global data? */
    if (!strcmp(a->argv[0], "global"))
    {
        /** @todo passing NULL is deprecated */
        if (a->argc < 3)
            CHECK_ERROR(a->virtualBox, SetExtraData(Bstr(a->argv[1]).raw(),
                                                    NULL));
        else if (a->argc == 3)
            CHECK_ERROR(a->virtualBox, SetExtraData(Bstr(a->argv[1]).raw(),
                                                    Bstr(a->argv[2]).raw()));
        else
            return errorSyntax(Misc::tr("Too many parameters"));
    }
    else
    {
        ComPtr<IMachine> machine;
        CHECK_ERROR(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                               machine.asOutParam()));
        if (machine)
        {
            /* open an existing session for the VM */
            CHECK_ERROR_RET(machine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);
            /* get the session machine */
            ComPtr<IMachine> sessionMachine;
            CHECK_ERROR_RET(a->session, COMGETTER(Machine)(sessionMachine.asOutParam()), RTEXITCODE_FAILURE);
            /** @todo passing NULL is deprecated */
            if (a->argc < 3)
                CHECK_ERROR(sessionMachine, SetExtraData(Bstr(a->argv[1]).raw(),
                                                         NULL));
            else if (a->argc == 3)
                CHECK_ERROR(sessionMachine, SetExtraData(Bstr(a->argv[1]).raw(),
                                                         Bstr(a->argv[2]).raw()));
            else
                return errorSyntax(Misc::tr("Too many parameters"));
        }
    }
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

RTEXITCODE handleSetProperty(HandlerArg *a) /** @todo r=andy Rename this to handle[Get|Set]SystemProperty? */
{
    HRESULT hrc;

    /* there must be two arguments: property name and value */
    if (a->argc != 2)
        return errorSyntax(Misc::tr("Incorrect number of parameters"));

    ComPtr<ISystemProperties> systemProperties;
    a->virtualBox->COMGETTER(SystemProperties)(systemProperties.asOutParam());

    ComPtr<IPlatformProperties> platformProperties;
    systemProperties->COMGETTER(Platform)(platformProperties.asOutParam());

    if (!strcmp(a->argv[0], "machinefolder"))
    {
        /* reset to default? */
        if (!strcmp(a->argv[1], "default"))
            CHECK_ERROR(systemProperties, COMSETTER(DefaultMachineFolder)(NULL));
        else
            CHECK_ERROR(systemProperties, COMSETTER(DefaultMachineFolder)(Bstr(a->argv[1]).raw()));
    }
    else if (!strcmp(a->argv[0], "hwvirtexclusive"))
    {
        bool   fHwVirtExclusive;

        if (!strcmp(a->argv[1], "on"))
            fHwVirtExclusive = true;
        else if (!strcmp(a->argv[1], "off"))
            fHwVirtExclusive = false;
        else
            return errorArgument(Misc::tr("Invalid hwvirtexclusive argument '%s'"), a->argv[1]);
        CHECK_ERROR(platformProperties, COMSETTER(ExclusiveHwVirt)(fHwVirtExclusive));
    }
    else if (   !strcmp(a->argv[0], "vrdeauthlibrary")
             || !strcmp(a->argv[0], "vrdpauthlibrary"))
    {
        if (!strcmp(a->argv[0], "vrdpauthlibrary"))
            RTStrmPrintf(g_pStdErr, Misc::tr("Warning: 'vrdpauthlibrary' is deprecated. Use 'vrdeauthlibrary'.\n"));

        /* reset to default? */
        if (!strcmp(a->argv[1], "default"))
            CHECK_ERROR(systemProperties, COMSETTER(VRDEAuthLibrary)(NULL));
        else
            CHECK_ERROR(systemProperties, COMSETTER(VRDEAuthLibrary)(Bstr(a->argv[1]).raw()));
    }
    else if (!strcmp(a->argv[0], "websrvauthlibrary"))
    {
        /* reset to default? */
        if (!strcmp(a->argv[1], "default"))
            CHECK_ERROR(systemProperties, COMSETTER(WebServiceAuthLibrary)(NULL));
        else
            CHECK_ERROR(systemProperties, COMSETTER(WebServiceAuthLibrary)(Bstr(a->argv[1]).raw()));
    }
    else if (!strcmp(a->argv[0], "vrdeextpack"))
    {
        /* disable? */
        if (!strcmp(a->argv[1], "null"))
            CHECK_ERROR(systemProperties, COMSETTER(DefaultVRDEExtPack)(NULL));
        else
            CHECK_ERROR(systemProperties, COMSETTER(DefaultVRDEExtPack)(Bstr(a->argv[1]).raw()));
    }
    else if (!strcmp(a->argv[0], "loghistorycount"))
    {
        uint32_t uVal;
        int vrc;
        vrc = RTStrToUInt32Ex(a->argv[1], NULL, 0, &uVal);
        if (vrc != VINF_SUCCESS)
            return errorArgument(Misc::tr("Error parsing Log history count '%s'"), a->argv[1]);
        CHECK_ERROR(systemProperties, COMSETTER(LogHistoryCount)(uVal));
    }
    else if (!strcmp(a->argv[0], "autostartdbpath"))
    {
        /* disable? */
        if (!strcmp(a->argv[1], "null"))
            CHECK_ERROR(systemProperties, COMSETTER(AutostartDatabasePath)(NULL));
        else
            CHECK_ERROR(systemProperties, COMSETTER(AutostartDatabasePath)(Bstr(a->argv[1]).raw()));
    }
    else if (!strcmp(a->argv[0], "defaultfrontend"))
    {
        Bstr bstrDefaultFrontend(a->argv[1]);
        if (!strcmp(a->argv[1], "default"))
            bstrDefaultFrontend.setNull();
        CHECK_ERROR(systemProperties, COMSETTER(DefaultFrontend)(bstrDefaultFrontend.raw()));
    }
    else if (!strcmp(a->argv[0], "logginglevel"))
    {
        Bstr bstrLoggingLevel(a->argv[1]);
        if (!strcmp(a->argv[1], "default"))
            bstrLoggingLevel.setNull();
        CHECK_ERROR(systemProperties, COMSETTER(LoggingLevel)(bstrLoggingLevel.raw()));
    }
    else if (!strcmp(a->argv[0], "proxymode"))
    {
        ProxyMode_T enmProxyMode;
        if (!RTStrICmpAscii(a->argv[1], "system"))
            enmProxyMode = ProxyMode_System;
        else if (!RTStrICmpAscii(a->argv[1], "noproxy"))
            enmProxyMode = ProxyMode_NoProxy;
        else if (!RTStrICmpAscii(a->argv[1], "manual"))
            enmProxyMode = ProxyMode_Manual;
        else
            return errorArgument(Misc::tr("Unknown proxy mode: '%s'"), a->argv[1]);
        CHECK_ERROR(systemProperties, COMSETTER(ProxyMode)(enmProxyMode));
    }
    else if (!strcmp(a->argv[0], "proxyurl"))
    {
        Bstr bstrProxyUrl(a->argv[1]);
        CHECK_ERROR(systemProperties, COMSETTER(ProxyURL)(bstrProxyUrl.raw()));
    }
#ifdef VBOX_WITH_MAIN_NLS
    else if (!strcmp(a->argv[0], "language"))
    {
        Bstr bstrLanguage(a->argv[1]);
        CHECK_ERROR(systemProperties, COMSETTER(LanguageId)(bstrLanguage.raw()));

        /* Kudge alert! Make sure the language change notification is processed,
                        otherwise it may arrive as (XP)COM shuts down and cause
                        trouble in debug builds. */
# ifdef DEBUG
        uint64_t const tsStart = RTTimeNanoTS();
# endif
        unsigned cMsgs = 0;
        int vrc;
        while (   RT_SUCCESS(vrc = NativeEventQueue::getMainEventQueue()->processEventQueue(32 /*ms*/))
               || vrc == VERR_INTERRUPTED)
            cMsgs++;
# ifdef DEBUG
        RTPrintf("vrc=%Rrc cMsgs=%u nsElapsed=%'RU64\n", vrc, cMsgs, RTTimeNanoTS() - tsStart);
# endif
    }
#endif
    else
        return errorSyntax(Misc::tr("Invalid parameter '%s'"), a->argv[0]);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

/**
 * sharedfolder add
 */
static RTEXITCODE handleSharedFolderAdd(HandlerArg *a)
{
    /*
     * Parse arguments (argv[0] == subcommand).
     */
    static const RTGETOPTDEF s_aAddOptions[] =
    {
        { "--name",             'n', RTGETOPT_REQ_STRING },
        { "-name",              'n', RTGETOPT_REQ_STRING },     // deprecated
        { "--hostpath",         'p', RTGETOPT_REQ_STRING },
        { "-hostpath",          'p', RTGETOPT_REQ_STRING },     // deprecated
        { "--readonly",         'r', RTGETOPT_REQ_NOTHING },
        { "-readonly",          'r', RTGETOPT_REQ_NOTHING },    // deprecated
        { "--transient",        't', RTGETOPT_REQ_NOTHING },
        { "-transient",         't', RTGETOPT_REQ_NOTHING },    // deprecated
        { "--automount",        'a', RTGETOPT_REQ_NOTHING },
        { "-automount",         'a', RTGETOPT_REQ_NOTHING },    // deprecated
        { "--auto-mount-point", 'm', RTGETOPT_REQ_STRING },
    };
    const char *pszMachineName    = NULL;
    const char *pszName           = NULL;
    const char *pszHostPath       = NULL;
    bool        fTransient        = false;
    bool        fWritable         = true;
    bool        fAutoMount        = false;
    const char *pszAutoMountPoint = "";

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aAddOptions, RT_ELEMENTS(s_aAddOptions), 1 /*iFirst*/, 0 /*fFlags*/);
    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'n':
                pszName = ValueUnion.psz;
                break;
            case 'p':
                pszHostPath = ValueUnion.psz;
                break;
            case 'r':
                fWritable = false;
                break;
            case 't':
                fTransient = true;
                break;
            case 'a':
                fAutoMount = true;
                break;
            case 'm':
                pszAutoMountPoint = ValueUnion.psz;
                break;
            case VINF_GETOPT_NOT_OPTION:
                if (pszMachineName)
                    return errorArgument(Misc::tr("Machine name given more than once: first '%s', then '%s'"),
                                         pszMachineName, ValueUnion.psz);
                pszMachineName = ValueUnion.psz;
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (!pszMachineName)
        return errorSyntax(Misc::tr("No machine was specified"));

    if (!pszName)
        return errorSyntax(Misc::tr("No shared folder name (--name) was given"));
    if (strchr(pszName, ' '))
        return errorSyntax(Misc::tr("Invalid shared folder name '%s': contains space"), pszName);
    if (strchr(pszName, '\t'))
        return errorSyntax(Misc::tr("Invalid shared folder name '%s': contains tabs"), pszName);
    if (strchr(pszName, '\n') || strchr(pszName, '\r'))
        return errorSyntax(Misc::tr("Invalid shared folder name '%s': contains newline"), pszName);

    if (!pszHostPath)
        return errorSyntax(Misc::tr("No host path (--hostpath) was given"));
    char szAbsHostPath[RTPATH_MAX];
    int vrc = RTPathAbs(pszHostPath, szAbsHostPath, sizeof(szAbsHostPath));
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Misc::tr("RTAbsPath failed on '%s': %Rrc"), pszHostPath, vrc);

    /*
     * Done parsing, do some work.
     */
    ComPtr<IMachine> ptrMachine;
    CHECK_ERROR2I_RET(a->virtualBox, FindMachine(Bstr(pszMachineName).raw(), ptrMachine.asOutParam()), RTEXITCODE_FAILURE);
    AssertReturn(ptrMachine.isNotNull(), RTEXITCODE_FAILURE);

    HRESULT hrc;
    if (fTransient)
    {
        /* open an existing session for the VM */
        CHECK_ERROR2I_RET(ptrMachine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);

        /* get the session machine */
        ComPtr<IMachine> ptrSessionMachine;
        CHECK_ERROR2I_RET(a->session, COMGETTER(Machine)(ptrSessionMachine.asOutParam()), RTEXITCODE_FAILURE);

        /* get the session console */
        ComPtr<IConsole> ptrConsole;
        CHECK_ERROR2I_RET(a->session, COMGETTER(Console)(ptrConsole.asOutParam()), RTEXITCODE_FAILURE);
        if (ptrConsole.isNull())
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Misc::tr("Machine '%s' is not currently running."), pszMachineName);

        CHECK_ERROR2(hrc, ptrConsole, CreateSharedFolder(Bstr(pszName).raw(), Bstr(szAbsHostPath).raw(),
                                                         fWritable, fAutoMount, Bstr(pszAutoMountPoint).raw()));
        a->session->UnlockMachine();
    }
    else
    {
        /* open a session for the VM */
        CHECK_ERROR2I_RET(ptrMachine, LockMachine(a->session, LockType_Write), RTEXITCODE_FAILURE);

        /* get the mutable session machine */
        ComPtr<IMachine> ptrSessionMachine;
        CHECK_ERROR2I_RET(a->session, COMGETTER(Machine)(ptrSessionMachine.asOutParam()), RTEXITCODE_FAILURE);

        CHECK_ERROR2(hrc, ptrSessionMachine, CreateSharedFolder(Bstr(pszName).raw(), Bstr(szAbsHostPath).raw(),
                                                                fWritable, fAutoMount, Bstr(pszAutoMountPoint).raw()));
        if (SUCCEEDED(hrc))
        {
            CHECK_ERROR2(hrc, ptrSessionMachine, SaveSettings());
        }

        a->session->UnlockMachine();
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

/**
 * sharedfolder remove
 */
static RTEXITCODE handleSharedFolderRemove(HandlerArg *a)
{
    /*
     * Parse arguments (argv[0] == subcommand).
     */
    static const RTGETOPTDEF s_aRemoveOptions[] =
    {
        { "--name",             'n', RTGETOPT_REQ_STRING },
        { "-name",              'n', RTGETOPT_REQ_STRING },     // deprecated
        { "--transient",        't', RTGETOPT_REQ_NOTHING },
        { "-transient",         't', RTGETOPT_REQ_NOTHING },    // deprecated
    };
    const char *pszMachineName    = NULL;
    const char *pszName           = NULL;
    bool        fTransient        = false;

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aRemoveOptions, RT_ELEMENTS(s_aRemoveOptions), 1 /*iFirst*/, 0 /*fFlags*/);
    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'n':
                pszName = ValueUnion.psz;
                break;
            case 't':
                fTransient = true;
                break;
            case VINF_GETOPT_NOT_OPTION:
                if (pszMachineName)
                    return errorArgument(Misc::tr("Machine name given more than once: first '%s', then '%s'"),
                                         pszMachineName, ValueUnion.psz);
                pszMachineName = ValueUnion.psz;
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (!pszMachineName)
        return errorSyntax(Misc::tr("No machine was specified"));
    if (!pszName)
        return errorSyntax(Misc::tr("No shared folder name (--name) was given"));

    /*
     * Done parsing, do some real work.
     */
    ComPtr<IMachine> ptrMachine;
    CHECK_ERROR2I_RET(a->virtualBox, FindMachine(Bstr(pszMachineName).raw(), ptrMachine.asOutParam()), RTEXITCODE_FAILURE);
    AssertReturn(ptrMachine.isNotNull(), RTEXITCODE_FAILURE);

    HRESULT hrc;
    if (fTransient)
    {
        /* open an existing session for the VM */
        CHECK_ERROR2I_RET(ptrMachine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);
        /* get the session machine */
        ComPtr<IMachine> ptrSessionMachine;
        CHECK_ERROR2I_RET(a->session, COMGETTER(Machine)(ptrSessionMachine.asOutParam()), RTEXITCODE_FAILURE);
        /* get the session console */
        ComPtr<IConsole> ptrConsole;
        CHECK_ERROR2I_RET(a->session, COMGETTER(Console)(ptrConsole.asOutParam()), RTEXITCODE_FAILURE);
        if (ptrConsole.isNull())
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Misc::tr("Machine '%s' is not currently running.\n"), pszMachineName);

        CHECK_ERROR2(hrc, ptrConsole, RemoveSharedFolder(Bstr(pszName).raw()));

        a->session->UnlockMachine();
    }
    else
    {
        /* open a session for the VM */
        CHECK_ERROR2I_RET(ptrMachine, LockMachine(a->session, LockType_Write), RTEXITCODE_FAILURE);

        /* get the mutable session machine */
        ComPtr<IMachine> ptrSessionMachine;
        CHECK_ERROR2I_RET(a->session, COMGETTER(Machine)(ptrSessionMachine.asOutParam()), RTEXITCODE_FAILURE);

        CHECK_ERROR2(hrc, ptrSessionMachine, RemoveSharedFolder(Bstr(pszName).raw()));

        /* commit and close the session */
        if (SUCCEEDED(hrc))
        {
            CHECK_ERROR2(hrc, ptrSessionMachine, SaveSettings());
        }
        a->session->UnlockMachine();
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static SymlinkPolicy_T nameToSymlinkPolicy(const char *pszName)
{
    if (!RTStrICmp(pszName, "forbidden"))
        return SymlinkPolicy_Forbidden;
    if (!RTStrICmp(pszName, "subtree"))
        return SymlinkPolicy_AllowedInShareSubtree;
    if (!RTStrICmp(pszName, "relative"))
        return SymlinkPolicy_AllowedToRelativeTargets;
    if (!RTStrICmp(pszName, "any"))
        return SymlinkPolicy_AllowedToAnyTarget;

    return SymlinkPolicy_None;
}

/**
 * modify shared folder properties
 */
static RTEXITCODE handleSharedFolderModify(HandlerArg *a)
{
    /*
     * Parse arguments (argv[0] == subcommand).
     */
    static const RTGETOPTDEF s_aModifyOptions[] =
    {
        { "--name",             'n', RTGETOPT_REQ_STRING },
        { "-name",              'n', RTGETOPT_REQ_STRING },     // deprecated
        { "--automount",        'a', RTGETOPT_REQ_BOOL },
        { "-automount",         'a', RTGETOPT_REQ_BOOL },    // deprecated
        { "--readonly",         'r', RTGETOPT_REQ_BOOL },
        { "-readonly",          'r', RTGETOPT_REQ_BOOL },    // deprecated
        { "--symlink-policy",   's', RTGETOPT_REQ_STRING },
        { "-symlink-policy",    's', RTGETOPT_REQ_STRING },     // deprecated
        { "--auto-mount-point", 'm', RTGETOPT_REQ_STRING },
        { "-auto-mount-point",  'm', RTGETOPT_REQ_STRING },     // deprecated
    };
    const char *pszMachineName    = NULL;
    const char *pszName           = NULL;
    int         fWritable         = -1;
    int         fAutoMount        = -1;
    const char *pszAutoMountPoint = NULL;
    SymlinkPolicy_T enmSymlinkPolicy = SymlinkPolicy_None;

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aModifyOptions, RT_ELEMENTS(s_aModifyOptions), 1 /*iFirst*/, 0 /*fFlags*/);
    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'n':
                pszName = ValueUnion.psz;
                break;
            case 'r':
                fWritable = !ValueUnion.f;
                break;
            case 'a':
                fAutoMount = ValueUnion.f;
                break;
            case 'm':
                pszAutoMountPoint = ValueUnion.psz;
                break;
            case 's':
                enmSymlinkPolicy = nameToSymlinkPolicy(ValueUnion.psz);
                if (enmSymlinkPolicy == SymlinkPolicy_None)
                    return errorArgument(Misc::tr("Invalid --symlink-policy argument '%s'"), ValueUnion.psz);
                break;
            case VINF_GETOPT_NOT_OPTION:
                if (pszMachineName)
                    return errorArgument(Misc::tr("Machine name given more than once: first '%s', then '%s'"),
                                         pszMachineName, ValueUnion.psz);
                pszMachineName = ValueUnion.psz;
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (!pszMachineName)
        return errorSyntax(Misc::tr("No machine was specified"));
    if (!pszName)
        return errorSyntax(Misc::tr("No shared folder name (--name) was supplied."));

    if (enmSymlinkPolicy == SymlinkPolicy_None && fWritable == -1 && fAutoMount == -1 && !pszAutoMountPoint)
        return errorSyntax(Misc::tr("No shared folder attributes specified."));

    /*
     * Done parsing, do some real work.
     */
    ComPtr<IMachine> ptrMachine;
    CHECK_ERROR2I_RET(a->virtualBox, FindMachine(Bstr(pszMachineName).raw(), ptrMachine.asOutParam()), RTEXITCODE_FAILURE);
    AssertReturn(ptrMachine.isNotNull(), RTEXITCODE_FAILURE);

    HRESULT hrc;
    /* Open a session for the VM using an exclusive lock as this is for permanent shared folders.
     * Support for modifying transient shared folders hasn't been implemented yet. */
    CHECK_ERROR_RET(ptrMachine, LockMachine(a->session, LockType_Write), RTEXITCODE_FAILURE);

    /* get the mutable session machine */
    ComPtr<IMachine> ptrSessionMachine;
    CHECK_ERROR_RET(a->session, COMGETTER(Machine)(ptrSessionMachine.asOutParam()), RTEXITCODE_FAILURE);

    /* find the desired shared folder to modify */
    com::SafeIfaceArray <ISharedFolder> sharedFolders;
    CHECK_ERROR_RET(ptrSessionMachine, COMGETTER(SharedFolders)(ComSafeArrayAsOutParam(sharedFolders)), RTEXITCODE_FAILURE);
    if (sharedFolders.size() == 0)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Misc::tr("Machine '%s' has no shared folders configured.\n"),
                              pszMachineName);

    bool fFound = false;
    for (size_t i = 0; i < sharedFolders.size(); ++i)
    {
        ComPtr<ISharedFolder> sharedFolder = sharedFolders[i];
        Bstr bstrSharedFolderName;
        CHECK_ERROR_RET(sharedFolder, COMGETTER(Name)(bstrSharedFolderName.asOutParam()), RTEXITCODE_FAILURE);
        Utf8Str strSharedFolderName(bstrSharedFolderName);
        if (!RTStrCmp(strSharedFolderName.c_str(), pszName))
        {
            fFound = true;
            if (enmSymlinkPolicy != SymlinkPolicy_None)
                CHECK_ERROR_RET(sharedFolder, COMSETTER(SymlinkPolicy)(enmSymlinkPolicy), RTEXITCODE_FAILURE);
            if (fWritable != -1)
                CHECK_ERROR_RET(sharedFolder, COMSETTER(Writable)((BOOL)fWritable), RTEXITCODE_FAILURE);
            if (fAutoMount != -1)
                CHECK_ERROR_RET(sharedFolder, COMSETTER(AutoMount)((BOOL)fAutoMount), RTEXITCODE_FAILURE);
            if (pszAutoMountPoint)
                CHECK_ERROR_RET(sharedFolder, COMSETTER(AutoMountPoint) (Bstr(pszAutoMountPoint).raw()), RTEXITCODE_FAILURE);
            break;
        }
    }
    if (!fFound)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Misc::tr("Could not find a shared folder named '%s'.\n"), pszName);

    /* commit and close the session */
    if (SUCCEEDED(hrc))
        CHECK_ERROR(ptrSessionMachine, SaveSettings());

    CHECK_ERROR_RET(a->session, UnlockMachine(), RTEXITCODE_FAILURE);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

RTEXITCODE handleSharedFolder(HandlerArg *a)
{
    if (a->argc < 1)
        return errorSyntax(Misc::tr("Not enough parameters"));

    if (!strcmp(a->argv[0], "add"))
    {
        setCurrentSubcommand(HELP_SCOPE_SHAREDFOLDER_ADD);
        return handleSharedFolderAdd(a);
    }

    if (!strcmp(a->argv[0], "remove"))
    {
        setCurrentSubcommand(HELP_SCOPE_SHAREDFOLDER_REMOVE);
        return handleSharedFolderRemove(a);
    }

    if (!strcmp(a->argv[0], "modify"))
    {
        setCurrentSubcommand(HELP_SCOPE_SHAREDFOLDER_MODIFY);
        return handleSharedFolderModify(a);
    }

    return errorUnknownSubcommand(a->argv[0]);
}

RTEXITCODE handleExtPack(HandlerArg *a)
{
    if (a->argc < 1)
        return errorNoSubcommand();

    ComObjPtr<IExtPackManager> ptrExtPackMgr;
    CHECK_ERROR2I_RET(a->virtualBox, COMGETTER(ExtensionPackManager)(ptrExtPackMgr.asOutParam()), RTEXITCODE_FAILURE);

    RTGETOPTSTATE   GetState;
    RTGETOPTUNION   ValueUnion;
    int             ch;
    HRESULT         hrc = S_OK;

    if (!strcmp(a->argv[0], "install"))
    {
        setCurrentSubcommand(HELP_SCOPE_EXTPACK_INSTALL);
        const char *pszName  = NULL;
        bool        fReplace = false;

        static const RTGETOPTDEF s_aInstallOptions[] =
        {
            { "--replace",        'r', RTGETOPT_REQ_NOTHING },
            { "--accept-license", 'a', RTGETOPT_REQ_STRING },
        };

        RTCList<RTCString> lstLicenseHashes;
        RTGetOptInit(&GetState, a->argc, a->argv, s_aInstallOptions, RT_ELEMENTS(s_aInstallOptions), 1, 0 /*fFlags*/);
        while ((ch = RTGetOpt(&GetState, &ValueUnion)))
        {
            switch (ch)
            {
                case 'r':
                    fReplace = true;
                    break;

                case 'a':
                    lstLicenseHashes.append(ValueUnion.psz);
                    lstLicenseHashes[lstLicenseHashes.size() - 1].toLower();
                    break;

                case VINF_GETOPT_NOT_OPTION:
                    if (pszName)
                        return errorSyntax(Misc::tr("Too many extension pack names given to \"extpack uninstall\""));
                    pszName = ValueUnion.psz;
                    break;

                default:
                    return errorGetOpt(ch, &ValueUnion);
            }
        }
        if (!pszName)
            return errorSyntax(Misc::tr("No extension pack name was given to \"extpack install\""));

        char szPath[RTPATH_MAX];
        int vrc = RTPathAbs(pszName, szPath, sizeof(szPath));
        if (RT_FAILURE(vrc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Misc::tr("RTPathAbs(%s,,) failed with vrc=%Rrc"), pszName, vrc);

        Bstr bstrTarball(szPath);
        Bstr bstrName;
        ComPtr<IExtPackFile> ptrExtPackFile;
        CHECK_ERROR2I_RET(ptrExtPackMgr, OpenExtPackFile(bstrTarball.raw(), ptrExtPackFile.asOutParam()), RTEXITCODE_FAILURE);
        CHECK_ERROR2I_RET(ptrExtPackFile, COMGETTER(Name)(bstrName.asOutParam()), RTEXITCODE_FAILURE);
        BOOL fShowLicense = true;
        CHECK_ERROR2I_RET(ptrExtPackFile, COMGETTER(ShowLicense)(&fShowLicense), RTEXITCODE_FAILURE);
        if (fShowLicense)
        {
            Bstr bstrLicense;
            CHECK_ERROR2I_RET(ptrExtPackFile,
                              QueryLicense(Bstr("").raw() /* PreferredLocale */,
                                           Bstr("").raw() /* PreferredLanguage */,
                                           Bstr("txt").raw() /* Format */,
                                           bstrLicense.asOutParam()), RTEXITCODE_FAILURE);
            Utf8Str strLicense(bstrLicense);
            uint8_t abHash[RTSHA256_HASH_SIZE];
            char    szDigest[RTSHA256_DIGEST_LEN + 1];
            RTSha256(strLicense.c_str(), strLicense.length(), abHash);
            vrc = RTSha256ToString(abHash, szDigest, sizeof(szDigest));
            AssertRCStmt(vrc, szDigest[0] = '\0');
            if (lstLicenseHashes.contains(szDigest))
                RTPrintf(Misc::tr("License accepted.\n"));
            else
            {
                RTPrintf("%s\n", strLicense.c_str());
                RTPrintf(Misc::tr("Do you agree to these license terms and conditions (y/n)? "));
                ch = RTStrmGetCh(g_pStdIn);
                RTPrintf("\n");
                if (ch != 'y' && ch != 'Y')
                {
                    RTPrintf(Misc::tr("Installation of \"%ls\" aborted.\n"), bstrName.raw());
                    return RTEXITCODE_FAILURE;
                }
                if (szDigest[0])
                    RTPrintf(Misc::tr("License accepted. For batch installation add\n"
                                      "--accept-license=%s\n"
                                      "to the VBoxManage command line.\n\n"), szDigest);
            }
        }
        ComPtr<IProgress> ptrProgress;
        CHECK_ERROR2I_RET(ptrExtPackFile, Install(fReplace, NULL, ptrProgress.asOutParam()), RTEXITCODE_FAILURE);
        hrc = showProgress(ptrProgress);
        CHECK_PROGRESS_ERROR_RET(ptrProgress, (Misc::tr("Failed to install \"%s\""), szPath), RTEXITCODE_FAILURE);

        RTPrintf(Misc::tr("Successfully installed \"%ls\".\n"), bstrName.raw());
    }
    else if (!strcmp(a->argv[0], "uninstall"))
    {
        setCurrentSubcommand(HELP_SCOPE_EXTPACK_UNINSTALL);
        const char *pszName = NULL;
        bool        fForced = false;

        static const RTGETOPTDEF s_aUninstallOptions[] =
        {
            { "--force",  'f', RTGETOPT_REQ_NOTHING },
        };

        RTGetOptInit(&GetState, a->argc, a->argv, s_aUninstallOptions, RT_ELEMENTS(s_aUninstallOptions), 1, 0);
        while ((ch = RTGetOpt(&GetState, &ValueUnion)))
        {
            switch (ch)
            {
                case 'f':
                    fForced = true;
                    break;

                case VINF_GETOPT_NOT_OPTION:
                    if (pszName)
                        return errorSyntax(Misc::tr("Too many extension pack names given to \"extpack uninstall\""));
                    pszName = ValueUnion.psz;
                    break;

                default:
                    return errorGetOpt(ch, &ValueUnion);
            }
        }
        if (!pszName)
            return errorSyntax(Misc::tr("No extension pack name was given to \"extpack uninstall\""));

        Bstr bstrName(pszName);
        ComPtr<IProgress> ptrProgress;
        CHECK_ERROR2I_RET(ptrExtPackMgr, Uninstall(bstrName.raw(), fForced, NULL, ptrProgress.asOutParam()), RTEXITCODE_FAILURE);
        hrc = showProgress(ptrProgress);
        CHECK_PROGRESS_ERROR_RET(ptrProgress, (Misc::tr("Failed to uninstall \"%s\""), pszName), RTEXITCODE_FAILURE);

        RTPrintf(Misc::tr("Successfully uninstalled \"%s\".\n"), pszName);
    }
    else if (!strcmp(a->argv[0], "cleanup"))
    {
        setCurrentSubcommand(HELP_SCOPE_EXTPACK_CLEANUP);
        if (a->argc > 1)
            return errorTooManyParameters(&a->argv[1]);
        CHECK_ERROR2I_RET(ptrExtPackMgr, Cleanup(), RTEXITCODE_FAILURE);
        RTPrintf(Misc::tr("Successfully performed extension pack cleanup\n"));
    }
    else
        return errorUnknownSubcommand(a->argv[0]);

    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE handleUnattendedDetect(HandlerArg *a)
{
    HRESULT hrc;

    /*
     * Options.  We work directly on an IUnattended instace while parsing
     * the options.  This saves a lot of extra clutter.
     */
    bool    fMachineReadable = false;
    char    szIsoPath[RTPATH_MAX];
    szIsoPath[0] = '\0';

    /*
     * Parse options.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--iso",                              'i', RTGETOPT_REQ_STRING },
        { "--machine-readable",                 'M', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i': // --iso
                vrc = RTPathAbs(ValueUnion.psz, szIsoPath, sizeof(szIsoPath));
                if (RT_FAILURE(vrc))
                    return errorSyntax(Misc::tr("RTPathAbs failed on '%s': %Rrc"), ValueUnion.psz, vrc);
                break;

            case 'M': // --machine-readable.
                fMachineReadable = true;
                break;

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /*
     * Check for required stuff.
     */
    if (szIsoPath[0] == '\0')
        return errorSyntax(Misc::tr("No ISO specified"));

    /*
     * Do the job.
     */
    ComPtr<IUnattended> ptrUnattended;
    CHECK_ERROR2_RET(hrc, a->virtualBox, CreateUnattendedInstaller(ptrUnattended.asOutParam()), RTEXITCODE_FAILURE);
    CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(IsoPath)(Bstr(szIsoPath).raw()), RTEXITCODE_FAILURE);
    CHECK_ERROR2(hrc, ptrUnattended, DetectIsoOS());
    RTEXITCODE rcExit = SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;

    /*
     * Retrieve the results.
     */
    Bstr bstrDetectedOSTypeId;
    CHECK_ERROR2_RET(hrc, ptrUnattended, COMGETTER(DetectedOSTypeId)(bstrDetectedOSTypeId.asOutParam()), RTEXITCODE_FAILURE);
    Bstr bstrDetectedVersion;
    CHECK_ERROR2_RET(hrc, ptrUnattended, COMGETTER(DetectedOSVersion)(bstrDetectedVersion.asOutParam()), RTEXITCODE_FAILURE);
    Bstr bstrDetectedFlavor;
    CHECK_ERROR2_RET(hrc, ptrUnattended, COMGETTER(DetectedOSFlavor)(bstrDetectedFlavor.asOutParam()), RTEXITCODE_FAILURE);
    Bstr bstrDetectedLanguages;
    CHECK_ERROR2_RET(hrc, ptrUnattended, COMGETTER(DetectedOSLanguages)(bstrDetectedLanguages.asOutParam()), RTEXITCODE_FAILURE);
    Bstr bstrDetectedHints;
    CHECK_ERROR2_RET(hrc, ptrUnattended, COMGETTER(DetectedOSHints)(bstrDetectedHints.asOutParam()), RTEXITCODE_FAILURE);
    SafeArray<BSTR> aImageNames;
    CHECK_ERROR2_RET(hrc, ptrUnattended, COMGETTER(DetectedImageNames)(ComSafeArrayAsOutParam(aImageNames)), RTEXITCODE_FAILURE);
    SafeArray<ULONG> aImageIndices;
    CHECK_ERROR2_RET(hrc, ptrUnattended, COMGETTER(DetectedImageIndices)(ComSafeArrayAsOutParam(aImageIndices)), RTEXITCODE_FAILURE);
    Assert(aImageNames.size() == aImageIndices.size());
    BOOL fInstallSupported = FALSE;
    CHECK_ERROR2_RET(hrc, ptrUnattended, COMGETTER(IsUnattendedInstallSupported)(&fInstallSupported), RTEXITCODE_FAILURE);

    if (fMachineReadable)
    {
        outputMachineReadableString("OSTypeId", &bstrDetectedOSTypeId);
        outputMachineReadableString("OSVersion", &bstrDetectedVersion);
        outputMachineReadableString("OSFlavor", &bstrDetectedFlavor);
        outputMachineReadableString("OSLanguages", &bstrDetectedLanguages);
        outputMachineReadableString("OSHints", &bstrDetectedHints);
        for (size_t i = 0; i < aImageNames.size(); i++)
        {
            Bstr const bstrName = aImageNames[i];
            outputMachineReadableStringWithFmtName(&bstrName, false, "ImageIndex%u", aImageIndices[i]);
        }
        outputMachineReadableBool("IsInstallSupported", &fInstallSupported);
    }
    else
    {
        RTMsgInfo(Misc::tr("Detected '%s' to be:\n"), szIsoPath);
        RTPrintf(Misc::tr("    OS TypeId    = %ls\n"
                          "    OS Version   = %ls\n"
                          "    OS Flavor    = %ls\n"
                          "    OS Languages = %ls\n"
                          "    OS Hints     = %ls\n"),
                 bstrDetectedOSTypeId.raw(),
                 bstrDetectedVersion.raw(),
                 bstrDetectedFlavor.raw(),
                 bstrDetectedLanguages.raw(),
                 bstrDetectedHints.raw());
        for (size_t i = 0; i < aImageNames.size(); i++)
            RTPrintf("    Image #%-3u   = %ls\n", aImageIndices[i], aImageNames[i]);
        if (fInstallSupported)
            RTPrintf(Misc::tr("    Unattended installation supported = yes\n"));
        else
            RTPrintf(Misc::tr("    Unattended installation supported = no\n"));
    }

    return rcExit;
}

static RTEXITCODE handleUnattendedInstall(HandlerArg *a)
{
    HRESULT hrc;
    char    szAbsPath[RTPATH_MAX];

    /*
     * Options.  We work directly on an IUnattended instance while parsing
     * the options.  This saves a lot of extra clutter.
     */
    ComPtr<IUnattended> ptrUnattended;
    CHECK_ERROR2_RET(hrc, a->virtualBox, CreateUnattendedInstaller(ptrUnattended.asOutParam()), RTEXITCODE_FAILURE);
    RTCList<RTCString>  arrPackageSelectionAdjustments;
    ComPtr<IMachine>    ptrMachine;
    bool                fDryRun = false;
    const char         *pszSessionType = "none";

    /*
     * Parse options.
     */
    enum kUnattendedInstallOpt
    {
        kUnattendedInstallOpt_AdminPassword = 1000,
        kUnattendedInstallOpt_AdminPasswordFile
    };
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--iso",                              'i', RTGETOPT_REQ_STRING },
        { "--user",                             'u', RTGETOPT_REQ_STRING },
        { "--password",                         'p', RTGETOPT_REQ_STRING }, /* Keep for backwards compatibility! */
        { "--password-file",                    'X', RTGETOPT_REQ_STRING }, /* Keep for backwards compatibility! */
        { "--user-password",                    'p', RTGETOPT_REQ_STRING },
        { "--user-password-file",               'X', RTGETOPT_REQ_STRING },
        { "--admin-password",                   kUnattendedInstallOpt_AdminPassword, RTGETOPT_REQ_STRING },
        { "--admin-password-file",              kUnattendedInstallOpt_AdminPasswordFile, RTGETOPT_REQ_STRING },
        { "--full-user-name",                   'U', RTGETOPT_REQ_STRING },
        { "--key",                              'k', RTGETOPT_REQ_STRING },
        { "--install-additions",                'A', RTGETOPT_REQ_NOTHING },
        { "--no-install-additions",             'N', RTGETOPT_REQ_NOTHING },
        { "--additions-iso",                    'a', RTGETOPT_REQ_STRING },
        { "--install-txs",                      't', RTGETOPT_REQ_NOTHING },
        { "--no-install-txs",                   'T', RTGETOPT_REQ_NOTHING },
        { "--validation-kit-iso",               'K', RTGETOPT_REQ_STRING },
        { "--locale",                           'l', RTGETOPT_REQ_STRING },
        { "--country",                          'Y', RTGETOPT_REQ_STRING },
        { "--time-zone",                        'z', RTGETOPT_REQ_STRING },
        { "--proxy",                            'y', RTGETOPT_REQ_STRING },
        { "--hostname",                         'H', RTGETOPT_REQ_STRING },
        { "--package-selection-adjustment",     's', RTGETOPT_REQ_STRING },
        { "--dry-run",                          'D', RTGETOPT_REQ_NOTHING },
        // advance options:
        { "--auxiliary-base-path",              'x', RTGETOPT_REQ_STRING },
        { "--image-index",                      'm', RTGETOPT_REQ_UINT32 },
        { "--script-template",                  'c', RTGETOPT_REQ_STRING },
        { "--post-install-template",            'C', RTGETOPT_REQ_STRING },
        { "--post-install-command",             'P', RTGETOPT_REQ_STRING },
        { "--extra-install-kernel-parameters",  'I', RTGETOPT_REQ_STRING },
        { "--language",                         'L', RTGETOPT_REQ_STRING },
        // start vm related options:
        { "--start-vm",                         'S', RTGETOPT_REQ_STRING },
        /** @todo Add a --wait option too for waiting for the VM to shut down or
         *        something like that...? */
    };

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case VINF_GETOPT_NOT_OPTION:
                if (ptrMachine.isNotNull())
                    return errorSyntax(Misc::tr("VM name/UUID given more than once!"));
                CHECK_ERROR2_RET(hrc, a->virtualBox, FindMachine(Bstr(ValueUnion.psz).raw(), ptrMachine.asOutParam()), RTEXITCODE_FAILURE);
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(Machine)(ptrMachine), RTEXITCODE_FAILURE);
                break;

            case 'i':   // --iso
                vrc = RTPathAbs(ValueUnion.psz, szAbsPath, sizeof(szAbsPath));
                if (RT_FAILURE(vrc))
                    return errorSyntax(Misc::tr("RTPathAbs failed on '%s': %Rrc"), ValueUnion.psz, vrc);
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(IsoPath)(Bstr(szAbsPath).raw()), RTEXITCODE_FAILURE);
                break;

            case 'u':   // --user
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(User)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 'p':   // --[user-]password
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(UserPassword)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case kUnattendedInstallOpt_AdminPassword: // --admin-password
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(AdminPassword)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 'X':   // --[user-]password-file
            {
                Utf8Str strPassword;
                RTEXITCODE rcExit = readPasswordFile(ValueUnion.psz, &strPassword);
                if (rcExit != RTEXITCODE_SUCCESS)
                    return rcExit;
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(UserPassword)(Bstr(strPassword).raw()), RTEXITCODE_FAILURE);
                break;
            }

            case kUnattendedInstallOpt_AdminPasswordFile:
            {
                Utf8Str strPassword;
                RTEXITCODE rcExit = readPasswordFile(ValueUnion.psz, &strPassword);
                if (rcExit != RTEXITCODE_SUCCESS)
                    return rcExit;
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(AdminPassword)(Bstr(strPassword).raw()), RTEXITCODE_FAILURE);
                break;
            }

            case 'U':   // --full-user-name
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(FullUserName)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 'k':   // --key
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(ProductKey)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 'A':   // --install-additions
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(InstallGuestAdditions)(TRUE), RTEXITCODE_FAILURE);
                break;
            case 'N':   // --no-install-additions
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(InstallGuestAdditions)(FALSE), RTEXITCODE_FAILURE);
                break;
            case 'a':   // --additions-iso
                vrc = RTPathAbs(ValueUnion.psz, szAbsPath, sizeof(szAbsPath));
                if (RT_FAILURE(vrc))
                    return errorSyntax(Misc::tr("RTPathAbs failed on '%s': %Rrc"), ValueUnion.psz, vrc);
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(AdditionsIsoPath)(Bstr(szAbsPath).raw()), RTEXITCODE_FAILURE);
                break;

            case 't':   // --install-txs
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(InstallTestExecService)(TRUE), RTEXITCODE_FAILURE);
                break;
            case 'T':   // --no-install-txs
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(InstallTestExecService)(FALSE), RTEXITCODE_FAILURE);
                break;
            case 'K':   // --valiation-kit-iso
                vrc = RTPathAbs(ValueUnion.psz, szAbsPath, sizeof(szAbsPath));
                if (RT_FAILURE(vrc))
                    return errorSyntax(Misc::tr("RTPathAbs failed on '%s': %Rrc"), ValueUnion.psz, vrc);
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(ValidationKitIsoPath)(Bstr(szAbsPath).raw()), RTEXITCODE_FAILURE);
                break;

            case 'l':   // --locale
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(Locale)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 'Y':   // --country
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(Country)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 'z':   // --time-zone;
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(TimeZone)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 'y':   // --proxy
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(Proxy)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 'H':   // --hostname
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(Hostname)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 's':   // --package-selection-adjustment
                arrPackageSelectionAdjustments.append(ValueUnion.psz);
                break;

            case 'D':
                fDryRun = true;
                break;

            case 'x':   // --auxiliary-base-path
                vrc = RTPathAbs(ValueUnion.psz, szAbsPath, sizeof(szAbsPath));
                if (RT_FAILURE(vrc))
                    return errorSyntax(Misc::tr("RTPathAbs failed on '%s': %Rrc"), ValueUnion.psz, vrc);
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(AuxiliaryBasePath)(Bstr(szAbsPath).raw()), RTEXITCODE_FAILURE);
                break;

            case 'm':   // --image-index
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(ImageIndex)(ValueUnion.u32), RTEXITCODE_FAILURE);
                break;

            case 'c':   // --script-template
                vrc = RTPathAbs(ValueUnion.psz, szAbsPath, sizeof(szAbsPath));
                if (RT_FAILURE(vrc))
                    return errorSyntax(Misc::tr("RTPathAbs failed on '%s': %Rrc"), ValueUnion.psz, vrc);
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(ScriptTemplatePath)(Bstr(szAbsPath).raw()), RTEXITCODE_FAILURE);
                break;

            case 'C':   // --post-install-script-template
                vrc = RTPathAbs(ValueUnion.psz, szAbsPath, sizeof(szAbsPath));
                if (RT_FAILURE(vrc))
                    return errorSyntax(Misc::tr("RTPathAbs failed on '%s': %Rrc"), ValueUnion.psz, vrc);
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(PostInstallScriptTemplatePath)(Bstr(szAbsPath).raw()), RTEXITCODE_FAILURE);
                break;

            case 'P':   // --post-install-command.
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(PostInstallCommand)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 'I':   // --extra-install-kernel-parameters
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(ExtraInstallKernelParameters)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 'L':   // --language
                CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(Language)(Bstr(ValueUnion.psz).raw()), RTEXITCODE_FAILURE);
                break;

            case 'S':   // --start-vm
                pszSessionType = ValueUnion.psz;
                break;

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /*
     * Check for required stuff.
     */
    if (ptrMachine.isNull())
        return errorSyntax(Misc::tr("Missing VM name/UUID"));

    /*
     * Set accumulative attributes.
     */
    if (arrPackageSelectionAdjustments.size() == 1)
        CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(PackageSelectionAdjustments)(Bstr(arrPackageSelectionAdjustments[0]).raw()),
                         RTEXITCODE_FAILURE);
    else if (arrPackageSelectionAdjustments.size() > 1)
    {
        RTCString strAdjustments;
        strAdjustments.join(arrPackageSelectionAdjustments, ";");
        CHECK_ERROR2_RET(hrc, ptrUnattended, COMSETTER(PackageSelectionAdjustments)(Bstr(strAdjustments).raw()), RTEXITCODE_FAILURE);
    }

    /*
     * Get details about the machine so we can display them below.
     */
    Bstr bstrMachineName;
    CHECK_ERROR2_RET(hrc, ptrMachine, COMGETTER(Name)(bstrMachineName.asOutParam()), RTEXITCODE_FAILURE);
    Bstr bstrUuid;
    CHECK_ERROR2_RET(hrc, ptrMachine, COMGETTER(Id)(bstrUuid.asOutParam()), RTEXITCODE_FAILURE);
    BSTR bstrInstalledOS;
    CHECK_ERROR2_RET(hrc, ptrMachine, COMGETTER(OSTypeId)(&bstrInstalledOS), RTEXITCODE_FAILURE);
    Utf8Str strInstalledOS(bstrInstalledOS);

    /*
     * Temporarily lock the machine to check whether it's running or not.
     * We take this opportunity to disable the first run wizard.
     */
    CHECK_ERROR2_RET(hrc, ptrMachine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);
    {
        ComPtr<IConsole> ptrConsole;
        CHECK_ERROR2(hrc, a->session, COMGETTER(Console)(ptrConsole.asOutParam()));

        if (   ptrConsole.isNull()
            && SUCCEEDED(hrc)
            && (   RTStrICmp(pszSessionType, "gui") == 0
                || RTStrICmp(pszSessionType, "none") == 0))
        {
            ComPtr<IMachine> ptrSessonMachine;
            CHECK_ERROR2(hrc, a->session, COMGETTER(Machine)(ptrSessonMachine.asOutParam()));
            if (ptrSessonMachine.isNotNull())
            {
                CHECK_ERROR2(hrc, ptrSessonMachine, SetExtraData(Bstr("GUI/FirstRun").raw(), Bstr("0").raw()));
            }
        }

        a->session->UnlockMachine();
        if (FAILED(hrc))
            return RTEXITCODE_FAILURE;
        if (ptrConsole.isNotNull())
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Misc::tr("Machine '%ls' is currently running"), bstrMachineName.raw());
    }

    /*
     * Do the work.
     */
    RTMsgInfo(Misc::tr("%s unattended installation of %s in machine '%ls' (%ls).\n"),
              RTStrICmp(pszSessionType, "none") == 0 ? Misc::tr("Preparing") : Misc::tr("Starting"),
              strInstalledOS.c_str(), bstrMachineName.raw(), bstrUuid.raw());

    CHECK_ERROR2_RET(hrc, ptrUnattended,Prepare(), RTEXITCODE_FAILURE);
    if (!fDryRun)
    {
        CHECK_ERROR2_RET(hrc, ptrUnattended, ConstructMedia(), RTEXITCODE_FAILURE);
        CHECK_ERROR2_RET(hrc, ptrUnattended, ReconfigureVM(), RTEXITCODE_FAILURE);
    }

    /*
     * Retrieve and display the parameters actually used.
     */
    RTMsgInfo(Misc::tr("Using values:\n"));
#define SHOW_ATTR(a_Attr, a_szText, a_Type, a_szFmt) do { \
            a_Type Value; \
            HRESULT hrc2 = ptrUnattended->COMGETTER(a_Attr)(&Value); \
            if (SUCCEEDED(hrc2)) \
                RTPrintf("  %32s = " a_szFmt "\n", a_szText, Value); \
            else \
                RTPrintf(Misc::tr("  %32s = failed: %Rhrc\n"), a_szText, hrc2); \
        } while (0)
#define SHOW_STR_ATTR(a_Attr, a_szText) do { \
            Bstr bstrString; \
            HRESULT hrc2 = ptrUnattended->COMGETTER(a_Attr)(bstrString.asOutParam()); \
            if (SUCCEEDED(hrc2)) \
                RTPrintf("  %32s = %ls\n", a_szText, bstrString.raw()); \
            else \
                RTPrintf(Misc::tr("  %32s = failed: %Rhrc\n"), a_szText, hrc2); \
        } while (0)

    SHOW_STR_ATTR(IsoPath,                       "isoPath");
    SHOW_STR_ATTR(User,                          "user");
    SHOW_STR_ATTR(UserPassword,                  "password"); /* Keep for backwards compatibility! */
    SHOW_STR_ATTR(UserPassword,                  "user-password");
    SHOW_STR_ATTR(AdminPassword,                 "admin-password");
    SHOW_STR_ATTR(FullUserName,                  "fullUserName");
    SHOW_STR_ATTR(ProductKey,                    "productKey");
    SHOW_STR_ATTR(AdditionsIsoPath,              "additionsIsoPath");
    SHOW_ATTR(    InstallGuestAdditions,         "installGuestAdditions",    BOOL, "%RTbool");
    SHOW_STR_ATTR(ValidationKitIsoPath,          "validationKitIsoPath");
    SHOW_ATTR(    InstallTestExecService,        "installTestExecService",   BOOL, "%RTbool");
    SHOW_STR_ATTR(Locale,                        "locale");
    SHOW_STR_ATTR(Country,                       "country");
    SHOW_STR_ATTR(TimeZone,                      "timeZone");
    SHOW_STR_ATTR(Proxy,                         "proxy");
    SHOW_STR_ATTR(Hostname,                      "hostname");
    SHOW_STR_ATTR(PackageSelectionAdjustments,   "packageSelectionAdjustments");
    SHOW_STR_ATTR(AuxiliaryBasePath,             "auxiliaryBasePath");
    SHOW_ATTR(    ImageIndex,                    "imageIndex",               ULONG, "%u");
    SHOW_STR_ATTR(ScriptTemplatePath,            "scriptTemplatePath");
    SHOW_STR_ATTR(PostInstallScriptTemplatePath, "postInstallScriptTemplatePath");
    SHOW_STR_ATTR(PostInstallCommand,            "postInstallCommand");
    SHOW_STR_ATTR(ExtraInstallKernelParameters,  "extraInstallKernelParameters");
    SHOW_STR_ATTR(Language,                      "language");
    SHOW_STR_ATTR(DetectedOSTypeId,              "detectedOSTypeId");
    SHOW_STR_ATTR(DetectedOSVersion,             "detectedOSVersion");
    SHOW_STR_ATTR(DetectedOSFlavor,              "detectedOSFlavor");
    SHOW_STR_ATTR(DetectedOSLanguages,           "detectedOSLanguages");
    SHOW_STR_ATTR(DetectedOSHints,               "detectedOSHints");
    {
        ULONG idxImage = 0;
        HRESULT hrc2 = ptrUnattended->COMGETTER(ImageIndex)(&idxImage);
        if (FAILED(hrc2))
            idxImage = 0;
        SafeArray<BSTR> aImageNames;
        hrc2 = ptrUnattended->COMGETTER(DetectedImageNames)(ComSafeArrayAsOutParam(aImageNames));
        if (SUCCEEDED(hrc2))
        {
            SafeArray<ULONG> aImageIndices;
            hrc2 = ptrUnattended->COMGETTER(DetectedImageIndices)(ComSafeArrayAsOutParam(aImageIndices));
            if (SUCCEEDED(hrc2))
            {
                Assert(aImageNames.size() == aImageIndices.size());
                for (size_t i = 0; i < aImageNames.size(); i++)
                {
                    char szTmp[64];
                    RTStrPrintf(szTmp, sizeof(szTmp), "detectedImage[%u]%s", i, idxImage != aImageIndices[i] ? "" : "*");
                    RTPrintf("  %32s = #%u: %ls\n", szTmp, aImageIndices[i], aImageNames[i]);
                }
            }
            else
                RTPrintf(Misc::tr("  %32s = failed: %Rhrc\n"), "detectedImageIndices", hrc2);
        }
        else
            RTPrintf(Misc::tr("  %32 = failed: %Rhrc\n"), "detectedImageNames", hrc2);
    }

#undef SHOW_STR_ATTR
#undef SHOW_ATTR

    /* We can drop the IUnatteded object now. */
    ptrUnattended.setNull();

    /*
     * Start the VM if requested.
     */
    if (   fDryRun
        || RTStrICmp(pszSessionType, "none") == 0)
    {
        if (!fDryRun)
            RTMsgInfo(Misc::tr("VM '%ls' (%ls) is ready to be started (e.g. VBoxManage startvm).\n"), bstrMachineName.raw(), bstrUuid.raw());
        hrc = S_OK;
    }
    else
    {
        com::SafeArray<IN_BSTR> aBstrEnv;
#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
        /* make sure the VM process will start on the same display as VBoxManage */
        const char *pszDisplay = RTEnvGet("DISPLAY");
        if (pszDisplay)
            aBstrEnv.push_back(BstrFmt("DISPLAY=%s", pszDisplay).raw());
        const char *pszXAuth = RTEnvGet("XAUTHORITY");
        if (pszXAuth)
            aBstrEnv.push_back(BstrFmt("XAUTHORITY=%s", pszXAuth).raw());
#endif
        ComPtr<IProgress> ptrProgress;
        CHECK_ERROR2(hrc, ptrMachine, LaunchVMProcess(a->session, Bstr(pszSessionType).raw(), ComSafeArrayAsInParam(aBstrEnv), ptrProgress.asOutParam()));
        if (SUCCEEDED(hrc) && !ptrProgress.isNull())
        {
            RTMsgInfo(Misc::tr("Waiting for VM '%ls' to power on...\n"), bstrMachineName.raw());
            CHECK_ERROR2(hrc, ptrProgress, WaitForCompletion(-1));
            if (SUCCEEDED(hrc))
            {
                BOOL fCompleted = true;
                CHECK_ERROR2(hrc, ptrProgress, COMGETTER(Completed)(&fCompleted));
                if (SUCCEEDED(hrc))
                {
                    ASSERT(fCompleted);

                    LONG iRc;
                    CHECK_ERROR2(hrc, ptrProgress, COMGETTER(ResultCode)(&iRc));
                    if (SUCCEEDED(hrc))
                    {
                        if (SUCCEEDED(iRc))
                            RTMsgInfo(Misc::tr("VM '%ls' (%ls) has been successfully started.\n"),
                                      bstrMachineName.raw(), bstrUuid.raw());
                        else
                        {
                            ProgressErrorInfo info(ptrProgress);
                            com::GluePrintErrorInfo(info);
                        }
                        hrc = iRc;
                    }
                }
            }
        }

        /*
         * Do we wait for the VM to power down?
         */
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


RTEXITCODE handleUnattended(HandlerArg *a)
{
    /*
     * Sub-command switch.
     */
    if (a->argc < 1)
        return errorNoSubcommand();

    if (!strcmp(a->argv[0], "detect"))
    {
        setCurrentSubcommand(HELP_SCOPE_UNATTENDED_DETECT);
        return handleUnattendedDetect(a);
    }

    if (!strcmp(a->argv[0], "install"))
    {
        setCurrentSubcommand(HELP_SCOPE_UNATTENDED_INSTALL);
        return handleUnattendedInstall(a);
    }

    /* Consider some kind of create-vm-and-install-guest-os command. */
    return errorUnknownSubcommand(a->argv[0]);
}

/**
 * Common Cloud profile options.
 */
typedef struct
{
    const char     *pszProviderName;
    const char     *pszProfileName;
} CLOUDPROFILECOMMONOPT;
typedef CLOUDPROFILECOMMONOPT *PCLOUDPROFILECOMMONOPT;

/**
 * Sets the properties of cloud profile
 *
 * @returns 0 on success, 1 on failure
 */

static RTEXITCODE setCloudProfileProperties(HandlerArg *a, int iFirst, PCLOUDPROFILECOMMONOPT pCommonOpts)
{

    HRESULT hrc = S_OK;

    Bstr bstrProvider(pCommonOpts->pszProviderName);
    Bstr bstrProfile(pCommonOpts->pszProfileName);

    /*
     * Parse options.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--clouduser",    'u', RTGETOPT_REQ_STRING },
        { "--fingerprint",  'p', RTGETOPT_REQ_STRING },
        { "--keyfile",      'k', RTGETOPT_REQ_STRING },
        { "--passphrase",   'P', RTGETOPT_REQ_STRING },
        { "--tenancy",      't', RTGETOPT_REQ_STRING },
        { "--compartment",  'c', RTGETOPT_REQ_STRING },
        { "--region",       'r', RTGETOPT_REQ_STRING }
    };

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    com::SafeArray<BSTR> names;
    com::SafeArray<BSTR> values;

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'u':   // --clouduser
                Bstr("user").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 'p':   // --fingerprint
                Bstr("fingerprint").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 'k':   // --keyfile
                Bstr("key_file").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 'P':   // --passphrase
                Bstr("pass_phrase").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 't':   // --tenancy
                Bstr("tenancy").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 'c':   // --compartment
                Bstr("compartment").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 'r':   // --region
                Bstr("region").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* check for required options */
    if (bstrProvider.isEmpty())
        return errorSyntax(Misc::tr("Parameter --provider is required"));
    if (bstrProfile.isEmpty())
        return errorSyntax(Misc::tr("Parameter --profile is required"));

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;

    ComPtr<ICloudProviderManager> pCloudProviderManager;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProvider> pCloudProvider;

    CHECK_ERROR2_RET(hrc, pCloudProviderManager,
                     GetProviderByShortName(bstrProvider.raw(), pCloudProvider.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProfile> pCloudProfile;

    if (pCloudProvider)
    {
        CHECK_ERROR2_RET(hrc, pCloudProvider,
                         GetProfileByName(bstrProfile.raw(), pCloudProfile.asOutParam()),
                         RTEXITCODE_FAILURE);
        CHECK_ERROR2_RET(hrc, pCloudProfile,
                         SetProperties(ComSafeArrayAsInParam(names), ComSafeArrayAsInParam(values)),
                         RTEXITCODE_FAILURE);
    }

    CHECK_ERROR2(hrc, pCloudProvider, SaveProfiles());

    RTPrintf(Misc::tr("Provider %ls: profile '%ls' was updated.\n"),bstrProvider.raw(), bstrProfile.raw());

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

/**
 * Gets the properties of cloud profile
 *
 * @returns 0 on success, 1 on failure
 */
static RTEXITCODE showCloudProfileProperties(HandlerArg *a, PCLOUDPROFILECOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    Bstr bstrProvider(pCommonOpts->pszProviderName);
    Bstr bstrProfile(pCommonOpts->pszProfileName);

    /* check for required options */
    if (bstrProvider.isEmpty())
        return errorSyntax(Misc::tr("Parameter --provider is required"));
    if (bstrProfile.isEmpty())
        return errorSyntax(Misc::tr("Parameter --profile is required"));

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
    ComPtr<ICloudProviderManager> pCloudProviderManager;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()),
                     RTEXITCODE_FAILURE);
    ComPtr<ICloudProvider> pCloudProvider;
    CHECK_ERROR2_RET(hrc, pCloudProviderManager,
                     GetProviderByShortName(bstrProvider.raw(), pCloudProvider.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProfile> pCloudProfile;
    if (pCloudProvider)
    {
        CHECK_ERROR2_RET(hrc, pCloudProvider,
                         GetProfileByName(bstrProfile.raw(), pCloudProfile.asOutParam()),
                         RTEXITCODE_FAILURE);

        Bstr bstrProviderID;
        pCloudProfile->COMGETTER(ProviderId)(bstrProviderID.asOutParam());
        RTPrintf(Misc::tr("Provider GUID: %ls\n"), bstrProviderID.raw());

        com::SafeArray<BSTR> names;
        com::SafeArray<BSTR> values;
        CHECK_ERROR2_RET(hrc, pCloudProfile,
                         GetProperties(Bstr().raw(), ComSafeArrayAsOutParam(names), ComSafeArrayAsOutParam(values)),
                         RTEXITCODE_FAILURE);
        size_t cNames = names.size();
        size_t cValues = values.size();
        bool fFirst = true;
        for (size_t k = 0; k < cNames; k++)
        {
            Bstr value;
            if (k < cValues)
                value = values[k];
            RTPrintf("%s%ls=%ls\n",
                     fFirst ? Misc::tr("Property:      ") : "               ",
                     names[k], value.raw());
            fFirst = false;
        }

        RTPrintf("\n");
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE addCloudProfile(HandlerArg *a, int iFirst, PCLOUDPROFILECOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    Bstr bstrProvider(pCommonOpts->pszProviderName);
    Bstr bstrProfile(pCommonOpts->pszProfileName);


    /* check for required options */
    if (bstrProvider.isEmpty())
        return errorSyntax(Misc::tr("Parameter --provider is required"));
    if (bstrProfile.isEmpty())
        return errorSyntax(Misc::tr("Parameter --profile is required"));

    /*
     * Parse options.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--clouduser",    'u', RTGETOPT_REQ_STRING },
        { "--fingerprint",  'p', RTGETOPT_REQ_STRING },
        { "--keyfile",      'k', RTGETOPT_REQ_STRING },
        { "--passphrase",   'P', RTGETOPT_REQ_STRING },
        { "--tenancy",      't', RTGETOPT_REQ_STRING },
        { "--compartment",  'c', RTGETOPT_REQ_STRING },
        { "--region",       'r', RTGETOPT_REQ_STRING }
    };

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    com::SafeArray<BSTR> names;
    com::SafeArray<BSTR> values;

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'u':   // --clouduser
                Bstr("user").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 'p':   // --fingerprint
                Bstr("fingerprint").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 'k':   // --keyfile
                Bstr("key_file").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 'P':   // --passphrase
                Bstr("pass_phrase").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 't':   // --tenancy
                Bstr("tenancy").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 'c':   // --compartment
                Bstr("compartment").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            case 'r':   // --region
                Bstr("region").detachTo(names.appendedRaw());
                Bstr(ValueUnion.psz).detachTo(values.appendedRaw());
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;

    ComPtr<ICloudProviderManager> pCloudProviderManager;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProvider> pCloudProvider;
    CHECK_ERROR2_RET(hrc, pCloudProviderManager,
                     GetProviderByShortName(bstrProvider.raw(), pCloudProvider.asOutParam()),
                     RTEXITCODE_FAILURE);

    CHECK_ERROR2_RET(hrc, pCloudProvider,
                     CreateProfile(bstrProfile.raw(),
                                   ComSafeArrayAsInParam(names),
                                   ComSafeArrayAsInParam(values)),
                     RTEXITCODE_FAILURE);

    CHECK_ERROR2(hrc, pCloudProvider, SaveProfiles());

    RTPrintf(Misc::tr("Provider %ls: profile '%ls' was added.\n"),bstrProvider.raw(), bstrProfile.raw());

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE deleteCloudProfile(HandlerArg *a, PCLOUDPROFILECOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    Bstr bstrProvider(pCommonOpts->pszProviderName);
    Bstr bstrProfile(pCommonOpts->pszProfileName);

    /* check for required options */
    if (bstrProvider.isEmpty())
        return errorSyntax(Misc::tr("Parameter --provider is required"));
    if (bstrProfile.isEmpty())
        return errorSyntax(Misc::tr("Parameter --profile is required"));

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
    ComPtr<ICloudProviderManager> pCloudProviderManager;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()),
                     RTEXITCODE_FAILURE);
    ComPtr<ICloudProvider> pCloudProvider;
    CHECK_ERROR2_RET(hrc, pCloudProviderManager,
                     GetProviderByShortName(bstrProvider.raw(), pCloudProvider.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProfile> pCloudProfile;
    if (pCloudProvider)
    {
        CHECK_ERROR2_RET(hrc, pCloudProvider,
                         GetProfileByName(bstrProfile.raw(), pCloudProfile.asOutParam()),
                         RTEXITCODE_FAILURE);

        CHECK_ERROR2_RET(hrc, pCloudProfile,
                         Remove(),
                         RTEXITCODE_FAILURE);

        CHECK_ERROR2_RET(hrc, pCloudProvider,
                         SaveProfiles(),
                         RTEXITCODE_FAILURE);

        RTPrintf(Misc::tr("Provider %ls: profile '%ls' was deleted.\n"), bstrProvider.raw(), bstrProfile.raw());
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

RTEXITCODE handleCloudProfile(HandlerArg *a)
{
    if (a->argc < 1)
        return errorNoSubcommand();

    static const RTGETOPTDEF s_aOptions[] =
    {
        /* common options */
        { "--provider",     'v', RTGETOPT_REQ_STRING },
        { "--profile",      'f', RTGETOPT_REQ_STRING },
        /* subcommands */
        { "add",            1000, RTGETOPT_REQ_NOTHING },
        { "show",           1001, RTGETOPT_REQ_NOTHING },
        { "update",         1002, RTGETOPT_REQ_NOTHING },
        { "delete",         1003, RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    CLOUDPROFILECOMMONOPT   CommonOpts = { NULL, NULL };
    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'v':   // --provider
                CommonOpts.pszProviderName = ValueUnion.psz;
                break;
            case 'f':   // --profile
                CommonOpts.pszProfileName = ValueUnion.psz;
                break;
            /* Sub-commands: */
            case 1000:
                setCurrentSubcommand(HELP_SCOPE_CLOUDPROFILE_ADD);
                return addCloudProfile(a, GetState.iNext, &CommonOpts);
            case 1001:
                setCurrentSubcommand(HELP_SCOPE_CLOUDPROFILE_SHOW);
                return showCloudProfileProperties(a, &CommonOpts);
            case 1002:
                setCurrentSubcommand(HELP_SCOPE_CLOUDPROFILE_UPDATE);
                return setCloudProfileProperties(a, GetState.iNext, &CommonOpts);
            case 1003:
                setCurrentSubcommand(HELP_SCOPE_CLOUDPROFILE_DELETE);
                return deleteCloudProfile(a, &CommonOpts);
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    return errorNoSubcommand();
}
