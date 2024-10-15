/* $Id: mappings.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Folders Service - Mappings support.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SHARED_FOLDERS
#ifdef UNITTEST
# include "testcase/tstSharedFolderService.h"
#endif

#include "mappings.h"
#include "vbsfpath.h"
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/list.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <VBox/AssertGuest.h>

#ifdef UNITTEST
# include "teststubs.h"
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
extern PVBOXHGCMSVCHELPERS g_pHelpers; /* service.cpp */


/* Shared folders order in the saved state and in the g_FolderMapping can differ.
 * So a translation array of root handle is needed.
 */

static MAPPING g_FolderMapping[SHFL_MAX_MAPPINGS];
static SHFLROOT g_aIndexFromRoot[SHFL_MAX_MAPPINGS];
/**< Array running parallel to g_aIndexFromRoot and which entries are increased
 * as an root handle is added or removed.
 *
 * This helps the guest figuring out that a mapping may have been reconfigured
 * or that saved state has been restored.  Entry reuse is very likely given that
 * vbsfRootHandleAdd() always starts searching at the start for an unused entry.
 */
static uint32_t g_auRootHandleVersions[SHFL_MAX_MAPPINGS];
/** Version number that is increased for every change made.
 * This is used by the automount guest service to wait for changes.
 * @note This does not need saving, the guest should be woken up and refresh
 *       its sate when restored. */
static uint32_t volatile g_uFolderMappingsVersion = 0;


/** For recording async vbsfMappingsWaitForChanges calls. */
typedef struct SHFLMAPPINGSWAIT
{
    RTLISTNODE          ListEntry;  /**< List entry. */
    PSHFLCLIENTDATA     pClient;    /**< The client that's waiting. */
    VBOXHGCMCALLHANDLE  hCall;      /**< The call handle to signal completion with. */
    PVBOXHGCMSVCPARM    pParm;      /**< The 32-bit unsigned parameter to stuff g_uFolderMappingsVersion into. */
} SHFLMAPPINGSWAIT;
/** Pointer to async mappings change wait. */
typedef SHFLMAPPINGSWAIT *PSHFLMAPPINGSWAIT;
/** List head for clients waiting on mapping changes (SHFLMAPPINGSWAIT). */
static RTLISTANCHOR g_MappingsChangeWaiters;
/** Number of clients waiting on mapping changes.
 * We use this to limit the number of waiting calls the clients can make.  */
static uint32_t     g_cMappingChangeWaiters = 0;
static void vbsfMappingsWakeupAllWaiters(void);


void vbsfMappingInit(void)
{
    unsigned root;

    for (root = 0; root < RT_ELEMENTS(g_aIndexFromRoot); root++)
    {
        g_aIndexFromRoot[root] = SHFL_ROOT_NIL;
    }

    RTListInit(&g_MappingsChangeWaiters);
}

/**
 * Called before loading mappings from saved state to drop the root IDs.
 */
void vbsfMappingLoadingStart(void)
{
    for (SHFLROOT idRoot = 0; idRoot < RT_ELEMENTS(g_aIndexFromRoot); idRoot++)
        g_aIndexFromRoot[idRoot] = SHFL_ROOT_NIL;

    for (SHFLROOT i = 0; i < RT_ELEMENTS(g_FolderMapping); i++)
        g_FolderMapping[i].fLoadedRootId = false;
}

/**
 * Called when a mapping is loaded to restore the root ID and make sure it
 * exists.
 *
 * @returns VBox status code.
 */
int vbsfMappingLoaded(const MAPPING *pLoadedMapping, SHFLROOT root)
{
    /* Mapping loaded from the saved state with the 'root' index. Which means
     * the guest uses the 'root' as root handle for this folder.
     * Check whether there is the same mapping in g_FolderMapping and
     * update the g_aIndexFromRoot.
     *
     * Also update the mapping properties, which were lost: cMappings.
     */
    if (root >= SHFL_MAX_MAPPINGS)
    {
        return VERR_INVALID_PARAMETER;
    }

    SHFLROOT i;
    for (i = 0; i < RT_ELEMENTS(g_FolderMapping); i++)
    {
        MAPPING *pMapping = &g_FolderMapping[i];

        /* Equal? */
        if (   pLoadedMapping->fValid == pMapping->fValid
            && ShflStringSizeOfBuffer(pLoadedMapping->pMapName) == ShflStringSizeOfBuffer(pMapping->pMapName)
            && memcmp(pLoadedMapping->pMapName, pMapping->pMapName, ShflStringSizeOfBuffer(pMapping->pMapName)) == 0)
        {
            Log(("vbsfMappingLoaded: root=%u i=%u (was %u) (%ls)\n",
                 root, i, g_aIndexFromRoot[root], pLoadedMapping->pMapName->String.utf16));

            if (!pMapping->fLoadedRootId)
            {
                /* First encounter. */
                pMapping->fLoadedRootId = true;

                /* Update the mapping properties. */
                pMapping->cMappings = pLoadedMapping->cMappings;
            }
            else
            {
                /* When pMapping->fLoadedRootId is already true it means that another HGCM client uses the same mapping. */
                Assert(pMapping->cMappings > 1);
            }

            /* Actual index is i. Remember that when the guest uses 'root' it is actually 'i'. */
            AssertLogRelMsg(g_aIndexFromRoot[root] == SHFL_ROOT_NIL,
                            ("idRoot=%u: current %u ([%s]), new %u (%ls [%s])\n",
                             root, g_aIndexFromRoot[root], g_FolderMapping[g_aIndexFromRoot[root]].pszFolderName,
                             i, pLoadedMapping->pMapName->String.utf16, pLoadedMapping->pszFolderName));
            g_aIndexFromRoot[root] = i;

            /* The mapping is known to the host and is used by the guest.
             * No need for a 'placeholder'.
             */
            return VINF_SUCCESS;
        }
    }

    /* No corresponding mapping on the host but the guest still uses it.
     * Add a 'placeholder' mapping.
     */
    LogRel2(("SharedFolders: mapping a placeholder for '%ls' -> '%s'\n",
              pLoadedMapping->pMapName->String.utf16, pLoadedMapping->pszFolderName));
    return vbsfMappingsAdd(pLoadedMapping->pszFolderName, pLoadedMapping->pMapName,
                           pLoadedMapping->fWritable, pLoadedMapping->fAutoMount, pLoadedMapping->pAutoMountPoint,
                           pLoadedMapping->fSymlinksCreate, /* fMissing = */ true, /* fPlaceholder = */ true,
                           pLoadedMapping->enmSymlinkPolicy);
}

/**
 * Called after loading mappings from saved state to make sure every mapping has
 * a root ID.
 */
void vbsfMappingLoadingDone(void)
{
    for (SHFLROOT iMapping = 0; iMapping < RT_ELEMENTS(g_FolderMapping); iMapping++)
        if (g_FolderMapping[iMapping].fValid)
        {
            AssertLogRel(g_FolderMapping[iMapping].pMapName);
            AssertLogRel(g_FolderMapping[iMapping].pszFolderName);

            SHFLROOT idRoot;
            for (idRoot = 0; idRoot < RT_ELEMENTS(g_aIndexFromRoot); idRoot++)
                if (g_aIndexFromRoot[idRoot] == iMapping)
                    break;
            if (idRoot >= RT_ELEMENTS(g_aIndexFromRoot))
            {
                for (idRoot = 0; idRoot < RT_ELEMENTS(g_aIndexFromRoot); idRoot++)
                    if (g_aIndexFromRoot[idRoot] == SHFL_ROOT_NIL)
                        break;
                if (idRoot < RT_ELEMENTS(g_aIndexFromRoot))
                    g_aIndexFromRoot[idRoot] = iMapping;
                else
                    LogRel(("SharedFolders: Warning! No free root ID entry for mapping #%u: %ls [%s]\n", iMapping,
                            g_FolderMapping[iMapping].pMapName->String.utf16, g_FolderMapping[iMapping].pszFolderName));
            }
        }

    /* Log the root ID mappings: */
    if (LogRelIs2Enabled())
        for (SHFLROOT idRoot = 0; idRoot < RT_ELEMENTS(g_aIndexFromRoot); idRoot++)
        {
            SHFLROOT const iMapping = g_aIndexFromRoot[idRoot];
            if (iMapping != SHFL_ROOT_NIL)
                LogRel2(("SharedFolders: idRoot %u: iMapping #%u: %ls [%s]\n", idRoot, iMapping,
                         g_FolderMapping[iMapping].pMapName->String.utf16, g_FolderMapping[iMapping].pszFolderName));
        }
}


MAPPING *vbsfMappingGetByRoot(SHFLROOT root)
{
    if (root < RT_ELEMENTS(g_aIndexFromRoot))
    {
        SHFLROOT iMapping = g_aIndexFromRoot[root];

        if (   iMapping != SHFL_ROOT_NIL
            && iMapping < RT_ELEMENTS(g_FolderMapping))
        {
            return &g_FolderMapping[iMapping];
        }
    }

    return NULL;
}

static SHFLROOT vbsfMappingGetRootFromIndex(SHFLROOT iMapping)
{
    unsigned root;

    for (root = 0; root < RT_ELEMENTS(g_aIndexFromRoot); root++)
    {
        if (iMapping == g_aIndexFromRoot[root])
        {
            return root;
        }
    }

    return SHFL_ROOT_NIL;
}

static MAPPING *vbsfMappingGetByName(PRTUTF16 pwszName, SHFLROOT *pRoot)
{
    for (unsigned i = 0; i < SHFL_MAX_MAPPINGS; i++)
    {
        if (   g_FolderMapping[i].fValid
            && !g_FolderMapping[i].fPlaceholder) /* Don't allow mapping placeholders. */
        {
            if (!RTUtf16LocaleICmp(g_FolderMapping[i].pMapName->String.utf16, pwszName))
            {
                SHFLROOT root = vbsfMappingGetRootFromIndex(i);

                if (root != SHFL_ROOT_NIL)
                {
                    if (pRoot)
                    {
                        *pRoot = root;
                    }
                    return &g_FolderMapping[i];
                }
                AssertFailed();
            }
        }
    }
    return NULL;
}

static void vbsfRootHandleAdd(SHFLROOT iMapping)
{
    for (unsigned root = 0; root < RT_ELEMENTS(g_aIndexFromRoot); root++)
    {
        if (g_aIndexFromRoot[root] == SHFL_ROOT_NIL)
        {
            g_aIndexFromRoot[root] = iMapping;
            g_auRootHandleVersions[root] += 1;
            return;
        }
    }

    AssertFailed();
}

static void vbsfRootHandleRemove(SHFLROOT iMapping)
{
    unsigned cFound = 0;

    for (unsigned root = 0; root < RT_ELEMENTS(g_aIndexFromRoot); root++)
    {
        if (g_aIndexFromRoot[root] == iMapping)
        {
            g_aIndexFromRoot[root] = SHFL_ROOT_NIL;
            g_auRootHandleVersions[root] += 1;
            Log(("vbsfRootHandleRemove: Removed root=%u (iMapping=%u)\n", root, iMapping));

            /* Note! Do not stop here as g_aIndexFromRoot may (at least it could
                     prior to the introduction of fLoadedRootId) contain
                     duplicates after restoring save state. */
            cFound++;
        }
    }

    Assert(cFound > 0); RT_NOREF(cFound);
}



#ifdef UNITTEST
/** Unit test the SHFL_FN_ADD_MAPPING API.  Located here as a form of API
 * documentation. */
void testMappingsAdd(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testMappingsAddBadParameters(hTest);
    /* Add tests as required... */
}
#endif
/*
 * We are always executed from one specific HGCM thread. So thread safe.
 */
int vbsfMappingsAdd(const char *pszFolderName, PSHFLSTRING pMapName, bool fWritable,
                    bool fAutoMount, PSHFLSTRING pAutoMountPoint, bool fSymlinksCreate, bool fMissing, bool fPlaceholder,
                    SymlinkPolicy_T enmSymlinkPolicy)
{
    unsigned i;

    Assert(pszFolderName && pMapName);

    Log(("vbsfMappingsAdd %ls\n", pMapName->String.utf16));

    /* Check for duplicates, ignoring placeholders to give the GUI to change stuff at runtime. */
    /** @todo bird: Not entirely sure about ignoring placeholders, but you cannot
     *        trigger auto-umounting without ignoring them. */
    if (!fPlaceholder)
    {
        for (i = 0; i < SHFL_MAX_MAPPINGS; i++)
        {
            if (   g_FolderMapping[i].fValid
                && !g_FolderMapping[i].fPlaceholder)
            {
                if (!RTUtf16LocaleICmp(g_FolderMapping[i].pMapName->String.utf16, pMapName->String.utf16))
                {
                    AssertMsgFailed(("vbsfMappingsAdd: %ls mapping already exists!!\n", pMapName->String.utf16));
                    return VERR_ALREADY_EXISTS;
                }
            }
        }
    }

    for (i = 0; i < SHFL_MAX_MAPPINGS; i++)
    {
        if (!g_FolderMapping[i].fValid)
        {
            /* Make sure the folder name is an absolute path, otherwise we're
               likely to get into trouble with buffer sizes in vbsfPathGuestToHost. */
            char szAbsFolderName[RTPATH_MAX];
            int rc = vbsfPathAbs(NULL, pszFolderName, szAbsFolderName, sizeof(szAbsFolderName));
            AssertRCReturn(rc, rc);

            g_FolderMapping[i].pszFolderName   = RTStrDup(szAbsFolderName);
            g_FolderMapping[i].pMapName        = ShflStringDup(pMapName);
            g_FolderMapping[i].pAutoMountPoint = ShflStringDup(pAutoMountPoint);
            if (   !g_FolderMapping[i].pszFolderName
                || !g_FolderMapping[i].pMapName
                || !g_FolderMapping[i].pAutoMountPoint)
            {
                RTStrFree(g_FolderMapping[i].pszFolderName);
                RTMemFree(g_FolderMapping[i].pMapName);
                RTMemFree(g_FolderMapping[i].pAutoMountPoint);
                return VERR_NO_MEMORY;
            }

            g_FolderMapping[i].fValid          = true;
            g_FolderMapping[i].cMappings       = 0;
            g_FolderMapping[i].fWritable       = fWritable;
            g_FolderMapping[i].fAutoMount      = fAutoMount;
            g_FolderMapping[i].fSymlinksCreate = fSymlinksCreate;
            g_FolderMapping[i].fMissing        = fMissing;
            g_FolderMapping[i].fPlaceholder    = fPlaceholder;
            g_FolderMapping[i].fLoadedRootId   = false;
            g_FolderMapping[i].enmSymlinkPolicy = enmSymlinkPolicy;

            /* Check if the host file system is case sensitive */
            RTFSPROPERTIES prop;
            prop.fCaseSensitive = false; /* Shut up MSC. */
            rc = RTFsQueryProperties(g_FolderMapping[i].pszFolderName, &prop);
#ifndef DEBUG_bird /* very annoying */
            AssertRC(rc);
#endif
            g_FolderMapping[i].fHostCaseSensitive = RT_SUCCESS(rc) ? prop.fCaseSensitive : false;
            vbsfRootHandleAdd(i);
            vbsfMappingsWakeupAllWaiters();
            break;
        }
    }
    if (i == SHFL_MAX_MAPPINGS)
    {
        AssertLogRelMsgFailed(("vbsfMappingsAdd: no more room to add mapping %s to %ls!!\n", pszFolderName, pMapName->String.utf16));
        return VERR_TOO_MUCH_DATA;
    }

    Log(("vbsfMappingsAdd: added mapping %s to %ls (slot %u, root %u)\n",
         pszFolderName, pMapName->String.utf16, i, vbsfMappingGetRootFromIndex(i)));
    return VINF_SUCCESS;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_REMOVE_MAPPING API.  Located here as a form of API
 * documentation. */
void testMappingsRemove(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testMappingsRemoveBadParameters(hTest);
    /* Add tests as required... */
}
#endif
int vbsfMappingsRemove(PSHFLSTRING pMapName)
{
    Assert(pMapName);
    Log(("vbsfMappingsRemove %ls\n", pMapName->String.utf16));

    /*
     * We must iterate thru the whole table as may have 0+ placeholder entries
     * and 0-1 regular entries with the same name.  Also, it is good to kick
     * the guest automounter into action wrt to evicting placeholders.
     */
    int rc = VERR_FILE_NOT_FOUND;
    for (unsigned i = 0; i < SHFL_MAX_MAPPINGS; i++)
    {
        if (g_FolderMapping[i].fValid)
        {
            if (!RTUtf16LocaleICmp(g_FolderMapping[i].pMapName->String.utf16, pMapName->String.utf16))
            {
                if (g_FolderMapping[i].cMappings != 0)
                {
                    LogRel2(("SharedFolders: removing '%ls' -> '%s'%s, which is still used by the guest\n", pMapName->String.utf16,
                             g_FolderMapping[i].pszFolderName, g_FolderMapping[i].fPlaceholder ? " (again)" : ""));
                    g_FolderMapping[i].fMissing = true;
                    g_FolderMapping[i].fPlaceholder = true;
                    vbsfMappingsWakeupAllWaiters();
                    rc = VINF_PERMISSION_DENIED;
                }
                else
                {
                    /* pMapName can be the same as g_FolderMapping[i].pMapName when
                     * called from vbsfUnmapFolder, log it before deallocating the memory. */
                    Log(("vbsfMappingsRemove: mapping %ls removed\n", pMapName->String.utf16));
                    bool fSame = g_FolderMapping[i].pMapName == pMapName;

                    RTStrFree(g_FolderMapping[i].pszFolderName);
                    RTMemFree(g_FolderMapping[i].pMapName);
                    RTMemFree(g_FolderMapping[i].pAutoMountPoint);
                    g_FolderMapping[i].pszFolderName   = NULL;
                    g_FolderMapping[i].pMapName        = NULL;
                    g_FolderMapping[i].pAutoMountPoint = NULL;
                    g_FolderMapping[i].fValid          = false;
                    vbsfRootHandleRemove(i);
                    vbsfMappingsWakeupAllWaiters();
                    if (rc == VERR_FILE_NOT_FOUND)
                        rc = VINF_SUCCESS;
                    if (fSame)
                        break;
                }
            }
        }
    }

    return rc;
}

const char* vbsfMappingsQueryHostRoot(SHFLROOT root)
{
    MAPPING *pFolderMapping = vbsfMappingGetByRoot(root);
    AssertReturn(pFolderMapping, NULL);
    if (pFolderMapping->fMissing)
        return NULL;
    return pFolderMapping->pszFolderName;
}

int vbsfMappingsQueryHostRootEx(SHFLROOT hRoot, const char **ppszRoot, uint32_t *pcbRootLen)
{
    MAPPING *pFolderMapping = vbsfMappingGetByRoot(hRoot);
    AssertReturn(pFolderMapping, VERR_INVALID_PARAMETER);
    if (pFolderMapping->fMissing)
        return VERR_NOT_FOUND;
    if (   pFolderMapping->pszFolderName == NULL
        || pFolderMapping->pszFolderName[0] == 0)
        return VERR_NOT_FOUND;
    *ppszRoot = pFolderMapping->pszFolderName;
    *pcbRootLen = (uint32_t)strlen(pFolderMapping->pszFolderName);
    return VINF_SUCCESS;
}

bool vbsfIsGuestMappingCaseSensitive(SHFLROOT root)
{
    MAPPING *pFolderMapping = vbsfMappingGetByRoot(root);
    AssertReturn(pFolderMapping, false);
    return pFolderMapping->fGuestCaseSensitive;
}

bool vbsfIsHostMappingCaseSensitive(SHFLROOT root)
{
    MAPPING *pFolderMapping = vbsfMappingGetByRoot(root);
    AssertReturn(pFolderMapping, false);
    return pFolderMapping->fHostCaseSensitive;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_QUERY_MAPPINGS API.  Located here as a form of API
 * documentation (or should it better be inline in include/VBox/shflsvc.h?) */
void testMappingsQuery(RTTEST hTest)
{
    /* The API should return all mappings if we provide enough buffers. */
    testMappingsQuerySimple(hTest);
    /* If we provide too few buffers that should be signalled correctly. */
    testMappingsQueryTooFewBuffers(hTest);
    /* The SHFL_MF_AUTOMOUNT flag means return only auto-mounted mappings. */
    testMappingsQueryAutoMount(hTest);
    /* The mappings return array must have numberOfMappings entries. */
    testMappingsQueryArrayWrongSize(hTest);
}
#endif
/**
 * @note If pMappings / *pcMappings is smaller than the actual amount of
 *       mappings that *could* have been returned *pcMappings contains the
 *       required buffer size so that the caller can retry the operation if
 *       wanted.
 */
int vbsfMappingsQuery(PSHFLCLIENTDATA pClient, bool fOnlyAutoMounts, PSHFLMAPPING pMappings, uint32_t *pcMappings)
{
    LogFlow(("vbsfMappingsQuery: pClient = %p, pMappings = %p, pcMappings = %p, *pcMappings = %d\n",
             pClient, pMappings, pcMappings, *pcMappings));

    uint32_t const cMaxMappings = *pcMappings;
    uint32_t       idx          = 0;
    for (uint32_t i = 0; i < SHFL_MAX_MAPPINGS; i++)
    {
        MAPPING *pFolderMapping = vbsfMappingGetByRoot(i);
        if (   pFolderMapping != NULL
            && pFolderMapping->fValid
            && (   !fOnlyAutoMounts
                || (pFolderMapping->fAutoMount && !pFolderMapping->fPlaceholder)) )
        {
            if (idx < cMaxMappings)
            {
                pMappings[idx].u32Status = SHFL_MS_NEW;
                pMappings[idx].root      = i;
            }
            idx++;
        }
    }

    /* Return actual number of mappings, regardless whether the handed in
     * mapping buffer was big enough. */
    /** @todo r=bird: This is non-standard interface behaviour.  We return
     *        VERR_BUFFER_OVERFLOW or at least a VINF_BUFFER_OVERFLOW here.
     *
     *        Guess this goes well along with ORing SHFL_MF_AUTOMOUNT into
     *        pClient->fu32Flags rather than passing it as fOnlyAutoMounts...
     *        Not amused by this. */
    *pcMappings = idx;

    RT_NOREF_PV(pClient);
    LogFlow(("vbsfMappingsQuery: returns VINF_SUCCESS (idx=%u, cMaxMappings=%u)\n", idx, cMaxMappings));
    return VINF_SUCCESS;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_QUERY_MAP_NAME API.  Located here as a form of API
 * documentation. */
void testMappingsQueryName(RTTEST hTest)
{
    /* If we query an valid mapping it should be returned. */
    testMappingsQueryNameValid(hTest);
    /* If we query an invalid mapping that should be signalled. */
    testMappingsQueryNameInvalid(hTest);
    /* If we pass in a bad string buffer that should be detected. */
    testMappingsQueryNameBadBuffer(hTest);
}
#endif
int vbsfMappingsQueryName(PSHFLCLIENTDATA pClient, SHFLROOT root, SHFLSTRING *pString)
{
    LogFlow(("vbsfMappingsQueryName: pClient = %p, root = %d, *pString = %p\n", pClient, root, pString));

    int rc;
    MAPPING *pFolderMapping = vbsfMappingGetByRoot(root);
    if (pFolderMapping)
    {
        if (pFolderMapping->fValid)
        {
            if (BIT_FLAG(pClient->fu32Flags, SHFL_CF_UTF8))
                rc = ShflStringCopyUtf16BufAsUtf8(pString, pFolderMapping->pMapName);
            else
            {
                /* Not using ShlfStringCopy here as behaviour shouldn't change... */
                if (pString->u16Size < pFolderMapping->pMapName->u16Size)
                {
                    Log(("vbsfMappingsQuery: passed string too short (%d < %d bytes)!\n",
                        pString->u16Size,  pFolderMapping->pMapName->u16Size));
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                {
                    pString->u16Length = pFolderMapping->pMapName->u16Length;
                    memcpy(pString->String.utf16, pFolderMapping->pMapName->String.utf16,
                           pFolderMapping->pMapName->u16Size);
                    rc = VINF_SUCCESS;
                }
            }
        }
        else
            rc = VERR_FILE_NOT_FOUND;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlow(("vbsfMappingsQueryName returns rc = %Rrc\n", rc));
    return rc;
}

/** Queries fWritable flag for the given root. Returns error if the root is not accessible.
 */
int vbsfMappingsQueryWritable(PSHFLCLIENTDATA pClient, SHFLROOT root, bool *fWritable)
{
    RT_NOREF1(pClient);
    int rc = VINF_SUCCESS;

    LogFlow(("vbsfMappingsQueryWritable: pClient = %p, root = %d\n", pClient, root));

    MAPPING *pFolderMapping = vbsfMappingGetByRoot(root);
    AssertReturn(pFolderMapping, VERR_INVALID_PARAMETER);

    if (   pFolderMapping->fValid
        && !pFolderMapping->fMissing)
        *fWritable = pFolderMapping->fWritable;
    else
        rc = VERR_FILE_NOT_FOUND;

    LogFlow(("vbsfMappingsQueryWritable returns rc = %Rrc\n", rc));

    return rc;
}

int vbsfMappingsQueryAutoMount(PSHFLCLIENTDATA pClient, SHFLROOT root, bool *fAutoMount)
{
    RT_NOREF1(pClient);
    int rc = VINF_SUCCESS;

    LogFlow(("vbsfMappingsQueryAutoMount: pClient = %p, root = %d\n", pClient, root));

    MAPPING *pFolderMapping = vbsfMappingGetByRoot(root);
    AssertReturn(pFolderMapping, VERR_INVALID_PARAMETER);

    if (pFolderMapping->fValid)
        *fAutoMount = pFolderMapping->fAutoMount;
    else
        rc = VERR_FILE_NOT_FOUND;

    LogFlow(("vbsfMappingsQueryAutoMount returns rc = %Rrc\n", rc));

    return rc;
}

int vbsfMappingsQuerySymlinksCreate(PSHFLCLIENTDATA pClient, SHFLROOT root, bool *fSymlinksCreate)
{
    RT_NOREF1(pClient);
    int rc = VINF_SUCCESS;

    LogFlow(("vbsfMappingsQuerySymlinksCreate: pClient = %p, root = %d\n", pClient, root));

    MAPPING *pFolderMapping = vbsfMappingGetByRoot(root);
    AssertReturn(pFolderMapping, VERR_INVALID_PARAMETER);

    if (pFolderMapping->fValid)
        *fSymlinksCreate = pFolderMapping->fSymlinksCreate;
    else
        rc = VERR_FILE_NOT_FOUND;

    LogFlow(("vbsfMappingsQuerySymlinksCreate returns rc = %Rrc\n", rc));

    return rc;
}

int vbsfMappingsQuerySymlinkPolicy(PSHFLCLIENTDATA pClient, SHFLROOT root, SymlinkPolicy_T *enmSymlinkPolicy)
{
    RT_NOREF1(pClient);
    int rc = VINF_SUCCESS;

    LogFlow(("vbsfMappingsQuerySymlinkPolicy: pClient = %p, root = %d\n", pClient, root));

    MAPPING *pFolderMapping = vbsfMappingGetByRoot(root);
    AssertReturn(pFolderMapping, VERR_INVALID_PARAMETER);

    if (pFolderMapping->fValid)
        *enmSymlinkPolicy = pFolderMapping->enmSymlinkPolicy;
    else
        rc = VERR_FILE_NOT_FOUND;

    LogFlow(("vbsfMappingsQuerySymlinkPolicy returns rc = %Rrc\n", rc));

    return rc;
}

/**
 * Implements SHFL_FN_QUERY_MAP_INFO.
 * @since VBox 6.0
 */
int vbsfMappingsQueryInfo(PSHFLCLIENTDATA pClient, SHFLROOT root, PSHFLSTRING pNameBuf, PSHFLSTRING pMntPtBuf,
                          uint64_t *pfFlags, uint32_t *puVersion)
{
    LogFlow(("vbsfMappingsQueryInfo: pClient=%p root=%d\n", pClient, root));

    /* Resolve the root handle. */
    int rc;
    PMAPPING pFolderMapping = vbsfMappingGetByRoot(root);
    if (pFolderMapping)
    {
        if (pFolderMapping->fValid)
        {
            /*
             * Produce the output.
             */
            *puVersion = g_auRootHandleVersions[root];

            *pfFlags = 0;
            if (pFolderMapping->fWritable)
                *pfFlags |= SHFL_MIF_WRITABLE;
            if (pFolderMapping->fAutoMount)
                *pfFlags |= SHFL_MIF_AUTO_MOUNT;
            if (pFolderMapping->fHostCaseSensitive)
                *pfFlags |= SHFL_MIF_HOST_ICASE;
            if (pFolderMapping->fGuestCaseSensitive)
                *pfFlags |= SHFL_MIF_GUEST_ICASE;
            if (pFolderMapping->fSymlinksCreate)
                *pfFlags |= SHFL_MIF_SYMLINK_CREATION;

            int rc2;
            if (pClient->fu32Flags & SHFL_CF_UTF8)
            {
                rc = ShflStringCopyUtf16BufAsUtf8(pNameBuf, pFolderMapping->pMapName);
                rc2 = ShflStringCopyUtf16BufAsUtf8(pMntPtBuf, pFolderMapping->pAutoMountPoint);
            }
            else
            {
                rc = ShflStringCopy(pNameBuf, pFolderMapping->pMapName, sizeof(RTUTF16));
                rc2 = ShflStringCopy(pMntPtBuf, pFolderMapping->pAutoMountPoint, sizeof(RTUTF16));
            }
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
        else
            rc = VERR_FILE_NOT_FOUND;
    }
    else
        rc = VERR_INVALID_PARAMETER;
    LogFlow(("vbsfMappingsQueryInfo: returns %Rrc\n", rc));
    return rc;
}



#ifdef UNITTEST
/** Unit test the SHFL_FN_MAP_FOLDER API.  Located here as a form of API
 * documentation. */
void testMapFolder(RTTEST hTest)
{
    /* If we try to map a valid name we should get the root. */
    testMapFolderValid(hTest);
    /* If we try to map a valid name we should get VERR_FILE_NOT_FOUND. */
    testMapFolderInvalid(hTest);
    /* If we map a folder twice we can unmap it twice.
     * Currently unmapping too often is only asserted but not signalled. */
    testMapFolderTwice(hTest);
    /* The delimiter should be converted in e.g. file delete operations. */
    testMapFolderDelimiter(hTest);
    /* Test case sensitive mapping by opening a file with the wrong case. */
    testMapFolderCaseSensitive(hTest);
    /* Test case insensitive mapping by opening a file with the wrong case. */
    testMapFolderCaseInsensitive(hTest);
    /* If the number or types of parameters are wrong the API should fail. */
    testMapFolderBadParameters(hTest);
}
#endif
int vbsfMapFolder(PSHFLCLIENTDATA pClient, PSHFLSTRING pszMapName,
                  RTUTF16 wcDelimiter, bool fCaseSensitive, SHFLROOT *pRoot)
{
    MAPPING *pFolderMapping = NULL;

    if (BIT_FLAG(pClient->fu32Flags, SHFL_CF_UTF8))
        Log(("vbsfMapFolder %s\n", pszMapName->String.utf8));
    else
        Log(("vbsfMapFolder %ls\n", pszMapName->String.utf16));

    AssertMsgReturn(wcDelimiter == '/' || wcDelimiter == '\\',
                    ("Invalid path delimiter: %#x\n", wcDelimiter),
                    VERR_INVALID_PARAMETER);
    if (pClient->PathDelimiter == 0)
    {
        pClient->PathDelimiter = wcDelimiter;
    }
    else
    {
        AssertMsgReturn(wcDelimiter == pClient->PathDelimiter,
                        ("wcDelimiter=%#x PathDelimiter=%#x", wcDelimiter, pClient->PathDelimiter),
                        VERR_INVALID_PARAMETER);
    }

    SHFLROOT RootTmp;
    if (!pRoot)
        pRoot = &RootTmp;
    if (BIT_FLAG(pClient->fu32Flags, SHFL_CF_UTF8))
    {
        int rc;
        PRTUTF16 utf16Name;

        rc = RTStrToUtf16((const char *) pszMapName->String.utf8, &utf16Name);
        if (RT_FAILURE (rc))
            return rc;

        pFolderMapping = vbsfMappingGetByName(utf16Name, pRoot);
        RTUtf16Free(utf16Name);
    }
    else
    {
        pFolderMapping = vbsfMappingGetByName(pszMapName->String.utf16, pRoot);
    }

    if (!pFolderMapping)
    {
        return VERR_FILE_NOT_FOUND;
    }

    /*
     * Check for reference count overflows and settings compatibility.
     * For paranoid reasons, we don't allow modifying the case sensitivity
     * setting while there are other mappings of a folder.
     */
    AssertLogRelReturn(*pRoot < RT_ELEMENTS(pClient->acMappings), VERR_INTERNAL_ERROR);
    AssertLogRelReturn(!pClient->fHasMappingCounts || pClient->acMappings[*pRoot] < _32K, VERR_TOO_MANY_OPENS);
    ASSERT_GUEST_LOGREL_MSG_RETURN(   pFolderMapping->cMappings == 0
                                   || pFolderMapping->fGuestCaseSensitive == fCaseSensitive,
                                   ("Incompatible case sensitivity setting: %s: %u mappings, %ssenitive, requested %ssenitive!\n",
                                    pFolderMapping->pszFolderName, pFolderMapping->cMappings,
                                    pFolderMapping->fGuestCaseSensitive ? "" : "in",  fCaseSensitive ? "" : "in"),
                                   VERR_INCOMPATIBLE_CONFIG);

    /*
     * Go ahead and map it.
     */
    if (pClient->fHasMappingCounts)
        pClient->acMappings[*pRoot] += 1;
    pFolderMapping->cMappings++;
    pFolderMapping->fGuestCaseSensitive = fCaseSensitive;
    Log(("vbsfMmapFolder (cMappings=%u, acMappings[%u]=%u)\n", pFolderMapping->cMappings, *pRoot, pClient->acMappings[*pRoot]));
    return VINF_SUCCESS;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_UNMAP_FOLDER API.  Located here as a form of API
 * documentation. */
void testUnmapFolder(RTTEST hTest)
{
    /* Unmapping a mapped folder should succeed.
     * If the folder is not mapped this is only asserted, not signalled. */
    testUnmapFolderValid(hTest);
    /* Unmapping a non-existant root should fail. */
    testUnmapFolderInvalid(hTest);
    /* If the number or types of parameters are wrong the API should fail. */
    testUnmapFolderBadParameters(hTest);
}
#endif
int vbsfUnmapFolder(PSHFLCLIENTDATA pClient, SHFLROOT root)
{
    RT_NOREF1(pClient);
    int rc = VINF_SUCCESS;

    MAPPING *pFolderMapping = vbsfMappingGetByRoot(root);
    if (pFolderMapping == NULL)
    {
        AssertFailed();
        return VERR_FILE_NOT_FOUND;
    }
    Assert(pFolderMapping->fValid && pFolderMapping->cMappings > 0);

    AssertLogRelReturn(root < RT_ELEMENTS(pClient->acMappings), VERR_INTERNAL_ERROR);
    AssertLogRelReturn(!pClient->fHasMappingCounts || pClient->acMappings[root] > 0, VERR_INVALID_HANDLE);

    if (pClient->fHasMappingCounts)
        pClient->acMappings[root] -= 1;

    if (pFolderMapping->cMappings > 0)
        pFolderMapping->cMappings--;

    uint32_t const cMappings = pFolderMapping->cMappings;
    if (   cMappings == 0
        && pFolderMapping->fPlaceholder)
    {
        /* Automatically remove, it is not used by the guest anymore. */
        Assert(pFolderMapping->fMissing);
        LogRel2(("SharedFolders: unmapping placeholder '%ls' -> '%s'\n",
                pFolderMapping->pMapName->String.utf16, pFolderMapping->pszFolderName));
        vbsfMappingsRemove(pFolderMapping->pMapName);
    }

    Log(("vbsfUnmapFolder (cMappings=%u, acMappings[%u]=%u)\n", cMappings, root, pClient->acMappings[root]));
    return rc;
}

/**
 * SHFL_FN_WAIT_FOR_MAPPINGS_CHANGES implementation.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on change.
 * @retval  VINF_TRY_AGAIN on resume.
 * @retval  VINF_HGCM_ASYNC_EXECUTE if waiting.
 * @retval  VERR_CANCELLED if cancelled.
 * @retval  VERR_OUT_OF_RESOURCES if there are too many pending waits.
 *
 * @param   pClient     The calling client.
 * @param   hCall       The call handle.
 * @param   pParm       The parameter (32-bit).
 * @param   fRestored   Set if this is a call restored & resubmitted from saved
 *                      state.
 * @since   VBox 6.0
 */
int vbsfMappingsWaitForChanges(PSHFLCLIENTDATA pClient, VBOXHGCMCALLHANDLE hCall, PVBOXHGCMSVCPARM pParm, bool fRestored)
{
    /*
     * Return immediately if the fodler mappings have changed since last call
     * or if we got restored from saved state (adding of global folders, etc).
     */
    uint32_t uCurVersion = g_uFolderMappingsVersion;
    if (   pParm->u.uint32 != uCurVersion
        || fRestored
        || (pClient->fu32Flags & SHFL_CF_CANCEL_NEXT_WAIT) )
    {
        int rc = VINF_SUCCESS;
        if (pClient->fu32Flags & SHFL_CF_CANCEL_NEXT_WAIT)
        {
            pClient->fu32Flags &= ~SHFL_CF_CANCEL_NEXT_WAIT;
            rc = VERR_CANCELLED;
        }
        else if (fRestored)
        {
            rc = VINF_TRY_AGAIN;
            if (pParm->u.uint32 == uCurVersion)
                uCurVersion = uCurVersion != UINT32_C(0x55555555) ? UINT32_C(0x55555555) : UINT32_C(0x99999999);
        }
        Log(("vbsfMappingsWaitForChanges: Version %#x -> %#x, returning %Rrc immediately.\n", pParm->u.uint32, uCurVersion, rc));
        pParm->u.uint32 = uCurVersion;
        return rc;
    }

    /*
     * Setup a wait if we can.
     */
    if (g_cMappingChangeWaiters < 64)
    {
        PSHFLMAPPINGSWAIT pWait = (PSHFLMAPPINGSWAIT)RTMemAlloc(sizeof(*pWait));
        if (pWait)
        {
            pWait->pClient = pClient;
            pWait->hCall   = hCall;
            pWait->pParm   = pParm;

            RTListAppend(&g_MappingsChangeWaiters, &pWait->ListEntry);
            g_cMappingChangeWaiters += 1;
            return VINF_HGCM_ASYNC_EXECUTE;
        }
        return VERR_NO_MEMORY;
    }
    LogRelMax(32, ("vbsfMappingsWaitForChanges: Too many threads waiting for changes!\n"));
    return VERR_OUT_OF_RESOURCES;
}

/**
 * SHFL_FN_CANCEL_MAPPINGS_CHANGES_WAITS implementation.
 *
 * @returns VINF_SUCCESS
 * @param   pClient     The calling client to cancel all waits for.
 * @since   VBox 6.0
 */
int vbsfMappingsCancelChangesWaits(PSHFLCLIENTDATA pClient)
{
    uint32_t const uCurVersion = g_uFolderMappingsVersion;

    PSHFLMAPPINGSWAIT pCur, pNext;
    RTListForEachSafe(&g_MappingsChangeWaiters, pCur, pNext, SHFLMAPPINGSWAIT, ListEntry)
    {
        if (pCur->pClient == pClient)
        {
            RTListNodeRemove(&pCur->ListEntry);
            pCur->pParm->u.uint32 = uCurVersion;
            g_pHelpers->pfnCallComplete(pCur->hCall, VERR_CANCELLED);
            RTMemFree(pCur);
        }
    }

    /* Set a flag to make sure the next SHFL_FN_WAIT_FOR_MAPPINGS_CHANGES doesn't block.
       This should help deal with races between this call and a thread about to do a wait. */
    pClient->fu32Flags |= SHFL_CF_CANCEL_NEXT_WAIT;

    return VINF_SUCCESS;
}

/**
 * Wakes up all clients waiting on
 */
static void vbsfMappingsWakeupAllWaiters(void)
{
    uint32_t const uCurVersion = ++g_uFolderMappingsVersion;

    PSHFLMAPPINGSWAIT pCur, pNext;
    RTListForEachSafe(&g_MappingsChangeWaiters, pCur, pNext, SHFLMAPPINGSWAIT, ListEntry)
    {
        RTListNodeRemove(&pCur->ListEntry);
        pCur->pParm->u.uint32 = uCurVersion;
        g_pHelpers->pfnCallComplete(pCur->hCall, VERR_CANCELLED);
        RTMemFree(pCur);
    }
}

