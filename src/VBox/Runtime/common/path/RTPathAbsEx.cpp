/* $Id: RTPathAbsEx.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathAbsEx and RTPathAbs.
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
#define LOG_GROUP RTLOGGROUP_PATH
#include "internal/iprt.h"
#include <iprt/path.h>

#include <iprt/err.h>
#include <iprt/ctype.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include "internal/path.h"



/**
 * Ensures that the drive letter is capitalized (prereq: RTPATH_PROP_VOLUME).
 */
DECLINLINE(void) rtPathAbsExUpperCaseDriveLetter(char *pszAbsPath)
{
    AssertReturnVoid(pszAbsPath[1] == ':');
    char ch = *pszAbsPath;
    AssertReturnVoid(RT_C_IS_ALPHA(ch));
    *pszAbsPath = RT_C_TO_UPPER(ch);
}


/**
 * Common worker for relative paths.
 *
 * Uses RTPATHABS_F_STOP_AT_BASE for RTPATHABS_F_STOP_AT_CWD.
 */
static int rtPathAbsExWithCwdOrBaseCommon(const char *pszBase, size_t cchBaseInPlace, PRTPATHPARSED pBaseParsed,
                                          const char *pszPath, PRTPATHPARSED pParsed, uint32_t fFlags,
                                          char *pszAbsPath, size_t *pcbAbsPath)
{
    AssertReturn(pBaseParsed->cComps > 0, VERR_INVALID_PARAMETER);

    /*
     * Clean up the base path first if necessary.
     *
     * Note! UNC tries to preserve the first two elements in the base path,
     *       unless it's a \\.\ or \\?\ prefix.
     */
    uint32_t const iBaseStop = (pBaseParsed->fProps & (RTPATH_PROP_UNC | RTPATH_PROP_SPECIAL_UNC)) != RTPATH_PROP_UNC
                            || pBaseParsed->cComps < 2 ? 0 : 1;
    uint32_t       iBaseLast = iBaseStop;
    if (pBaseParsed->fProps & (RTPATH_PROP_DOT_REFS | RTPATH_PROP_DOTDOT_REFS))
    {
        uint32_t const cComps = pBaseParsed->cComps;
        uint32_t       i      = iBaseStop + 1;
        while (i < cComps)
        {
            uint32_t const cchComp = pBaseParsed->aComps[i].cch;
            if (   cchComp > 2
                || pszPath[pBaseParsed->aComps[i].off] != '.'
                || (cchComp == 2 && pszPath[pBaseParsed->aComps[i].off + 1] != '.') )
                iBaseLast = i;
            else
            {
                Assert(cchComp == 1 || cchComp == 2);
                pBaseParsed->aComps[i].cch = 0;
                if (cchComp == 2)
                {
                    while (iBaseLast > 0 && pBaseParsed->aComps[iBaseLast].cch == 0)
                        iBaseLast--;
                    if (iBaseLast > iBaseStop)
                    {
                        Assert(pBaseParsed->aComps[iBaseLast].cch != 0);
                        pBaseParsed->aComps[iBaseLast].cch = 0;
                        iBaseLast--;
                    }
                }
            }
            i++;
        }
        Assert(iBaseLast < cComps);
    }
    else
        iBaseLast = pBaseParsed->cComps - 1;

    /*
     * Clean up the path next if needed.
     */
    int32_t iLast = -1; /* Is signed here! */
    if (pParsed->fProps & (RTPATH_PROP_DOT_REFS | RTPATH_PROP_DOTDOT_REFS))
    {
        uint32_t const cComps = pParsed->cComps;
        uint32_t       i      = 0;

        /* If we have a volume specifier, take it from the base path. */
        if (pParsed->fProps & RTPATH_PROP_VOLUME)
            pParsed->aComps[i++].cch = 0;

        while (i < cComps)
        {
            uint32_t const cchComp = pParsed->aComps[i].cch;
            if (   cchComp > 2
                || pszPath[pParsed->aComps[i].off] != '.'
                || (cchComp == 2 && pszPath[pParsed->aComps[i].off + 1] != '.') )
                iLast = i;
            else
            {
                Assert(cchComp == 1 || cchComp == 2);
                pParsed->aComps[i].cch = 0;
                if (cchComp == 2)
                {
                    while (iLast >= 0 && pParsed->aComps[iLast].cch == 0)
                        iLast--;
                    if (iLast >= 0)
                    {
                        Assert(pParsed->aComps[iLast].cch != 0);
                        pParsed->aComps[iLast].cch = 0;
                        iLast--;
                    }
                    else if (   iBaseLast > iBaseStop
                             && !(fFlags & RTPATHABS_F_STOP_AT_BASE))
                    {
                        while (iBaseLast > iBaseStop && pBaseParsed->aComps[iBaseLast].cch == 0)
                            iBaseLast--;
                        if (iBaseLast > iBaseStop)
                        {
                            Assert(pBaseParsed->aComps[iBaseLast].cch != 0);
                            pBaseParsed->aComps[iBaseLast].cch = 0;
                            iBaseLast--;
                        }
                    }
                }
            }
            i++;
        }
        Assert(iLast < (int32_t)cComps);
    }
    else
    {
        /* If we have a volume specifier, take it from the base path. */
        iLast = pParsed->cComps - 1;
        if (pParsed->fProps & RTPATH_PROP_VOLUME)
        {
            pParsed->aComps[0].cch = 0;
            if (iLast == 0)
                iLast = -1;
        }
    }

    /*
     * Do we need a trailing slash in the base?
     * If nothing is taken from pszPath, preserve its trailing slash,
     * otherwise make sure there is a slash for joining the two.
     */
    Assert(!(pParsed->fProps & RTPATH_PROP_ROOT_SLASH));
    if (pBaseParsed->cComps == 1)
    {
        AssertReturn(pBaseParsed->fProps & RTPATH_PROP_ROOT_SLASH, VERR_PATH_DOES_NOT_START_WITH_ROOT);
        Assert(!(pBaseParsed->fProps & RTPATH_PROP_DIR_SLASH));
    }
    else
    {
        Assert(pBaseParsed->cComps > 1);
        if (   iLast >= 0
            || (pParsed->fProps & RTPATH_PROP_DIR_SLASH)
            || (fFlags & RTPATHABS_F_ENSURE_TRAILING_SLASH) )
            pBaseParsed->fProps |= RTPATH_PROP_DIR_SLASH;
        else
            pBaseParsed->fProps &= ~RTPATH_PROP_DIR_SLASH;
    }

    /* Apply the trailing flash flag to the input path: */
    if (   iLast >= 0
        && (fFlags & RTPATHABS_F_ENSURE_TRAILING_SLASH))
        pParsed->fProps |= RTPATH_PROP_DIR_SLASH;

    /*
     * Combine the two.  RTPathParsedReassemble can handle in place stuff, as
     * long as the path doesn't grow.
     */
    int rc = RTPathParsedReassemble(pszBase, pBaseParsed, fFlags & RTPATH_STR_F_STYLE_MASK, pszAbsPath, *pcbAbsPath);
    if (RT_SUCCESS(rc))
    {
        if (pBaseParsed->fProps & RTPATH_PROP_VOLUME)
            rtPathAbsExUpperCaseDriveLetter(pszAbsPath);

        cchBaseInPlace = pBaseParsed->cchPath;
        Assert(cchBaseInPlace == strlen(pszAbsPath));
        if (iLast >= 0)
        {
            rc = RTPathParsedReassemble(pszPath, pParsed, fFlags & RTPATH_STR_F_STYLE_MASK,
                                        &pszAbsPath[cchBaseInPlace], *pcbAbsPath - cchBaseInPlace);
            if (RT_SUCCESS(rc))
            {
                *pcbAbsPath = cchBaseInPlace + pParsed->cchPath;
                Assert(*pcbAbsPath == strlen(pszAbsPath));
            }
            else
                *pcbAbsPath = cchBaseInPlace + pParsed->cchPath + 1;
        }
        else
            *pcbAbsPath = cchBaseInPlace;
    }
    else if (rc == VERR_BUFFER_OVERFLOW)
    {
        if (iLast >= 0)
        {
            RTPathParsedReassemble(pszPath, pParsed, fFlags & RTPATH_STR_F_STYLE_MASK, pszAbsPath, 0);
            *pcbAbsPath = pBaseParsed->cchPath + pParsed->cchPath + 1;
        }
        else
            *pcbAbsPath = pBaseParsed->cchPath + 1;
    }

    return rc;
}


/**
 * Handles the no-root-path scenario where we do CWD prefixing.
 */
static int rtPathAbsExWithCwd(const char *pszPath, PRTPATHPARSED pParsed, uint32_t fFlags, char *pszAbsPath, size_t *pcbAbsPath)
{
    /*
     * Get the current directory and place it in the output buffer.
     */
    size_t cchInPlace;
    size_t cbCwd      = *pcbAbsPath;
    char  *pszCwdFree = NULL;
    char  *pszCwd     = pszAbsPath;
    int    rc;
    if (   !(fFlags & RTPATH_STR_F_STYLE_DOS)
        || (pParsed->fProps & (RTPATH_PROP_VOLUME | RTPATH_PROP_ROOT_SLASH)) != RTPATH_PROP_VOLUME )
        rc = RTPathGetCurrent(pszCwd, cbCwd);
    else
        rc = RTPathGetCurrentOnDrive(pszPath[0], pszCwd, cbCwd);
    if (RT_SUCCESS(rc))
        cchInPlace = strlen(pszCwd);
    else if (rc == VERR_BUFFER_OVERFLOW)
    {
        /* Allocate a big temporary buffer so we can return the correct length
           (the destination buffer might even be big enough if pszPath includes
           sufficient '..' entries). */
        cchInPlace = 0;
        cbCwd      = RT_MAX(cbCwd * 4, RTPATH_BIG_MAX);
        pszCwdFree = pszCwd = (char *)RTMemTmpAlloc(cbCwd);
        if (pszCwdFree)
        {
            if (   !(fFlags & RTPATH_STR_F_STYLE_DOS)
                || (pParsed->fProps & (RTPATH_PROP_VOLUME | RTPATH_PROP_ROOT_SLASH)) != RTPATH_PROP_VOLUME )
                rc = RTPathGetCurrent(pszCwd, cbCwd);
            else
                rc = RTPathGetCurrentOnDrive(pszPath[0], pszCwd, cbCwd);
            if (RT_FAILURE(rc))
            {
                if (rc == VERR_BUFFER_OVERFLOW)
                    rc = VERR_FILENAME_TOO_LONG;
                RTMemTmpFree(pszCwdFree);
                return rc;
            }
        }
        else
        {
            *pcbAbsPath = cbCwd + 1 + pParsed->cchPath + 1;
            return rc;
        }
    }
    else
        return rc;

    /*
     * Parse the path.
     */
    union
    {
        RTPATHPARSED Parsed;
        uint8_t      abPadding[1024];
    } uCwd;
    PRTPATHPARSED pCwdParsedFree = NULL;
    PRTPATHPARSED pCwdParsed     = &uCwd.Parsed;
    size_t        cbCwdParsed    = sizeof(uCwd);
    rc = RTPathParse(pszCwd, pCwdParsed, cbCwdParsed, fFlags & RTPATH_STR_F_STYLE_MASK);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else if (rc == VERR_BUFFER_OVERFLOW)
    {
        cbCwdParsed = RT_UOFFSETOF_DYN(RTPATHPARSED, aComps[pCwdParsed->cComps + 2]);
        pCwdParsedFree = pCwdParsed = (PRTPATHPARSED)RTMemTmpAlloc(cbCwdParsed);
        AssertReturnStmt(pCwdParsed, RTMemTmpFree(pszCwdFree), VERR_NO_TMP_MEMORY);
        rc = RTPathParse(pszCwd, pCwdParsed, cbCwdParsed, fFlags & RTPATH_STR_F_STYLE_MASK);
        AssertRCReturnStmt(rc, RTMemTmpFree(pCwdParsedFree); RTMemTmpFree(pszCwdFree), rc);
    }
    else
        AssertMsgFailedReturn(("rc=%Rrc '%s'\n", rc, pszPath), rc);

    /*
     * Join paths with the base-path code.
     */
    if (fFlags & RTPATHABS_F_STOP_AT_CWD)
        fFlags |= RTPATHABS_F_STOP_AT_BASE;
    else
        fFlags &= ~RTPATHABS_F_STOP_AT_BASE;
    rc = rtPathAbsExWithCwdOrBaseCommon(pszCwd, cchInPlace, pCwdParsed, pszPath, pParsed, fFlags, pszAbsPath, pcbAbsPath);
    if (pCwdParsedFree)
        RTMemTmpFree(pCwdParsedFree);
    if (pszCwdFree)
        RTMemTmpFree(pszCwdFree);
    return rc;
}


/**
 * Handles the no-root-path scenario where we've got a base path.
 */
static int rtPathAbsExWithBase(const char *pszBase, const char *pszPath, PRTPATHPARSED pParsed, uint32_t fFlags,
                               char *pszAbsPath, size_t *pcbAbsPath)
{
    /*
     * Parse the base path.
     */
    union
    {
        RTPATHPARSED Parsed;
        uint8_t      abPadding[1024];
    } uBase;
    PRTPATHPARSED pBaseParsedFree = NULL;
    PRTPATHPARSED pBaseParsed     = &uBase.Parsed;
    size_t        cbBaseParsed    = sizeof(uBase);
    int rc = RTPathParse(pszBase, pBaseParsed, cbBaseParsed, fFlags & RTPATH_STR_F_STYLE_MASK);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else if (rc == VERR_BUFFER_OVERFLOW)
    {
        cbBaseParsed = RT_UOFFSETOF_DYN(RTPATHPARSED, aComps[pBaseParsed->cComps + 2]);
        pBaseParsedFree = pBaseParsed = (PRTPATHPARSED)RTMemTmpAlloc(cbBaseParsed);
        AssertReturn(pBaseParsed, VERR_NO_TMP_MEMORY);
        rc = RTPathParse(pszBase, pBaseParsed, cbBaseParsed, fFlags & RTPATH_STR_F_STYLE_MASK);
        AssertRCReturnStmt(rc, RTMemTmpFree(pBaseParsedFree), rc);
    }
    else
        AssertMsgFailedReturn(("rc=%Rrc '%s'\n", rc, pszPath), rc);

    /*
     * If the base path isn't absolute, we need to deal with that.
     */
    size_t cchInPlace = 0;
    if ((pBaseParsed->fProps & (RTPATH_PROP_ABSOLUTE | RTPATH_PROP_EXTRA_SLASHES | RTPATH_PROP_DOT_REFS)) == RTPATH_PROP_ABSOLUTE)
    { /* likely */ }
    else
    {
        cchInPlace = *pcbAbsPath;
        rc = RTPathAbsEx(NULL, pszBase, fFlags, pszAbsPath, &cchInPlace);
        if (RT_SUCCESS(rc))
        {
            Assert(strlen(pszAbsPath) == cchInPlace);
            Assert(cchInPlace > 0);
        }
        else
        {
/** @todo Allocate temp buffer like we do for CWD? */
            /* This is over generious, but don't want to put too much effort into it yet. */
            if (rc == VERR_BUFFER_OVERFLOW)
                *pcbAbsPath = cchInPlace + 1 + pParsed->cchPath + 1;
            return rc;
        }

        /*
         * Reparse it.
         */
        rc = RTPathParse(pszAbsPath, pBaseParsed, cbBaseParsed, fFlags & RTPATH_STR_F_STYLE_MASK);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else if (rc == VERR_BUFFER_OVERFLOW)
        {
            if (pBaseParsedFree)
                RTMemTmpFree(pBaseParsedFree);
            cbBaseParsed = RT_UOFFSETOF_DYN(RTPATHPARSED, aComps[pBaseParsed->cComps + 2]);
            pBaseParsedFree = pBaseParsed = (PRTPATHPARSED)RTMemTmpAlloc(cbBaseParsed);
            AssertReturn(pBaseParsed, VERR_NO_TMP_MEMORY);
            rc = RTPathParse(pszAbsPath, pBaseParsed, cbBaseParsed, fFlags & RTPATH_STR_F_STYLE_MASK);
            AssertRCReturnStmt(rc, RTMemTmpFree(pBaseParsedFree), rc);
        }
        else
            AssertMsgFailedReturn(("rc=%Rrc '%s'\n", rc, pszPath), rc);
    }

    /*
     * Join paths with the CWD code.
     */
    rc = rtPathAbsExWithCwdOrBaseCommon(cchInPlace ? pszAbsPath : pszBase, cchInPlace, pBaseParsed,
                                        pszPath, pParsed, fFlags, pszAbsPath, pcbAbsPath);
    if (pBaseParsedFree)
        RTMemTmpFree(pBaseParsedFree);
    return rc;
}


/**
 * Handles the RTPATH_PROP_ROOT_SLASH case.
 */
static int rtPathAbsExRootSlash(const char *pszBase, const char *pszPath, PRTPATHPARSED pParsed,
                                uint32_t fFlags, char *pszAbsPath, size_t *pcbAbsPath)
{
    /*
     * Eliminate dot and dot-dot components.
     * Note! aComp[0] is the root stuff and must never be dropped.
     */
    uint32_t const cComps = pParsed->cComps;
    uint32_t const iStop  = (pParsed->fProps & (RTPATH_PROP_UNC | RTPATH_PROP_SPECIAL_UNC)) != RTPATH_PROP_UNC
                         || pParsed->cComps < 2 ? 0 : 1;
    uint32_t       iLast  = iStop;
    uint32_t       i      = iStop + 1;
    while (i < cComps)
    {
        uint32_t const cchComp = pParsed->aComps[i].cch;
        if (   cchComp > 2
            || pszPath[pParsed->aComps[i].off] != '.'
            || (cchComp == 2 && pszPath[pParsed->aComps[i].off + 1] != '.') )
            iLast = i;
        else
        {
            Assert(cchComp == 1 || cchComp == 2);
            pParsed->aComps[i].cch = 0;
            if (cchComp == 2)
            {
                while (iLast > iStop && pParsed->aComps[iLast].cch == 0)
                    iLast--;
                if (iLast > iStop)
                {
                    Assert(pParsed->aComps[iLast].cch > 0);
                    pParsed->aComps[iLast].cch = 0;
                    iLast--;
                }
            }
        }
        i++;
    }

    /*
     * Before we continue, ensure trailing slash if requested.
     */
    if (   (fFlags & RTPATHABS_F_ENSURE_TRAILING_SLASH)
        && iLast > 0)
        pParsed->fProps |= RTPATH_PROP_DIR_SLASH;

    /*
     * DOS-style: Do we need to supply a drive letter or UNC root?
     */
    size_t cchRootPrefix = 0;
    if (   (fFlags & RTPATH_STR_F_STYLE_DOS)
        && !(pParsed->fProps & (RTPATH_PROP_VOLUME | RTPATH_PROP_UNC)) )
    {
        /* Use the drive/UNC from the base path if we have one and it has such a component: */
        if (pszBase)
        {
            union
            {
                RTPATHPARSED    Parsed;
                uint8_t         abPadding[sizeof(RTPATHPARSED) + sizeof(pParsed->aComps[0]) * 2];
            } uBase;
            int rc = RTPathParse(pszBase, &uBase.Parsed, sizeof(uBase), fFlags & RTPATH_STR_F_STYLE_MASK);
            AssertMsgReturn(RT_SUCCESS(rc) || rc == VERR_BUFFER_OVERFLOW, ("%Rrc - '%s'\n", rc, pszBase), rc);

            if (uBase.Parsed.fProps & RTPATH_PROP_VOLUME)
            {
                /* get the drive letter. */
                Assert(uBase.Parsed.aComps[0].cch == 2 || uBase.Parsed.aComps[0].cch == 3);
                cchRootPrefix = RT_MIN(uBase.Parsed.aComps[0].cch, 2);
                if (cchRootPrefix < *pcbAbsPath)
                    memcpy(pszAbsPath, &pszBase[uBase.Parsed.aComps[0].off], cchRootPrefix);
                else
                {
                    rc = RTPathParsedReassemble(pszPath, pParsed, fFlags & RTPATH_STR_F_STYLE_MASK, pszAbsPath, 0);
                    Assert(rc == VERR_BUFFER_OVERFLOW);

                    *pcbAbsPath = cchRootPrefix + pParsed->cchPath + 1;
                    return VERR_BUFFER_OVERFLOW;
                }
                rtPathAbsExUpperCaseDriveLetter(pszAbsPath);
            }
            else if (uBase.Parsed.fProps & RTPATH_PROP_UNC)
            {
                /* Include the share if we've got one. */
                cchRootPrefix = uBase.Parsed.aComps[0].cch;
                if (uBase.Parsed.cComps >= 2 && !(uBase.Parsed.fProps & RTPATH_PROP_SPECIAL_UNC))
                    cchRootPrefix += uBase.Parsed.aComps[1].cch;
                else if (uBase.Parsed.fProps & RTPATH_PROP_ROOT_SLASH)
                    cchRootPrefix--;
                if (cchRootPrefix < *pcbAbsPath)
                {
                    if (uBase.Parsed.cComps < 2 || (uBase.Parsed.fProps & RTPATH_PROP_SPECIAL_UNC))
                        memcpy(pszAbsPath, &pszBase[uBase.Parsed.aComps[0].off], cchRootPrefix);
                    else
                    {
                        size_t cchFirst = uBase.Parsed.aComps[0].cch;
                        memcpy(pszAbsPath, &pszBase[uBase.Parsed.aComps[0].off], cchFirst);
                        memcpy(&pszAbsPath[cchFirst], &pszBase[uBase.Parsed.aComps[1].off], uBase.Parsed.aComps[1].cch);
                    }
                }
                else
                {
                    rc = RTPathParsedReassemble(pszPath, pParsed, fFlags & RTPATH_STR_F_STYLE_MASK, pszAbsPath, 0);
                    Assert(rc == VERR_BUFFER_OVERFLOW);

                    *pcbAbsPath = cchRootPrefix + pParsed->cchPath + 1;
                    return VERR_BUFFER_OVERFLOW;
                }
            }
            else
                pszBase = NULL;
        }

        /* Otherwise, query the current drive: */
        if (!pszBase)
        {
            int rc = RTPathGetCurrentDrive(pszAbsPath, *pcbAbsPath);
            if (RT_SUCCESS(rc))
                cchRootPrefix = strlen(pszAbsPath);
            else
            {
                if (rc == VERR_BUFFER_OVERFLOW)
                {
                    int rc2 = RTPathParsedReassemble(pszPath, pParsed, fFlags & RTPATH_STR_F_STYLE_MASK, pszAbsPath, 0);
                    Assert(rc2 == VERR_BUFFER_OVERFLOW);

                    char *pszTmp = (char *)RTMemTmpAlloc(RTPATH_BIG_MAX);
                    if (pszTmp)
                    {
                        rc2 = RTPathGetCurrentDrive(pszTmp, RTPATH_BIG_MAX);
                        if (RT_SUCCESS(rc2))
                            *pcbAbsPath = strlen(pszTmp) + pParsed->cchPath + 1;
                        else
                            *pcbAbsPath = RT_MAX(*pcbAbsPath * 2, (size_t)RTPATH_BIG_MAX * 3 + pParsed->cchPath + 1);
                        RTMemTmpFree(pszTmp);
                    }
                    else
                        rc = VERR_NO_TMP_MEMORY;
                }
                return rc;
            }
        }
    }

    /*
     * Reassemble the path and return.
     */
    int rc = RTPathParsedReassemble(pszPath, pParsed, fFlags & RTPATH_STR_F_STYLE_MASK,
                                    pszAbsPath + cchRootPrefix, *pcbAbsPath - cchRootPrefix);
    *pcbAbsPath = cchRootPrefix + pParsed->cchPath + (rc == VERR_BUFFER_OVERFLOW);
    return rc;
}


/**
 * Handles the RTPATH_PROP_ABSOLUTE case.
 */
static int rtPathAbsExAbsolute(const char *pszPath, PRTPATHPARSED pParsed, uint32_t fFlags, char *pszAbsPath, size_t *pcbAbsPath)
{
    if (pParsed->fProps & RTPATH_PROP_DOT_REFS)
    {
        uint32_t i = pParsed->cComps;
        while (i-- > 0)
            if (   pParsed->aComps[i].cch == 1
                && pszPath[pParsed->aComps[i].off] == '.')
                pParsed->aComps[i].cch = 0;
    }

    if (   (fFlags & RTPATHABS_F_ENSURE_TRAILING_SLASH)
        && pParsed->cComps > 1)
        pParsed->fProps |= RTPATH_PROP_DIR_SLASH;

    int rc = RTPathParsedReassemble(pszPath, pParsed, fFlags & RTPATH_STR_F_STYLE_MASK, pszAbsPath, *pcbAbsPath);
    *pcbAbsPath = pParsed->cchPath + (rc == VERR_BUFFER_OVERFLOW);
    if (RT_SUCCESS(rc) && (pParsed->fProps & RTPATH_PROP_VOLUME))
        rtPathAbsExUpperCaseDriveLetter(pszAbsPath);
    return rc;
}


RTDECL(int) RTPathAbsEx(const char *pszBase, const char *pszPath, uint32_t fFlags, char *pszAbsPath, size_t *pcbAbsPath)
{
    LogFlow(("RTPathAbsEx: pszBase=%s pszPath=%s fFlags=%#x\n", pszBase, pszPath, fFlags));

    /*
     * Some input validation.
     */
    AssertPtr(pszPath);
    AssertPtr(pszAbsPath);
    AssertPtr(pcbAbsPath);
    AssertReturn(*pszPath != '\0', VERR_PATH_ZERO_LENGTH);

    AssertCompile(RTPATH_STR_F_STYLE_HOST == 0);
    AssertReturn(   RTPATH_STR_F_IS_VALID(fFlags, RTPATHABS_F_STOP_AT_BASE | RTPATHABS_F_STOP_AT_CWD | RTPATHABS_F_ENSURE_TRAILING_SLASH)
                 && !(fFlags & RTPATH_STR_F_MIDDLE), VERR_INVALID_FLAGS);
    if ((fFlags & RTPATH_STR_F_STYLE_MASK) == RTPATH_STR_F_STYLE_HOST)
        fFlags |= RTPATH_STYLE;

    /*
     * Parse the path we're to straigthen out.
     */
    union
    {
        RTPATHPARSED Parsed;
        uint8_t      abPadding[1024];
    } uBuf;
    PRTPATHPARSED pParsedFree = NULL;
    PRTPATHPARSED pParsed     = &uBuf.Parsed;
    size_t        cbParsed    = sizeof(uBuf);
    int rc = RTPathParse(pszPath, pParsed, cbParsed, fFlags & RTPATH_STR_F_STYLE_MASK);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else if (rc == VERR_BUFFER_OVERFLOW)
    {
        cbParsed = RT_UOFFSETOF_DYN(RTPATHPARSED, aComps[pParsed->cComps + 2]);
        pParsedFree = pParsed = (PRTPATHPARSED)RTMemTmpAlloc(cbParsed);
        AssertReturn(pParsed, VERR_NO_TMP_MEMORY);
        rc = RTPathParse(pszPath, pParsed, cbParsed, fFlags & RTPATH_STR_F_STYLE_MASK);
        AssertRCReturnStmt(rc, RTMemTmpFree(pParsedFree), rc);
    }
    else
        AssertMsgFailedReturn(("rc=%Rrc '%s'\n", rc, pszPath), rc);

    /*
     * Check if the input is more or less perfect as it is.
     */
    if (pParsed->fProps & RTPATH_PROP_ABSOLUTE)
        rc = rtPathAbsExAbsolute(pszPath, pParsed, fFlags, pszAbsPath, pcbAbsPath);
    /*
     * What about relative but with a root slash.
     */
    else if (pParsed->fProps & RTPATH_PROP_ROOT_SLASH)
        rc = rtPathAbsExRootSlash(pszBase, pszPath, pParsed, fFlags, pszAbsPath, pcbAbsPath);
    /*
     * Not exactly perfect.  No root slash.
     *
     * If we have a base path, we use it unless we're into drive letters and
     * pszPath refers to a different drive letter.
     */
    else if (   pszBase
             && (   !(fFlags & RTPATH_STR_F_STYLE_DOS)
                 /** @todo add flag for skipping this and always using the base path? */
                 || !(pParsed->fProps & RTPATH_PROP_VOLUME)
                 || (   RT_C_IS_ALPHA(pszBase[0])
                     && pszBase[1] == ':'
                     && RT_C_TO_UPPER(pszBase[0]) == RT_C_TO_UPPER(pszPath[0])
                    )
                )
            )
        rc = rtPathAbsExWithBase(pszBase, pszPath, pParsed, fFlags, pszAbsPath, pcbAbsPath);
    else
        rc = rtPathAbsExWithCwd(pszPath, pParsed, fFlags, pszAbsPath, pcbAbsPath);

    if (pParsedFree)
        RTMemTmpFree(pParsedFree);
    LogFlow(("RTPathAbsEx: returns %Rrc *pcbAbsPath=%#zx\n", rc, *pcbAbsPath));
    return rc;
}


RTDECL(int) RTPathAbs(const char *pszPath, char *pszAbsPath, size_t cbAbsPath)
{
    return RTPathAbsEx(NULL, pszPath, RTPATH_STR_F_STYLE_HOST, pszAbsPath, &cbAbsPath);
}

