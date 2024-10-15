/* $Id: manifest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Manifest file handling, old style - deprecated.
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
#include "internal/iprt.h"
#include <iprt/manifest.h>

#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/sha.h>
#include <iprt/stream.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Internal per file structure used by RTManifestVerify
 */
typedef struct RTMANIFESTFILEENTRY
{
    char *pszManifestFile;
    char *pszManifestDigest;
    PRTMANIFESTTEST pTestPattern;
} RTMANIFESTFILEENTRY;
typedef RTMANIFESTFILEENTRY* PRTMANIFESTFILEENTRY;

/**
 * Internal structure used for the progress callback
 */
typedef struct RTMANIFESTCALLBACKDATA
{
    PFNRTPROGRESS pfnProgressCallback;
    void *pvUser;
    size_t cMaxFiles;
    size_t cCurrentFile;
} RTMANIFESTCALLBACKDATA;
typedef RTMANIFESTCALLBACKDATA* PRTMANIFESTCALLBACKDATA;


/*******************************************************************************
*   Private functions
*******************************************************************************/

DECLINLINE(char *) rtManifestPosOfCharInBuf(char const *pv, size_t cb, char c)
{
    char *pb = (char *)pv;
    for (; cb; --cb, ++pb)
        if (RT_UNLIKELY(*pb == c))
            return pb;
    return NULL;
}

DECLINLINE(size_t) rtManifestIndexOfCharInBuf(char const *pv, size_t cb, char c)
{
    char const *pb = (char const *)pv;
    for (size_t i=0; i < cb; ++i, ++pb)
        if (RT_UNLIKELY(*pb == c))
            return i;
    return cb;
}

static DECLCALLBACK(int) rtSHAProgressCallback(unsigned uPercent, void *pvUser)
{
    PRTMANIFESTCALLBACKDATA pData = (PRTMANIFESTCALLBACKDATA)pvUser;
    return pData->pfnProgressCallback((unsigned)(  (uPercent + (float)pData->cCurrentFile * 100.0)
                                                 / (float)pData->cMaxFiles),
                                      pData->pvUser);
}


/*******************************************************************************
*   Public functions
*******************************************************************************/

RTR3DECL(int) RTManifestVerify(const char *pszManifestFile, PRTMANIFESTTEST paTests, size_t cTests, size_t *piFailed)
{
    /* Validate input */
    AssertPtrReturn(pszManifestFile, VERR_INVALID_POINTER);

    /* Open the manifest file */
    RTFILE file;
    int rc = RTFileOpen(&file, pszManifestFile, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return rc;

    void *pvBuf = 0;
    do
    {
        uint64_t cbSize;
        rc = RTFileQuerySize(file, &cbSize);
        if (RT_FAILURE(rc))
            break;

        /* Cast down for the case size_t < uint64_t. This isn't really correct,
           but we consider manifest files bigger than size_t as not supported
           by now. */
        size_t cbToRead = (size_t)cbSize;
        pvBuf = RTMemAlloc(cbToRead);
        if (!pvBuf)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        size_t cbRead = 0;
        rc = RTFileRead(file, pvBuf, cbToRead, &cbRead);
        if (RT_FAILURE(rc))
            break;

        rc = RTManifestVerifyFilesBuf(pvBuf, cbRead, paTests, cTests, piFailed);
    }while (0);

    /* Cleanup */
    if (pvBuf)
        RTMemFree(pvBuf);

    RTFileClose(file);

    return rc;
}

RTR3DECL(int) RTManifestVerifyFiles(const char *pszManifestFile, const char * const *papszFiles, size_t cFiles, size_t *piFailed,
                                    PFNRTPROGRESS pfnProgressCallback, void *pvUser)
{
    /* Validate input */
    AssertPtrReturn(pszManifestFile, VERR_INVALID_POINTER);
    AssertPtrReturn(papszFiles, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnProgressCallback, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    /* Create our compare list */
    PRTMANIFESTTEST paFiles = (PRTMANIFESTTEST)RTMemTmpAllocZ(sizeof(RTMANIFESTTEST) * cFiles);
    if (!paFiles)
        return VERR_NO_MEMORY;

    RTMANIFESTCALLBACKDATA callback = { pfnProgressCallback, pvUser, cFiles, 0 };
    /* Fill our compare list */
    for (size_t i = 0; i < cFiles; ++i)
    {
        char *pszDigest;
        if (pfnProgressCallback)
        {
            callback.cCurrentFile = i;
            rc = RTSha1DigestFromFile(papszFiles[i], &pszDigest, rtSHAProgressCallback, &callback);
        }
        else
            rc = RTSha1DigestFromFile(papszFiles[i], &pszDigest, NULL, NULL);
        if (RT_FAILURE(rc))
            break;
        paFiles[i].pszTestFile = (char*)papszFiles[i];
        paFiles[i].pszTestDigest = pszDigest;
    }

    /* Do the verification */
    if (RT_SUCCESS(rc))
        rc = RTManifestVerify(pszManifestFile, paFiles, cFiles, piFailed);

    /* Cleanup */
    for (size_t i = 0; i < cFiles; ++i)
    {
        if (paFiles[i].pszTestDigest)
            RTStrFree((char*)paFiles[i].pszTestDigest);
    }
    RTMemTmpFree(paFiles);

    return rc;
}

RTR3DECL(int) RTManifestWriteFiles(const char *pszManifestFile, RTDIGESTTYPE enmDigestType,
                                   const char * const *papszFiles, size_t cFiles,
                                   PFNRTPROGRESS pfnProgressCallback, void *pvUser)
{
    /* Validate input */
    AssertPtrReturn(pszManifestFile, VERR_INVALID_POINTER);
    AssertPtrReturn(papszFiles, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnProgressCallback, VERR_INVALID_POINTER);

    RTFILE file;
    int rc = RTFileOpen(&file, pszManifestFile, RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_ALL);
    if (RT_FAILURE(rc))
        return rc;

    PRTMANIFESTTEST paFiles = 0;
    void *pvBuf = 0;
    do
    {
        paFiles = (PRTMANIFESTTEST)RTMemAllocZ(sizeof(RTMANIFESTTEST) * cFiles);
        if (!paFiles)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        RTMANIFESTCALLBACKDATA callback = { pfnProgressCallback, pvUser, cFiles, 0 };
        for (size_t i = 0; i < cFiles; ++i)
        {
            paFiles[i].pszTestFile = papszFiles[i];
            /* Calculate the SHA1 digest of every file */
            if (pfnProgressCallback)
            {
                callback.cCurrentFile = i;
                rc = RTSha1DigestFromFile(paFiles[i].pszTestFile, (char**)&paFiles[i].pszTestDigest, rtSHAProgressCallback, &callback);
            }
            else
                rc = RTSha1DigestFromFile(paFiles[i].pszTestFile, (char**)&paFiles[i].pszTestDigest, NULL, NULL);
            if (RT_FAILURE(rc))
                break;
        }

        if (RT_SUCCESS(rc))
        {
            size_t cbSize = 0;
            rc = RTManifestWriteFilesBuf(&pvBuf, &cbSize, enmDigestType, paFiles, cFiles);
            if (RT_FAILURE(rc))
                break;

            rc = RTFileWrite(file, pvBuf, cbSize, 0);
        }
    }while (0);

    RTFileClose(file);

    /* Cleanup */
    if (pvBuf)
        RTMemFree(pvBuf);
    if (paFiles)
    {
        for (size_t i = 0; i < cFiles; ++i)
            if (paFiles[i].pszTestDigest)
                RTStrFree((char*)paFiles[i].pszTestDigest);
        RTMemFree(paFiles);
    }

    /* Delete the manifest file on failure */
    if (RT_FAILURE(rc))
        RTFileDelete(pszManifestFile);

    return rc;
}


RTR3DECL(int) RTManifestVerifyDigestType(void const *pvBuf, size_t cbSize, RTDIGESTTYPE *penmDigestType)
{
    /* Validate input */
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbSize > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(penmDigestType, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    char const *pcBuf = (char *)pvBuf;
    size_t cbRead = 0;
    /* Parse the manifest file line by line */
    for (;;)
    {
        if (cbRead >= cbSize)
            return VERR_MANIFEST_UNSUPPORTED_DIGEST_TYPE;

        size_t cch = rtManifestIndexOfCharInBuf(pcBuf, cbSize - cbRead, '\n') + 1;

        /* Skip empty lines (UNIX/DOS format) */
        if (   (   cch == 1
                && pcBuf[0] == '\n')
            || (   cch == 2
                && pcBuf[0] == '\r'
                && pcBuf[1] == '\n'))
        {
            pcBuf += cch;
            cbRead += cch;
            continue;
        }

/** @todo r=bird: Missing space check here. */
        /* Check for the digest algorithm */
        if (   pcBuf[0] == 'S'
            && pcBuf[1] == 'H'
            && pcBuf[2] == 'A'
            && pcBuf[3] == '1')
        {
            *penmDigestType = RTDIGESTTYPE_SHA1;
            break;
        }
        if (   pcBuf[0] == 'S'
            && pcBuf[1] == 'H'
            && pcBuf[2] == 'A'
            && pcBuf[3] == '2'
            && pcBuf[4] == '5'
            && pcBuf[5] == '6')
        {
            *penmDigestType = RTDIGESTTYPE_SHA256;
            break;
        }

        pcBuf += cch;
        cbRead += cch;
    }

    return rc;
}


RTR3DECL(int) RTManifestVerifyFilesBuf(void *pvBuf, size_t cbSize, PRTMANIFESTTEST paTests, size_t cTests, size_t *piFailed)
{
    /* Validate input */
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbSize > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(paTests, VERR_INVALID_POINTER);
    AssertReturn(cTests > 0, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(piFailed, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    PRTMANIFESTFILEENTRY paFiles = (PRTMANIFESTFILEENTRY)RTMemTmpAllocZ(sizeof(RTMANIFESTFILEENTRY) * cTests);
    if (!paFiles)
        return VERR_NO_MEMORY;

    /* Fill our compare list */
    for (size_t i = 0; i < cTests; ++i)
        paFiles[i].pTestPattern = &paTests[i];

    char *pcBuf = (char*)pvBuf;
    size_t cbRead = 0;
    /* Parse the manifest file line by line */
    for (;;)
    {
        if (cbRead >= cbSize)
            break;

        size_t cch = rtManifestIndexOfCharInBuf(pcBuf, cbSize - cbRead, '\n') + 1;

        /* Skip empty lines (UNIX/DOS format) */
        if (   (   cch == 1
                && pcBuf[0] == '\n')
            || (   cch == 2
                && pcBuf[0] == '\r'
                && pcBuf[1] == '\n'))
        {
            pcBuf += cch;
            cbRead += cch;
            continue;
        }

        /** @todo r=bird:
         *  -# Better deal with this EOF line platform dependency
         *  -# The SHA1 and SHA256 tests should probably include a blank space check.
         *  -# If there is a specific order to the elements in the string, it would be
         *     good if the delimiter searching checked for it.
         *  -# Deal with filenames containing delimiter characters.
         */

        /* Check for the digest algorithm */
        if (   cch < 4
            || (   !(   pcBuf[0] == 'S'
                     && pcBuf[1] == 'H'
                     && pcBuf[2] == 'A'
                     && pcBuf[3] == '1')
                &&
                   !(   pcBuf[0] == 'S'
                     && pcBuf[1] == 'H'
                     && pcBuf[2] == 'A'
                     && pcBuf[3] == '2'
                     && pcBuf[4] == '5'
                     && pcBuf[5] == '6')
               )
            )
        {
            /* Digest unsupported */
            rc = VERR_MANIFEST_UNSUPPORTED_DIGEST_TYPE;
            break;
        }

        /* Try to find the filename */
        char *pszNameStart = rtManifestPosOfCharInBuf(pcBuf, cch, '(');
        if (!pszNameStart)
        {
            rc = VERR_MANIFEST_WRONG_FILE_FORMAT;
            break;
        }
        char *pszNameEnd = rtManifestPosOfCharInBuf(pcBuf, cch, ')');
        if (!pszNameEnd)
        {
            rc = VERR_MANIFEST_WRONG_FILE_FORMAT;
            break;
        }

        /* Copy the filename part */
        size_t cchName = pszNameEnd - pszNameStart - 1;
        char *pszName = (char *)RTMemTmpAlloc(cchName + 1);
        if (!pszName)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        memcpy(pszName, pszNameStart + 1, cchName);
        pszName[cchName] = '\0';

        /* Try to find the digest sum */
        char *pszDigestStart = rtManifestPosOfCharInBuf(pcBuf, cch, '=') + 1;
        if (!pszDigestStart)
        {
            RTMemTmpFree(pszName);
            rc = VERR_MANIFEST_WRONG_FILE_FORMAT;
            break;
        }
        char *pszDigestEnd = rtManifestPosOfCharInBuf(pcBuf, cch, '\r');
        if (!pszDigestEnd)
            pszDigestEnd = rtManifestPosOfCharInBuf(pcBuf, cch, '\n');
        if (!pszDigestEnd)
        {
            RTMemTmpFree(pszName);
            rc = VERR_MANIFEST_WRONG_FILE_FORMAT;
            break;
        }
        /* Copy the digest part */
        size_t cchDigest = pszDigestEnd - pszDigestStart - 1;
        char *pszDigest = (char *)RTMemTmpAlloc(cchDigest + 1);
        if (!pszDigest)
        {
            RTMemTmpFree(pszName);
            rc = VERR_NO_MEMORY;
            break;
        }
        memcpy(pszDigest, pszDigestStart + 1, cchDigest);
        pszDigest[cchDigest] = '\0';

        /* Check our file list against the extracted data */
        bool fFound = false;
        for (size_t i = 0; i < cTests; ++i)
        {
            /** @todo r=bird: Using RTStrStr here looks bogus. */
            if (RTStrStr(paFiles[i].pTestPattern->pszTestFile, RTStrStrip(pszName)) != NULL)
            {
                /* Add the data of the manifest file to the file list */
                paFiles[i].pszManifestFile = RTStrDup(RTStrStrip(pszName));
                paFiles[i].pszManifestDigest = RTStrDup(RTStrStrip(pszDigest));
                fFound = true;
                break;
            }
        }
        RTMemTmpFree(pszName);
        RTMemTmpFree(pszDigest);
        if (!fFound)
        {
            /* There have to be an entry in the file list */
            rc = VERR_MANIFEST_FILE_MISMATCH;
            break;
        }

        pcBuf += cch;
        cbRead += cch;
    }

    if (   rc == VINF_SUCCESS
        || rc == VERR_EOF)
    {
        rc = VINF_SUCCESS;
        for (size_t i = 0; i < cTests; ++i)
        {
            /* If there is an entry in the file list, which hasn't an
             * equivalent in the manifest file, its an error. */
            if (   !paFiles[i].pszManifestFile
                || !paFiles[i].pszManifestDigest)
            {
                rc = VERR_MANIFEST_FILE_MISMATCH;
                break;
            }

            /* Do the manifest SHA digest match against the actual digest? */
            if (RTStrICmp(paFiles[i].pszManifestDigest, paFiles[i].pTestPattern->pszTestDigest))
            {
                if (piFailed)
                    *piFailed = i;
                rc = VERR_MANIFEST_DIGEST_MISMATCH;
                break;
            }
        }
    }

    /* Cleanup */
    for (size_t i = 0; i < cTests; ++i)
    {
        if (paFiles[i].pszManifestFile)
            RTStrFree(paFiles[i].pszManifestFile);
        if (paFiles[i].pszManifestDigest)
            RTStrFree(paFiles[i].pszManifestDigest);
    }
    RTMemTmpFree(paFiles);

    return rc;
}

RTR3DECL(int) RTManifestWriteFilesBuf(void **ppvBuf, size_t *pcbSize, RTDIGESTTYPE enmDigestType, PRTMANIFESTTEST paFiles, size_t cFiles)
{
    /* Validate input */
    AssertPtrReturn(ppvBuf, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);
    AssertPtrReturn(paFiles, VERR_INVALID_POINTER);
    AssertReturn(cFiles > 0, VERR_INVALID_PARAMETER);

    const char *pcszDigestType;
    switch (enmDigestType)
    {
        case RTDIGESTTYPE_CRC32:  pcszDigestType = "CRC32";  break;
        case RTDIGESTTYPE_CRC64:  pcszDigestType = "CRC64";  break;
        case RTDIGESTTYPE_MD5:    pcszDigestType = "MD5";    break;
        case RTDIGESTTYPE_SHA1:   pcszDigestType = "SHA1";   break;
        case RTDIGESTTYPE_SHA256: pcszDigestType = "SHA256"; break;
        default: return VERR_INVALID_PARAMETER;
    }

    /* Calculate the size necessary for the memory buffer. */
    size_t cbSize = 0;
    size_t cbMaxSize = 0;
    for (size_t i = 0; i < cFiles; ++i)
    {
        size_t cbTmp = strlen(RTPathFilename(paFiles[i].pszTestFile))
                     + strlen(paFiles[i].pszTestDigest)
                     + strlen(pcszDigestType)
                     + 6;
        cbMaxSize = RT_MAX(cbMaxSize, cbTmp);
        cbSize += cbTmp;
    }

    /* Create the memory buffer */
    void *pvBuf = RTMemAlloc(cbSize);
    if (!pvBuf)
        return VERR_NO_MEMORY;

    /* Allocate a temporary string buffer. */
    char *pszTmp = RTStrAlloc(cbMaxSize + 1);
    if (!pszTmp)
    {
        RTMemFree(pvBuf);
        return VERR_NO_MEMORY;
    }
    size_t cbPos = 0;

    for (size_t i = 0; i < cFiles; ++i)
    {
        size_t cch = RTStrPrintf(pszTmp, cbMaxSize + 1, "%s (%s)= %s\n", pcszDigestType, RTPathFilename(paFiles[i].pszTestFile), paFiles[i].pszTestDigest);
        memcpy(&((char*)pvBuf)[cbPos], pszTmp, cch);
        cbPos += cch;
    }
    RTStrFree(pszTmp);

    /* Results */
    *ppvBuf = pvBuf;
    *pcbSize = cbSize;

    return VINF_SUCCESS;
}

