/* $Id: manifest3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Manifest, the bits with the most dependencies.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/md5.h>
#include <iprt/mem.h>
#include <iprt/sha.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>
#include <iprt/zero.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Hashes data.
 *
 * Used when hashing a file, stream or similar.
 */
typedef struct RTMANIFESTHASHES
{
    /** The desired attribute types.
     * Only the hashes indicated by this will be calculated. */
    uint32_t        fAttrs;
    /** The size. */
    RTFOFF          cbStream;

    /** The MD5 context.  */
    RTMD5CONTEXT    Md5Ctx;
    /** The SHA-1 context.  */
    RTSHA1CONTEXT   Sha1Ctx;
    /** The SHA-256 context.  */
    RTSHA256CONTEXT Sha256Ctx;
    /** The SHA-512 context.  */
    RTSHA512CONTEXT Sha512Ctx;

    /** The MD5 digest. */
    uint8_t         abMd5Digest[RTMD5_HASH_SIZE];
    /** The SHA-1 digest. */
    uint8_t         abSha1Digest[RTSHA1_HASH_SIZE];
    /** The SHA-256 digest. */
    uint8_t         abSha256Digest[RTSHA256_HASH_SIZE];
    /** The SHA-512 digest. */
    uint8_t         abSha512Digest[RTSHA512_HASH_SIZE];
} RTMANIFESTHASHES;
/** Pointer to a the hashes for a stream. */
typedef RTMANIFESTHASHES *PRTMANIFESTHASHES;


/**
 * The internal data of a manifest passthru I/O stream.
 */
typedef struct RTMANIFESTPTIOS
{
    /** The stream we're reading from or writing to. */
    RTVFSIOSTREAM       hVfsIos;
    /** The hashes.  */
    PRTMANIFESTHASHES   pHashes;
    /** The current hash position. */
    RTFOFF              offCurPos;
    /** Whether we're reading or writing. */
    bool                fReadOrWrite;
    /** Whether we've already added the entry to the manifest. */
    bool                fAddedEntry;
    /** The entry name. */
    char               *pszEntry;
    /** The manifest to add the entry to. */
    RTMANIFEST          hManifest;
} RTMANIFESTPTIOS;
/** Pointer to a the internal data of a manifest passthru I/O stream. */
typedef RTMANIFESTPTIOS *PRTMANIFESTPTIOS;



/**
 * Creates a hashes structure.
 *
 * @returns Pointer to a hashes structure.
 * @param   fAttrs              The desired hashes, RTMANIFEST_ATTR_XXX.
 */
static PRTMANIFESTHASHES rtManifestHashesCreate(uint32_t fAttrs)
{
    PRTMANIFESTHASHES pHashes = (PRTMANIFESTHASHES)RTMemTmpAllocZ(sizeof(*pHashes));
    if (pHashes)
    {
        pHashes->fAttrs     = fAttrs;
        /*pHashes->cbStream   = 0;*/
        if (fAttrs & RTMANIFEST_ATTR_MD5)
            RTMd5Init(&pHashes->Md5Ctx);
        if (fAttrs & RTMANIFEST_ATTR_SHA1)
            RTSha1Init(&pHashes->Sha1Ctx);
        if (fAttrs & RTMANIFEST_ATTR_SHA256)
            RTSha256Init(&pHashes->Sha256Ctx);
        if (fAttrs & RTMANIFEST_ATTR_SHA512)
            RTSha512Init(&pHashes->Sha512Ctx);
    }
    return pHashes;
}


/**
 * Updates the hashes with a block of data.
 *
 * @param   pHashes             The hashes structure.
 * @param   pvBuf               The data block.
 * @param   cbBuf               The size of the data block.
 */
static void rtManifestHashesUpdate(PRTMANIFESTHASHES pHashes, void const *pvBuf, size_t cbBuf)
{
    pHashes->cbStream += cbBuf;
    if (pHashes->fAttrs & RTMANIFEST_ATTR_MD5)
        RTMd5Update(&pHashes->Md5Ctx, pvBuf, cbBuf);
    if (pHashes->fAttrs & RTMANIFEST_ATTR_SHA1)
        RTSha1Update(&pHashes->Sha1Ctx, pvBuf, cbBuf);
    if (pHashes->fAttrs & RTMANIFEST_ATTR_SHA256)
        RTSha256Update(&pHashes->Sha256Ctx, pvBuf, cbBuf);
    if (pHashes->fAttrs & RTMANIFEST_ATTR_SHA512)
        RTSha512Update(&pHashes->Sha512Ctx, pvBuf, cbBuf);
}


/**
 * Finalizes all the hashes.
 *
 * @param   pHashes             The hashes structure.
 */
static void rtManifestHashesFinal(PRTMANIFESTHASHES pHashes)
{
    if (pHashes->fAttrs & RTMANIFEST_ATTR_MD5)
        RTMd5Final(pHashes->abMd5Digest, &pHashes->Md5Ctx);
    if (pHashes->fAttrs & RTMANIFEST_ATTR_SHA1)
        RTSha1Final(&pHashes->Sha1Ctx, pHashes->abSha1Digest);
    if (pHashes->fAttrs & RTMANIFEST_ATTR_SHA256)
        RTSha256Final(&pHashes->Sha256Ctx, pHashes->abSha256Digest);
    if (pHashes->fAttrs & RTMANIFEST_ATTR_SHA512)
        RTSha512Final(&pHashes->Sha512Ctx, pHashes->abSha512Digest);
}


/**
 * Adds the hashes to a manifest entry.
 *
 * @returns IPRT status code.
 * @param   pHashes             The hashes structure.
 * @param   hManifest           The manifest to add them to.
 * @param   pszEntry            The entry name.
 */
static int rtManifestHashesSetAttrs(PRTMANIFESTHASHES pHashes, RTMANIFEST hManifest, const char *pszEntry)
{
    char szValue[RTSHA512_DIGEST_LEN + 8];
    int rc = VINF_SUCCESS;
    int rc2;

    if (pHashes->fAttrs & RTMANIFEST_ATTR_SIZE)
    {
        RTStrPrintf(szValue, sizeof(szValue), "%RU64", (uint64_t)pHashes->cbStream);
        rc2 = RTManifestEntrySetAttr(hManifest, pszEntry, "SIZE", szValue, RTMANIFEST_ATTR_SIZE);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }

    if (pHashes->fAttrs & RTMANIFEST_ATTR_MD5)
    {
        rc2 = RTMd5ToString(pHashes->abMd5Digest, szValue, sizeof(szValue));
        if (RT_SUCCESS(rc2))
            rc2 = RTManifestEntrySetAttr(hManifest, pszEntry, "MD5", szValue, RTMANIFEST_ATTR_MD5);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }

    if (pHashes->fAttrs & RTMANIFEST_ATTR_SHA1)
    {
        rc2 = RTSha1ToString(pHashes->abSha1Digest, szValue, sizeof(szValue));
        if (RT_SUCCESS(rc2))
            rc2 = RTManifestEntrySetAttr(hManifest, pszEntry, "SHA1", szValue, RTMANIFEST_ATTR_SHA1);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }

    if (pHashes->fAttrs & RTMANIFEST_ATTR_SHA256)
    {
        rc2 = RTSha256ToString(pHashes->abSha256Digest, szValue, sizeof(szValue));
        if (RT_SUCCESS(rc2))
            rc2 = RTManifestEntrySetAttr(hManifest, pszEntry, "SHA256", szValue, RTMANIFEST_ATTR_SHA256);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }

    if (pHashes->fAttrs & RTMANIFEST_ATTR_SHA512)
    {
        rc2 = RTSha512ToString(pHashes->abSha512Digest, szValue, sizeof(szValue));
        if (RT_SUCCESS(rc2))
            rc2 = RTManifestEntrySetAttr(hManifest, pszEntry, "SHA512", szValue, RTMANIFEST_ATTR_SHA512);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }
    return rc;
}


/**
 * Destroys the hashes.
 *
 * @param   pHashes             The hashes structure.  NULL is ignored.
 */
static void rtManifestHashesDestroy(PRTMANIFESTHASHES pHashes)
{
    RTMemTmpFree(pHashes);
}



/*
 *
 *   M a n i f e s t   p a s s t h r u   I / O    s t r e a m
 *   M a n i f e s t   p a s s t h r u   I / O    s t r e a m
 *   M a n i f e s t   p a s s t h r u   I / O    s t r e a m
 *
 */


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtManifestPtIos_Close(void *pvThis)
{
    PRTMANIFESTPTIOS pThis = (PRTMANIFESTPTIOS)pvThis;

    int rc = VINF_SUCCESS;
    if (!pThis->fAddedEntry)
    {
        rtManifestHashesFinal(pThis->pHashes);
        rc = rtManifestHashesSetAttrs(pThis->pHashes, pThis->hManifest, pThis->pszEntry);
    }

    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;
    rtManifestHashesDestroy(pThis->pHashes);
    pThis->pHashes = NULL;
    RTStrFree(pThis->pszEntry);
    pThis->pszEntry = NULL;
    RTManifestRelease(pThis->hManifest);
    pThis->hManifest = NIL_RTMANIFEST;

    return rc;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtManifestPtIos_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTMANIFESTPTIOS pThis = (PRTMANIFESTPTIOS)pvThis;
    return RTVfsIoStrmQueryInfo(pThis->hVfsIos, pObjInfo, enmAddAttr);
}

/**
 * Updates the hashes with a scather/gather buffer.
 *
 * @param   pThis               The passthru I/O stream instance data.
 * @param   pSgBuf              The scather/gather buffer (clone, can be
 *                              updated).
 * @param   cbLeft              The number of bytes to take from the buffer.
 */
static void rtManifestPtIos_UpdateHashes(PRTMANIFESTPTIOS pThis, PRTSGBUF pSgBuf, size_t cbLeft)
{
    while (cbLeft > 0)
    {
        Assert(!RTSgBufIsAtEnd(pSgBuf));
        size_t             cbSeg = cbLeft;
        void const * const pvSeg = RTSgBufGetNextSegment(pSgBuf, &cbSeg);
        rtManifestHashesUpdate(pThis->pHashes, pvSeg, cbSeg);
        cbLeft -= cbSeg;
    }
}

/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtManifestPtIos_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTMANIFESTPTIOS pThis = (PRTMANIFESTPTIOS)pvThis;
    int rc;

    /*
     * Clone the buffer for the manifest pass.
     */
    RTSGBUF CloneSgBuf;
    RTSgBufClone(&CloneSgBuf, pSgBuf);

    /*
     * To make sure we're continuing where we left off, we must have the exact
     * stream position since a previous read using 'off' may change it.
     */
    RTFOFF offActual = off == -1 ? RTVfsIoStrmTell(pThis->hVfsIos) : off;
    if (offActual == pThis->offCurPos)
    {
        size_t const cbReq = RTSgBufCalcLengthLeft(pSgBuf);
        rc = RTVfsIoStrmSgRead(pThis->hVfsIos, off, pSgBuf, fBlocking, pcbRead);
        if (RT_SUCCESS(rc))
        {
            rtManifestPtIos_UpdateHashes(pThis, &CloneSgBuf, pcbRead ? *pcbRead : cbReq);
            pThis->offCurPos += pcbRead ? *pcbRead : cbReq;
        }
        Assert(RTVfsIoStrmTell(pThis->hVfsIos) == pThis->offCurPos);
    }
    else
    {
        /*
         * If we're skipping over stuff, we need to read the gap and hash it.
         */
        if (pThis->offCurPos < offActual)
        {
            size_t cbBuf = _8K;
            void  *pvBuf = alloca(cbBuf);
            do
            {
                RTFOFF cbGap = off - pThis->offCurPos;
                size_t cbThisRead = cbGap >= (RTFOFF)cbBuf ? cbBuf : (size_t)cbGap;
                size_t cbActual;
                rc = RTVfsIoStrmReadAt(pThis->hVfsIos, pThis->offCurPos, pvBuf, cbThisRead, fBlocking, &cbActual);
                if (RT_FAILURE(rc) || rc == VINF_TRY_AGAIN)
                    return rc;

                rtManifestHashesUpdate(pThis->pHashes, pvBuf, cbActual);
                pThis->offCurPos += cbActual;

                if (rc == VINF_EOF)
                {
                    if (pcbRead)
                        *pcbRead = 0;
                    else
                        rc = VERR_EOF;
                    return rc;
                }
            } while (pThis->offCurPos < offActual);
            Assert(RTVfsIoStrmTell(pThis->hVfsIos) == offActual);
        }

        /*
         * At this point we've eliminated any gap and can execute the requested read.
         */
        rc = RTVfsIoStrmSgRead(pThis->hVfsIos, off, pSgBuf, fBlocking, pcbRead);
        if (RT_SUCCESS(rc))
        {
            /* See if there is anything to update the hash with. */
            size_t cbLeft = pcbRead ? *pcbRead : RTSgBufCalcLengthLeft(&CloneSgBuf);
            while (cbLeft > 0)
            {
                Assert(!RTSgBufIsAtEnd(&CloneSgBuf));
                size_t         cbSeg = cbLeft;
                const uint8_t *pbSeg = (uint8_t const *)RTSgBufGetNextSegment(&CloneSgBuf, &cbSeg);

                if (   offActual >= pThis->offCurPos
                    && pThis->offCurPos < offActual + (ssize_t)cbSeg)
                {
                    size_t offSeg = (size_t)(offActual - pThis->offCurPos);
                    rtManifestHashesUpdate(pThis->pHashes, &pbSeg[offSeg], cbSeg - offSeg);
                    pThis->offCurPos += cbSeg - offSeg;
                }

                cbLeft -= cbSeg;
            }
        }
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtManifestPtIos_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    PRTMANIFESTPTIOS pThis = (PRTMANIFESTPTIOS)pvThis;
    Assert(RTVfsIoStrmTell(pThis->hVfsIos) == pThis->offCurPos);

    /*
     * Validate the offset.
     */
    if (off < 0 || off == pThis->offCurPos)
    { /* likely */ }
    else
    {
        /* We cannot go back and rewrite stuff. Sorry. */
        AssertReturn(off > pThis->offCurPos, VERR_WRONG_ORDER);

        /*
         * We've got a gap between the current and new position.
         * Fill it with zeros and hope for the best.
         */
        uint64_t cbZeroGap = off - pThis->offCurPos;
        do
        {
            size_t cbToZero = cbZeroGap >= sizeof(g_abRTZero64K) ? sizeof(g_abRTZero64K) : (size_t)cbZeroGap;
            size_t cbZeroed = 0;
            int rc = RTVfsIoStrmWrite(pThis->hVfsIos, g_abRTZero64K, cbToZero, true /*fBlocking*/, &cbZeroed);
            if (RT_FAILURE(rc))
                return rc;
            pThis->offCurPos += cbZeroed;
            rtManifestHashesUpdate(pThis->pHashes, g_abRTZero64K, cbZeroed);
            cbZeroGap -= cbZeroed;
        } while (cbZeroGap > 0);
        Assert(off == pThis->offCurPos);
    }

    /*
     * Clone the buffer for the manifest pass.
     */
    RTSGBUF CloneSgBuf;
    RTSgBufClone(&CloneSgBuf, pSgBuf);
    size_t const cbReq = RTSgBufCalcLengthLeft(pSgBuf);

    /*
     * Do the writing.
     */
    int rc = RTVfsIoStrmSgWrite(pThis->hVfsIos, -1 /*off*/, pSgBuf, fBlocking, pcbWritten);
    if (RT_SUCCESS(rc))
    {
        rtManifestPtIos_UpdateHashes(pThis, &CloneSgBuf, pcbWritten ? *pcbWritten : cbReq);
        pThis->offCurPos += pcbWritten ? *pcbWritten : cbReq;
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtManifestPtIos_Flush(void *pvThis)
{
    PRTMANIFESTPTIOS pThis = (PRTMANIFESTPTIOS)pvThis;
    return RTVfsIoStrmFlush(pThis->hVfsIos);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int) rtManifestPtIos_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                                 uint32_t *pfRetEvents)
{
    PRTMANIFESTPTIOS pThis = (PRTMANIFESTPTIOS)pvThis;
    return RTVfsIoStrmPoll(pThis->hVfsIos, fEvents, cMillies, fIntr, pfRetEvents);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtManifestPtIos_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTMANIFESTPTIOS pThis = (PRTMANIFESTPTIOS)pvThis;
    RTFOFF off = RTVfsIoStrmTell(pThis->hVfsIos);
    if (off < 0)
        return (int)off;
    *poffActual = off;
    return VINF_SUCCESS;
}


/**
 * The manifest passthru I/O stream vtable.
 */
static RTVFSIOSTREAMOPS g_rtManifestPassthruIosOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_IO_STREAM,
        "manifest passthru I/O stream",
        rtManifestPtIos_Close,
        rtManifestPtIos_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSIOSTREAMOPS_VERSION,
    0,
    rtManifestPtIos_Read,
    rtManifestPtIos_Write,
    rtManifestPtIos_Flush,
    rtManifestPtIos_PollOne,
    rtManifestPtIos_Tell,
    NULL /* Skip */,
    NULL /* ZeroFill */,
    RTVFSIOSTREAMOPS_VERSION,
};



RTDECL(int) RTManifestEntryAddPassthruIoStream(RTMANIFEST hManifest, RTVFSIOSTREAM hVfsIos, const char *pszEntry,
                                               uint32_t fAttrs, bool fReadOrWrite, PRTVFSIOSTREAM phVfsIosPassthru)
{
    /*
     * Validate input.
     */
    AssertReturn(fAttrs < RTMANIFEST_ATTR_END, VERR_INVALID_PARAMETER);
    AssertPtr(pszEntry);
    AssertPtr(phVfsIosPassthru);

    RTFOFF const offCurPos = RTVfsIoStrmTell(hVfsIos);
    AssertReturn(offCurPos >= 0, (int)offCurPos);

    uint32_t cRefs = RTManifestRetain(hManifest);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    cRefs = RTVfsIoStrmRetain(hVfsIos);
    AssertReturnStmt(cRefs != UINT32_MAX, RTManifestRelease(hManifest), VERR_INVALID_HANDLE);

    /*
     * Create an instace of the passthru I/O stream.
     */
    PRTMANIFESTPTIOS pThis;
    RTVFSIOSTREAM    hVfsPtIos;
    int rc = RTVfsNewIoStream(&g_rtManifestPassthruIosOps, sizeof(*pThis), fReadOrWrite ? RTFILE_O_READ : RTFILE_O_WRITE,
                              NIL_RTVFS, NIL_RTVFSLOCK, &hVfsPtIos, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVfsIos          = hVfsIos;
        pThis->pHashes          = rtManifestHashesCreate(fAttrs);
        pThis->offCurPos        = offCurPos;
        pThis->hManifest        = hManifest;
        pThis->fReadOrWrite     = fReadOrWrite;
        pThis->fAddedEntry      = false;
        pThis->pszEntry         = RTStrDup(pszEntry);
        if (pThis->pszEntry && pThis->pHashes)
        {
            *phVfsIosPassthru = hVfsPtIos;
            return VINF_SUCCESS;
        }

        RTVfsIoStrmRelease(hVfsPtIos);
    }
    else
    {
        RTVfsIoStrmRelease(hVfsIos);
        RTManifestRelease(hManifest);
    }
    return rc;
}


RTDECL(int) RTManifestPtIosAddEntryNow(RTVFSIOSTREAM hVfsPtIos)
{
    PRTMANIFESTPTIOS pThis = (PRTMANIFESTPTIOS)RTVfsIoStreamToPrivate(hVfsPtIos, &g_rtManifestPassthruIosOps);
    AssertReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fAddedEntry, VERR_WRONG_ORDER);

    pThis->fAddedEntry = true;
    rtManifestHashesFinal(pThis->pHashes);
    return rtManifestHashesSetAttrs(pThis->pHashes, pThis->hManifest, pThis->pszEntry);
}


RTDECL(bool) RTManifestPtIosIsInstanceOf(RTVFSIOSTREAM hVfsPtIos)
{
    if (hVfsPtIos != NIL_RTVFSIOSTREAM)
    {
        PRTMANIFESTPTIOS pThis = (PRTMANIFESTPTIOS)RTVfsIoStreamToPrivate(hVfsPtIos, &g_rtManifestPassthruIosOps);
        if (pThis)
            return true;
    }
    return false;
}


RTDECL(int) RTManifestEntryAddIoStream(RTMANIFEST hManifest, RTVFSIOSTREAM hVfsIos, const char *pszEntry, uint32_t fAttrs)
{
    /*
     * Note! This is a convenicence function, so just use the available public
     *       methods to get the job done.
     */
    AssertReturn(fAttrs < RTMANIFEST_ATTR_END, VERR_INVALID_PARAMETER);
    AssertPtr(pszEntry);

    /*
     * Allocate and initialize the hash contexts, hash digests and I/O buffer.
     */
    PRTMANIFESTHASHES pHashes = rtManifestHashesCreate(fAttrs);
    if (!pHashes)
        return VERR_NO_TMP_MEMORY;

    int         rc;
    size_t      cbBuf = _1M;
    void       *pvBuf = RTMemTmpAlloc(cbBuf);
    if (RT_UNLIKELY(!pvBuf))
    {
        cbBuf = _4K;
        pvBuf = RTMemTmpAlloc(cbBuf);
    }
    if (RT_LIKELY(pvBuf))
    {
        /*
         * Process the stream data.
         */
        for (;;)
        {
            size_t cbRead;
            rc = RTVfsIoStrmRead(hVfsIos, pvBuf, cbBuf, true /*fBlocking*/, &cbRead);
            if (   (rc == VINF_EOF && cbRead == 0)
                || RT_FAILURE(rc))
                break;
            rtManifestHashesUpdate(pHashes, pvBuf, cbRead);
        }
        RTMemTmpFree(pvBuf);
        if (RT_SUCCESS(rc))
        {
            /*
             * Add the entry with the finalized hashes.
             */
            rtManifestHashesFinal(pHashes);
            rc = RTManifestEntryAdd(hManifest, pszEntry);
            if (RT_SUCCESS(rc))
                rc = rtManifestHashesSetAttrs(pHashes, hManifest, pszEntry);
        }
    }
    else
        rc = VERR_NO_TMP_MEMORY;

    rtManifestHashesDestroy(pHashes);
    return rc;
}

