/* $Id: vfschain.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Virtual File System, Chains.
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
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>

#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>

#include "internal/file.h"
#include "internal/magics.h"
//#include "internal/vfs.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static PCRTVFSCHAINELEMENTREG rtVfsChainFindProviderLocked(const char *pszProvider);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Init the critical section once. */
static RTONCE       g_rtVfsChainElementInitOnce = RTONCE_INITIALIZER;
/** Critical section protecting g_rtVfsChainElementProviderList. */
static RTCRITSECTRW g_rtVfsChainElementCritSect;
/** List of VFS chain element providers (RTVFSCHAINELEMENTREG). */
static RTLISTANCHOR g_rtVfsChainElementProviderList;



RTDECL(int) RTVfsChainValidateOpenFileOrIoStream(PRTVFSCHAINSPEC pSpec, PRTVFSCHAINELEMSPEC pElement,
                                                 uint32_t *poffError, PRTERRINFO pErrInfo)
{
    if (pElement->cArgs < 1)
        return VERR_VFS_CHAIN_AT_LEAST_ONE_ARG;
    if (pElement->cArgs > 4)
        return VERR_VFS_CHAIN_AT_MOST_FOUR_ARGS;
    if (!*pElement->paArgs[0].psz)
        return VERR_VFS_CHAIN_EMPTY_ARG;

    /*
     * Calculate the flags, storing them in the first argument.
     */
    const char *pszAccess = pElement->cArgs >= 2 ? pElement->paArgs[1].psz : "";
    if (!*pszAccess)
        pszAccess = (pSpec->fOpenFile & RTFILE_O_ACCESS_MASK) == RTFILE_O_READWRITE ? "rw"
                  : (pSpec->fOpenFile & RTFILE_O_ACCESS_MASK) == RTFILE_O_READ      ? "r"
                  : (pSpec->fOpenFile & RTFILE_O_ACCESS_MASK) == RTFILE_O_WRITE     ? "w"
                  :                                                                   "rw";

    const char *pszDisp = pElement->cArgs >= 3 ? pElement->paArgs[2].psz : "";
    if (!*pszDisp)
        pszDisp = strchr(pszAccess, 'w') != NULL ? "open-create" : "open";

    const char *pszSharing = pElement->cArgs >= 4 ? pElement->paArgs[3].psz : "";

    int rc = RTFileModeToFlagsEx(pszAccess, pszDisp, pszSharing, &pElement->uProvider);
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;

    /*
     * Now try figure out which argument offended us.
     */
    AssertReturn(pElement->cArgs > 1, VERR_VFS_CHAIN_IPE);
    if (   pElement->cArgs == 2
        || RT_FAILURE(RTFileModeToFlagsEx(pszAccess, "open-create", "", &pElement->uProvider)))
    {
        *poffError = pElement->paArgs[1].offSpec;
        rc = RTErrInfoSet(pErrInfo, VERR_VFS_CHAIN_INVALID_ARGUMENT, "Expected valid access flags: 'r', 'rw', or 'w'");
    }
    else if (   pElement->cArgs == 3
             || RT_FAILURE(RTFileModeToFlagsEx(pszAccess, pszDisp, "", &pElement->uProvider)))
    {
        *poffError = pElement->paArgs[2].offSpec;
        rc = RTErrInfoSet(pErrInfo, VERR_VFS_CHAIN_INVALID_ARGUMENT,
                          "Expected valid open disposition: create, create-replace, open, open-create, open-append, open-truncate");
    }
    else
    {
        *poffError = pElement->paArgs[3].offSpec;
        rc = RTErrInfoSet(pErrInfo, VERR_VFS_CHAIN_INVALID_ARGUMENT, "Expected valid sharing flags: nr, nw, nrw, d");

    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainOpen_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
                                                 PRTVFSCHAINELEMSPEC pElement, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg);

    /*
     * Basic checks.
     */
    if (   pElement->enmType != RTVFSOBJTYPE_DIR
        && pElement->enmType != RTVFSOBJTYPE_FILE
        && pElement->enmType != RTVFSOBJTYPE_IO_STREAM)
        return VERR_VFS_CHAIN_ONLY_FILE_OR_IOS_OR_DIR;
    if (   pElement->enmTypeIn != RTVFSOBJTYPE_DIR
        && pElement->enmTypeIn != RTVFSOBJTYPE_FS_STREAM
        && pElement->enmTypeIn != RTVFSOBJTYPE_VFS)
    {
        if (pElement->enmTypeIn == RTVFSOBJTYPE_INVALID)
        {
            /*
             * First element: Transform into 'stdfile' or 'stddir' if registered.
             */
            const char            *pszNewProvider = pElement->enmType == RTVFSOBJTYPE_DIR ? "stddir" : "stdfile";
            PCRTVFSCHAINELEMENTREG pNewProvider   = rtVfsChainFindProviderLocked(pszNewProvider);
            if (pNewProvider)
            {
                pElement->pProvider = pNewProvider;
                return pNewProvider->pfnValidate(pNewProvider, pSpec, pElement, poffError, pErrInfo);
            }
            return VERR_VFS_CHAIN_CANNOT_BE_FIRST_ELEMENT;
        }
        return VERR_VFS_CHAIN_TAKES_DIR_OR_FSS_OR_VFS;
    }

    /*
     * Make common cause with 'stdfile' if we're opening a file or I/O stream.
     * If the input is a FSS, we have to make sure it's a read-only operation.
     */
    if (   pElement->enmType == RTVFSOBJTYPE_FILE
        || pElement->enmType == RTVFSOBJTYPE_IO_STREAM)
    {
        int rc = RTVfsChainValidateOpenFileOrIoStream(pSpec, pElement, poffError, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            if (pElement->enmTypeIn != RTVFSOBJTYPE_FS_STREAM)
                return VINF_SUCCESS;
            if (   !(pElement->uProvider & RTFILE_O_WRITE)
                &&  (pElement->uProvider & RTFILE_O_ACTION_MASK) ==  RTFILE_O_OPEN)
                return VINF_SUCCESS;
            *poffError = pElement->cArgs > 1 ? pElement->paArgs[1].offSpec : pElement->offSpec;
            return VERR_VFS_CHAIN_INVALID_ARGUMENT;
        }
        return rc;
    }


    /*
     * Directory checks.  Path argument only, optional. If not given the root directory of a VFS or the
     */
    if (pElement->cArgs > 1)
        return VERR_VFS_CHAIN_AT_MOST_ONE_ARG;
    pElement->uProvider = 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnInstantiate}
 */
static DECLCALLBACK(int) rtVfsChainOpen_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                    PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                    PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, pElement, poffError, pErrInfo);
    AssertReturn(hPrevVfsObj != NIL_RTVFSOBJ, VERR_VFS_CHAIN_IPE);

    /*
     * File system stream: Seek thru the stream looking for the object to open.
     */
    RTVFSFSSTREAM hVfsFssIn = RTVfsObjToFsStream(hPrevVfsObj);
    if (hVfsFssIn != NIL_RTVFSFSSTREAM)
    {
        return VERR_NOT_IMPLEMENTED;
    }

    /*
     * VFS: Use RTVfsFileOpen or RTVfsDirOpen.
     */
    RTVFS hVfsIn = RTVfsObjToVfs(hPrevVfsObj);
    if (hVfsIn != NIL_RTVFS)
    {
        if (   pElement->enmType == RTVFSOBJTYPE_FILE
            || pElement->enmType == RTVFSOBJTYPE_IO_STREAM)
        {
            RTVFSFILE hVfsFile = NIL_RTVFSFILE;
            int rc = RTVfsFileOpen(hVfsIn, pElement->paArgs[0].psz, pElement->uProvider, &hVfsFile);
            RTVfsRelease(hVfsIn);
            if (RT_SUCCESS(rc))
            {
                *phVfsObj = RTVfsObjFromFile(hVfsFile);
                RTVfsFileRelease(hVfsFile);
                if (*phVfsObj != NIL_RTVFSOBJ)
                    return VINF_SUCCESS;
                rc = VERR_VFS_CHAIN_CAST_FAILED;
            }
            return rc;
        }
        if (pElement->enmType == RTVFSOBJTYPE_DIR)
        {
            RTVFSDIR hVfsDir = NIL_RTVFSDIR;
            int rc = RTVfsDirOpen(hVfsIn, pElement->paArgs[0].psz, (uint32_t)pElement->uProvider, &hVfsDir);
            RTVfsRelease(hVfsIn);
            if (RT_SUCCESS(rc))
            {
                *phVfsObj = RTVfsObjFromDir(hVfsDir);
                RTVfsDirRelease(hVfsDir);
                if (*phVfsObj != NIL_RTVFSOBJ)
                    return VINF_SUCCESS;
                rc = VERR_VFS_CHAIN_CAST_FAILED;
            }
            return rc;
        }
        RTVfsRelease(hVfsIn);
        return VERR_VFS_CHAIN_IPE;
    }

    /*
     * Directory: Similar to above, just relative to a directory.
     */
    RTVFSDIR hVfsDirIn = RTVfsObjToDir(hPrevVfsObj);
    if (hVfsDirIn != NIL_RTVFSDIR)
    {
        if (   pElement->enmType == RTVFSOBJTYPE_FILE
            || pElement->enmType == RTVFSOBJTYPE_IO_STREAM)
        {
            RTVFSFILE hVfsFile = NIL_RTVFSFILE;
            int rc = RTVfsDirOpenFile(hVfsDirIn, pElement->paArgs[0].psz, pElement->uProvider, &hVfsFile);
            RTVfsDirRelease(hVfsDirIn);
            if (RT_SUCCESS(rc))
            {
                *phVfsObj = RTVfsObjFromFile(hVfsFile);
                RTVfsFileRelease(hVfsFile);
                if (*phVfsObj != NIL_RTVFSOBJ)
                    return VINF_SUCCESS;
                rc = VERR_VFS_CHAIN_CAST_FAILED;
            }
            return rc;
        }
        if (pElement->enmType == RTVFSOBJTYPE_DIR)
        {
            RTVFSDIR hVfsDir = NIL_RTVFSDIR;
            int rc = RTVfsDirOpenDir(hVfsDirIn, pElement->paArgs[0].psz, pElement->uProvider, &hVfsDir);
            RTVfsDirRelease(hVfsDirIn);
            if (RT_SUCCESS(rc))
            {
                *phVfsObj = RTVfsObjFromDir(hVfsDir);
                RTVfsDirRelease(hVfsDir);
                if (*phVfsObj != NIL_RTVFSOBJ)
                    return VINF_SUCCESS;
                rc = VERR_VFS_CHAIN_CAST_FAILED;
            }
            return rc;
        }
        RTVfsDirRelease(hVfsDirIn);
        return VERR_VFS_CHAIN_IPE;
    }

    AssertFailed();
    return VERR_VFS_CHAIN_CAST_FAILED;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnCanReuseElement}
 */
static DECLCALLBACK(bool) rtVfsChainOpen_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                         PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                         PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pElement, pReuseSpec, pReuseElement);
    return false;
}


/** VFS chain element 'gunzip'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainGunzipReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "open",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Generic VFS open, that can open files (or I/O stream) and directories in a VFS, directory or file system stream.\n"
                                "If used as the first element in a chain, it will work like 'stdfile' or 'stddir' and work on the real file system.\n"
                                "First argument is the filename or directory path.\n"
                                "Second argument is access mode, files only, optional: r, w, rw.\n"
                                "Third argument is open disposition, files only, optional: create, create-replace, open, open-create, open-append, open-truncate.\n"
                                "Forth argument is file sharing, files only, optional: nr, nw, nrw, d.",
    /* pfnValidate = */         rtVfsChainOpen_Validate,
    /* pfnInstantiate = */      rtVfsChainOpen_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainOpen_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainGunzipReg, rtVfsChainGunzipReg);




/**
 * Initializes the globals via RTOnce.
 *
 * @returns IPRT status code
 * @param   pvUser              Unused, ignored.
 */
static DECLCALLBACK(int) rtVfsChainElementRegisterInit(void *pvUser)
{
    NOREF(pvUser);
    if (!g_rtVfsChainElementProviderList.pNext)
        RTListInit(&g_rtVfsChainElementProviderList);
    int rc = RTCritSectRwInit(&g_rtVfsChainElementCritSect);
    if (RT_SUCCESS(rc))
    {
    }
    return rc;
}


RTDECL(int) RTVfsChainElementRegisterProvider(PRTVFSCHAINELEMENTREG pRegRec, bool fFromCtor)
{
    int rc;

    /*
     * Input validation.
     */
    AssertPtrReturn(pRegRec, VERR_INVALID_POINTER);
    AssertMsgReturn(pRegRec->uVersion   == RTVFSCHAINELEMENTREG_VERSION, ("%#x", pRegRec->uVersion),    VERR_INVALID_POINTER);
    AssertMsgReturn(pRegRec->uEndMarker == RTVFSCHAINELEMENTREG_VERSION, ("%#zx", pRegRec->uEndMarker), VERR_INVALID_POINTER);
    AssertReturn(pRegRec->fReserved == 0, VERR_INVALID_POINTER);
    AssertPtrReturn(pRegRec->pszName,               VERR_INVALID_POINTER);
    AssertPtrReturn(pRegRec->pfnValidate,           VERR_INVALID_POINTER);
    AssertPtrReturn(pRegRec->pfnInstantiate,        VERR_INVALID_POINTER);
    AssertPtrReturn(pRegRec->pfnCanReuseElement,    VERR_INVALID_POINTER);

    /*
     * Init and take the lock.
     */
    if (!fFromCtor)
    {
        rc = RTOnce(&g_rtVfsChainElementInitOnce, rtVfsChainElementRegisterInit, NULL);
        if (RT_FAILURE(rc))
            return rc;
        rc = RTCritSectRwEnterExcl(&g_rtVfsChainElementCritSect);
        if (RT_FAILURE(rc))
            return rc;
    }
    else if (!g_rtVfsChainElementProviderList.pNext)
        RTListInit(&g_rtVfsChainElementProviderList);

    /*
     * Duplicate name?
     */
    rc = VINF_SUCCESS;
    PRTVFSCHAINELEMENTREG pIterator, pIterNext;
    RTListForEachSafe(&g_rtVfsChainElementProviderList, pIterator, pIterNext, RTVFSCHAINELEMENTREG, ListEntry)
    {
        if (!strcmp(pIterator->pszName, pRegRec->pszName))
        {
            AssertMsgFailed(("duplicate name '%s' old=%p new=%p\n",  pIterator->pszName, pIterator, pRegRec));
            rc = VERR_ALREADY_EXISTS;
            break;
        }
    }

    /*
     * If not, append the record to the list.
     */
    if (RT_SUCCESS(rc))
        RTListAppend(&g_rtVfsChainElementProviderList, &pRegRec->ListEntry);

    /*
     * Leave the lock and return.
     */
    if (!fFromCtor)
        RTCritSectRwLeaveExcl(&g_rtVfsChainElementCritSect);
    return rc;
}


/**
 * Allocates and initializes an empty spec
 *
 * @returns Pointer to the spec on success, NULL on failure.
 */
static PRTVFSCHAINSPEC rtVfsChainSpecAlloc(void)
{
    PRTVFSCHAINSPEC pSpec = (PRTVFSCHAINSPEC)RTMemTmpAlloc(sizeof(*pSpec));
    if (pSpec)
    {
        pSpec->fOpenFile      = 0;
        pSpec->fOpenDir       = 0;
        pSpec->cElements      = 0;
        pSpec->paElements     = NULL;
    }
    return pSpec;
}


/**
 * Checks if @a ch is a character that can be escaped.
 *
 * @returns true / false.
 * @param   ch          The character to consider.
 */
DECLINLINE(bool) rtVfsChainSpecIsEscapableChar(char ch)
{
    return ch == '('
        || ch == ')'
        || ch == '{'
        || ch == '}'
        || ch == '\\'
        || ch == ','
        || ch == '|'
        || ch == ':';
}


/**
 * Duplicate a spec string after unescaping it.
 *
 * This differs from RTStrDupN in that it uses RTMemTmpAlloc instead of
 * RTMemAlloc.
 *
 * @returns String copy on success, NULL on failure.
 * @param   psz         The string to duplicate.
 * @param   cch         The number of bytes to duplicate.
 * @param   prc         The status code variable to set on failure. (Leeps the
 *                      code shorter. -lazy bird)
 */
DECLINLINE(char *) rtVfsChainSpecDupStrN(const char *psz, size_t cch, int *prc)
{
    char *pszCopy = (char *)RTMemTmpAlloc(cch + 1);
    if (pszCopy)
    {
        if (!memchr(psz, '\\', cch))
        {
            /* Plain string, copy it raw. */
            memcpy(pszCopy, psz, cch);
            pszCopy[cch] = '\0';
        }
        else
        {
            /* Has escape sequences, must unescape it. */
            char *pszDst = pszCopy;
            while (cch-- > 0)
            {
                char ch = *psz++;
                if (ch == '\\' && cch > 0)
                {
                    char ch2 = *psz;
                    if (rtVfsChainSpecIsEscapableChar(ch2))
                    {
                        psz++;
                        cch--;
                        ch = ch2;
                    }
                }
                *pszDst++ = ch;
            }
            *pszDst = '\0';
        }
    }
    else
        *prc = VERR_NO_TMP_MEMORY;
    return pszCopy;
}


/**
 * Adds an empty element to the chain specification.
 *
 * The caller is responsible for filling it the element attributes.
 *
 * @returns Pointer to the new element on success, NULL on failure.  The
 *          pointer is only valid till the next call to this function.
 * @param   pSpec       The chain specification.
 * @param   prc         The status code variable to set on failure. (Leeps the
 *                      code shorter. -lazy bird)
 */
static PRTVFSCHAINELEMSPEC rtVfsChainSpecAddElement(PRTVFSCHAINSPEC pSpec, uint16_t offSpec, int *prc)
{
    AssertPtr(pSpec);

    /*
     * Resize the element table if necessary.
     */
    uint32_t const iElement = pSpec->cElements;
    if ((iElement % 32) == 0)
    {
        PRTVFSCHAINELEMSPEC paNew = (PRTVFSCHAINELEMSPEC)RTMemTmpAlloc((iElement + 32) * sizeof(paNew[0]));
        if (!paNew)
        {
            *prc = VERR_NO_TMP_MEMORY;
            return NULL;
        }

        if (iElement)
            memcpy(paNew, pSpec->paElements, iElement * sizeof(paNew[0]));
        RTMemTmpFree(pSpec->paElements);
        pSpec->paElements = paNew;
    }

    /*
     * Initialize and add the new element.
     */
    PRTVFSCHAINELEMSPEC pElement = &pSpec->paElements[iElement];
    pElement->pszProvider = NULL;
    pElement->enmTypeIn   = iElement ? pSpec->paElements[iElement - 1].enmType : RTVFSOBJTYPE_INVALID;
    pElement->enmType     = RTVFSOBJTYPE_INVALID;
    pElement->offSpec     = offSpec;
    pElement->cchSpec     = 0;
    pElement->cArgs       = 0;
    pElement->paArgs      = NULL;
    pElement->pProvider   = NULL;
    pElement->hVfsObj     = NIL_RTVFSOBJ;

    pSpec->cElements = iElement + 1;
    return pElement;
}


/**
 * Adds an argument to the element spec.
 *
 * @returns IPRT status code.
 * @param   pElement            The element.
 * @param   psz                 The start of the argument string.
 * @param   cch                 The length of the argument string, escape
 *                              sequences counted twice.
 */
static int rtVfsChainSpecElementAddArg(PRTVFSCHAINELEMSPEC pElement, const char *psz, size_t cch, uint16_t offSpec)
{
    uint32_t iArg = pElement->cArgs;
    if ((iArg % 32) == 0)
    {
        PRTVFSCHAINELEMENTARG paNew = (PRTVFSCHAINELEMENTARG)RTMemTmpAlloc((iArg + 32) * sizeof(paNew[0]));
        if (!paNew)
            return VERR_NO_TMP_MEMORY;
        if (iArg)
            memcpy(paNew, pElement->paArgs, iArg * sizeof(paNew[0]));
        RTMemTmpFree(pElement->paArgs);
        pElement->paArgs = paNew;
    }

    int rc = VINF_SUCCESS;
    pElement->paArgs[iArg].psz     = rtVfsChainSpecDupStrN(psz, cch, &rc);
    pElement->paArgs[iArg].offSpec = offSpec;
    pElement->cArgs = iArg + 1;
    return rc;
}


RTDECL(void)    RTVfsChainSpecFree(PRTVFSCHAINSPEC pSpec)
{
    if (!pSpec)
        return;

    uint32_t i = pSpec->cElements;
    while (i-- > 0)
    {
        uint32_t iArg = pSpec->paElements[i].cArgs;
        while (iArg-- > 0)
            RTMemTmpFree(pSpec->paElements[i].paArgs[iArg].psz);
        RTMemTmpFree(pSpec->paElements[i].paArgs);
        RTMemTmpFree(pSpec->paElements[i].pszProvider);
        if (pSpec->paElements[i].hVfsObj != NIL_RTVFSOBJ)
        {
            RTVfsObjRelease(pSpec->paElements[i].hVfsObj);
            pSpec->paElements[i].hVfsObj = NIL_RTVFSOBJ;
        }
    }

    RTMemTmpFree(pSpec->paElements);
    pSpec->paElements = NULL;
    RTMemTmpFree(pSpec);
}


/**
 * Checks if @a psz is pointing to the final element specification.
 *
 * @returns true / false.
 * @param   psz         Start of an element or path.
 * @param   pcch        Where to return the length.
 */
static bool rtVfsChainSpecIsFinalElement(const char *psz, size_t *pcch)
{
    size_t off = 0;
    char   ch;
    while ((ch = psz[off]) != '\0')
    {
        if (ch == '|' || ch == ':')
            return false;
        if (   ch == '\\'
            && rtVfsChainSpecIsEscapableChar(psz[off + 1]))
            off++;
        off++;
    }
    *pcch = off;
    return off > 0;
}


/**
 * Makes the final path element.
 * @returns IPRT status code
 * @param   pElement    The element.
 * @param   pszPath     The path.
 * @param   cchPath     The path length.
 */
static int rtVfsChainSpecMakeFinalPathElement(PRTVFSCHAINELEMSPEC pElement, const char *pszPath, size_t cchPath)
{
    pElement->pszProvider = NULL;
    pElement->enmType     = RTVFSOBJTYPE_END;
    pElement->cchSpec     = (uint16_t)cchPath;
    return rtVfsChainSpecElementAddArg(pElement, pszPath, cchPath, pElement->offSpec);
}


/**
 * Finds the end of the argument string.
 *
 * @returns The offset of the end character relative to @a psz.
 * @param   psz             The argument string.
 * @param   chCloseParen    The closing parenthesis.
 */
static size_t rtVfsChainSpecFindArgEnd(const char *psz, char const chCloseParen)
{
    size_t off = 0;
    char   ch;
    while (  (ch = psz[off]) != '\0'
           && ch != ','
           && ch != chCloseParen)
    {
        if (   ch == '\\'
            && rtVfsChainSpecIsEscapableChar(psz[off+1]))
            off++;
        off++;
    }
    return off;
}


RTDECL(int) RTVfsChainSpecParse(const char *pszSpec, uint32_t fFlags, RTVFSOBJTYPE enmDesiredType,
                                PRTVFSCHAINSPEC *ppSpec, uint32_t *poffError)
{
    if (poffError)
    {
        AssertPtrReturn(poffError, VERR_INVALID_POINTER);
        *poffError = 0;
    }
    AssertPtrReturn(ppSpec, VERR_INVALID_POINTER);
    *ppSpec = NULL;
    AssertPtrReturn(pszSpec, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTVFSCHAIN_PF_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(enmDesiredType > RTVFSOBJTYPE_INVALID && enmDesiredType < RTVFSOBJTYPE_END, VERR_INVALID_PARAMETER);

    /*
     * Check the start of the specification and allocate an empty return spec.
     */
    if (strncmp(pszSpec, RTVFSCHAIN_SPEC_PREFIX, sizeof(RTVFSCHAIN_SPEC_PREFIX) - 1))
        return VERR_VFS_CHAIN_NO_PREFIX;
    const char *pszSrc = RTStrStripL(pszSpec + sizeof(RTVFSCHAIN_SPEC_PREFIX) - 1);
    if (!*pszSrc)
        return VERR_VFS_CHAIN_EMPTY;

    PRTVFSCHAINSPEC pSpec = rtVfsChainSpecAlloc();
    if (!pSpec)
        return VERR_NO_TMP_MEMORY;
    pSpec->enmDesiredType = enmDesiredType;

    /*
     * Parse the spec one element at a time.
     */
    int rc = VINF_SUCCESS;
    while (*pszSrc && RT_SUCCESS(rc))
    {
        /*
         * Digest element separator, except for the first element.
         */
        if (*pszSrc == '|' || *pszSrc == ':')
        {
            if (pSpec->cElements != 0)
                pszSrc = RTStrStripL(pszSrc + 1);
            else
            {
                rc = VERR_VFS_CHAIN_LEADING_SEPARATOR;
                break;
            }
        }
        else if (pSpec->cElements != 0)
        {
            rc = VERR_VFS_CHAIN_EXPECTED_SEPARATOR;
            break;
        }

        /*
         * Ok, there should be an element here so add one to the return struct.
         */
        PRTVFSCHAINELEMSPEC pElement = rtVfsChainSpecAddElement(pSpec, (uint16_t)(pszSrc - pszSpec), &rc);
        if (!pElement)
            break;

        /*
         * First up is the VFS object type followed by a parenthesis/curly, or
         * this could be the trailing action.  Alternatively, we could have a
         * final path-only element here.
         */
        size_t cch;
        if (strncmp(pszSrc, "base", cch = 4) == 0)
            pElement->enmType = RTVFSOBJTYPE_BASE;
        else if (strncmp(pszSrc, "vfs",  cch = 3) == 0)
            pElement->enmType = RTVFSOBJTYPE_VFS;
        else if (strncmp(pszSrc, "fss",  cch = 3) == 0)
            pElement->enmType = RTVFSOBJTYPE_FS_STREAM;
        else if (strncmp(pszSrc, "ios",  cch = 3) == 0)
            pElement->enmType = RTVFSOBJTYPE_IO_STREAM;
        else if (strncmp(pszSrc, "dir",  cch = 3) == 0)
            pElement->enmType = RTVFSOBJTYPE_DIR;
        else if (strncmp(pszSrc, "file", cch = 4) == 0)
            pElement->enmType = RTVFSOBJTYPE_FILE;
        else if (strncmp(pszSrc, "sym",  cch = 3) == 0)
            pElement->enmType = RTVFSOBJTYPE_SYMLINK;
        else
        {
            if (rtVfsChainSpecIsFinalElement(pszSrc, &cch))
                rc = rtVfsChainSpecMakeFinalPathElement(pElement, pszSrc, cch);
            else if (*pszSrc == '\0')
                rc = VERR_VFS_CHAIN_TRAILING_SEPARATOR;
            else
                rc = VERR_VFS_CHAIN_UNKNOWN_TYPE;
            break;
        }

        /* Check and skip past the parenthesis/curly.  If not there, we might
           have a final path element at our hands. */
        char const chOpenParen = pszSrc[cch];
        if (chOpenParen != '(' && chOpenParen != '{')
        {
            if (rtVfsChainSpecIsFinalElement(pszSrc, &cch))
                rc = rtVfsChainSpecMakeFinalPathElement(pElement, pszSrc, cch);
            else
                rc = VERR_VFS_CHAIN_EXPECTED_LEFT_PARENTHESES;
            break;
        }
        char const chCloseParen = (chOpenParen == '(' ? ')' : '}');
        pszSrc = RTStrStripL(pszSrc + cch + 1);

        /*
         * The name of the element provider.
         */
        cch = rtVfsChainSpecFindArgEnd(pszSrc, chCloseParen);
        if (!cch)
        {
            rc = VERR_VFS_CHAIN_EXPECTED_PROVIDER_NAME;
            break;
        }
        pElement->pszProvider = rtVfsChainSpecDupStrN(pszSrc, cch, &rc);
        if (!pElement->pszProvider)
            break;
        pszSrc += cch;

        /*
         * The arguments.
         */
        while (*pszSrc == ',')
        {
            pszSrc = RTStrStripL(pszSrc + 1);
            cch = rtVfsChainSpecFindArgEnd(pszSrc, chCloseParen);
            rc = rtVfsChainSpecElementAddArg(pElement, pszSrc, cch, (uint16_t)(pszSrc - pszSpec));
            if (RT_FAILURE(rc))
                break;
            pszSrc += cch;
        }
        if (RT_FAILURE(rc))
            break;

        /* Must end with a right parentheses/curly. */
        if (*pszSrc != chCloseParen)
        {
            rc = VERR_VFS_CHAIN_EXPECTED_RIGHT_PARENTHESES;
            break;
        }
        pElement->cchSpec = (uint16_t)(pszSrc - pszSpec) - pElement->offSpec + 1;

        pszSrc = RTStrStripL(pszSrc + 1);
    }

#if 0
    /*
     * Dump the chain.  Useful for debugging the above code.
     */
    RTAssertMsg2("dbg: cElements=%d rc=%Rrc\n", pSpec->cElements, rc);
    for (uint32_t i = 0; i < pSpec->cElements; i++)
    {
        uint32_t const cArgs = pSpec->paElements[i].cArgs;
        RTAssertMsg2("dbg: #%u: enmTypeIn=%d enmType=%d cArgs=%d",
                     i, pSpec->paElements[i].enmTypeIn, pSpec->paElements[i].enmType, cArgs);
        for (uint32_t j = 0; j < cArgs; j++)
            RTAssertMsg2(j == 0 ? (cArgs > 1 ? " [%s" : " [%s]") : j + 1 < cArgs ? ", %s" : ", %s]",
                         pSpec->paElements[i].paArgs[j].psz);
        RTAssertMsg2(" offSpec=%d cchSpec=%d", pSpec->paElements[i].offSpec, pSpec->paElements[i].cchSpec);
        RTAssertMsg2(" spec: %.*s\n", pSpec->paElements[i].cchSpec, &pszSpec[pSpec->paElements[i].offSpec]);
    }
#endif

    /*
     * Return the chain on success; Cleanup and set the error indicator on
     * failure.
     */
    if (RT_SUCCESS(rc))
        *ppSpec = pSpec;
    else
    {
        if (poffError)
            *poffError = (uint32_t)(pszSrc - pszSpec);
        RTVfsChainSpecFree(pSpec);
    }
    return rc;
}


/**
 * Looks up @a pszProvider among the registered providers.
 *
 * @returns Pointer to registration record if found, NULL if not.
 * @param   pszProvider         The provider.
 */
static PCRTVFSCHAINELEMENTREG rtVfsChainFindProviderLocked(const char *pszProvider)
{
    PCRTVFSCHAINELEMENTREG pIterator;
    RTListForEach(&g_rtVfsChainElementProviderList, pIterator, RTVFSCHAINELEMENTREG, ListEntry)
    {
        if (strcmp(pIterator->pszName, pszProvider) == 0)
            return pIterator;
    }
    return NULL;
}


/**
 * Does reusable object type matching.
 *
 * @returns true if the types matches, false if not.
 * @param   pElement        The target element specification.
 * @param   pReuseElement   The source element specification.
 */
static bool rtVfsChainMatchReusableType(PRTVFSCHAINELEMSPEC pElement, PRTVFSCHAINELEMSPEC pReuseElement)
{
    if (pElement->enmType == pReuseElement->enmType)
        return true;

    /* File objects can always be cast to I/O streams.  */
    if (   pElement->enmType == RTVFSOBJTYPE_IO_STREAM
        && pReuseElement->enmType == RTVFSOBJTYPE_FILE)
        return true;

    /* I/O stream objects may be file objects. */
    if (   pElement->enmType == RTVFSOBJTYPE_FILE
        && pReuseElement->enmType == RTVFSOBJTYPE_IO_STREAM)
    {
        RTVFSFILE hVfsFile = RTVfsObjToFile(pReuseElement->hVfsObj);
        if (hVfsFile != NIL_RTVFSFILE)
        {
            RTVfsFileRelease(hVfsFile);
            return true;
        }
    }
    return false;
}


RTDECL(int) RTVfsChainSpecCheckAndSetup(PRTVFSCHAINSPEC pSpec, PCRTVFSCHAINSPEC pReuseSpec,
                                        PRTVFSOBJ phVfsObj, const char **ppszFinalPath, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    AssertPtrReturn(poffError, VERR_INVALID_POINTER);
    *poffError = 0;
    AssertPtrReturn(phVfsObj, VERR_INVALID_POINTER);
    *phVfsObj = NIL_RTVFSOBJ;
    AssertPtrReturn(ppszFinalPath, VERR_INVALID_POINTER);
    *ppszFinalPath = NULL;
    AssertPtrReturn(pSpec, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pErrInfo, VERR_INVALID_POINTER);

    /*
     * Check for final path-only component as we will not touch it yet.
     */
    uint32_t cElements = pSpec->cElements;
    if (cElements > 0)
    {
        if (pSpec->paElements[pSpec->cElements - 1].enmType == RTVFSOBJTYPE_END)
        {
            if (cElements > 1)
                cElements--;
            else
            {
                *ppszFinalPath = pSpec->paElements[0].paArgs[0].psz;
                return VERR_VFS_CHAIN_PATH_ONLY;
            }
        }
    }
    else
        return VERR_VFS_CHAIN_EMPTY;

    /*
     * Enter the critical section after making sure it has been initialized.
     */
    int rc = RTOnce(&g_rtVfsChainElementInitOnce, rtVfsChainElementRegisterInit, NULL);
    if (RT_SUCCESS(rc))
        rc = RTCritSectRwEnterShared(&g_rtVfsChainElementCritSect);
    if (RT_SUCCESS(rc))
    {
        /*
         * Resolve and check each element first.
         */
        for (uint32_t i = 0; i < cElements; i++)
        {
            PRTVFSCHAINELEMSPEC const pElement = &pSpec->paElements[i];
            *poffError = pElement->offSpec;
            pElement->pProvider = rtVfsChainFindProviderLocked(pElement->pszProvider);
            if (pElement->pProvider)
            {
                rc = pElement->pProvider->pfnValidate(pElement->pProvider, pSpec, pElement, poffError, pErrInfo);
                if (RT_SUCCESS(rc))
                    continue;
            }
            else
                rc = VERR_VFS_CHAIN_PROVIDER_NOT_FOUND;
            break;
        }

        /*
         * Check that the desired type is compatible with the last element.
         */
        if (RT_SUCCESS(rc))
        {
            PRTVFSCHAINELEMSPEC const pLast = &pSpec->paElements[cElements - 1];
            if (cElements == pSpec->cElements)
            {
                if (   pLast->enmType == pSpec->enmDesiredType
                    || pSpec->enmDesiredType == RTVFSOBJTYPE_BASE
                    || (   pLast->enmType == RTVFSOBJTYPE_FILE
                        && pSpec->enmDesiredType == RTVFSOBJTYPE_IO_STREAM) )
                    rc = VINF_SUCCESS;
                else
                {
                    *poffError = pLast->offSpec;
                    rc = VERR_VFS_CHAIN_FINAL_TYPE_MISMATCH;
                }
            }
            /* Ends with a path-only element, so check the type of the element preceding it. */
            else if (   pLast->enmType == RTVFSOBJTYPE_DIR
                     || pLast->enmType == RTVFSOBJTYPE_VFS
                     || pLast->enmType == RTVFSOBJTYPE_FS_STREAM)
                rc = VINF_SUCCESS;
            else
            {
                *poffError = pLast->offSpec;
                rc = VERR_VFS_CHAIN_TYPE_MISMATCH_PATH_ONLY;
            }
        }

        if (RT_SUCCESS(rc))
        {
            /*
             * Try construct the chain.
             */
            RTVFSOBJ hPrevVfsObj = NIL_RTVFSOBJ; /* No extra reference, kept in chain structure. */
            for (uint32_t i = 0; i < cElements; i++)
            {
                PRTVFSCHAINELEMSPEC const pElement = &pSpec->paElements[i];
                *poffError = pElement->offSpec;

                /*
                 * Try reuse the VFS objects at the start of the passed in reuse chain.
                 */
                if (!pReuseSpec)
                { /* likely */ }
                else
                {
                    if (i < pReuseSpec->cElements)
                    {
                        PRTVFSCHAINELEMSPEC const pReuseElement = &pReuseSpec->paElements[i];
                        if (pReuseElement->hVfsObj != NIL_RTVFSOBJ)
                        {
                            if (strcmp(pElement->pszProvider, pReuseElement->pszProvider) == 0)
                            {
                                if (rtVfsChainMatchReusableType(pElement, pReuseElement))
                                {
                                    if (pElement->pProvider->pfnCanReuseElement(pElement->pProvider, pSpec, pElement,
                                                                                pReuseSpec, pReuseElement))
                                    {
                                        uint32_t cRefs = RTVfsObjRetain(pReuseElement->hVfsObj);
                                        if (cRefs != UINT32_MAX)
                                        {
                                            pElement->hVfsObj = hPrevVfsObj = pReuseElement->hVfsObj;
                                            continue;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    pReuseSpec = NULL;
                }

                /*
                 * Instantiate a new VFS object.
                 */
                RTVFSOBJ hVfsObj = NIL_RTVFSOBJ;
                rc = pElement->pProvider->pfnInstantiate(pElement->pProvider, pSpec, pElement, hPrevVfsObj,
                                                         &hVfsObj, poffError, pErrInfo);
                if (RT_FAILURE(rc))
                    break;
                pElement->hVfsObj = hVfsObj;
                hPrevVfsObj = hVfsObj;
            }

            /*
             * Add another reference to the final object and return.
             */
            if (RT_SUCCESS(rc))
            {
                uint32_t cRefs = RTVfsObjRetain(hPrevVfsObj);
                AssertStmt(cRefs != UINT32_MAX, rc = VERR_VFS_CHAIN_IPE);
                *phVfsObj      = hPrevVfsObj;
                *ppszFinalPath = cElements == pSpec->cElements ? NULL : pSpec->paElements[cElements].paArgs[0].psz;
            }
        }

        int rc2 = RTCritSectRwLeaveShared(&g_rtVfsChainElementCritSect);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }
    return rc;
}


RTDECL(int) RTVfsChainElementDeregisterProvider(PRTVFSCHAINELEMENTREG pRegRec, bool fFromDtor)
{
    /*
     * Fend off wildlife.
     */
    if (pRegRec == NULL)
        return VINF_SUCCESS;
    AssertPtrReturn(pRegRec, VERR_INVALID_POINTER);
    AssertMsgReturn(pRegRec->uVersion   == RTVFSCHAINELEMENTREG_VERSION, ("%#x", pRegRec->uVersion),    VERR_INVALID_POINTER);
    AssertMsgReturn(pRegRec->uEndMarker == RTVFSCHAINELEMENTREG_VERSION, ("%#zx", pRegRec->uEndMarker), VERR_INVALID_POINTER);
    AssertPtrReturn(pRegRec->pszName, VERR_INVALID_POINTER);

    /*
     * Take the lock if that's safe.
     */
    if (!fFromDtor)
        RTCritSectRwEnterExcl(&g_rtVfsChainElementCritSect);
    else if (!g_rtVfsChainElementProviderList.pNext)
        RTListInit(&g_rtVfsChainElementProviderList);

    /*
     * Ok, remove it.
     */
    int rc = VERR_NOT_FOUND;
    PRTVFSCHAINELEMENTREG pIterator, pIterNext;
    RTListForEachSafe(&g_rtVfsChainElementProviderList, pIterator, pIterNext, RTVFSCHAINELEMENTREG, ListEntry)
    {
        if (pIterator == pRegRec)
        {
            RTListNodeRemove(&pRegRec->ListEntry);
            rc = VINF_SUCCESS;
            break;
        }
    }

    /*
     * Leave the lock and return.
     */
    if (!fFromDtor)
        RTCritSectRwLeaveExcl(&g_rtVfsChainElementCritSect);
    return rc;
}


RTDECL(int) RTVfsChainOpenObj(const char *pszSpec, uint64_t fFileOpen, uint32_t fObjFlags,
                              PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    /*
     * Validate input.
     */
    uint32_t offErrorIgn;
    if (!poffError)
        poffError = &offErrorIgn;
    *poffError = 0;
    AssertPtrReturn(pszSpec, VERR_INVALID_POINTER);
    AssertReturn(*pszSpec != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(phVfsObj, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pErrInfo, VERR_INVALID_POINTER);

    int rc = rtFileRecalcAndValidateFlags(&fFileOpen);
    if (RT_FAILURE(rc))
        return rc;
    AssertMsgReturn(   RTPATH_F_IS_VALID(fObjFlags, RTVFSOBJ_F_VALID_MASK)
                    && (fObjFlags & RTVFSOBJ_F_CREATE_MASK) <= RTVFSOBJ_F_CREATE_DIRECTORY,
                    ("fObjFlags=%#x\n", fObjFlags),
                    VERR_INVALID_FLAGS);

    /*
     * Try for a VFS chain first, falling back on regular file system stuff if it's just a path.
     */
    PRTVFSCHAINSPEC pSpec = NULL;
    if (strncmp(pszSpec, RTVFSCHAIN_SPEC_PREFIX, sizeof(RTVFSCHAIN_SPEC_PREFIX) - 1) == 0)
    {
        rc = RTVfsChainSpecParse(pszSpec,  0 /*fFlags*/, RTVFSOBJTYPE_DIR, &pSpec, poffError);
        if (RT_FAILURE(rc))
            return rc;

        Assert(pSpec->cElements > 0);
        if (   pSpec->cElements > 1
            || pSpec->paElements[0].enmType != RTVFSOBJTYPE_END)
        {
            const char *pszFinal = NULL;
            RTVFSOBJ    hVfsObj  = NIL_RTVFSOBJ;
            pSpec->fOpenFile = fFileOpen;
            rc = RTVfsChainSpecCheckAndSetup(pSpec, NULL /*pReuseSpec*/, &hVfsObj, &pszFinal, poffError, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                if (!pszFinal)
                {
                    *phVfsObj = hVfsObj;
                    rc = VINF_SUCCESS;
                }
                else
                {
                    /*
                     * Do a file open with the final path on the returned object.
                     */
                    RTVFS           hVfs    = RTVfsObjToVfs(hVfsObj);
                    RTVFSDIR        hVfsDir = RTVfsObjToDir(hVfsObj);
                    RTVFSFSSTREAM   hVfsFss = RTVfsObjToFsStream(hVfsObj);
                    if (hVfs != NIL_RTVFS)
                        rc = RTVfsObjOpen(hVfs, pszFinal, fFileOpen, fObjFlags, phVfsObj);
                    else if (hVfsDir != NIL_RTVFSDIR)
                        rc = RTVfsDirOpenObj(hVfsDir, pszFinal, fFileOpen, fObjFlags, phVfsObj);
                    else if (hVfsFss != NIL_RTVFSFSSTREAM)
                        rc = VERR_NOT_IMPLEMENTED;
                    else
                        rc = VERR_VFS_CHAIN_TYPE_MISMATCH_PATH_ONLY;
                    RTVfsRelease(hVfs);
                    RTVfsDirRelease(hVfsDir);
                    RTVfsFsStrmRelease(hVfsFss);
                    RTVfsObjRelease(hVfsObj);
                }
            }

            RTVfsChainSpecFree(pSpec);
            return rc;
        }

        /* Only a path element. */
        pszSpec = pSpec->paElements[0].paArgs[0].psz;
    }

    /*
     * Path to regular file system.
     * Go via the directory VFS wrapper to avoid duplicating code.
     */
    RTVFSDIR hVfsParentDir = NIL_RTVFSDIR;
    const char *pszFilename;
    if (RTPathHasPath(pszSpec))
    {
        char *pszCopy = RTStrDup(pszSpec);
        if (pszCopy)
        {
            RTPathStripFilename(pszCopy);
            rc = RTVfsDirOpenNormal(pszCopy, 0 /*fOpen*/, &hVfsParentDir);
            RTStrFree(pszCopy);
        }
        else
            rc = VERR_NO_STR_MEMORY;
        pszFilename = RTPathFilename(pszSpec);
    }
    else
    {
        pszFilename = pszSpec;
        rc = RTVfsDirOpenNormal(".", 0 /*fOpen*/, &hVfsParentDir);
    }
    if (RT_SUCCESS(rc))
    {
        rc = RTVfsDirOpenObj(hVfsParentDir, pszFilename, fFileOpen, fObjFlags, phVfsObj);
        RTVfsDirRelease(hVfsParentDir);
    }

    RTVfsChainSpecFree(pSpec);
    return rc;
}


RTDECL(int) RTVfsChainOpenDir(const char *pszSpec, uint32_t fOpen,
                              PRTVFSDIR phVfsDir, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    uint32_t offErrorIgn;
    if (!poffError)
        poffError = &offErrorIgn;
    *poffError = 0;
    AssertPtrReturn(pszSpec, VERR_INVALID_POINTER);
    AssertReturn(*pszSpec != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(phVfsDir, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pErrInfo, VERR_INVALID_POINTER);

    /*
     * Try for a VFS chain first, falling back on regular file system stuff if it's just a path.
     */
    int rc;
    PRTVFSCHAINSPEC pSpec = NULL;
    if (strncmp(pszSpec, RTVFSCHAIN_SPEC_PREFIX, sizeof(RTVFSCHAIN_SPEC_PREFIX) - 1) == 0)
    {
        rc = RTVfsChainSpecParse(pszSpec,  0 /*fFlags*/, RTVFSOBJTYPE_DIR, &pSpec, poffError);
        if (RT_FAILURE(rc))
            return rc;

        Assert(pSpec->cElements > 0);
        if (   pSpec->cElements > 1
            || pSpec->paElements[0].enmType != RTVFSOBJTYPE_END)
        {
            const char *pszFinal = NULL;
            RTVFSOBJ    hVfsObj  = NIL_RTVFSOBJ;
            pSpec->fOpenFile = RTFILE_O_READ;
            rc = RTVfsChainSpecCheckAndSetup(pSpec, NULL /*pReuseSpec*/, &hVfsObj, &pszFinal, poffError, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                if (!pszFinal)
                {
                    /* Try convert it to a directory object and we're done. */
                    *phVfsDir = RTVfsObjToDir(hVfsObj);
                    if (*phVfsDir)
                        rc = VINF_SUCCESS;
                    else
                        rc = VERR_VFS_CHAIN_CAST_FAILED;
                }
                else
                {
                    /*
                     * Do a file open with the final path on the returned object.
                     */
                    RTVFS           hVfs    = RTVfsObjToVfs(hVfsObj);
                    RTVFSDIR        hVfsDir = RTVfsObjToDir(hVfsObj);
                    RTVFSFSSTREAM   hVfsFss = RTVfsObjToFsStream(hVfsObj);
                    if (hVfs != NIL_RTVFS)
                        rc = RTVfsDirOpen(hVfs, pszFinal, fOpen, phVfsDir);
                    else if (hVfsDir != NIL_RTVFSDIR)
                        rc = RTVfsDirOpenDir(hVfsDir, pszFinal, fOpen, phVfsDir);
                    else if (hVfsFss != NIL_RTVFSFSSTREAM)
                        rc = VERR_NOT_IMPLEMENTED;
                    else
                        rc = VERR_VFS_CHAIN_TYPE_MISMATCH_PATH_ONLY;
                    RTVfsRelease(hVfs);
                    RTVfsDirRelease(hVfsDir);
                    RTVfsFsStrmRelease(hVfsFss);
                }
                RTVfsObjRelease(hVfsObj);
            }

            RTVfsChainSpecFree(pSpec);
            return rc;
        }

        /* Only a path element. */
        pszSpec = pSpec->paElements[0].paArgs[0].psz;
    }

    /*
     * Path to regular file system.
     */
    rc = RTVfsDirOpenNormal(pszSpec, fOpen, phVfsDir);

    RTVfsChainSpecFree(pSpec);
    return rc;
}


RTDECL(int) RTVfsChainOpenParentDir(const char *pszSpec, uint32_t fOpen, PRTVFSDIR phVfsDir, const char **ppszChild,
                                    uint32_t *poffError, PRTERRINFO pErrInfo)
{
    uint32_t offErrorIgn;
    if (!poffError)
        poffError = &offErrorIgn;
    *poffError = 0;
    AssertPtrReturn(pszSpec, VERR_INVALID_POINTER);
    AssertReturn(*pszSpec != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(phVfsDir, VERR_INVALID_POINTER);
    AssertPtrReturn(ppszChild, VERR_INVALID_POINTER);
    *ppszChild = NULL;
    AssertPtrNullReturn(pErrInfo, VERR_INVALID_POINTER);

    /*
     * Process the spec from the end, trying to find the child part of it.
     * We cannot use RTPathFilename here because we must ignore trailing slashes.
     */
    const char * const pszEnd   = RTStrEnd(pszSpec, RTSTR_MAX);
    const char        *pszChild = pszEnd;
    while (   pszChild != pszSpec
           && RTPATH_IS_SLASH(pszChild[-1]))
        pszChild--;
    while (   pszChild != pszSpec
           && !RTPATH_IS_SLASH(pszChild[-1])
           && !RTPATH_IS_VOLSEP(pszChild[-1]))
        pszChild--;
    size_t const cchChild = pszEnd - pszChild;
    *ppszChild = pszChild;

    /*
     * Try for a VFS chain first, falling back on regular file system stuff if it's just a path.
     */
    int rc;
    PRTVFSCHAINSPEC pSpec = NULL;
    if (strncmp(pszSpec, RTVFSCHAIN_SPEC_PREFIX, sizeof(RTVFSCHAIN_SPEC_PREFIX) - 1) == 0)
    {
        rc = RTVfsChainSpecParse(pszSpec,  0 /*fFlags*/, RTVFSOBJTYPE_DIR, &pSpec, poffError);
        if (RT_FAILURE(rc))
            return rc;

        Assert(pSpec->cElements > 0);
        if (   pSpec->cElements > 1
            || pSpec->paElements[0].enmType != RTVFSOBJTYPE_END)
        {
            /*
             * Check that it ends with a path-only element and that this in turn ends with
             * what pszChild points to.  (We cannot easiy figure out the parent part of
             * an element that isn't path-only, so we don't bother trying try.)
             */
            PRTVFSCHAINELEMSPEC pLast = &pSpec->paElements[pSpec->cElements - 1];
            if (pLast->pszProvider == NULL)
            {
                size_t cchFinal = strlen(pLast->paArgs[0].psz);
                if (   cchFinal >= cchChild
                    && memcmp(&pLast->paArgs[0].psz[cchFinal - cchChild], pszChild, cchChild + 1) == 0)
                {
                    /*
                     * Drop the child part so we have a path to the parent, then setup the chain.
                     */
                    if (cchFinal > cchChild)
                        pLast->paArgs[0].psz[cchFinal - cchChild] = '\0';
                    else
                        pSpec->cElements--;

                    const char *pszFinal = NULL;
                    RTVFSOBJ    hVfsObj  = NIL_RTVFSOBJ;
                    pSpec->fOpenFile = fOpen;
                    rc = RTVfsChainSpecCheckAndSetup(pSpec, NULL /*pReuseSpec*/, &hVfsObj, &pszFinal, poffError, pErrInfo);
                    if (RT_SUCCESS(rc))
                    {
                        if (!pszFinal)
                        {
                            Assert(cchFinal == cchChild);

                            /* Try convert it to a file object and we're done. */
                            *phVfsDir = RTVfsObjToDir(hVfsObj);
                            if (*phVfsDir)
                                rc = VINF_SUCCESS;
                            else
                                rc = VERR_VFS_CHAIN_CAST_FAILED;
                        }
                        else
                        {
                            /*
                             * Do a file open with the final path on the returned object.
                             */
                            RTVFS           hVfs    = RTVfsObjToVfs(hVfsObj);
                            RTVFSDIR        hVfsDir = RTVfsObjToDir(hVfsObj);
                            RTVFSFSSTREAM   hVfsFss = RTVfsObjToFsStream(hVfsObj);
                            if (hVfs != NIL_RTVFS)
                                rc = RTVfsDirOpen(hVfs, pszFinal, fOpen, phVfsDir);
                            else if (hVfsDir != NIL_RTVFSDIR)
                                rc = RTVfsDirOpenDir(hVfsDir, pszFinal, fOpen, phVfsDir);
                            else if (hVfsFss != NIL_RTVFSFSSTREAM)
                                rc = VERR_NOT_IMPLEMENTED;
                            else
                                rc = VERR_VFS_CHAIN_TYPE_MISMATCH_PATH_ONLY;
                            RTVfsRelease(hVfs);
                            RTVfsDirRelease(hVfsDir);
                            RTVfsFsStrmRelease(hVfsFss);
                        }
                        RTVfsObjRelease(hVfsObj);
                    }
                }
                else
                    rc = VERR_VFS_CHAIN_TOO_SHORT_FOR_PARENT;
            }
            else
                rc = VERR_VFS_CHAIN_NOT_PATH_ONLY;

            RTVfsChainSpecFree(pSpec);
            return rc;
        }

        /* Only a path element. */
        pszSpec = pSpec->paElements[0].paArgs[0].psz;
    }

    /*
     * Path to regular file system.
     */
    if (RTPathHasPath(pszSpec))
    {
        char *pszCopy = RTStrDup(pszSpec);
        if (pszCopy)
        {
            RTPathStripFilename(pszCopy);
            rc = RTVfsDirOpenNormal(pszCopy, fOpen, phVfsDir);
            RTStrFree(pszCopy);
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    else
        rc = RTVfsDirOpenNormal(".", fOpen, phVfsDir);

    RTVfsChainSpecFree(pSpec);
    return rc;

}


RTDECL(int) RTVfsChainOpenFile(const char *pszSpec, uint64_t fOpen,
                               PRTVFSFILE phVfsFile, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    uint32_t offErrorIgn;
    if (!poffError)
        poffError = &offErrorIgn;
    *poffError = 0;
    AssertPtrReturn(pszSpec, VERR_INVALID_POINTER);
    AssertReturn(*pszSpec != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(phVfsFile, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pErrInfo, VERR_INVALID_POINTER);

    /*
     * Try for a VFS chain first, falling back on regular file system stuff if it's just a path.
     */
    int rc;
    PRTVFSCHAINSPEC pSpec = NULL;
    if (strncmp(pszSpec, RTVFSCHAIN_SPEC_PREFIX, sizeof(RTVFSCHAIN_SPEC_PREFIX) - 1) == 0)
    {
        rc = RTVfsChainSpecParse(pszSpec,  0 /*fFlags*/, RTVFSOBJTYPE_FILE, &pSpec, poffError);
        if (RT_FAILURE(rc))
            return rc;

        Assert(pSpec->cElements > 0);
        if (   pSpec->cElements > 1
            || pSpec->paElements[0].enmType != RTVFSOBJTYPE_END)
        {
            const char *pszFinal = NULL;
            RTVFSOBJ    hVfsObj  = NIL_RTVFSOBJ;
            pSpec->fOpenFile = fOpen;
            rc = RTVfsChainSpecCheckAndSetup(pSpec, NULL /*pReuseSpec*/, &hVfsObj, &pszFinal, poffError, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                if (!pszFinal)
                {
                    /* Try convert it to a file object and we're done. */
                    *phVfsFile = RTVfsObjToFile(hVfsObj);
                    if (*phVfsFile)
                        rc = VINF_SUCCESS;
                    else
                        rc = VERR_VFS_CHAIN_CAST_FAILED;
                }
                else
                {
                    /*
                     * Do a file open with the final path on the returned object.
                     */
                    RTVFS           hVfs    = RTVfsObjToVfs(hVfsObj);
                    RTVFSDIR        hVfsDir = RTVfsObjToDir(hVfsObj);
                    RTVFSFSSTREAM   hVfsFss = RTVfsObjToFsStream(hVfsObj);
                    if (hVfs != NIL_RTVFS)
                        rc = RTVfsFileOpen(hVfs, pszFinal, fOpen, phVfsFile);
                    else if (hVfsDir != NIL_RTVFSDIR)
                        rc = RTVfsDirOpenFile(hVfsDir, pszFinal, fOpen, phVfsFile);
                    else if (hVfsFss != NIL_RTVFSFSSTREAM)
                        rc = VERR_NOT_IMPLEMENTED;
                    else
                        rc = VERR_VFS_CHAIN_TYPE_MISMATCH_PATH_ONLY;
                    RTVfsRelease(hVfs);
                    RTVfsDirRelease(hVfsDir);
                    RTVfsFsStrmRelease(hVfsFss);
                }
                RTVfsObjRelease(hVfsObj);
            }

            RTVfsChainSpecFree(pSpec);
            return rc;
        }

        /* Only a path element. */
        pszSpec = pSpec->paElements[0].paArgs[0].psz;
    }

    /*
     * Path to regular file system.
     */
    RTFILE hFile;
    rc = RTFileOpen(&hFile, pszSpec, fOpen);
    if (RT_SUCCESS(rc))
    {
        RTVFSFILE hVfsFile;
        rc = RTVfsFileFromRTFile(hFile, fOpen, false /*fLeaveOpen*/, &hVfsFile);
        if (RT_SUCCESS(rc))
            *phVfsFile = hVfsFile;
        else
            RTFileClose(hFile);
    }

    RTVfsChainSpecFree(pSpec);
    return rc;
}


RTDECL(int) RTVfsChainOpenIoStream(const char *pszSpec, uint64_t fOpen,
                                   PRTVFSIOSTREAM phVfsIos, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    uint32_t offErrorIgn;
    if (!poffError)
        poffError = &offErrorIgn;
    *poffError = 0;
    AssertPtrReturn(pszSpec, VERR_INVALID_POINTER);
    AssertReturn(*pszSpec != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(phVfsIos, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pErrInfo, VERR_INVALID_POINTER);

    /*
     * Try for a VFS chain first, falling back on regular file system stuff if it's just a path.
     */
    int rc;
    PRTVFSCHAINSPEC pSpec = NULL;
    if (strncmp(pszSpec, RTVFSCHAIN_SPEC_PREFIX, sizeof(RTVFSCHAIN_SPEC_PREFIX) - 1) == 0)
    {
        rc = RTVfsChainSpecParse(pszSpec,  0 /*fFlags*/, RTVFSOBJTYPE_IO_STREAM, &pSpec, poffError);
        if (RT_FAILURE(rc))
            return rc;

        Assert(pSpec->cElements > 0);
        if (   pSpec->cElements > 1
            || pSpec->paElements[0].enmType != RTVFSOBJTYPE_END)
        {
            const char *pszFinal = NULL;
            RTVFSOBJ    hVfsObj  = NIL_RTVFSOBJ;
            pSpec->fOpenFile = fOpen;
            rc = RTVfsChainSpecCheckAndSetup(pSpec, NULL /*pReuseSpec*/, &hVfsObj, &pszFinal, poffError, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                if (!pszFinal)
                {
                    /* Try convert it to an I/O object and we're done. */
                    *phVfsIos = RTVfsObjToIoStream(hVfsObj);
                    if (*phVfsIos)
                        rc = VINF_SUCCESS;
                    else
                        rc = VERR_VFS_CHAIN_CAST_FAILED;
                }
                else
                {
                    /*
                     * Do a file open with the final path on the returned object.
                     */
                    RTVFS           hVfs    = RTVfsObjToVfs(hVfsObj);
                    RTVFSDIR        hVfsDir = RTVfsObjToDir(hVfsObj);
                    RTVFSFSSTREAM   hVfsFss = RTVfsObjToFsStream(hVfsObj);
                    RTVFSFILE       hVfsFile = NIL_RTVFSFILE;
                    if (hVfs != NIL_RTVFS)
                        rc = RTVfsFileOpen(hVfs, pszFinal, fOpen, &hVfsFile);
                    else if (hVfsDir != NIL_RTVFSDIR)
                        rc = RTVfsDirOpenFile(hVfsDir, pszFinal, fOpen, &hVfsFile);
                    else if (hVfsFss != NIL_RTVFSFSSTREAM)
                        rc = VERR_NOT_IMPLEMENTED;
                    else
                        rc = VERR_VFS_CHAIN_TYPE_MISMATCH_PATH_ONLY;
                    if (RT_SUCCESS(rc))
                    {
                        *phVfsIos = RTVfsFileToIoStream(hVfsFile);
                        if (*phVfsIos)
                            rc = VINF_SUCCESS;
                        else
                            rc = VERR_VFS_CHAIN_CAST_FAILED;
                        RTVfsFileRelease(hVfsFile);
                    }
                    RTVfsRelease(hVfs);
                    RTVfsDirRelease(hVfsDir);
                    RTVfsFsStrmRelease(hVfsFss);
                }
                RTVfsObjRelease(hVfsObj);
            }

            RTVfsChainSpecFree(pSpec);
            return rc;
        }

        /* Only a path element. */
        pszSpec = pSpec->paElements[0].paArgs[0].psz;
    }

    /*
     * Path to regular file system.
     */
    RTFILE hFile;
    rc = RTFileOpen(&hFile, pszSpec, fOpen);
    if (RT_SUCCESS(rc))
    {
        RTVFSFILE hVfsFile;
        rc = RTVfsFileFromRTFile(hFile, fOpen, false /*fLeaveOpen*/, &hVfsFile);
        if (RT_SUCCESS(rc))
        {
            *phVfsIos = RTVfsFileToIoStream(hVfsFile);
            RTVfsFileRelease(hVfsFile);
        }
        else
            RTFileClose(hFile);
    }

    RTVfsChainSpecFree(pSpec);
    return rc;
}


/**
 * The equivalent of RTPathQueryInfoEx
 */
RTDECL(int) RTVfsChainQueryInfo(const char *pszSpec, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs,
                                uint32_t fFlags, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    uint32_t offErrorIgn;
    if (!poffError)
        poffError = &offErrorIgn;
    *poffError = 0;
    AssertPtrReturn(pszSpec, VERR_INVALID_POINTER);
    AssertReturn(*pszSpec != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
    AssertReturn(enmAdditionalAttribs >= RTFSOBJATTRADD_NOTHING && enmAdditionalAttribs <= RTFSOBJATTRADD_LAST,
                 VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pErrInfo, VERR_INVALID_POINTER);

    /*
     * Try for a VFS chain first, falling back on regular file system stuff if it's just a path.
     */
    int rc;
    PRTVFSCHAINSPEC pSpec = NULL;
    if (strncmp(pszSpec, RTVFSCHAIN_SPEC_PREFIX, sizeof(RTVFSCHAIN_SPEC_PREFIX) - 1) == 0)
    {
        rc = RTVfsChainSpecParse(pszSpec,  0 /*fFlags*/, RTVFSOBJTYPE_BASE, &pSpec, poffError);
        if (RT_FAILURE(rc))
            return rc;

        Assert(pSpec->cElements > 0);
        if (   pSpec->cElements > 1
            || pSpec->paElements[0].enmType != RTVFSOBJTYPE_END)
        {
            const char *pszFinal = NULL;
            RTVFSOBJ    hVfsObj  = NIL_RTVFSOBJ;
            pSpec->fOpenFile = RTFILE_O_READ | RTFILE_O_OPEN;
            rc = RTVfsChainSpecCheckAndSetup(pSpec, NULL /*pReuseSpec*/, &hVfsObj, &pszFinal, poffError, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                if (!pszFinal)
                {
                    /*
                     * Do the job on the final object.
                     */
                    rc = RTVfsObjQueryInfo(hVfsObj, pObjInfo, enmAdditionalAttribs);
                }
                else
                {
                    /*
                     * Do a path query operation on the penultimate object.
                     */
                    RTVFS           hVfs    = RTVfsObjToVfs(hVfsObj);
                    RTVFSDIR        hVfsDir = RTVfsObjToDir(hVfsObj);
                    RTVFSFSSTREAM   hVfsFss = RTVfsObjToFsStream(hVfsObj);
                    if (hVfs != NIL_RTVFS)
                        rc = RTVfsQueryPathInfo(hVfs, pszFinal, pObjInfo, enmAdditionalAttribs, fFlags);
                    else if (hVfsDir != NIL_RTVFSDIR)
                        rc = RTVfsDirQueryPathInfo(hVfsDir, pszFinal, pObjInfo, enmAdditionalAttribs, fFlags);
                    else if (hVfsFss != NIL_RTVFSFSSTREAM)
                        rc = VERR_NOT_SUPPORTED;
                    else
                        rc = VERR_VFS_CHAIN_TYPE_MISMATCH_PATH_ONLY;
                    RTVfsRelease(hVfs);
                    RTVfsDirRelease(hVfsDir);
                    RTVfsFsStrmRelease(hVfsFss);
                }
                RTVfsObjRelease(hVfsObj);
            }

            RTVfsChainSpecFree(pSpec);
            return rc;
        }

        /* Only a path element. */
        pszSpec = pSpec->paElements[0].paArgs[0].psz;
    }

    /*
     * Path to regular file system.
     */
    rc = RTPathQueryInfoEx(pszSpec, pObjInfo, enmAdditionalAttribs, fFlags);

    RTVfsChainSpecFree(pSpec);
    return rc;
}


RTDECL(bool) RTVfsChainIsSpec(const char *pszSpec)
{
    return pszSpec
        && strncmp(pszSpec, RT_STR_TUPLE(RTVFSCHAIN_SPEC_PREFIX)) == 0;
}


RTDECL(int) RTVfsChainQueryFinalPath(const char *pszSpec, char **ppszFinalPath, uint32_t *poffError)
{
    /* Make sure we've got an error info variable. */
    uint32_t offErrorIgn;
    if (!poffError)
        poffError = &offErrorIgn;
    *poffError = 0;

    /*
     * If not chain specifier, just duplicate the input and return.
     */
    if (strncmp(pszSpec, RTVFSCHAIN_SPEC_PREFIX, sizeof(RTVFSCHAIN_SPEC_PREFIX) - 1) != 0)
        return RTStrDupEx(ppszFinalPath, pszSpec);

    /*
     * Parse it and check out the last element.
     */
    PRTVFSCHAINSPEC pSpec = NULL;
    int rc = RTVfsChainSpecParse(pszSpec,  0 /*fFlags*/, RTVFSOBJTYPE_BASE, &pSpec, poffError);
    if (RT_SUCCESS(rc))
    {
        PCRTVFSCHAINELEMSPEC pLast = &pSpec->paElements[pSpec->cElements - 1];
        if (pLast->pszProvider == NULL)
            rc = RTStrDupEx(ppszFinalPath, pLast->paArgs[0].psz);
        else
        {
            rc = VERR_VFS_CHAIN_NOT_PATH_ONLY;
            *poffError = pLast->offSpec;
        }
        RTVfsChainSpecFree(pSpec);
    }
    return rc;
}


RTDECL(int) RTVfsChainSplitOffFinalPath(char *pszSpec, char **ppszSpec, char **ppszFinalPath, uint32_t *poffError)
{
    /* Make sure we've got an error info variable. */
    uint32_t offErrorIgn;
    if (!poffError)
        poffError = &offErrorIgn;
    *poffError = 0;

    /*
     * If not chain specifier, just duplicate the input and return.
     */
    if (strncmp(pszSpec, RTVFSCHAIN_SPEC_PREFIX, sizeof(RTVFSCHAIN_SPEC_PREFIX) - 1) != 0)
    {
        *ppszSpec      = NULL;
        *ppszFinalPath = pszSpec;
        return VINF_SUCCESS;
    }

    /*
     * Parse it and check out the last element.
     */
    PRTVFSCHAINSPEC pSpec = NULL;
    int rc = RTVfsChainSpecParse(pszSpec,  0 /*fFlags*/, RTVFSOBJTYPE_BASE, &pSpec, poffError);
    if (RT_SUCCESS(rc))
    {
        Assert(pSpec->cElements > 0);
        PCRTVFSCHAINELEMSPEC pLast = &pSpec->paElements[pSpec->cElements - 1];
        if (pLast->pszProvider == NULL)
        {
            char *psz = &pszSpec[pLast->offSpec];
            *ppszFinalPath = psz;
            if (pSpec->cElements > 1)
            {
                *ppszSpec = pszSpec;

                /* Remove the separator and any whitespace around it. */
                while (   psz != pszSpec
                       && RT_C_IS_SPACE(psz[-1]))
                    psz--;
                if (    psz != pszSpec
                    && (   psz[-1] == ':'
                        || psz[-1] == '|'))
                    psz--;
                while (   psz != pszSpec
                       && RT_C_IS_SPACE(psz[-1]))
                    psz--;
                *psz = '\0';
            }
            else
                *ppszSpec = NULL;
        }
        else
        {
            *ppszFinalPath = NULL;
            *ppszSpec      = pszSpec;
        }
        RTVfsChainSpecFree(pSpec);
    }
    else
    {
        *ppszSpec      = NULL;
        *ppszFinalPath = NULL;
    }
    return rc;
}

