/* $Id: VBoxWinDrvStore.cpp 108020 2025-01-23 08:29:22Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxWinDrvStore - Windows driver store handling.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <iprt/win/windows.h>
#include <iprt/win/setupapi.h>
#include <newdev.h> /* For INSTALLFLAG_XXX. */
#include <cfgmgr32.h> /* For MAX_DEVICE_ID_LEN. */

#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/ldr.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/utf16.h>

#ifdef DEBUG
# include <iprt/stream.h>
#endif

#include <VBox/GuestHost/VBoxWinDrvDefs.h>
#include <VBox/GuestHost/VBoxWinDrvStore.h>

#include "VBoxWinDrvCommon.h"


/*********************************************************************************************************************************
*   Interface prototypes                                                                                                         *
*********************************************************************************************************************************/
static DECLCALLBACK(int) vboxWinDrvStoreLegacyImpl_Enumerate(PVBOXWINDRVSTORE pThis);


/*********************************************************************************************************************************
*   Implementation                                                                                                               *
*********************************************************************************************************************************/

/** Function pointer for a general Windows driver store enumeration callback. */
typedef int (*PFNVBOXWINDRVSTORE_ENUM_FILES_CALLBACK)(const char *pszFilePathAbs, void *pvCtx);

/**
 * Structure for keeping a generic Windows driver store file enumeration context.
 */
typedef struct _VBOXWINDRVENUMFILESCTX
{
    /** Pointer to driver store instance. */
    PVBOXWINDRVSTORE     pDrvStore;
    /** Pointer to driver store list to add found files to. */
    PVBOXWINDRVSTORELIST pList;
    /** Filter to apply for enumeration. Optional. */
    VBOXWINDRVSTOREENTRY Filter;
} VBOXWINDRVENUMFILESCTX;
/** Pointer to a generic Windows driver store file enumeration context. */
typedef VBOXWINDRVENUMFILESCTX *PVBOXWINDRVENUMFILESCTX;


/**
 * Creates a Windows driver store entry.
 *
 * @returns VBox status code.
 * @param   ppEntry             Where to return the created Windows driver store entry on success.
 */
int vboxWinDrvStoreEntryCreate(PVBOXWINDRVSTOREENTRY *ppEntry)
{
    PVBOXWINDRVSTOREENTRY pEntry = (PVBOXWINDRVSTOREENTRY)RTMemAllocZ(sizeof(VBOXWINDRVSTOREENTRY));
    AssertPtrReturn(pEntry, VERR_NO_MEMORY);

    *ppEntry = pEntry;

    return VINF_SUCCESS;
}

/**
 * Destroys a Windows driver store entry.
 *
 * @param   pEntry              Windows driver store entry to destroy.
 */
void vboxWinDrvStoreEntryDestroy(PVBOXWINDRVSTOREENTRY pEntry)
{
    if (!pEntry)
        return;

    RTMemFree(pEntry);
    pEntry = NULL;
}

/**
 * Duplicates a Windows driver store entry.
 *
 * @returns Duplicated Windows driver store entry.  Caller must correct
 *          VBOXWINDRVSTOREENTRY::Node (list entry) immediately.
 * @param   pEntry              Windows driver store list to duplicate.
 */
static PVBOXWINDRVSTOREENTRY vboxWinDrvStoreEntryDup(PVBOXWINDRVSTOREENTRY pEntry)
{
    return (PVBOXWINDRVSTOREENTRY)RTMemDup(pEntry, sizeof(VBOXWINDRVSTOREENTRY));
}

/**
 * Initializes a Windows driver store list.
 *
 * @param   pList               Windows driver store list to initialize.
 */
static void vboxWinDrvStoreListInit(PVBOXWINDRVSTORELIST pList)
{
    RT_BZERO(pList, sizeof(VBOXWINDRVSTORELIST));

    RTListInit(&pList->List);
}

/**
 * Creates a Windows driver store list.
 *
 * @returns VBox status code.
 * @param   ppList              Where to return the created Windows driver store list on success.
 */
static int vboxWinDrvStoreListCreate(PVBOXWINDRVSTORELIST *ppList)
{
    PVBOXWINDRVSTORELIST pList = (PVBOXWINDRVSTORELIST)RTMemAlloc(sizeof(VBOXWINDRVSTORELIST));
    AssertPtrReturn(pList, VERR_NO_MEMORY);

    vboxWinDrvStoreListInit(pList);

    *ppList = pList;

    return VINF_SUCCESS;
}

/**
 * Destroys a Windows driver store list.
 *
 * @param   pList               Windows driver store list to destroy.
 *                              The pointer will be invalid after return.
 */
static void vboxWinDrvStoreListDestroy(PVBOXWINDRVSTORELIST pList)
{
    if (!pList)
        return;

    PVBOXWINDRVSTOREENTRY pCur, pNext;
    RTListForEachSafe(&pList->List, pCur, pNext, VBOXWINDRVSTOREENTRY, Node)
    {
        RTListNodeRemove(&pCur->Node);
        vboxWinDrvStoreEntryDestroy(pCur);
    }
}

/**
 * Free's a Windows driver store list.
 *
 * @param   pList               Windows driver store list to free.
 *                              The pointer will be invalid after return.
 */
void VBoxWinDrvStoreListFree(PVBOXWINDRVSTORELIST pList)
{
    if (!pList)
        return;

    vboxWinDrvStoreListDestroy(pList);
    RTMemFree(pList);
}

/**
 * Adds an entry to a Windows driver store list.
 *
 * @returns VBox status code.
 * @param   pList               Windows driver store list to add entry to.
 * @param   pEntry              Entry to add.
 */
static int vboxWinDrvStoreListAdd(PVBOXWINDRVSTORELIST pList, PVBOXWINDRVSTOREENTRY pEntry)
{
    RTListAppend(&pList->List, &pEntry->Node);
    pList->cEntries++;

    return VINF_SUCCESS;
}

/**
 * Enumeration for the driver store list query type.
 */
typedef enum VBOXWINDRVSTORELISTQUERYTYPE
{
    /** Queries all entries. */
    VBOXWINDRVSTORELISTQUERYTYPE_ANY = 0,
    /** Query by PnP (Hardware) ID. */
    VBOXWINDRVSTORELISTQUERYTYPE_PNP_ID,
    /** Query by model name. */
    VBOXWINDRVSTORELISTQUERYTYPE_MODEL_NAME,
    /** Query by driver name (.sys). */
    VBOXWINDRVSTORELISTQUERYTYPE_DRIVER_NAME
} VBOXWINDRVSTORELISTQUERYTYPE;

/**
 * Queries a driver store list.
 *
 * @returns VBox status code.
 * @param   pList               Driver store list to query.
 * @param   enmType             Query type.
 * @param   pszTypeConst        Query data. Must match \a enmType.
 * @param   ppListResults       Where to return found results on success.
 *                              Must be destroyed with VBoxWinDrvStoreListFree().
 */
static int vboxWinDrvStoreListQueryEx(PVBOXWINDRVSTORELIST pList, VBOXWINDRVSTORELISTQUERYTYPE enmType, const char *pszTypeConst,
                                      PVBOXWINDRVSTORELIST *ppListResults)
{
    PVBOXWINDRVSTORELIST pListResults;
    int rc = vboxWinDrvStoreListCreate(&pListResults);
    AssertRCReturn(rc, rc);

    /* Currently all query types require strings, so do this for all query times for now. */
    char *pszType = NULL;
    if (pszTypeConst)
    {
        AssertReturn(RTStrIsValidEncoding(pszTypeConst), VERR_INVALID_PARAMETER);
        pszType = RTStrDup(pszTypeConst);
        AssertPtrReturnStmt(pszType, VBoxWinDrvStoreListFree(pListResults), VERR_NO_MEMORY);

        /* Convert strings to lowercase, as RTStrSimplePatternMatch() is case sensitive. */
        RTStrToLower(pszType);
    }

    PVBOXWINDRVSTOREENTRY pCur;
    RTListForEach(&pList->List, pCur, VBOXWINDRVSTOREENTRY, Node)
    {
        bool fFound = false;
        if (pszType == NULL) /* No query type specified? Then directly add the entry to the list. */
            fFound = true;
        else
        {
            PRTUTF16 apszHaystacks[3] = {};  /* Array of haystacks to search in. */
            size_t   cHaystacks = 0;         /* Number of haystacks to search in, zero-based. */
            switch (enmType)
            {
                case VBOXWINDRVSTORELISTQUERYTYPE_PNP_ID:
                {
                    apszHaystacks[cHaystacks++] = pCur->wszPnpId;
                    break;
                }
                case VBOXWINDRVSTORELISTQUERYTYPE_MODEL_NAME:
                {
                    apszHaystacks[cHaystacks++] = pCur->wszModel;
                    break;
                }
                case VBOXWINDRVSTORELISTQUERYTYPE_DRIVER_NAME:
                {
                    apszHaystacks[cHaystacks++] = pCur->wszDriverName;
                    break;
                }
                case VBOXWINDRVSTORELISTQUERYTYPE_ANY:
                {
                    apszHaystacks[cHaystacks++] = pCur->wszPnpId;
                    apszHaystacks[cHaystacks++] = pCur->wszModel;
                    apszHaystacks[cHaystacks++] = pCur->wszDriverName;
                    break;
                }

                default:
                    rc = VERR_NOT_IMPLEMENTED;
                    break;
            }
            if (RT_FAILURE(rc))
                break;

            Assert(cHaystacks <= RT_ELEMENTS(apszHaystacks));
            for (size_t i = 0; i < cHaystacks; i++)
            {
                /* Slow, but does the job for now. */
                char *pszHaystack;
                rc = RTUtf16ToUtf8(apszHaystacks[i], &pszHaystack);
                if (RT_SUCCESS(rc))
                {
                    RTStrToLower(pszHaystack);

                    fFound = RTStrSimplePatternMatch(pszType, pszHaystack);

                    RTStrFree(pszHaystack);

                    if (fFound)
                        break;
                }
            }
        }

        if (fFound)
        {
            PVBOXWINDRVSTOREENTRY pEntry = vboxWinDrvStoreEntryDup(pCur);
            if (pEntry)
                vboxWinDrvStoreListAdd(pListResults, pEntry);
            else
                AssertFailedBreakStmt(rc = VERR_NO_MEMORY);
        }
    }

    RTStrFree(pszType);

    if (RT_SUCCESS(rc))
        *ppListResults = pListResults;
    else
        VBoxWinDrvStoreListFree(pListResults);

    return rc;
}

/**
 * Enumerates files of a given (local) directory.
 *
 * @returns VBox status code.
 * @param   pszPathAbs          Absolute path to enumerate files for.
 * @param   pszFilter           Filter to use for enumeration (NT wildcards supported).
 * @param   pfnCallback         Pointer to callback function to invoke for matching files.
 * @param   pvCtx               User-supplied pointer to use.
 */
static int vboxWinDrvStoreEnumerateFiles(const char *pszPathAbs, const char *pszFilter,
                                         PFNVBOXWINDRVSTORE_ENUM_FILES_CALLBACK pfnCallback, void *pvCtx)
{
    char szPathWithFilter[RTPATH_MAX];
    int rc = RTStrCopy(szPathWithFilter, sizeof(szPathWithFilter), pszPathAbs);
    AssertRCReturn(rc, rc);

    RTDIR hDir = NIL_RTDIR;

    if (pszFilter)
    {
        rc = RTPathAppend(szPathWithFilter, sizeof(szPathWithFilter), pszFilter);
        if (RT_SUCCESS(rc))
            rc = RTDirOpenFiltered(&hDir, szPathWithFilter, RTDIRFILTER_WINNT, 0);
    }
    else
        rc = RTDirOpen(&hDir, szPathWithFilter);

    if (RT_FAILURE(rc))
        return rc;

    for (;;)
    {
        char szFileAbs[RTPATH_MAX];

        RTDIRENTRY DirEntry;
        rc = RTDirRead(hDir, &DirEntry, NULL);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_NO_MORE_FILES)
                rc = VINF_SUCCESS;
            break;
        }

        rc = RTStrCopy(szFileAbs, sizeof(szFileAbs), pszPathAbs);
        AssertRCBreak(rc);
        rc = RTPathAppend(szFileAbs, sizeof(szFileAbs), DirEntry.szName);
        AssertRCBreak(rc);

        rc = pfnCallback(szFileAbs, pvCtx);
        if (RT_FAILURE(rc))
            break;
    }

    RTDirClose(hDir);
    return rc;
}

/**
 * Initializes a driver store entry from a given INF file.
 *
 * @returns VBox status code.
 * @param   pEntry              Driver store entry to initialize.
 * @param   pszFilePathAbs      Absolute path to INF file.
 */
static int vboxWinDrvStoreEntryInitFromInf(PVBOXWINDRVSTOREENTRY pEntry, const char *pszFilePathAbs)
{
    HINF hInf;
    int rc = VBoxWinDrvInfOpenUtf8(pszFilePathAbs, &hInf);
    if (RT_FAILURE(rc))
        return rc;

    PRTUTF16 pwszFile;
    rc = RTStrToUtf16(RTPathFilename(pszFilePathAbs), &pwszFile);
    if (RT_SUCCESS(rc))
    {
        rc = RTUtf16Copy(pEntry->wszInfFile, RT_ELEMENTS(pEntry->wszInfFile), pwszFile);
        if (RT_SUCCESS(rc))
        {
            PRTUTF16 pwszMainSection;
            VBOXWINDRVINFTYPE enmType = VBoxWinDrvInfGetTypeEx(hInf, &pwszMainSection);
            if (enmType != VBOXWINDRVINFTYPE_INVALID)
            {
                PRTUTF16 pwszModel;
                rc = VBoxWinDrvInfQueryFirstModel(hInf, pwszMainSection, &pwszModel);
                if (RT_SUCCESS(rc))
                {
                    rc = RTUtf16Copy(pEntry->wszModel, RT_ELEMENTS(pEntry->wszModel), pwszModel);
                    if (RT_SUCCESS(rc))
                    {
                        /* PnP ID is optional. */
                        PRTUTF16 pwszPnpId;
                        int rc2 = VBoxWinDrvInfQueryFirstPnPId(hInf, pEntry->wszModel, &pwszPnpId);
                        if (RT_SUCCESS(rc2))
                        {
                            rc = RTUtf16Copy(pEntry->wszPnpId, RT_ELEMENTS(pEntry->wszPnpId), pwszPnpId);
                            RTUtf16Free(pwszPnpId);
                        }
                    }

                    RTUtf16Free(pwszModel);
                }

                RTUtf16Free(pwszMainSection);
            }
            else
                rc = VERR_INVALID_PARAMETER;
        }

        int rc2 = VBoxWinDrvInfQuerySectionVer(hInf, &pEntry->Ver);
        if (RT_SUCCESS(rc))
            rc = rc2;

        RTUtf16Free(pwszFile);
    }

    VBoxWinDrvInfClose(hInf);
    return rc;
}

/**
 * Initializes a driver store.
 *
 * @returns VBox status code.
 * @param   pDrvStore           Driver store to initialize.
 */
static int vboxWinDrvStoreInit(PVBOXWINDRVSTORE pDrvStore)
{
    vboxWinDrvStoreListInit(&pDrvStore->lstDrivers);

    int rc = VINF_SUCCESS;

    uint64_t const uNtVer = RTSystemGetNtVersion();
    if (uNtVer >= RTSYSTEM_MAKE_NT_VERSION(5, 0, 0)) /* for W2K, W7, Vista / 2008 Server and up. */
    {
        pDrvStore->Backend.Iface.pfnEnumerate = vboxWinDrvStoreLegacyImpl_Enumerate;
    }
    else
    {
        rc = VERR_NOT_IMPLEMENTED;
    }

    if (   RT_SUCCESS(rc)
        && pDrvStore->Backend.Iface.pfnEnumerate)
        rc = pDrvStore->Backend.Iface.pfnEnumerate(pDrvStore);

    return rc;
}

/**
 * Creates a driver store.
 *
 * @returns VBox status code.
 * @param   pDrvStore           Where to return the driver store on success.
 */
int VBoxWinDrvStoreCreate(PVBOXWINDRVSTORE *ppDrvStore)
{
    PVBOXWINDRVSTORE pDrvStore = (PVBOXWINDRVSTORE)RTMemAllocZ(sizeof(VBOXWINDRVSTORE));
    AssertPtrReturn(pDrvStore, VERR_NO_MEMORY);

    int rc = vboxWinDrvStoreInit(pDrvStore);
    if (RT_SUCCESS(rc))
    {
        *ppDrvStore = pDrvStore;
    }
    else
        RTMemFree(pDrvStore);

    return rc;
}

/**
 * Destroys a driver store.
 *
 * @param   pDrvStore           Driver store to destroy.
 *                              The pointer will be invalid after return.
 */
void VBoxWinDrvStoreDestroy(PVBOXWINDRVSTORE pDrvStore)
{
    if (!pDrvStore)
        return;

    vboxWinDrvStoreListDestroy(&pDrvStore->lstDrivers);

    RTMemFree(pDrvStore);
    pDrvStore = NULL;
}

/**
 * Queries the driver store for a specific pattern.
 *
 * @returns VBox status code.
 * @param   pDrvStore           Driver store to query.
 * @param   pszPattern          Pattern (RTStrSimplePatternMatch) to query for.
 * @param   ppResults           Where to return the results list on success.
 *                              Must be free'd with VBoxWinDrvStoreListFree().
 */
int VBoxWinDrvStoreQueryAny(PVBOXWINDRVSTORE pDrvStore, const char *pszPattern, PVBOXWINDRVSTORELIST *ppResults)
{
    return vboxWinDrvStoreListQueryEx(&pDrvStore->lstDrivers, VBOXWINDRVSTORELISTQUERYTYPE_ANY, pszPattern, ppResults);
}

/**
 * Queries the driver store for all found entries.
 *
 * @returns VBox status code.
 * @param   pDrvStore           Driver store to query.
 * @param   ppResults           Where to return the results list on success.
 *                              Must be free'd with VBoxWinDrvStoreListFree().
 */
int VBoxWinDrvStoreQueryAll(PVBOXWINDRVSTORE pDrvStore, PVBOXWINDRVSTORELIST *ppResults)
{
    return VBoxWinDrvStoreQueryAny(pDrvStore, NULL /* pszPattern */, ppResults);
}

/**
 * Queries the driver store for a PnP ID.
 *
 * @returns VBox status code.
 * @param   pDrvStore           Driver store to query.
 * @param   pszPnpId            PnP ID pattern (RTStrSimplePatternMatch) to
 *                              query.
 * @param   ppResults           Where to return the results list on success.
 *                              Must be free'd with VBoxWinDrvStoreListFree().
 */
int VBoxWinDrvStoreQueryByPnpId(PVBOXWINDRVSTORE pDrvStore, const char *pszPnpId, PVBOXWINDRVSTORELIST *ppResults)
{
    return vboxWinDrvStoreListQueryEx(&pDrvStore->lstDrivers, VBOXWINDRVSTORELISTQUERYTYPE_PNP_ID, pszPnpId, ppResults);
}

/**
 * Queries the driver store for a model name.
 *
 * @returns VBox status code.
 * @param   pDrvStore           Driver store to query.
 * @param   pszPnpId            Model name pattern (RTStrSimplePatternMatch) to
 *                              query.
 * @param   ppResults           Where to return the results list on success.
 *                              Must be free'd with VBoxWinDrvStoreListFree().
 */
int VBoxWinDrvStoreQueryByModelName(PVBOXWINDRVSTORE pDrvStore, const char *pszModelName, PVBOXWINDRVSTORELIST *ppResults)
{
    return vboxWinDrvStoreListQueryEx(&pDrvStore->lstDrivers, VBOXWINDRVSTORELISTQUERYTYPE_MODEL_NAME, pszModelName, ppResults);
}

/**
 * Returns the backend's location.
 *
 * @returns The backend's location.
 * @param   pDrvStore           Driver store to return location for.
 */
const char *VBoxWinDrvStoreBackendGetLocation(PVBOXWINDRVSTORE pDrvStore)
{
    /* Currently the only type available, so keep it simple for now. */
    return pDrvStore->Backend.u.LocalFs.szPathAbs;
}


/*********************************************************************************************************************************
*   Leacy driver store implementation                                                                                            *
*********************************************************************************************************************************/

static DECLCALLBACK(int) vboxWinDrvStoreImplLegacy_OemInfEnumCallback(const char *pszFilePathAbs, void *pvCtx)
{
    PVBOXWINDRVENUMFILESCTX pCtx = (PVBOXWINDRVENUMFILESCTX)pvCtx;

    bool fFound = false;

    PVBOXWINDRVSTOREENTRY pEntry = NULL;
    int rc = vboxWinDrvStoreEntryCreate(&pEntry);
    if (RT_SUCCESS(rc))
    {
        rc = vboxWinDrvStoreEntryInitFromInf(pEntry, pszFilePathAbs);
        if (RT_SUCCESS(rc))
        {
            do
            {
                /* Filter by model? */
                if (   pCtx->Filter.wszModel[0] != '\0'
                    && RTUtf16ICmp(pCtx->Filter.wszModel, pEntry->wszModel))
                {
                    break;
                }

                /* Filter by PnP ID? */
                if (   pCtx->Filter.wszPnpId[0] != '\0'
                    /* Insensitive case compare for GUIDs. */
                    && RTUtf16ICmp(pCtx->Filter.wszPnpId, pEntry->wszPnpId))
                {
                    break;
                }

                rc = vboxWinDrvStoreListAdd(pCtx->pList, pEntry);
                AssertRCBreak(rc);
                fFound = true;

            } while (0);
        }

        if (   RT_FAILURE(rc)
            || !fFound)
            vboxWinDrvStoreEntryDestroy(pEntry);
    }

    return VINF_SUCCESS; /* Keep enumeration going. */
}

static DECLCALLBACK(int) vboxWinDrvStoreLegacyImpl_Enumerate(PVBOXWINDRVSTORE pThis)
{
    /* Note1: Do *not* rely on environment variables here due to security reasons. */
    /* Note2: Do *not* use GetSystemWindowsDirectoryW() here, as this breaks running on W2K (not available there). */
    RTUTF16 wszWinDir[RTPATH_MAX];
    UINT    cwcWinDir = GetWindowsDirectoryW(wszWinDir, RT_ELEMENTS(wszWinDir));
    if (cwcWinDir <= 0)
        return VERR_PATH_NOT_FOUND;

    char *pszWinDir;
    int rc = RTUtf16ToUtf8(wszWinDir, &pszWinDir);
    AssertRCReturn(rc, rc);

    rc = RTStrCopy(pThis->Backend.u.LocalFs.szPathAbs, sizeof(pThis->Backend.u.LocalFs.szPathAbs), pszWinDir);
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(pThis->Backend.u.LocalFs.szPathAbs, sizeof(pThis->Backend.u.LocalFs.szPathAbs), "INF");

    if (RT_FAILURE(rc))
        return rc;

    pThis->Backend.enmType = VBOXWINDRVSTOREBACKENDTYPE_LOCAL_FS;

    VBOXWINDRVENUMFILESCTX Ctx;
    RT_ZERO(Ctx);
    Ctx.pDrvStore = pThis;
    Ctx.pList     = &pThis->lstDrivers;

    return vboxWinDrvStoreEnumerateFiles(pThis->Backend.u.LocalFs.szPathAbs, "oem*.inf",
                                         vboxWinDrvStoreImplLegacy_OemInfEnumCallback, &Ctx);
}

