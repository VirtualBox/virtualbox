/* $Id: vfsbase.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Virtual File System, Base.
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
#define LOG_GROUP RTLOGGROUP_FS
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/poll.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/zero.h>

#include "internal/file.h"
#include "internal/fs.h"
#include "internal/magics.h"
#include "internal/path.h"
//#include "internal/vfs.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The instance data alignment. */
#define RTVFS_INST_ALIGNMENT        16U

/** The max number of symbolic links to resolve in a path. */
#define RTVFS_MAX_LINKS             20U


/** Asserts that the VFS base object vtable is valid. */
#define RTVFSOBJ_ASSERT_OPS(a_pObjOps, a_enmType) \
    do \
    { \
        Assert((a_pObjOps)->uVersion == RTVFSOBJOPS_VERSION); \
        Assert((a_pObjOps)->enmType == (a_enmType) || (a_enmType) == RTVFSOBJTYPE_INVALID); \
        AssertPtr((a_pObjOps)->pszName); \
        Assert(*(a_pObjOps)->pszName); \
        AssertPtr((a_pObjOps)->pfnClose); \
        AssertPtr((a_pObjOps)->pfnQueryInfo); \
        AssertPtrNull((a_pObjOps)->pfnQueryInfoEx); \
        Assert((a_pObjOps)->uEndMarker == RTVFSOBJOPS_VERSION); \
    } while (0)

/** Asserts that the VFS set object vtable is valid. */
#define RTVFSOBJSET_ASSERT_OPS(a_pSetOps, a_offObjOps) \
    do \
    { \
        Assert((a_pSetOps)->uVersion == RTVFSOBJSETOPS_VERSION); \
        Assert((a_pSetOps)->offObjOps == (a_offObjOps)); \
        AssertPtrNull((a_pSetOps)->pfnSetMode); \
        AssertPtrNull((a_pSetOps)->pfnSetTimes); \
        AssertPtrNull((a_pSetOps)->pfnSetOwner); \
        Assert((a_pSetOps)->uEndMarker == RTVFSOBJSETOPS_VERSION); \
    } while (0)

/** Asserts that the VFS directory vtable is valid. */
#define RTVFSDIR_ASSERT_OPS(pDirOps, a_enmType) \
    do { \
        RTVFSOBJ_ASSERT_OPS(&(pDirOps)->Obj, a_enmType); \
        RTVFSOBJSET_ASSERT_OPS(&(pDirOps)->ObjSet, RT_UOFFSETOF(RTVFSDIROPS, ObjSet) - RT_UOFFSETOF(RTVFSDIROPS, Obj)); \
        Assert((pDirOps)->uVersion == RTVFSDIROPS_VERSION); \
        Assert(!(pDirOps)->fReserved); \
        AssertPtr((pDirOps)->pfnOpen); \
        AssertPtrNull((pDirOps)->pfnOpenFile); \
        AssertPtrNull((pDirOps)->pfnOpenDir); \
        AssertPtrNull((pDirOps)->pfnCreateDir); \
        AssertPtrNull((pDirOps)->pfnOpenSymlink); \
        AssertPtrNull((pDirOps)->pfnCreateSymlink); \
        AssertPtrNull((pDirOps)->pfnUnlinkEntry); \
        AssertPtrNull((pDirOps)->pfnRenameEntry); \
        AssertPtr((pDirOps)->pfnRewindDir); \
        AssertPtr((pDirOps)->pfnReadDir); \
        Assert((pDirOps)->uEndMarker == RTVFSDIROPS_VERSION); \
    } while (0)

/** Asserts that the VFS I/O stream vtable is valid. */
#define RTVFSIOSTREAM_ASSERT_OPS(pIoStreamOps, a_enmType) \
    do { \
        RTVFSOBJ_ASSERT_OPS(&(pIoStreamOps)->Obj, a_enmType); \
        Assert((pIoStreamOps)->uVersion == RTVFSIOSTREAMOPS_VERSION); \
        Assert(!((pIoStreamOps)->fFeatures & ~RTVFSIOSTREAMOPS_FEAT_VALID_MASK)); \
        AssertPtr((pIoStreamOps)->pfnRead); \
        AssertPtrNull((pIoStreamOps)->pfnWrite); \
        AssertPtr((pIoStreamOps)->pfnFlush); \
        AssertPtrNull((pIoStreamOps)->pfnPollOne); \
        AssertPtr((pIoStreamOps)->pfnTell); \
        AssertPtrNull((pIoStreamOps)->pfnSkip); \
        AssertPtrNull((pIoStreamOps)->pfnZeroFill); \
        Assert((pIoStreamOps)->uEndMarker == RTVFSIOSTREAMOPS_VERSION); \
    } while (0)

/** Asserts that the VFS I/O stream vtable is valid. */
#define RTVFSFILE_ASSERT_OPS(pFileOps, a_enmType) \
    do { \
        RTVFSIOSTREAM_ASSERT_OPS(&(pFileOps)->Stream, a_enmType); \
        Assert((pFileOps)->uVersion == RTVFSFILEOPS_VERSION); \
        Assert((pFileOps)->fReserved == 0); \
        AssertPtr((pFileOps)->pfnSeek); \
        AssertPtrNull((pFileOps)->pfnQuerySize); \
        AssertPtrNull((pFileOps)->pfnSetSize); \
        AssertPtrNull((pFileOps)->pfnQueryMaxSize); \
        Assert((pFileOps)->uEndMarker == RTVFSFILEOPS_VERSION); \
    } while (0)

/** Asserts that the VFS symlink vtable is valid. */
#define RTVFSSYMLINK_ASSERT_OPS(pSymlinkOps, a_enmType) \
    do { \
        RTVFSOBJ_ASSERT_OPS(&(pSymlinkOps)->Obj, a_enmType); \
        RTVFSOBJSET_ASSERT_OPS(&(pSymlinkOps)->ObjSet, RT_UOFFSETOF(RTVFSSYMLINKOPS, ObjSet) - RT_UOFFSETOF(RTVFSSYMLINKOPS, Obj)); \
        Assert((pSymlinkOps)->uVersion == RTVFSSYMLINKOPS_VERSION); \
        Assert(!(pSymlinkOps)->fReserved); \
        AssertPtr((pSymlinkOps)->pfnRead); \
        Assert((pSymlinkOps)->uEndMarker == RTVFSSYMLINKOPS_VERSION); \
    } while (0)


/** Validates a VFS handle and returns @a rcRet if it's invalid. */
#define RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, rcRet) \
    do { \
        if ((hVfs) != NIL_RTVFS) \
        { \
            AssertPtrReturn((hVfs), (rcRet)); \
            AssertReturn((hVfs)->uMagic == RTVFS_MAGIC, (rcRet)); \
        } \
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** @todo Move all this stuff to internal/vfs.h */


/**
 * The VFS internal lock data.
 */
typedef struct RTVFSLOCKINTERNAL
{
    /** The number of references to the this lock. */
    uint32_t volatile       cRefs;
    /** The lock type. */
    RTVFSLOCKTYPE           enmType;
    /** Type specific data. */
    union
    {
        /** Read/Write semaphore handle. */
        RTSEMRW             hSemRW;
        /** Fast mutex semaphore handle. */
        RTSEMFASTMUTEX      hFastMtx;
        /** Regular mutex semaphore handle. */
        RTSEMMUTEX          hMtx;
    } u;
} RTVFSLOCKINTERNAL;


/**
 * The VFS base object handle data.
 *
 * All other VFS handles are derived from this one.  The final handle type is
 * indicated by RTVFSOBJOPS::enmType via the RTVFSOBJINTERNAL::pOps member.
 */
typedef struct RTVFSOBJINTERNAL
{
    /** The VFS magic (RTVFSOBJ_MAGIC). */
    uint32_t                uMagic : 31;
    /** Set if we've got no VFS reference but still got a valid hVfs.
     * This is hack for permanent root directory objects. */
    uint32_t                fNoVfsRef : 1;
    /** The number of references to this VFS object. */
    uint32_t volatile       cRefs;
    /** Pointer to the instance data. */
    void                   *pvThis;
    /** The vtable. */
    PCRTVFSOBJOPS           pOps;
    /** The lock protecting all access to the VFS.
     * Only valid if RTVFS_C_THREAD_SAFE is set, otherwise it is NIL_RTVFSLOCK. */
    RTVFSLOCK               hLock;
    /** Reference back to the VFS containing this object. */
    RTVFS                   hVfs;
} RTVFSOBJINTERNAL;


/**
 * The VFS filesystem stream handle data.
 *
 * @extends RTVFSOBJINTERNAL
 */
typedef struct RTVFSFSSTREAMINTERNAL
{
    /** The VFS magic (RTVFSFSTREAM_MAGIC). */
    uint32_t                uMagic;
    /** File open flags, at a minimum the access mask. */
    uint32_t                fFlags;
    /** The vtable. */
    PCRTVFSFSSTREAMOPS      pOps;
    /** The base object handle data. */
    RTVFSOBJINTERNAL        Base;
} RTVFSFSSTREAMINTERNAL;


/**
 * The VFS handle data.
 *
 * @extends RTVFSOBJINTERNAL
 */
typedef struct RTVFSINTERNAL
{
    /** The VFS magic (RTVFS_MAGIC). */
    uint32_t                uMagic;
    /** Creation flags (RTVFS_C_XXX). */
    uint32_t                fFlags;
    /** The vtable. */
    PCRTVFSOPS              pOps;
    /** The base object handle data. */
    RTVFSOBJINTERNAL        Base;
} RTVFSINTERNAL;


/**
 * The VFS directory handle data.
 *
 * @extends RTVFSOBJINTERNAL
 */
typedef struct RTVFSDIRINTERNAL
{
    /** The VFS magic (RTVFSDIR_MAGIC). */
    uint32_t                uMagic;
    /** Reserved for flags or something. */
    uint32_t                fReserved;
    /** The vtable. */
    PCRTVFSDIROPS           pOps;
    /** The base object handle data. */
    RTVFSOBJINTERNAL        Base;
} RTVFSDIRINTERNAL;


/**
 * The VFS symbolic link handle data.
 *
 * @extends RTVFSOBJINTERNAL
 */
typedef struct RTVFSSYMLINKINTERNAL
{
    /** The VFS magic (RTVFSSYMLINK_MAGIC). */
    uint32_t                uMagic;
    /** Reserved for flags or something. */
    uint32_t                fReserved;
    /** The vtable. */
    PCRTVFSSYMLINKOPS       pOps;
    /** The base object handle data. */
    RTVFSOBJINTERNAL        Base;
} RTVFSSYMLINKINTERNAL;


/**
 * The VFS I/O stream handle data.
 *
 * This is often part of a type specific handle, like a file or pipe.
 *
 * @extends RTVFSOBJINTERNAL
 */
typedef struct RTVFSIOSTREAMINTERNAL
{
    /** The VFS magic (RTVFSIOSTREAM_MAGIC). */
    uint32_t                uMagic;
    /** File open flags, at a minimum the access mask. */
    uint32_t                fFlags;
    /** The vtable. */
    PCRTVFSIOSTREAMOPS      pOps;
    /** The base object handle data. */
    RTVFSOBJINTERNAL        Base;
} RTVFSIOSTREAMINTERNAL;


/**
 * The VFS file handle data.
 *
 * @extends RTVFSIOSTREAMINTERNAL
 */
typedef struct RTVFSFILEINTERNAL
{
    /** The VFS magic (RTVFSFILE_MAGIC). */
    uint32_t                uMagic;
    /** Reserved for flags or something. */
    uint32_t                fReserved;
    /** The vtable. */
    PCRTVFSFILEOPS          pOps;
    /** The stream handle data. */
    RTVFSIOSTREAMINTERNAL   Stream;
} RTVFSFILEINTERNAL;

#if 0 /* later */

/**
 * The VFS pipe handle data.
 *
 * @extends RTVFSIOSTREAMINTERNAL
 */
typedef struct RTVFSPIPEINTERNAL
{
    /** The VFS magic (RTVFSPIPE_MAGIC). */
    uint32_t                uMagic;
    /** Reserved for flags or something. */
    uint32_t                fReserved;
    /** The vtable. */
    PCRTVFSPIPEOPS          pOps;
    /** The stream handle data. */
    RTVFSIOSTREAMINTERNAL   Stream;
} RTVFSPIPEINTERNAL;


/**
 * The VFS socket handle data.
 *
 * @extends RTVFSIOSTREAMINTERNAL
 */
typedef struct RTVFSSOCKETINTERNAL
{
    /** The VFS magic (RTVFSSOCKET_MAGIC). */
    uint32_t                uMagic;
    /** Reserved for flags or something. */
    uint32_t                fReserved;
    /** The vtable. */
    PCRTVFSSOCKETOPS        pOps;
    /** The stream handle data. */
    RTVFSIOSTREAMINTERNAL   Stream;
} RTVFSSOCKETINTERNAL;

#endif /* later */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLINLINE(uint32_t) rtVfsObjRelease(RTVFSOBJINTERNAL *pThis);
static int rtVfsTraverseToParent(RTVFSINTERNAL *pThis, PRTVFSPARSEDPATH pPath, uint32_t fFlags, RTVFSDIRINTERNAL **ppVfsParentDir);
static int rtVfsDirFollowSymlinkObjToParent(RTVFSDIRINTERNAL **ppVfsParentDir, RTVFSOBJ hVfsObj,
                                            PRTVFSPARSEDPATH pPath, uint32_t fFlags);



/**
 * Translates a RTVFSOBJTYPE value into a string.
 *
 * @returns Pointer to readonly name.
 * @param   enmType             The object type to name.
 */
RTDECL(const char *) RTVfsTypeName(RTVFSOBJTYPE enmType)
{
    switch (enmType)
    {
        case RTVFSOBJTYPE_INVALID:      return "invalid";
        case RTVFSOBJTYPE_BASE:         return "base";
        case RTVFSOBJTYPE_VFS:          return "VFS";
        case RTVFSOBJTYPE_FS_STREAM:    return "FS stream";
        case RTVFSOBJTYPE_IO_STREAM:    return "I/O stream";
        case RTVFSOBJTYPE_DIR:          return "directory";
        case RTVFSOBJTYPE_FILE:         return "file";
        case RTVFSOBJTYPE_SYMLINK:      return "symlink";
        case RTVFSOBJTYPE_END:          return "end";
        case RTVFSOBJTYPE_32BIT_HACK:
            break;
    }
    return "unknown";
}


/*
 *
 *  V F S   L o c k   A b s t r a c t i o n
 *  V F S   L o c k   A b s t r a c t i o n
 *  V F S   L o c k   A b s t r a c t i o n
 *
 *
 */


RTDECL(uint32_t) RTVfsLockRetain(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->enmType > RTVFSLOCKTYPE_INVALID && pThis->enmType < RTVFSLOCKTYPE_END, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p %d\n", cRefs, pThis, pThis->enmType));
    return cRefs;
}


RTDECL(uint32_t) RTVfsLockRetainDebug(RTVFSLOCK hLock, RT_SRC_POS_DECL)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->enmType > RTVFSLOCKTYPE_INVALID && pThis->enmType < RTVFSLOCKTYPE_END, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p %d\n", cRefs, pThis, pThis->enmType));
    LogFlow(("RTVfsLockRetainDebug(%p) -> %d;  caller: %s %s(%u)\n", hLock, cRefs, pszFunction, pszFile, iLine));
    RT_SRC_POS_NOREF();
    return cRefs;
}


/**
 * Destroys a VFS lock handle.
 *
 * @param   pThis               The lock to destroy.
 */
static void rtVfsLockDestroy(RTVFSLOCKINTERNAL *pThis)
{
    switch (pThis->enmType)
    {
        case RTVFSLOCKTYPE_RW:
            RTSemRWDestroy(pThis->u.hSemRW);
            pThis->u.hSemRW = NIL_RTSEMRW;
            break;

        case RTVFSLOCKTYPE_FASTMUTEX:
            RTSemFastMutexDestroy(pThis->u.hFastMtx);
            pThis->u.hFastMtx = NIL_RTSEMFASTMUTEX;
            break;

        case RTVFSLOCKTYPE_MUTEX:
            RTSemMutexDestroy(pThis->u.hMtx);
            pThis->u.hFastMtx = NIL_RTSEMMUTEX;
            break;

        default:
            AssertMsgFailedReturnVoid(("%p %d\n", pThis, pThis->enmType));
    }

    pThis->enmType = RTVFSLOCKTYPE_INVALID;
    RTMemFree(pThis);
}


RTDECL(uint32_t) RTVfsLockRelease(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    if (pThis == NIL_RTVFSLOCK)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->enmType > RTVFSLOCKTYPE_INVALID && pThis->enmType < RTVFSLOCKTYPE_END, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p %d\n", cRefs, pThis, pThis->enmType));
    if (cRefs == 0)
        rtVfsLockDestroy(pThis);
    return cRefs;
}


/**
 * Creates a read/write lock.
 *
 * @returns IPRT status code
 * @param   phLock              Where to return the lock handle.
 */
static int rtVfsLockCreateRW(PRTVFSLOCK phLock)
{
    RTVFSLOCKINTERNAL *pThis = (RTVFSLOCKINTERNAL *)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->cRefs    = 1;
    pThis->enmType  = RTVFSLOCKTYPE_RW;

    int rc = RTSemRWCreate(&pThis->u.hSemRW);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    *phLock = pThis;
    return VINF_SUCCESS;
}


/**
 * Creates a fast mutex lock.
 *
 * @returns IPRT status code
 * @param   phLock              Where to return the lock handle.
 */
static int rtVfsLockCreateFastMutex(PRTVFSLOCK phLock)
{
    RTVFSLOCKINTERNAL *pThis = (RTVFSLOCKINTERNAL *)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->cRefs    = 1;
    pThis->enmType  = RTVFSLOCKTYPE_FASTMUTEX;

    int rc = RTSemFastMutexCreate(&pThis->u.hFastMtx);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    *phLock = pThis;
    return VINF_SUCCESS;

}


/**
 * Creates a mutex lock.
 *
 * @returns IPRT status code
 * @param   phLock              Where to return the lock handle.
 */
static int rtVfsLockCreateMutex(PRTVFSLOCK phLock)
{
    RTVFSLOCKINTERNAL *pThis = (RTVFSLOCKINTERNAL *)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->cRefs    = 1;
    pThis->enmType  = RTVFSLOCKTYPE_MUTEX;

    int rc = RTSemMutexCreate(&pThis->u.hMtx);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    *phLock = pThis;
    return VINF_SUCCESS;
}


/**
 * Acquires the lock for reading.
 *
 * @param   hLock               Non-nil lock handle.
 * @internal
 */
RTDECL(void) RTVfsLockAcquireReadSlow(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    int                rc;

    AssertPtr(pThis);
    switch (pThis->enmType)
    {
        case RTVFSLOCKTYPE_RW:
            rc = RTSemRWRequestRead(pThis->u.hSemRW, RT_INDEFINITE_WAIT);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_FASTMUTEX:
            rc = RTSemFastMutexRequest(pThis->u.hFastMtx);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_MUTEX:
            rc = RTSemMutexRequest(pThis->u.hMtx, RT_INDEFINITE_WAIT);
            AssertRC(rc);
            break;
        default:
            AssertFailed();
    }
}


/**
 * Release a lock held for reading.
 *
 * @param   hLock               Non-nil lock handle.
 * @internal
 */
RTDECL(void) RTVfsLockReleaseReadSlow(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    int                rc;

    AssertPtr(pThis);
    switch (pThis->enmType)
    {
        case RTVFSLOCKTYPE_RW:
            rc = RTSemRWReleaseRead(pThis->u.hSemRW);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_FASTMUTEX:
            rc = RTSemFastMutexRelease(pThis->u.hFastMtx);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_MUTEX:
            rc = RTSemMutexRelease(pThis->u.hMtx);
            AssertRC(rc);
            break;
        default:
            AssertFailed();
    }
}


/**
 * Acquires the lock for writing.
 *
 * @param   hLock               Non-nil lock handle.
 * @internal
 */
RTDECL(void) RTVfsLockAcquireWriteSlow(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    int                rc;

    AssertPtr(pThis);
    switch (pThis->enmType)
    {
        case RTVFSLOCKTYPE_RW:
            rc = RTSemRWRequestWrite(pThis->u.hSemRW, RT_INDEFINITE_WAIT);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_FASTMUTEX:
            rc = RTSemFastMutexRequest(pThis->u.hFastMtx);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_MUTEX:
            rc = RTSemMutexRequest(pThis->u.hMtx, RT_INDEFINITE_WAIT);
            AssertRC(rc);
            break;
        default:
            AssertFailed();
    }
}


/**
 * Release a lock held for writing.
 *
 * @param   hLock               Non-nil lock handle.
 * @internal
 */
RTDECL(void) RTVfsLockReleaseWriteSlow(RTVFSLOCK hLock)
{
    RTVFSLOCKINTERNAL *pThis = hLock;
    int                rc;

    AssertPtr(pThis);
    switch (pThis->enmType)
    {
        case RTVFSLOCKTYPE_RW:
            rc = RTSemRWReleaseWrite(pThis->u.hSemRW);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_FASTMUTEX:
            rc = RTSemFastMutexRelease(pThis->u.hFastMtx);
            AssertRC(rc);
            break;

        case RTVFSLOCKTYPE_MUTEX:
            rc = RTSemMutexRelease(pThis->u.hMtx);
            AssertRC(rc);
            break;
        default:
            AssertFailed();
    }
}



/*
 *
 *  B A S E   O B J E C T
 *  B A S E   O B J E C T
 *  B A S E   O B J E C T
 *
 */

/**
 * Internal object retainer that asserts sanity in strict builds.
 *
 * @param   pThis               The base object handle data.
 * @param   pszCaller           Where we're called from.
 */
DECLINLINE(void) rtVfsObjRetainVoid(RTVFSOBJINTERNAL *pThis, const char *pszCaller)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
LogFlow(("rtVfsObjRetainVoid(%p/%p) -> %d;  caller=%s\n", pThis, pThis->pvThis, cRefs, pszCaller)); RT_NOREF(pszCaller);
    AssertMsg(cRefs > 1 && cRefs < _1M,
              ("%#x %p ops=%p %s (%d); caller=%s\n", cRefs, pThis, pThis->pOps, pThis->pOps->pszName, pThis->pOps->enmType, pszCaller));
    NOREF(cRefs);
}


/**
 * Initializes the base object part of a new object.
 *
 * @returns IPRT status code.
 * @param   pThis               Pointer to the base object part.
 * @param   pObjOps             The base object vtable.
 * @param   hVfs                The VFS handle to associate with.
 * @param   fNoVfsRef           If set, do not retain an additional reference to
 *                              @a hVfs.  Permanent root dir hack.
 * @param   hLock               The lock handle, pseudo handle or nil.
 * @param   pvThis              Pointer to the private data.
 */
static int rtVfsObjInitNewObject(RTVFSOBJINTERNAL *pThis, PCRTVFSOBJOPS pObjOps, RTVFS hVfs, bool fNoVfsRef,
                                 RTVFSLOCK hLock, void *pvThis)
{
    /*
     * Deal with the lock first as that's the most complicated matter.
     */
    if (hLock != NIL_RTVFSLOCK)
    {
        int rc;
        if (hLock == RTVFSLOCK_CREATE_RW)
        {
            rc = rtVfsLockCreateRW(&hLock);
            AssertRCReturn(rc, rc);
        }
        else if (hLock == RTVFSLOCK_CREATE_FASTMUTEX)
        {
            rc = rtVfsLockCreateFastMutex(&hLock);
            AssertRCReturn(rc, rc);
        }
        else if (hLock == RTVFSLOCK_CREATE_MUTEX)
        {
            rc = rtVfsLockCreateMutex(&hLock);
            AssertRCReturn(rc, rc);
        }
        else
        {
            /*
             * The caller specified a lock, we consume the this reference.
             */
            AssertPtrReturn(hLock, VERR_INVALID_HANDLE);
            AssertReturn(hLock->enmType > RTVFSLOCKTYPE_INVALID && hLock->enmType < RTVFSLOCKTYPE_END, VERR_INVALID_HANDLE);
            AssertReturn(hLock->cRefs > 0, VERR_INVALID_HANDLE);
        }
    }
    else if (hVfs != NIL_RTVFS)
    {
        /*
         * Retain a reference to the VFS lock, if there is one.
         */
        hLock = hVfs->Base.hLock;
        if (hLock != NIL_RTVFSLOCK)
        {
            uint32_t cRefs = RTVfsLockRetain(hLock);
            if (RT_UNLIKELY(cRefs == UINT32_MAX))
                return VERR_INVALID_HANDLE;
        }
    }


    /*
     * Do the actual initializing.
     */
    pThis->uMagic       = RTVFSOBJ_MAGIC;
    pThis->fNoVfsRef    = fNoVfsRef;
    pThis->pvThis       = pvThis;
    pThis->pOps         = pObjOps;
    pThis->cRefs        = 1;
    pThis->hVfs         = hVfs;
    pThis->hLock        = hLock;
    if (hVfs != NIL_RTVFS && !fNoVfsRef)
        rtVfsObjRetainVoid(&hVfs->Base, "rtVfsObjInitNewObject");

    return VINF_SUCCESS;
}


RTDECL(int) RTVfsNewBaseObj(PCRTVFSOBJOPS pObjOps, size_t cbInstance, RTVFS hVfs, RTVFSLOCK hLock,
                            PRTVFSOBJ phVfsObj, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pObjOps);
    AssertReturn(pObjOps->uVersion   == RTVFSOBJOPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pObjOps->uEndMarker == RTVFSOBJOPS_VERSION, VERR_VERSION_MISMATCH);
    RTVFSOBJ_ASSERT_OPS(pObjOps, RTVFSOBJTYPE_BASE);
    Assert(cbInstance > 0);
    AssertPtr(ppvInstance);
    AssertPtr(phVfsObj);
    RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, VERR_INVALID_HANDLE);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSOBJINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSOBJINTERNAL *pThis = (RTVFSOBJINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(pThis, pObjOps, hVfs, false /*fNoVfsRef*/, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    *phVfsObj    = pThis;
    *ppvInstance = pThis->pvThis;
    return VINF_SUCCESS;
}


RTDECL(void *) RTVfsObjToPrivate(RTVFSOBJ hVfsObj, PCRTVFSOBJOPS pObjOps)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NULL);
    if (pThis->pOps != pObjOps)
        return NULL;
    return pThis->pvThis;
}


/**
 * Internal object retainer that asserts sanity in strict builds.
 *
 * @returns The new reference count.
 * @param   pThis               The base object handle data.
 */
DECLINLINE(uint32_t) rtVfsObjRetain(RTVFSOBJINTERNAL *pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
LogFlow(("rtVfsObjRetain(%p/%p) -> %d\n", pThis, pThis->pvThis, cRefs));
    AssertMsg(cRefs > 1 && cRefs < _1M,
              ("%#x %p ops=%p %s (%d)\n", cRefs, pThis, pThis->pOps, pThis->pOps->pszName, pThis->pOps->enmType));
    return cRefs;
}

/**
 * Internal object retainer that asserts sanity in strict builds.
 *
 * @returns The new reference count.
 * @param   pThis               The base object handle data.
 */
DECLINLINE(uint32_t) rtVfsObjRetainDebug(RTVFSOBJINTERNAL *pThis, const char *pszApi, RT_SRC_POS_DECL)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M,
              ("%#x %p ops=%p %s (%d)\n", cRefs, pThis, pThis->pOps, pThis->pOps->pszName, pThis->pOps->enmType));
    LogFlow(("%s(%p/%p) -> %2d;  caller: %s %s(%d) \n", pszApi, pThis, pThis->pvThis, cRefs, pszFunction, pszFile, iLine));
    RT_SRC_POS_NOREF(); RT_NOREF(pszApi);
    return cRefs;
}


#ifdef DEBUG
# undef RTVfsObjRetain
#endif
RTDECL(uint32_t) RTVfsObjRetain(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, UINT32_MAX);

    return rtVfsObjRetain(pThis);
}
#ifdef DEBUG
# define RTVfsObjRetain(hVfsObj)    RTVfsObjRetainDebug(hVfsObj, RT_SRC_POS)
#endif


RTDECL(uint32_t) RTVfsObjRetainDebug(RTVFSOBJ hVfsObj, RT_SRC_POS_DECL)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, UINT32_MAX);

    return rtVfsObjRetainDebug(pThis, "RTVfsObjRetainDebug", RT_SRC_POS_ARGS);
}


/**
 * Does the actual object destruction for rtVfsObjRelease().
 *
 * @param   pThis               The object to destroy.
 */
static void rtVfsObjDestroy(RTVFSOBJINTERNAL *pThis)
{
    RTVFSOBJTYPE const enmType = pThis->pOps->enmType;

    /*
     * Invalidate the object.
     */
    RTVfsLockAcquireWrite(pThis->hLock);    /* paranoia */
    void *pvToFree = NULL;
    switch (enmType)
    {
        case RTVFSOBJTYPE_BASE:
            pvToFree = pThis;
            break;

        case RTVFSOBJTYPE_VFS:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSINTERNAL, Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSINTERNAL, Base)->uMagic, RTVFS_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_FS_STREAM:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSFSSTREAMINTERNAL, Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSFSSTREAMINTERNAL, Base)->uMagic, RTVFSFSSTREAM_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_IO_STREAM:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSIOSTREAMINTERNAL, Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSIOSTREAMINTERNAL, Base)->uMagic, RTVFSIOSTREAM_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_DIR:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSDIRINTERNAL, Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSDIRINTERNAL, Base)->uMagic, RTVFSDIR_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_FILE:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSFILEINTERNAL, Stream.Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSFILEINTERNAL, Stream.Base)->uMagic, RTVFSFILE_MAGIC_DEAD);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSIOSTREAMINTERNAL, Base)->uMagic, RTVFSIOSTREAM_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_SYMLINK:
            pvToFree         = RT_FROM_MEMBER(pThis, RTVFSSYMLINKINTERNAL, Base);
            ASMAtomicWriteU32(&RT_FROM_MEMBER(pThis, RTVFSSYMLINKINTERNAL, Base)->uMagic, RTVFSSYMLINK_MAGIC_DEAD);
            break;

        case RTVFSOBJTYPE_INVALID:
        case RTVFSOBJTYPE_END:
        case RTVFSOBJTYPE_32BIT_HACK:
            AssertMsgFailed(("enmType=%d ops=%p %s\n", enmType, pThis->pOps, pThis->pOps->pszName));
            break;
        /* no default as we want gcc warnings. */
    }
    pThis->uMagic = RTVFSOBJ_MAGIC_DEAD;
    RTVfsLockReleaseWrite(pThis->hLock);

    /*
     * Close the object and free the handle.
     */
    int rc = pThis->pOps->pfnClose(pThis->pvThis);
    AssertRC(rc);
    if (pThis->hVfs != NIL_RTVFS)
    {
        if (!pThis->fNoVfsRef)
            rtVfsObjRelease(&pThis->hVfs->Base);
        pThis->hVfs = NIL_RTVFS;
    }
    if (pThis->hLock != NIL_RTVFSLOCK)
    {
        RTVfsLockRelease(pThis->hLock);
        pThis->hLock = NIL_RTVFSLOCK;
    }
    RTMemFree(pvToFree);
}


/**
 * Internal object releaser that asserts sanity in strict builds.
 *
 * @returns The new reference count.
 * @param   pcRefs              The reference counter.
 */
DECLINLINE(uint32_t) rtVfsObjRelease(RTVFSOBJINTERNAL *pThis)
{
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p ops=%p %s (%d)\n", cRefs, pThis, pThis->pOps, pThis->pOps->pszName, pThis->pOps->enmType));
    LogFlow(("rtVfsObjRelease(%p/%p) -> %d\n", pThis, pThis->pvThis, cRefs));
    if (cRefs == 0)
        rtVfsObjDestroy(pThis);
    return cRefs;
}


RTDECL(uint32_t) RTVfsObjRelease(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis == NIL_RTVFSOBJ)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(pThis);
}


RTDECL(RTVFSOBJTYPE)    RTVfsObjGetType(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, RTVFSOBJTYPE_INVALID);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, RTVFSOBJTYPE_INVALID);
        return pThis->pOps->enmType;
    }
    return RTVFSOBJTYPE_INVALID;
}


RTDECL(RTVFS)           RTVfsObjToVfs(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFS);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFS);

        if (pThis->pOps->enmType == RTVFSOBJTYPE_VFS)
        {
            rtVfsObjRetainVoid(pThis, "RTVfsObjToVfs");
            LogFlow(("RTVfsObjToVfs(%p) -> %p\n", pThis, RT_FROM_MEMBER(pThis, RTVFSINTERNAL, Base)));
            return RT_FROM_MEMBER(pThis, RTVFSINTERNAL, Base);
        }
    }
    return NIL_RTVFS;
}


RTDECL(RTVFSFSSTREAM)   RTVfsObjToFsStream(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFSFSSTREAM);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSFSSTREAM);

        if (pThis->pOps->enmType == RTVFSOBJTYPE_FS_STREAM)
        {
            rtVfsObjRetainVoid(pThis, "RTVfsObjToFsStream");
            return RT_FROM_MEMBER(pThis, RTVFSFSSTREAMINTERNAL, Base);
        }
    }
    return NIL_RTVFSFSSTREAM;
}


RTDECL(RTVFSDIR)        RTVfsObjToDir(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFSDIR);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSDIR);

        if (pThis->pOps->enmType == RTVFSOBJTYPE_DIR)
        {
            rtVfsObjRetainVoid(pThis, "RTVfsObjToDir");
            return RT_FROM_MEMBER(pThis, RTVFSDIRINTERNAL, Base);
        }
    }
    return NIL_RTVFSDIR;
}


RTDECL(RTVFSIOSTREAM)   RTVfsObjToIoStream(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFSIOSTREAM);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSIOSTREAM);

        if (   pThis->pOps->enmType == RTVFSOBJTYPE_IO_STREAM
            || pThis->pOps->enmType == RTVFSOBJTYPE_FILE)
        {
            rtVfsObjRetainVoid(pThis, "RTVfsObjToIoStream");
            return RT_FROM_MEMBER(pThis, RTVFSIOSTREAMINTERNAL, Base);
        }
    }
    return NIL_RTVFSIOSTREAM;
}


RTDECL(RTVFSFILE)       RTVfsObjToFile(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFSFILE);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSFILE);

        if (pThis->pOps->enmType == RTVFSOBJTYPE_FILE)
        {
            rtVfsObjRetainVoid(pThis, "RTVfsObjToFile");
            return RT_FROM_MEMBER(pThis, RTVFSFILEINTERNAL, Stream.Base);
        }
    }
    return NIL_RTVFSFILE;
}


RTDECL(RTVFSSYMLINK)    RTVfsObjToSymlink(RTVFSOBJ hVfsObj)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    if (pThis != NIL_RTVFSOBJ)
    {
        AssertPtrReturn(pThis, NIL_RTVFSSYMLINK);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSSYMLINK);

        if (pThis->pOps->enmType == RTVFSOBJTYPE_SYMLINK)
        {
            rtVfsObjRetainVoid(pThis, "RTVfsObjToSymlink");
            return RT_FROM_MEMBER(pThis, RTVFSSYMLINKINTERNAL, Base);
        }
    }
    return NIL_RTVFSSYMLINK;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromVfs(RTVFS hVfs)
{
    if (hVfs != NIL_RTVFS)
    {
        RTVFSOBJINTERNAL *pThis = &hVfs->Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis, "RTVfsObjFromVfs");
        LogFlow(("RTVfsObjFromVfs(%p) -> %p\n", hVfs, pThis));
        return pThis;
    }
    return NIL_RTVFSOBJ;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromFsStream(RTVFSFSSTREAM hVfsFss)
{
    if (hVfsFss != NIL_RTVFSFSSTREAM)
    {
        RTVFSOBJINTERNAL *pThis = &hVfsFss->Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis, "RTVfsObjFromFsStream");
        return pThis;
    }
    return NIL_RTVFSOBJ;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromDir(RTVFSDIR hVfsDir)
{
    if (hVfsDir != NIL_RTVFSDIR)
    {
        RTVFSOBJINTERNAL *pThis = &hVfsDir->Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis, "RTVfsObjFromDir");
        return pThis;
    }
    return NIL_RTVFSOBJ;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromIoStream(RTVFSIOSTREAM hVfsIos)
{
    if (hVfsIos != NIL_RTVFSIOSTREAM)
    {
        RTVFSOBJINTERNAL *pThis = &hVfsIos->Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis, "RTVfsObjFromIoStream");
        return pThis;
    }
    return NIL_RTVFSOBJ;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromFile(RTVFSFILE hVfsFile)
{
    if (hVfsFile != NIL_RTVFSFILE)
    {
        RTVFSOBJINTERNAL *pThis = &hVfsFile->Stream.Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis, "RTVfsObjFromFile");
        return pThis;
    }
    return NIL_RTVFSOBJ;
}


RTDECL(RTVFSOBJ)        RTVfsObjFromSymlink(RTVFSSYMLINK hVfsSym)
{
    if (hVfsSym != NIL_RTVFSSYMLINK)
    {
        RTVFSOBJINTERNAL *pThis = &hVfsSym->Base;
        AssertPtrReturn(pThis, NIL_RTVFSOBJ);
        AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, NIL_RTVFSOBJ);

        rtVfsObjRetainVoid(pThis, "RTVfsObjFromSymlink");
        return pThis;
    }
    return NIL_RTVFSOBJ;
}


RTDECL(int)             RTVfsObjOpen(RTVFS hVfs, const char *pszPath, uint64_t fFileOpen, uint32_t fObjFlags, PRTVFSOBJ phVfsObj)
{
    /*
     * Validate input.
     */
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertPtrReturn(phVfsObj, VERR_INVALID_POINTER);

    int rc = rtFileRecalcAndValidateFlags(&fFileOpen);
    if (RT_FAILURE(rc))
        return rc;
    AssertMsgReturn(   RTPATH_F_IS_VALID(fObjFlags, RTVFSOBJ_F_VALID_MASK)
                    && (fObjFlags & RTVFSOBJ_F_CREATE_MASK) <= RTVFSOBJ_F_CREATE_DIRECTORY,
                    ("fObjFlags=%#x\n", fObjFlags),
                    VERR_INVALID_FLAGS);
    /*
     * Parse the path, assume current directory is root since we've got no
     * caller context here.
     */
    PRTVFSPARSEDPATH pPath;
    rc = RTVfsParsePathA(pszPath, "/", &pPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * Tranverse the path, resolving the parent node.
         * We'll do the symbolic link checking here with help of pfnOpen.
         */
        RTVFSDIRINTERNAL *pVfsParentDir;
        rc = rtVfsTraverseToParent(pThis, pPath, (fObjFlags & RTPATH_F_NO_SYMLINKS) | RTPATH_F_ON_LINK, &pVfsParentDir);
        if (RT_SUCCESS(rc))
        {

           /*
             * Do the opening.  Loop if we need to follow symbolic links.
             */
            for (uint32_t cLoops = 1; ; cLoops++)
            {
                /* If we end with a directory slash, adjust open flags. */
                if (pPath->fDirSlash)
                {
                    fObjFlags &= ~RTVFSOBJ_F_OPEN_ANY | RTVFSOBJ_F_OPEN_DIRECTORY;
                    if ((fObjFlags & RTVFSOBJ_F_CREATE_MASK) != RTVFSOBJ_F_CREATE_DIRECTORY)
                        fObjFlags = (fObjFlags & ~RTVFSOBJ_F_CREATE_MASK) | RTVFSOBJ_F_CREATE_NOTHING;
                }
                if (fObjFlags & RTPATH_F_FOLLOW_LINK)
                    fObjFlags |= RTVFSOBJ_F_OPEN_SYMLINK;

                /* Open it. */
                const char *pszEntryName = &pPath->szPath[pPath->aoffComponents[pPath->cComponents - 1]];
                RTVFSOBJ    hVfsObj;
                RTVfsLockAcquireWrite(pVfsParentDir->Base.hLock);
                rc = pVfsParentDir->pOps->pfnOpen(pVfsParentDir->Base.pvThis, pszEntryName, fFileOpen, fObjFlags, &hVfsObj);
                RTVfsLockReleaseWrite(pVfsParentDir->Base.hLock);
                if (RT_FAILURE(rc))
                    break;

                /* We're done if we don't follow links or this wasn't a link. */
                if (   !(fObjFlags & RTPATH_F_FOLLOW_LINK)
                    || RTVfsObjGetType(*phVfsObj) != RTVFSOBJTYPE_SYMLINK)
                {
                    *phVfsObj = hVfsObj;
                    break;
                }

                /* Follow symbolic link. */
                if (cLoops < RTVFS_MAX_LINKS)
                    rc = rtVfsDirFollowSymlinkObjToParent(&pVfsParentDir, hVfsObj, pPath, fObjFlags & RTPATH_F_MASK);
                else
                    rc = VERR_TOO_MANY_SYMLINKS;
                RTVfsObjRelease(hVfsObj);
                if (RT_FAILURE(rc))
                    break;
            }
            RTVfsDirRelease(pVfsParentDir);
        }
        RTVfsParsePathFree(pPath);
    }
    return rc;
}


RTDECL(int)         RTVfsObjQueryInfo(RTVFSOBJ hVfsObj, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, VERR_INVALID_HANDLE);

    RTVfsLockAcquireRead(pThis->hLock);
    int rc = pThis->pOps->pfnQueryInfo(pThis->pvThis, pObjInfo, enmAddAttr);
    RTVfsLockReleaseRead(pThis->hLock);
    return rc;
}


/**
 * Gets the RTVFSOBJSETOPS for the given base object.
 *
 * @returns Pointer to the vtable if supported by the type, otherwise NULL.
 * @param   pThis               The base object.
 */
static PCRTVFSOBJSETOPS rtVfsObjGetSetOps(RTVFSOBJINTERNAL *pThis)
{
    switch (pThis->pOps->enmType)
    {
        case RTVFSOBJTYPE_DIR:
            return &RT_FROM_MEMBER(pThis, RTVFSDIRINTERNAL, Base)->pOps->ObjSet;
        case RTVFSOBJTYPE_FILE:
            return &RT_FROM_MEMBER(pThis, RTVFSFILEINTERNAL, Stream.Base)->pOps->ObjSet;
        case RTVFSOBJTYPE_SYMLINK:
            return &RT_FROM_MEMBER(pThis, RTVFSSYMLINKINTERNAL, Base)->pOps->ObjSet;
        default:
            return NULL;
    }
}


RTDECL(int)         RTVfsObjSetMode(RTVFSOBJ hVfsObj, RTFMODE fMode, RTFMODE fMask)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, VERR_INVALID_HANDLE);

    fMode = rtFsModeNormalize(fMode, NULL, 0, 0);
    if (!rtFsModeIsValid(fMode))
        return VERR_INVALID_PARAMETER;

    PCRTVFSOBJSETOPS pObjSetOps = rtVfsObjGetSetOps(pThis);
    AssertReturn(pObjSetOps, VERR_INVALID_FUNCTION);

    int rc;
    if (pObjSetOps->pfnSetMode)
    {
        RTVfsLockAcquireWrite(pThis->hLock);
        rc = pObjSetOps->pfnSetMode(pThis->pvThis, fMode, fMask);
        RTVfsLockReleaseWrite(pThis->hLock);
    }
    else
        rc = VERR_WRITE_PROTECT;
    return rc;
}


RTDECL(int)         RTVfsObjSetTimes(RTVFSOBJ hVfsObj, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                     PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, VERR_INVALID_HANDLE);

    AssertPtrNullReturn(pAccessTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pModificationTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pChangeTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pBirthTime, VERR_INVALID_POINTER);

    PCRTVFSOBJSETOPS pObjSetOps = rtVfsObjGetSetOps(pThis);
    AssertReturn(pObjSetOps, VERR_INVALID_FUNCTION);

    int rc;
    if (pObjSetOps->pfnSetTimes)
    {
        RTVfsLockAcquireWrite(pThis->hLock);
        rc = pObjSetOps->pfnSetTimes(pThis->pvThis, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
        RTVfsLockReleaseWrite(pThis->hLock);
    }
    else
        rc = VERR_WRITE_PROTECT;
    return rc;
}


RTDECL(int)         RTVfsObjSetOwner(RTVFSOBJ hVfsObj, RTUID uid, RTGID gid)
{
    RTVFSOBJINTERNAL *pThis = hVfsObj;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSOBJ_MAGIC, VERR_INVALID_HANDLE);

    PCRTVFSOBJSETOPS pObjSetOps = rtVfsObjGetSetOps(pThis);
    AssertReturn(pObjSetOps, VERR_INVALID_FUNCTION);

    int rc;
    if (pObjSetOps->pfnSetOwner)
    {
        RTVfsLockAcquireWrite(pThis->hLock);
        rc = pObjSetOps->pfnSetOwner(pThis->pvThis, uid, gid);
        RTVfsLockReleaseWrite(pThis->hLock);
    }
    else
        rc = VERR_WRITE_PROTECT;
    return rc;
}


/*
 *
 *  U T I L   U T I L   U T I L
 *  U T I L   U T I L   U T I L
 *  U T I L   U T I L   U T I L
 *
 */


RTDECL(int) RTVfsParsePathAppend(PRTVFSPARSEDPATH pPath, const char *pszPath, uint16_t *piRestartComp)
{
    AssertReturn(*pszPath != '/' && *pszPath != '\\', VERR_INTERNAL_ERROR_4);

    /* In case *piRestartComp was set higher than the number of components
       before making the call to this function. */
    if (piRestartComp && *piRestartComp + 1 >= pPath->cComponents)
        *piRestartComp = pPath->cComponents > 0 ? pPath->cComponents - 1 : 0;

/** @todo The '..' handling doesn't really work wrt to symbolic links in the
 *        path.  */

    /*
     * Append a slash to the destination path if necessary.
     */
    char * const pszDst         = pPath->szPath;
    size_t       offDst         = pPath->cch;
    if (pPath->cComponents > 0)
    {
        pszDst[offDst++] = '/';
        if (offDst >= RTVFSPARSEDPATH_MAX)
            return VERR_FILENAME_TOO_LONG;
    }
    if (pPath->fAbsolute)
        Assert(pszDst[offDst - 1] == '/' && pszDst[0] == '/');
    else
        Assert(offDst == 0 || (pszDst[0] != '/' && pszDst[offDst - 1] == '/'));

    /*
     * Parse and append the relative path.
     */
    const char *pszSrc = pszPath;
    pPath->fDirSlash   = false;
    for (;;)
    {
        /* Copy until we encounter the next slash. */
        pPath->aoffComponents[pPath->cComponents++] = (uint16_t)offDst;
        for (;;)
        {
            char ch = *pszSrc++;
            if (   ch != '/'
                && ch != '\\'
                && ch != '\0')
            {
                pszDst[offDst++] = ch;
                if (offDst < RTVFSPARSEDPATH_MAX)
                { /* likely */ }
                else
                    return VERR_FILENAME_TOO_LONG;
            }
            else
            {
                /* Deal with dot components before we processes the slash/end. */
                if (pszDst[offDst - 1] == '.')
                {
                    if (   offDst == 1
                        || pszDst[offDst - 2] == '/')
                    {
                        pPath->cComponents--;
                        offDst = pPath->aoffComponents[pPath->cComponents];
                    }
                    else if (   offDst > 3
                             && pszDst[offDst - 2] == '.'
                             && pszDst[offDst - 3] == '/')
                    {
                        if (   pPath->fAbsolute
                            || offDst < 5
                            || pszDst[offDst - 4] != '.'
                            || pszDst[offDst - 5] != '.'
                            || (offDst >= 6 && pszDst[offDst - 6] != '/') )
                        {
                            pPath->cComponents -= pPath->cComponents > 1 ? 2 : 1;
                            offDst = pPath->aoffComponents[pPath->cComponents];
                            if (piRestartComp && *piRestartComp + 1 >= pPath->cComponents)
                                *piRestartComp = pPath->cComponents > 0 ? pPath->cComponents - 1 : 0;
                        }
                    }
                }

                if (ch != '\0')
                {
                    /* Skip unnecessary slashes and check for end of path. */
                    while ((ch = *pszSrc) == '/' || ch == '\\')
                        pszSrc++;

                    if (ch == '\0')
                        pPath->fDirSlash = true;
                }

                if (ch == '\0')
                {
                    /* Drop trailing slash unless it's the root slash. */
                    if (   offDst > 0
                        && pszDst[offDst - 1] == '/'
                        && (   !pPath->fAbsolute
                            || offDst > 1))
                        offDst--;

                    /* Terminate the string and enter its length. */
                    pszDst[offDst]     = '\0';
                    pszDst[offDst + 1] = '\0'; /* for aoffComponents[pPath->cComponents] */
                    pPath->cch = (uint16_t)offDst;
                    pPath->aoffComponents[pPath->cComponents] = (uint16_t)(offDst + 1);
                    return VINF_SUCCESS;
                }

                /* Append component separator before continuing with the next component. */
                if (offDst > 0 && pszDst[offDst - 1] != '/')
                    pszDst[offDst++] = '/';
                if (offDst >= RTVFSPARSEDPATH_MAX)
                    return VERR_FILENAME_TOO_LONG;
                break;
            }
        }
    }
}


/** @todo Replace RTVfsParsePath with RTPathParse and friends?  */
RTDECL(int) RTVfsParsePath(PRTVFSPARSEDPATH pPath, const char *pszPath, const char *pszCwd)
{
    if (*pszPath != '/' && *pszPath != '\\')
    {
        if (pszCwd)
        {
            /*
             * Relative with a CWD.
             */
            int rc = RTVfsParsePath(pPath, pszCwd, NULL /*crash if pszCwd is not absolute*/);
            if (RT_FAILURE(rc))
                return rc;
        }
        else
        {
            /*
             * Relative.
             */
            pPath->cch               = 0;
            pPath->cComponents       = 0;
            pPath->fDirSlash         = false;
            pPath->fAbsolute         = false;
            pPath->aoffComponents[0] = 0;
            pPath->aoffComponents[1] = 1;
            pPath->szPath[0]         = '\0';
            pPath->szPath[1]         = '\0';
        }
    }
    else
    {
        /*
         * Make pszPath relative, i.e. set up pPath for the root and skip
         * leading slashes in pszPath before appending it.
         */
        pPath->cch               = 1;
        pPath->cComponents       = 0;
        pPath->fDirSlash         = false;
        pPath->fAbsolute         = true;
        pPath->aoffComponents[0] = 1;
        pPath->aoffComponents[1] = 2;
        pPath->szPath[0]         = '/';
        pPath->szPath[1]         = '\0';
        pPath->szPath[2]         = '\0';
        while (pszPath[0] == '/' || pszPath[0] == '\\')
            pszPath++;
        if (!pszPath[0])
            return VINF_SUCCESS;
    }
    return RTVfsParsePathAppend(pPath, pszPath, NULL);
}



RTDECL(int) RTVfsParsePathA(const char *pszPath, const char *pszCwd, PRTVFSPARSEDPATH *ppPath)
{
    /*
     * Allocate the output buffer and hand the problem to rtVfsParsePath.
     */
    int rc;
    PRTVFSPARSEDPATH pPath = (PRTVFSPARSEDPATH)RTMemTmpAlloc(sizeof(RTVFSPARSEDPATH));
    if (pPath)
    {
        rc = RTVfsParsePath(pPath, pszPath, pszCwd);
        if (RT_FAILURE(rc))
        {
            RTMemTmpFree(pPath);
            pPath = NULL;
        }
    }
    else
        rc = VERR_NO_TMP_MEMORY;
    *ppPath = pPath;                    /* always set it */
    return rc;
}


RTDECL(void) RTVfsParsePathFree(PRTVFSPARSEDPATH pPath)
{
    if (pPath)
    {
        pPath->cch               = UINT16_MAX;
        pPath->cComponents       = UINT16_MAX;
        pPath->aoffComponents[0] = UINT16_MAX;
        pPath->aoffComponents[1] = UINT16_MAX;
        RTMemTmpFree(pPath);
    }
}


/**
 * Handles a symbolic link, adding it to
 *
 * @returns IPRT status code.
 * @param   ppCurDir            The current directory variable. We change it if
 *                              the symbolic links is absolute.
 * @param   pPath               The parsed path to update.
 * @param   iPathComponent      The current path component.
 * @param   hSymlink            The symbolic link to process.
 */
static int rtVfsTraverseHandleSymlink(RTVFSDIRINTERNAL **ppCurDir, PRTVFSPARSEDPATH pPath,
                                      uint16_t iPathComponent, RTVFSSYMLINK hSymlink)
{
    /*
     * Read the link and append the trailing path to it.
     */
    char szPath[RTPATH_MAX];
    int rc = RTVfsSymlinkRead(hSymlink, szPath, sizeof(szPath) - 1);
    if (RT_SUCCESS(rc))
    {
        szPath[sizeof(szPath) - 1] = '\0';
        if (iPathComponent + 1 < pPath->cComponents)
            rc = RTPathAppend(szPath, sizeof(szPath), &pPath->szPath[pPath->aoffComponents[iPathComponent + 1]]);
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Special hack help vfsstddir.cpp deal with symbolic links.
         */
        RTVFSDIRINTERNAL *pCurDir = *ppCurDir;
        char             *pszPath = szPath;
        if (pCurDir->pOps->pfnFollowAbsoluteSymlink)
        {
            size_t cchRoot = rtPathRootSpecLen(szPath);
            if (cchRoot > 0)
            {
                pszPath = &szPath[cchRoot];
                char const chSaved = *pszPath;
                *pszPath = '\0';
                RTVFSDIRINTERNAL *pVfsRootDir;
                RTVfsLockAcquireWrite(pCurDir->Base.hLock);
                rc = pCurDir->pOps->pfnFollowAbsoluteSymlink(pCurDir, szPath, &pVfsRootDir);
                RTVfsLockAcquireWrite(pCurDir->Base.hLock);
                *pszPath = chSaved;
                if (RT_SUCCESS(rc))
                {
                    RTVfsDirRelease(pCurDir);
                    *ppCurDir = pCurDir = pVfsRootDir;
                }
                else if (rc == VERR_PATH_IS_RELATIVE)
                    pszPath = szPath;
                else
                    return rc;
            }
        }

        rc = RTVfsParsePath(pPath, pszPath, NULL);
        if (RT_SUCCESS(rc))
        {
            /*
             * Deal with absolute references in a VFS setup.
             * Note! The current approach only correctly handles this on root volumes.
             */
            if (   pPath->fAbsolute
                && pCurDir->Base.hVfs != NIL_RTVFS) /** @todo This needs fixing once we implement mount points. */
            {
                RTVFSINTERNAL    *pVfs = pCurDir->Base.hVfs;
                RTVFSDIRINTERNAL *pVfsRootDir;
                RTVfsLockAcquireRead(pVfs->Base.hLock);
                rc = pVfs->pOps->pfnOpenRoot(pVfs->Base.pvThis, &pVfsRootDir);
                RTVfsLockReleaseRead(pVfs->Base.hLock);
                if (RT_SUCCESS(rc))
                {
                    RTVfsDirRelease(pCurDir);
                    *ppCurDir = pCurDir = pVfsRootDir;
                }
                else
                    return rc;
            }
        }
    }
    else if (rc == VERR_BUFFER_OVERFLOW)
        rc = VERR_FILENAME_TOO_LONG;
    return rc == VERR_BUFFER_OVERFLOW ? VERR_FILENAME_TOO_LONG : rc;
}


/**
 * Internal worker for various open functions as well as RTVfsTraverseToParent.
 *
 *
 * @returns IPRT status code.
 * @param   pThis           The VFS.
 * @param   pPath           The parsed path.  This may be changed as symbolic
 *                          links are processed during the path traversal.  If
 *                          it contains zero components, a dummy component is
 *                          added to assist the caller.
 * @param   fFlags          RTPATH_F_XXX.
 * @param   ppVfsParentDir  Where to return the parent directory handle
 *                          (referenced).
 */
static int rtVfsDirTraverseToParent(RTVFSDIRINTERNAL *pThis, PRTVFSPARSEDPATH pPath, uint32_t fFlags,
                                    RTVFSDIRINTERNAL **ppVfsParentDir)
{
    /*
     * Assert sanity.
     */
    AssertPtr(pThis);
    Assert(pThis->uMagic == RTVFSDIR_MAGIC);
    Assert(pThis->Base.cRefs > 0);
    AssertPtr(pPath);
    AssertPtr(ppVfsParentDir);
    *ppVfsParentDir = NULL;
    Assert(RTPATH_F_IS_VALID(fFlags, 0));

    /*
     * Start with the pThis directory.
     */
    if (RTVfsDirRetain(pThis) == UINT32_MAX)
        return VERR_INVALID_HANDLE;
    RTVFSDIRINTERNAL *pCurDir = pThis;

    /*
     * Special case for traversing zero components.
     * We fake up a "./" in the pPath to help the caller along.
     */
    if (pPath->cComponents == 0)
    {
        pPath->fDirSlash         = true;
        pPath->szPath[0]         = '.';
        pPath->szPath[1]         = '\0';
        pPath->szPath[2]         = '\0';
        pPath->cch               = 1;
        pPath->cComponents       = 1;
        pPath->aoffComponents[0] = 0;
        pPath->aoffComponents[1] = 1;
        pPath->aoffComponents[2] = 1;

        *ppVfsParentDir = pCurDir;
        return VINF_SUCCESS;
    }


    /*
     * The traversal loop.
     */
    int      rc         = VINF_SUCCESS;
    unsigned cLinks     = 0;
    uint16_t iComponent = 0;
    for (;;)
    {
        /*
         * Are we done yet?
         */
        bool fFinal = iComponent + 1 >= pPath->cComponents;
        if (fFinal && (fFlags & RTPATH_F_ON_LINK))
        {
            *ppVfsParentDir = pCurDir;
            return VINF_SUCCESS;
        }

        /*
         * Try open the next entry.
         */
        const char     *pszEntry    = &pPath->szPath[pPath->aoffComponents[iComponent]];
        char           *pszEntryEnd = &pPath->szPath[pPath->aoffComponents[iComponent + 1] - 1];
        *pszEntryEnd = '\0';
        RTVFSDIR        hDir     = NIL_RTVFSDIR;
        RTVFSSYMLINK    hSymlink = NIL_RTVFSSYMLINK;
        RTVFS           hVfsMnt  = NIL_RTVFS;
        RTVFSOBJ        hVfsObj  = NIL_RTVFSOBJ;
        if (fFinal)
        {
            RTVfsLockAcquireRead(pCurDir->Base.hLock);
            rc = pCurDir->pOps->pfnOpen(pCurDir->Base.pvThis, pszEntry,
                                        RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE,
                                        RTVFSOBJ_F_OPEN_SYMLINK | RTVFSOBJ_F_CREATE_NOTHING
                                        | RTVFSOBJ_F_TRAVERSAL | RTPATH_F_ON_LINK,
                                        &hVfsObj);
            RTVfsLockReleaseRead(pCurDir->Base.hLock);
            *pszEntryEnd = '\0';
            if (RT_FAILURE(rc))
            {
                if (   rc == VERR_PATH_NOT_FOUND
                    || rc == VERR_FILE_NOT_FOUND
                    || rc == VERR_IS_A_DIRECTORY
                    || rc == VERR_IS_A_FILE
                    || rc == VERR_IS_A_FIFO
                    || rc == VERR_IS_A_SOCKET
                    || rc == VERR_IS_A_CHAR_DEVICE
                    || rc == VERR_IS_A_BLOCK_DEVICE
                    || rc == VERR_NOT_SYMLINK)
                {
                    *ppVfsParentDir = pCurDir;
                    return VINF_SUCCESS;
                }
                break;
            }
            hSymlink = RTVfsObjToSymlink(hVfsObj);
            Assert(hSymlink != NIL_RTVFSSYMLINK);
        }
        else
        {
            RTVfsLockAcquireRead(pCurDir->Base.hLock);
            rc = pCurDir->pOps->pfnOpen(pCurDir->Base.pvThis, pszEntry,
                                        RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE,
                                        RTVFSOBJ_F_OPEN_DIRECTORY   | RTVFSOBJ_F_OPEN_SYMLINK | RTVFSOBJ_F_OPEN_MOUNT
                                        | RTVFSOBJ_F_CREATE_NOTHING | RTVFSOBJ_F_TRAVERSAL    | RTPATH_F_ON_LINK,
                                        &hVfsObj);
            RTVfsLockReleaseRead(pCurDir->Base.hLock);
            *pszEntryEnd = '/';
            if (RT_FAILURE(rc))
            {
                if (rc == VERR_FILE_NOT_FOUND)
                    rc = VERR_PATH_NOT_FOUND;
                break;
            }
            hDir     = RTVfsObjToDir(hVfsObj);
            hSymlink = RTVfsObjToSymlink(hVfsObj);
            hVfsMnt  = RTVfsObjToVfs(hVfsObj);
        }
        Assert(   (hDir != NIL_RTVFSDIR && hSymlink == NIL_RTVFSSYMLINK && hVfsMnt == NIL_RTVFS)
               || (hDir == NIL_RTVFSDIR && hSymlink != NIL_RTVFSSYMLINK && hVfsMnt == NIL_RTVFS)
               || (hDir == NIL_RTVFSDIR && hSymlink == NIL_RTVFSSYMLINK && hVfsMnt != NIL_RTVFS));
        RTVfsObjRelease(hVfsObj);

        if (hDir != NIL_RTVFSDIR)
        {
            /*
             * Directory - advance down the path.
             */
            AssertPtr(hDir);
            Assert(hDir->uMagic == RTVFSDIR_MAGIC);
            RTVfsDirRelease(pCurDir);
            pCurDir = hDir;
            iComponent++;
        }
        else if (hSymlink != NIL_RTVFSSYMLINK)
        {
            /*
             * Symbolic link - deal with it and retry the current component.
             */
            AssertPtr(hSymlink);
            Assert(hSymlink->uMagic == RTVFSSYMLINK_MAGIC);
            if (fFlags & RTPATH_F_NO_SYMLINKS)
            {
                rc = VERR_SYMLINK_NOT_ALLOWED;
                break;
            }
            cLinks++;
            if (cLinks >= RTVFS_MAX_LINKS)
            {
                rc = VERR_TOO_MANY_SYMLINKS;
                break;
            }
            rc = rtVfsTraverseHandleSymlink(&pCurDir, pPath, iComponent, hSymlink);
            if (RT_FAILURE(rc))
                break;
            iComponent = 0;
        }
        else
        {
            /*
             * Mount point - deal with it and retry the current component.
             */
            RTVfsDirRelease(pCurDir);
            RTVfsLockAcquireRead(hVfsMnt->Base.hLock);
            rc = hVfsMnt->pOps->pfnOpenRoot(hVfsMnt->Base.pvThis, &pCurDir);
            RTVfsLockReleaseRead(hVfsMnt->Base.hLock);
            if (RT_FAILURE(rc))
            {
                pCurDir = NULL;
                break;
            }
            iComponent = 0;
            /** @todo union mounts. */
        }
    }

    if (pCurDir)
        RTVfsDirRelease(pCurDir);

    return rc;
}


/**
 * Internal worker for various open functions as well as RTVfsTraverseToParent.
 *
 * @returns IPRT status code.
 * @param   pThis           The VFS.
 * @param   pPath           The parsed path.  This may be changed as symbolic
 *                          links are processed during the path traversal.
 * @param   fFlags          RTPATH_F_XXX.
 * @param   ppVfsParentDir  Where to return the parent directory handle
 *                          (referenced).
 */
static int rtVfsTraverseToParent(RTVFSINTERNAL *pThis, PRTVFSPARSEDPATH pPath, uint32_t fFlags, RTVFSDIRINTERNAL **ppVfsParentDir)
{
    /*
     * Assert sanity.
     */
    AssertPtr(pThis);
    Assert(pThis->uMagic == RTVFS_MAGIC);
    Assert(pThis->Base.cRefs > 0);
    AssertPtr(pPath);
    AssertPtr(ppVfsParentDir);
    *ppVfsParentDir = NULL;
    Assert(RTPATH_F_IS_VALID(fFlags, 0));

    /*
     * Open the root directory and join paths with the directory traversal.
     */
    /** @todo Union mounts, traversal optimization methods, races, ++ */
    RTVFSDIRINTERNAL *pRootDir;
    RTVfsLockAcquireRead(pThis->Base.hLock);
    int rc = pThis->pOps->pfnOpenRoot(pThis->Base.pvThis, &pRootDir);
    RTVfsLockReleaseRead(pThis->Base.hLock);
    if (RT_SUCCESS(rc))
    {
        rc = rtVfsDirTraverseToParent(pRootDir, pPath, fFlags, ppVfsParentDir);
        RTVfsDirRelease(pRootDir);
    }
    return rc;
}



/**
 * Follows a symbolic link object to the next parent directory.
 *
 * @returns IPRT status code
 * @param   ppVfsParentDir  Pointer to the parent directory of @a hVfsObj on
 *                          input, the parent directory of the link target on
 *                          return.
 * @param   hVfsObj         Symbolic link object handle.
 * @param   pPath           Path buffer to use parse the symbolic link target.
 * @param   fFlags          See rtVfsDirTraverseToParent.
 */
static int rtVfsDirFollowSymlinkObjToParent(RTVFSDIRINTERNAL **ppVfsParentDir, RTVFSOBJ hVfsObj,
                                            PRTVFSPARSEDPATH pPath, uint32_t fFlags)
{
    RTVFSSYMLINK hVfsSymlink = RTVfsObjToSymlink(hVfsObj);
    AssertReturn(hVfsSymlink != NIL_RTVFSSYMLINK, VERR_INTERNAL_ERROR_3);

    int rc = rtVfsTraverseHandleSymlink(ppVfsParentDir, pPath, pPath->cComponents, hVfsSymlink);
    if (RT_SUCCESS(rc))
    {
        RTVFSDIRINTERNAL *pVfsStartDir = *ppVfsParentDir;
        rc = rtVfsDirTraverseToParent(pVfsStartDir, pPath, fFlags, ppVfsParentDir);
        RTVfsDirRelease(pVfsStartDir);
    }

    RTVfsSymlinkRelease(hVfsSymlink);
    return rc;
}



RTDECL(int) RTVfsUtilDummyPollOne(uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr, uint32_t *pfRetEvents)
{
    NOREF(fEvents);
    int rc;
    if (fIntr)
        rc = RTThreadSleep(cMillies);
    else
    {
        uint64_t uMsStart = RTTimeMilliTS();
        do
            rc = RTThreadSleep(cMillies);
        while (   rc == VERR_INTERRUPTED
               && RTTimeMilliTS() - uMsStart < cMillies);
        if (rc == VERR_INTERRUPTED)
            rc = VERR_TIMEOUT;
    }

    *pfRetEvents = 0;
    return rc;
}


RTDECL(int) RTVfsUtilPumpIoStreams(RTVFSIOSTREAM hVfsIosSrc, RTVFSIOSTREAM hVfsIosDst, size_t cbBufHint)
{
    /*
     * Allocate a temporary buffer.
     */
    size_t cbBuf = cbBufHint;
    if (!cbBuf)
        cbBuf = _64K;
    else if (cbBuf < _4K)
        cbBuf = _4K;
    else if (cbBuf > _1M)
        cbBuf = _1M;

    void *pvBuf = RTMemTmpAlloc(cbBuf);
    if (!pvBuf)
    {
        cbBuf = _4K;
        pvBuf = RTMemTmpAlloc(cbBuf);
        if (!pvBuf)
            return VERR_NO_TMP_MEMORY;
    }

    /*
     * Pump loop.
     */
    int rc;
    for (;;)
    {
        size_t cbRead;
        rc = RTVfsIoStrmRead(hVfsIosSrc, pvBuf, cbBuf, true /*fBlocking*/, &cbRead);
        if (RT_FAILURE(rc))
            break;
        if (rc == VINF_EOF && cbRead == 0)
            break;

        rc = RTVfsIoStrmWrite(hVfsIosDst, pvBuf, cbRead, true /*fBlocking*/, NULL /*cbWritten*/);
        if (RT_FAILURE(rc))
            break;
    }

    RTMemTmpFree(pvBuf);

    /*
     * Flush the destination stream on success to make sure we've caught
     * errors caused by buffering delays.
     */
    if (RT_SUCCESS(rc))
        rc = RTVfsIoStrmFlush(hVfsIosDst);

    return rc;
}





/*
 * F I L E S Y S T E M   R O O T
 * F I L E S Y S T E M   R O O T
 * F I L E S Y S T E M   R O O T
 */


RTDECL(int) RTVfsNew(PCRTVFSOPS pVfsOps, size_t cbInstance, RTVFS hVfs, RTVFSLOCK hLock,
                     PRTVFS phVfs, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pVfsOps);
    AssertReturn(pVfsOps->uVersion   == RTVFSOPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pVfsOps->uEndMarker == RTVFSOPS_VERSION, VERR_VERSION_MISMATCH);
    RTVFSOBJ_ASSERT_OPS(&pVfsOps->Obj, RTVFSOBJTYPE_VFS);
    Assert(cbInstance > 0);
    AssertPtr(ppvInstance);
    AssertPtr(phVfs);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSINTERNAL *pThis = (RTVFSINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(&pThis->Base, &pVfsOps->Obj, hVfs, false /*fNoVfsRef*/, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->uMagic = RTVFS_MAGIC;
    pThis->pOps   = pVfsOps;

    *phVfs       = pThis;
    *ppvInstance = pThis->Base.pvThis;

    LogFlow(("RTVfsNew -> VINF_SUCCESS; hVfs=%p pvThis=%p\n", pThis, pThis->Base.pvThis));
    return VINF_SUCCESS;
}

#ifdef DEBUG
# undef RTVfsRetain
#endif
RTDECL(uint32_t) RTVfsRetain(RTVFS hVfs)
{
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, UINT32_MAX);
    uint32_t cRefs = rtVfsObjRetain(&pThis->Base);
    LogFlow(("RTVfsRetain(%p/%p) -> %d\n", pThis, pThis->Base.pvThis, cRefs));
    return cRefs;
}
#ifdef DEBUG
# define RTVfsRetain(hVfs)          RTVfsRetainDebug(hVfs, RT_SRC_POS)
#endif


RTDECL(uint32_t) RTVfsRetainDebug(RTVFS hVfs, RT_SRC_POS_DECL)
{
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, UINT32_MAX);
    RT_SRC_POS_NOREF();
    return rtVfsObjRetainDebug(&pThis->Base, "RTVfsRetainDebug", RT_SRC_POS_ARGS);
}


RTDECL(uint32_t) RTVfsRelease(RTVFS hVfs)
{
    RTVFSINTERNAL *pThis = hVfs;
    if (pThis == NIL_RTVFS)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, UINT32_MAX);
#ifdef LOG_ENABLED
    void *pvThis = pThis->Base.pvThis;
#endif
    uint32_t cRefs = rtVfsObjRelease(&pThis->Base);
    Log(("RTVfsRelease(%p/%p) -> %d\n", pThis, pvThis, cRefs));
    return cRefs;
}


RTDECL(int) RTVfsOpenRoot(RTVFS hVfs, PRTVFSDIR phDir)
{
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(phDir, VERR_INVALID_POINTER);
    *phDir = NIL_RTVFSDIR;

    if (!pThis->pOps->pfnOpenRoot)
        return VERR_NOT_SUPPORTED;
    RTVfsLockAcquireRead(pThis->Base.hLock);
    int rc = pThis->pOps->pfnOpenRoot(pThis->Base.pvThis, phDir);
    RTVfsLockReleaseRead(pThis->Base.hLock);

    return rc;
}


RTDECL(int) RTVfsQueryPathInfo(RTVFS hVfs, const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr, uint32_t fFlags)
{
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
    AssertReturn(enmAddAttr >= RTFSOBJATTRADD_NOTHING &&  enmAddAttr <= RTFSOBJATTRADD_LAST, VERR_INVALID_PARAMETER);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Parse the path, assume current directory is root since we've got no
     * caller context here.  Then traverse to the parent directory.
     */
    PRTVFSPARSEDPATH pPath;
    int rc = RTVfsParsePathA(pszPath, "/", &pPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * Tranverse the path, resolving the parent node.
         * We'll do the symbolic link checking here with help of pfnOpen/pfnQueryEntryInfo.
         */
        RTVFSDIRINTERNAL *pVfsParentDir;
        rc = rtVfsTraverseToParent(pThis, pPath, (fFlags & RTPATH_F_NO_SYMLINKS) | RTPATH_F_ON_LINK, &pVfsParentDir);
        if (RT_SUCCESS(rc))
        {
            /*
             * Do the opening.  Loop if we need to follow symbolic links.
             */
            uint32_t fObjFlags = RTVFSOBJ_F_OPEN_ANY | RTVFSOBJ_F_CREATE_NOTHING;
            for (uint32_t cLoops = 1; ; cLoops++)
            {
                /* If we end with a directory slash, adjust open flags. */
                if (pPath->fDirSlash)
                {
                    fObjFlags &= ~RTVFSOBJ_F_OPEN_ANY | RTVFSOBJ_F_OPEN_DIRECTORY;
                    if ((fObjFlags & RTVFSOBJ_F_CREATE_MASK) != RTVFSOBJ_F_CREATE_DIRECTORY)
                        fObjFlags = (fObjFlags & ~RTVFSOBJ_F_CREATE_MASK) | RTVFSOBJ_F_CREATE_NOTHING;
                }
                if (fObjFlags & RTPATH_F_FOLLOW_LINK)
                    fObjFlags |= RTVFSOBJ_F_OPEN_SYMLINK;

                /* Do the querying.  If pfnQueryEntryInfo is available, we use it first,
                   falling back on pfnOpen in case of symbolic links that needs following. */
                const char *pszEntryName = &pPath->szPath[pPath->aoffComponents[pPath->cComponents - 1]];
                if (pVfsParentDir->pOps->pfnQueryEntryInfo)
                {
                    RTVfsLockAcquireRead(pVfsParentDir->Base.hLock);
                    rc = pVfsParentDir->pOps->pfnQueryEntryInfo(pVfsParentDir->Base.pvThis, pszEntryName, pObjInfo, enmAddAttr);
                    RTVfsLockReleaseRead(pVfsParentDir->Base.hLock);
                    if (RT_FAILURE(rc))
                        break;
                    if (   !RTFS_IS_SYMLINK(pObjInfo->Attr.fMode)
                        || !(fFlags & RTPATH_F_FOLLOW_LINK))
                    {
                        if (   (fObjFlags & RTVFSOBJ_F_OPEN_MASK) != RTVFSOBJ_F_OPEN_ANY
                            && !RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode))
                            rc = VERR_NOT_A_DIRECTORY;
                        break;
                    }
                }

                RTVFSOBJ hVfsObj;
                RTVfsLockAcquireWrite(pVfsParentDir->Base.hLock);
                rc = pVfsParentDir->pOps->pfnOpen(pVfsParentDir->Base.pvThis, pszEntryName,
                                                  RTFILE_O_ACCESS_ATTR_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                                  fObjFlags, &hVfsObj);
                RTVfsLockReleaseWrite(pVfsParentDir->Base.hLock);
                if (RT_FAILURE(rc))
                    break;

                /* If we don't follow links or this wasn't a link we just have to do the query and we're done. */
                if (   !(fObjFlags & RTPATH_F_FOLLOW_LINK)
                    || RTVfsObjGetType(hVfsObj) != RTVFSOBJTYPE_SYMLINK)
                {
                    rc = RTVfsObjQueryInfo(hVfsObj, pObjInfo, enmAddAttr);
                    RTVfsObjRelease(hVfsObj);
                    break;
                }

                /* Follow symbolic link. */
                if (cLoops < RTVFS_MAX_LINKS)
                    rc = rtVfsDirFollowSymlinkObjToParent(&pVfsParentDir, hVfsObj, pPath, fObjFlags & RTPATH_F_MASK);
                else
                    rc = VERR_TOO_MANY_SYMLINKS;
                RTVfsObjRelease(hVfsObj);
                if (RT_FAILURE(rc))
                    break;
            }
            RTVfsDirRelease(pVfsParentDir);
        }
        RTVfsParsePathFree(pPath);
    }
    return rc;
}



RTDECL(int) RTVfsQueryRangeState(RTVFS hVfs, uint64_t off, size_t cb, bool *pfUsed)
{
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, VERR_INVALID_HANDLE);

    if (!pThis->pOps->pfnQueryRangeState)
        return VERR_NOT_SUPPORTED;
    RTVfsLockAcquireRead(pThis->Base.hLock);
    int rc = pThis->pOps->pfnQueryRangeState(pThis->Base.pvThis, off, cb, pfUsed);
    RTVfsLockReleaseRead(pThis->Base.hLock);

    return rc;
}


RTDECL(int) RTVfsQueryLabel(RTVFS hVfs, bool fAlternative, char *pszLabel, size_t cbLabel, size_t *pcbActual)
{
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, VERR_INVALID_HANDLE);

    if (cbLabel > 0)
        AssertPtrReturn(pszLabel, VERR_INVALID_POINTER);

    int rc;
    if (pThis->pOps->Obj.pfnQueryInfoEx)
    {
        size_t cbActualIgn;
        if (!pcbActual)
            pcbActual = &cbActualIgn;

        RTVfsLockAcquireRead(pThis->Base.hLock);
        rc = pThis->pOps->Obj.pfnQueryInfoEx(pThis->Base.pvThis, !fAlternative ? RTVFSQIEX_VOL_LABEL : RTVFSQIEX_VOL_LABEL_ALT,
                                             pszLabel, cbLabel, pcbActual);
        RTVfsLockReleaseRead(pThis->Base.hLock);
    }
    else
        rc = VERR_NOT_SUPPORTED;
    return rc;
}



/*
 *
 *  F I L E S Y S T E M   S T R E A M
 *  F I L E S Y S T E M   S T R E A M
 *  F I L E S Y S T E M   S T R E A M
 *
 */


RTDECL(int) RTVfsNewFsStream(PCRTVFSFSSTREAMOPS pFsStreamOps, size_t cbInstance, RTVFS hVfs, RTVFSLOCK hLock, uint32_t fAccess,
                             PRTVFSFSSTREAM phVfsFss, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pFsStreamOps);
    AssertReturn(pFsStreamOps->uVersion   == RTVFSFSSTREAMOPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pFsStreamOps->uEndMarker == RTVFSFSSTREAMOPS_VERSION, VERR_VERSION_MISMATCH);
    Assert(!pFsStreamOps->fReserved);
    RTVFSOBJ_ASSERT_OPS(&pFsStreamOps->Obj, RTVFSOBJTYPE_FS_STREAM);
    Assert((fAccess & (RTFILE_O_READ | RTFILE_O_WRITE)) == fAccess);
    Assert(fAccess);
    if (fAccess & RTFILE_O_READ)
        AssertPtr(pFsStreamOps->pfnNext);
    if (fAccess & RTFILE_O_WRITE)
    {
        AssertPtr(pFsStreamOps->pfnAdd);
        AssertPtr(pFsStreamOps->pfnEnd);
    }
    Assert(cbInstance > 0);
    AssertPtr(ppvInstance);
    AssertPtr(phVfsFss);
    RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, VERR_INVALID_HANDLE);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSFSSTREAMINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSFSSTREAMINTERNAL *pThis = (RTVFSFSSTREAMINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(&pThis->Base, &pFsStreamOps->Obj, hVfs, false /*fNoVfsRef*/, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));

    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->uMagic = RTVFSFSSTREAM_MAGIC;
    pThis->pOps   = pFsStreamOps;
    pThis->fFlags = fAccess;
    if (fAccess == RTFILE_O_READ)
        pThis->fFlags |= RTFILE_O_OPEN   | RTFILE_O_DENY_NONE;
    else if (fAccess == RTFILE_O_WRITE)
        pThis->fFlags |= RTFILE_O_CREATE | RTFILE_O_DENY_ALL;
    else
        pThis->fFlags |= RTFILE_O_OPEN   | RTFILE_O_DENY_ALL;

    *phVfsFss     = pThis;
    *ppvInstance  = pThis->Base.pvThis;
    return VINF_SUCCESS;
}


#ifdef DEBUG
# undef RTVfsFsStrmRetain
#endif
RTDECL(uint32_t)    RTVfsFsStrmRetain(RTVFSFSSTREAM hVfsFss)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, UINT32_MAX);
    return rtVfsObjRetain(&pThis->Base);
}
#ifdef DEBUG
# define RTVfsFsStrmRetain(hVfsFss) RTVfsFsStrmRetainDebug(hVfsFss, RT_SRC_POS)
#endif


RTDECL(uint32_t)    RTVfsFsStrmRetainDebug(RTVFSFSSTREAM hVfsFss, RT_SRC_POS_DECL)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, UINT32_MAX);
    return rtVfsObjRetainDebug(&pThis->Base, "RTVfsFsStrmRetain", RT_SRC_POS_ARGS);
}


RTDECL(uint32_t)    RTVfsFsStrmRelease(RTVFSFSSTREAM hVfsFss)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    if (pThis == NIL_RTVFSFSSTREAM)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(&pThis->Base);
}


RTDECL(int)         RTVfsFsStrmQueryInfo(RTVFSFSSTREAM hVfsFss, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsObjQueryInfo(&pThis->Base, pObjInfo, enmAddAttr);
}


RTDECL(int)         RTVfsFsStrmNext(RTVFSFSSTREAM hVfsFss, char **ppszName, RTVFSOBJTYPE *penmType, PRTVFSOBJ phVfsObj)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrNullReturn(ppszName, VERR_INVALID_POINTER);
    if (ppszName)
        *ppszName = NULL;
    AssertPtrNullReturn(penmType, VERR_INVALID_POINTER);
    if (penmType)
        *penmType = RTVFSOBJTYPE_INVALID;
    AssertPtrNullReturn(penmType, VERR_INVALID_POINTER);
    if (phVfsObj)
        *phVfsObj = NIL_RTVFSOBJ;

    AssertReturn(pThis->fFlags & RTFILE_O_READ, VERR_INVALID_FUNCTION);

    return pThis->pOps->pfnNext(pThis->Base.pvThis, ppszName, penmType, phVfsObj);
}


RTDECL(int)         RTVfsFsStrmAdd(RTVFSFSSTREAM hVfsFss, const char *pszPath, RTVFSOBJ hVfsObj, uint32_t fFlags)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath != '\0', VERR_INVALID_NAME);
    AssertPtrReturn(hVfsObj, VERR_INVALID_HANDLE);
    AssertReturn(hVfsObj->uMagic == RTVFSOBJ_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!(fFlags & ~RTVFSFSSTRM_ADD_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(pThis->fFlags & RTFILE_O_WRITE, VERR_INVALID_FUNCTION);

    return pThis->pOps->pfnAdd(pThis->Base.pvThis, pszPath, hVfsObj, fFlags);
}


RTDECL(int)         RTVfsFsStrmPushFile(RTVFSFSSTREAM hVfsFss, const char *pszPath, uint64_t cbFile,
                                        PCRTFSOBJINFO paObjInfo, uint32_t cObjInfo, uint32_t fFlags, PRTVFSIOSTREAM phVfsIos)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    AssertPtrReturn(phVfsIos, VERR_INVALID_POINTER);
    *phVfsIos = NIL_RTVFSIOSTREAM;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, VERR_INVALID_HANDLE);

    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath != '\0', VERR_INVALID_NAME);

    AssertReturn(!(fFlags & ~RTVFSFSSTRM_PUSH_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(RT_BOOL(cbFile == UINT64_MAX) == RT_BOOL(fFlags & RTVFSFSSTRM_PUSH_F_STREAM), VERR_INVALID_FLAGS);

    if (cObjInfo)
    {
        AssertPtrReturn(paObjInfo, VERR_INVALID_POINTER);
        AssertReturn(paObjInfo[0].Attr.enmAdditional == RTFSOBJATTRADD_UNIX, VERR_INVALID_PARAMETER);
    }

    AssertReturn(pThis->fFlags & RTFILE_O_WRITE, VERR_INVALID_FUNCTION);
    if (pThis->pOps->pfnPushFile)
        return pThis->pOps->pfnPushFile(pThis->Base.pvThis, pszPath, cbFile, paObjInfo, cObjInfo, fFlags, phVfsIos);
    return VERR_NOT_SUPPORTED;
}


RTDECL(int)         RTVfsFsStrmEnd(RTVFSFSSTREAM hVfsFss)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, VERR_INVALID_HANDLE);

    return pThis->pOps->pfnEnd(pThis->Base.pvThis);
}


RTDECL(void *) RTVfsFsStreamToPrivate(RTVFSFSSTREAM hVfsFss, PCRTVFSFSSTREAMOPS pFsStreamOps)
{
    RTVFSFSSTREAMINTERNAL *pThis = hVfsFss;
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->uMagic == RTVFSFSSTREAM_MAGIC, NULL);
    if (pThis->pOps != pFsStreamOps)
        return NULL;
    return pThis->Base.pvThis;
}


/*
 *
 *  D I R   D I R   D I R
 *  D I R   D I R   D I R
 *  D I R   D I R   D I R
 *
 */


RTDECL(int) RTVfsNewDir(PCRTVFSDIROPS pDirOps, size_t cbInstance, uint32_t fFlags, RTVFS hVfs, RTVFSLOCK hLock,
                        PRTVFSDIR phVfsDir, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pDirOps);
    AssertReturn(pDirOps->uVersion   == RTVFSDIROPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pDirOps->uEndMarker == RTVFSDIROPS_VERSION, VERR_VERSION_MISMATCH);
    Assert(!pDirOps->fReserved);
    RTVFSDIR_ASSERT_OPS(pDirOps, RTVFSOBJTYPE_DIR);
    Assert(cbInstance > 0);
    AssertReturn(!(fFlags & ~RTVFSDIR_F_NO_VFS_REF), VERR_INVALID_FLAGS);
    AssertPtr(ppvInstance);
    AssertPtr(phVfsDir);
    RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, VERR_INVALID_HANDLE);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSDIRINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSDIRINTERNAL *pThis = (RTVFSDIRINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(&pThis->Base, &pDirOps->Obj, hVfs, RT_BOOL(fFlags & RTVFSDIR_F_NO_VFS_REF), hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->uMagic       = RTVFSDIR_MAGIC;
    pThis->fReserved    = 0;
    pThis->pOps         = pDirOps;

    *phVfsDir    = pThis;
    *ppvInstance = pThis->Base.pvThis;
    return VINF_SUCCESS;
}


RTDECL(void *) RTVfsDirToPrivate(RTVFSDIR hVfsDir, PCRTVFSDIROPS pDirOps)
{
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, NULL);
    if (pThis->pOps != pDirOps)
        return NULL;
    return pThis->Base.pvThis;
}


#ifdef DEBUG
# undef RTVfsDirRetain
#endif
RTDECL(uint32_t) RTVfsDirRetain(RTVFSDIR hVfsDir)
{
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, UINT32_MAX);
    uint32_t cRefs = rtVfsObjRetain(&pThis->Base);
    LogFlow(("RTVfsDirRetain(%p/%p) -> %#x\n", pThis, pThis->Base.pvThis, cRefs));
    return cRefs;
}
#ifdef DEBUG
# define RTVfsDirRetain(hVfsDir)    RTVfsDirRetainDebug(hVfsDir, RT_SRC_POS)
#endif


RTDECL(uint32_t) RTVfsDirRetainDebug(RTVFSDIR hVfsDir, RT_SRC_POS_DECL)
{
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, UINT32_MAX);
    return rtVfsObjRetainDebug(&pThis->Base, "RTVfsDirRetain", RT_SRC_POS_ARGS);
}


RTDECL(uint32_t) RTVfsDirRelease(RTVFSDIR hVfsDir)
{
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    if (pThis == NIL_RTVFSDIR)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, UINT32_MAX);
#ifdef LOG_ENABLED
    void *pvThis = pThis->Base.pvThis;
#endif
    uint32_t cRefs = rtVfsObjRelease(&pThis->Base);
    LogFlow(("RTVfsDirRelease(%p/%p) -> %#x\n", pThis, pvThis, cRefs));
    return cRefs;
}


RTDECL(int) RTVfsDirOpen(RTVFS hVfs, const char *pszPath, uint32_t fFlags, PRTVFSDIR phVfsDir)
{
    /*
     * Validate input.
     */
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertPtrReturn(phVfsDir, VERR_INVALID_POINTER);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS); /** @todo sort out flags! */

    /*
     * Parse the path, assume current directory is root since we've got no
     * caller context here.
     */
    PRTVFSPARSEDPATH pPath;
    int rc = RTVfsParsePathA(pszPath, "/", &pPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * Tranverse the path, resolving the parent node.
         * We'll do the symbolic link checking here with help of pfnOpen/pfnOpenDir.
         */
        RTVFSDIRINTERNAL *pVfsParentDir;
        rc = rtVfsTraverseToParent(pThis, pPath, (fFlags & RTPATH_F_NO_SYMLINKS) | RTPATH_F_ON_LINK, &pVfsParentDir);
        if (RT_SUCCESS(rc))
        {
            /*
             * Do the opening.  Loop if we need to follow symbolic links.
             */
            uint64_t fOpenFlags = RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN;
            uint32_t fObjFlags  = RTVFSOBJ_F_OPEN_DIRECTORY | RTVFSOBJ_F_OPEN_SYMLINK | RTVFSOBJ_F_CREATE_NOTHING;
            for (uint32_t cLoops = 1; ; cLoops++)
            {
                /* Do the querying.  If pfnOpenDir is available, we use it first, falling
                   back on pfnOpen in case of symbolic links that needs following. */
                const char *pszEntryName = &pPath->szPath[pPath->aoffComponents[pPath->cComponents - 1]];
                if (pVfsParentDir->pOps->pfnOpenDir)
                {
                    RTVfsLockAcquireRead(pVfsParentDir->Base.hLock);
                    rc = pVfsParentDir->pOps->pfnOpenDir(pVfsParentDir->Base.pvThis, pszEntryName, fFlags, phVfsDir);
                    RTVfsLockReleaseRead(pVfsParentDir->Base.hLock);
                    if (   RT_SUCCESS(rc)
                        || (   rc != VERR_NOT_A_DIRECTORY
                            && rc != VERR_IS_A_SYMLINK))
                        break;
                }

                RTVFSOBJ hVfsObj;
                RTVfsLockAcquireWrite(pVfsParentDir->Base.hLock);
                rc = pVfsParentDir->pOps->pfnOpen(pVfsParentDir->Base.pvThis, pszEntryName, fOpenFlags, fObjFlags, &hVfsObj);
                RTVfsLockReleaseWrite(pVfsParentDir->Base.hLock);
                if (RT_FAILURE(rc))
                    break;

                /* If we don't follow links or this wasn't a link we just have to do the query and we're done. */
                if (   !(fObjFlags & RTPATH_F_FOLLOW_LINK)
                    || RTVfsObjGetType(hVfsObj) != RTVFSOBJTYPE_SYMLINK)
                {
                    *phVfsDir = RTVfsObjToDir(hVfsObj);
                    AssertStmt(*phVfsDir != NIL_RTVFSDIR, rc = VERR_INTERNAL_ERROR_3);
                    RTVfsObjRelease(hVfsObj);
                    break;
                }

                /* Follow symbolic link. */
                if (cLoops < RTVFS_MAX_LINKS)
                    rc = rtVfsDirFollowSymlinkObjToParent(&pVfsParentDir, hVfsObj, pPath, fObjFlags & RTPATH_F_MASK);
                else
                    rc = VERR_TOO_MANY_SYMLINKS;
                RTVfsObjRelease(hVfsObj);
                if (RT_FAILURE(rc))
                    break;
            }
            RTVfsDirRelease(pVfsParentDir);
        }
        RTVfsParsePathFree(pPath);
    }
    return rc;
}


RTDECL(int) RTVfsDirOpenDir(RTVFSDIR hVfsDir, const char *pszPath, uint32_t fFlags, PRTVFSDIR phVfsDir)
{
    /*
     * Validate input.
     */
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertPtrReturn(phVfsDir, VERR_INVALID_POINTER);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS); /** @todo sort out flags! */

    /*
     * Parse the path, it's always relative to the given directory.
     */
    PRTVFSPARSEDPATH pPath;
    int rc = RTVfsParsePathA(pszPath, NULL, &pPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * Tranverse the path, resolving the parent node.
         * We'll do the symbolic link checking here with help of pfnOpen/pfnOpenDir.
         */
        RTVFSDIRINTERNAL *pVfsParentDir;
        uint32_t const    fTraverse = (fFlags & RTPATH_F_NO_SYMLINKS) | RTPATH_F_ON_LINK;
        rc = rtVfsDirTraverseToParent(pThis, pPath, fTraverse, &pVfsParentDir);
        if (RT_SUCCESS(rc))
        {
            /*
             * Do the opening.  Loop if we need to follow symbolic links.
             */
            uint64_t fOpenFlags = RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN;
            uint32_t fObjFlags  = RTVFSOBJ_F_OPEN_DIRECTORY | RTVFSOBJ_F_OPEN_SYMLINK | RTVFSOBJ_F_CREATE_NOTHING | fTraverse;
            for (uint32_t cLoops = 1; ; cLoops++)
            {
                /* Do the querying.  If pfnOpenDir is available, we use it first, falling
                   back on pfnOpen in case of symbolic links that needs following. */
                const char *pszEntryName = &pPath->szPath[pPath->aoffComponents[pPath->cComponents - 1]];
                if (pVfsParentDir->pOps->pfnOpenDir)
                {
                    RTVfsLockAcquireRead(pVfsParentDir->Base.hLock);
                    rc = pVfsParentDir->pOps->pfnOpenDir(pVfsParentDir->Base.pvThis, pszEntryName, fFlags, phVfsDir);
                    RTVfsLockReleaseRead(pVfsParentDir->Base.hLock);
                    if (   RT_SUCCESS(rc)
                        || (   rc != VERR_NOT_A_DIRECTORY
                            && rc != VERR_IS_A_SYMLINK))
                        break;
                }

                RTVFSOBJ hVfsObj;
                RTVfsLockAcquireWrite(pVfsParentDir->Base.hLock);
                rc = pVfsParentDir->pOps->pfnOpen(pVfsParentDir->Base.pvThis, pszEntryName, fOpenFlags, fObjFlags, &hVfsObj);
                RTVfsLockReleaseWrite(pVfsParentDir->Base.hLock);
                if (RT_FAILURE(rc))
                    break;

                /* If we don't follow links or this wasn't a link we just have to do the query and we're done. */
                if (   !(fObjFlags & RTPATH_F_FOLLOW_LINK)
                    || RTVfsObjGetType(hVfsObj) != RTVFSOBJTYPE_SYMLINK)
                {
                    *phVfsDir = RTVfsObjToDir(hVfsObj);
                    AssertStmt(*phVfsDir != NIL_RTVFSDIR, rc = VERR_INTERNAL_ERROR_3);
                    RTVfsObjRelease(hVfsObj);
                    break;
                }

                /* Follow symbolic link. */
                if (cLoops < RTVFS_MAX_LINKS)
                    rc = rtVfsDirFollowSymlinkObjToParent(&pVfsParentDir, hVfsObj, pPath, fTraverse);
                else
                    rc = VERR_TOO_MANY_SYMLINKS;
                RTVfsObjRelease(hVfsObj);
                if (RT_FAILURE(rc))
                    break;
            }
            RTVfsDirRelease(pVfsParentDir);
        }
        RTVfsParsePathFree(pPath);
    }
    return rc;
}


RTDECL(int) RTVfsDirCreateDir(RTVFSDIR hVfsDir, const char *pszRelPath, RTFMODE fMode, uint32_t fFlags, PRTVFSDIR phVfsDir)
{
    /*
     * Validate input.
     */
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszRelPath, VERR_INVALID_POINTER);
    AssertPtrNullReturn(phVfsDir, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTDIRCREATE_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);
    fMode = rtFsModeNormalize(fMode, pszRelPath, 0, RTFS_TYPE_DIRECTORY);
    AssertReturn(rtFsModeIsValidPermissions(fMode), VERR_INVALID_FMODE);
    if (!(fFlags & RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_DONT_SET))
        fMode |= RTFS_DOS_NT_NOT_CONTENT_INDEXED;

    /*
     * Parse the path, it's always relative to the given directory.
     */
    PRTVFSPARSEDPATH pPath;
    int rc = RTVfsParsePathA(pszRelPath, NULL, &pPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * Tranverse the path, resolving the parent node.
         * We'll do the symbolic link checking here with help of pfnOpen/pfnOpenDir.
         */
        RTVFSDIRINTERNAL *pVfsParentDir;
        uint32_t          fTraverse = (fFlags & RTDIRCREATE_FLAGS_NO_SYMLINKS ? RTPATH_F_NO_SYMLINKS : 0) | RTPATH_F_ON_LINK;
        rc = rtVfsDirTraverseToParent(pThis, pPath, fTraverse, &pVfsParentDir);
        if (RT_SUCCESS(rc))
        {
            /*
             * Do the opening.  Loop if we need to follow symbolic links.
             */
            uint64_t fOpenFlags = RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_CREATE
                                | ((fMode << RTFILE_O_CREATE_MODE_SHIFT) & RTFILE_O_CREATE_MODE_MASK);
            uint32_t fObjFlags  = RTVFSOBJ_F_OPEN_SYMLINK | RTVFSOBJ_F_CREATE_DIRECTORY | fTraverse;
            for (uint32_t cLoops = 1; ; cLoops++)
            {
                /* Do the querying.  If pfnOpenDir is available, we use it first, falling
                   back on pfnOpen in case of symbolic links that needs following. */
                const char *pszEntryName = &pPath->szPath[pPath->aoffComponents[pPath->cComponents - 1]];
                if (pVfsParentDir->pOps->pfnCreateDir)
                {
                    RTVfsLockAcquireRead(pVfsParentDir->Base.hLock);
                    rc = pVfsParentDir->pOps->pfnCreateDir(pVfsParentDir->Base.pvThis, pszEntryName, fMode, phVfsDir);
                    RTVfsLockReleaseRead(pVfsParentDir->Base.hLock);
                    if (   RT_SUCCESS(rc)
                        || (   rc != VERR_NOT_A_DIRECTORY
                            && rc != VERR_IS_A_SYMLINK))
                        break;
                }

                RTVFSOBJ hVfsObj;
                RTVfsLockAcquireWrite(pVfsParentDir->Base.hLock);
                rc = pVfsParentDir->pOps->pfnOpen(pVfsParentDir->Base.pvThis, pszEntryName, fOpenFlags, fObjFlags, &hVfsObj);
                RTVfsLockReleaseWrite(pVfsParentDir->Base.hLock);
                if (RT_FAILURE(rc))
                    break;

                /* If we don't follow links or this wasn't a link we just have to do the query and we're done. */
                if (   !(fObjFlags & RTPATH_F_FOLLOW_LINK)
                    || RTVfsObjGetType(hVfsObj) != RTVFSOBJTYPE_SYMLINK)
                {
                    if (phVfsDir)
                    {
                        *phVfsDir = RTVfsObjToDir(hVfsObj);
                        AssertStmt(*phVfsDir != NIL_RTVFSDIR, rc = VERR_INTERNAL_ERROR_3);
                    }
                    RTVfsObjRelease(hVfsObj);
                    break;
                }

                /* Follow symbolic link. */
                if (cLoops < RTVFS_MAX_LINKS)
                    rc = rtVfsDirFollowSymlinkObjToParent(&pVfsParentDir, hVfsObj, pPath, fTraverse);
                else
                    rc = VERR_TOO_MANY_SYMLINKS;
                RTVfsObjRelease(hVfsObj);
                if (RT_FAILURE(rc))
                    break;
            }
            RTVfsDirRelease(pVfsParentDir);
        }
        RTVfsParsePathFree(pPath);
    }
    return rc;
}


RTDECL(int) RTVfsDirOpenFile(RTVFSDIR hVfsDir, const char *pszPath, uint64_t fOpen, PRTVFSFILE phVfsFile)
{
    /*
     * Validate input.
     */
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertPtrReturn(phVfsFile, VERR_INVALID_POINTER);

    int rc = rtFileRecalcAndValidateFlags(&fOpen);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Parse the path, it's always relative to the given directory.
     */
    PRTVFSPARSEDPATH pPath;
    rc = RTVfsParsePathA(pszPath, NULL, &pPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * Tranverse the path, resolving the parent node.
         * We'll do the symbolic link checking here with help of pfnOpen/pfnOpenFile.
         */
        RTVFSDIRINTERNAL *pVfsParentDir;
        uint32_t const    fTraverse = (fOpen & RTFILE_O_NO_SYMLINKS ? RTPATH_F_NO_SYMLINKS : 0) | RTPATH_F_ON_LINK;
        rc = rtVfsDirTraverseToParent(pThis, pPath, fTraverse, &pVfsParentDir);
        if (RT_SUCCESS(rc))
        {
            /** @todo join path with RTVfsFileOpen.   */

            /*
             * Do the opening.  Loop if we need to follow symbolic links.
             */
            bool     fDirSlash = pPath->fDirSlash;

            uint32_t fObjFlags = RTVFSOBJ_F_OPEN_ANY_FILE | RTVFSOBJ_F_OPEN_SYMLINK;
            if (   (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_CREATE
                || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_CREATE_REPLACE)
                fObjFlags |= RTVFSOBJ_F_CREATE_FILE;
            else
                fObjFlags |= RTVFSOBJ_F_CREATE_NOTHING;
            fObjFlags  |= fTraverse & RTPATH_F_MASK;

            for (uint32_t cLoops = 1;; cLoops++)
            {
                /* Do the querying.  If pfnOpenFile is available, we use it first, falling
                   back on pfnOpen in case of symbolic links that needs following or we got
                   a trailing directory slash (to get file-not-found error). */
                const char *pszEntryName = &pPath->szPath[pPath->aoffComponents[pPath->cComponents - 1]];
                if (   pVfsParentDir->pOps->pfnOpenFile
                    && !fDirSlash)
                {
                    RTVfsLockAcquireRead(pVfsParentDir->Base.hLock);
                    rc = pVfsParentDir->pOps->pfnOpenFile(pVfsParentDir->Base.pvThis, pszEntryName, fOpen, phVfsFile);
                    RTVfsLockReleaseRead(pVfsParentDir->Base.hLock);
                    if (   RT_SUCCESS(rc)
                        || (   rc != VERR_NOT_A_FILE
                            && rc != VERR_IS_A_SYMLINK))
                        break;
                }

                RTVFSOBJ hVfsObj;
                RTVfsLockAcquireWrite(pVfsParentDir->Base.hLock);
                rc = pVfsParentDir->pOps->pfnOpen(pVfsParentDir->Base.pvThis, pszEntryName, fOpen, fObjFlags, &hVfsObj);
                RTVfsLockReleaseWrite(pVfsParentDir->Base.hLock);
                if (RT_FAILURE(rc))
                    break;

                /* If we don't follow links or this wasn't a link we just have to do the query and we're done. */
                if (   !(fObjFlags & RTPATH_F_FOLLOW_LINK)
                    || RTVfsObjGetType(hVfsObj) != RTVFSOBJTYPE_SYMLINK)
                {
                    *phVfsFile = RTVfsObjToFile(hVfsObj);
                    AssertStmt(*phVfsFile != NIL_RTVFSFILE, rc = VERR_INTERNAL_ERROR_3);
                    RTVfsObjRelease(hVfsObj);
                    break;
                }

                /* Follow symbolic link. */
                if (cLoops < RTVFS_MAX_LINKS)
                    rc = rtVfsDirFollowSymlinkObjToParent(&pVfsParentDir, hVfsObj, pPath, fTraverse);
                else
                    rc = VERR_TOO_MANY_SYMLINKS;
                RTVfsObjRelease(hVfsObj);
                if (RT_FAILURE(rc))
                    break;
                fDirSlash |= pPath->fDirSlash;
            }
            RTVfsDirRelease(pVfsParentDir);
        }
        RTVfsParsePathFree(pPath);
    }
    return rc;
}


RTDECL(int) RTVfsDirOpenFileAsIoStream(RTVFSDIR hVfsDir, const char *pszPath, uint64_t fOpen, PRTVFSIOSTREAM phVfsIos)
{
    RTVFSFILE hVfsFile;
    int rc = RTVfsDirOpenFile(hVfsDir, pszPath, fOpen, &hVfsFile);
    if (RT_SUCCESS(rc))
    {
        *phVfsIos = RTVfsFileToIoStream(hVfsFile);
        AssertStmt(*phVfsIos != NIL_RTVFSIOSTREAM, rc = VERR_INTERNAL_ERROR_2);
        RTVfsFileRelease(hVfsFile);
    }
    return rc;
}


RTDECL(int) RTVfsDirOpenObj(RTVFSDIR hVfsDir, const char *pszPath, uint64_t fFileOpen, uint32_t fObjFlags, PRTVFSOBJ phVfsObj)
{
    /*
     * Validate input.
     */
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertPtrReturn(phVfsObj, VERR_INVALID_POINTER);

    int rc = rtFileRecalcAndValidateFlags(&fFileOpen);
    if (RT_FAILURE(rc))
        return rc;
    AssertMsgReturn(   RTPATH_F_IS_VALID(fObjFlags, RTVFSOBJ_F_VALID_MASK)
                    && (fObjFlags & RTVFSOBJ_F_CREATE_MASK) <= RTVFSOBJ_F_CREATE_DIRECTORY,
                    ("fObjFlags=%#x\n", fObjFlags),
                    VERR_INVALID_FLAGS);

    /*
     * Parse the relative path.  If it ends with a directory slash or it boils
     * down to an empty path (i.e. re-opening hVfsDir), adjust the flags to only
     * open/create directories.
     */
    PRTVFSPARSEDPATH pPath;
    rc = RTVfsParsePathA(pszPath, NULL, &pPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * Tranverse the path, resolving the parent node.
         * We'll do the symbolic link checking here with help of pfnOpen.
         */
        RTVFSDIRINTERNAL *pVfsParentDir;
        rc = rtVfsDirTraverseToParent(pThis, pPath, (fObjFlags & RTPATH_F_NO_SYMLINKS) | RTPATH_F_ON_LINK, &pVfsParentDir);
        if (RT_SUCCESS(rc))
        {
            /*
             * Do the opening.  Loop if we need to follow symbolic links.
             */
            for (uint32_t cLoops = 1;; cLoops++)
            {
                /* If we end with a directory slash, adjust open flags. */
                if (pPath->fDirSlash)
                {
                    fObjFlags &= ~RTVFSOBJ_F_OPEN_ANY | RTVFSOBJ_F_OPEN_DIRECTORY;
                    if ((fObjFlags & RTVFSOBJ_F_CREATE_MASK) != RTVFSOBJ_F_CREATE_DIRECTORY)
                        fObjFlags = (fObjFlags & ~RTVFSOBJ_F_CREATE_MASK) | RTVFSOBJ_F_CREATE_NOTHING;
                }
                if (fObjFlags & RTPATH_F_FOLLOW_LINK)
                    fObjFlags |= RTVFSOBJ_F_OPEN_SYMLINK;

                /* Open it. */
                const char *pszEntryName = &pPath->szPath[pPath->aoffComponents[pPath->cComponents - 1]];
                RTVFSOBJ    hVfsObj;
                RTVfsLockAcquireWrite(pVfsParentDir->Base.hLock);
                rc = pVfsParentDir->pOps->pfnOpen(pVfsParentDir->Base.pvThis, pszEntryName, fFileOpen, fObjFlags, &hVfsObj);
                RTVfsLockReleaseWrite(pVfsParentDir->Base.hLock);
                if (RT_FAILURE(rc))
                    break;

                /* We're done if we don't follow links or this wasn't a link. */
                if (   !(fObjFlags & RTPATH_F_FOLLOW_LINK)
                    || RTVfsObjGetType(*phVfsObj) != RTVFSOBJTYPE_SYMLINK)
                {
                    *phVfsObj = hVfsObj;
                    break;
                }

                /* Follow symbolic link. */
                if (cLoops < RTVFS_MAX_LINKS)
                    rc = rtVfsDirFollowSymlinkObjToParent(&pVfsParentDir, hVfsObj, pPath, fObjFlags & RTPATH_F_MASK);
                else
                    rc = VERR_TOO_MANY_SYMLINKS;
                RTVfsObjRelease(hVfsObj);
                if (RT_FAILURE(rc))
                    break;
            }

            RTVfsDirRelease(pVfsParentDir);
        }
        RTVfsParsePathFree(pPath);
    }
    return rc;
}


RTDECL(int) RTVfsDirQueryPathInfo(RTVFSDIR hVfsDir, const char *pszPath, PRTFSOBJINFO pObjInfo,
                                  RTFSOBJATTRADD enmAddAttr, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
    AssertReturn(enmAddAttr >= RTFSOBJATTRADD_NOTHING &&  enmAddAttr <= RTFSOBJATTRADD_LAST, VERR_INVALID_PARAMETER);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Parse the relative path.  Then traverse to the parent directory.
     */
    PRTVFSPARSEDPATH pPath;
    int rc = RTVfsParsePathA(pszPath, NULL, &pPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * Tranverse the path, resolving the parent node.
         * We'll do the symbolic link checking here with help of pfnOpen.
         */
        RTVFSDIRINTERNAL *pVfsParentDir;
        rc = rtVfsDirTraverseToParent(pThis, pPath, (fFlags & RTPATH_F_NO_SYMLINKS) | RTPATH_F_ON_LINK, &pVfsParentDir);
        if (RT_SUCCESS(rc))
        {
            /*
             * Do the opening.  Loop if we need to follow symbolic links.
             */
            uint32_t fObjFlags = RTVFSOBJ_F_OPEN_ANY | RTVFSOBJ_F_CREATE_NOTHING;
            for (uint32_t cLoops = 1;; cLoops++)
            {
                /* If we end with a directory slash, adjust open flags. */
                if (pPath->fDirSlash)
                {
                    fObjFlags &= ~RTVFSOBJ_F_OPEN_ANY | RTVFSOBJ_F_OPEN_DIRECTORY;
                    if ((fObjFlags & RTVFSOBJ_F_CREATE_MASK) != RTVFSOBJ_F_CREATE_DIRECTORY)
                        fObjFlags = (fObjFlags & ~RTVFSOBJ_F_CREATE_MASK) | RTVFSOBJ_F_CREATE_NOTHING;
                }
                if (fObjFlags & RTPATH_F_FOLLOW_LINK)
                    fObjFlags |= RTVFSOBJ_F_OPEN_SYMLINK;

                /* Do the querying.  If pfnQueryEntryInfo is available, we use it first,
                   falling back on pfnOpen in case of symbolic links that needs following. */
                const char *pszEntryName = &pPath->szPath[pPath->aoffComponents[pPath->cComponents - 1]];
                if (pVfsParentDir->pOps->pfnQueryEntryInfo)
                {
                    RTVfsLockAcquireRead(pVfsParentDir->Base.hLock);
                    rc = pVfsParentDir->pOps->pfnQueryEntryInfo(pVfsParentDir->Base.pvThis, pszEntryName, pObjInfo, enmAddAttr);
                    RTVfsLockReleaseRead(pVfsParentDir->Base.hLock);
                    if (RT_FAILURE(rc))
                        break;
                    if (   !RTFS_IS_SYMLINK(pObjInfo->Attr.fMode)
                        || !(fFlags & RTPATH_F_FOLLOW_LINK))
                    {
                        if (   (fObjFlags & RTVFSOBJ_F_OPEN_MASK) != RTVFSOBJ_F_OPEN_ANY
                            && !RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode))
                            rc = VERR_NOT_A_DIRECTORY;
                        break;
                    }
                }

                RTVFSOBJ hVfsObj;
                RTVfsLockAcquireWrite(pVfsParentDir->Base.hLock);
                rc = pVfsParentDir->pOps->pfnOpen(pVfsParentDir->Base.pvThis, pszEntryName,
                                                  RTFILE_O_ACCESS_ATTR_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                                  fObjFlags, &hVfsObj);
                RTVfsLockReleaseWrite(pVfsParentDir->Base.hLock);
                if (RT_FAILURE(rc))
                    break;

                /* If we don't follow links or this wasn't a link we just have to do the query and we're done. */
                if (   !(fObjFlags & RTPATH_F_FOLLOW_LINK)
                    || RTVfsObjGetType(hVfsObj) != RTVFSOBJTYPE_SYMLINK)
                {
                    rc = RTVfsObjQueryInfo(hVfsObj, pObjInfo, enmAddAttr);
                    RTVfsObjRelease(hVfsObj);
                    break;
                }

                /* Follow symbolic link. */
                if (cLoops < RTVFS_MAX_LINKS)
                    rc = rtVfsDirFollowSymlinkObjToParent(&pVfsParentDir, hVfsObj, pPath, fObjFlags & RTPATH_F_MASK);
                else
                    rc = VERR_TOO_MANY_SYMLINKS;
                RTVfsObjRelease(hVfsObj);
                if (RT_FAILURE(rc))
                    break;
            }

            RTVfsDirRelease(pVfsParentDir);
        }
        RTVfsParsePathFree(pPath);
    }
    return rc;
}


RTDECL(int) RTVfsDirRemoveDir(RTVFSDIR hVfsDir, const char *pszRelPath, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszRelPath, VERR_INVALID_POINTER);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);

    /*
     * Parse the path, it's always relative to the given directory.
     */
    PRTVFSPARSEDPATH pPath;
    int rc = RTVfsParsePathA(pszRelPath, NULL, &pPath);
    if (RT_SUCCESS(rc))
    {
        if (pPath->cComponents > 0)
        {
            /*
             * Tranverse the path, resolving the parent node, not checking for symbolic
             * links in the final element, and ask the directory to remove the subdir.
             */
            RTVFSDIRINTERNAL *pVfsParentDir;
            rc = rtVfsDirTraverseToParent(pThis, pPath, RTPATH_F_ON_LINK, &pVfsParentDir);
            if (RT_SUCCESS(rc))
            {
                const char *pszEntryName = &pPath->szPath[pPath->aoffComponents[pPath->cComponents - 1]];

                RTVfsLockAcquireWrite(pVfsParentDir->Base.hLock);
                if (pVfsParentDir->pOps->pfnUnlinkEntry)
                    rc = pVfsParentDir->pOps->pfnUnlinkEntry(pVfsParentDir->Base.pvThis, pszEntryName, RTFS_TYPE_DIRECTORY);
                else
                    rc = VERR_NOT_SUPPORTED;
                RTVfsLockReleaseWrite(pVfsParentDir->Base.hLock);

                RTVfsDirRelease(pVfsParentDir);
            }
        }
        else
            rc = VERR_PATH_ZERO_LENGTH;
        RTVfsParsePathFree(pPath);
    }
    return rc;
}



RTDECL(int) RTVfsDirReadEx(RTVFSDIR hVfsDir, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry, RTFSOBJATTRADD enmAddAttr)
{
    /*
     * Validate input.
     */
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pDirEntry, VERR_INVALID_POINTER);
    AssertReturn(enmAddAttr >= RTFSOBJATTRADD_NOTHING && enmAddAttr <= RTFSOBJATTRADD_LAST, VERR_INVALID_PARAMETER);

    size_t cbDirEntry = sizeof(*pDirEntry);
    if (!pcbDirEntry)
        pcbDirEntry = &cbDirEntry;
    else
    {
        cbDirEntry = *pcbDirEntry;
        AssertMsgReturn(cbDirEntry >= RT_UOFFSETOF(RTDIRENTRYEX, szName[2]),
                        ("Invalid *pcbDirEntry=%d (min %zu)\n", *pcbDirEntry, RT_UOFFSETOF(RTDIRENTRYEX, szName[2])),
                        VERR_INVALID_PARAMETER);
    }

    /*
     * Call the directory method.
     */
    RTVfsLockAcquireRead(pThis->Base.hLock);
    int rc = pThis->pOps->pfnReadDir(pThis->Base.pvThis, pDirEntry, pcbDirEntry, enmAddAttr);
    RTVfsLockReleaseRead(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsDirRewind(RTVFSDIR hVfsDir)
{
    /*
     * Validate input.
     */
    RTVFSDIRINTERNAL *pThis = hVfsDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSDIR_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Call the directory method.
     */
    RTVfsLockAcquireRead(pThis->Base.hLock);
    int rc = pThis->pOps->pfnRewindDir(pThis->Base.pvThis);
    RTVfsLockReleaseRead(pThis->Base.hLock);
    return rc;
}


/*
 *
 *  S Y M B O L I C   L I N K
 *  S Y M B O L I C   L I N K
 *  S Y M B O L I C   L I N K
 *
 */

RTDECL(int) RTVfsNewSymlink(PCRTVFSSYMLINKOPS pSymlinkOps, size_t cbInstance, RTVFS hVfs, RTVFSLOCK hLock,
                            PRTVFSSYMLINK phVfsSym, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pSymlinkOps);
    AssertReturn(pSymlinkOps->uVersion   == RTVFSSYMLINKOPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pSymlinkOps->uEndMarker == RTVFSSYMLINKOPS_VERSION, VERR_VERSION_MISMATCH);
    Assert(!pSymlinkOps->fReserved);
    RTVFSSYMLINK_ASSERT_OPS(pSymlinkOps, RTVFSOBJTYPE_SYMLINK);
    Assert(cbInstance > 0);
    AssertPtr(ppvInstance);
    AssertPtr(phVfsSym);
    RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, VERR_INVALID_HANDLE);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSSYMLINKINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSSYMLINKINTERNAL *pThis = (RTVFSSYMLINKINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(&pThis->Base, &pSymlinkOps->Obj, hVfs, false /*fNoVfsRef*/, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->uMagic = RTVFSSYMLINK_MAGIC;
    pThis->pOps   = pSymlinkOps;

    *phVfsSym     = pThis;
    *ppvInstance  = pThis->Base.pvThis;
    return VINF_SUCCESS;
}


RTDECL(void *) RTVfsSymlinkToPrivate(RTVFSSYMLINK hVfsSym, PCRTVFSSYMLINKOPS pSymlinkOps)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, NULL);
    if (pThis->pOps != pSymlinkOps)
        return NULL;
    return pThis->Base.pvThis;
}


RTDECL(uint32_t)    RTVfsSymlinkRetain(RTVFSSYMLINK hVfsSym)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, UINT32_MAX);
    return rtVfsObjRetain(&pThis->Base);
}


RTDECL(uint32_t)    RTVfsSymlinkRetainDebug(RTVFSSYMLINK hVfsSym, RT_SRC_POS_DECL)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, UINT32_MAX);
    return rtVfsObjRetainDebug(&pThis->Base, "RTVfsSymlinkRetainDebug", RT_SRC_POS_ARGS);
}


RTDECL(uint32_t)    RTVfsSymlinkRelease(RTVFSSYMLINK hVfsSym)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    if (pThis == NIL_RTVFSSYMLINK)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(&pThis->Base);
}


RTDECL(int)         RTVfsSymlinkQueryInfo(RTVFSSYMLINK hVfsSym, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsObjQueryInfo(&pThis->Base, pObjInfo, enmAddAttr);
}


RTDECL(int)  RTVfsSymlinkSetMode(RTVFSSYMLINK hVfsSym, RTFMODE fMode, RTFMODE fMask)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, VERR_INVALID_HANDLE);

    fMode = rtFsModeNormalize(fMode, NULL, 0, RTFS_TYPE_SYMLINK);
    if (!rtFsModeIsValid(fMode))
        return VERR_INVALID_PARAMETER;

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->ObjSet.pfnSetMode(pThis->Base.pvThis, fMode, fMask);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsSymlinkSetTimes(RTVFSSYMLINK hVfsSym, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                 PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, VERR_INVALID_HANDLE);

    AssertPtrNullReturn(pAccessTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pModificationTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pChangeTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pBirthTime, VERR_INVALID_POINTER);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->ObjSet.pfnSetTimes(pThis->Base.pvThis, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsSymlinkSetOwner(RTVFSSYMLINK hVfsSym, RTUID uid, RTGID gid)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, VERR_INVALID_HANDLE);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->ObjSet.pfnSetOwner(pThis->Base.pvThis, uid, gid);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsSymlinkRead(RTVFSSYMLINK hVfsSym, char *pszTarget, size_t cbTarget)
{
    RTVFSSYMLINKINTERNAL *pThis = hVfsSym;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSSYMLINK_MAGIC, VERR_INVALID_HANDLE);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnRead(pThis->Base.pvThis, pszTarget, cbTarget);
    RTVfsLockReleaseWrite(pThis->Base.hLock);

    return rc;
}



/*
 *
 *  I / O   S T R E A M     I / O   S T R E A M     I / O   S T R E A M
 *  I / O   S T R E A M     I / O   S T R E A M     I / O   S T R E A M
 *  I / O   S T R E A M     I / O   S T R E A M     I / O   S T R E A M
 *
 */

RTDECL(int) RTVfsNewIoStream(PCRTVFSIOSTREAMOPS pIoStreamOps, size_t cbInstance, uint32_t fOpen, RTVFS hVfs, RTVFSLOCK hLock,
                             PRTVFSIOSTREAM phVfsIos, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    AssertPtr(pIoStreamOps);
    AssertReturn(pIoStreamOps->uVersion   == RTVFSIOSTREAMOPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pIoStreamOps->uEndMarker == RTVFSIOSTREAMOPS_VERSION, VERR_VERSION_MISMATCH);
    Assert(!(pIoStreamOps->fFeatures & ~RTVFSIOSTREAMOPS_FEAT_VALID_MASK));
    RTVFSIOSTREAM_ASSERT_OPS(pIoStreamOps, RTVFSOBJTYPE_IO_STREAM);
    Assert(cbInstance > 0);
    Assert(fOpen & RTFILE_O_ACCESS_MASK);
    AssertPtr(ppvInstance);
    AssertPtr(phVfsIos);
    RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, VERR_INVALID_HANDLE);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSIOSTREAMINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSIOSTREAMINTERNAL *pThis = (RTVFSIOSTREAMINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(&pThis->Base, &pIoStreamOps->Obj, hVfs, false /*fNoVfsRef*/, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->uMagic = RTVFSIOSTREAM_MAGIC;
    pThis->fFlags = fOpen;
    pThis->pOps   = pIoStreamOps;

    *phVfsIos     = pThis;
    *ppvInstance  = pThis->Base.pvThis;
    return VINF_SUCCESS;
}


RTDECL(void *) RTVfsIoStreamToPrivate(RTVFSIOSTREAM hVfsIos, PCRTVFSIOSTREAMOPS pIoStreamOps)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, NULL);
    if (pThis->pOps != pIoStreamOps)
        return NULL;
    return pThis->Base.pvThis;
}


#ifdef DEBUG
# undef RTVfsIoStrmRetain
#endif
RTDECL(uint32_t)    RTVfsIoStrmRetain(RTVFSIOSTREAM hVfsIos)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, UINT32_MAX);
    return rtVfsObjRetain(&pThis->Base);
}
#ifdef DEBUG
# define RTVfsIoStrmRetain(hVfsIos) RTVfsIoStrmRetainDebug(hVfsIos, RT_SRC_POS)
#endif


RTDECL(uint32_t)    RTVfsIoStrmRetainDebug(RTVFSIOSTREAM hVfsIos, RT_SRC_POS_DECL)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, UINT32_MAX);
    return rtVfsObjRetainDebug(&pThis->Base, "RTVfsIoStrmRetainDebug", RT_SRC_POS_ARGS);
}


RTDECL(uint32_t)    RTVfsIoStrmRelease(RTVFSIOSTREAM hVfsIos)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    if (pThis == NIL_RTVFSIOSTREAM)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(&pThis->Base);
}


RTDECL(RTVFSFILE)   RTVfsIoStrmToFile(RTVFSIOSTREAM hVfsIos)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, NIL_RTVFSFILE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, NIL_RTVFSFILE);

    if (pThis->pOps->Obj.enmType == RTVFSOBJTYPE_FILE)
    {
        rtVfsObjRetainVoid(&pThis->Base, "RTVfsIoStrmToFile");
        return RT_FROM_MEMBER(pThis, RTVFSFILEINTERNAL, Stream);
    }

    /* this is no crime, so don't assert. */
    return NIL_RTVFSFILE;
}


RTDECL(int) RTVfsIoStrmQueryInfo(RTVFSIOSTREAM hVfsIos, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsObjQueryInfo(&pThis->Base, pObjInfo, enmAddAttr);
}


RTDECL(int) RTVfsIoStrmRead(RTVFSIOSTREAM hVfsIos, void *pvBuf, size_t cbToRead, bool fBlocking, size_t *pcbRead)
{
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    if (pcbRead)
        *pcbRead = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(fBlocking || pcbRead, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_READ, VERR_ACCESS_DENIED);

    RTSGSEG Seg = { pvBuf, cbToRead };
    RTSGBUF SgBuf;
    RTSgBufInit(&SgBuf, &Seg, 1);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnRead(pThis->Base.pvThis, -1 /*off*/, &SgBuf, fBlocking, pcbRead);
    RTVfsLockReleaseWrite(pThis->Base.hLock);

    Assert(   rc != VINF_SUCCESS
           || RTSgBufIsAtEnd(&SgBuf)
           || (pcbRead && cbToRead == *pcbRead + RTSgBufCalcLengthLeft(&SgBuf)));
    Assert(!pcbRead || *pcbRead + RTSgBufCalcLengthLeft(&SgBuf) == cbToRead || RT_FAILURE(rc));
    return rc;
}


RTDECL(int) RTVfsIoStrmReadAt(RTVFSIOSTREAM hVfsIos, RTFOFF off, void *pvBuf, size_t cbToRead,
                              bool fBlocking, size_t *pcbRead)
{
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    if (pcbRead)
        *pcbRead = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(fBlocking || pcbRead, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_READ, VERR_ACCESS_DENIED);

    RTSGSEG Seg = { pvBuf, cbToRead };
    RTSGBUF SgBuf;
    RTSgBufInit(&SgBuf, &Seg, 1);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnRead(pThis->Base.pvThis, off, &SgBuf, fBlocking, pcbRead);
    RTVfsLockReleaseWrite(pThis->Base.hLock);

    Assert(   rc != VINF_SUCCESS
           || RTSgBufIsAtEnd(&SgBuf)
           || (pcbRead && cbToRead == *pcbRead + RTSgBufCalcLengthLeft(&SgBuf)));
    Assert(!pcbRead || *pcbRead + RTSgBufCalcLengthLeft(&SgBuf) == cbToRead || RT_FAILURE(rc));
    return rc;
}


RTDECL(int) RTVfsIoStrmWrite(RTVFSIOSTREAM hVfsIos, const void *pvBuf, size_t cbToWrite, bool fBlocking, size_t *pcbWritten)
{
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    if (pcbWritten)
        *pcbWritten = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(fBlocking || pcbWritten, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_WRITE, VERR_ACCESS_DENIED);

    int rc;
    if (pThis->pOps->pfnWrite)
    {
        RTSGSEG Seg = { (void *)pvBuf, cbToWrite };
        RTSGBUF SgBuf;
        RTSgBufInit(&SgBuf, &Seg, 1);

        RTVfsLockAcquireWrite(pThis->Base.hLock);
        rc = pThis->pOps->pfnWrite(pThis->Base.pvThis, -1 /*off*/, &SgBuf, fBlocking, pcbWritten);
        RTVfsLockReleaseWrite(pThis->Base.hLock);

        Assert(!pcbWritten || *pcbWritten + RTSgBufCalcLengthLeft(&SgBuf) == cbToWrite || RT_FAILURE(rc));
    }
    else
        rc = VERR_WRITE_PROTECT;
    return rc;
}


RTDECL(int) RTVfsIoStrmWriteAt(RTVFSIOSTREAM hVfsIos, RTFOFF off, const void *pvBuf, size_t cbToWrite,
                               bool fBlocking, size_t *pcbWritten)
{
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    if (pcbWritten)
        *pcbWritten = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(fBlocking || pcbWritten, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_WRITE, VERR_ACCESS_DENIED);

    int rc;
    if (pThis->pOps->pfnWrite)
    {
        RTSGSEG Seg = { (void *)pvBuf, cbToWrite };
        RTSGBUF SgBuf;
        RTSgBufInit(&SgBuf, &Seg, 1);

        RTVfsLockAcquireWrite(pThis->Base.hLock);
        rc = pThis->pOps->pfnWrite(pThis->Base.pvThis, off, &SgBuf, fBlocking, pcbWritten);
        RTVfsLockReleaseWrite(pThis->Base.hLock);

        Assert(!pcbWritten || *pcbWritten + RTSgBufCalcLengthLeft(&SgBuf) == cbToWrite || RT_FAILURE(rc));
    }
    else
        rc = VERR_WRITE_PROTECT;
    return rc;
}


RTDECL(int) RTVfsIoStrmSgRead(RTVFSIOSTREAM hVfsIos, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    if (pcbRead)
        *pcbRead = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pSgBuf);
    AssertReturn(fBlocking || pcbRead, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_READ, VERR_ACCESS_DENIED);
#ifdef RT_STRICT
    size_t const cbToReadAssert = RTSgBufCalcLengthLeft(pSgBuf);
#endif

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc;
    if (!(pThis->pOps->fFeatures & RTVFSIOSTREAMOPS_FEAT_NO_SG))
        rc = pThis->pOps->pfnRead(pThis->Base.pvThis, off, pSgBuf, fBlocking, pcbRead);
    else
    {
        size_t cbRead = 0;
        rc = VINF_SUCCESS;

        while (!RTSgBufIsAtEnd(pSgBuf))
        {
            RTSGBUF SgBuf;
            RTSGSEG SgSeg;
            SgSeg.pvSeg = RTSgBufGetCurrentSegment(pSgBuf, ~(size_t)0, &SgSeg.cbSeg);
            RTSgBufInit(&SgBuf, &SgSeg, 1);

            size_t cbReadSeg = pcbRead ? 0 : SgSeg.cbSeg;
            rc = pThis->pOps->pfnRead(pThis->Base.pvThis, off, &SgBuf, fBlocking, pcbRead ? &cbReadSeg : NULL);
            if (RT_FAILURE(rc))
                break;
            cbRead += cbReadSeg;
            RTSgBufAdvance(pSgBuf, cbReadSeg);
            if ((pcbRead && cbReadSeg != SgSeg.cbSeg) || rc != VINF_SUCCESS)
                break;
            if (off != -1)
                off += cbReadSeg;
        }

        if (pcbRead)
            *pcbRead = cbRead;
    }
    RTVfsLockReleaseWrite(pThis->Base.hLock);

    Assert(   rc != VINF_SUCCESS
           || RTSgBufIsAtEnd(pSgBuf)
           || (pcbRead && cbToReadAssert == *pcbRead + RTSgBufCalcLengthLeft(pSgBuf)));
    Assert(!pcbRead || *pcbRead + RTSgBufCalcLengthLeft(pSgBuf) == cbToReadAssert || RT_FAILURE(rc));
    return rc;
}


RTDECL(int) RTVfsIoStrmSgWrite(RTVFSIOSTREAM hVfsIos, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    if (pcbWritten)
        *pcbWritten = 0;
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pSgBuf);
    AssertReturn(fBlocking || pcbWritten, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->fFlags & RTFILE_O_WRITE, VERR_ACCESS_DENIED);

    int rc;
    if (pThis->pOps->pfnWrite)
    {
        RTVfsLockAcquireWrite(pThis->Base.hLock);
        if (!(pThis->pOps->fFeatures & RTVFSIOSTREAMOPS_FEAT_NO_SG))
            rc = pThis->pOps->pfnWrite(pThis->Base.pvThis, off, pSgBuf, fBlocking, pcbWritten);
        else
        {
            size_t cbWritten = 0;
            rc = VINF_SUCCESS;

            while (!RTSgBufIsAtEnd(pSgBuf))
            {
                RTSGBUF SgBuf;
                RTSGSEG SgSeg;
                SgSeg.pvSeg = RTSgBufGetCurrentSegment(pSgBuf, ~(size_t)0, &SgSeg.cbSeg);
                RTSgBufInit(&SgBuf, &SgSeg, 1);

                size_t cbWrittenSeg = 0;
                rc = pThis->pOps->pfnWrite(pThis->Base.pvThis, off, &SgBuf, fBlocking, pcbWritten ? &cbWrittenSeg : NULL);
                if (RT_FAILURE(rc))
                    break;
                if (pcbWritten)
                {
                    cbWritten += cbWrittenSeg;
                    RTSgBufAdvance(pSgBuf, cbWrittenSeg);
                    if (cbWrittenSeg != SgSeg.cbSeg)
                        break;
                    if (off != -1)
                        off += cbWrittenSeg;
                }
                else
                {
                    RTSgBufAdvance(pSgBuf, SgSeg.cbSeg);
                    if (off != -1)
                        off += SgSeg.cbSeg;
                }
            }

            if (pcbWritten)
                *pcbWritten = cbWritten;
        }
        RTVfsLockReleaseWrite(pThis->Base.hLock);
    }
    else
        rc = VERR_WRITE_PROTECT;
    return rc;
}


RTDECL(int) RTVfsIoStrmFlush(RTVFSIOSTREAM hVfsIos)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);

    RTVfsLockAcquireWrite(pThis->Base.hLock);
    int rc = pThis->pOps->pfnFlush(pThis->Base.pvThis);
    RTVfsLockReleaseWrite(pThis->Base.hLock);
    return rc;
}


RTDECL(int) RTVfsIoStrmPoll(RTVFSIOSTREAM hVfsIos, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                            uint32_t *pfRetEvents)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, VERR_INVALID_HANDLE);

    int rc;
    if (pThis->pOps->pfnPollOne)
    {
        RTVfsLockAcquireWrite(pThis->Base.hLock);
        rc = pThis->pOps->pfnPollOne(pThis->Base.pvThis, fEvents, cMillies, fIntr, pfRetEvents);
        RTVfsLockReleaseWrite(pThis->Base.hLock);
    }
    /*
     * Default implementation.  Polling for non-error events returns
     * immediately, waiting for errors will work like sleep.
     */
    else if (fEvents != RTPOLL_EVT_ERROR)
    {
        *pfRetEvents = fEvents & ~RTPOLL_EVT_ERROR;
        rc = VINF_SUCCESS;
    }
    else if (fIntr)
        rc = RTThreadSleep(cMillies);
    else
    {
        uint64_t uMsStart = RTTimeMilliTS();
        do
            rc = RTThreadSleep(cMillies);
        while (   rc == VERR_INTERRUPTED
               && RTTimeMilliTS() - uMsStart < cMillies);
        if (rc == VERR_INTERRUPTED)
            rc = VERR_TIMEOUT;
    }
    return rc;
}


RTDECL(RTFOFF) RTVfsIoStrmTell(RTVFSIOSTREAM hVfsIos)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, -1);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, -1);

    RTFOFF off;
    RTVfsLockAcquireRead(pThis->Base.hLock);
    int rc = pThis->pOps->pfnTell(pThis->Base.pvThis, &off);
    RTVfsLockReleaseRead(pThis->Base.hLock);
    if (RT_FAILURE(rc))
        off = rc;
    return off;
}


RTDECL(int) RTVfsIoStrmSkip(RTVFSIOSTREAM hVfsIos, RTFOFF cb)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, -1);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, -1);
    AssertReturn(cb >= 0, VERR_INVALID_PARAMETER);

    int rc;
    if (pThis->pOps->pfnSkip)
    {
        RTVfsLockAcquireWrite(pThis->Base.hLock);
        rc = pThis->pOps->pfnSkip(pThis->Base.pvThis, cb);
        RTVfsLockReleaseWrite(pThis->Base.hLock);
    }
    else if (pThis->pOps->Obj.enmType == RTVFSOBJTYPE_FILE)
    {
        RTVFSFILEINTERNAL *pThisFile = RT_FROM_MEMBER(pThis, RTVFSFILEINTERNAL, Stream);
        RTFOFF             offIgnored;

        RTVfsLockAcquireWrite(pThis->Base.hLock);
        rc = pThisFile->pOps->pfnSeek(pThis->Base.pvThis, cb, RTFILE_SEEK_CURRENT, &offIgnored);
        RTVfsLockReleaseWrite(pThis->Base.hLock);
    }
    else
    {
        void *pvBuf = RTMemTmpAlloc(_64K);
        if (pvBuf)
        {
            rc = VINF_SUCCESS;
            while (cb > 0)
            {
                size_t cbToRead = (size_t)RT_MIN(cb, _64K);
                RTVfsLockAcquireWrite(pThis->Base.hLock);
                rc = RTVfsIoStrmRead(hVfsIos, pvBuf, cbToRead, true /*fBlocking*/, NULL);
                RTVfsLockReleaseWrite(pThis->Base.hLock);
                if (RT_FAILURE(rc))
                    break;
                cb -= cbToRead;
            }

            RTMemTmpFree(pvBuf);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
    }
    return rc;
}


RTDECL(int) RTVfsIoStrmZeroFill(RTVFSIOSTREAM hVfsIos, RTFOFF cb)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, -1);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, -1);

    int rc;
    if (pThis->pOps->pfnZeroFill)
    {
        RTVfsLockAcquireWrite(pThis->Base.hLock);
        rc = pThis->pOps->pfnZeroFill(pThis->Base.pvThis, cb);
        RTVfsLockReleaseWrite(pThis->Base.hLock);
    }
    else
    {
        rc = VINF_SUCCESS;
        while (cb > 0)
        {
            size_t cbToWrite = (size_t)RT_MIN(cb, (ssize_t)sizeof(g_abRTZero64K));
            RTVfsLockAcquireWrite(pThis->Base.hLock);
            rc = RTVfsIoStrmWrite(hVfsIos, g_abRTZero64K, cbToWrite, true /*fBlocking*/, NULL);
            RTVfsLockReleaseWrite(pThis->Base.hLock);
            if (RT_FAILURE(rc))
                break;
            cb -= cbToWrite;
        }
    }
    return rc;
}


RTDECL(bool) RTVfsIoStrmIsAtEnd(RTVFSIOSTREAM hVfsIos)
{
    /*
     * There is where the zero read behavior comes in handy.
     */
    char    bDummy;
    size_t  cbRead;
    int rc = RTVfsIoStrmRead(hVfsIos, &bDummy, 0 /*cbToRead*/, false /*fBlocking*/, &cbRead);
    return rc == VINF_EOF;
}



RTDECL(uint64_t) RTVfsIoStrmGetOpenFlags(RTVFSIOSTREAM hVfsIos)
{
    RTVFSIOSTREAMINTERNAL *pThis = hVfsIos;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->uMagic == RTVFSIOSTREAM_MAGIC, 0);
    return pThis->fFlags;
}



/*
 *
 *  F I L E   F I L E   F I L E
 *  F I L E   F I L E   F I L E
 *  F I L E   F I L E   F I L E
 *
 */

RTDECL(int) RTVfsNewFile(PCRTVFSFILEOPS pFileOps, size_t cbInstance, uint32_t fOpen, RTVFS hVfs, RTVFSLOCK hLock,
                         PRTVFSFILE phVfsFile, void **ppvInstance)
{
    /*
     * Validate the input, be extra strict in strict builds.
     */
    RTVFSFILE_ASSERT_OPS(pFileOps, RTVFSOBJTYPE_FILE);
    Assert(cbInstance > 0);
    Assert(fOpen & (RTFILE_O_ACCESS_MASK | RTFILE_O_ACCESS_ATTR_MASK));
    AssertPtr(ppvInstance);
    AssertPtr(phVfsFile);
    RTVFS_ASSERT_VALID_HANDLE_OR_NIL_RETURN(hVfs, VERR_INVALID_HANDLE);

    /*
     * Allocate the handle + instance data.
     */
    size_t const cbThis = RT_ALIGN_Z(sizeof(RTVFSFILEINTERNAL), RTVFS_INST_ALIGNMENT)
                        + RT_ALIGN_Z(cbInstance, RTVFS_INST_ALIGNMENT);
    RTVFSFILEINTERNAL *pThis = (RTVFSFILEINTERNAL *)RTMemAllocZ(cbThis);
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = rtVfsObjInitNewObject(&pThis->Stream.Base, &pFileOps->Stream.Obj, hVfs, false /*fNoVfsRef*/, hLock,
                                   (char *)pThis + RT_ALIGN_Z(sizeof(*pThis), RTVFS_INST_ALIGNMENT));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->uMagic        = RTVFSFILE_MAGIC;
    pThis->fReserved     = 0;
    pThis->pOps          = pFileOps;
    pThis->Stream.uMagic = RTVFSIOSTREAM_MAGIC;
    pThis->Stream.fFlags = fOpen;
    pThis->Stream.pOps   = &pFileOps->Stream;

    *phVfsFile   = pThis;
    *ppvInstance = pThis->Stream.Base.pvThis;
    return VINF_SUCCESS;
}


RTDECL(int) RTVfsFileOpen(RTVFS hVfs, const char *pszFilename, uint64_t fOpen, PRTVFSFILE phVfsFile)
{
    /*
     * Validate input.
     */
    RTVFSINTERNAL *pThis = hVfs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFS_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertPtrReturn(phVfsFile, VERR_INVALID_POINTER);

    int rc = rtFileRecalcAndValidateFlags(&fOpen);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Parse the path, assume current directory is root since we've got no
     * caller context here.
     */
    PRTVFSPARSEDPATH pPath;
    rc = RTVfsParsePathA(pszFilename, "/", &pPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * Tranverse the path, resolving the parent node.
         * We'll do the symbolic link checking here with help of pfnOpen/pfnOpenFile.
         */
        RTVFSDIRINTERNAL *pVfsParentDir;
        uint32_t const    fTraverse = (fOpen & RTFILE_O_NO_SYMLINKS ? RTPATH_F_NO_SYMLINKS : 0) | RTPATH_F_ON_LINK;
        rc = rtVfsTraverseToParent(pThis, pPath, fTraverse, &pVfsParentDir);
        if (RT_SUCCESS(rc))
        {
            /** @todo join path with RTVfsDirOpenFile.   */
            /*
             * Do the opening.  Loop if we need to follow symbolic links.
             */
            bool     fDirSlash = pPath->fDirSlash;

            uint32_t fObjFlags = RTVFSOBJ_F_OPEN_ANY_FILE | RTVFSOBJ_F_OPEN_SYMLINK;
            if (   (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_CREATE
                || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_CREATE_REPLACE)
                fObjFlags |= RTVFSOBJ_F_CREATE_FILE;
            else
                fObjFlags |= RTVFSOBJ_F_CREATE_NOTHING;
            fObjFlags  |= fTraverse & RTPATH_F_MASK;

            for (uint32_t cLoops = 1;; cLoops++)
            {
                /* Do the querying.  If pfnOpenFile is available, we use it first, falling
                   back on pfnOpen in case of symbolic links that needs following or we got
                   a trailing directory slash (to get file-not-found error). */
                const char *pszEntryName = &pPath->szPath[pPath->aoffComponents[pPath->cComponents - 1]];
                if (   pVfsParentDir->pOps->pfnOpenFile
                    && !fDirSlash)
                {
                    RTVfsLockAcquireRead(pVfsParentDir->Base.hLock);
                    rc = pVfsParentDir->pOps->pfnOpenFile(pVfsParentDir->Base.pvThis, pszEntryName, fOpen, phVfsFile);
                    RTVfsLockReleaseRead(pVfsParentDir->Base.hLock);
                    if (   RT_SUCCESS(rc)
                        || (   rc != VERR_NOT_A_FILE
                            && rc != VERR_IS_A_SYMLINK))
                        break;
                }

                RTVFSOBJ hVfsObj;
                RTVfsLockAcquireWrite(pVfsParentDir->Base.hLock);
                rc = pVfsParentDir->pOps->pfnOpen(pVfsParentDir->Base.pvThis, pszEntryName, fOpen, fObjFlags, &hVfsObj);
                RTVfsLockReleaseWrite(pVfsParentDir->Base.hLock);
                if (RT_FAILURE(rc))
                    break;

                /* If we don't follow links or this wasn't a link we just have to do the query and we're done. */
                if (   !(fObjFlags & RTPATH_F_FOLLOW_LINK)
                    || RTVfsObjGetType(hVfsObj) != RTVFSOBJTYPE_SYMLINK)
                {
                    *phVfsFile = RTVfsObjToFile(hVfsObj);
                    AssertStmt(*phVfsFile != NIL_RTVFSFILE, rc = VERR_INTERNAL_ERROR_3);
                    RTVfsObjRelease(hVfsObj);
                    break;
                }

                /* Follow symbolic link. */
                if (cLoops < RTVFS_MAX_LINKS)
                    rc = rtVfsDirFollowSymlinkObjToParent(&pVfsParentDir, hVfsObj, pPath, fTraverse);
                else
                    rc = VERR_TOO_MANY_SYMLINKS;
                RTVfsObjRelease(hVfsObj);
                if (RT_FAILURE(rc))
                    break;
                fDirSlash |= pPath->fDirSlash;
            }
            RTVfsDirRelease(pVfsParentDir);
        }
        RTVfsParsePathFree(pPath);
    }
    return rc;

}


#ifdef DEBUG
# undef RTVfsFileRetain
#endif
RTDECL(uint32_t)    RTVfsFileRetain(RTVFSFILE hVfsFile)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, UINT32_MAX);
    return rtVfsObjRetain(&pThis->Stream.Base);
}
#ifdef DEBUG
# define RTVfsFileRetain(hVfsFile)  RTVfsFileRetainDebug(hVfsFile, RT_SRC_POS)
#endif


RTDECL(uint32_t)    RTVfsFileRetainDebug(RTVFSFILE hVfsFile, RT_SRC_POS_DECL)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, UINT32_MAX);
    return rtVfsObjRetainDebug(&pThis->Stream.Base, "RTVFsFileRetainDebug", RT_SRC_POS_ARGS);
}


RTDECL(uint32_t)    RTVfsFileRelease(RTVFSFILE hVfsFile)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    if (pThis == NIL_RTVFSFILE)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, UINT32_MAX);
    return rtVfsObjRelease(&pThis->Stream.Base);
}


RTDECL(RTVFSIOSTREAM) RTVfsFileToIoStream(RTVFSFILE hVfsFile)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, NIL_RTVFSIOSTREAM);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, NIL_RTVFSIOSTREAM);

    rtVfsObjRetainVoid(&pThis->Stream.Base, "RTVfsFileToIoStream");
    return &pThis->Stream;
}


RTDECL(int)         RTVfsFileQueryInfo(RTVFSFILE hVfsFile, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsObjQueryInfo(&pThis->Stream.Base, pObjInfo, enmAddAttr);
}


RTDECL(int)         RTVfsFileRead(RTVFSFILE hVfsFile, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    if (pcbRead)
        *pcbRead = 0;
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsIoStrmRead(&pThis->Stream, pvBuf, cbToRead, true /*fBlocking*/, pcbRead);
}


RTDECL(int)         RTVfsFileWrite(RTVFSFILE hVfsFile, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    if (pcbWritten)
        *pcbWritten = 0;
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsIoStrmWrite(&pThis->Stream, pvBuf, cbToWrite, true /*fBlocking*/, pcbWritten);
}


RTDECL(int)         RTVfsFileWriteAt(RTVFSFILE hVfsFile, RTFOFF off, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    if (pcbWritten)
        *pcbWritten = 0;
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);

    int rc = RTVfsFileSeek(hVfsFile, off, RTFILE_SEEK_BEGIN, NULL);
    if (RT_SUCCESS(rc))
        rc = RTVfsIoStrmWriteAt(&pThis->Stream, off, pvBuf, cbToWrite, true /*fBlocking*/, pcbWritten);

    return rc;
}


RTDECL(int)         RTVfsFileReadAt(RTVFSFILE hVfsFile, RTFOFF off, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    if (pcbRead)
        *pcbRead = 0;
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);

    int rc = RTVfsFileSeek(hVfsFile, off, RTFILE_SEEK_BEGIN, NULL);
    if (RT_SUCCESS(rc))
        rc = RTVfsIoStrmReadAt(&pThis->Stream, off, pvBuf, cbToRead, true /*fBlocking*/, pcbRead);

    return rc;
}


RTDECL(int) RTVfsFileSgRead(RTVFSFILE hVfsFile, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    AssertPtrNullReturn(pcbRead, VERR_INVALID_POINTER);
    if (pcbRead)
        *pcbRead = 0;
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);

    return RTVfsIoStrmSgRead(&pThis->Stream, off, pSgBuf, fBlocking, pcbRead);
}


RTDECL(int) RTVfsFileSgWrite(RTVFSFILE hVfsFile, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    if (pcbWritten)
        *pcbWritten = 0;
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);

    return RTVfsIoStrmSgWrite(&pThis->Stream, off, pSgBuf, fBlocking, pcbWritten);
}


RTDECL(int) RTVfsFileFlush(RTVFSFILE hVfsFile)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsIoStrmFlush(&pThis->Stream);
}


RTDECL(RTFOFF) RTVfsFilePoll(RTVFSFILE hVfsFile, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                  uint32_t *pfRetEvents)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsIoStrmPoll(&pThis->Stream, fEvents, cMillies, fIntr, pfRetEvents);
}


RTDECL(RTFOFF) RTVfsFileTell(RTVFSFILE hVfsFile)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    return RTVfsIoStrmTell(&pThis->Stream);
}


RTDECL(int) RTVfsFileSeek(RTVFSFILE hVfsFile, RTFOFF offSeek, uint32_t uMethod, uint64_t *poffActual)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);

    AssertReturn(   uMethod == RTFILE_SEEK_BEGIN
                 || uMethod == RTFILE_SEEK_CURRENT
                 || uMethod == RTFILE_SEEK_END, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(poffActual, VERR_INVALID_POINTER);

    RTFOFF offActual = 0;
    RTVfsLockAcquireWrite(pThis->Stream.Base.hLock);
    int rc = pThis->pOps->pfnSeek(pThis->Stream.Base.pvThis, offSeek, uMethod, &offActual);
    RTVfsLockReleaseWrite(pThis->Stream.Base.hLock);
    if (RT_SUCCESS(rc) && poffActual)
    {
        Assert(offActual >= 0);
        *poffActual = offActual;
    }

    return rc;
}


RTDECL(int) RTVfsFileQuerySize(RTVFSFILE hVfsFile, uint64_t *pcbSize)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);

    RTVfsLockAcquireWrite(pThis->Stream.Base.hLock);
    int rc = pThis->pOps->pfnQuerySize(pThis->Stream.Base.pvThis, pcbSize);
    RTVfsLockReleaseWrite(pThis->Stream.Base.hLock);

    return rc;
}


RTDECL(int)         RTVfsFileSetSize(RTVFSFILE hVfsFile, uint64_t cbSize, uint32_t fFlags)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(RTVFSFILE_SIZE_F_IS_VALID(fFlags), VERR_INVALID_FLAGS);
    AssertReturn(pThis->Stream.fFlags & RTFILE_O_WRITE, VERR_ACCESS_DENIED);

    int rc;
    if (pThis->pOps->pfnSetSize)
    {
        RTVfsLockAcquireWrite(pThis->Stream.Base.hLock);
        rc = pThis->pOps->pfnSetSize(pThis->Stream.Base.pvThis, cbSize, fFlags);
        RTVfsLockReleaseWrite(pThis->Stream.Base.hLock);
    }
    else
        rc = VERR_WRITE_PROTECT;
    return rc;
}


RTDECL(RTFOFF)      RTVfsFileGetMaxSize(RTVFSFILE hVfsFile)
{
    uint64_t cbMax;
    int rc = RTVfsFileQueryMaxSize(hVfsFile, &cbMax);
    return RT_SUCCESS(rc) ? (RTFOFF)RT_MIN(cbMax, (uint64_t)RTFOFF_MAX) : -1;
}


RTDECL(int)         RTVfsFileQueryMaxSize(RTVFSFILE hVfsFile, uint64_t *pcbMax)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pcbMax, VERR_INVALID_POINTER);
    *pcbMax = RTFOFF_MAX;

    int rc;
    if (pThis->pOps->pfnQueryMaxSize)
    {
        RTVfsLockAcquireWrite(pThis->Stream.Base.hLock);
        rc = pThis->pOps->pfnQueryMaxSize(pThis->Stream.Base.pvThis, pcbMax);
        RTVfsLockReleaseWrite(pThis->Stream.Base.hLock);
    }
    else
        rc = VERR_WRITE_PROTECT;
    return rc;
}


RTDECL(uint64_t) RTVfsFileGetOpenFlags(RTVFSFILE hVfsFile)
{
    RTVFSFILEINTERNAL *pThis = hVfsFile;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->uMagic == RTVFSFILE_MAGIC, 0);
    return pThis->Stream.fFlags;
}

