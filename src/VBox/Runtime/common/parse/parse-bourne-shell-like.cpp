/* $Id: parse-bourne-shell-like.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTParseShell - Bourne-like Shell Parser.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#include "internal/iprt.h"
#include <iprt/parseshell.h>

#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTPARSESRCFILE
{
    /** For looking up by the filename string (case sensitive). */
    RTSTRSPACECORE      ByName;
    /** For looking up by the file ID. */
    AVLU32NODECORE      ById;
    /** The filename. */
    char                szFilename[RT_FLEXIBLE_ARRAY];
} RTPARSESRCFILE;
typedef RTPARSESRCFILE *PRTPARSESRCFILE;

typedef struct RTPARSESRCMGR
{
    /** String space managing the filenames. */
    RTSTRSPACE          hStrSpace;
    /** By ID AVL tree root. */
    AVLU32TREE          IdTreeRoot;
    /** The next filename ID. */
    uint32_t            idNext;
} RTPARSESRCMGR;
typedef RTPARSESRCMGR *PRTPARSESRCMGR;



/**
 * Instance data for a bourne-like shell parser.
 */
typedef struct RTPARSESHELLINT
{
    /** Magic value (RTPARSESHELLINT_MAGIC). */
    uint32_t            uMagic;
    /** Reference count. */
    uint32_t volatile   cRefs;
    /** Parser flags, RTPARSESHELL_F_XXX. */
    uint64_t            fFlags;
    /** Source name manager. */
    RTPARSESRCMGR       SrcMgr;
    /** Parser instance name (for logging/whatever). */
    char                szName[RT_FLEXIBLE_ARRAY];
} RTPARSESHELLINT;

/** Magic value for RTPARSESHELLINT. */
#define RTPARSESHELLINT_MAGIC   UINT32_C(0x18937566)


typedef enum RTPARSESHELLTOKEN
{
    RTPARSESHELLTOKEN_INVALID = 0,
    RTPARSESHELLTOKEN_EOF,
    RTPARSESHELLTOKEN_NEWLINE,      /**< Newline (\n) */
    RTPARSESHELLTOKEN_NOT,          /**< ! cmd */
    RTPARSESHELLTOKEN_BACKGROUND,   /**< cmd & */
    RTPARSESHELLTOKEN_AND,          /**< cmd1 && cmd2 */
    RTPARSESHELLTOKEN_PIPE,         /**< cmd1 | cmd2 */
    RTPARSESHELLTOKEN_OR,           /**< cmd1 || cmd2 */
    RTPARSESHELLTOKEN_SEMICOLON,    /**< cmd ; */
    RTPARSESHELLTOKEN_END_CASE      /**< pattern) cmd ;; */
};

/**
 * The parser state.
 */
typedef struct RTPARSESHELLSTATE
{
    /** Pointer to the buffer of bytes to parse. */
    const char         *pchSrc;
    /** Number of characters (bytes) to parse starting with pchSrc. */
    size_t              cchSrc;
    /** Current source offset. */
    size_t              offSrc;
    /** Offset relative to pchSrc of the start of the current line. */
    size_t              offSrcLineStart;
    /** Current line number. */
    uint32_t            iLine;
    /** The source file ID. */
    uint32_t            idSrcFile;
    /** The parser instance. */
    PRTPARSESHELLSTATE  pParser;
} RTPARSESHELLSTATE;
typedef RTPARSESHELLSTATE *PRTPARSESHELLSTATE;



static void rtParseSrcMgrInit(PRTPARSESRCMGR pSrcMgr)
{
    pSrcMgr->hStrSpace  = NULL;
    pSrcMgr->IdTreeRoot = NULL;
    pSrcMgr->idNext     = 1;
}


/**
 * @callback_method_impl{AVLU32CALLBACK, Worker for rtParseSrcMgrDelete.}
 */
static DECLCALLBACK(int) AVLU32CALLBACK,(PAVLU32NODECORE pNode, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTPARSESRCFILE pSrcFile = RT_FROM_MEMBER(pNode, RTPARSESRCFILE, ById);
    RTMemFree(pSrcFile);
}


static void rtParseSrcMgrDelete(PRTPARSESRCMGR pSrcMgr)
{
    RTAvlU32Destroy(pSrcMgr->IdTreeRoot, rtParseSrcMgrDeleteSrcFile, NULL);
    pSrcMgr->hStrSpace  = NULL;
    pSrcMgr->IdTreeRoot = NULL;
    pSrcMgr->idNext     = 1;
}


static int32_t rtParseSrcMgrFilenameToIdx(PRTPARSESRCMGR pSrcMgr, const char *pszFilename)
{
    /*
     * Look it up.
     */
    RTSTRSPACECORE pStrCore = RTStrSpaceGet(pSrcMgr->hStrSpace, pszFilename);
    if (pStrCore)
    {
        PRTPARSESRCFILE pSrcFile = RT_FROM_MEMBER(pNode, RTPARSESRCFILE, ByName);
        AssertMsg(pSrcFile->ById.Key > 0 && pSrcFile->ById.Key < UINT16_MAX, ("%#x\n", pSrcFile->ById.Key));
        return pSrcFile->ById.Key;
    }

    /*
     * Add it.
     */
    uint32_t const idFile = pSrcMgr->idNext;
    AssertReturn(idFile < UINT16_MAX, VERR_TOO_MANY_OPEN_FILES);

    size_t const cchFilename = strlen(pszFilename);
    PRTPARSESRCFILE pSrcFile = (PRTPARSESRCFILE)RTMemAllocZVar(RT_UOFFSETOF_DYN(RTPARSESRCFILE, szFilename[cchFilename + 1]));
    if (pSrcFile)
    {
        memcpy(&pSrcFile->szFilename[0], pszFilename, cchFilename);
        if (RTStrSpaceInsert(pSrcMgr->hStrSpace, &pSrcFile->ByName))
        {
            pSrcFile->ById.Key = idFile;
            if (RTAvlU32Insert(&pSrcMgr->IdTreeRoot, &pSrcFile->ById))
            {
                pSrcMgr->idNext = idFile + 1;
                return (int32_t)idFile;
            }
            AssertFailed();
            RTStrSpaceRemove(pSrcMgr->hStrSpace, &pSrcFile->szFilename);
        }
        else
            AssertFailed();
        RTMemFree(pSrcFile);
        return VERR_INTERNAL_ERROR_2;
    }
    return VERR_NO_MEMORY;
}


RTDECL(int) RTParseShellCreate(PRTPARSESHELL phParser, uint64_t fFlags, const char *pszName)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phParser, VERR_INVALID_POINTER);
    *phParser = NIL_RTPARSESHELL;

    AssertPtrReturn(!(fFlags & ~0), VERR_INVALID_FLAGS);

    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    size_t const cchName = strlen(pszName);
    AssertReturn(cchName > 0 && cchName < 1024, VERR_OUT_OF_RANGE);

    /*
     * Allocate and initialize the parser instance.
     */
    RTPARSESHELLINT *pThis = (RTPARSESHELLINT *)RTMemAllocZVar(RT_UOFFSETOF_DYN(RTPARSESHELLINT, szName[cchName + 1]));
    AssertReturn(pThis, VERR_NO_MEMORY);

    pThis->uMagic = RTPARSESHELLINT_MAGIC;
    pThis->cRefs  = 1;
    pThis->fFlags = fFlags;
    memcpy(pThis->szName, pszName, cchName);

    rtParseSrcMgrInit(&pThis->SrcMgr);

    /*
     * Return success.
     */
    *phParser = pThis;
    return VINF_SUCCESS;
}


RTDECL(uint32_t) RTParseShellRelease(RTPARSESHELL hParser)
{
    if (hParser == NIL_RTPARSESHELL)
        return 0;

    RTPARSESHELLINT * const pThis = hParser;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertPtrReturn(pThis->uMagic == RTPARSESHELLINT_MAGIC, UINT32_MAX);

    uint32_t const cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < _4K);
    if (cRefs == 0)
    {
        pThis->uMagic = ~RTPARSESHELLINT_MAGIC;
        pThis->szName[0] = '~';
        rtParseSrcMgrDelete(&pThis->SrcMgr);
        RTMemFree(pThis);
    }
    return cRefs;
}


RTDECL(uint32_t) RTParseShellRetain(RTPARSESHELL hParser)
{
    RTPARSESHELLINT * const pThis = hParser;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertPtrReturn(pThis->uMagic == RTPARSESHELLINT_MAGIC, UINT32_MAX);

    uint32_t const cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs > 1);
    Assert(cRefs < _4K);
    return cRefs;
}


static rtParseShellGetToken(P RTPARSESHELLSTATE pState)
{

}


static int rtParseShellStringInt(RTPARSESHELLINT *pThis, const char *pchString, size_t cchString,
                                 const char *pszFilename, uint32_t iLine, uint32_t fFlags,
                                 PRTLISTANCHOR pNodeList, PRTERRINFO pErrInfo)
{
    /*
     * Add the source file to the source manager.
     */
    int32_t const idFile = rtParseSrcMgrFilenameToIdx(&pThis->SrcMgr, pszFilename);
    AssertReturn(idFile > 0, idFile);

    /*
     *
     */


}


static void rtParseTreeDestroyList(PRTLISTANCHOR pNodeList)
{
    RT_NOREF(pNodeList);
}



RTDECL(int) RTParseShellString(RTPARSESHELL hParser, const char *pchString, size_t cchString,
                               const char *pszFilename, uint32_t iStartLine, uint32_t fFlags,
                               PRTLISTANCHOR pNodeList, PRTERRINFO pErrInfo)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pNodeList, VERR_INVALID_POINTER);
    RTListInit(pNodeList);

    RTPARSESHELLINT * const pThis = hParser;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertPtrReturn(pThis->uMagic == RTPARSESHELLINT_MAGIC, UINT32_MAX);

    AssertPtrReturn(pchString, VERR_INVALID_POINTER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertPtrReturn(fFlags & ~(UINT32_C(0)), VERR_INVALID_FLAGS);

    /*
     * Call common function for doing the parsing.
     */
    int rc = rtParseShellStringInt(pThis, pchString, cchString, pszFilename, iStartLine, fFlags, pNodeList, pErrInfo);
    if (RT_FAILURE(rc))
        rtParseTreeDestroyList(pNodeList);
    return rc;
}


RTDECL(uint32_t) RTParseShellFile(RTPARSESHELL hParser, const char *pszFilename, uint32_t fFlags,
                                  PRTLISTANCHOR pNodeList, PRTERRINFO pErrInfo)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pNodeList, VERR_INVALID_POINTER);
    RTListInit(pNodeList);

    RTPARSESHELLINT * const pThis = hParser;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertPtrReturn(pThis->uMagic == RTPARSESHELLINT_MAGIC, UINT32_MAX);

    AssertPtrReturn(pchString, VERR_INVALID_POINTER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertPtrReturn(fFlags & ~(UINT32_C(0)), VERR_INVALID_FLAGS);

    /*
     * Read the file into memory and hand it over to the common parser function.
     */
    void  *pvFile = NULL;
    size_t cbFile = 0;
    int rc = RTFileReadAll(pszFilename, &pvFile, &cbFile)
    if (RT_SUCCESS(rc))
    {
        rc = RTStrValidateEncodingEx((char *)pvFile, cbFile, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
        if (RT_SUCCESS(rc))
        {
            /*
             * Call common function for doing the parsing.
             */
            rc = rtParseShellStringInt(pThis, pchString, cchString, pszFilename, 1 /*iStartLine*/, fFlags, pNodeList, pErrInfo);
            if (RT_FAILURE(rc))
                rtParseTreeDestroyList(pNodeList);
        }
        else
            rc = RTErrInfoSetF(pErrInfo, rc, "RTStrValidateEncodingEx failed on %s: %Rrc", pszFilename, rc);
        RTFileReadAllFree(pvFile, cbFile);
    }
    return rc;
}

