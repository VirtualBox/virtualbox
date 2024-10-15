/* $Id: RTEfiSigDb.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Utility for manipulating EFI signature databases.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/err.h>
#include <iprt/efi.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/vfs.h>

#include <iprt/formats/efi-signature.h>
#include <iprt/formats/efi-varstore.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Signature type identifier to internal type mapping. */
struct RTEFISIGDBID2TYPEENTRY
{
    const char   *pszId;
    RTEFISIGTYPE enmType;
} g_aId2SigType[] =
{
    { "sha256",  RTEFISIGTYPE_SHA256  },
    { "rsa2048", RTEFISIGTYPE_RSA2048 },
    { "x509",    RTEFISIGTYPE_X509    }
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Display the version of the cache program.
 *
 * @returns exit code.
 */
static RTEXITCODE rtEfiSigDbVersion(void)
{
    RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
    return RTEXITCODE_SUCCESS;
}


/**
 * Shows the usage of the program.
 *
 * @returns Exit code.
 * @param   pszArg0             Program name.
 * @param   pszCommand          Command selector, NULL if all.
 */
static RTEXITCODE rtEfiSigDbUsage(const char *pszArg0, const char *pszCommand)
{
    if (!pszCommand || !strcmp(pszCommand, "list"))
        RTPrintf("Usage: %s list <signature database path>\n"
                 , RTPathFilename(pszArg0));

    if (!pszCommand || !strcmp(pszCommand, "add"))
        RTPrintf("Usage: %s add <signature database path> <x509|sha256|rsa2048> <owner uuid> <signature path> ...\n"
                 , RTPathFilename(pszArg0));

    if (!pszCommand || !strcmp(pszCommand, "initnvram"))
        RTPrintf("Usage: %s initnvram <nvram path> <init options>\n"
                 "\n"
                 "Init Options:\n"
                 "  --pk <path>\n"
                 "      Init the PK with the given signature.\n"
                 "  --pk-owner <uuid>\n"
                 "      Set the given UUID as the owner of the PK.\n"
                 "  --kek <path>\n"
                 "      Init the KEK with the given signature.\n"
                 "  --kek-owner <uuid>\n"
                 "      Set the given UUID as the owner of the KEK.\n"
                 "  --db <x509|sha256|rsa2048>:<owner uuid>:<path>\n"
                 "      Adds the given signature with the owner UUID and type to the db, can be given multiple times.\n"
                 "  --secure-boot <on|off>\n"
                 "      Enables or disables secure boot\n"
                 , RTPathFilename(pszArg0));

    return RTEXITCODE_SUCCESS;
}


static RTEFISIGTYPE rtEfiSigDbGetTypeById(const char *pszId)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aId2SigType); i++)
        if (!strcmp(pszId, g_aId2SigType[i].pszId))
            return g_aId2SigType[i].enmType;

    return RTEFISIGTYPE_INVALID;
}


/**
 * Opens the specified signature database, returning an VFS file handle on success.
 *
 * @returns IPRT status code.
 * @param   pszPath             Path to the signature database.
 * @param   phVfsFile           Where to return the VFS file handle on success.
 */
static int rtEfiSigDbOpen(const char *pszPath, PRTVFSFILE phVfsFile)
{
    int rc;

    if (RTVfsChainIsSpec(pszPath))
    {
        RTVFSOBJ hVfsObj;
        rc = RTVfsChainOpenObj(pszPath, RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                               RTVFSOBJ_F_OPEN_ANY | RTVFSOBJ_F_CREATE_NOTHING | RTPATH_F_ON_LINK,
                               &hVfsObj, NULL, NULL);
        if (   RT_SUCCESS(rc)
            && RTVfsObjGetType(hVfsObj) == RTVFSOBJTYPE_FILE)
        {
            *phVfsFile = RTVfsObjToFile(hVfsObj);
            RTVfsObjRelease(hVfsObj);
        }
        else
        {
            RTPrintf("'%s' doesn't point to a file\n", pszPath);
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else
        rc = RTVfsFileOpenNormal(pszPath, RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                 phVfsFile);

    return rc;
}


/**
 * Signature database enumeration callback.
 */
static DECLCALLBACK(int) rtEfiSgDbEnum(RTEFISIGDB hEfiSigDb, RTEFISIGTYPE enmSigType, PCRTUUID pUuidOwner,
                                       const void *pvSig, size_t cbSig, void *pvUser)
{
    RT_NOREF(hEfiSigDb, pvUser);

    uint32_t *pidxSig = (uint32_t *)pvUser;

    RTPrintf("%02u: %s\n", (*pidxSig)++, RTEfiSigDbTypeStringify(enmSigType));
    RTPrintf("    Owner: %RTuuid\n", pUuidOwner);
    RTPrintf("    Signature:\n"
             "%.*Rhxd\n\n", cbSig, pvSig);
    return VINF_SUCCESS;
}


/**
 * Handles the 'list' command.
 *
 * @returns Program exit code.
 * @param   pszArg0             The program name.
 * @param   cArgs               The number of arguments to the 'add' command.
 * @param   papszArgs           The argument vector, starting after 'add'.
 */
static RTEXITCODE rtEfiSgDbCmdList(const char *pszArg0, int cArgs, char **papszArgs)
{
    RT_NOREF(pszArg0);

    if (!cArgs)
    {
        RTPrintf("An input path must be given\n");
        return RTEXITCODE_FAILURE;
    }

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    RTVFSFILE hVfsFile = NIL_RTVFSFILE;
    int rc = rtEfiSigDbOpen(papszArgs[0], &hVfsFile);
    if (RT_SUCCESS(rc))
    {
        RTEFISIGDB hEfiSigDb;
        rc = RTEfiSigDbCreate(&hEfiSigDb);
        if (RT_SUCCESS(rc))
        {
            uint32_t idxSig = 0;

            rc = RTEfiSigDbAddFromExistingDb(hEfiSigDb, hVfsFile);
            if (RT_SUCCESS(rc))
                RTEfiSigDbEnum(hEfiSigDb, rtEfiSgDbEnum, &idxSig);
            else
            {
                RTPrintf("Loading the signature database failed with %Rrc\n", rc);
                rcExit = RTEXITCODE_FAILURE;
            }

            RTEfiSigDbDestroy(hEfiSigDb);
        }
        else
        {
            RTPrintf("Creating the signature database failed with %Rrc\n", rc);
            rcExit = RTEXITCODE_FAILURE;
        }

        RTVfsFileRelease(hVfsFile);
    }
    else
        rcExit = RTEXITCODE_FAILURE;

    return rcExit;
}


/**
 * Handles the 'add' command.
 *
 * @returns Program exit code.
 * @param   pszArg0             The program name.
 * @param   cArgs               The number of arguments to the 'add' command.
 * @param   papszArgs           The argument vector, starting after 'add'.
 */
static RTEXITCODE rtEfiSgDbCmdAdd(const char *pszArg0, int cArgs, char **papszArgs)
{
    RT_NOREF(pszArg0);

    if (!cArgs)
    {
        RTPrintf("The signature database path is missing\n");
        return RTEXITCODE_FAILURE;
    }

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    RTVFSFILE hVfsFile = NIL_RTVFSFILE;
    int rc = rtEfiSigDbOpen(papszArgs[0], &hVfsFile);
    if (RT_SUCCESS(rc))
    {
        RTEFISIGDB hEfiSigDb;
        rc = RTEfiSigDbCreate(&hEfiSigDb);
        if (RT_SUCCESS(rc))
        {
            uint64_t cbSigDb = 0;
            rc = RTVfsFileQuerySize(hVfsFile, &cbSigDb);
            if (   RT_SUCCESS(rc)
                && cbSigDb)
                rc = RTEfiSigDbAddFromExistingDb(hEfiSigDb, hVfsFile);
            if (RT_SUCCESS(rc))
            {
                cArgs--;
                papszArgs++;

                while (cArgs >= 3)
                {
                    RTEFISIGTYPE enmSigType    = rtEfiSigDbGetTypeById(papszArgs[0]);
                    const char *pszUuidOwner   = papszArgs[1];
                    const char *pszSigDataPath = papszArgs[2];

                    if (enmSigType == RTEFISIGTYPE_INVALID)
                    {
                        RTPrintf("Signature type '%s' is not known\n", papszArgs[0]);
                        break;
                    }

                    RTUUID UuidOwner;
                    rc = RTUuidFromStr(&UuidOwner, pszUuidOwner);
                    if (RT_FAILURE(rc))
                    {
                        RTPrintf("UUID '%s' is malformed\n", pszUuidOwner);
                        break;
                    }

                    RTVFSFILE hVfsFileSigData = NIL_RTVFSFILE;
                    rc = rtEfiSigDbOpen(pszSigDataPath, &hVfsFileSigData);
                    if (RT_FAILURE(rc))
                    {
                        RTPrintf("Opening '%s' failed with %Rrc\n", pszSigDataPath, rc);
                        break;
                    }

                    rc = RTEfiSigDbAddSignatureFromFile(hEfiSigDb, enmSigType, &UuidOwner, hVfsFileSigData);
                    RTVfsFileRelease(hVfsFileSigData);
                    if (RT_FAILURE(rc))
                    {
                        RTPrintf("Adding signature data from '%s' failed with %Rrc\n", pszSigDataPath, rc);
                        break;
                    }
                    papszArgs += 3;
                    cArgs     -= 3;
                }

                if (RT_SUCCESS(rc))
                {
                    if (!cArgs)
                    {
                        rc = RTVfsFileSeek(hVfsFile, 0 /*offSeek*/, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
                        AssertRC(rc);

                        rc = RTEfiSigDbWriteToFile(hEfiSigDb, hVfsFile);
                        if (RT_FAILURE(rc))
                        {
                            RTPrintf("Writing the updated signature database failed with %Rrc\n", rc);
                            rcExit = RTEXITCODE_FAILURE;
                        }
                    }
                    else
                    {
                        RTPrintf("Incomplete list of entries to add given\n");
                        rcExit = RTEXITCODE_FAILURE;
                    }
                }
            }
            else
            {
                RTPrintf("Loading the signature database failed with %Rrc\n", rc);
                rcExit = RTEXITCODE_FAILURE;
            }

            RTEfiSigDbDestroy(hEfiSigDb);
        }
        else
        {
            RTPrintf("Creating the signature database failed with %Rrc\n", rc);
            rcExit = RTEXITCODE_FAILURE;
        }

        RTVfsFileRelease(hVfsFile);
    }
    else
        rcExit = RTEXITCODE_FAILURE;

    return rcExit;
}


/**
 * Adds the given signature to the given database.
 *
 * @returns IPRT status code.
 * @param   hEfiSigDb           The EFI signature database handle.
 * @param   pszSigPath          The signature data path.
 * @param   pszSigType          The signature type.
 * @param   pszUuidOwner        The owner UUID.
 */
static int rtEfiSigDbAddSig(RTEFISIGDB hEfiSigDb, const char *pszSigPath, const char *pszSigType, const char *pszUuidOwner)
{
    RTEFISIGTYPE enmSigType    = rtEfiSigDbGetTypeById(pszSigType);
    if (enmSigType == RTEFISIGTYPE_INVALID)
        return RTMsgErrorRc(VERR_INVALID_PARAMETER, "Signature type '%s' is unknown!", pszSigType);

    RTUUID UuidOwner;
    int rc = RTUuidFromStr(&UuidOwner, pszUuidOwner);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(VERR_INVALID_PARAMETER, "Owner UUID '%s' is malformed!", pszUuidOwner);

    RTVFSFILE hVfsFileSigData = NIL_RTVFSFILE;
    rc = rtEfiSigDbOpen(pszSigPath, &hVfsFileSigData);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "Opening '%s' failed: %Rrc", pszSigPath, rc);

    rc = RTEfiSigDbAddSignatureFromFile(hEfiSigDb, enmSigType, &UuidOwner, hVfsFileSigData);
    RTVfsFileRelease(hVfsFileSigData);
    if (RT_FAILURE(rc))
        return RTMsgErrorRc(rc, "Adding signature '%s' failed: %Rrc", pszSigPath, rc);

    return VINF_SUCCESS;
}


/**
 * Sets the given attributes for the given EFI variable store variable.
 *
 * @returns IPRT status code.
 * @param   hVfsVarStore        Handle of the EFI variable store VFS.
 * @param   pszVar              The variable to set the attributes for.
 * @param   fAttr               The attributes to set, see EFI_VAR_HEADER_ATTR_XXX.
 */
static int rtEfiSigDbSetVarAttr(RTVFS hVfsVarStore, const char *pszVar, uint32_t fAttr)
{
    char szVarPath[_1K];
    ssize_t cch = RTStrPrintf2(szVarPath, sizeof(szVarPath), "/raw/%s/attr", pszVar);
    Assert(cch > 0); RT_NOREF(cch);

    RTVFSFILE hVfsFileAttr = NIL_RTVFSFILE;
    int rc = RTVfsFileOpen(hVfsVarStore, szVarPath,
                           RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                           &hVfsFileAttr);
    if (RT_SUCCESS(rc))
    {
        uint32_t fAttrLe = RT_H2LE_U32(fAttr);
        rc = RTVfsFileWrite(hVfsFileAttr, &fAttrLe, sizeof(fAttrLe), NULL /*pcbWritten*/);
        RTVfsFileRelease(hVfsFileAttr);
    }

    return rc;
}


/**
 * Adds the given variable to the variable store.
 *
 * @returns IPRT status code.
 * @param   hVfsVarStore        Handle of the EFI variable store VFS.
 * @param   pGuid               The EFI GUID of the variable.
 * @param   pszVar              The variable name.
 * @param   fAttr               Attributes for the variable.
 * @param   phVfsFile           Where to return the VFS file handle to the created variable on success.
 */
static int rtEfiSigDbVarStoreAddVar(RTVFS hVfsVarStore, PCEFI_GUID pGuid, const char *pszVar, uint32_t fAttr, PRTVFSFILE phVfsFile)
{
    RTUUID UuidVar;
    RTEfiGuidToUuid(&UuidVar, pGuid);

    char szVarPath[_1K];
    ssize_t cch = RTStrPrintf2(szVarPath, sizeof(szVarPath), "/by-uuid/%RTuuid/%s", &UuidVar, pszVar);
    Assert(cch > 0); RT_NOREF(cch);

    int rc = RTVfsFileOpen(hVfsVarStore, szVarPath,
                           RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                           phVfsFile);
    if (   rc == VERR_PATH_NOT_FOUND
        || rc == VERR_FILE_NOT_FOUND)
    {
        /*
         * Try to create the owner GUID of the variable by creating the appropriate directory,
         * ignore error if it exists already.
         */
        RTVFSDIR hVfsDirRoot = NIL_RTVFSDIR;
        rc = RTVfsOpenRoot(hVfsVarStore, &hVfsDirRoot);
        if (RT_SUCCESS(rc))
        {
            char szGuidPath[_1K];
            cch = RTStrPrintf2(szGuidPath, sizeof(szGuidPath), "by-uuid/%RTuuid", &UuidVar);
            Assert(cch > 0);

            RTVFSDIR hVfsDirGuid = NIL_RTVFSDIR;
            rc = RTVfsDirCreateDir(hVfsDirRoot, szGuidPath, 0755, 0 /*fFlags*/, &hVfsDirGuid);
            if (RT_SUCCESS(rc))
                RTVfsDirRelease(hVfsDirGuid);
            else if (rc == VERR_ALREADY_EXISTS)
                rc = VINF_SUCCESS;

            RTVfsDirRelease(hVfsDirRoot);
        }
        else
            rc = RTMsgErrorRc(rc, "Opening variable storage root directory failed: %Rrc", rc);

        if (RT_SUCCESS(rc))
        {
            rc = RTVfsFileOpen(hVfsVarStore, szVarPath,
                               RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_CREATE,
                               phVfsFile);
            if (RT_SUCCESS(rc))
                rc = rtEfiSigDbSetVarAttr(hVfsVarStore, pszVar, fAttr);
        }

        if (RT_FAILURE(rc))
            rc = RTMsgErrorRc(rc, "Creating the variable '%s' failed: %Rrc", pszVar, rc);
    }

    return rc;
}


/**
 * Creates the given variable and sets the data.
 *
 * @returns IPRT status code.
 * @param   hVfsVarStore        Handle of the EFI variable store VFS.
 * @param   pGuid               The EFI GUID of the variable.
 * @param   pszVar              The variable name.
 * @param   fAttr               Attributes for the variable.
 * @param   pvBuf               The data to write.
 * @param   cbBuf               Number of bytes of data.
 */
static int rtEfiSigDbVarStoreSetVar(RTVFS hVfsVarStore, PCEFI_GUID pGuid, const char *pszVar, uint32_t fAttr,
                                    const void *pvBuf, size_t cbBuf)
{
    RTVFSFILE hVfsFileVar = NIL_RTVFSFILE;
    int rc = rtEfiSigDbVarStoreAddVar(hVfsVarStore, pGuid, pszVar, fAttr, &hVfsFileVar);
    if (RT_SUCCESS(rc))
    {
        rc = RTVfsFileWrite(hVfsFileVar, pvBuf, cbBuf, NULL /*pcbWritten*/);
        if (RT_FAILURE(rc))
            rc = RTMsgErrorRc(rc, "Writing variable '%s' failed: %Rrc", pszVar, rc);
        RTVfsFileRelease(hVfsFileVar);
    }
    else
        rc = RTMsgErrorRc(rc, "Creating variable '%s' failed: %Rrc", pszVar, rc);

    return rc;
}


/**
 * Adds the given signature to the given signature database of the given EFI variable store.
 *
 * @returns IPRT status code.
 * @param   hVfsVarStore        Handle of the EFI variable store VFS.
 * @param   pGuid               The EFI GUID of the variable.
 * @param   pszDb               The signature database to update.
 * @param   fWipeDbBefore       Flag whether to wipe the database before adding the signature.
 * @param   cSigs               Number of signatures following.
 * @param   ...                 A triple of signature path, signature type and owner uuid string pointers for each
 *                              signature.
 */
static int rtEfiSigDbVarStoreAddToDb(RTVFS hVfsVarStore, PCEFI_GUID pGuid, const char *pszDb, bool fWipeDbBefore, uint32_t cSigs,
                                     ... /*const char *pszSigPath, const char *pszSigType, const char *pszUuidOwner*/)
{
    RTVFSFILE hVfsFileSigDb = NIL_RTVFSFILE;
    int rc = rtEfiSigDbVarStoreAddVar(hVfsVarStore, pGuid, pszDb,
                                        EFI_VAR_HEADER_ATTR_NON_VOLATILE
                                      | EFI_VAR_HEADER_ATTR_BOOTSERVICE_ACCESS
                                      | EFI_VAR_HEADER_ATTR_RUNTIME_ACCESS
                                      | EFI_AUTH_VAR_HEADER_ATTR_TIME_BASED_AUTH_WRITE_ACCESS,
                                      &hVfsFileSigDb);
    if (RT_SUCCESS(rc))
    {
        RTEFISIGDB hEfiSigDb;
        rc = RTEfiSigDbCreate(&hEfiSigDb);
        if (RT_SUCCESS(rc))
        {
            if (!fWipeDbBefore)
                rc = RTEfiSigDbAddFromExistingDb(hEfiSigDb, hVfsFileSigDb);
            if (RT_SUCCESS(rc))
            {
                va_list VarArgs;
                va_start(VarArgs, cSigs);

                while (   cSigs--
                       && RT_SUCCESS(rc))
                {
                    const char *pszSigPath   = va_arg(VarArgs, const char *);
                    const char *pszSigType   = va_arg(VarArgs, const char *);
                    const char *pszUuidOwner = va_arg(VarArgs, const char *);

                    rc = rtEfiSigDbAddSig(hEfiSigDb, pszSigPath, pszSigType, pszUuidOwner);
                }

                va_end(VarArgs);
                if (RT_SUCCESS(rc))
                {
                    rc = RTVfsFileSeek(hVfsFileSigDb, 0 /*offSeek*/, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
                    AssertRC(rc);

                    rc = RTEfiSigDbWriteToFile(hEfiSigDb, hVfsFileSigDb);
                    if (RT_FAILURE(rc))
                        rc = RTMsgErrorRc(rc, "Writing updated signature database failed: %Rrc", rc);
                }
            }
            else
                rc = RTMsgErrorRc(rc, "Loading signature database failed: %Rrc", rc);

            RTEfiSigDbDestroy(hEfiSigDb);
        }
        else
            rc = RTMsgErrorRc(rc, "Creating signature database failed: %Rrc", rc);

        RTVfsFileRelease(hVfsFileSigDb);
    }
    else
        rc = RTMsgErrorRc(rc, "Opening signature database '%s' failed: %Rrc", pszDb, rc);

    return rc;
}


/**
 * Handles the 'initnvram' command.
 *
 * @returns Program exit code.
 * @param   pszArg0             The program name.
 * @param   cArgs               The number of arguments to the 'add' command.
 * @param   papszArgs           The argument vector, starting after 'add'.
 */
static RTEXITCODE rtEfiSgDbCmdInitNvram(const char *pszArg0, int cArgs, char **papszArgs)
{
    RT_NOREF(pszArg0);
    RTERRINFOSTATIC ErrInfo;

    /*
     * Parse the command line.
     */
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--pk",                       'p', RTGETOPT_REQ_STRING     },
        { "--pk-owner",                 'o', RTGETOPT_REQ_STRING     },
        { "--kek",                      'k', RTGETOPT_REQ_STRING     },
        { "--kek-owner",                'w', RTGETOPT_REQ_STRING     },
        { "--db",                       'd', RTGETOPT_REQ_STRING     },
        { "--secure-boot",              's', RTGETOPT_REQ_BOOL_ONOFF }
    };

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    RTGETOPTSTATE State;
    int rc = RTGetOptInit(&State, cArgs, papszArgs, &s_aOptions[0], RT_ELEMENTS(s_aOptions), 0,  RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptInit failed: %Rrc", rc);

    const char *pszNvram        = NULL;
    const char *pszPkPath       = NULL;
    const char *pszUuidPkOwner  = NULL;
    const char *pszKekPath      = NULL;
    const char *pszUuidKekOwner = NULL;
    const char **papszDb        = NULL;
    bool       fSecureBoot      = true;
    bool       fSetSecureBoot   = false;
    uint32_t   cDbEntries       = 0;
    uint32_t   cDbEntriesMax    = 0;

    RTGETOPTUNION   ValueUnion;
    int             chOpt;
    while ((chOpt = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (chOpt)
        {
            case 'p':
                pszPkPath = ValueUnion.psz;
                break;
            case 'o':
                pszUuidPkOwner = ValueUnion.psz;
                break;

            case 'k':
                pszKekPath = ValueUnion.psz;
                break;
            case 'w':
                pszUuidKekOwner = ValueUnion.psz;
                break;

            case 'd':
            {
                if (cDbEntries == cDbEntriesMax)
                {
                    uint32_t cDbEntriesMaxNew = cDbEntriesMax + 10;
                    const char **papszDbNew = (const char **)RTMemRealloc(papszDb, cDbEntriesMaxNew * sizeof(const char *));
                    if (!papszDbNew)
                        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Out of memory allocating memory for '%s'", ValueUnion.psz);

                    papszDb       = papszDbNew;
                    cDbEntriesMax = cDbEntriesMaxNew;
                }

                papszDb[cDbEntries++] = ValueUnion.psz;
                break;
            }

            case 's':
                fSecureBoot = ValueUnion.f;
                fSetSecureBoot = true;
                break;

            case VINF_GETOPT_NOT_OPTION:
                /* The first non-option is the NVRAM file. */
                if (!pszNvram)
                    pszNvram = ValueUnion.psz;
                else
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid option '%s'", ValueUnion.psz);
                break;

            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }

    if (!pszNvram)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "The NVRAM file path is missing");

    if (   pszPkPath
        && !pszUuidPkOwner)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "The PK is missing the owner UUID");

    if (   pszKekPath
        && !pszUuidKekOwner)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "The KEK is missing the owner UUID");

    RTVFSFILE hVfsFileNvram = NIL_RTVFSFILE;
    rc = RTVfsFileOpenNormal(pszNvram, RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                             &hVfsFileNvram);
    if (RT_SUCCESS(rc))
    {
        RTVFS hVfsEfiVarStore = NIL_RTVFS;
        rc = RTEfiVarStoreOpenAsVfs(hVfsFileNvram, 0 /*fMntFlags*/, 0 /*fVarStoreFlags*/, &hVfsEfiVarStore, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            EFI_GUID GuidSecurityDb = EFI_IMAGE_SECURITY_DATABASE_GUID;
            EFI_GUID GuidGlobalVar = EFI_GLOBAL_VARIABLE_GUID;

            if (pszPkPath)
                rc = rtEfiSigDbVarStoreAddToDb(hVfsEfiVarStore, &GuidGlobalVar, "PK", true /*fWipeDbBefore*/, 1 /*cSigs*/, pszPkPath, "x509", pszUuidPkOwner);
            if (   RT_SUCCESS(rc)
                && pszKekPath)
                rc = rtEfiSigDbVarStoreAddToDb(hVfsEfiVarStore, &GuidGlobalVar ,"KEK", true /*fWipeDbBefore*/, 1 /*cSigs*/, pszKekPath, "x509", pszUuidKekOwner);

            if (   RT_SUCCESS(rc)
                && cDbEntries)
            {
                /** @todo Optimize to avoid re-opening and re-parsing the database for every entry. */
                for (uint32_t i = 0; i < cDbEntries && RT_SUCCESS(rc); i++)
                {
                    const char *pszDbEntry = papszDb[i];

                    const char *pszSigType   = pszDbEntry;
                    const char *pszUuidOwner = strchr(pszSigType, ':');
                    if (pszUuidOwner)
                        pszUuidOwner++;
                    const char *pszSigPath   = pszUuidOwner ? strchr(pszUuidOwner, ':') : NULL;
                    if (pszSigPath)
                        pszSigPath++;

                    if (   pszUuidOwner
                        && pszSigPath)
                    {
                        char *pszSigTypeFree   = RTStrDupN(pszSigType, pszUuidOwner - pszSigType - 1);
                        char *pszUuidOwnerFree = RTStrDupN(pszUuidOwner, pszSigPath - pszUuidOwner - 1);

                        if (   pszSigTypeFree
                            && pszUuidOwnerFree)
                            rc = rtEfiSigDbVarStoreAddToDb(hVfsEfiVarStore, &GuidSecurityDb, "db",
                                                           i == 0 ? true : false /*fWipeDbBefore*/,
                                                           1 /*cSigs*/,
                                                           pszSigPath, pszSigTypeFree, pszUuidOwnerFree);
                        else
                            rc = RTMsgErrorRc(VERR_NO_MEMORY, "Out of memory!");

                        if (pszSigTypeFree)
                            RTStrFree(pszSigTypeFree);
                        if (pszUuidOwnerFree)
                            RTStrFree(pszUuidOwnerFree);
                    }
                    else
                        rc = RTMsgErrorRc(VERR_INVALID_PARAMETER, "DB entry '%s' is malformed!", pszDbEntry);
                }

                if (RT_FAILURE(rc))
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Initializing the NVRAM '%s' failed: %Rrc", pszNvram, rc);
            }

            if (   RT_SUCCESS(rc)
                && fSetSecureBoot)
            {
                EFI_GUID GuidSecureBootEnable = EFI_SECURE_BOOT_ENABLE_DISABLE_GUID;
                uint8_t bVar = fSecureBoot ? 0x1 : 0x0;
                rtEfiSigDbVarStoreSetVar(hVfsEfiVarStore, &GuidSecureBootEnable, "SecureBootEnable",
                                           EFI_VAR_HEADER_ATTR_NON_VOLATILE
                                         | EFI_VAR_HEADER_ATTR_BOOTSERVICE_ACCESS
                                         | EFI_VAR_HEADER_ATTR_RUNTIME_ACCESS,
                                         &bVar, sizeof(bVar));
            }

            RTVfsRelease(hVfsEfiVarStore);
        }

        RTVfsFileRelease(hVfsFileNvram);
    }

    if (papszDb)
        RTMemFree(papszDb);
    return rcExit;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Switch on the command.
     */
    RTEXITCODE rcExit = RTEXITCODE_SYNTAX;
    if (argc < 2)
        rtEfiSigDbUsage(argv[0], NULL);
    else if (!strcmp(argv[1], "list"))
        rcExit = rtEfiSgDbCmdList(argv[0], argc - 2, argv + 2);
    else if (!strcmp(argv[1], "add"))
        rcExit = rtEfiSgDbCmdAdd(argv[0], argc - 2, argv + 2);
    else if (!strcmp(argv[1], "initnvram"))
        rcExit = rtEfiSgDbCmdInitNvram(argv[0], argc - 2, argv + 2);
    else if (   !strcmp(argv[1], "-h")
             || !strcmp(argv[1], "-?")
             || !strcmp(argv[1], "--help"))
        rcExit = rtEfiSigDbUsage(argv[0], NULL);
    else if (   !strcmp(argv[1], "-V")
             || !strcmp(argv[1], "--version"))
        rcExit = rtEfiSigDbVersion();
    else
        RTMsgError("Unknown command: '%s'", argv[1]);

    return rcExit;
}

