/* $Id: pemfile-write.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - PEM file writer.
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
#include <iprt/crypto/pem.h>

#include <iprt/asn1.h>
#include <iprt/base64.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/vfs.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Used by rtCrPemWriteAsn1Callback to buffer data before outputting it as
 * BASE64.
 *
 * An encoded line is 64 characters long plus a newline, covering 48 bytes
 * of binary data.  We want about 4KB of output:
 *       4096 / 65   = 63.015384615384615384615384615385
 *       64 * 65 + 1 = 4161 (0x1041)
 */
typedef struct PEMOUTPUTASN1
{
    size_t          cbPending;
    PFNRTSTROUTPUT  pfnOutput;
    void           *pvUser;
    size_t          cchRet;
    uint8_t         abBlock[0x0c00];
    char            szBlock[0x1060];
} PEMOUTPUTASN1;
typedef PEMOUTPUTASN1 *PPEMOUTPUTASN1;



RTDECL(size_t) RTCrPemWriteBlob(PFNRTSTROUTPUT pfnOutput, void *pvUser,
                                const void *pvContent, size_t cbContent, const char *pszMarker)
{
    /*
     * -----BEGIN XXXXX-----
     */
    size_t cchRet = pfnOutput(pvUser, RT_STR_TUPLE("-----BEGIN "));
    size_t const cchMarker = strlen(pszMarker);
    cchRet += pfnOutput(pvUser, pszMarker, cchMarker);
    cchRet += pfnOutput(pvUser, RT_STR_TUPLE("-----\n"));

    /*
     * base64 - in reasonably sized stack blocks.
     * An encoded line is 64 characters long plus a newline, covering 48 bytes
     * of binary data.  We want about 4KB of output:
     *       4096 / 65   = 63.015384615384615384615384615385
     *       64 * 65 + 1 = 4161 (0x1041)
     */
    const size_t cbMaxBlock = 64 * 48;
    while (cbContent > 0)
    {
        char   szBlock[0x1060];
        size_t cbBlock = RT_MIN(cbContent, cbMaxBlock);
        size_t cchBlock = 0;
        int rc = RTBase64EncodeEx(pvContent, cbBlock, RTBASE64_FLAGS_EOL_LF,
                                  szBlock, sizeof(szBlock), &cchBlock);
        AssertRC(rc);
        szBlock[cchBlock++] = '\n';
        szBlock[cchBlock]   = '\0';

        cchRet += pfnOutput(pvUser, szBlock, cchBlock);

        pvContent  = (uint8_t const *)pvContent + cbBlock;
        cbContent -= cbBlock;
    }

    /*
     * -----END XXXXX-----
     */
    cchRet += pfnOutput(pvUser, RT_STR_TUPLE("-----END "));
    cchRet += pfnOutput(pvUser, pszMarker, cchMarker);
    cchRet += pfnOutput(pvUser, RT_STR_TUPLE("-----\n"));

    /* termination call */
    cchRet += pfnOutput(pvUser, NULL, 0);

    return cchRet;
}


RTDECL(ssize_t) RTCrPemWriteBlobToVfsIoStrm(RTVFSIOSTREAM hVfsIos, const void *pvContent, size_t cbContent, const char *pszMarker)
{
    VFSIOSTRMOUTBUF Buf;
    VFSIOSTRMOUTBUF_INIT(&Buf, hVfsIos);
    size_t cchRet = RTCrPemWriteBlob(RTVfsIoStrmStrOutputCallback, &Buf, pvContent, cbContent, pszMarker);
    Assert(Buf.offBuf == 0);
    return RT_SUCCESS(Buf.rc) ? (ssize_t)cchRet : Buf.rc;
}


RTDECL(ssize_t) RTCrPemWriteBlobToVfsFile(RTVFSFILE hVfsFile, const void *pvContent, size_t cbContent, const char *pszMarker)
{
    RTVFSIOSTREAM hVfsIos = RTVfsFileToIoStream(hVfsFile);
    AssertReturn(hVfsIos != NIL_RTVFSIOSTREAM, VERR_INVALID_HANDLE);
    ssize_t cchRet = RTCrPemWriteBlobToVfsIoStrm(hVfsIos, pvContent, cbContent, pszMarker);
    RTVfsIoStrmRelease(hVfsIos);
    return cchRet;
}


/** @callback_method_impl{FNRTASN1ENCODEWRITER} */
static DECLCALLBACK(int) rtCrPemWriteAsn1Callback(const void *pvBuf, size_t cbToWrite, void *pvUser, PRTERRINFO pErrInfo)
{
    PPEMOUTPUTASN1 pThis = (PPEMOUTPUTASN1)pvUser;
    AssertCompile((sizeof(pThis->abBlock) % 48) == 0);

    while (cbToWrite > 0)
    {
        size_t offDst = pThis->cbPending;
        AssertStmt(offDst <= sizeof(pThis->abBlock), offDst = sizeof(pThis->abBlock));
        size_t cbDst = sizeof(pThis->abBlock) - offDst;
        if (cbToWrite < cbDst)
        {
            /* Buffer not full: Append and return. */
            memcpy(&pThis->abBlock[offDst], pvBuf, cbToWrite);
            pThis->cbPending = offDst + cbToWrite;
            break;
        }

        /* Fill the buffer and flush it: */
        memcpy(&pThis->abBlock[offDst], pvBuf, cbDst);
        Assert(offDst + cbDst == sizeof(pThis->abBlock));

        size_t cchBlock = 0;
        int rc = RTBase64EncodeEx(pThis->abBlock, sizeof(pThis->abBlock), RTBASE64_FLAGS_EOL_LF,
                                  pThis->szBlock, sizeof(pThis->szBlock), &cchBlock);
        AssertRC(rc);
        pThis->szBlock[cchBlock++] = '\n';
        pThis->szBlock[cchBlock]   = '\0';

        pThis->cchRet += pThis->pfnOutput(pThis->pvUser, pThis->szBlock, cchBlock);
        pThis->cbPending = 0;

        /* Advance. */
        pvBuf      = (uint8_t const *)pvBuf + cbDst;
        cbToWrite -= cbDst;
    }

    RT_NOREF(pErrInfo);
    return VINF_SUCCESS;
}


RTDECL(ssize_t) RTCrPemWriteAsn1(PFNRTSTROUTPUT pfnOutput, void *pvUser, PRTASN1CORE pRoot,
                                 uint32_t fFlags, const char *pszMarker, PRTERRINFO pErrInfo)
{
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);

    /*
     * Prepare the ASN.1 data for DER encoding.
     */
    int rc = RTAsn1EncodePrepare(pRoot, RTASN1ENCODE_F_DER, NULL /*pcbEncoded*/, pErrInfo);
    AssertRCReturn(rc, rc);

    /*
     * -----BEGIN XXXXX-----
     */
    size_t cchRet = pfnOutput(pvUser, RT_STR_TUPLE("-----BEGIN "));
    size_t const cchMarker = strlen(pszMarker);
    cchRet += pfnOutput(pvUser, pszMarker, cchMarker);
    cchRet += pfnOutput(pvUser, RT_STR_TUPLE("-----\n"));

    /*
     * BASE64
     */
    PEMOUTPUTASN1 This;
    This.pfnOutput = pfnOutput;
    This.pvUser    = pvUser;
    This.cchRet    = 0;
    This.cbPending = 0;
    rc = RTAsn1EncodeWrite(pRoot, RTASN1ENCODE_F_DER, rtCrPemWriteAsn1Callback, &This, pErrInfo);
    AssertRCReturn(rc, rc);
    cchRet += This.cchRet;

    Assert(This.cbPending <= sizeof(This.abBlock));
    if (This.cbPending)
    {
        size_t cchBlock = 0;
        rc = RTBase64EncodeEx(This.abBlock, This.cbPending, RTBASE64_FLAGS_EOL_LF,
                              This.szBlock, sizeof(This.szBlock), &cchBlock);
        AssertRC(rc);
        This.szBlock[cchBlock++] = '\n';
        This.szBlock[cchBlock]   = '\0';

        cchRet += pfnOutput(pvUser, This.szBlock, cchBlock);
    }

    /*
     * -----END XXXXX-----
     */
    cchRet += pfnOutput(pvUser, RT_STR_TUPLE("-----END "));
    cchRet += pfnOutput(pvUser, pszMarker, cchMarker);
    cchRet += pfnOutput(pvUser, RT_STR_TUPLE("-----\n"));

    /* termination call */
    cchRet += pfnOutput(pvUser, NULL, 0);

    return cchRet;
}


RTDECL(ssize_t) RTCrPemWriteAsn1ToVfsIoStrm(RTVFSIOSTREAM hVfsIos, PRTASN1CORE pRoot,
                                            uint32_t fFlags, const char *pszMarker, PRTERRINFO pErrInfo)
{
    VFSIOSTRMOUTBUF Buf;
    VFSIOSTRMOUTBUF_INIT(&Buf, hVfsIos);
    ssize_t cchRet = RTCrPemWriteAsn1(RTVfsIoStrmStrOutputCallback, &Buf, pRoot, fFlags, pszMarker, pErrInfo);
    Assert(Buf.offBuf == 0);
    return RT_SUCCESS(Buf.rc) ? (ssize_t)cchRet : Buf.rc;
}


RTDECL(ssize_t) RTCrPemWriteAsn1ToVfsFile(RTVFSFILE hVfsFile, PRTASN1CORE pRoot,
                                          uint32_t fFlags, const char *pszMarker, PRTERRINFO pErrInfo)
{
    RTVFSIOSTREAM hVfsIos = RTVfsFileToIoStream(hVfsFile);
    AssertReturn(hVfsIos != NIL_RTVFSIOSTREAM, VERR_INVALID_HANDLE);
    ssize_t cchRet = RTCrPemWriteAsn1ToVfsIoStrm(hVfsIos, pRoot, fFlags, pszMarker, pErrInfo);
    RTVfsIoStrmRelease(hVfsIos);
    return cchRet;
}
