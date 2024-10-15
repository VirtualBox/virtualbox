/* $Id: fuzzmastercmd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Fuzzing framework API, master command.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#include <iprt/base64.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/json.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/tcp.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/vfs.h>
#include <iprt/zip.h>


/**
 * A running fuzzer state.
 */
typedef struct RTFUZZRUN
{
    /** List node. */
    RTLISTNODE                  NdFuzzed;
    /** Identifier. */
    char                        *pszId;
    /** Number of processes. */
    uint32_t                    cProcs;
    /** Target recorder flags. */
    uint32_t                    fTgtRecFlags;
    /** The fuzzing observer state handle. */
    RTFUZZOBS                   hFuzzObs;
    /** Flag whether fuzzing was started. */
    bool                        fStarted;
    /** Time when this run was created. */
    RTTIME                      TimeCreated;
    /** Millisecond timestamp when the run was created. */
    uint64_t                    tsCreatedMs;
} RTFUZZRUN;
/** Pointer to a running fuzzer state. */
typedef RTFUZZRUN *PRTFUZZRUN;


/**
 * Fuzzing master command state.
 */
typedef struct RTFUZZCMDMASTER
{
    /** List of running fuzzers. */
    RTLISTANCHOR                LstFuzzed;
    /** The port to listen on. */
    uint16_t                    uPort;
    /** The TCP server for requests. */
    PRTTCPSERVER                hTcpSrv;
    /** The root temp directory. */
    const char                  *pszTmpDir;
    /** The root results directory. */
    const char                  *pszResultsDir;
    /** Flag whether to shutdown. */
    bool                        fShutdown;
    /** The response message. */
    char                        *pszResponse;
} RTFUZZCMDMASTER;
/** Pointer to a fuzzing master command state. */
typedef RTFUZZCMDMASTER *PRTFUZZCMDMASTER;


/**
 * Wrapper around RTErrInfoSetV / RTMsgErrorV.
 *
 * @returns @a rc
 * @param   pErrInfo            Extended error info.
 * @param   rc                  The return code.
 * @param   pszFormat           The message format.
 * @param   ...                 The message format arguments.
 */
static int rtFuzzCmdMasterErrorRc(PRTERRINFO pErrInfo, int rc, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    if (pErrInfo)
        RTErrInfoSetV(pErrInfo, rc, pszFormat, va);
    else
        RTMsgErrorV(pszFormat, va);
    va_end(va);
    return rc;
}


/**
 * Returns a running fuzzer state by the given ID.
 *
 * @returns Pointer to the running fuzzer state or NULL if not found.
 * @param   pThis               The fuzzing master command state.
 * @param   pszId               The ID to look for.
 */
static PRTFUZZRUN rtFuzzCmdMasterGetFuzzerById(PRTFUZZCMDMASTER pThis, const char *pszId)
{
    PRTFUZZRUN pIt = NULL;
    RTListForEach(&pThis->LstFuzzed, pIt, RTFUZZRUN, NdFuzzed)
    {
        if (!RTStrCmp(pIt->pszId, pszId))
            return pIt;
    }

    return NULL;
}


#if 0 /* unused */
/**
 * Processes and returns the value of the given config item in the JSON request.
 *
 * @returns IPRT status code.
 * @param   ppszStr             Where to store the pointer to the string on success.
 * @param   pszCfgItem          The config item to resolve.
 * @param   hJsonCfg            The JSON object containing the item.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessCfgString(char **ppszStr, const char *pszCfgItem, RTJSONVAL hJsonCfg, PRTERRINFO pErrInfo)
{
    int rc = RTJsonValueQueryStringByName(hJsonCfg, pszCfgItem, ppszStr);
    if (RT_FAILURE(rc))
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to query string value of \"%s\"", pszCfgItem);

    return rc;
}


/**
 * Processes and returns the value of the given config item in the JSON request.
 *
 * @returns IPRT status code.
 * @param   pfVal               Where to store the config value on success.
 * @param   pszCfgItem          The config item to resolve.
 * @param   hJsonCfg            The JSON object containing the item.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessCfgBool(bool *pfVal, const char *pszCfgItem, RTJSONVAL hJsonCfg, PRTERRINFO pErrInfo)
{
    int rc = RTJsonValueQueryBooleanByName(hJsonCfg, pszCfgItem, pfVal);
    if (RT_FAILURE(rc))
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to query boolean value of \"%s\"", pszCfgItem);

    return rc;
}


/**
 * Processes and returns the value of the given config item in the JSON request.
 *
 * @returns IPRT status code.
 * @param   pfVal               Where to store the config value on success.
 * @param   pszCfgItem          The config item to resolve.
 * @param   hJsonCfg            The JSON object containing the item.
 * @param   fDef                Default value if the item wasn't found.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessCfgBoolDef(bool *pfVal, const char *pszCfgItem, RTJSONVAL hJsonCfg, bool fDef, PRTERRINFO pErrInfo)
{
    int rc = RTJsonValueQueryBooleanByName(hJsonCfg, pszCfgItem, pfVal);
    if (rc == VERR_NOT_FOUND)
    {
        *pfVal = fDef;
        rc = VINF_SUCCESS;
    }
    else if (RT_FAILURE(rc))
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to query boolean value of \"%s\"", pszCfgItem);

    return rc;
}
#endif


/**
 * Processes and returns the value of the given config item in the JSON request.
 *
 * @returns IPRT status code.
 * @param   pcbVal              Where to store the config value on success.
 * @param   pszCfgItem          The config item to resolve.
 * @param   hJsonCfg            The JSON object containing the item.
 * @param   cbDef               Default value if the item wasn't found.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessCfgSizeDef(size_t *pcbVal, const char *pszCfgItem, RTJSONVAL hJsonCfg, size_t cbDef, PRTERRINFO pErrInfo)
{
    *pcbVal = cbDef; /* Make GCC 6.3.0 happy. */

    int64_t i64Val = 0;
    int rc = RTJsonValueQueryIntegerByName(hJsonCfg, pszCfgItem, &i64Val);
    if (rc == VERR_NOT_FOUND)
        rc = VINF_SUCCESS;
    else if (RT_FAILURE(rc))
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to query size_t value of \"%s\"", pszCfgItem);
    else if (i64Val < 0 || (size_t)i64Val != (uint64_t)i64Val)
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_OUT_OF_RANGE, "JSON request malformed: Integer \"%s\" is out of range", pszCfgItem);
    else
        *pcbVal = (size_t)i64Val;

    return rc;
}


/**
 * Processes and returns the value of the given config item in the JSON request.
 *
 * @returns IPRT status code.
 * @param   pcbVal              Where to store the config value on success.
 * @param   pszCfgItem          The config item to resolve.
 * @param   hJsonCfg            The JSON object containing the item.
 * @param   cbDef               Default value if the item wasn't found.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessCfgU32Def(uint32_t *pu32Val, const char *pszCfgItem, RTJSONVAL hJsonCfg, uint32_t u32Def, PRTERRINFO pErrInfo)
{
    int64_t i64Val = 0;
    int rc = RTJsonValueQueryIntegerByName(hJsonCfg, pszCfgItem, &i64Val);
    if (rc == VERR_NOT_FOUND)
    {
        *pu32Val = u32Def;
        rc = VINF_SUCCESS;
    }
    else if (RT_FAILURE(rc))
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to query uint32_t value of \"%s\"", pszCfgItem);
    else if (i64Val < 0 || (uint32_t)i64Val != (uint64_t)i64Val)
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_OUT_OF_RANGE, "JSON request malformed: Integer \"%s\" is out of range", pszCfgItem);
    else
        *pu32Val = (uint32_t)i64Val;

    return rc;
}


/**
 * Returns the configured input channel for the binary under test.
 *
 * @returns Selected input channel or RTFUZZOBSINPUTCHAN_INVALID if an error occurred.
 * @param   pszCfgItem          The config item to resolve.
 * @param   hJsonCfg            The JSON object containing the item.
 * @param   enmChanDef          Default value if the item wasn't found.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static RTFUZZOBSINPUTCHAN rtFuzzCmdMasterFuzzRunProcessCfgGetInputChan(const char *pszCfgItem, RTJSONVAL hJsonCfg, RTFUZZOBSINPUTCHAN enmChanDef, PRTERRINFO pErrInfo)
{
    RTFUZZOBSINPUTCHAN enmInputChan = RTFUZZOBSINPUTCHAN_INVALID;

    RTJSONVAL hJsonVal;
    int rc = RTJsonValueQueryByName(hJsonCfg, pszCfgItem, &hJsonVal);
    if (rc == VERR_NOT_FOUND)
        enmInputChan = enmChanDef;
    else if (RT_SUCCESS(rc))
    {
        const char *pszBinary = RTJsonValueGetString(hJsonVal);
        if (pszBinary)
        {
            if (!RTStrCmp(pszBinary, "File"))
                enmInputChan = RTFUZZOBSINPUTCHAN_FILE;
            else if (!RTStrCmp(pszBinary, "Stdin"))
                enmInputChan = RTFUZZOBSINPUTCHAN_STDIN;
            else if (!RTStrCmp(pszBinary, "FuzzingAware"))
                enmInputChan = RTFUZZOBSINPUTCHAN_FUZZING_AWARE_CLIENT;
            else
                rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_PARAMETER, "JSON request malformed: \"%s\" for \"%s\" is not known", pszCfgItem, pszBinary);
        }
        else
            rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "JSON request malformed: \"%s\" is not a string", pszCfgItem);

        RTJsonValueRelease(hJsonVal);
    }
    else
        rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to query \"%s\"", pszCfgItem);

    return enmInputChan;
}


/**
 * Processes binary related configs for the given fuzzing run.
 *
 * @returns IPRT status code.
 * @param   pFuzzRun            The fuzzing run.
 * @param   hJsonRoot           The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessBinaryCfg(PRTFUZZRUN pFuzzRun, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonVal;
    int rc = RTJsonValueQueryByName(hJsonRoot, "BinaryPath", &hJsonVal);
    if (RT_SUCCESS(rc))
    {
        const char *pszBinary = RTJsonValueGetString(hJsonVal);
        if (RT_LIKELY(pszBinary))
        {
            RTFUZZOBSINPUTCHAN enmInputChan = rtFuzzCmdMasterFuzzRunProcessCfgGetInputChan("InputChannel", hJsonRoot, RTFUZZOBSINPUTCHAN_STDIN, pErrInfo);
            if (enmInputChan != RTFUZZOBSINPUTCHAN_INVALID)
            {
                rc = RTFuzzObsSetTestBinary(pFuzzRun->hFuzzObs, pszBinary, enmInputChan);
                if (RT_FAILURE(rc))
                    rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Failed to add the binary path for the fuzzing run");
            }
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "JSON request malformed: \"BinaryPath\" is not a string");
        RTJsonValueRelease(hJsonVal);
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to query value of \"BinaryPath\"");

    return rc;
}


/**
 * Processes argument related configs for the given fuzzing run.
 *
 * @returns IPRT status code.
 * @param   pFuzzRun            The fuzzing run.
 * @param   hJsonRoot           The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessArgCfg(PRTFUZZRUN pFuzzRun, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonValArgArray;
    int rc = RTJsonValueQueryByName(hJsonRoot, "Arguments", &hJsonValArgArray);
    if (RT_SUCCESS(rc))
    {
        unsigned cArgs = 0;
        rc = RTJsonValueQueryArraySize(hJsonValArgArray, &cArgs);
        if (RT_SUCCESS(rc))
        {
            if (cArgs > 0)
            {
                const char **papszArgs = (const char **)RTMemAllocZ(cArgs * sizeof(const char *));
                RTJSONVAL *pahJsonVal = (RTJSONVAL *)RTMemAllocZ(cArgs * sizeof(RTJSONVAL));
                if (RT_LIKELY(papszArgs && pahJsonVal))
                {
                    unsigned idx = 0;

                    for (idx = 0; idx < cArgs && RT_SUCCESS(rc); idx++)
                    {
                        rc = RTJsonValueQueryByIndex(hJsonValArgArray, idx, &pahJsonVal[idx]);
                        if (RT_SUCCESS(rc))
                        {
                            papszArgs[idx] = RTJsonValueGetString(pahJsonVal[idx]);
                            if (RT_UNLIKELY(!papszArgs[idx]))
                                rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "Argument %u is not a string", idx);
                        }
                    }

                    if (RT_SUCCESS(rc))
                    {
                        rc = RTFuzzObsSetTestBinaryArgs(pFuzzRun->hFuzzObs, papszArgs, cArgs);
                        if (RT_FAILURE(rc))
                            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Failed to set arguments for the fuzzing run");
                    }

                    /* Release queried values. */
                    while (idx > 0)
                    {
                        RTJsonValueRelease(pahJsonVal[idx - 1]);
                        idx--;
                    }
                }
                else
                    rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_NO_MEMORY, "Out of memory allocating memory for the argument vector");

                if (papszArgs)
                    RTMemFree(papszArgs);
                if (pahJsonVal)
                    RTMemFree(pahJsonVal);
            }
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: \"Arguments\" is not an array");
        RTJsonValueRelease(hJsonValArgArray);
    }

    return rc;
}


/**
 * Processes process environment related configs for the given fuzzing run.
 *
 * @returns IPRT status code.
 * @param   pFuzzRun            The fuzzing run.
 * @param   hJsonRoot           The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessEnvironment(PRTFUZZRUN pFuzzRun, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonValEnv;
    int rc = RTJsonValueQueryByName(hJsonRoot, "Env", &hJsonValEnv);
    if (RT_SUCCESS(rc))
    {
        bool fReplaceEnv = false; /* false means to append everything to the default block. */

        rc = RTJsonValueQueryBooleanByName(hJsonRoot, "EnvReplace", &fReplaceEnv);
        if (   RT_SUCCESS(rc)
            || rc == VERR_NOT_FOUND)
        {
            RTJSONIT hEnvIt;
            RTENV hEnv = NULL;

            if (fReplaceEnv)
                rc = RTEnvCreate(&hEnv);
            else
                rc = RTEnvClone(&hEnv, RTENV_DEFAULT);

            if (RT_SUCCESS(rc))
            {
                rc = RTJsonIteratorBeginArray(hJsonValEnv, &hEnvIt);
                if (RT_SUCCESS(rc))
                {
                    do
                    {
                        RTJSONVAL hVal;
                        rc = RTJsonIteratorQueryValue(hEnvIt, &hVal, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            const char *pszVar = RTJsonValueGetString(hVal);
                            if (RT_LIKELY(pszVar))
                                rc = RTEnvPutEx(hEnv, pszVar);
                            RTJsonValueRelease(hVal);
                        }
                        if (RT_FAILURE(rc))
                            break;
                        rc = RTJsonIteratorNext(hEnvIt);
                    } while (RT_SUCCESS(rc));

                    if (   rc == VERR_JSON_IS_EMPTY
                        || rc == VERR_JSON_ITERATOR_END)
                        rc = VINF_SUCCESS;
                    else
                        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to parse environment");

                    RTJsonIteratorFree(hEnvIt);
                }
                else if (rc == VERR_JSON_IS_EMPTY)
                    rc = VINF_SUCCESS;
                else
                    rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: \"Environment\" is not an array");

                if (RT_SUCCESS(rc))
                {
                    rc = RTFuzzObsSetTestBinaryEnv(pFuzzRun->hFuzzObs, hEnv);
                    AssertRC(rc);
                }
                else if (hEnv)
                    RTEnvDestroy(hEnv);
            }
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to create environment block");
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to query \"EnvReplace\"");

        RTJsonValueRelease(hJsonValEnv);
    }
    else if (rc == VERR_NOT_FOUND)
        rc = VINF_SUCCESS; /* Just keep using the default environment. */
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to query the \"Environment\"");

    return rc;
}


/**
 * Processes process environment related configs for the given fuzzing run.
 *
 * @returns IPRT status code.
 * @param   pFuzzRun            The fuzzing run.
 * @param   hJsonRoot           The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessSanitizers(PRTFUZZRUN pFuzzRun, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonValSan;
    int rc = RTJsonValueQueryByName(hJsonRoot, "Sanitizers", &hJsonValSan);
    if (RT_SUCCESS(rc))
    {
        uint32_t fSanitizers = 0;
        RTJSONIT hSanIt;
        rc = RTJsonIteratorBeginArray(hJsonValSan, &hSanIt);
        if (RT_SUCCESS(rc))
        {
            do
            {
                RTJSONVAL hVal;
                rc = RTJsonIteratorQueryValue(hSanIt, &hVal, NULL);
                if (RT_SUCCESS(rc))
                {
                    const char *pszSan = RTJsonValueGetString(hVal);
                    if (!RTStrICmp(pszSan, "Asan"))
                        fSanitizers |= RTFUZZOBS_SANITIZER_F_ASAN;
                    else if (!RTStrICmp(pszSan, "SanCov"))
                        fSanitizers |= RTFUZZOBS_SANITIZER_F_SANCOV;
                    else
                        rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_NOT_FOUND, "JSON request malformed: The sanitizer '%s' is not known", pszSan);
                    RTJsonValueRelease(hVal);
                }
                if (RT_FAILURE(rc))
                    break;

                rc = RTJsonIteratorNext(hSanIt);
            } while (RT_SUCCESS(rc));

            if (   rc == VERR_JSON_IS_EMPTY
                || rc == VERR_JSON_ITERATOR_END)
                rc = VINF_SUCCESS;
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to parse sanitizers");

            RTJsonIteratorFree(hSanIt);
        }
        else if (rc == VERR_JSON_IS_EMPTY)
            rc = VINF_SUCCESS;
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: \"Sanitizers\" is not an array");

        if (RT_SUCCESS(rc))
        {
            rc = RTFuzzObsSetTestBinarySanitizers(pFuzzRun->hFuzzObs, fSanitizers);
            AssertRC(rc);
        }

        RTJsonValueRelease(hJsonValSan);
    }
    else if (rc == VERR_NOT_FOUND)
        rc = VINF_SUCCESS; /* Just keep using the defaults. */
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to query the \"Sanitizers\"");

    return rc;
}


/**
 * Processes the given seed and adds it to the input corpus.
 *
 * @returns IPRT status code.
 * @param   hFuzzCtx            The fuzzing context handle.
 * @param   pszCompression      Compression used for the seed.
 * @param   pszSeed             The seed as a base64 encoded string.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessSeed(RTFUZZCTX hFuzzCtx, const char *pszCompression, const char *pszSeed, PRTERRINFO pErrInfo)
{
    int rc = VINF_SUCCESS;
    ssize_t cbSeedDecoded = RTBase64DecodedSize(pszSeed, NULL);
    if (cbSeedDecoded > 0)
    {
        uint8_t *pbSeedDecoded = (uint8_t *)RTMemAllocZ(cbSeedDecoded);
        if (RT_LIKELY(pbSeedDecoded))
        {
            rc = RTBase64Decode(pszSeed, pbSeedDecoded, cbSeedDecoded, NULL, NULL);
            if (RT_SUCCESS(rc))
            {
                /* Decompress if applicable. */
                if (!RTStrICmp(pszCompression, "None"))
                    rc = RTFuzzCtxCorpusInputAdd(hFuzzCtx, pbSeedDecoded, cbSeedDecoded);
                else
                {
                    RTVFSIOSTREAM hVfsIosSeed;
                    rc = RTVfsIoStrmFromBuffer(RTFILE_O_READ, pbSeedDecoded, cbSeedDecoded, &hVfsIosSeed);
                    if (RT_SUCCESS(rc))
                    {
                        RTVFSIOSTREAM hVfsDecomp = NIL_RTVFSIOSTREAM;

                        if (!RTStrICmp(pszCompression, "Gzip"))
                            rc = RTZipGzipDecompressIoStream(hVfsIosSeed, RTZIPGZIPDECOMP_F_ALLOW_ZLIB_HDR, &hVfsDecomp);
                        else
                            rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "Request error: Compression \"%s\" is not known", pszCompression);

                        if (RT_SUCCESS(rc))
                        {
                            RTVFSFILE hVfsFile;
                            rc = RTVfsMemFileCreate(hVfsDecomp, 2 * _1M, &hVfsFile);
                            if (RT_SUCCESS(rc))
                            {
                                rc = RTVfsFileSeek(hVfsFile, 0, RTFILE_SEEK_BEGIN, NULL);
                                if (RT_SUCCESS(rc))
                                {
                                    /* The VFS file contains the buffer for the seed now. */
                                    rc = RTFuzzCtxCorpusInputAddFromVfsFile(hFuzzCtx, hVfsFile);
                                    if (RT_FAILURE(rc))
                                        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to add input seed");
                                    RTVfsFileRelease(hVfsFile);
                                }
                                else
                                    rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "Request error: Failed to seek to the beginning of the seed");
                            }
                            else
                                rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "Request error: Failed to decompress input seed");

                            RTVfsIoStrmRelease(hVfsDecomp);
                        }

                        RTVfsIoStrmRelease(hVfsIosSeed);
                    }
                    else
                        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to create I/O stream from seed buffer");
                }
            }
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to decode the seed string");

            RTMemFree(pbSeedDecoded);
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_NO_MEMORY, "Request error: Failed to allocate %zd bytes of memory for the seed", cbSeedDecoded);
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "JSON request malformed: Couldn't find \"Seed\" doesn't contain a base64 encoded value");

    return rc;
}


/**
 * Processes a signle input seed for the given fuzzing run.
 *
 * @returns IPRT status code.
 * @param   pFuzzRun            The fuzzing run.
 * @param   hJsonSeed           The seed node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessInputSeedSingle(PRTFUZZRUN pFuzzRun, RTJSONVAL hJsonSeed, PRTERRINFO pErrInfo)
{
    RTFUZZCTX hFuzzCtx;
    int rc = RTFuzzObsQueryCtx(pFuzzRun->hFuzzObs, &hFuzzCtx);
    if (RT_SUCCESS(rc))
    {
        RTJSONVAL hJsonValComp;
        rc = RTJsonValueQueryByName(hJsonSeed, "Compression", &hJsonValComp);
        if (RT_SUCCESS(rc))
        {
            const char *pszCompression = RTJsonValueGetString(hJsonValComp);
            if (RT_LIKELY(pszCompression))
            {
                RTJSONVAL hJsonValSeed;
                rc = RTJsonValueQueryByName(hJsonSeed, "Seed", &hJsonValSeed);
                if (RT_SUCCESS(rc))
                {
                    const char *pszSeed = RTJsonValueGetString(hJsonValSeed);
                    if (RT_LIKELY(pszSeed))
                        rc = rtFuzzCmdMasterFuzzRunProcessSeed(hFuzzCtx, pszCompression, pszSeed, pErrInfo);
                    else
                        rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "JSON request malformed: \"Seed\" value is not a string");

                    RTJsonValueRelease(hJsonValSeed);
                }
                else
                    rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Couldn't find \"Seed\" value");
            }
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "JSON request malformed: \"Compression\" value is not a string");

            RTJsonValueRelease(hJsonValComp);
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Couldn't find \"Compression\" value");

        RTFuzzCtxRelease(hFuzzCtx);
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Failed to query fuzzing context from observer");

    return rc;
}


/**
 * Processes the given seed file and adds it to the input corpus.
 *
 * @returns IPRT status code.
 * @param   hFuzzCtx            The fuzzing context handle.
 * @param   pszCompression      Compression used for the seed.
 * @param   pszSeed             The seed as a base64 encoded string.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessSeedFile(RTFUZZCTX hFuzzCtx, const char *pszCompression, const char *pszFile, PRTERRINFO pErrInfo)
{
    int rc = VINF_SUCCESS;

    /* Decompress if applicable. */
    if (!RTStrICmp(pszCompression, "None"))
        rc = RTFuzzCtxCorpusInputAddFromFile(hFuzzCtx, pszFile);
    else
    {
        RTVFSIOSTREAM hVfsIosSeed;
        rc = RTVfsIoStrmOpenNormal(pszFile, RTFILE_O_OPEN | RTFILE_O_READ, &hVfsIosSeed);
        if (RT_SUCCESS(rc))
        {
            RTVFSIOSTREAM hVfsDecomp = NIL_RTVFSIOSTREAM;

            if (!RTStrICmp(pszCompression, "Gzip"))
                rc = RTZipGzipDecompressIoStream(hVfsIosSeed, RTZIPGZIPDECOMP_F_ALLOW_ZLIB_HDR, &hVfsDecomp);
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "Request error: Compression \"%s\" is not known", pszCompression);

            if (RT_SUCCESS(rc))
            {
                RTVFSFILE hVfsFile;
                rc = RTVfsMemFileCreate(hVfsDecomp, 2 * _1M, &hVfsFile);
                if (RT_SUCCESS(rc))
                {
                    rc = RTVfsFileSeek(hVfsFile, 0, RTFILE_SEEK_BEGIN, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        /* The VFS file contains the buffer for the seed now. */
                        rc = RTFuzzCtxCorpusInputAddFromVfsFile(hFuzzCtx, hVfsFile);
                        if (RT_FAILURE(rc))
                            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to add input seed");
                        RTVfsFileRelease(hVfsFile);
                    }
                    else
                        rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "Request error: Failed to seek to the beginning of the seed");
                }
                else
                    rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "Request error: Failed to decompress input seed");

                RTVfsIoStrmRelease(hVfsDecomp);
            }

            RTVfsIoStrmRelease(hVfsIosSeed);
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to create I/O stream from seed buffer");
    }

    return rc;
}


/**
 * Processes a signle input seed given as a file path for the given fuzzing run.
 *
 * @returns IPRT status code.
 * @param   pFuzzRun            The fuzzing run.
 * @param   hJsonSeed           The seed node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessInputSeedFileSingle(PRTFUZZRUN pFuzzRun, RTJSONVAL hJsonSeed, PRTERRINFO pErrInfo)
{
    RTFUZZCTX hFuzzCtx;
    int rc = RTFuzzObsQueryCtx(pFuzzRun->hFuzzObs, &hFuzzCtx);
    if (RT_SUCCESS(rc))
    {
        RTJSONVAL hJsonValComp;
        rc = RTJsonValueQueryByName(hJsonSeed, "Compression", &hJsonValComp);
        if (RT_SUCCESS(rc))
        {
            const char *pszCompression = RTJsonValueGetString(hJsonValComp);
            if (RT_LIKELY(pszCompression))
            {
                RTJSONVAL hJsonValFile;
                rc = RTJsonValueQueryByName(hJsonSeed, "File", &hJsonValFile);
                if (RT_SUCCESS(rc))
                {
                    const char *pszFile = RTJsonValueGetString(hJsonValFile);
                    if (RT_LIKELY(pszFile))
                        rc = rtFuzzCmdMasterFuzzRunProcessSeedFile(hFuzzCtx, pszCompression, pszFile, pErrInfo);
                    else
                        rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "JSON request malformed: \"File\" value is not a string");

                    RTJsonValueRelease(hJsonValFile);
                }
                else
                    rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Couldn't find \"File\" value");
            }
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_INVALID_STATE, "JSON request malformed: \"Compression\" value is not a string");

            RTJsonValueRelease(hJsonValComp);
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Couldn't find \"Compression\" value");

        RTFuzzCtxRelease(hFuzzCtx);
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Failed to query fuzzing context from observer");

    return rc;
}


/**
 * Processes input seed related configs for the given fuzzing run.
 *
 * @returns IPRT status code.
 * @param   pFuzzRun            The fuzzing run.
 * @param   hJsonRoot           The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessInputSeeds(PRTFUZZRUN pFuzzRun, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonValSeedArray;
    int rc = RTJsonValueQueryByName(hJsonRoot, "InputSeeds", &hJsonValSeedArray);
    if (RT_SUCCESS(rc))
    {
        RTJSONIT hIt;
        rc = RTJsonIteratorBegin(hJsonValSeedArray, &hIt);
        if (RT_SUCCESS(rc))
        {
            RTJSONVAL hJsonInpSeed;
            while (   RT_SUCCESS(rc)
                   && RTJsonIteratorQueryValue(hIt, &hJsonInpSeed, NULL) != VERR_JSON_ITERATOR_END)
            {
                rc = rtFuzzCmdMasterFuzzRunProcessInputSeedSingle(pFuzzRun, hJsonInpSeed, pErrInfo);
                RTJsonValueRelease(hJsonInpSeed);
                if (RT_FAILURE(rc))
                    break;
                rc = RTJsonIteratorNext(hIt);
            }

            if (rc == VERR_JSON_ITERATOR_END)
                rc = VINF_SUCCESS;
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to create array iterator");

        RTJsonValueRelease(hJsonValSeedArray);
    }
    else if (rc == VERR_NOT_FOUND)
        rc = VINF_SUCCESS;

    if (RT_SUCCESS(rc))
    {
        rc = RTJsonValueQueryByName(hJsonRoot, "InputSeedFiles", &hJsonValSeedArray);
        if (RT_SUCCESS(rc))
        {
            RTJSONIT hIt;
            rc = RTJsonIteratorBegin(hJsonValSeedArray, &hIt);
            if (RT_SUCCESS(rc))
            {
                RTJSONVAL hJsonInpSeed;
                while (   RT_SUCCESS(rc)
                       && RTJsonIteratorQueryValue(hIt, &hJsonInpSeed, NULL) != VERR_JSON_ITERATOR_END)
                {
                    rc = rtFuzzCmdMasterFuzzRunProcessInputSeedFileSingle(pFuzzRun, hJsonInpSeed, pErrInfo);
                    RTJsonValueRelease(hJsonInpSeed);
                    if (RT_FAILURE(rc))
                        break;
                    rc = RTJsonIteratorNext(hIt);
                }

                if (rc == VERR_JSON_ITERATOR_END)
                    rc = VINF_SUCCESS;
            }
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to create array iterator");

            RTJsonValueRelease(hJsonValSeedArray);
        }
        else if (rc == VERR_NOT_FOUND)
            rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Processes miscellaneous config items.
 *
 * @returns IPRT status code.
 * @param   pFuzzRun            The fuzzing run.
 * @param   hJsonRoot           The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessMiscCfg(PRTFUZZRUN pFuzzRun, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    size_t cbTmp;
    int rc = rtFuzzCmdMasterFuzzRunProcessCfgSizeDef(&cbTmp, "InputSeedMax", hJsonRoot, 0, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        RTFUZZCTX hFuzzCtx;
        rc = RTFuzzObsQueryCtx(pFuzzRun->hFuzzObs, &hFuzzCtx);
        AssertRC(rc);

        rc = RTFuzzCtxCfgSetInputSeedMaximum(hFuzzCtx, cbTmp);
        if (RT_FAILURE(rc))
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to set maximum input seed size to %zu", cbTmp);
    }

    if (RT_SUCCESS(rc))
        rc = rtFuzzCmdMasterFuzzRunProcessCfgU32Def(&pFuzzRun->cProcs, "FuzzingProcs", hJsonRoot, 0, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        uint32_t msTimeoutMax = 0;
        rc = rtFuzzCmdMasterFuzzRunProcessCfgU32Def(&msTimeoutMax, "TimeoutMax", hJsonRoot, 1000, pErrInfo);
        if (RT_SUCCESS(rc))
            rc = RTFuzzObsSetTestBinaryTimeout(pFuzzRun->hFuzzObs, msTimeoutMax);
    }

    return rc;
}


/**
 * Processes target recording related configs for the given fuzzing run.
 *
 * @returns IPRT status code.
 * @param   pFuzzRun            The fuzzing run.
 * @param   hJsonRoot           The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunProcessTgtRecFlags(PRTFUZZRUN pFuzzRun, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonValTgt;
    int rc = RTJsonValueQueryByName(hJsonRoot, "TgtRec", &hJsonValTgt);
    if (RT_SUCCESS(rc))
    {
        uint32_t fTgtRecFlags = 0;
        RTJSONIT hTgtIt;
        rc = RTJsonIteratorBeginArray(hJsonValTgt, &hTgtIt);
        if (RT_SUCCESS(rc))
        {
            do
            {
                RTJSONVAL hVal;
                rc = RTJsonIteratorQueryValue(hTgtIt, &hVal, NULL);
                if (RT_SUCCESS(rc))
                {
                    const char *pszTgtRec = RTJsonValueGetString(hVal);
                    if (!RTStrICmp(pszTgtRec, "StdOut"))
                        fTgtRecFlags |= RTFUZZTGT_REC_STATE_F_STDOUT;
                    else if (!RTStrICmp(pszTgtRec, "StdErr"))
                        fTgtRecFlags |= RTFUZZTGT_REC_STATE_F_STDERR;
                    else if (!RTStrICmp(pszTgtRec, "ProcSts"))
                        fTgtRecFlags |= RTFUZZTGT_REC_STATE_F_PROCSTATUS;
                    else if (!RTStrICmp(pszTgtRec, "SanCov"))
                        fTgtRecFlags |= RTFUZZTGT_REC_STATE_F_SANCOV;
                    else
                        rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_NOT_FOUND, "JSON request malformed: The recording flags '%s' is not known", pszTgtRec);
                    RTJsonValueRelease(hVal);
                }
                if (RT_FAILURE(rc))
                    break;

                rc = RTJsonIteratorNext(hTgtIt);
            } while (RT_SUCCESS(rc));

            if (   rc == VERR_JSON_IS_EMPTY
                || rc == VERR_JSON_ITERATOR_END)
                rc = VINF_SUCCESS;
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to parse target recording flags");

            RTJsonIteratorFree(hTgtIt);
        }
        else if (rc == VERR_JSON_IS_EMPTY)
            rc = VINF_SUCCESS;
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: \"TgtRec\" is not an array");

        pFuzzRun->fTgtRecFlags = fTgtRecFlags;

        RTJsonValueRelease(hJsonValTgt);
    }
    else if (rc == VERR_NOT_FOUND)
    {
        pFuzzRun->fTgtRecFlags = RTFUZZTGT_REC_STATE_F_PROCSTATUS;
        rc = VINF_SUCCESS; /* Just keep using the defaults. */
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Failed to query \"TgtRec\"");

    return rc;
}


/**
 * Sets up the directories for the given fuzzing run.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing master command state.
 * @param   pFuzzRun            The fuzzing run to setup the directories for.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterFuzzRunSetupDirectories(PRTFUZZCMDMASTER pThis, PRTFUZZRUN pFuzzRun, PRTERRINFO pErrInfo)
{
    /* Create temp directories. */
    char szTmpDir[RTPATH_MAX];
    int rc = RTPathJoin(&szTmpDir[0], sizeof(szTmpDir), pThis->pszTmpDir, pFuzzRun->pszId);
    AssertRC(rc);
    rc = RTDirCreate(szTmpDir, 0700,   RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_SET
                                     | RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_NOT_CRITICAL);
    if (rc == VERR_ALREADY_EXISTS)
    {
        /* Clear the directory. */
        rc = RTDirRemoveRecursive(szTmpDir, RTDIRRMREC_F_CONTENT_ONLY);
    }

    if (RT_SUCCESS(rc))
    {
        rc = RTFuzzObsSetTmpDirectory(pFuzzRun->hFuzzObs, szTmpDir);
        if (RT_SUCCESS(rc))
        {
            rc = RTPathJoin(&szTmpDir[0], sizeof(szTmpDir), pThis->pszResultsDir, pFuzzRun->pszId);
            AssertRC(rc);
            rc = RTDirCreate(szTmpDir, 0700,   RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_SET
                                             | RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_NOT_CRITICAL);
            if (RT_SUCCESS(rc) || rc == VERR_ALREADY_EXISTS)
            {
                rc = RTFuzzObsSetResultDirectory(pFuzzRun->hFuzzObs, szTmpDir);
                if (RT_FAILURE(rc))
                    rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to set results directory to %s", szTmpDir);
            }
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to create results directory %s", szTmpDir);
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to set temporary directory to %s", szTmpDir);
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to create temporary directory %s", szTmpDir);

    return rc;
}


/**
 * Creates a new fuzzing run with the given ID.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing master command state.
 * @param   pszId               The ID to use.
 * @param   hJsonRoot           The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterCreateFuzzRunWithId(PRTFUZZCMDMASTER pThis, const char *pszId, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    int rc = VINF_SUCCESS;
    PRTFUZZRUN pFuzzRun = (PRTFUZZRUN)RTMemAllocZ(sizeof(*pFuzzRun));
    if (RT_LIKELY(pFuzzRun))
    {
        pFuzzRun->pszId = RTStrDup(pszId);
        if (RT_LIKELY(pFuzzRun->pszId))
        {
            rc = rtFuzzCmdMasterFuzzRunProcessTgtRecFlags(pFuzzRun, hJsonRoot, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                rc = RTFuzzObsCreate(&pFuzzRun->hFuzzObs, RTFUZZCTXTYPE_BLOB, pFuzzRun->fTgtRecFlags);
                if (RT_SUCCESS(rc))
                {
                    rc = rtFuzzCmdMasterFuzzRunProcessBinaryCfg(pFuzzRun, hJsonRoot, pErrInfo);
                    if (RT_SUCCESS(rc))
                        rc = rtFuzzCmdMasterFuzzRunProcessArgCfg(pFuzzRun, hJsonRoot, pErrInfo);
                    if (RT_SUCCESS(rc))
                        rc = rtFuzzCmdMasterFuzzRunProcessEnvironment(pFuzzRun, hJsonRoot, pErrInfo);
                    if (RT_SUCCESS(rc))
                        rc = rtFuzzCmdMasterFuzzRunProcessInputSeeds(pFuzzRun, hJsonRoot, pErrInfo);
                    if (RT_SUCCESS(rc))
                        rc = rtFuzzCmdMasterFuzzRunProcessMiscCfg(pFuzzRun, hJsonRoot, pErrInfo);
                    if (RT_SUCCESS(rc))
                        rc = rtFuzzCmdMasterFuzzRunProcessSanitizers(pFuzzRun, hJsonRoot, pErrInfo);
                    if (RT_SUCCESS(rc))
                        rc = rtFuzzCmdMasterFuzzRunSetupDirectories(pThis, pFuzzRun, pErrInfo);

                    if (RT_SUCCESS(rc))
                    {
                        /* Start fuzzing. */
                        RTListAppend(&pThis->LstFuzzed, &pFuzzRun->NdFuzzed);
                        rc = RTFuzzObsExecStart(pFuzzRun->hFuzzObs, pFuzzRun->cProcs);
                        if (RT_SUCCESS(rc))
                        {
                            RTTIMESPEC TimeSpec;
                            RTTimeNow(&TimeSpec);
                            RTTimeLocalExplode(&pFuzzRun->TimeCreated, &TimeSpec);
                            pFuzzRun->tsCreatedMs = RTTimeMilliTS();
                            pFuzzRun->fStarted = true;
                            return VINF_SUCCESS;
                        }
                        else
                            rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to start fuzzing with %Rrc", rc);
                    }

                    int rc2 = RTFuzzObsDestroy(pFuzzRun->hFuzzObs);
                    AssertRC(rc2); RT_NOREF(rc2);
                }
            }

            RTStrFree(pFuzzRun->pszId);
            pFuzzRun->pszId = NULL;
        }
        else
            rc = VERR_NO_STR_MEMORY;

        RTMemFree(pFuzzRun);
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_NO_MEMORY, "Request error: Out of memory allocating the fuzzer state");

    return rc;
}


/**
 * Resolves the fuzzing run from the given ID config item and the given JSON request.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing master command state.
 * @param   hJsonRoot           The root node of the JSON request.
 * @param   pszIdItem           The JSON item which contains the ID of the fuzzing run.
 * @param   ppFuzzRun           Where to store the pointer to the fuzzing run on success.
 */
static int rtFuzzCmdMasterQueryFuzzRunFromJson(PRTFUZZCMDMASTER pThis, RTJSONVAL hJsonRoot, const char *pszIdItem, PRTERRINFO pErrInfo,
                                               PRTFUZZRUN *ppFuzzRun)
{
    RTJSONVAL hJsonValId;
    int rc = RTJsonValueQueryByName(hJsonRoot, pszIdItem, &hJsonValId);
    if (RT_SUCCESS(rc))
    {
        const char *pszId = RTJsonValueGetString(hJsonValId);
        if (pszId)
        {
            PRTFUZZRUN pFuzzRun = rtFuzzCmdMasterGetFuzzerById(pThis, pszId);
            if (pFuzzRun)
                *ppFuzzRun = pFuzzRun;
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_NOT_FOUND, "Request error: The ID \"%s\" wasn't found", pszId);
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_JSON_VALUE_INVALID_TYPE, "JSON request malformed: \"Id\" is not a string value");

        RTJsonValueRelease(hJsonValId);
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Couldn't find \"Id\" value");
    return rc;
}


/**
 * Processes the "StartFuzzing" request.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing master command state.
 * @param   hJsonRoot           The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterProcessJsonReqStart(PRTFUZZCMDMASTER pThis, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonValId;
    int rc = RTJsonValueQueryByName(hJsonRoot, "Id", &hJsonValId);
    if (RT_SUCCESS(rc))
    {
        const char *pszId = RTJsonValueGetString(hJsonValId);
        if (pszId)
        {
            PRTFUZZRUN pFuzzRun = rtFuzzCmdMasterGetFuzzerById(pThis, pszId);
            if (!pFuzzRun)
                rc = rtFuzzCmdMasterCreateFuzzRunWithId(pThis, pszId, hJsonRoot, pErrInfo);
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_ALREADY_EXISTS, "Request error: The ID \"%s\" is already registered", pszId);
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_JSON_VALUE_INVALID_TYPE, "JSON request malformed: \"Id\" is not a string value");

        RTJsonValueRelease(hJsonValId);
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Couldn't find \"Id\" value");
    return rc;
}


/**
 * Processes the "StopFuzzing" request.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing master command state.
 * @param   hJsonValRoot        The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterProcessJsonReqStop(PRTFUZZCMDMASTER pThis, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    PRTFUZZRUN pFuzzRun;
    int rc = rtFuzzCmdMasterQueryFuzzRunFromJson(pThis, hJsonRoot, "Id", pErrInfo, &pFuzzRun);
    if (RT_SUCCESS(rc))
    {
        RTListNodeRemove(&pFuzzRun->NdFuzzed);
        RTFuzzObsExecStop(pFuzzRun->hFuzzObs);
        RTFuzzObsDestroy(pFuzzRun->hFuzzObs);
        RTStrFree(pFuzzRun->pszId);
        RTMemFree(pFuzzRun);
    }

    return rc;
}


/**
 * Processes the "SuspendFuzzing" request.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing master command state.
 * @param   hJsonValRoot        The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterProcessJsonReqSuspend(PRTFUZZCMDMASTER pThis, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    PRTFUZZRUN pFuzzRun;
    int rc = rtFuzzCmdMasterQueryFuzzRunFromJson(pThis, hJsonRoot, "Id", pErrInfo, &pFuzzRun);
    if (RT_SUCCESS(rc))
    {
        if (pFuzzRun->fStarted)
        {
            rc = RTFuzzObsExecStop(pFuzzRun->hFuzzObs);
            if (RT_SUCCESS(rc))
                pFuzzRun->fStarted = false;
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Suspending the fuzzing process failed");
        }
    }

    return rc;
}


/**
 * Processes the "ResumeFuzzing" request.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing master command state.
 * @param   hJsonValRoot        The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterProcessJsonReqResume(PRTFUZZCMDMASTER pThis, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    PRTFUZZRUN pFuzzRun;
    int rc = rtFuzzCmdMasterQueryFuzzRunFromJson(pThis, hJsonRoot, "Id", pErrInfo, &pFuzzRun);
    if (RT_SUCCESS(rc))
    {
        if (!pFuzzRun->fStarted)
        {
            rc = rtFuzzCmdMasterFuzzRunProcessCfgU32Def(&pFuzzRun->cProcs, "FuzzingProcs", hJsonRoot, pFuzzRun->cProcs, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                rc = RTFuzzObsExecStart(pFuzzRun->hFuzzObs, pFuzzRun->cProcs);
                if (RT_SUCCESS(rc))
                    pFuzzRun->fStarted = true;
                else
                    rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Resuming the fuzzing process failed");
            }
        }
    }

    return rc;
}


/**
 * Processes the "SaveFuzzingState" request.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing master command state.
 * @param   hJsonValRoot        The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterProcessJsonReqSaveState(PRTFUZZCMDMASTER pThis, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    PRTFUZZRUN pFuzzRun;
    int rc = rtFuzzCmdMasterQueryFuzzRunFromJson(pThis, hJsonRoot, "Id", pErrInfo, &pFuzzRun);
    if (RT_SUCCESS(rc))
    {
        /* Suspend fuzzing, save and resume if not stopped. */
        if (pFuzzRun->fStarted)
        {
            rc = RTFuzzObsExecStop(pFuzzRun->hFuzzObs);
            if (RT_FAILURE(rc))
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Suspending the fuzzing process failed");
        }

        if (RT_SUCCESS(rc))
        {
            RTFUZZCTX hFuzzCtx;
            rc = RTFuzzObsQueryCtx(pFuzzRun->hFuzzObs, &hFuzzCtx);
            AssertRC(rc);

            void *pvState = NULL;
            size_t cbState = 0;
            rc = RTFuzzCtxStateExportToMem(hFuzzCtx, &pvState, &cbState);
            if (RT_SUCCESS(rc))
            {
                /* Encode to base64. */
                size_t cbStateStr = RTBase64EncodedLength(cbState) + 1;
                char *pszState = (char *)RTMemAllocZ(cbStateStr);
                if (pszState)
                {
                    rc = RTBase64Encode(pvState, cbState, pszState, cbStateStr, &cbStateStr);
                    if (RT_SUCCESS(rc))
                    {
                        /* Strip all new lines from the srting. */
                        size_t offStr = 0;
                        while (offStr < cbStateStr)
                        {
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
                            char *pszEol = strchr(&pszState[offStr], '\r');
#else
                            char *pszEol = strchr(&pszState[offStr], '\n');
#endif
                            if (pszEol)
                            {
                                offStr += pszEol - &pszState[offStr];
                                memmove(pszEol, &pszEol[RTBASE64_EOL_SIZE], cbStateStr - offStr - RTBASE64_EOL_SIZE);
                                cbStateStr -= RTBASE64_EOL_SIZE;
                            }
                            else
                                break;
                        }

                        const char s_szState[] = "{ \"State\": %s }";
                        pThis->pszResponse = RTStrAPrintf2(s_szState, pszState);
                        if (RT_UNLIKELY(!pThis->pszResponse))
                            rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_BUFFER_OVERFLOW, "Request error: Response data buffer overflow", rc);
                    }
                    else
                        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to encode the state as a base64 string");
                    RTMemFree(pszState);
                }
                else
                    rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_NO_STR_MEMORY, "Request error: Failed to allocate a state string for the response");
                RTMemFree(pvState);
            }
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Exporting the state failed");
        }

        if (pFuzzRun->fStarted)
        {
            int rc2 = RTFuzzObsExecStart(pFuzzRun->hFuzzObs, pFuzzRun->cProcs);
            if (RT_FAILURE(rc2))
                rtFuzzCmdMasterErrorRc(pErrInfo, rc2, "Request error: Resuming the fuzzing process failed");
        }
    }

    return rc;
}


/**
 * Queries the statistics for the given fuzzing run and adds the result to the response.
 *
 * @returns IPRT static code.
 * @param   pThis               The fuzzing master command state.
 * @param   pFuzzRun            The fuzzing run.
 * @param   pszIndent           Indentation to use.
 * @param   fLast               Flags whether this is the last element in the list.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterProcessQueryRunStats(PRTFUZZCMDMASTER pThis, PRTFUZZRUN pFuzzRun,
                                               const char *pszIndent, bool fLast, PRTERRINFO pErrInfo)
{
    RTFUZZOBSSTATS ObsStats;
    RTFUZZCTXSTATS CtxStats;
    RTFUZZCTX hFuzzCtx;
    RT_ZERO(ObsStats); RT_ZERO(CtxStats);

    int rc = RTFuzzObsQueryCtx(pFuzzRun->hFuzzObs, &hFuzzCtx);
    if (RT_SUCCESS(rc))
    {
        rc = RTFuzzCtxQueryStats(hFuzzCtx, &CtxStats);
        RTFuzzCtxRelease(hFuzzCtx);
    }

    if (RT_SUCCESS(rc))
        rc = RTFuzzObsQueryStats(pFuzzRun->hFuzzObs, &ObsStats);
    if (RT_SUCCESS(rc))
    {
        const char s_szStatsFmt[] = "%s{ \n"
                                    "%s    \"Id\":                 \"%s\"\n"
                                    "%s    \"TimeCreated\":        \"%s\"\n"
                                    "%s    \"UptimeSec\":          %llu\n"
                                    "%s    \"FuzzedInputsPerSec\": %u\n"
                                    "%s    \"FuzzedInputs\":       %u\n"
                                    "%s    \"FuzzedInputsHang\":   %u\n"
                                    "%s    \"FuzzedInputsCrash\":  %u\n"
                                    "%s    \"MemoryUsage\":        %zu\n"
                                    "%s    \"CorpusSize\":         %llu\n"
                                    "%s}%s\n";
        char aszTime[_1K]; RT_ZERO(aszTime);
        char aszStats[_4K]; RT_ZERO(aszStats);

        if (RTTimeToString(&pFuzzRun->TimeCreated, aszTime, sizeof(aszTime)))
        {
            ssize_t cchStats = RTStrPrintf2(&aszStats[0], sizeof(aszStats),
                                            s_szStatsFmt, pszIndent,
                                            pszIndent, pFuzzRun->pszId,
                                            pszIndent, aszTime,
                                            pszIndent, (RTTimeMilliTS() - pFuzzRun->tsCreatedMs) / RT_MS_1SEC_64,
                                            pszIndent, ObsStats.cFuzzedInputsPerSec,
                                            pszIndent, ObsStats.cFuzzedInputs,
                                            pszIndent, ObsStats.cFuzzedInputsHang,
                                            pszIndent, ObsStats.cFuzzedInputsCrash,
                                            pszIndent, CtxStats.cbMemory,
                                            pszIndent, CtxStats.cMutations,
                                            pszIndent, fLast ? "" : ",");
            if (RT_LIKELY(cchStats > 0))
            {
                rc = RTStrAAppend(&pThis->pszResponse, &aszStats[0]);
                if (RT_FAILURE(rc))
                    rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to build statistics response", rc);
            }
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_BUFFER_OVERFLOW, "Request error: Response data buffer overflow");
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_BUFFER_OVERFLOW, "Request error: Buffer overflow conerting time to string");
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "Request error: Failed to query fuzzing statistics with %Rrc", rc);

    return rc;
}


/**
 * Processes the "QueryStats" request.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing master command state.
 * @param   hJsonValRoot        The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterProcessJsonReqQueryStats(PRTFUZZCMDMASTER pThis, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonValId;
    int rc = RTJsonValueQueryByName(hJsonRoot, "Id", &hJsonValId);
    if (RT_SUCCESS(rc))
    {
        RTJsonValueRelease(hJsonValId);
        PRTFUZZRUN pFuzzRun;
        rc = rtFuzzCmdMasterQueryFuzzRunFromJson(pThis, hJsonRoot, "Id", pErrInfo, &pFuzzRun);
        if (RT_SUCCESS(rc))
            rc = rtFuzzCmdMasterProcessQueryRunStats(pThis, pFuzzRun, "    ",
                                                     true /*fLast*/, pErrInfo);
    }
    else if (rc == VERR_NOT_FOUND)
    {
        /* Id is not there, so collect statistics of all running jobs. */
        rc = RTStrAAppend(&pThis->pszResponse, "    [\n");
        if (RT_SUCCESS(rc))
        {
            PRTFUZZRUN pRun = NULL;
            RTListForEach(&pThis->LstFuzzed, pRun, RTFUZZRUN, NdFuzzed)
            {
                bool fLast = RTListNodeIsLast(&pThis->LstFuzzed, &pRun->NdFuzzed);
                rc = rtFuzzCmdMasterProcessQueryRunStats(pThis, pRun, "        ", fLast, pErrInfo);
                if (RT_FAILURE(rc))
                    break;
            }
            if (RT_SUCCESS(rc))
                rc = RTStrAAppend(&pThis->pszResponse, "    ]\n");
        }
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Couldn't get \"Id\" value");

    return rc;
}


/**
 * Processes a JSON request.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing master command state.
 * @param   hJsonValRoot        The root node of the JSON request.
 * @param   pErrInfo            Where to store the error information on failure, optional.
 */
static int rtFuzzCmdMasterProcessJsonReq(PRTFUZZCMDMASTER pThis, RTJSONVAL hJsonRoot, PRTERRINFO pErrInfo)
{
    RTJSONVAL hJsonValReq;
    int rc = RTJsonValueQueryByName(hJsonRoot, "Request", &hJsonValReq);
    if (RT_SUCCESS(rc))
    {
        const char *pszReq = RTJsonValueGetString(hJsonValReq);
        if (pszReq)
        {
            if (!RTStrCmp(pszReq, "StartFuzzing"))
                rc = rtFuzzCmdMasterProcessJsonReqStart(pThis, hJsonRoot, pErrInfo);
            else if (!RTStrCmp(pszReq, "StopFuzzing"))
                rc = rtFuzzCmdMasterProcessJsonReqStop(pThis, hJsonRoot, pErrInfo);
            else if (!RTStrCmp(pszReq, "SuspendFuzzing"))
                rc = rtFuzzCmdMasterProcessJsonReqSuspend(pThis, hJsonRoot, pErrInfo);
            else if (!RTStrCmp(pszReq, "ResumeFuzzing"))
                rc = rtFuzzCmdMasterProcessJsonReqResume(pThis, hJsonRoot, pErrInfo);
            else if (!RTStrCmp(pszReq, "SaveFuzzingState"))
                rc = rtFuzzCmdMasterProcessJsonReqSaveState(pThis, hJsonRoot, pErrInfo);
            else if (!RTStrCmp(pszReq, "QueryStats"))
                rc = rtFuzzCmdMasterProcessJsonReqQueryStats(pThis, hJsonRoot, pErrInfo);
            else if (!RTStrCmp(pszReq, "Shutdown"))
                pThis->fShutdown = true;
            else
                rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_JSON_VALUE_INVALID_TYPE, "JSON request malformed: \"Request\" contains unknown value \"%s\"", pszReq);
        }
        else
            rc = rtFuzzCmdMasterErrorRc(pErrInfo, VERR_JSON_VALUE_INVALID_TYPE, "JSON request malformed: \"Request\" is not a string value");

        RTJsonValueRelease(hJsonValReq);
    }
    else
        rc = rtFuzzCmdMasterErrorRc(pErrInfo, rc, "JSON request malformed: Couldn't find \"Request\" value");

    return rc;
}


/**
 * Loads a fuzzing configuration for immediate startup from the given file.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing master command state.
 * @param   pszFuzzCfg          The fuzzing config to load.
 */
static int rtFuzzCmdMasterFuzzCfgLoadFromFile(PRTFUZZCMDMASTER pThis, const char *pszFuzzCfg)
{
    RTJSONVAL hJsonRoot;
    int rc = RTJsonParseFromFile(&hJsonRoot, pszFuzzCfg, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = rtFuzzCmdMasterProcessJsonReqStart(pThis, hJsonRoot, NULL);
        RTJsonValueRelease(hJsonRoot);
    }
    else
        rc = rtFuzzCmdMasterErrorRc(NULL, rc, "JSON request malformed: Couldn't load file \"%s\"", pszFuzzCfg);

    return rc;
}


/**
 * Destroys all running fuzzers for the given master state.
 *
 * @param   pThis               The fuzzing master command state.
 */
static void rtFuzzCmdMasterDestroy(PRTFUZZCMDMASTER pThis)
{
    RT_NOREF(pThis);
}


/**
 * Sends an ACK response to the client.
 *
 * @param   hSocket             The socket handle to send the ACK to.
 * @param   pszResponse         Additional response data.
 */
static void rtFuzzCmdMasterTcpSendAck(RTSOCKET hSocket, const char *pszResponse)
{
    const char s_szSucc[] = "{ \"Status\": \"ACK\" }\n";
    const char s_szSuccResp[] = "{ \"Status\": \"ACK\"\n  \"Response\":\n";
    const char s_szSuccRespClose[] = "\n }\n";
    if (pszResponse)
    {
        RTSGSEG aSegs[3];
        RTSGBUF SgBuf;
        aSegs[0].pvSeg = (void *)s_szSuccResp;
        aSegs[0].cbSeg = sizeof(s_szSuccResp) - 1;
        aSegs[1].pvSeg = (void *)pszResponse;
        aSegs[1].cbSeg = strlen(pszResponse);
        aSegs[2].pvSeg = (void *)s_szSuccRespClose;
        aSegs[2].cbSeg = sizeof(s_szSuccRespClose) - 1;

        RTSgBufInit(&SgBuf, &aSegs[0], RT_ELEMENTS(aSegs));
        RTTcpSgWrite(hSocket, &SgBuf);
    }
    else
        RTTcpWrite(hSocket, s_szSucc, sizeof(s_szSucc));
}


/**
 * Sends an NACK response to the client.
 *
 * @param   hSocket             The socket handle to send the ACK to.
 * @param   pErrInfo            Optional error information to send along.
 */
static void rtFuzzCmdMasterTcpSendNAck(RTSOCKET hSocket, PRTERRINFO pErrInfo)
{
    const char s_szFail[] = "{ \"Status\": \"NACK\" }\n";
    const char s_szFailInfo[] = "{ \"Status\": \"NACK\"\n \"Information\": \"%s\" }\n";

    if (pErrInfo)
    {
        char szTmp[_1K];
        ssize_t cchResp = RTStrPrintf2(szTmp, sizeof(szTmp), s_szFailInfo, pErrInfo->pszMsg);
        if (cchResp > 0)
            RTTcpWrite(hSocket, szTmp, cchResp);
        else
            RTTcpWrite(hSocket, s_szFail, strlen(s_szFail));
    }
    else
        RTTcpWrite(hSocket, s_szFail, strlen(s_szFail));
}


/**
 * TCP server serving callback for a single connection.
 *
 * @returns IPRT status code.
 * @param   hSocket             The socket handle of the connection.
 * @param   pvUser              Opaque user data.
 */
static DECLCALLBACK(int) rtFuzzCmdMasterTcpServe(RTSOCKET hSocket, void *pvUser)
{
    PRTFUZZCMDMASTER pThis = (PRTFUZZCMDMASTER)pvUser;
    size_t cbReqMax = _32K;
    size_t cbReq = 0;
    uint8_t *pbReq = (uint8_t *)RTMemAllocZ(cbReqMax);

    if (RT_LIKELY(pbReq))
    {
        uint8_t *pbCur = pbReq;

        for (;;)
        {
            size_t cbThisRead = cbReqMax - cbReq;
            int rc = RTTcpRead(hSocket, pbCur, cbThisRead, &cbThisRead);
            if (   RT_SUCCESS(rc)
                && cbThisRead)
            {
                cbReq += cbThisRead;

                /* Check for a zero terminator marking the end of the request. */
                uint8_t *pbEnd = (uint8_t *)memchr(pbCur, 0, cbThisRead);
                if (pbEnd)
                {
                    /* Adjust request size, data coming after the zero terminiator is ignored right now. */
                    cbReq -= cbThisRead - (pbEnd - pbCur) + 1;

                    RTJSONVAL hJsonReq;
                    RTERRINFOSTATIC ErrInfo;
                    RTErrInfoInitStatic(&ErrInfo);

                    rc = RTJsonParseFromBuf(&hJsonReq, pbReq, cbReq, &ErrInfo.Core);
                    if (RT_SUCCESS(rc))
                    {
                        rc = rtFuzzCmdMasterProcessJsonReq(pThis, hJsonReq, &ErrInfo.Core);
                        if (RT_SUCCESS(rc))
                            rtFuzzCmdMasterTcpSendAck(hSocket, pThis->pszResponse);
                        else
                            rtFuzzCmdMasterTcpSendNAck(hSocket, &ErrInfo.Core);
                        RTJsonValueRelease(hJsonReq);
                    }
                    else
                        rtFuzzCmdMasterTcpSendNAck(hSocket, &ErrInfo.Core);

                    if (pThis->pszResponse)
                    {
                        RTStrFree(pThis->pszResponse);
                        pThis->pszResponse = NULL;
                    }
                    break;
                }
                else if (cbReq == cbReqMax)
                {
                    /* Try to increase the buffer. */
                    uint8_t *pbReqNew = (uint8_t *)RTMemRealloc(pbReq, cbReqMax + _32K);
                    if (RT_LIKELY(pbReqNew))
                    {
                        cbReqMax += _32K;
                        pbReq = pbReqNew;
                        pbCur = pbReq + cbReq;
                    }
                    else
                        rtFuzzCmdMasterTcpSendNAck(hSocket, NULL);
                }
                else
                    pbCur += cbThisRead;
            }
            else
                break;
        }
    }
    else
        rtFuzzCmdMasterTcpSendNAck(hSocket, NULL);

    if (pbReq)
        RTMemFree(pbReq);

    return pThis->fShutdown ? VERR_TCP_SERVER_STOP : VINF_SUCCESS;
}


/**
 * Mainloop for the fuzzing master.
 *
 * @returns Process exit code.
 * @param   pThis               The fuzzing master command state.
 * @param   pszLoadCfg          Initial config to load.
 */
static RTEXITCODE rtFuzzCmdMasterRun(PRTFUZZCMDMASTER pThis, const char *pszLoadCfg)
{
    if (pszLoadCfg)
    {
        int rc = rtFuzzCmdMasterFuzzCfgLoadFromFile(pThis, pszLoadCfg);
        if (RT_FAILURE(rc))
            return RTEXITCODE_FAILURE;
    }

    /* Start up the control server. */
    int rc = RTTcpServerCreateEx(NULL, pThis->uPort, &pThis->hTcpSrv);
    if (RT_SUCCESS(rc))
    {
        do
        {
            rc = RTTcpServerListen(pThis->hTcpSrv, rtFuzzCmdMasterTcpServe, pThis);
        } while (rc != VERR_TCP_SERVER_STOP);
    }

    RTTcpServerDestroy(pThis->hTcpSrv);
    rtFuzzCmdMasterDestroy(pThis);
    return RTEXITCODE_SUCCESS;
}


RTR3DECL(RTEXITCODE) RTFuzzCmdMaster(unsigned cArgs, char **papszArgs)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--fuzz-config",                     'c', RTGETOPT_REQ_STRING  },
        { "--temp-dir",                        't', RTGETOPT_REQ_STRING  },
        { "--results-dir",                     'r', RTGETOPT_REQ_STRING  },
        { "--listen-port",                     'p', RTGETOPT_REQ_UINT16  },
        { "--daemonize",                       'd', RTGETOPT_REQ_NOTHING },
        { "--daemonized",                      'Z', RTGETOPT_REQ_NOTHING },
        { "--help",                            'h', RTGETOPT_REQ_NOTHING },
        { "--version",                         'V', RTGETOPT_REQ_NOTHING },
    };

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_SUCCESS(rc))
    {
        /* Option variables:  */
        bool fDaemonize = false;
        const char *pszLoadCfg = NULL;
        RTFUZZCMDMASTER This;

        RTListInit(&This.LstFuzzed);
        This.hTcpSrv       = NIL_RTTCPSERVER;
        This.uPort         = 4242;
        This.pszTmpDir     = NULL;
        This.pszResultsDir = NULL;
        This.fShutdown     = false;
        This.pszResponse   = NULL;

        /* Argument parsing loop. */
        bool fContinue = true;
        do
        {
            RTGETOPTUNION ValueUnion;
            int chOpt = RTGetOpt(&GetState, &ValueUnion);
            switch (chOpt)
            {
                case 0:
                    fContinue = false;
                    break;

                case 'c':
                    pszLoadCfg = ValueUnion.psz;
                    break;

                case 'p':
                    This.uPort = ValueUnion.u16;
                    break;

                case 't':
                    This.pszTmpDir = ValueUnion.psz;
                    break;

                case 'r':
                    This.pszResultsDir = ValueUnion.psz;
                    break;

                case 'd':
                    fDaemonize = true;
                    break;

                case 'Z':
                    fDaemonize = false;
                    break;

                case 'h':
                    RTPrintf("Usage: to be written\nOption dump:\n");
                    for (unsigned i = 0; i < RT_ELEMENTS(s_aOptions); i++)
                        RTPrintf(" -%c,%s\n", s_aOptions[i].iShort, s_aOptions[i].pszLong);
                    fContinue = false;
                    break;

                case 'V':
                    RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
                    fContinue = false;
                    break;

                default:
                    rcExit = RTGetOptPrintError(chOpt, &ValueUnion);
                    fContinue = false;
                    break;
            }
        } while (fContinue);

        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /*
             * Daemonize ourselves if asked to.
             */
            if (fDaemonize)
            {
                rc = RTProcDaemonize(papszArgs, "--daemonized");
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTProcDaemonize: %Rrc\n", rc);
            }
            else
                rcExit = rtFuzzCmdMasterRun(&This, pszLoadCfg);
        }
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_SYNTAX, "RTGetOptInit: %Rrc", rc);
    return rcExit;
}

