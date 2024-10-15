/* $Id: bldprog-strtab-template.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Build Program - String Table Generator.
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


/*
 * (Avoid include sanity checks as this is ring-3 only, C++ code.)
 */
#if defined(__cplusplus) && defined(IN_RING3)


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def BLDPROG_STRTAB_MAX_STRLEN
 * The max length of strings in the table. */
#if !defined(BLDPROG_STRTAB_MAX_STRLEN) || defined(DOXYGEN_RUNNING)
# define BLDPROG_STRTAB_MAX_STRLEN 256
#endif

/** @def BLDPROG_STRTAB_WITH_COMPRESSION
 * Enables very simple string compression.
 */
#if defined(DOXYGEN_RUNNING)
# define BLDPROG_STRTAB_WITH_COMPRESSION
#endif

/** @def BLDPROG_STRTAB_WITH_CAMEL_WORDS
 * Modifies the string compression to look for camel case words.
 */
#if defined(DOXYGEN_RUNNING)
# define BLDPROG_STRTAB_WITH_CAMEL_WORDS
#endif

/** @def BLDPROG_STRTAB_PURE_ASCII
 * String compression assumes pure 7-bit ASCII and will fail on UTF-8 when this
 * is defined.  Otherwise, the compression code will require IPRT to link.
 */
#if defined(DOXYGEN_RUNNING)
# define BLDPROG_STRTAB_PURE_ASCII
#endif



/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/stdarg.h>
#include <iprt/ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
# include <map>
# include <iprt/sanitized/string>

# define BLDPROG_STRTAB_WITH_WORD_SEP_ALTERNATIVE
typedef struct BLDPROGWORDFREQSTATS
{
    uint32_t cWithoutSep;   /**< Number of occurances without a separator. */
# ifdef BLDPROG_STRTAB_WITH_WORD_SEP_ALTERNATIVE
    uint32_t cWithSep;      /**< Number of occurance with a separator. */
    char     chSep;         /**< The separator.  First come basis. */
# endif
} BLDPROGWORDFREQSTATS;

typedef std::map<std::string, BLDPROGWORDFREQSTATS> BLDPROGWORDFREQMAP;

#endif

#include "../../src/VBox/Runtime/include/internal/strhash.h" /** @todo make this one public */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Build table string.
 */
typedef struct BLDPROGSTRING
{
    /** The string.
     * @note This may be modified or replaced (allocated from heap) when
     *       compressing the string table. */
    char                   *pszString;
    /** The string hash value. */
    uint32_t                uHash;
    /** The strint table offset.   */
    uint32_t                offStrTab;
    /** The string length. */
    size_t                  cchString;
    /** Pointer to the next string reference (same string table entry). */
    struct BLDPROGSTRING   *pNextRef;
    /** Pointer to the next string with the same hash value (collision). */
    struct BLDPROGSTRING   *pNextCollision;

} BLDPROGSTRING;
/** Pointer to a string table string. */
typedef BLDPROGSTRING *PBLDPROGSTRING;


/** String table data. */
typedef struct BLDPROGSTRTAB
{
    /** The size of g_papStrHash. */
    size_t              cStrHash;
    /** String hash table. */
    PBLDPROGSTRING     *papStrHash;
    /** Duplicate strings found by AddString. */
    size_t              cDuplicateStrings;
    /** Total length of the unique strings (no terminators). */
    size_t              cchUniqueStrings;
    /** Number of unique strings after AddString. */
    size_t              cUniqueStrings;
    /** Number of collisions. */
    size_t              cCollisions;

    /** Number of entries in apSortedStrings. */
    size_t              cSortedStrings;
    /** The sorted string table. */
    PBLDPROGSTRING     *papSortedStrings;

#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
    /** The 256 words we've picked to be indexed by reference. */
    BLDPROGSTRING       aCompDict[256];
    /** The frequency of the 256 dictionary entries.  */
    size_t              auCompDictFreq[256];
    /** Incoming strings pending compression. */
    PBLDPROGSTRING     *papPendingStrings;
    /** Current number of entries in papStrPending. */
    size_t              cPendingStrings;
    /** The allocated size of papPendingStrings. */
    size_t              cMaxPendingStrings;
    /** Work frequency map.
     * @todo rewrite in plain C.  */
    BLDPROGWORDFREQMAP  Frequencies;
    /** Map of characters used by input strings. */
    uint64_t            bmUsedChars[256/64];
#endif

    /** The string table. */
    char               *pachStrTab;
    /** The actual string table size. */
    size_t              cchStrTab;
} BLDPROGSTRTAB;
typedef BLDPROGSTRTAB *PBLDPROGSTRTAB;

#if RT_CLANG_PREREQ(4, 0) || RT_GNUC_PREREQ(4, 6)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif


#ifdef BLDPROG_STRTAB_WITH_COMPRESSION

/**
 * Same as ASMBitTest.
 *
 * We cannot safely use ASMBitTest here because it must be inline, as this code
 * is used to build RuntimeBldProg. */
DECLINLINE(bool) BldProgBitIsSet(uint64_t const *pbmBitmap, size_t iBit)
{
    return RT_BOOL(pbmBitmap[iBit / 64] & RT_BIT_64(iBit % 64));
}


/**
 * Same as ASMBitSet.
 *
 * We cannot safely use ASMBitSet here because it must be inline, as this code
 * is used to build RuntimeBldProg.
 */
DECLINLINE(void) BldProgBitSet(uint64_t *pbmBitmap, size_t iBit)
{
    pbmBitmap[iBit / 64] |= RT_BIT_64(iBit % 64);
}

#endif


/**
 * Initializes the strint table compiler.
 *
 * @returns success indicator (out of memory if false).
 * @param   pThis           The strint table compiler instance.
 * @param   cMaxStrings     The max number of strings we'll be adding.
 */
static bool BldProgStrTab_Init(PBLDPROGSTRTAB pThis, size_t cMaxStrings)
{
    pThis->cStrHash = 0;
    pThis->papStrHash = NULL;
    pThis->cDuplicateStrings = 0;
    pThis->cchUniqueStrings = 0;
    pThis->cUniqueStrings = 0;
    pThis->cCollisions = 0;
    pThis->cSortedStrings = 0;
    pThis->papSortedStrings = NULL;
    pThis->pachStrTab = NULL;
    pThis->cchStrTab = 0;
#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
    memset(pThis->aCompDict, 0, sizeof(pThis->aCompDict));
    pThis->papPendingStrings = NULL;
    pThis->cPendingStrings = 0;
    pThis->cMaxPendingStrings = cMaxStrings;
    memset(pThis->bmUsedChars, 0, sizeof(pThis->bmUsedChars));
    BldProgBitSet(pThis->bmUsedChars, 0);    /* Some parts of the code still thinks zero is a terminator, so don't use it for now. */
# ifndef BLDPROG_STRTAB_PURE_ASCII
    BldProgBitSet(pThis->bmUsedChars, 0xff); /* Reserve escape byte for codepoints above 127. */
# endif
#endif

    /*
     * Allocate a hash table double the size of all strings (to avoid too
     * many collisions).  Add all strings to it, finding duplicates in the
     * process.
     */
#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
    cMaxStrings += RT_ELEMENTS(pThis->aCompDict);
#endif
    cMaxStrings *= 2;
    pThis->papStrHash = (PBLDPROGSTRING *)calloc(cMaxStrings, sizeof(pThis->papStrHash[0]));
    if (pThis->papStrHash)
    {
        pThis->cStrHash = cMaxStrings;
#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
        pThis->papPendingStrings = (PBLDPROGSTRING *)calloc(pThis->cMaxPendingStrings, sizeof(pThis->papPendingStrings[0]));
        if (pThis->papPendingStrings)
#endif
            return true;

#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
        free(pThis->papStrHash);
        pThis->papStrHash = NULL;
#endif
    }
    return false;
}


#if 0 /* unused */
static void BldProgStrTab_Delete(PBLDPROGSTRTAB pThis)
{
    free(pThis->papStrHash);
    free(pThis->papSortedStrings);
    free(pThis->pachStrTab);
# ifdef BLDPROG_STRTAB_WITH_COMPRESSION
    free(pThis->papPendingStrings);
# endif
    memset(pThis, 0, sizeof(*pThis));
}
#endif

#ifdef BLDPROG_STRTAB_WITH_COMPRESSION

DECLINLINE(size_t) bldProgStrTab_compressorFindNextWord(const char *pszSrc, char ch, const char **ppszSrc)
{
    /*
     * Skip leading word separators.
     */
# ifdef BLDPROG_STRTAB_WITH_CAMEL_WORDS
    while (   ch == ' '
           || ch == '-'
           || ch == '+'
           || ch == '_')
# else
    while (ch == ' ')
# endif
        ch = *++pszSrc;
    if (ch)
    {
        /*
         * Find end of word.
         */
        size_t cchWord = 1;
# ifdef BLDPROG_STRTAB_WITH_CAMEL_WORDS
        char chPrev = ch;
        while (   (ch = pszSrc[cchWord]) != ' '
               && ch != '\0'
               && ch != '-'
               && ch != '+'
               && ch != '_'
               && (   ch == chPrev
                   || !RT_C_IS_UPPER(ch)
                   || RT_C_IS_UPPER(chPrev)) )
        {
            chPrev = ch;
            cchWord++;
        }
# else
        while ((ch = pszSrc[cchWord]) != ' ' && ch != '\0')
            cchWord++;
# endif
        *ppszSrc = pszSrc;
        return cchWord;
    }

    *ppszSrc = pszSrc;
    return 0;
}


/**
 * Analyzes a string.
 *
 * @param   pThis   The strint table compiler instance.
 * @param   pStr    The string to analyze.
 */
static void bldProgStrTab_compressorAnalyzeString(PBLDPROGSTRTAB pThis, PBLDPROGSTRING pStr)
{
    /*
     * Mark all the string characters as used.
     */
    const char *psz = pStr->pszString;
    char ch;
    while ((ch = *psz++) != '\0')
        BldProgBitSet(pThis->bmUsedChars, (uint8_t)ch);

    /*
     * For now we just consider words.
     */
    psz = pStr->pszString;
    while ((ch = *psz) != '\0')
    {
        size_t cchWord = bldProgStrTab_compressorFindNextWord(psz, ch, &psz);
        if (cchWord > 1)
        {
            std::string strWord(psz, cchWord);
            BLDPROGWORDFREQMAP::iterator it = pThis->Frequencies.find(strWord);
            if (it != pThis->Frequencies.end())
            {
# ifdef BLDPROG_STRTAB_WITH_WORD_SEP_ALTERNATIVE
                char const chSep = psz[cchWord];
                if (chSep != '\0' && (it->second.chSep == chSep || it->second.chSep == '\0'))
                {
                    it->second.chSep = chSep;
                    it->second.cWithSep++;
                }
                else
# endif
                    it->second.cWithoutSep++;
            }
            else
            {
# ifdef BLDPROG_STRTAB_WITH_WORD_SEP_ALTERNATIVE
                char const chSep = psz[cchWord];
                if (chSep != '\0')
                {
                    BLDPROGWORDFREQSTATS const NewWord = { 0, 0, chSep };
                    pThis->Frequencies[strWord] = NewWord;
                }
                else
                {
                    static BLDPROGWORDFREQSTATS const s_NewWord = { 0, 0, 0 };
                    pThis->Frequencies[strWord] = s_NewWord;
                }
# else
                pThis->Frequencies[strWord].cWithoutSep = 0;
# endif
            }

# if 0 /** @todo need better accounting for overlapping alternatives before this can be enabled. */
            /* Two words - immediate yields calc may lie when this enabled and we may pick the wrong words. */
            if (ch == ' ')
            {
                ch = psz[++cchWord];
                if (ch != ' ' && ch != '\0')
                {
                    size_t const cchSaved = cchWord;

                    do
                        cchWord++;
                    while ((ch = psz[cchWord]) != ' ' && ch != '\0');

                    strWord = std::string(psz, cchWord);
                    BLDPROGWORDFREQMAP::iterator it = pThis->Frequencies.find(strWord);
                    if (it != pThis->Frequencies.end())
                        it->second += cchWord - 1;
                    else
                        pThis->Frequencies[strWord] = 0;

                    cchWord = cchSaved;
                }
            }
# endif
        }
        else if (!cchWord)
            break;

        /* Advance. */
        psz += cchWord;
    }
    pStr->cchString = psz - pStr->pszString;
    if (pStr->cchString > BLDPROG_STRTAB_MAX_STRLEN)
    {
        fprintf(stderr,  "error: String to long (%u)\n", (unsigned)pStr->cchString);
        abort();
    }
}

#endif /* BLDPROG_STRTAB_WITH_COMPRESSION */

/**
 * Adds a string to the hash table.
 * @param   pThis   The strint table compiler instance.
 * @param   pStr    The string.
 */
static void bldProgStrTab_AddStringToHashTab(PBLDPROGSTRTAB pThis, PBLDPROGSTRING pStr)
{
    pStr->pNextRef       = NULL;
    pStr->pNextCollision = NULL;
    pStr->offStrTab      = 0;
    pStr->uHash          = sdbm(pStr->pszString, &pStr->cchString);
    if (pStr->cchString > BLDPROG_STRTAB_MAX_STRLEN)
    {
        fprintf(stderr,  "error: String to long (%u)\n", (unsigned)pStr->cchString);
        exit(RTEXITCODE_FAILURE);
    }

    size_t idxHash = pStr->uHash % pThis->cStrHash;
    PBLDPROGSTRING pCur = pThis->papStrHash[idxHash];
    if (!pCur)
        pThis->papStrHash[idxHash] = pStr;
    else
    {
        /* Look for matching string. */
        do
        {
            if (   pCur->uHash      == pStr->uHash
                && pCur->cchString  == pStr->cchString
                && memcmp(pCur->pszString, pStr->pszString, pStr->cchString) == 0)
            {
                pStr->pNextRef = pCur->pNextRef;
                pCur->pNextRef = pStr;
                pThis->cDuplicateStrings++;
                return;
            }
            pCur = pCur->pNextCollision;
        } while (pCur != NULL);

        /* No matching string, insert. */
        pThis->cCollisions++;
        pStr->pNextCollision = pThis->papStrHash[idxHash];
        pThis->papStrHash[idxHash] = pStr;
    }

    pThis->cUniqueStrings++;
    pThis->cchUniqueStrings += pStr->cchString;
}


/**
 * Adds a string to the string table.
 *
 * @param   pThis   The strint table compiler instance.
 * @param   pStr    The string.
 */
static void BldProgStrTab_AddString(PBLDPROGSTRTAB pThis, PBLDPROGSTRING pStr)
{
#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
    bldProgStrTab_compressorAnalyzeString(pThis, pStr);
    if (pThis->cPendingStrings < pThis->cMaxPendingStrings)
        pThis->papPendingStrings[pThis->cPendingStrings++] = pStr;
    else
        abort();
#else
    bldProgStrTab_AddStringToHashTab(pThis, pStr);
#endif
}


/**
 * Adds a string to the string table.
 *
 * @param   pThis   The strint table compiler instance.
 * @param   pStr    The string entry (uninitialized).
 * @param   psz     The string, will be duplicated if compression is enabled.
 */
DECLINLINE(void) BldProgStrTab_AddStringDup(PBLDPROGSTRTAB pThis, PBLDPROGSTRING pStr, const char *psz)
{
#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
    pStr->pszString = strdup(psz);
    if (!pStr->pszString)
        abort();
#else
    pStr->pszString = (char *)psz;
#endif
    BldProgStrTab_AddString(pThis, pStr);
}

#ifdef BLDPROG_STRTAB_WITH_COMPRESSION

/**
 * Copies @a cchSrc chars from @a pchSrc to @a pszDst, escaping special
 * sequences.
 *
 * @returns New @a pszDst position, NULL if invalid source encoding.
 * @param   pszDst              The destination buffer.
 * @param   pszSrc              The source buffer.
 * @param   cchSrc              How much to copy.
 */
static char *bldProgStrTab_compressorCopyAndEscape(char *pszDst, const char *pszSrc, size_t cchSrc)
{
    while (cchSrc-- > 0)
    {
        char ch = *pszSrc;
        if (!((unsigned char)ch & 0x80))
        {
            *pszDst++ = ch;
            pszSrc++;
        }
        else
        {
# ifdef BLDPROG_STRTAB_PURE_ASCII
            fprintf(stderr, "error: unexpected char value %#x\n", ch);
            return NULL;
# else
            RTUNICP uc;
            int rc = RTStrGetCpEx(&pszSrc, &uc);
            if (RT_SUCCESS(rc))
            {
                *pszDst++ = (unsigned char)0xff; /* escape single code point. */
                pszDst = RTStrPutCp(pszDst, uc);
            }
            else
            {
                fprintf(stderr, "Error: RTStrGetCpEx failed with rc=%d\n", rc);
                return NULL;
            }
# endif
        }
    }
    return pszDst;
}


/**
 * Replaces the dictionary words and escapes non-ascii chars in a string.
 *
 * @param   pThis       The strint table compiler instance.
 * @param   pString     The string to fixup.
 */
static bool bldProgStrTab_compressorFixupString(PBLDPROGSTRTAB pThis, PBLDPROGSTRING pStr)
{
    char        szNew[BLDPROG_STRTAB_MAX_STRLEN * 2];
    char       *pszDst    = szNew;
    const char *pszSrc    = pStr->pszString;
    const char *pszSrcEnd = pszSrc + pStr->cchString;

    char ch;
    while ((ch = *pszSrc) != '\0')
    {
        const char * const pszSrcUncompressed = pszSrc;
        size_t cchWord = bldProgStrTab_compressorFindNextWord(pszSrc, ch, &pszSrc);
        size_t cchSrcUncompressed = pszSrc - pszSrcUncompressed;
        if (cchSrcUncompressed > 0)
        {
            pszDst = bldProgStrTab_compressorCopyAndEscape(pszDst, pszSrcUncompressed, cchSrcUncompressed);
            if (!pszDst)
                return false;
        }
        if (!cchWord)
            break;

        /* Check for g_aWord matches. */
        if (cchWord > 1)
        {
            size_t cchMax = pszSrcEnd - pszSrc;
            for (unsigned i = 0; i < RT_ELEMENTS(pThis->aCompDict); i++)
            {
                size_t cchLen = pThis->aCompDict[i].cchString;
                if (   cchLen >= cchWord
                    && cchLen <= cchMax
                    && memcmp(pThis->aCompDict[i].pszString, pszSrc, cchLen) == 0)
                {
                    *pszDst++ = (unsigned char)i;
                    pszSrc += cchLen;
                    cchWord = 0;
                    break;
                }
            }
        }

        if (cchWord > 0)
        {
            /* Copy the current word. */
            pszDst = bldProgStrTab_compressorCopyAndEscape(pszDst, pszSrc, cchWord);
            if (!pszDst)
                return false;
            pszSrc += cchWord;
        }
    }

    /* Just terminate it now. */
    *pszDst = '\0';

    /*
     * Update the string.
     */
    size_t cchNew = pszDst - &szNew[0];
    if (cchNew > pStr->cchString)
    {
        pStr->pszString = (char *)malloc(cchNew + 1);
        if (!pStr->pszString)
        {
            fprintf(stderr, "Out of memory!\n");
            return false;
        }
    }
    memcpy(pStr->pszString, szNew, cchNew + 1);
    pStr->cchString = cchNew;

    return true;
}


/**
 * Entry in SortedDictionary.
 *
 * Uses variable length string member, so not class and allocated via malloc.
 */
struct SortedDictionaryEntry
{
    size_t      m_cchGain;
    size_t      m_cchString;
    RT_FLEXIBLE_ARRAY_EXTENSION
    char        m_szString[RT_FLEXIBLE_ARRAY];

    /** Allocates and initializes a new entry. */
    static SortedDictionaryEntry *allocate(const char *a_pch, size_t a_cch, size_t a_cchGain, char a_chSep)
    {
        size_t cbString = a_cch + !!a_chSep + 1;
        SortedDictionaryEntry *pNew = (SortedDictionaryEntry *)malloc(RT_UOFFSETOF(SortedDictionaryEntry, m_szString) + cbString);
        if (pNew)
        {
            pNew->m_cchGain   = a_cchGain;
            memcpy(pNew->m_szString, a_pch, a_cch);
            if (a_chSep)
                pNew->m_szString[a_cch++] = a_chSep;
            pNew->m_szString[a_cch] = '\0';
            pNew->m_cchString = a_cch;
        }
        return pNew;
    }


    /** Compares this dictionary entry with an incoming one.
     * @retval -1 if this entry is of less worth than the new one.
     * @retval  0 if this entry is of equal worth to the new one.
     * @retval +1 if this entry is of more worth than the new one.
     */
    int compare(size_t a_cchGain, size_t a_cchString)
    {
        /* Higher gain is preferred of course: */
        if (m_cchGain < a_cchGain)
            return -1;
        if (m_cchGain > a_cchGain)
            return 1;

        /* Gain is the same. Prefer the shorter string, as it will result in a shorter string table: */
        if (m_cchString > a_cchString)
            return -1;
        if (m_cchString < a_cchString)
            return 1;
        return 0;
    }
};


/**
 * Insertion sort dictionary that keeps the 256 best words.
 *
 * Used by bldProgStrTab_compressorDoStringCompression to pick the dictionary
 * words.
 */
class SortedDictionary
{
public:
    size_t                  m_cEntries;
    SortedDictionaryEntry  *m_apEntries[256];

    SortedDictionary()
        : m_cEntries(0)
    {
        for (size_t i = 0; i < RT_ELEMENTS(m_apEntries); i++)
            m_apEntries[i] = NULL;
    }

    ~SortedDictionary()
    {
        while (m_cEntries > 0)
        {
            free(m_apEntries[--m_cEntries]);
            m_apEntries[m_cEntries] = NULL;
        }
    }


    /**
     * Inserts a new entry, if it's worth it.
     * @returns true on succes, false if out of memory.
     */
    bool insert(const char *a_pchString, size_t a_cchStringBase, size_t a_cchGain, char a_chSep = 0)
    {
        size_t const cchString = a_cchStringBase + (a_chSep + 1);

        /*
         * Drop the insert if the symbol table is full and the insert is less worth the last entry:
         */
        if (   m_cEntries >= RT_ELEMENTS(m_apEntries)
            && m_apEntries[RT_ELEMENTS(m_apEntries) - 1]->compare(a_cchGain, cchString) >= 0)
            return true;

        /*
         * Create a new entry to insert.
         */
        SortedDictionaryEntry *pNewEntry = SortedDictionaryEntry::allocate(a_pchString, a_cchStringBase, a_cchGain, a_chSep);
        if (!pNewEntry)
            return false;

        /*
         * Find the insert point.
         */
        if (m_cEntries == 0)
        {
            m_apEntries[0] = pNewEntry;
            m_cEntries     = 1;
        }
        else
        {
            /* If table is full, drop the last entry before we start (already made
               sure the incoming entry is preferable to the one were dropping): */
            if (m_cEntries >= RT_ELEMENTS(m_apEntries))
            {
                free(m_apEntries[RT_ELEMENTS(m_apEntries) - 1]);
                m_apEntries[RT_ELEMENTS(m_apEntries) - 1] = NULL;
                m_cEntries = RT_ELEMENTS(m_apEntries) - 1;
            }

            /* Find where to insert the new entry: */
            /** @todo use binary search. */
            size_t i = m_cEntries;
            while (i > 0 && m_apEntries[i - 1]->compare(a_cchGain, cchString) < 0)
                i--;

            /* Shift entries to make room and insert the new entry. */
            if (i < m_cEntries)
                memmove(&m_apEntries[i + 1], &m_apEntries[i], (m_cEntries - i) * sizeof(m_apEntries[0]));
            m_apEntries[i] = pNewEntry;
            m_cEntries++;
        }
        return true;
    }
};


/**
 * Compresses the vendor and product strings.
 *
 * This is very very simple (a lot less work than the string table for
 * instance).
 */
static bool bldProgStrTab_compressorDoStringCompression(PBLDPROGSTRTAB pThis, bool fVerbose)
{
    /*
     * Sort the frequency analyzis result and pick the top entries for any
     * available dictionary slots.
     */
    SortedDictionary SortedDict;
    for (BLDPROGWORDFREQMAP::iterator it = pThis->Frequencies.begin(); it != pThis->Frequencies.end(); ++it)
    {
        bool fInsert;
        size_t const cchString      = it->first.length();
# ifndef BLDPROG_STRTAB_WITH_WORD_SEP_ALTERNATIVE
        size_t const cchGainWithout = it->second.cWithoutSep * cchString;
# else
        size_t const cchGainWithout = (it->second.cWithoutSep + it->second.cWithSep) * cchString;
        size_t const cchGainWith    = it->second.cWithSep * (cchString + 1);
        if (cchGainWith > cchGainWithout)
            fInsert = SortedDict.insert(it->first.c_str(), cchString, cchGainWith, it->second.chSep);
        else
# endif
            fInsert = SortedDict.insert(it->first.c_str(), cchString, cchGainWithout);
        if (!fInsert)
            return false;
    }

    size_t cb     = 0;
    size_t cWords = 0;
    size_t iDict  = 0;
# ifdef BLDPROG_STRTAB_PURE_ASCII
    for (size_t i = 0; i < RT_ELEMENTS(pThis->aCompDict); i++)
# else
    for (size_t i = 0; i < RT_ELEMENTS(pThis->aCompDict) - 1; i++)
# endif
    {
        char        szTmp[2] = { (char)i, '\0' };
        const char *psz      = szTmp;
        if (   BldProgBitIsSet(pThis->bmUsedChars, i)
            || iDict >= SortedDict.m_cEntries)
        {
            /* character entry  */
            pThis->auCompDictFreq[i]      = 0;
            pThis->aCompDict[i].cchString = 1;
        }
        else
        {
            /* word entry */
            cb += SortedDict.m_apEntries[iDict]->m_cchGain;
            pThis->auCompDictFreq[i]      = SortedDict.m_apEntries[iDict]->m_cchGain;
            pThis->aCompDict[i].cchString = SortedDict.m_apEntries[iDict]->m_cchString;
            psz = SortedDict.m_apEntries[iDict]->m_szString;
            cWords++;
            iDict++;
        }
        pThis->aCompDict[i].pszString = (char *)malloc(pThis->aCompDict[i].cchString + 1);
        if (pThis->aCompDict[i].pszString)
            memcpy(pThis->aCompDict[i].pszString, psz, pThis->aCompDict[i].cchString + 1);
        else
            return false;
    }

    if (fVerbose)
        printf("debug: Estimated string compression saving: %u bytes\n"
               "debug: %u words, %u characters\n"
               , (unsigned)cb, (unsigned)cWords, (unsigned)(RT_ELEMENTS(pThis->aCompDict) - cWords));

    /*
     * Rework the strings.
     */
    size_t cchOld = 0;
    size_t cchOldMax = 0;
    size_t cchOldMin = BLDPROG_STRTAB_MAX_STRLEN;
    size_t cchNew    = 0;
    size_t cchNewMax = 0;
    size_t cchNewMin = BLDPROG_STRTAB_MAX_STRLEN;
    size_t i         = pThis->cPendingStrings;
    while (i-- > 0)
    {
        PBLDPROGSTRING pCurStr = pThis->papPendingStrings[i];
        cchOld += pCurStr->cchString;
        if (pCurStr->cchString > cchOldMax)
            cchOldMax = pCurStr->cchString;
        if (pCurStr->cchString < cchOldMin)
            cchOldMin = pCurStr->cchString;

        if (!bldProgStrTab_compressorFixupString(pThis, pCurStr))
            return false;

        cchNew += pCurStr->cchString;
        if (pCurStr->cchString > cchNewMax)
            cchNewMax = pCurStr->cchString;
        if (pCurStr->cchString < cchNewMin)
            cchNewMin = pCurStr->cchString;

        bldProgStrTab_AddStringToHashTab(pThis, pCurStr);
    }

    /*
     * Do debug stats.
     */
    if (fVerbose)
    {
        for (i = 0; i < RT_ELEMENTS(pThis->aCompDict); i++)
            cchNew += pThis->aCompDict[i].cchString + 1;

        printf("debug: Strings: original: %u bytes;  compressed: %u bytes;", (unsigned)cchOld, (unsigned)cchNew);
        if (cchNew < cchOld)
            printf("  saving %u bytes (%u%%)\n", (unsigned)(cchOld - cchNew), (unsigned)((cchOld - cchNew) * 100 / cchOld));
        else
            printf("  wasting %u bytes!\n", (unsigned)(cchOld - cchNew));
        printf("debug: Original string lengths:   average %u; min %u; max %u\n",
               (unsigned)(cchOld / pThis->cPendingStrings), (unsigned)cchOldMin, (unsigned)cchOldMax);
        printf("debug: Compressed string lengths: average %u; min %u; max %u\n",
               (unsigned)(cchNew / pThis->cPendingStrings), (unsigned)cchNewMin, (unsigned)cchNewMax);
    }

    return true;
}

#endif /* BLDPROG_STRTAB_WITH_COMPRESSION */

/**
 * Inserts a string into g_apUniqueStrings.
 * @param   pThis   The strint table compiler instance.
 * @param   pStr    The string.
 */
static void bldProgStrTab_InsertUniqueString(PBLDPROGSTRTAB pThis, PBLDPROGSTRING pStr)
{
    size_t iIdx;
    size_t iEnd = pThis->cSortedStrings;
    if (iEnd)
    {
        size_t iStart = 0;
        for (;;)
        {
            iIdx = iStart + (iEnd - iStart) / 2;
            if (pThis->papSortedStrings[iIdx]->cchString < pStr->cchString)
            {
                if (iIdx <= iStart)
                    break;
                iEnd = iIdx;
            }
            else if (pThis->papSortedStrings[iIdx]->cchString > pStr->cchString)
            {
                if (++iIdx >= iEnd)
                    break;
                iStart = iIdx;
            }
            else
                break;
        }

        if (iIdx != pThis->cSortedStrings)
            memmove(&pThis->papSortedStrings[iIdx + 1], &pThis->papSortedStrings[iIdx],
                    (pThis->cSortedStrings - iIdx) * sizeof(pThis->papSortedStrings[iIdx]));
    }
    else
        iIdx = 0;

    pThis->papSortedStrings[iIdx] = pStr;
    pThis->cSortedStrings++;
}


/**
 * Compiles the string table after the string has been added.
 *
 * This will save space by dropping string terminators, eliminating duplicates
 * and try find strings that are sub-strings of others.
 *
 * Will initialize the StrRef of all StrTabString instances.
 *
 * @returns success indicator (error flagged).
 * @param   pThis   The strint table compiler instance.
 */
static bool BldProgStrTab_CompileIt(PBLDPROGSTRTAB pThis, bool fVerbose)
{
#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
    /*
     * Do the compression and add all the compressed strings to the table.
     */
    if (!bldProgStrTab_compressorDoStringCompression(pThis, fVerbose))
        return false;

    /*
     * Add the dictionary strings.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aCompDict); i++)
        if (pThis->aCompDict[i].cchString > 1)
            bldProgStrTab_AddStringToHashTab(pThis, &pThis->aCompDict[i]);
# ifdef RT_STRICT
        else if (   pThis->aCompDict[i].cchString != 1
#  ifndef BLDPROG_STRTAB_PURE_ASCII
                 && i != 0xff
#  endif
                )
            abort();
# endif
#endif
    if (fVerbose)
        printf("debug: %u unique strings (%u bytes), %u duplicates, %u collisions\n",
               (unsigned)pThis->cUniqueStrings, (unsigned)pThis->cchUniqueStrings,
               (unsigned)pThis->cDuplicateStrings, (unsigned)pThis->cCollisions);

    /*
     * Create papSortedStrings from the hash table.  The table is sorted by
     * string length, with the longer strings first, so that we increase our
     * chances of locating duplicate substrings.
     */
    pThis->papSortedStrings = (PBLDPROGSTRING *)malloc(sizeof(pThis->papSortedStrings[0]) * pThis->cUniqueStrings);
    if (!pThis->papSortedStrings)
        return false;
    pThis->cSortedStrings = 0;
    size_t idxHash = pThis->cStrHash;
    while (idxHash-- > 0)
    {
        PBLDPROGSTRING pCur = pThis->papStrHash[idxHash];
        if (pCur)
        {
            do
            {
                bldProgStrTab_InsertUniqueString(pThis, pCur);
                pCur = pCur->pNextCollision;
            } while (pCur);
        }
    }

    /*
     * Create the actual string table.
     */
    pThis->pachStrTab = (char *)malloc(pThis->cchUniqueStrings + 1);
    if (!pThis->pachStrTab)
        return false;
    pThis->cchStrTab  = 0;
    for (size_t i = 0; i < pThis->cSortedStrings; i++)
    {
        PBLDPROGSTRING      pCur      = pThis->papSortedStrings[i];
        const char * const  pszCur    = pCur->pszString;
        size_t       const  cchCur    = pCur->cchString;
        size_t              offStrTab = pThis->cchStrTab;

        /*
         * See if the string is a substring already found in the string table.
         * Excluding the zero terminator increases the chances for this.
         */
        size_t      cchLeft   = pThis->cchStrTab >= cchCur ? pThis->cchStrTab - cchCur : 0;
        const char *pchLeft   = pThis->pachStrTab;
        char const  chFirst   = *pszCur;
        while (cchLeft > 0)
        {
            const char *pchCandidate = (const char *)memchr(pchLeft, chFirst, cchLeft);
            if (!pchCandidate)
                break;
            if (memcmp(pchCandidate, pszCur,  cchCur) == 0)
            {
                offStrTab = pchCandidate - pThis->pachStrTab;
                break;
            }

            cchLeft -= pchCandidate + 1 - pchLeft;
            pchLeft  = pchCandidate + 1;
        }

        if (offStrTab == pThis->cchStrTab)
        {
            /*
             * See if the start of the string overlaps the end of the string table.
             */
            if (pThis->cchStrTab && cchCur > 1)
            {
                cchLeft = RT_MIN(pThis->cchStrTab, cchCur - 1);
                pchLeft = &pThis->pachStrTab[pThis->cchStrTab - cchLeft];
                while (cchLeft > 0)
                {
                    const char *pchCandidate = (const char *)memchr(pchLeft, chFirst, cchLeft);
                    if (!pchCandidate)
                        break;
                    cchLeft -= pchCandidate - pchLeft;
                    pchLeft  = pchCandidate;
                    if (memcmp(pchLeft, pszCur, cchLeft) == 0)
                    {
                        size_t cchToCopy = cchCur - cchLeft;
                        memcpy(&pThis->pachStrTab[offStrTab], &pszCur[cchLeft], cchToCopy);
                        pThis->cchStrTab += cchToCopy;
                        offStrTab = pchCandidate - pThis->pachStrTab;
                        break;
                    }
                    cchLeft--;
                    pchLeft++;
                }
            }

            /*
             * If we didn't have any luck above, just append the string.
             */
            if (offStrTab == pThis->cchStrTab)
            {
                memcpy(&pThis->pachStrTab[offStrTab], pszCur, cchCur);
                pThis->cchStrTab += cchCur;
            }
        }

        /*
         * Set the string table offset for all the references to this string.
         */
        do
        {
            pCur->offStrTab = (uint32_t)offStrTab;
            pCur = pCur->pNextRef;
        } while (pCur != NULL);
    }

    if (fVerbose)
        printf("debug: String table: %u bytes\n", (unsigned)pThis->cchStrTab);
    return true;
}


#ifdef RT_STRICT
/**
 * Sanity checks a string table string.
 * @param   pThis   The strint table compiler instance.
 * @param   pStr    The string to check.
 */
static void BldProgStrTab_CheckStrTabString(PBLDPROGSTRTAB pThis, PBLDPROGSTRING pStr)
{
    if (pStr->cchString != strlen(pStr->pszString))
        abort();
    if (pStr->offStrTab >= pThis->cchStrTab)
        abort();
    if (pStr->offStrTab + pStr->cchString > pThis->cchStrTab)
        abort();
    if (memcmp(pStr->pszString, &pThis->pachStrTab[pStr->offStrTab], pStr->cchString) != 0)
        abort();
}
#endif


/**
 * Output the string table string in C string litteral fashion.
 *
 * @param   pThis   The strint table instance.
 * @param   pString The string to print.
 * @param   pOut    The output stream.
 */
static void BldProgStrTab_PrintCStringLitteral(PBLDPROGSTRTAB pThis, PBLDPROGSTRING pString, FILE *pOut)
{
    const unsigned char *psz = (const unsigned char *)pString->pszString;
    unsigned char uch;
    while ((uch = *psz++) != '\0')
    {
#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
        if (pThis->aCompDict[uch].cchString == 1)
#else
        if (!(uch & 0x80))
#endif
        {
#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
            if (uch == 0xff)
                abort();
#endif
            if (uch != '\'' && uch != '\\')
                fputc((char)uch, pOut);
            else
            {
                fputc('\\', pOut);
                fputc((char)uch, pOut);
            }
        }
#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
# ifndef BLDPROG_STRTAB_PURE_ASCII
        else if (uch == 0xff)
        {
            RTUNICP uc = RTStrGetCp((const char *)psz);
            psz += RTStrCpSize(uc);
            fprintf(pOut, "\\u%04x", uc);
        }
# endif
        else
            fputs(pThis->aCompDict[uch].pszString, pOut);
#else
        else
            fprintf(pOut, "\\x%02x", (unsigned)uch);
        NOREF(pThis);
#endif
    }
}


/**
 * Writes the string table code to the output stream.
 *
 * @param   pThis               The strint table compiler instance.
 * @param   pOut                The output stream.
 * @param   pszScope            The scoping ("static " or empty string),
 *                              including trailing space.
 * @param   pszPrefix           The variable prefix. Typically "g_" for C programs,
 *                              whereas C++ classes normally use "class::s_g".
 * @param   pszBaseName         The base name for the variables.  The user
 *                              accessible variable will end up as just the
 *                              prefix and basename concatenated.
 */
static void BldProgStrTab_WriteStringTable(PBLDPROGSTRTAB pThis, FILE *pOut,
                                           const char *pszScope, const char *pszPrefix, const char *pszBaseName)
{
#ifdef RT_STRICT
    /*
     * Do some quick sanity checks while we're here.
     */
# ifdef BLDPROG_STRTAB_WITH_COMPRESSION
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aCompDict); i++)
    {
        if (BldProgBitIsSet(pThis->bmUsedChars, i)
#  ifdef BLDPROG_STRTAB_PURE_ASCII
            ? pThis->aCompDict[i].cchString != 1U
#  else
            ? pThis->aCompDict[i].cchString != (i != 0xffU ? 1U : 0U)
#  endif
            : pThis->aCompDict[i].cchString < 1U)
            abort();
        if (pThis->aCompDict[i].cchString > 1U)
            BldProgStrTab_CheckStrTabString(pThis, &pThis->aCompDict[i]);
    }
# endif
#endif

    /*
     * Create a table for speeding up the character categorization.
     */
    uint8_t abCharCat[256];
    memset(abCharCat, 0, sizeof(abCharCat));
    abCharCat[(unsigned char)'\\'] = 1;
    abCharCat[(unsigned char)'\''] = 1;
    for (unsigned i = 0; i < 0x20; i++)
        abCharCat[i] = 2;
    for (unsigned i = 0x7f; i < 0x100; i++)
        abCharCat[i] = 2;
#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
    for (unsigned i = 0; i < 0x100; i++)
        if (!BldProgBitIsSet(pThis->bmUsedChars, i)) /* Encode table references using '\xYY'. */
            abCharCat[i] = 2;
#endif

    /*
     * We follow the sorted string table, one string per line.
     */
    fprintf(pOut,
            "#include <iprt/bldprog-strtab.h>\n"
            "\n"
            "static const char g_achStrTab%s[] =\n"
            "{\n",
            pszBaseName);

    uint32_t off = 0;
    for (uint32_t i = 0; i < pThis->cSortedStrings; i++)
    {
        PBLDPROGSTRING pCur   = pThis->papSortedStrings[i];
        uint32_t       offEnd = pCur->offStrTab + (uint32_t)pCur->cchString;
        if (offEnd > off)
        {
            /* Comment with an uncompressed and more readable version of the string. */
            if (off == pCur->offStrTab)
                fprintf(pOut, "/* 0x%05x = \"", off);
            else
                fprintf(pOut, "/* 0X%05x = \"", off);
            BldProgStrTab_PrintCStringLitteral(pThis, pCur, pOut);
            fputs("\" */\n", pOut);

            /* Must use char by char here or we may trigger the max string
               length limit in the compiler, */
            fputs("    ", pOut);
            for (; off < offEnd; off++)
            {
                unsigned char uch = pThis->pachStrTab[off];
                fputc('\'', pOut);
                if (abCharCat[uch] == 0)
                    fputc(uch, pOut);
                else if (abCharCat[uch] != 1)
                    fprintf(pOut, "\\x%02x", (unsigned)uch);
                else
                {
                    fputc('\\', pOut);
                    fputc(uch, pOut);
                }
                fputc('\'', pOut);
                fputc(',', pOut);
            }
            fputc('\n', pOut);
        }
    }

    fprintf(pOut,
            "};\n"
            "AssertCompile(sizeof(g_achStrTab%s) == %#x);\n\n",
            pszBaseName, (unsigned)pThis->cchStrTab);

#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
    /*
     * Write the compression dictionary.
     */
    fprintf(pOut,
            "static const RTBLDPROGSTRREF g_aCompDict%s[%u] = \n"
            "{\n",
            pszBaseName, (unsigned)RT_ELEMENTS(pThis->aCompDict));
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aCompDict); i++)
        if (pThis->aCompDict[i].cchString > 1)
            fprintf(pOut, "    /*[%3u]=*/ { %#08x, %#04x }, // %6lu - %s\n", i,
                    pThis->aCompDict[i].offStrTab, (unsigned)pThis->aCompDict[i].cchString,
                    (unsigned long)pThis->auCompDictFreq[i], pThis->aCompDict[i].pszString);
# ifndef BLDPROG_STRTAB_PURE_ASCII
        else if (i == 0xff)
            fprintf(pOut, "    /*[%3u]=*/ { 0x000000, 0x00 }, // UTF-8 escape\n", i);
# endif
        else if (i == 0)
            fprintf(pOut, "    /*[%3u]=*/ { 0x000000, 0x00 }, // unused, because zero terminator\n", i);
        else if (i < 0x20 || i >= 0x7f)
            fprintf(pOut, "    /*[%3u]=*/ { 0x000000, 0x00 }, // %02x\n", i, i);
        else
            fprintf(pOut, "    /*[%3u]=*/ { 0x000000, 0x00 }, // '%c'\n", i,  (char)i);
    fprintf(pOut, "};\n\n");
#endif


    /*
     * Write the string table data structure.
     */
    fprintf(pOut,
            "%sconst RTBLDPROGSTRTAB %s%s = \n"
            "{\n"
            "    /*.pchStrTab  = */ &g_achStrTab%s[0],\n"
            "    /*.cchStrTab  = */ sizeof(g_achStrTab%s),\n"
            ,
            pszScope, pszPrefix, pszBaseName, pszBaseName, pszBaseName);
#ifdef BLDPROG_STRTAB_WITH_COMPRESSION
    fprintf(pOut,
            "    /*.cCompDict  = */ %u,\n"
            "    /*.paCompDict = */ &g_aCompDict%s[0]\n"
            "};\n"
# ifndef BLDPROG_STRTAB_PURE_ASCII /* 255 or 256 entries is how the decoder knows  */
            , (unsigned)RT_ELEMENTS(pThis->aCompDict) - 1,
# else
            , (unsigned)RT_ELEMENTS(pThis->aCompDict),
# endif
            pszBaseName);
#else
    fprintf(pOut,
            "    /*.cCompDict  = */ 0,\n"
            "    /*.paCompDict = */ NULL\n"
            "};\n");
#endif
}

#if RT_CLANG_PREREQ(4, 0) || RT_GNUC_PREREQ(4, 6)
#  pragma GCC diagnostic pop
#endif

#endif /* __cplusplus && IN_RING3 */

