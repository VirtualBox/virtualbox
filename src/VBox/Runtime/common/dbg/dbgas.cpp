/* $Id: dbgas.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Address Space.
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
#include <iprt/dbg.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/avl.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a module table entry. */
typedef struct RTDBGASMOD *PRTDBGASMOD;
/** Pointer to an address space mapping node. */
typedef struct RTDBGASMAP *PRTDBGASMAP;
/** Pointer to a name head. */
typedef struct RTDBGASNAME *PRTDBGASNAME;

/**
 * Module entry.
 */
typedef struct RTDBGASMOD
{
    /** Node core, the module handle is the key. */
    AVLPVNODECORE       Core;
    /** Pointer to the first mapping of the module or a segment within it. */
    PRTDBGASMAP         pMapHead;
    /** Pointer to the next module with an identical name. */
    PRTDBGASMOD         pNextName;
    /** The index into RTDBGASINT::papModules. */
    uint32_t            iOrdinal;
} RTDBGASMOD;

/**
 * An address space mapping, either of a full module or a segment.
 */
typedef struct RTDBGASMAP
{
    /** The AVL node core. Contains the address range. */
    AVLRUINTPTRNODECORE Core;
    /** Pointer to the next mapping of the module. */
    PRTDBGASMAP         pNext;
    /** Pointer to the module. */
    PRTDBGASMOD         pMod;
    /** Which segment in the module.
     * This is NIL_RTDBGSEGIDX when the entire module is mapped. */
    RTDBGSEGIDX         iSeg;
} RTDBGASMAP;

/**
 * Name in the address space.
 */
typedef struct RTDBGASNAME
{
    /** The string space node core.*/
    RTSTRSPACECORE      StrCore;
    /** The list of nodes  */
    PRTDBGASMOD         pHead;
} RTDBGASNAME;

/**
 * Debug address space instance.
 */
typedef struct RTDBGASINT
{
    /** Magic value (RTDBGAS_MAGIC). */
    uint32_t            u32Magic;
    /** The number of reference to this address space. */
    uint32_t volatile   cRefs;
    /** Handle of the read-write lock. */
    RTSEMRW             hLock;
    /** Number of modules in the module address space. */
    uint32_t            cModules;
    /** Pointer to the module table.
     * The valid array length is given by cModules. */
    PRTDBGASMOD        *papModules;
    /** AVL tree translating module handles to module entries. */
    AVLPVTREE           ModTree;
    /** AVL tree mapping addresses to modules. */
    AVLRUINTPTRTREE     MapTree;
    /** Names of the modules in the name space. */
    RTSTRSPACE          NameSpace;
    /** The first address the AS. */
    RTUINTPTR           FirstAddr;
    /** The last address in the AS. */
    RTUINTPTR           LastAddr;
    /** The name of the address space. (variable length) */
    char                szName[1];
} RTDBGASINT;
/** Pointer to an a debug address space instance. */
typedef RTDBGASINT *PRTDBGASINT;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates an address space handle and returns rc if not valid. */
#define RTDBGAS_VALID_RETURN_RC(pDbgAs, rc) \
    do { \
        AssertPtrReturn((pDbgAs), (rc)); \
        AssertReturn((pDbgAs)->u32Magic == RTDBGAS_MAGIC, (rc)); \
        AssertReturn((pDbgAs)->cRefs > 0, (rc)); \
    } while (0)

/** Locks the address space for reading. */
#define RTDBGAS_LOCK_READ(pDbgAs) \
    do { \
        int rcLock = RTSemRWRequestRead((pDbgAs)->hLock, RT_INDEFINITE_WAIT); \
        AssertRC(rcLock); \
    } while (0)

/** Unlocks the address space after reading. */
#define RTDBGAS_UNLOCK_READ(pDbgAs) \
    do { \
        int rcLock = RTSemRWReleaseRead((pDbgAs)->hLock); \
        AssertRC(rcLock); \
    } while (0)

/** Locks the address space for writing. */
#define RTDBGAS_LOCK_WRITE(pDbgAs) \
    do { \
        int rcLock = RTSemRWRequestWrite((pDbgAs)->hLock, RT_INDEFINITE_WAIT); \
        AssertRC(rcLock); \
    } while (0)

/** Unlocks the address space after writing. */
#define RTDBGAS_UNLOCK_WRITE(pDbgAs) \
    do { \
        int rcLock = RTSemRWReleaseWrite((pDbgAs)->hLock); \
        AssertRC(rcLock); \
    } while (0)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtDbgAsModuleUnlinkMod(PRTDBGASINT pDbgAs, PRTDBGASMOD pMod);
static void rtDbgAsModuleUnlinkByMap(PRTDBGASINT pDbgAs, PRTDBGASMAP pMap);


RTDECL(int) RTDbgAsCreate(PRTDBGAS phDbgAs, RTUINTPTR FirstAddr, RTUINTPTR LastAddr, const char *pszName)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(phDbgAs, VERR_INVALID_POINTER);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertReturn(FirstAddr < LastAddr, VERR_INVALID_PARAMETER);

    /*
     * Allocate memory for the instance data.
     */
    size_t cchName = strlen(pszName);
    PRTDBGASINT pDbgAs = (PRTDBGASINT)RTMemAllocVar(RT_UOFFSETOF_DYN(RTDBGASINT, szName[cchName + 1]));
    if (!pDbgAs)
        return VERR_NO_MEMORY;

    /* initialize it. */
    pDbgAs->u32Magic    = RTDBGAS_MAGIC;
    pDbgAs->cRefs       = 1;
    pDbgAs->hLock       = NIL_RTSEMRW;
    pDbgAs->cModules    = 0;
    pDbgAs->papModules  = NULL;
    pDbgAs->ModTree     = NULL;
    pDbgAs->MapTree     = NULL;
    pDbgAs->NameSpace   = NULL;
    pDbgAs->FirstAddr   = FirstAddr;
    pDbgAs->LastAddr    = LastAddr;
    memcpy(pDbgAs->szName, pszName, cchName + 1);
    int rc = RTSemRWCreate(&pDbgAs->hLock);
    if (RT_SUCCESS(rc))
    {
        *phDbgAs = pDbgAs;
        return VINF_SUCCESS;
    }

    pDbgAs->u32Magic = 0;
    RTMemFree(pDbgAs);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsCreate);


RTDECL(int) RTDbgAsCreateV(PRTDBGAS phDbgAs, RTUINTPTR FirstAddr, RTUINTPTR LastAddr, const char *pszNameFmt, va_list va)
{
    AssertPtrReturn(pszNameFmt, VERR_INVALID_POINTER);

    char *pszName;
    RTStrAPrintfV(&pszName, pszNameFmt, va);
    if (!pszName)
        return VERR_NO_MEMORY;

    int rc = RTDbgAsCreate(phDbgAs, FirstAddr, LastAddr, pszName);

    RTStrFree(pszName);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsCreateV);


RTDECL(int) RTDbgAsCreateF(PRTDBGAS phDbgAs, RTUINTPTR FirstAddr, RTUINTPTR LastAddr, const char *pszNameFmt, ...)
{
    va_list va;
    va_start(va, pszNameFmt);
    int rc = RTDbgAsCreateV(phDbgAs, FirstAddr, LastAddr, pszNameFmt, va);
    va_end(va);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsCreateF);


/**
 * Callback used by RTDbgAsDestroy to free all mapping nodes.
 *
 * @returns 0
 * @param   pNode           The map node.
 * @param   pvUser          NULL.
 */
static DECLCALLBACK(int) rtDbgAsDestroyMapCallback(PAVLRUINTPTRNODECORE pNode, void *pvUser)
{
    RTMemFree(pNode);
    NOREF(pvUser);
    return 0;
}


/**
 * Callback used by RTDbgAsDestroy to free all name space nodes.
 *
 * @returns 0
 * @param   pStr            The name node.
 * @param   pvUser          NULL.
 */
static DECLCALLBACK(int) rtDbgAsDestroyNameCallback(PRTSTRSPACECORE pStr, void *pvUser)
{
    RTMemFree(pStr);
    NOREF(pvUser);
    return 0;
}


/**
 * Destroys the address space.
 *
 * This means unlinking all the modules it currently contains, potentially
 * causing some or all of them to be destroyed as they are managed by
 * reference counting.
 *
 * @param   pDbgAs          The address space instance to be destroyed.
 */
static void rtDbgAsDestroy(PRTDBGASINT pDbgAs)
{
    /*
     * Mark the address space invalid and release all the modules.
     */
    ASMAtomicWriteU32(&pDbgAs->u32Magic, ~RTDBGAS_MAGIC);

    RTAvlrUIntPtrDestroy(&pDbgAs->MapTree, rtDbgAsDestroyMapCallback, NULL);
    RTStrSpaceDestroy(&pDbgAs->NameSpace, rtDbgAsDestroyNameCallback, NULL);

    uint32_t i = pDbgAs->cModules;
    while (i-- > 0)
    {
        PRTDBGASMOD pMod = pDbgAs->papModules[i];
        AssertPtr(pMod);
        if (RT_VALID_PTR(pMod))
        {
            Assert(pMod->iOrdinal == i);
            RTDbgModRelease((RTDBGMOD)pMod->Core.Key);
            pMod->Core.Key = NIL_RTDBGMOD;
            pMod->iOrdinal = UINT32_MAX;
            RTMemFree(pMod);
        }
        pDbgAs->papModules[i] = NULL;
    }
    RTSemRWDestroy(pDbgAs->hLock);
    pDbgAs->hLock = NIL_RTSEMRW;
    RTMemFree(pDbgAs->papModules);
    pDbgAs->papModules = NULL;

    RTMemFree(pDbgAs);
}


RTDECL(uint32_t) RTDbgAsRetain(RTDBGAS hDbgAs)
{
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, UINT32_MAX);
    return ASMAtomicIncU32(&pDbgAs->cRefs);
}
RT_EXPORT_SYMBOL(RTDbgAsRetain);


RTDECL(uint32_t) RTDbgAsRelease(RTDBGAS hDbgAs)
{
    if (hDbgAs == NIL_RTDBGAS)
        return 0;
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pDbgAs->cRefs);
    if (!cRefs)
        rtDbgAsDestroy(pDbgAs);
    return cRefs;
}
RT_EXPORT_SYMBOL(RTDbgAsRelease);


RTDECL(int) RTDbgAsLockExcl(RTDBGAS hDbgAs)
{
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);
    RTDBGAS_LOCK_WRITE(pDbgAs);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTDbgAsLockExcl);


RTDECL(int) RTDbgAsUnlockExcl(RTDBGAS hDbgAs)
{
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);
    RTDBGAS_UNLOCK_WRITE(pDbgAs);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTDbgAsUnlockExcl);


RTDECL(const char *) RTDbgAsName(RTDBGAS hDbgAs)
{
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, NULL);
    return pDbgAs->szName;
}
RT_EXPORT_SYMBOL(RTDbgAsName);


RTDECL(RTUINTPTR) RTDbgAsFirstAddr(RTDBGAS hDbgAs)
{
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, 0);
    return pDbgAs->FirstAddr;
}
RT_EXPORT_SYMBOL(RTDbgAsFirstAddr);


RTDECL(RTUINTPTR) RTDbgAsLastAddr(RTDBGAS hDbgAs)
{
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, 0);
    return pDbgAs->LastAddr;
}
RT_EXPORT_SYMBOL(RTDbgAsLastAddr);


RTDECL(uint32_t) RTDbgAsModuleCount(RTDBGAS hDbgAs)
{
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, 0);
    return pDbgAs->cModules;
}
RT_EXPORT_SYMBOL(RTDbgAsModuleCount);


/**
 * Common worker for RTDbgAsModuleLink and RTDbgAsModuleLinkSeg.
 *
 * @returns IPRT status code.
 * @param   pDbgAs          Pointer to the address space instance data.
 * @param   hDbgMod         The module to link.
 * @param   iSeg            The segment to link or NIL if all.
 * @param   Addr            The address we're linking it at.
 * @param   cb              The size of what we're linking.
 * @param   pszName         The name of the module.
 * @param   fFlags          See RTDBGASLINK_FLAGS_*.
 *
 * @remarks The caller must have locked the address space for writing.
 */
static int rtDbgAsModuleLinkCommon(PRTDBGASINT pDbgAs, RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg,
                                   RTUINTPTR Addr, RTUINTPTR cb, const char *pszName, uint32_t fFlags)
{
    /*
     * Check that the requested space is undisputed.
     */
    for (;;)
    {
        PRTDBGASMAP pAdjMod = (PRTDBGASMAP)RTAvlrUIntPtrGetBestFit(&pDbgAs->MapTree, Addr, false /* fAbove */);
        if (     pAdjMod
            &&   pAdjMod->Core.KeyLast >= Addr)
        {
            if (!(fFlags & RTDBGASLINK_FLAGS_REPLACE))
                return VERR_ADDRESS_CONFLICT;
            rtDbgAsModuleUnlinkByMap(pDbgAs, pAdjMod);
            continue;
        }
        pAdjMod = (PRTDBGASMAP)RTAvlrUIntPtrGetBestFit(&pDbgAs->MapTree, Addr, true /* fAbove */);
        if (     pAdjMod
            &&   pAdjMod->Core.Key <= Addr + cb - 1)
        {
            if (!(fFlags & RTDBGASLINK_FLAGS_REPLACE))
                return VERR_ADDRESS_CONFLICT;
            rtDbgAsModuleUnlinkByMap(pDbgAs, pAdjMod);
            continue;
        }
        break;
    }

    /*
     * First, create or find the module table entry.
     */
    PRTDBGASMOD pMod = (PRTDBGASMOD)RTAvlPVGet(&pDbgAs->ModTree, hDbgMod);
    if (!pMod)
    {
        /*
         * Ok, we need a new entry. Grow the table if necessary.
         */
        if (!(pDbgAs->cModules % 32))
        {
            void *pvNew = RTMemRealloc(pDbgAs->papModules, sizeof(pDbgAs->papModules[0]) * (pDbgAs->cModules + 32));
            if (!pvNew)
                return VERR_NO_MEMORY;
            pDbgAs->papModules = (PRTDBGASMOD *)pvNew;
        }
        pMod = (PRTDBGASMOD)RTMemAlloc(sizeof(*pMod));
        if (!pMod)
            return VERR_NO_MEMORY;
        pMod->Core.Key  = hDbgMod;
        pMod->pMapHead  = NULL;
        pMod->pNextName = NULL;
        if (RT_UNLIKELY(!RTAvlPVInsert(&pDbgAs->ModTree, &pMod->Core)))
        {
            AssertFailed();
            pDbgAs->cModules--;
            RTMemFree(pMod);
            return VERR_INTERNAL_ERROR;
        }
        pMod->iOrdinal = pDbgAs->cModules;
        pDbgAs->papModules[pDbgAs->cModules] = pMod;
        pDbgAs->cModules++;
        RTDbgModRetain(hDbgMod);

        /*
         * Add it to the name space.
         */
        PRTDBGASNAME pName = (PRTDBGASNAME)RTStrSpaceGet(&pDbgAs->NameSpace, pszName);
        if (!pName)
        {
            size_t cchName = strlen(pszName);
            pName = (PRTDBGASNAME)RTMemAlloc(sizeof(*pName) + cchName + 1);
            if (!pName)
            {
                RTDbgModRelease(hDbgMod);
                pDbgAs->cModules--;
                RTAvlPVRemove(&pDbgAs->ModTree, hDbgMod);
                RTMemFree(pMod);
                return VERR_NO_MEMORY;
            }
            pName->StrCore.cchString = cchName;
            pName->StrCore.pszString = (char *)memcpy(pName + 1, pszName, cchName + 1);
            pName->pHead = pMod;
            if (!RTStrSpaceInsert(&pDbgAs->NameSpace, &pName->StrCore))
                AssertFailed();
        }
        else
        {
            /* quick, but unfair. */
            pMod->pNextName = pName->pHead;
            pName->pHead = pMod;
        }
    }

    /*
     * Create a mapping node.
     */
    int rc;
    PRTDBGASMAP pMap = (PRTDBGASMAP)RTMemAlloc(sizeof(*pMap));
    if (pMap)
    {
        pMap->Core.Key      = Addr;
        pMap->Core.KeyLast  = Addr + cb - 1;
        pMap->pMod          = pMod;
        pMap->iSeg          = iSeg;
        if (RTAvlrUIntPtrInsert(&pDbgAs->MapTree, &pMap->Core))
        {
            PRTDBGASMAP *pp = &pMod->pMapHead;
            while (*pp && (*pp)->Core.Key < Addr)
                pp = &(*pp)->pNext;
            pMap->pNext = *pp;
            *pp = pMap;
            return VINF_SUCCESS;
        }

        AssertFailed();
        RTMemFree(pMap);
        rc = VERR_ADDRESS_CONFLICT;
    }
    else
        rc = VERR_NO_MEMORY;

    /*
     * Unlink the module if this was the only mapping.
     */
    if (!pMod->pMapHead)
        rtDbgAsModuleUnlinkMod(pDbgAs, pMod);
    return rc;
}


RTDECL(int) RTDbgAsModuleLink(RTDBGAS hDbgAs, RTDBGMOD hDbgMod, RTUINTPTR ImageAddr, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);
    const char *pszName = RTDbgModName(hDbgMod);
    if (!pszName)
        return VERR_INVALID_HANDLE;
    RTUINTPTR cb = RTDbgModImageSize(hDbgMod);
    if (!cb)
        return VERR_OUT_OF_RANGE;
    if (    ImageAddr           < pDbgAs->FirstAddr
        ||  ImageAddr           > pDbgAs->LastAddr
        ||  ImageAddr + cb - 1  < pDbgAs->FirstAddr
        ||  ImageAddr + cb - 1  > pDbgAs->LastAddr
        ||  ImageAddr + cb - 1  < ImageAddr)
        return VERR_OUT_OF_RANGE;
    AssertReturn(!(fFlags & ~RTDBGASLINK_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Invoke worker common with RTDbgAsModuleLinkSeg.
     */
    RTDBGAS_LOCK_WRITE(pDbgAs);
    int rc = rtDbgAsModuleLinkCommon(pDbgAs, hDbgMod, NIL_RTDBGSEGIDX, ImageAddr, cb, pszName, fFlags);
    RTDBGAS_UNLOCK_WRITE(pDbgAs);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsModuleLink);


RTDECL(int) RTDbgAsModuleLinkSeg(RTDBGAS hDbgAs, RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR SegAddr, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);
    const char *pszName = RTDbgModName(hDbgMod);
    if (!pszName)
        return VERR_INVALID_HANDLE;
    RTUINTPTR cb = RTDbgModSegmentSize(hDbgMod, iSeg);
    if (!cb)
        return VERR_OUT_OF_RANGE;
    if (    SegAddr           < pDbgAs->FirstAddr
        ||  SegAddr           > pDbgAs->LastAddr
        ||  SegAddr + cb - 1  < pDbgAs->FirstAddr
        ||  SegAddr + cb - 1  > pDbgAs->LastAddr
        ||  SegAddr + cb - 1  < SegAddr)
        return VERR_OUT_OF_RANGE;
    AssertReturn(!(fFlags & ~RTDBGASLINK_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Invoke worker common with RTDbgAsModuleLinkSeg.
     */
    RTDBGAS_LOCK_WRITE(pDbgAs);
    int rc = rtDbgAsModuleLinkCommon(pDbgAs, hDbgMod, iSeg, SegAddr, cb, pszName, fFlags);
    RTDBGAS_UNLOCK_WRITE(pDbgAs);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsModuleLinkSeg);


/**
 * Worker for RTDbgAsModuleUnlink, RTDbgAsModuleUnlinkByAddr and rtDbgAsModuleLinkCommon.
 *
 * @param   pDbgAs          Pointer to the address space instance data.
 * @param   pMod            The module to unlink.
 *
 * @remarks The caller must have locked the address space for writing.
 */
static void rtDbgAsModuleUnlinkMod(PRTDBGASINT pDbgAs, PRTDBGASMOD pMod)
{
    Assert(!pMod->pMapHead);

    /*
     * Unlink it from the name.
     */
    const char  *pszName = RTDbgModName((RTDBGMOD)pMod->Core.Key);
    PRTDBGASNAME pName   = (PRTDBGASNAME)RTStrSpaceGet(&pDbgAs->NameSpace, pszName);
    AssertReturnVoid(pName);

    if (pName->pHead == pMod)
        pName->pHead = pMod->pNextName;
    else
        for (PRTDBGASMOD pCur = pName->pHead; pCur; pCur = pCur->pNextName)
            if (pCur->pNextName == pMod)
            {
                pCur->pNextName = pMod->pNextName;
                break;
            }
    pMod->pNextName = NULL;

    /*
     * Free the name if this was the last reference to it.
     */
    if (!pName->pHead)
    {
        pName = (PRTDBGASNAME)RTStrSpaceRemove(&pDbgAs->NameSpace, pName->StrCore.pszString);
        Assert(pName);
        RTMemFree(pName);
    }

    /*
     * Remove it from the module handle tree.
     */
    PAVLPVNODECORE pNode = RTAvlPVRemove(&pDbgAs->ModTree, pMod->Core.Key);
    Assert(pNode == &pMod->Core); NOREF(pNode);

    /*
     * Remove it from the module table by replacing it by the last entry.
     */
    pDbgAs->cModules--;
    uint32_t iMod = pMod->iOrdinal;
    Assert(iMod <= pDbgAs->cModules);
    if (iMod != pDbgAs->cModules)
    {
        PRTDBGASMOD pTailMod = pDbgAs->papModules[pDbgAs->cModules];
        pTailMod->iOrdinal = iMod;
        pDbgAs->papModules[iMod] = pTailMod;
    }
    pMod->iOrdinal = UINT32_MAX;

    /*
     * Free it.
     */
    RTMemFree(pMod);
}


/**
 * Worker for RTDbgAsModuleUnlink and RTDbgAsModuleUnlinkByAddr.
 *
 * @param   pDbgAs          Pointer to the address space instance data.
 * @param   pMap            The map to unlink and free.
 *
 * @remarks The caller must have locked the address space for writing.
 */
static void rtDbgAsModuleUnlinkMap(PRTDBGASINT pDbgAs, PRTDBGASMAP pMap)
{
    /* remove from the tree */
    PAVLRUINTPTRNODECORE pNode = RTAvlrUIntPtrRemove(&pDbgAs->MapTree, pMap->Core.Key);
    Assert(pNode == &pMap->Core); NOREF(pNode);

    /* unlink */
    PRTDBGASMOD pMod = pMap->pMod;
    if (pMod->pMapHead == pMap)
        pMod->pMapHead = pMap->pNext;
    else
    {
        bool fFound = false;
        for (PRTDBGASMAP pCur = pMod->pMapHead; pCur; pCur = pCur->pNext)
            if (pCur->pNext == pMap)
            {
                pCur->pNext = pMap->pNext;
                fFound = true;
                break;
            }
        Assert(fFound);
    }

    /* free it */
    pMap->Core.Key = pMap->Core.KeyLast = 0;
    pMap->pNext = NULL;
    pMap->pMod = NULL;
    RTMemFree(pMap);
}


/**
 * Worker for RTDbgAsModuleUnlinkByAddr and rtDbgAsModuleLinkCommon that
 * unlinks a single mapping and releases the module if it's the last one.
 *
 * @param   pDbgAs      The address space instance.
 * @param   pMap        The mapping to unlink.
 *
 * @remarks The caller must have locked the address space for writing.
 */
static void rtDbgAsModuleUnlinkByMap(PRTDBGASINT pDbgAs, PRTDBGASMAP pMap)
{
    /*
     * Unlink it from the address space.
     * Unlink the module as well if it's the last mapping it has.
     */
    PRTDBGASMOD pMod = pMap->pMod;
    rtDbgAsModuleUnlinkMap(pDbgAs, pMap);
    if (!pMod->pMapHead)
        rtDbgAsModuleUnlinkMod(pDbgAs, pMod);
}


RTDECL(int) RTDbgAsModuleUnlink(RTDBGAS hDbgAs, RTDBGMOD hDbgMod)
{
    /*
     * Validate input.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);
    if (hDbgMod == NIL_RTDBGMOD)
        return VINF_SUCCESS;

    RTDBGAS_LOCK_WRITE(pDbgAs);
    PRTDBGASMOD pMod = (PRTDBGASMOD)RTAvlPVGet(&pDbgAs->ModTree, hDbgMod);
    if (!pMod)
    {
        RTDBGAS_UNLOCK_WRITE(pDbgAs);
        return VERR_NOT_FOUND;
    }

    /*
     * Unmap all everything and release the module.
     */
    while (pMod->pMapHead)
        rtDbgAsModuleUnlinkMap(pDbgAs, pMod->pMapHead);
    rtDbgAsModuleUnlinkMod(pDbgAs, pMod);

    RTDBGAS_UNLOCK_WRITE(pDbgAs);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTDbgAsModuleUnlink);


RTDECL(int) RTDbgAsModuleUnlinkByAddr(RTDBGAS hDbgAs, RTUINTPTR Addr)
{
    /*
     * Validate input.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);

    RTDBGAS_LOCK_WRITE(pDbgAs);
    PRTDBGASMAP pMap = (PRTDBGASMAP)RTAvlrUIntPtrRangeGet(&pDbgAs->MapTree, Addr);
    if (!pMap)
    {
        RTDBGAS_UNLOCK_WRITE(pDbgAs);
        return VERR_NOT_FOUND;
    }

    /*
     * Hand it to
     */
    rtDbgAsModuleUnlinkByMap(pDbgAs, pMap);

    RTDBGAS_UNLOCK_WRITE(pDbgAs);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTDbgAsModuleUnlinkByAddr);


RTDECL(RTDBGMOD) RTDbgAsModuleByIndex(RTDBGAS hDbgAs, uint32_t iModule)
{
    /*
     * Validate input.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, NIL_RTDBGMOD);

    RTDBGAS_LOCK_READ(pDbgAs);
    if (iModule >= pDbgAs->cModules)
    {
        RTDBGAS_UNLOCK_READ(pDbgAs);
        return NIL_RTDBGMOD;
    }

    /*
     * Get, retain and return it.
     */
    RTDBGMOD hMod = (RTDBGMOD)pDbgAs->papModules[iModule]->Core.Key;
    RTDbgModRetain(hMod);

    RTDBGAS_UNLOCK_READ(pDbgAs);
    return hMod;
}
RT_EXPORT_SYMBOL(RTDbgAsModuleByIndex);


RTDECL(int) RTDbgAsModuleByAddr(RTDBGAS hDbgAs, RTUINTPTR Addr, PRTDBGMOD phMod, PRTUINTPTR pAddr, PRTDBGSEGIDX piSeg)
{
    /*
     * Validate input.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);

    RTDBGAS_LOCK_READ(pDbgAs);
    PRTDBGASMAP pMap = (PRTDBGASMAP)RTAvlrUIntPtrRangeGet(&pDbgAs->MapTree, Addr);
    if (!pMap)
    {
        RTDBGAS_UNLOCK_READ(pDbgAs);
        return VERR_NOT_FOUND;
    }

    /*
     * Set up the return values.
     */
    if (phMod)
    {
        RTDBGMOD hMod = (RTDBGMOD)pMap->pMod->Core.Key;
        RTDbgModRetain(hMod);
        *phMod = hMod;
    }
    if (pAddr)
        *pAddr = pMap->Core.Key;
    if (piSeg)
        *piSeg = pMap->iSeg;

    RTDBGAS_UNLOCK_READ(pDbgAs);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTDbgAsModuleByAddr);


RTDECL(int) RTDbgAsModuleByName(RTDBGAS hDbgAs, const char *pszName, uint32_t iName, PRTDBGMOD phMod)
{
    /*
     * Validate input.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);
    AssertPtrReturn(phMod, VERR_INVALID_POINTER);

    RTDBGAS_LOCK_READ(pDbgAs);
    PRTDBGASNAME pName = (PRTDBGASNAME)RTStrSpaceGet(&pDbgAs->NameSpace, pszName);
    if (!pName)
    {
        RTDBGAS_UNLOCK_READ(pDbgAs);
        return VERR_NOT_FOUND;
    }

    PRTDBGASMOD pMod = pName->pHead;
    while (iName-- > 0)
    {
        pMod = pMod->pNextName;
        if (!pMod)
        {
            RTDBGAS_UNLOCK_READ(pDbgAs);
            return VERR_OUT_OF_RANGE;
        }
    }

    /*
     * Get, retain and return it.
     */
    RTDBGMOD hMod = (RTDBGMOD)pMod->Core.Key;
    RTDbgModRetain(hMod);
    *phMod = hMod;

    RTDBGAS_UNLOCK_READ(pDbgAs);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTDbgAsModuleByName);


RTDECL(int) RTDbgAsModuleQueryMapByIndex(RTDBGAS hDbgAs, uint32_t iModule, PRTDBGASMAPINFO paMappings, uint32_t *pcMappings, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    uint32_t const  cMappings = *pcMappings;
    PRTDBGASINT     pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

    RTDBGAS_LOCK_READ(pDbgAs);
    if (iModule >= pDbgAs->cModules)
    {
        RTDBGAS_UNLOCK_READ(pDbgAs);
        return VERR_OUT_OF_RANGE;
    }

    /*
     * Copy the mapping information about the module.
     */
    int         rc    = VINF_SUCCESS;
    PRTDBGASMAP pMap  = pDbgAs->papModules[iModule]->pMapHead;
    uint32_t    cMaps = 0;
    while (pMap)
    {
        if (cMaps >= cMappings)
        {
            rc = VINF_BUFFER_OVERFLOW;
            break;
        }
        paMappings[cMaps].Address = pMap->Core.Key;
        paMappings[cMaps].iSeg    = pMap->iSeg;
        cMaps++;
        pMap = pMap->pNext;
    }

    RTDBGAS_UNLOCK_READ(pDbgAs);
    *pcMappings = cMaps;
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsModuleQueryMapByIndex);


/**
 * Internal worker that looks up and retains a module.
 *
 * @returns Module handle, NIL_RTDBGMOD if not found.
 * @param   pDbgAs          The address space instance data.
 * @param   Addr            Address within the module.
 * @param   piSeg           where to return the segment index.
 * @param   poffSeg         Where to return the segment offset.
 * @param   pMapAddr        The mapping address (RTDBGASMAP::Core.Key).
 */
DECLINLINE(RTDBGMOD) rtDbgAsModuleByAddr(PRTDBGASINT pDbgAs, RTUINTPTR Addr, PRTDBGSEGIDX piSeg, PRTUINTPTR poffSeg, PRTUINTPTR pMapAddr)
{
    RTDBGMOD hMod = NIL_RTDBGMOD;

    RTDBGAS_LOCK_READ(pDbgAs);
    PRTDBGASMAP pMap = (PRTDBGASMAP)RTAvlrUIntPtrRangeGet(&pDbgAs->MapTree, Addr);
    if (pMap)
    {
        hMod = (RTDBGMOD)pMap->pMod->Core.Key;
        RTDbgModRetain(hMod);
        *piSeg = pMap->iSeg != NIL_RTDBGSEGIDX ? pMap->iSeg : RTDBGSEGIDX_RVA;
        *poffSeg = Addr - pMap->Core.Key;
        if (pMapAddr)
            *pMapAddr = pMap->Core.Key;
    }
    RTDBGAS_UNLOCK_READ(pDbgAs);

    return hMod;
}


/**
 * Adjusts the address to correspond to the mapping of the module/segment.
 *
 * @param   pAddr           The address to adjust (in/out).
 * @param   iSeg            The related segment.
 * @param   hDbgMod         The module handle.
 * @param   MapAddr         The mapping address.
 * @param   iMapSeg         The segment that's mapped, NIL_RTDBGSEGIDX or
 *                          RTDBGSEGIDX_RVA if the whole module is mapped here.
 */
DECLINLINE(void) rtDbgAsAdjustAddressByMapping(PRTUINTPTR pAddr, RTDBGSEGIDX iSeg,
                                               RTDBGMOD hDbgMod, RTUINTPTR MapAddr, RTDBGSEGIDX iMapSeg)
{
    if (iSeg == RTDBGSEGIDX_ABS)
        return;

    if (iSeg == RTDBGSEGIDX_RVA)
    {
        if (    iMapSeg == RTDBGSEGIDX_RVA
            ||  iMapSeg == NIL_RTDBGSEGIDX)
            *pAddr += MapAddr;
        else
        {
            RTUINTPTR SegRva = RTDbgModSegmentRva(hDbgMod, iMapSeg);
            AssertReturnVoid(SegRva != RTUINTPTR_MAX);
            AssertMsg(SegRva <= *pAddr, ("SegRva=%RTptr *pAddr=%RTptr\n", SegRva, *pAddr));
            *pAddr += MapAddr - SegRva;
        }
    }
    else
    {
        if (    iMapSeg != RTDBGSEGIDX_RVA
            &&  iMapSeg != NIL_RTDBGSEGIDX)
        {
            Assert(iMapSeg == iSeg);
            *pAddr += MapAddr;
        }
        else
        {
            RTUINTPTR SegRva = RTDbgModSegmentRva(hDbgMod, iSeg);
            AssertReturnVoid(SegRva != RTUINTPTR_MAX);
            *pAddr += MapAddr + SegRva;
        }
    }
}


/**
 * Adjusts the symbol value to correspond to the mapping of the module/segment.
 *
 * @param   pSymbol         The returned symbol info.
 * @param   hDbgMod         The module handle.
 * @param   MapAddr         The mapping address.
 * @param   iMapSeg         The segment that's mapped, NIL_RTDBGSEGIDX if the
 *                          whole module is mapped here.
 */
DECLINLINE(void) rtDbgAsAdjustSymbolValue(PRTDBGSYMBOL pSymbol, RTDBGMOD hDbgMod, RTUINTPTR MapAddr, RTDBGSEGIDX iMapSeg)
{
    Assert(pSymbol->iSeg   != NIL_RTDBGSEGIDX);
    Assert(pSymbol->offSeg == pSymbol->Value);
    rtDbgAsAdjustAddressByMapping(&pSymbol->Value, pSymbol->iSeg, hDbgMod, MapAddr, iMapSeg);
}


/**
 * Adjusts the line number address to correspond to the mapping of the module/segment.
 *
 * @param   pLine           The returned line number info.
 * @param   hDbgMod         The module handle.
 * @param   MapAddr         The mapping address.
 * @param   iMapSeg         The segment that's mapped, NIL_RTDBGSEGIDX if the
 *                          whole module is mapped here.
 */
DECLINLINE(void) rtDbgAsAdjustLineAddress(PRTDBGLINE pLine, RTDBGMOD hDbgMod, RTUINTPTR MapAddr, RTDBGSEGIDX iMapSeg)
{
    Assert(pLine->iSeg   != NIL_RTDBGSEGIDX);
    Assert(pLine->offSeg == pLine->Address);
    rtDbgAsAdjustAddressByMapping(&pLine->Address, pLine->iSeg, hDbgMod, MapAddr, iMapSeg);
}


RTDECL(int) RTDbgAsSymbolAdd(RTDBGAS hDbgAs, const char *pszSymbol, RTUINTPTR Addr, RTUINTPTR cb, uint32_t fFlags, uint32_t *piOrdinal)
{
    /*
    * Validate input and resolve the address.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);

    RTDBGSEGIDX iSeg   = NIL_RTDBGSEGIDX; /* shut up gcc */
    RTUINTPTR   offSeg = 0;
    RTDBGMOD    hMod   = rtDbgAsModuleByAddr(pDbgAs, Addr, &iSeg, &offSeg, NULL);
    if (hMod == NIL_RTDBGMOD)
        return VERR_NOT_FOUND;

    /*
     * Forward the call.
     */
    int rc = RTDbgModSymbolAdd(hMod, pszSymbol, iSeg, offSeg, cb, fFlags, piOrdinal);
    RTDbgModRelease(hMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsSymbolAdd);


/**
 * Creates a snapshot of the module table on the temporary heap.
 *
 * The caller must release all the module handles before freeing the table
 * using RTMemTmpFree.
 *
 * @returns Module table snaphot.
 * @param   pDbgAs          The address space instance data.
 * @param   pcModules       Where to return the number of modules.
 */
static PRTDBGMOD rtDbgAsSnapshotModuleTable(PRTDBGASINT pDbgAs, uint32_t *pcModules)
{
    RTDBGAS_LOCK_READ(pDbgAs);

    uint32_t iMod = *pcModules = pDbgAs->cModules;
    PRTDBGMOD pahModules = (PRTDBGMOD)RTMemTmpAlloc(sizeof(pahModules[0]) * RT_MAX(iMod, 1));
    if (pahModules)
    {
        while (iMod-- > 0)
        {
            RTDBGMOD hMod = (RTDBGMOD)pDbgAs->papModules[iMod]->Core.Key;
            pahModules[iMod] = hMod;
            RTDbgModRetain(hMod);
        }
    }

    RTDBGAS_UNLOCK_READ(pDbgAs);
    return pahModules;
}


RTDECL(int) RTDbgAsSymbolByAddr(RTDBGAS hDbgAs, RTUINTPTR Addr, uint32_t fFlags,
                                PRTINTPTR poffDisp, PRTDBGSYMBOL pSymbol, PRTDBGMOD phMod)
{
    /*
     * Validate input and resolve the address.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);
    if (phMod)
        *phMod = NIL_RTDBGMOD;

    RTDBGSEGIDX iSeg    = NIL_RTDBGSEGIDX; /* shut up gcc */
    RTUINTPTR   offSeg  = 0;
    RTUINTPTR   MapAddr = 0;
    RTDBGMOD    hMod    = rtDbgAsModuleByAddr(pDbgAs, Addr, &iSeg, &offSeg, &MapAddr);
    if (hMod == NIL_RTDBGMOD)
    {
        /*
         * Check for absolute symbols.  Requires iterating all modules.
         */
        if (fFlags & RTDBGSYMADDR_FLAGS_SKIP_ABS)
            return VERR_NOT_FOUND;

        uint32_t cModules;
        PRTDBGMOD pahModules = rtDbgAsSnapshotModuleTable(pDbgAs, &cModules);
        if (!pahModules)
            return VERR_NO_TMP_MEMORY;

        int      rc;
        RTINTPTR offBestDisp = RTINTPTR_MAX;
        uint32_t iBest       = UINT32_MAX;
        for (uint32_t i = 0; i < cModules; i++)
        {
            RTINTPTR offDisp;
            rc = RTDbgModSymbolByAddr(pahModules[i], RTDBGSEGIDX_ABS, Addr, fFlags, &offDisp, pSymbol);
            if (RT_SUCCESS(rc) && RT_ABS(offDisp) < offBestDisp)
            {
                offBestDisp = RT_ABS(offDisp);
                iBest = i;
            }
        }

        if (iBest == UINT32_MAX)
            rc = VERR_NOT_FOUND;
        else
        {
            hMod = pahModules[iBest];
            rc = RTDbgModSymbolByAddr(hMod, RTDBGSEGIDX_ABS, Addr, fFlags, poffDisp, pSymbol);
            if (RT_SUCCESS(rc))
            {
                rtDbgAsAdjustSymbolValue(pSymbol, hMod, MapAddr, iSeg);
                if (phMod)
                    RTDbgModRetain(*phMod = hMod);
            }
        }

        for (uint32_t i = 0; i < cModules; i++)
            RTDbgModRelease(pahModules[i]);
        RTMemTmpFree(pahModules);
        return rc;
    }

    /*
     * Forward the call.
     */
    int rc = RTDbgModSymbolByAddr(hMod, iSeg, offSeg, fFlags, poffDisp, pSymbol);
    if (RT_SUCCESS(rc))
        rtDbgAsAdjustSymbolValue(pSymbol, hMod, MapAddr, iSeg);
    if (phMod)
        *phMod = hMod;
    else
        RTDbgModRelease(hMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsSymbolByAddr);


RTDECL(int) RTDbgAsSymbolByAddrA(RTDBGAS hDbgAs, RTUINTPTR Addr, uint32_t fFlags,
                                 PRTINTPTR poffDisp, PRTDBGSYMBOL *ppSymInfo, PRTDBGMOD phMod)
{
    /*
     * Validate input and resolve the address.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);

    RTDBGSEGIDX iSeg    = NIL_RTDBGSEGIDX;
    RTUINTPTR   offSeg  = 0;
    RTUINTPTR   MapAddr = 0;
    RTDBGMOD    hMod    = rtDbgAsModuleByAddr(pDbgAs, Addr, &iSeg, &offSeg, &MapAddr);
    if (hMod == NIL_RTDBGMOD)
    {
        if (phMod)
            *phMod = NIL_RTDBGMOD;
        return VERR_NOT_FOUND;
    }

    /*
     * Forward the call.
     */
    int rc = RTDbgModSymbolByAddrA(hMod, iSeg, offSeg, fFlags, poffDisp, ppSymInfo);
    if (RT_SUCCESS(rc))
        rtDbgAsAdjustSymbolValue(*ppSymInfo, hMod, MapAddr, iSeg);
    if (phMod)
        *phMod = hMod;
    else
        RTDbgModRelease(hMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsSymbolByAddrA);


/**
 * Attempts to find a mapping of the specified symbol/module and
 * adjust it's Value field accordingly.
 *
 * @returns true / false success indicator.
 * @param   pDbgAs          The address space.
 * @param   hDbgMod         The module handle.
 * @param   pSymbol         The symbol info.
 */
static bool rtDbgAsFindMappingAndAdjustSymbolValue(PRTDBGASINT pDbgAs, RTDBGMOD hDbgMod, PRTDBGSYMBOL pSymbol)
{
    /*
     * Absolute segments needs no fixing.
     */
    RTDBGSEGIDX const iSeg = pSymbol->iSeg;
    if (iSeg == RTDBGSEGIDX_ABS)
        return true;

    RTDBGAS_LOCK_READ(pDbgAs);

    /*
     * Lookup up the module by it's handle and iterate the mappings looking for one
     * that either encompasses the entire module or the segment in question.
     */
    PRTDBGASMOD pMod = (PRTDBGASMOD)RTAvlPVGet(&pDbgAs->ModTree, hDbgMod);
    if (pMod)
    {
        for (PRTDBGASMAP pMap = pMod->pMapHead; pMap; pMap = pMap->pNext)
        {
            /* Exact segment match or full-mapping. */
            if (    iSeg == pMap->iSeg
                ||  pMap->iSeg == NIL_RTDBGSEGIDX)
            {
                RTUINTPTR   MapAddr = pMap->Core.Key;
                RTDBGSEGIDX iMapSeg = pMap->iSeg;

                RTDBGAS_UNLOCK_READ(pDbgAs);
                rtDbgAsAdjustSymbolValue(pSymbol, hDbgMod, MapAddr, iMapSeg);
                return true;
            }

            /* Symbol uses RVA and the mapping doesn't, see if it's in the mapped segment. */
            if (iSeg == RTDBGSEGIDX_RVA)
            {
                Assert(pMap->iSeg != NIL_RTDBGSEGIDX);
                RTUINTPTR SegRva = RTDbgModSegmentRva(hDbgMod, pMap->iSeg);
                Assert(SegRva != RTUINTPTR_MAX);
                RTUINTPTR cbSeg = RTDbgModSegmentSize(hDbgMod, pMap->iSeg);
                if (SegRva - pSymbol->Value < cbSeg)
                {
                    RTUINTPTR   MapAddr = pMap->Core.Key;
                    RTDBGSEGIDX iMapSeg = pMap->iSeg;

                    RTDBGAS_UNLOCK_READ(pDbgAs);
                    rtDbgAsAdjustSymbolValue(pSymbol, hDbgMod, MapAddr, iMapSeg);
                    return true;
                }
            }
        }
    }
    /* else: Unmapped while we were searching. */

    RTDBGAS_UNLOCK_READ(pDbgAs);
    return false;
}


RTDECL(int) RTDbgAsSymbolByName(RTDBGAS hDbgAs, const char *pszSymbol, PRTDBGSYMBOL pSymbol, PRTDBGMOD phMod)
{
    /*
     * Validate input.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszSymbol, VERR_INVALID_POINTER);
    AssertPtrReturn(pSymbol, VERR_INVALID_POINTER);

    /*
     * Look for module pattern.
     */
    const char *pachModPat = NULL;
    size_t      cchModPat  = 0;
    const char *pszBang    = strchr(pszSymbol, '!');
    if (pszBang)
    {
        pachModPat = pszSymbol;
        cchModPat = pszBang - pszSymbol;
        pszSymbol = pszBang + 1;
        if (!*pszSymbol)
            return VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE;
        /* Note! Zero length module -> no pattern -> escape for symbol with '!'. */
    }

    /*
     * Iterate the modules, looking for the symbol.
     */
    uint32_t cModules;
    PRTDBGMOD pahModules = rtDbgAsSnapshotModuleTable(pDbgAs, &cModules);
    if (!pahModules)
        return VERR_NO_TMP_MEMORY;

    for (uint32_t i = 0; i < cModules; i++)
    {
        if (    cchModPat == 0
            ||  RTStrSimplePatternNMatch(pachModPat, cchModPat, RTDbgModName(pahModules[i]), RTSTR_MAX))
        {
            int rc = RTDbgModSymbolByName(pahModules[i], pszSymbol, pSymbol);
            if (RT_SUCCESS(rc))
            {
                if (rtDbgAsFindMappingAndAdjustSymbolValue(pDbgAs, pahModules[i], pSymbol))
                {
                    if (phMod)
                        RTDbgModRetain(*phMod = pahModules[i]);
                    for (; i < cModules; i++)
                        RTDbgModRelease(pahModules[i]);
                    RTMemTmpFree(pahModules);
                    return rc;
                }
            }
        }
        RTDbgModRelease(pahModules[i]);
    }

    RTMemTmpFree(pahModules);
    return VERR_SYMBOL_NOT_FOUND;
}
RT_EXPORT_SYMBOL(RTDbgAsSymbolByName);


RTDECL(int) RTDbgAsSymbolByNameA(RTDBGAS hDbgAs, const char *pszSymbol, PRTDBGSYMBOL *ppSymbol, PRTDBGMOD phMod)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(ppSymbol, VERR_INVALID_POINTER);
    *ppSymbol = NULL;
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszSymbol, VERR_INVALID_POINTER);

    /*
     * Look for module pattern.
     */
    const char *pachModPat = NULL;
    size_t      cchModPat  = 0;
    const char *pszBang    = strchr(pszSymbol, '!');
    if (pszBang)
    {
        pachModPat = pszSymbol;
        cchModPat = pszBang - pszSymbol;
        pszSymbol = pszBang + 1;
        if (!*pszSymbol)
            return VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE;
        /* Note! Zero length module -> no pattern -> escape for symbol with '!'. */
    }

    /*
     * Iterate the modules, looking for the symbol.
     */
    uint32_t cModules;
    PRTDBGMOD pahModules = rtDbgAsSnapshotModuleTable(pDbgAs, &cModules);
    if (!pahModules)
        return VERR_NO_TMP_MEMORY;

    for (uint32_t i = 0; i < cModules; i++)
    {
        if (    cchModPat == 0
            ||  RTStrSimplePatternNMatch(pachModPat, cchModPat, RTDbgModName(pahModules[i]), RTSTR_MAX))
        {
            int rc = RTDbgModSymbolByNameA(pahModules[i], pszSymbol, ppSymbol);
            if (RT_SUCCESS(rc))
            {
                if (rtDbgAsFindMappingAndAdjustSymbolValue(pDbgAs, pahModules[i], *ppSymbol))
                {
                    if (phMod)
                        RTDbgModRetain(*phMod = pahModules[i]);
                    for (; i < cModules; i++)
                        RTDbgModRelease(pahModules[i]);
                    RTMemTmpFree(pahModules);
                    return rc;
                }
            }
        }
        RTDbgModRelease(pahModules[i]);
    }

    RTMemTmpFree(pahModules);
    return VERR_SYMBOL_NOT_FOUND;
}
RT_EXPORT_SYMBOL(RTDbgAsSymbolByNameA);


RTDECL(int) RTDbgAsLineAdd(RTDBGAS hDbgAs, const char *pszFile, uint32_t uLineNo, RTUINTPTR Addr, uint32_t *piOrdinal)
{
    /*
    * Validate input and resolve the address.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);

    RTDBGSEGIDX iSeg   = NIL_RTDBGSEGIDX; /* shut up gcc */
    RTUINTPTR   offSeg = 0;               /* ditto */
    RTDBGMOD    hMod   = rtDbgAsModuleByAddr(pDbgAs, Addr, &iSeg, &offSeg, NULL);
    if (hMod == NIL_RTDBGMOD)
        return VERR_NOT_FOUND;

    /*
     * Forward the call.
     */
    int rc = RTDbgModLineAdd(hMod, pszFile, uLineNo, iSeg, offSeg, piOrdinal);
    RTDbgModRelease(hMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsLineAdd);


RTDECL(int) RTDbgAsLineByAddr(RTDBGAS hDbgAs, RTUINTPTR Addr, PRTINTPTR poffDisp, PRTDBGLINE pLine, PRTDBGMOD phMod)
{
    /*
     * Validate input and resolve the address.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);

    RTDBGSEGIDX iSeg    = NIL_RTDBGSEGIDX; /* shut up gcc */
    RTUINTPTR   offSeg  = 0;
    RTUINTPTR   MapAddr = 0;
    RTDBGMOD    hMod    = rtDbgAsModuleByAddr(pDbgAs, Addr, &iSeg, &offSeg, &MapAddr);
    if (hMod == NIL_RTDBGMOD)
        return VERR_NOT_FOUND;

    /*
     * Forward the call.
     */
    int rc = RTDbgModLineByAddr(hMod, iSeg, offSeg, poffDisp, pLine);
    if (RT_SUCCESS(rc))
    {
        rtDbgAsAdjustLineAddress(pLine, hMod, MapAddr, iSeg);
        if (phMod)
            *phMod = hMod;
        else
            RTDbgModRelease(hMod);
    }
    else
        RTDbgModRelease(hMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsLineByAddr);


RTDECL(int) RTDbgAsLineByAddrA(RTDBGAS hDbgAs, RTUINTPTR Addr, PRTINTPTR poffDisp, PRTDBGLINE *ppLine, PRTDBGMOD phMod)
{
    /*
     * Validate input and resolve the address.
     */
    PRTDBGASINT pDbgAs = hDbgAs;
    RTDBGAS_VALID_RETURN_RC(pDbgAs, VERR_INVALID_HANDLE);

    RTDBGSEGIDX iSeg    = NIL_RTDBGSEGIDX; /* shut up gcc */
    RTUINTPTR   offSeg  = 0;
    RTUINTPTR   MapAddr = 0;
    RTDBGMOD    hMod    = rtDbgAsModuleByAddr(pDbgAs, Addr, &iSeg, &offSeg, &MapAddr);
    if (hMod == NIL_RTDBGMOD)
        return VERR_NOT_FOUND;

    /*
     * Forward the call.
     */
    int rc = RTDbgModLineByAddrA(hMod, iSeg, offSeg, poffDisp, ppLine);
    if (RT_SUCCESS(rc))
    {
        rtDbgAsAdjustLineAddress(*ppLine, hMod, MapAddr, iSeg);
        if (phMod)
            *phMod = hMod;
        else
            RTDbgModRelease(hMod);
    }
    else
        RTDbgModRelease(hMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgAsLineByAddrA);

