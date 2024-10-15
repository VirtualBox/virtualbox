/* $Id: tstRTDigest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTSha*, RTMd5, RTCrc*.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/sha.h>
#include <iprt/md2.h>
#include <iprt/md5.h>
#include <iprt/crc.h>

#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/crypto/digest.h>


static int Error(const char *pszFormat, ...)
{
    char szName[RTPATH_MAX];
    if (!RTProcGetExecutablePath(szName, sizeof(szName)))
        strcpy(szName, "tstRTDigest");

    RTStrmPrintf(g_pStdErr, "%s: error: ", RTPathFilename(szName));
    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    va_end(va);

    return 1;
}


static int MyReadFile(RTFILE hFile, void *pvBuf, size_t cbToRead, size_t *pcbRead, uint64_t *pcbMaxLeft)
{
    int rc = VINF_SUCCESS;
    if (*pcbMaxLeft > 0)
    {
        if (cbToRead > *pcbMaxLeft)
            cbToRead = (size_t)*pcbMaxLeft;
        rc = RTFileRead(hFile, pvBuf, cbToRead, pcbRead);
        if (RT_SUCCESS(rc))
            *pcbMaxLeft -= *pcbRead;
    }
    else
        *pcbRead = 0;
    return rc;
}


static char *MyGetNextSignificantLine(PRTSTREAM pFile, char *pszBuf, size_t cbBuf, uint32_t *piLine, int *prc)
{
    for (;;)
    {
        *pszBuf = '\0';
        int rc = RTStrmGetLine(pFile, pszBuf, cbBuf);
        if (RT_FAILURE(rc))
        {
            if (rc != VERR_EOF)
            {
                Error("Read error: %Rrc", rc);
                *prc = rc;
                return NULL;
            }
            if (!*pszBuf)
                return NULL;
        }
        *piLine += 1;

        /* Significant? */
        char *pszStart = RTStrStrip(pszBuf);
        if (*pszStart && *pszStart != '#')
            return pszStart;
    }
}


int main(int argc, char **argv)
{
     RTR3InitExe(argc, &argv, 0);

     RTDIGESTTYPE enmDigestType  = RTDIGESTTYPE_INVALID;
     const char  *pszDigestType  = "NotSpecified";

     enum
     {
         kMethod_Full,
         kMethod_Block,
         kMethod_File,
         kMethod_CVAS
     } enmMethod = kMethod_Block;

     uint64_t   offStart    = 0;
     uint64_t   cbMax       = UINT64_MAX;
     bool       fTestcase   = false;

     static const RTGETOPTDEF s_aOptions[] =
     {
         { "--type",   't', RTGETOPT_REQ_STRING },
         { "--method", 'm', RTGETOPT_REQ_STRING },
         { "--help",   'h', RTGETOPT_REQ_NOTHING },
         { "--length", 'l', RTGETOPT_REQ_UINT64 },
         { "--offset", 'o', RTGETOPT_REQ_UINT64 },
         { "--testcase", 'x', RTGETOPT_REQ_NOTHING },
     };

     int ch;
     RTGETOPTUNION ValueUnion;
     RTGETOPTSTATE GetState;
     RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
     while ((ch = RTGetOpt(&GetState, &ValueUnion)))
     {
         switch (ch)
         {
             case 't':
                 if (!RTStrICmp(ValueUnion.psz, "crc32"))
                 {
                     pszDigestType  = "CRC32";
                     enmDigestType  = RTDIGESTTYPE_CRC32;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "crc64"))
                 {
                     pszDigestType = "CRC64";
                     enmDigestType = RTDIGESTTYPE_CRC64;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "md2"))
                 {
                     pszDigestType = "MD2";
                     enmDigestType = RTDIGESTTYPE_MD2;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "md5"))
                 {
                     pszDigestType = "MD5";
                     enmDigestType = RTDIGESTTYPE_MD5;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "sha1"))
                 {
                     pszDigestType = "SHA-1";
                     enmDigestType = RTDIGESTTYPE_SHA1;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "sha224"))
                 {
                     pszDigestType = "SHA-224";
                     enmDigestType = RTDIGESTTYPE_SHA224;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "sha256"))
                 {
                     pszDigestType = "SHA-256";
                     enmDigestType = RTDIGESTTYPE_SHA256;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "sha384"))
                 {
                     pszDigestType = "SHA-384";
                     enmDigestType = RTDIGESTTYPE_SHA384;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "sha512"))
                 {
                     pszDigestType = "SHA-512";
                     enmDigestType = RTDIGESTTYPE_SHA512;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "sha512/224"))
                 {
                     pszDigestType = "SHA-512/224";
                     enmDigestType = RTDIGESTTYPE_SHA512T224;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "sha512/256"))
                 {
                     pszDigestType = "SHA-512/256";
                     enmDigestType = RTDIGESTTYPE_SHA3_256;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "sha3-224"))
                 {
                     pszDigestType = "SHA3-224";
                     enmDigestType = RTDIGESTTYPE_SHA3_224;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "sha3-256"))
                 {
                     pszDigestType = "SHA3-256";
                     enmDigestType = RTDIGESTTYPE_SHA3_256;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "sha3-384"))
                 {
                     pszDigestType = "SHA3-384";
                     enmDigestType = RTDIGESTTYPE_SHA3_384;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "sha3-512"))
                 {
                     pszDigestType = "SHA3-512";
                     enmDigestType = RTDIGESTTYPE_SHA3_512;
                 }
#if 0
                 else if (!RTStrICmp(ValueUnion.psz, "shake128"))
                 {
                     pszDigestType = "SHAKE128";
                     enmDigestType = RTDIGESTTYPE_SHAKE128;
                 }
                 else if (!RTStrICmp(ValueUnion.psz, "shake256"))
                 {
                     pszDigestType = "SHAKE256";
                     enmDigestType = RTDIGESTTYPE_SHAKE256;
                 }
#endif
                 else
                 {
                     Error("Invalid digest type: %s\n", ValueUnion.psz);
                     return 1;
                 }
                 break;

             case 'm':
                 if (!RTStrICmp(ValueUnion.psz, "full"))
                     enmMethod = kMethod_Full;
                 else if (!RTStrICmp(ValueUnion.psz, "block"))
                     enmMethod = kMethod_Block;
                 else if (!RTStrICmp(ValueUnion.psz, "file"))
                     enmMethod = kMethod_File;
                 else if (!RTStrICmp(ValueUnion.psz, "cvas"))
                     enmMethod = kMethod_CVAS;
                 else
                 {
                     Error("Invalid digest method: %s\n", ValueUnion.psz);
                     return 1;
                 }
                 break;

             case 'l':
                 cbMax = ValueUnion.u64;
                 break;

             case 'o':
                 offStart = ValueUnion.u64;
                 break;

             case 'x':
                 fTestcase = true;
                 break;

             case 'h':
                 RTPrintf("usage: tstRTDigest -t <digest-type> [-o <offset>] [-l <length>] [-m method] [-x] file [file2 [..]]\n"
                          "\n"
                          "Options:\n"
                          "  -t,--type <hash-algo>\n"
                          "  -o,--offset <file-offset>\n"
                          "  -l,--length <byte-count>\n"
                          "  -m,--method <full|block|file|cvas>\n"
                          "     block: Init+Update+Finalize, data from file(s). Default.\n"
                          "     file:  RTSha*DigestFromFile. Only SHA1 and SHA256.\n"
                          "     cvas:  NIST test vectors processed by RTCrDigest*.\n"
                          "     full:  Not implemented\n"
                          "  -x,--testcase\n"
                          "    For generating C code.\n"
                          );
                 return 1;

             case VINF_GETOPT_NOT_OPTION:
             {
                 if (enmDigestType == RTDIGESTTYPE_INVALID)
                     return Error("No digest type was specified\n");

                 switch (enmMethod)
                 {
                     case kMethod_Full:
                         return Error("Full file method is not implemented\n");

                     case kMethod_File:
                         if (offStart != 0 || cbMax != UINT64_MAX)
                             return Error("The -l and -o options do not work with the 'file' method.");
                         switch (enmDigestType)
                         {
                             case RTDIGESTTYPE_SHA1:
                             {
                                 char *pszDigest;
                                 int rc = RTSha1DigestFromFile(ValueUnion.psz, &pszDigest, NULL, NULL);
                                 if (RT_FAILURE(rc))
                                     return Error("RTSha1Digest(%s,) -> %Rrc\n", ValueUnion.psz, rc);
                                 RTPrintf("%s  %s\n", pszDigest, ValueUnion.psz);
                                 RTStrFree(pszDigest);
                                 break;
                             }

                             case RTDIGESTTYPE_SHA256:
                             {
                                 char *pszDigest;
                                 int rc = RTSha256DigestFromFile(ValueUnion.psz, &pszDigest, NULL, NULL);
                                 if (RT_FAILURE(rc))
                                     return Error("RTSha256Digest(%s,) -> %Rrc\n", ValueUnion.psz, rc);
                                 RTPrintf("%s  %s\n", pszDigest, ValueUnion.psz);
                                 RTStrFree(pszDigest);
                                 break;
                             }
                             default:
                                 return Error("The file method isn't implemented for this digest\n");
                         }
                         break;

                     case kMethod_Block:
                     {
                         RTFILE hFile;
                         int rc = RTFileOpen(&hFile, ValueUnion.psz, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
                         if (RT_FAILURE(rc))
                             return Error("RTFileOpen(,%s,) -> %Rrc\n", ValueUnion.psz, rc);
                         if (offStart != 0)
                         {
                             rc = RTFileSeek(hFile, offStart, RTFILE_SEEK_BEGIN, NULL);
                             if (RT_FAILURE(rc))
                                 return Error("RTFileSeek(%s,%ull) -> %Rrc\n", ValueUnion.psz, offStart, rc);
                         }

                         uint64_t cbMaxLeft = cbMax;
                         size_t  cbRead;
                         uint8_t abBuf[_64K];
                         char   *pszDigest = (char *)&abBuf[0];
                         switch (enmDigestType)
                         {
                             case RTDIGESTTYPE_CRC32:
                             {
                                 uint32_t uCRC32 = RTCrc32Start();
                                 for (;;)
                                 {
                                     rc = MyReadFile(hFile, abBuf, sizeof(abBuf), &cbRead, &cbMaxLeft);
                                     if (RT_FAILURE(rc) || !cbRead)
                                         break;
                                     uCRC32 = RTCrc32Process(uCRC32, abBuf, cbRead);
                                 }
                                 uCRC32 = RTCrc32Finish(uCRC32);
                                 RTStrPrintf(pszDigest, sizeof(abBuf), "%08RX32", uCRC32);
                                 break;
                             }

                             case RTDIGESTTYPE_CRC64:
                             {
                                 uint64_t uCRC64 = RTCrc64Start();
                                 for (;;)
                                 {
                                     rc = MyReadFile(hFile, abBuf, sizeof(abBuf), &cbRead, &cbMaxLeft);
                                     if (RT_FAILURE(rc) || !cbRead)
                                         break;
                                     uCRC64 = RTCrc64Process(uCRC64, abBuf, cbRead);
                                 }
                                 uCRC64 = RTCrc64Finish(uCRC64);
                                 RTStrPrintf(pszDigest, sizeof(abBuf), "%016RX64", uCRC64);
                                 break;
                             }

                             case RTDIGESTTYPE_MD2:
                             {
                                 RTMD2CONTEXT Ctx;
                                 RTMd2Init(&Ctx);
                                 for (;;)
                                 {
                                     rc = MyReadFile(hFile, abBuf, sizeof(abBuf), &cbRead, &cbMaxLeft);
                                     if (RT_FAILURE(rc) || !cbRead)
                                         break;
                                     RTMd2Update(&Ctx, abBuf, cbRead);
                                 }
                                 uint8_t abDigest[RTMD2_HASH_SIZE];
                                 RTMd2Final(&Ctx, abDigest);
                                 RTMd2ToString(abDigest, pszDigest, sizeof(abBuf));
                                 break;
                             }

                             case RTDIGESTTYPE_MD5:
                             {
                                 RTMD5CONTEXT Ctx;
                                 RTMd5Init(&Ctx);
                                 for (;;)
                                 {
                                     rc = MyReadFile(hFile, abBuf, sizeof(abBuf), &cbRead, &cbMaxLeft);
                                     if (RT_FAILURE(rc) || !cbRead)
                                         break;
                                     RTMd5Update(&Ctx, abBuf, cbRead);
                                 }
                                 uint8_t abDigest[RTMD5HASHSIZE];
                                 RTMd5Final(abDigest, &Ctx);
                                 RTMd5ToString(abDigest, pszDigest, sizeof(abBuf));
                                 break;
                             }

                             case RTDIGESTTYPE_SHA1:
                             {
                                 RTSHA1CONTEXT Ctx;
                                 RTSha1Init(&Ctx);
                                 for (;;)
                                 {
                                     rc = MyReadFile(hFile, abBuf, sizeof(abBuf), &cbRead, &cbMaxLeft);
                                     if (RT_FAILURE(rc) || !cbRead)
                                         break;
                                     RTSha1Update(&Ctx, abBuf, cbRead);
                                 }
                                 uint8_t abDigest[RTSHA1_HASH_SIZE];
                                 RTSha1Final(&Ctx, abDigest);
                                 RTSha1ToString(abDigest, pszDigest, sizeof(abBuf));
                                 break;
                             }

                             case RTDIGESTTYPE_SHA256:
                             {
                                 RTSHA256CONTEXT Ctx;
                                 RTSha256Init(&Ctx);
                                 for (;;)
                                 {
                                     rc = MyReadFile(hFile, abBuf, sizeof(abBuf), &cbRead, &cbMaxLeft);
                                     if (RT_FAILURE(rc) || !cbRead)
                                         break;
                                     RTSha256Update(&Ctx, abBuf, cbRead);
                                 }
                                 uint8_t abDigest[RTSHA256_HASH_SIZE];
                                 RTSha256Final(&Ctx, abDigest);
                                 RTSha256ToString(abDigest, pszDigest, sizeof(abBuf));
                                 break;
                             }

                             case RTDIGESTTYPE_SHA512:
                             {
                                 RTSHA512CONTEXT Ctx;
                                 RTSha512Init(&Ctx);
                                 for (;;)
                                 {
                                     rc = MyReadFile(hFile, abBuf, sizeof(abBuf), &cbRead, &cbMaxLeft);
                                     if (RT_FAILURE(rc) || !cbRead)
                                         break;
                                     RTSha512Update(&Ctx, abBuf, cbRead);
                                 }
                                 uint8_t abDigest[RTSHA512_HASH_SIZE];
                                 RTSha512Final(&Ctx, abDigest);
                                 RTSha512ToString(abDigest, pszDigest, sizeof(abBuf));
                                 break;
                             }

                             case RTDIGESTTYPE_SHA3_224:
                             {
                                 RTSHA3T224CONTEXT Ctx;
                                 RTSha3t224Init(&Ctx);
                                 for (;;)
                                 {
                                     rc = MyReadFile(hFile, abBuf, sizeof(abBuf), &cbRead, &cbMaxLeft);
                                     if (RT_FAILURE(rc) || !cbRead)
                                         break;
                                     RTSha3t224Update(&Ctx, abBuf, cbRead);
                                 }
                                 uint8_t abDigest[RTSHA3_224_HASH_SIZE];
                                 RTSha3t224Final(&Ctx, abDigest);
                                 RTSha3t224ToString(abDigest, pszDigest, sizeof(abBuf));
                                 break;
                             }

                             case RTDIGESTTYPE_SHA3_256:
                             {
                                 RTSHA3T256CONTEXT Ctx;
                                 RTSha3t256Init(&Ctx);
                                 for (;;)
                                 {
                                     rc = MyReadFile(hFile, abBuf, sizeof(abBuf), &cbRead, &cbMaxLeft);
                                     if (RT_FAILURE(rc) || !cbRead)
                                         break;
                                     RTSha3t256Update(&Ctx, abBuf, cbRead);
                                 }
                                 uint8_t abDigest[RTSHA3_256_HASH_SIZE];
                                 RTSha3t256Final(&Ctx, abDigest);
                                 RTSha3t256ToString(abDigest, pszDigest, sizeof(abBuf));
                                 break;
                             }

                             case RTDIGESTTYPE_SHA3_384:
                             {
                                 RTSHA3T384CONTEXT Ctx;
                                 RTSha3t384Init(&Ctx);
                                 for (;;)
                                 {
                                     rc = MyReadFile(hFile, abBuf, sizeof(abBuf), &cbRead, &cbMaxLeft);
                                     if (RT_FAILURE(rc) || !cbRead)
                                         break;
                                     RTSha3t384Update(&Ctx, abBuf, cbRead);
                                 }
                                 uint8_t abDigest[RTSHA3_384_HASH_SIZE];
                                 RTSha3t384Final(&Ctx, abDigest);
                                 RTSha3t384ToString(abDigest, pszDigest, sizeof(abBuf));
                                 break;
                             }

                             case RTDIGESTTYPE_SHA3_512:
                             {
                                 RTSHA3T512CONTEXT Ctx;
                                 RTSha3t512Init(&Ctx);
                                 for (;;)
                                 {
                                     rc = MyReadFile(hFile, abBuf, sizeof(abBuf), &cbRead, &cbMaxLeft);
                                     if (RT_FAILURE(rc) || !cbRead)
                                         break;
                                     RTSha3t512Update(&Ctx, abBuf, cbRead);
                                 }
                                 uint8_t abDigest[RTSHA3_512_HASH_SIZE];
                                 RTSha3t512Final(&Ctx, abDigest);
                                 RTSha3t512ToString(abDigest, pszDigest, sizeof(abBuf));
                                 break;
                             }

                             /** @todo SHAKE128 and SHAKE256   */

                             default:
                                 return Error("Internal error #1: %d %s\n", enmDigestType, pszDigest);
                         }
                         RTFileClose(hFile);
                         if (RT_FAILURE(rc) && rc != VERR_EOF)
                         {
                             RTPrintf("Partial: %s  %s\n", pszDigest, ValueUnion.psz);
                             return Error("RTFileRead(%s) -> %Rrc\n", ValueUnion.psz, rc);
                         }

                         if (!fTestcase)
                             RTPrintf("%s  %s\n", pszDigest, ValueUnion.psz);
                         else if (offStart)
                             RTPrintf("        { &g_abRandom72KB[%#4llx], %5llu, \"%s\", \"%s %llu bytes @%llu\" },\n",
                                      offStart, cbMax - cbMaxLeft, pszDigest, pszDigestType, offStart, cbMax - cbMaxLeft);
                         else
                             RTPrintf("        { &g_abRandom72KB[0],     %5llu, \"%s\", \"%s %llu bytes\" },\n",
                                      cbMax - cbMaxLeft, pszDigest, pszDigestType, cbMax - cbMaxLeft);
                         break;
                     }


                     /*
                      * Process a SHS response file:
                      *     http://csrc.nist.gov/groups/STM/cavp/index.html#03
                      */
                     case kMethod_CVAS:
                     {
                         RTCRDIGEST hDigest;
                         int rc = RTCrDigestCreateByType(&hDigest, enmDigestType);
                         if (RT_FAILURE(rc))
                             return Error("Failed to create digest calculator for %s: %Rrc", pszDigestType, rc);

                         uint32_t const cbDigest = RTCrDigestGetHashSize(hDigest);
                         if (!cbDigest || cbDigest >= _1K)
                             return Error("Unexpected hash size: %#x\n", cbDigest);

                         PRTSTREAM pFile;
                         rc = RTStrmOpen(ValueUnion.psz, "r", &pFile);
                         if (RT_FAILURE(rc))
                             return Error("Failed to open CVAS file '%s': %Rrc\n", ValueUnion.psz, rc);

                         /*
                          * Parse the input file.
                          * ASSUME order: Len, Msg, MD.
                          */
                         static char    s_szLine[_256K];
                         char          *psz;
                         uint32_t       cPassed = 0;
                         uint32_t       cErrors = 0;
                         uint32_t       iLine   = 1;
                         for (;;)
                         {
                             psz = MyGetNextSignificantLine(pFile, s_szLine, sizeof(s_szLine), &iLine, &rc);
                             if (!psz)
                                 break;

                             /* Skip [L = 20] stuff. */
                             if (*psz == '[')
                                 continue;

                             /* Message length. */
                             uint64_t cMessageBits;
                             if (RTStrNICmp(psz, RT_STR_TUPLE("Len =")))
                                 return Error("%s(%d): Expected 'Len =' found '%.10s...'", ValueUnion.psz, iLine, psz);
                             psz = RTStrStripL(psz + 5);
                             rc = RTStrToUInt64Full(psz, 0, &cMessageBits);
                             if (rc != VINF_SUCCESS)
                                 return Error("%s(%d): Error parsing length '%s': %Rrc\n", ValueUnion.psz, iLine, psz, rc);

                             /* The message text. */
                             psz = MyGetNextSignificantLine(pFile, s_szLine, sizeof(s_szLine), &iLine, &rc);
                             if (!psz)
                                 return Error("%s(%d): Expected message text not EOF.", ValueUnion.psz, iLine);
                             if (RTStrNICmp(psz, RT_STR_TUPLE("Msg =")))
                                 return Error("%s(%d): Expected 'Msg =' found '%.10s...'", ValueUnion.psz, iLine, psz);
                             psz = RTStrStripL(psz + 5);

                             size_t const   cbMessage = (cMessageBits + 7) / 8;
                             static uint8_t s_abMessage[sizeof(s_szLine) / 2];
                             if (cbMessage > 0)
                             {
                                 rc = RTStrConvertHexBytes(psz, s_abMessage, cbMessage, 0 /*fFlags*/);
                                 if (rc != VINF_SUCCESS)
                                     return Error("%s(%d): Error parsing message '%.10s...': %Rrc\n",
                                                  ValueUnion.psz, iLine, psz, rc);
                             }

                             /* The message digest. */
                             psz = MyGetNextSignificantLine(pFile, s_szLine, sizeof(s_szLine), &iLine, &rc);
                             if (!psz)
                                 return Error("%s(%d): Expected message digest not EOF.", ValueUnion.psz, iLine);
                             if (RTStrNICmp(psz, RT_STR_TUPLE("MD =")))
                                 return Error("%s(%d): Expected 'MD =' found '%.10s...'", ValueUnion.psz, iLine, psz);
                             psz = RTStrStripL(psz + 4);

                             static uint8_t s_abExpectedDigest[_1K];
                             rc = RTStrConvertHexBytes(psz, s_abExpectedDigest, cbDigest, 0 /*fFlags*/);
                             if (rc != VINF_SUCCESS)
                                 return Error("%s(%d): Error parsing message digest '%.10s...': %Rrc\n",
                                              ValueUnion.psz, iLine, psz, rc);

                             /*
                              * Do the testing.
                              */
                             rc = RTCrDigestReset(hDigest);
                             if (rc != VINF_SUCCESS)
                                 return Error("RTCrDigestReset failed: %Rrc", rc);

                             rc = RTCrDigestUpdate(hDigest, s_abMessage, cbMessage);
                             if (rc != VINF_SUCCESS)
                                 return Error("RTCrDigestUpdate failed: %Rrc", rc);

                             static uint8_t s_abActualDigest[_1K];
                             rc = RTCrDigestFinal(hDigest, s_abActualDigest, cbDigest);
                             if (rc != VINF_SUCCESS)
                                 return Error("RTCrDigestFinal failed: %Rrc", rc);

                             if (memcmp(s_abActualDigest, s_abExpectedDigest, cbDigest) == 0)
                                 cPassed++;
                             else
                             {
                                 Error("%s(%d): Message digest mismatch. Expected %.*RThxs, got %.*RThxs.",
                                       ValueUnion.psz, iLine, cbDigest, s_abExpectedDigest, cbDigest, s_abActualDigest);
                                 cErrors++;
                             }
                         }

                         RTStrmClose(pFile);
                         if (cErrors > 0)
                             return Error("Failed: %u error%s (%u passed)", cErrors, cErrors == 1 ? "" : "s", cPassed);
                         RTPrintf("Passed %u test%s.\n", cPassed, cPassed == 1 ? "" : "s");
                         if (RT_FAILURE(rc))
                             return Error("Failed: %Rrc", rc);
                         break;
                     }

                     default:
                         return Error("Internal error #2\n");
                 }
                 break;
             }

             default:
                return RTGetOptPrintError(ch, &ValueUnion);
         }
     }

     return 0;
}

