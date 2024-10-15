/* $Id: inifile.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - INI-file parser.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#include <iprt/inifile.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/latin1.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/vfs.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def RTINIFILE_MAX_SIZE
 * The maximum INI-file size we accept loading. */
#if ARCH_BITS > 32
# define RTINIFILE_MAX_SIZE         (_64M - 2U)
#elif ARCH_BITS > 16
# define RTINIFILE_MAX_SIZE         (_16M - 2U)
#else
# define RTINIFILE_MAX_SIZE         (_64K - 2U)
#endif

/** @def RTINIFILE_MAX_SECTIONS
 * The maximum number of sections we accept in an INI-file. */
#if ARCH_BITS > 32
# define RTINIFILE_MAX_SECTIONS     (_1M)
#elif ARCH_BITS > 16
# define RTINIFILE_MAX_SECTIONS     (_256K)
#else
# define RTINIFILE_MAX_SECTIONS     (_1K)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * File encoding types.
 */
typedef enum RTINIFILEENCODING
{
    /** The customary invalid zero value.   */
    RTINIFILEENCODING_INVALID = 0,
    /** We treat this as latin-1. */
    RTINIFILEENCODING_ANSI,
    /** UTF-8. */
    RTINIFILEENCODING_UTF8,
    /** Little endian UTF-16. */
    RTINIFILEENCODING_UTF16LE,
    /** Big endian UTF-16. */
    RTINIFILEENCODING_UTF16BE,
    /** End of valid encoding types. */
    RTINIFILEENCODING_END
} RTINIFILEENCODING;


/**
 * Preparsed section info.
 */
typedef struct RTINIFILESECTION
{
    /** The section name offset (byte). */
    uint32_t            offName;
    /** The section length in bytes starting with the name. */
    uint32_t            cchSection;
    /** The UTF-8 length of the section name. */
    uint32_t            cchName;
    /** Offset into the section where to start looking for values. */
    uint32_t            cchSkipToValues : 24;
    /** @todo use 4 bits for flags and stuff. like escaped name.   */
} RTINIFILESECTION;
/** Pointer to preparsed section info. */
typedef RTINIFILESECTION *PRTINIFILESECTION;


/**
 * INI-file instance data.
 */
typedef struct RTINIFILEINT
{
    /** Magic value (RTINIFILEINT_MAGIC). */
    uint32_t            u32Magic;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** The file we're working on. */
    RTVFSFILE           hVfsFile;
    /** Flags, RTINIFILE_F_XXX. */
    uint32_t            fFlags;

    /** The original file encoding. */
    RTINIFILEENCODING   enmEncoding;
    /** Pointer to the file content (converted to UTF-8). */
    char               *pszFile;
    /** The file size. */
    uint32_t            cbFile;
    /** Number of sections. */
    uint32_t            cSections;
    /** Sections in the loaded file. */
    PRTINIFILESECTION   paSections;

} RTINIFILEINT;
/** Pointer to an INI-file instance. */
typedef RTINIFILEINT *PRTINIFILEINT;


static int rtIniFileLoad(PRTINIFILEINT pThis)
{
    /*
     * Load the entire file into memory, ensuring two terminating zeros.
     */
    uint64_t cbFile;
    int rc = RTVfsFileQuerySize(pThis->hVfsFile, &cbFile);
    AssertRCReturn(rc, rc);

    if (cbFile > RTINIFILE_MAX_SIZE)
        return VERR_TOO_MUCH_DATA;
    if (cbFile == 0)
        return VINF_SUCCESS; /* Nothing to do. */

    pThis->cbFile  = (uint32_t)cbFile;
    pThis->pszFile = (char *)RTMemAllocZ(pThis->cbFile + 2);
    if (!pThis->pszFile)
        return VERR_NO_MEMORY;

    rc = RTVfsFileReadAt(pThis->hVfsFile, 0, pThis->pszFile, pThis->cbFile, NULL);
    AssertRCReturn(rc, rc);

    /*
     * Detect encoding and convert to BOM prefixed UTF-8.
     */
    if (   (uint8_t)pThis->pszFile[0] == UINT8_C(0xef)
        && (uint8_t)pThis->pszFile[1] == UINT8_C(0xbb)
        && (uint8_t)pThis->pszFile[2] == UINT8_C(0xbf))
    {
        pThis->enmEncoding = RTINIFILEENCODING_UTF8;
        rc = RTStrValidateEncoding(&pThis->pszFile[3]);
        if (RT_FAILURE(rc))
            return rc;
    }
    else
    {
        size_t cchUtf8;
        if (   (uint8_t)pThis->pszFile[0] == UINT8_C(0xfe)
                 && (uint8_t)pThis->pszFile[1] == UINT8_C(0xff))
        {
            pThis->enmEncoding = RTINIFILEENCODING_UTF16BE;
            rc = RTUtf16BigCalcUtf8LenEx((PCRTUTF16)&pThis->pszFile[2], RTSTR_MAX, &cchUtf8);
        }
        else if (   (uint8_t)pThis->pszFile[0] == UINT8_C(0xff)
                 && (uint8_t)pThis->pszFile[1] == UINT8_C(0xfe))
        {
            pThis->enmEncoding = RTINIFILEENCODING_UTF16LE;
            rc = RTUtf16LittleCalcUtf8LenEx((PCRTUTF16)&pThis->pszFile[2], RTSTR_MAX, &cchUtf8);
        }
        else
        {
            pThis->enmEncoding = RTINIFILEENCODING_ANSI;
            rc = RTLatin1CalcUtf8LenEx(pThis->pszFile, RTSTR_MAX, &cchUtf8);
        }
        if (RT_FAILURE(rc))
            return rc;

        char *pszUtf8Bom = (char *)RTMemAllocZ(3 + cchUtf8 + 1);
        if (!pszUtf8Bom)
            return VERR_NO_MEMORY;
        pszUtf8Bom[0] = '\xEF';
        pszUtf8Bom[1] = '\xBB';
        pszUtf8Bom[2] = '\xBF';

        char *pszUtf8 = pszUtf8Bom + 3;
        if (pThis->enmEncoding == RTINIFILEENCODING_UTF16BE)
            rc = RTUtf16BigToUtf8Ex((PCRTUTF16)&pThis->pszFile[2], RTSTR_MAX, &pszUtf8, cchUtf8 + 1, NULL);
        else if (pThis->enmEncoding == RTINIFILEENCODING_UTF16LE)
            rc = RTUtf16LittleToUtf8Ex((PCRTUTF16)&pThis->pszFile[2], RTSTR_MAX, &pszUtf8, cchUtf8 + 1, NULL);
        else
            rc = RTLatin1ToUtf8Ex(pThis->pszFile, RTSTR_MAX, &pszUtf8, cchUtf8 + 1, NULL);
        AssertRCReturnStmt(rc, RTMemFree(pszUtf8Bom), rc);

        RTMemFree(pThis->pszFile);
        pThis->pszFile = pszUtf8Bom;
        pThis->cbFile  = 3 + (uint32_t)cchUtf8;
    }

    /*
     * Do a rough section count.
     * Section zero is for unsectioned values at the start of the file.
     */
    uint32_t    cSections = 1;
    const char *psz       = pThis->pszFile + 3;
    char        ch;
    while ((ch = *psz) != '\0')
    {
        while (RT_C_IS_SPACE(ch))
            ch = *++psz;
        if (ch == '[')
            cSections++;

        /* next line. */
        psz = strchr(psz, '\n');
        if (psz)
            psz++;
        else
            break;
    }
    if (cSections > RTINIFILE_MAX_SECTIONS)
        return VERR_TOO_MUCH_DATA;

    /*
     * Allocation section array and do the preparsing.
     */
    pThis->paSections = (PRTINIFILESECTION)RTMemAllocZ(sizeof(pThis->paSections[0]) * cSections);
    if (!pThis->paSections)
        return VERR_NO_MEMORY;

    uint32_t iSection = 0;
    pThis->paSections[0].offName         = 3;
    pThis->paSections[0].cchName         = 0;
    pThis->paSections[0].cchSkipToValues = 0;
    psz = pThis->pszFile + 3;
    while ((ch = *psz) != '\0')
    {
        const char *const pszLine = psz;

        while (RT_C_IS_SPACE(ch))
            ch = *++psz;
        if (ch == '[')
        {
            /* Complete previous section. */
            pThis->paSections[iSection].cchSection = (uint32_t)(pszLine - &pThis->pszFile[pThis->paSections[iSection].offName]);

            /* New section. */
            iSection++;
            AssertReturn(iSection < cSections, VERR_INTERNAL_ERROR_3);
            const char * const pszName = ++psz;
            pThis->paSections[iSection].offName = (uint32_t)(psz - pThis->pszFile);

            /* Figure the name length. We're very very relaxed about terminating bracket. */
            while ((ch = *psz) != '\0' && ch != ']' && ch != '\r' && ch != '\n')
                psz++;
            pThis->paSections[iSection].cchName = (uint32_t)(psz - pszName);

            /* Set skip count to the start of the next line. */
            while (ch != '\0' && ch != '\n')
                ch = *++psz;
            pThis->paSections[iSection].cchSkipToValues = (uint32_t)(psz - pszName + 1);

            if (ch == '\n')
                psz++;
            else
                break;
        }
        else
        {
            psz = strchr(psz, '\n');
            if (psz)
                psz++;
            else
                break;
        }
    }

    /* Complete the final section. */
    pThis->paSections[iSection].cchSection = pThis->cbFile - pThis->paSections[iSection].offName;
    pThis->cSections = iSection + 1;

    return VINF_SUCCESS;
}


RTDECL(int) RTIniFileCreateFromVfsFile(PRTINIFILE phIniFile, RTVFSFILE hVfsFile, uint32_t fFlags)
{
    /*
     * Validate input, retaining a reference to the file.
     */
    AssertPtrReturn(phIniFile, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTINIFILE_F_VALID_MASK), VERR_INVALID_FLAGS);

    uint32_t cRefs = RTVfsFileRetain(hVfsFile);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create an instance.
     */
    PRTINIFILEINT pThis = (PRTINIFILEINT)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        pThis->u32Magic = RTINIFILE_MAGIC;
        pThis->cRefs    = 1;
        pThis->hVfsFile = hVfsFile;
        pThis->fFlags   = fFlags;

        int rc = rtIniFileLoad(pThis);
        if (RT_SUCCESS(rc))
        {

            *phIniFile = pThis;
            return VINF_SUCCESS;
        }
        RTIniFileRelease(pThis);
        return rc;
    }
    RTVfsFileRelease(hVfsFile);
    return VERR_NO_MEMORY;
}


RTDECL(uint32_t) RTIniFileRetain(RTINIFILE hIniFile)
{
    PRTINIFILEINT pThis = hIniFile;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTINIFILE_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs > 1);
    Assert(cRefs < _64K);
    return cRefs;
}


RTDECL(uint32_t) RTIniFileRelease(RTINIFILE hIniFile)
{
    if (hIniFile == NIL_RTINIFILE)
        return 0;
    PRTINIFILEINT pThis = hIniFile;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTINIFILE_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < _64K);
    if (cRefs == 0)
    {
        AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTINIFILE_MAGIC_DEAD, RTINIFILE_MAGIC), UINT32_MAX);
        RTMemFree(pThis->paSections);
        pThis->paSections = NULL;
        RTMemFree(pThis->pszFile);
        pThis->pszFile    = NULL;
        RTVfsFileRelease(pThis->hVfsFile);
        pThis->hVfsFile   = NIL_RTVFSFILE;
        RTMemFree(pThis);
    }
    return cRefs;
}


/**
 * Worker for RTIniFileQueryValue.
 */
static int rtIniFileQueryValueInSection(PRTINIFILEINT pThis, PRTINIFILESECTION pSection, const char *pszKey, size_t cchKey,
                                        char *pszValue, size_t cbValue, size_t *pcbActual)
{
    /*
     * Scan the section, looking for the matching key.
     */
    Assert(pSection->cchSkipToValues <= pSection->cchSection);
    const char * const  pszEnd  = &pThis->pszFile[pSection->offName + pSection->cchSection];
    const char *        pszNext = pszEnd;
    for (const char    *psz     = &pThis->pszFile[pSection->offName + pSection->cchSkipToValues];
         (uintptr_t)psz < (uintptr_t)pszEnd;
         psz = pszNext)
    {
        /* Find start of next line so we can use 'continue' to skip a line. */
        pszNext = strchr(psz, '\n');
        if (pszNext)
            pszNext++;
        else
            pszNext = pszEnd;

        /* Skip leading spaces. */
        char ch;
        while ((ch = *psz) != '\0' && RT_C_IS_SPACE(ch))
            psz++;
        if (   ch != ';'  /* comment line */
            && ch != '\n' /* empty line */
            && ch != '\r' /* empty line */
            && (uintptr_t)psz < (uintptr_t)pszEnd)
        {
            /* Find end of key name, if any. */
            const char *pszCurKey = psz;
            size_t      cchCurKey;
            const char *pszEqual;
            if (ch != '=')
            {
                /** @todo deal with escaped equal signs? */
                pszEqual = strchr(psz, '=');
                if (pszEqual)
                {
                    if ((uintptr_t)pszEqual < (uintptr_t)pszNext)
                        cchCurKey = pszEqual - pszCurKey;
                    else
                        continue;
                }
                else
                    break;

                /* Strip trailing spaces from the current key name. */
                while (cchCurKey > 0 && RT_C_IS_SPACE(pszCurKey[cchCurKey - 1]))
                    cchCurKey--;
            }
            else
            {
                cchCurKey = 0;
                pszEqual  = psz;
            }

            /* Match the keys. */
            /** @todo escape sequences? */
            if (   cchCurKey == cchKey
                && RTStrNICmp(pszCurKey, pszKey, cchKey) == 0)
            {
                /*
                 * Copy out the return value, without quotes.
                 */

                /* Skip leading blanks. */
                psz = pszEqual + 1;
                while ((ch = *psz) && RT_C_IS_SPACE(ch) && ch != '\n')
                    psz++;

                /* Strip trailing spaces. */
                size_t cchCurValue = pszNext - psz;
                while (cchCurValue > 1 && RT_C_IS_SPACE(psz[cchCurValue - 1]))
                    cchCurValue--;

                /* Strip quotes. */
                if (   cchCurValue > 2
                    && (   (ch = *psz) == '"'
                        || ch          == '\'' )
                    && psz[cchCurValue - 1] == ch)
                {
                    cchCurValue -= 2;
                    psz++;
                }

                /* Do the copying. */
                if (cchCurValue < cbValue)
                {
                    memcpy(pszValue, psz, cchCurValue);
                    pszValue[cchCurValue] = '\0';
                    if (pcbActual)
                        *pcbActual = cchCurValue;
                    return VINF_SUCCESS;
                }

                if (cbValue > 0)
                {
                    memcpy(pszValue, psz, cbValue - 1);
                    pszValue[cbValue - 1] = '\0';
                }
                if (pcbActual)
                    *pcbActual = cchCurValue + 1;
                return VERR_BUFFER_OVERFLOW;
            }
        }
    }
    return VERR_NOT_FOUND;
}


RTDECL(int) RTIniFileQueryValue(RTINIFILE hIniFile, const char *pszSection, const char *pszKey,
                                char *pszValue, size_t cbValue, size_t *pcbActual)
{
    /*
     * Validate input.
     */
    PRTINIFILEINT pThis = hIniFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTINIFILE_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pszSection, VERR_INVALID_POINTER);
    AssertPtrReturn(pszKey, VERR_INVALID_POINTER);
    size_t const cchKey = strlen(pszKey);
    if (cbValue)
        AssertPtrReturn(pszValue, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pcbActual, VERR_INVALID_POINTER);

    /*
     * Search relevant sections.
     */
    int rc;
    if (pszSection == NULL)
        rc = rtIniFileQueryValueInSection(pThis, &pThis->paSections[0], pszKey, cchKey, pszValue, cbValue, pcbActual);
    else
    {
        rc = VERR_NOT_FOUND;
        uint32_t const cchSection = (uint32_t)strlen(pszSection);
        for (uint32_t iSection = 1; iSection < pThis->cSections; iSection++)
            if (   pThis->paSections[iSection].cchName == cchSection
                && RTStrNICmp(&pThis->pszFile[pThis->paSections[iSection].offName], pszSection, cchSection) == 0)
            {
                rc = rtIniFileQueryValueInSection(pThis, &pThis->paSections[iSection], pszKey, cchKey,
                                                  pszValue, cbValue, pcbActual);
                if (rc != VERR_NOT_FOUND)
                    break;
            }
    }
    return rc;
}


/**
 * Worker for RTIniFileQueryPair.
 *
 * This can also be used to count the number of pairs in a section.
 */
static int rtIniFileQueryPairInSection(PRTINIFILEINT pThis, PRTINIFILESECTION pSection, uint32_t *pidxPair,
                                       char *pszKey, size_t cbKey, size_t *pcbKeyActual,
                                       char *pszValue, size_t cbValue, size_t *pcbValueActual)
{
    uint32_t idxPair = *pidxPair;

    /*
     * Scan the section, looking for the matching key.
     */
    Assert(pSection->cchSkipToValues <= pSection->cchSection);
    const char * const  pszEnd  = &pThis->pszFile[pSection->offName + pSection->cchSection];
    const char *        pszNext = pszEnd;
    for (const char    *psz     = &pThis->pszFile[pSection->offName + pSection->cchSkipToValues];
         (uintptr_t)psz < (uintptr_t)pszEnd;
         psz = pszNext)
    {
        /* Find start of next line so we can use 'continue' to skip a line. */
        pszNext = strchr(psz, '\n');
        if (pszNext)
            pszNext++;
        else
            pszNext = pszEnd;

        /* Skip leading spaces. */
        char ch;
        while ((ch = *psz) != '\0' && RT_C_IS_SPACE(ch))
            psz++;
        if (   ch != ';'  /* comment line */
            && ch != '\n' /* empty line */
            && ch != '\r' /* empty line */
            && (uintptr_t)psz < (uintptr_t)pszEnd)
        {
            /* Find end of key name, if any. */
            const char *pszCurKey = psz;
            size_t      cchCurKey;
            const char *pszEqual;
            if (ch != '=')
            {
                /** @todo deal with escaped equal signs? */
                pszEqual = strchr(psz, '=');
                if (pszEqual)
                {
                    if ((uintptr_t)pszEqual < (uintptr_t)pszNext)
                        cchCurKey = pszEqual - pszCurKey;
                    else
                        continue;
                }
                else
                    break;
            }
            else
            {
                cchCurKey = 0;
                pszEqual  = psz;
            }

            /* Is this the pair we're looking for? */
            if (idxPair > 0)
                idxPair--;
            else
            {
                /*
                 * Yes it's the stuff we're looking for.
                 * Prepare the the return stuff.
                 */

                /* Strip trailing spaces from the key name. */
                while (cchCurKey > 0 && RT_C_IS_SPACE(pszCurKey[cchCurKey - 1]))
                    cchCurKey--;

                /* Skip leading blanks from the value. */
                psz = pszEqual + 1;
                while ((ch = *psz) && RT_C_IS_SPACE(ch) && ch != '\n')
                    psz++;

                /* Strip trailing spaces from the value. */
                size_t cchCurValue = pszNext - psz;
                while (cchCurValue > 1 && RT_C_IS_SPACE(psz[cchCurValue - 1]))
                    cchCurValue--;

                /* Strip value quotes. */
                if (   cchCurValue > 2
                    && (   (ch = *psz) == '"'
                        || ch          == '\'' )
                    && psz[cchCurValue - 1] == ch)
                {
                    cchCurValue -= 2;
                    psz++;
                }

                /*
                 * Copy the stuff out.
                 */
                if (   cchCurValue < cbValue
                    && cchCurKey   < cbKey)
                {
                    memcpy(pszKey, pszCurKey, cchCurKey);
                    pszKey[cchCurKey] = '\0';
                    if (pcbKeyActual)
                        *pcbKeyActual = cchCurKey;

                    memcpy(pszValue, psz, cchCurValue);
                    pszValue[cchCurValue] = '\0';
                    if (pcbValueActual)
                        *pcbValueActual = cchCurValue;

                    *pidxPair = 0;
                    return VINF_SUCCESS;
                }

                /* Buffer overflow. Copy out what we can. */
                if (cbKey > 0)
                {
                    if (cchCurKey < cbKey)
                        cbKey = cchCurKey + 1;
                    memcpy(pszKey, pszCurKey, cbKey - 1);
                    pszKey[cbKey - 1] = '\0';
                }
                if (pcbKeyActual)
                    *pcbKeyActual = cchCurKey + 1;

                if (cbValue > 0)
                {
                    if (cchCurValue < cbValue)
                        cbValue = cchCurValue + 1;
                    memcpy(pszValue, psz, cbValue - 1);
                    pszValue[cbValue - 1] = '\0';
                }
                if (pcbValueActual)
                    *pcbValueActual = cchCurValue + 1;

                *pidxPair = 0;
                return VERR_BUFFER_OVERFLOW;
            }
        }
    }
    *pidxPair = idxPair;
    return VERR_NOT_FOUND;
}


RTDECL(int) RTIniFileQueryPair(RTINIFILE hIniFile, const char *pszSection, uint32_t idxPair,
                               char *pszKey, size_t cbKey, size_t *pcbKeyActual,
                               char *pszValue, size_t cbValue, size_t *pcbValueActual)
{
    /*
     * Validate input.
     */
    PRTINIFILEINT pThis = hIniFile;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTINIFILE_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pszSection, VERR_INVALID_POINTER);
    if (cbKey)
        AssertPtrReturn(pszKey, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pcbKeyActual, VERR_INVALID_POINTER);
    if (cbValue)
        AssertPtrReturn(pszValue, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pcbValueActual, VERR_INVALID_POINTER);

    /*
     * Search relevant sections.
     */
    int rc;
    if (pszSection == NULL)
        rc = rtIniFileQueryPairInSection(pThis, &pThis->paSections[0], &idxPair,
                                         pszKey, cbKey, pcbKeyActual, pszValue, cbValue, pcbValueActual);
    else
    {
        rc = VERR_NOT_FOUND;
        uint32_t const cchSection = (uint32_t)strlen(pszSection);
        for (uint32_t iSection = 1; iSection < pThis->cSections; iSection++)
            if (   pThis->paSections[iSection].cchName == cchSection
                && RTStrNICmp(&pThis->pszFile[pThis->paSections[iSection].offName], pszSection, cchSection) == 0)
            {
                rc = rtIniFileQueryPairInSection(pThis, &pThis->paSections[iSection], &idxPair,
                                                 pszKey, cbKey, pcbKeyActual, pszValue, cbValue, pcbValueActual);
                if (rc != VERR_NOT_FOUND)
                    break;
            }
    }
    return rc;
}

