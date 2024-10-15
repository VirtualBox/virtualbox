/* $Id: fuzz-config.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Fuzzing framework API, config API.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include <iprt/fuzz.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/json.h>
#include <iprt/vfs.h>
#include <iprt/zip.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The index filename used to get all the other content. */
#define RTFUZZ_CFG_INDEX_FILE_NAME      "index.json"
/** The custom config object member name. */
#define RTFUZZ_CFG_JSON_CUSTOM_CFG      "CustomCfg"
/** The input corpus array member name. */
#define RTFUZZ_CFG_JSON_INPUT_CORPUS    "InputCorpus"
/** The input name. */
#define RTFUZZ_CFG_JSON_INPUT_NAME      "Name"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Internal fuzzer config state.
 */
typedef struct RTFUZZCFGINT
{
    /** Magic value identifying the struct. */
    uint32_t                    u32Magic;
    /** Reference counter. */
    volatile uint32_t           cRefs;
    /** The VFS file handle we get the config from. */
    RTVFSFILE                   hVfsFile;
    /** The JSON root handle of the config. */
    RTJSONVAL                   hJsonRoot;
    /** The custom config file handle if existing. */
    RTVFSFILE                   hVfsFileCustomCfg;
} RTFUZZCFGINT;
/** Pointer to the internal fuzzer config state. */
typedef RTFUZZCFGINT *PRTFUZZCFGINT;
/** Pointer to a const internal fuzzer config state. */
typedef const RTFUZZCFGINT *PCRTFUZZCFGINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Creates a filesystem stream from the given VFS file object.
 *
 * @returns IPRT status code.
 * @param   phVfsFss                Where to store the handle to the filesystem stream on success.
 * @param   hVFsFile                The VFS file handle.
 */
static int rtFuzzCfgTarFssFromVfsFile(PRTVFSFSSTREAM phVfsFss, RTVFSFILE hVfsFile)
{
    int rc = RTVfsFileSeek(hVfsFile, 0, RTFILE_SEEK_BEGIN, NULL);
    if (RT_SUCCESS(rc))
    {
        RTVFSIOSTREAM hVfsFileIos = RTVfsFileToIoStream(hVfsFile);
        if (hVfsFileIos != NIL_RTVFSIOSTREAM)
        {
            RTVFSIOSTREAM hGunzipIos;
            rc = RTZipGzipDecompressIoStream(hVfsFileIos, 0 /*fFlags*/, &hGunzipIos);
            if (RT_SUCCESS(rc))
            {
                RTVFSFSSTREAM hTarFss;
                rc = RTZipTarFsStreamFromIoStream(hGunzipIos, 0 /*fFlags*/, &hTarFss);
                if (RT_SUCCESS(rc))
                {
                    RTVfsIoStrmRelease(hGunzipIos);
                    RTVfsIoStrmRelease(hVfsFileIos);
                    *phVfsFss = hTarFss;
                    return VINF_SUCCESS;
                }

                RTVfsIoStrmRelease(hGunzipIos);
            }

            RTVfsIoStrmRelease(hVfsFileIos);
        }
        else
            rc = VERR_INVALID_STATE; /** @todo */
    }

    return rc;
}


/**
 * Finds a given file in the filesystem stream.
 *
 * @returns IPRT status code.
 * @param   hVfsFss                 The filesystem stream handle.
 * @param   pszFilename             The filename to look for.
 * @param   fValidateUtf8           Flag whether tpo validate the content as UTF-8.
 * @param   phVfsFile               Where to store the VFS file handle on success (content is completely in memory).
 */
static int rtFuzzCfgFindFile(RTVFSFSSTREAM hVfsFss, const char *pszFilename, bool fValidateUtf8,
                             PRTVFSFILE phVfsFile)
{
    int rc = VINF_SUCCESS;

    *phVfsFile = NIL_RTVFSFILE;
    for (;;)
    {
        /*
         * Get the next stream object.
         */
        char           *pszName;
        RTVFSOBJ        hVfsObj;
        RTVFSOBJTYPE    enmType;
        rc = RTVfsFsStrmNext(hVfsFss, &pszName, &enmType, &hVfsObj);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_EOF)
                rc = VERR_NOT_FOUND;
            break;
        }
        const char *pszAdjName = pszName[0] == '.' && pszName[1] == '/' ? &pszName[2] : pszName;

        if (   !strcmp(pszAdjName, pszFilename)
            && (   enmType == RTVFSOBJTYPE_FILE
                || enmType == RTVFSOBJTYPE_IO_STREAM))
        {
            RTStrFree(pszName);

            RTVFSIOSTREAM hVfsIos = RTVfsObjToIoStream(hVfsObj);
            rc = RTVfsMemorizeIoStreamAsFile(hVfsIos, RTFILE_O_READ, phVfsFile);
            if (   RT_SUCCESS(rc)
                && fValidateUtf8)
                rc = RTVfsIoStrmValidateUtf8Encoding(hVfsIos,
                                                     RTVFS_VALIDATE_UTF8_BY_RTC_3629 | RTVFS_VALIDATE_UTF8_NO_NULL,
                                                     NULL);

            RTVfsObjRelease(hVfsObj);
            RTVfsIoStrmRelease(hVfsIos);
            if (   RT_FAILURE(rc)
                && *phVfsFile != NIL_RTVFSFILE)
            {
                RTVfsFileRelease(*phVfsFile);
                *phVfsFile = NIL_RTVFSFILE;
            }
            return rc;
        }

        /*
         * Clean up.
         */
        RTVfsObjRelease(hVfsObj);
        RTStrFree(pszName);
    }

    return rc;
}


/**
 * Returns the memorized file handle for the given name from the given tarball VFS file handle.
 *
 * @returns IPRT status code.
 * @param   hVfsTarball             The VFS file handle of the tarball containing the object.
 * @param   pszFilename             The filename to look for.
 * @param   fValidateUtf8           Flag whether tpo validate the content as UTF-8.
 * @param   phVfsFile               Where to store the VFS file handle on success (content is completely in memory).
 */
static int rtFuzzCfgGrabFileFromTarball(RTVFSFILE hVfsTarball, const char *pszFilename, bool fValidateUtf8, PRTVFSFILE phVfsFile)
{
    RTVFSFSSTREAM hVfsFss;
    int rc = rtFuzzCfgTarFssFromVfsFile(&hVfsFss, hVfsTarball);
    if (RT_SUCCESS(rc))
    {
        /* Search for the index file and parse it. */
        RTVFSFILE hVfsJson;
        rc = rtFuzzCfgFindFile(hVfsFss, pszFilename, fValidateUtf8, &hVfsJson);
        RTVfsFsStrmRelease(hVfsFss);
        if (RT_SUCCESS(rc))
            *phVfsFile = hVfsJson;
    }

    return rc;
}


/**
 * Loads the given fuzzing config.
 *
 * @returns IPRT status code.
 * @param   pThis                   The fuzzing config instance.
 * @param   pErrInfo                Additional error information, optional.
 */
static int rtFuzzCfgLoad(PRTFUZZCFGINT pThis, PRTERRINFO pErrInfo)
{
    /* Search for the index file and parse it. */
    RTVFSFILE hVfsJson;
    int rc = rtFuzzCfgGrabFileFromTarball(pThis->hVfsFile, RTFUZZ_CFG_INDEX_FILE_NAME, true /*fValidateUtf8*/, &hVfsJson);
    if (RT_SUCCESS(rc))
    {
        rc = RTJsonParseFromVfsFile(&pThis->hJsonRoot, hVfsJson, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            /* Look for the custom config in the JSON and find it in the VFS file. */
            char *pszCustomCfgFilename = NULL;
            rc = RTJsonValueQueryStringByName(pThis->hJsonRoot, RTFUZZ_CFG_JSON_CUSTOM_CFG, &pszCustomCfgFilename);
            if (rc == VERR_NOT_FOUND)
                rc = VINF_SUCCESS; /* The custom config is optional. */
            if (   RT_SUCCESS(rc)
                && pszCustomCfgFilename)
            {
                rc = rtFuzzCfgGrabFileFromTarball(pThis->hVfsFile, pszCustomCfgFilename, false /*fValidateUtf8*/, &pThis->hVfsFileCustomCfg);
                RTStrFree(pszCustomCfgFilename);
            }

            if (RT_FAILURE(rc))
            {
                RTJsonValueRelease(pThis->hJsonRoot);
                pThis->hJsonRoot = NIL_RTJSONVAL;
            }
        }

        RTVfsFileRelease(hVfsJson);
    }

    return rc;
}


/**
 * Searches for the given object name in the given JSON array, returning the object on success.
 *
 * @returns IPRT status code.
 * @param   hJsonValArr             JSON value handle containing the input corpus objects.
 * @param   pszName                 The name to look for.
 * @param   phJsonVal               Where to store the referenced JSON value on success.
 */
static int rtFuzzCfgQueryInputCorpusEntryFromArray(RTJSONVAL hJsonValArr, const char *pszName, PRTJSONVAL phJsonVal)
{
    int rc = VERR_NOT_FOUND;
    uint32_t cEntries = RTJsonValueGetArraySize(hJsonValArr);

    for (uint32_t i = 0; i < cEntries; i++)
    {
        RTJSONVAL hJsonVal;
        int rc2 = RTJsonValueQueryByIndex(hJsonValArr, i, &hJsonVal);
        if (RT_SUCCESS(rc2))
        {
            char *pszObjName;
            rc2 = RTJsonValueQueryStringByName(hJsonVal, RTFUZZ_CFG_JSON_INPUT_NAME, &pszObjName);
            if (RT_SUCCESS(rc2))
            {
                if (!strcmp(pszObjName, pszName))
                {
                    RTStrFree(pszObjName);
                    *phJsonVal = hJsonVal;
                    return VINF_SUCCESS;
                }

                RTStrFree(pszObjName);
            }

            RTJsonValueRelease(hJsonVal);
        }

        if (RT_FAILURE(rc2))
        {
            rc = rc2;
            break;
        }
    }

    return rc;
}


/**
 * Queries a 64bit unsigned integer.
 *
 * @returns IPRT status code.
 * @param   hJsonInp                JSON object handle to search in.
 * @param   pszName                 Value name to look for.
 * @param   pu64Val                 Where to store the value on success.
 */
static int rtFuzzCfgInputQueryU64(RTJSONVAL hJsonInp, const char *pszName, uint64_t *pu64Val)
{
    int64_t i64Val;
    int rc = RTJsonValueQueryIntegerByName(hJsonInp, pszName, &i64Val);
    if (RT_SUCCESS(rc))
    {
        if (i64Val >= 0)
            *pu64Val = (uint64_t)i64Val;
        else
            rc = VERR_OUT_OF_RANGE;
    }

    return rc;
}


/**
 * Queries a 64bit unsigned integer, supplying a default value if the name is not found in the
 * given JSON object.
 *
 * @returns IPRT status code.
 * @param   hJsonInp                JSON object handle to search in.
 * @param   pszName                 Value name to look for.
 * @param   pu64Val                 Where to store the value on success.
 * @param   u64Def                  The value to set if the value is not found.
 */
static int rtFuzzCfgInputQueryU64Def(RTJSONVAL hJsonInp, const char *pszName, uint64_t *pu64Val, uint64_t u64Def)
{
    int rc = rtFuzzCfgInputQueryU64(hJsonInp, pszName, pu64Val);
    if (rc == VERR_NOT_FOUND)
    {
        *pu64Val = u64Def;
        rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Adds the given input to the given fuzzing contexts input corpus.
 *
 * @returns IPRT status code.
 * @param   hFuzzCtx                The fuzzing context to add the input to.
 * @param   hJsonInp                The JSON input object with further parameters.
 * @param   hVfsIos                 The VFS I/O stream of the input data to add.
 */
static int rtFuzzCfgAddInputToCtx(RTFUZZCTX hFuzzCtx, RTJSONVAL hJsonInp, RTVFSIOSTREAM hVfsIos)
{
    uint64_t offMutStart = 0;
    int rc = rtFuzzCfgInputQueryU64Def(hJsonInp, "MutationStartOffset", &offMutStart, 0);
    if (RT_SUCCESS(rc))
    {
        uint64_t cbMutRange = UINT64_MAX;
        rc = rtFuzzCfgInputQueryU64Def(hJsonInp, "MutationRangeSize", &cbMutRange, UINT64_MAX);
        if (RT_SUCCESS(rc))
            rc = RTFuzzCtxCorpusInputAddFromVfsIoStrmEx(hFuzzCtx, hVfsIos, offMutStart, cbMutRange);
    }

    return rc;
}


/**
 * Sets the global fuzzer config form the given JSON object.
 *
 * @returns IPRT status code.
 * @param   hJsonRoot               The JSON object handle for the fuzzer config.
 * @param   hFuzzCtx                The fuzzing context to configure.
 */
static int rtFuzzCfgSetFuzzCtxCfg(RTJSONVAL hJsonRoot, RTFUZZCTX hFuzzCtx)
{
    uint64_t u64Tmp;
    int rc = rtFuzzCfgInputQueryU64(hJsonRoot, "Seed", &u64Tmp);
    if (RT_SUCCESS(rc))
        rc = RTFuzzCtxReseed(hFuzzCtx, u64Tmp);
    else if (rc == VERR_NOT_FOUND)
        rc = VINF_SUCCESS;

    if (RT_SUCCESS(rc))
    {
        rc = rtFuzzCfgInputQueryU64(hJsonRoot, "InputSizeMax", &u64Tmp);
        if (RT_SUCCESS(rc))
            rc = RTFuzzCtxCfgSetInputSeedMaximum(hFuzzCtx, (size_t)u64Tmp);
        else if (rc == VERR_NOT_FOUND)
            rc = VINF_SUCCESS;
    }

    if (RT_SUCCESS(rc))
    {
        uint64_t offMutateStart = 0;
        uint64_t cbMutateRange = UINT64_MAX;
        rc = rtFuzzCfgInputQueryU64(hJsonRoot, "MutationStartOffset", &offMutateStart);
        if (rc == VERR_NOT_FOUND)
            rc = VINF_SUCCESS;

        if (RT_SUCCESS(rc))
        {
            rc = rtFuzzCfgInputQueryU64(hJsonRoot, "MutationRangeSize", &cbMutateRange);
            if (rc == VERR_NOT_FOUND)
                rc = VINF_SUCCESS;
        }

        if (RT_SUCCESS(rc))
            rc = RTFuzzCtxCfgSetMutationRange(hFuzzCtx, offMutateStart, cbMutateRange);
    }

    /** @todo More here */
    return rc;
}


/**
 * Adds all inputs in the iven config file to the given fuzzer context.
 *
 * @returns IPRT status code.
 * @param   pThis                   The fuzzing config instance.
 * @param   hJsonValCorpusArr       The JSON array handle containing the input corpus configuration.
 * @param   hFuzzCtx                The fuzzing context to configure.
 */
static int rtFuzzCfgAddFuzzCtxInputs(PRTFUZZCFGINT pThis, RTJSONVAL hJsonValCorpusArr, RTFUZZCTX hFuzzCtx)
{
    /*
     * Go through the tarball sequentially and search the corresponding entries in the JSON array
     * instead of the other way around because reopening the tarball and seeking around
     * each time (filesystem stream) is much more expensive.
     */
    RTVFSFSSTREAM hVfsFss;
    int rc = rtFuzzCfgTarFssFromVfsFile(&hVfsFss, pThis->hVfsFile);
    if (RT_SUCCESS(rc))
    {
        for (;;)
        {
            /*
             * Get the next stream object.
             */
            char           *pszName;
            RTVFSOBJ        hVfsObj;
            RTVFSOBJTYPE    enmType;
            rc = RTVfsFsStrmNext(hVfsFss, &pszName, &enmType, &hVfsObj);
            if (RT_FAILURE(rc))
            {
                if (rc == VERR_EOF)
                    rc = VINF_SUCCESS;
                break;
            }

            if (   enmType == RTVFSOBJTYPE_FILE
                || enmType == RTVFSOBJTYPE_IO_STREAM)
            {
                const char *pszAdjName = pszName[0] == '.' && pszName[1] == '/' ? &pszName[2] : pszName;

                /* Skip the index.json. */
                if (strcmp(pszAdjName, RTFUZZ_CFG_INDEX_FILE_NAME))
                {
                    /* Look for a JSON object with the matching filename and process it. */
                    RTJSONVAL hJsonInp = NIL_RTJSONVAL;
                    rc = rtFuzzCfgQueryInputCorpusEntryFromArray(hJsonValCorpusArr, pszAdjName, &hJsonInp);
                    if (RT_SUCCESS(rc))
                    {
                        RTVFSIOSTREAM hVfsIos = RTVfsObjToIoStream(hVfsObj);
                        rc = rtFuzzCfgAddInputToCtx(hFuzzCtx, hJsonInp, hVfsIos);
                        RTVfsIoStrmRelease(hVfsIos);
                        RTJsonValueRelease(hJsonInp);
                    }
                }
            }

            /*
             * Clean up.
             */
            RTVfsObjRelease(hVfsObj);
            RTStrFree(pszName);
            if (RT_FAILURE(rc))
                break; /* Abort on error. */
        }

        RTVfsFsStrmRelease(hVfsFss);
    }

    return rc;
}


/**
 * Destroys the given fuzzing config.
 *
 * @param   pThis                   The fuzzing config instance to destroy.
 */
static void rtFuzzCfgDestroy(PRTFUZZCFGINT pThis)
{
    RTJsonValueRelease(pThis->hJsonRoot);
    RTVfsFileRelease(pThis->hVfsFile);
    if (pThis->hVfsFileCustomCfg != NIL_RTVFSFILE)
        RTVfsFileRelease(pThis->hVfsFileCustomCfg);
    pThis->hVfsFile = NIL_RTVFSFILE;
    RTMemFree(pThis);
}


RTDECL(int) RTFuzzCfgCreateFromVfsFile(PRTFUZZCFG phFuzzCfg, RTVFSFILE hVfsFile, PRTERRINFO pErrInfo)
{
    AssertPtrReturn(phFuzzCfg, VERR_INVALID_POINTER);

    int rc;
    PRTFUZZCFGINT pThis = (PRTFUZZCFGINT)RTMemAllocZ(sizeof(*pThis));
    if (RT_LIKELY(pThis))
    {
        pThis->u32Magic          = 0; /** @todo */
        pThis->cRefs             = 1;
        RTVfsFileRetain(hVfsFile);
        pThis->hVfsFile          = hVfsFile;
        pThis->hVfsFileCustomCfg = NIL_RTVFSFILE;

        rc = rtFuzzCfgLoad(pThis, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            *phFuzzCfg = pThis;
            return VINF_SUCCESS;
        }

        RTVfsFileRelease(hVfsFile);
        pThis->hVfsFile = NULL;
        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTFuzzCfgCreateFromFile(PRTFUZZCFG phFuzzCfg, const char *pszFilename, PRTERRINFO pErrInfo)
{
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);

    RTVFSFILE hVfsFile;
    int rc = RTVfsFileOpenNormal(pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE, &hVfsFile);
    if (RT_SUCCESS(rc))
    {
        rc = RTFuzzCfgCreateFromVfsFile(phFuzzCfg, hVfsFile, pErrInfo);
        RTVfsFileRelease(hVfsFile);
    }

    return rc;
}


RTDECL(uint32_t) RTFuzzCfgRetain(RTFUZZCFG hFuzzCfg)
{
    PRTFUZZCFGINT pThis = hFuzzCfg;

    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    return cRefs;
}


RTDECL(uint32_t) RTFuzzCfgRelease(RTFUZZCFG hFuzzCfg)
{
    PRTFUZZCFGINT pThis = hFuzzCfg;
    if (pThis == NIL_RTFUZZCFG)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    if (cRefs == 0)
        rtFuzzCfgDestroy(pThis);
    return cRefs;
}


RTDECL(int) RTFuzzCfgImport(RTFUZZCFG hFuzzCfg, RTFUZZCTX hFuzzCtx, uint32_t fFlags)
{
    AssertReturn(hFuzzCfg != NIL_RTFUZZCFG, VERR_INVALID_HANDLE);
    AssertReturn(hFuzzCtx != NIL_RTFUZZCTX, VERR_INVALID_HANDLE);
    AssertReturn(!(fFlags & ~RTFUZZCFG_IMPORT_F_VALID), VERR_INVALID_PARAMETER);

    /* Get the input corpus array. */
    PRTFUZZCFGINT pThis = hFuzzCfg;
    RTJSONVAL hJsonValCorpusArr;
    int rc = RTJsonValueQueryByName(pThis->hJsonRoot, RTFUZZ_CFG_JSON_INPUT_CORPUS, &hJsonValCorpusArr);
    if (RT_SUCCESS(rc))
    {
        if (RTJsonValueGetType(hJsonValCorpusArr) == RTJSONVALTYPE_ARRAY)
        {
            /* If not ommitted set the global fuzzing context config now. */
            if (!(fFlags & RTFUZZCFG_IMPORT_F_ONLY_INPUT))
                rc = rtFuzzCfgSetFuzzCtxCfg(pThis->hJsonRoot, hFuzzCtx);

            if (RT_SUCCESS(rc))
                rc = rtFuzzCfgAddFuzzCtxInputs(pThis, hJsonValCorpusArr, hFuzzCtx);
        }
        else
            rc = VERR_JSON_VALUE_INVALID_TYPE;
    }

    return rc;
}


RTDECL(int) RTFuzzCfgQueryCustomCfg(RTFUZZCFG hFuzzCfg, PRTVFSFILE phVfsFile)
{
    PRTFUZZCFGINT pThis = hFuzzCfg;

    if (pThis->hVfsFileCustomCfg != NIL_RTVFSFILE)
    {
        RTVfsFileRetain(pThis->hVfsFileCustomCfg);
        *phVfsFile = pThis->hVfsFileCustomCfg;
        return VINF_SUCCESS;
    }

    return VERR_NOT_FOUND;
}
