/* $Id: dvmvfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Disk Volume Management API (DVM) - VFS glue.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_FS /** @todo fix log group  */
#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/dvm.h>
#include <iprt/err.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/file.h>
#include <iprt/sg.h>
#include <iprt/vfslowlevel.h>
#include <iprt/poll.h>
#include <iprt/log.h>
#include "internal/dvm.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a volume manager VFS. */
typedef struct RTDVMVFSVOL *PRTDVMVFSVOL;

/**
 * The internal data of a DVM volume I/O stream.
 */
typedef struct RTVFSDVMFILE
{
    /** The volume the VFS file belongs to. */
    RTDVMVOLUME     hVol;
    /** Pointer to the VFS volume.  Can be NULL. */
    PRTDVMVFSVOL    pVfsVol;
    /** Current position. */
    uint64_t        offCurPos;
    /** Set if readable. */
    bool            fCanRead;
    /** Set if writable. */
    bool            fCanWrite;
} RTVFSDVMFILE;
/** Pointer to a the internal data of a DVM volume file. */
typedef RTVFSDVMFILE *PRTVFSDVMFILE;

/**
 * The internal data of a DVM volume symlink.
 */
typedef struct RTVFSDVMSYMLINK
{
    /** The DVM volume the symlink represent. */
    RTDVMVOLUME     hVol;
    /** The DVM volume manager @a hVol belongs to. */
    RTDVM           hVolMgr;
    /** The symlink name. */
    char           *pszSymlink;
    /** The symlink target (volXX). */
    char            szTarget[16];
} RTVFSDVMSYMLINK;
/** Pointer to a the internal data of a DVM volume file. */
typedef RTVFSDVMSYMLINK *PRTVFSDVMSYMLINK;

/**
 * The volume manager VFS (root) dir data.
 */
typedef struct RTDVMVFSDIR
{
    /** Pointer to the VFS volume. */
    PRTDVMVFSVOL    pVfsVol;
    /** The current directory offset. */
    uint32_t        offDir;
    /** Set if we need to try return hCurVolume again because of buffer overflow. */
    bool            fReturnCurrent;
    /** Pointer to name alias string (returned by RTDvmVolumeQueryName, free it). */
    char           *pszNameAlias;
    /** The current DVM volume. */
    RTDVMVOLUME     hCurVolume;
} RTDVMVFSDIR;
/** Pointer to a volume manager VFS (root) dir. */
typedef RTDVMVFSDIR *PRTDVMVFSDIR;

/**
 * A volume manager VFS for use in chains (thing pseudo/devfs).
 */
typedef struct RTDVMVFSVOL
{
    /** The volume manager. */
    RTDVM           hVolMgr;
    /** Whether to close it on success. */
    bool            fCloseDvm;
    /** Whether the access is read-only. */
    bool            fReadOnly;
    /** Number of volumes. */
    uint32_t        cVolumes;
    /** Self reference. */
    RTVFS           hVfsSelf;
} RTDVMVFSVOL;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) rtDvmVfsVol_OpenRoot(void *pvThis, PRTVFSDIR phVfsDir);


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtDvmVfsFile_Close(void *pvThis)
{
    PRTVFSDVMFILE pThis = (PRTVFSDVMFILE)pvThis;

    RTDvmVolumeRelease(pThis->hVol);
    return VINF_SUCCESS;
}


/**
 * Worker for rtDvmVfsFile_QueryInfoWorker and rtDvmVfsSym_QueryInfoWorker.
 */
static int rtDvmVfsFileSym_QueryAddAttrWorker(RTDVMVOLUME hVolume, RTDVM hVolMgr,
                                              PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_NOTHING:
        case RTFSOBJATTRADD_UNIX:
            pObjInfo->Attr.u.Unix.uid           = (RTUID)RTDvmVolumeGetType(hVolume);
            pObjInfo->Attr.u.Unix.gid           = hVolMgr != NIL_RTDVM ? (RTGID)RTDvmMapGetFormatType(hVolMgr) : NIL_RTGID;
            pObjInfo->Attr.u.Unix.cHardlinks    = 1;
            pObjInfo->Attr.u.Unix.INodeIdDevice = 0;
            pObjInfo->Attr.u.Unix.INodeId       = 0;
            pObjInfo->Attr.u.Unix.fFlags        = 0;
            pObjInfo->Attr.u.Unix.GenerationId  = 0;
            pObjInfo->Attr.u.Unix.Device        = 0;
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
        {
            RTDVMVOLTYPE enmType = RTDvmVolumeGetType(hVolume);
            pObjInfo->Attr.u.UnixOwner.uid = (RTUID)enmType;
            RTStrCopy(pObjInfo->Attr.u.UnixOwner.szName, sizeof(pObjInfo->Attr.u.UnixOwner.szName),
                      RTDvmVolumeTypeGetDescr(enmType));
            break;
        }

        case RTFSOBJATTRADD_UNIX_GROUP:
            if (hVolMgr != NIL_RTDVM)
            {
                pObjInfo->Attr.u.UnixGroup.gid  = (RTGID)RTDvmMapGetFormatType(hVolMgr);
                RTStrCopy(pObjInfo->Attr.u.UnixGroup.szName, sizeof(pObjInfo->Attr.u.UnixGroup.szName),
                          RTDvmMapGetFormatName(hVolMgr));
            }
            else
            {
                pObjInfo->Attr.u.UnixGroup.gid  = NIL_RTGID;
                pObjInfo->Attr.u.UnixGroup.szName[0] = '\0';
            }
            break;

        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.u.EASize.cb = 0;
            break;

        default:
            return VERR_INVALID_PARAMETER;
    }
    return VINF_SUCCESS;
}


/**
 * Worker for rtDvmVfsFile_QueryInfo, rtDvmVfsDir_QueryEntryInfo, and
 * rtDvmVfsDir_ReadDir.
 */
static int rtDvmVfsFile_QueryInfoWorker(RTDVMVOLUME hVolume, RTDVM hVolMgr, bool fReadOnly,
                                        PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{

    pObjInfo->cbObject    = RTDvmVolumeGetSize(hVolume);
    pObjInfo->cbAllocated = pObjInfo->cbObject;
    RTTimeSpecSetNano(&pObjInfo->AccessTime, 0);
    RTTimeSpecSetNano(&pObjInfo->ModificationTime, 0);
    RTTimeSpecSetNano(&pObjInfo->ChangeTime, 0);
    RTTimeSpecSetNano(&pObjInfo->BirthTime, 0);
    pObjInfo->Attr.fMode = RTFS_TYPE_FILE | RTFS_DOS_NT_NORMAL;
    if (fReadOnly)
        pObjInfo->Attr.fMode |= RTFS_DOS_READONLY | 0444;
    else
        pObjInfo->Attr.fMode |= 0666;

    return rtDvmVfsFileSym_QueryAddAttrWorker(hVolume, hVolMgr, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtDvmVfsFile_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTVFSDVMFILE pThis = (PRTVFSDVMFILE)pvThis;
    return rtDvmVfsFile_QueryInfoWorker(pThis->hVol,
                                        pThis->pVfsVol ? pThis->pVfsVol->hVolMgr   : NIL_RTDVM,
                                        pThis->pVfsVol ? pThis->pVfsVol->fReadOnly : !pThis->fCanWrite,
                                        pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtDvmVfsFile_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTVFSDVMFILE const pThis = (PRTVFSDVMFILE)pvThis;

    Assert(pSgBuf->cSegs == 1);
    NOREF(fBlocking);

    /*
     * Find the current position and check if it's within the volume.
     */
    uint64_t const cbVol = RTDvmVolumeGetSize(pThis->hVol);
    uint64_t offUnsigned = off < 0 ? pThis->offCurPos : (uint64_t)off;
    if (offUnsigned >= cbVol)
    {
        if (pcbRead)
        {
            *pcbRead = 0;
            pThis->offCurPos = offUnsigned;
            return VINF_EOF;
        }
        return VERR_EOF;
    }

    size_t       cbSeg = 0;
    void * const pvSeg = RTSgBufGetCurrentSegment(pSgBuf, ~(size_t)0, &cbSeg);

    int    rcRet = VINF_SUCCESS;
    size_t cbToRead;
    if (cbSeg <= cbVol - offUnsigned)
        cbToRead = cbSeg;
    else if (pcbRead)
    {
        rcRet    = VINF_EOF;
        cbToRead = (size_t)(cbVol - offUnsigned);
    }
    else
        return VERR_EOF;

    /*
     * Ok, we've got a valid stretch within the file.  Do the reading.
     */
    if (cbToRead > 0)
    {
        int rc2 = RTDvmVolumeRead(pThis->hVol, offUnsigned, pvSeg, cbToRead);
        if (RT_SUCCESS(rc2))
        {
            offUnsigned += cbToRead;
            RTSgBufAdvance(pSgBuf, cbToRead);
        }
        else
        {
            cbToRead = 0;
            rcRet = rc2;
        }
    }

    pThis->offCurPos = offUnsigned;
    if (pcbRead)
        *pcbRead = cbToRead;
    return rcRet;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtDvmVfsFile_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    PRTVFSDVMFILE const pThis = (PRTVFSDVMFILE)pvThis;

    Assert(pSgBuf->cSegs == 1);
    NOREF(fBlocking);

    /*
     * Find the current position and check if it's within the volume.
     * Writing beyond the end of a volume is not supported.
     */
    uint64_t const cbVol = RTDvmVolumeGetSize(pThis->hVol);
    uint64_t offUnsigned = off < 0 ? pThis->offCurPos : (uint64_t)off;
    if (offUnsigned >= cbVol)
    {
        if (pcbWritten)
        {
            *pcbWritten = 0;
            pThis->offCurPos = offUnsigned;
        }
        return VERR_DISK_FULL; /** @todo VERR_DISK_FULL is not the best status code. */
    }

    size_t       cbSeg = 0;
    void * const pvSeg = RTSgBufGetCurrentSegment(pSgBuf, ~(size_t)0, &cbSeg);

    size_t cbToWrite;
    if (cbSeg <= cbVol - offUnsigned)
        cbToWrite = cbSeg;
    else if (pcbWritten)
        cbToWrite = (size_t)(cbVol - offUnsigned);
    else
        return VERR_EOF; /** @todo status to use above? or rather use VERR_DISK_FULL? */

    /*
     * Ok, we've got a valid stretch within the file.  Do the reading.
     */
    int rc = VINF_SUCCESS;
    if (cbToWrite > 0)
    {
        rc = RTDvmVolumeWrite(pThis->hVol, offUnsigned, pvSeg, cbToWrite);
        if (RT_SUCCESS(rc))
        {
            offUnsigned += cbToWrite;
            RTSgBufAdvance(pSgBuf, cbToWrite);
        }
        else
            cbToWrite = 0;
    }

    pThis->offCurPos = offUnsigned;
    if (pcbWritten)
        *pcbWritten = cbToWrite;
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtDvmVfsFile_Flush(void *pvThis)
{
    NOREF(pvThis);
    return VINF_SUCCESS; /** @todo Implement missing DVM API. */
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtDvmVfsFile_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTVFSDVMFILE pThis = (PRTVFSDVMFILE)pvThis;
    *poffActual = pThis->offCurPos;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtDvmVfsFile_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    NOREF(pvThis);
    NOREF(fMode);
    NOREF(fMask);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtDvmVfsFile_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                               PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    NOREF(pvThis);
    NOREF(pAccessTime);
    NOREF(pModificationTime);
    NOREF(pChangeTime);
    NOREF(pBirthTime);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtDvmVfsFile_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    NOREF(pvThis);
    NOREF(uid);
    NOREF(gid);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtDvmVfsFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTVFSDVMFILE pThis = (PRTVFSDVMFILE)pvThis;

    /*
     * Seek relative to which position.
     */
    uint64_t offWrt;
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            offWrt = 0;
            break;

        case RTFILE_SEEK_CURRENT:
            offWrt = pThis->offCurPos;
            break;

        case RTFILE_SEEK_END:
            offWrt = RTDvmVolumeGetSize(pThis->hVol);
            break;

        default:
            return VERR_INTERNAL_ERROR_5;
    }

    /*
     * Calc new position, take care to stay within bounds.
     *
     * @todo: Setting position beyond the end of the volume does not make sense.
     */
    uint64_t offNew;
    if (offSeek == 0)
        offNew = offWrt;
    else if (offSeek > 0)
    {
        offNew = offWrt + offSeek;
        if (   offNew < offWrt
            || offNew > RTFOFF_MAX)
            offNew = RTFOFF_MAX;
    }
    else if ((uint64_t)-offSeek < offWrt)
        offNew = offWrt + offSeek;
    else
        offNew = 0;

    /*
     * Update the state and set return value.
     */
    pThis->offCurPos = offNew;

    *poffActual = offNew;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) rtDvmVfsFile_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTVFSDVMFILE pThis = (PRTVFSDVMFILE)pvThis;
    *pcbFile = RTDvmVolumeGetSize(pThis->hVol);
    return VINF_SUCCESS;
}


/**
 * Standard file operations.
 */
DECL_HIDDEN_CONST(const RTVFSFILEOPS) g_rtDvmVfsStdFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "DvmFile",
            rtDvmVfsFile_Close,
            rtDvmVfsFile_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        rtDvmVfsFile_Read,
        rtDvmVfsFile_Write,
        rtDvmVfsFile_Flush,
        NULL /*pfnPollOne*/,
        rtDvmVfsFile_Tell,
        NULL /*Skip*/,
        NULL /*ZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    /*RTVFSIOFILEOPS_FEAT_NO_AT_OFFSET*/ 0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        rtDvmVfsFile_SetMode,
        rtDvmVfsFile_SetTimes,
        rtDvmVfsFile_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtDvmVfsFile_Seek,
    rtDvmVfsFile_QuerySize,
    NULL /*SetSize*/,
    NULL /*QueryMaxSize*/,
    RTVFSFILEOPS_VERSION
};


/**
 * Internal worker for RTDvmVolumeCreateVfsFile and rtDvmVfsDir_OpenFile.
 *
 * @returns IPRT status code.
 * @param   pVfsVol         The VFS volume, optional.
 * @param   hVol            The volume handle. (Reference not consumed.)
 * @param   fOpen           RTFILE_O_XXX (valid).
 * @param   phVfsFileOut    Where to return the handle to the file.
 */
static int rtDvmVfsCreateFileForVolume(PRTDVMVFSVOL pVfsVol, RTDVMVOLUME hVol, uint64_t fOpen, PRTVFSFILE phVfsFileOut)
{
    uint32_t cRefs = RTDvmVolumeRetain(hVol);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create the volume file.
     */
    RTVFSFILE       hVfsFile;
    PRTVFSDVMFILE   pThis;
    int rc = RTVfsNewFile(&g_rtDvmVfsStdFileOps, sizeof(*pThis), fOpen, NIL_RTVFS, NIL_RTVFSLOCK, &hVfsFile, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->offCurPos = 0;
        pThis->hVol      = hVol;
        pThis->fCanRead  = RT_BOOL(fOpen & RTFILE_O_READ);
        pThis->fCanWrite = RT_BOOL(fOpen & RTFILE_O_WRITE);
        pThis->pVfsVol   = pVfsVol;

        *phVfsFileOut = hVfsFile;
        return VINF_SUCCESS;
    }

    RTDvmVolumeRelease(hVol);
    return rc;
}


RTDECL(int) RTDvmVolumeCreateVfsFile(RTDVMVOLUME hVol, uint64_t fOpen, PRTVFSFILE phVfsFileOut)
{
    AssertPtrReturn(hVol, VERR_INVALID_HANDLE);
    AssertPtrReturn(phVfsFileOut, VERR_INVALID_POINTER);
    AssertReturn(fOpen & RTFILE_O_ACCESS_MASK, VERR_INVALID_FLAGS);
    AssertReturn(!(fOpen & ~RTFILE_O_VALID_MASK), VERR_INVALID_FLAGS);
    return rtDvmVfsCreateFileForVolume(NULL, hVol, fOpen, phVfsFileOut);
}


/*********************************************************************************************************************************
*   DVM Symbolic Link Objects                                                                                                    *
*********************************************************************************************************************************/
/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtDvmVfsSym_Close(void *pvThis)
{
    PRTVFSDVMSYMLINK pThis = (PRTVFSDVMSYMLINK)pvThis;
    if (pThis->pszSymlink)
    {
        RTStrFree(pThis->pszSymlink);
        pThis->pszSymlink = NULL;
    }
    if (pThis->hVol != NIL_RTDVMVOLUME)
    {
        RTDvmVolumeRelease(pThis->hVol);
        pThis->hVol = NIL_RTDVMVOLUME;
    }
    if (pThis->hVolMgr != NIL_RTDVM)
    {
        RTDvmRelease(pThis->hVolMgr);
        pThis->hVolMgr = NIL_RTDVM;
    }
    return VINF_SUCCESS;
}


/**
 * Worker for rtDvmVfsSym_QueryInfo and rtDvmVfsDir_Read.
 *
 * @returns IPRT status code.
 * @param   hVolume             The volume handle.
 * @param   hVolMgr             The volume manager handle. Optional.
 * @param   pszTarget           The link target.
 * @param   pObjInfo            The object info structure to populate.
 * @param   enmAddAttr          The additional attributes to supply.
 */
static int rtDvmVfsSym_QueryInfoWorker(RTDVMVOLUME hVolume, RTDVM hVolMgr, const char *pszTarget,
                                       PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RT_ZERO(*pObjInfo);
    pObjInfo->cbObject = pObjInfo->cbAllocated = pszTarget ? strlen(pszTarget) : 0;
    pObjInfo->Attr.fMode = 0777 | RTFS_TYPE_SYMLINK | RTFS_DOS_NT_REPARSE_POINT;

    return rtDvmVfsFileSym_QueryAddAttrWorker(hVolume, hVolMgr, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtDvmVfsSym_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTVFSDVMSYMLINK pThis = (PRTVFSDVMSYMLINK)pvThis;
    return rtDvmVfsSym_QueryInfoWorker(pThis->hVol, pThis->hVolMgr, pThis->szTarget, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSSYMLINKOPS,pfnRead}
 */
static DECLCALLBACK(int) rtDvmVfsSym_Read(void *pvThis, char *pszTarget, size_t cbTarget)
{
    PRTVFSDVMSYMLINK pThis = (PRTVFSDVMSYMLINK)pvThis;
    return RTStrCopy(pszTarget, cbTarget, pThis->szTarget);
}


/**
 * DVM symbolic link operations.
 */
static const RTVFSSYMLINKOPS g_rtDvmVfsSymOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_SYMLINK,
        "DvmSymlink",
        rtDvmVfsSym_Close,
        rtDvmVfsSym_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSSYMLINKOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSSYMLINKOPS, ObjSet) - RT_UOFFSETOF(RTVFSSYMLINKOPS, Obj),
        NULL /*rtDvmVfsSym_SetMode*/,
        NULL /*rtDvmVfsSym_SetTimes*/,
        NULL /*rtDvmVfsSym_SetOwner*/,
        RTVFSOBJSETOPS_VERSION
    },
    rtDvmVfsSym_Read,
    RTVFSSYMLINKOPS_VERSION
};


/**
 * Internal worker for rtDvmVfsDir_OpenFile.
 *
 * @returns IPRT status code.
 * @param   hVol                The volume handle (not consumed).
 * @param   hVolMgr             The volume manager handle (not consumed).
 * @param   iVol                The volume number.
 * @param   pszSymlink          The volume name. Consumed on success.
 * @param   phVfsSymlinkOut     Where to return the handle to the file.
 */
static int rtDvmVfsCreateSymlinkForVolume(RTDVMVOLUME hVol, RTDVM hVolMgr, uint32_t iVol, char *pszSymlink,
                                          PRTVFSSYMLINK phVfsSymlinkOut)
{
    uint32_t cRefs = RTDvmVolumeRetain(hVol);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    cRefs = RTDvmRetain(hVolMgr);
    AssertReturnStmt(cRefs != UINT32_MAX, RTDvmVolumeRelease(hVol), VERR_INVALID_HANDLE);

    /*
     * Create the symlink.
     */
    RTVFSSYMLINK        hVfsSym;
    PRTVFSDVMSYMLINK    pThis;
    int rc = RTVfsNewSymlink(&g_rtDvmVfsSymOps, sizeof(*pThis), NIL_RTVFS, NIL_RTVFSLOCK, &hVfsSym, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVol       = hVol;
        pThis->hVolMgr    = hVolMgr;
        pThis->pszSymlink = pszSymlink;
        RTStrPrintf(pThis->szTarget, sizeof(pThis->szTarget), "vol%u", iVol);

        *phVfsSymlinkOut = hVfsSym;
        return VINF_SUCCESS;
    }
    RTDvmRelease(hVolMgr);
    RTDvmVolumeRelease(hVol);
    return rc;
}



/*********************************************************************************************************************************
*   DVM Directory Objects                                                                                                        *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtDvmVfsDir_Close(void *pvThis)
{
    PRTDVMVFSDIR pThis = (PRTDVMVFSDIR)pvThis;

    if (pThis->hCurVolume != NIL_RTDVMVOLUME)
    {
        RTDvmVolumeRelease(pThis->hCurVolume);
        pThis->hCurVolume = NIL_RTDVMVOLUME;
    }

    if (pThis->pszNameAlias)
    {
        RTStrFree(pThis->pszNameAlias);
        pThis->pszNameAlias = NULL;
    }

    pThis->pVfsVol = NULL;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtDvmVfsDir_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTDVMVFSDIR pThis = (PRTDVMVFSDIR)pvThis;
    pObjInfo->cbObject    = pThis->pVfsVol->cVolumes;
    pObjInfo->cbAllocated = pThis->pVfsVol->cVolumes;
    RTTimeSpecSetNano(&pObjInfo->AccessTime, 0);
    RTTimeSpecSetNano(&pObjInfo->ModificationTime, 0);
    RTTimeSpecSetNano(&pObjInfo->ChangeTime, 0);
    RTTimeSpecSetNano(&pObjInfo->BirthTime, 0);
    pObjInfo->Attr.fMode = RTFS_TYPE_DIRECTORY | RTFS_DOS_DIRECTORY;
    if (pThis->pVfsVol->fReadOnly)
        pObjInfo->Attr.fMode |= RTFS_DOS_READONLY | 0555;
    else
        pObjInfo->Attr.fMode |= 0777;

    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_NOTHING:
        case RTFSOBJATTRADD_UNIX:
            pObjInfo->Attr.u.Unix.uid           = NIL_RTUID;
            pObjInfo->Attr.u.Unix.gid           = (RTGID)RTDvmMapGetFormatType(pThis->pVfsVol->hVolMgr);
            pObjInfo->Attr.u.Unix.cHardlinks    = pThis->pVfsVol->cVolumes;
            pObjInfo->Attr.u.Unix.INodeIdDevice = 0;
            pObjInfo->Attr.u.Unix.INodeId       = 0;
            pObjInfo->Attr.u.Unix.fFlags        = 0;
            pObjInfo->Attr.u.Unix.GenerationId  = 0;
            pObjInfo->Attr.u.Unix.Device        = 0;
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
            pObjInfo->Attr.u.UnixOwner.uid      = NIL_RTUID;
            pObjInfo->Attr.u.UnixOwner.szName[0] = '\0';
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            pObjInfo->Attr.u.UnixGroup.gid      = (RTGID)RTDvmMapGetFormatType(pThis->pVfsVol->hVolMgr);
            RTStrCopy(pObjInfo->Attr.u.UnixGroup.szName, sizeof(pObjInfo->Attr.u.UnixGroup.szName),
                      RTDvmMapGetFormatName(pThis->pVfsVol->hVolMgr));
            break;

        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.u.EASize.cb = 0;
            break;

        default:
            return VERR_INVALID_PARAMETER;
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtDvmVfsDir_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    NOREF(pvThis); NOREF(fMode); NOREF(fMask);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtDvmVfsDir_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                              PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    NOREF(pvThis); NOREF(pAccessTime); NOREF(pModificationTime); NOREF(pChangeTime); NOREF(pBirthTime);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtDvmVfsDir_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    RT_NOREF(pvThis, uid, gid);
    return VERR_NOT_SUPPORTED;
}


static int rtDvmVfsDir_FindEntry(PRTDVMVFSDIR pThis, const char *pszEntry,
                                 PRTDVMVOLUME phVolume, uint32_t *piVol, char **ppszSymlink)
{
    *phVolume    = NIL_RTDVMVOLUME;
    *ppszSymlink = NULL;
    *piVol       = UINT32_MAX;

    /*
     * Enumerate the volumes and try match the volume name.
     */
    int rc;
    PRTDVMVFSVOL pVfsVol = pThis->pVfsVol;
    if (pVfsVol->cVolumes > 0)
    {
        /* The first volume. */
        uint32_t iVol = 0;
        RTDVMVOLUME hVol;
        rc = RTDvmMapQueryFirstVolume(pThis->pVfsVol->hVolMgr, &hVol);
        while (RT_SUCCESS(rc))
        {
            /* Match the name. */
            bool  fMatch;
            char *pszVolName;
            rc = RTDvmVolumeQueryName(hVol, &pszVolName);
            if (RT_SUCCESS(rc))
            {
                fMatch = RTStrCmp(pszEntry, pszVolName) == 0 && *pszVolName != '\0';
                if (fMatch)
                {
                    *phVolume    = hVol;
                    *ppszSymlink = pszVolName;
                    *piVol       = iVol;
                    return VINF_SUCCESS;
                }
                RTStrFree(pszVolName);
            }
            else if (rc == VERR_NOT_SUPPORTED)
                fMatch = false;
            else
            {
                RTDvmVolumeRelease(hVol);
                break;
            }

            /* Match the sequential volume number. */
            if (!fMatch)
            {
                char szTmp[16];
                RTStrPrintf(szTmp, sizeof(szTmp), "vol%u", iVol);
                fMatch = RTStrCmp(pszEntry, szTmp) == 0;
            }

            if (fMatch)
            {
                *phVolume = hVol;
                *piVol    = iVol;
                return VINF_SUCCESS;
            }

            /* More volumes? */
            iVol++;
            if (iVol >= pVfsVol->cVolumes)
            {
                RTDvmVolumeRelease(hVol);
                rc = VERR_FILE_NOT_FOUND;
                break;
            }

            /* Get the next volume. */
            RTDVMVOLUME hVolNext;
            rc = RTDvmMapQueryNextVolume(pThis->pVfsVol->hVolMgr, hVol, &hVolNext);
            RTDvmVolumeRelease(hVol);
            hVol = hVolNext;
        }
    }
    else
        rc = VERR_FILE_NOT_FOUND;
    return rc;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpen}
 */
static DECLCALLBACK(int) rtDvmVfsDir_Open(void *pvThis, const char *pszEntry, uint64_t fOpen, uint32_t fFlags, PRTVFSOBJ phVfsObj)
{
    PRTDVMVFSDIR pThis = (PRTDVMVFSDIR)pvThis;

    /*
     * Special case: '.' and '..'
     */
    if (   pszEntry[0] == '.'
        && (   pszEntry[1] == '\0'
            || (   pszEntry[1] == '.'
                && pszEntry[2] == '\0')))
    {
        if (   (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN
            || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN_CREATE
            || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_CREATE_REPLACE)
        {
            if (fFlags & RTVFSOBJ_F_OPEN_DIRECTORY)
            {
                RTVFSDIR hVfsDir;
                int rc = rtDvmVfsVol_OpenRoot(pThis->pVfsVol, &hVfsDir);
                if (RT_SUCCESS(rc))
                {
                    *phVfsObj = RTVfsObjFromDir(hVfsDir);
                    RTVfsDirRelease(hVfsDir);
                    AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                }
                return rc;
            }
            return VERR_IS_A_DIRECTORY;
        }
        return VERR_ACCESS_DENIED;
    }

    /*
     * Open volume file.
     */
    RTDVMVOLUME hVolume    = NIL_RTDVMVOLUME;
    uint32_t    iVol       = 0;
    char       *pszSymlink = NULL;
    int rc = rtDvmVfsDir_FindEntry(pThis, pszEntry, &hVolume, &iVol, &pszSymlink);
    if (RT_SUCCESS(rc))
    {
        if (   (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN
            || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN_CREATE
            || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_CREATE_REPLACE)
        {
            if (fFlags & (RTVFSOBJ_F_OPEN_FILE | RTVFSOBJ_F_OPEN_DEV_BLOCK))
            {
                if (!pszSymlink)
                {
                    if (   !(fOpen & RTFILE_O_WRITE)
                        || !pThis->pVfsVol->fReadOnly)
                    {
                        /* Create file object. */
                        RTVFSFILE hVfsFile;
                        rc = rtDvmVfsCreateFileForVolume(pThis->pVfsVol, hVolume, fOpen, &hVfsFile);
                        if (RT_SUCCESS(rc))
                        {
                            *phVfsObj = RTVfsObjFromFile(hVfsFile);
                            RTVfsFileRelease(hVfsFile);
                            AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                        }
                    }
                    else
                        rc = VERR_WRITE_PROTECT;
                }
                else
                    rc = VERR_IS_A_SYMLINK;
            }
            else if (fFlags & RTVFSOBJ_F_OPEN_SYMLINK)
            {
                /* Create symlink object */
                RTVFSSYMLINK hVfsSym = NIL_RTVFSSYMLINK; /* (older gcc maybe used uninitialized) */
                rc = rtDvmVfsCreateSymlinkForVolume(hVolume, pThis->pVfsVol ? pThis->pVfsVol->hVolMgr : NIL_RTDVM, iVol,
                                                    pszSymlink, &hVfsSym);
                if (RT_SUCCESS(rc))
                {
                    *phVfsObj = RTVfsObjFromSymlink(hVfsSym);
                    RTVfsSymlinkRelease(hVfsSym);
                    AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                    pszSymlink = NULL;
                }
            }
            else
                rc = VERR_IS_A_FILE;
        }
        else
            rc = VERR_ALREADY_EXISTS;
        RTDvmVolumeRelease(hVolume);
        if (pszSymlink)
            RTStrFree(pszSymlink);
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpenFile}
 */
static DECLCALLBACK(int) rtDvmVfsDir_OpenFile(void *pvThis, const char *pszFilename, uint64_t fOpen, PRTVFSFILE phVfsFile)
{
    RTVFSOBJ hVfsObj;
    int rc = rtDvmVfsDir_Open(pvThis, pszFilename, fOpen, RTVFSOBJ_F_OPEN_FILE, &hVfsObj);
    if (RT_SUCCESS(rc))
    {
        *phVfsFile = RTVfsObjToFile(hVfsObj);
        RTVfsObjRelease(hVfsObj);
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnCreateDir}
 */
static DECLCALLBACK(int) rtDvmVfsDir_CreateDir(void *pvThis, const char *pszSubDir, RTFMODE fMode, PRTVFSDIR phVfsDir)
{
    RT_NOREF(pvThis, pszSubDir, fMode, phVfsDir);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpenSymlink}
 */
static DECLCALLBACK(int) rtDvmVfsDir_OpenSymlink(void *pvThis, const char *pszSymlink, PRTVFSSYMLINK phVfsSymlink)
{
    RT_NOREF(pvThis, pszSymlink, phVfsSymlink);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnCreateSymlink}
 */
static DECLCALLBACK(int) rtDvmVfsDir_CreateSymlink(void *pvThis, const char *pszSymlink, const char *pszTarget,
                                                  RTSYMLINKTYPE enmType, PRTVFSSYMLINK phVfsSymlink)
{
    RT_NOREF(pvThis, pszSymlink, pszTarget, enmType, phVfsSymlink);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnUnlinkEntry}
 */
static DECLCALLBACK(int) rtDvmVfsDir_UnlinkEntry(void *pvThis, const char *pszEntry, RTFMODE fType)
{
    RT_NOREF(pvThis, pszEntry, fType);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnRenameEntry}
 */
static DECLCALLBACK(int) rtDvmVfsDir_RenameEntry(void *pvThis, const char *pszEntry, RTFMODE fType, const char *pszNewName)
{
    RT_NOREF(pvThis, pszEntry, fType, pszNewName);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnRewindDir}
 */
static DECLCALLBACK(int) rtDvmVfsDir_RewindDir(void *pvThis)
{
    PRTDVMVFSDIR pThis = (PRTDVMVFSDIR)pvThis;

    if (pThis->hCurVolume != NIL_RTDVMVOLUME)
    {
        RTDvmVolumeRelease(pThis->hCurVolume);
        pThis->hCurVolume = NIL_RTDVMVOLUME;
    }
    pThis->fReturnCurrent = false;
    pThis->offDir         = 0;
    if (pThis->pszNameAlias)
    {
        RTStrFree(pThis->pszNameAlias);
        pThis->pszNameAlias = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnReadDir}
 */
static DECLCALLBACK(int) rtDvmVfsDir_ReadDir(void *pvThis, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry,
                                            RTFSOBJATTRADD enmAddAttr)
{
    PRTDVMVFSDIR pThis   = (PRTDVMVFSDIR)pvThis;
    PRTDVMVFSVOL pVfsVol = pThis->pVfsVol;
    int          rc;

    /*
     * Format the volume name since we'll be needing it all but the final call.
     */
    char         szVolNo[16];
    size_t const cchVolNo = RTStrPrintf(szVolNo, sizeof(szVolNo), "vol%u", pThis->offDir);

    if (!pThis->fReturnCurrent)
    {
        /*
         * Do we have a pending name alias to return?
         */
        if (pThis->pszNameAlias)
        {
            size_t cchNameAlias = strlen(pThis->pszNameAlias);
            size_t cbNeeded     = RT_UOFFSETOF_DYN(RTDIRENTRYEX, szName[cchNameAlias + 1]);
            if (cbNeeded <= *pcbDirEntry)
            {
                *pcbDirEntry = cbNeeded;

                /* Do the names. */
                pDirEntry->cbName = (uint16_t)cchNameAlias;
                memcpy(pDirEntry->szName, pThis->pszNameAlias, cchNameAlias + 1);
                pDirEntry->cwcShortName = 0;
                pDirEntry->wszShortName[0] = '\0';


                /* Do the rest. */
                rc = rtDvmVfsSym_QueryInfoWorker(pThis->hCurVolume, pVfsVol->hVolMgr, szVolNo, &pDirEntry->Info, enmAddAttr);
                if (RT_SUCCESS(rc))
                {
                    RTStrFree(pThis->pszNameAlias);
                    pThis->pszNameAlias = NULL;
                    pThis->offDir      += 1;
                }
                return rc;
            }

            *pcbDirEntry = cbNeeded;
            return VERR_BUFFER_OVERFLOW;
        }

        /*
         * Get the next volume to return info about.
         */
        if (pThis->offDir < pVfsVol->cVolumes)
        {
            RTDVMVOLUME hNextVolume;
            if (pThis->offDir == 0)
                rc = RTDvmMapQueryFirstVolume(pVfsVol->hVolMgr, &hNextVolume);
            else
                rc = RTDvmMapQueryNextVolume(pVfsVol->hVolMgr, pThis->hCurVolume, &hNextVolume);
            if (RT_FAILURE(rc))
                return rc;
            RTDvmVolumeRelease(pThis->hCurVolume);
            pThis->hCurVolume = hNextVolume;

            /* Check if we need to return a name alias later. */
            rc = RTDvmVolumeQueryName(pThis->hCurVolume, &pThis->pszNameAlias);
            if (RT_FAILURE(rc))
                pThis->pszNameAlias = NULL;
            else if (*pThis->pszNameAlias == '\0')
            {
                RTStrFree(pThis->pszNameAlias);
                pThis->pszNameAlias = NULL;
            }
        }
        else
        {
            RTDvmVolumeRelease(pThis->hCurVolume);
            pThis->hCurVolume = NIL_RTDVMVOLUME;
            return VERR_NO_MORE_FILES;
        }
    }

    /*
     * Figure out the name length.
     */
    size_t cbNeeded = RT_UOFFSETOF_DYN(RTDIRENTRYEX, szName[cchVolNo + 1]);
    if (cbNeeded <= *pcbDirEntry)
    {
        *pcbDirEntry = cbNeeded;

        /* Do the names. */
        pDirEntry->cbName = (uint16_t)cchVolNo;
        memcpy(pDirEntry->szName, szVolNo, cchVolNo + 1);
        pDirEntry->cwcShortName = 0;
        pDirEntry->wszShortName[0] = '\0';

        /* Do the rest. */
        rc = rtDvmVfsFile_QueryInfoWorker(pThis->hCurVolume, pVfsVol->hVolMgr, pVfsVol->fReadOnly, &pDirEntry->Info, enmAddAttr);
        if (RT_SUCCESS(rc))
        {
            pThis->fReturnCurrent = false;
            if (!pThis->pszNameAlias)
                pThis->offDir += 1;
            return rc;
        }
    }
    else
    {
        *pcbDirEntry = cbNeeded;
        rc = VERR_BUFFER_OVERFLOW;
    }
    pThis->fReturnCurrent = true;
    return rc;
}


/**
 * DVM (root) directory operations.
 */
static const RTVFSDIROPS g_rtDvmVfsDirOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_DIR,
        "DvmDir",
        rtDvmVfsDir_Close,
        rtDvmVfsDir_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSDIROPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSDIROPS, ObjSet) - RT_UOFFSETOF(RTVFSDIROPS, Obj),
        rtDvmVfsDir_SetMode,
        rtDvmVfsDir_SetTimes,
        rtDvmVfsDir_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtDvmVfsDir_Open,
    NULL /* pfnFollowAbsoluteSymlink */,
    rtDvmVfsDir_OpenFile,
    NULL /* pfnOpenDir */,
    rtDvmVfsDir_CreateDir,
    rtDvmVfsDir_OpenSymlink,
    rtDvmVfsDir_CreateSymlink,
    NULL /* pfnQueryEntryInfo */,
    rtDvmVfsDir_UnlinkEntry,
    rtDvmVfsDir_RenameEntry,
    rtDvmVfsDir_RewindDir,
    rtDvmVfsDir_ReadDir,
    RTVFSDIROPS_VERSION,
};



/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnClose}
 */
static DECLCALLBACK(int) rtDvmVfsVol_Close(void *pvThis)
{
    PRTDVMVFSVOL pThis = (PRTDVMVFSVOL)pvThis;
    LogFlow(("rtDvmVfsVol_Close(%p)\n", pThis));

    if (   pThis->fCloseDvm
        && pThis->hVolMgr != NIL_RTDVM )
        RTDvmRelease(pThis->hVolMgr);
    pThis->hVolMgr = NIL_RTDVM;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtDvmVfsVol_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RT_NOREF(pvThis, pObjInfo, enmAddAttr);
    return VERR_WRONG_TYPE;
}


/**
 * @interface_method_impl{RTVFSOPS,pfnOpenRoot}
 */
static DECLCALLBACK(int) rtDvmVfsVol_OpenRoot(void *pvThis, PRTVFSDIR phVfsDir)
{
    PRTDVMVFSVOL pThis = (PRTDVMVFSVOL)pvThis;

    PRTDVMVFSDIR pNewDir;
    int rc = RTVfsNewDir(&g_rtDvmVfsDirOps, sizeof(*pNewDir), 0 /*fFlags*/, pThis->hVfsSelf,
                         NIL_RTVFSLOCK /*use volume lock*/, phVfsDir, (void **)&pNewDir);
    if (RT_SUCCESS(rc))
    {
        pNewDir->offDir         = 0;
        pNewDir->pVfsVol        = pThis;
        pNewDir->fReturnCurrent = false;
        pNewDir->pszNameAlias   = NULL;
        pNewDir->hCurVolume     = NIL_RTDVMVOLUME;
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSOPS,pfnQueryRangeState}
 */
static DECLCALLBACK(int) rtDvmVfsVol_QueryRangeState(void *pvThis, uint64_t off, size_t cb, bool *pfUsed)
{
    RT_NOREF(pvThis, off, cb, pfUsed);
    return VERR_NOT_IMPLEMENTED;
}


DECL_HIDDEN_CONST(const RTVFSOPS) g_rtDvmVfsVolOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_VFS,
        "DvmVol",
        rtDvmVfsVol_Close,
        rtDvmVfsVol_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSOPS_VERSION,
    0 /* fFeatures */,
    rtDvmVfsVol_OpenRoot,
    rtDvmVfsVol_QueryRangeState,
    RTVFSOPS_VERSION
};



/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtDvmVfsChain_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
                                                PRTVFSCHAINELEMSPEC pElement, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec);

    /*
     * Basic checks.
     */
    if (pElement->enmTypeIn != RTVFSOBJTYPE_FILE)
        return pElement->enmTypeIn == RTVFSOBJTYPE_INVALID ? VERR_VFS_CHAIN_CANNOT_BE_FIRST_ELEMENT : VERR_VFS_CHAIN_TAKES_FILE;
    if (pElement->enmType != RTVFSOBJTYPE_VFS)
        return VERR_VFS_CHAIN_ONLY_VFS;

    if (pElement->cArgs > 1)
        return VERR_VFS_CHAIN_AT_MOST_ONE_ARG;

    /*
     * Parse the flag if present, save in pElement->uProvider.
     */
    /** @todo allow specifying sector size   */
    bool fReadOnly = (pSpec->fOpenFile & RTFILE_O_ACCESS_MASK) == RTFILE_O_READ;
    if (pElement->cArgs > 0)
    {
        const char *psz = pElement->paArgs[0].psz;
        if (*psz)
        {
            if (   !strcmp(psz, "ro")
                || !strcmp(psz, "r"))
                fReadOnly = true;
            else if (!strcmp(psz, "rw"))
                fReadOnly = false;
            else
            {
                *poffError = pElement->paArgs[0].offSpec;
                return RTErrInfoSet(pErrInfo, VERR_VFS_CHAIN_INVALID_ARGUMENT, "Expected 'ro' or 'rw' as argument");
            }
        }
    }

    pElement->uProvider = fReadOnly;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnInstantiate}
 */
static DECLCALLBACK(int) rtDvmVfsChain_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                   PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                   PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, poffError, pErrInfo);
    AssertReturn(hPrevVfsObj != NIL_RTVFSOBJ, VERR_VFS_CHAIN_IPE);

    /*
     * Instantiate the volume manager and open the map stuff.
     */
    RTVFSFILE hPrevVfsFile = RTVfsObjToFile(hPrevVfsObj);
    AssertReturn(hPrevVfsFile != NIL_RTVFSFILE, VERR_VFS_CHAIN_CAST_FAILED);

    RTDVM hVolMgr;
    int rc = RTDvmCreate(&hVolMgr, hPrevVfsFile, 512, 0 /*fFlags*/);
    RTVfsFileRelease(hPrevVfsFile);
    if (RT_SUCCESS(rc))
    {
        rc = RTDvmMapOpen(hVolMgr);
        if (RT_SUCCESS(rc))
        {
            /*
             * Create a VFS instance for the volume manager.
             */
            RTVFS        hVfs  = NIL_RTVFS;
            PRTDVMVFSVOL pThis = NULL;
            rc = RTVfsNew(&g_rtDvmVfsVolOps, sizeof(RTDVMVFSVOL), NIL_RTVFS, RTVFSLOCK_CREATE_RW, &hVfs, (void **)&pThis);
            if (RT_SUCCESS(rc))
            {
                pThis->hVolMgr   = hVolMgr;
                pThis->fCloseDvm = true;
                pThis->fReadOnly = pElement->uProvider == (uint64_t)true;
                pThis->cVolumes  = RTDvmMapGetValidVolumes(hVolMgr);
                pThis->hVfsSelf  = hVfs;

                *phVfsObj = RTVfsObjFromVfs(hVfs);
                RTVfsRelease(hVfs);
                return *phVfsObj != NIL_RTVFSOBJ ? VINF_SUCCESS : VERR_VFS_CHAIN_CAST_FAILED;
            }
        }
        else
            rc = RTErrInfoSetF(pErrInfo, rc, "RTDvmMapOpen failed: %Rrc", rc);
        RTDvmRelease(hVolMgr);
    }
    else
        rc = RTErrInfoSetF(pErrInfo, rc, "RTDvmCreate failed: %Rrc", rc);
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnCanReuseElement}
 */
static DECLCALLBACK(bool) rtDvmVfsChain_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                        PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                        PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pElement, pReuseSpec, pReuseElement);
    return false;
}


/** VFS chain element 'file'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainIsoFsVolReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "dvm",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Opens a container image using the VD API.\n"
                                "Optionally takes one parameter 'ro' (read only) or 'rw' (read write).\n",
    /* pfnValidate = */         rtDvmVfsChain_Validate,
    /* pfnInstantiate = */      rtDvmVfsChain_Instantiate,
    /* pfnCanReuseElement = */  rtDvmVfsChain_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainIsoFsVolReg, rtVfsChainIsoFsVolReg);

