/* $Id: RTPathGlob.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathGlob
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
#include "internal/iprt.h"
#include <iprt/path.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/uni.h>

#if defined(RT_OS_WINDOWS)
# include <iprt/utf16.h>
# include <iprt/win/windows.h>
# include "../../r3/win/internal-r3-win.h"

#elif defined(RT_OS_OS2)
# define INCL_BASE
# include <os2.h>
# undef RT_MAX /* collision */

#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Maximum number of results. */
#define RTPATHGLOB_MAX_RESULTS          _32K
/** Maximum number of zero-or-more wildcards in a pattern.
 * This limits stack usage and recursion depth, as well as execution time. */
#define RTPATHMATCH_MAX_ZERO_OR_MORE    24
/** Maximum number of variable items. */
#define RTPATHMATCH_MAX_VAR_ITEMS       _4K



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Matching operation.
 */
typedef enum RTPATHMATCHOP
{
    RTPATHMATCHOP_INVALID = 0,
    /** EOS: Returns a match if at end of string. */
    RTPATHMATCHOP_RETURN_MATCH_IF_AT_END,
    /** Asterisk: Returns a match (trailing asterisk). */
    RTPATHMATCHOP_RETURN_MATCH,
    /** Asterisk: Returns a match (just asterisk), unless it's '.' or '..'. */
    RTPATHMATCHOP_RETURN_MATCH_EXCEPT_DOT_AND_DOTDOT,
    /** Plain text: Case sensitive string compare. */
    RTPATHMATCHOP_STRCMP,
    /** Plain text: Case insensitive string compare. */
    RTPATHMATCHOP_STRICMP,
    /** Question marks: Skips exactly one code point. */
    RTPATHMATCHOP_SKIP_ONE_CODEPOINT,
    /** Question marks: Skips exactly RTPATHMATCHCORE::cch code points. */
    RTPATHMATCHOP_SKIP_MULTIPLE_CODEPOINTS,
    /** Char set: Requires the next codepoint to be in the ASCII-7 set defined by
     *            RTPATHMATCHCORE::pch & RTPATHMATCHCORE::cch.  No ranges. */
    RTPATHMATCHOP_CODEPOINT_IN_SET_ASCII7,
    /** Char set: Requires the next codepoint to not be in the ASCII-7 set defined
     *            by RTPATHMATCHCORE::pch & RTPATHMATCHCORE::cch.  No ranges. */
    RTPATHMATCHOP_CODEPOINT_NOT_IN_SET_ASCII7,
    /** Char set: Requires the next codepoint to be in the extended set defined by
     *            RTPATHMATCHCORE::pch & RTPATHMATCHCORE::cch.  Ranges, UTF-8. */
    RTPATHMATCHOP_CODEPOINT_IN_SET_EXTENDED,
    /** Char set: Requires the next codepoint to not be in the extended set defined
     *            by RTPATHMATCHCORE::pch & RTPATHMATCHCORE::cch.  Ranges, UTF-8. */
    RTPATHMATCHOP_CODEPOINT_NOT_IN_SET_EXTENDED,
    /** Variable: Case sensitive variable value compare, RTPATHMATCHCORE::uOp2 is
     *            the variable table index. */
    RTPATHMATCHOP_VARIABLE_VALUE_CMP,
    /** Variable: Case insensitive variable value compare, RTPATHMATCHCORE::uOp2 is
     *            the variable table index. */
    RTPATHMATCHOP_VARIABLE_VALUE_ICMP,
    /** Asterisk: Match zero or more code points, there must be at least
     * RTPATHMATCHCORE::cch code points after it. */
    RTPATHMATCHOP_ZERO_OR_MORE,
    /** Asterisk: Match zero or more code points, there must be at least
     * RTPATHMATCHCORE::cch code points after it, unless it's '.' or '..'. */
    RTPATHMATCHOP_ZERO_OR_MORE_EXCEPT_DOT_AND_DOTDOT,
    /** End of valid operations.   */
    RTPATHMATCHOP_END
} RTPATHMATCHOP;

/**
 * Matching instruction.
 */
typedef struct RTPATHMATCHCORE
{
    /** The action to take. */
    RTPATHMATCHOP       enmOpCode;
    /** Generic value operand. */
    uint16_t            uOp2;
    /** Generic length operand. */
    uint16_t            cch;
    /** Generic string pointer operand. */
    const char         *pch;
} RTPATHMATCHCORE;
/** Pointer to a matching instruction. */
typedef RTPATHMATCHCORE *PRTPATHMATCHCORE;
/** Pointer to a const matching instruction. */
typedef RTPATHMATCHCORE const *PCRTPATHMATCHCORE;

/**
 * Path matching instruction allocator.
 */
typedef struct RTPATHMATCHALLOC
{
    /** Allocated array of instructions. */
    PRTPATHMATCHCORE    paInstructions;
    /** Index of the next free entry in paScratch. */
    uint32_t            iNext;
    /** Number of instructions allocated. */
    uint32_t            cAllocated;
} RTPATHMATCHALLOC;
/** Pointer to a matching instruction allocator. */
typedef RTPATHMATCHALLOC *PRTPATHMATCHALLOC;

/**
 * Path matching cache, mainly intended for variables like the PATH.
 */
typedef struct RTPATHMATCHCACHE
{
    /** @todo optimize later. */
    uint32_t            iNothingYet;
} RTPATHMATCHCACHE;
/** Pointer to a path matching cache. */
typedef RTPATHMATCHCACHE *PRTPATHMATCHCACHE;



/** Parsed path entry.*/
typedef struct RTPATHGLOBPPE
{
    /** Normal: Index into RTPATHGLOB::MatchInstrAlloc.paInstructions. */
    uint32_t            iMatchProg : 16;
    /** Set if this is a normal entry which is matched using iMatchProg. */
    uint32_t            fNormal : 1;
    /** !fNormal: Plain name that can be dealt with using without
     * enumerating the whole directory, unless of course the file system is case
     * sensitive and the globbing isn't (that needs figuring out on a per
     * directory basis). */
    uint32_t            fPlain : 1;
    /** !fNormal: Match zero or more subdirectories. */
    uint32_t            fStarStar : 1;
    /** !fNormal: The whole component is a variable expansion. */
    uint32_t            fExpVariable : 1;

    /** Filter: Set if it only matches directories. */
    uint32_t            fDir : 1;
    /** Set if it's the final component. */
    uint32_t            fFinal : 1;

    /** Unused bits. */
    uint32_t            fReserved : 2+8;
} RTPATHGLOBPPE;


typedef struct RTPATHGLOB
{
    /** Path buffer. */
    char                szPath[RTPATH_MAX];
    /** Temporary buffers. */
    union
    {
        /** File system object info structure. */
        RTFSOBJINFO     ObjInfo;
        /** Directory entry buffer. */
        RTDIRENTRY      DirEntry;
        /** Padding the buffer to an unreasonably large size. */
        uint8_t         abPadding[RTPATH_MAX + sizeof(RTDIRENTRY)];
    } u;


    /** Where to insert the next one.*/
    PRTPATHGLOBENTRY   *ppNext;
    /** The head pointer. */
    PRTPATHGLOBENTRY    pHead;
    /** Result count. */
    uint32_t            cResults;
    /** Counts path overflows. */
    uint32_t            cPathOverflows;
    /** The input flags. */
    uint32_t            fFlags;
    /** Matching instruction allocator. */
    RTPATHMATCHALLOC    MatchInstrAlloc;
    /** Matching state. */
    RTPATHMATCHCACHE    MatchCache;

    /** The pattern string.   */
    const char         *pszPattern;
    /** The parsed path.   */
    PRTPATHPARSED       pParsed;
    /** The component to start with. */
    uint16_t            iFirstComp;
    /** The corresponding path offset (previous components already present). */
    uint16_t            offFirstPath;
    /** Path component information we need. */
    RTPATHGLOBPPE       aComps[1];
} RTPATHGLOB;
typedef RTPATHGLOB *PRTPATHGLOB;


/**
 * Matching variable lookup table.
 * Currently so small we don't bother sorting it and doing binary lookups.
 */
typedef struct RTPATHMATCHVAR
{
    /** The variable name. */
    const char     *pszName;
    /** The variable name length. */
    uint16_t        cchName;
    /** Only available as the verify first component.  */
    bool            fFirstOnly;

    /**
     * Queries a given variable value.
     *
     * @returns IPRT status code.
     * @retval  VERR_BUFFER_OVERFLOW
     * @retval  VERR_TRY_AGAIN if the caller should skip this value item and try the
     *          next one instead (e.g. env var not present).
     * @retval  VINF_EOF when retrieving the last one, if possible.
     * @retval  VERR_EOF when @a iItem is past the item space.
     *
     * @param   iItem       The variable value item to retrieve. (A variable may
     *                      have more than one value, e.g. 'BothProgramFile' on a
     *                      64-bit system or 'Path'.)
     * @param   pszBuf      Where to return the value.
     * @param   cbBuf       The buffer size.
     * @param   pcchValue   Where to return the length of the return string.
     * @param   pCache      Pointer to the path matching cache.  May speed up
     *                      enumerating PATH items and similar.
     */
    DECLCALLBACKMEMBER(int, pfnQuery,(uint32_t iItem, char *pszBuf, size_t cbBuf, size_t *pcchValue, PRTPATHMATCHCACHE pCache));

    /**
     * Matching method, optional.
     *
     * @returns IPRT status code.
     * @retval  VINF_SUCCESS on match.
     * @retval  VERR_MISMATCH on mismatch.
     *
     * @param   pszMatch    String to match with (not terminated).
     * @param   cchMatch    The length of what we match with.
     * @param   fIgnoreCase Whether to ignore case or not when comparing.
     * @param   pcchMatched Where to return the length of the match (value length).
     */
    DECLCALLBACKMEMBER(int, pfnMatch,(const char *pchMatch, size_t cchMatch, bool fIgnoreCase, size_t *pcchMatched));

} RTPATHMATCHVAR;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtPathGlobExecRecursiveStarStar(PRTPATHGLOB pGlob, size_t offPath, uint32_t iStarStarComp, size_t offStarStarPath);
static int rtPathGlobExecRecursiveVarExp(PRTPATHGLOB pGlob, size_t offPath, uint32_t iComp);
static int rtPathGlobExecRecursivePlainText(PRTPATHGLOB pGlob, size_t offPath, uint32_t iComp);
static int rtPathGlobExecRecursiveGeneric(PRTPATHGLOB pGlob, size_t offPath, uint32_t iComp);


/**
 * Implements the two variable access functions for a simple one value variable.
 */
#define RTPATHMATCHVAR_SIMPLE(a_Name, a_GetStrExpr) \
    static DECLCALLBACK(int) RT_CONCAT(rtPathVarQuery_,a_Name)(uint32_t iItem, char *pszBuf, size_t cbBuf, size_t *pcchValue, \
                                                               PRTPATHMATCHCACHE pCache) \
    { \
        if (iItem == 0) \
        { \
            const char *pszValue = a_GetStrExpr; \
            size_t      cchValue = strlen(pszValue); \
            if (cchValue + 1 <= cbBuf) \
            { \
                memcpy(pszBuf, pszValue, cchValue + 1); \
                *pcchValue = cchValue; \
                return VINF_EOF; \
            } \
            return VERR_BUFFER_OVERFLOW; \
        } \
        NOREF(pCache);\
        return VERR_EOF; \
    } \
    static DECLCALLBACK(int) RT_CONCAT(rtPathVarMatch_,a_Name)(const char *pchMatch, size_t cchMatch, bool fIgnoreCase, \
                                                               size_t *pcchMatched) \
    { \
        const char *pszValue = a_GetStrExpr; \
        size_t      cchValue = strlen(pszValue); \
        if (   cchValue >= cchMatch \
            && (  !fIgnoreCase \
                ? memcmp(pszValue, pchMatch, cchValue) == 0 \
                : RTStrNICmp(pszValue, pchMatch, cchValue) == 0) ) \
        { \
            *pcchMatched = cchValue; \
            return VINF_SUCCESS; \
        } \
        return VERR_MISMATCH; \
    } \
    typedef int RT_CONCAT(DummyColonType_,a_Name)

/**
 * Implements mapping a glob variable to an environment variable.
 */
#define RTPATHMATCHVAR_SIMPLE_ENVVAR(a_Name, a_pszEnvVar, a_cbMaxValue) \
    static DECLCALLBACK(int) RT_CONCAT(rtPathVarQuery_,a_Name)(uint32_t iItem, char *pszBuf, size_t cbBuf, size_t *pcchValue, \
                                                               PRTPATHMATCHCACHE pCache) \
    { \
        if (iItem == 0) \
        { \
            int rc = RTEnvGetEx(RTENV_DEFAULT, a_pszEnvVar, pszBuf, cbBuf, pcchValue); \
            if (RT_SUCCESS(rc)) \
                return VINF_EOF; \
            if (rc != VERR_ENV_VAR_NOT_FOUND) \
                return rc; \
        } \
        NOREF(pCache);\
        return VERR_EOF; \
    } \
    static DECLCALLBACK(int) RT_CONCAT(rtPathVarMatch_,a_Name)(const char *pchMatch, size_t cchMatch, bool fIgnoreCase, \
                                                               size_t *pcchMatched) \
    { \
        char   szValue[a_cbMaxValue]; \
        size_t cchValue; \
        int rc = RTEnvGetEx(RTENV_DEFAULT, a_pszEnvVar, szValue, sizeof(szValue), &cchValue); \
        if (   RT_SUCCESS(rc) \
            && cchValue >= cchMatch \
            && (  !fIgnoreCase \
                ? memcmp(szValue, pchMatch, cchValue) == 0 \
                : RTStrNICmp(szValue, pchMatch, cchValue) == 0) ) \
        { \
            *pcchMatched = cchValue; \
            return VINF_SUCCESS; \
        } \
        return VERR_MISMATCH; \
    } \
    typedef int RT_CONCAT(DummyColonType_,a_Name)

/**
 * Implements mapping a glob variable to multiple environment variable values.
 *
 * @param   a_Name              The variable name.
 * @param   a_apszVarNames      Assumes to be a global variable that RT_ELEMENTS
 *                              works correctly on.
 * @param   a_cbMaxValue        The max expected value size.
 */
#define RTPATHMATCHVAR_MULTIPLE_ENVVARS(a_Name, a_apszVarNames, a_cbMaxValue) \
    static DECLCALLBACK(int) RT_CONCAT(rtPathVarQuery_,a_Name)(uint32_t iItem, char *pszBuf, size_t cbBuf, size_t *pcchValue, \
                                                               PRTPATHMATCHCACHE pCache) \
    { \
        if (iItem < RT_ELEMENTS(a_apszVarNames)) \
        { \
            int rc = RTEnvGetEx(RTENV_DEFAULT, a_apszVarNames[iItem], pszBuf, cbBuf, pcchValue); \
            if (RT_SUCCESS(rc)) \
                return iItem + 1 == RT_ELEMENTS(a_apszVarNames) ? VINF_EOF : VINF_SUCCESS; \
            if (rc == VERR_ENV_VAR_NOT_FOUND) \
                rc = VERR_TRY_AGAIN; \
            return rc; \
        } \
        NOREF(pCache);\
        return VERR_EOF; \
    } \
    static DECLCALLBACK(int) RT_CONCAT(rtPathVarMatch_,a_Name)(const char *pchMatch, size_t cchMatch, bool fIgnoreCase, \
                                                               size_t *pcchMatched) \
    { \
        for (uint32_t iItem = 0; iItem < RT_ELEMENTS(a_apszVarNames); iItem++) \
        { \
            char   szValue[a_cbMaxValue]; \
            size_t cchValue; \
            int rc = RTEnvGetEx(RTENV_DEFAULT, a_apszVarNames[iItem], szValue, sizeof(szValue), &cchValue);\
            if (   RT_SUCCESS(rc) \
                && cchValue >= cchMatch \
                && (  !fIgnoreCase \
                    ? memcmp(szValue, pchMatch, cchValue) == 0 \
                    : RTStrNICmp(szValue, pchMatch, cchValue) == 0) ) \
            { \
                *pcchMatched = cchValue; \
                return VINF_SUCCESS; \
            } \
        } \
        return VERR_MISMATCH; \
    } \
    typedef int RT_CONCAT(DummyColonType_,a_Name)


RTPATHMATCHVAR_SIMPLE(Arch, RTBldCfgTargetArch());
RTPATHMATCHVAR_SIMPLE(Bits, RT_XSTR(ARCH_BITS));
#ifdef RT_OS_WINDOWS
RTPATHMATCHVAR_SIMPLE_ENVVAR(WinAppData,                    "AppData",              RTPATH_MAX);
RTPATHMATCHVAR_SIMPLE_ENVVAR(WinProgramData,                "ProgramData",          RTPATH_MAX);
RTPATHMATCHVAR_SIMPLE_ENVVAR(WinProgramFiles,               "ProgramFiles",         RTPATH_MAX);
RTPATHMATCHVAR_SIMPLE_ENVVAR(WinCommonProgramFiles,         "CommonProgramFiles",       RTPATH_MAX);
# if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
RTPATHMATCHVAR_SIMPLE_ENVVAR(WinOtherProgramFiles,          "ProgramFiles(x86)",        RTPATH_MAX);
RTPATHMATCHVAR_SIMPLE_ENVVAR(WinOtherCommonProgramFiles,    "CommonProgramFiles(x86)",  RTPATH_MAX);
# else
#  error "Port ME!"
# endif
static const char * const a_apszWinProgramFilesVars[] =
{
    "ProgramFiles",
# ifdef RT_ARCH_AMD64
    "ProgramFiles(x86)",
# endif
};
RTPATHMATCHVAR_MULTIPLE_ENVVARS(WinAllProgramFiles, a_apszWinProgramFilesVars, RTPATH_MAX);
static const char * const a_apszWinCommonProgramFilesVars[] =
{
    "CommonProgramFiles",
# ifdef RT_ARCH_AMD64
    "CommonProgramFiles(x86)",
# endif
};
RTPATHMATCHVAR_MULTIPLE_ENVVARS(WinAllCommonProgramFiles, a_apszWinCommonProgramFilesVars, RTPATH_MAX);
#endif


/**
 * @interface_method_impl{RTPATHMATCHVAR,pfnQuery, Enumerates the PATH}
 */
static DECLCALLBACK(int) rtPathVarQuery_Path(uint32_t iItem, char *pszBuf, size_t cbBuf, size_t *pcchValue,
                                             PRTPATHMATCHCACHE pCache)
{
    RT_NOREF_PV(pCache);

    /*
     * Query the PATH value.
     */
/** @todo cache this in pCache with iItem and offset.   */
    char       *pszPathFree = NULL;
    char       *pszPath     = pszBuf;
    size_t      cchActual;
    const char *pszVarNm    = "PATH";
    int rc = RTEnvGetEx(RTENV_DEFAULT, pszVarNm, pszPath, cbBuf, &cchActual);
#ifdef RT_OS_WINDOWS
    if (rc == VERR_ENV_VAR_NOT_FOUND)
        rc = RTEnvGetEx(RTENV_DEFAULT, pszVarNm = "Path", pszPath, cbBuf, &cchActual);
#endif
    if (rc == VERR_BUFFER_OVERFLOW)
    {
        for (uint32_t iTry = 0;; iTry++)
        {
            size_t cbPathBuf = RT_ALIGN_Z(cchActual + 1 + 64 * iTry, 64);
            pszPathFree = (char *)RTMemTmpAlloc(cbPathBuf);
            rc = RTEnvGetEx(RTENV_DEFAULT, pszVarNm, pszPathFree, cbPathBuf, &cchActual);
            if (RT_SUCCESS(rc))
                break;
            RTMemTmpFree(pszPathFree);
            AssertReturn(cchActual >= cbPathBuf, VERR_INTERNAL_ERROR_3);
            if (iTry >= 9)
                return rc;
        }
        pszPath = pszPathFree;
    }

    /*
     * Spool forward to the given PATH item.
     */
    rc = VERR_EOF;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    const char  chSep = ';';
#else
    const char  chSep = ':';
#endif
    while (*pszPath != '\0')
    {
        char *pchSep = strchr(pszPath, chSep);

        /* We ignore empty strings, which is probably not entirely correct,
           but works better on DOS based system with many entries added
           without checking whether there is a trailing separator or not.
           Thus, the current directory is only searched if a '.' is present
           in the PATH. */
        if (pchSep == pszPath)
            pszPath++;
        else if (iItem > 0)
        {
            /* If we didn't find a separator, the item doesn't exists. Quit. */
            if (!pchSep)
                break;

            pszPath = pchSep + 1;
            iItem--;
        }
        else
        {
            /* We've reached the item we wanted. */
            size_t cchComp = pchSep ? pchSep - pszPath : strlen(pszPath);
            if (cchComp < cbBuf)
            {
                if (pszBuf != pszPath)
                    memmove(pszBuf, pszPath, cchComp);
                pszBuf[cchComp] = '\0';
                rc = pchSep ? VINF_SUCCESS : VINF_EOF;
            }
            else
                rc = VERR_BUFFER_OVERFLOW;
            *pcchValue = cchComp;
            break;
        }
    }

    if (pszPathFree)
        RTMemTmpFree(pszPathFree);
    return rc;
}


#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
/**
 * @interface_method_impl{RTPATHMATCHVAR,pfnQuery,
 *      The system drive letter + colon.}.
 */
static DECLCALLBACK(int) rtPathVarQuery_DosSystemDrive(uint32_t iItem, char *pszBuf, size_t cbBuf, size_t *pcchValue,
                                                       PRTPATHMATCHCACHE pCache)
{
    RT_NOREF_PV(pCache);

    if (iItem == 0)
    {
        AssertReturn(cbBuf >= 3, VERR_BUFFER_OVERFLOW);

# ifdef RT_OS_WINDOWS
        /* Since this is used at the start of a pattern, we assume
           we've got more than enough buffer space. */
        AssertReturn(g_pfnGetSystemWindowsDirectoryW, VERR_SYMBOL_NOT_FOUND);
        PRTUTF16 pwszTmp = (PRTUTF16)pszBuf;
        UINT cch = g_pfnGetSystemWindowsDirectoryW(pwszTmp, (UINT)(cbBuf / sizeof(WCHAR)));
        if (cch >= 2)
        {
            RTUTF16 wcDrive = pwszTmp[0];
            if (   RT_C_IS_ALPHA(wcDrive)
                && pwszTmp[1] == ':')
            {
                pszBuf[0] = wcDrive;
                pszBuf[1] = ':';
                pszBuf[2] = '\0';
                *pcchValue = 2;
                return VINF_EOF;
            }
        }
# else
        ULONG ulDrive = ~(ULONG)0;
        APIRET rc = DosQuerySysInfo(QSV_BOOT_DRIVE, QSV_BOOT_DRIVE, &ulDrive, sizeof(ulDrive));
        ulDrive--; /* 1 = 'A' */
        if (   rc == NO_ERROR
            && ulDrive <= (ULONG)'Z')
        {
            pszBuf[0] = (char)ulDrive + 'A';
            pszBuf[1] = ':';
            pszBuf[2] = '\0';
            *pcchValue = 2;
            return VINF_EOF;
        }
# endif
        return VERR_INTERNAL_ERROR_4;
    }
    return VERR_EOF;
}
#endif


#ifdef RT_OS_WINDOWS
/**
 * @interface_method_impl{RTPATHMATCHVAR,pfnQuery,
 *      The system root directory (C:\Windows).}.
 */
static DECLCALLBACK(int) rtPathVarQuery_WinSystemRoot(uint32_t iItem, char *pszBuf, size_t cbBuf, size_t *pcchValue,
                                                      PRTPATHMATCHCACHE pCache)
{
    RT_NOREF_PV(pCache);

    if (iItem == 0)
    {
        Assert(pszBuf); Assert(cbBuf);
        AssertReturn(g_pfnGetSystemWindowsDirectoryW, VERR_SYMBOL_NOT_FOUND);
        RTUTF16 wszSystemRoot[MAX_PATH];
        UINT cchSystemRoot = g_pfnGetSystemWindowsDirectoryW(wszSystemRoot, MAX_PATH);
        if (cchSystemRoot > 0)
            return RTUtf16ToUtf8Ex(wszSystemRoot, cchSystemRoot, &pszBuf, cbBuf, pcchValue);
        return RTErrConvertFromWin32(GetLastError());
    }
    return VERR_EOF;
}
#endif

#undef RTPATHMATCHVAR_SIMPLE
#undef RTPATHMATCHVAR_SIMPLE_ENVVAR
#undef RTPATHMATCHVAR_DOUBLE_ENVVAR

/**
 * Variables.
 */
static RTPATHMATCHVAR const g_aVariables[] =
{
    { RT_STR_TUPLE("Arch"),                     false,  rtPathVarQuery_Arch,                        rtPathVarMatch_Arch },
    { RT_STR_TUPLE("Bits"),                     false,  rtPathVarQuery_Bits,                        rtPathVarMatch_Bits },
    { RT_STR_TUPLE("Path"),                     true,   rtPathVarQuery_Path,                        NULL },
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    { RT_STR_TUPLE("SystemDrive"),              true,   rtPathVarQuery_DosSystemDrive,              NULL },
#endif
#ifdef RT_OS_WINDOWS
    { RT_STR_TUPLE("SystemRoot"),               true,   rtPathVarQuery_WinSystemRoot,               NULL },
    { RT_STR_TUPLE("AppData"),                  true,   rtPathVarQuery_WinAppData,                  rtPathVarMatch_WinAppData },
    { RT_STR_TUPLE("ProgramData"),              true,   rtPathVarQuery_WinProgramData,              rtPathVarMatch_WinProgramData },
    { RT_STR_TUPLE("ProgramFiles"),             true,   rtPathVarQuery_WinProgramFiles,             rtPathVarMatch_WinProgramFiles },
    { RT_STR_TUPLE("OtherProgramFiles"),        true,   rtPathVarQuery_WinOtherProgramFiles,        rtPathVarMatch_WinOtherProgramFiles },
    { RT_STR_TUPLE("AllProgramFiles"),          true,   rtPathVarQuery_WinAllProgramFiles,          rtPathVarMatch_WinAllProgramFiles },
    { RT_STR_TUPLE("CommonProgramFiles"),       true,   rtPathVarQuery_WinCommonProgramFiles,       rtPathVarMatch_WinCommonProgramFiles },
    { RT_STR_TUPLE("OtherCommonProgramFiles"),  true,   rtPathVarQuery_WinOtherCommonProgramFiles,  rtPathVarMatch_WinOtherCommonProgramFiles },
    { RT_STR_TUPLE("AllCommonProgramFiles"),    true,   rtPathVarQuery_WinAllCommonProgramFiles,    rtPathVarMatch_WinAllCommonProgramFiles },
#endif
};



/**
 * Handles a complicated set.
 *
 * A complicated set is either using ranges, character classes or code points
 * outside the ASCII-7 range.
 *
 * @returns VINF_SUCCESS or VERR_MISMATCH.  May also return UTF-8 decoding
 *          errors as well as VERR_PATH_MATCH_FEATURE_NOT_IMPLEMENTED.
 *
 * @param   ucInput     The input code point to match with.
 * @param   pchSet      The start of the set specification (after caret).
 * @param   cchSet      The length of the set specification.
 */
static int rtPathMatchExecExtendedSet(RTUNICP ucInput, const char *pchSet, size_t cchSet)
{
    while (cchSet > 0)
    {
        RTUNICP ucSet;
        int rc = RTStrGetCpNEx(&pchSet, &cchSet, &ucSet);
        AssertRCReturn(rc, rc);

        /*
         * Check for character class, collating symbol and equvalence class.
         */
        if (ucSet == '[' && cchSet > 0)
        {
            char chNext = *pchSet;
            if (chNext == ':')
            {
#define CHECK_CHAR_CLASS(a_szClassNm, a_BoolTestExpr) \
                    if (   cchSet >= sizeof(a_szClassNm) \
                        && memcmp(pchSet, a_szClassNm "]", sizeof(a_szClassNm)) == 0) \
                    { \
                        if (a_BoolTestExpr) \
                            return VINF_SUCCESS; \
                        pchSet += sizeof(a_szClassNm); \
                        cchSet -= sizeof(a_szClassNm); \
                        continue; \
                    } do { } while (0)

                CHECK_CHAR_CLASS(":alpha:", RTUniCpIsAlphabetic(ucInput));
                CHECK_CHAR_CLASS(":alnum:", RTUniCpIsAlphabetic(ucInput) || RTUniCpIsDecDigit(ucInput)); /** @todo figure what's correct here and fix uni.h */
                CHECK_CHAR_CLASS(":blank:", ucInput == ' ' || ucInput == '\t');
                CHECK_CHAR_CLASS(":cntrl:", ucInput < 31 || ucInput == 127);
                CHECK_CHAR_CLASS(":digit:", RTUniCpIsDecDigit(ucInput));
                CHECK_CHAR_CLASS(":lower:", RTUniCpIsLower(ucInput));
                CHECK_CHAR_CLASS(":print:", RTUniCpIsAlphabetic(ucInput) || (RT_C_IS_PRINT(ucInput) && ucInput < 127)); /** @todo fixme*/
                CHECK_CHAR_CLASS(":punct:", RT_C_IS_PRINT(ucInput) && ucInput < 127); /** @todo fixme*/
                CHECK_CHAR_CLASS(":space:", RTUniCpIsSpace(ucInput));
                CHECK_CHAR_CLASS(":upper:", RTUniCpIsUpper(ucInput));
                CHECK_CHAR_CLASS(":xdigit:", RTUniCpIsHexDigit(ucInput));
                AssertMsgFailedReturn(("Unknown or malformed char class: '%.*s'\n", cchSet + 1, pchSet - 1),
                                      VERR_PATH_GLOB_UNKNOWN_CHAR_CLASS);
#undef CHECK_CHAR_CLASS
            }
            /** @todo implement collating symbol and equvalence class. */
            else if (chNext == '=' || chNext == '.')
                AssertFailedReturn(VERR_PATH_MATCH_FEATURE_NOT_IMPLEMENTED);
        }

        /*
         * Check for range (leading or final dash does not constitute a range).
         */
        if (cchSet > 1 && *pchSet == '-')
        {
            pchSet++;                   /* skip dash */
            cchSet--;

            RTUNICP ucSet2;
            rc = RTStrGetCpNEx(&pchSet, &cchSet, &ucSet2);
            AssertRCReturn(rc, rc);
            Assert(ucSet < ucSet2);
            if (ucInput >= ucSet && ucInput <= ucSet2)
                return VINF_SUCCESS;
        }
        /*
         * Single char comparison.
         */
        else if (ucInput == ucSet)
            return VINF_SUCCESS;
    }
    return VERR_MISMATCH;
}


/**
 * Variable matching fallback using the query function.
 *
 * This must not be inlined as it consuming a lot of stack!  Which is why it's
 * placed a couple of functions away from the recursive rtPathExecMatch.
 *
 * @returns VINF_SUCCESS or VERR_MISMATCH.
 * @param   pchInput            The current input position.
 * @param   cchInput            The amount of input left..
 * @param   idxVar              The variable table index.
 * @param   fIgnoreCase         Whether to ignore case when comparing.
 * @param   pcchMatched         Where to return how much we actually matched up.
 * @param   pCache              Pointer to the path matching cache.
 */
DECL_NO_INLINE(static, int) rtPathMatchExecVariableFallback(const char *pchInput, size_t cchInput, uint16_t idxVar,
                                                            bool fIgnoreCase, size_t *pcchMatched, PRTPATHMATCHCACHE pCache)
{
    for (uint32_t iItem = 0; iItem < RTPATHMATCH_MAX_VAR_ITEMS; iItem++)
    {
        char   szValue[RTPATH_MAX];
        size_t cchValue;
        int rc = g_aVariables[idxVar].pfnQuery(iItem, szValue, sizeof(szValue), &cchValue, pCache);
        if (RT_SUCCESS(rc))
        {
            if (cchValue <= cchInput)
            {
                if (  !fIgnoreCase
                    ? memcmp(pchInput, szValue, cchValue) == 0
                    : RTStrNICmp(pchInput, szValue, cchValue) == 0)
                {
                    *pcchMatched = cchValue;
                    return VINF_SUCCESS;
                }
            }
            if (rc == VINF_EOF)
                return VERR_MISMATCH;
        }
        else if (rc == VERR_EOF)
            return VERR_MISMATCH;
        else
            Assert(rc == VERR_BUFFER_OVERFLOW || rc == VERR_TRY_AGAIN);
    }
    AssertFailed();
    return VERR_MISMATCH;
}


/**
 * Variable matching worker.
 *
 * @returns VINF_SUCCESS or VERR_MISMATCH.
 * @param   pchInput            The current input position.
 * @param   cchInput            The amount of input left..
 * @param   idxVar              The variable table index.
 * @param   fIgnoreCase         Whether to ignore case when comparing.
 * @param   pcchMatched         Where to return how much we actually matched up.
 * @param   pCache              Pointer to the path matching cache.
 */
static int rtPathMatchExecVariable(const char *pchInput, size_t cchInput, uint16_t idxVar,
                                   bool fIgnoreCase, size_t *pcchMatched, PRTPATHMATCHCACHE pCache)
{
    Assert(idxVar < RT_ELEMENTS(g_aVariables));
    if (g_aVariables[idxVar].pfnMatch)
        return g_aVariables[idxVar].pfnMatch(pchInput, cchInput, fIgnoreCase, pcchMatched);
    return rtPathMatchExecVariableFallback(pchInput, cchInput, idxVar, fIgnoreCase, pcchMatched, pCache);
}


/**
 * Variable matching worker.
 *
 * @returns VINF_SUCCESS or VERR_MISMATCH.
 * @param   pchInput            The current input position.
 * @param   cchInput            The amount of input left..
 * @param   pProg               The first matching program instruction.
 * @param   pCache              Pointer to the path matching cache.
 */
static int rtPathMatchExec(const char *pchInput, size_t cchInput, PCRTPATHMATCHCORE pProg, PRTPATHMATCHCACHE pCache)
{
    for (;;)
    {
        switch (pProg->enmOpCode)
        {
            case RTPATHMATCHOP_RETURN_MATCH_IF_AT_END:
                return cchInput == 0 ? VINF_SUCCESS : VERR_MISMATCH;

            case RTPATHMATCHOP_RETURN_MATCH:
                return VINF_SUCCESS;

            case RTPATHMATCHOP_RETURN_MATCH_EXCEPT_DOT_AND_DOTDOT:
                if (   cchInput > 2
                    || cchInput < 1
                    || pchInput[0] != '.'
                    || (cchInput == 2 && pchInput[1] != '.') )
                    return VINF_SUCCESS;
                return VERR_MISMATCH;

            case RTPATHMATCHOP_STRCMP:
                if (pProg->cch > cchInput)
                    return VERR_MISMATCH;
                if (memcmp(pchInput, pProg->pch, pProg->cch) != 0)
                    return VERR_MISMATCH;
                cchInput -= pProg->cch;
                pchInput += pProg->cch;
                break;

            case RTPATHMATCHOP_STRICMP:
                if (pProg->cch > cchInput)
                    return VERR_MISMATCH;
                if (RTStrNICmp(pchInput, pProg->pch, pProg->cch) != 0)
                    return VERR_MISMATCH;
                cchInput -= pProg->cch;
                pchInput += pProg->cch;
                break;

            case RTPATHMATCHOP_SKIP_ONE_CODEPOINT:
            {
                if (cchInput == 0)
                    return VERR_MISMATCH;
                RTUNICP ucInputIgnore;
                int rc = RTStrGetCpNEx(&pchInput, &cchInput, &ucInputIgnore);
                AssertRCReturn(rc, rc);
                break;
            }

            case RTPATHMATCHOP_SKIP_MULTIPLE_CODEPOINTS:
            {
                uint16_t cCpsLeft = pProg->cch;
                Assert(cCpsLeft > 1);
                if (cCpsLeft > cchInput)
                    return VERR_MISMATCH;
                while (cCpsLeft-- > 0)
                {
                    RTUNICP ucInputIgnore;
                    int rc = RTStrGetCpNEx(&pchInput, &cchInput, &ucInputIgnore);
                    if (RT_FAILURE(rc))
                        return rc == VERR_END_OF_STRING ? VERR_MISMATCH : rc;
                }
                break;
            }

            case RTPATHMATCHOP_CODEPOINT_IN_SET_ASCII7:
            {
                if (cchInput == 0)
                    return VERR_MISMATCH;
                RTUNICP ucInput;
                int rc = RTStrGetCpNEx(&pchInput, &cchInput, &ucInput);
                AssertRCReturn(rc, rc);
                if (ucInput >= 0x80)
                    return VERR_MISMATCH;
                if (memchr(pProg->pch, (char)ucInput, pProg->cch) == NULL)
                    return VERR_MISMATCH;
                break;
            }

            case RTPATHMATCHOP_CODEPOINT_NOT_IN_SET_ASCII7:
            {
                if (cchInput == 0)
                    return VERR_MISMATCH;
                RTUNICP ucInput;
                int rc = RTStrGetCpNEx(&pchInput, &cchInput, &ucInput);
                AssertRCReturn(rc, rc);
                if (ucInput >= 0x80)
                    break;
                if (memchr(pProg->pch, (char)ucInput, pProg->cch) != NULL)
                    return VERR_MISMATCH;
                break;
            }

            case RTPATHMATCHOP_CODEPOINT_IN_SET_EXTENDED:
            {
                if (cchInput == 0)
                    return VERR_MISMATCH;
                RTUNICP ucInput;
                int rc = RTStrGetCpNEx(&pchInput, &cchInput, &ucInput);
                AssertRCReturn(rc, rc);
                rc = rtPathMatchExecExtendedSet(ucInput, pProg->pch, pProg->cch);
                if (rc == VINF_SUCCESS)
                    break;
                return rc;
            }

            case RTPATHMATCHOP_CODEPOINT_NOT_IN_SET_EXTENDED:
            {
                if (cchInput == 0)
                    return VERR_MISMATCH;
                RTUNICP ucInput;
                int rc = RTStrGetCpNEx(&pchInput, &cchInput, &ucInput);
                AssertRCReturn(rc, rc);
                rc = rtPathMatchExecExtendedSet(ucInput, pProg->pch, pProg->cch);
                if (rc == VERR_MISMATCH)
                    break;
                if (rc == VINF_SUCCESS)
                    rc = VERR_MISMATCH;
                return rc;
            }

            case RTPATHMATCHOP_VARIABLE_VALUE_CMP:
            case RTPATHMATCHOP_VARIABLE_VALUE_ICMP:
            {
                size_t cchMatched = 0;
                int rc = rtPathMatchExecVariable(pchInput, cchInput, pProg->uOp2,
                                                 pProg->enmOpCode == RTPATHMATCHOP_VARIABLE_VALUE_ICMP, &cchMatched, pCache);
                if (rc == VINF_SUCCESS)
                {
                    pchInput += cchMatched;
                    cchInput -= cchMatched;
                    break;
                }
                return rc;
            }

            /*
             * This is the expensive one. It always completes the program.
             */
            case RTPATHMATCHOP_ZERO_OR_MORE:
            {
                if (cchInput < pProg->cch)
                    return VERR_MISMATCH;
                size_t cchMatched = cchInput - pProg->cch;
                do
                {
                    int rc = rtPathMatchExec(&pchInput[cchMatched], cchInput - cchMatched, pProg + 1, pCache);
                    if (RT_SUCCESS(rc))
                        return rc;
                } while (cchMatched-- > 0);
                return VERR_MISMATCH;
            }

            /*
             * Variant of the above that doesn't match '.' and '..' entries.
             */
            case RTPATHMATCHOP_ZERO_OR_MORE_EXCEPT_DOT_AND_DOTDOT:
            {
                if (cchInput < pProg->cch)
                    return VERR_MISMATCH;
                if (   cchInput <= 2
                    && cchInput > 0
                    && pchInput[0] == '.'
                    && (cchInput == 1 || pchInput[1] == '.') )
                    return VERR_MISMATCH;
                size_t cchMatched = cchInput - pProg->cch;
                do
                {
                    int rc = rtPathMatchExec(&pchInput[cchMatched], cchInput - cchMatched, pProg + 1, pCache);
                    if (RT_SUCCESS(rc))
                        return rc;
                } while (cchMatched-- > 0);
                return VERR_MISMATCH;
            }

            default:
                AssertMsgFailedReturn(("enmOpCode=%d\n", pProg->enmOpCode), VERR_INTERNAL_ERROR_3);
        }

        pProg++;
    }
}




/**
 * Compiles a path matching program.
 *
 * @returns IPRT status code.
 * @param   pchPattern          The pattern to compile.
 * @param   cchPattern          The length of the pattern.
 * @param   fIgnoreCase         Whether to ignore case or not when doing the
 *                              actual matching later on.
 * @param   pAllocator          Pointer to the instruction allocator & result
 *                              array.  The compiled "program" starts at
 *                              PRTPATHMATCHALLOC::paInstructions[PRTPATHMATCHALLOC::iNext]
 *                              (input iNext value).
 *
 * @todo Expose this matching code and also use it for RTDirOpenFiltered
 */
static int rtPathMatchCompile(const char *pchPattern, size_t cchPattern, bool fIgnoreCase, PRTPATHMATCHALLOC pAllocator)
{
    /** @todo PORTME: big endian. */
    static const uint8_t s_bmMetaChars[256/8] =
    {
        0x00, 0x00, 0x00, 0x00, /*  0 thru 31 */
        0x10, 0x04, 0x00, 0x80, /* 32 thru 63 */
        0x00, 0x00, 0x00, 0x08, /* 64 thru 95 */
        0x00, 0x00, 0x00, 0x00, /* 96 thru 127 */
        /* UTF-8 multibyte: */
        0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    };
    Assert(ASMBitTest(s_bmMetaChars, '$')); AssertCompile('$' == 0x24 /*36*/);
    Assert(ASMBitTest(s_bmMetaChars, '*')); AssertCompile('*' == 0x2a /*42*/);
    Assert(ASMBitTest(s_bmMetaChars, '?')); AssertCompile('?' == 0x3f /*63*/);
    Assert(ASMBitTest(s_bmMetaChars, '[')); AssertCompile('[' == 0x5b /*91*/);

    /*
     * For checking for the first instruction.
     */
    uint16_t const iFirst = pAllocator->iNext;

    /*
     * This is for tracking zero-or-more instructions and for calculating
     * the minimum amount of input required for it to be considered.
     */
    uint16_t aiZeroOrMore[RTPATHMATCH_MAX_ZERO_OR_MORE];
    uint8_t  cZeroOrMore = 0;
    size_t   offInput    = 0;

    /*
     * Loop thru the pattern and translate it into string matching instructions.
     */
    for (;;)
    {
        /*
         * Allocate the next instruction.
         */
        if (pAllocator->iNext >= pAllocator->cAllocated)
        {
            uint32_t cNew = pAllocator->cAllocated ? pAllocator->cAllocated * 2 : 2;
            void *pvNew = RTMemRealloc(pAllocator->paInstructions, cNew * sizeof(pAllocator->paInstructions[0]));
            AssertReturn(pvNew, VERR_NO_MEMORY);
            pAllocator->paInstructions = (PRTPATHMATCHCORE)pvNew;
            pAllocator->cAllocated     = cNew;
        }
        PRTPATHMATCHCORE pInstr = &pAllocator->paInstructions[pAllocator->iNext++];
        pInstr->pch  = pchPattern;
        pInstr->cch  = 0;
        pInstr->uOp2 = 0;

        /*
         * Special case: End of pattern.
         */
        if (!cchPattern)
        {
            pInstr->enmOpCode = RTPATHMATCHOP_RETURN_MATCH_IF_AT_END;
            break;
        }

        /*
         * Parse the next bit of the pattern.
         */
        char ch = *pchPattern;
        if (ASMBitTest(s_bmMetaChars, (uint8_t)ch))
        {
            /*
             * Zero or more characters wildcard.
             */
            if (ch == '*')
            {
                /* Skip extra asterisks. */
                do
                {
                    cchPattern--;
                    pchPattern++;
                } while (cchPattern > 0 && *pchPattern == '*');

                /* There is a special optimization for trailing '*'. */
                pInstr->cch = 1;
                if (cchPattern == 0)
                {
                    pInstr->enmOpCode = iFirst + 1U == pAllocator->iNext
                                      ? RTPATHMATCHOP_RETURN_MATCH_EXCEPT_DOT_AND_DOTDOT : RTPATHMATCHOP_RETURN_MATCH;
                    break;
                }

                pInstr->enmOpCode = iFirst + 1U == pAllocator->iNext
                                  ? RTPATHMATCHOP_ZERO_OR_MORE_EXCEPT_DOT_AND_DOTDOT : RTPATHMATCHOP_ZERO_OR_MORE;
                pInstr->uOp2      = (uint16_t)offInput;
                AssertReturn(cZeroOrMore < RT_ELEMENTS(aiZeroOrMore), VERR_OUT_OF_RANGE);
                aiZeroOrMore[cZeroOrMore] = (uint16_t)(pInstr - pAllocator->paInstructions);

                /* cchInput unchanged, zero-or-more matches. */
                continue;
            }

            /*
             * Single character wildcard.
             */
            if (ch == '?')
            {
                /* Count them if more. */
                uint16_t cchQms = 1;
                while (cchQms < cchPattern && pchPattern[cchQms] == '?')
                    cchQms++;

                pInstr->cch = cchQms;
                pInstr->enmOpCode = cchQms == 1 ? RTPATHMATCHOP_SKIP_ONE_CODEPOINT : RTPATHMATCHOP_SKIP_MULTIPLE_CODEPOINTS;

                cchPattern -= cchQms;
                pchPattern += cchQms;
                offInput   += cchQms;
                continue;
            }

            /*
             * Character in set.
             *
             * Note that we skip the first char in the set as that is the only place
             * ']' can be placed if one desires to explicitly include it in the set.
             * To make life a bit more interesting, [:class:] is allowed inside the
             * set, so we have to do the counting game to find the end.
             */
            if (ch == '[')
            {
                if (   cchPattern > 2
                    && (const char *)memchr(pchPattern + 2, ']', cchPattern) != NULL)
                {

                    /* Check for not-in. */
                    bool   fInverted = false;
                    size_t offStart  = 1;
                    if (pchPattern[offStart] == '^')
                    {
                        fInverted = true;
                        offStart++;
                    }

                    /* Special case for ']' as the first char, it doesn't indicate closing then. */
                    size_t off = offStart;
                    if (pchPattern[off] == ']')
                        off++;

                    bool fExtended = false;
                    while (off < cchPattern)
                    {
                        ch = pchPattern[off++];
                        if (ch == '[')
                        {
                            if (off < cchPattern)
                            {
                                char chOpen = pchPattern[off];
                                if (   chOpen == ':'
                                    || chOpen == '='
                                    || chOpen == '.')
                                {
                                    off++;
                                    const char *pchFound = (const char *)memchr(&pchPattern[off], ']', cchPattern - off);
                                    if (   pchFound
                                        && pchFound[-1] == chOpen)
                                    {
                                        fExtended = true;
                                        off = pchFound - pchPattern + 1;
                                    }
                                    else
                                        AssertFailed();
                                }
                            }
                        }
                        /* Check for closing. */
                        else if (ch == ']')
                            break;
                        /* Check for range expression, promote to extended if this happens. */
                        else if (   ch == '-'
                                 && off != offStart + 1
                                 && off < cchPattern
                                 && pchPattern[off] != ']')
                            fExtended = true;
                        /* UTF-8 multibyte chars forces us to use the extended version too. */
                        else if ((uint8_t)ch >= 0x80)
                            fExtended = true;
                    }

                    if (ch == ']')
                    {
                        pInstr->pch = &pchPattern[offStart];
                        pInstr->cch = (uint16_t)(off - offStart - 1);
                        if (!fExtended)
                            pInstr->enmOpCode = !fInverted
                                              ? RTPATHMATCHOP_CODEPOINT_IN_SET_ASCII7 : RTPATHMATCHOP_CODEPOINT_NOT_IN_SET_ASCII7;
                        else
                            pInstr->enmOpCode = !fInverted
                                              ? RTPATHMATCHOP_CODEPOINT_IN_SET_EXTENDED
                                              : RTPATHMATCHOP_CODEPOINT_NOT_IN_SET_EXTENDED;
                        pchPattern += off;
                        cchPattern -= off;
                        offInput   += 1;
                        continue;
                    }

                    /* else: invalid, treat it as */
                    AssertFailed();
                }
            }
            /*
             * Variable matching.
             */
            else if (ch == '$')
            {
                const char *pchFound;
                if (   cchPattern > 3
                    && pchPattern[1] == '{'
                    && (pchFound = (const char *)memchr(pchPattern + 2, '}', cchPattern)) != NULL
                    && pchFound != &pchPattern[2])
                {
                    /* skip to the variable name. */
                    pchPattern += 2;
                    cchPattern -= 2;
                    size_t cchVarNm = pchFound - pchPattern;

                    /* Look it up. */
                    uint32_t iVar;
                    for (iVar = 0; iVar < RT_ELEMENTS(g_aVariables); iVar++)
                        if (   g_aVariables[iVar].cchName == cchVarNm
                            && memcmp(g_aVariables[iVar].pszName, pchPattern, cchVarNm) == 0)
                            break;
                    if (iVar < RT_ELEMENTS(g_aVariables))
                    {
                        pInstr->uOp2      = (uint16_t)iVar;
                        pInstr->enmOpCode = !fIgnoreCase ? RTPATHMATCHOP_VARIABLE_VALUE_CMP : RTPATHMATCHOP_VARIABLE_VALUE_ICMP;
                        pInstr->pch       = pchPattern;             /* not necessary */
                        pInstr->cch       = (uint16_t)cchPattern;   /* ditto */
                        pchPattern += cchVarNm + 1;
                        cchPattern -= cchVarNm + 1;
                        AssertMsgReturn(!g_aVariables[iVar].fFirstOnly || iFirst + 1U == pAllocator->iNext,
                                        ("Glob variable '%s' should be first\n", g_aVariables[iVar].pszName),
                                        VERR_PATH_MATCH_VARIABLE_MUST_BE_FIRST);
                        /* cchInput unchanged, value can be empty. */
                        continue;
                    }
                    AssertMsgFailedReturn(("Unknown path matching variable '%.*s'\n", cchVarNm, pchPattern),
                                          VERR_PATH_MATCH_UNKNOWN_VARIABLE);
                }
            }
            else
                AssertFailedReturn(VERR_INTERNAL_ERROR_2); /* broken bitmap / compiler codeset */
        }

        /*
         * Plain text.  Look for the next meta char.
         */
        uint32_t cchPlain = 1;
        while (cchPlain < cchPattern)
        {
            ch = pchPattern[cchPlain];
            if (!ASMBitTest(s_bmMetaChars, (uint8_t)ch))
            { /* probable */ }
            else if (   ch == '?'
                     || ch == '*')
                break;
            else if (ch == '$')
            {
                const char *pchFound;
                if (   cchPattern > cchPlain + 3
                    && pchPattern[cchPlain + 1] == '{'
                    && (pchFound = (const char *)memchr(&pchPattern[cchPlain + 2], '}', cchPattern - cchPlain - 2)) != NULL
                    && pchFound != &pchPattern[cchPlain + 2])
                break;
            }
            else if (ch == '[')
            {
                /* We don't put a lot of effort into getting this 100% right here,
                   no point it complicating things for malformed expressions. */
                if (   cchPattern > cchPlain + 2
                    && memchr(&pchPattern[cchPlain + 2], ']', cchPattern - cchPlain - 1) != NULL)
                    break;
            }
            else
                AssertFailedReturn(VERR_INTERNAL_ERROR_2); /* broken bitmap / compiler codeset */
            cchPlain++;
        }
        pInstr->enmOpCode = !fIgnoreCase ? RTPATHMATCHOP_STRCMP : RTPATHMATCHOP_STRICMP;
        pInstr->cch       = cchPlain;
        Assert(pInstr->pch == pchPattern);
        Assert(pInstr->uOp2 == 0);
        pchPattern += cchPlain;
        cchPattern -= cchPlain;
        offInput   += cchPlain;
    }

    /*
     * Optimize zero-or-more matching.
     */
    while (cZeroOrMore-- > 0)
    {
        PRTPATHMATCHCORE pInstr = &pAllocator->paInstructions[aiZeroOrMore[cZeroOrMore]];
        pInstr->uOp2 = (uint16_t)(offInput - pInstr->uOp2);
    }

    /** @todo It's possible to use offInput to inject a instruction for checking
     *        minimum input length at the start of the program.  Not sure it's
     *        worth it though, unless it's long a complicated expression... */
    return VINF_SUCCESS;
}


/**
 * Parses the glob pattern.
 *
 * This compiles filename matching programs for each component and determins the
 * optimal search strategy for them.
 *
 * @returns IPRT status code.
 * @param   pGlob               The glob instance data.
 * @param   pszPattern          The pattern to parse.
 * @param   pParsed             The RTPathParse output for the pattern.
 * @param   fFlags              The glob flags (same as pGlob->fFlags).
 */
static int rtPathGlobParse(PRTPATHGLOB pGlob, const char *pszPattern, PRTPATHPARSED pParsed, uint32_t fFlags)
{
    AssertReturn(pParsed->cComps > 0, VERR_INVALID_PARAMETER); /* shouldn't happen */
    uint32_t iComp = 0;

    /*
     * If we've got a rootspec, mark it as plain.  On platforms with
     * drive letter and/or UNC we don't allow wildcards or such in
     * the drive letter spec or UNC server name.  (At least not yet.)
     */
    if (RTPATH_PROP_HAS_ROOT_SPEC(pParsed->fProps))
    {
        AssertReturn(pParsed->aComps[0].cch < sizeof(pGlob->szPath) - 1, VERR_FILENAME_TOO_LONG);
        memcpy(pGlob->szPath, &pszPattern[pParsed->aComps[0].off], pParsed->aComps[0].cch);
        pGlob->offFirstPath = pParsed->aComps[0].cch;
        pGlob->iFirstComp   = iComp = 1;
    }
    else
    {
        const char * const pszComp = &pszPattern[pParsed->aComps[0].off];

        /*
         * The tilde is only applicable to the first component, expand it
         * immediately.
         */
        if (   *pszComp == '~'
            && !(fFlags & RTPATHGLOB_F_NO_TILDE))
        {
            if (pParsed->aComps[0].cch == 1)
            {
                int rc = RTPathUserHome(pGlob->szPath, sizeof(pGlob->szPath) - 1);
                AssertRCReturn(rc, rc);
            }
            else
                AssertMsgFailedReturn(("'%.*s' is not supported yet\n", pszComp, pParsed->aComps[0].cch),
                                      VERR_PATH_MATCH_FEATURE_NOT_IMPLEMENTED);
            pGlob->offFirstPath = (uint16_t)RTPathEnsureTrailingSeparator(pGlob->szPath, sizeof(pGlob->szPath));
            pGlob->iFirstComp   = iComp = 1;
        }
    }

    /*
     * Process the other components.
     */
    bool fStarStar = false;
    for (; iComp < pParsed->cComps; iComp++)
    {
        const char *pszComp = &pszPattern[pParsed->aComps[iComp].off];
        uint16_t    cchComp = pParsed->aComps[iComp].cch;
        Assert(pGlob->aComps[iComp].fNormal == false);

        pGlob->aComps[iComp].fDir = iComp + 1 < pParsed->cComps || (fFlags & RTPATHGLOB_F_ONLY_DIRS);
        if (   cchComp != 2
            || pszComp[0] != '*'
            || pszComp[1] != '*'
            || (fFlags & RTPATHGLOB_F_NO_STARSTAR) )
        {
            /* Compile the pattern. */
            uint16_t const iMatchProg = pGlob->MatchInstrAlloc.iNext;
            pGlob->aComps[iComp].iMatchProg = iMatchProg;
            int rc = rtPathMatchCompile(pszComp, cchComp, RT_BOOL(fFlags & RTPATHGLOB_F_IGNORE_CASE),
                                        &pGlob->MatchInstrAlloc);
            if (RT_FAILURE(rc))
                return rc;

            /* Check for plain text as well as full variable matching (not applicable after '**'). */
            uint16_t const cInstructions = pGlob->MatchInstrAlloc.iNext - iMatchProg;
            if (   cInstructions == 2
                && !fStarStar
                && pGlob->MatchInstrAlloc.paInstructions[iMatchProg + 1].enmOpCode == RTPATHMATCHOP_RETURN_MATCH_IF_AT_END)
            {
                if (   pGlob->MatchInstrAlloc.paInstructions[iMatchProg].enmOpCode == RTPATHMATCHOP_STRCMP
                    || pGlob->MatchInstrAlloc.paInstructions[iMatchProg].enmOpCode == RTPATHMATCHOP_STRICMP)
                    pGlob->aComps[iComp].fPlain  = true;
                else if (   pGlob->MatchInstrAlloc.paInstructions[iMatchProg].enmOpCode == RTPATHMATCHOP_VARIABLE_VALUE_CMP
                         || pGlob->MatchInstrAlloc.paInstructions[iMatchProg].enmOpCode == RTPATHMATCHOP_VARIABLE_VALUE_ICMP)
                {
                    pGlob->aComps[iComp].fExpVariable = true;
                    AssertMsgReturn(   iComp == 0
                                    || !g_aVariables[pGlob->MatchInstrAlloc.paInstructions[iMatchProg].uOp2].fFirstOnly,
                                    ("Glob variable '%.*s' can only be used as the path component.\n",  cchComp, pszComp),
                                    VERR_PATH_MATCH_VARIABLE_MUST_BE_FIRST);
                }
                else
                    pGlob->aComps[iComp].fNormal = true;
            }
            else
                pGlob->aComps[iComp].fNormal = true;
        }
        else
        {
            /* Recursive "**" matching. */
            pGlob->aComps[iComp].fNormal   = false;
            pGlob->aComps[iComp].fStarStar = true;
            AssertReturn(!fStarStar, VERR_PATH_MATCH_FEATURE_NOT_IMPLEMENTED); /** @todo implement multiple '**' sequences in a pattern. */
            fStarStar = true;
        }
    }
    pGlob->aComps[pParsed->cComps - 1].fFinal = true;

    return VINF_SUCCESS;
}


/**
 * This is for skipping overly long directories entries.
 *
 * Since our directory entry buffer can hold filenames of RTPATH_MAX bytes, we
 * can safely skip filenames that are longer.  There are very few file systems
 * that can actually store filenames longer than 255 bytes at time of coding
 * (2015-09), and extremely few which can exceed 4096 (RTPATH_MAX) bytes.
 *
 * @returns IPRT status code.
 * @param   hDir        The directory handle.
 * @param   cbNeeded    The required entry size.
 */
DECL_NO_INLINE(static, int) rtPathGlobSkipDirEntry(RTDIR hDir, size_t cbNeeded)
{
    int rc = VERR_BUFFER_OVERFLOW;
    cbNeeded = RT_ALIGN_Z(cbNeeded, 16);
    PRTDIRENTRY pDirEntry = (PRTDIRENTRY)RTMemTmpAlloc(cbNeeded);
    if (pDirEntry)
    {
        rc = RTDirRead(hDir, pDirEntry, &cbNeeded);
        RTMemTmpFree(pDirEntry);
    }
    return rc;
}


/**
 * Adds a result.
 *
 * @returns IPRT status code.
 * @retval  VINF_CALLBACK_RETURN if we can stop searching.
 *
 * @param   pGlob       The glob instance data.
 * @param   cchPath     The number of bytes to add from pGlob->szPath.
 * @param   uType       The RTDIRENTRYTYPE value.
 */
DECL_NO_INLINE(static, int) rtPathGlobAddResult(PRTPATHGLOB pGlob, size_t cchPath, uint8_t uType)
{
    if (pGlob->cResults < RTPATHGLOB_MAX_RESULTS)
    {
        PRTPATHGLOBENTRY pEntry = (PRTPATHGLOBENTRY)RTMemAlloc(RT_UOFFSETOF_DYN(RTPATHGLOBENTRY, szPath[cchPath + 1]));
        if (pEntry)
        {
            pEntry->uType   = uType;
            pEntry->cchPath = (uint16_t)cchPath;
            memcpy(pEntry->szPath, pGlob->szPath, cchPath);
            pEntry->szPath[cchPath] = '\0';

            pEntry->pNext  = NULL;
            *pGlob->ppNext = pEntry;
            pGlob->ppNext  = &pEntry->pNext;
            pGlob->cResults++;

            if (!(pGlob->fFlags & RTPATHGLOB_F_FIRST_ONLY))
                return VINF_SUCCESS;
            return VINF_CALLBACK_RETURN;
        }
        return VERR_NO_MEMORY;
    }
    return VERR_TOO_MUCH_DATA;
}


/**
 * Adds a result, constructing the path from two string.
 *
 * @returns IPRT status code.
 * @retval  VINF_CALLBACK_RETURN if we can stop searching.
 *
 * @param   pGlob       The glob instance data.
 * @param   cchPath     The number of bytes to add from pGlob->szPath.
 * @param   pchName     The string (usual filename) to append to the szPath.
 * @param   cchName     The length of the string to append.
 * @param   uType       The RTDIRENTRYTYPE value.
 */
DECL_NO_INLINE(static, int) rtPathGlobAddResult2(PRTPATHGLOB pGlob, size_t cchPath, const char *pchName, size_t cchName,
                                                 uint8_t uType)
{
    if (pGlob->cResults < RTPATHGLOB_MAX_RESULTS)
    {
        PRTPATHGLOBENTRY pEntry = (PRTPATHGLOBENTRY)RTMemAlloc(RT_UOFFSETOF_DYN(RTPATHGLOBENTRY, szPath[cchPath + cchName + 1]));
        if (pEntry)
        {
            pEntry->uType   = uType;
            pEntry->cchPath = (uint16_t)(cchPath + cchName);
            memcpy(pEntry->szPath, pGlob->szPath, cchPath);
            memcpy(&pEntry->szPath[cchPath], pchName, cchName);
            pEntry->szPath[cchPath + cchName] = '\0';

            pEntry->pNext  = NULL;
            *pGlob->ppNext = pEntry;
            pGlob->ppNext  = &pEntry->pNext;
            pGlob->cResults++;

            if (!(pGlob->fFlags & RTPATHGLOB_F_FIRST_ONLY))
                return VINF_SUCCESS;
            return VINF_CALLBACK_RETURN;
        }
        return VERR_NO_MEMORY;
    }
    return VERR_TOO_MUCH_DATA;
}


/**
 * Prepares a result, constructing the path from two string.
 *
 * The caller must call either rtPathGlobCommitResult or
 * rtPathGlobRollbackResult to complete the operation.
 *
 * @returns IPRT status code.
 * @retval  VINF_CALLBACK_RETURN if we can stop searching.
 *
 * @param   pGlob       The glob instance data.
 * @param   cchPath     The number of bytes to add from pGlob->szPath.
 * @param   pchName     The string (usual filename) to append to the szPath.
 * @param   cchName     The length of the string to append.
 * @param   uType       The RTDIRENTRYTYPE value.
 */
DECL_NO_INLINE(static, int) rtPathGlobAlmostAddResult(PRTPATHGLOB pGlob, size_t cchPath, const char *pchName, size_t cchName,
                                                      uint8_t uType)
{
    if (pGlob->cResults < RTPATHGLOB_MAX_RESULTS)
    {
        PRTPATHGLOBENTRY pEntry = (PRTPATHGLOBENTRY)RTMemAlloc(RT_UOFFSETOF_DYN(RTPATHGLOBENTRY, szPath[cchPath + cchName + 1]));
        if (pEntry)
        {
            pEntry->uType   = uType;
            pEntry->cchPath = (uint16_t)(cchPath + cchName);
            memcpy(pEntry->szPath, pGlob->szPath, cchPath);
            memcpy(&pEntry->szPath[cchPath], pchName, cchName);
            pEntry->szPath[cchPath + cchName] = '\0';

            pEntry->pNext  = NULL;
            *pGlob->ppNext = pEntry;
            /* Note! We don't update ppNext here, that is done in rtPathGlobCommitResult. */

            if (!(pGlob->fFlags & RTPATHGLOB_F_FIRST_ONLY))
                return VINF_SUCCESS;
            return VINF_CALLBACK_RETURN;
        }
        return VERR_NO_MEMORY;
    }
    return VERR_TOO_MUCH_DATA;
}


/**
 * Commits a pending result from rtPathGlobAlmostAddResult.
 *
 * @param   pGlob       The glob instance data.
 * @param   uType       The RTDIRENTRYTYPE value.
 */
static void rtPathGlobCommitResult(PRTPATHGLOB pGlob, uint8_t uType)
{
    PRTPATHGLOBENTRY pEntry = *pGlob->ppNext;
    AssertPtr(pEntry);
    pEntry->uType = uType;
    pGlob->ppNext = &pEntry->pNext;
    pGlob->cResults++;
}


/**
 * Rolls back a pending result from rtPathGlobAlmostAddResult.
 *
 * @param   pGlob       The glob instance data.
 */
static void rtPathGlobRollbackResult(PRTPATHGLOB pGlob)
{
    PRTPATHGLOBENTRY pEntry = *pGlob->ppNext;
    AssertPtr(pEntry);
    RTMemFree(pEntry);
    *pGlob->ppNext = NULL;
}



/**
 * Whether to call rtPathGlobExecRecursiveVarExp for the next component.
 *
 * @returns true / false.
 * @param   pGlob       The glob instance data.
 * @param   offPath     The next path offset/length.
 * @param   iComp       The next component.
 */
DECLINLINE(bool) rtPathGlobExecIsExpVar(PRTPATHGLOB pGlob, size_t offPath, uint32_t iComp)
{
    return pGlob->aComps[iComp].fExpVariable
        && (  !(pGlob->fFlags & RTPATHGLOB_F_IGNORE_CASE)
            || (offPath ? !RTFsIsCaseSensitive(pGlob->szPath) : !RTFsIsCaseSensitive(".")) );
}

/**
 * Whether to call rtPathGlobExecRecursivePlainText for the next component.
 *
 * @returns true / false.
 * @param   pGlob       The glob instance data.
 * @param   offPath     The next path offset/length.
 * @param   iComp       The next component.
 */
DECLINLINE(bool) rtPathGlobExecIsPlainText(PRTPATHGLOB pGlob, size_t offPath, uint32_t iComp)
{
    return pGlob->aComps[iComp].fPlain
        && (  !(pGlob->fFlags & RTPATHGLOB_F_IGNORE_CASE)
            || (offPath ? !RTFsIsCaseSensitive(pGlob->szPath) : !RTFsIsCaseSensitive(".")) );
}


/**
 * Helper for rtPathGlobExecRecursiveVarExp and rtPathGlobExecRecursivePlainText
 * that compares a file mode mask with dir/no-dir wishes of the caller.
 *
 * @returns true if match, false if not.
 * @param   pGlob       The glob instance data.
 * @param   fMode       The file mode (only the type is used).
 */
DECLINLINE(bool) rtPathGlobExecIsMatchFinalWithFileMode(PRTPATHGLOB pGlob, RTFMODE fMode)
{
    if (!(pGlob->fFlags & (RTPATHGLOB_F_NO_DIRS | RTPATHGLOB_F_ONLY_DIRS)))
        return true;
    return RT_BOOL(pGlob->fFlags & RTPATHGLOB_F_ONLY_DIRS) == RTFS_IS_DIRECTORY(fMode);
}


/**
 * Recursive globbing - star-star mode.
 *
 * @returns IPRT status code.
 * @retval  VINF_CALLBACK_RETURN is used to implement RTPATHGLOB_F_FIRST_ONLY.
 *
 * @param   pGlob               The glob instance data.
 * @param   offPath             The current path offset/length.
 * @param   iStarStarComp       The star-star component index.
 * @param   offStarStarPath     The offset of the star-star component in the
 *                              pattern path.
 */
DECL_NO_INLINE(static, int) rtPathGlobExecRecursiveStarStar(PRTPATHGLOB pGlob, size_t offPath, uint32_t iStarStarComp,
                                                            size_t offStarStarPath)
{
    /** @todo implement multi subdir matching. */
    RT_NOREF_PV(pGlob);
    RT_NOREF_PV(offPath);
    RT_NOREF_PV(iStarStarComp);
    RT_NOREF_PV(offStarStarPath);
    return VERR_PATH_MATCH_FEATURE_NOT_IMPLEMENTED;
}



/**
 * Recursive globbing - variable expansion optimization.
 *
 * @returns IPRT status code.
 * @retval  VINF_CALLBACK_RETURN is used to implement RTPATHGLOB_F_FIRST_ONLY.
 *
 * @param   pGlob               The glob instance data.
 * @param   offPath             The current path offset/length.
 * @param   iComp               The current component.
 */
DECL_NO_INLINE(static, int) rtPathGlobExecRecursiveVarExp(PRTPATHGLOB pGlob, size_t offPath, uint32_t iComp)
{
    Assert(iComp < pGlob->pParsed->cComps);
    Assert(pGlob->szPath[offPath] == '\0');
    Assert(pGlob->aComps[iComp].fExpVariable);
    Assert(!pGlob->aComps[iComp].fPlain);
    Assert(!pGlob->aComps[iComp].fStarStar);
    Assert(rtPathGlobExecIsExpVar(pGlob, offPath, iComp));

    /*
     * Fish the variable index out of the first matching instruction.
     */
    Assert(      pGlob->MatchInstrAlloc.paInstructions[pGlob->aComps[iComp].iMatchProg].enmOpCode
              == RTPATHMATCHOP_VARIABLE_VALUE_CMP
           ||   pGlob->MatchInstrAlloc.paInstructions[pGlob->aComps[iComp].iMatchProg].enmOpCode
              == RTPATHMATCHOP_VARIABLE_VALUE_ICMP);
    uint16_t const iVar = pGlob->MatchInstrAlloc.paInstructions[pGlob->aComps[iComp].iMatchProg].uOp2;

    /*
     * Enumerate all the variable, giving them the plain text treatment.
     */
    for (uint32_t iItem = 0; iItem < RTPATHMATCH_MAX_VAR_ITEMS; iItem++)
    {
        size_t cch;
        int rcVar = g_aVariables[iVar].pfnQuery(iItem, &pGlob->szPath[offPath], sizeof(pGlob->szPath) - offPath, &cch,
                                                &pGlob->MatchCache);
        if (RT_SUCCESS(rcVar))
        {
            Assert(pGlob->szPath[offPath + cch] == '\0');

            int rc = RTPathQueryInfoEx(pGlob->szPath, &pGlob->u.ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
            if (RT_SUCCESS(rc))
            {
                if (pGlob->aComps[iComp].fFinal)
                {
                    if (rtPathGlobExecIsMatchFinalWithFileMode(pGlob, pGlob->u.ObjInfo.Attr.fMode))
                    {
                        rc = rtPathGlobAddResult(pGlob, cch,
                                                 (pGlob->u.ObjInfo.Attr.fMode & RTFS_TYPE_MASK)
                                                 >> RTFS_TYPE_DIRENTRYTYPE_SHIFT);
                        if (rc != VINF_SUCCESS)
                            return rc;
                    }
                }
                else if (RTFS_IS_DIRECTORY(pGlob->u.ObjInfo.Attr.fMode))
                {
                    Assert(pGlob->aComps[iComp].fDir);
                    cch = RTPathEnsureTrailingSeparator(pGlob->szPath, sizeof(pGlob->szPath));
                    if (cch > 0)
                    {
                        if (rtPathGlobExecIsExpVar(pGlob, cch, iComp + 1))
                            rc = rtPathGlobExecRecursiveVarExp(pGlob, cch, iComp + 1);
                        else if (rtPathGlobExecIsPlainText(pGlob, cch, iComp + 1))
                            rc = rtPathGlobExecRecursivePlainText(pGlob, cch, iComp + 1);
                        else if (pGlob->aComps[pGlob->iFirstComp].fStarStar)
                            rc = rtPathGlobExecRecursiveStarStar(pGlob, cch, iComp + 1, cch);
                        else
                            rc = rtPathGlobExecRecursiveGeneric(pGlob, cch, iComp + 1);
                        if (rc != VINF_SUCCESS)
                            return rc;
                    }
                    else
                        pGlob->cPathOverflows++;
                }
            }
            /* else: file doesn't exist or something else is wrong, ignore this. */
            if (rcVar == VINF_EOF)
                return VINF_SUCCESS;
        }
        else if (rcVar == VERR_EOF)
            return VINF_SUCCESS;
        else if (rcVar != VERR_TRY_AGAIN)
        {
            Assert(rcVar == VERR_BUFFER_OVERFLOW);
            pGlob->cPathOverflows++;
        }
    }
    AssertFailedReturn(VINF_SUCCESS); /* Too many items returned, probably buggy query method. */
}


/**
 * Recursive globbing - plain text optimization.
 *
 * @returns IPRT status code.
 * @retval  VINF_CALLBACK_RETURN is used to implement RTPATHGLOB_F_FIRST_ONLY.
 *
 * @param   pGlob               The glob instance data.
 * @param   offPath             The current path offset/length.
 * @param   iComp               The current component.
 */
DECL_NO_INLINE(static, int) rtPathGlobExecRecursivePlainText(PRTPATHGLOB pGlob, size_t offPath, uint32_t iComp)
{
    /*
     * Instead of recursing, we loop thru adjacent plain text components.
     */
    for (;;)
    {
        /*
         * Preconditions.
         */
        Assert(iComp < pGlob->pParsed->cComps);
        Assert(pGlob->szPath[offPath] == '\0');
        Assert(pGlob->aComps[iComp].fPlain);
        Assert(!pGlob->aComps[iComp].fExpVariable);
        Assert(!pGlob->aComps[iComp].fStarStar);
        Assert(rtPathGlobExecIsPlainText(pGlob, offPath, iComp));
        Assert(pGlob->MatchInstrAlloc.paInstructions[pGlob->aComps[iComp].iMatchProg].enmOpCode
                  == RTPATHMATCHOP_STRCMP
               ||   pGlob->MatchInstrAlloc.paInstructions[pGlob->aComps[iComp].iMatchProg].enmOpCode
                  == RTPATHMATCHOP_STRICMP);

        /*
         * Add the plain text component to the path.
         */
        size_t const cch = pGlob->pParsed->aComps[iComp].cch;
        if (cch + pGlob->aComps[iComp].fDir < sizeof(pGlob->szPath) - offPath)
        {
            memcpy(&pGlob->szPath[offPath], &pGlob->pszPattern[pGlob->pParsed->aComps[iComp].off], cch);
            offPath += cch;
            pGlob->szPath[offPath] = '\0';

            /*
             * Check if it exists.
             */
            int rc = RTPathQueryInfoEx(pGlob->szPath, &pGlob->u.ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
            if (RT_SUCCESS(rc))
            {
                if (pGlob->aComps[iComp].fFinal)
                {
                    if (rtPathGlobExecIsMatchFinalWithFileMode(pGlob, pGlob->u.ObjInfo.Attr.fMode))
                        return rtPathGlobAddResult(pGlob, offPath,
                                                   (pGlob->u.ObjInfo.Attr.fMode & RTFS_TYPE_MASK)
                                                   >> RTFS_TYPE_DIRENTRYTYPE_SHIFT);
                    break;
                }

                if (RTFS_IS_DIRECTORY(pGlob->u.ObjInfo.Attr.fMode))
                {
                    Assert(pGlob->aComps[iComp].fDir);
                    pGlob->szPath[offPath++] = RTPATH_SLASH;
                    pGlob->szPath[offPath]   = '\0';

                    iComp++;
                    if (rtPathGlobExecIsExpVar(pGlob, offPath, iComp))
                        return rtPathGlobExecRecursiveVarExp(pGlob, offPath, iComp);
                    if (!rtPathGlobExecIsPlainText(pGlob, offPath, iComp))
                        return rtPathGlobExecRecursiveGeneric(pGlob, offPath, iComp);
                    if (pGlob->aComps[pGlob->iFirstComp].fStarStar)
                        return rtPathGlobExecRecursiveStarStar(pGlob, offPath, iComp, offPath);

                    /* Continue with the next plain text component. */
                    continue;
                }
            }
            /* else: file doesn't exist or something else is wrong, ignore this. */
        }
        else
            pGlob->cPathOverflows++;
        break;
    }
    return VINF_SUCCESS;
}


/**
 * Recursive globbing - generic.
 *
 * @returns IPRT status code.
 * @retval  VINF_CALLBACK_RETURN is used to implement RTPATHGLOB_F_FIRST_ONLY.
 *
 * @param   pGlob               The glob instance data.
 * @param   offPath             The current path offset/length.
 * @param   iComp               The current component.
 */
DECL_NO_INLINE(static, int) rtPathGlobExecRecursiveGeneric(PRTPATHGLOB pGlob, size_t offPath, uint32_t iComp)
{
    /*
     * Enumerate entire directory and match each entry.
     */
    RTDIR hDir;
    int rc = RTDirOpen(&hDir, offPath ? pGlob->szPath : ".");
    if (RT_SUCCESS(rc))
    {
        for (;;)
        {
            size_t cch = sizeof(pGlob->u);
            rc = RTDirRead(hDir, &pGlob->u.DirEntry, &cch);
            if (RT_SUCCESS(rc))
            {
                if (pGlob->aComps[iComp].fFinal)
                {
                    /*
                     * Final component: Check if it matches the current pattern.
                     */
                    if (   !(pGlob->fFlags & (RTPATHGLOB_F_NO_DIRS | RTPATHGLOB_F_ONLY_DIRS))
                        ||    RT_BOOL(pGlob->fFlags & RTPATHGLOB_F_ONLY_DIRS)
                           == (pGlob->u.DirEntry.enmType == RTDIRENTRYTYPE_DIRECTORY)
                        || pGlob->u.DirEntry.enmType == RTDIRENTRYTYPE_UNKNOWN)
                    {
                        rc = rtPathMatchExec(pGlob->u.DirEntry.szName, pGlob->u.DirEntry.cbName,
                                             &pGlob->MatchInstrAlloc.paInstructions[pGlob->aComps[iComp].iMatchProg],
                                             &pGlob->MatchCache);
                        if (RT_SUCCESS(rc))
                        {
                            /* Construct the result. */
                            if (   pGlob->u.DirEntry.enmType != RTDIRENTRYTYPE_UNKNOWN
                                || !(pGlob->fFlags & (RTPATHGLOB_F_NO_DIRS | RTPATHGLOB_F_ONLY_DIRS)) )
                                rc = rtPathGlobAddResult2(pGlob, offPath, pGlob->u.DirEntry.szName, pGlob->u.DirEntry.cbName,
                                                          (uint8_t)pGlob->u.DirEntry.enmType);
                            else
                            {
                                rc = rtPathGlobAlmostAddResult(pGlob, offPath,
                                                               pGlob->u.DirEntry.szName, pGlob->u.DirEntry.cbName,
                                                               (uint8_t)RTDIRENTRYTYPE_UNKNOWN);
                                if (RT_SUCCESS(rc))
                                {
                                    RTDirQueryUnknownType((*pGlob->ppNext)->szPath, false /*fFollowSymlinks*/,
                                                          &pGlob->u.DirEntry.enmType);
                                    if (   RT_BOOL(pGlob->fFlags & RTPATHGLOB_F_ONLY_DIRS)
                                        == (pGlob->u.DirEntry.enmType == RTDIRENTRYTYPE_DIRECTORY))
                                        rtPathGlobCommitResult(pGlob, (uint8_t)pGlob->u.DirEntry.enmType);
                                    else
                                        rtPathGlobRollbackResult(pGlob);
                                }
                            }
                            if (rc != VINF_SUCCESS)
                                break;
                        }
                        else
                        {
                            AssertMsgBreak(rc == VERR_MISMATCH, ("%Rrc\n", rc));
                            rc = VINF_SUCCESS;
                        }
                    }
                }
                /*
                 * Intermediate component: Directories only.
                 */
                else if (   pGlob->u.DirEntry.enmType == RTDIRENTRYTYPE_DIRECTORY
                         || pGlob->u.DirEntry.enmType == RTDIRENTRYTYPE_UNKNOWN)
                {
                    rc = rtPathMatchExec(pGlob->u.DirEntry.szName, pGlob->u.DirEntry.cbName,
                                         &pGlob->MatchInstrAlloc.paInstructions[pGlob->aComps[iComp].iMatchProg],
                                         &pGlob->MatchCache);
                    if (RT_SUCCESS(rc))
                    {
                        /* Recurse down into the alleged directory. */
                        cch = offPath + pGlob->u.DirEntry.cbName;
                        if (cch + 1 < sizeof(pGlob->szPath))
                        {
                            memcpy(&pGlob->szPath[offPath], pGlob->u.DirEntry.szName, pGlob->u.DirEntry.cbName);
                            pGlob->szPath[cch++] = RTPATH_SLASH;
                            pGlob->szPath[cch]   = '\0';

                            if (rtPathGlobExecIsExpVar(pGlob, cch, iComp + 1))
                                rc = rtPathGlobExecRecursiveVarExp(pGlob, cch, iComp + 1);
                            else if (rtPathGlobExecIsPlainText(pGlob, cch, iComp + 1))
                                rc = rtPathGlobExecRecursivePlainText(pGlob, cch, iComp + 1);
                            else if (pGlob->aComps[pGlob->iFirstComp].fStarStar)
                                rc = rtPathGlobExecRecursiveStarStar(pGlob, cch, iComp + 1, cch);
                            else
                                rc = rtPathGlobExecRecursiveGeneric(pGlob, cch, iComp + 1);
                            if (rc != VINF_SUCCESS)
                                return rc;
                        }
                        else
                            pGlob->cPathOverflows++;
                    }
                    else
                    {
                        AssertMsgBreak(rc == VERR_MISMATCH, ("%Rrc\n", rc));
                        rc = VINF_SUCCESS;
                    }
                }
            }
            /*
             * RTDirRead failure.
             */
            else
            {
                /* The end?  */
                if (rc == VERR_NO_MORE_FILES)
                    rc = VINF_SUCCESS;
                /* Try skip the entry if we end up with an overflow (szPath can't hold it either then). */
                else if (rc == VERR_BUFFER_OVERFLOW)
                {
                    pGlob->cPathOverflows++;
                    rc = rtPathGlobSkipDirEntry(hDir, cch);
                    if (RT_SUCCESS(rc))
                        continue;
                }
                /* else: Any other error is unexpected and should be reported. */
                break;
            }
        }

        RTDirClose(hDir);
    }
    /* Directory doesn't exist or something else is wrong, ignore this. */
    else
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * Executes a glob search.
 *
 * @returns IPRT status code.
 * @param   pGlob               The glob instance data.
 */
static int rtPathGlobExec(PRTPATHGLOB pGlob)
{
    Assert(pGlob->offFirstPath < sizeof(pGlob->szPath));
    Assert(pGlob->szPath[pGlob->offFirstPath] == '\0');

    int rc;
    if (RT_LIKELY(pGlob->iFirstComp < pGlob->pParsed->cComps))
    {
        /*
         * Call the appropriate function.
         */
        if (rtPathGlobExecIsExpVar(pGlob, pGlob->offFirstPath, pGlob->iFirstComp))
            rc = rtPathGlobExecRecursiveVarExp(pGlob, pGlob->offFirstPath, pGlob->iFirstComp);
        else if (rtPathGlobExecIsPlainText(pGlob, pGlob->offFirstPath, pGlob->iFirstComp))
            rc = rtPathGlobExecRecursivePlainText(pGlob, pGlob->offFirstPath, pGlob->iFirstComp);
        else if (pGlob->aComps[pGlob->iFirstComp].fStarStar)
            rc = rtPathGlobExecRecursiveStarStar(pGlob, pGlob->offFirstPath, pGlob->iFirstComp, pGlob->offFirstPath);
        else
            rc = rtPathGlobExecRecursiveGeneric(pGlob, pGlob->offFirstPath, pGlob->iFirstComp);
    }
    else
    {
        /*
         * Special case where we only have a root component or tilde expansion.
         */
        Assert(pGlob->offFirstPath > 0);
        rc = RTPathQueryInfoEx(pGlob->szPath, &pGlob->u.ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
        if (   RT_SUCCESS(rc)
            && rtPathGlobExecIsMatchFinalWithFileMode(pGlob, pGlob->u.ObjInfo.Attr.fMode))
            rc = rtPathGlobAddResult(pGlob, pGlob->offFirstPath,
                                     (pGlob->u.ObjInfo.Attr.fMode & RTFS_TYPE_MASK) >> RTFS_TYPE_DIRENTRYTYPE_SHIFT);
        else
            rc = VINF_SUCCESS;
    }

    /*
     * Adjust the status code.  Check for results, hide RTPATHGLOB_F_FIRST_ONLY
     * status code, and add warning if necessary.
     */
    if (pGlob->cResults > 0)
    {
        if (rc == VINF_CALLBACK_RETURN)
            rc = VINF_SUCCESS;
        if (rc == VINF_SUCCESS)
        {
            if (pGlob->cPathOverflows > 0)
                rc = VINF_BUFFER_OVERFLOW;
        }
    }
    else
        rc = VERR_FILE_NOT_FOUND;

    return rc;
}


RTDECL(int) RTPathGlob(const char *pszPattern, uint32_t fFlags, PPCRTPATHGLOBENTRY ppHead, uint32_t *pcResults)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(ppHead, VERR_INVALID_POINTER);
    *ppHead = NULL;
    if (pcResults)
    {
        AssertPtrReturn(pcResults, VERR_INVALID_POINTER);
        *pcResults = 0;
    }
    AssertPtrReturn(pszPattern, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTPATHGLOB_F_MASK), VERR_INVALID_FLAGS);
    AssertReturn((fFlags & (RTPATHGLOB_F_NO_DIRS | RTPATHGLOB_F_ONLY_DIRS)) != (RTPATHGLOB_F_NO_DIRS | RTPATHGLOB_F_ONLY_DIRS),
                 VERR_INVALID_FLAGS);

    /*
     * Parse the path.
     */
    size_t        cbParsed = RT_UOFFSETOF(RTPATHPARSED, aComps[1]); /** @todo 16 after testing */
    PRTPATHPARSED pParsed = (PRTPATHPARSED)RTMemTmpAlloc(cbParsed);
    AssertReturn(pParsed, VERR_NO_MEMORY);
    int rc = RTPathParse(pszPattern, pParsed, cbParsed, RTPATH_STR_F_STYLE_HOST);
    if (rc == VERR_BUFFER_OVERFLOW)
    {
        cbParsed = RT_UOFFSETOF_DYN(RTPATHPARSED, aComps[pParsed->cComps + 1]);
        RTMemTmpFree(pParsed);
        pParsed = (PRTPATHPARSED)RTMemTmpAlloc(cbParsed);
        AssertReturn(pParsed, VERR_NO_MEMORY);

        rc = RTPathParse(pszPattern, pParsed, cbParsed, RTPATH_STR_F_STYLE_HOST);
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Check dir slash vs. only/not dir flag.
         */
        if (   !(fFlags & RTPATHGLOB_F_NO_DIRS)
            || (   !(pParsed->fProps & RTPATH_PROP_DIR_SLASH)
                && (   !(pParsed->fProps & (RTPATH_PROP_ROOT_SLASH | RTPATH_PROP_UNC))
                    || pParsed->cComps > 1) ) )
        {
            if (pParsed->fProps & RTPATH_PROP_DIR_SLASH)
                fFlags |= RTPATHGLOB_F_ONLY_DIRS;

            /*
             * Allocate and initialize the glob state data structure.
             */
            size_t      cbGlob = RT_UOFFSETOF_DYN(RTPATHGLOB, aComps[pParsed->cComps + 1]);
            PRTPATHGLOB pGlob  = (PRTPATHGLOB)RTMemTmpAllocZ(cbGlob);
            if (pGlob)
            {
                pGlob->pszPattern = pszPattern;
                pGlob->fFlags     = fFlags;
                pGlob->pParsed    = pParsed;
                pGlob->ppNext     = &pGlob->pHead;
                rc = rtPathGlobParse(pGlob, pszPattern, pParsed, fFlags);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Execute the search.
                     */
                    rc = rtPathGlobExec(pGlob);
                    if (RT_SUCCESS(rc))
                    {
                        *ppHead = pGlob->pHead;
                        if (pcResults)
                            *pcResults = pGlob->cResults;
                    }
                    else
                        RTPathGlobFree(pGlob->pHead);
                }

                RTMemTmpFree(pGlob->MatchInstrAlloc.paInstructions);
                RTMemTmpFree(pGlob);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_NOT_FOUND;
    }
    RTMemTmpFree(pParsed);
    return rc;


}


RTDECL(void) RTPathGlobFree(PCRTPATHGLOBENTRY pHead)
{
    PRTPATHGLOBENTRY pCur = (PRTPATHGLOBENTRY)pHead;
    while (pCur)
    {
        PRTPATHGLOBENTRY pNext = pCur->pNext;
        pCur->pNext = NULL;
        RTMemFree(pCur);
        pCur = pNext;
    }
}

