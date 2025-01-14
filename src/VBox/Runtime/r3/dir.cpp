/* $Id: dir.cpp 107838 2025-01-14 18:08:00Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT - Directory Manipulation, Part 1.
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
#define LOG_GROUP RTLOGGROUP_DIR
#include <iprt/dir.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/uni.h>
#define RTDIR_AGNOSTIC
#include "internal/dir.h"
#include "internal/path.h"


static DECLCALLBACK(bool) rtDirFilterWinNtMatch(PRTDIRINTERNAL pDir, const char *pszName);
static DECLCALLBACK(bool) rtDirFilterWinNtMatchNoWildcards(PRTDIRINTERNAL pDir, const char *pszName);
DECLINLINE(bool) rtDirFilterWinNtMatchEon(PCRTUNICP puszFilter);
static bool rtDirFilterWinNtMatchDosStar(unsigned iDepth, RTUNICP uc, const char *pszNext, PCRTUNICP puszFilter);
static bool rtDirFilterWinNtMatchStar(unsigned iDepth, RTUNICP uc, const char *pszNext, PCRTUNICP puszFilter);
static bool rtDirFilterWinNtMatchBase(unsigned iDepth, const char *pszName, PCRTUNICP puszFilter);



RTDECL(int) RTDirCreateFullPath(const char *pszPath, RTFMODE fMode)
{
    return RTDirCreateFullPathEx(pszPath, fMode, 0);
}


RTDECL(int) RTDirCreateFullPathEx(const char *pszPath, RTFMODE fMode, uint32_t fFlags)
{
    /*
     * Resolve the path.
     */
    char *pszAbsPath = RTPathAbsDup(pszPath);
    if (!pszAbsPath)
        return VERR_NO_TMP_MEMORY;

    /*
     * Iterate the path components making sure each of them exists.
     */
    /* skip volume name */
    char *psz = &pszAbsPath[rtPathVolumeSpecLen(pszAbsPath)];

    /* skip the root slash if any */
    if (RTPATH_IS_SLASH(*psz))
        psz++;

    /* iterate over path components. */
    int rc = VINF_SUCCESS;
    do
    {
        /* the next component is NULL, stop iterating */
        if (!*psz)
            break;
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
        char *psz2 = strchr(psz, '/');
        psz = strchr(psz, RTPATH_SLASH);
        if (psz2 && (!psz || (uintptr_t)psz2 < (uintptr_t)psz))
            psz = psz2;
#else
        psz = strchr(psz, RTPATH_SLASH);
#endif
        if (psz)
            *psz = '\0';

        /*
         * ASSUME that RTDirCreate will return VERR_ALREADY_EXISTS and not VERR_ACCESS_DENIED in those cases
         * where the directory exists but we don't have write access to the parent directory.
         */
        rc = RTDirCreate(pszAbsPath, fMode, fFlags);
        if (rc == VERR_ALREADY_EXISTS)
            rc = VINF_SUCCESS;

        if (!psz)
            break;
        *psz++ = RTPATH_DELIMITER;
    } while (RT_SUCCESS(rc));

    RTStrFree(pszAbsPath);
    return rc;
}


/**
 * Filter a the filename in the against a filter.
 *
 * @returns true if the name matches the filter.
 * @returns false if the name doesn't match filter.
 * @param   pDir        The directory handle.
 * @param   pszName     The path to match to the filter.
 */
static DECLCALLBACK(bool) rtDirFilterWinNtMatchNoWildcards(PRTDIRINTERNAL pDir, const char *pszName)
{
    /*
     * Walk the string and compare.
     */
    PCRTUNICP   pucFilter = pDir->puszFilter;
    const char *psz = pszName;
    RTUNICP     uc;
    do
    {
        int rc = RTStrGetCpEx(&psz, &uc);
        AssertRCReturn(rc, false);
        RTUNICP ucFilter = *pucFilter++;
        if (    uc != ucFilter
            &&  RTUniCpToUpper(uc) != ucFilter)
            return false;
    } while (uc);
    return true;
}


/**
 * Matches end of name.
 */
DECLINLINE(bool) rtDirFilterWinNtMatchEon(PCRTUNICP puszFilter)
{
    RTUNICP ucFilter;
    while (     (ucFilter = *puszFilter) == '>'
           ||   ucFilter == '<'
           ||   ucFilter == '*'
           ||   ucFilter == '"')
        puszFilter++;
    return !ucFilter;
}


/**
 * Recursive star matching.
 * Practically the same as normal star, except that the dos star stops
 * when hitting the last dot.
 *
 * @returns true on match.
 * @returns false on miss.
 */
static bool rtDirFilterWinNtMatchDosStar(unsigned iDepth, RTUNICP uc, const char *pszNext, PCRTUNICP puszFilter)
{
    AssertReturn(iDepth++ < 256, false);

    /*
     * If there is no dos star, we should work just like the NT star.
     * Since that's generally faster algorithms, we jump down to there if we can.
     */
    const char *pszDosDot = strrchr(pszNext, '.');
    if (!pszDosDot && uc == '.')
        pszDosDot = pszNext - 1;
    if (!pszDosDot)
        return rtDirFilterWinNtMatchStar(iDepth, uc, pszNext, puszFilter);

    /*
     * Inspect the next filter char(s) until we find something to work on.
     */
    RTUNICP ucFilter = *puszFilter++;
    switch (ucFilter)
    {
        /*
         * The star expression is the last in the pattern.
         * We're fine if the name ends with a dot.
         */
        case '\0':
            return !pszDosDot[1];

        /*
         * Simplified by brute force.
         */
        case '>': /* dos question mark */
        case '?':
        case '*':
        case '<': /* dos star */
        case '"': /* dos dot */
        {
            puszFilter--;
            const char *pszStart = pszNext;
            do
            {
                if (rtDirFilterWinNtMatchBase(iDepth, pszNext, puszFilter))
                    return true;
                int rc = RTStrGetCpEx(&pszNext, &uc); AssertRCReturn(rc, false);
            } while ((intptr_t)pszDosDot - (intptr_t)pszNext >= -1);

            /* backtrack and do the current char. */
            pszNext = RTStrPrevCp(NULL, pszStart); AssertReturn(pszNext, false);
            return rtDirFilterWinNtMatchBase(iDepth, pszNext, puszFilter);
        }

        /*
         * Ok, we've got zero or more characters.
         * We'll try match starting at each occurrence of this character.
         */
        default:
        {
            if (    RTUniCpToUpper(uc) == ucFilter
                &&  rtDirFilterWinNtMatchBase(iDepth, pszNext, puszFilter))
                return true;
            do
            {
                int rc = RTStrGetCpEx(&pszNext, &uc); AssertRCReturn(rc, false);
                if (    RTUniCpToUpper(uc) == ucFilter
                    &&  rtDirFilterWinNtMatchBase(iDepth, pszNext, puszFilter))
                    return true;
            } while ((intptr_t)pszDosDot - (intptr_t)pszNext >= -1);
            return false;
        }
    }
    /* won't ever get here! */
}


/**
 * Recursive star matching.
 *
 * @returns true on match.
 * @returns false on miss.
 */
static bool rtDirFilterWinNtMatchStar(unsigned iDepth, RTUNICP uc, const char *pszNext, PCRTUNICP puszFilter)
{
    AssertReturn(iDepth++ < 256, false);

    /*
     * Inspect the next filter char(s) until we find something to work on.
     */
    for (;;)
    {
        RTUNICP ucFilter = *puszFilter++;
        switch (ucFilter)
        {
            /*
             * The star expression is the last in the pattern.
             * Cool, that means we're done!
             */
            case '\0':
                return true;

            /*
             * Just in case (doubt we ever get here), just merge it with the current one.
             */
            case '*':
                break;

            /*
             * Skip a fixed number of chars.
             * Figure out how many by walking the filter ignoring '*'s.
             */
            case '?':
            {
                unsigned cQms = 1;
                while ((ucFilter = *puszFilter) == '*' || ucFilter == '?')
                {
                    cQms += ucFilter == '?';
                    puszFilter++;
                }
                do
                {
                    if (!uc)
                        return false;
                    int rc = RTStrGetCpEx(&pszNext, &uc); AssertRCReturn(rc, false);
                } while (--cQms > 0);
                /* done? */
                if (!ucFilter)
                    return true;
                break;
            }

            /*
             * The simple way is to try char by char and match the remaining
             * expression. If it's trailing we're done.
             */
            case '>': /* dos question mark */
            {
                if (rtDirFilterWinNtMatchEon(puszFilter))
                    return true;
                const char *pszStart = pszNext;
                do
                {
                    if (rtDirFilterWinNtMatchBase(iDepth, pszNext, puszFilter))
                        return true;
                    int rc = RTStrGetCpEx(&pszNext, &uc); AssertRCReturn(rc, false);
                } while (uc);

                /* backtrack and do the current char. */
                pszNext = RTStrPrevCp(NULL, pszStart); AssertReturn(pszNext, false);
                return rtDirFilterWinNtMatchBase(iDepth, pszNext, puszFilter);
            }

            /*
             * This bugger is interesting.
             * Time for brute force. Iterate the name char by char.
             */
            case '<':
            {
                do
                {
                    if (rtDirFilterWinNtMatchDosStar(iDepth, uc, pszNext, puszFilter))
                        return true;
                    int rc = RTStrGetCpEx(&pszNext, &uc); AssertRCReturn(rc, false);
                } while (uc);
                return false;
            }

            /*
             * This guy matches a '.' or the end of the name.
             * It's very simple if the rest of the filter expression also matches eon.
             */
            case '"':
                if (rtDirFilterWinNtMatchEon(puszFilter))
                    return true;
                ucFilter = '.';
                RT_FALL_THRU();

            /*
             * Ok, we've got zero or more characters.
             * We'll try match starting at each occurrence of this character.
             */
            default:
            {
                do
                {
                    if (    RTUniCpToUpper(uc) == ucFilter
                        &&  rtDirFilterWinNtMatchBase(iDepth, pszNext, puszFilter))
                        return true;
                    int rc = RTStrGetCpEx(&pszNext, &uc); AssertRCReturn(rc, false);
                } while (uc);
                return false;
            }
        }
    } /* for (;;) */

    /* won't ever get here! */
}


/**
 * Filter a the filename in the against a filter.
 *
 * The rules are as follows:
 *      '?'     Matches exactly one char.
 *      '*'     Matches zero or more chars.
 *      '<'     The dos star, matches zero or more chars except the DOS dot.
 *      '>'     The dos question mark, matches one char, but dots and end-of-name eats them.
 *      '"'     The dos dot, matches a dot or end-of-name.
 *
 * @returns true if the name matches the filter.
 * @returns false if the name doesn't match filter.
 * @param   iDepth      The recursion depth.
 * @param   pszName     The path to match to the filter.
 * @param   puszFilter  The filter string.
 */
static bool rtDirFilterWinNtMatchBase(unsigned iDepth, const char *pszName, PCRTUNICP puszFilter)
{
    AssertReturn(iDepth++ < 256, false);

    /*
     * Walk the string and match it up char by char.
     */
    RTUNICP uc;
    do
    {
        RTUNICP ucFilter = *puszFilter++;
        int rc = RTStrGetCpEx(&pszName, &uc); AssertRCReturn(rc, false);
        switch (ucFilter)
        {
            /* Exactly one char. */
            case '?':
                if (!uc)
                    return false;
                break;

            /* One char, but the dos dot and end-of-name eats '>' and '<'. */
            case '>': /* dos ? */
                if (!uc)
                    return rtDirFilterWinNtMatchEon(puszFilter);
                if (uc == '.')
                {
                    while ((ucFilter = *puszFilter) == '>' || ucFilter == '<')
                        puszFilter++;
                    if (ucFilter == '"' || ucFilter == '.')  /* not 100% sure about the last dot */
                        ++puszFilter;
                    else /* the does question mark doesn't match '.'s, so backtrack. */
                        pszName = RTStrPrevCp(NULL, pszName);
                }
                break;

            /* Match a dot or the end-of-name. */
            case '"': /* dos '.' */
                if (uc != '.')
                {
                    if (uc)
                        return false;
                    return rtDirFilterWinNtMatchEon(puszFilter);
                }
                break;

            /* zero or more */
            case '*':
                return rtDirFilterWinNtMatchStar(iDepth, uc, pszName, puszFilter);
            case '<': /* dos '*' */
                return rtDirFilterWinNtMatchDosStar(iDepth, uc, pszName, puszFilter);


            /* uppercased match */
            default:
            {
                if (RTUniCpToUpper(uc) != ucFilter)
                    return false;
                break;
            }
        }
    } while (uc);

    return true;
}


/**
 * Filter a the filename in the against a filter.
 *
 * @returns true if the name matches the filter.
 * @returns false if the name doesn't match filter.
 * @param   pDir        The directory handle.
 * @param   pszName     The path to match to the filter.
 */
static DECLCALLBACK(bool) rtDirFilterWinNtMatch(PRTDIRINTERNAL pDir, const char *pszName)
{
    return rtDirFilterWinNtMatchBase(0, pszName, pDir->puszFilter);
}


/**
 * Initializes a WinNt like wildcard filter.
 *
 * @returns Pointer to the filter function.
 * @returns NULL if the filter doesn't filter out anything.
 * @param   pDir        The directory handle (not yet opened).
 */
static PFNRTDIRFILTER rtDirFilterWinNtInit(PRTDIRINTERNAL pDir)
{
    /*
     * Check for the usual * and <"< (*.* in DOS language) patterns.
     */
    if (    (pDir->cchFilter == 1 && pDir->pszFilter[0] == '*')
        ||  (pDir->cchFilter == 3 && !memcmp(pDir->pszFilter, "<\".>", 3))
        )
        return NULL;

    /*
     * Uppercase the expression, also do a little optimizations when possible.
     */
    bool fHaveWildcards = false;
    unsigned iRead = 0;
    unsigned iWrite = 0;
    while (iRead < pDir->cucFilter)
    {
        RTUNICP uc = pDir->puszFilter[iRead++];
        if (uc == '*')
        {
            fHaveWildcards = true;
            /* remove extra stars. */
            RTUNICP uc2;
            while ((uc2 = pDir->puszFilter[iRead + 1]) == '*')
                iRead++;
        }
        else if (uc == '?' || uc == '>' || uc == '<' || uc == '"')
            fHaveWildcards = true;
        else
            uc = RTUniCpToUpper(uc);
        pDir->puszFilter[iWrite++] = uc;
    }
    pDir->puszFilter[iWrite] = 0;
    pDir->cucFilter = iWrite;

    return fHaveWildcards
        ? rtDirFilterWinNtMatch
        : rtDirFilterWinNtMatchNoWildcards;
}


/**
 * Common worker for opening a directory.
 *
 * @returns IPRT status code.
 * @param   phDir               Where to store the directory handle.
 * @param   pszPath             The specified path.
 * @param   pszFilter           Pointer to where the filter start in the path.
 *                              NULL if no filter.
 * @param   enmFilter           The type of filter to apply.
 * @param   fFlags              RTDIR_F_XXX.
 * @param   hRelativeDir        The directory @a pvNativeRelative is relative
 *                              to, ~(uintptr_t)0 if absolute.
 * @param   pvNativeRelative    The native relative path.  NULL if absolute or
 *                              we're to use (consume) hRelativeDir.
 */
static int rtDirOpenCommon(RTDIR *phDir, const char *pszPath, const char *pszFilter, RTDIRFILTER enmFilter,
                           uint32_t fFlags, uintptr_t hRelativeDir, void *pvNativeRelative)
{
    /*
     * Expand the path.
     *
     * The purpose of this exercise to have the abs path around
     * for querying extra information about the objects we list.
     * As a sideeffect we also validate the path here.
     *
     * Note! The RTDIR_F_NO_ABS_PATH mess is there purely for allowing us to
     *       work around PATH_MAX using CWD on linux and other unixy systems.
     */
    char  *pszAbsPath;
    size_t cbFilter;                    /* includes '\0' (thus cb and not cch). */
    size_t cucFilter0;                  /* includes U+0. */
    bool   fDirSlash = false;
    if (!pszFilter)
    {
        if (*pszPath != '\0')
        {
            const char *pszLast = strchr(pszPath, '\0') - 1;
            if (RTPATH_IS_SLASH(*pszLast))
                fDirSlash = true;
        }

        cbFilter = cucFilter0 = 0;
        if (!(fFlags & RTDIR_F_NO_ABS_PATH))
            pszAbsPath = RTPathAbsExDup(NULL, pszPath, RTPATHABS_F_ENSURE_TRAILING_SLASH);
        else
        {
            size_t cchTmp = strlen(pszPath);
            pszAbsPath = RTStrAlloc(cchTmp + 2);
            if (pszAbsPath)
            {
                memcpy(pszAbsPath, pszPath, cchTmp);
                pszAbsPath[cchTmp] = RTPATH_SLASH;
                pszAbsPath[cchTmp + 1 - fDirSlash] = '\0';
            }
        }
    }
    else
    {
        cbFilter = strlen(pszFilter) + 1;
        cucFilter0 = RTStrUniLen(pszFilter) + 1;

        if (pszFilter != pszPath)
        {
            /* yea, I'm lazy. sue me. */
            char *pszTmp = RTStrDup(pszPath);
            if (!pszTmp)
                return VERR_NO_MEMORY;
            pszTmp[pszFilter - pszPath] = '\0';
            if (!(fFlags & RTDIR_F_NO_ABS_PATH))
            {
                pszAbsPath = RTPathAbsExDup(NULL, pszTmp, RTPATHABS_F_ENSURE_TRAILING_SLASH);
                RTStrFree(pszTmp);
            }
            else
            {
                pszAbsPath = pszTmp;
                RTPathEnsureTrailingSeparator(pszAbsPath, strlen(pszPath) + 1);
            }
        }
        else if (!(fFlags & RTDIR_F_NO_ABS_PATH))
            pszAbsPath = RTPathAbsExDup(NULL, ".", RTPATHABS_F_ENSURE_TRAILING_SLASH);
        else
            pszAbsPath = RTStrDup("." RTPATH_SLASH_STR);
        fDirSlash = true;
    }
    if (!pszAbsPath)
        return VERR_NO_MEMORY;
    Assert(strchr(pszAbsPath, '\0')[-1] == RTPATH_SLASH);

    /*
     * Allocate and initialize the directory handle.
     *
     * The posix definition of Data.d_name allows it to be < NAME_MAX + 1,
     * thus the horrible ugliness here. Solaris uses d_name[1] for instance.
     */
    size_t const cchAbsPath      = strlen(pszAbsPath);
    size_t const cbDir           = rtDirNativeGetStructSize(pszAbsPath);
    size_t const cbAllocated     = cbDir
                                 + cucFilter0 * sizeof(RTUNICP)
                                 + cbFilter
                                 + cchAbsPath + 1 + 4;
    PRTDIRINTERNAL pDir = (PRTDIRINTERNAL)RTMemAllocZ(cbAllocated);
    if (!pDir)
    {
        RTStrFree(pszAbsPath);
        return VERR_NO_MEMORY;
    }
    uint8_t *pb = (uint8_t *)pDir + cbDir;

    /* initialize it */
    pDir->u32Magic = RTDIR_MAGIC;
    pDir->cbSelf   = cbDir;
    if (cbFilter)
    {
        pDir->puszFilter = (PRTUNICP)pb;
        int rc2 = RTStrToUniEx(pszFilter, RTSTR_MAX, &pDir->puszFilter, cucFilter0, &pDir->cucFilter);
        AssertRC(rc2);
        pb += cucFilter0 * sizeof(RTUNICP);
        pDir->pszFilter = (char *)memcpy(pb, pszFilter, cbFilter);
        pDir->cchFilter = cbFilter - 1;
        pb += cbFilter;
    }
    else
    {
        pDir->puszFilter = NULL;
        pDir->cucFilter = 0;
        pDir->pszFilter = NULL;
        pDir->cchFilter = 0;
    }
    pDir->enmFilter = enmFilter;
    switch (enmFilter)
    {
        default:
        case RTDIRFILTER_NONE:
            pDir->pfnFilter = NULL;
            break;
        case RTDIRFILTER_WINNT:
            pDir->pfnFilter = rtDirFilterWinNtInit(pDir);
            break;
        case RTDIRFILTER_UNIX:
            pDir->pfnFilter = NULL;
            break;
        case RTDIRFILTER_UNIX_UPCASED:
            pDir->pfnFilter = NULL;
            break;
    }
    pDir->cchPath       = cchAbsPath;
    pDir->pszPath       = (char *)memcpy(pb, pszAbsPath, cchAbsPath);
    pb[cchAbsPath]      = '\0';
    Assert(pb - (uint8_t *)pDir + cchAbsPath + 1 <= cbAllocated);
    pDir->pszName       = NULL;
    pDir->cchName       = 0;
    pDir->fFlags        = fFlags;
    pDir->fDirSlash     = fDirSlash;
    pDir->fDataUnread   = false;

    /*
     * Hand it over to the native part.
     */
    int rc = rtDirNativeOpen(pDir, hRelativeDir, pvNativeRelative);
    if (RT_SUCCESS(rc))
        *phDir = pDir;
    else
        RTMemFree(pDir);
    RTStrFree(pszAbsPath);
    return rc;
}


RTDECL(int) RTDirOpen(RTDIR *phDir, const char *pszPath)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phDir, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);

    /*
     * Take common cause with RTDirOpenFiltered().
     */
    int rc = rtDirOpenCommon(phDir, pszPath, NULL, RTDIRFILTER_NONE, 0 /*fFlags*/, ~(uintptr_t)0, NULL);
    LogFlow(("RTDirOpen(%p:{%p}, %p:{%s}): return %Rrc\n", phDir, *phDir, pszPath, pszPath, rc));
    return rc;
}


DECLHIDDEN(int) rtDirOpenRelativeOrHandle(RTDIR *phDir, const char *pszPath, RTDIRFILTER enmFilter, uint32_t fFlags,
                                          uintptr_t hRelativeDir, void *pvNativeRelative)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phDir, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTDIR_F_VALID_MASK), VERR_INVALID_FLAGS);
    switch (enmFilter)
    {
        case RTDIRFILTER_UNIX:
        case RTDIRFILTER_UNIX_UPCASED:
            AssertMsgFailed(("%d is not implemented!\n", enmFilter));
            return VERR_NOT_IMPLEMENTED;
        case RTDIRFILTER_NONE:
        case RTDIRFILTER_WINNT:
            break;
        default:
            AssertMsgFailedReturn(("%d\n", enmFilter), VERR_INVALID_PARAMETER);
    }

    /*
     * Find the last component, i.e. where the filter criteria starts and the dir name ends.
     */
    const char *pszFilter;
    if (enmFilter == RTDIRFILTER_NONE)
        pszFilter = NULL;
    else
    {
        pszFilter = RTPathFilename(pszPath);
        if (!pszFilter) /* trailing slash => directory to read => no filter. */
            enmFilter = RTDIRFILTER_NONE;
    }

    /*
     * Call worker common with RTDirOpen which will verify the path, allocate
     * and initialize the handle, and finally call the backend.
     */
    int rc = rtDirOpenCommon(phDir, pszPath, pszFilter, enmFilter, fFlags, hRelativeDir, pvNativeRelative);

    LogFlow(("RTDirOpenFiltered(%p:{%p}, %p:{%s}, %d, %#x, %p, %p): return %Rrc\n",
             phDir,*phDir, pszPath, pszPath, enmFilter, fFlags, hRelativeDir, pvNativeRelative, rc));
    return rc;
}


RTDECL(int) RTDirOpenFiltered(RTDIR *phDir, const char *pszPath, RTDIRFILTER enmFilter, uint32_t fFlags)
{
    return rtDirOpenRelativeOrHandle(phDir, pszPath, enmFilter, fFlags, ~(uintptr_t)0, NULL);
}


RTDECL(bool) RTDirIsValid(RTDIR hDir)
{
    return RT_VALID_PTR(hDir)
        && hDir->u32Magic == RTDIR_MAGIC;
}


RTDECL(int) RTDirFlushParent(const char *pszChild)
{
    char        *pszPath;
    char        *pszPathFree = NULL;
    size_t const cchChild    = strlen(pszChild);
    if (cchChild < RTPATH_MAX)
        pszPath = (char *)alloca(cchChild + 1);
    else
    {
        pszPathFree = pszPath = (char *)RTMemTmpAlloc(cchChild + 1);
        if (!pszPath)
            return VERR_NO_TMP_MEMORY;
    }
    memcpy(pszPath, pszChild, cchChild);
    pszPath[cchChild] = '\0';
    RTPathStripFilename(pszPath);

    int rc = RTDirFlush(pszPath);

    if (pszPathFree)
        RTMemTmpFree(pszPathFree);
    return rc;
}


RTDECL(int) RTDirQueryUnknownTypeEx(const char *pszComposedName, bool fFollowSymlinks,
                                    RTDIRENTRYTYPE *penmType, PRTFSOBJINFO pObjInfo)
{
    int rc = RTPathQueryInfoEx(pszComposedName, pObjInfo, RTFSOBJATTRADD_NOTHING,
                               fFollowSymlinks ? RTPATH_F_FOLLOW_LINK : RTPATH_F_ON_LINK);
    if (RT_FAILURE(rc))
        return rc;

    if (RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode))
        *penmType = RTDIRENTRYTYPE_DIRECTORY;
    else if (RTFS_IS_FILE(pObjInfo->Attr.fMode))
        *penmType = RTDIRENTRYTYPE_FILE;
    else if (RTFS_IS_SYMLINK(pObjInfo->Attr.fMode))
        *penmType = RTDIRENTRYTYPE_SYMLINK;
    else if (RTFS_IS_FIFO(pObjInfo->Attr.fMode))
        *penmType = RTDIRENTRYTYPE_FIFO;
    else if (RTFS_IS_DEV_CHAR(pObjInfo->Attr.fMode))
        *penmType = RTDIRENTRYTYPE_DEV_CHAR;
    else if (RTFS_IS_DEV_BLOCK(pObjInfo->Attr.fMode))
        *penmType = RTDIRENTRYTYPE_DEV_BLOCK;
    else if (RTFS_IS_SOCKET(pObjInfo->Attr.fMode))
        *penmType = RTDIRENTRYTYPE_SOCKET;
    else if (RTFS_IS_WHITEOUT(pObjInfo->Attr.fMode))
        *penmType = RTDIRENTRYTYPE_WHITEOUT;
    else
        *penmType = RTDIRENTRYTYPE_UNKNOWN;

    return VINF_SUCCESS;
}


RTDECL(int) RTDirQueryUnknownType(const char *pszComposedName, bool fFollowSymlinks, RTDIRENTRYTYPE *penmType)
{
    if (   *penmType != RTDIRENTRYTYPE_UNKNOWN
        && (   !fFollowSymlinks
            || *penmType != RTDIRENTRYTYPE_SYMLINK))
        return VINF_SUCCESS;

    RTFSOBJINFO ObjInfo;
    return RTDirQueryUnknownTypeEx(pszComposedName, fFollowSymlinks, penmType, &ObjInfo);
}


RTDECL(bool) RTDirEntryIsStdDotLink(PRTDIRENTRY pDirEntry)
{
    if (pDirEntry->szName[0] != '.')
        return false;
    if (pDirEntry->cbName == 1)
        return true;
    if (pDirEntry->cbName != 2)
        return false;
    return pDirEntry->szName[1] == '.';
}


RTDECL(bool) RTDirEntryExIsStdDotLink(PCRTDIRENTRYEX pDirEntryEx)
{
    if (pDirEntryEx->szName[0] != '.')
        return false;
    if (pDirEntryEx->cbName == 1)
        return true;
    if (pDirEntryEx->cbName != 2)
        return false;
    return pDirEntryEx->szName[1] == '.';
}


RTDECL(int) RTDirReadExA(RTDIR hDir, PRTDIRENTRYEX *ppDirEntry, size_t *pcbDirEntry, RTFSOBJATTRADD enmAddAttr, uint32_t fFlags)
{
    PRTDIRENTRYEX pDirEntry  = *ppDirEntry;
    size_t        cbDirEntry = *pcbDirEntry;
    if (pDirEntry != NULL && cbDirEntry >= sizeof(RTDIRENTRYEX))
    { /* likely */ }
    else
    {
        Assert(pDirEntry == NULL);
        Assert(cbDirEntry == 0);

        cbDirEntry  = RT_ALIGN_Z(sizeof(RTDIRENTRYEX), 16);
        *ppDirEntry = pDirEntry = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntry);
        if (pDirEntry)
            *pcbDirEntry = cbDirEntry;
        else
        {
            *pcbDirEntry = 0;
            return VERR_NO_TMP_MEMORY;
        }
    }

    for (;;)
    {
        int rc = RTDirReadEx(hDir, pDirEntry, &cbDirEntry, enmAddAttr, fFlags);
        if (rc != VERR_BUFFER_OVERFLOW)
            return rc;

        /* Grow the buffer. */
        RTMemTmpFree(pDirEntry);
        cbDirEntry = RT_MAX(RT_ALIGN_Z(cbDirEntry, 64), *pcbDirEntry + 64);
        *ppDirEntry = pDirEntry = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntry);
        if (pDirEntry)
            *pcbDirEntry = cbDirEntry;
        else
        {
            *pcbDirEntry = 0;
            return VERR_NO_TMP_MEMORY;
        }
    }
}


RTDECL(void) RTDirReadExAFree(PRTDIRENTRYEX *ppDirEntry, size_t *pcbDirEntry)
{
    PRTDIRENTRYEX pDirEntry = *ppDirEntry;
    if (pDirEntry != NULL && *pcbDirEntry >= sizeof(*pcbDirEntry))
        RTMemTmpFree(pDirEntry);
    else
    {
        Assert(pDirEntry == NULL);
        Assert(*pcbDirEntry == 0);
    }
    *ppDirEntry  = NULL;
    *pcbDirEntry = 0;
}

