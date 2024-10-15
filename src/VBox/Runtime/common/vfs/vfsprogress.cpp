/* $Id: vfsprogress.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Virtual File System, progress filter for files.
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
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Private data of a standard file.
 */
typedef struct RTVFSPROGRESSFILE
{
    /** This is negative (RT_FAILURE) if canceled.  */
    int             rcCanceled;
    /** RTVFSPROGRESS_F_XXX. */
    uint32_t        fFlags;
    /** Progress callback.   */
    PFNRTPROGRESS   pfnProgress;
    /** User argument for the callback. */
    void           *pvUser;
    /** The I/O stream handle. */
    RTVFSIOSTREAM   hVfsIos;
    /** The file handle.  NIL_RTFILE if a pure I/O stream. */
    RTVFSFILE       hVfsFile;
    /** Total number of bytes expected to be read and written. */
    uint64_t        cbExpected;
    /** The number of bytes expected to be read. */
    uint64_t        cbExpectedRead;
    /** The number of bytes expected to be written. */
    uint64_t        cbExpectedWritten;
    /** Number of bytes currently read. */
    uint64_t        cbCurrentlyRead;
    /** Number of bytes currently written. */
    uint64_t        cbCurrentlyWritten;
    /** Current precentage.   */
    unsigned        uCurPct;
} RTVFSPROGRESSFILE;
/** Pointer to the private data of a standard file. */
typedef RTVFSPROGRESSFILE *PRTVFSPROGRESSFILE;


/**
 * Update the progress and do the progress callback if necessary.
 *
 * @returns Callback return code.
 * @param   pThis     The file progress instance.
 */
static int rtVfsProgressFile_UpdateProgress(PRTVFSPROGRESSFILE pThis)
{
    uint64_t cbDone = RT_MIN(pThis->cbCurrentlyRead, pThis->cbExpectedRead)
                    + RT_MIN(pThis->cbCurrentlyWritten, pThis->cbExpectedWritten);
    unsigned uPct = cbDone * 100 / pThis->cbExpected;
    if (uPct == pThis->uCurPct)
        return pThis->rcCanceled;
    pThis->uCurPct = uPct;

    int rc = pThis->pfnProgress(uPct, pThis->pvUser);
    if (!(pThis->fFlags & RTVFSPROGRESS_F_CANCELABLE))
        rc = VINF_SUCCESS;
    else if (RT_FAILURE(rc) && RT_SUCCESS(pThis->rcCanceled))
        pThis->rcCanceled = rc;

    return rc;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtVfsProgressFile_Close(void *pvThis)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;

    if (pThis->hVfsFile != NIL_RTVFSFILE)
    {
        RTVfsFileRelease(pThis->hVfsFile);
        pThis->hVfsFile = NIL_RTVFSFILE;
    }
    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;

    pThis->pfnProgress = NULL;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtVfsProgressFile_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    int rc = pThis->rcCanceled;
    if (RT_SUCCESS(rc))
        rc = RTVfsIoStrmQueryInfo(pThis->hVfsIos, pObjInfo, enmAddAttr);
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtVfsProgressFile_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;

    int rc = pThis->rcCanceled;
    if (RT_SUCCESS(rc))
    {
        /* Simplify a little there if a seeks is implied and assume the read goes well. */
        if (   off >= 0
            && (pThis->fFlags & RTVFSPROGRESS_F_FORWARD_SEEK_AS_READ))
        {
            uint64_t offCurrent = RTVfsFileTell(pThis->hVfsFile);
            if (offCurrent < (uint64_t)off)
                pThis->cbCurrentlyRead += off - offCurrent;
        }

        /* Calc the request before calling down the stack. */
        size_t const cbReq = RTSgBufCalcLengthLeft(pSgBuf);

        /* Do the read. */
        rc = RTVfsIoStrmSgRead(pThis->hVfsIos, off, pSgBuf, fBlocking, pcbRead);
        if (RT_SUCCESS(rc))
        {
            /* Update the progress (we cannot cancel here, sorry). */
            pThis->cbCurrentlyRead += pcbRead ? *pcbRead : cbReq;
            rtVfsProgressFile_UpdateProgress(pThis);
        }
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtVfsProgressFile_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;

    int rc = pThis->rcCanceled;
    if (RT_SUCCESS(rc))
    {
        /* Simplify a little there if a seeks is implied and assume the write goes well. */
        if (   off >= 0
            && (pThis->fFlags & RTVFSPROGRESS_F_FORWARD_SEEK_AS_WRITE))
        {
            uint64_t offCurrent = RTVfsFileTell(pThis->hVfsFile);
            if (offCurrent < (uint64_t)off)
                pThis->cbCurrentlyWritten += off - offCurrent;
        }

        /* Calc the request before calling down the stack. */
        size_t const cbReq = RTSgBufCalcLengthLeft(pSgBuf);

        /* Do the read. */
        rc = RTVfsIoStrmSgWrite(pThis->hVfsIos, off, pSgBuf, fBlocking, pcbWritten);
        if (RT_SUCCESS(rc))
        {
            /* Update the progress (we cannot cancel here, sorry). */
            pThis->cbCurrentlyWritten += pcbWritten ? *pcbWritten : cbReq;
            rtVfsProgressFile_UpdateProgress(pThis);
        }
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtVfsProgressFile_Flush(void *pvThis)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    int rc = pThis->rcCanceled;
    if (RT_SUCCESS(rc))
        rc = RTVfsIoStrmFlush(pThis->hVfsIos);
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int)
rtVfsProgressFile_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr, uint32_t *pfRetEvents)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    int rc = pThis->rcCanceled;
    if (RT_SUCCESS(rc))
        rc = RTVfsIoStrmPoll(pThis->hVfsIos, fEvents, cMillies, fIntr, pfRetEvents);
    else
    {
        *pfRetEvents |= RTPOLL_EVT_ERROR;
        rc = VINF_SUCCESS;
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtVfsProgressFile_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    *poffActual = RTVfsIoStrmTell(pThis->hVfsIos);
    return *poffActual >= 0 ? VINF_SUCCESS : (int)*poffActual;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnSkip}
 */
static DECLCALLBACK(int) rtVfsProgressFile_Skip(void *pvThis, RTFOFF cb)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    int rc = pThis->rcCanceled;
    if (RT_SUCCESS(rc))
    {
        rc = RTVfsIoStrmSkip(pThis->hVfsIos, cb);
        if (RT_SUCCESS(rc))
        {
            pThis->cbCurrentlyRead += cb;
            rtVfsProgressFile_UpdateProgress(pThis);
        }
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnZeroFill}
 */
static DECLCALLBACK(int) rtVfsProgressFile_ZeroFill(void *pvThis, RTFOFF cb)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    int rc = pThis->rcCanceled;
    if (RT_SUCCESS(rc))
    {
        rc = RTVfsIoStrmZeroFill(pThis->hVfsIos, cb);
        if (RT_SUCCESS(rc))
        {
            pThis->cbCurrentlyWritten += cb;
            rtVfsProgressFile_UpdateProgress(pThis);
        }
    }
    return rc;
}


/**
 * I/O stream progress operations.
 */
DECL_HIDDEN_CONST(const RTVFSIOSTREAMOPS) g_rtVfsProgressIosOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_IO_STREAM,
        "I/O Stream Progress",
        rtVfsProgressFile_Close,
        rtVfsProgressFile_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSIOSTREAMOPS_VERSION,
    0,
    rtVfsProgressFile_Read,
    rtVfsProgressFile_Write,
    rtVfsProgressFile_Flush,
    rtVfsProgressFile_PollOne,
    rtVfsProgressFile_Tell,
    rtVfsProgressFile_Skip,
    rtVfsProgressFile_ZeroFill,
    RTVFSIOSTREAMOPS_VERSION,
};



/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtVfsProgressFile_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    //return RTVfsFileSetMode(pThis->hVfsIos, fMode, fMask); - missing
    RT_NOREF(pThis, fMode, fMask);
    AssertFailedReturn(VERR_NOT_IMPLEMENTED);
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtVfsProgressFile_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                                    PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    //return RTVfsFileSetTimes(pThis->hVfsIos, pAccessTime, pModificationTime, pChangeTime, pBirthTime); - missing
    RT_NOREF(pThis, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
    AssertFailedReturn(VERR_NOT_IMPLEMENTED);
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtVfsProgressFile_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    //return RTVfsFileSetOwern(pThis->hVfsIos, uid, gid); - missing
    RT_NOREF(pThis, uid, gid);
    AssertFailedReturn(VERR_NOT_IMPLEMENTED);
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtVfsProgressFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;

    uint64_t offPrev = UINT64_MAX;
    if (pThis->fFlags & (RTVFSPROGRESS_F_FORWARD_SEEK_AS_READ | RTVFSPROGRESS_F_FORWARD_SEEK_AS_WRITE))
        offPrev = RTVfsFileTell(pThis->hVfsFile);

    uint64_t offActual = 0;
    int rc = RTVfsFileSeek(pThis->hVfsFile, offSeek, uMethod, &offActual);
    if (RT_SUCCESS(rc))
    {
        if (poffActual)
            *poffActual = offActual;

        /* Do progress updates as requested. */
        if (pThis->fFlags & (RTVFSPROGRESS_F_FORWARD_SEEK_AS_READ | RTVFSPROGRESS_F_FORWARD_SEEK_AS_WRITE))
        {
            if (offActual > offPrev)
            {
                if (pThis->fFlags & RTVFSPROGRESS_F_FORWARD_SEEK_AS_READ)
                    pThis->cbCurrentlyRead += offActual - offPrev;
                else
                    pThis->cbCurrentlyWritten += offActual - offPrev;
                rtVfsProgressFile_UpdateProgress(pThis);
            }
        }
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) rtVfsProgressFile_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    return RTVfsFileQuerySize(pThis->hVfsFile, pcbFile);
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSetSize}
 */
static DECLCALLBACK(int) rtVfsProgressFile_SetSize(void *pvThis, uint64_t cbFile, uint32_t fFlags)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    return RTVfsFileSetSize(pThis->hVfsFile, cbFile, fFlags);
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQueryMaxSize}
 */
static DECLCALLBACK(int) rtVfsProgressFile_QueryMaxSize(void *pvThis, uint64_t *pcbMax)
{
    PRTVFSPROGRESSFILE pThis = (PRTVFSPROGRESSFILE)pvThis;
    return RTVfsFileQueryMaxSize(pThis->hVfsFile, pcbMax);
}



/**
 * File progress operations.
 */
DECL_HIDDEN_CONST(const RTVFSFILEOPS) g_rtVfsProgressFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "File Progress",
            rtVfsProgressFile_Close,
            rtVfsProgressFile_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        0,
        rtVfsProgressFile_Read,
        rtVfsProgressFile_Write,
        rtVfsProgressFile_Flush,
        rtVfsProgressFile_PollOne,
        rtVfsProgressFile_Tell,
        rtVfsProgressFile_Skip,
        rtVfsProgressFile_ZeroFill,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        rtVfsProgressFile_SetMode,
        rtVfsProgressFile_SetTimes,
        rtVfsProgressFile_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtVfsProgressFile_Seek,
    rtVfsProgressFile_QuerySize,
    rtVfsProgressFile_SetSize,
    rtVfsProgressFile_QueryMaxSize,
    RTVFSFILEOPS_VERSION
};


RTDECL(int) RTVfsCreateProgressForIoStream(RTVFSIOSTREAM hVfsIos, PFNRTPROGRESS pfnProgress, void *pvUser, uint32_t fFlags,
                                           uint64_t cbExpectedRead, uint64_t cbExpectedWritten, PRTVFSIOSTREAM phVfsIos)
{
    AssertPtrReturn(pfnProgress, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTVFSPROGRESS_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(!(fFlags & RTVFSPROGRESS_F_FORWARD_SEEK_AS_READ) || !(fFlags & RTVFSPROGRESS_F_FORWARD_SEEK_AS_WRITE),
                 VERR_INVALID_FLAGS);

    uint32_t cRefs = RTVfsIoStrmRetain(hVfsIos);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    PRTVFSPROGRESSFILE pThis;
    int rc = RTVfsNewIoStream(&g_rtVfsProgressIosOps, sizeof(*pThis), RTVfsIoStrmGetOpenFlags(hVfsIos),
                              NIL_RTVFS, NIL_RTVFSLOCK, phVfsIos, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->rcCanceled           = VINF_SUCCESS;
        pThis->fFlags               = fFlags;
        pThis->pfnProgress          = pfnProgress;
        pThis->pvUser               = pvUser;
        pThis->hVfsIos              = hVfsIos;
        pThis->hVfsFile             = RTVfsIoStrmToFile(hVfsIos);
        pThis->cbCurrentlyRead      = 0;
        pThis->cbCurrentlyWritten   = 0;
        pThis->cbExpectedRead       = cbExpectedRead;
        pThis->cbExpectedWritten    = cbExpectedWritten;
        pThis->cbExpected           = cbExpectedRead + cbExpectedWritten;
        if (!pThis->cbExpected)
            pThis->cbExpected       = 1;
        pThis->uCurPct              = 0;
    }
    return rc;
}


RTDECL(int) RTVfsCreateProgressForFile(RTVFSFILE hVfsFile, PFNRTPROGRESS pfnProgress, void *pvUser, uint32_t fFlags,
                                       uint64_t cbExpectedRead, uint64_t cbExpectedWritten, PRTVFSFILE phVfsFile)
{
    AssertPtrReturn(pfnProgress, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTVFSPROGRESS_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(!(fFlags & RTVFSPROGRESS_F_FORWARD_SEEK_AS_READ) || !(fFlags & RTVFSPROGRESS_F_FORWARD_SEEK_AS_WRITE),
                 VERR_INVALID_FLAGS);

    uint32_t cRefs = RTVfsFileRetain(hVfsFile);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    RTVFSIOSTREAM hVfsIos = RTVfsFileToIoStream(hVfsFile);
    AssertReturnStmt(hVfsIos != NIL_RTVFSIOSTREAM, RTVfsFileRelease(hVfsFile), VERR_INVALID_HANDLE);

    PRTVFSPROGRESSFILE pThis;
    int rc = RTVfsNewFile(&g_rtVfsProgressFileOps, sizeof(*pThis), RTVfsFileGetOpenFlags(hVfsFile),
                          NIL_RTVFS, NIL_RTVFSLOCK, phVfsFile, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->fFlags               = fFlags;
        pThis->pfnProgress          = pfnProgress;
        pThis->pvUser               = pvUser;
        pThis->hVfsIos              = hVfsIos;
        pThis->hVfsFile             = hVfsFile;
        pThis->cbCurrentlyRead      = 0;
        pThis->cbCurrentlyWritten   = 0;
        pThis->cbExpectedRead       = cbExpectedRead;
        pThis->cbExpectedWritten    = cbExpectedWritten;
        pThis->cbExpected           = cbExpectedRead + cbExpectedWritten;
        if (!pThis->cbExpected)
            pThis->cbExpected       = 1;
        pThis->uCurPct              = 0;
    }
    return rc;
}

