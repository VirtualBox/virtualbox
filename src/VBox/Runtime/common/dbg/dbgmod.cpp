/* $Id: dbgmod.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Module Interpreter.
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
#define LOG_GROUP RTLOGGROUP_DBG
#include <iprt/dbg.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/avl.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/strcache.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include "internal/dbgmod.h"
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Debug info interpreter registration record. */
typedef struct RTDBGMODREGDBG
{
    /** Pointer to the next record. */
    struct RTDBGMODREGDBG  *pNext;
    /** Pointer to the virtual function table for the interpreter.  */
    PCRTDBGMODVTDBG         pVt;
    /** Usage counter.  */
    uint32_t volatile       cUsers;
} RTDBGMODREGDBG;
typedef RTDBGMODREGDBG *PRTDBGMODREGDBG;

/** Image interpreter registration record. */
typedef struct RTDBGMODREGIMG
{
    /** Pointer to the next record. */
    struct RTDBGMODREGIMG  *pNext;
    /** Pointer to the virtual function table for the interpreter.  */
    PCRTDBGMODVTIMG         pVt;
    /** Usage counter.  */
    uint32_t volatile       cUsers;
} RTDBGMODREGIMG;
typedef RTDBGMODREGIMG *PRTDBGMODREGIMG;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates a debug module handle and returns rc if not valid. */
#define RTDBGMOD_VALID_RETURN_RC(pDbgMod, rc) \
    do { \
        AssertPtrReturn((pDbgMod), (rc)); \
        AssertReturn((pDbgMod)->u32Magic == RTDBGMOD_MAGIC, (rc)); \
        AssertReturn((pDbgMod)->cRefs > 0, (rc)); \
    } while (0)

/** Locks the debug module. */
#define RTDBGMOD_LOCK(pDbgMod) \
    do { \
        int rcLock = RTCritSectEnter(&(pDbgMod)->CritSect); \
        AssertRC(rcLock); \
    } while (0)

/** Unlocks the debug module. */
#define RTDBGMOD_UNLOCK(pDbgMod) \
    do { \
        int rcLock = RTCritSectLeave(&(pDbgMod)->CritSect); \
        AssertRC(rcLock); \
    } while (0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Init once object for lazy registration of the built-in image and debug
 * info interpreters. */
static RTONCE           g_rtDbgModOnce = RTONCE_INITIALIZER;
/** Read/Write semaphore protecting the list of registered interpreters.  */
static RTSEMRW          g_hDbgModRWSem = NIL_RTSEMRW;
/** List of registered image interpreters.  */
static PRTDBGMODREGIMG  g_pImgHead;
/** List of registered debug infor interpreters.  */
static PRTDBGMODREGDBG  g_pDbgHead;
/** String cache for the debug info interpreters.
 * RTSTRCACHE is thread safe. */
DECL_HIDDEN_DATA(RTSTRCACHE)  g_hDbgModStrCache = NIL_RTSTRCACHE;





/**
 * Cleanup debug info interpreter globals.
 *
 * @param   enmReason           The cause of the termination.
 * @param   iStatus             The meaning of this depends on enmReason.
 * @param   pvUser              User argument, unused.
 */
static DECLCALLBACK(void) rtDbgModTermCallback(RTTERMREASON enmReason, int32_t iStatus, void *pvUser)
{
    NOREF(iStatus); NOREF(pvUser);
    if (enmReason == RTTERMREASON_UNLOAD)
    {
        RTSemRWDestroy(g_hDbgModRWSem);
        g_hDbgModRWSem = NIL_RTSEMRW;

        RTStrCacheDestroy(g_hDbgModStrCache);
        g_hDbgModStrCache = NIL_RTSTRCACHE;

        PRTDBGMODREGDBG pDbg = g_pDbgHead;
        g_pDbgHead = NULL;
        while (pDbg)
        {
            PRTDBGMODREGDBG pNext = pDbg->pNext;
            AssertMsg(pDbg->cUsers == 0, ("%#x %s\n", pDbg->cUsers, pDbg->pVt->pszName));
            RTMemFree(pDbg);
            pDbg = pNext;
        }

        PRTDBGMODREGIMG pImg = g_pImgHead;
        g_pImgHead = NULL;
        while (pImg)
        {
            PRTDBGMODREGIMG pNext = pImg->pNext;
            AssertMsg(pImg->cUsers == 0, ("%#x %s\n", pImg->cUsers, pImg->pVt->pszName));
            RTMemFree(pImg);
            pImg = pNext;
        }
    }
}


/**
 * Internal worker for register a debug interpreter.
 *
 * Called while owning the write lock or when locking isn't required.
 *
 * @returns IPRT status code.
 * @retval  VERR_NO_MEMORY
 * @retval  VERR_ALREADY_EXISTS
 *
 * @param   pVt                 The virtual function table of the debug
 *                              module interpreter.
 */
static int rtDbgModDebugInterpreterRegister(PCRTDBGMODVTDBG pVt)
{
    /*
     * Search or duplicate registration.
     */
    PRTDBGMODREGDBG pPrev = NULL;
    for (PRTDBGMODREGDBG pCur = g_pDbgHead; pCur; pCur = pCur->pNext)
    {
        if (pCur->pVt == pVt)
            return VERR_ALREADY_EXISTS;
        if (!strcmp(pCur->pVt->pszName, pVt->pszName))
            return VERR_ALREADY_EXISTS;
        pPrev = pCur;
    }

    /*
     * Create a new record and add it to the end of the list.
     */
    PRTDBGMODREGDBG pReg = (PRTDBGMODREGDBG)RTMemAlloc(sizeof(*pReg));
    if (!pReg)
        return VERR_NO_MEMORY;
    pReg->pVt    = pVt;
    pReg->cUsers = 0;
    pReg->pNext  = NULL;
    if (pPrev)
        pPrev->pNext = pReg;
    else
        g_pDbgHead   = pReg;
    return VINF_SUCCESS;
}


/**
 * Internal worker for register a image interpreter.
 *
 * Called while owning the write lock or when locking isn't required.
 *
 * @returns IPRT status code.
 * @retval  VERR_NO_MEMORY
 * @retval  VERR_ALREADY_EXISTS
 *
 * @param   pVt                 The virtual function table of the image
 *                              interpreter.
 */
static int rtDbgModImageInterpreterRegister(PCRTDBGMODVTIMG pVt)
{
    /*
     * Search or duplicate registration.
     */
    PRTDBGMODREGIMG pPrev = NULL;
    for (PRTDBGMODREGIMG pCur = g_pImgHead; pCur; pCur = pCur->pNext)
    {
        if (pCur->pVt == pVt)
            return VERR_ALREADY_EXISTS;
        if (!strcmp(pCur->pVt->pszName, pVt->pszName))
            return VERR_ALREADY_EXISTS;
        pPrev = pCur;
    }

    /*
     * Create a new record and add it to the end of the list.
     */
    PRTDBGMODREGIMG pReg = (PRTDBGMODREGIMG)RTMemAlloc(sizeof(*pReg));
    if (!pReg)
        return VERR_NO_MEMORY;
    pReg->pVt    = pVt;
    pReg->cUsers = 0;
    pReg->pNext  = NULL;
    if (pPrev)
        pPrev->pNext = pReg;
    else
        g_pImgHead   = pReg;
    return VINF_SUCCESS;
}


/**
 * Do-once callback that initializes the read/write semaphore and registers
 * the built-in interpreters.
 *
 * @returns IPRT status code.
 * @param   pvUser      NULL.
 */
static DECLCALLBACK(int) rtDbgModInitOnce(void *pvUser)
{
    NOREF(pvUser);

    /*
     * Create the semaphore and string cache.
     */
    int rc = RTSemRWCreate(&g_hDbgModRWSem);
    AssertRCReturn(rc, rc);

    rc = RTStrCacheCreate(&g_hDbgModStrCache, "RTDBGMOD");
    if (RT_SUCCESS(rc))
    {
        /*
         * Register the interpreters.
         */
        rc = rtDbgModDebugInterpreterRegister(&g_rtDbgModVtDbgNm);
        if (RT_SUCCESS(rc))
            rc = rtDbgModDebugInterpreterRegister(&g_rtDbgModVtDbgMapSym);
        if (RT_SUCCESS(rc))
            rc = rtDbgModDebugInterpreterRegister(&g_rtDbgModVtDbgDwarf);
        if (RT_SUCCESS(rc))
            rc = rtDbgModDebugInterpreterRegister(&g_rtDbgModVtDbgCodeView);
#ifdef IPRT_WITH_GHIDRA_DBG_MOD
        if (RT_SUCCESS(rc))
            rc = rtDbgModDebugInterpreterRegister(&g_rtDbgModVtDbgGhidra);
#endif
#ifdef RT_OS_WINDOWS
        if (RT_SUCCESS(rc))
            rc = rtDbgModDebugInterpreterRegister(&g_rtDbgModVtDbgDbgHelp);
#else
        if (RT_SUCCESS(rc))
            rc = rtDbgModDebugInterpreterRegister(&g_rtDbgModVtDbgPdb);
#endif
        if (RT_SUCCESS(rc))
            rc = rtDbgModImageInterpreterRegister(&g_rtDbgModVtImgLdr);
        if (RT_SUCCESS(rc))
        {
            /*
             * Finally, register the IPRT cleanup callback.
             */
            rc = RTTermRegisterCallback(rtDbgModTermCallback, NULL);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

            /* bail out: use the termination callback. */
        }
    }
    else
        g_hDbgModStrCache = NIL_RTSTRCACHE;
    rtDbgModTermCallback(RTTERMREASON_UNLOAD, 0, NULL);
    return rc;
}


/**
 * Performs lazy init of our global variables.
 * @returns IPRT status code.
 */
DECLINLINE(int) rtDbgModLazyInit(void)
{
    return RTOnce(&g_rtDbgModOnce, rtDbgModInitOnce, NULL);
}


RTDECL(int) RTDbgModCreate(PRTDBGMOD phDbgMod, const char *pszName, RTUINTPTR cbSeg, uint32_t fFlags)
{
    /*
     * Input validation and lazy initialization.
     */
    AssertPtrReturn(phDbgMod, VERR_INVALID_POINTER);
    *phDbgMod = NIL_RTDBGMOD;
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertReturn(*pszName, VERR_INVALID_PARAMETER);
    AssertReturn(fFlags == 0 || fFlags == RTDBGMOD_F_NOT_DEFERRED, VERR_INVALID_FLAGS);

    int rc = rtDbgModLazyInit();
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Allocate a new module instance.
     */
    PRTDBGMODINT pDbgMod = (PRTDBGMODINT)RTMemAllocZ(sizeof(*pDbgMod));
    if (!pDbgMod)
        return VERR_NO_MEMORY;
    pDbgMod->u32Magic = RTDBGMOD_MAGIC;
    pDbgMod->cRefs = 1;
    rc = RTCritSectInit(&pDbgMod->CritSect);
    if (RT_SUCCESS(rc))
    {
        pDbgMod->pszImgFileSpecified = RTStrCacheEnter(g_hDbgModStrCache, pszName);
        pDbgMod->pszName = RTStrCacheEnterLower(g_hDbgModStrCache, RTPathFilenameEx(pszName, RTPATH_STR_F_STYLE_DOS));
        if (pDbgMod->pszName)
        {
            rc = rtDbgModContainerCreate(pDbgMod, cbSeg);
            if (RT_SUCCESS(rc))
            {
                *phDbgMod = pDbgMod;
                return rc;
            }
            RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszImgFile);
            RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszName);
        }
        RTCritSectDelete(&pDbgMod->CritSect);
    }

    RTMemFree(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModCreate);


RTDECL(int) RTDbgModCreateFromMap(PRTDBGMOD phDbgMod, const char *pszFilename, const char *pszName,
                                  RTUINTPTR uSubtrahend, RTDBGCFG hDbgCfg)
{
    RT_NOREF_PV(hDbgCfg);

    /*
     * Input validation and lazy initialization.
     */
    AssertPtrReturn(phDbgMod, VERR_INVALID_POINTER);
    *phDbgMod = NIL_RTDBGMOD;
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszName, VERR_INVALID_POINTER);
    AssertReturn(uSubtrahend == 0, VERR_NOT_IMPLEMENTED); /** @todo implement uSubtrahend. */

    int rc = rtDbgModLazyInit();
    if (RT_FAILURE(rc))
        return rc;

    if (!pszName)
        pszName = RTPathFilenameEx(pszFilename, RTPATH_STR_F_STYLE_DOS);

    /*
     * Allocate a new module instance.
     */
    PRTDBGMODINT pDbgMod = (PRTDBGMODINT)RTMemAllocZ(sizeof(*pDbgMod));
    if (!pDbgMod)
        return VERR_NO_MEMORY;
    pDbgMod->u32Magic = RTDBGMOD_MAGIC;
    pDbgMod->cRefs = 1;
    rc = RTCritSectInit(&pDbgMod->CritSect);
    if (RT_SUCCESS(rc))
    {
        pDbgMod->pszName = RTStrCacheEnterLower(g_hDbgModStrCache, pszName);
        if (pDbgMod->pszName)
        {
            pDbgMod->pszDbgFile = RTStrCacheEnter(g_hDbgModStrCache, pszFilename);
            if (pDbgMod->pszDbgFile)
            {
                /*
                 * Try the map file readers.
                 */
                rc = RTSemRWRequestRead(g_hDbgModRWSem, RT_INDEFINITE_WAIT);
                if (RT_SUCCESS(rc))
                {
                    for (PRTDBGMODREGDBG pCur = g_pDbgHead; pCur; pCur = pCur->pNext)
                    {
                        if (pCur->pVt->fSupports & RT_DBGTYPE_MAP)
                        {
                            pDbgMod->pDbgVt = pCur->pVt;
                            pDbgMod->pvDbgPriv = NULL;
                            rc = pCur->pVt->pfnTryOpen(pDbgMod, RTLDRARCH_WHATEVER, hDbgCfg);
                            if (RT_SUCCESS(rc))
                            {
                                ASMAtomicIncU32(&pCur->cUsers);
                                RTSemRWReleaseRead(g_hDbgModRWSem);

                                *phDbgMod = pDbgMod;
                                return rc;
                            }
                        }
                    }

                    /* bail out */
                    rc = VERR_DBG_NO_MATCHING_INTERPRETER;
                    RTSemRWReleaseRead(g_hDbgModRWSem);
                }
                RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszName);
            }
            else
                rc = VERR_NO_STR_MEMORY;
            RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszDbgFile);
        }
        else
            rc = VERR_NO_STR_MEMORY;
        RTCritSectDelete(&pDbgMod->CritSect);
    }

    RTMemFree(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModCreateFromMap);



/*
 *
 *  E x e c u t a b l e   I m a g e   F i l e s
 *  E x e c u t a b l e   I m a g e   F i l e s
 *  E x e c u t a b l e   I m a g e   F i l e s
 *
 */


/**
 * Opens debug information for an image.
 *
 * @returns IPRT status code
 * @param   pDbgMod             The debug module structure.
 *
 * @note    This will generally not look for debug info stored in external
 *          files.  rtDbgModFromPeImageExtDbgInfoCallback can help with that.
 */
static int rtDbgModOpenDebugInfoInsideImage(PRTDBGMODINT pDbgMod)
{
    AssertReturn(!pDbgMod->pDbgVt, VERR_DBG_MOD_IPE);
    AssertReturn(pDbgMod->pImgVt, VERR_DBG_MOD_IPE);

    int rc = RTSemRWRequestRead(g_hDbgModRWSem, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        for (PRTDBGMODREGDBG pDbg = g_pDbgHead; pDbg; pDbg = pDbg->pNext)
        {
            pDbgMod->pDbgVt    = pDbg->pVt;
            pDbgMod->pvDbgPriv = NULL;
            rc = pDbg->pVt->pfnTryOpen(pDbgMod, pDbgMod->pImgVt->pfnGetArch(pDbgMod), NIL_RTDBGCFG);
            if (RT_SUCCESS(rc))
            {
                /*
                 * That's it!
                 */
                ASMAtomicIncU32(&pDbg->cUsers);
                RTSemRWReleaseRead(g_hDbgModRWSem);
                return VINF_SUCCESS;
            }

            pDbgMod->pDbgVt = NULL;
            Assert(pDbgMod->pvDbgPriv == NULL);
        }
        RTSemRWReleaseRead(g_hDbgModRWSem);
    }

    return VERR_DBG_NO_MATCHING_INTERPRETER;
}


/** @callback_method_impl{FNRTDBGCFGOPEN} */
static DECLCALLBACK(int) rtDbgModExtDbgInfoOpenCallback(RTDBGCFG hDbgCfg, const char *pszFilename, void *pvUser1, void *pvUser2)
{
    PRTDBGMODINT        pDbgMod   = (PRTDBGMODINT)pvUser1;
    PCRTLDRDBGINFO      pDbgInfo  = (PCRTLDRDBGINFO)pvUser2;
    RT_NOREF_PV(pDbgInfo); /** @todo consider a more direct search for a interpreter. */

    Assert(!pDbgMod->pDbgVt);
    Assert(!pDbgMod->pvDbgPriv);
    Assert(!pDbgMod->pszDbgFile);
    Assert(pDbgMod->pImgVt);

    /*
     * Set the debug file name and try possible interpreters.
     */
    pDbgMod->pszDbgFile = RTStrCacheEnter(g_hDbgModStrCache, pszFilename);

    int rc = RTSemRWRequestRead(g_hDbgModRWSem, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        for (PRTDBGMODREGDBG pDbg = g_pDbgHead; pDbg; pDbg = pDbg->pNext)
        {
            pDbgMod->pDbgVt    = pDbg->pVt;
            pDbgMod->pvDbgPriv = NULL;
            rc = pDbg->pVt->pfnTryOpen(pDbgMod, pDbgMod->pImgVt->pfnGetArch(pDbgMod), hDbgCfg);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Got it!
                 */
                ASMAtomicIncU32(&pDbg->cUsers);
                RTSemRWReleaseRead(g_hDbgModRWSem);
                return VINF_CALLBACK_RETURN;
            }

            pDbgMod->pDbgVt = NULL;
            Assert(pDbgMod->pvDbgPriv == NULL);
        }
        RTSemRWReleaseRead(g_hDbgModRWSem);
    }

    /* No joy. */
    RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszDbgFile);
    pDbgMod->pszDbgFile = NULL;
    return rc;
}


/**
 * Argument package used by rtDbgModOpenDebugInfoExternalToImage.
 */
typedef struct RTDBGMODOPENDIETI
{
    PRTDBGMODINT    pDbgMod;
    RTDBGCFG        hDbgCfg;
} RTDBGMODOPENDIETI;


/** @callback_method_impl{FNRTLDRENUMDBG} */
static DECLCALLBACK(int)
rtDbgModOpenDebugInfoExternalToImageCallback(RTLDRMOD hLdrMod, PCRTLDRDBGINFO pDbgInfo, void *pvUser)
{
    RTDBGMODOPENDIETI * const pArgs   = (RTDBGMODOPENDIETI *)pvUser;
    PRTDBGMODINT const        pDbgMod = pArgs->pDbgMod;
    RT_NOREF_PV(hLdrMod);

    Assert(pDbgInfo->enmType > RTLDRDBGINFOTYPE_INVALID && pDbgInfo->enmType < RTLDRDBGINFOTYPE_END);
    const char *pszExtFile = pDbgInfo->pszExtFile;
    if (!pszExtFile)
    {
        /*
         * If a external debug type comes without a file name, calculate a likely
         * debug filename for it. (Hack for NT4 drivers and the 'nt' module for
         * NT and W2K.)  Prefer the image name rather than the module name, since
         * the latter can be normalized or using a different name (e.g. 'nt').
         */
        const char *pszExtSuff = NULL;
        size_t      cchExtSuff = 0;
        if (pDbgInfo->enmType == RTLDRDBGINFOTYPE_CODEVIEW_DBG)
            pszExtSuff = ".dbg", cchExtSuff = sizeof(".dbg") - 1;
        else if (   pDbgInfo->enmType == RTLDRDBGINFOTYPE_CODEVIEW_PDB20
                 || pDbgInfo->enmType == RTLDRDBGINFOTYPE_CODEVIEW_PDB70)
            pszExtSuff = ".pdb", cchExtSuff = sizeof(".pdb") - 1;
        if (pszExtSuff)
        {
            const char * const pszName = pDbgMod->pszImgFile
                                       ? RTPathFilenameEx(pDbgMod->pszImgFile, RTPATH_STR_F_STYLE_DOS)
                                       : pDbgMod->pszName;
            if (pszName)
            {
                const char * const  pszOldSuff    = RTPathSuffix(pszName);
                size_t const        cchName       = pszOldSuff ? (size_t)(pszOldSuff - pszName) : strlen(pszName);
                char * const        pszExtFileBuf = (char *)alloca(cchName + cchExtSuff + 1);
                if (pszExtFileBuf)
                {
                    memcpy(mempcpy(pszExtFileBuf, pszName, cchName), pszExtSuff, cchExtSuff + 1);
                    pszExtFile = pszExtFileBuf;
                }
            }
        }
        if (!pszExtFile)
        {
            Log2(("rtDbgModOpenDebugInfoExternalToImageCallback: enmType=%d\n", pDbgInfo->enmType));
            return VINF_SUCCESS;
        }
    }

    /*
     * Switch on type and call the appropriate search function.
     */
    int rc;
    switch (pDbgInfo->enmType)
    {
        case RTLDRDBGINFOTYPE_CODEVIEW_PDB70:
            rc = RTDbgCfgOpenPdb70(pArgs->hDbgCfg, pszExtFile,
                                   &pDbgInfo->u.Pdb70.Uuid,
                                   pDbgInfo->u.Pdb70.uAge,
                                   rtDbgModExtDbgInfoOpenCallback, pDbgMod, (void *)pDbgInfo);
            break;

        case RTLDRDBGINFOTYPE_CODEVIEW_PDB20:
            rc = RTDbgCfgOpenPdb20(pArgs->hDbgCfg, pszExtFile,
                                   pDbgInfo->u.Pdb20.cbImage,
                                   pDbgInfo->u.Pdb20.uTimestamp,
                                   pDbgInfo->u.Pdb20.uAge,
                                   rtDbgModExtDbgInfoOpenCallback, pDbgMod, (void *)pDbgInfo);
            break;

        case RTLDRDBGINFOTYPE_CODEVIEW_DBG:
            rc = RTDbgCfgOpenDbg(pArgs->hDbgCfg, pszExtFile,
                                 pDbgInfo->u.Dbg.cbImage,
                                 pDbgInfo->u.Dbg.uTimestamp,
                                 rtDbgModExtDbgInfoOpenCallback, pDbgMod, (void *)pDbgInfo);
            break;

        case RTLDRDBGINFOTYPE_DWARF_DWO:
            rc = RTDbgCfgOpenDwo(pArgs->hDbgCfg, pszExtFile,
                                 pDbgInfo->u.Dwo.uCrc32,
                                 rtDbgModExtDbgInfoOpenCallback, pDbgMod, (void *)pDbgInfo);
            break;

        default:
            Log(("rtDbgModOpenDebugInfoExternalToImageCallback: Don't know how to handle enmType=%d and pszFileExt=%s\n",
                 pDbgInfo->enmType, pszExtFile));
            return VERR_DBG_TODO;
    }
    if (RT_SUCCESS(rc))
    {
        LogFlow(("RTDbgMod: Successfully opened external debug info '%s' for '%s'\n",
                 pDbgMod->pszDbgFile, pDbgMod->pszImgFile));
        return VINF_CALLBACK_RETURN;
    }
    Log(("rtDbgModOpenDebugInfoExternalToImageCallback: '%s' (enmType=%d) for '%s'  -> %Rrc\n",
         pszExtFile, pDbgInfo->enmType, pDbgMod->pszImgFile, rc));
    return rc;
}


/**
 * Opens debug info listed in the image that is stored in a separate file.
 *
 * @returns IPRT status code
 * @param   pDbgMod             The debug module.
 * @param   hDbgCfg             The debug config.  Can be NIL.
 */
static int rtDbgModOpenDebugInfoExternalToImage(PRTDBGMODINT pDbgMod, RTDBGCFG hDbgCfg)
{
    Assert(!pDbgMod->pDbgVt);

    RTDBGMODOPENDIETI Args;
    Args.pDbgMod = pDbgMod;
    Args.hDbgCfg = hDbgCfg;
    int rc = pDbgMod->pImgVt->pfnEnumDbgInfo(pDbgMod, rtDbgModOpenDebugInfoExternalToImageCallback, &Args);
    if (RT_SUCCESS(rc) && pDbgMod->pDbgVt)
        return VINF_SUCCESS;

    LogFlow(("rtDbgModOpenDebugInfoExternalToImage: rc=%Rrc\n", rc));
    return VERR_NOT_FOUND;
}


/** @callback_method_impl{FNRTDBGCFGOPEN} */
static DECLCALLBACK(int) rtDbgModExtDbgInfoOpenCallback2(RTDBGCFG hDbgCfg, const char *pszFilename, void *pvUser1, void *pvUser2)
{
    PRTDBGMODINT        pDbgMod   = (PRTDBGMODINT)pvUser1;
    RT_NOREF_PV(pvUser2); /** @todo image matching string or smth. */

    Assert(!pDbgMod->pDbgVt);
    Assert(!pDbgMod->pvDbgPriv);
    Assert(!pDbgMod->pszDbgFile);
    Assert(pDbgMod->pImgVt);

    /*
     * Set the debug file name and try possible interpreters.
     */
    pDbgMod->pszDbgFile = RTStrCacheEnter(g_hDbgModStrCache, pszFilename);

    int rc = RTSemRWRequestRead(g_hDbgModRWSem, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        for (PRTDBGMODREGDBG pDbg = g_pDbgHead; pDbg; pDbg = pDbg->pNext)
        {
            pDbgMod->pDbgVt    = pDbg->pVt;
            pDbgMod->pvDbgPriv = NULL;
            rc = pDbg->pVt->pfnTryOpen(pDbgMod, pDbgMod->pImgVt->pfnGetArch(pDbgMod), hDbgCfg);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Got it!
                 */
                ASMAtomicIncU32(&pDbg->cUsers);
                RTSemRWReleaseRead(g_hDbgModRWSem);
                return VINF_CALLBACK_RETURN;
            }
            pDbgMod->pDbgVt    = NULL;
            Assert(pDbgMod->pvDbgPriv == NULL);
        }
    }

    /* No joy. */
    RTSemRWReleaseRead(g_hDbgModRWSem);
    RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszDbgFile);
    pDbgMod->pszDbgFile = NULL;
    return rc;
}


/**
 * Opens external debug info that is not listed in the image.
 *
 * @returns IPRT status code
 * @param   pDbgMod             The debug module.
 * @param   hDbgCfg             The debug config.  Can be NIL.
 */
static int rtDbgModOpenDebugInfoExternalToImage2(PRTDBGMODINT pDbgMod, RTDBGCFG hDbgCfg)
{
    int rc;
    Assert(!pDbgMod->pDbgVt);
    Assert(pDbgMod->pImgVt);

    /*
     * Figure out what to search for based on the image format.
     */
    const char *pszzExts = NULL;
    RTLDRFMT    enmFmt = pDbgMod->pImgVt->pfnGetFormat(pDbgMod);
    switch (enmFmt)
    {
        case RTLDRFMT_MACHO:
        {
            RTUUID  Uuid;
            PRTUUID pUuid = &Uuid;
            rc = pDbgMod->pImgVt->pfnQueryProp(pDbgMod, RTLDRPROP_UUID, &Uuid, sizeof(Uuid), NULL);
            if (RT_FAILURE(rc))
                pUuid = NULL;

            rc = RTDbgCfgOpenDsymBundle(hDbgCfg, pDbgMod->pszImgFile, pUuid,
                                        rtDbgModExtDbgInfoOpenCallback2, pDbgMod, NULL /*pvUser2*/);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;
            break;
        }

#if 0 /* Will be links in the image if these apply. .map readers for PE or ELF we don't have. */
        case RTLDRFMT_ELF:
            pszzExts = ".debug\0.dwo\0";
            break;
        case RTLDRFMT_PE:
            pszzExts = ".map\0";
            break;
#endif
#if 0 /* Haven't implemented .sym or .map file readers for OS/2 yet. */
        case RTLDRFMT_LX:
            pszzExts = ".sym\0.map\0";
            break;
#endif
        default:
            rc = VERR_NOT_IMPLEMENTED;
            break;
    }

    NOREF(pszzExts);
#if 0 /* Later */
    if (pszzExts)
    {

    }
#endif

    LogFlow(("rtDbgModOpenDebugInfoExternalToImage2: rc=%Rrc\n", rc));
    return VERR_NOT_FOUND;
}


RTDECL(int) RTDbgModCreateFromImage(PRTDBGMOD phDbgMod, const char *pszFilename, const char *pszName,
                                    RTLDRARCH enmArch, RTDBGCFG hDbgCfg)
{
    /*
     * Input validation and lazy initialization.
     */
    AssertPtrReturn(phDbgMod, VERR_INVALID_POINTER);
    *phDbgMod = NIL_RTDBGMOD;
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszName, VERR_INVALID_POINTER);
    AssertReturn(enmArch > RTLDRARCH_INVALID && enmArch < RTLDRARCH_END, VERR_INVALID_PARAMETER);

    int rc = rtDbgModLazyInit();
    if (RT_FAILURE(rc))
        return rc;

    if (!pszName)
        pszName = RTPathFilenameEx(pszFilename, RTPATH_STR_F_STYLE_DOS);

    /*
     * Allocate a new module instance.
     */
    PRTDBGMODINT pDbgMod = (PRTDBGMODINT)RTMemAllocZ(sizeof(*pDbgMod));
    if (!pDbgMod)
        return VERR_NO_MEMORY;
    pDbgMod->u32Magic = RTDBGMOD_MAGIC;
    pDbgMod->cRefs = 1;
    rc = RTCritSectInit(&pDbgMod->CritSect);
    if (RT_SUCCESS(rc))
    {
        pDbgMod->pszName = RTStrCacheEnterLower(g_hDbgModStrCache, pszName);
        if (pDbgMod->pszName)
        {
            pDbgMod->pszImgFile = RTStrCacheEnter(g_hDbgModStrCache, pszFilename);
            if (pDbgMod->pszImgFile)
            {
                RTStrCacheRetain(pDbgMod->pszImgFile);
                pDbgMod->pszImgFileSpecified = pDbgMod->pszImgFile;

                /*
                 * Find an image reader which groks the file.
                 */
                rc = RTSemRWRequestRead(g_hDbgModRWSem, RT_INDEFINITE_WAIT);
                if (RT_SUCCESS(rc))
                {
                    PRTDBGMODREGIMG pImg;
                    for (pImg = g_pImgHead; pImg; pImg = pImg->pNext)
                    {
                        pDbgMod->pImgVt    = pImg->pVt;
                        pDbgMod->pvImgPriv = NULL;
                        /** @todo need to specify some arch stuff here. */
                        rc = pImg->pVt->pfnTryOpen(pDbgMod, enmArch, 0 /*fLdrFlags*/);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Image detected, but found no debug info we were
                             * able to understand.
                             */
                            /** @todo some generic way of matching image and debug info, flexible signature
                             *        of some kind. Apple uses UUIDs, microsoft uses a UUID+age or a
                             *        size+timestamp, and GNU a CRC32 (last time I checked). */
                            rc = rtDbgModOpenDebugInfoExternalToImage(pDbgMod, hDbgCfg);
                            if (RT_FAILURE(rc))
                                rc = rtDbgModOpenDebugInfoInsideImage(pDbgMod);
                            if (RT_FAILURE(rc))
                                rc = rtDbgModOpenDebugInfoExternalToImage2(pDbgMod, hDbgCfg);
                            if (RT_FAILURE(rc))
                                rc = rtDbgModCreateForExports(pDbgMod);
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * We're done!
                                 */
                                ASMAtomicIncU32(&pImg->cUsers);
                                RTSemRWReleaseRead(g_hDbgModRWSem);

                                *phDbgMod = pDbgMod;
                                return VINF_SUCCESS;
                            }

                            /* Failed, close up the shop. */
                            pDbgMod->pImgVt->pfnClose(pDbgMod);
                            pDbgMod->pImgVt = NULL;
                            pDbgMod->pvImgPriv = NULL;
                            break;
                        }
                    }

                    /*
                     * Could it be a file containing raw debug info?
                     */
                    if (!pImg)
                    {
                        pDbgMod->pImgVt     = NULL;
                        pDbgMod->pvImgPriv  = NULL;
                        pDbgMod->pszDbgFile = pDbgMod->pszImgFile;
                        pDbgMod->pszImgFile = NULL;

                        for (PRTDBGMODREGDBG pDbg = g_pDbgHead; pDbg; pDbg = pDbg->pNext)
                        {
                            pDbgMod->pDbgVt = pDbg->pVt;
                            pDbgMod->pvDbgPriv = NULL;
                            rc = pDbg->pVt->pfnTryOpen(pDbgMod, enmArch, hDbgCfg);
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * That's it!
                                 */
                                ASMAtomicIncU32(&pDbg->cUsers);
                                RTSemRWReleaseRead(g_hDbgModRWSem);

                                *phDbgMod = pDbgMod;
                                return rc;
                            }
                        }

                        pDbgMod->pszImgFile = pDbgMod->pszDbgFile;
                        pDbgMod->pszDbgFile = NULL;
                    }

                    /* bail out */
                    rc = VERR_DBG_NO_MATCHING_INTERPRETER;
                    RTSemRWReleaseRead(g_hDbgModRWSem);
                }
                RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszImgFileSpecified);
                RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszImgFile);
            }
            else
                rc = VERR_NO_STR_MEMORY;
            RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszName);
        }
        else
            rc = VERR_NO_STR_MEMORY;
        RTCritSectDelete(&pDbgMod->CritSect);
    }

    RTMemFree(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModCreateFromImage);





/*
 *
 *  P E   I M A G E
 *  P E   I M A G E
 *  P E   I M A G E
 *
 */



/** @callback_method_impl{FNRTDBGCFGOPEN} */
static DECLCALLBACK(int) rtDbgModFromPeImageOpenCallback(RTDBGCFG hDbgCfg, const char *pszFilename, void *pvUser1, void *pvUser2)
{
    PRTDBGMODINT        pDbgMod   = (PRTDBGMODINT)pvUser1;
    PRTDBGMODDEFERRED   pDeferred = (PRTDBGMODDEFERRED)pvUser2;
    LogFlow(("rtDbgModFromPeImageOpenCallback: %s\n", pszFilename));
    RT_NOREF_PV(hDbgCfg);

    Assert(pDbgMod->pImgVt == NULL);
    Assert(pDbgMod->pvImgPriv == NULL);
    Assert(pDbgMod->pDbgVt == NULL);
    Assert(pDbgMod->pvDbgPriv == NULL);

    /*
     * Replace the image file name while probing it.
     */
    const char *pszNewImgFile = RTStrCacheEnter(g_hDbgModStrCache, pszFilename);
    if (!pszNewImgFile)
        return VERR_NO_STR_MEMORY;
    const char *pszOldImgFile = pDbgMod->pszImgFile;
    pDbgMod->pszImgFile = pszNewImgFile;

    /*
     * Find an image reader which groks the file.
     */
    int rc = RTSemRWRequestRead(g_hDbgModRWSem, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        rc = VERR_DBG_NO_MATCHING_INTERPRETER;
        PRTDBGMODREGIMG pImg;
        for (pImg = g_pImgHead; pImg; pImg = pImg->pNext)
        {
            pDbgMod->pImgVt    = pImg->pVt;
            pDbgMod->pvImgPriv = NULL;
            int rc2 = pImg->pVt->pfnTryOpen(pDbgMod, RTLDRARCH_WHATEVER, 0 /*fLdrFlags*/);
            if (RT_SUCCESS(rc2))
            {
                rc = rc2;
                break;
            }
            pDbgMod->pImgVt    = NULL;
            Assert(pDbgMod->pvImgPriv == NULL);
        }
        RTSemRWReleaseRead(g_hDbgModRWSem);
        if (RT_SUCCESS(rc))
        {
            /*
             * Check the deferred info.
             */
            RTUINTPTR cbImage = pDbgMod->pImgVt->pfnImageSize(pDbgMod);
            if (   pDeferred->cbImage == 0
                || pDeferred->cbImage == cbImage)
            {
                uint32_t uTimestamp = pDeferred->u.PeImage.uTimestamp; /** @todo add method for getting the timestamp. */
                if (   pDeferred->u.PeImage.uTimestamp == 0
                    || pDeferred->u.PeImage.uTimestamp == uTimestamp)
                {
                    Log(("RTDbgMod: Found matching PE image '%s'\n", pszFilename));

                    /*
                     * We found the executable image we need, now go find any
                     * debug info associated with it.  For PE images, this is
                     * generally found in an external file, so we do a sweep
                     * for that first.
                     *
                     * Then try open debug inside the module, and finally
                     * falling back on exports.
                     */
                    rc = rtDbgModOpenDebugInfoExternalToImage(pDbgMod, pDeferred->hDbgCfg);
                    if (RT_FAILURE(rc))
                        rc = rtDbgModOpenDebugInfoInsideImage(pDbgMod);
                    if (RT_FAILURE(rc))
                        rc = rtDbgModCreateForExports(pDbgMod);
                    if (RT_SUCCESS(rc))
                    {
                        RTStrCacheRelease(g_hDbgModStrCache, pszOldImgFile);
                        return VINF_CALLBACK_RETURN;
                    }

                    /* Something bad happened, just give up. */
                    Log(("rtDbgModFromPeImageOpenCallback: rtDbgModCreateForExports failed: %Rrc\n", rc));
                }
                else
                {
                    LogFlow(("rtDbgModFromPeImageOpenCallback: uTimestamp mismatch (found %#x, expected %#x) - %s\n",
                             uTimestamp, pDeferred->u.PeImage.uTimestamp, pszFilename));
                    rc = VERR_DBG_FILE_MISMATCH;
                }
            }
            else
            {
                LogFlow(("rtDbgModFromPeImageOpenCallback: cbImage mismatch (found %#x, expected %#x) - %s\n",
                         cbImage, pDeferred->cbImage, pszFilename));
                rc = VERR_DBG_FILE_MISMATCH;
            }

            pDbgMod->pImgVt->pfnClose(pDbgMod);
            pDbgMod->pImgVt    = NULL;
            pDbgMod->pvImgPriv = NULL;
        }
        else
            LogFlow(("rtDbgModFromPeImageOpenCallback: Failed %Rrc - %s\n", rc, pszFilename));
    }

    /* Restore image name. */
    pDbgMod->pszImgFile = pszOldImgFile;
    RTStrCacheRelease(g_hDbgModStrCache, pszNewImgFile);
    return rc;
}


/** @callback_method_impl{FNRTDBGMODDEFERRED}  */
static DECLCALLBACK(int) rtDbgModFromPeImageDeferredCallback(PRTDBGMODINT pDbgMod, PRTDBGMODDEFERRED pDeferred)
{
    int rc;

    Assert(pDbgMod->pszImgFile);
    if (!pDbgMod->pImgVt)
        rc = RTDbgCfgOpenPeImage(pDeferred->hDbgCfg, pDbgMod->pszImgFile,
                                 pDeferred->cbImage, pDeferred->u.PeImage.uTimestamp,
                                 rtDbgModFromPeImageOpenCallback, pDbgMod, pDeferred);
    else
    {
        rc = rtDbgModOpenDebugInfoExternalToImage(pDbgMod, pDeferred->hDbgCfg);
        if (RT_FAILURE(rc))
            rc = rtDbgModOpenDebugInfoInsideImage(pDbgMod);
        if (RT_FAILURE(rc))
            rc = rtDbgModCreateForExports(pDbgMod);
    }
    return rc;
}


RTDECL(int) RTDbgModCreateFromPeImage(PRTDBGMOD phDbgMod, const char *pszFilename, const char *pszName,
                                      PRTLDRMOD phLdrMod, uint32_t cbImage, uint32_t uTimestamp, RTDBGCFG hDbgCfg)
{
    /*
     * Input validation and lazy initialization.
     */
    AssertPtrReturn(phDbgMod, VERR_INVALID_POINTER);
    *phDbgMod = NIL_RTDBGMOD;
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename, VERR_INVALID_PARAMETER);
    if (!pszName)
        pszName = RTPathFilenameEx(pszFilename, RTPATH_STR_F_STYLE_DOS);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrNullReturn(phLdrMod, VERR_INVALID_POINTER);
    RTLDRMOD hLdrMod = phLdrMod ? *phLdrMod : NIL_RTLDRMOD;
    AssertReturn(hLdrMod == NIL_RTLDRMOD || RTLdrSize(hLdrMod) != ~(size_t)0, VERR_INVALID_HANDLE);

    int rc = rtDbgModLazyInit();
    if (RT_FAILURE(rc))
        return rc;

    uint64_t fDbgCfg = 0;
    if (hDbgCfg)
    {
        rc = RTDbgCfgQueryUInt(hDbgCfg, RTDBGCFGPROP_FLAGS, &fDbgCfg);
        AssertRCReturn(rc, rc);
    }

    /*
     * Allocate a new module instance.
     */
    PRTDBGMODINT pDbgMod = (PRTDBGMODINT)RTMemAllocZ(sizeof(*pDbgMod));
    if (!pDbgMod)
        return VERR_NO_MEMORY;
    pDbgMod->u32Magic = RTDBGMOD_MAGIC;
    pDbgMod->cRefs = 1;
    rc = RTCritSectInit(&pDbgMod->CritSect);
    if (RT_SUCCESS(rc))
    {
        pDbgMod->pszName = RTStrCacheEnterLower(g_hDbgModStrCache, pszName);
        if (pDbgMod->pszName)
        {
            pDbgMod->pszImgFile = RTStrCacheEnter(g_hDbgModStrCache, pszFilename);
            if (pDbgMod->pszImgFile)
            {
                RTStrCacheRetain(pDbgMod->pszImgFile);
                pDbgMod->pszImgFileSpecified = pDbgMod->pszImgFile;

                /*
                 * If we have a loader module, we must instantiate the loader
                 * side of things regardless of the deferred setting.
                 */
                if (hLdrMod != NIL_RTLDRMOD)
                {
                    if (!cbImage)
                        cbImage = (uint32_t)RTLdrSize(hLdrMod);
                    pDbgMod->pImgVt = &g_rtDbgModVtImgLdr;

                    rc = rtDbgModLdrOpenFromHandle(pDbgMod, hLdrMod);
                }
                if (RT_SUCCESS(rc))
                {
                    /* We now own the loader handle, so clear the caller variable. */
                    if (phLdrMod)
                        *phLdrMod = NIL_RTLDRMOD;

                    /*
                     * Do it now or procrastinate?
                     */
                    if (!(fDbgCfg & RTDBGCFG_FLAGS_DEFERRED) || !cbImage)
                    {
                        RTDBGMODDEFERRED Deferred;
                        Deferred.cbImage = cbImage;
                        Deferred.hDbgCfg = hDbgCfg;
                        Deferred.u.PeImage.uTimestamp = uTimestamp;
                        rc = rtDbgModFromPeImageDeferredCallback(pDbgMod, &Deferred);
                    }
                    else
                    {
                        PRTDBGMODDEFERRED pDeferred;
                        rc = rtDbgModDeferredCreate(pDbgMod, rtDbgModFromPeImageDeferredCallback, cbImage, hDbgCfg,
                                                    0 /*cbDeferred*/, 0 /*fFlags*/, &pDeferred);
                        if (RT_SUCCESS(rc))
                            pDeferred->u.PeImage.uTimestamp = uTimestamp;
                    }
                    if (RT_SUCCESS(rc))
                    {
                        *phDbgMod = pDbgMod;
                        return VINF_SUCCESS;
                    }

                    /* Failed, bail out. */
                    if (hLdrMod != NIL_RTLDRMOD)
                    {
                        Assert(pDbgMod->pImgVt);
                        pDbgMod->pImgVt->pfnClose(pDbgMod);
                    }
                }
                RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszName);
            }
            else
                rc = VERR_NO_STR_MEMORY;
            RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszImgFileSpecified);
            RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszImgFile);
        }
        else
            rc = VERR_NO_STR_MEMORY;
        RTCritSectDelete(&pDbgMod->CritSect);
    }

    RTMemFree(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModCreateFromPeImage);




/*
 *
 *  M a c h - O   I M A G E
 *  M a c h - O   I M A G E
 *  M a c h - O   I M A G E
 *
 */


/**
 * Argument package used when opening Mach-O images and .dSYMs files.
 */
typedef struct RTDBGMODMACHOARGS
{
    /** For use more internal use in file locator callbacks. */
    RTLDRARCH           enmArch;
    /** For use more internal use in file locator callbacks. */
    PCRTUUID            pUuid;
    /** For use more internal use in file locator callbacks. */
    bool                fOpenImage;
    /** RTDBGMOD_F_XXX. */
    uint32_t            fFlags;
} RTDBGMODMACHOARGS;
/** Pointer to a const segment package. */
typedef RTDBGMODMACHOARGS const *PCRTDBGMODMACHOARGS;



/** @callback_method_impl{FNRTDBGCFGOPEN} */
static DECLCALLBACK(int)
rtDbgModFromMachOImageOpenDsymMachOCallback(RTDBGCFG hDbgCfg, const char *pszFilename, void *pvUser1, void *pvUser2)
{
    PRTDBGMODINT        pDbgMod = (PRTDBGMODINT)pvUser1;
    PCRTDBGMODMACHOARGS pArgs   = (PCRTDBGMODMACHOARGS)pvUser2;
    RT_NOREF_PV(hDbgCfg);

    Assert(!pDbgMod->pDbgVt);
    Assert(!pDbgMod->pvDbgPriv);
    Assert(!pDbgMod->pszDbgFile);
    Assert(!pDbgMod->pImgVt);
    Assert(!pDbgMod->pvDbgPriv);
    Assert(pDbgMod->pszImgFile);
    Assert(pDbgMod->pszImgFileSpecified);

    const char *pszImgFileOrg = pDbgMod->pszImgFile;
    pDbgMod->pszImgFile = RTStrCacheEnter(g_hDbgModStrCache, pszFilename);
    if (!pDbgMod->pszImgFile)
        return VERR_NO_STR_MEMORY;
    RTStrCacheRetain(pDbgMod->pszImgFile);
    pDbgMod->pszDbgFile = pDbgMod->pszImgFile;

    /*
     * Try image interpreters as the dwarf file inside the dSYM bundle is a
     * Mach-O file with dwarf debug sections insides it and no code or data.
     */
    int rc = RTSemRWRequestRead(g_hDbgModRWSem, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        rc = VERR_DBG_NO_MATCHING_INTERPRETER;
        PRTDBGMODREGIMG pImg;
        for (pImg = g_pImgHead; pImg; pImg = pImg->pNext)
        {
            pDbgMod->pImgVt    = pImg->pVt;
            pDbgMod->pvImgPriv = NULL;
            int rc2 = pImg->pVt->pfnTryOpen(pDbgMod, pArgs->enmArch,
                                            pArgs->fFlags & RTDBGMOD_F_MACHO_LOAD_LINKEDIT ? RTLDR_O_MACHO_LOAD_LINKEDIT : 0);
            if (RT_SUCCESS(rc2))
            {
                rc = rc2;
                break;
            }
            pDbgMod->pImgVt    = NULL;
            Assert(pDbgMod->pvImgPriv == NULL);
        }

        if (RT_SUCCESS(rc))
        {
            /*
             * Check the UUID if one was given.
             */
            if (pArgs->pUuid)
            {
                RTUUID UuidOpened;
                rc = pDbgMod->pImgVt->pfnQueryProp(pDbgMod, RTLDRPROP_UUID, &UuidOpened, sizeof(UuidOpened), NULL);
                if (RT_SUCCESS(rc))
                {
                    if (RTUuidCompare(&UuidOpened, pArgs->pUuid) != 0)
                        rc = VERR_DBG_FILE_MISMATCH;
                }
                else if (rc == VERR_NOT_FOUND || rc == VERR_NOT_IMPLEMENTED)
                    rc = VERR_DBG_FILE_MISMATCH;
            }
            if (RT_SUCCESS(rc))
            {
                /*
                 * Pass it to the DWARF reader(s).  Careful to restrict this or
                 * the dbghelp wrapper may end up being overly helpful.
                 */
                for (PRTDBGMODREGDBG pDbg = g_pDbgHead; pDbg; pDbg = pDbg->pNext)
                {
                    if (pDbg->pVt->fSupports & (RT_DBGTYPE_DWARF | RT_DBGTYPE_STABS | RT_DBGTYPE_WATCOM))

                    {
                        pDbgMod->pDbgVt    = pDbg->pVt;
                        pDbgMod->pvDbgPriv = NULL;
                        rc = pDbg->pVt->pfnTryOpen(pDbgMod, pDbgMod->pImgVt->pfnGetArch(pDbgMod), hDbgCfg);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Got it!
                             */
                            ASMAtomicIncU32(&pDbg->cUsers);
                            RTSemRWReleaseRead(g_hDbgModRWSem);
                            RTStrCacheRelease(g_hDbgModStrCache, pszImgFileOrg);
                            return VINF_CALLBACK_RETURN;
                        }
                        pDbgMod->pDbgVt    = NULL;
                        Assert(pDbgMod->pvDbgPriv == NULL);
                    }
                }

                /*
                 * Likely fallback for when opening image.
                 */
                if (pArgs->fOpenImage)
                {
                    rc = rtDbgModCreateForExports(pDbgMod);
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * Done.
                         */
                        RTSemRWReleaseRead(g_hDbgModRWSem);
                        RTStrCacheRelease(g_hDbgModStrCache, pszImgFileOrg);
                        return VINF_CALLBACK_RETURN;
                    }
                }
            }

            pDbgMod->pImgVt->pfnClose(pDbgMod);
            pDbgMod->pImgVt    = NULL;
            pDbgMod->pvImgPriv = NULL;
        }
    }

    /* No joy. */
    RTSemRWReleaseRead(g_hDbgModRWSem);
    RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszImgFile);
    pDbgMod->pszImgFile = pszImgFileOrg;
    RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszDbgFile);
    pDbgMod->pszDbgFile = NULL;
    return rc;
}


static int rtDbgModFromMachOImageWorker(PRTDBGMODINT pDbgMod, RTLDRARCH enmArch, uint32_t cbImage,
                                        uint32_t cSegs, PCRTDBGSEGMENT paSegs, PCRTUUID pUuid, RTDBGCFG hDbgCfg, uint32_t fFlags)
{
    RT_NOREF_PV(cbImage); RT_NOREF_PV(cSegs); RT_NOREF_PV(paSegs);

    RTDBGMODMACHOARGS Args;
    Args.enmArch    = enmArch;
    Args.pUuid      = pUuid && RTUuidIsNull(pUuid) ? pUuid : NULL;
    Args.fOpenImage = false;
    Args.fFlags     = fFlags;

    /*
     * Search for the .dSYM bundle first, since that's generally all we need.
     */
    int rc = RTDbgCfgOpenDsymBundle(hDbgCfg, pDbgMod->pszImgFile, pUuid,
                                    rtDbgModFromMachOImageOpenDsymMachOCallback, pDbgMod, &Args);
    if (RT_FAILURE(rc))
    {
        /*
         * If we cannot get at the .dSYM, try the executable image.
         */
        Args.fOpenImage = true;
        rc = RTDbgCfgOpenMachOImage(hDbgCfg, pDbgMod->pszImgFile, pUuid,
                                    rtDbgModFromMachOImageOpenDsymMachOCallback, pDbgMod, &Args);
    }
    return rc;
}


/** @callback_method_impl{FNRTDBGMODDEFERRED}  */
static DECLCALLBACK(int) rtDbgModFromMachOImageDeferredCallback(PRTDBGMODINT pDbgMod, PRTDBGMODDEFERRED pDeferred)
{
    return rtDbgModFromMachOImageWorker(pDbgMod, pDeferred->u.MachO.enmArch, pDeferred->cbImage,
                                        pDeferred->u.MachO.cSegs, pDeferred->u.MachO.aSegs,
                                        &pDeferred->u.MachO.Uuid, pDeferred->hDbgCfg, pDeferred->fFlags);
}


RTDECL(int) RTDbgModCreateFromMachOImage(PRTDBGMOD phDbgMod, const char *pszFilename, const char *pszName, RTLDRARCH enmArch,
                                         PRTLDRMOD phLdrModIn, uint32_t cbImage, uint32_t cSegs, PCRTDBGSEGMENT paSegs,
                                         PCRTUUID pUuid, RTDBGCFG hDbgCfg, uint32_t fFlags)
{
    /*
     * Input validation and lazy initialization.
     */
    AssertPtrReturn(phDbgMod, VERR_INVALID_POINTER);
    *phDbgMod = NIL_RTDBGMOD;
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename, VERR_INVALID_PARAMETER);
    if (!pszName)
        pszName = RTPathFilenameEx(pszFilename, RTPATH_STR_F_STYLE_HOST);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    if (cSegs)
    {
        AssertReturn(cSegs < 1024, VERR_INVALID_PARAMETER);
        AssertPtrReturn(paSegs, VERR_INVALID_POINTER);
        AssertReturn(!cbImage, VERR_INVALID_PARAMETER);
    }
    AssertPtrNullReturn(pUuid, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTDBGMOD_F_VALID_MASK), VERR_INVALID_FLAGS);

    AssertPtrNullReturn(phLdrModIn, VERR_INVALID_POINTER);
    RTLDRMOD hLdrModIn = phLdrModIn ? *phLdrModIn : NIL_RTLDRMOD;
    AssertReturn(hLdrModIn == NIL_RTLDRMOD || RTLdrSize(hLdrModIn) != ~(size_t)0, VERR_INVALID_HANDLE);

    AssertReturn(cbImage || cSegs || hLdrModIn != NIL_RTLDRMOD, VERR_INVALID_PARAMETER);

    int rc = rtDbgModLazyInit();
    if (RT_FAILURE(rc))
        return rc;

    uint64_t fDbgCfg = 0;
    if (hDbgCfg)
    {
        rc = RTDbgCfgQueryUInt(hDbgCfg, RTDBGCFGPROP_FLAGS, &fDbgCfg);
        AssertRCReturn(rc, rc);
    }

    /*
     * If we got no UUID but the caller passed in a module handle, try
     * query the UUID from it.
     */
    RTUUID UuidFromImage = RTUUID_INITIALIZE_NULL;
    if ((!pUuid || RTUuidIsNull(pUuid)) && hLdrModIn != NIL_RTLDRMOD)
    {
        rc = RTLdrQueryProp(hLdrModIn, RTLDRPROP_UUID, &UuidFromImage, sizeof(UuidFromImage));
        if (RT_SUCCESS(rc))
            pUuid = &UuidFromImage;
    }

    /*
     * Allocate a new module instance.
     */
    PRTDBGMODINT pDbgMod = (PRTDBGMODINT)RTMemAllocZ(sizeof(*pDbgMod));
    if (!pDbgMod)
        return VERR_NO_MEMORY;
    pDbgMod->u32Magic = RTDBGMOD_MAGIC;
    pDbgMod->cRefs = 1;
    rc = RTCritSectInit(&pDbgMod->CritSect);
    if (RT_SUCCESS(rc))
    {
        pDbgMod->pszName = RTStrCacheEnterLower(g_hDbgModStrCache, pszName);
        if (pDbgMod->pszName)
        {
            pDbgMod->pszImgFile = RTStrCacheEnter(g_hDbgModStrCache, pszFilename);
            if (pDbgMod->pszImgFile)
            {
                RTStrCacheRetain(pDbgMod->pszImgFile);
                pDbgMod->pszImgFileSpecified = pDbgMod->pszImgFile;

                /*
                 * Load it immediately?
                 */
                if (   !(fDbgCfg & RTDBGCFG_FLAGS_DEFERRED)
                    || cSegs /* for the time being. */
                    || (!cbImage && !cSegs)
                    || (fFlags & RTDBGMOD_F_NOT_DEFERRED)
                    || hLdrModIn != NIL_RTLDRMOD)
                {
                    rc = rtDbgModFromMachOImageWorker(pDbgMod, enmArch, cbImage, cSegs, paSegs, pUuid, hDbgCfg, fFlags);
                    if (RT_FAILURE(rc) && hLdrModIn != NIL_RTLDRMOD)
                    {
                        /*
                         * Create module based on exports from hLdrModIn.
                         */
                        if (!cbImage)
                            cbImage = (uint32_t)RTLdrSize(hLdrModIn);
                        pDbgMod->pImgVt = &g_rtDbgModVtImgLdr;

                        rc = rtDbgModLdrOpenFromHandle(pDbgMod, hLdrModIn);
                        if (RT_SUCCESS(rc))
                        {
                            /* We now own the loader handle, so clear the caller variable. */
                            if (phLdrModIn)
                                *phLdrModIn = NIL_RTLDRMOD;

                            /** @todo delayed exports stuff   */
                            rc = rtDbgModCreateForExports(pDbgMod);
                        }
                    }
                }
                else
                {
                    /*
                     * Procrastinate.  Need image size atm.
                     */
                    PRTDBGMODDEFERRED pDeferred;
                    rc = rtDbgModDeferredCreate(pDbgMod, rtDbgModFromMachOImageDeferredCallback, cbImage, hDbgCfg,
                                                RT_UOFFSETOF_DYN(RTDBGMODDEFERRED, u.MachO.aSegs[cSegs]),
                                                0 /*fFlags*/, &pDeferred);
                    if (RT_SUCCESS(rc))
                    {
                        pDeferred->u.MachO.Uuid    = *pUuid;
                        pDeferred->u.MachO.enmArch = enmArch;
                        pDeferred->u.MachO.cSegs   = cSegs;
                        if (cSegs)
                            memcpy(&pDeferred->u.MachO.aSegs, paSegs, cSegs * sizeof(paSegs[0]));
                    }
                }
                if (RT_SUCCESS(rc))
                {
                    *phDbgMod = pDbgMod;
                    return VINF_SUCCESS;
                }

                /* Failed, bail out. */
                RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszName);
            }
            else
                rc = VERR_NO_STR_MEMORY;
            RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszImgFileSpecified);
            RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszImgFile);
        }
        else
            rc = VERR_NO_STR_MEMORY;
        RTCritSectDelete(&pDbgMod->CritSect);
    }

    RTMemFree(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModCreateFromMachOImage);



/**
 * Destroys an module after the reference count has reached zero.
 *
 * @param   pDbgMod     The module instance.
 */
static void rtDbgModDestroy(PRTDBGMODINT pDbgMod)
{
    /*
     * Close the debug info interpreter first, then the image interpret.
     */
    RTCritSectEnter(&pDbgMod->CritSect); /* paranoia  */

    if (pDbgMod->pDbgVt)
    {
        pDbgMod->pDbgVt->pfnClose(pDbgMod);
        pDbgMod->pDbgVt = NULL;
        pDbgMod->pvDbgPriv = NULL;
    }

    if (pDbgMod->pImgVt)
    {
        pDbgMod->pImgVt->pfnClose(pDbgMod);
        pDbgMod->pImgVt = NULL;
        pDbgMod->pvImgPriv = NULL;
    }

    /*
     * Free the resources.
     */
    ASMAtomicWriteU32(&pDbgMod->u32Magic, ~RTDBGMOD_MAGIC);
    RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszName);
    RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszImgFile);
    RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszImgFileSpecified);
    RTStrCacheRelease(g_hDbgModStrCache, pDbgMod->pszDbgFile);
    RTCritSectLeave(&pDbgMod->CritSect); /* paranoia  */
    RTCritSectDelete(&pDbgMod->CritSect);
    RTMemFree(pDbgMod);
}


RTDECL(uint32_t) RTDbgModRetain(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, UINT32_MAX);
    return ASMAtomicIncU32(&pDbgMod->cRefs);
}
RT_EXPORT_SYMBOL(RTDbgModRetain);


RTDECL(uint32_t) RTDbgModRelease(RTDBGMOD hDbgMod)
{
    if (hDbgMod == NIL_RTDBGMOD)
        return 0;
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pDbgMod->cRefs);
    if (!cRefs)
        rtDbgModDestroy(pDbgMod);
    return cRefs;
}
RT_EXPORT_SYMBOL(RTDbgModRelease);


RTDECL(const char *) RTDbgModName(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, NULL);
    return pDbgMod->pszName;
}
RT_EXPORT_SYMBOL(RTDbgModName);


RTDECL(const char *) RTDbgModDebugFile(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, NULL);
    if (pDbgMod->fDeferred || pDbgMod->fExports)
        return NULL;
    return pDbgMod->pszDbgFile;
}
RT_EXPORT_SYMBOL(RTDbgModDebugFile);


RTDECL(const char *) RTDbgModImageFile(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, NULL);
    return pDbgMod->pszImgFileSpecified;
}
RT_EXPORT_SYMBOL(RTDbgModImageFile);


RTDECL(const char *) RTDbgModImageFileUsed(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, NULL);
    return pDbgMod->pszImgFile == pDbgMod->pszImgFileSpecified ? NULL : pDbgMod->pszImgFile;
}
RT_EXPORT_SYMBOL(RTDbgModImageFileUsed);


RTDECL(bool) RTDbgModIsDeferred(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, false);
    return pDbgMod->fDeferred;
}


RTDECL(bool) RTDbgModIsExports(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, false);
    return pDbgMod->fExports;
}


RTDECL(int) RTDbgModRemoveAll(RTDBGMOD hDbgMod, bool fLeaveSegments)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);

    RTDBGMOD_LOCK(pDbgMod);

    /* Only possible on container modules. */
    int rc = VINF_SUCCESS;
    if (pDbgMod->pDbgVt != &g_rtDbgModVtDbgContainer)
    {
        if (fLeaveSegments)
        {
            rc = rtDbgModContainer_LineRemoveAll(pDbgMod);
            if (RT_SUCCESS(rc))
                rc = rtDbgModContainer_SymbolRemoveAll(pDbgMod);
        }
        else
            rc = rtDbgModContainer_RemoveAll(pDbgMod);
    }
    else
        rc = VERR_ACCESS_DENIED;

    RTDBGMOD_UNLOCK(pDbgMod);
    return rc;
}


RTDECL(RTDBGSEGIDX) RTDbgModRvaToSegOff(RTDBGMOD hDbgMod, RTUINTPTR uRva, PRTUINTPTR poffSeg)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, NIL_RTDBGSEGIDX);
    RTDBGMOD_LOCK(pDbgMod);

    RTDBGSEGIDX iSeg = pDbgMod->pDbgVt->pfnRvaToSegOff(pDbgMod, uRva, poffSeg);

    RTDBGMOD_UNLOCK(pDbgMod);
    return iSeg;
}
RT_EXPORT_SYMBOL(RTDbgModRvaToSegOff);


RTDECL(uint64_t) RTDbgModGetTag(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, 0);
    return pDbgMod->uTag;
}
RT_EXPORT_SYMBOL(RTDbgModGetTag);


RTDECL(int) RTDbgModSetTag(RTDBGMOD hDbgMod, uint64_t uTag)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    RTDBGMOD_LOCK(pDbgMod);

    pDbgMod->uTag = uTag;

    RTDBGMOD_UNLOCK(pDbgMod);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTDbgModSetTag);


RTDECL(RTUINTPTR) RTDbgModImageSize(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, RTUINTPTR_MAX);
    RTDBGMOD_LOCK(pDbgMod);

    RTUINTPTR cbImage = pDbgMod->pDbgVt->pfnImageSize(pDbgMod);

    RTDBGMOD_UNLOCK(pDbgMod);
    return cbImage;
}
RT_EXPORT_SYMBOL(RTDbgModImageSize);


RTDECL(RTLDRFMT) RTDbgModImageGetFormat(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, RTLDRFMT_INVALID);
    RTDBGMOD_LOCK(pDbgMod);

    RTLDRFMT enmFmt;
    if (   pDbgMod->pImgVt
        && pDbgMod->pImgVt->pfnGetFormat)
        enmFmt = pDbgMod->pImgVt->pfnGetFormat(pDbgMod);
    else
        enmFmt = RTLDRFMT_INVALID;

    RTDBGMOD_UNLOCK(pDbgMod);
    return enmFmt;
}
RT_EXPORT_SYMBOL(RTDbgModImageGetFormat);


RTDECL(RTLDRARCH) RTDbgModImageGetArch(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, RTLDRARCH_INVALID);
    RTDBGMOD_LOCK(pDbgMod);

    RTLDRARCH enmArch;
    if (   pDbgMod->pImgVt
        && pDbgMod->pImgVt->pfnGetArch)
        enmArch = pDbgMod->pImgVt->pfnGetArch(pDbgMod);
    else
        enmArch = RTLDRARCH_WHATEVER;

    RTDBGMOD_UNLOCK(pDbgMod);
    return enmArch;
}
RT_EXPORT_SYMBOL(RTDbgModImageGetArch);


RTDECL(int) RTDbgModImageQueryProp(RTDBGMOD hDbgMod, RTLDRPROP enmProp, void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pcbRet, VERR_INVALID_POINTER);
    RTDBGMOD_LOCK(pDbgMod);

    int rc;
    if (   pDbgMod->pImgVt
        && pDbgMod->pImgVt->pfnQueryProp)
        rc = pDbgMod->pImgVt->pfnQueryProp(pDbgMod, enmProp, pvBuf, cbBuf, pcbRet);
    else
        rc = VERR_NOT_FOUND;

    RTDBGMOD_UNLOCK(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModImageQueryProp);


RTDECL(int) RTDbgModSegmentAdd(RTDBGMOD hDbgMod, RTUINTPTR uRva, RTUINTPTR cb, const char *pszName,
                               uint32_t fFlags, PRTDBGSEGIDX piSeg)
{
    /*
     * Validate input.
     */
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    AssertMsgReturn(uRva + cb >= uRva, ("uRva=%RTptr cb=%RTptr\n", uRva, cb), VERR_DBG_ADDRESS_WRAP);
    Assert(*pszName);
    size_t cchName = strlen(pszName);
    AssertReturn(cchName > 0, VERR_DBG_SEGMENT_NAME_OUT_OF_RANGE);
    AssertMsgReturn(cchName < RTDBG_SEGMENT_NAME_LENGTH, ("pszName=%s cchName=%zu\n", pszName, cchName),
        VERR_DBG_SEGMENT_NAME_OUT_OF_RANGE);
    AssertMsgReturn(!fFlags, ("%#x\n", fFlags), VERR_INVALID_PARAMETER);
    AssertPtrNull(piSeg);
    AssertMsgReturn(!piSeg || *piSeg == NIL_RTDBGSEGIDX || *piSeg <= RTDBGSEGIDX_LAST, ("%#x\n", *piSeg), VERR_DBG_SPECIAL_SEGMENT);

    /*
     * Do the deed.
     */
    RTDBGMOD_LOCK(pDbgMod);
    int rc = pDbgMod->pDbgVt->pfnSegmentAdd(pDbgMod, uRva, cb, pszName, cchName, fFlags, piSeg);
    RTDBGMOD_UNLOCK(pDbgMod);

    return rc;

}
RT_EXPORT_SYMBOL(RTDbgModSegmentAdd);


RTDECL(RTDBGSEGIDX) RTDbgModSegmentCount(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, NIL_RTDBGSEGIDX);
    RTDBGMOD_LOCK(pDbgMod);

    RTDBGSEGIDX cSegs = pDbgMod->pDbgVt->pfnSegmentCount(pDbgMod);

    RTDBGMOD_UNLOCK(pDbgMod);
    return cSegs;
}
RT_EXPORT_SYMBOL(RTDbgModSegmentCount);


RTDECL(int) RTDbgModSegmentByIndex(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, PRTDBGSEGMENT pSegInfo)
{
    AssertMsgReturn(iSeg <= RTDBGSEGIDX_LAST, ("%#x\n", iSeg), VERR_DBG_SPECIAL_SEGMENT);
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    RTDBGMOD_LOCK(pDbgMod);

    int rc = pDbgMod->pDbgVt->pfnSegmentByIndex(pDbgMod, iSeg, pSegInfo);

    RTDBGMOD_UNLOCK(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModSegmentByIndex);


RTDECL(RTUINTPTR) RTDbgModSegmentSize(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg)
{
    if (iSeg == RTDBGSEGIDX_RVA)
        return RTDbgModImageSize(hDbgMod);
    RTDBGSEGMENT SegInfo;
    int rc = RTDbgModSegmentByIndex(hDbgMod, iSeg, &SegInfo);
    return RT_SUCCESS(rc) ? SegInfo.cb : RTUINTPTR_MAX;
}
RT_EXPORT_SYMBOL(RTDbgModSegmentSize);


RTDECL(RTUINTPTR) RTDbgModSegmentRva(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg)
{
    RTDBGSEGMENT SegInfo;
    int rc = RTDbgModSegmentByIndex(hDbgMod, iSeg, &SegInfo);
    return RT_SUCCESS(rc) ? SegInfo.uRva : RTUINTPTR_MAX;
}
RT_EXPORT_SYMBOL(RTDbgModSegmentRva);


RTDECL(int) RTDbgModSymbolAdd(RTDBGMOD hDbgMod, const char *pszSymbol, RTDBGSEGIDX iSeg, RTUINTPTR off,
                              RTUINTPTR cb, uint32_t fFlags, uint32_t *piOrdinal)
{
    /*
     * Validate input.
     */
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszSymbol, VERR_INVALID_POINTER);
    size_t cchSymbol = strlen(pszSymbol);
    AssertReturn(cchSymbol, VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE);
    AssertReturn(cchSymbol < RTDBG_SYMBOL_NAME_LENGTH, VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE);
    AssertMsgReturn(   iSeg <= RTDBGSEGIDX_LAST
                    || (    iSeg >= RTDBGSEGIDX_SPECIAL_FIRST
                        &&  iSeg <= RTDBGSEGIDX_SPECIAL_LAST),
                    ("%#x\n", iSeg),
                    VERR_DBG_INVALID_SEGMENT_INDEX);
    AssertMsgReturn(off + cb >= off, ("off=%RTptr cb=%RTptr\n", off, cb), VERR_DBG_ADDRESS_WRAP);
    AssertReturn(!(fFlags & ~RTDBGSYMBOLADD_F_VALID_MASK), VERR_INVALID_FLAGS);

    RTDBGMOD_LOCK(pDbgMod);

    /*
     * Convert RVAs.
     */
    if (iSeg == RTDBGSEGIDX_RVA)
    {
        iSeg = pDbgMod->pDbgVt->pfnRvaToSegOff(pDbgMod, off, &off);
        if (iSeg == NIL_RTDBGSEGIDX)
        {
            RTDBGMOD_UNLOCK(pDbgMod);
            return VERR_DBG_INVALID_RVA;
        }
    }

    /*
     * Get down to business.
     */
    int rc = pDbgMod->pDbgVt->pfnSymbolAdd(pDbgMod, pszSymbol, cchSymbol, iSeg, off, cb, fFlags, piOrdinal);

    RTDBGMOD_UNLOCK(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModSymbolAdd);


RTDECL(uint32_t) RTDbgModSymbolCount(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, UINT32_MAX);
    RTDBGMOD_LOCK(pDbgMod);

    uint32_t cSymbols = pDbgMod->pDbgVt->pfnSymbolCount(pDbgMod);

    RTDBGMOD_UNLOCK(pDbgMod);
    return cSymbols;
}
RT_EXPORT_SYMBOL(RTDbgModSymbolCount);


RTDECL(int) RTDbgModSymbolByOrdinal(RTDBGMOD hDbgMod, uint32_t iOrdinal, PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    RTDBGMOD_LOCK(pDbgMod);

    int rc = pDbgMod->pDbgVt->pfnSymbolByOrdinal(pDbgMod, iOrdinal, pSymInfo);

    RTDBGMOD_UNLOCK(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModSymbolByOrdinal);


RTDECL(int) RTDbgModSymbolByOrdinalA(RTDBGMOD hDbgMod, uint32_t iOrdinal, PRTDBGSYMBOL *ppSymInfo)
{
    AssertPtr(ppSymInfo);
    *ppSymInfo = NULL;

    PRTDBGSYMBOL pSymInfo = RTDbgSymbolAlloc();
    if (!pSymInfo)
        return VERR_NO_MEMORY;

    int rc = RTDbgModSymbolByOrdinal(hDbgMod, iOrdinal, pSymInfo);

    if (RT_SUCCESS(rc))
        *ppSymInfo = pSymInfo;
    else
        RTDbgSymbolFree(pSymInfo);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModSymbolByOrdinalA);


/**
 * Return a segment number/name as symbol if we couldn't find any
 * valid symbols within the segment.
 */
DECL_NO_INLINE(static, int)
rtDbgModSymbolByAddrTrySegments(PRTDBGMODINT pDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR off,
                                PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo)
{
    Assert(iSeg <= RTDBGSEGIDX_LAST);
    RTDBGSEGMENT SegInfo;
    int rc = pDbgMod->pDbgVt->pfnSegmentByIndex(pDbgMod, iSeg, &SegInfo);
    if (RT_SUCCESS(rc))
    {
        pSymInfo->Value  = 0;
        pSymInfo->cb     = SegInfo.cb;
        pSymInfo->offSeg = 0;
        pSymInfo->iSeg   = iSeg;
        pSymInfo->fFlags = 0;
        if (SegInfo.szName[0])
            RTStrPrintf(pSymInfo->szName, sizeof(pSymInfo->szName), "start_seg%u_%s", SegInfo.iSeg, SegInfo.szName);
        else
            RTStrPrintf(pSymInfo->szName, sizeof(pSymInfo->szName), "start_seg%u", SegInfo.iSeg);
        if (poffDisp)
            *poffDisp = off;
        return VINF_SUCCESS;
    }
    return VERR_SYMBOL_NOT_FOUND;
}


RTDECL(int) RTDbgModSymbolByAddr(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                 PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo)
{
    /*
     * Validate input.
     */
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    AssertPtrNull(poffDisp);
    AssertPtr(pSymInfo);
    AssertReturn(!(fFlags & ~RTDBGSYMADDR_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);

    RTDBGMOD_LOCK(pDbgMod);

    /*
     * Convert RVAs.
     */
    if (iSeg == RTDBGSEGIDX_RVA)
    {
        iSeg = pDbgMod->pDbgVt->pfnRvaToSegOff(pDbgMod, off, &off);
        if (iSeg == NIL_RTDBGSEGIDX)
        {
            RTDBGMOD_UNLOCK(pDbgMod);
            return VERR_DBG_INVALID_RVA;
        }
    }

    /*
     * Get down to business.
     */
    int rc = pDbgMod->pDbgVt->pfnSymbolByAddr(pDbgMod, iSeg, off, fFlags, poffDisp, pSymInfo);

    /* If we failed to locate a symbol, try use the specified segment as a reference. */
    if (   rc == VERR_SYMBOL_NOT_FOUND
        && iSeg <= RTDBGSEGIDX_LAST
        && !(fFlags & RTDBGSYMADDR_FLAGS_GREATER_OR_EQUAL))
        rc = rtDbgModSymbolByAddrTrySegments(pDbgMod, iSeg, off, poffDisp, pSymInfo);

    RTDBGMOD_UNLOCK(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModSymbolByAddr);


RTDECL(int) RTDbgModSymbolByAddrA(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                  PRTINTPTR poffDisp, PRTDBGSYMBOL *ppSymInfo)
{
    AssertPtr(ppSymInfo);
    *ppSymInfo = NULL;

    PRTDBGSYMBOL pSymInfo = RTDbgSymbolAlloc();
    if (!pSymInfo)
        return VERR_NO_MEMORY;

    int rc = RTDbgModSymbolByAddr(hDbgMod, iSeg, off, fFlags, poffDisp, pSymInfo);

    if (RT_SUCCESS(rc))
        *ppSymInfo = pSymInfo;
    else
        RTDbgSymbolFree(pSymInfo);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModSymbolByAddrA);


RTDECL(int) RTDbgModSymbolByName(RTDBGMOD hDbgMod, const char *pszSymbol, PRTDBGSYMBOL pSymInfo)
{
    /*
     * Validate input.
     */
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    AssertPtr(pszSymbol);
    size_t cchSymbol = strlen(pszSymbol);
    AssertReturn(cchSymbol, VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE);
    AssertReturn(cchSymbol < RTDBG_SYMBOL_NAME_LENGTH, VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE);
    AssertPtr(pSymInfo);

    /*
     * Make the query.
     */
    RTDBGMOD_LOCK(pDbgMod);
    int rc = pDbgMod->pDbgVt->pfnSymbolByName(pDbgMod, pszSymbol, cchSymbol, pSymInfo);
    RTDBGMOD_UNLOCK(pDbgMod);

    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModSymbolByName);


RTDECL(int) RTDbgModSymbolByNameA(RTDBGMOD hDbgMod, const char *pszSymbol, PRTDBGSYMBOL *ppSymInfo)
{
    AssertPtr(ppSymInfo);
    *ppSymInfo = NULL;

    PRTDBGSYMBOL pSymInfo = RTDbgSymbolAlloc();
    if (!pSymInfo)
        return VERR_NO_MEMORY;

    int rc = RTDbgModSymbolByName(hDbgMod, pszSymbol, pSymInfo);

    if (RT_SUCCESS(rc))
        *ppSymInfo = pSymInfo;
    else
        RTDbgSymbolFree(pSymInfo);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModSymbolByNameA);


RTDECL(int) RTDbgModLineAdd(RTDBGMOD hDbgMod, const char *pszFile, uint32_t uLineNo,
                            RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t *piOrdinal)
{
    /*
     * Validate input.
     */
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    AssertPtr(pszFile);
    size_t cchFile = strlen(pszFile);
    AssertReturn(cchFile, VERR_DBG_FILE_NAME_OUT_OF_RANGE);
    AssertReturn(cchFile < RTDBG_FILE_NAME_LENGTH, VERR_DBG_FILE_NAME_OUT_OF_RANGE);
    AssertMsgReturn(   iSeg <= RTDBGSEGIDX_LAST
                    || iSeg == RTDBGSEGIDX_RVA,
                    ("%#x\n", iSeg),
                    VERR_DBG_INVALID_SEGMENT_INDEX);
    AssertReturn(uLineNo > 0 && uLineNo < UINT32_MAX, VERR_INVALID_PARAMETER);

    RTDBGMOD_LOCK(pDbgMod);

    /*
     * Convert RVAs.
     */
    if (iSeg == RTDBGSEGIDX_RVA)
    {
        iSeg = pDbgMod->pDbgVt->pfnRvaToSegOff(pDbgMod, off, &off);
        if (iSeg == NIL_RTDBGSEGIDX)
        {
            RTDBGMOD_UNLOCK(pDbgMod);
            return VERR_DBG_INVALID_RVA;
        }
    }

    /*
     * Get down to business.
     */
    int rc = pDbgMod->pDbgVt->pfnLineAdd(pDbgMod, pszFile, cchFile, uLineNo, iSeg, off, piOrdinal);

    RTDBGMOD_UNLOCK(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModLineAdd);


RTDECL(uint32_t) RTDbgModLineCount(RTDBGMOD hDbgMod)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, UINT32_MAX);
    RTDBGMOD_LOCK(pDbgMod);

    uint32_t cLineNumbers = pDbgMod->pDbgVt->pfnLineCount(pDbgMod);

    RTDBGMOD_UNLOCK(pDbgMod);
    return cLineNumbers;
}
RT_EXPORT_SYMBOL(RTDbgModLineCount);


RTDECL(int) RTDbgModLineByOrdinal(RTDBGMOD hDbgMod, uint32_t iOrdinal, PRTDBGLINE pLineInfo)
{
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    RTDBGMOD_LOCK(pDbgMod);

    int rc = pDbgMod->pDbgVt->pfnLineByOrdinal(pDbgMod, iOrdinal, pLineInfo);

    RTDBGMOD_UNLOCK(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModLineByOrdinal);


RTDECL(int) RTDbgModLineByOrdinalA(RTDBGMOD hDbgMod, uint32_t iOrdinal, PRTDBGLINE *ppLineInfo)
{
    AssertPtr(ppLineInfo);
    *ppLineInfo = NULL;

    PRTDBGLINE pLineInfo = RTDbgLineAlloc();
    if (!pLineInfo)
        return VERR_NO_MEMORY;

    int rc = RTDbgModLineByOrdinal(hDbgMod, iOrdinal, pLineInfo);

    if (RT_SUCCESS(rc))
        *ppLineInfo = pLineInfo;
    else
        RTDbgLineFree(pLineInfo);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModLineByOrdinalA);


RTDECL(int) RTDbgModLineByAddr(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTINTPTR poffDisp, PRTDBGLINE pLineInfo)
{
    /*
     * Validate input.
     */
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    AssertPtrNull(poffDisp);
    AssertPtr(pLineInfo);

    RTDBGMOD_LOCK(pDbgMod);

    /*
     * Convert RVAs.
     */
    if (iSeg == RTDBGSEGIDX_RVA)
    {
        iSeg = pDbgMod->pDbgVt->pfnRvaToSegOff(pDbgMod, off, &off);
        if (iSeg == NIL_RTDBGSEGIDX)
        {
            RTDBGMOD_UNLOCK(pDbgMod);
            return VERR_DBG_INVALID_RVA;
        }
    }

    int rc = pDbgMod->pDbgVt->pfnLineByAddr(pDbgMod, iSeg, off, poffDisp, pLineInfo);

    RTDBGMOD_UNLOCK(pDbgMod);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModLineByAddr);


RTDECL(int) RTDbgModLineByAddrA(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTINTPTR poffDisp, PRTDBGLINE *ppLineInfo)
{
    AssertPtr(ppLineInfo);
    *ppLineInfo = NULL;

    PRTDBGLINE pLineInfo = RTDbgLineAlloc();
    if (!pLineInfo)
        return VERR_NO_MEMORY;

    int rc = RTDbgModLineByAddr(hDbgMod, iSeg, off, poffDisp, pLineInfo);

    if (RT_SUCCESS(rc))
        *ppLineInfo = pLineInfo;
    else
        RTDbgLineFree(pLineInfo);
    return rc;
}
RT_EXPORT_SYMBOL(RTDbgModLineByAddrA);


RTDECL(int) RTDbgModUnwindFrame(RTDBGMOD hDbgMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    /*
     * Validate input.
     */
    PRTDBGMODINT pDbgMod = hDbgMod;
    RTDBGMOD_VALID_RETURN_RC(pDbgMod, VERR_INVALID_HANDLE);
    AssertPtr(pState);
    AssertReturn(pState->u32Magic == RTDBGUNWINDSTATE_MAGIC, VERR_INVALID_MAGIC);

    RTDBGMOD_LOCK(pDbgMod);

    /*
     * Convert RVAs.
     */
    if (iSeg == RTDBGSEGIDX_RVA)
    {
        iSeg = pDbgMod->pDbgVt->pfnRvaToSegOff(pDbgMod, off, &off);
        if (iSeg == NIL_RTDBGSEGIDX)
        {
            RTDBGMOD_UNLOCK(pDbgMod);
            return VERR_DBG_INVALID_RVA;
        }
    }

    /*
     * Try the debug module first, then the image.
     */
    int rc = VERR_DBG_NO_UNWIND_INFO;
    if (pDbgMod->pDbgVt->pfnUnwindFrame)
        rc = pDbgMod->pDbgVt->pfnUnwindFrame(pDbgMod, iSeg, off, pState);
    if (   (   rc == VERR_DBG_NO_UNWIND_INFO
            || rc == VERR_DBG_UNWIND_INFO_NOT_FOUND)
        && pDbgMod->pImgVt
        && pDbgMod->pImgVt->pfnUnwindFrame)
    {
        if (rc == VERR_DBG_NO_UNWIND_INFO)
            rc = pDbgMod->pImgVt->pfnUnwindFrame(pDbgMod, iSeg, off, pState);
        else
        {
            rc = pDbgMod->pImgVt->pfnUnwindFrame(pDbgMod, iSeg, off, pState);
            if (rc == VERR_DBG_NO_UNWIND_INFO)
                rc = VERR_DBG_UNWIND_INFO_NOT_FOUND;
        }
    }

    RTDBGMOD_UNLOCK(pDbgMod);
    return rc;

}
RT_EXPORT_SYMBOL(RTDbgModUnwindFrame);

