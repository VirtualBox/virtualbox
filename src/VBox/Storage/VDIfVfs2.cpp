/* $Id: VDIfVfs2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual Disk Image (VDI), I/O interface to IPRT VFS I/O stream glue.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/file.h>
#include <iprt/sg.h>
#include <iprt/vfslowlevel.h>
#include <iprt/poll.h>
#include <VBox/vd.h>
#include <VBox/vd-ifs-internal.h>

#include <VBox/log.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Extended VD I/O interface structure that vdIfFromVfs_xxx uses.
 *
 * It's passed as pvUser to each call.
 */
typedef struct VDIFFROMVFS
{
    VDINTERFACEIO   CoreIo;

    /** Magic.  */
    uint32_t        u32Magic;
    /** The stream access mode (RTFILE_O_ACCESS_MASK), possibly others. */
    uint32_t        fAccessMode;
    /** The I/O stream.  This is NIL after it's been closed. */
    RTVFSIOSTREAM   hVfsIos;
    /** Completion callback. */
    PFNVDCOMPLETED  pfnCompleted;
    /** User parameter for the completion callback. */
    void           *pvCompletedUser;
    /** Set if hVfsIos has been opened. */
    bool            fOpened;
} VDIFFROMVFS;
/** Magic value for VDIFFROMVFS::u32Magic. */
#define VDIFFROMVFS_MAGIC   UINT32_C(0x11223344)

/** Pointer to the instance data for the vdIfFromVfs_ methods. */
typedef struct VDIFFROMVFS *PVDIFFROMVFS;


typedef struct FILESTORAGEINTERNAL
{
    /** File handle. */
    RTFILE         file;
} FILESTORAGEINTERNAL, *PFILESTORAGEINTERNAL;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

#define STATUS_WAIT    UINT32_C(0)
#define STATUS_WRITE   UINT32_C(1)
#define STATUS_WRITING UINT32_C(2)
#define STATUS_READ    UINT32_C(3)
#define STATUS_READING UINT32_C(4)
#define STATUS_END     UINT32_C(5)

/* Enable for getting some flow history. */
#if 0
# define DEBUG_PRINT_FLOW() RTPrintf("%s\n", __FUNCTION__)
#else
# define DEBUG_PRINT_FLOW() do {} while (0)
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/** @name VDINTERFACEIO stubs returning not-implemented.
 * @{
 */

/** @interface_method_impl{VDINTERFACEIO,pfnDelete}  */
static DECLCALLBACK(int) notImpl_Delete(void *pvUser, const char *pcszFilename)
{
    NOREF(pvUser); NOREF(pcszFilename);
    Log(("%s\n",  __FUNCTION__));
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}

/** @interface_method_impl{VDINTERFACEIO,pfnMove}  */
static DECLCALLBACK(int) notImpl_Move(void *pvUser, const char *pcszSrc, const char *pcszDst, unsigned fMove)
{
    NOREF(pvUser); NOREF(pcszSrc); NOREF(pcszDst); NOREF(fMove);
    Log(("%s\n",  __FUNCTION__));
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}

/** @interface_method_impl{VDINTERFACEIO,pfnGetFreeSpace}  */
static DECLCALLBACK(int) notImpl_GetFreeSpace(void *pvUser, const char *pcszFilename, int64_t *pcbFreeSpace)
{
    NOREF(pvUser); NOREF(pcszFilename); NOREF(pcbFreeSpace);
    Log(("%s\n",  __FUNCTION__));
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}

/** @interface_method_impl{VDINTERFACEIO,pfnGetModificationTime}  */
static DECLCALLBACK(int) notImpl_GetModificationTime(void *pvUser, const char *pcszFilename, PRTTIMESPEC pModificationTime)
{
    NOREF(pvUser); NOREF(pcszFilename); NOREF(pModificationTime);
    Log(("%s\n",  __FUNCTION__));
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}

/** @interface_method_impl{VDINTERFACEIO,pfnSetSize}  */
static DECLCALLBACK(int) notImpl_SetSize(void *pvUser, void *pvStorage, uint64_t cb)
{
    NOREF(pvUser); NOREF(pvStorage); NOREF(cb);
    Log(("%s\n",  __FUNCTION__));
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}

#if 0  /* unused */
/** @interface_method_impl{VDINTERFACEIO,pfnWriteSync}  */
static DECLCALLBACK(int) notImpl_WriteSync(void *pvUser, void *pvStorage, uint64_t off, const void *pvBuf,
                                           size_t cbWrite, size_t *pcbWritten)
{
    RT_NOREF6(pvUser, pvStorage, off, pvBuf, cbWrite, pcbWritten)
    Log(("%s\n",  __FUNCTION__));
    return VERR_NOT_IMPLEMENTED;
}
#endif

/** @interface_method_impl{VDINTERFACEIO,pfnFlushSync}  */
static DECLCALLBACK(int) notImpl_FlushSync(void *pvUser, void *pvStorage)
{
    NOREF(pvUser); NOREF(pvStorage);
    Log(("%s\n",  __FUNCTION__));
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}

/** @} */


/** @interface_method_impl{VDINTERFACEIO,pfnOpen}  */
static DECLCALLBACK(int) vdIfFromVfs_Open(void *pvUser, const char *pszLocation, uint32_t fOpen,
                                          PFNVDCOMPLETED pfnCompleted, void **ppvStorage)
{
    RT_NOREF1(pszLocation);
    PVDIFFROMVFS pThis = (PVDIFFROMVFS)pvUser;

    /*
     * Validate input.
     */
    AssertPtrReturn(ppvStorage, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnCompleted, VERR_INVALID_PARAMETER);

    /*
     * We ignore the name, assuming the caller is opening the stream/file we're        .
     * serving.  Thus, after close, all open calls fail.
     */
    AssertReturn(!pThis->fOpened, VERR_FILE_NOT_FOUND);
    AssertReturn(pThis->hVfsIos != NIL_RTVFSIOSTREAM, VERR_FILE_NOT_FOUND); /* paranoia */
    AssertMsgReturn((pThis->fAccessMode & fOpen & RTFILE_O_ACCESS_MASK) == (fOpen & RTFILE_O_ACCESS_MASK),
                    ("fAccessMode=%#x fOpen=%#x\n", pThis->fAccessMode, fOpen), VERR_ACCESS_DENIED);

    pThis->fAccessMode      = fOpen & RTFILE_O_ACCESS_MASK;
    pThis->fOpened          = true;
    pThis->pfnCompleted     = pfnCompleted;
    pThis->pvCompletedUser  = pvUser;

    *ppvStorage = pThis->hVfsIos;
    return VINF_SUCCESS;
}

/** @interface_method_impl{VDINTERFACEIO,pfnClose}  */
static DECLCALLBACK(int) vdIfFromVfs_Close(void *pvUser, void *pvStorage)
{
    PVDIFFROMVFS pThis = (PVDIFFROMVFS)pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertReturn(pThis->hVfsIos == (RTVFSIOSTREAM)pvStorage, VERR_INVALID_HANDLE);
    AssertReturn(pThis->fOpened, VERR_INVALID_HANDLE);

    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;

    return VINF_SUCCESS;
}


/** @interface_method_impl{VDINTERFACEIO,pfnGetSize}  */
static DECLCALLBACK(int) vdIfFromVfs_GetSize(void *pvUser, void *pvStorage, uint64_t *pcb)
{
    PVDIFFROMVFS            pThis = (PVDIFFROMVFS)pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertReturn(pThis->hVfsIos == (RTVFSIOSTREAM)pvStorage, VERR_INVALID_HANDLE);
    AssertReturn(pThis->fOpened, VERR_INVALID_HANDLE);

    RTFSOBJINFO ObjInfo;
    int rc = RTVfsIoStrmQueryInfo(pThis->hVfsIos, &ObjInfo, RTFSOBJATTRADD_NOTHING);
    if (RT_SUCCESS(rc))
        *pcb = ObjInfo.cbObject;
    return rc;
}

/** @interface_method_impl{VDINTERFACEIO,pfnReadSync}  */
static DECLCALLBACK(int) vdIfFromVfs_ReadSync(void *pvUser, void *pvStorage, uint64_t off, void *pvBuf,
                                              size_t cbToRead, size_t *pcbRead)
{
    PVDIFFROMVFS            pThis = (PVDIFFROMVFS)pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertReturn(pThis->hVfsIos == (RTVFSIOSTREAM)pvStorage, VERR_INVALID_HANDLE);
    AssertReturn(pThis->fOpened, VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    AssertReturn(pThis->fAccessMode & RTFILE_O_READ, VERR_ACCESS_DENIED);

    return RTVfsIoStrmReadAt(pThis->hVfsIos, off, pvBuf, cbToRead, true /*fBlocking*/, pcbRead);
}


/** @interface_method_impl{VDINTERFACEIO,pfnWriteSync}  */
static DECLCALLBACK(int) vdIfFromVfs_WriteSync(void *pvUser, void *pvStorage, uint64_t off, void const *pvBuf,
                                               size_t cbToWrite, size_t *pcbWritten)
{
    PVDIFFROMVFS            pThis = (PVDIFFROMVFS)pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertReturn(pThis->hVfsIos == (RTVFSIOSTREAM)pvStorage, VERR_INVALID_HANDLE);
    AssertReturn(pThis->fOpened, VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    AssertReturn(pThis->fAccessMode & RTFILE_O_WRITE, VERR_ACCESS_DENIED);

    return RTVfsIoStrmWriteAt(pThis->hVfsIos, off, pvBuf, cbToWrite, true /*fBlocking*/, pcbWritten);
}


VBOXDDU_DECL(int) VDIfCreateFromVfsStream(RTVFSIOSTREAM hVfsIos, uint32_t fAccessMode, PVDINTERFACEIO *ppIoIf)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(ppIoIf, VERR_INVALID_POINTER);
    *ppIoIf = NULL;
    AssertReturn(hVfsIos != NIL_RTVFSIOSTREAM, VERR_INVALID_HANDLE);
    AssertReturn(fAccessMode & RTFILE_O_ACCESS_MASK, VERR_INVALID_FLAGS);

    uint32_t cRefs = RTVfsIoStrmRetain(hVfsIos);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Allocate and init a callback + instance data structure.
     */
    int rc;
    PVDIFFROMVFS pThis = (PVDIFFROMVFS)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        pThis->CoreIo.pfnOpen                = vdIfFromVfs_Open;
        pThis->CoreIo.pfnClose               = vdIfFromVfs_Close;
        pThis->CoreIo.pfnDelete              = notImpl_Delete;
        pThis->CoreIo.pfnMove                = notImpl_Move;
        pThis->CoreIo.pfnGetFreeSpace        = notImpl_GetFreeSpace;
        pThis->CoreIo.pfnGetModificationTime = notImpl_GetModificationTime;
        pThis->CoreIo.pfnGetSize             = vdIfFromVfs_GetSize;
        pThis->CoreIo.pfnSetSize             = notImpl_SetSize;
        pThis->CoreIo.pfnReadSync            = vdIfFromVfs_ReadSync;
        pThis->CoreIo.pfnWriteSync           = vdIfFromVfs_WriteSync;
        pThis->CoreIo.pfnFlushSync           = notImpl_FlushSync;

        pThis->hVfsIos     = hVfsIos;
        pThis->fAccessMode = fAccessMode;
        pThis->fOpened     = false;
        pThis->u32Magic    = VDIFFROMVFS_MAGIC;

        PVDINTERFACE pFakeList = NULL;
        rc = VDInterfaceAdd(&pThis->CoreIo.Core, "FromVfsStream", VDINTERFACETYPE_IO, pThis, sizeof(pThis->CoreIo), &pFakeList);
        if (RT_SUCCESS(rc))
        {
            *ppIoIf = &pThis->CoreIo;
            return VINF_SUCCESS;
        }

        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;
    RTVfsIoStrmRelease(hVfsIos);
    return rc;
}


VBOXDDU_DECL(int) VDIfDestroyFromVfsStream(PVDINTERFACEIO pIoIf)
{
    if (pIoIf)
    {
        PVDIFFROMVFS            pThis = (PVDIFFROMVFS)pIoIf;
        AssertPtrReturn(pThis, VERR_INVALID_POINTER);
        AssertReturn(pThis->u32Magic == VDIFFROMVFS_MAGIC, VERR_INVALID_MAGIC);

        if (pThis->hVfsIos != NIL_RTVFSIOSTREAM)
        {
            RTVfsIoStrmRelease(pThis->hVfsIos);
            pThis->hVfsIos = NIL_RTVFSIOSTREAM;
        }
        pThis->u32Magic = ~VDIFFROMVFS_MAGIC;
        RTMemFree(pThis);
    }
    return VINF_SUCCESS;
}

