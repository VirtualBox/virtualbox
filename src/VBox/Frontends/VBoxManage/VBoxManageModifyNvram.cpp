/* $Id: VBoxManageModifyNvram.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - The nvram control related commands.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/errcore.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/ctype.h>
#include <iprt/stream.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/uuid.h>
#include <VBox/log.h>

#include "VBoxManage.h"
using namespace com;

DECLARE_TRANSLATION_CONTEXT(Nvram);

// funcs
///////////////////////////////////////////////////////////////////////////////


/**
 * Handles the 'modifynvram myvm inituefivarstore' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   nvram           Reference to the NVRAM store interface.
 */
static RTEXITCODE handleModifyNvramInitUefiVarStore(HandlerArg *a, ComPtr<INvramStore> &nvramStore)
{
    if (a->argc != 2)
        return errorTooManyParameters(&a->argv[1]);

    CHECK_ERROR2I_RET(nvramStore, InitUefiVariableStore(0 /*aSize*/), RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the 'modifynvram myvm enrollmssignatures' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   nvram           Reference to the NVRAM store interface.
 */
static RTEXITCODE handleModifyNvramEnrollMsSignatures(HandlerArg *a, ComPtr<INvramStore> &nvramStore)
{
    if (a->argc != 2)
        return errorTooManyParameters(&a->argv[1]);

    ComPtr<IUefiVariableStore> uefiVarStore;
    CHECK_ERROR2I_RET(nvramStore, COMGETTER(UefiVariableStore)(uefiVarStore.asOutParam()), RTEXITCODE_FAILURE);

    CHECK_ERROR2I_RET(uefiVarStore, EnrollDefaultMsSignatures(), RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}


/**
 * Helper for handleModifyNvramEnrollPlatformKey() and handleModifyNvramEnrollMok().
 *
 * This function reads key from file and enrolls it either as a PK (Platform Key)
 * or as a MOK (Machine Owner Key).
 *
 * @returns Exit code.
 * @param   pszKey          Path to a file which contains the key.
 * @param   pszOwnerUuid    Owner's UUID.
 * @param   nvramStore      Reference to the NVRAM store interface.
 * @param   fPk             If True, a key will be enrolled as a PK, otherwise as a MOK.
 */
static RTEXITCODE handleModifyNvramEnrollPlatformKeyOrMok(const char *pszKey, const char *pszOwnerUuid,
                                                          ComPtr<INvramStore> &nvramStore, bool fPk)
{
    RTFILE hKeyFile;

    int vrc = RTFileOpen(&hKeyFile, pszKey, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(vrc))
    {
        uint64_t cbSize;
        vrc = RTFileQuerySize(hKeyFile, &cbSize);
        if (RT_SUCCESS(vrc))
        {
            if (cbSize <= _32K)
            {
                SafeArray<BYTE> aKey((size_t)cbSize);
                vrc = RTFileRead(hKeyFile, aKey.raw(), (size_t)cbSize, NULL);
                if (RT_SUCCESS(vrc))
                {
                    RTFileClose(hKeyFile);

                    ComPtr<IUefiVariableStore> uefiVarStore;
                    CHECK_ERROR2I_RET(nvramStore, COMGETTER(UefiVariableStore)(uefiVarStore.asOutParam()), RTEXITCODE_FAILURE);
                    if (fPk)
                        CHECK_ERROR2I_RET(uefiVarStore, EnrollPlatformKey(ComSafeArrayAsInParam(aKey), Bstr(pszOwnerUuid).raw()), RTEXITCODE_FAILURE);
                    else
                        CHECK_ERROR2I_RET(uefiVarStore, AddSignatureToMok(ComSafeArrayAsInParam(aKey), Bstr(pszOwnerUuid).raw(), SignatureType_X509), RTEXITCODE_FAILURE);

                    return RTEXITCODE_SUCCESS;
                }
                else
                    RTMsgError(Nvram::tr("Cannot read contents of file \"%s\": %Rrc"), pszKey, vrc);
            }
            else
                RTMsgError(Nvram::tr("File \"%s\" is bigger than 32KByte"), pszKey);
        }
        else
            RTMsgError(Nvram::tr("Cannot get size of file \"%s\": %Rrc"), pszKey, vrc);

        RTFileClose(hKeyFile);
    }
    else
        RTMsgError(Nvram::tr("Cannot open file \"%s\": %Rrc"), pszKey, vrc);

    return RTEXITCODE_FAILURE;
}


/**
 * Handles the 'modifynvram myvm enrollpk' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   nvramStore      Reference to the NVRAM store interface.
 */
static RTEXITCODE handleModifyNvramEnrollPlatformKey(HandlerArg *a, ComPtr<INvramStore> &nvramStore)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* common options */
        { "--platform-key", 'p', RTGETOPT_REQ_STRING },
        { "--owner-uuid",   'f', RTGETOPT_REQ_STRING }
    };

    const char *pszPlatformKey = NULL;
    const char *pszOwnerUuid = NULL;

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc - 2, &a->argv[2], s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'p':
                pszPlatformKey = ValueUnion.psz;
                break;
            case 'f':
                pszOwnerUuid = ValueUnion.psz;
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (!pszPlatformKey)
        return errorSyntax(Nvram::tr("No platform key file path was given to \"enrollpk\""));
    if (!pszOwnerUuid)
        return errorSyntax(Nvram::tr("No owner UUID was given to \"enrollpk\""));

    return handleModifyNvramEnrollPlatformKeyOrMok(pszPlatformKey, pszOwnerUuid, nvramStore, true /* fPk */);
}


/**
 * Handles the 'modifynvram myvm enrollmok' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   nvramStore      Reference to the NVRAM store interface.
 */
static RTEXITCODE handleModifyNvramEnrollMok(HandlerArg *a, ComPtr<INvramStore> &nvramStore)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* common options */
        { "--mok",          'p', RTGETOPT_REQ_STRING },
        { "--owner-uuid",   'f', RTGETOPT_REQ_STRING }
    };

    const char *pszMok = NULL;
    const char *pszOwnerUuid = NULL;

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc - 2, &a->argv[2], s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'p':
                pszMok = ValueUnion.psz;
                break;
            case 'f':
                pszOwnerUuid = ValueUnion.psz;
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (!pszMok)
        return errorSyntax(Nvram::tr("No machine owner key file path was given to \"enrollpk\""));
    if (!pszOwnerUuid)
        return errorSyntax(Nvram::tr("No owner UUID was given to \"enrollpk\""));

    return handleModifyNvramEnrollPlatformKeyOrMok(pszMok, pszOwnerUuid, nvramStore, false /* fPk */);
}


/**
 * Handles the 'modifynvram myvm enrollorclpk' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   nvram           Reference to the NVRAM store interface.
 */
static RTEXITCODE handleModifyNvramEnrollOraclePlatformKey(HandlerArg *a, ComPtr<INvramStore> &nvramStore)
{
    if (a->argc != 2)
        return errorTooManyParameters(&a->argv[1]);

    ComPtr<IUefiVariableStore> uefiVarStore;
    CHECK_ERROR2I_RET(nvramStore, COMGETTER(UefiVariableStore)(uefiVarStore.asOutParam()), RTEXITCODE_FAILURE);

    CHECK_ERROR2I_RET(uefiVarStore, EnrollOraclePlatformKey(), RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the 'modifynvram myvm secureboot' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   nvram           Reference to the NVRAM store interface.
 */
static RTEXITCODE handleModifyNvramSecureBoot(HandlerArg *a, ComPtr<INvramStore> &nvramStore)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* common options */
        { "--enable",       'e', RTGETOPT_REQ_NOTHING },
        { "--disable",      'd', RTGETOPT_REQ_NOTHING }
    };

    int enable = -1;

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc - 2, &a->argv[2], s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'e':   // --enable
                if (enable >= 0)
                    return errorSyntax(Nvram::tr("You can specify either --enable or --disable once."));
                enable = 1;
                break;

            case 'd':   // --disable
                if (enable >= 0)
                    return errorSyntax(Nvram::tr("You can specify either --enable or --disable once."));
                enable = 0;
                break;

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (enable < 0)
        return errorSyntax(Nvram::tr("You have to specify either --enable or --disable."));

    ComPtr<IUefiVariableStore> uefiVarStore;
    CHECK_ERROR2I_RET(nvramStore, COMGETTER(UefiVariableStore)(uefiVarStore.asOutParam()), RTEXITCODE_FAILURE);

    CHECK_ERROR2I_RET(uefiVarStore, COMSETTER(SecureBootEnabled((BOOL)enable)), RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the 'modifynvram myvm listvars' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   nvram           Reference to the NVRAM store interface.
 */
static RTEXITCODE handleModifyNvramListUefiVars(HandlerArg *a, ComPtr<INvramStore> &nvramStore)
{
    if (a->argc != 2)
        return errorTooManyParameters(&a->argv[1]);

    ComPtr<IUefiVariableStore> uefiVarStore;
    CHECK_ERROR2I_RET(nvramStore, COMGETTER(UefiVariableStore)(uefiVarStore.asOutParam()), RTEXITCODE_FAILURE);

    com::SafeArray<BSTR> aNames;
    com::SafeArray<BSTR> aOwnerGuids;
    CHECK_ERROR2I_RET(uefiVarStore, QueryVariables(ComSafeArrayAsOutParam(aNames), ComSafeArrayAsOutParam(aOwnerGuids)), RTEXITCODE_FAILURE);
    for (size_t i = 0; i < aNames.size(); i++)
    {
        Bstr strName      = aNames[i];
        Bstr strOwnerGuid = aOwnerGuids[i];

        RTPrintf("%-32ls {%ls}\n", strName.raw(), strOwnerGuid.raw());
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the 'modifynvram myvm queryvar' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   nvram           Reference to the NVRAM store interface.
 */
static RTEXITCODE handleModifyNvramQueryUefiVar(HandlerArg *a, ComPtr<INvramStore> &nvramStore)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* common options */
        { "--name",       'n', RTGETOPT_REQ_STRING },
        { "--filename",   'f', RTGETOPT_REQ_STRING }
    };

    const char *pszVarName = NULL;
    const char *pszVarDataFilename = NULL;

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc - 2, &a->argv[2], s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'n':
                pszVarName = ValueUnion.psz;
                break;
            case 'f':
                pszVarDataFilename = ValueUnion.psz;
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (!pszVarName)
        return errorSyntax(Nvram::tr("No variable name was given to \"queryvar\""));

    ComPtr<IUefiVariableStore> uefiVarStore;
    CHECK_ERROR2I_RET(nvramStore, COMGETTER(UefiVariableStore)(uefiVarStore.asOutParam()), RTEXITCODE_FAILURE);

    Bstr strOwnerGuid;
    com::SafeArray<UefiVariableAttributes_T> aVarAttrs;
    com::SafeArray<BYTE> aData;
    CHECK_ERROR2I_RET(uefiVarStore, QueryVariableByName(Bstr(pszVarName).raw(), strOwnerGuid.asOutParam(),
                                                        ComSafeArrayAsOutParam(aVarAttrs), ComSafeArrayAsOutParam(aData)),
                      RTEXITCODE_FAILURE);

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    if (!pszVarDataFilename)
    {
        RTPrintf("%s {%ls}:\n"
                 "%.*Rhxd\n", pszVarName, strOwnerGuid.raw(), aData.size(), aData.raw());
    }
    else
    {
        /* Just write the data to the file. */
        RTFILE hFile = NIL_RTFILE;
        vrc = RTFileOpen(&hFile, pszVarDataFilename, RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTFileWrite(hFile, aData.raw(), aData.size(), NULL /*pcbWritten*/);
            if (RT_FAILURE(vrc))
                rcExit = RTMsgErrorExitFailure(Nvram::tr("Error writing to '%s': %Rrc"), pszVarDataFilename, vrc);

            RTFileClose(hFile);
        }
        else
            rcExit = RTMsgErrorExitFailure(Nvram::tr("Error opening '%s': %Rrc"), pszVarDataFilename, vrc);
    }

    return rcExit;
}


/**
 * Handles the 'modifynvram myvm deletevar' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   nvram           Reference to the NVRAM store interface.
 */
static RTEXITCODE handleModifyNvramDeleteUefiVar(HandlerArg *a, ComPtr<INvramStore> &nvramStore)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* common options */
        { "--name",       'n', RTGETOPT_REQ_STRING },
        { "--owner-uuid", 'f', RTGETOPT_REQ_STRING }
    };

    const char *pszVarName = NULL;
    const char *pszOwnerUuid = NULL;

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc - 2, &a->argv[2], s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'n':
                pszVarName = ValueUnion.psz;
                break;
            case 'f':
                pszOwnerUuid = ValueUnion.psz;
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (!pszVarName)
        return errorSyntax(Nvram::tr("No variable name was given to \"deletevar\""));
    if (!pszOwnerUuid)
        return errorSyntax(Nvram::tr("No owner UUID was given to \"deletevar\""));

    ComPtr<IUefiVariableStore> uefiVarStore;
    CHECK_ERROR2I_RET(nvramStore, COMGETTER(UefiVariableStore)(uefiVarStore.asOutParam()), RTEXITCODE_FAILURE);
    CHECK_ERROR2I_RET(uefiVarStore, DeleteVariable(Bstr(pszVarName).raw(), Bstr(pszOwnerUuid).raw()), RTEXITCODE_FAILURE);

    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the 'modifynvram myvm changevar' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   nvram           Reference to the NVRAM store interface.
 */
static RTEXITCODE handleModifyNvramChangeUefiVar(HandlerArg *a, ComPtr<INvramStore> &nvramStore)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* common options */
        { "--name",       'n', RTGETOPT_REQ_STRING },
        { "--filename",   'f', RTGETOPT_REQ_STRING }
    };

    const char *pszVarName = NULL;
    const char *pszVarDataFilename = NULL;

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc - 2, &a->argv[2], s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'n':
                pszVarName = ValueUnion.psz;
                break;
            case 'f':
                pszVarDataFilename = ValueUnion.psz;
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (!pszVarName)
        return errorSyntax(Nvram::tr("No variable name was given to \"changevar\""));
    if (!pszVarDataFilename)
        return errorSyntax(Nvram::tr("No variable data filename was given to \"changevar\""));

    RTFILE hFile = NIL_RTFILE;
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    vrc = RTFileOpen(&hFile, pszVarDataFilename, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(vrc))
    {
        uint64_t cbFile = 0;
        vrc = RTFileQuerySize(hFile, &cbFile);
        if (RT_SUCCESS(vrc))
        {
            com::SafeArray<BYTE> aData;
            aData.resize(cbFile);

            vrc = RTFileRead(hFile, aData.raw(), aData.size(), NULL /*pcbRead*/);
            RTFileClose(hFile);

            if (RT_SUCCESS(vrc))
            {
                ComPtr<IUefiVariableStore> uefiVarStore;
                CHECK_ERROR2I_RET(nvramStore, COMGETTER(UefiVariableStore)(uefiVarStore.asOutParam()), RTEXITCODE_FAILURE);
                CHECK_ERROR2I_RET(uefiVarStore, ChangeVariable(Bstr(pszVarName).raw(), ComSafeArrayAsInParam(aData)), RTEXITCODE_FAILURE);
            }
            else
                rcExit = RTMsgErrorExitFailure(Nvram::tr("Error reading from '%s': %Rrc"), pszVarDataFilename, vrc);
        }
    }
    else
        rcExit = RTMsgErrorExitFailure(Nvram::tr("Error opening '%s': %Rrc"), pszVarDataFilename, vrc);

    return rcExit;
}


/**
 * Handles the 'modifynvram' command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 */
RTEXITCODE handleModifyNvram(HandlerArg *a)
{
    HRESULT hrc = S_OK;
    ComPtr<IMachine> machine;
    ComPtr<INvramStore> nvramStore;

    if (a->argc < 2)
        return errorNoSubcommand();

    /* try to find the given machine */
    CHECK_ERROR_RET(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                               machine.asOutParam()), RTEXITCODE_FAILURE);

    /* open a session for the VM (new or shared) */
    CHECK_ERROR_RET(machine, LockMachine(a->session, LockType_Write), RTEXITCODE_FAILURE);

    /* get the mutable session machine */
    a->session->COMGETTER(Machine)(machine.asOutParam());
    hrc = machine->COMGETTER(NonVolatileStore)(nvramStore.asOutParam());
    if (FAILED(hrc)) goto leave;

    if (!strcmp(a->argv[1], "inituefivarstore"))
    {
        setCurrentSubcommand(HELP_SCOPE_MODIFYNVRAM_INITUEFIVARSTORE);
        hrc = handleModifyNvramInitUefiVarStore(a, nvramStore) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else if (!strcmp(a->argv[1], "enrollmssignatures"))
    {
        setCurrentSubcommand(HELP_SCOPE_MODIFYNVRAM_ENROLLMSSIGNATURES);
        hrc = handleModifyNvramEnrollMsSignatures(a, nvramStore) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else if (!strcmp(a->argv[1], "enrollpk"))
    {
        setCurrentSubcommand(HELP_SCOPE_MODIFYNVRAM_ENROLLPK);
        hrc = handleModifyNvramEnrollPlatformKey(a, nvramStore) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else if (!strcmp(a->argv[1], "enrollmok"))
    {
        setCurrentSubcommand(HELP_SCOPE_MODIFYNVRAM_ENROLLMOK);
        hrc = handleModifyNvramEnrollMok(a, nvramStore) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else if (!strcmp(a->argv[1], "enrollorclpk"))
    {
        setCurrentSubcommand(HELP_SCOPE_MODIFYNVRAM_ENROLLORCLPK);
        hrc = handleModifyNvramEnrollOraclePlatformKey(a, nvramStore) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else if (!strcmp(a->argv[1], "secureboot"))
    {
        setCurrentSubcommand(HELP_SCOPE_MODIFYNVRAM_SECUREBOOT);
        hrc = handleModifyNvramSecureBoot(a, nvramStore) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else if (!strcmp(a->argv[1], "listvars"))
    {
        setCurrentSubcommand(HELP_SCOPE_MODIFYNVRAM_LISTVARS);
        hrc = handleModifyNvramListUefiVars(a, nvramStore) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else if (!strcmp(a->argv[1], "queryvar"))
    {
        setCurrentSubcommand(HELP_SCOPE_MODIFYNVRAM_QUERYVAR);
        hrc = handleModifyNvramQueryUefiVar(a, nvramStore) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else if (!strcmp(a->argv[1], "deletevar"))
    {
        setCurrentSubcommand(HELP_SCOPE_MODIFYNVRAM_DELETEVAR);
        hrc = handleModifyNvramDeleteUefiVar(a, nvramStore) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else if (!strcmp(a->argv[1], "changevar"))
    {
        setCurrentSubcommand(HELP_SCOPE_MODIFYNVRAM_CHANGEVAR);
        hrc = handleModifyNvramChangeUefiVar(a, nvramStore) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else
        return errorUnknownSubcommand(a->argv[1]);

    /* commit changes */
    if (SUCCEEDED(hrc))
        CHECK_ERROR(machine, SaveSettings());

leave:
    /* it's important to always close sessions */
    a->session->UnlockMachine();

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}
