/* $Id: clipboard-transfers.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard: Common clipboard transfer handling code.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/log.h>

#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/list.h>
#include <iprt/path.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/uri.h>
#include <iprt/utf16.h>

#include <VBox/err.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/GuestHost/SharedClipboard-transfers.h>



/*********************************************************************************************************************************
 * Prototypes                                                                                                                    *
 ********************************************************************************************************************************/

static void shClTransferCopyCallbacks(PSHCLTRANSFERCALLBACKS pCallbacksDst, PSHCLTRANSFERCALLBACKS pCallbacksSrc);
DECLINLINE(void) shClTransferLock(PSHCLTRANSFER pTransfer);
DECLINLINE(void) shClTransferUnlock(PSHCLTRANSFER pTransfer);
static void shClTransferSetCallbacks(PSHCLTRANSFER pTransfer, PSHCLTRANSFERCALLBACKS pCallbacks);
static int shClTransferSetStatus(PSHCLTRANSFER pTransfer, SHCLTRANSFERSTATUS enmStatus);
static int shClTransferThreadCreate(PSHCLTRANSFER pTransfer, PFNSHCLTRANSFERTHREAD pfnThreadFunc, void *pvUser);
static int shClTransferThreadDestroy(PSHCLTRANSFER pTransfer, RTMSINTERVAL uTimeoutMs);

static void shclTransferCtxTransferRemoveAndUnregister(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer);
static PSHCLTRANSFER shClTransferCtxGetTransferByIdInternal(PSHCLTRANSFERCTX pTransferCtx, SHCLTRANSFERID uId);
static PSHCLTRANSFER shClTransferCtxGetTransferByIndexInternal(PSHCLTRANSFERCTX pTransferCtx, uint32_t uIdx);


/*********************************************************************************************************************************
 * Transfer List                                                                                                                 *
 ********************************************************************************************************************************/

/**
 * Initializes a transfer list.
 *
 * @param   pList               Transfer list to initialize.
 */
void ShClTransferListInit(PSHCLLIST pList)
{
    RT_ZERO(pList->Hdr);
    RTListInit(&pList->lstEntries);
}

/**
 * Destroys a transfer list.
 *
 * @param   pList               Transfer list to destroy.
 */
void ShClTransferListDestroy(PSHCLLIST pList)
{
    if (!pList)
        return;

    PSHCLLISTENTRY pEntry, pEntryNext;
    RTListForEachSafe(&pList->lstEntries, pEntry, pEntryNext, SHCLLISTENTRY, Node)
    {
        RTListNodeRemove(&pEntry->Node);
        ShClTransferListEntryDestroy(pEntry);
        RTMemFree(pEntry);
    }

    RT_ZERO(pList->Hdr);
}

/**
 * Adds a list entry to a transfer list.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to add entry to.
 * @param   pEntry              Entry to add.
 * @param   fAppend             \c true to append to a list, or \c false to prepend.
 */
int ShClTransferListAddEntry(PSHCLLIST pList, PSHCLLISTENTRY pEntry, bool fAppend)
{
    AssertReturn(ShClTransferListEntryIsValid(pEntry), VERR_INVALID_PARAMETER);

    if (fAppend)
        RTListAppend(&pList->lstEntries, &pEntry->Node);
    else
        RTListPrepend(&pList->lstEntries, &pEntry->Node);
    pList->Hdr.cEntries++;

    LogFlowFunc(("%p: '%s' (%RU32 bytes) + %RU32 bytes info -> now %RU32 entries\n",
                 pList, pEntry->pszName, pEntry->cbName, pEntry->cbInfo, pList->Hdr.cEntries));

    return VINF_SUCCESS;
}

/**
 * Allocates a new transfer list.
 *
 * @returns Allocated transfer list on success, or NULL on failure.
 */
PSHCLLIST ShClTransferListAlloc(void)
{
    PSHCLLIST pList = (PSHCLLIST)RTMemAllocZ(sizeof(SHCLLIST));
    if (pList)
    {
        ShClTransferListInit(pList);
        return pList;
    }

    return NULL;
}

/**
 * Frees a transfer list.
 *
 * @param   pList               Transfer list to free. The pointer will be
 *                              invalid after returning from this function.
 */
void ShClTransferListFree(PSHCLLIST pList)
{
    if (!pList)
        return;

    ShClTransferListDestroy(pList);

    RTMemFree(pList);
    pList = NULL;
}

/**
 * Returns a specific list entry of a transfer list.
 *
 * @returns Pointer to list entry if found, or NULL if not found.
 * @param   pList               Clipboard transfer list to get list entry from.
 * @param   uIdx                Index of list entry to return.
 */
DECLINLINE(PSHCLLISTENTRY) shClTransferListGetEntryById(PSHCLLIST pList, uint32_t uIdx)
{
    if (uIdx >= pList->Hdr.cEntries)
        return NULL;

    Assert(!RTListIsEmpty(&pList->lstEntries));

    PSHCLLISTENTRY pIt = RTListGetFirst(&pList->lstEntries, SHCLLISTENTRY, Node);
    while (uIdx) /** @todo Slow, but works for now. */
    {
        pIt = RTListGetNext(&pList->lstEntries, pIt, SHCLLISTENTRY, Node);
        uIdx--;
    }

    return pIt;
}

/**
 * Initializes an list handle info structure.
 *
 * @returns VBox status code.
 * @param   pInfo               List handle info structure to initialize.
 */
int ShClTransferListHandleInfoInit(PSHCLLISTHANDLEINFO pInfo)
{
    AssertPtrReturn(pInfo, VERR_INVALID_POINTER);

    pInfo->hList   = NIL_SHCLLISTHANDLE;
    pInfo->enmType = SHCLOBJTYPE_INVALID;

    pInfo->pszPathLocalAbs = NULL;

    RT_ZERO(pInfo->u);

    return VINF_SUCCESS;
}

/**
 * Destroys a list handle info structure.
 *
 * @param   pInfo               List handle info structure to destroy.
 */
void ShClTransferListHandleInfoDestroy(PSHCLLISTHANDLEINFO pInfo)
{
    if (!pInfo)
        return;

    if (pInfo->pszPathLocalAbs)
    {
        RTStrFree(pInfo->pszPathLocalAbs);
        pInfo->pszPathLocalAbs = NULL;
    }
}

/**
 * Allocates a transfer list header structure.
 *
 * @returns VBox status code.
 * @param   ppListHdr           Where to store the allocated transfer list header structure on success.
 */
int ShClTransferListHdrAlloc(PSHCLLISTHDR *ppListHdr)
{
    int rc;

    PSHCLLISTHDR pListHdr = (PSHCLLISTHDR)RTMemAllocZ(sizeof(SHCLLISTHDR));
    if (pListHdr)
    {
        *ppListHdr = pListHdr;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Frees a transfer list header structure.
 *
 * @param   pListEntry          Transfer list header structure to free.
 *                              The pointer will be invalid on return.
 */
void ShClTransferListHdrFree(PSHCLLISTHDR pListHdr)
{
    if (!pListHdr)
        return;

    LogFlowFuncEnter();

    ShClTransferListHdrDestroy(pListHdr);

    RTMemFree(pListHdr);
    pListHdr = NULL;
}

/**
 * Duplicates (allocates) a transfer list header structure.
 *
 * @returns Duplicated transfer list header structure on success.
 * @param   pListHdr            Transfer list header to duplicate.
 */
PSHCLLISTHDR ShClTransferListHdrDup(PSHCLLISTHDR pListHdr)
{
    AssertPtrReturn(pListHdr, NULL);

    PSHCLLISTHDR pListHdrDup = (PSHCLLISTHDR)RTMemAlloc(sizeof(SHCLLISTHDR));
    if (pListHdrDup)
        *pListHdrDup = *pListHdr;

    return pListHdrDup;
}

/**
 * Initializes a transfer list header structure.
 *
 * @returns VBox status code.
 * @param   pListHdr            Transfer list header struct to initialize.
 */
int ShClTransferListHdrInit(PSHCLLISTHDR pListHdr)
{
    AssertPtrReturn(pListHdr, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    ShClTransferListHdrReset(pListHdr);

    return VINF_SUCCESS;
}

/**
 * Destroys a transfer list header structure.
 *
 * @param   pListHdr            Transfer list header struct to destroy.
 */
void ShClTransferListHdrDestroy(PSHCLLISTHDR pListHdr)
{
    if (!pListHdr)
        return;

    LogFlowFuncEnter();
}

/**
 * Resets a transfer list header structure.
 *
 * @returns VBox status code.
 * @param   pListHdr            Transfer list header struct to reset.
 */
void ShClTransferListHdrReset(PSHCLLISTHDR pListHdr)
{
    AssertPtrReturnVoid(pListHdr);

    LogFlowFuncEnter();

    RT_BZERO(pListHdr, sizeof(SHCLLISTHDR));
}

/**
 * Returns whether a given transfer list header is valid or not.
 *
 * @returns \c true if valid, \c false if not.
 * @param   pListHdr            Transfer list header to validate.
 */
bool ShClTransferListHdrIsValid(PSHCLLISTHDR pListHdr)
{
    RT_NOREF(pListHdr);
    return true; /** @todo Implement this. */
}

/**
 * (Deep-)Copies a transfer list open parameters structure from one into another.
 *
 * @returns VBox status code.
 * @param   pDst                Destination parameters to copy to.
 * @param   pSrc                Source parameters to copy from.
 */
int ShClTransferListOpenParmsCopy(PSHCLLISTOPENPARMS pDst, PSHCLLISTOPENPARMS pSrc)
{
    AssertPtrReturn(pDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pSrc, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    if (pSrc->pszFilter)
    {
        pDst->pszFilter = RTStrDup(pSrc->pszFilter);
        if (!pDst->pszFilter)
            rc = VERR_NO_MEMORY;
    }

    if (   RT_SUCCESS(rc)
        && pSrc->pszPath)
    {
        pDst->pszPath = RTStrDup(pSrc->pszPath);
        if (!pDst->pszPath)
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        pDst->fList    = pDst->fList;
        pDst->cbFilter = pSrc->cbFilter;
        pDst->cbPath   = pSrc->cbPath;
    }

    return rc;
}

/**
 * Duplicates a transfer list open parameters structure.
 *
 * @returns Duplicated transfer list open parameters structure on success, or NULL on failure.
 * @param   pParms              Transfer list open parameters structure to duplicate.
 */
PSHCLLISTOPENPARMS ShClTransferListOpenParmsDup(PSHCLLISTOPENPARMS pParms)
{
    AssertPtrReturn(pParms, NULL);

    PSHCLLISTOPENPARMS pParmsDup = (PSHCLLISTOPENPARMS)RTMemAllocZ(sizeof(SHCLLISTOPENPARMS));
    if (!pParmsDup)
        return NULL;

    int rc = ShClTransferListOpenParmsCopy(pParmsDup, pParms);
    if (RT_FAILURE(rc))
    {
        ShClTransferListOpenParmsDestroy(pParmsDup);

        RTMemFree(pParmsDup);
        pParmsDup = NULL;
    }

    return pParmsDup;
}

/**
 * Initializes a transfer list open parameters structure.
 *
 * @returns VBox status code.
 * @param   pParms              Transfer list open parameters structure to initialize.
 */
int ShClTransferListOpenParmsInit(PSHCLLISTOPENPARMS pParms)
{
    AssertPtrReturn(pParms, VERR_INVALID_POINTER);

    RT_BZERO(pParms, sizeof(SHCLLISTOPENPARMS));

    pParms->cbFilter  = SHCL_TRANSFER_PATH_MAX; /** @todo Make this dynamic. */
    pParms->pszFilter = RTStrAlloc(pParms->cbFilter);

    pParms->cbPath    = SHCL_TRANSFER_PATH_MAX; /** @todo Make this dynamic. */
    pParms->pszPath   = RTStrAlloc(pParms->cbPath);

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}

/**
 * Destroys a transfer list open parameters structure.
 *
 * @param   pParms              Transfer list open parameters structure to destroy.
 */
void ShClTransferListOpenParmsDestroy(PSHCLLISTOPENPARMS pParms)
{
    if (!pParms)
        return;

    if (pParms->pszFilter)
    {
        RTStrFree(pParms->pszFilter);
        pParms->pszFilter = NULL;
    }

    if (pParms->pszPath)
    {
        RTStrFree(pParms->pszPath);
        pParms->pszPath = NULL;
    }
}

/**
 * Creates (allocates) and initializes a clipboard list entry structure.
 *
 * @returns VBox status code.
 * @param   ppListEntry         Where to return the created clipboard list entry structure on success.
 *                              Must be free'd with  ShClTransferListEntryFree().
 */
int ShClTransferListEntryAlloc(PSHCLLISTENTRY *ppListEntry)
{
    AssertPtrReturn(ppListEntry, VERR_INVALID_POINTER);

    PSHCLLISTENTRY pListEntry = (PSHCLLISTENTRY)RTMemAllocZ(sizeof(SHCLLISTENTRY));
    if (!pListEntry)
        return VERR_NO_MEMORY;

    *ppListEntry = pListEntry;

    return VINF_SUCCESS;
}

/**
 * Frees a clipboard list entry structure.
 *
 * @param   pEntry              Clipboard list entry structure to free.
 *                              The pointer will be invalid on return.
 */
void ShClTransferListEntryFree(PSHCLLISTENTRY pEntry)
{
    if (!pEntry)
        return;

    /* Make sure to destroy the entry properly, in case the caller forgot this. */
    ShClTransferListEntryDestroy(pEntry);

    RTMemFree(pEntry);
    pEntry = NULL;
}

/**
 * (Deep-)Copies a clipboard list entry structure.
 *
 * @returns VBox status code.
 * @param   pDst                Destination list entry to copy to.
 * @param   pSrc                Source list entry to copy from.
 */
int ShClTransferListEntryCopy(PSHCLLISTENTRY pDst, PSHCLLISTENTRY pSrc)
{
    AssertPtrReturn(pDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pSrc, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    *pDst = *pSrc;

    if (pSrc->pszName)
    {
        pDst->pszName = RTStrDup(pSrc->pszName);
        if (!pDst->pszName)
            rc = VERR_NO_MEMORY;
    }

    if (   RT_SUCCESS(rc)
        && pSrc->pvInfo)
    {
        pDst->pvInfo = RTMemDup(pSrc->pvInfo, pSrc->cbInfo);
        if (pDst->pvInfo)
        {
            pDst->cbInfo = pSrc->cbInfo;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(rc))
    {
        if (pDst->pvInfo)
        {
            RTMemFree(pDst->pvInfo);
            pDst->pvInfo = NULL;
            pDst->cbInfo = 0;
        }
    }

    return rc;
}

/**
 * Duplicates (allocates) a clipboard list entry structure.
 *
 * @returns Duplicated clipboard list entry structure on success.
 * @param   pEntry              Clipboard list entry to duplicate.
 */
PSHCLLISTENTRY ShClTransferListEntryDup(PSHCLLISTENTRY pEntry)
{
    AssertPtrReturn(pEntry, NULL);

    int rc = VINF_SUCCESS;

    PSHCLLISTENTRY pListEntryDup = (PSHCLLISTENTRY)RTMemAllocZ(sizeof(SHCLLISTENTRY));
    if (pListEntryDup)
        rc = ShClTransferListEntryCopy(pListEntryDup, pEntry);

    if (RT_FAILURE(rc))
    {
        ShClTransferListEntryDestroy(pListEntryDup);

        RTMemFree(pListEntryDup);
        pListEntryDup = NULL;
    }

    return pListEntryDup;
}

/**
 * Returns whether a given list entry name is valid or not.
 *
 * @returns \c true if valid, or \c false if not.
 * @param   pszName             Name to check.
 * @param   cbName              Size (in bytes) of \a pszName to check.
 *                              Includes terminator.
 */
static bool shclTransferListEntryNameIsValid(const char *pszName, size_t cbName)
{
    if (!pszName)
        return false;

    size_t const cchLen = strlen(pszName);

    if (  !cbName
        || cchLen == 0
        || cchLen > cbName                 /* Includes zero termination */ - 1
        || cchLen > SHCLLISTENTRY_MAX_NAME /* Ditto */ - 1)
    {
        return false;
    }

    int rc = ShClTransferValidatePath(pszName, false /* fMustExist */);
    if (RT_FAILURE(rc))
        return false;

    return true;
}

/**
 * Initializes a clipboard list entry structure, extended version.
 *
 * @returns VBox status code.
 * @param   pListEntry          Clipboard list entry structure to initialize.
 * @param   fInfo               Info flags (of type VBOX_SHCL_INFO_F_XXX).
 * @param   pszName             Name (e.g. filename) to use. Can be NULL if not being used.
 *                              Up to SHCLLISTENTRY_MAX_NAME characters.
 * @param   pvInfo              Pointer to info data to assign. Must match \a fInfo.
 *                              The list entry takes the ownership of the data on success.
 * @param   cbInfo              Size (in bytes) of \a pvInfo data to assign.
 */
int ShClTransferListEntryInitEx(PSHCLLISTENTRY pListEntry, uint32_t fInfo, const char *pszName, void *pvInfo, uint32_t cbInfo)
{
    AssertPtrReturn(pListEntry, VERR_INVALID_POINTER);
    AssertReturn   (   pszName == NULL
                    || shclTransferListEntryNameIsValid(pszName, strlen(pszName) + 1), VERR_INVALID_PARAMETER);
    /* pvInfo + cbInfo depend on fInfo. See below. */

    RT_BZERO(pListEntry, sizeof(SHCLLISTENTRY));

    if (pszName)
    {
        pListEntry->pszName = RTStrDupN(pszName, SHCLLISTENTRY_MAX_NAME);
        AssertPtrReturn(pListEntry->pszName, VERR_NO_MEMORY);
        pListEntry->cbName = (uint32_t)strlen(pListEntry->pszName) + 1 /* Include terminator */;
    }

    pListEntry->pvInfo = pvInfo;
    pListEntry->cbInfo = cbInfo;
    pListEntry->fInfo  = fInfo;

    return VINF_SUCCESS;
}

/**
 * Initializes a clipboard list entry structure (as empty / invalid).
 *
 * @returns VBox status code.
 * @param   pListEntry          Clipboard list entry structure to initialize.
 */
int ShClTransferListEntryInit(PSHCLLISTENTRY pListEntry)
{
    return ShClTransferListEntryInitEx(pListEntry, VBOX_SHCL_INFO_F_NONE, NULL /* pszName */, NULL /* pvInfo */, 0 /* cbInfo */);
}

/**
 * Destroys a clipboard list entry structure.
 *
 * @param   pListEntry          Clipboard list entry structure to destroy.
 */
void ShClTransferListEntryDestroy(PSHCLLISTENTRY pListEntry)
{
    if (!pListEntry)
        return;

    if (pListEntry->pszName)
    {
        RTStrFree(pListEntry->pszName);

        pListEntry->pszName = NULL;
        pListEntry->cbName  = 0;
    }

    if (pListEntry->pvInfo)
    {
        RTMemFree(pListEntry->pvInfo);
        pListEntry->pvInfo = NULL;
        pListEntry->cbInfo = 0;
    }
}

/**
 * Returns whether a given clipboard list entry is valid or not.
 *
 * @returns \c true if valid, \c false if not.
 * @param   pListEntry          Clipboard list entry to validate.
 */
bool ShClTransferListEntryIsValid(PSHCLLISTENTRY pListEntry)
{
    AssertPtrReturn(pListEntry, false);

    bool fValid = false;

    if (shclTransferListEntryNameIsValid(pListEntry->pszName, pListEntry->cbName))
    {
        fValid =    pListEntry->cbInfo == 0 /* cbInfo / pvInfo is optional. */
                 || pListEntry->pvInfo != NULL;
    }

    if (!fValid)
        LogRel2(("Shared Clipboard: List entry '%s' is invalid\n", pListEntry->pszName));

    return fValid;
}


/*********************************************************************************************************************************
 * Transfer Object                                                                                                               *
 ********************************************************************************************************************************/

/**
 * Initializes a transfer object context.
 *
 * @returns VBox status code.
 * @param   pObjCtx             Transfer object context to initialize.
 */
int ShClTransferObjCtxInit(PSHCLCLIENTTRANSFEROBJCTX pObjCtx)
{
    AssertPtrReturn(pObjCtx, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    pObjCtx->uHandle  = NIL_SHCLOBJHANDLE;

    return VINF_SUCCESS;
}

/**
 * Destroys a transfer object context.
 *
 * @param   pObjCtx             Transfer object context to destroy.
 */
void ShClTransferObjCtxDestroy(PSHCLCLIENTTRANSFEROBJCTX pObjCtx)
{
    AssertPtrReturnVoid(pObjCtx);

    LogFlowFuncEnter();
}

/**
 * Returns if a transfer object context is valid or not.
 *
 * @returns \c true if valid, \c false if not.
 * @param   pObjCtx             Transfer object context to check.
 */
bool ShClTransferObjCtxIsValid(PSHCLCLIENTTRANSFEROBJCTX pObjCtx)
{
    return (   pObjCtx
            && pObjCtx->uHandle != NIL_SHCLOBJHANDLE);
}

/**
 * Initializes a transfer object structure.
 *
 * @returns VBox status code.
 * @param   pObj                Object structure to initialize.
 */
int ShClTransferObjInit(PSHCLTRANSFEROBJ pObj)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);

    pObj->hObj    = NIL_SHCLOBJHANDLE;
    pObj->enmType = SHCLOBJTYPE_INVALID;

    pObj->pszPathLocalAbs = NULL;

    RT_ZERO(pObj->u);

    return VINF_SUCCESS;
}

/**
 * Destroys a transfer object structure.
 *
 * @param   pObj                Object structure to destroy.
 */
void ShClTransferObjDestroy(PSHCLTRANSFEROBJ pObj)
{
    if (!pObj)
        return;

    if (pObj->pszPathLocalAbs)
    {
        RTStrFree(pObj->pszPathLocalAbs);
        pObj->pszPathLocalAbs = NULL;
    }
}

/**
 * Initializes a transfer object open parameters structure.
 *
 * @returns VBox status code.
 * @param   pParms              Transfer object open parameters structure to initialize.
 */
int ShClTransferObjOpenParmsInit(PSHCLOBJOPENCREATEPARMS pParms)
{
    AssertPtrReturn(pParms, VERR_INVALID_POINTER);

    int rc;

    RT_BZERO(pParms, sizeof(SHCLOBJOPENCREATEPARMS));

    pParms->cbPath    = RTPATH_MAX; /** @todo Make this dynamic. */
    pParms->pszPath   = RTStrAlloc(pParms->cbPath);
    if (pParms->pszPath)
    {
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Copies a transfer object open parameters structure from source to destination.
 *
 * @returns VBox status code.
 * @param   pParmsDst           Where to copy the source transfer object open parameters to.
 * @param   pParmsSrc           Which source transfer object open parameters to copy.
 */
int ShClTransferObjOpenParmsCopy(PSHCLOBJOPENCREATEPARMS pParmsDst, PSHCLOBJOPENCREATEPARMS pParmsSrc)
{
    int rc;

    *pParmsDst = *pParmsSrc;

    if (pParmsSrc->pszPath)
    {
        Assert(pParmsSrc->cbPath);
        pParmsDst->pszPath = RTStrDup(pParmsSrc->pszPath);
        if (pParmsDst->pszPath)
        {
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VINF_SUCCESS;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Destroys a transfer object open parameters structure.
 *
 * @param   pParms              Transfer object open parameters structure to destroy.
 */
void ShClTransferObjOpenParmsDestroy(PSHCLOBJOPENCREATEPARMS pParms)
{
    if (!pParms)
        return;

    if (pParms->pszPath)
    {
        RTStrFree(pParms->pszPath);
        pParms->pszPath = NULL;
    }
}

/**
 * Returns a specific transfer object of a transfer.
 *
 * @returns Pointer to transfer object if found, or NULL if not found.
 * @param   pTransfer           Clipboard transfer to get transfer object from.
 * @param   hObj                Object handle of the object to get handle info for.
 */
PSHCLTRANSFEROBJ ShClTransferObjGet(PSHCLTRANSFER pTransfer, SHCLOBJHANDLE hObj)
{
    PSHCLTRANSFEROBJ pIt;
    RTListForEach(&pTransfer->lstObj, pIt, SHCLTRANSFEROBJ, Node) /** @todo Slooow ...but works for now. */
    {
        if (pIt->hObj == hObj)
            return pIt;
    }

    return NULL;
}

/**
 * Opens a transfer object.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to open the object for.
 * @param   pOpenCreateParms    Open / create parameters of transfer object to open / create.
 * @param   phObj               Where to store the handle of transfer object opened on success.
 */
int ShClTransferObjOpen(PSHCLTRANSFER pTransfer, PSHCLOBJOPENCREATEPARMS pOpenCreateParms, PSHCLOBJHANDLE phObj)
{
    AssertPtrReturn(pTransfer,        VERR_INVALID_POINTER);
    AssertPtrReturn(pOpenCreateParms, VERR_INVALID_POINTER);
    AssertPtrReturn(phObj,            VERR_INVALID_POINTER);
    AssertMsgReturn(pTransfer->pszPathRootAbs, ("Transfer has no root path set\n"), VERR_INVALID_PARAMETER);
    /** @todo Check pOpenCreateParms->fCreate flags. */
    AssertMsgReturn(pOpenCreateParms->pszPath, ("No path in open/create params set\n"), VERR_INVALID_PARAMETER);

    if (pTransfer->cObjHandles >= pTransfer->cMaxObjHandles)
        return VERR_SHCLPB_MAX_OBJECTS_REACHED;

    LogFlowFunc(("pszPath=%s, fCreate=0x%x\n", pOpenCreateParms->pszPath, pOpenCreateParms->fCreate));

    int rc;
    if (pTransfer->ProviderIface.pfnObjOpen)
        rc = pTransfer->ProviderIface.pfnObjOpen(&pTransfer->ProviderCtx, pOpenCreateParms, phObj);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
         LogRel(("Shared Clipboard: Opening object '%s' (flags %#x) failed with %Rrc\n",
                 pOpenCreateParms->pszPath, pOpenCreateParms->fCreate, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Closes a transfer object.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer that contains the object to close.
 * @param   hObj                Handle of transfer object to close.
 */
int ShClTransferObjClose(PSHCLTRANSFER pTransfer, SHCLOBJHANDLE hObj)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    int rc;
    if (pTransfer->ProviderIface.pfnObjClose)
        rc = pTransfer->ProviderIface.pfnObjClose(&pTransfer->ProviderCtx, hObj);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Reading object 0x%x failed with %Rrc\n", hObj, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reads from a transfer object.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer that contains the object to read from.
 * @param   hObj                Handle of transfer object to read from.
 * @param   pvBuf               Buffer for where to store the read data.
 * @param   cbBuf               Size (in bytes) of buffer.
 * @param   fFlags              Read flags. Optional.
 * @param   pcbRead             Where to return how much bytes were read on success. Optional.
 */
int ShClTransferObjRead(PSHCLTRANSFER pTransfer,
                        SHCLOBJHANDLE hObj, void *pvBuf, uint32_t cbBuf, uint32_t fFlags, uint32_t *pcbRead)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf,     VERR_INVALID_POINTER);
    AssertReturn   (cbBuf,     VERR_INVALID_PARAMETER);
    /* pcbRead is optional. */
    /** @todo Validate fFlags. */

    int rc;
    if (pTransfer->ProviderIface.pfnObjRead)
        rc = pTransfer->ProviderIface.pfnObjRead(&pTransfer->ProviderCtx, hObj, pvBuf, cbBuf, fFlags, pcbRead);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Reading object 0x%x failed with %Rrc\n", hObj, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Writes to a transfer object.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer that contains the object to write to.
 * @param   hObj                Handle of transfer object to write to.
 * @param   pvBuf               Buffer of data to write.
 * @param   cbBuf               Size (in bytes) of buffer to write.
 * @param   fFlags              Write flags. Optional.
 * @param   pcbWritten          How much bytes were writtenon success. Optional.
 */
int ShClTransferObjWrite(PSHCLTRANSFER pTransfer,
                         SHCLOBJHANDLE hObj, void *pvBuf, uint32_t cbBuf, uint32_t fFlags, uint32_t *pcbWritten)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf,     VERR_INVALID_POINTER);
    AssertReturn   (cbBuf,     VERR_INVALID_PARAMETER);
    /* pcbWritten is optional. */

    int rc;
    if (pTransfer->ProviderIface.pfnObjWrite)
        rc = pTransfer->ProviderIface.pfnObjWrite(&pTransfer->ProviderCtx, hObj, pvBuf, cbBuf, fFlags, pcbWritten);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Writing object 0x%x failed with %Rrc\n", hObj, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Duplicates a transfer object data chunk.
 *
 * @returns Duplicated object data chunk on success, or NULL on failure.
 * @param   pDataChunk          Transfer object data chunk to duplicate.
 */
PSHCLOBJDATACHUNK ShClTransferObjDataChunkDup(PSHCLOBJDATACHUNK pDataChunk)
{
    AssertPtrReturn(pDataChunk, NULL);

    PSHCLOBJDATACHUNK pDataChunkDup = (PSHCLOBJDATACHUNK)RTMemAllocZ(sizeof(SHCLOBJDATACHUNK));
    if (!pDataChunkDup)
        return NULL;

    if (pDataChunk->pvData)
    {
        Assert(pDataChunk->cbData);

        pDataChunkDup->uHandle = pDataChunk->uHandle;
        pDataChunkDup->pvData  = RTMemDup(pDataChunk->pvData, pDataChunk->cbData);
        AssertPtrReturn(pDataChunkDup->pvData, NULL);
        pDataChunkDup->cbData  = pDataChunk->cbData;
    }

    return pDataChunkDup;
}

/**
 * Destroys a transfer object data chunk.
 *
 * @param   pDataChunk          Transfer object data chunk to destroy.
 */
void ShClTransferObjDataChunkDestroy(PSHCLOBJDATACHUNK pDataChunk)
{
    if (!pDataChunk)
        return;

    if (pDataChunk->pvData)
    {
        Assert(pDataChunk->cbData);

        RTMemFree(pDataChunk->pvData);

        pDataChunk->pvData = NULL;
        pDataChunk->cbData = 0;
    }

    pDataChunk->uHandle = 0;
}

/**
 * Frees a transfer object data chunk.
 *
 * @param   pDataChunk          Transfer object data chunk to free.
 *                              The pointer will be invalid on return.
 */
void ShClTransferObjDataChunkFree(PSHCLOBJDATACHUNK pDataChunk)
{
    if (!pDataChunk)
        return;

    ShClTransferObjDataChunkDestroy(pDataChunk);

    RTMemFree(pDataChunk);
    pDataChunk = NULL;
}


/*********************************************************************************************************************************
 * Transfer                                                                                                                      *
 ********************************************************************************************************************************/

/**
 * Creates a clipboard transfer, extended version.
 *
 * @returns VBox status code.
 * @param   enmDir              Specifies the transfer direction of this transfer.
 * @param   enmSource           Specifies the data source of the transfer.
 * @param   pCallbacks          Callback table to use. Optional and can be NULL.
 * @param   cbMaxChunkSize      Maximum transfer chunk size (in bytes) to use.
 * @param   cMaxListHandles     Maximum list entries the transfer can have.
 * @param   cMaxObjHandles      Maximum transfer objects the transfer can have.
 * @param   ppTransfer          Where to return the created clipboard transfer struct.
 *                              Must be destroyed by ShClTransferDestroy().
 */
int ShClTransferCreateEx(SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource, PSHCLTRANSFERCALLBACKS pCallbacks,
                         uint32_t cbMaxChunkSize, uint32_t cMaxListHandles, uint32_t cMaxObjHandles, PSHCLTRANSFER *ppTransfer)
{
    AssertPtrReturn(ppTransfer, VERR_INVALID_POINTER);
    /* pCallbacks can be NULL. */

    LogFlowFuncEnter();

    PSHCLTRANSFER pTransfer = (PSHCLTRANSFER)RTMemAllocZ(sizeof(SHCLTRANSFER));
    AssertPtrReturn(pTransfer, VERR_NO_MEMORY);

    pTransfer->State.uID       = NIL_SHCLTRANSFERID;
    pTransfer->State.enmStatus = SHCLTRANSFERSTATUS_NONE;
    pTransfer->State.enmDir    = enmDir;
    pTransfer->State.enmSource = enmSource;

    pTransfer->Thread.hThread    = NIL_RTTHREAD;
    pTransfer->Thread.fCancelled = false;
    pTransfer->Thread.fStarted   = false;
    pTransfer->Thread.fStop      = false;

    pTransfer->pszPathRootAbs    = NULL;

    pTransfer->uTimeoutMs      = SHCL_TIMEOUT_DEFAULT_MS;
    pTransfer->cbMaxChunkSize  = cbMaxChunkSize;
    pTransfer->cMaxListHandles = cMaxListHandles;
    pTransfer->cMaxObjHandles  = cMaxObjHandles;

    pTransfer->pvUser = NULL;
    pTransfer->cbUser = 0;

    RTListInit(&pTransfer->lstHandles);
    RTListInit(&pTransfer->lstObj);

    /* The provider context + interface is NULL by default. */
    RT_ZERO(pTransfer->ProviderCtx);
    RT_ZERO(pTransfer->ProviderIface);

    /* Make sure to set the callbacks before calling pfnOnCreate below. */
    shClTransferSetCallbacks(pTransfer, pCallbacks);

    ShClTransferListInit(&pTransfer->lstRoots);

    int rc = RTCritSectInit(&pTransfer->CritSect);
    AssertRCReturn(rc, rc);

    rc = RTSemEventCreate(&pTransfer->StatusChangeEvent);
    AssertRCReturn(rc, rc);

    rc = ShClEventSourceCreate(&pTransfer->Events, 0 /* uID */);
    if (RT_SUCCESS(rc))
    {
        if (pTransfer->Callbacks.pfnOnCreated)
            pTransfer->Callbacks.pfnOnCreated(&pTransfer->CallbackCtx);

        *ppTransfer = pTransfer;
    }
    else
    {
        if (pTransfer)
        {
            ShClTransferDestroy(pTransfer);
            RTMemFree(pTransfer);
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates a clipboard transfer with default settings.
 *
 * @returns VBox status code.
 * @param   enmDir              Specifies the transfer direction of this transfer.
 * @param   enmSource           Specifies the data source of the transfer.
 * @param   pCallbacks          Callback table to use. Optional and can be NULL.
 * @param   ppTransfer          Where to return the created clipboard transfer struct.
 *                              Must be destroyed by ShClTransferDestroy().
 */
int ShClTransferCreate(SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource, PSHCLTRANSFERCALLBACKS pCallbacks, PSHCLTRANSFER *ppTransfer)
{
    return ShClTransferCreateEx(enmDir, enmSource, pCallbacks,
                                SHCL_TRANSFER_DEFAULT_MAX_CHUNK_SIZE,
                                SHCL_TRANSFER_DEFAULT_MAX_LIST_HANDLES,
                                SHCL_TRANSFER_DEFAULT_MAX_OBJ_HANDLES,
                                ppTransfer);
}

/**
 * Destroys a clipboard transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to destroy.
 *                              The pointer will be invalid after return.
 */
int ShClTransferDestroy(PSHCLTRANSFER pTransfer)
{
    if (!pTransfer)
        return VINF_SUCCESS;

    if (!RTCritSectIsInitialized(&pTransfer->CritSect))
        return VINF_SUCCESS;

    /* Must come before the refcount check below, as the callback might release a reference. */
    if (pTransfer->Callbacks.pfnOnDestroy)
        pTransfer->Callbacks.pfnOnDestroy(&pTransfer->CallbackCtx);

    AssertMsgReturn(ASMAtomicReadU32(&pTransfer->cRefs) == 0,
                    ("Number of references > 0 (%RU32)\n", pTransfer->cRefs), VERR_WRONG_ORDER);

    LogFlowFuncEnter();

    int rc = shClTransferThreadDestroy(pTransfer, SHCL_TIMEOUT_DEFAULT_MS);
    if (RT_FAILURE(rc))
        return rc;

    ShClTransferReset(pTransfer);

    if (RTCritSectIsInitialized(&pTransfer->CritSect))
        RTCritSectDelete(&pTransfer->CritSect);

    rc = RTSemEventDestroy(pTransfer->StatusChangeEvent);
    AssertRCReturn(rc, rc);
    pTransfer->StatusChangeEvent = NIL_RTSEMEVENT;

    ShClEventSourceDestroy(&pTransfer->Events);

    RTMemFree(pTransfer);
    pTransfer = NULL;

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}

/**
 * Initializes a clipboard transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer to initialize.
 */
int ShClTransferInit(PSHCLTRANSFER pTransfer)
{
    shClTransferLock(pTransfer);

    AssertMsgReturnStmt(pTransfer->State.enmStatus < SHCLTRANSFERSTATUS_INITIALIZED,
                        ("Wrong status (currently is %s)\n", ShClTransferStatusToStr(pTransfer->State.enmStatus)),
                        shClTransferUnlock(pTransfer), VERR_WRONG_ORDER);

    pTransfer->cRefs = 0;

    LogFlowFunc(("uID=%RU32, enmDir=%RU32, enmSource=%RU32\n",
                 pTransfer->State.uID, pTransfer->State.enmDir, pTransfer->State.enmSource));

    pTransfer->cListHandles    = 0;
    pTransfer->uListHandleNext = 1;

    pTransfer->cObjHandles     = 0;
    pTransfer->uObjHandleNext  = 1;

    pTransfer->Thread.fStarted   = false;
    pTransfer->Thread.fStop      = false;
    pTransfer->Thread.fCancelled = false;

    int rc = VINF_SUCCESS;

    if (pTransfer->Callbacks.pfnOnInitialize)
        rc = pTransfer->Callbacks.pfnOnInitialize(&pTransfer->CallbackCtx);
    if (RT_SUCCESS(rc))
    {
        /* Sanity: Make sure that the transfer we're gonna report as INITIALIZED
         *         actually has some root entries set, as the other side can query for those at any time then. */
        if (pTransfer->State.enmDir == SHCLTRANSFERDIR_TO_REMOTE)
            AssertMsgStmt(ShClTransferRootsCount(pTransfer), ("Transfer has no root entries set (yet)\n"), rc = VERR_WRONG_ORDER);

        if (RT_SUCCESS(rc))
            rc = shClTransferSetStatus(pTransfer, SHCLTRANSFERSTATUS_INITIALIZED);
    }

    shClTransferUnlock(pTransfer);

    /* Note: Callback will be called after we unlocked the transfer, as the caller might access the transfer right away. */
    if (   RT_SUCCESS(rc)
        && pTransfer->Callbacks.pfnOnInitialized)
        pTransfer->Callbacks.pfnOnInitialized(&pTransfer->CallbackCtx);

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Initialziation of transfer failed with %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Returns whether a transfer is in a running state or not.
 *
 * @returns @c true if in running state, or @c false if not.
 * @param   pTransfer           Clipboard transfer to return status for.
 */
bool ShClTransferIsRunning(PSHCLTRANSFER pTransfer)
{
    shClTransferLock(pTransfer);

    bool const fRunning = pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_STARTED;

    shClTransferUnlock(pTransfer);

    return fRunning;
}

/**
 * Returns whether a transfer has been (successfully) completed or not.
 *
 * @returns @c true if complete, or @c false if not.
 * @param   pTransfer           Clipboard transfer to return status for.
 */
bool ShClTransferIsComplete(PSHCLTRANSFER pTransfer)
{
    shClTransferLock(pTransfer);

    bool const fCompleted = pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_COMPLETED;

    shClTransferUnlock(pTransfer);

    return fCompleted;
}

/**
 * Returns whether a transfer has been aborted due to cancelling, killing or an error.
 *
 * @returns @c true if in aborted state, or @c false if not.
 * @param   pTransfer           Clipboard transfer to return status for.
 */
bool ShClTransferIsAborted(PSHCLTRANSFER pTransfer)
{
    shClTransferLock(pTransfer);

    bool const fAborted =    pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_CANCELED
                          || pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_KILLED
                          || pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_ERROR;

    shClTransferUnlock(pTransfer);

    return fAborted;
}

/**
 * Locks a transfer.
 *
 * @param   pTransfer           Transfer to lock.
 */
DECLINLINE(void) shClTransferLock(PSHCLTRANSFER pTransfer)
{
    int rc2 = RTCritSectEnter(&pTransfer->CritSect);
    AssertRC(rc2);
}

/**
 * Unlocks a transfer.
 *
 * @param   pTransfer           Transfer to unlock.
 */
DECLINLINE(void) shClTransferUnlock(PSHCLTRANSFER pTransfer)
{
    int rc2 = RTCritSectLeave(&pTransfer->CritSect);
    AssertRC(rc2);
}

/**
 * Acquires a reference to this transfer.
 *
 * @returns New reference count.
 * @param   pTransfer           Transfer to acquire reference for.
 */
uint32_t ShClTransferAcquire(PSHCLTRANSFER pTransfer)
{
    return ASMAtomicIncU32(&pTransfer->cRefs);
}

/**
 * Releases a reference to this transfer.
 *
 * @returns New reference count.
 * @param   pTransfer           Transfer to release reference for.
 */
uint32_t ShClTransferRelease(PSHCLTRANSFER pTransfer)
{
    const uint32_t cRefs = ASMAtomicDecU32(&pTransfer->cRefs);
    Assert(pTransfer->cRefs <= VBOX_SHCL_MAX_TRANSFERS); /* Not perfect, but better than nothing. */
    return cRefs;
}

/**
 * Opens a transfer list.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to handle.
 * @param   pOpenParms          List open parameters to use for opening.
 * @param   phList              Where to store the List handle of opened list on success.
 */
int ShClTransferListOpen(PSHCLTRANSFER pTransfer, PSHCLLISTOPENPARMS pOpenParms,
                         PSHCLLISTHANDLE phList)
{
    AssertPtrReturn(pTransfer,  VERR_INVALID_POINTER);
    AssertPtrReturn(pOpenParms, VERR_INVALID_POINTER);
    AssertPtrReturn(phList,     VERR_INVALID_POINTER);

    if (pTransfer->cListHandles == pTransfer->cMaxListHandles)
        return VERR_SHCLPB_MAX_LISTS_REACHED;

    int rc;
    if (pTransfer->ProviderIface.pfnListOpen)
        rc = pTransfer->ProviderIface.pfnListOpen(&pTransfer->ProviderCtx, pOpenParms, phList);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
         LogRel(("Shared Clipboard: Opening list '%s' (fiter '%s', flags %#x) failed with %Rrc\n",
                 pOpenParms->pszPath, pOpenParms->pszFilter, pOpenParms->fList, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Closes a transfer list.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to handle.
 * @param   hList               Handle of list to close.
 */
int ShClTransferListClose(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    if (hList == NIL_SHCLLISTHANDLE)
        return VINF_SUCCESS;

    int rc;
    if (pTransfer->ProviderIface.pfnListClose)
        rc = pTransfer->ProviderIface.pfnListClose(&pTransfer->ProviderCtx, hList);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Closing list 0x%x entry failed with %Rrc\n", hList, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Retrieves the header of a transfer list.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to handle.
 * @param   hList               Handle of list to get header for.
 * @param   pHdr                Where to store the returned list header information.
 */
int ShClTransferListGetHeader(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList,
                              PSHCLLISTHDR pHdr)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(pHdr,      VERR_INVALID_POINTER);

    LogFlowFunc(("hList=%RU64\n", hList));

    int rc;
    if (pTransfer->ProviderIface.pfnListHdrRead)
        rc = pTransfer->ProviderIface.pfnListHdrRead(&pTransfer->ProviderCtx, hList, pHdr);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Reading list header list 0x%x entry failed with %Rrc\n", hList, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Returns a specific list handle info of a clipboard transfer.
 *
 * @returns Pointer to list handle info if found, or NULL if not found.
 * @param   pTransfer           Clipboard transfer to get list handle info from.
 * @param   hList               List handle of the list to get handle info for.
 */
PSHCLLISTHANDLEINFO ShClTransferListGetByHandle(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList)
{
    PSHCLLISTHANDLEINFO pIt;
    RTListForEach(&pTransfer->lstHandles, pIt, SHCLLISTHANDLEINFO, Node) /** @todo Sloooow ... improve this. */
    {
        if (pIt->hList == hList)
            return pIt;
    }

    return NULL;
}

/**
 * Returns the a transfer object of a transfer list.
 *
 * Currently not implemented and wil return NULL.
 *
 * @returns Pointer to transfer object, or NULL if not found / invalid.
 * @param   pTransfer           Clipboard transfer to return transfer object for.
 * @param   hList               Handle of clipboard transfer list to get object for.
 * @param   uIdx                Index of object to get.
 */
PSHCLTRANSFEROBJ ShClTransferListGetObj(PSHCLTRANSFER pTransfer,
                                        SHCLLISTHANDLE hList, uint64_t uIdx)
{
    AssertPtrReturn(pTransfer, NULL);

    RT_NOREF(hList, uIdx);

    LogFlowFunc(("hList=%RU64\n", hList));

    return NULL;
}

/**
 * Reads a single transfer list entry.
 *
 * @returns VBox status code or VERR_NO_MORE_FILES if the end of the list has been reached.
 * @param   pTransfer           Clipboard transfer to handle.
 * @param   hList               List handle of list to read from.
 * @param   pEntry              Where to store the read information.
 */
int ShClTransferListRead(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList,
                         PSHCLLISTENTRY pEntry)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(pEntry,    VERR_INVALID_POINTER);

    LogFlowFunc(("hList=%RU64\n", hList));

    int rc;
    if (pTransfer->ProviderIface.pfnListEntryRead)
        rc = pTransfer->ProviderIface.pfnListEntryRead(&pTransfer->ProviderCtx, hList, pEntry);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Reading list for list 0x%x entry failed with %Rrc\n", hList, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Writes a single transfer list entry.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to handle.
 * @param   hList               List handle of list to write to.
 * @param   pEntry              Entry information to write.
 */
int ShClTransferListWrite(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList,
                          PSHCLLISTENTRY pEntry)
{
    RT_NOREF(pTransfer, hList, pEntry);

    int rc = VINF_SUCCESS;

#if 0
    if (pTransfer->ProviderIface.pfnListEntryWrite)
        rc = pTransfer->ProviderIface.pfnListEntryWrite(&pTransfer->ProviderCtx, hList, pEntry);
#endif

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Writing list entry to list 0x%x failed with %Rrc\n", hList, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Returns whether a given transfer list handle is valid or not.
 *
 * @returns \c true if list handle is valid, \c false if not.
 * @param   pTransfer           Clipboard transfer to handle.
 * @param   hList               List handle to check.
 */
bool ShClTransferListHandleIsValid(PSHCLTRANSFER pTransfer, SHCLLISTHANDLE hList)
{
    bool fIsValid = false;

    if (pTransfer->State.enmSource == SHCLSOURCE_LOCAL)
    {
        fIsValid = ShClTransferListGetByHandle(pTransfer, hList) != NULL;
    }
    else if (pTransfer->State.enmSource == SHCLSOURCE_REMOTE)
    {
        AssertFailed(); /** @todo Implement. */
    }
    else
        AssertFailedStmt(fIsValid = false);

    return fIsValid;
}

/**
 * Copies a transfer callback table from source to destination.
 *
 * @param   pCallbacksDst       Callback destination.
 * @param   pCallbacksSrc       Callback source. If set to NULL, the
 *                              destination callback table will be unset.
 */
static void shClTransferCopyCallbacks(PSHCLTRANSFERCALLBACKS pCallbacksDst, PSHCLTRANSFERCALLBACKS pCallbacksSrc)
{
    AssertPtrReturnVoid(pCallbacksDst);
    /* pCallbacksSrc can be NULL */

    if (pCallbacksSrc) /* Set */
    {
#define SET_CALLBACK(a_pfnCallback) \
        if (pCallbacksSrc->a_pfnCallback) \
            pCallbacksDst->a_pfnCallback = pCallbacksSrc->a_pfnCallback

        SET_CALLBACK(pfnOnCreated);
        SET_CALLBACK(pfnOnInitialize);
        SET_CALLBACK(pfnOnInitialized);
        SET_CALLBACK(pfnOnDestroy);
        SET_CALLBACK(pfnOnStarted);
        SET_CALLBACK(pfnOnCompleted);
        SET_CALLBACK(pfnOnError);
        SET_CALLBACK(pfnOnRegistered);
        SET_CALLBACK(pfnOnUnregistered);

#undef SET_CALLBACK

        pCallbacksDst->pvUser = pCallbacksSrc->pvUser;
        pCallbacksDst->cbUser = pCallbacksSrc->cbUser;
    }
    else /* Unset */
        RT_BZERO(pCallbacksDst, sizeof(SHCLTRANSFERCALLBACKS));
}

/**
 * Sets or unsets the callback table to be used for a clipboard transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to set callbacks for.
 * @param   pCallbacks          Pointer to callback table to set. If set to NULL,
 *                              existing callbacks for this transfer will be unset.
 *
 * @note    Must come before initializing the transfer via ShClTransferInit().
 */
static void shClTransferSetCallbacks(PSHCLTRANSFER pTransfer, PSHCLTRANSFERCALLBACKS pCallbacks)
{
    AssertPtrReturnVoid(pTransfer);
    /* pCallbacks can be NULL. */

    shClTransferCopyCallbacks(&pTransfer->Callbacks, pCallbacks);

    /* Make sure that the callback context has all values set according to the callback table.
     * This only needs to be done once, so do this here. */
    pTransfer->CallbackCtx.pTransfer = pTransfer;
    pTransfer->CallbackCtx.pvUser    = pTransfer->Callbacks.pvUser;
    pTransfer->CallbackCtx.cbUser    = pTransfer->Callbacks.cbUser;
}

/**
 * Sets the transfer provider for a given transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer to create transfer provider for.
 * @param   pProvider           Provider to use.
 */
int ShClTransferSetProvider(PSHCLTRANSFER pTransfer, PSHCLTXPROVIDER pProvider)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(pProvider, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    int rc = VINF_SUCCESS;

    pTransfer->ProviderIface         = pProvider->Interface;
    pTransfer->ProviderCtx.pTransfer = pTransfer;
    pTransfer->ProviderCtx.pvUser    = pProvider->pvUser;
    pTransfer->ProviderCtx.cbUser    = pProvider->cbUser;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sets the current status.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to set status for.
 * @param   enmStatus           Status to set.
 *
 * @note    Caller needs to take critical section.
 */
static int shClTransferSetStatus(PSHCLTRANSFER pTransfer, SHCLTRANSFERSTATUS enmStatus)
{
    Assert(RTCritSectIsOwner(&pTransfer->CritSect));
#if 0
    AssertMsgReturn(pTransfer->State.enmStatus != enmStatus,
                    ("Setting the same status twice in a row (%#x), please report this!\n", enmStatus), VERR_WRONG_ORDER);
#endif
    pTransfer->State.enmStatus = enmStatus;

    LogFlowFunc(("enmStatus=%s\n", ShClTransferStatusToStr(pTransfer->State.enmStatus)));

    return RTSemEventSignal(pTransfer->StatusChangeEvent);
}

/**
 * Returns the number of transfer root list entries.
 *
 * @returns Root list entry count.
 * @param   pTransfer           Clipboard transfer to return root entry count for.
 */
uint64_t ShClTransferRootsCount(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, 0);

    shClTransferLock(pTransfer);

    uint32_t const cRoots = pTransfer->lstRoots.Hdr.cEntries;

    shClTransferUnlock(pTransfer);

    return cRoots;
}

/**
 * Resets the root list of a clipboard transfer.
 *
 * @param   pTransfer           Transfer to clear transfer root list for.
 *
 * @note    Caller needs to take critical section.
 */
static void shClTransferRootsReset(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturnVoid(pTransfer);
    Assert(RTCritSectIsOwner(&pTransfer->CritSect));

    if (pTransfer->pszPathRootAbs)
    {
        RTStrFree(pTransfer->pszPathRootAbs);
        pTransfer->pszPathRootAbs = NULL;
    }

    ShClTransferListDestroy(&pTransfer->lstRoots);
}

/**
 * Resets a clipboard transfer.
 *
 * @param   pTransfer           Clipboard transfer to reset.
 */
void ShClTransferReset(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturnVoid(pTransfer);

    LogFlowFuncEnter();

    shClTransferLock(pTransfer);

    shClTransferRootsReset(pTransfer);

    PSHCLLISTHANDLEINFO pItList, pItListNext;
    RTListForEachSafe(&pTransfer->lstHandles, pItList, pItListNext, SHCLLISTHANDLEINFO, Node)
    {
        ShClTransferListHandleInfoDestroy(pItList);

        RTListNodeRemove(&pItList->Node);

        RTMemFree(pItList);
    }

    PSHCLTRANSFEROBJ pItObj, pItObjNext;
    RTListForEachSafe(&pTransfer->lstObj, pItObj, pItObjNext, SHCLTRANSFEROBJ, Node)
    {
        ShClTransferObjDestroy(pItObj);

        RTListNodeRemove(&pItObj->Node);

        RTMemFree(pItObj);
    }

    shClTransferUnlock(pTransfer);
}

/**
 * Get a specific root list entry.
 *
 * @returns Const pointer to root list entry if found, or NULL if not found..
 * @param   pTransfer           Clipboard transfer to get root list entry of.
 * @param   uIndex              Index (zero-based) of entry to get.
 */
PCSHCLLISTENTRY ShClTransferRootsEntryGet(PSHCLTRANSFER pTransfer, uint64_t uIndex)
{
    AssertPtrReturn(pTransfer, NULL);

    shClTransferLock(pTransfer);

    if (uIndex >= pTransfer->lstRoots.Hdr.cEntries)
    {
        shClTransferUnlock(pTransfer);
        return NULL;
    }

    PCSHCLLISTENTRY pEntry = shClTransferListGetEntryById(&pTransfer->lstRoots, uIndex);

    shClTransferUnlock(pTransfer);

    return pEntry;
}

/**
 * Reads the root entries of a clipboard transfer.
 *
 * This gives the provider interface the chance of reading root entries information.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to read root list for.
 */
int ShClTransferRootListRead(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    int rc;
    if (pTransfer->ProviderIface.pfnRootListRead)
        rc = pTransfer->ProviderIface.pfnRootListRead(&pTransfer->ProviderCtx);
    else
        rc = VERR_NOT_SUPPORTED;

    shClTransferLock(pTransfer);

    /* Make sure that we have at least an empty root path set. */
    if (   RT_SUCCESS(rc)
        && !pTransfer->pszPathRootAbs)
    {
        if (RTStrAPrintf(&pTransfer->pszPathRootAbs, "") < 0)
            rc = VERR_NO_MEMORY;
    }

    shClTransferUnlock(pTransfer);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Set the root list entries for a given clipboard transfer, extended version.
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer to set transfer list entries for.
 * @param   pszRoots            String list (separated by \a pszSep) of root entries to set.
 *                              All entries must have the same root path.
 * @param   cbRoots             Size (in bytes) of string list. Includes zero terminator.
 * @param   pszSep              String separator to use for splitting up the root entries.
 *
 * @note    Accepts local paths or URI string lists (absolute only).
 */
int ShClTransferRootsSetFromStringListEx(PSHCLTRANSFER pTransfer, const char *pszRoots, size_t cbRoots, const char *pszSep)
{
    AssertPtrReturn(pTransfer,      VERR_INVALID_POINTER);
    AssertPtrReturn(pszRoots,       VERR_INVALID_POINTER);
    AssertReturn(cbRoots,           VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszSep,         VERR_INVALID_POINTER);

#ifdef DEBUG_andy
    LogFlowFunc(("Data:\n%.*Rhxd\n", cbRoots, pszRoots));
#endif

    if (!RTStrIsValidEncoding(pszRoots))
        return VERR_INVALID_UTF8_ENCODING;

    int rc = VINF_SUCCESS;

    shClTransferLock(pTransfer);

    shClTransferRootsReset(pTransfer);

    PSHCLLIST pLstRoots      = &pTransfer->lstRoots;
    char     *pszPathRootAbs = NULL;
    size_t    cchPathRootAbs = 0;

    RTCList<RTCString> lstRootEntries = RTCString(pszRoots, cbRoots).split(pszSep);
    if (!lstRootEntries.size())
    {
        shClTransferUnlock(pTransfer);
        return VINF_SUCCESS;
    }

    for (size_t i = 0; i < lstRootEntries.size(); ++i)
    {
        char *pszPathCur = NULL;

        char *pszPath = NULL;
        rc = RTUriFilePathEx(lstRootEntries.at(i).c_str(),
                             RTPATH_STR_F_STYLE_UNIX, &pszPath, 0 /*cbPath*/, NULL /*pcchPath*/);
        if (RT_SUCCESS(rc))
        {
            pszPathCur = pszPath;
            pszPath    = NULL; /* pszPath has ownership now. */
        }
        else if (rc == VERR_URI_NOT_FILE_SCHEME) /* Local file path? */
        {
            pszPathCur = RTStrDup(lstRootEntries.at(i).c_str());
            rc = VINF_SUCCESS;
        }

        LogFlowFunc(("pszPathCur=%s\n", pszPathCur));

        rc = ShClTransferValidatePath(pszPathCur, false /* fMustExist */);
        if (RT_FAILURE(rc))
        {
            RTStrFree(pszPathCur);
            break;
        }

        /* No root path determined yet? */
        if (!pszPathRootAbs)
        {
            pszPathRootAbs = RTStrDup(pszPathCur);
            if (pszPathRootAbs)
            {
                RTPathStripFilename(pszPathRootAbs);

                LogFlowFunc(("pszPathRootAbs=%s\n", pszPathRootAbs));

                /* We don't want to have a relative directory here. */
                if (RTPathStartsWithRoot(pszPathRootAbs))
                {
                    cchPathRootAbs = RTStrNLen(pszPathRootAbs, RTPATH_MAX);
                    LogRel2(("Shared Clipboard: Transfer uses root '%s'\n", pszPathRootAbs));
                }
                else
                    rc = VERR_PATH_IS_RELATIVE;
            }
            else
                rc = VERR_NO_MEMORY;
        }

        if (RT_SUCCESS(rc))
        {
            PSHCLLISTENTRY pEntry;
            rc = ShClTransferListEntryAlloc(&pEntry);
            if (RT_SUCCESS(rc))
            {
                PSHCLFSOBJINFO pFsObjInfo = (PSHCLFSOBJINFO)RTMemAllocZ(sizeof(SHCLFSOBJINFO));
                if (pFsObjInfo)
                {
                    if (pTransfer->State.enmDir == SHCLTRANSFERDIR_TO_REMOTE)
                        rc = ShClFsObjInfoQueryLocal(pszPathCur, pFsObjInfo);
                    if (RT_SUCCESS(rc))
                    {
                        /* Calculate the relative path within the root path. */
                        Assert(RTStrNLen(pszPathCur, RTPATH_MAX) >= cchPathRootAbs); /* Sanity. */
                        const char *pszPathRelToRoot = pszPathCur + cchPathRootAbs + 1 /* Skip slash */;
                        if (    pszPathRelToRoot
                            && *pszPathRelToRoot != '\0')
                        {
                            LogRel2(("Shared Clipboard: Adding list entry '%s'\n", pszPathRelToRoot));

                            rc = ShClTransferListEntryInitEx(pEntry, VBOX_SHCL_INFO_F_FSOBJINFO, pszPathRelToRoot,
                                                             pFsObjInfo, sizeof(SHCLFSOBJINFO));
                            if (RT_SUCCESS(rc))
                            {
                                rc = ShClTransferListAddEntry(pLstRoots, pEntry, true /* fAppend */);
                                if (RT_SUCCESS(rc))
                                    pFsObjInfo = NULL; /* pEntry has ownership now. */
                            }
                        }
                        else
                        {
                            LogRel(("Shared Clipboard: Unable to construct relative path for '%s' (root is '%s')\n",
                                    pszPathCur, pszPathRootAbs));
                            rc = VERR_PATH_DOES_NOT_START_WITH_ROOT;
                        }
                    }

                    if (pFsObjInfo)
                    {
                        RTMemFree(pFsObjInfo);
                        pFsObjInfo = NULL;
                    }
                }
                else
                    rc = VERR_NO_MEMORY;

                if (RT_FAILURE(rc))
                    ShClTransferListEntryFree(pEntry);
            }
        }

        RTStrFree(pszPathCur);
    }

    /* No (valid) root directory found? Bail out early. */
    if (   RT_SUCCESS(rc)
        && !pszPathRootAbs)
        rc = VERR_PATH_DOES_NOT_START_WITH_ROOT;

    if (RT_SUCCESS(rc))
    {
        pTransfer->pszPathRootAbs = pszPathRootAbs;
        LogFlowFunc(("pszPathRootAbs=%s, cRoots=%zu\n", pTransfer->pszPathRootAbs, pTransfer->lstRoots.Hdr.cEntries));
    }
    else
    {
        LogRel(("Shared Clipboard: Unable to set roots for transfer, rc=%Rrc\n", rc));
        ShClTransferListDestroy(pLstRoots);
        RTStrFree(pszPathRootAbs);
    }

    shClTransferUnlock(pTransfer);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sets the root list entries for a given clipboard transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer to set transfer list entries for.
 * @param   pszRoots            String list (separated by SHCL_TRANSFER_URI_LIST_SEP_STR) of root entries to set.
 *                              All entries must have the same root path.
 * @param   cbRoots             Size (in bytes) of string list. Includes zero terminator.
 *
 * @note    Accepts local paths or URI string lists (absolute only).
 */
int ShClTransferRootsSetFromStringList(PSHCLTRANSFER pTransfer, const char *pszRoots, size_t cbRoots)
{
    return ShClTransferRootsSetFromStringListEx(pTransfer, pszRoots, cbRoots, SHCL_TRANSFER_URI_LIST_SEP_STR);
}

/**
 * Sets the root list entries for a given clipboard transfer, UTF-16 (Unicode) version.
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer to set transfer list entries for.
 * @param   pwszRoots           Unicode string list (separated by CRLF) of root entries to set.
 *                              All entries must have the same root path.
 * @param   cbRoots             Size (in bytes) of string list. Includes zero terminator.
 *
 * @note    Accepts local paths or URI string lists (absolute only).
 */
int ShClTransferRootsSetFromStringListUnicode(PSHCLTRANSFER pTransfer, PRTUTF16 pwszRoots, size_t cbRoots)
{
    AssertPtrReturn(pwszRoots, VERR_INVALID_POINTER);
    AssertReturn(cbRoots, VERR_INVALID_PARAMETER);
    AssertReturn(cbRoots % sizeof(RTUTF16) == 0, VERR_INVALID_PARAMETER);

    size_t cwcRoots = cbRoots / sizeof(RTUTF16);

    /* This may slightly overestimate the space needed. */
    size_t chDst = 0;
    int rc = ShClUtf16LenUtf8(pwszRoots, cwcRoots, &chDst);
    if (RT_SUCCESS(rc))
    {
        chDst++; /* Add space for terminator. */

        char *pszDst = (char *)RTStrAlloc(chDst);
        if (pszDst)
        {
            size_t cbActual = 0;
            rc = ShClConvUtf16CRLFToUtf8LF(pwszRoots, cwcRoots, pszDst, chDst, &cbActual);
            if (RT_SUCCESS(rc))
                rc = ShClTransferRootsSetFromStringList(pTransfer, pszDst, cbActual + 1 /* Include terminator */);

            RTStrFree(pszDst);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}

/**
 * Sets a single path as a transfer root.
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer to set transfer list entries for.
 * @param   pszPath             Path to use as transfer root. Can be a single file or a directory.
 *
 * @note    Convenience function, uses ShClTransferRootsInitFromStringList() internally.
 */
int ShClTransferRootsSetFromPath(PSHCLTRANSFER pTransfer, const char *pszPath)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);

    char *pszRoots = NULL;
    int rc = RTStrAAppend(&pszRoots, pszPath);
    AssertRCReturn(rc, rc);
    rc = RTStrAAppend(&pszRoots, SHCL_TRANSFER_URI_LIST_SEP_STR);
    AssertRCReturn(rc, rc);
    rc =  ShClTransferRootsSetFromStringList(pTransfer, pszRoots, strlen(pszRoots) + 1 /* Include terminator */);
    RTStrFree(pszRoots);
    return rc;
}

/**
 * Returns the clipboard transfer's ID.
 *
 * @returns The transfer's ID.
 * @param   pTransfer           Clipboard transfer to return ID for.
 */
SHCLTRANSFERID ShClTransferGetID(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, 0);

    shClTransferLock(pTransfer);

    SHCLTRANSFERID const uID = pTransfer->State.uID;

    shClTransferUnlock(pTransfer);

    return uID;
}

/**
 * Returns the clipboard transfer's direction.
 *
 * @returns The transfer's direction.
 * @param   pTransfer           Clipboard transfer to return direction for.
 */
SHCLTRANSFERDIR ShClTransferGetDir(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, SHCLTRANSFERDIR_UNKNOWN);

    shClTransferLock(pTransfer);

    SHCLTRANSFERDIR const enmDir = pTransfer->State.enmDir;

    shClTransferUnlock(pTransfer);

    return enmDir;
}

/**
 * Returns the absolute root path of a transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to return absolute root path for.
 * @param   pszPath             Where to store the returned path.
 * @param   cbPath              Size (in bytes) of  \a pszPath.
 */
int ShClTransferGetRootPathAbs(PSHCLTRANSFER pTransfer, char *pszPath, size_t cbPath)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    shClTransferLock(pTransfer);

    AssertMsgReturn(pTransfer->pszPathRootAbs, ("Transfer has no root path set (yet)\n"), VERR_WRONG_ORDER);

    int const rc = RTStrCopy(pszPath, cbPath, pTransfer->pszPathRootAbs);

    shClTransferUnlock(pTransfer);

    return rc;
}

/**
 * Returns the transfer's source.
 *
 * @returns The transfer's source.
 * @param   pTransfer           Clipboard transfer to return source for.
 */
SHCLSOURCE ShClTransferGetSource(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, SHCLSOURCE_INVALID);

    shClTransferLock(pTransfer);

    SHCLSOURCE const enmSource = pTransfer->State.enmSource;

    shClTransferUnlock(pTransfer);

    return enmSource;
}

/**
 * Returns the current transfer status.
 *
 * @returns Current transfer status.
 * @param   pTransfer           Clipboard transfer to return status for.
 *
 * @note    Caller needs to take critical section.
 */
DECLINLINE(SHCLTRANSFERSTATUS) shClTransferGetStatusLocked(PSHCLTRANSFER pTransfer)
{
    Assert(RTCritSectIsOwner(&pTransfer->CritSect));

    shClTransferLock(pTransfer);

    SHCLTRANSFERSTATUS const enmStatus = pTransfer->State.enmStatus;

    shClTransferUnlock(pTransfer);

    return enmStatus;
}

/**
 * Returns the current transfer status.
 *
 * @returns Current transfer status.
 * @param   pTransfer           Clipboard transfer to return status for.
 */
SHCLTRANSFERSTATUS ShClTransferGetStatus(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, SHCLTRANSFERSTATUS_NONE);

    shClTransferLock(pTransfer);

    SHCLTRANSFERSTATUS const enmSts = shClTransferGetStatusLocked(pTransfer);

    shClTransferUnlock(pTransfer);

    return enmSts;
}

/**
 * Runs a started clipboard transfer in a dedicated thread.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to run.
 * @param   pfnThreadFunc       Pointer to thread function to use.
 * @param   pvUser              Pointer to user-provided data. Optional.
 */
int ShClTransferRun(PSHCLTRANSFER pTransfer, PFNSHCLTRANSFERTHREAD pfnThreadFunc, void *pvUser)
{
    AssertPtrReturn(pTransfer,     VERR_INVALID_POINTER);
    AssertPtrReturn(pfnThreadFunc, VERR_INVALID_POINTER);
    /* pvUser is optional. */

    AssertMsgReturn(pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_STARTED,
                    ("Wrong status (currently is %s)\n", ShClTransferStatusToStr(pTransfer->State.enmStatus)),
                    VERR_WRONG_ORDER);

    int rc = shClTransferThreadCreate(pTransfer, pfnThreadFunc, pvUser);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Starts an initialized transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to start.
 */
int ShClTransferStart(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    shClTransferLock(pTransfer);

    /* Ready to start? */
    AssertMsgReturnStmt(pTransfer->ProviderIface.pfnRootListRead != NULL,
                        ("No provider interface set (yet)\n"),
                        shClTransferUnlock(pTransfer), VERR_WRONG_ORDER);
    AssertMsgReturnStmt(pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_INITIALIZED,
                        ("Wrong status (currently is %s)\n", ShClTransferStatusToStr(pTransfer->State.enmStatus)),
                        shClTransferUnlock(pTransfer), VERR_WRONG_ORDER);

    int rc = shClTransferSetStatus(pTransfer, SHCLTRANSFERSTATUS_STARTED);

    shClTransferUnlock(pTransfer);

    if (pTransfer->Callbacks.pfnOnStarted)
        pTransfer->Callbacks.pfnOnStarted(&pTransfer->CallbackCtx);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Stops a started transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to stop.
 */
int ShClTransferStop(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    int rc = shClTransferThreadDestroy(pTransfer, SHCL_TIMEOUT_DEFAULT_MS);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Completes a transfer (as successful).
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to complete.
 */
int ShClTransferComplete(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    shClTransferLock(pTransfer);

    AssertMsgReturnStmt(   pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_INITIALIZED
                        || pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_STARTED,
                        ("Wrong status (currently is %s)\n", ShClTransferStatusToStr(pTransfer->State.enmStatus)),
                        shClTransferUnlock(pTransfer), VERR_WRONG_ORDER);

    int rc = shClTransferSetStatus(pTransfer, SHCLTRANSFERSTATUS_COMPLETED);

    shClTransferUnlock(pTransfer);

    if (pTransfer->Callbacks.pfnOnCompleted)
        pTransfer->Callbacks.pfnOnCompleted(&pTransfer->CallbackCtx, rc);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Cancels or sets an error for a transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to cancel or set error for.
 * @param   rc                  Error code to set.
 *                              If set to VERR_CANCELLED, the transfer will be canceled.
 */
static int shClTransferCancelOrError(PSHCLTRANSFER pTransfer, int rc)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    LogFlowFunc(("%Rrc\n", rc));

    shClTransferLock(pTransfer);

    int rc2;

    if (   pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_INITIALIZED
        || pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_STARTED)
    {
        if (rc == VERR_CANCELLED)
        {
            rc2 = shClTransferSetStatus(pTransfer, SHCLTRANSFERSTATUS_CANCELED);

            if (pTransfer->Callbacks.pfnOnCompleted)
                pTransfer->Callbacks.pfnOnCompleted(&pTransfer->CallbackCtx, VERR_CANCELLED);
        }
        else
        {
            rc2 = shClTransferSetStatus(pTransfer, SHCLTRANSFERSTATUS_ERROR);

            if (pTransfer->Callbacks.pfnOnError)
                pTransfer->Callbacks.pfnOnError(&pTransfer->CallbackCtx, rc);
        }
    }
    else /* Nothing to do. */
        rc2 = VINF_SUCCESS;

    shClTransferUnlock(pTransfer);

    LogFlowFuncLeaveRC(rc2);
    return rc2;
}

/**
 * Cancels a transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to cancel.
 */
int ShClTransferCancel(PSHCLTRANSFER pTransfer)
{
    return shClTransferCancelOrError(pTransfer, VERR_CANCELLED);
}

/**
 * Kills a transfer.
 *
 * Currently cancels it internally.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to kill.
 */
int ShClTransferKill(PSHCLTRANSFER pTransfer)
{
    return ShClTransferCancel(pTransfer);
}

/**
 * Sets an error for a transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to set error for.
 * @param   rc                  Error code to set.
 */
int ShClTransferError(PSHCLTRANSFER pTransfer, int rc)
{
    return shClTransferCancelOrError(pTransfer, rc);
}

/**
 * Internal struct for keeping a transfer thread context.
 */
typedef struct _SHCLTRANSFERTHREADCTX
{
    /** Pointer to transfer. */
    PSHCLTRANSFER         pTransfer;
    /** User-supplied context data. Can be NULL if not being used. */
    void                 *pvUser;
    /** Pointer to thread function to use. */
    PFNSHCLTRANSFERTHREAD pfnThread;
} SHCLTRANSFERTHREADCTX;
/** Pointer to internal struct for keeping a transfer thread context. */
typedef SHCLTRANSFERTHREADCTX *PSHCLTRANSFERTHREADCTX;

/**
 * Worker thread for a transfer.
 *
 * @returns VBox status code.
 * @param   ThreadSelf          Thread self handle. Not being used.
 * @param   pvUser              Context data of type PSHCLTRANSFERTHREADCTX.
 */
static DECLCALLBACK(int) shClTransferThreadWorker(RTTHREAD ThreadSelf, void *pvUser)
{
    RT_NOREF(ThreadSelf);

    LogFlowFuncEnter();

    SHCLTRANSFERTHREADCTX Ctx;
    memcpy(&Ctx, pvUser, sizeof(SHCLTRANSFERTHREADCTX));

    LogFlowFunc(("pfnThread=%p, pTransfer=%p, pvUser=%p\n", Ctx.pfnThread, Ctx.pTransfer, Ctx.pvUser));

    PSHCLTRANSFER pTransfer = Ctx.pTransfer;

    shClTransferLock(pTransfer);

    pTransfer->Thread.fStarted = true;
    pTransfer->Thread.fStop    = false;

    shClTransferUnlock(pTransfer);

    RTThreadUserSignal(RTThreadSelf());

    int rc = Ctx.pfnThread(pTransfer, Ctx.pvUser);

    if (pTransfer->Callbacks.pfnOnCompleted)
        pTransfer->Callbacks.pfnOnCompleted(&pTransfer->CallbackCtx, rc);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates a thread for a clipboard transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to create thread for.
 * @param   pfnThreadFunc       Thread function to use for this transfer.
 * @param   pvUser              Pointer to user-provided data.
 */
static int shClTransferThreadCreate(PSHCLTRANSFER pTransfer, PFNSHCLTRANSFERTHREAD pfnThreadFunc, void *pvUser)

{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    shClTransferLock(pTransfer);

    /* Already marked for stopping? */
    AssertMsgReturn(pTransfer->Thread.fStop == false,
                    ("Transfer thread already marked for stopping"), VERR_WRONG_ORDER);
    /* Already started? */
    AssertMsgReturn(pTransfer->Thread.fStarted == false,
                    ("Transfer thread already started"), VERR_WRONG_ORDER);

    SHCLTRANSFERTHREADCTX Ctx = { pTransfer, pvUser, pfnThreadFunc };

    /* Spawn a worker thread, so that we don't block the window thread for too long. */
    int rc = RTThreadCreate(&pTransfer->Thread.hThread, shClTransferThreadWorker,
                            &Ctx, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE,
                            "shcltx");
    if (RT_SUCCESS(rc))
    {
        shClTransferUnlock(pTransfer); /* Leave lock while waiting. */

        int rc2 = RTThreadUserWait(pTransfer->Thread.hThread, SHCL_TIMEOUT_DEFAULT_MS);
        AssertRC(rc2);

        shClTransferLock(pTransfer);

        if (pTransfer->Thread.fStarted) /* Did the thread indicate that it started correctly? */
        {
            /* Nothing to do in here. */
        }
        else
            rc = VERR_GENERAL_FAILURE; /** @todo Find a better rc. */
    }

    shClTransferUnlock(pTransfer);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Destroys the thread of a clipboard transfer.
 *
 * @returns VBox status code. Will return thread rc.
 * @param   pTransfer           Clipboard transfer to destroy thread for.
 * @param   uTimeoutMs          Timeout (in ms) to wait for thread destruction.
 */
static int shClTransferThreadDestroy(PSHCLTRANSFER pTransfer, RTMSINTERVAL uTimeoutMs)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    shClTransferLock(pTransfer);

    if (!pTransfer->Thread.fStarted)
    {
        shClTransferUnlock(pTransfer);
        return VINF_SUCCESS;
    }

    LogFlowFuncEnter();

    /* Set stop indicator. */
    pTransfer->Thread.fStop = true;

    shClTransferUnlock(pTransfer); /* Leave lock while waiting. */

    int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
    Assert(pTransfer->Thread.hThread != NIL_RTTHREAD);
    int rc = RTThreadWait(pTransfer->Thread.hThread, uTimeoutMs, &rcThread);

    LogFlowFunc(("Waiting for thread resulted in %Rrc (thread exited with %Rrc)\n", rc, rcThread));

    if (RT_SUCCESS(rc))
    {
        pTransfer->Thread.fStarted = false;
        pTransfer->Thread.hThread  = NIL_RTTHREAD;

        rc = rcThread; /* Return the thread rc to the caller. */
    }
    else
        LogRel(("Shared Clipboard: Waiting for thread of transfer %RU16 failed with %Rrc\n", pTransfer->State.uID, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Waits for the transfer status to change, internal version.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to wait for.
 * @param   msTimeout           Timeout (in ms) to wait.
 * @param   penmStatus          Where to return the new (current) transfer status on success.
 *                              Optional and can be NULL.
 */
static int shClTransferWaitForStatusChangeInternal(PSHCLTRANSFER pTransfer, RTMSINTERVAL msTimeout, SHCLTRANSFERSTATUS *penmStatus)
{
    LogFlowFunc(("Waiting for status change (%RU32 timeout) ...\n", msTimeout));

    int rc = RTSemEventWait(pTransfer->StatusChangeEvent, msTimeout);
    if (RT_SUCCESS(rc))
    {
        if (penmStatus)
        {
            shClTransferLock(pTransfer);

            *penmStatus = pTransfer->State.enmStatus;

            shClTransferUnlock(pTransfer);
        }
    }

    return rc;
}

/**
 * Waits for the transfer status to change.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to wait for.
 * @param   msTimeout           Timeout (in ms) to wait.
 * @param   penmStatus          Where to return the new (current) transfer status on success.
 *                              Optional and can be NULL.
 */
int ShClTransferWaitForStatusChange(PSHCLTRANSFER pTransfer, RTMSINTERVAL msTimeout, SHCLTRANSFERSTATUS *penmStatus)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    int rc = shClTransferWaitForStatusChangeInternal(pTransfer, msTimeout, penmStatus);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Waits for a specific transfer status.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to wait for.
 * @param   msTimeout           Timeout (in ms) to wait.
 * @param   enmStatus           Transfer status to wait for.
 */
int ShClTransferWaitForStatus(PSHCLTRANSFER pTransfer, RTMSINTERVAL msTimeout, SHCLTRANSFERSTATUS enmStatus)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    uint64_t const tsStartMs = RTTimeMilliTS();
    uint64_t       msLeft    = msTimeout;
    for (;;)
    {
        SHCLTRANSFERSTATUS enmCurStatus;
        rc = shClTransferWaitForStatusChangeInternal(pTransfer, msLeft, &enmCurStatus);
        if (RT_FAILURE(rc))
            break;

        if (enmCurStatus == enmStatus)
            break;

        msLeft -= RT_MIN(msLeft, RTTimeMilliTS() - tsStartMs);
        if (msLeft == 0)
        {
            rc = VERR_TIMEOUT;
            break;
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/*********************************************************************************************************************************
 * Transfer Context                                                                                                              *
 ********************************************************************************************************************************/

/**
 * Locks a transfer context.
 *
 * @param   pTransferCtx        Transfer context to lock.
 */
DECLINLINE(void) shClTransferCtxLock(PSHCLTRANSFERCTX pTransferCtx)
{
    int rc2 = RTCritSectEnter(&pTransferCtx->CritSect);
    AssertRC(rc2);
}

/**
 * Unlocks a transfer context.
 *
 * @param   pTransferCtx        Transfer context to unlock.
 */
DECLINLINE(void) shClTransferCtxUnlock(PSHCLTRANSFERCTX pTransferCtx)
{
    int rc2 = RTCritSectLeave(&pTransferCtx->CritSect);
    AssertRC(rc2);
}

/**
 * Initializes a clipboard transfer context.
 *
 * @returns VBox status code.
 * @param   pTransferCtx                Transfer context to initialize.
 */
int ShClTransferCtxInit(PSHCLTRANSFERCTX pTransferCtx)
{
    AssertPtrReturn(pTransferCtx, VERR_INVALID_POINTER);

    LogFlowFunc(("pTransferCtx=%p\n", pTransferCtx));

    int rc = RTCritSectInit(&pTransferCtx->CritSect);
    if (RT_SUCCESS(rc))
    {
        rc = RTSemEventCreate(&pTransferCtx->ChangedEvent);
        if (RT_SUCCESS(rc))
        {
            RT_ZERO(pTransferCtx->ChangedEventData);

            RTListInit(&pTransferCtx->List);

            pTransferCtx->cTransfers  = 0;
            pTransferCtx->cRunning    = 0;
            pTransferCtx->cMaxRunning = 64; /** @todo Make this configurable? */

            RT_ZERO(pTransferCtx->bmTransferIds);

            ShClTransferCtxReset(pTransferCtx);
        }
    }

    return VINF_SUCCESS;
}

/**
 * Destroys a clipboard transfer context.
 *
 * @param   pTransferCtx                Transfer context to destroy.
 */
void ShClTransferCtxDestroy(PSHCLTRANSFERCTX pTransferCtx)
{
    if (!pTransferCtx)
        return;

    LogFlowFunc(("pTransferCtx=%p\n", pTransferCtx));

    shClTransferCtxLock(pTransferCtx);

    PSHCLTRANSFER pTransfer, pTransferNext;
    RTListForEachSafe(&pTransferCtx->List, pTransfer, pTransferNext, SHCLTRANSFER, Node)
    {
        shclTransferCtxTransferRemoveAndUnregister(pTransferCtx, pTransfer);
        ShClTransferDestroy(pTransfer);
    }

    pTransferCtx->cRunning   = 0;
    pTransferCtx->cTransfers = 0;

    shClTransferCtxUnlock(pTransferCtx);

    RTSemEventDestroy(pTransferCtx->ChangedEvent);
    pTransferCtx->ChangedEvent = NIL_RTSEMEVENT;

    if (RTCritSectIsInitialized(&pTransferCtx->CritSect))
        RTCritSectDelete(&pTransferCtx->CritSect);
}

/**
 * Resets a clipboard transfer context.
 *
 * @param   pTransferCtx                Transfer context to reset.
 */
void ShClTransferCtxReset(PSHCLTRANSFERCTX pTransferCtx)
{
    AssertPtrReturnVoid(pTransferCtx);

    shClTransferCtxLock(pTransferCtx);

    LogFlowFuncEnter();

    PSHCLTRANSFER pTransfer;
    RTListForEach(&pTransferCtx->List, pTransfer, SHCLTRANSFER, Node)
        ShClTransferReset(pTransfer);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    /** @todo Anything to do here? */
#endif

    shClTransferCtxUnlock(pTransferCtx);
}

/**
 * Signals a change event.
 *
 * @returns VBox status code.
 * @param   pTransferCtx        Transfer context to return transfer for.
 * @param   fRegistered         Whether a transfer got registered or unregistered.
 * @param   pTransfer           Transfer bound to the event.
 */
static int shClTransferCtxSignal(PSHCLTRANSFERCTX pTransferCtx, bool fRegistered, PSHCLTRANSFER pTransfer)
{
    Assert(RTCritSectIsOwner(&pTransferCtx->CritSect));

    LogFlowFunc(("fRegistered=%RTbool, pTransfer=%p\n", fRegistered, pTransfer));

    pTransferCtx->ChangedEventData.fRegistered = fRegistered;
    pTransferCtx->ChangedEventData.pTransfer   = pTransfer;

    return RTSemEventSignal(pTransferCtx->ChangedEvent);
}

/**
 * Returns a specific clipboard transfer, internal version.
 *
 * @returns Clipboard transfer found, or NULL if not found.
 * @param   pTransferCtx                Transfer context to return transfer for.
 * @param   idTransfer                  ID of the transfer to return.
 *
 * @note    Caller needs to take critical section.
 */
static PSHCLTRANSFER shClTransferCtxGetTransferByIdInternal(PSHCLTRANSFERCTX pTransferCtx, SHCLTRANSFERID idTransfer)
{
    Assert(RTCritSectIsOwner(&pTransferCtx->CritSect));

    PSHCLTRANSFER pTransfer;
    RTListForEach(&pTransferCtx->List, pTransfer, SHCLTRANSFER, Node) /** @todo Slow, but works for now. */
    {
        if (pTransfer->State.uID == idTransfer)
            return pTransfer;
    }

    return NULL;
}

/**
 * Returns a specific clipboard transfer by index, internal version.
 *
 * @returns Clipboard transfer found, or NULL if not found.
 * @param   pTransferCtx                Transfer context to return transfer for.
 * @param   uIdx                        Index of the transfer to return.
 *
 * @note    Caller needs to take critical section.
 */
static PSHCLTRANSFER shClTransferCtxGetTransferByIndexInternal(PSHCLTRANSFERCTX pTransferCtx, uint32_t uIdx)
{
    Assert(RTCritSectIsOwner(&pTransferCtx->CritSect));

    uint32_t idx = 0;

    PSHCLTRANSFER pTransfer;
    RTListForEach(&pTransferCtx->List, pTransfer, SHCLTRANSFER, Node) /** @todo Slow, but works for now. */
    {
        if (uIdx == idx)
            return pTransfer;
        idx++;
    }

    return NULL;
}

/**
 * Returns a clipboard transfer for a specific transfer ID.
 *
 * @returns Clipboard transfer found, or NULL if not found.
 * @param   pTransferCtx                Transfer context to return transfer for.
 * @param   uID                         ID of the transfer to return.
 */
PSHCLTRANSFER ShClTransferCtxGetTransferById(PSHCLTRANSFERCTX pTransferCtx, uint32_t uID)
{
    shClTransferCtxLock(pTransferCtx);

    PSHCLTRANSFER const pTransfer = shClTransferCtxGetTransferByIdInternal(pTransferCtx, uID);

    shClTransferCtxUnlock(pTransferCtx);

    return pTransfer;
}

/**
 * Returns a clipboard transfer for a specific list index.
 *
 * @returns Clipboard transfer found, or NULL if not found.
 * @param   pTransferCtx                Transfer context to return transfer for.
 * @param   uIdx                        List index of the transfer to return.
 */
PSHCLTRANSFER ShClTransferCtxGetTransferByIndex(PSHCLTRANSFERCTX pTransferCtx, uint32_t uIdx)
{
    shClTransferCtxLock(pTransferCtx);

    PSHCLTRANSFER const pTransfer = shClTransferCtxGetTransferByIndexInternal(pTransferCtx, uIdx);

    shClTransferCtxUnlock(pTransferCtx);

    return pTransfer;
}


/**
 * Returns the last clipboard transfer registered.
 *
 * @returns Clipboard transfer found, or NULL if not found.
 * @param   pTransferCtx                Transfer context to return transfer for.
 */
PSHCLTRANSFER ShClTransferCtxGetTransferLast(PSHCLTRANSFERCTX pTransferCtx)
{
    shClTransferCtxLock(pTransferCtx);

    PSHCLTRANSFER const pTransfer = RTListGetLast(&pTransferCtx->List, SHCLTRANSFER, Node);

    shClTransferCtxUnlock(pTransferCtx);

    return pTransfer;
}

/**
 * Returns the number of running clipboard transfers for a given transfer context.
 *
 * @returns Number of running transfers.
 * @param   pTransferCtx                Transfer context to return number for.
 */
uint32_t ShClTransferCtxGetRunningTransfers(PSHCLTRANSFERCTX pTransferCtx)
{
    AssertPtrReturn(pTransferCtx, 0);

    shClTransferCtxLock(pTransferCtx);

    uint32_t const cRunning = pTransferCtx->cRunning;

    shClTransferCtxUnlock(pTransferCtx);

    return cRunning;
}

/**
 * Returns the number of total clipboard transfers for a given transfer context.
 *
 * @returns Number of total transfers.
 * @param   pTransferCtx                Transfer context to return number for.
 */
uint32_t ShClTransferCtxGetTotalTransfers(PSHCLTRANSFERCTX pTransferCtx)
{
    AssertPtrReturn(pTransferCtx, 0);

    shClTransferCtxLock(pTransferCtx);

    uint32_t const cTransfers = pTransferCtx->cTransfers;

    shClTransferCtxUnlock(pTransferCtx);

    return cTransfers;
}

static int shClTransferCreateIDInternal(PSHCLTRANSFERCTX pTransferCtx, SHCLTRANSFERID *pidTransfer)
{
    /*
     * Pick a random bit as starting point.  If it's in use, search forward
     * for a free one, wrapping around.  We've reserved both the zero'th and
     * max-1 IDs.
     */
    SHCLTRANSFERID idTransfer = RTRandU32Ex(1, VBOX_SHCL_MAX_TRANSFERS - 2);

    if (!ASMBitTestAndSet(&pTransferCtx->bmTransferIds[0], idTransfer))
    { /* likely */ }
    else if (pTransferCtx->cTransfers < VBOX_SHCL_MAX_TRANSFERS - 2 /* First and last are not used */)
    {
        /* Forward search. */
        int iHit = ASMBitNextClear(&pTransferCtx->bmTransferIds[0], VBOX_SHCL_MAX_TRANSFERS, idTransfer);
        if (iHit < 0)
            iHit = ASMBitFirstClear(&pTransferCtx->bmTransferIds[0], VBOX_SHCL_MAX_TRANSFERS);
        AssertLogRelMsgReturn(iHit >= 0, ("Transfer count: %RU16\n", pTransferCtx->cTransfers), VERR_SHCLPB_MAX_TRANSFERS_REACHED);
        idTransfer = iHit;
        AssertLogRelMsgReturn(!ASMBitTestAndSet(&pTransferCtx->bmTransferIds[0], idTransfer), ("idObject=%#x\n", idTransfer), VERR_INTERNAL_ERROR_2);
    }
    else
    {
        LogFunc(("Maximum number of transfers reached (%RU16 transfers)\n", pTransferCtx->cTransfers));
        return VERR_SHCLPB_MAX_TRANSFERS_REACHED;
    }

    *pidTransfer = idTransfer;

    return VINF_SUCCESS;
}

/**
 * Creates a new transfer ID for a given transfer context.
 *
 * @returns VBox status code.
 * @retval  VERR_SHCLPB_MAX_TRANSFERS_REACHED if the maximum of concurrent transfers is reached.
 * @param   pTransferCtx        Transfer context to create transfer ID for.
 * @param   pidTransfer         Where to return the transfer ID on success.
 */
int ShClTransferCtxCreateId(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFERID pidTransfer)
{
    AssertPtrReturn(pTransferCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pidTransfer, VERR_INVALID_POINTER);

    shClTransferCtxLock(pTransferCtx);

    int rc = shClTransferCreateIDInternal(pTransferCtx, pidTransfer);

    shClTransferCtxUnlock(pTransferCtx);

    return rc;
}

/**
 * Registers a clipboard transfer with a new transfer ID.
 *
 * @return  VBox status code.
 * @retval  VERR_SHCLPB_MAX_TRANSFERS_REACHED if the maximum of concurrent transfers is reached.
 * @param   pTransferCtx        Transfer context to register transfer to.
 * @param   pTransfer           Transfer to register. The context takes ownership of the transfer on success.
 * @param   idTransfer          Transfer ID to use for registering the given transfer.
 */
static int shClTransferCtxTransferRegisterExInternal(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer, SHCLTRANSFERID idTransfer)
{
    AssertPtrReturn(pTransferCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    Assert(idTransfer != NIL_SHCLTRANSFERID);

    shClTransferCtxLock(pTransferCtx);

    pTransfer->State.uID = idTransfer;

    RTListAppend(&pTransferCtx->List, &pTransfer->Node);

    pTransferCtx->cTransfers++;

    Log2Func(("pTransfer=%p, idTransfer=%RU32 -- now %RU16 transfer(s)\n", pTransfer, idTransfer, pTransferCtx->cTransfers));

    shClTransferCtxUnlock(pTransferCtx);

    if (pTransfer->Callbacks.pfnOnRegistered)
        pTransfer->Callbacks.pfnOnRegistered(&pTransfer->CallbackCtx, pTransferCtx);

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}

/**
 * Registers a clipboard transfer with a transfer context, i.e. allocates a transfer ID.
 *
 * @return  VBox status code.
 * @retval  VERR_SHCLPB_MAX_TRANSFERS_REACHED if the maximum of concurrent transfers for this context has been reached.
 * @param   pTransferCtx        Transfer context to register transfer to.
 * @param   pTransfer           Transfer to register. The context takes ownership of the transfer on success.
 * @param   pidTransfer         Where to return the transfer ID on success. Optional.
 */
int ShClTransferCtxRegister(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer, PSHCLTRANSFERID pidTransfer)
{
    AssertPtrReturn(pTransferCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    shClTransferCtxLock(pTransferCtx);

    SHCLTRANSFERID idTransfer;
    int rc = shClTransferCreateIDInternal(pTransferCtx, &idTransfer);
    if (RT_SUCCESS(rc))
    {
        rc = shClTransferCtxTransferRegisterExInternal(pTransferCtx, pTransfer, idTransfer);
        if (RT_SUCCESS(rc))
        {
            if (pidTransfer)
                *pidTransfer = idTransfer;
        }
    }

    shClTransferCtxUnlock(pTransferCtx);

    return rc;
}

/**
 * Registers a clipboard transfer with a transfer context by specifying an ID for the transfer.
 *
 * @return  VBox status code.
 * @retval  VERR_ALREADY_EXISTS if a transfer with the given ID already exists.
 * @retval  VERR_SHCLPB_MAX_TRANSFERS_REACHED if the maximum of concurrent transfers for this context has been reached.
 * @param   pTransferCtx        Transfer context to register transfer to.
 * @param   pTransfer           Transfer to register.
 * @param   idTransfer          Transfer ID to use for registration.
 *
 * @note    This function ASSUMES you have created \a idTransfer with ShClTransferCtxCreateId().
 */
int ShClTransferCtxRegisterById(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer, SHCLTRANSFERID idTransfer)
{
    shClTransferCtxLock(pTransferCtx);

    if (pTransferCtx->cTransfers < VBOX_SHCL_MAX_TRANSFERS - 2 /* First and last are not used */)
    {
        RTListAppend(&pTransferCtx->List, &pTransfer->Node);

        shClTransferLock(pTransfer);

        pTransfer->State.uID = idTransfer;

        shClTransferUnlock(pTransfer);

        int rc = shClTransferCtxSignal(pTransferCtx, true /* fRegistered */, pTransfer);

        pTransferCtx->cTransfers++;

        LogFunc(("Registered transfer ID %RU16 -- now %RU16 transfers total\n", idTransfer, pTransferCtx->cTransfers));

        shClTransferCtxUnlock(pTransferCtx);

        if (pTransfer->Callbacks.pfnOnRegistered)
            pTransfer->Callbacks.pfnOnRegistered(&pTransfer->CallbackCtx, pTransferCtx);

        return rc;
    }

    LogFunc(("Maximum number of transfers reached (%RU16 transfers)\n", pTransferCtx->cTransfers));

    shClTransferCtxUnlock(pTransferCtx);

    return VERR_SHCLPB_MAX_TRANSFERS_REACHED;
}

/**
 * Removes and unregisters a transfer from a transfer context.
 *
 * @param   pTransferCtx        Transfer context to remove transfer from.
 * @param   pTransfer           Transfer to remove.
 *
 * @note    Caller needs to take critical section.
 */
static void shclTransferCtxTransferRemoveAndUnregister(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer)
{
    Assert(RTCritSectIsOwner(&pTransferCtx->CritSect));

    RTListNodeRemove(&pTransfer->Node);

    Assert(pTransferCtx->cTransfers);
    pTransferCtx->cTransfers--;

    Assert(pTransferCtx->cTransfers >= pTransferCtx->cRunning);

    shClTransferCtxUnlock(pTransferCtx);

    if (pTransfer->Callbacks.pfnOnUnregistered)
        pTransfer->Callbacks.pfnOnUnregistered(&pTransfer->CallbackCtx, pTransferCtx);

    shClTransferCtxLock(pTransferCtx);

    LogFlowFunc(("Now %RU32 transfers left\n", pTransferCtx->cTransfers));
}

/**
 * Unregisters a transfer from an transfer context, given by its ID.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_FOUND if the transfer ID was not found.
 * @param   pTransferCtx        Transfer context to unregister transfer from.
 * @param   idTransfer          Transfer ID to unregister.
 */
int ShClTransferCtxUnregisterById(PSHCLTRANSFERCTX pTransferCtx, SHCLTRANSFERID idTransfer)
{
    AssertPtrReturn(pTransferCtx, VERR_INVALID_POINTER);
    AssertReturn(idTransfer, VERR_INVALID_PARAMETER);

    shClTransferCtxLock(pTransferCtx);

    int rc = VINF_SUCCESS;

    LogFlowFunc(("idTransfer=%RU32\n", idTransfer));

    if (ASMBitTestAndClear(&pTransferCtx->bmTransferIds, idTransfer))
    {
        PSHCLTRANSFER pTransfer = shClTransferCtxGetTransferByIdInternal(pTransferCtx, idTransfer);
        if (pTransfer)
        {
            shclTransferCtxTransferRemoveAndUnregister(pTransferCtx, pTransfer);

            rc = shClTransferCtxSignal(pTransferCtx, false /* fRegistered */, pTransfer);
        }
    }
    else
        rc = VERR_NOT_FOUND;

    shClTransferCtxUnlock(pTransferCtx);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Waits for a transfer context event.
 *
 * @returns VBox status code.
 * @param   pTransferCtx        Transfer context to wait for.
 * @param   msTimeout           Timeout (in ms) to wait.
 * @param   pEvent              Where to return the event data on success.
 */
static int shClTransferCtxWaitInternal(PSHCLTRANSFERCTX pTransferCtx, RTMSINTERVAL msTimeout, PSHCLTRANSFERCTXEVENT pEvent)
{
    LogFlowFunc(("Waiting for transfer context change (%RU32 timeout) ...\n", msTimeout));

    int rc = RTSemEventWait(pTransferCtx->ChangedEvent, msTimeout);
    if (RT_SUCCESS(rc))
    {
        shClTransferCtxLock(pTransferCtx);

        memcpy(pEvent, &pTransferCtx->ChangedEventData, sizeof(SHCLTRANSFERCTXEVENT));

        shClTransferCtxUnlock(pTransferCtx);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Waits for transfer to be (un-)registered.
 *
 * @returns VBox status code.
 * @param   pTransferCtx        Transfer context to wait for.
 * @param   msTimeout           Timeout (in ms) to wait.
 * @param   fRegister           Pass \c true for registering, or \c false for unregistering a transfer.
 * @param   idTransfer          Transfer ID to wait for.
 *                              Pass NIL_SHCLTRANSFERID for any transfer.
 * @param   ppTransfer          Where to return the transfer being (un-)registered. Optional and can be NULL.
 */
int ShClTransferCtxWait(PSHCLTRANSFERCTX pTransferCtx, RTMSINTERVAL msTimeout, bool fRegister, SHCLTRANSFERID idTransfer,
                        PSHCLTRANSFER *ppTransfer)
{
    AssertPtrReturn(pTransferCtx, VERR_INVALID_POINTER);

    int rc = VERR_TIMEOUT;

    uint64_t const tsStartMs = RTTimeMilliTS();
    uint64_t       msLeft    = msTimeout;
    for (;;)
    {
        SHCLTRANSFERCTXEVENT Event;
        rc = shClTransferCtxWaitInternal(pTransferCtx, msLeft, &Event);
        if (RT_FAILURE(rc))
            break;

        shClTransferCtxLock(pTransferCtx);

        if (Event.fRegistered == fRegister)
        {
            if (   idTransfer != NIL_SHCLTRANSFERID
                && Event.pTransfer
                && ShClTransferGetID(Event.pTransfer) == idTransfer)
            {
                if (ppTransfer)
                    *ppTransfer = Event.pTransfer;
                rc = VINF_SUCCESS;
            }
        }

        shClTransferCtxUnlock(pTransferCtx);

        if (RT_SUCCESS(rc))
            break;

        msLeft -= RT_MIN(msLeft, RTTimeMilliTS() - tsStartMs);
        if (msLeft == 0)
            break;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Cleans up all associated transfers which are not needed (anymore).
 * This can be due to transfers which only have been announced but not / never being run.
 *
 * @param   pTransferCtx        Transfer context to cleanup transfers for.
 */
void ShClTransferCtxCleanup(PSHCLTRANSFERCTX pTransferCtx)
{
    AssertPtrReturnVoid(pTransferCtx);

    shClTransferCtxLock(pTransferCtx);

    LogFlowFunc(("pTransferCtx=%p, cTransfers=%RU16 cRunning=%RU16\n",
                 pTransferCtx, pTransferCtx->cTransfers, pTransferCtx->cRunning));

    if (pTransferCtx->cTransfers == 0)
    {
        shClTransferCtxUnlock(pTransferCtx);
        return;
    }

    /* Remove all transfers which are not in a running state (e.g. only announced). */
    PSHCLTRANSFER pTransfer, pTransferNext;
    RTListForEachSafe(&pTransferCtx->List, pTransfer, pTransferNext, SHCLTRANSFER, Node)
    {
        shClTransferLock(pTransfer);

        SHCLTRANSFERSTATUS const enmStatus = shClTransferGetStatusLocked(pTransfer);
        LogFlowFunc(("\tTransfer #%RU16: %s\n", pTransfer->State.uID, ShClTransferStatusToStr(enmStatus)));

        if (enmStatus != SHCLTRANSFERSTATUS_STARTED)
        {
            shClTransferUnlock(pTransfer);

            shclTransferCtxTransferRemoveAndUnregister(pTransferCtx, pTransfer);
            ShClTransferDestroy(pTransfer);
        }
        else
            shClTransferUnlock(pTransfer);
    }

    shClTransferCtxUnlock(pTransferCtx);
}

/**
 * Returns whether the maximum of concurrent transfers of a specific transfer contexthas been reached or not.
 *
 * @returns \c if maximum has been reached, \c false if not.
 * @param   pTransferCtx        Transfer context to determine value for.
 */
bool ShClTransferCtxIsMaximumReached(PSHCLTRANSFERCTX pTransferCtx)
{
    AssertPtrReturn(pTransferCtx, true);

    shClTransferCtxLock(pTransferCtx);

    LogFlowFunc(("cRunning=%RU32, cMaxRunning=%RU32\n", pTransferCtx->cRunning, pTransferCtx->cMaxRunning));

    Assert(pTransferCtx->cRunning <= pTransferCtx->cMaxRunning);
    bool const fMaximumReached = pTransferCtx->cRunning == pTransferCtx->cMaxRunning;

    shClTransferCtxUnlock(pTransferCtx);

    return fMaximumReached;
}

/**
 * Copies file system objinfo from IPRT to Shared Clipboard format.
 *
 * @return VBox status code.
 * @param  pDst                 The Shared Clipboard structure to convert data to.
 * @param  pSrc                 The IPRT structure to convert data from.
 */
int ShClFsObjInfoFromIPRT(PSHCLFSOBJINFO pDst, PCRTFSOBJINFO pSrc)
{
    AssertPtrReturn(pDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pSrc, VERR_INVALID_POINTER);

    pDst->cbObject          = pSrc->cbObject;
    pDst->cbAllocated       = pSrc->cbAllocated;
    pDst->AccessTime        = pSrc->AccessTime;
    pDst->ModificationTime  = pSrc->ModificationTime;
    pDst->ChangeTime        = pSrc->ChangeTime;
    pDst->BirthTime         = pSrc->BirthTime;
    pDst->Attr.fMode        = pSrc->Attr.fMode;
    /* Clear bits which we don't pass through for security reasons. */
    pDst->Attr.fMode       &= ~(RTFS_UNIX_ISUID | RTFS_UNIX_ISGID | RTFS_UNIX_ISTXT);
    RT_ZERO(pDst->Attr.u);
    switch (pSrc->Attr.enmAdditional)
    {
        default:
            RT_FALL_THROUGH();
        case RTFSOBJATTRADD_NOTHING:
            pDst->Attr.enmAdditional        = SHCLFSOBJATTRADD_NOTHING;
            break;

        case RTFSOBJATTRADD_UNIX:
            pDst->Attr.enmAdditional        = SHCLFSOBJATTRADD_UNIX;
            pDst->Attr.u.Unix.uid           = pSrc->Attr.u.Unix.uid;
            pDst->Attr.u.Unix.gid           = pSrc->Attr.u.Unix.gid;
            pDst->Attr.u.Unix.cHardlinks    = pSrc->Attr.u.Unix.cHardlinks;
            pDst->Attr.u.Unix.INodeIdDevice = pSrc->Attr.u.Unix.INodeIdDevice;
            pDst->Attr.u.Unix.INodeId       = pSrc->Attr.u.Unix.INodeId;
            pDst->Attr.u.Unix.fFlags        = pSrc->Attr.u.Unix.fFlags;
            pDst->Attr.u.Unix.GenerationId  = pSrc->Attr.u.Unix.GenerationId;
            pDst->Attr.u.Unix.Device        = pSrc->Attr.u.Unix.Device;
            break;

        case RTFSOBJATTRADD_EASIZE:
            pDst->Attr.enmAdditional        = SHCLFSOBJATTRADD_EASIZE;
            pDst->Attr.u.EASize.cb          = pSrc->Attr.u.EASize.cb;
            break;
    }

    return VINF_SUCCESS;
}

/**
 * Queries local file system information from a given path.
 *
 * @returns VBox status code.
 * @param   pszPath             Path to query file system information for.
 * @param   pObjInfo            Where to return the queried file system information on success.
 */
int ShClFsObjInfoQueryLocal(const char *pszPath, PSHCLFSOBJINFO pObjInfo)
{
    RTFSOBJINFO objInfo;
    int rc = RTPathQueryInfo(pszPath, &objInfo,
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
                            RTFSOBJATTRADD_NOTHING
#else
                            RTFSOBJATTRADD_UNIX
#endif
                            );
    if (RT_SUCCESS(rc))
        rc = ShClFsObjInfoFromIPRT(pObjInfo, &objInfo);

    return rc;
}

/**
 * Translates a clipboard transfer status (SHCLTRANSFERSTATUS_XXX) into a string.
 *
 * @returns Transfer status string name.
 * @param   enmStatus           The transfer status to translate.
 */
const char *ShClTransferStatusToStr(SHCLTRANSFERSTATUS enmStatus)
{
    switch (enmStatus)
    {
        RT_CASE_RET_STR(SHCLTRANSFERSTATUS_NONE);
        RT_CASE_RET_STR(SHCLTRANSFERSTATUS_REQUESTED);
        RT_CASE_RET_STR(SHCLTRANSFERSTATUS_INITIALIZED);
        RT_CASE_RET_STR(SHCLTRANSFERSTATUS_UNINITIALIZED);
        RT_CASE_RET_STR(SHCLTRANSFERSTATUS_STARTED);
        RT_CASE_RET_STR(SHCLTRANSFERSTATUS_COMPLETED);
        RT_CASE_RET_STR(SHCLTRANSFERSTATUS_CANCELED);
        RT_CASE_RET_STR(SHCLTRANSFERSTATUS_KILLED);
        RT_CASE_RET_STR(SHCLTRANSFERSTATUS_ERROR);
    }
    return "Unknown";
}

/**
 * Transforms a path so that it can be sent over to the other party.
 *
 * @returns VBox status code.
 * @param   pszPath             Path to transform. Will be modified in place.
 * @param   cbPath              Size (in bytes) of \a pszPath.
 *
 * @note    Shared Clipboard file paths always are sent over as UNIX-style paths.
 *          Sending over back slashes ('\') could happen on non-Windows OSes as part of a path or file name.
 */
int ShClTransferTransformPath(char *pszPath, size_t cbPath)
{
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    RT_NOREF(cbPath);
    RTPathChangeToUnixSlashes(pszPath, true /* fForce */);
#else
    RT_NOREF(pszPath, cbPath);
#endif
    return VINF_SUCCESS;
}

/**
 * Validates whether a given path matches our set of rules or not.
 *
 * @returns VBox status code.
 * @param   pcszPath            Path to validate.
 * @param   fMustExist          Whether the path to validate also must exist.
 */
int ShClTransferValidatePath(const char *pcszPath, bool fMustExist)
{
    int rc = VINF_SUCCESS;

    if (!strlen(pcszPath))
        rc = VERR_INVALID_PARAMETER;

    if (   RT_SUCCESS(rc)
        && !RTStrIsValidEncoding(pcszPath))
    {
        rc = VERR_INVALID_UTF8_ENCODING;
    }

    if (   RT_SUCCESS(rc)
        && RTStrStr(pcszPath, ".."))
    {
        rc = VERR_INVALID_PARAMETER;
    }

    if (   RT_SUCCESS(rc)
        && fMustExist)
    {
        RTFSOBJINFO objInfo;
        rc = RTPathQueryInfo(pcszPath, &objInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(rc))
        {
            if (RTFS_IS_DIRECTORY(objInfo.Attr.fMode))
            {
                if (!RTDirExists(pcszPath)) /* Path must exist. */
                    rc = VERR_PATH_NOT_FOUND;
            }
            else if (RTFS_IS_FILE(objInfo.Attr.fMode))
            {
                if (!RTFileExists(pcszPath)) /* File must exist. */
                    rc = VERR_FILE_NOT_FOUND;
            }
            else /* Everything else (e.g. symbolic links) are not supported. */
            {
                LogRelMax(64, ("Shared Clipboard: Path '%s' contains a symbolic link or junction, which are not supported\n", pcszPath));
                rc = VERR_NOT_SUPPORTED;
            }
        }
    }

    if (RT_FAILURE(rc))
        LogRelMax(64, ("Shared Clipboard: Validating path '%s' failed: %Rrc\n", pcszPath, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Resolves a relative path of a specific transfer to its absolute path.
 *
 * @returns VBox status code.
 * @param   pTransfer           Clipboard transfer to resolve path for.
 * @param   pszPath             Relative path to resolve.
 *                              Paths have to end with a (back-)slash, otherwise this is considered to be a file.
 * @param   fFlags              Resolve flags. Currently not used and must be 0.
 * @param   ppszResolved        Where to store the allocated resolved path. Must be free'd by the called using RTStrFree().
 */
int ShClTransferResolvePathAbs(PSHCLTRANSFER pTransfer, const char *pszPath, uint32_t fFlags, char **ppszResolved)
{
    AssertPtrReturn(pTransfer,    VERR_INVALID_POINTER);
    AssertPtrReturn(pszPath,      VERR_INVALID_POINTER);
    AssertReturn   (fFlags == 0,  VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppszResolved, VERR_INVALID_POINTER);

    LogFlowFunc(("pszPath=%s, fFlags=%#x (pszPathRootAbs=%s, cRootListEntries=%RU64)\n",
                 pszPath, fFlags, pTransfer->pszPathRootAbs, pTransfer->lstRoots.Hdr.cEntries));

    int rc = ShClTransferValidatePath(pszPath, false /* fMustExist */);
    if (RT_SUCCESS(rc))
    {
        rc = VERR_PATH_NOT_FOUND; /* Play safe by default. */

        PSHCLLISTENTRY pEntry;
        RTListForEach(&pTransfer->lstRoots.lstEntries, pEntry, SHCLLISTENTRY, Node)
        {
            LogFlowFunc(("\tpEntry->pszName=%s\n", pEntry->pszName));

            if (RTStrStartsWith(pszPath, pEntry->pszName)) /* Case-sensitive! */
            {
                rc = VINF_SUCCESS;
                break;
            }
        }

        if (RT_SUCCESS(rc))
        {
            char *pszPathAbs = RTPathJoinA(pTransfer->pszPathRootAbs, pszPath);
            if (pszPathAbs)
            {
                char   szResolved[RTPATH_MAX];
                size_t cbResolved = sizeof(szResolved);
                rc = RTPathAbsEx(pTransfer->pszPathRootAbs, pszPathAbs, RTPATH_STR_F_STYLE_HOST, szResolved, &cbResolved);

                RTStrFree(pszPathAbs);
                pszPathAbs = NULL;

                if (RT_SUCCESS(rc))
                {
                    LogRel2(("Shared Clipboard: Resolved: '%s' -> '%s'\n", pszPath, szResolved));

                    *ppszResolved = RTStrDup(szResolved);
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Resolving absolute path for '%s' failed, rc=%Rrc\n", pszPath, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Converts Shared Clipboard create flags (see SharedClipboard-transfers.h) into IPRT create flags.
 *
 * @returns IPRT status code.
 * @param       fShClFlags  Shared clipboard create flags.
 * @param[out]  pfOpen      Where to store the RTFILE_O_XXX flags for
 *                          RTFileOpen.
 *
 * @sa Initially taken from vbsfConvertFileOpenFlags().
 */
int ShClTransferConvertFileCreateFlags(uint32_t fShClFlags, uint64_t *pfOpen)
{
    AssertMsgReturnStmt(!(fShClFlags & ~SHCL_OBJ_CF_VALID_MASK), ("%#x4\n", fShClFlags), *pfOpen = 0, VERR_INVALID_FLAGS);

    uint64_t fOpen = 0;

    switch (fShClFlags & SHCL_OBJ_CF_ACCESS_MASK_RW)
    {
        case SHCL_OBJ_CF_ACCESS_NONE:
        {
#ifdef RT_OS_WINDOWS
            if ((fShClFlags & SHCL_OBJ_CF_ACCESS_MASK_ATTR) != SHCL_OBJ_CF_ACCESS_ATTR_NONE)
                fOpen |= RTFILE_O_OPEN | RTFILE_O_ATTR_ONLY;
            else
#endif
                fOpen |= RTFILE_O_OPEN | RTFILE_O_READ;
            LogFlowFunc(("SHCL_OBJ_CF_ACCESS_NONE\n"));
            break;
        }

        case SHCL_OBJ_CF_ACCESS_READ:
        {
            fOpen |= RTFILE_O_OPEN | RTFILE_O_READ;
            LogFlowFunc(("SHCL_OBJ_CF_ACCESS_READ\n"));
            break;
        }

        default:
            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }

    switch (fShClFlags & SHCL_OBJ_CF_ACCESS_MASK_ATTR)
    {
        case SHCL_OBJ_CF_ACCESS_ATTR_NONE:
        {
            fOpen |= RTFILE_O_ACCESS_ATTR_DEFAULT;
            LogFlowFunc(("SHCL_OBJ_CF_ACCESS_ATTR_NONE\n"));
            break;
        }

        case SHCL_OBJ_CF_ACCESS_ATTR_READ:
        {
            fOpen |= RTFILE_O_ACCESS_ATTR_READ;
            LogFlowFunc(("SHCL_OBJ_CF_ACCESS_ATTR_READ\n"));
            break;
        }

        default:
            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }

    /* Sharing mask */
    switch (fShClFlags & SHCL_OBJ_CF_ACCESS_MASK_DENY)
    {
        case SHCL_OBJ_CF_ACCESS_DENYNONE:
            fOpen |= RTFILE_O_DENY_NONE;
            LogFlowFunc(("SHCL_OBJ_CF_ACCESS_DENYNONE\n"));
            break;

        case SHCL_OBJ_CF_ACCESS_DENYWRITE:
            fOpen |= RTFILE_O_DENY_WRITE;
            LogFlowFunc(("SHCL_OBJ_CF_ACCESS_DENYWRITE\n"));
            break;

        default:
            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }

    *pfOpen = fOpen;

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}

