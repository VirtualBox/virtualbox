/* $Id: pdbvfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - PDB Virtual Filesystem (read only).
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_FS
#include "internal/iprt.h"
#include <iprt/fsvfs.h>

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>
#include <iprt/utf16.h>
#include <iprt/uuid.h>
#include <iprt/formats/pdb.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to an ISO volume (VFS instance data). */
typedef struct RTFSPDBVOL *PRTFSPDBVOL;
/** Pointer to a const ISO volume (VFS instance data). */
typedef struct RTFSPDBVOL const *PCRTFSPDBVOL;


/**
 * Substream info.
 */
typedef struct RTFSPDBSUBSTREAM
{
    /** The parent stream. */
    uint32_t    idxStream;
    /** The substream index within the parent. */
    uint8_t     idxSubstream;
    /** The offset of the start of the substream within the parent. */
    uint32_t    offSubstream;
    /** The size of the substream. */
    uint32_t    cbSubstream;
    /** The name of the substream (including the parent one at the start).
     * Readonly string constant, as substreams are all hardcoded. */
    const char *pszName;
} RTFSPDBSUBSTREAM;
typedef RTFSPDBSUBSTREAM *PRTFSPDBSUBSTREAM;
typedef RTFSPDBSUBSTREAM const *PCRTFSPDBSUBSTREAM;


/**
 * Stream info.
 */
typedef struct RTFSPDBSTREAMINFO
{
    /** The stream name.
     * Unnamed streams will be set to NULL. Standard streams are assigned
     * names (C-litterals) the rest is matched up with RTFSPDBVOL::pszzNames
     * content. */
    const char         *pszName;
    /** Size of the stream. */
    uint32_t            cbStream;
    /** Number of pages in the stream. */
    uint32_t            cPages;
    /** Pointer to the page map for the stream (within RTFSPDBVOL::pbRoot). */
    union
    {
        void const     *pv;
        PCRTPDB20PAGE   pa20;
        PCRTPDB70PAGE   pa70;
    } PageMap;
    /** Number of substreams. */
    uint8_t             cSubstreams;
    /** The index into RTFSPDBVOL::aSubstreams of the first one. */
    uint8_t             idxFirstSubstream;
} RTFSPDBSTREAMINFO;
typedef RTFSPDBSTREAMINFO *PRTFSPDBSTREAMINFO;
typedef RTFSPDBSTREAMINFO const *PCRTFSPDBSTREAMINFO;


/**
 * Private data for a VFS file object.
 */
typedef struct RTFSPDBFILEOBJ
{
    /** Pointer to the PDB volume data. */
    PRTFSPDBVOL         pPdb;
    /** The stream number (0 based). */
    uint32_t            idxStream;
    /** The start offset of the substream this file object represents. This is
     * zero for full streams. */
    uint32_t            offSubstream;
    /** Size of the stream (or substream). */
    uint32_t            cbStream;
    /** Number of pages in the stream (the whole stream, not just the substream). */
    uint32_t            cPages;
    /** Pointer to the page map for the stream (within RTFSPDBVOL::pbRoot)
     * (again the whole stream, not just the substream). */
    union
    {
        void const     *pv;
        PCRTPDB20PAGE   pa20;
        PCRTPDB70PAGE   pa70;
    } PageMap;
    /** The current file offset. */
    uint64_t            offFile;
} RTFSPDBFILEOBJ;
typedef RTFSPDBFILEOBJ *PRTFSPDBFILEOBJ;

/**
 * Private data for a VFS directory object.
 */
typedef struct RTFSPDBDIROBJ
{
    /** Pointer to the PDB volume data. */
    PRTFSPDBVOL         pPdb;
    /** The next stream and substream number to return info for when reading. */
    union
    {
        uint32_t            idxNext;
        struct
        {
            /** This is zero for the whole stream and non-zero for substreams. */
            uint32_t        idxNextSubstream : 8;
            /** The next stream number. */
            uint32_t        idxNextStream    : 24;
        } s;
    } u;
} RTFSPDBDIROBJ;
typedef RTFSPDBDIROBJ *PRTFSPDBDIROBJ;


/**
 * Indicates which PDB version we're accessing.
 */
typedef enum RTFSPDBVER
{
    /** Invalid zero value.   */
    RTFSPDBVER_INVALID = 0,
    /** Accessing a v2.0 PDB. */
    RTFSPDBVER_2,
    /** Accessing a v7.0 PDB. */
    RTFSPDBVER_7
} RTFSPDBVER;


/**
 * A PDB volume.
 */
typedef struct RTFSPDBVOL
{
    /** Handle to itself. */
    RTVFS               hVfsSelf;
    /** The file, partition, or whatever backing the PDB file. */
    RTVFSFILE           hVfsBacking;
    /** The size of the backing thingy. */
    uint64_t            cbBacking;
    /** The size of the backing thingy in sectors (cbSector). */
    uint64_t            cBackingPages;
    /** Flags. */
    uint32_t            fFlags;
    /** The page size (in bytes). */
    uint32_t            cbPage;
    /** The format version. */
    RTFSPDBVER          enmVersion;
    /** Number of streams.   */
    uint32_t            cStreams;
    /** The size of the root stream. */
    uint32_t            cbRoot;
    /** Pointer to the root directory bytes. */
    uint8_t            *pbRoot;

    /** @name PDB metadata from stream \#1.
     * @{ */
    /** The PDB age. */
    uint32_t            uAge;
    /** The PDB timestamp. */
    uint32_t            uTimestamp;
    /** The PDB UUID. */
    RTUUID              Uuid;
    /** The VC date (see RTPDB70NAMES::uVersion and RTPDB20NAMES::uVersion). */
    uint32_t            uVcDate;
    /** Size of the name string table. */
    uint32_t            cbNames;
    /** Name string table. */
    char               *pszzNames;
    /** @} */

    /** The DBI header from stream \#3. */
    RTPDBDBIHDR         DbiHdr;

    /** Extra per-stream info.  We've do individual allocations here in case we
     * want to add write support. */
    PRTFSPDBSTREAMINFO *papStreamInfo;

    /** Number of substreams in aSubstreams. */
    uint8_t             cSubstreams;
    /** Substreams (only from the DBI atm.). */
    RTFSPDBSUBSTREAM    aSubstreams[8];
} RTFSPDBVOL;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) rtFsPdbVol_OpenRoot(void *pvThis, PRTVFSDIR phVfsDir);



/**
 * Helper for methods returning file information.
 */
static void rtFsPdbPopulateObjInfo(PRTFSPDBVOL pPdb, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr,
                                   uint32_t cbStream, uint32_t idxStream, uint8_t idxSubstream, bool fIsDir)
{
    RTTimeSpecSetNano(&pObjInfo->AccessTime, 0);
    RTTimeSpecSetNano(&pObjInfo->ModificationTime, 0);
    RTTimeSpecSetNano(&pObjInfo->ChangeTime, 0);
    RTTimeSpecSetNano(&pObjInfo->BirthTime, 0);
    pObjInfo->cbObject              = cbStream == UINT32_MAX ? 0 : cbStream;
    pObjInfo->cbAllocated           = (uint64_t)RTPdbSizeToPages(cbStream, pPdb->cbPage) * pPdb->cbPage;
    pObjInfo->Attr.fMode            = RTFS_UNIX_IRUSR | RTFS_UNIX_IRGRP | RTFS_UNIX_IROTH | RTFS_DOS_READONLY;
    if (!fIsDir)
        pObjInfo->Attr.fMode       |= RTFS_TYPE_FILE;
    else
        pObjInfo->Attr.fMode       |= RTFS_TYPE_DIRECTORY | RTFS_UNIX_IXUSR | RTFS_UNIX_IXGRP | RTFS_UNIX_IXOTH;
    pObjInfo->Attr.enmAdditional    = enmAddAttr;

    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_NOTHING: RT_FALL_THRU();
        case RTFSOBJATTRADD_UNIX:
            pObjInfo->Attr.u.Unix.uid           = NIL_RTUID;
            pObjInfo->Attr.u.Unix.gid           = NIL_RTGID;
            pObjInfo->Attr.u.Unix.cHardlinks    = 1 + fIsDir;
            pObjInfo->Attr.u.Unix.INodeIdDevice = 0;
            pObjInfo->Attr.u.Unix.INodeId       = idxStream | ((uint64_t)idxSubstream << 32);
            pObjInfo->Attr.u.Unix.fFlags        = 0;
            pObjInfo->Attr.u.Unix.GenerationId  = 0;
            pObjInfo->Attr.u.Unix.Device        = 0;
            break;
        case RTFSOBJATTRADD_UNIX_OWNER:
            pObjInfo->Attr.u.UnixOwner.uid       = 0;
            pObjInfo->Attr.u.UnixOwner.szName[0] = '\0';
            break;
        case RTFSOBJATTRADD_UNIX_GROUP:
            pObjInfo->Attr.u.UnixGroup.gid       = 0;
            pObjInfo->Attr.u.UnixGroup.szName[0] = '\0';
            break;
        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.u.EASize.cb = 0;
            break;
        default:
            AssertFailedBreak();
    }
}

/**
 * Helper methods for opening a stream.
 *
 * This is used internally w/o any associated RTVFSFILE handle for reading
 * stream \#1 containing PDB metadata.
 */
static void rtFsPdbPopulateFileObj(PRTFSPDBFILEOBJ pNewFile, PRTFSPDBVOL pPdb, uint32_t idxStream,
                                   uint32_t cbStream, void const *pvPageMap)
{
    pNewFile->pPdb          = pPdb;
    pNewFile->idxStream     = idxStream;
    pNewFile->offSubstream  = 0;
    pNewFile->cbStream      = cbStream;
    pNewFile->PageMap.pv    = pvPageMap;
    pNewFile->cPages        = RTPdbSizeToPages(cbStream, pPdb->cbPage);
    pNewFile->offFile       = 0;
}


/**
 * Helper methods for opening a stream or substream.
 */
static void rtFsPdbPopulateFileObjFromInfo(PRTFSPDBFILEOBJ pNewFile, PRTFSPDBVOL pPdb,
                                           uint32_t idxStream, PCRTFSPDBSUBSTREAM pSubstream)
{
    PCRTFSPDBSTREAMINFO pInfo = pPdb->papStreamInfo[idxStream];
    rtFsPdbPopulateFileObj(pNewFile, pPdb, idxStream, pInfo->cbStream, pInfo->PageMap.pv);
    Assert(pInfo->cPages == pNewFile->cPages);
    if (pSubstream)
    {
        pNewFile->offSubstream = pSubstream->offSubstream;
        pNewFile->cbStream     = pSubstream->cbSubstream;
    }
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtFsPdbFile_Close(void *pvThis)
{
    PRTFSPDBFILEOBJ const pThis = (PRTFSPDBFILEOBJ)pvThis;
    LogFlow(("rtFsPdbFile_Close(%p/%p)\n", pThis, pThis->pPdb));
    pThis->pPdb = NULL;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsPdbFile_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTFSPDBFILEOBJ const pThis = (PRTFSPDBFILEOBJ)pvThis;
    rtFsPdbPopulateObjInfo(pThis->pPdb, pObjInfo, enmAddAttr, pThis->cbStream, pThis->idxStream, 0, false /*fIsDir*/);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtFsPdbFile_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTFSPDBFILEOBJ const pThis = (PRTFSPDBFILEOBJ)pvThis;
    PRTFSPDBVOL const     pPdb  = pThis->pPdb;
    AssertReturn(pSgBuf->cSegs == 1, VERR_INTERNAL_ERROR_3);
    AssertReturn(pPdb, VERR_INTERNAL_ERROR_2);
    RT_NOREF(fBlocking);

    /* Apply default offset and switch to unsigned offset variable. */
    uint64_t offFile;
    if (off == -1)
        offFile = pThis->offFile;
    else
    {
        AssertReturn(off >= 0, VERR_INTERNAL_ERROR_3);
        offFile = (uint64_t)off;
    }

    /*
     * Check for EOF and figure out how much to read.
     */
    if (offFile >= pThis->cbStream)
    {
        if (pcbRead)
        {
            *pcbRead = 0;
            return VINF_EOF;
        }
        return VERR_EOF;
    }

    int    rcRet    = VINF_SUCCESS;
    size_t cbToRead = RTSgBufCalcLengthLeft(pSgBuf);
    if (   cbToRead           > pThis->cbStream
        || offFile + cbToRead > pThis->cbStream)
    {
        if (!pcbRead)
            return VERR_EOF;
        cbToRead = (size_t)(pThis->cbStream - offFile);
        rcRet    = VINF_EOF;
    }

    /*
     * Do it page by page, buffer segment by buffer segment, whatever is smaller.
     */
    uint64_t const offStart = offFile;
    while (cbToRead > 0)
    {
        uint32_t       iPageMap     = (uint32_t)((offFile + pThis->offSubstream) / pPdb->cbPage);
        uint32_t const offInPage    = (uint32_t)((offFile + pThis->offSubstream) % pPdb->cbPage);
        size_t         cbLeftInPage = pPdb->cbPage - offInPage;
        if (cbLeftInPage > cbToRead)
            cbLeftInPage = cbToRead;
        void * const   pvDst = RTSgBufGetCurrentSegment(pSgBuf, cbLeftInPage, &cbLeftInPage);
        AssertReturn(pvDst, VERR_INTERNAL_ERROR_4);

        uint64_t const offPageInFile = (pPdb->enmVersion == RTFSPDBVER_2
                                        ? pThis->PageMap.pa20[iPageMap] : pThis->PageMap.pa70[iPageMap])
                                     * (uint64_t)pPdb->cbPage;
        int rcRead = RTVfsFileReadAt(pPdb->hVfsBacking, offPageInFile + offInPage, pvDst, cbLeftInPage, NULL /*pcbRead*/);
        if (RT_SUCCESS(rcRead))
        {
            size_t cbAssert = RTSgBufAdvance(pSgBuf, cbLeftInPage); Assert(cbAssert == cbLeftInPage); RT_NOREF(cbAssert);
            offFile        += cbLeftInPage;
            cbToRead       -= cbLeftInPage;
        }
        /* If we can return the number of bytes we've read, we'll advance the
           file offset.  Otherwise we won't and return immediately. */
        else if (!pcbRead)
            return rcRead;
        else
        {
            rcRet = rcRead != VERR_EOF ? rcRead : VERR_READ_ERROR;
            break;
        }
    }

    /*
     * Update the file position and stuff.
     */
    pThis->offFile = offFile;
    if (pcbRead)
        *pcbRead   = offFile - offStart;
    return rcRet;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtFsPdbFile_Flush(void *pvThis)
{
    RT_NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtFsPdbFile_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTFSPDBFILEOBJ const pThis = (PRTFSPDBFILEOBJ)pvThis;
    *poffActual = pThis->offFile;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtFsPdbFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTFSPDBFILEOBJ const pThis = (PRTFSPDBFILEOBJ)pvThis;
    RTFOFF offNew;
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            offNew = offSeek;
            break;
        case RTFILE_SEEK_END:
            offNew = (RTFOFF)pThis->cbStream + offSeek;
            break;
        case RTFILE_SEEK_CURRENT:
            offNew = (RTFOFF)pThis->offFile + offSeek;
            break;
        default:
            return VERR_INVALID_PARAMETER;
    }
    if (offNew >= 0)
    {
        pThis->offFile = offNew;
        *poffActual    = offNew;
        return VINF_SUCCESS;
    }
    return VERR_NEGATIVE_SEEK;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) rtFsPdbFile_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTFSPDBFILEOBJ const pThis = (PRTFSPDBFILEOBJ)pvThis;
    *pcbFile = pThis->cbStream;
    return VINF_SUCCESS;
}


/**
 * PDB FS file operations.
 */
DECL_HIDDEN_CONST(const RTVFSFILEOPS) g_rtFsPdbFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "PDB File",
            rtFsPdbFile_Close,
            rtFsPdbFile_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        rtFsPdbFile_Read,
        NULL /*pfnWrite*/,
        rtFsPdbFile_Flush,
        NULL /*pfnPollOne*/,
        rtFsPdbFile_Tell,
        NULL /*pfnSkip*/,
        NULL /*pfnZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        NULL /*SetMode*/,
        NULL /*SetTimes*/,
        NULL /*SetOwner*/,
        RTVFSOBJSETOPS_VERSION
    },
    rtFsPdbFile_Seek,
    rtFsPdbFile_QuerySize,
    NULL /*SetSize*/,
    NULL /*QueryMaxSize*/,
    RTVFSFILEOPS_VERSION
};



/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtFsPdbDir_Close(void *pvThis)
{
    PRTFSPDBDIROBJ const pThis = (PRTFSPDBDIROBJ)pvThis;
    LogFlow(("rtFsPdbDir_Close(%p/%p)\n", pThis, pThis->pPdb));
    pThis->pPdb = NULL;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsPdbDir_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTFSPDBDIROBJ const pThis = (PRTFSPDBDIROBJ)pvThis;
    PRTFSPDBVOL    const pPdb  = pThis->pPdb;
    rtFsPdbPopulateObjInfo(pPdb, pObjInfo, enmAddAttr, pPdb->cbRoot, 0 /* root dir is stream zero */,
                           0 /*idxSubstream*/, true /*fIsDir*/);
    return VINF_SUCCESS;
}


/**
 * Helper for looping up a substream name.
 */
DECLINLINE(PCRTFSPDBSUBSTREAM) rtFsPdbDirLookupSubstream(PRTFSPDBVOL pPdb, uint32_t idxStream, const char *pszName)
{
    uint8_t const idxFirst = idxStream != UINT32_MAX ? pPdb->papStreamInfo[idxStream]->idxFirstSubstream : 0;
    uint8_t       cLeft    = idxStream != UINT32_MAX ? pPdb->papStreamInfo[idxStream]->cSubstreams       : pPdb->cSubstreams;
    while (cLeft-- > 0)
    {
        PCRTFSPDBSUBSTREAM pSubstream = &pPdb->aSubstreams[idxFirst + cLeft];
        if (strcmp(pSubstream->pszName, pszName) == 0)
            return pSubstream;
    }
    return NULL;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpen}
 */
static DECLCALLBACK(int) rtFsPdbDir_Open(void *pvThis, const char *pszEntry, uint64_t fOpen,
                                         uint32_t fFlags, PRTVFSOBJ phVfsObj)
{
    PRTFSPDBDIROBJ  const pThis = (PRTFSPDBDIROBJ)pvThis;
    PRTFSPDBVOL     const pPdb  = pThis->pPdb;
    int                   rc;

    /*
     * We cannot create or replace anything, just open stuff.
     */
    if (   (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN
        || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN_CREATE)
    { /* likely */ }
    else
        return VERR_WRITE_PROTECT;

    /*
     * Special cases '.' and '..'
     */
    if (   pszEntry[0] == '.'
        && (   pszEntry[1] == '\0'
            || (pszEntry[1] == '.' && pszEntry[2] == '\0')))
    {
        if (fFlags & RTVFSOBJ_F_OPEN_DIRECTORY)
        {
            RTVFSDIR hVfsDir;
            rc = rtFsPdbVol_OpenRoot(pPdb, &hVfsDir);
            if (RT_SUCCESS(rc))
            {
                *phVfsObj = RTVfsObjFromDir(hVfsDir);
                RTVfsDirRelease(hVfsDir);
                AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
            }
        }
        else
            rc = VERR_IS_A_DIRECTORY;
        return rc;
    }

    /*
     * The given filename can be:
     *      - just the stream index: "1";
     *      - just the name provided it doesn't start with a digit: "pdb";
     *      - or the combination of the two: "1-pdb".
     */
    uint32_t           idxStream;
    PCRTFSPDBSUBSTREAM pSubstream = NULL;
    if (RT_C_IS_DIGIT(*pszEntry))
    {
        char *pszNext;
        rc = RTStrToUInt32Ex(pszEntry, &pszNext, 10, &idxStream);
        if (   (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
            || (*pszNext != '\0' && *pszNext != '-')
            || idxStream >= pPdb->cStreams)
        {
            Log2(("rtFsPdbDir_Open: RTStrToUInt32Ex(%s,) -> %Rrc\n", pszEntry, VERR_PATH_NOT_FOUND));
            return VERR_PATH_NOT_FOUND;
        }
        if (   *pszNext == '-'
            && RTStrCmp(pszNext + 1, pPdb->papStreamInfo[idxStream]->pszName) != 0)
        {
            pSubstream = rtFsPdbDirLookupSubstream(pPdb, idxStream, pszNext + 1);
            if (pSubstream)
                Assert(pSubstream->idxStream == idxStream);
            else
            {
                Log2(("rtFsPdbDir_Open: idxStream=%#x name mismatch '%s', expected '%s'\n",
                      idxStream, pszEntry, pPdb->papStreamInfo[idxStream]->pszName));
                return VERR_PATH_NOT_FOUND;
            }
        }
    }
    else
    {
        for (idxStream = 0; idxStream < pPdb->cStreams; idxStream++)
        {
            const char * const pszStreamName = pPdb->papStreamInfo[idxStream]->pszName;
            if (pszStreamName && strcmp(pszEntry, pszStreamName) == 0)
                break;
        }
        if (idxStream >= pPdb->cStreams)
        {
            pSubstream = rtFsPdbDirLookupSubstream(pPdb, UINT32_MAX, pszEntry);
            if (pSubstream)
                idxStream = pSubstream->idxStream;
            else
            {
                Log2(("rtFsPdbDir_Open: '%s' not found in name table\n", pszEntry));
                return VERR_PATH_NOT_FOUND;
            }
        }
    }

    /*
     * If opening a file, create a new file object and return it.
     */
    if (fFlags & RTVFSOBJ_F_OPEN_FILE)
    {
        RTVFSFILE       hVfsFile = NIL_RTVFSFILE;
        PRTFSPDBFILEOBJ pNewFile;
        rc = RTVfsNewFile(&g_rtFsPdbFileOps, sizeof(*pNewFile), fOpen, pPdb->hVfsSelf, NIL_RTVFSLOCK /*use volume lock*/,
                          &hVfsFile, (void **)&pNewFile);
        if (RT_SUCCESS(rc))
        {
            rtFsPdbPopulateFileObjFromInfo(pNewFile, pPdb, idxStream, pSubstream);

            /* Convert it to a file object. */
            *phVfsObj = RTVfsObjFromFile(hVfsFile);
            RTVfsFileRelease(hVfsFile);
            AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);

            LogFlow(("rtFsPdbDir_Open: idxStream=%#x cbStream=%#RX64\n", idxStream, pNewFile->cbStream));
        }
    }
    else
        rc = VERR_IS_A_FILE;
    return rc;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnRewindDir}
 */
static DECLCALLBACK(int) rtFsPdbDir_RewindDir(void *pvThis)
{
    PRTFSPDBDIROBJ pThis = (PRTFSPDBDIROBJ)pvThis;
    pThis->u.idxNext = 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnReadDir}
 */
static DECLCALLBACK(int) rtFsPdbDir_ReadDir(void *pvThis, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry,
                                            RTFSOBJATTRADD enmAddAttr)
{
    PRTFSPDBDIROBJ const  pThis        = (PRTFSPDBDIROBJ)pvThis;
    PRTFSPDBVOL    const  pPdb         = pThis->pPdb;
    uint32_t const        idxStream    = pThis->u.s.idxNextStream;
    uint32_t const        idxSubstream = pThis->u.s.idxNextSubstream;
    if (idxStream < pPdb->cStreams)
    {
        PCRTFSPDBSTREAMINFO const pStreamInfo    = pPdb->papStreamInfo[idxStream];
        PCRTFSPDBSUBSTREAM const  pSubstreamInfo = !idxSubstream ? NULL
                                                 : &pPdb->aSubstreams[pStreamInfo->idxFirstSubstream + idxSubstream - 1];

        /*
         * Do names first as they may cause overflows.
         */
        char          szStreamNo[64];
        ssize_t const cchStreamNo = RTStrFormatU32(szStreamNo, sizeof(szStreamNo), idxStream, 10, 0, 0, 0);
        Assert(cchStreamNo > 0);

        /* Provide a more descriptive name if possible. */
        const char   *pszOtherName = !idxSubstream ? pStreamInfo->pszName : pSubstreamInfo->pszName;
        size_t const  cchOtherName = pszOtherName ? strlen(pszOtherName) : 0;

        /* Do the name stuff. */
        size_t const cchName  = cchOtherName ? cchStreamNo + 1 + cchOtherName : cchStreamNo;
        size_t const cbNeeded = RT_UOFFSETOF(RTDIRENTRYEX, szName) + cchName + 1;
        if (*pcbDirEntry < cbNeeded)
        {
            Log3(("rtFsPdbDir_ReadDir: VERR_BUFFER_OVERFLOW - cbDst=%zu cbNeeded=%zu\n", *pcbDirEntry, cbNeeded));
            *pcbDirEntry = cbNeeded;
            return VERR_BUFFER_OVERFLOW;
        }
        pDirEntry->cbName = (uint16_t)cchName;
        memcpy(pDirEntry->szName, szStreamNo, cchStreamNo);
        if (cchOtherName)
        {
            pDirEntry->szName[cchStreamNo] = '-';
            memcpy(&pDirEntry->szName[cchStreamNo + 1], pszOtherName, cchOtherName);
        }
        pDirEntry->szName[cchName] = '\0';

        if (cchOtherName && !idxSubstream)
        {
            Assert(cchStreamNo <= 8);
            pDirEntry->cwcShortName = cchStreamNo;
            szStreamNo[cchStreamNo] = '\0';
            int rc = RTUtf16CopyAscii(pDirEntry->wszShortName, RT_ELEMENTS(pDirEntry->wszShortName), szStreamNo);
            AssertRC(rc);
        }
        else
        {
            pDirEntry->cwcShortName    = 0;
            pDirEntry->wszShortName[0] = '\0';
        }

        /* Provide the other info. */
        rtFsPdbPopulateObjInfo(pPdb, &pDirEntry->Info, enmAddAttr,
                               !pSubstreamInfo ? pStreamInfo->cbStream : pSubstreamInfo->cbSubstream,
                               idxStream, idxSubstream, false /*fIsDir*/);

        /* Advance the directory location and return. */
        if (idxSubstream >= pStreamInfo->cSubstreams)
        {
            pThis->u.s.idxNextSubstream = 0;
            pThis->u.s.idxNextStream    = idxStream + 1;
        }
        else
            pThis->u.s.idxNextSubstream = idxSubstream + 1;

        return VINF_SUCCESS;
    }

    Log3(("rtFsPdbDir_ReadDir9660: idxNext=%#x: VERR_NO_MORE_FILES\n", pThis->u.idxNext));
    return VERR_NO_MORE_FILES;
}


/**
 * PDB (root) directory operations.
 */
static const RTVFSDIROPS g_rtFsPdbDirOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_DIR,
        "PDB Dir",
        rtFsPdbDir_Close,
        rtFsPdbDir_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSDIROPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSDIROPS, ObjSet) - RT_UOFFSETOF(RTVFSDIROPS, Obj),
        NULL /*SetMode*/,
        NULL /*SetTimes*/,
        NULL /*SetOwner*/,
        RTVFSOBJSETOPS_VERSION
    },
    rtFsPdbDir_Open,
    NULL /* pfnFollowAbsoluteSymlink */,
    NULL /* pfnOpenFile */,
    NULL /* pfnOpenDir */,
    NULL /* pfnCreateDir */,
    NULL /* pfnOpenSymlink */,
    NULL /* pfnCreateSymlink */,
    NULL /* pfnQueryEntryInfo */,
    NULL /* pfnUnlinkEntry */,
    NULL /* pfnRenameEntry */,
    rtFsPdbDir_RewindDir,
    rtFsPdbDir_ReadDir,
    RTVFSDIROPS_VERSION,
};


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnClose}
 */
static DECLCALLBACK(int) rtFsPdbVol_Close(void *pvThis)
{
    PRTFSPDBVOL pThis = (PRTFSPDBVOL)pvThis;
    Log(("rtFsPdbVol_Close(%p)\n", pThis));

    RTVfsFileRelease(pThis->hVfsBacking);
    pThis->hVfsBacking = NIL_RTVFSFILE;

    RTMemFree(pThis->pbRoot);
    pThis->pbRoot = NULL;

    RTMemFree(pThis->pszzNames);
    pThis->pszzNames = NULL;

    if (pThis->papStreamInfo)
    {
        uint32_t idxStream = pThis->cStreams;
        while (idxStream-- > 0)
            RTMemFree(pThis->papStreamInfo[idxStream]);
        RTMemFree(pThis->papStreamInfo);
        pThis->papStreamInfo = NULL;
    }
    pThis->cStreams = 0;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsPdbVol_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RT_NOREF(pvThis, pObjInfo, enmAddAttr);
    return VERR_WRONG_TYPE;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnQueryInfoEx}
 */
static DECLCALLBACK(int) rtFsPdbVol_QueryInfoEx(void *pvThis, RTVFSQIEX enmInfo, void *pvInfo, size_t cbInfo, size_t *pcbRet)
{
    PRTFSPDBVOL pThis = (PRTFSPDBVOL)pvThis;
    LogFlow(("rtFsPdbVol_QueryInfo(%p, %d,, %#zx,)\n", pThis, enmInfo, cbInfo));
    RT_NOREF(pThis, pvInfo, cbInfo, pcbRet);

    ssize_t cchRet;
    switch (enmInfo)
    {
        /* This is the same as the symbol chache subdir name: */
        case RTVFSQIEX_VOL_LABEL:
            if (   pThis->enmVersion == RTFSPDBVER_2
                || RTUuidIsNull(&pThis->Uuid))
                cchRet = RTStrPrintf2((char *)pvInfo, cbInfo, "%08X%x", pThis->uTimestamp, pThis->uAge);
            else
                cchRet = RTStrPrintf2((char *)pvInfo, cbInfo, "%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%x",
                                      pThis->Uuid.Gen.u32TimeLow,
                                      pThis->Uuid.Gen.u16TimeMid,
                                      pThis->Uuid.Gen.u16TimeHiAndVersion,
                                      pThis->Uuid.Gen.u8ClockSeqHiAndReserved,
                                      pThis->Uuid.Gen.u8ClockSeqLow,
                                      pThis->Uuid.Gen.au8Node[0],
                                      pThis->Uuid.Gen.au8Node[1],
                                      pThis->Uuid.Gen.au8Node[2],
                                      pThis->Uuid.Gen.au8Node[3],
                                      pThis->Uuid.Gen.au8Node[4],
                                      pThis->Uuid.Gen.au8Node[5],
                                      pThis->uAge);
            break;

        /* This exposes the PDB and VC versions: */
        case RTVFSQIEX_VOL_LABEL_ALT:
            cchRet = RTStrPrintf2((char *)pvInfo, cbInfo,
                                  "pdb-v%u-%u", pThis->enmVersion == RTFSPDBVER_2 ? 2 : 7, pThis->uVcDate);
            break;

        case RTVFSQIEX_VOL_SERIAL:
            if (cbInfo == sizeof(uint64_t) || cbInfo == sizeof(uint32_t))
            {
                *pcbRet = cbInfo;
                ((uint32_t *)pvInfo)[0] = pThis->uTimestamp;
                if (cbInfo == sizeof(uint64_t))
                    ((uint32_t *)pvInfo)[1] = pThis->uAge;
                return VINF_SUCCESS;
            }
            if (   pThis->enmVersion != RTFSPDBVER_2
                && !RTUuidIsNull(&pThis->Uuid))
            {
                *pcbRet = sizeof(RTUUID);
                if (cbInfo == sizeof(RTUUID))
                {
                    *(PRTUUID)pvInfo = pThis->Uuid;
                    return VINF_SUCCESS;
                }
            }
            else
                *pcbRet = sizeof(uint64_t);
            return cbInfo < *pcbRet ? VERR_BUFFER_OVERFLOW : VERR_BUFFER_UNDERFLOW;

        default:
            return VERR_NOT_SUPPORTED;
    }

    if (cchRet > 0)
    {
        *pcbRet = (size_t)cchRet;
        return VINF_SUCCESS;
    }
    *pcbRet = (size_t)-cchRet;
    return VERR_BUFFER_OVERFLOW;
}


/**
 * @interface_method_impl{RTVFSOPS,pfnOpenRoot}
 */
static DECLCALLBACK(int) rtFsPdbVol_OpenRoot(void *pvThis, PRTVFSDIR phVfsDir)
{
    PRTFSPDBVOL    pThis = (PRTFSPDBVOL)pvThis;
    PRTFSPDBDIROBJ pNewDir;
    int rc = RTVfsNewDir(&g_rtFsPdbDirOps, sizeof(*pNewDir), 0 /*fFlags*/, pThis->hVfsSelf,
                         NIL_RTVFSLOCK /*use volume lock*/, phVfsDir, (void **)&pNewDir);
    if (RT_SUCCESS(rc))
    {
        pNewDir->pPdb      = pThis;
        pNewDir->u.idxNext = 0;
        return VINF_SUCCESS;
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSOPS,pfnQueryRangeState}
 */
static DECLCALLBACK(int) rtFsPdbVol_QueryRangeState(void *pvThis, uint64_t off, size_t cb, bool *pfUsed)
{
    RT_NOREF(pvThis, off, cb, pfUsed);
    return VERR_NOT_IMPLEMENTED;
}


DECL_HIDDEN_CONST(const RTVFSOPS) g_rtFsPdbVolOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_VFS,
        "PDB",
        rtFsPdbVol_Close,
        rtFsPdbVol_QueryInfo,
        rtFsPdbVol_QueryInfoEx,
        RTVFSOBJOPS_VERSION
    },
    RTVFSOPS_VERSION,
    0 /* fFeatures */,
    rtFsPdbVol_OpenRoot,
    rtFsPdbVol_QueryRangeState,
    RTVFSOPS_VERSION
};


/**
 * Internal helper for reading an entire stream into a heap block.
 *
 * Use RTMemTmpFree to free the returned memory when done.
 */
static int rtFsPdbVolReadStreamToHeap(PRTFSPDBVOL pThis, uint32_t idxStream,
                                      uint8_t **ppbStream, uint32_t *pcbStream, PRTERRINFO pErrInfo)
{
    *ppbStream = NULL;
    *pcbStream = 0;
    if (idxStream >= pThis->cStreams)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_NOT_FOUND, "idxStream=%#x not found, max %#x", idxStream, pThis->cStreams);

    /*
     * Fake a handle and reuse the file read code.
     */
    RTFSPDBFILEOBJ FileObj;
    rtFsPdbPopulateFileObjFromInfo(&FileObj, pThis, idxStream, NULL);

    size_t const    cbDst = (size_t)FileObj.cbStream + !FileObj.cbStream;
    uint8_t * const pbDst = (uint8_t *)RTMemTmpAllocZ(cbDst);
    if (!pbDst)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_NO_MEMORY,
                                   "Failed to allocate memory for reading stream #%x: %#zx bytes", idxStream, cbDst);

    RTSGSEG SgSeg = { pbDst, FileObj.cbStream };
    RTSGBUF SgBuf;
    RTSgBufInit(&SgBuf, &SgSeg, 1);

    int rc = rtFsPdbFile_Read(&FileObj, 0, &SgBuf, true /*fBlocking*/, NULL);
    if (RT_SUCCESS(rc))
    {
        *ppbStream = pbDst;
        *pcbStream = FileObj.cbStream;
    }
    else
        RTMemTmpFree(pbDst);
    return rc;
}


/**
 * Internal helper for reading from a stream.
 */
static int rtFsPdbVolReadStreamAt(PRTFSPDBVOL pThis, uint32_t idxStream, uint32_t offStream,
                                  void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RTFSPDBFILEOBJ FileObj;
    rtFsPdbPopulateFileObjFromInfo(&FileObj, pThis, idxStream, NULL);

    RTSGSEG SgSeg = { pvBuf, cbToRead };
    RTSGBUF SgBuf;
    RTSgBufInit(&SgBuf, &SgSeg, 1);

    return rtFsPdbFile_Read(&FileObj, offStream, &SgBuf, true /*fBlocking*/, pcbRead);
}


/**
 * Adds a substream.
 */
static void rtFsPdbVolAddSubstream(PRTFSPDBVOL pThis, uint32_t idxStream, uint8_t idxSubstream,
                                   uint32_t offSubstream, uint32_t cbSubstream, const char *pszName)
{
    /*
     * Sanity checking.
     */
    Assert(idxSubstream > 0); /* zero is used for the parent stream when enumerating directories. */

    AssertReturnVoid(idxStream < pThis->cStreams);
    PRTFSPDBSTREAMINFO const pStreamInfo = pThis->papStreamInfo[idxStream];
    Assert(RTStrStartsWith(pszName, pStreamInfo->pszName));
    AssertReturnVoid(offSubstream < pStreamInfo->cbStream);
    AssertStmt(cbSubstream <= pStreamInfo->cbStream - offSubstream, cbSubstream = pStreamInfo->cbStream - offSubstream);

    AssertReturnVoid(pThis->cSubstreams < RT_ELEMENTS(pThis->aSubstreams));
    PRTFSPDBSUBSTREAM const pSubstreamInfo = &pThis->aSubstreams[pThis->cSubstreams];
    Assert(   pThis->cSubstreams == 0
           || pSubstreamInfo[-1].idxStream < idxStream
           || (   pSubstreamInfo[-1].idxStream == idxStream
               && pSubstreamInfo[-1].idxSubstream < idxSubstream
               && pSubstreamInfo[-1].offSubstream <= offSubstream));

    /*
     * Add it.
     */
    pSubstreamInfo->idxStream    = idxStream;
    pSubstreamInfo->idxSubstream = idxSubstream;
    pSubstreamInfo->offSubstream = offSubstream;
    pSubstreamInfo->cbSubstream  = cbSubstream;
    pSubstreamInfo->pszName      = pszName;

    if (!pStreamInfo->cSubstreams)
        pStreamInfo->idxFirstSubstream = pThis->cSubstreams;
    pStreamInfo->cSubstreams++;

    pThis->cSubstreams++;
}


/**
 * Helper for rtFsPdbVolLoadStream3 to set standard stream names.
 */
static void rtFsPdbVolSetDefaultStreamName(PRTFSPDBVOL pThis, uint32_t idxStream, const char *pszDefaultName)
{
    if (idxStream < pThis->cStreams)
    {
        /* We override entries from the string table, as the standard stream
           names must be assumed to work when the streams are present! */
        if ((uintptr_t)pThis->papStreamInfo[idxStream]->pszName - (uintptr_t)pThis->pszzNames < pThis->cbNames)
            Log(("rtFsPdbVolSetDefaultStreamName: Overriding string table name for stream %#x: %s -> %s\n",
                 idxStream, pThis->papStreamInfo[idxStream]->pszName, pszDefaultName));
        pThis->papStreamInfo[idxStream]->pszName = pszDefaultName;
    }
}


/**
 * Worker for rtFsPdbVolTryInit that extracts info from the DBI stream.
 */
static int rtFsPdbVolLoadStream3(PRTFSPDBVOL pThis, PRTERRINFO pErrInfo)
{
    /* On the offchance that the DBI stream is missing... */
    if (3 >= pThis->cStreams)
        return VINF_SUCCESS;

    /*
     * Read the header.
     */
    union
    {
        RTPDBDBIHDR     New;
        RTPDBDBIHDROLD  Old;
        uint16_t        aidxStream[32];
    } Hdr;
    size_t cbRead = 0;
    int rc = rtFsPdbVolReadStreamAt(pThis, 3, 0, &Hdr, sizeof(Hdr.New), &cbRead);
    if (RT_FAILURE(rc))
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Error eeading the DBI header");

    if (Hdr.New.uSignature == RTPDBDBIHDR_SIGNATURE)
    {
        Log(("rtFsPdbVolLoadStream3: New DBI header\n"));
        if (cbRead != sizeof(Hdr.New))
            return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Bogus DBI header size: cbRead=%#zx, expected %#zx",
                                       cbRead, sizeof(Hdr.New));
        if (Hdr.New.uVcVersion < RTPDBDBIHDR_VCVER_50)
            return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Bogus DBI header version: %u (%#x), expected min %u",
                                       Hdr.New.uVcVersion, Hdr.New.uVcVersion, RTPDBDBIHDR_VCVER_50);
        if (Hdr.New.uVcVersion < RTPDBDBIHDR_VCVER_60)
            Hdr.New.cbEditContinueSubstream = 0;
        pThis->DbiHdr = Hdr.New;
    }
    else
    {
        /* Convert old header to new to new. */
        if (cbRead < sizeof(Hdr.Old))
            return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Bogus DBI header size: cbRead=%#zx, expected %#zx",
                                       cbRead, sizeof(Hdr.Old));
        Log(("rtFsPdbVolLoadStream3: Old DBI header\n"));
        RT_ZERO(pThis->DbiHdr);
        pThis->DbiHdr.idxMFC                    = UINT16_MAX;
        pThis->DbiHdr.idxGlobalStream           = Hdr.Old.idxGlobalStream;
        pThis->DbiHdr.idxPublicStream           = Hdr.Old.idxPublicStream;
        pThis->DbiHdr.idxSymRecStream           = Hdr.Old.idxSymRecStream;
        pThis->DbiHdr.cbModInfoSubstream        = Hdr.Old.cbModInfoSubstream;
        pThis->DbiHdr.cbSectContribSubstream    = Hdr.Old.cbSectContribSubstream;
        pThis->DbiHdr.cbSrcInfoSubstream        = Hdr.Old.cbSrcInfoSubstream;
        pThis->DbiHdr.uAge                      = pThis->uAge;
    }

    Log2(("  DBI Hdr uSignature               = %#x\n", pThis->DbiHdr.uSignature));
    Log2(("  DBI Hdr uVcVersion               = %#x\n", pThis->DbiHdr.uVcVersion));
    if (pThis->DbiHdr.uAge == pThis->uAge)
        Log2(("  DBI Hdr uAge                     = %#x (same as PDB header)\n", pThis->DbiHdr.uAge));
    else
        Log2(("  DBI Hdr uAge                     = %#x - differs from DB.uAge %#x!\n", pThis->DbiHdr.uAge, pThis->uAge));
    Log2(("  DBI Hdr idxGlobalStream          = %#x\n", pThis->DbiHdr.idxGlobalStream));
    if (pThis->DbiHdr.PdbDllVer.New.fNewVerFmt)
    {
        Log2(("  DBI Hdr PdbDllVer.u16            = %#x - %u.%u (new)\n", pThis->DbiHdr.PdbDllVer.u16,
              pThis->DbiHdr.PdbDllVer.New.uMajor, pThis->DbiHdr.PdbDllVer.New.uMinor));
        Log2(("  DBI Hdr uPdbDllBuild             = %u (%#x)\n", pThis->DbiHdr.uPdbDllBuild, pThis->DbiHdr.uPdbDllBuild));
        Log2(("  DBI Hdr uPdbDllRBuild            = %u (%#x)\n", pThis->DbiHdr.uPdbDllRBuild, pThis->DbiHdr.uPdbDllRBuild));
    }
    else
    {
        Log2(("  DBI Hdr PdbDllVer                = %#x - %u.%u.%u (old)\n", pThis->DbiHdr.PdbDllVer.u16,
              pThis->DbiHdr.PdbDllVer.Old.uMajor, pThis->DbiHdr.PdbDllVer.Old.uMinor,
              pThis->DbiHdr.PdbDllVer.Old.uRBuild));
        if (pThis->DbiHdr.uPdbDllBuild)
            Log2(("  DBI Hdr uPdbDllBuild             = %u (%#x)!!\n", pThis->DbiHdr.uPdbDllBuild, pThis->DbiHdr.uPdbDllBuild));
        if (pThis->DbiHdr.uPdbDllRBuild)
            Log2(("  DBI Hdr uPdbDllRBuild            = %u (%#x)!!\n", pThis->DbiHdr.uPdbDllRBuild, pThis->DbiHdr.uPdbDllRBuild));
    }
    Log2(("  DBI Hdr idxPublicStream          = %#x\n", pThis->DbiHdr.idxPublicStream));
    Log2(("  DBI Hdr idxSymRecStream          = %#x\n", pThis->DbiHdr.idxSymRecStream));
    Log2(("  DBI Hdr cbModInfoSubstream       = %#x\n", pThis->DbiHdr.cbModInfoSubstream));
    Log2(("  DBI Hdr cbSectContribSubstream   = %#x\n", pThis->DbiHdr.cbSectContribSubstream));
    Log2(("  DBI Hdr cbSectionMapSubstream    = %#x\n", pThis->DbiHdr.cbSectionMapSubstream));
    Log2(("  DBI Hdr cbSrcInfoSubstream       = %#x\n", pThis->DbiHdr.cbSrcInfoSubstream));
    Log2(("  DBI Hdr cbTypeServerMapSubstream = %#x\n", pThis->DbiHdr.cbTypeServerMapSubstream));
    Log2(("  DBI Hdr idxMFC                   = %#x\n", pThis->DbiHdr.idxMFC));
    Log2(("  DBI Hdr cbOptDbgHdr              = %#x\n", pThis->DbiHdr.cbOptDbgHdr));
    Log2(("  DBI Hdr cbEditContinueSubstream  = %#x\n", pThis->DbiHdr.cbEditContinueSubstream));
    Log2(("  DBI Hdr fFlags                   = %#x%s%s%s%s%s\n", pThis->DbiHdr.fFlags, pThis->DbiHdr.fFlags ? " -" : "",
          pThis->DbiHdr.fFlags & RTPDBDBIHDR_F_INCREMENTAL_LINK      ? " IncrmentalLink" : "",
          pThis->DbiHdr.fFlags & RTPDBDBIHDR_F_PRIVATE_SYMS_STRIPPED ? " PrivateSymsStripped" : "",
          pThis->DbiHdr.fFlags & RTPDBDBIHDR_F_CONFLICTING_TYPES     ? " ConflictingTypes" : "",
          pThis->DbiHdr.fFlags & RTPDBDBIHDR_F_RESERVED              ? " !ReservedFlagsSet!" : ""));
    Log2(("  DBI Hdr uMachine                 = %#x\n", pThis->DbiHdr.uMachine));
    Log2(("  DBI Hdr uReserved                = %#x\n", pThis->DbiHdr.uReserved));

    /*
     * Apply new standard stream names.
     */
    rtFsPdbVolSetDefaultStreamName(pThis, pThis->DbiHdr.idxGlobalStream, "global-symbol-hash");
    rtFsPdbVolSetDefaultStreamName(pThis, pThis->DbiHdr.idxPublicStream, "public-symbol-hash");
    rtFsPdbVolSetDefaultStreamName(pThis, pThis->DbiHdr.idxSymRecStream, "symbol-records");

    /*
     * Fill in substream info.
     */
    uint32_t offSubstream = sizeof(Hdr.New);
    if (pThis->DbiHdr.cbModInfoSubstream)
    {
        rtFsPdbVolAddSubstream(pThis, 3, 1, offSubstream, pThis->DbiHdr.cbModInfoSubstream, "dbi-module-info");
        offSubstream += pThis->DbiHdr.cbModInfoSubstream;
    }
    if (pThis->DbiHdr.cbSectContribSubstream)
    {
        rtFsPdbVolAddSubstream(pThis, 3, 2, offSubstream, pThis->DbiHdr.cbSectContribSubstream, "dbi-section-contributions");
        offSubstream += pThis->DbiHdr.cbSectContribSubstream;
    }
    if (pThis->DbiHdr.cbSectionMapSubstream)
    {
        rtFsPdbVolAddSubstream(pThis, 3, 3, offSubstream, pThis->DbiHdr.cbSectionMapSubstream, "dbi-section-map");
        offSubstream += pThis->DbiHdr.cbSectionMapSubstream;
    }
    if (pThis->DbiHdr.cbSrcInfoSubstream)
    {
        rtFsPdbVolAddSubstream(pThis, 3, 4, offSubstream, pThis->DbiHdr.cbSrcInfoSubstream, "dbi-source-info");
        offSubstream += pThis->DbiHdr.cbSrcInfoSubstream;
    }
    if (pThis->DbiHdr.cbTypeServerMapSubstream)
    {
        rtFsPdbVolAddSubstream(pThis, 3, 5, offSubstream, pThis->DbiHdr.cbTypeServerMapSubstream, "dbi-type-server-map");
        offSubstream += pThis->DbiHdr.cbTypeServerMapSubstream;
    }
    if (pThis->DbiHdr.cbEditContinueSubstream)
    {
        rtFsPdbVolAddSubstream(pThis, 3, 6, offSubstream, pThis->DbiHdr.cbEditContinueSubstream, "dbi-continue-and-edit");
        offSubstream += pThis->DbiHdr.cbEditContinueSubstream;
    }
    uint32_t const offOptDbgHdr = offSubstream;
    if (pThis->DbiHdr.cbOptDbgHdr)
    {
        rtFsPdbVolAddSubstream(pThis, 3, 7, offSubstream, pThis->DbiHdr.cbOptDbgHdr, "dbi-optional-header");
        offSubstream += pThis->DbiHdr.cbOptDbgHdr;
    }
    if (offSubstream < pThis->papStreamInfo[3]->cbStream)
        rtFsPdbVolAddSubstream(pThis, 3, 8, offSubstream, pThis->papStreamInfo[3]->cbStream - offSubstream, "dbi-unknown");

    /*
     * Read the optional header if present, it identifies a bunch of standard streams.
     */
    if (pThis->DbiHdr.cbOptDbgHdr > 1)
    {
        RT_ZERO(Hdr);
        rc = rtFsPdbVolReadStreamAt(pThis, 3, offOptDbgHdr, &Hdr, RT_MIN(sizeof(Hdr), pThis->DbiHdr.cbOptDbgHdr), &cbRead);
        if (RT_FAILURE(rc))
            return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Error eeading the DBI optional header at %#x LB %#x",
                                       offOptDbgHdr, pThis->DbiHdr.cbOptDbgHdr);
        static const char * const s_apszIdxNames[] =
        {
            /* [RTPDBDBIOPT_IDX_FPO_MASM] = */              "image-fpo-masm-section",
            /* [RTPDBDBIOPT_IDX_EXCEPTION] = */             "image-exception",
            /* [RTPDBDBIOPT_IDX_FIXUP] = */                 "image-fixup",
            /* [RTPDBDBIOPT_IDX_OMAP_TO_SRC] = */           "omap-to-src",
            /* [RTPDBDBIOPT_IDX_OMAP_FROM_SRC] = */         "omap-from-src",
            /* [RTPDBDBIOPT_IDX_SECTION_HEADERS] = */       "image-section-headers",
            /* [RTPDBDBIOPT_IDX_CLR_TOKEN_ID_MAP] = */      "clr-token-id-map",
            /* [RTPDBDBIOPT_IDX_XDATA] = */                 "image-xdata-section",
            /* [RTPDBDBIOPT_IDX_PDATA] = */                 "image-pdata-section",
            /* [RTPDBDBIOPT_IDX_FPO] = */                   "image-fpo",
            /* [RTPDBDBIOPT_IDX_ORG_SECTION_HEADERS] = */   "image-orginal-section-headers",
        };
        uint32_t idxName = pThis->DbiHdr.cbOptDbgHdr / 2;
        if (idxName > RT_ELEMENTS(s_apszIdxNames))
        {
#ifdef LOG_ENABLED
            while (idxName-- > RT_ELEMENTS(s_apszIdxNames))
                Log(("Unknown DBI optional header entry %u: %#x\n", idxName, Hdr.aidxStream[idxName]));
#endif
            idxName = RT_ELEMENTS(s_apszIdxNames);
        }
        while (idxName-- > 0)
            rtFsPdbVolSetDefaultStreamName(pThis, Hdr.aidxStream[idxName], s_apszIdxNames[idxName]);
    }

    return VINF_SUCCESS;
}


/**
 * Worker for rtFsPdbVolLoadStream1 that parses the PDB metadata stream.
 */
static int rtFsPdbVolLoadStream1Inner(PRTFSPDBVOL pThis, uint8_t const *pb, size_t cb, PRTERRINFO pErrInfo)
{
    /*
     * Process the header part.
     */
    if (pThis->enmVersion == RTFSPDBVER_2)
    {
        PCRTPDB20NAMES pHdr = (PCRTPDB20NAMES)pb;
        if (cb < sizeof(*pHdr))
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_NO_MEMORY,
                                       "Stream #1 is smaller than expected: %#x, vs min %#zx", cb, sizeof(*pHdr));
        pThis->uTimestamp = pHdr->uTimestamp;
        pThis->uAge       = pHdr->uAge;
        pThis->uVcDate    = pHdr->uVersion;
        pThis->cbNames    = pHdr->cbNames;
        pb += sizeof(*pHdr);
        cb -= sizeof(*pHdr);
    }
    else
    {
        PCRTPDB70NAMES pHdr = (PCRTPDB70NAMES)pb;
        if (cb < sizeof(*pHdr))
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_NO_MEMORY,
                                       "Stream #1 is smaller than expected: %#x, vs min %#zx", cb, sizeof(*pHdr));
        pThis->uTimestamp = pHdr->uTimestamp;
        pThis->uAge       = pHdr->uAge;
        pThis->uVcDate    = pHdr->uVersion;
        pThis->Uuid       = pHdr->Uuid;
        pThis->cbNames    = pHdr->cbNames;
        pb += sizeof(*pHdr);
        cb -= sizeof(*pHdr);
    }

    /*
     * Set default stream names that depends on the VC date.
     */
    /** @todo */

    /*
     * Load the string table if present.
     */
    if (pThis->cbNames == 0)
        return VINF_SUCCESS;
    if (cb < pThis->cbNames)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "Bogus string table: size given as %#x, but only %#x bytes left", pThis->cbNames, cb);
    pThis->pszzNames = (char *)RTMemDupEx(pb, pThis->cbNames, 2 /* two extra zero bytes */);
    if (!pThis->pszzNames)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_NO_MEMORY, "Failed to allocate string table: %#x + 2 bytes", pThis->cbNames);

    pb += pThis->cbNames;
    cb -= pThis->cbNames;

    /** @todo MIPS format variation may have alignment padding here.   */

    /*
     * What follows now is the hash table mapping string table offset to stream
     * numbers.  This is frequently misaligned, so we take care to load the
     * stuff byte-by-byte on architectures which are sensive to such things.
     *
     * Structure description: https://llvm.org/docs/PDB/HashTable.html
     */
    if (cb < 4 * sizeof(uint32_t))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "Bogus hash table: Min size of 16 bytes, but only %#x bytes left", cb);
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
# define GET_U32(a_pb, a_off)  *(uint32_t const *)&a_pb[(a_off)]
#else
# define GET_U32(a_pb, a_off)  RT_MAKE_U32_FROM_U8(a_pb[0 + (a_off)], a_pb[1 + (a_off)], a_pb[2 + (a_off)], a_pb[3 + (a_off)])
#endif

    /* Sizes (irrelevant to us as we're not reconstructing the actual hash table yet): */
    uint32_t const cTabSize     = GET_U32(pb, 0);
    uint32_t const cTabCapacity = GET_U32(pb, 4);
    if (cTabSize > cTabCapacity)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "Bogus hash table: cTabSize=%#x > cTabCapacity=%#x", cTabSize, cTabCapacity);
    if (cTabSize == 0)
        return VINF_SUCCESS;
    pb += 8;
    cb -= 8;

    /* Present bit vector: */
    uint32_t const cPresentVec = RT_MAKE_U32_FROM_U8(pb[0], pb[1], pb[2], pb[3]);
    pb += 4;
    cb -= 4;
    if ((uint64_t)cPresentVec + 1 > cb / 4)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "Bogus hash table: cPresentVec=%#x + 1 delete vec > %#x", cPresentVec, cb / 4);
    uint8_t const * const pbPresentBits = pb;
    pb += cPresentVec * 4;
    cb -= cPresentVec * 4;

    /* Scan the present vector as it gives the number of key/value pairs following the deleted bit vector. */
    uint64_t cPresent = 0;
    for (uint32_t off = 0; off < cPresentVec; off += 4)
    {
        uint32_t uWord = GET_U32(pbPresentBits, off);
        while (uWord)
        {
            cPresent += uWord & 1;
            uWord >>= 1;
        }
    }

    /* Deleted vector (irrelevant to us): */
    uint32_t const cDeletedVec = RT_MAKE_U32_FROM_U8(pb[0], pb[1], pb[2], pb[3]);
    pb += 4;
    cb -= 4;
    if ((uint64_t)cDeletedVec + cPresent > cb / 4)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "Bogus hash table: cDeletedVec=%#x cPresent=%#RX64 > %#x", cDeletedVec, cPresent, cb / 4);
    pb += cDeletedVec * 4;
    cb -= cDeletedVec * 4;

    /* What remains is cPresent pairs of string table offset and stream IDs. */
    Assert(cb / 4 >= cPresent);
    for (uint32_t i = 0, off = 0; i < cPresent; i++, off += 8)
    {
        uint32_t const offString = GET_U32(pb, off);
        uint32_t const idxStream = GET_U32(pb, off + 4);
        if (offString < pThis->cbNames)
        {
            if (idxStream < pThis->cStreams)
            {
                /* Skip leading slashes.  In-string slashes are removed afterwards. */
                const char *pszName = &pThis->pszzNames[offString];
                while (*pszName == '/' || *pszName == '\\')
                    pszName++;
                if (pszName[0])
                    pThis->papStreamInfo[idxStream]->pszName = pszName;
            }
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                           "Bogus hash table entry %#x: offString=%#x, max %#x (offString=%#x '%s')",
                                           i, idxStream, pThis->cStreams, offString, &pThis->pszzNames[offString]);
        }
        else
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                       "Bogus hash table entry %#x: offString=%#x, max %#x (idxStream=%#x)",
                                       i, offString, pThis->cbNames, idxStream);
    }

    /*
     * Sanitize strings and convert any in-string slashes to underscores to
     * avoid VFS confusion.  This has to be done after loading the hash table
     * so the slash skipping there works correctly should anyone do sub-string
     * optimizations involving slashes.
     */
    for (uint32_t idxStream = 0; idxStream < pThis->cStreams; idxStream++)
    {
        char *pszName = (char *)pThis->papStreamInfo[idxStream]->pszName;
        if (pszName)
        {
            RTStrPurgeEncoding(pszName);
            pszName = strpbrk(pszName, "/\\");
            while (pszName)
            {
                *pszName = '_';
                pszName = strpbrk(pszName + 1, "/\\");
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Worker for rtFsPdbVolTryInit that loads the PDB metadata from stream \#1.
 */
static int rtFsPdbVolLoadStream1(PRTFSPDBVOL pThis, PRTERRINFO pErrInfo)
{
    /*
     * Assign default stream names based on basic PDB version.
     */
    switch (pThis->cStreams)
    {
        default:
        case 5:
            if (pThis->enmVersion == RTFSPDBVER_7) /** @todo condition? */
                pThis->papStreamInfo[4]->pszName = "name-map";
            RT_FALL_THRU();
        case 4:
            pThis->papStreamInfo[3]->pszName = "dbi";
            RT_FALL_THRU();
        case 3:
            pThis->papStreamInfo[2]->pszName = "tpi";
            RT_FALL_THRU();
        case 2:
            pThis->papStreamInfo[1]->pszName = "pdb";
            RT_FALL_THRU();
        case 1:
            pThis->papStreamInfo[0]->pszName = "root";
            break;
    }
    if (pThis->cStreams >= 2)
    {
        /*
         * Read the stream into temporary heap memory and process it.
         */
        uint8_t *pbStream = NULL;
        uint32_t cbStream = 0;
        int rc = rtFsPdbVolReadStreamToHeap(pThis, 1, &pbStream, &cbStream, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            rc = rtFsPdbVolLoadStream1Inner(pThis, pbStream, cbStream, pErrInfo);
            RTMemTmpFree(pbStream);
        }
        return rc;
    }
    return VINF_SUCCESS;
}


/**
 * Helper for rtFsPdbVolTryInit.
 */
static int rtFsPdbVolAllocInitialStreamInfo(PRTFSPDBVOL pThis, PRTERRINFO pErrInfo)
{
    pThis->papStreamInfo = (PRTFSPDBSTREAMINFO *)RTMemAllocZ(sizeof(pThis->papStreamInfo[0]) * pThis->cStreams);
    if (pThis->papStreamInfo)
    {
        for (uint32_t idxStream = 0; idxStream < pThis->cStreams; idxStream++)
        {
            pThis->papStreamInfo[idxStream] = (PRTFSPDBSTREAMINFO)RTMemAllocZ(sizeof(*pThis->papStreamInfo[0]));
            if (pThis->papStreamInfo[idxStream])
            { }
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_NO_MEMORY, "Failed to allocate RTFSPDBSTREAMINFO #%u", idxStream);
        }
        return VINF_SUCCESS;
    }
    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_NO_MEMORY,
                               "Failed to allocate papStreamInfo array with %u entries", pThis->cStreams);
}


/**
 * Worker for RTFsPdbVolOpen.
 *
 * @returns IPRT status code.
 * @param   pThis           The PDB VFS instance to initialize.
 * @param   hVfsSelf        The PDB VFS handle (no reference consumed).
 * @param   hVfsBacking     The file backing the alleged PDB file system.
 *                          Reference is consumed (via rtFsPdbVol_Close).
 * @param   fFlags          Flags, RTFSPDB_F_XXX.
 * @param   pErrInfo        Where to return additional error info.  Can be NULL.
 */
static int rtFsPdbVolTryInit(PRTFSPDBVOL pThis, RTVFS hVfsSelf, RTVFSFILE hVfsBacking, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    /*
     * First initialize the state so that rtFsPdbVol_Close won't trip up.
     */
    pThis->hVfsSelf         = hVfsSelf;
    pThis->hVfsBacking      = hVfsBacking; /* Caller referenced it for us, we consume it; rtFsPdbVol_Close releases it. */
    pThis->cbBacking        = 0;
    pThis->cBackingPages    = 0;
    pThis->fFlags           = fFlags;
    pThis->cbPage           = 0;
    pThis->enmVersion       = RTFSPDBVER_INVALID;
    pThis->uAge             = 0;
    pThis->uTimestamp       = 0;
    RTUuidClear(&pThis->Uuid);
    pThis->cbNames          = 0;
    pThis->pszzNames        = NULL;
    pThis->papStreamInfo    = NULL;
    pThis->cSubstreams      = 0;

    /*
     * Do init stuff that may fail.
     */
    int rc = RTVfsFileQuerySize(hVfsBacking, &pThis->cbBacking);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Read, validate and load the file header.
     */
    union
    {
        RTPDB70HDR  Hdr70;
        RTPDB20HDR  Hdr20;
    } Buf;
    RT_ZERO(Buf);

    rc = RTVfsFileReadAt(hVfsBacking, 0, &Buf, sizeof(Buf), NULL);
    if (RT_FAILURE(rc))
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Unable to file header");

    uint32_t cPages;
    uint32_t cbMinRootStream;
    uint64_t offRootPageMap;
    if (memcmp(Buf.Hdr70.szSignature, RTPDB_SIGNATURE_700, sizeof(Buf.Hdr70.szSignature)) == 0)
    {
        pThis->enmVersion = RTFSPDBVER_7;
        pThis->cbPage     = Buf.Hdr70.cbPage;
        pThis->cbRoot     = Buf.Hdr70.cbRoot;
        cPages            = Buf.Hdr70.cPages;
        cbMinRootStream   = RT_UOFFSETOF(RTPDB70ROOT, aStreams[4]) + sizeof(RTPDB70PAGE) * 4;
        offRootPageMap    = Buf.Hdr70.iRootPages * pThis->cbPage;
    }
    else if (memcmp(Buf.Hdr20.szSignature, RTPDB_SIGNATURE_200, sizeof(Buf.Hdr20.szSignature)) == 0)
    {
        pThis->enmVersion = RTFSPDBVER_2;
        pThis->cbPage     = Buf.Hdr20.cbPage;
        pThis->cbRoot     = Buf.Hdr20.RootStream.cbStream;
        cPages            = Buf.Hdr20.cPages;
        cbMinRootStream   = RT_UOFFSETOF(RTPDB20ROOT, aStreams[4]) + sizeof(RTPDB20PAGE) * 4; /** @todo ?? */
        offRootPageMap    = RT_UOFFSETOF(RTPDB20HDR, aiRootPageMap);
    }
    else
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_INVALID_MAGIC, "Unknown file header signature: %.44Rhxs", Buf.Hdr70.szSignature);

    if (   pThis->cbPage != _4K
        && pThis->cbPage != _8K
        && pThis->cbPage != _16K
        && pThis->cbPage != _32K
        && pThis->cbPage != _64K
        && pThis->cbPage != _2K
        && pThis->cbPage != _1K
        && pThis->cbPage != 512)
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Unsupported page size: %#x", pThis->cbPage);
    pThis->cBackingPages = pThis->cbBacking / pThis->cbPage;
    if (cPages != pThis->cBackingPages)
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Unexpected page count: %#x, expected %#llx (page size %#x)",
                                   cPages, pThis->cBackingPages, pThis->cbPage);

    if (offRootPageMap > pThis->cbBacking - pThis->cbPage)
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Bogus root page map start: %#llx (file size %#llx)",
                                   offRootPageMap, pThis->cbBacking);
    if (pThis->cbRoot < cbMinRootStream)
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Root stream is smaller than expected: %#x, expected at least %#x",
                                   pThis->cbRoot, cbMinRootStream);
    if (pThis->cbRoot > _64M)
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Root stream is too large: %#x, max supported %#x",
                                   pThis->cbRoot, _64M);
    if (pThis->cbRoot > pThis->cbBacking)
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Root stream is too large: %#x, file size %#llx",
                                   pThis->cbRoot, pThis->cbBacking);

    /*
     * Load the root stream into memory.
     */
    uint32_t const cRootPages = (pThis->cbRoot + pThis->cbPage - 1) / pThis->cbPage;
    pThis->pbRoot = (uint8_t *)RTMemAllocZ(RT_ALIGN_32(pThis->cbRoot, 64));
    if (!pThis->pbRoot)
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Failed to allocate root stream backing: %#x bytes",
                                   RT_ALIGN_32(pThis->cbRoot, 64));
    /* Get the root page map. */
    size_t const cbPageMap = cRootPages * (pThis->enmVersion == RTFSPDBVER_2 ? sizeof(RTPDB20PAGE) : sizeof(RTPDB70PAGE));
    void * const pvPageMap = RTMemTmpAlloc(cbPageMap);
    if (!pvPageMap)
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Failed to allocate %#zx bytes for the root page map", cbPageMap);

    rc = RTVfsFileReadAt(hVfsBacking, offRootPageMap, pvPageMap, cbPageMap, NULL);
    if (RT_SUCCESS(rc))
    {
        /* Validate the page map. */
        for (uint32_t iPageMap = 0; iPageMap < cRootPages; iPageMap++)
        {
            RTPDB70PAGE const iPageNo = pThis->enmVersion == RTFSPDBVER_2
                                      ? ((PCRTPDB20PAGE)pvPageMap)[iPageMap] : ((PCRTPDB70PAGE)pvPageMap)[iPageMap];
            if (iPageNo > 0 && iPageNo < pThis->cBackingPages)
                continue;
            rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_OUT_OF_RANGE,
                                     "Root page map entry %#x is out of bounds: %#x (max %#llx)",
                                     iPageMap, iPageNo, pThis->cBackingPages);
            break;
        }

        /* Read using regular file reader. */
        RTFSPDBFILEOBJ FileObj;
        rtFsPdbPopulateFileObj(&FileObj, pThis, 0, pThis->cbRoot, pvPageMap);

        RTSGSEG SgSeg = { pThis->pbRoot, pThis->cbRoot };
        RTSGBUF SgBuf;
        RTSgBufInit(&SgBuf, &SgSeg, 1);

        rc = rtFsPdbFile_Read(&FileObj, 0, &SgBuf, true /*fBlocking*/, NULL);
    }
    RTMemTmpFree(pvPageMap);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Validate the root stream.
     */
    if (pThis->enmVersion == RTFSPDBVER_2)
    {
        PCRTPDB20ROOT const pRoot = (PCRTPDB20ROOT)pThis->pbRoot;

        /* Stream count. */
        if (   RT_UOFFSETOF_DYN(RTPDB20ROOT, aStreams[pRoot->cStreams]) <= pThis->cbRoot
            && pRoot->cStreams >= 1)
            pThis->cStreams = pRoot->cStreams;
        else
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_OUT_OF_RANGE,
                                       "Bogus root stream count: %#x (cbRoot=%#x)", pRoot->cStreams, pThis->cbRoot);

        rc = rtFsPdbVolAllocInitialStreamInfo(pThis, pErrInfo);
        if (RT_FAILURE(rc))
            return rc;

        /* Validate stream data and page map while populating the stream info table. */
        PCRTPDB20PAGE const pauPageMap  = (PCRTPDB20PAGE)&pRoot->aStreams[pThis->cStreams];
        uint32_t const      cPageMapMax = pThis->cbRoot - RT_UOFFSETOF_DYN(RTPDB20ROOT, aStreams[pThis->cStreams]);
        uint32_t            iPageMap    = 0;
        for (uint32_t idxStream = 0; idxStream < pThis->cStreams; idxStream++)
        {
            uint32_t cStreamPages = RTPdbSizeToPages(pRoot->aStreams[idxStream].cbStream, pThis->cbPage);
            if (iPageMap + cStreamPages <= cPageMapMax)
            {
                PRTFSPDBSTREAMINFO const pInfo = pThis->papStreamInfo[idxStream];
                pInfo->cbStream     = pRoot->aStreams[idxStream].cbStream;
                pInfo->cPages       = cStreamPages;
                pInfo->PageMap.pa20 = &pauPageMap[iPageMap];

                while (cStreamPages-- > 0)
                    if (pauPageMap[iPageMap] == 0 || pauPageMap[iPageMap] < pThis->cBackingPages)
                        iPageMap++;
                    else
                        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_OUT_OF_RANGE,
                                                   "Bogus page map entry %#x belonging to stream %#x: %#x (max %#llx)",
                                                   iPageMap, idxStream, pauPageMap[iPageMap], pThis->cBackingPages);
            }
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_OUT_OF_RANGE,
                                           "Stream %#x exceeds the page map space: cbStream=%#x iPageMap=%#x cPageMapMax=%#x",
                                           idxStream, pRoot->aStreams[idxStream].cbStream, iPageMap, cPageMapMax);
        }
    }
    else
    {
        PCRTPDB70ROOT const pRoot = (PCRTPDB70ROOT)pThis->pbRoot;

        /* Stream count. */
        if (   RT_UOFFSETOF_DYN(RTPDB70ROOT, aStreams[pRoot->cStreams]) <= pThis->cbRoot
            && pRoot->cStreams >= 1)
            pThis->cStreams = pRoot->cStreams;
        else
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_OUT_OF_RANGE,
                                       "Bogus root stream count: %#x (cbRoot=%#x)", pRoot->cStreams, pThis->cbRoot);

        rc = rtFsPdbVolAllocInitialStreamInfo(pThis, pErrInfo);
        if (RT_FAILURE(rc))
            return rc;

        /* Validate stream data and page map while populating the stream info table. */
        PCRTPDB70PAGE const pauPageMap  = (PCRTPDB70PAGE)&pRoot->aStreams[pThis->cStreams];
        uint32_t const      cPageMapMax = pThis->cbRoot - RT_UOFFSETOF_DYN(RTPDB70ROOT, aStreams[pThis->cStreams]);
        uint32_t            iPageMap    = 0;
        for (uint32_t idxStream = 0; idxStream < pThis->cStreams; idxStream++)
        {
            uint32_t cStreamPages = RTPdbSizeToPages(pRoot->aStreams[idxStream].cbStream, pThis->cbPage);
            if (iPageMap + cStreamPages <= cPageMapMax)
            {
                PRTFSPDBSTREAMINFO const pInfo = pThis->papStreamInfo[idxStream];
                pInfo->cbStream     = pRoot->aStreams[idxStream].cbStream;
                pInfo->cPages       = cStreamPages;
                pInfo->PageMap.pa70 = &pauPageMap[iPageMap];

                while (cStreamPages-- > 0)
                    if (pauPageMap[iPageMap] == 0 || pauPageMap[iPageMap] < pThis->cBackingPages)
                        iPageMap++;
                    else
                        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_OUT_OF_RANGE,
                                                   "Bogus page map entry %#x belonging to stream %#x: %#x (max %#llx)",
                                                   iPageMap, idxStream, pauPageMap[iPageMap], pThis->cBackingPages);
            }
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_OUT_OF_RANGE,
                                           "Stream %#x exceeds the page map space: cbStream=%#x iPageMap=%#x cPageMapMax=%#x",
                                           idxStream, pRoot->aStreams[idxStream].cbStream, iPageMap, cPageMapMax);
        }

    }

    /*
     * Load info from streams #1 (PDB & names) and #3 (DBI).
     */
    rc = rtFsPdbVolLoadStream1(pThis, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = rtFsPdbVolLoadStream3(pThis, pErrInfo);
    return rc;
}


RTDECL(int) RTFsPdbVolOpen(RTVFSFILE hVfsFileIn, uint32_t fFlags, PRTVFS phVfs, PRTERRINFO pErrInfo)
{
    /*
     * Quick input validation.
     */
    AssertPtrReturn(phVfs, VERR_INVALID_POINTER);
    *phVfs = NIL_RTVFS;
    AssertReturn(!(fFlags & ~RTFSPDB_F_NO_NAMES), VERR_INVALID_FLAGS);

    uint32_t cRefs = RTVfsFileRetain(hVfsFileIn);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create a new PDB VFS instance and try initialize it using the given input file.
     */
    RTVFS       hVfs   = NIL_RTVFS;
    PRTFSPDBVOL pThis = NULL;
    int rc = RTVfsNew(&g_rtFsPdbVolOps, sizeof(RTFSPDBVOL), NIL_RTVFS, RTVFSLOCK_CREATE_RW, &hVfs, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        rc = rtFsPdbVolTryInit(pThis, hVfs, hVfsFileIn, fFlags, pErrInfo);
        if (RT_SUCCESS(rc))
            *phVfs = hVfs;
        else
            RTVfsRelease(hVfs);
    }
    else
        RTVfsFileRelease(hVfsFileIn);
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainPdbFsVol_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
                                                     PRTVFSCHAINELEMSPEC pElement, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec);

    /*
     * Basic checks.
     */
    if (pElement->enmTypeIn != RTVFSOBJTYPE_FILE)
        return pElement->enmTypeIn == RTVFSOBJTYPE_INVALID ? VERR_VFS_CHAIN_CANNOT_BE_FIRST_ELEMENT : VERR_VFS_CHAIN_TAKES_FILE;
    if (   pElement->enmType != RTVFSOBJTYPE_VFS
        && pElement->enmType != RTVFSOBJTYPE_DIR)
        return VERR_VFS_CHAIN_ONLY_DIR_OR_VFS;
    if (pElement->cArgs > 1)
        return VERR_VFS_CHAIN_AT_MOST_ONE_ARG;

    /*
     * Parse the flag if present, save in pElement->uProvider.
     */
    uint32_t fFlags = 0;
    if (pElement->cArgs > 0)
    {
        for (uint32_t iArg = 0; iArg < pElement->cArgs; iArg++)
        {
            const char *psz = pElement->paArgs[iArg].psz;
            if (*psz)
            {
                if (!strcmp(psz, "nonames"))
                    fFlags |= RTFSPDB_F_NO_NAMES;
                else
                {
                    *poffError = pElement->paArgs[iArg].offSpec;
                    return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_CHAIN_INVALID_ARGUMENT, "Only knows: 'nojoliet' and 'norock'");
                }
            }
        }
    }

    pElement->uProvider = fFlags;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnInstantiate}
 */
static DECLCALLBACK(int) rtVfsChainPdbFsVol_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                        PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                        PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, poffError);

    int         rc;
    RTVFSFILE   hVfsFileIn = RTVfsObjToFile(hPrevVfsObj);
    if (hVfsFileIn != NIL_RTVFSFILE)
    {
        RTVFS hVfs;
        rc = RTFsPdbVolOpen(hVfsFileIn, pElement->uProvider, &hVfs, pErrInfo);
        RTVfsFileRelease(hVfsFileIn);
        if (RT_SUCCESS(rc))
        {
            *phVfsObj = RTVfsObjFromVfs(hVfs);
            RTVfsRelease(hVfs);
            if (*phVfsObj != NIL_RTVFSOBJ)
                return VINF_SUCCESS;
            rc = VERR_VFS_CHAIN_CAST_FAILED;
        }
    }
    else
        rc = VERR_VFS_CHAIN_CAST_FAILED;
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnCanReuseElement}
 */
static DECLCALLBACK(bool) rtVfsChainPdbFsVol_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                             PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                             PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pReuseSpec);
    if (   pElement->paArgs[0].uProvider == pReuseElement->paArgs[0].uProvider
        || !pReuseElement->paArgs[0].uProvider)
        return true;
    return false;
}


/** VFS chain element 'file'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainPdbFsVolReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "pdbfs",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Open a PDB file system, requires a file object on the left side.\n",
    /* pfnValidate = */         rtVfsChainPdbFsVol_Validate,
    /* pfnInstantiate = */      rtVfsChainPdbFsVol_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainPdbFsVol_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainPdbFsVolReg, rtVfsChainPdbFsVolReg);

