/* $Id: clipboard-transfers.cpp 115131 2026-08-25 17:30:42Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard: Common clipboard transfer handling code.
 */

/*
 * Copyright (C) 2019-2026 Oracle and/or its affiliates.
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
#ifdef VBOX_WITH_SHARED_CLIPBOARD_HOST
# include <VBox/HostServices/VBoxSharedClipboardSvc.h>
#endif
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
static void shClTransferDestroyConsume(PSHCLTRANSFER pTransfer);

static void shclTransferCtxTransferRemoveLocked(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer);
static void shclTransferCtxTransferNotifyUnregistered(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer);
static void shclTransferCtxTransferRemoveAndUnregister(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer);
static PSHCLTRANSFER shClTransferCtxGetTransferByIdInternal(PSHCLTRANSFERCTX pTransferCtx, SHCLTRANSFERID uId);
static PSHCLTRANSFER shClTransferCtxGetTransferByIndexInternal(PSHCLTRANSFERCTX pTransferCtx, uint32_t uIdx);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_HOST
void ShClSvcTransferDestroy(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer);
void ShClSvcTransferDestroyById(PSHCLCLIENT pClient, SHCLTRANSFERID idTransfer);
void ShClSvcTransferDestroyByIdEx(PSHCLCLIENT pClient, SHCLTRANSFERID idTransfer, bool fNotifyGuest);
PSHCLTRANSFER shClSvcTransferDetach(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer);
void shClSvcTransferDestroyDetached(PSHCLTRANSFER pTransfer);
int ShClSvcTransferReportDetachedStatus(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, SHCLTRANSFERSTATUS enmStatus, int rcStatus);
#endif


/**
 * Checks whether a transfer ID is in the assignable context-local range.
 *
 * @returns true if the ID can be used by a transfer context, false otherwise.
 * @param   idTransfer          Transfer ID to check before narrowing to SHCLTRANSFERID.
 */
bool ShClTransferIdIsValid(uint32_t idTransfer)
{
    return    idTransfer > 0
           && idTransfer < VBOX_SHCL_MAX_TRANSFERS - 1;
}


/**
 * Checks whether a transfer key is usable for lifecycle tracking.
 *
 * @returns true if the key identifies a non-nil service transfer, false otherwise.
 * @param   pKey            Transfer key to validate.
 */
bool ShClTransferKeyIsValid(PCSHCLTRANSFERKEY pKey)
{
    if (!pKey)
        return false;

    SHCLSESSIONID const idSession = VBOX_SHCL_CONTEXTID_GET_SESSION(pKey->uContextId);
    return    VBOX_SHCL_CONTEXTID_GET_EVENT(pKey->uContextId) == 0
           && idSession != 0
           && idSession != NIL_SHCLSESSIONID
           && ShClTransferIdIsValid(VBOX_SHCL_CONTEXTID_GET_TRANSFER(pKey->uContextId))
           && pKey->uGeneration != 0
           && pKey->uGeneration != NIL_SHCLTRANSFERGEN;
}


/**
 * Initializes a normalized host-side transfer key.
 *
 * @param   pKey            Transfer key to initialize.
 * @param   idSession       Service session ID.
 * @param   idTransfer      Service transfer ID.
 * @param   uGeneration     Host-private transfer generation.
 */
void ShClTransferKeyInit(PSHCLTRANSFERKEY pKey, SHCLSESSIONID idSession, SHCLTRANSFERID idTransfer,
                         SHCLTRANSFERGEN uGeneration)
{
    AssertPtrReturnVoid(pKey);
    pKey->uContextId  = VBOX_SHCL_CONTEXTID_MAKE(idSession, idTransfer, 0 /* idEvent */);
    pKey->uGeneration = uGeneration;
}


/**
 * Resets a host-side transfer key to its invalid value.
 *
 * @param   pKey            Transfer key to reset.
 */
void ShClTransferKeyReset(PSHCLTRANSFERKEY pKey)
{
    AssertPtrReturnVoid(pKey);
    pKey->uContextId  = 0;
    pKey->uGeneration = NIL_SHCLTRANSFERGEN;
}


/**
 * Checks whether two host-side transfer keys are equal.
 *
 * @returns                     true if the keys are equal, false otherwise.
 * @param   pLeft           First transfer key.
 * @param   pRight          Second transfer key.
 */
bool ShClTransferKeyIsEqual(PCSHCLTRANSFERKEY pLeft, PCSHCLTRANSFERKEY pRight)
{
    return    pLeft
           && pRight
           && pLeft->uContextId == pRight->uContextId
           && pLeft->uGeneration == pRight->uGeneration;
}


/**
 * Returns the service session ID encoded in a host-side transfer key.
 *
 * @returns                     The encoded service session ID, or NIL_SHCLSESSIONID if @a pKey is NULL.
 * @param   pKey            Transfer key to inspect.
 */
SHCLSESSIONID ShClTransferKeyGetSessionId(PCSHCLTRANSFERKEY pKey)
{
    AssertPtrReturn(pKey, NIL_SHCLSESSIONID);
    return VBOX_SHCL_CONTEXTID_GET_SESSION(pKey->uContextId);
}


/**
 * Returns the transfer ID encoded in a host-side transfer key.
 *
 * @returns                     The encoded transfer ID, or NIL_SHCLTRANSFERID if @a pKey is NULL.
 * @param   pKey            Transfer key to inspect.
 */
SHCLTRANSFERID ShClTransferKeyGetTransferId(PCSHCLTRANSFERKEY pKey)
{
    AssertPtrReturn(pKey, NIL_SHCLTRANSFERID);
    return VBOX_SHCL_CONTEXTID_GET_TRANSFER(pKey->uContextId);
}


/**
 * Checks whether a transfer status is part of the Shared Clipboard protocol.
 *
 * @returns true if the status is valid, false otherwise.
 * @param   enmStatus       Transfer status to validate.
 */
bool ShClTransferStatusIsValid(SHCLTRANSFERSTATUS enmStatus)
{
    switch (enmStatus)
    {
        case SHCLTRANSFERSTATUS_NONE:
        case SHCLTRANSFERSTATUS_REQUESTED:
        case SHCLTRANSFERSTATUS_INITIALIZED:
        case SHCLTRANSFERSTATUS_UNINITIALIZED:
        case SHCLTRANSFERSTATUS_STARTED:
        case SHCLTRANSFERSTATUS_COMPLETED:
        case SHCLTRANSFERSTATUS_CANCELED:
        case SHCLTRANSFERSTATUS_KILLED:
        case SHCLTRANSFERSTATUS_ERROR:
            return true;

        default:
            return false;
    }
}


/**
 * Checks whether a transfer status ends the transfer lifecycle.
 *
 * @returns true if the status is terminal, false otherwise.
 * @param   enmStatus       Transfer status to classify.
 */
bool ShClTransferStatusIsTerminal(SHCLTRANSFERSTATUS enmStatus)
{
    return    enmStatus == SHCLTRANSFERSTATUS_COMPLETED
           || enmStatus == SHCLTRANSFERSTATUS_CANCELED
           || enmStatus == SHCLTRANSFERSTATUS_KILLED
           || enmStatus == SHCLTRANSFERSTATUS_ERROR
           || enmStatus == SHCLTRANSFERSTATUS_UNINITIALIZED;
}


/**
 * Checks whether a transfer status and result form a valid service reply.
 *
 * @returns true if the status is valid and the result matches it, false otherwise.
 * @param   enmStatus       Transfer status to validate.
 * @param   rcTransfer      Transfer result associated with the status.
 */
bool ShClTransferStatusResultIsValid(SHCLTRANSFERSTATUS enmStatus, int rcTransfer)
{
    if (!ShClTransferStatusIsValid(enmStatus))
        return false;

    switch (enmStatus)
    {
        case SHCLTRANSFERSTATUS_CANCELED:
            return rcTransfer == VERR_CANCELLED;

        case SHCLTRANSFERSTATUS_KILLED:
        case SHCLTRANSFERSTATUS_ERROR:
            return RT_FAILURE(rcTransfer);

        default:
            return RT_SUCCESS(rcTransfer);
    }
}


/**
 * Checks whether a service-reported transfer status may follow the previous
 * service-reported status.  This describes lifecycle records, not the
 * lower-level transfer state mutation sequence.
 *
 * @returns true if both statuses are valid and the transition is monotonic, false otherwise.
 * @param   enmOldStatus    Current transfer status.
 * @param   enmNewStatus    Incoming transfer status.
 */
bool ShClTransferStatusTransitionIsValid(SHCLTRANSFERSTATUS enmOldStatus, SHCLTRANSFERSTATUS enmNewStatus)
{
    if (   !ShClTransferStatusIsValid(enmOldStatus)
        || !ShClTransferStatusIsValid(enmNewStatus))
        return false;

    if (enmOldStatus == enmNewStatus)
        return true;

    switch (enmOldStatus)
    {
        case SHCLTRANSFERSTATUS_REQUESTED:
            return    enmNewStatus == SHCLTRANSFERSTATUS_INITIALIZED
                   || (   ShClTransferStatusIsTerminal(enmNewStatus)
                       && enmNewStatus != SHCLTRANSFERSTATUS_COMPLETED);

        case SHCLTRANSFERSTATUS_INITIALIZED:
            return    enmNewStatus == SHCLTRANSFERSTATUS_STARTED
                   || ShClTransferStatusIsTerminal(enmNewStatus);

        case SHCLTRANSFERSTATUS_STARTED:
            return ShClTransferStatusIsTerminal(enmNewStatus);

        default:
            return false;
    }
}


/**
 * Returns whether a transfer path is relative in both Unix and DOS path styles.
 */
static bool shClTransferPathIsRelative(const char *pszPath)
{
    AssertPtrReturn(pszPath, false);

    if (*pszPath == '\0')
        return true;

    union
    {
        RTPATHSPLIT Split;
        uint8_t     ab[RTPATH_MAX + sizeof(RTPATHSPLIT)];
    } u;

    int rc = RTPathSplit(pszPath, &u.Split, sizeof(u), RTPATH_STR_F_STYLE_UNIX);
    if (   RT_SUCCESS(rc)
        && RTPATH_PROP_HAS_ROOT_SPEC(u.Split.fProps))
        return false;

    rc = RTPathSplit(pszPath, &u.Split, sizeof(u), RTPATH_STR_F_STYLE_DOS);
    if (   RT_SUCCESS(rc)
        && RTPATH_PROP_HAS_ROOT_SPEC(u.Split.fProps))
        return false;

    return true;
}


/**
 * Returns whether @a pszPath is equal to or below absolute root @a pszRoot.
 */
static bool shClTransferPathIsBelowRootAbs(const char *pszPath, const char *pszRoot)
{
    AssertPtrReturn(pszPath, false);
    AssertPtrReturn(pszRoot, false);

    size_t const cchRoot = strlen(pszRoot);
    if (!cchRoot)
        return true;

    if (RTPathCompare(pszPath, pszRoot) == 0)
        return true;

    if (   cchRoot == 1
        && RTPATH_IS_SLASH(pszRoot[0]))
        return RTPATH_IS_SLASH(pszPath[0]);

    return RTPathStartsWith(pszPath, pszRoot);
}


/**
 * Returns whether the root list entry describes a directory.
 */
static bool shClTransferListEntryIsDirectory(PCSHCLLISTENTRY pEntry)
{
    if (   pEntry
        && (pEntry->fInfo & VBOX_SHCL_INFO_F_FSOBJINFO)
        && pEntry->pvInfo
        && pEntry->cbInfo == sizeof(SHCLFSOBJINFO))
    {
        PCSHCLFSOBJINFO pFsObjInfo = (PCSHCLFSOBJINFO)pEntry->pvInfo;
        return RTFS_IS_DIRECTORY(pFsObjInfo->Attr.fMode);
    }

    return false;
}


/**
 * Returns whether a relative transfer path is authorized by a root list entry.
 */
static bool shClTransferPathMatchesRootEntry(const char *pszPath, PCSHCLLISTENTRY pEntry)
{
    AssertPtrReturn(pszPath, false);
    AssertPtrReturn(pEntry, false);
    AssertPtrReturn(pEntry->pszName, false);

    size_t const cchRoot = strlen(pEntry->pszName);
    if (strncmp(pszPath, pEntry->pszName, cchRoot))
        return false;

    if (pszPath[cchRoot] == '\0')
        return true;

    if (!RTPATH_IS_SLASH(pszPath[cchRoot]))
        return false;

    return shClTransferListEntryIsDirectory(pEntry);
}


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
    pInfo->u.Local.hFile = NIL_RTFILE;

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

    switch (pInfo->enmType)
    {
        case SHCLOBJTYPE_DIRECTORY:
            if (RTDirIsValid(pInfo->u.Local.hDir))
            {
                RTDirClose(pInfo->u.Local.hDir);
                pInfo->u.Local.hDir = NIL_RTDIR;
            }
            break;

        case SHCLOBJTYPE_FILE:
            if (RTFileIsValid(pInfo->u.Local.hFile))
            {
                RTFileClose(pInfo->u.Local.hFile);
                pInfo->u.Local.hFile = NIL_RTFILE;
            }
            break;

        default:
            break;
    }

    if (pInfo->pszPathLocalAbs)
    {
        RTStrFree(pInfo->pszPathLocalAbs);
        pInfo->pszPathLocalAbs = NULL;
    }
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
    AssertPtrReturn(pListHdr, false);

    if (pListHdr->fFeatures & ~SHCL_TRANSFER_LIST_FEATURE_F_VALID_MASK)
        return false;

    if (pListHdr->cEntries > UINT32_MAX)
        return false;

    if (   pListHdr->cEntries == 0
        && pListHdr->cbTotalSize != 0)
        return false;

    return true;
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
        pDst->fList    = pSrc->fList;
        pDst->cbFilter = pSrc->cbFilter;
        pDst->cbPath   = pSrc->cbPath;
    }
    else
        ShClTransferListOpenParmsDestroy(pDst);

    return rc;
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

    int rc = VINF_SUCCESS;
    if (   !pParms->pszFilter
        || !pParms->pszPath)
    {
        ShClTransferListOpenParmsDestroy(pParms);
        rc = VERR_NO_MEMORY;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
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
    int rc = ShClTransferValidatePathEx(pszName, cbName, false /* fMustExist */);
    if (RT_FAILURE(rc))
        return false;

    if (*pszName == '\0')
        return false;

    if (!shClTransferPathIsRelative(pszName))
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
 *                              Up to SHCLLISTENTRY_MAX_NAME bytes, including the terminator.
 * @param   pvInfo              Pointer to info data to assign. Must match \a fInfo.
 *                              The list entry takes the ownership of the data on success.
 * @param   cbInfo              Size (in bytes) of \a pvInfo data to assign.
 */
int ShClTransferListEntryInitEx(PSHCLLISTENTRY pListEntry, uint32_t fInfo, const char *pszName, void *pvInfo, uint32_t cbInfo)
{
    AssertPtrReturn(pListEntry, VERR_INVALID_POINTER);

    size_t cchName = 0;
    if (pszName)
    {
        cchName = RTStrNLen(pszName, SHCLLISTENTRY_MAX_NAME);
        if (   cchName >= SHCLLISTENTRY_MAX_NAME
            || !shclTransferListEntryNameIsValid(pszName, cchName + 1))
            return VERR_INVALID_PARAMETER;
    }
    /* pvInfo + cbInfo depend on fInfo. See below. */

    RT_BZERO(pListEntry, sizeof(SHCLLISTENTRY));

    if (pszName)
    {
        pListEntry->pszName = RTStrDupN(pszName, cchName);
        AssertPtrReturn(pListEntry->pszName, VERR_NO_MEMORY);
        pListEntry->cbName = (uint32_t)cchName + 1 /* Include terminator */;
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

    if (   shclTransferListEntryNameIsValid(pListEntry->pszName, pListEntry->cbName)
        && !(pListEntry->fInfo & ~VBOX_SHCL_INFO_F_VALID_MASK))
    {
        if (pListEntry->fInfo & VBOX_SHCL_INFO_F_FSOBJINFO)
        {
            fValid =    pListEntry->pvInfo != NULL
                     && pListEntry->cbInfo == sizeof(SHCLFSOBJINFO);
        }
        else
            fValid =    pListEntry->pvInfo == NULL
                     && pListEntry->cbInfo == 0;
    }

    if (!fValid)
    {
        size_t const cbName  = RT_MIN((size_t)pListEntry->cbName, (size_t)SHCLLISTENTRY_MAX_NAME);
        size_t const cchName = pListEntry->pszName && cbName ? RTStrNLen(pListEntry->pszName, cbName) : 0;
        LogRelMax(16, ("Shared Clipboard: List entry '%.*s' is invalid\n",
                 (int)RT_MIN(cchName, (size_t)128), pListEntry->pszName ? pListEntry->pszName : ""));
    }

    return fValid;
}


/*********************************************************************************************************************************
 * Transfer Object                                                                                                               *
 ********************************************************************************************************************************/

/**
 * Initializes a transfer object structure.
 *
 * @returns VBox status code.
 * @param   pObj                Object structure to initialize.
 */
int ShClTransferObjInit(PSHCLTRANSFEROBJ pObj)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);

    pObj->hObj      = NIL_SHCLOBJHANDLE;
    pObj->pProgress = NULL;
    pObj->enmType   = SHCLOBJTYPE_INVALID;

    pObj->pszPathLocalAbs = NULL;

    RT_ZERO(pObj->u);
    pObj->u.Local.hFile = NIL_RTFILE;

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

    switch (pObj->enmType)
    {
        case SHCLOBJTYPE_DIRECTORY:
            if (RTDirIsValid(pObj->u.Local.hDir))
            {
                RTDirClose(pObj->u.Local.hDir);
                pObj->u.Local.hDir = NIL_RTDIR;
            }
            break;

        case SHCLOBJTYPE_FILE:
            if (RTFileIsValid(pObj->u.Local.hFile))
            {
                RTFileClose(pObj->u.Local.hFile);
                pObj->u.Local.hFile = NIL_RTFILE;
            }
            break;

        default:
            break;
    }

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
    AssertMsgReturn(pOpenCreateParms->pszPath, ("No path in open/create params set\n"), VERR_INVALID_PARAMETER);
    AssertReturn(pOpenCreateParms->cbPath, VERR_INVALID_PARAMETER);

    *phObj = NIL_SHCLOBJHANDLE;

    if (pOpenCreateParms->fCreate & ~SHCL_OBJ_CF_VALID_MASK)
        return VERR_INVALID_FLAGS;

    if (!shClTransferPathIsRelative(pOpenCreateParms->pszPath))
        return VERR_PATH_IS_NOT_RELATIVE;

    int rc = ShClTransferValidatePath(pOpenCreateParms->pszPath, false /* fMustExist */);
    if (RT_FAILURE(rc))
        return rc;

    if (pTransfer->cObjHandles >= pTransfer->cMaxObjHandles)
        return VERR_SHCLPB_MAX_OBJECTS_REACHED;

    LogFlowFunc(("pszPath=%s, fCreate=0x%x\n", pOpenCreateParms->pszPath, pOpenCreateParms->fCreate));

    if (pTransfer->ProviderIface.pfnObjOpen)
        rc = pTransfer->ProviderIface.pfnObjOpen(&pTransfer->ProviderCtx, pOpenCreateParms, phObj);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_SUCCESS(rc))
    {
        int const rc2 = ShClTransferProgressObjRegister(pTransfer, *phObj, pOpenCreateParms->pszPath);
        if (RT_FAILURE(rc2))
            LogFlowFunc(("Registering object progress failed with %Rrc\n", rc2));
    }

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Opening object '%.*s' for transfer %RU16/%RU64 in session %RU16 (flags %#x) failed with %Rrc\n",
                       128, pOpenCreateParms->pszPath, ShClTransferKeyGetTransferId(&pTransfer->State.Key),
                       pTransfer->State.Key.uGeneration, ShClTransferKeyGetSessionId(&pTransfer->State.Key),
                       pOpenCreateParms->fCreate, rc));

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
    AssertReturn(hObj != NIL_SHCLOBJHANDLE, VERR_INVALID_HANDLE);

    int rc;
    if (pTransfer->ProviderIface.pfnObjClose)
        rc = pTransfer->ProviderIface.pfnObjClose(&pTransfer->ProviderCtx, hObj);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_SUCCESS(rc))
        ShClTransferProgressObjUnregister(pTransfer, hObj);

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Closing object %RU64 for transfer %RU16/%RU64 in session %RU16 failed with %Rrc\n",
                       hObj, ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.Key.uGeneration,
                       ShClTransferKeyGetSessionId(&pTransfer->State.Key), rc));

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
    AssertReturn   (hObj != NIL_SHCLOBJHANDLE, VERR_INVALID_HANDLE);
    AssertReturn   (fFlags == 0, VERR_INVALID_FLAGS);
    AssertReturn   (cbBuf <= pTransfer->cbMaxChunkSize, VERR_BUFFER_OVERFLOW);
    /* pcbRead is optional. */

    int rc;
    if (pTransfer->ProviderIface.pfnObjRead)
        rc = pTransfer->ProviderIface.pfnObjRead(&pTransfer->ProviderCtx, hObj, pvBuf, cbBuf, fFlags, pcbRead);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Reading %RU32 bytes from object %RU64 for transfer %RU16/%RU64 in session %RU16 (flags %#x) failed with %Rrc\n",
                       cbBuf, hObj, ShClTransferKeyGetTransferId(&pTransfer->State.Key),
                       pTransfer->State.Key.uGeneration, ShClTransferKeyGetSessionId(&pTransfer->State.Key), fFlags, rc));

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
    AssertReturn   (hObj != NIL_SHCLOBJHANDLE, VERR_INVALID_HANDLE);
    AssertReturn   (fFlags == 0, VERR_INVALID_FLAGS);
    AssertReturn   (cbBuf <= pTransfer->cbMaxChunkSize, VERR_BUFFER_OVERFLOW);
    /* pcbWritten is optional. */

    int rc;
    if (pTransfer->ProviderIface.pfnObjWrite)
        rc = pTransfer->ProviderIface.pfnObjWrite(&pTransfer->ProviderCtx, hObj, pvBuf, cbBuf, fFlags, pcbWritten);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Writing %RU32 bytes to object %RU64 for transfer %RU16/%RU64 in session %RU16 (flags %#x) failed with %Rrc\n",
                       cbBuf, hObj, ShClTransferKeyGetTransferId(&pTransfer->State.Key),
                       pTransfer->State.Key.uGeneration, ShClTransferKeyGetSessionId(&pTransfer->State.Key), fFlags, rc));

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

    pDataChunkDup->uHandle = pDataChunk->uHandle;
    pDataChunkDup->cbData  = pDataChunk->cbData;

    if (pDataChunk->cbData)
    {
        AssertPtrReturnStmt(pDataChunk->pvData, RTMemFree(pDataChunkDup), NULL);

        pDataChunkDup->pvData = RTMemDup(pDataChunk->pvData, pDataChunk->cbData);
        if (!pDataChunkDup->pvData)
        {
            RTMemFree(pDataChunkDup);
            return NULL;
        }
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

    pDataChunk->uHandle = NIL_SHCLOBJHANDLE;
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
static int shClTransferCreateInternal(SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource, PSHCLTRANSFERCALLBACKS pCallbacks,
                                      uint32_t cbMaxChunkSize, uint32_t cMaxListHandles, uint32_t cMaxObjHandles,
                                      PSHCLTRANSFER *ppTransfer)
{
    AssertPtrReturn(ppTransfer, VERR_INVALID_POINTER);
    AssertReturn(ShClTransferDirIsValid(enmDir), VERR_INVALID_PARAMETER);
    AssertReturn(ShClSourceIsValid(enmSource), VERR_INVALID_PARAMETER);
    AssertReturn(cbMaxChunkSize, VERR_INVALID_PARAMETER);
    AssertReturn(cMaxListHandles, VERR_INVALID_PARAMETER);
    AssertReturn(cMaxObjHandles, VERR_INVALID_PARAMETER);
    AssertReturn(cMaxListHandles < VBOX_SHCL_MAX_TRANSFERS, VERR_INVALID_PARAMETER);
    AssertReturn(cMaxObjHandles < VBOX_SHCL_MAX_TRANSFERS, VERR_INVALID_PARAMETER);
    /* pCallbacks can be NULL. */

    LogFlowFuncEnter();

    PSHCLTRANSFER pTransfer = (PSHCLTRANSFER)RTMemAllocZ(sizeof(SHCLTRANSFER));
    AssertPtrReturn(pTransfer, VERR_NO_MEMORY);

    ShClTransferKeyReset(&pTransfer->State.Key);
    pTransfer->State.enmStatus = SHCLTRANSFERSTATUS_NONE;
    pTransfer->State.enmDir    = enmDir;
    pTransfer->State.enmSource = enmSource;

    pTransfer->Thread.hThread    = NIL_RTTHREAD;
    pTransfer->Thread.fCancelled = false;
    pTransfer->Thread.fStarted   = false;
    pTransfer->Thread.fStop      = false;

    pTransfer->pszPathRootAbs    = NULL;
    pTransfer->pOwnerCtx         = NULL;

    pTransfer->uTimeoutMs      = SHCL_TIMEOUT_DEFAULT_MS;
    pTransfer->cbMaxChunkSize  = cbMaxChunkSize;
    pTransfer->cMaxListHandles = cMaxListHandles;
    pTransfer->cMaxObjHandles  = cMaxObjHandles;

    pTransfer->pvUser = NULL;
    pTransfer->cbUser = 0;

    RTListInit(&pTransfer->lstHandles);
    RTListInit(&pTransfer->lstObj);
    RTListInit(&pTransfer->lstProgressObj);

    /* The provider context + interface is NULL by default. */
    RT_ZERO(pTransfer->ProviderCtx);
    RT_ZERO(pTransfer->ProviderIface);

    /* Make sure to set the callbacks before calling pfnOnCreate below. */
    shClTransferSetCallbacks(pTransfer, pCallbacks);

    ShClTransferListInit(&pTransfer->lstRoots);

    pTransfer->StatusChangeEvent = NIL_RTSEMEVENT;
    pTransfer->hNoRefsEvent      = NIL_RTSEMEVENTMULTI;

    int rc = RTCritSectInit(&pTransfer->CritSect);
    if (RT_SUCCESS(rc))
    {
        rc = RTSemEventMultiCreate(&pTransfer->hNoRefsEvent);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemEventCreate(&pTransfer->StatusChangeEvent);
            if (RT_SUCCESS(rc))
            {
                rc = ShClEventSourceInit(&pTransfer->Events, 0 /* uID */);
                if (RT_SUCCESS(rc))
                {
                    if (pTransfer->Callbacks.pfnOnCreated)
                        pTransfer->Callbacks.pfnOnCreated(&pTransfer->CallbackCtx);

                    *ppTransfer = pTransfer;
                    LogFlowFuncLeaveRC(rc);
                    return rc;
                }

                RTSemEventDestroy(pTransfer->StatusChangeEvent);
                pTransfer->StatusChangeEvent = NIL_RTSEMEVENT;
            }

            RTSemEventMultiDestroy(pTransfer->hNoRefsEvent);
            pTransfer->hNoRefsEvent = NIL_RTSEMEVENTMULTI;
        }

        RTCritSectDelete(&pTransfer->CritSect);
    }

    RTMemFree(pTransfer);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates a clipboard transfer, extended version.
 *
 * @returns VBox status code.
 * @param   enmDir              Specifies the transfer direction of this transfer.
 * @param   enmSource           Specifies the data source of the transfer.
 * @param   cbMaxChunkSize      Maximum transfer chunk size (in bytes) to use.
 * @param   cMaxListHandles     Maximum list entries the transfer can have.
 * @param   cMaxObjHandles      Maximum transfer objects the transfer can have.
 * @param   ppTransfer          Where to return the created clipboard transfer struct.
 *                              Must be destroyed by ShClTransferDestroy().
 */
int ShClTransferCreateEx(SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource, uint32_t cbMaxChunkSize,
                         uint32_t cMaxListHandles, uint32_t cMaxObjHandles, PSHCLTRANSFER *ppTransfer)
{
    return shClTransferCreateInternal(enmDir, enmSource, NULL /* pCallbacks */, cbMaxChunkSize,
                                      cMaxListHandles, cMaxObjHandles, ppTransfer);
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
    return shClTransferCreateInternal(enmDir, enmSource, pCallbacks,
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

    AssertMsgReturn(pTransfer->pOwnerCtx == NULL,
                    ("Transfer is still registered with context %p\n", pTransfer->pOwnerCtx), VERR_WRONG_ORDER);

    LogFlowFuncEnter();

    int rc = shClTransferThreadDestroy(pTransfer, SHCL_TIMEOUT_DEFAULT_MS);
    if (RT_FAILURE(rc))
        return rc;

    AssertMsgReturn(ASMAtomicReadU32(&pTransfer->cRefs) == 0,
                    ("Number of references > 0 (%RU32)\n", pTransfer->cRefs), VERR_WRONG_ORDER);

    /* Callback-owned state may still be used by the worker or a temporary
     * reference holder.  Destroy it only after both have been drained. */
    if (pTransfer->Callbacks.pfnOnDestroy)
    {
        pTransfer->Callbacks.pfnOnDestroy(&pTransfer->CallbackCtx);
        pTransfer->Callbacks.pfnOnDestroy = NULL;
    }

    ShClTransferReset(pTransfer);

    if (RTCritSectIsInitialized(&pTransfer->CritSect))
        RTCritSectDelete(&pTransfer->CritSect);

    rc = RTSemEventDestroy(pTransfer->StatusChangeEvent);
    AssertRCReturn(rc, rc);
    pTransfer->StatusChangeEvent = NIL_RTSEMEVENT;

    ShClEventSourceTerm(&pTransfer->Events);

    rc = RTSemEventMultiDestroy(pTransfer->hNoRefsEvent);
    AssertRCReturn(rc, rc);
    pTransfer->hNoRefsEvent = NIL_RTSEMEVENTMULTI;

    RTMemFree(pTransfer);
    pTransfer = NULL;

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}

/**
 * Consumes a transfer during owner shutdown.
 *
 * Unlike ShClTransferDestroy(), this cannot leave a detached transfer behind:
 * callbacks, the transfer worker and outstanding reference holders are drained
 * before the transfer is freed.
 *
 * @param   pTransfer           Clipboard transfer to consume. The pointer is
 *                              invalid after return.
 */
static void shClTransferDestroyConsume(PSHCLTRANSFER pTransfer)
{
    if (!pTransfer)
        return;

    AssertMsgReturnVoid(!RTCritSectIsOwner(&pTransfer->CritSect),
                        ("The transfer lock must not be held while consuming a transfer\n"));

    int rc = shClTransferThreadDestroy(pTransfer, SHCL_TIMEOUT_DEFAULT_MS);

    shClTransferLock(pTransfer);
    bool const fThreadActive = pTransfer->Thread.hThread != NIL_RTTHREAD;
    shClTransferUnlock(pTransfer);
    if (fThreadActive)
    {
        LogRelMax(16, ("Shared Clipboard: Transfer %RU16/%RU64 in session %RU16 did not stop within the normal teardown timeout (%Rrc); continuing to wait safely\n",
                       ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.Key.uGeneration,
                       ShClTransferKeyGetSessionId(&pTransfer->State.Key), rc));
        rc = shClTransferThreadDestroy(pTransfer, RT_INDEFINITE_WAIT);

        shClTransferLock(pTransfer);
        bool const fThreadStillActive =    pTransfer->Thread.hThread != NIL_RTTHREAD
                                        || pTransfer->Thread.fStarted;
        shClTransferUnlock(pTransfer);
        AssertFatalMsg(!fThreadStillActive, ("Reaping the transfer worker failed with %Rrc\n", rc));
    }

    if (RT_FAILURE(rc))
        LogFlowFunc(("Ignoring completed transfer worker status %Rrc during consuming teardown\n", rc));

    uint32_t cRefs;
    for (;;)
    {
        /* Serialize the zero observation with the final releaser.  It signals
         * hNoRefsEvent before dropping this lock, so the event cannot be
         * destroyed while ShClTransferRelease() is still using it. */
        shClTransferLock(pTransfer);
        cRefs = ASMAtomicReadU32(&pTransfer->cRefs);
        shClTransferUnlock(pTransfer);
        if (!cRefs)
            break;

        LogRel2(("Shared Clipboard: Waiting for %RU32 transfer reference(s) to drain during teardown\n", cRefs));
        rc = RTSemEventMultiWait(pTransfer->hNoRefsEvent, RT_INDEFINITE_WAIT);
        AssertFatalMsgRC(rc, ("Waiting for transfer references to drain failed with %Rrc\n", rc));
    }

    /* A retained user may have started a worker while the initial stop was in
     * progress.  With all retained users gone, no legitimate caller can
     * publish another one, so drain the final worker before callback state. */
    rc = shClTransferThreadDestroy(pTransfer, RT_INDEFINITE_WAIT);

    shClTransferLock(pTransfer);
    bool const fThreadStillActive =    pTransfer->Thread.hThread != NIL_RTTHREAD
                                    || pTransfer->Thread.fStarted;
    shClTransferUnlock(pTransfer);
    AssertFatalMsg(!fThreadStillActive, ("Final transfer worker drain failed with %Rrc\n", rc));

    if (RT_FAILURE(rc))
        LogFlowFunc(("Ignoring completed transfer worker status %Rrc after reference drain\n", rc));

    /* Temporary reference holders may use callback-owned per-transfer state.
     * Destroy that state only after all of them and the worker have drained. */
    if (pTransfer->Callbacks.pfnOnDestroy)
    {
        pTransfer->Callbacks.pfnOnDestroy(&pTransfer->CallbackCtx);
        pTransfer->Callbacks.pfnOnDestroy = NULL;
    }

    rc = ShClTransferDestroy(pTransfer);
    AssertFatalMsgRC(rc, ("Final transfer destruction failed with %Rrc\n", rc));
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
 * Sets the exact aggregate payload size of a transfer.
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer to update.
 * @param   cbTotal             Exact total payload size in bytes.
 */
int ShClTransferProgressSetTotal(PSHCLTRANSFER pTransfer, uint64_t cbTotal)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    shClTransferLock(pTransfer);

    int rc = VINF_SUCCESS;
    if (pTransfer->Progress.fTotalKnown)
    {
        if (pTransfer->Progress.cbTotal != cbTotal)
            rc = VERR_WRONG_ORDER;
    }
    else if (pTransfer->Progress.cbProcessed > cbTotal)
        rc = VERR_TOO_MUCH_DATA;
    else
    {
        pTransfer->Progress.cbTotal     = cbTotal;
        pTransfer->Progress.fTotalKnown = true;
    }

    shClTransferUnlock(pTransfer);
    return rc;
}

/**
 * Sets exact aggregate progress from a root list containing only regular files.
 *
 * Directories are deliberately rejected because their object size does not
 * include the payload sizes of their descendants.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if any root is not a regular file with valid
 *          file-system information.
 * @param   pTransfer           Transfer whose root list to inspect.
 */
int ShClTransferProgressSetTotalFromRoots(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    uint64_t cbTotal = 0;
    uint64_t cEntries = 0;
    int rc = VINF_SUCCESS;

    shClTransferLock(pTransfer);

    PCSHCLLISTENTRY pEntry;
    RTListForEach(&pTransfer->lstRoots.lstEntries, pEntry, SHCLLISTENTRY, Node)
    {
        cEntries++;
        if (   !(pEntry->fInfo & VBOX_SHCL_INFO_F_FSOBJINFO)
            || !pEntry->pvInfo
            || pEntry->cbInfo != sizeof(SHCLFSOBJINFO))
        {
            rc = VERR_NOT_SUPPORTED;
            break;
        }

        PCSHCLFSOBJINFO const pObjInfo = (PCSHCLFSOBJINFO)pEntry->pvInfo;
        if (!RTFS_IS_FILE(pObjInfo->Attr.fMode))
        {
            rc = VERR_NOT_SUPPORTED;
            break;
        }
        if (pObjInfo->cbObject < 0)
        {
            rc = VERR_INVALID_PARAMETER;
            break;
        }
        uint64_t const cbObject = (uint64_t)pObjInfo->cbObject;
        if (cbObject > UINT64_MAX - cbTotal)
        {
            rc = VERR_OUT_OF_RANGE;
            break;
        }
        cbTotal += cbObject;
    }

    if (   RT_SUCCESS(rc)
        && cEntries != pTransfer->lstRoots.Hdr.cEntries)
        rc = VERR_WRONG_ORDER;
    if (RT_SUCCESS(rc))
    {
        if (pTransfer->Progress.fTotalKnown)
        {
            if (pTransfer->Progress.cbTotal != cbTotal)
                rc = VERR_WRONG_ORDER;
        }
        else if (pTransfer->Progress.cbProcessed > cbTotal)
            rc = VERR_TOO_MUCH_DATA;
        else
        {
            pTransfer->Progress.cbTotal     = cbTotal;
            pTransfer->Progress.fTotalKnown = true;
        }
    }

    shClTransferUnlock(pTransfer);
    return rc;
}

/** Adds unique payload bytes while the transfer lock is held. */
static int shClTransferProgressAddLocked(PSHCLTRANSFER pTransfer, uint32_t cbDelta,
                                         uint64_t *pcbProcessed, uint64_t *pcbTotal, bool *pfNotify)
{
    Assert(RTCritSectIsOwner(&pTransfer->CritSect));

    int rc;
    if (cbDelta > UINT64_MAX - pTransfer->Progress.cbProcessed)
        rc = VERR_OUT_OF_RANGE;
    else
    {
        uint64_t const cbProcessed = pTransfer->Progress.cbProcessed + cbDelta;
        if (   pTransfer->Progress.fTotalKnown
            && cbProcessed > pTransfer->Progress.cbTotal)
            rc = VERR_TOO_MUCH_DATA;
        else
        {
            pTransfer->Progress.cbProcessed = cbProcessed;
            if (pTransfer->Progress.fTotalKnown)
            {
                *pcbProcessed = cbProcessed;
                *pcbTotal     = pTransfer->Progress.cbTotal;

                /*
                 * Calculate floor(cbProcessed * 100 / cbTotal) without an
                 * overflowing multiplication.  Testing percentage thresholds
                 * also keeps this portable to compilers without a native
                 * 128-bit integer type.
                 */
                uint32_t uPercentMin = 0;
                uint32_t uPercentMax = 100;
                uint64_t const cbWhole = pTransfer->Progress.cbTotal / 100;
                uint64_t const cbRem   = pTransfer->Progress.cbTotal % 100;
                while (uPercentMin < uPercentMax)
                {
                    uint32_t const uPercent = (uPercentMin + uPercentMax + 1) / 2;
                    uint64_t const cbThreshold = cbWhole * uPercent
                                               + (cbRem * uPercent + 99) / 100;
                    if (cbProcessed >= cbThreshold)
                        uPercentMin = uPercent;
                    else
                        uPercentMax = uPercent - 1;
                }

                uint8_t const uPercent = (uint8_t)RT_MIN(uPercentMin, 99U);
                if (!pTransfer->Progress.fReportedAny)
                {
                    pTransfer->Progress.fReportedAny = true;
                    pTransfer->Progress.uLastPercent = uPercent;
                    *pfNotify = true;
                }
                else if (   uPercent > 0
                         && uPercent != pTransfer->Progress.uLastPercent)
                {
                    pTransfer->Progress.uLastPercent = uPercent;
                    *pfNotify = true;
                }
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_NOT_AVAILABLE;
        }
    }

    return rc;
}


/**
 * Adds successfully transferred unique object payload bytes to aggregate progress.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_AVAILABLE if an exact total is not known yet.  The
 *          processed-byte count is still retained for a later exact total.
 * @param   pTransfer           Transfer to update.
 * @param   cbDelta             Number of newly processed payload bytes.
 * @param   pcbProcessed        Where to return the aggregate processed bytes
 *                              when an exact total is known.
 * @param   pcbTotal            Where to return the exact total bytes.
 * @param   pfNotify            Where to return whether this snapshot should be
 *                              published.  The first positive snapshot is
 *                              published, then at most one per changed integer
 *                              percentage while the transfer is active.
 */
int ShClTransferProgressAdd(PSHCLTRANSFER pTransfer, uint32_t cbDelta,
                            uint64_t *pcbProcessed, uint64_t *pcbTotal, bool *pfNotify)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertReturn(cbDelta, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbProcessed, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbTotal, VERR_INVALID_POINTER);
    AssertPtrReturn(pfNotify, VERR_INVALID_POINTER);

    *pcbProcessed = 0;
    *pcbTotal     = 0;
    *pfNotify     = false;

    shClTransferLock(pTransfer);
    int const rc = shClTransferProgressAddLocked(pTransfer, cbDelta, pcbProcessed, pcbTotal, pfNotify);
    shClTransferUnlock(pTransfer);
    return rc;
}


/**
 * Registers an open object for idempotent progress accounting.
 *
 * Providers which already maintain an SHCLTRANSFEROBJ only have its logical
 * path progress attached.  Providers with opaque remote handles get a small
 * tracking-only object which is removed by ShClTransferProgressObjUnregister().
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer owning the object.
 * @param   hObj                Open object handle.
 * @param   pszPath             Root-relative object path.
 */
int ShClTransferProgressObjRegister(PSHCLTRANSFER pTransfer, SHCLOBJHANDLE hObj, const char *pszPath)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertReturn(hObj != NIL_SHCLOBJHANDLE, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);

    PSHCLTRANSFEROBJ pObjNew = (PSHCLTRANSFEROBJ)RTMemAllocZ(sizeof(*pObjNew));
    if (pObjNew)
    {
        int const rc2 = ShClTransferObjInit(pObjNew);
        AssertRC(rc2);
        pObjNew->hObj      = hObj;
        pObjNew->enmSource = SHCLSOURCE_REMOTE;
    }

    PSHCLTRANSFERPROGRESSOBJ pProgressNew
        = (PSHCLTRANSFERPROGRESSOBJ)RTMemAllocZ(sizeof(*pProgressNew));
    if (pProgressNew)
    {
        pProgressNew->pszPath = RTStrDup(pszPath);
        if (!pProgressNew->pszPath)
        {
            RTMemFree(pProgressNew);
            pProgressNew = NULL;
        }
    }

    shClTransferLock(pTransfer);

    int rc = VINF_SUCCESS;
    PSHCLTRANSFEROBJ pObj = ShClTransferObjGet(pTransfer, hObj);
    if (!pObj)
    {
        if (pObjNew)
        {
            pObj = pObjNew;
            pObjNew = NULL;
            RTListAppend(&pTransfer->lstObj, &pObj->Node);
            pTransfer->cObjHandles++;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        PSHCLTRANSFERPROGRESSOBJ pProgress = NULL;
        PSHCLTRANSFERPROGRESSOBJ pProgressIt;
        RTListForEach(&pTransfer->lstProgressObj, pProgressIt, SHCLTRANSFERPROGRESSOBJ, Node)
        {
            if (RTStrCmp(pProgressIt->pszPath, pszPath) == 0)
            {
                pProgress = pProgressIt;
                break;
            }
        }
        if (   !pProgress
            && pProgressNew)
        {
            pProgress = pProgressNew;
            pProgressNew = NULL;
            RTListAppend(&pTransfer->lstProgressObj, &pProgress->Node);
        }

        if (!pProgress)
            rc = VERR_NO_MEMORY;
        else
            pObj->pProgress = pProgress;
    }

    shClTransferUnlock(pTransfer);

    if (pObjNew)
    {
        ShClTransferObjDestroy(pObjNew);
        RTMemFree(pObjNew);
    }
    if (pProgressNew)
    {
        RTStrFree(pProgressNew->pszPath);
        RTMemFree(pProgressNew);
    }
    return rc;
}


/**
 * Unregisters a tracking-only remote object.
 *
 * @param   pTransfer           Transfer owning the object.
 * @param   hObj                Object handle to unregister.
 */
void ShClTransferProgressObjUnregister(PSHCLTRANSFER pTransfer, SHCLOBJHANDLE hObj)
{
    AssertPtrReturnVoid(pTransfer);

    PSHCLTRANSFEROBJ pObjFree = NULL;

    shClTransferLock(pTransfer);
    PSHCLTRANSFEROBJ const pObj = ShClTransferObjGet(pTransfer, hObj);
    if (   pObj
        && pObj->enmType == SHCLOBJTYPE_INVALID)
    {
        RTListNodeRemove(&pObj->Node);
        Assert(pTransfer->cObjHandles);
        pTransfer->cObjHandles--;
        pObjFree = pObj;
    }
    shClTransferUnlock(pTransfer);

    if (pObjFree)
    {
        ShClTransferObjDestroy(pObjFree);
        RTMemFree(pObjFree);
    }
}


/**
 * Accounts a successful sequential read as a unique per-path payload prefix.
 *
 * Reopened or concurrent streams may read the same prefix repeatedly.  Only
 * growth beyond the largest prefix previously observed for that path advances
 * aggregate progress.
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer owning the object.
 * @param   hObj                Object handle which was read.
 * @param   cbDelta             Number of bytes returned by the read.
 * @param   pcbProcessed        Where to return aggregate processed bytes.
 * @param   pcbTotal            Where to return the exact aggregate total.
 * @param   pfNotify            Where to return whether to publish the snapshot.
 */
int ShClTransferProgressObjAdd(PSHCLTRANSFER pTransfer, SHCLOBJHANDLE hObj, uint32_t cbDelta,
                               uint64_t *pcbProcessed, uint64_t *pcbTotal, bool *pfNotify)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertReturn(hObj != NIL_SHCLOBJHANDLE, VERR_INVALID_HANDLE);
    AssertReturn(cbDelta, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbProcessed, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbTotal, VERR_INVALID_POINTER);
    AssertPtrReturn(pfNotify, VERR_INVALID_POINTER);

    *pcbProcessed = 0;
    *pcbTotal     = 0;
    *pfNotify     = false;

    shClTransferLock(pTransfer);

    int rc = VERR_NOT_FOUND;
    PSHCLTRANSFEROBJ const pObj = ShClTransferObjGet(pTransfer, hObj);
    if (   pObj
        && pObj->pProgress)
    {
        if (cbDelta > UINT64_MAX - pObj->State.cbProcessed)
            rc = VERR_OUT_OF_RANGE;
        else
        {
            uint64_t const cbObjProcessed = pObj->State.cbProcessed + cbDelta;
            if (cbObjProcessed <= pObj->pProgress->cbProcessed)
            {
                pObj->State.cbProcessed = cbObjProcessed;
                if (pTransfer->Progress.fTotalKnown)
                {
                    *pcbProcessed = pTransfer->Progress.cbProcessed;
                    *pcbTotal     = pTransfer->Progress.cbTotal;
                    rc = VINF_SUCCESS;
                }
                else
                    rc = VERR_NOT_AVAILABLE;
            }
            else
            {
                uint64_t const cbUnique = cbObjProcessed - pObj->pProgress->cbProcessed;
                Assert(cbUnique <= cbDelta);
                rc = shClTransferProgressAddLocked(pTransfer, (uint32_t)cbUnique,
                                                   pcbProcessed, pcbTotal, pfNotify);
                if (   RT_SUCCESS(rc)
                    || rc == VERR_NOT_AVAILABLE)
                {
                    pObj->State.cbProcessed = cbObjProcessed;
                    pObj->pProgress->cbProcessed = cbObjProcessed;
                }
            }
        }
    }

    shClTransferUnlock(pTransfer);
    return rc;
}

/**
 * Acquires a reference to this transfer.
 *
 * @returns New reference count.
 * @param   pTransfer           Transfer to acquire reference for.
 */
uint32_t ShClTransferAcquire(PSHCLTRANSFER pTransfer)
{
    shClTransferLock(pTransfer);

    uint32_t const cRefs = ASMAtomicReadU32(&pTransfer->cRefs);
    AssertRelease(cRefs < UINT32_MAX);

    if (cRefs == 0)
    {
        int rc = RTSemEventMultiReset(pTransfer->hNoRefsEvent);
        AssertFatalMsgRC(rc, ("Resetting the transfer reference event failed with %Rrc\n", rc));
    }

    uint32_t const cRefsNew = ASMAtomicIncU32(&pTransfer->cRefs);

    shClTransferUnlock(pTransfer);
    return cRefsNew;
}

/**
 * Releases a reference to this transfer.
 *
 * @returns New reference count.
 * @param   pTransfer           Transfer to release reference for.
 */
uint32_t ShClTransferRelease(PSHCLTRANSFER pTransfer)
{
    shClTransferLock(pTransfer);

    AssertRelease(ASMAtomicReadU32(&pTransfer->cRefs) > 0);
    uint32_t const cRefs = ASMAtomicDecU32(&pTransfer->cRefs);
    if (cRefs == 0)
    {
        int rc = RTSemEventMultiSignal(pTransfer->hNoRefsEvent);
        AssertFatalMsgRC(rc, ("Signalling the transfer reference event failed with %Rrc\n", rc));
    }

    shClTransferUnlock(pTransfer);
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

    *phList = NIL_SHCLLISTHANDLE;

    if (pOpenParms->fList & ~VBOX_SHCL_LIST_F_VALID_MASK)
        return VERR_INVALID_FLAGS;

    if (pOpenParms->pszPath)
    {
        int rc2 = ShClTransferValidatePath(pOpenParms->pszPath, false /* fMustExist */);
        if (RT_FAILURE(rc2))
            return rc2;
        if (!shClTransferPathIsRelative(pOpenParms->pszPath))
            return VERR_PATH_IS_NOT_RELATIVE;
    }

    if (   pOpenParms->pszFilter
        && !RTStrIsValidEncoding(pOpenParms->pszFilter))
        return VERR_INVALID_UTF8_ENCODING;

    if (pTransfer->cListHandles >= pTransfer->cMaxListHandles)
        return VERR_SHCLPB_MAX_LISTS_REACHED;

    int rc;
    if (pTransfer->ProviderIface.pfnListOpen)
        rc = pTransfer->ProviderIface.pfnListOpen(&pTransfer->ProviderCtx, pOpenParms, phList);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Opening list '%.*s' (filter '%.*s') for transfer %RU16/%RU64 in session %RU16 (flags %#x) failed with %Rrc\n",
                       128, pOpenParms->pszPath ? pOpenParms->pszPath : "",
                       128, pOpenParms->pszFilter ? pOpenParms->pszFilter : "",
                       ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.Key.uGeneration,
                       ShClTransferKeyGetSessionId(&pTransfer->State.Key), pOpenParms->fList, rc));

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
        LogRelMax(16, ("Shared Clipboard: Closing list %RU64 for transfer %RU16/%RU64 in session %RU16 failed with %Rrc\n",
                       hList, ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.Key.uGeneration,
                       ShClTransferKeyGetSessionId(&pTransfer->State.Key), rc));

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

    if (   RT_SUCCESS(rc)
        && !ShClTransferListHdrIsValid(pHdr))
        rc = VERR_INVALID_PARAMETER;

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Reading header of list %RU64 for transfer %RU16/%RU64 in session %RU16 failed with %Rrc\n",
                       hList, ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.Key.uGeneration,
                       ShClTransferKeyGetSessionId(&pTransfer->State.Key), rc));

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

    if (   RT_FAILURE(rc)
        && rc != VERR_NO_MORE_FILES)
        LogRelMax(16, ("Shared Clipboard: Reading entry from list %RU64 for transfer %RU16/%RU64 in session %RU16 failed with %Rrc\n",
                       hList, ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.Key.uGeneration,
                       ShClTransferKeyGetSessionId(&pTransfer->State.Key), rc));

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
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(pEntry,    VERR_INVALID_POINTER);
    AssertReturn   (hList != NIL_SHCLLISTHANDLE, VERR_INVALID_HANDLE);

    int rc;
    if (pTransfer->ProviderIface.pfnListEntryWrite)
        rc = pTransfer->ProviderIface.pfnListEntryWrite(&pTransfer->ProviderCtx, hList, pEntry);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Writing entry to list %RU64 for transfer %RU16/%RU64 in session %RU16 failed with %Rrc\n",
                       hList, ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.Key.uGeneration,
                       ShClTransferKeyGetSessionId(&pTransfer->State.Key), rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
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
    AssertReturn(ShClTransferStatusIsValid(enmStatus), VERR_INVALID_PARAMETER);
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

    PSHCLTRANSFEROBJ pObj;
    RTListForEach(&pTransfer->lstObj, pObj, SHCLTRANSFEROBJ, Node)
        pObj->pProgress = NULL;

    PSHCLTRANSFERPROGRESSOBJ pProgress, pProgressNext;
    RTListForEachSafe(&pTransfer->lstProgressObj, pProgress, pProgressNext, SHCLTRANSFERPROGRESSOBJ, Node)
    {
        RTListNodeRemove(&pProgress->Node);
        RTStrFree(pProgress->pszPath);
        RTMemFree(pProgress);
    }
    RT_ZERO(pTransfer->Progress);
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

    pTransfer->cListHandles   = 0;
    pTransfer->uListHandleNext = 0;
    pTransfer->cObjHandles    = 0;
    pTransfer->uObjHandleNext = 0;

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

    if (RT_SUCCESS(rc))
        ShClTransferProgressSetTotalFromRoots(pTransfer);

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

    int rc = RTStrValidateEncodingEx(pszRoots, cbRoots,
                                     RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
    if (RT_FAILURE(rc))
        return rc;

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
                    if (pTransfer->State.enmSource == SHCLSOURCE_LOCAL)
                        rc = ShClFsObjInfoQueryLocal(pszPathCur, pFsObjInfo);
                    if (   RT_SUCCESS(rc)
                        && pTransfer->State.enmSource == SHCLSOURCE_LOCAL)
                    {
                        if (   !RTFS_IS_DIRECTORY(pFsObjInfo->Attr.fMode)
                            && !RTFS_IS_FILE(pFsObjInfo->Attr.fMode))
                        {
                            LogRelMax(16, ("Shared Clipboard: Root path '%.*s' for transfer %RU16/%RU64 in session %RU16 is not a regular file or directory (%#x)\n",
                                           128, pszPathCur, ShClTransferKeyGetTransferId(&pTransfer->State.Key),
                                           pTransfer->State.Key.uGeneration,
                                           ShClTransferKeyGetSessionId(&pTransfer->State.Key), pFsObjInfo->Attr.fMode));
                            rc = VERR_NOT_SUPPORTED;
                        }
                    }

                    if (RT_SUCCESS(rc))
                    {
                        if (!shClTransferPathIsBelowRootAbs(pszPathCur, pszPathRootAbs))
                        {
                            LogRelMax(16, ("Shared Clipboard: Path '%.*s' does not start with transfer root '%.*s'\n", 128, pszPathCur, 128, pszPathRootAbs));
                            rc = VERR_PATH_DOES_NOT_START_WITH_ROOT;
                        }
                    }

                    if (RT_SUCCESS(rc))
                    {
                        /* Calculate the relative path within the root path. */
                        Assert(RTStrNLen(pszPathCur, RTPATH_MAX) >= cchPathRootAbs); /* Sanity. */
                        /* RTPathStripFilename leaves filesystem roots slash-terminated. */
                        const char *pszPathRelToRoot = pszPathCur + cchPathRootAbs;
                        if (   cchPathRootAbs
                            && !RTPATH_IS_SLASH(pszPathRootAbs[cchPathRootAbs - 1])
                            && RTPATH_IS_SLASH(*pszPathRelToRoot))
                            pszPathRelToRoot++;
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
                            LogRelMax(16, ("Shared Clipboard: Unable to construct a relative path for '%.*s' under transfer root '%.*s'\n", 128, pszPathCur, 128, pszPathRootAbs));
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
        LogRelMax(16, ("Shared Clipboard: Setting roots for transfer %RU16/%RU64 in session %RU16 failed with %Rrc\n",
                       ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.Key.uGeneration,
                       ShClTransferKeyGetSessionId(&pTransfer->State.Key), rc));
        ShClTransferListDestroy(pLstRoots);
        RTStrFree(pszPathRootAbs);
    }

    shClTransferUnlock(pTransfer);

    if (RT_SUCCESS(rc))
        ShClTransferProgressSetTotalFromRoots(pTransfer);

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

    SHCLTRANSFERID const uID = ShClTransferKeyGetTransferId(&pTransfer->State.Key);

    shClTransferUnlock(pTransfer);

    return uID;
}

/**
 * Returns the clipboard transfer's owning service session ID.
 *
 * @returns The transfer's service session ID.
 * @param   pTransfer           Clipboard transfer to return session ID for.
 */
SHCLSESSIONID ShClTransferGetSessionId(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, NIL_SHCLSESSIONID);

    shClTransferLock(pTransfer);

    SHCLSESSIONID const idSession = ShClTransferKeyGetSessionId(&pTransfer->State.Key);

    shClTransferUnlock(pTransfer);

    return idSession;
}

/**
 * Returns the clipboard transfer's host-private generation.
 *
 * @returns The transfer's host-private generation.
 * @param   pTransfer           Clipboard transfer to return generation for.
 */
SHCLTRANSFERGEN ShClTransferGetGeneration(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pTransfer, NIL_SHCLTRANSFERGEN);

    shClTransferLock(pTransfer);

    SHCLTRANSFERGEN const uGeneration = pTransfer->State.Key.uGeneration;

    shClTransferUnlock(pTransfer);

    return uGeneration;
}


/**
 * Returns the clipboard transfer's host-side identity.
 *
 * @param   pTransfer       Clipboard transfer to inspect.
 * @param   pKey            Where to store the transfer key.
 */
void ShClTransferGetKey(PSHCLTRANSFER pTransfer, PSHCLTRANSFERKEY pKey)
{
    AssertPtrReturnVoid(pTransfer);
    AssertPtrReturnVoid(pKey);

    shClTransferLock(pTransfer);
    *pKey = pTransfer->State.Key;
    shClTransferUnlock(pTransfer);
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
    bool fCallCallback = false;
    bool fSignalEvents = false;

    if (   pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_NONE
        || pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_REQUESTED
        || pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_INITIALIZED
        || pTransfer->State.enmStatus == SHCLTRANSFERSTATUS_STARTED)
    {
        if (rc == VERR_CANCELLED)
            rc2 = shClTransferSetStatus(pTransfer, SHCLTRANSFERSTATUS_CANCELED);
        else
            rc2 = shClTransferSetStatus(pTransfer, SHCLTRANSFERSTATUS_ERROR);
        fCallCallback = true;
        fSignalEvents = RT_SUCCESS(rc2);
    }
    else /* Nothing to do. */
        rc2 = VINF_SUCCESS;

    shClTransferUnlock(pTransfer);

    if (fSignalEvents)
    {
        int const rc3 = ShClEventSourceSignalAll(&pTransfer->Events, rc);
        AssertRC(rc3);
    }

    if (fCallCallback)
    {
        if (rc == VERR_CANCELLED)
        {
            if (pTransfer->Callbacks.pfnOnCompleted)
                pTransfer->Callbacks.pfnOnCompleted(&pTransfer->CallbackCtx, VERR_CANCELLED);
        }
        else if (pTransfer->Callbacks.pfnOnError)
            pTransfer->Callbacks.pfnOnError(&pTransfer->CallbackCtx, rc);
    }

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
    AssertReturn(RT_FAILURE(rc), VERR_INVALID_PARAMETER);
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

    shClTransferUnlock(pTransfer);

    RTThreadUserSignal(RTThreadSelf());

    int rc = Ctx.pfnThread(pTransfer, Ctx.pvUser);

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

    /* A handle is published before the worker can set fStarted.  Treat the
     * handle as authoritative so teardown cannot miss a starting worker. */
    if (pTransfer->Thread.hThread == NIL_RTTHREAD)
    {
        shClTransferUnlock(pTransfer);
        return VINF_SUCCESS;
    }
    pTransfer->Thread.fStop = true;

    LogFlowFuncEnter();

    /* Snapshot the waitable handle. A finite-timeout failure leaves it intact
     * for the consuming indefinite retry. */
    RTTHREAD const hThread = pTransfer->Thread.hThread;

    shClTransferUnlock(pTransfer); /* Leave lock while waiting. */

    int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
    Assert(hThread != NIL_RTTHREAD);
    int rc = RTThreadWait(hThread, uTimeoutMs, &rcThread);

    LogFlowFunc(("Waiting for thread resulted in %Rrc (thread exited with %Rrc)\n", rc, rcThread));

    if (RT_SUCCESS(rc))
    {
        shClTransferLock(pTransfer);
        pTransfer->Thread.fStarted = false;
        if (pTransfer->Thread.hThread == hThread)
            pTransfer->Thread.hThread = NIL_RTTHREAD;
        shClTransferUnlock(pTransfer);

        rc = rcThread; /* Return the thread rc to the caller. */
    }
    else
        LogRelMax(16, ("Shared Clipboard: Waiting for worker of transfer %RU16/%RU64 in session %RU16 failed with %Rrc\n",
                       ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.Key.uGeneration,
                       ShClTransferKeyGetSessionId(&pTransfer->State.Key), rc));

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

        uint64_t const msElapsed = RTTimeMilliTS() - tsStartMs;
        msLeft -= RT_MIN(msLeft, msElapsed);
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
        RTListInit(&pTransferCtx->List);

        pTransferCtx->cTransfers       = 0;
        pTransferCtx->cRunning         = 0;
        pTransferCtx->cMaxRunning      = 64; /** @todo Make this configurable? */
        pTransferCtx->cTransferIdsUsed = 0;
        pTransferCtx->idSession        = NIL_SHCLSESSIONID;
        pTransferCtx->uNextGeneration  = 1;

        RT_ZERO(pTransferCtx->bmTransferIds);
        RT_ZERO(pTransferCtx->bmTransferIdsUsed);

        ShClTransferCtxReset(pTransferCtx);
    }

    return rc;
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

    RTLISTANCHOR ListDestroy;
    RTListInit(&ListDestroy);

    shClTransferCtxLock(pTransferCtx);

    PSHCLTRANSFER pTransfer, pTransferNext;
    RTListForEachSafe(&pTransferCtx->List, pTransfer, pTransferNext, SHCLTRANSFER, Node)
    {
        shclTransferCtxTransferRemoveLocked(pTransferCtx, pTransfer);
        RTListAppend(&ListDestroy, &pTransfer->Node);
    }

    pTransferCtx->cRunning   = 0;
    pTransferCtx->cTransfers = 0;

    shClTransferCtxUnlock(pTransferCtx);

    RTListForEachSafe(&ListDestroy, pTransfer, pTransferNext, SHCLTRANSFER, Node)
    {
        RTListNodeRemove(&pTransfer->Node);
        shclTransferCtxTransferNotifyUnregistered(pTransferCtx, pTransfer);
        shClTransferDestroyConsume(pTransfer);
    }

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
 * Starts a new service session for a transfer context.
 *
 * @returns VBox status code.
 * @param   pTransferCtx        Transfer context to bind to idSession.
 * @param   idSession           Service session ID to bind.
 */
int ShClTransferCtxBeginSession(PSHCLTRANSFERCTX pTransferCtx, SHCLSESSIONID idSession)
{
    AssertPtrReturn(pTransferCtx, VERR_INVALID_POINTER);
    AssertReturn(idSession != 0 && idSession != NIL_SHCLSESSIONID, VERR_INVALID_PARAMETER);

    shClTransferCtxLock(pTransferCtx);

    int rc;
    if (pTransferCtx->cTransfers == 0)
    {
        RT_ZERO(pTransferCtx->bmTransferIds);
        RT_ZERO(pTransferCtx->bmTransferIdsUsed);
        pTransferCtx->idSession        = idSession;
        pTransferCtx->cTransferIdsUsed = 0;
        pTransferCtx->cRunning         = 0;
        pTransferCtx->cTransfers       = 0;
        if (   pTransferCtx->uNextGeneration == 0
            || pTransferCtx->uNextGeneration == NIL_SHCLTRANSFERGEN)
            pTransferCtx->uNextGeneration = 1;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_WRONG_ORDER;

    shClTransferCtxUnlock(pTransferCtx);

    LogFlowFuncLeaveRC(rc);
    return rc;
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
        if (ShClTransferKeyGetTransferId(&pTransfer->State.Key) == idTransfer)
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
 * Returns and retains a clipboard transfer for a specific transfer ID.
 *
 * @returns Retained clipboard transfer, or NULL if not found.
 * @param   pTransferCtx        Transfer context to return transfer for.
 * @param   uID                 ID of the transfer to return.
 *
 * @note    The caller must release a returned transfer with ShClTransferRelease().
 */
PSHCLTRANSFER ShClTransferCtxGetTransferByIdRetained(PSHCLTRANSFERCTX pTransferCtx, uint32_t uID)
{
    AssertPtrReturn(pTransferCtx, NULL);

    shClTransferCtxLock(pTransferCtx);

    PSHCLTRANSFER pTransfer = shClTransferCtxGetTransferByIdInternal(pTransferCtx, uID);
    if (pTransfer)
        ShClTransferAcquire(pTransfer);

    shClTransferCtxUnlock(pTransferCtx);
    return pTransfer;
}

/**
 * Returns a clipboard transfer for a specific host-side transfer key.
 *
 * @returns Clipboard transfer found, or NULL if not found or the key does not match.
 * @param   pTransferCtx        Transfer context to return transfer for.
 * @param   pKey                Host-side transfer key to match.
 */
PSHCLTRANSFER ShClTransferCtxGetTransferByKey(PSHCLTRANSFERCTX pTransferCtx, PCSHCLTRANSFERKEY pKey)
{
    AssertPtrReturn(pTransferCtx, NULL);
    AssertReturn(ShClTransferKeyIsValid(pKey), NULL);

    SHCLSESSIONID const idSession = ShClTransferKeyGetSessionId(pKey);
    SHCLTRANSFERID const idTransfer = ShClTransferKeyGetTransferId(pKey);

    shClTransferCtxLock(pTransferCtx);

    PSHCLTRANSFER pTransfer = NULL;
    if (pTransferCtx->idSession == idSession)
    {
        pTransfer = shClTransferCtxGetTransferByIdInternal(pTransferCtx, idTransfer);
        if (pTransfer)
        {
            PSHCLTRANSFER const pTransferToUnlock = pTransfer;
            shClTransferLock(pTransferToUnlock);
            if (!ShClTransferKeyIsEqual(&pTransferToUnlock->State.Key, pKey))
                pTransfer = NULL;
            shClTransferUnlock(pTransferToUnlock);
        }
    }

    shClTransferCtxUnlock(pTransferCtx);

    return pTransfer;
}

/**
 * Returns and retains a clipboard transfer for a host-side transfer key.
 *
 * @returns Retained clipboard transfer, or NULL if not found or the key does not match.
 * @param   pTransferCtx        Transfer context to return transfer for.
 * @param   pKey                Host-side transfer key to match.
 *
 * @note    The caller must release a returned transfer with ShClTransferRelease().
 */
PSHCLTRANSFER ShClTransferCtxGetTransferByKeyRetained(PSHCLTRANSFERCTX pTransferCtx, PCSHCLTRANSFERKEY pKey)
{
    AssertPtrReturn(pTransferCtx, NULL);
    AssertReturn(ShClTransferKeyIsValid(pKey), NULL);

    SHCLSESSIONID const idSession = ShClTransferKeyGetSessionId(pKey);
    SHCLTRANSFERID const idTransfer = ShClTransferKeyGetTransferId(pKey);

    shClTransferCtxLock(pTransferCtx);

    PSHCLTRANSFER pTransfer = NULL;
    if (pTransferCtx->idSession == idSession)
    {
        pTransfer = shClTransferCtxGetTransferByIdInternal(pTransferCtx, idTransfer);
        if (pTransfer)
        {
            PSHCLTRANSFER const pTransferToUnlock = pTransfer;
            shClTransferLock(pTransferToUnlock);
            if (!ShClTransferKeyIsEqual(&pTransferToUnlock->State.Key, pKey))
                pTransfer = NULL;
            else
                ShClTransferAcquire(pTransferToUnlock);
            shClTransferUnlock(pTransferToUnlock);
        }
    }

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

/**
 * Creates the next non-reserved transfer generation for a locked transfer context.
 *
 * @returns Transfer generation.
 * @param   pTransferCtx        Transfer context to update.
 */
static SHCLTRANSFERGEN shClTransferCtxCreateGenerationInternal(PSHCLTRANSFERCTX pTransferCtx)
{
    Assert(RTCritSectIsOwner(&pTransferCtx->CritSect));

    if (   pTransferCtx->uNextGeneration == 0
        || pTransferCtx->uNextGeneration == NIL_SHCLTRANSFERGEN)
        pTransferCtx->uNextGeneration = 1;

    SHCLTRANSFERGEN const uGeneration = pTransferCtx->uNextGeneration++;
    if (   pTransferCtx->uNextGeneration == 0
        || pTransferCtx->uNextGeneration == NIL_SHCLTRANSFERGEN)
        pTransferCtx->uNextGeneration = 1;

    return uGeneration;
}


/**
 * Allocates a context-local transfer ID for a locked transfer context.
 *
 * @returns VBox status code.
 * @param   pTransferCtx        Transfer context to allocate from.
 * @param   pidTransfer         Where to return the allocated transfer ID.
 */
static int shClTransferCreateIDInternal(PSHCLTRANSFERCTX pTransferCtx, SHCLTRANSFERID *pidTransfer)
{
    Assert(RTCritSectIsOwner(&pTransferCtx->CritSect));

    if (pTransferCtx->cTransferIdsUsed >= VBOX_SHCL_MAX_TRANSFERS - 2 /* First and last are not used */)
    {
        LogFunc(("Maximum number of transfer IDs consumed in this session (%RU16 IDs)\n", pTransferCtx->cTransferIdsUsed));
        return VERR_SHCLPB_MAX_TRANSFERS_REACHED;
    }

    /*
     * Pick a random bit as starting point.  If it's in use, search forward,
     * wrapping around.  We've reserved both the zero'th and max-1 IDs.
     */
    SHCLTRANSFERID idTransfer = RTRandU32Ex(1, VBOX_SHCL_MAX_TRANSFERS - 2);
    for (uint32_t i = 0; i < VBOX_SHCL_MAX_TRANSFERS - 2; i++)
    {
        if (   !ASMBitTest(&pTransferCtx->bmTransferIdsUsed[0], idTransfer)
            && !ASMBitTest(&pTransferCtx->bmTransferIds[0], idTransfer))
        {
            ASMBitSet(&pTransferCtx->bmTransferIds[0], idTransfer);
            ASMBitSet(&pTransferCtx->bmTransferIdsUsed[0], idTransfer);
            pTransferCtx->cTransferIdsUsed++;
            *pidTransfer = idTransfer;
            return VINF_SUCCESS;
        }

        idTransfer++;
        if (!ShClTransferIdIsValid(idTransfer))
            idTransfer = 1;
    }

    LogFunc(("Maximum number of transfers reached (%RU16 IDs consumed)\n", pTransferCtx->cTransferIdsUsed));
    return VERR_SHCLPB_MAX_TRANSFERS_REACHED;
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
    Assert(RTCritSectIsOwner(&pTransferCtx->CritSect));
    Assert(ShClTransferIdIsValid(idTransfer));

    shClTransferLock(pTransfer);
    ShClTransferKeyInit(&pTransfer->State.Key, pTransferCtx->idSession, idTransfer,
                        shClTransferCtxCreateGenerationInternal(pTransferCtx));
    pTransfer->pOwnerCtx = pTransferCtx;
    shClTransferUnlock(pTransfer);

    RTListAppend(&pTransferCtx->List, &pTransfer->Node);

    pTransferCtx->cTransfers++;

    int rc = VINF_SUCCESS;

    Log2Func(("pTransfer=%p, idTransfer=%RU32, idSession=%RU16, uGeneration=%RU64 -- now %RU16 transfer(s)\n",
              pTransfer, idTransfer, pTransferCtx->idSession, pTransfer->State.Key.uGeneration, pTransferCtx->cTransfers));

    shClTransferCtxUnlock(pTransferCtx);

    if (pTransfer->Callbacks.pfnOnRegistered)
        pTransfer->Callbacks.pfnOnRegistered(&pTransfer->CallbackCtx, pTransferCtx);

    shClTransferCtxLock(pTransferCtx);

    LogFlowFuncLeaveRC(rc);
    return rc;
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

    SHCLTRANSFERID idTransfer = NIL_SHCLTRANSFERID; /* Shut up MSVC. */

    int rc;
    if (ShClTransferIdIsValid(ShClTransferKeyGetTransferId(&pTransfer->State.Key)))
        rc = VERR_ALREADY_EXISTS;
    else
        rc = shClTransferCreateIDInternal(pTransferCtx, &idTransfer);
    if (RT_SUCCESS(rc))
    {
        rc = shClTransferCtxTransferRegisterExInternal(pTransferCtx, pTransfer, idTransfer);
        if (RT_SUCCESS(rc) && pidTransfer)
            *pidTransfer = idTransfer;
    }

    shClTransferCtxUnlock(pTransferCtx);

    return rc;
}

/**
 * Registers a clipboard transfer with a transfer context by specifying an ID for the transfer.
 *
 * @return  VBox status code.
 * @retval  VERR_ALREADY_EXISTS if a transfer with the given ID already exists or was already consumed in this session.
 * @retval  VERR_SHCLPB_MAX_TRANSFERS_REACHED if the maximum of concurrent transfers for this context has been reached.
 * @param   pTransferCtx        Transfer context to register transfer to.
 * @param   pTransfer           Transfer to register.
 * @param   idTransfer          Transfer ID to use for registration.
 */
int ShClTransferCtxRegisterById(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer, SHCLTRANSFERID idTransfer)
{
    AssertPtrReturn(pTransferCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertReturn(ShClTransferIdIsValid(idTransfer), VERR_INVALID_PARAMETER);

    shClTransferCtxLock(pTransferCtx);

    int rc;
    if (ShClTransferIdIsValid(ShClTransferKeyGetTransferId(&pTransfer->State.Key)))
        rc = VERR_ALREADY_EXISTS;
    else if (pTransferCtx->cTransfers < VBOX_SHCL_MAX_TRANSFERS - 2 /* First and last are not used */)
    {
        PSHCLTRANSFER const pExisting = shClTransferCtxGetTransferByIdInternal(pTransferCtx, idTransfer);
        bool const fLive = ASMBitTest(&pTransferCtx->bmTransferIds[0], idTransfer);
        bool const fUsed = ASMBitTest(&pTransferCtx->bmTransferIdsUsed[0], idTransfer);

        if (pExisting)
            rc = VERR_ALREADY_EXISTS;
        else if (fUsed && !fLive)
            rc = VERR_ALREADY_EXISTS;
        else if (!fUsed && fLive)
            rc = VERR_INTERNAL_ERROR_2;
        else
        {
            if (!fUsed)
            {
                ASMBitSet(&pTransferCtx->bmTransferIds[0], idTransfer);
                ASMBitSet(&pTransferCtx->bmTransferIdsUsed[0], idTransfer);
                pTransferCtx->cTransferIdsUsed++;
            }

            rc = shClTransferCtxTransferRegisterExInternal(pTransferCtx, pTransfer, idTransfer);
        }
    }
    else
    {
        LogFunc(("Maximum number of transfers reached (%RU16 transfers)\n", pTransferCtx->cTransfers));
        rc = VERR_SHCLPB_MAX_TRANSFERS_REACHED;
    }

    shClTransferCtxUnlock(pTransferCtx);

    return rc;
}

/**
 * Removes a transfer from a transfer context.
 *
 * @param   pTransferCtx        Transfer context to remove transfer from.
 * @param   pTransfer           Transfer to remove.
 *
 * @note    Caller needs to take critical section.
 */
static void shclTransferCtxTransferRemoveLocked(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer)
{
    Assert(RTCritSectIsOwner(&pTransferCtx->CritSect));

    SHCLTRANSFERID const idTransfer = ShClTransferGetID(pTransfer);
    if (ShClTransferIdIsValid(idTransfer))
        ASMBitClear(&pTransferCtx->bmTransferIds[0], idTransfer);

    RTListNodeRemove(&pTransfer->Node);

    Assert(pTransferCtx->cTransfers);
    pTransferCtx->cTransfers--;

    Assert(pTransferCtx->cTransfers >= pTransferCtx->cRunning);

    LogFlowFunc(("Now %RU32 transfers left\n", pTransferCtx->cTransfers));
}

/**
 * Notifies a transfer that it was removed from a transfer context.
 *
 * @param   pTransferCtx        Transfer context the transfer was removed from.
 * @param   pTransfer           Transfer that was removed.
 *
 * @note    No owner or transfer-context lock may be held by the caller.
 */
static void shclTransferCtxTransferNotifyUnregistered(PSHCLTRANSFERCTX pTransferCtx, PSHCLTRANSFER pTransfer)
{
    Assert(!RTCritSectIsOwner(&pTransferCtx->CritSect));
    Assert(pTransfer->pOwnerCtx == pTransferCtx);

    if (pTransfer->Callbacks.pfnOnUnregistered)
        pTransfer->Callbacks.pfnOnUnregistered(&pTransfer->CallbackCtx, pTransferCtx);

    pTransfer->pOwnerCtx = NULL;
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

    shclTransferCtxTransferRemoveLocked(pTransferCtx, pTransfer);

    shClTransferCtxUnlock(pTransferCtx);

    shclTransferCtxTransferNotifyUnregistered(pTransferCtx, pTransfer);

    shClTransferCtxLock(pTransferCtx);
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
    AssertReturn(ShClTransferIdIsValid(idTransfer), VERR_INVALID_PARAMETER);

    shClTransferCtxLock(pTransferCtx);

    int rc = VINF_SUCCESS;

    LogFlowFunc(("idTransfer=%RU32\n", idTransfer));

    PSHCLTRANSFER pTransfer = shClTransferCtxGetTransferByIdInternal(pTransferCtx, idTransfer);
    if (pTransfer)
    {
        shclTransferCtxTransferRemoveAndUnregister(pTransferCtx, pTransfer);
    }
    else if (ASMBitTest(&pTransferCtx->bmTransferIds[0], idTransfer))
    {
        ASMBitClear(&pTransferCtx->bmTransferIds[0], idTransfer);
        rc = VERR_NOT_FOUND;
    }
    else
        rc = VERR_NOT_FOUND;

    shClTransferCtxUnlock(pTransferCtx);

    LogFlowFuncLeaveRC(rc);
    return rc;
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
    int rc = RTPathQueryInfoEx(pszPath, &objInfo,
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
                               RTFSOBJATTRADD_NOTHING,
#else
                               RTFSOBJATTRADD_UNIX,
#endif
                               RTPATH_F_ON_LINK);
    if (RT_SUCCESS(rc))
    {
        if (RTFS_IS_SYMLINK(objInfo.Attr.fMode))
            rc = VERR_IS_A_SYMLINK;
        else
            rc = ShClFsObjInfoFromIPRT(pObjInfo, &objInfo);
    }

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
 * Rules:
 * - An empty path is allowed.
 * - Dot components ("." or "..") are forbidden.
 * - If \a fMustExist is \c true, the path either has to be a file or a directory and must exist.
 * - Symbolic links are forbidden.
 *
 * @returns VBox status code.
 * @param   pcszPath            Zero-terminated path to validate.
 * @param   fMustExist          Whether the path to validate also must exist.
 */
int ShClTransferValidatePath(const char *pcszPath, bool fMustExist)
{
    AssertPtrReturn(pcszPath, VERR_INVALID_POINTER);

    size_t const cchPath = RTStrNLen(pcszPath, SHCLLISTENTRY_MAX_NAME);
    if (cchPath >= SHCLLISTENTRY_MAX_NAME)
        return VERR_INVALID_PARAMETER;

    return ShClTransferValidatePathEx(pcszPath, cchPath + 1, fMustExist);
}

/**
 * Validates whether a given path matches our set of rules or not, extended version.
 *
 * Validates the supplied size and requires the terminator to be the last byte.
 *
 * @returns VBox status code.
 * @param   pcszPath            Path to validate.
 * @param   cbPath              Size (in bytes) of @a pcszPath, including the terminator.
 *                              Must not exceed SHCLLISTENTRY_MAX_NAME.
 * @param   fMustExist          Whether the path to validate also must exist.
 */
int ShClTransferValidatePathEx(const char *pcszPath, size_t cbPath, bool fMustExist)
{
    if (!pcszPath)
        return VERR_INVALID_POINTER;

    if (   !cbPath
        || cbPath > SHCLLISTENTRY_MAX_NAME)
        return VERR_INVALID_PARAMETER;

    size_t const cchPath = RTStrNLen(pcszPath, cbPath);
    if (cchPath != cbPath - 1)
        return VERR_INVALID_PARAMETER;

    int rc = VINF_SUCCESS;

    if (!cchPath)
        return rc;

    char *pszSanitized = RTStrDupN(pcszPath, cchPath);
    if (!pszSanitized)
        return VERR_NO_MEMORY;

    rc = ShClPathSanitize(pszSanitized, cbPath);
    if (   RT_SUCCESS(rc)
        && shClTransferPathIsRelative(pcszPath)
        && strcmp(pszSanitized, pcszPath))
        rc = VERR_INVALID_PARAMETER;
    RTStrFree(pszSanitized);

    if (RT_SUCCESS(rc))
    {
        union
        {
            RTPATHSPLIT     Split;
            uint8_t         ab[RTPATH_MAX + sizeof(RTPATHSPLIT)];
        } u;

        rc = RTPathSplit(pcszPath, &u.Split, sizeof(u), RTPATH_STR_F_STYLE_HOST);
        if (RT_SUCCESS(rc))
        {
            if (u.Split.fProps & (RTPATH_PROP_DOT_REFS | RTPATH_PROP_DOTDOT_REFS | RTPATH_PROP_EXTRA_SLASHES))
                rc = VERR_INVALID_PARAMETER;
            else if (fMustExist)
            {
                RTFSOBJINFO objInfo;
                rc = RTPathQueryInfoEx(pcszPath, &objInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                if (RT_SUCCESS(rc))
                {
                    if (   !RTFS_IS_DIRECTORY(objInfo.Attr.fMode)
                        && !RTFS_IS_FILE(objInfo.Attr.fMode))
                    {
                        LogRelMax(16, ("Shared Clipboard: Path '%.*s' contains a symbolic link, junction or unsupported object type (%#x)\n", 128, pcszPath, objInfo.Attr.fMode));
                        rc = VERR_NOT_SUPPORTED;
                    }
                }
            }
        }
    }

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Validating path '%.*s' failed with %Rrc\n", 128, pcszPath, rc));

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

    *ppszResolved = NULL;

    int rc = ShClTransferValidatePath(pszPath, false /* fMustExist */);
    if (RT_SUCCESS(rc))
    {
        if (!shClTransferPathIsRelative(pszPath))
            rc = VERR_PATH_IS_NOT_RELATIVE;
    }

    if (RT_SUCCESS(rc))
    {
        rc = VERR_PATH_NOT_FOUND; /* Play safe by default. */

        PSHCLLISTENTRY pEntry;
        RTListForEach(&pTransfer->lstRoots.lstEntries, pEntry, SHCLLISTENTRY, Node)
        {
            LogFlowFunc(("\tpEntry->pszName=%s\n", pEntry->pszName));

            if (shClTransferPathMatchesRootEntry(pszPath, pEntry)) /* Case-sensitive! */
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
                rc = RTPathAbsEx(pTransfer->pszPathRootAbs, pszPathAbs,
                                 RTPATH_STR_F_STYLE_HOST | RTPATHABS_F_STOP_AT_BASE, szResolved, &cbResolved);

                RTStrFree(pszPathAbs);
                pszPathAbs = NULL;

                if (RT_SUCCESS(rc))
                {
                    if (!shClTransferPathIsBelowRootAbs(szResolved, pTransfer->pszPathRootAbs))
                        rc = VERR_PATH_DOES_NOT_START_WITH_ROOT;
                    else
                    {
                        LogRel2(("Shared Clipboard: Resolved: '%s' -> '%s'\n", pszPath, szResolved));

                        *ppszResolved = RTStrDup(szResolved);
                        if (!*ppszResolved)
                            rc = VERR_NO_MEMORY;
                    }
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Resolving absolute path for '%.*s' in transfer %RU16/%RU64 session %RU16 failed with %Rrc\n",
                       128, pszPath, ShClTransferKeyGetTransferId(&pTransfer->State.Key),
                       pTransfer->State.Key.uGeneration, ShClTransferKeyGetSessionId(&pTransfer->State.Key), rc));

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

#ifdef VBOX_WITH_SHARED_CLIPBOARD_HOST
/**
 * Waits for a reply from the guest and unwraps a signalled event error.
 *
 * ShClEventWaitEx() only initializes the event result after the event has been
 * signalled.  In particular, a timeout or an interrupted wait must retain its
 * own status instead of being replaced by an uninitialized event result.
 *
 * @returns VBox status code.
 * @param   pEvent              Event to wait for.
 * @param   uTimeoutMs          Maximum time to wait.
 * @param   ppPayload           Where to return the reply payload on success.
 */
static int shClSvcTransferGuestReplyWait(PSHCLEVENT pEvent, RTMSINTERVAL uTimeoutMs,
                                         PSHCLEVENTPAYLOAD *ppPayload)
{
    int rcEvent;
    int rc = ShClEventWaitEx(pEvent, uTimeoutMs, &rcEvent, ppPayload);
    if (rc == VERR_SHCLPB_EVENT_FAILED)
        rc = rcEvent;
    return rc;
}


/**
 * Reads a root list header from the guest, asynchronous version.
 *
 * @returns VBox status code.
 * @param   pClient             Client to read from.
 * @param   pTransfer           Transfer to read root list header for.
 * @param   ppEvent             Where to return the event to wait for.
 *                              Must be released by the caller with ShClEventRelease().
 */
static int ShClSvcTransferGHRootListReadHdrAsync(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, PSHCLEVENT *ppEvent)
{
    LogFlowFuncEnter();

    int rc;

    PSHCLCLIENTMSG pMsgHdr = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_TRANSFER_ROOT_LIST_HDR_READ,
                                                   VBOX_SHCL_CPARMS_ROOT_LIST_HDR_READ_REQ);
    if (pMsgHdr)
    {
        PSHCLEVENT pEvent;
        rc = ShClEventSourceGenerateAndRegisterEvent(&pTransfer->Events, &pEvent);
        if (RT_SUCCESS(rc))
        {
            HGCMSvcSetU64(&pMsgHdr->aParms[0], VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID,
                                                                        ShClTransferGetID(pTransfer), pEvent->idEvent));
            HGCMSvcSetU32(&pMsgHdr->aParms[1], 0 /* fRoots */);

            ShClSvcClientLock(pClient);

            ShClSvcClientMsgAdd(pClient, pMsgHdr, true /* fAppend */);
            rc = ShClSvcClientWakeup(pClient);

            ShClSvcClientUnlock(pClient);

            /* Remove event from list if caller did not request event handle or in case
             * of failure (in this case caller should not release event). */
            if (   RT_FAILURE(rc)
                || !ppEvent)
            {
                ShClEventRelease(pEvent);
                pEvent = NULL;
            }
            else
                *ppEvent = pEvent;
        }
        else
            ShClSvcClientMsgFree(pClient, pMsgHdr);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reads a root list header from the guest.
 *
 * @returns VBox status code.
 * @param   pClient             Client to read from.
 * @param   pTransfer           Transfer to read root list header for.
 * @param   pHdr                Where to store the root list header on succeess.
 */
int ShClSvcTransferGHRootListReadHdr(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, PSHCLLISTHDR pHdr)
{
    PSHCLEVENT pEvent;
    int rc = ShClSvcTransferGHRootListReadHdrAsync(pClient, pTransfer, &pEvent);
    if (RT_SUCCESS(rc))
    {
        PSHCLEVENTPAYLOAD pPayload;
        rc = shClSvcTransferGuestReplyWait(pEvent, pTransfer->uTimeoutMs, &pPayload);
        if (RT_SUCCESS(rc))
        {
            Assert(pPayload->cbData == sizeof(SHCLLISTHDR));

            memcpy(pHdr, (PSHCLLISTHDR)pPayload->pvData, sizeof(SHCLLISTHDR));

            LogFlowFunc(("cRoots=%RU32, fFeatures=0x%x\n", pHdr->cEntries, pHdr->fFeatures));

            ShClPayloadDestroy(pPayload);
        }
        ShClEventRelease(pEvent);
        pEvent = NULL;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reads a root list entry from the guest, asynchronous version.
 *
 * @returns VBox status code.
 * @param   pClient             Client to read from.
 * @param   pTransfer           Transfer to read root list header for.
 * @param   idxEntry            Index of entry to read.
 * @param   ppEvent             Where to return the event to wait for.
 *                              Must be released by the caller with ShClEventRelease().
 */
static int ShClSvcTransferGHRootListReadEntryAsync(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, uint64_t idxEntry,
                                                   PSHCLEVENT *ppEvent)
{
    LogFlowFuncEnter();

    PSHCLCLIENTMSG pMsgEntry = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_TRANSFER_ROOT_LIST_ENTRY_READ,
                                                     VBOX_SHCL_CPARMS_ROOT_LIST_ENTRY_READ_REQ);
    int rc;
    if (pMsgEntry)
    {
        PSHCLEVENT pEvent;
        rc = ShClEventSourceGenerateAndRegisterEvent(&pTransfer->Events, &pEvent);
        if (RT_SUCCESS(rc))
        {
            HGCMSvcSetU64(&pMsgEntry->aParms[0],
                          VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID, ShClTransferGetID(pTransfer), pEvent->idEvent));
            HGCMSvcSetU32(&pMsgEntry->aParms[1], 0 /* fFeatures */);
            HGCMSvcSetU64(&pMsgEntry->aParms[2], idxEntry /* uIndex */);

            ShClSvcClientLock(pClient);

            ShClSvcClientMsgAdd(pClient, pMsgEntry, true /* fAppend */);
            rc = ShClSvcClientWakeup(pClient);

            ShClSvcClientUnlock(pClient);

            /* Remove event from list if caller did not request event handle or in case
             * of failure (in this case caller should not release event). */
            if (   RT_FAILURE(rc)
                || !ppEvent)
            {
                ShClEventRelease(pEvent);
                pEvent = NULL;
            }
            else
                *ppEvent = pEvent;
        }
        else
            ShClSvcClientMsgFree(pClient, pMsgEntry);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeave();
    return rc;
}

/**
 * Reads a root list entry from the guest.
 *
 * @returns VBox status code.
 * @param   pClient             Client to read from.
 * @param   pTransfer           Transfer to read root list header for.
 * @param   idxEntry            Index of entry to read.
 * @param   ppListEntry         Where to return the allocated root list entry.
 */
int ShClSvcTransferGHRootListReadEntry(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, uint64_t idxEntry,
                                       PSHCLLISTENTRY *ppListEntry)
{
    AssertPtrReturn(ppListEntry, VERR_INVALID_POINTER);

    PSHCLEVENT pEvent;
    int rc = ShClSvcTransferGHRootListReadEntryAsync(pClient, pTransfer, idxEntry, &pEvent);
    if (RT_SUCCESS(rc))
    {
        PSHCLEVENTPAYLOAD pPayload;
        rc = shClSvcTransferGuestReplyWait(pEvent, pTransfer->uTimeoutMs, &pPayload);
        if (RT_SUCCESS(rc))
        {
            *ppListEntry = (PSHCLLISTENTRY)pPayload->pvData; /* ppLisEntry own pPayload-pvData now. */
        }

        ShClEventRelease(pEvent);
        pEvent = NULL;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/*********************************************************************************************************************************
*   Provider interface implementation                                                                                            *
*********************************************************************************************************************************/

/** @copydoc SHCLTXPROVIDERIFACE::pfnRootListRead */
DECLCALLBACK(int) ShClSvcTransferIfaceGHRootListRead(PSHCLTXPROVIDERCTX pCtx)
{
    LogFlowFuncEnter();

    PSHCLCLIENT pClient = (PSHCLCLIENT)pCtx->pvUser;
    AssertPtr(pClient);

    SHCLLISTHDR Hdr;
    int rc = ShClSvcTransferGHRootListReadHdr(pClient, pCtx->pTransfer, &Hdr);
    if (RT_SUCCESS(rc))
    {
        for (uint64_t i = 0; i < Hdr.cEntries; i++)
        {
            PSHCLLISTENTRY pEntry;
            rc = ShClSvcTransferGHRootListReadEntry(pClient, pCtx->pTransfer, i, &pEntry);
            if (RT_SUCCESS(rc))
                rc = ShClTransferListAddEntry(&pCtx->pTransfer->lstRoots, pEntry, true /* fAppend */);

            if (RT_FAILURE(rc))
                break;
        }
    }

    LogFlowFuncLeave();
    return rc;
}

/**
 * Sets a transfer list open request to HGCM service parameters.
 *
 * @returns VBox status code.
 * @param   cParms              Number of HGCM parameters supplied in \a aParms.
 * @param   aParms              Array of HGCM parameters.
 * @param   idCtx               Context ID to use.
 * @param   pOpenParms          List open parameters to set.
 */
static int shClSvcTransferMsgSetListOpen(uint32_t cParms, VBOXHGCMSVCPARM aParms[],
                                         uint64_t idCtx, PSHCLLISTOPENPARMS pOpenParms)
{
    int rc;

    if (cParms == VBOX_SHCL_CPARMS_LIST_OPEN)
    {
        HGCMSvcSetU64(&aParms[0], idCtx);
        HGCMSvcSetU32(&aParms[1], pOpenParms->fList);
        HGCMSvcSetPv (&aParms[2], pOpenParms->pszFilter, pOpenParms->cbFilter);
        HGCMSvcSetPv (&aParms[3], pOpenParms->pszPath, pOpenParms->cbPath);
        HGCMSvcSetU64(&aParms[4], 0); /* OUT: uHandle */

        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListOpen */
DECLCALLBACK(int) ShClSvcTransferIfaceGHListOpen(PSHCLTXPROVIDERCTX pCtx,
                                                 PSHCLLISTOPENPARMS pOpenParms, PSHCLLISTHANDLE phList)
{
    LogFlowFuncEnter();

    PSHCLCLIENT pClient = (PSHCLCLIENT)pCtx->pvUser;
    AssertPtr(pClient);

    int rc;

    PSHCLCLIENTMSG pMsg = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_TRANSFER_LIST_OPEN,
                                                VBOX_SHCL_CPARMS_LIST_OPEN);
    if (pMsg)
    {
        PSHCLEVENT pEvent;
        rc = ShClEventSourceGenerateAndRegisterEvent(&pCtx->pTransfer->Events, &pEvent);
        if (RT_SUCCESS(rc))
        {
            pMsg->idCtx = VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID, ShClTransferKeyGetTransferId(&pCtx->pTransfer->State.Key),
                                                   pEvent->idEvent);

            rc = ShClTransferTransformPath(pOpenParms->pszPath, pOpenParms->cbPath);
            if (RT_SUCCESS(rc))
                rc = shClSvcTransferMsgSetListOpen(pMsg->cParms, pMsg->aParms, pMsg->idCtx, pOpenParms);
            if (RT_SUCCESS(rc))
            {
                ShClSvcClientLock(pClient);

                ShClSvcClientMsgAdd(pClient, pMsg, true /* fAppend */);
                rc = ShClSvcClientWakeup(pClient);

                ShClSvcClientUnlock(pClient);

                if (RT_SUCCESS(rc))
                {
                    PSHCLEVENTPAYLOAD pPayload;
                    rc = shClSvcTransferGuestReplyWait(pEvent, pCtx->pTransfer->uTimeoutMs, &pPayload);
                    if (RT_SUCCESS(rc))
                    {
                        Assert(pPayload->cbData == sizeof(SHCLREPLY));

                        PSHCLREPLY pReply = (PSHCLREPLY)pPayload->pvData;
                        AssertPtr(pReply);

                        Assert(pReply->uType == VBOX_SHCL_TX_REPLYMSGTYPE_LIST_OPEN);

                        LogFlowFunc(("hList=%RU64\n", pReply->u.ListOpen.uHandle));

                        *phList = pReply->u.ListOpen.uHandle;

                        ShClPayloadDestroy(pPayload);
                    }
                }
            }

            ShClEventRelease(pEvent);
        }
        else
            ShClSvcClientMsgFree(pClient, pMsg);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sets a transfer list close request to HGCM service parameters.
 *
 * @returns VBox status code.
 * @param   cParms              Number of HGCM parameters supplied in \a aParms.
 * @param   aParms              Array of HGCM parameters.
 * @param   idCtx               Context ID to use.
 * @param   hList               Handle of list to close.
 */
static int shClSvcTransferMsgSetListClose(uint32_t cParms, VBOXHGCMSVCPARM aParms[],
                                          uint64_t idCtx, SHCLLISTHANDLE hList)
{
    int rc;

    if (cParms == VBOX_SHCL_CPARMS_LIST_CLOSE)
    {
        HGCMSvcSetU64(&aParms[0], idCtx);
        HGCMSvcSetU64(&aParms[1], hList);

        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListClose */
DECLCALLBACK(int) ShClSvcTransferIfaceGHListClose(PSHCLTXPROVIDERCTX pCtx, SHCLLISTHANDLE hList)
{
    LogFlowFuncEnter();

    PSHCLCLIENT pClient = (PSHCLCLIENT)pCtx->pvUser;
    AssertPtr(pClient);

    int rc;

    PSHCLCLIENTMSG pMsg = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_TRANSFER_LIST_CLOSE,
                                                VBOX_SHCL_CPARMS_LIST_CLOSE);
    if (pMsg)
    {
        PSHCLEVENT pEvent;
        rc = ShClEventSourceGenerateAndRegisterEvent(&pCtx->pTransfer->Events, &pEvent);
        if (RT_SUCCESS(rc))
        {
            pMsg->idCtx = VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID, ShClTransferKeyGetTransferId(&pCtx->pTransfer->State.Key),
                                                   pEvent->idEvent);

            rc = shClSvcTransferMsgSetListClose(pMsg->cParms, pMsg->aParms, pMsg->idCtx, hList);
            if (RT_SUCCESS(rc))
            {
                ShClSvcClientLock(pClient);

                ShClSvcClientMsgAdd(pClient, pMsg, true /* fAppend */);
                rc = ShClSvcClientWakeup(pClient);

                ShClSvcClientUnlock(pClient);

                if (RT_SUCCESS(rc))
                {
                    PSHCLEVENTPAYLOAD pPayload;
                    rc = shClSvcTransferGuestReplyWait(pEvent, pCtx->pTransfer->uTimeoutMs, &pPayload);
                    if (RT_SUCCESS(rc))
                    {
                        ShClPayloadDestroy(pPayload);
                    }
                }
            }

            ShClEventRelease(pEvent);
        }
        else
            ShClSvcClientMsgFree(pClient, pMsg);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListHdrRead */
DECLCALLBACK(int) ShClSvcTransferIfaceGHListHdrRead(PSHCLTXPROVIDERCTX pCtx,
                                                    SHCLLISTHANDLE hList, PSHCLLISTHDR pListHdr)
{
    LogFlowFuncEnter();

    PSHCLCLIENT pClient = (PSHCLCLIENT)pCtx->pvUser;
    AssertPtr(pClient);

    int rc;

    PSHCLCLIENTMSG pMsg = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_TRANSFER_LIST_HDR_READ,
                                                VBOX_SHCL_CPARMS_LIST_HDR_READ_REQ);
    if (pMsg)
    {
        PSHCLEVENT pEvent;
        rc = ShClEventSourceGenerateAndRegisterEvent(&pCtx->pTransfer->Events, &pEvent);
        if (RT_SUCCESS(rc))
        {
            HGCMSvcSetU64(&pMsg->aParms[0], VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID,
                                                                     ShClTransferKeyGetTransferId(&pCtx->pTransfer->State.Key), pEvent->idEvent));
            HGCMSvcSetU64(&pMsg->aParms[1], hList);
            HGCMSvcSetU32(&pMsg->aParms[2], 0 /* fFlags */);

            ShClSvcClientLock(pClient);

            ShClSvcClientMsgAdd(pClient, pMsg, true /* fAppend */);
            rc = ShClSvcClientWakeup(pClient);

            ShClSvcClientUnlock(pClient);

            if (RT_SUCCESS(rc))
            {
                PSHCLEVENTPAYLOAD pPayload;
                rc = shClSvcTransferGuestReplyWait(pEvent, pCtx->pTransfer->uTimeoutMs, &pPayload);
                if (RT_SUCCESS(rc))
                {
                    Assert(pPayload->cbData == sizeof(SHCLLISTHDR));

                    *pListHdr = *(PSHCLLISTHDR)pPayload->pvData;

                    ShClPayloadDestroy(pPayload);
                }
            }

            ShClEventRelease(pEvent);
        }
        else
            ShClSvcClientMsgFree(pClient, pMsg);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListEntryRead */
DECLCALLBACK(int) ShClSvcTransferIfaceGHListEntryRead(PSHCLTXPROVIDERCTX pCtx,
                                                      SHCLLISTHANDLE hList, PSHCLLISTENTRY pListEntry)
{
    LogFlowFuncEnter();

    PSHCLCLIENT pClient = (PSHCLCLIENT)pCtx->pvUser;
    AssertPtr(pClient);

    int rc;

    PSHCLCLIENTMSG pMsg = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_TRANSFER_LIST_ENTRY_READ,
                                                VBOX_SHCL_CPARMS_LIST_ENTRY_READ);
    if (pMsg)
    {
        PSHCLEVENT pEvent;
        rc = ShClEventSourceGenerateAndRegisterEvent(&pCtx->pTransfer->Events, &pEvent);
        if (RT_SUCCESS(rc))
        {
            HGCMSvcSetU64(&pMsg->aParms[0], VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID,
                                                                     ShClTransferKeyGetTransferId(&pCtx->pTransfer->State.Key), pEvent->idEvent));
            HGCMSvcSetU64(&pMsg->aParms[1], hList);
            HGCMSvcSetU32(&pMsg->aParms[2], 0 /* fInfo */);

            ShClSvcClientLock(pClient);

            ShClSvcClientMsgAdd(pClient, pMsg, true /* fAppend */);
            rc = ShClSvcClientWakeup(pClient);

            ShClSvcClientUnlock(pClient);

            if (RT_SUCCESS(rc))
            {
                PSHCLEVENTPAYLOAD pPayload;
                rc = shClSvcTransferGuestReplyWait(pEvent, pCtx->pTransfer->uTimeoutMs, &pPayload);
                if (RT_SUCCESS(rc))
                {
                    Assert(pPayload->cbData == sizeof(SHCLLISTENTRY));

                    rc = ShClTransferListEntryCopy(pListEntry, (PSHCLLISTENTRY)pPayload->pvData);

                    ShClPayloadDestroy(pPayload);
                }
            }

            ShClEventRelease(pEvent);
        }
        else
            ShClSvcClientMsgFree(pClient, pMsg);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjOpen */
DECLCALLBACK(int) ShClSvcTransferIfaceGHObjOpen(PSHCLTXPROVIDERCTX pCtx, PSHCLOBJOPENCREATEPARMS pCreateParms, PSHCLOBJHANDLE phObj)
{
    LogFlowFuncEnter();

    PSHCLCLIENT pClient = (PSHCLCLIENT)pCtx->pvUser;
    AssertPtr(pClient);

    int rc;

    PSHCLCLIENTMSG pMsg = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_OPEN,
                                                VBOX_SHCL_CPARMS_OBJ_OPEN);
    if (pMsg)
    {
        PSHCLEVENT pEvent;
        rc = ShClEventSourceGenerateAndRegisterEvent(&pCtx->pTransfer->Events, &pEvent);
        if (RT_SUCCESS(rc))
        {
            LogFlowFunc(("pszPath=%s, fCreate=0x%x\n", pCreateParms->pszPath, pCreateParms->fCreate));

            rc = ShClTransferTransformPath(pCreateParms->pszPath, pCreateParms->cbPath);
            if (RT_SUCCESS(rc))
            {
                const uint32_t cbPath = (uint32_t)strlen(pCreateParms->pszPath) + 1; /* Include terminating zero */

                HGCMSvcSetU64(&pMsg->aParms[0], VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID,
                                                                         ShClTransferKeyGetTransferId(&pCtx->pTransfer->State.Key), pEvent->idEvent));
                HGCMSvcSetU64(&pMsg->aParms[1], 0); /* uHandle */
                HGCMSvcSetPv (&pMsg->aParms[2], pCreateParms->pszPath, cbPath);
                HGCMSvcSetU32(&pMsg->aParms[3], pCreateParms->fCreate);

                ShClSvcClientLock(pClient);

                ShClSvcClientMsgAdd(pClient, pMsg, true /* fAppend */);
                rc = ShClSvcClientWakeup(pClient);

                ShClSvcClientUnlock(pClient);

                if (RT_SUCCESS(rc))
                {
                    PSHCLEVENTPAYLOAD pPayload;
                    rc = shClSvcTransferGuestReplyWait(pEvent, pCtx->pTransfer->uTimeoutMs, &pPayload);
                    if (RT_SUCCESS(rc))
                    {
                        Assert(pPayload->cbData == sizeof(SHCLREPLY));

                        PSHCLREPLY pReply = (PSHCLREPLY)pPayload->pvData;
                        AssertPtr(pReply);

                        Assert(pReply->uType == VBOX_SHCL_TX_REPLYMSGTYPE_OBJ_OPEN);

                        LogFlowFunc(("hObj=%RU64\n", pReply->u.ObjOpen.uHandle));

                        *phObj = pReply->u.ObjOpen.uHandle;

                        ShClPayloadDestroy(pPayload);
                    }
                }
            }

            ShClEventRelease(pEvent);
        }
        else
            ShClSvcClientMsgFree(pClient, pMsg);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjClose */
DECLCALLBACK(int) ShClSvcTransferIfaceGHObjClose(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj)
{
    LogFlowFuncEnter();

    PSHCLCLIENT pClient = (PSHCLCLIENT)pCtx->pvUser;
    AssertPtr(pClient);

    int rc;

    PSHCLCLIENTMSG pMsg = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_CLOSE,
                                                VBOX_SHCL_CPARMS_OBJ_CLOSE);
    if (pMsg)
    {
        PSHCLEVENT pEvent;
        rc = ShClEventSourceGenerateAndRegisterEvent(&pCtx->pTransfer->Events, &pEvent);
        if (RT_SUCCESS(rc))
        {
            HGCMSvcSetU64(&pMsg->aParms[0], VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID,
                                                                     ShClTransferKeyGetTransferId(&pCtx->pTransfer->State.Key), pEvent->idEvent));
            HGCMSvcSetU64(&pMsg->aParms[1], hObj);

            ShClSvcClientLock(pClient);

            ShClSvcClientMsgAdd(pClient, pMsg, true /* fAppend */);
            rc = ShClSvcClientWakeup(pClient);

            ShClSvcClientUnlock(pClient);

            if (RT_SUCCESS(rc))
            {
                PSHCLEVENTPAYLOAD pPayload;
                rc = shClSvcTransferGuestReplyWait(pEvent, pCtx->pTransfer->uTimeoutMs, &pPayload);
                if (RT_SUCCESS(rc))
                {
                    Assert(pPayload->cbData == sizeof(SHCLREPLY));
#ifdef VBOX_STRICT
                    PSHCLREPLY pReply = (PSHCLREPLY)pPayload->pvData;
                    AssertPtr(pReply);

                    Assert(pReply->uType == VBOX_SHCL_TX_REPLYMSGTYPE_OBJ_CLOSE);

                    LogFlowFunc(("hObj=%RU64\n", pReply->u.ObjClose.uHandle));
#endif
                    ShClPayloadDestroy(pPayload);
                }
            }

            ShClEventRelease(pEvent);
        }
        else
            ShClSvcClientMsgFree(pClient, pMsg);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjRead */
DECLCALLBACK(int) ShClSvcTransferIfaceGHObjRead(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj,
                                                void *pvData, uint32_t cbData, uint32_t fFlags, uint32_t *pcbRead)
{
    LogFlowFuncEnter();

    PSHCLCLIENT pClient = (PSHCLCLIENT)pCtx->pvUser;
    AssertPtr(pClient);

    int rc;

    PSHCLCLIENTMSG pMsg = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_READ,
                                                VBOX_SHCL_CPARMS_OBJ_READ_REQ);
    if (pMsg)
    {
        PSHCLEVENT pEvent;
        rc = ShClEventSourceGenerateAndRegisterEvent(&pCtx->pTransfer->Events, &pEvent);
        if (RT_SUCCESS(rc))
        {
            HGCMSvcSetU64(&pMsg->aParms[0], VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID,
                                                                     ShClTransferKeyGetTransferId(&pCtx->pTransfer->State.Key), pEvent->idEvent));
            HGCMSvcSetU64(&pMsg->aParms[1], hObj);
            HGCMSvcSetU32(&pMsg->aParms[2], cbData);
            HGCMSvcSetU32(&pMsg->aParms[3], fFlags);

            ShClSvcClientLock(pClient);

            ShClSvcClientMsgAdd(pClient, pMsg, true /* fAppend */);
            rc = ShClSvcClientWakeup(pClient);

            ShClSvcClientUnlock(pClient);

            if (RT_SUCCESS(rc))
            {
                PSHCLEVENTPAYLOAD pPayload;
                rc = shClSvcTransferGuestReplyWait(pEvent, pCtx->pTransfer->uTimeoutMs, &pPayload);
                if (RT_SUCCESS(rc))
                {
                    Assert(pPayload->cbData == sizeof(SHCLOBJDATACHUNK));

                    PSHCLOBJDATACHUNK pDataChunk = (PSHCLOBJDATACHUNK)pPayload->pvData;
                    AssertPtr(pDataChunk);

                    const uint32_t cbRead = RT_MIN(cbData, pDataChunk->cbData);

                    memcpy(pvData, pDataChunk->pvData, cbRead);

                    if (pcbRead)
                        *pcbRead = cbRead;

                    ShClPayloadDestroy(pPayload);
                }
            }

            ShClEventRelease(pEvent);
        }
        else
            ShClSvcClientMsgFree(pClient, pMsg);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reports a transfer status to the guest.
 *
 * @returns VBox status code.
 * @param   pClient             Client that owns the transfer.
 * @param   idTransfer          Transfer ID to report status for.
 * @param   enmDir              Transfer direction to report status for.
 * @param   enmSts              Status to report.
 * @param   rcTransfer          Result code to report. Optional and depending on status.
 * @param   ppEvent             Where to return the wait event on success. Optional.
 *                              Must be released by the caller with ShClEventRelease().
 *
 * @note    Caller must enter the client's critical section.
 */
static int shClSvcTransferSendStatusExAsync(PSHCLCLIENT pClient, SHCLTRANSFERID idTransfer,
                                            SHCLTRANSFERDIR enmDir, SHCLTRANSFERSTATUS enmSts, int rcTransfer,
                                            PSHCLEVENT *ppEvent)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);
    AssertReturn(idTransfer != NIL_SHCLTRANSFERID, VERR_INVALID_PARAMETER);
    /* ppEvent is optional. */

    Assert(RTCritSectIsOwner(&pClient->CritSect));

    PSHCLCLIENTMSG pMsgReadData = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_TRANSFER_STATUS,
                                                        VBOX_SHCL_CPARMS_TRANSFER_STATUS);
    if (!pMsgReadData)
        return VERR_NO_MEMORY;

    PSHCLEVENT pEvent;
    int rc = ShClEventSourceGenerateAndRegisterEvent(&pClient->EventSrc, &pEvent);
    if (RT_SUCCESS(rc))
    {
        HGCMSvcSetU64(&pMsgReadData->aParms[0], VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID, idTransfer, pEvent->idEvent));
        HGCMSvcSetU32(&pMsgReadData->aParms[1], enmDir);
        HGCMSvcSetU32(&pMsgReadData->aParms[2], enmSts);
        HGCMSvcSetU32(&pMsgReadData->aParms[3], (uint32_t)rcTransfer); /** @todo uint32_t vs. int. */
        HGCMSvcSetU32(&pMsgReadData->aParms[4], 0 /* fFlags, unused */);

        ShClSvcClientMsgAdd(pClient, pMsgReadData, true /* fAppend */);

        rc = ShClSvcClientWakeup(pClient);
        if (RT_SUCCESS(rc))
        {
            if (enmSts == SHCLTRANSFERSTATUS_ERROR)
                LogRelMax(16, ("Shared Clipboard: Reported error status %Rrc for transfer %RU16 to guest\n",
                               rcTransfer, idTransfer));
            else
                LogRel2(("Shared Clipboard: Reported status %s (rc=%Rrc) of transfer %RU16 to guest\n",
                         ShClTransferStatusToStr(enmSts), rcTransfer, idTransfer));

            if (ppEvent)
            {
                *ppEvent = pEvent; /* Takes ownership. */
            }
            else /* If event is not consumed by the caller, release the event again. */
                ShClEventRelease(pEvent);
        }
        else
            ShClEventRelease(pEvent);
    }
    else
        rc = VERR_SHCLPB_MAX_EVENTS_REACHED;

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Reporting status %s (%Rrc) for transfer %RU16 to guest failed with %Rrc\n",
                       ShClTransferStatusToStr(enmSts), rcTransfer, idTransfer, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reports a transfer status to the guest, internal version.
 *
 * @returns VBox status code.
 * @param   pClient             Client that owns the transfer.
 * @param   pTransfer           Transfer to report status for.
 * @param   enmSts              Status to report.
 * @param   rcTransfer          Result code to report. Optional and depending on status.
 * @param   ppEvent             Where to return the wait event on success. Optional.
 *                              Must be released by the caller with ShClEventRelease().
 *
 * @note    Caller must enter the client's critical section.
 */
int shClSvcTransferSendStatusAsync(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, SHCLTRANSFERSTATUS enmSts,
                                   int rcTransfer, PSHCLEVENT *ppEvent)
{
    AssertPtrReturn(pClient,   VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    /* ppEvent is optional. */

    return shClSvcTransferSendStatusExAsync(pClient, ShClTransferGetID(pTransfer), ShClTransferGetDir(pTransfer),
                                            enmSts, rcTransfer, ppEvent);
}

/**
 * Cleans up (unregisters and destroys) all transfers not in started state (anymore).
 *
 * @param   pClient             Client to clean up transfers for.
 *
 * @note    Caller needs to take the critical section.
 */
static void shClSvcTransferCleanupAllUnused(PSHCLCLIENT pClient)
{
    Assert(RTCritSectIsOwner(&pClient->CritSect));

    LogFlowFuncEnter();

    PSHCLTRANSFERCTX pTxCtx = &pClient->Transfers.Ctx;

    for (;;)
    {
        PSHCLTRANSFER pTransfer = NULL;

        shClTransferCtxLock(pTxCtx);

        PSHCLTRANSFER pIt;
        RTListForEach(&pTxCtx->List, pIt, SHCLTRANSFER, Node)
        {
            if (ShClTransferGetStatus(pIt) != SHCLTRANSFERSTATUS_STARTED)
            {
                pTransfer = pIt;
                shclTransferCtxTransferRemoveLocked(pTxCtx, pTransfer);
                break;
            }
        }

        shClTransferCtxUnlock(pTxCtx);

        if (!pTransfer)
            break;

        bool const fNotifyUninitialized = !ShClTransferStatusIsTerminal(ShClTransferGetStatus(pTransfer));
        int rc2 = VINF_SUCCESS;
        if (fNotifyUninitialized)
        {
            /* Let the guest know while the client state is still serialized. */
            rc2 = shClSvcTransferSendStatusAsync(pClient, pTransfer,
                                                  SHCLTRANSFERSTATUS_UNINITIALIZED, VINF_SUCCESS, NULL /* ppEvent */);
            AssertRC(rc2);
        }

        ShClSvcClientUnlock(pClient);
        if (fNotifyUninitialized)
        {
            rc2 = ShClSvcTransferReportDetachedStatus(pClient, pTransfer,
                                                       SHCLTRANSFERSTATUS_UNINITIALIZED, VINF_SUCCESS);
            AssertRC(rc2);
        }
        shclTransferCtxTransferNotifyUnregistered(pTxCtx, pTransfer);

        shClTransferDestroyConsume(pTransfer);
        ShClSvcClientLock(pClient);
    }
}


/**
 * Creates a new transfer on the host.
 *
 * @returns VBox status code.
 * @param   pClient             Client that owns the transfer.
 * @param   enmDir              Transfer direction to create.
 * @param   enmSource           Transfer source to create.
 * @param   pCallbacks          Callback table to copy into the transfer. Optional and can be NULL.
 * @param   idTransfer          Transfer ID to use for creation.
 *                              If set to NIL_SHCLTRANSFERID, a new transfer ID will be created.
 * @param   ppTransfer          Where to return the retained transfer on success. Optional and can be NULL.
 *                              The caller must release it with ShClTransferRelease().
 */
int ShClSvcTransferCreate(PSHCLCLIENT pClient, SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource,
                          PSHCLTRANSFERCALLBACKS pCallbacks, SHCLTRANSFERID idTransfer, PSHCLTRANSFER *ppTransfer)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);
    if (ppTransfer)
        *ppTransfer = NULL;

    LogFlowFuncEnter();

    ShClSvcClientLock(pClient);

    PSHCLTRANSFER pTransfer = NULL;
    bool fReleaseCreationRef = false;
    int rc = VERR_ACCESS_DENIED;
    if (shClSvcClientTransfersAreAllowed(pClient))
    {
        /* Cleanup drops the client lock while consuming stale transfers, so
         * recheck policy after it reacquires the lock. */
        shClSvcTransferCleanupAllUnused(pClient);
        if (shClSvcClientTransfersAreAllowed(pClient))
            rc = ShClTransferCreate(enmDir, enmSource, pCallbacks, &pTransfer);
    }
    if (RT_SUCCESS(rc))
    {
        /* Establish pointer ownership before registration publishes the
         * transfer to concurrent teardown paths. */
        ShClTransferAcquire(pTransfer);
        fReleaseCreationRef = true;

        if (idTransfer == NIL_SHCLTRANSFERID)
            rc = ShClTransferCtxRegister(&pClient->Transfers.Ctx, pTransfer, &idTransfer);
        else
            rc = ShClTransferCtxRegisterById(&pClient->Transfers.Ctx, pTransfer, idTransfer);
        if (RT_SUCCESS(rc))
        {
            if (ppTransfer)
            {
                *ppTransfer = pTransfer;
                fReleaseCreationRef = false; /* The caller takes ownership. */
            }
        }
    }

    ShClSvcClientUnlock(pClient);

    SHCLTRANSFERID const idCreated = pTransfer ? ShClTransferGetID(pTransfer) : NIL_SHCLTRANSFERID;
    if (fReleaseCreationRef)
        ShClTransferRelease(pTransfer);

    if (RT_FAILURE(rc))
    {
        if (ShClTransferIdIsValid(idCreated))
            ShClSvcTransferDestroyById(pClient, idCreated);
        else
        {
            /* Registration never published this transfer, so the creation
             * path still owns its pointer exclusively. */
            shClTransferDestroyConsume(pTransfer);
        }
    }

    if (   RT_FAILURE(rc)
        && rc != VERR_ACCESS_DENIED)
        LogRelMax(16, ("Shared Clipboard: Creating %s transfer for client %RU32 failed with %Rrc\n",
                       enmDir == SHCLTRANSFERDIR_GUEST_TO_HOST ? "guest-to-host" : "host-to-guest",
                       pClient->State.uClientID, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Detaches a service transfer by ID.
 *
 * @returns The exclusively claimed transfer, or NULL if it was not registered.
 * @param   pClient             Client owning the transfer context.
 * @param   idTransfer          ID of the transfer to claim.
 * @param   pExpected           Expected transfer pointer, or NULL when claiming
 *                              solely by ID.
 */
static PSHCLTRANSFER shClSvcTransferClaimById(PSHCLCLIENT pClient, SHCLTRANSFERID idTransfer,
                                               PSHCLTRANSFER pExpected)
{
    PSHCLTRANSFERCTX pTxCtx = &pClient->Transfers.Ctx;

    shClTransferCtxLock(pTxCtx);

    PSHCLTRANSFER pTransfer = shClTransferCtxGetTransferByIdInternal(pTxCtx, idTransfer);
    if (pTransfer)
    {
        if (   !pExpected
            || pTransfer == pExpected)
            shclTransferCtxTransferRemoveLocked(pTxCtx, pTransfer);
        else
        {
            LogRel2(("Shared Clipboard: Transfer ID %RU16 resolved to %p instead of expected transfer %p\n",
                     idTransfer, pTransfer, pExpected));
            pTransfer = NULL;
        }
    }

    shClTransferCtxUnlock(pTxCtx);
    return pTransfer;
}


/**
 * Detaches a service transfer for deferred unregistration and destruction.
 *
 * The returned transfer is no longer discoverable through its context, but its
 * owner context remains valid until shClSvcTransferDestroyDetached() performs
 * the potentially blocking unregistration callback and consuming destruction.
 *
 * @returns Exclusively owned detached transfer, or NULL if it was not registered.
 * @param   pClient             Client owning the transfer context.
 * @param   pTransfer           Retained exact transfer to detach.
 */
PSHCLTRANSFER shClSvcTransferDetach(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pClient, NULL);
    AssertPtrReturn(pTransfer, NULL);
    SHCLTRANSFERID const idTransfer = ShClTransferGetID(pTransfer);
    AssertReturn(ShClTransferIdIsValid(idTransfer), NULL);

    return shClSvcTransferClaimById(pClient, idTransfer, pTransfer);
}


/**
 * Completes unregistration and destroys an exclusively owned detached transfer.
 *
 * @param   pTransfer           Exclusively owned transfer to consume.
 */
void shClSvcTransferDestroyDetached(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturnVoid(pTransfer);
    PSHCLTRANSFERCTX const pOwnerCtx = pTransfer->pOwnerCtx;
    AssertPtrReturnVoid(pOwnerCtx);

    shclTransferCtxTransferNotifyUnregistered(pOwnerCtx, pTransfer);
    shClTransferDestroyConsume(pTransfer);
}


/**
 * Finishes destruction of an exclusively claimed service transfer.
 *
 * @param   pClient             Client that owned the transfer.
 * @param   pTransfer           Exclusively claimed transfer to consume.
 * @param   fNotifyGuest        Whether to report UNINITIALIZED to the guest.
 */
static void shClSvcTransferDestroyClaimed(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, bool fNotifyGuest)
{
    PSHCLTRANSFERCTX pTxCtx = &pClient->Transfers.Ctx;
    bool const fNotifyUninitialized = !ShClTransferStatusIsTerminal(ShClTransferGetStatus(pTransfer));

    if (fNotifyGuest && fNotifyUninitialized)
    {
        ShClSvcClientLock(pClient);
        int rc = shClSvcTransferSendStatusAsync(pClient, pTransfer,
                                                SHCLTRANSFERSTATUS_UNINITIALIZED, VINF_SUCCESS, NULL /* ppEvent */);
        AssertRC(rc);
        ShClSvcClientUnlock(pClient);
    }

    if (fNotifyUninitialized)
    {
        int const rc = ShClSvcTransferReportDetachedStatus(pClient, pTransfer,
                                                            SHCLTRANSFERSTATUS_UNINITIALIZED, VINF_SUCCESS);
        AssertRC(rc);
    }

    shclTransferCtxTransferNotifyUnregistered(pTxCtx, pTransfer);

    shClTransferDestroyConsume(pTransfer);
}

/**
 * Destroys a transfer on the host by its context-local ID, extended version.
 *
 * The ID is not reused during a transfer-context session, so claiming under
 * the context lock does not depend on the lifetime of a borrowed pointer.
 *
 * @param   pClient             Client to destroy transfer for.
 * @param   idTransfer          ID of the transfer to destroy.
 * @param   fNotifyGuest        Whether to report UNINITIALIZED to the guest.
 */
void ShClSvcTransferDestroyByIdEx(PSHCLCLIENT pClient, SHCLTRANSFERID idTransfer, bool fNotifyGuest)
{
    AssertPtrReturnVoid(pClient);
    AssertReturnVoid(ShClTransferIdIsValid(idTransfer));
    AssertMsgReturnVoid(!RTCritSectIsOwner(&pClient->CritSect),
                        ("The client lock must not be held while destroying a transfer\n"));

    LogFlowFuncEnter();

    PSHCLTRANSFER pTransfer = shClSvcTransferClaimById(pClient, idTransfer, NULL /* pExpected */);
    if (pTransfer)
        shClSvcTransferDestroyClaimed(pClient, pTransfer, fNotifyGuest);
    else
        LogRel2(("Shared Clipboard: Transfer %RU16 was already detached\n", idTransfer));

    LogFlowFuncLeave();
}

/**
 * Destroys a transfer on the host by its context-local ID.
 *
 * @param   pClient             Client to destroy transfer for.
 * @param   idTransfer          ID of the transfer to destroy.
 */
void ShClSvcTransferDestroyById(PSHCLCLIENT pClient, SHCLTRANSFERID idTransfer)
{
    ShClSvcTransferDestroyByIdEx(pClient, idTransfer, true /* fNotifyGuest */);
}

/**
 * Destroys an exclusively owned transfer pointer on the host.
 *
 * @param   pClient             Client to destroy transfer for.
 * @param   pTransfer           Transfer to destroy.
 *                              The pointer will be invalid after return.
 *
 * @note    This pointer form is only safe for a newly created transfer whose
 *          lifetime and publication remain exclusively controlled by the
 *          caller. General callers must snapshot its ID while the pointer is
 *          known valid and use ShClSvcTransferDestroyById().
 */
void ShClSvcTransferDestroy(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer)
{
    if (!pTransfer)
        return;

    AssertPtrReturnVoid(pClient);
    AssertMsgReturnVoid(!RTCritSectIsOwner(&pClient->CritSect),
                        ("The client lock must not be held while destroying a transfer\n"));

    LogFlowFuncEnter();

    /* The exclusive-ownership contract makes this dereference safe. */
    SHCLTRANSFERID const idTransfer = ShClTransferGetID(pTransfer);
    AssertReturnVoid(ShClTransferIdIsValid(idTransfer));

    PSHCLTRANSFER pClaimed = shClSvcTransferClaimById(pClient, idTransfer, pTransfer /* pExpected */);
    if (pClaimed)
        shClSvcTransferDestroyClaimed(pClient, pClaimed, true /* fNotifyGuest */);
    else
        LogRel2(("Shared Clipboard: Exclusively owned transfer %p was already detached\n", pTransfer));

    LogFlowFuncLeave();
}

/**
 * Detaches all transfers from a Shared Clipboard client for later destruction.
 *
 * @param   pClient             Client to detach transfers from.
 * @param   pList               Destination list for detached transfers.
 *
 * @note    Destruction callbacks are deferred until
 *          shClSvcTransferDestroyDetachedAll(), so the caller may hold an
 *          outer ownership lock while invoking this function.
 */
void shClSvcTransferDetachAll(PSHCLCLIENT pClient, PRTLISTANCHOR pList)
{
    AssertPtrReturnVoid(pClient);
    AssertPtrReturnVoid(pList);

    ShClSvcClientLock(pClient);

    PSHCLTRANSFERCTX pTxCtx = &pClient->Transfers.Ctx;
    for (;;)
    {
        shClTransferCtxLock(pTxCtx);

        PSHCLTRANSFER pTransfer = shClTransferCtxGetTransferByIndexInternal(pTxCtx, 0 /* Index */);
        if (pTransfer)
            shclTransferCtxTransferRemoveLocked(pTxCtx, pTransfer);

        shClTransferCtxUnlock(pTxCtx);

        if (!pTransfer)
            break;

        if (!ShClTransferStatusIsTerminal(ShClTransferGetStatus(pTransfer)))
        {
            int const rc = shClSvcTransferSendStatusAsync(pClient, pTransfer,
                                                           SHCLTRANSFERSTATUS_UNINITIALIZED, VINF_SUCCESS, NULL /* ppEvent */);
            AssertRC(rc);
        }

        RTListAppend(pList, &pTransfer->Node);
    }

    ShClSvcClientUnlock(pClient);
}

/**
 * Destroys transfers previously detached by shClSvcTransferDetachAll().
 *
 * @param   pList               List of detached transfers to consume.
 *
 * @note    No service, client or transfer-context ownership lock may be held.
 */
void shClSvcTransferDestroyDetachedAll(PRTLISTANCHOR pList)
{
    AssertPtrReturnVoid(pList);

    PSHCLTRANSFER pTransfer, pTransferNext;
    RTListForEachSafe(pList, pTransfer, pTransferNext, SHCLTRANSFER, Node)
    {
        RTListNodeRemove(&pTransfer->Node);

        PSHCLTRANSFERCTX pTxCtx = pTransfer->pOwnerCtx;
        AssertPtr(pTxCtx);
        shclTransferCtxTransferNotifyUnregistered(pTxCtx, pTransfer);

        shClTransferDestroyConsume(pTransfer);
    }
}


/**
 * Destroys all transfers of a Shared Clipboard client.
 *
 * @param   pClient             Client to destroy transfers for.
 */
void shClSvcTransferDestroyAll(PSHCLCLIENT pClient)
{
    if (!pClient)
        return;
    AssertMsgReturnVoid(!RTCritSectIsOwner(&pClient->CritSect),
                        ("The client lock must not be held while destroying transfers\n"));

    LogFlowFuncEnter();

    RTLISTANCHOR ListDestroy;
    RTListInit(&ListDestroy);

    shClSvcTransferDetachAll(pClient, &ListDestroy);
    shClSvcTransferDestroyDetachedAll(&ListDestroy);
}

#endif /* VBOX_WITH_SHARED_CLIPBOARD_HOST */

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

    LogFlowFunc(("uID=%RU32, enmDir=%RU32, enmSource=%RU32\n",
                 ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.enmDir, pTransfer->State.enmSource));

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
        if (pTransfer->State.enmSource == SHCLSOURCE_LOCAL)
            AssertMsgStmt(ShClTransferRootsCount(pTransfer), ("Transfer has no root entries set (yet)\n"), rc = VERR_WRONG_ORDER);

        if (RT_SUCCESS(rc))
            rc = shClTransferSetStatus(pTransfer, SHCLTRANSFERSTATUS_INITIALIZED);
    }

    shClTransferUnlock(pTransfer);

    /* Note: Callback will be called after we unlocked the transfer, as the caller might access the transfer right away. */
    if (   RT_SUCCESS(rc)
        && pTransfer->Callbacks.pfnOnInitialized)
        rc = pTransfer->Callbacks.pfnOnInitialized(&pTransfer->CallbackCtx);

    if (RT_FAILURE(rc))
        LogRel2(("Shared Clipboard: Initializing transfer %RU16/%RU64 in session %RU16 failed with %Rrc\n",
                 ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.Key.uGeneration,
                 ShClTransferKeyGetSessionId(&pTransfer->State.Key), rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_HOST
/**
 * Initializes a (created) transfer on the host.
 *
 * @returns VBox status code.
 * @param   pClient             Client that owns the transfer.
 * @param   pTransfer           Transfer to initialize.
 */
int ShClSvcTransferInit(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    ShClSvcClientLock(pClient);

    int rc;
    if (!shClSvcClientTransfersAreAllowed(pClient))
        rc = VERR_ACCESS_DENIED;
    else
    {
        Assert(ShClTransferGetStatus(pTransfer) == SHCLTRANSFERSTATUS_NONE);

        PSHCLTRANSFERCTX pTxCtx = &pClient->Transfers.Ctx;

        if (!ShClTransferCtxIsMaximumReached(pTxCtx))
        {
            SHCLTRANSFERDIR const enmDir = ShClTransferGetDir(pTransfer);

            LogRel2(("Shared Clipboard: Initializing %s transfer ...\n",
                     enmDir == SHCLTRANSFERDIR_GUEST_TO_HOST ? "guest -> host" : "host -> guest"));

            rc = ShClTransferInit(pTransfer);
        }
        else
            rc = VERR_SHCLPB_MAX_TRANSFERS_REACHED;

        /* Tell the guest the outcome. */
        int rc2 = shClSvcTransferSendStatusAsync(pClient, pTransfer,
                                                   RT_SUCCESS(rc)
                                                 ? SHCLTRANSFERSTATUS_INITIALIZED : SHCLTRANSFERSTATUS_ERROR, rc,
                                                 NULL /* ppEvent */);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    if (   RT_FAILURE(rc)
        && rc != VERR_ACCESS_DENIED)
        LogRelMax(16, ("Shared Clipboard: Initializing transfer %RU16/%RU64 in session %RU16 for client %RU32 failed with %Rrc\n",
                       ShClTransferKeyGetTransferId(&pTransfer->State.Key), pTransfer->State.Key.uGeneration,
                       ShClTransferKeyGetSessionId(&pTransfer->State.Key), pClient->State.uClientID, rc));

    ShClSvcClientUnlock(pClient);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#endif /* VBOX_WITH_SHARED_CLIPBOARD_HOST */
