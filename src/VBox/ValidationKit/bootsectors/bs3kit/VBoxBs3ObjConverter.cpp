/* $Id: VBoxBs3ObjConverter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Validation Kit - Boot Sector 3 object file convert.
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <iprt/types.h>
#include <iprt/ctype.h>
#include <iprt/assert.h>
#include <iprt/sort.h>
#include <iprt/x86.h>

#include <iprt/formats/elf64.h>
#include <iprt/formats/elf-amd64.h>
#include <iprt/formats/pecoff.h>
#include <iprt/formats/omf.h>
#include <iprt/formats/codeview.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if ARCH_BITS == 64 && !defined(RT_OS_WINDOWS) && !defined(RT_OS_DARWIN)
# define ELF_FMT_X64  "lx"
# define ELF_FMT_D64  "ld"
#else
# define ELF_FMT_X64  "llx"
# define ELF_FMT_D64  "lld"
#endif

/** Compares an OMF string with a constant string. */
#define IS_OMF_STR_EQUAL_EX(a_cch1, a_pch1, a_szConst2) \
    ( (a_cch1) == sizeof(a_szConst2) - 1 && memcmp(a_pch1, a_szConst2, sizeof(a_szConst2) - 1) == 0 )

/** Compares an OMF string with a constant string. */
#define IS_OMF_STR_EQUAL(a_pchZeroPrefixed, a_szConst2) \
    IS_OMF_STR_EQUAL_EX((uint8_t)((a_pchZeroPrefixed)[0]), &((a_pchZeroPrefixed)[1]), a_szConst2)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Verbosity level. */
static unsigned g_cVerbose = 0;
/** Indicates that it's output from the 16-bit watcom C or C++ compiler.
 * We will do some massaging for fixup records when this is used.  */
static bool     g_f16BitWatcomC = false;


/*
 * Minimal assertion support.
 */

RTDECL(bool) RTAssertShouldPanic(void)
{
    return true;
}


RTDECL(void) RTAssertMsg1Weak(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    fprintf(stderr,
            "VBoxBs3ObjConverter: assertion failed in %s (%s:%u)!\n"
            "VBoxBs3ObjConverter: %s\n",
            pszFunction, pszFile, uLine, pszExpr);
}


/**
 * Opens a file for binary reading or writing.
 *
 * @returns File stream handle.
 * @param   pszFile             The name of the file.
 * @param   fWrite              Whether to open for writing or reading.
 */
static FILE *openfile(const char *pszFile,  bool fWrite)
{
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    FILE *pFile = fopen(pszFile, fWrite ? "wb" : "rb");
#else
    FILE *pFile = fopen(pszFile, fWrite ? "w" : "r");
#endif
    if (!pFile)
        fprintf(stderr, "error: Failed to open '%s' for %s: %s (%d)\n",
                pszFile, fWrite ? "writing" : "reading", strerror(errno), errno);
    return pFile;
}


/**
 * Read the given file into memory.
 *
 * @returns true on success, false on failure.
 * @param   pszFile     The file to read.
 * @param   ppvFile     Where to return the memory.
 * @param   pcbFile     Where to return the size.
 */
static bool readfile(const char *pszFile, void **ppvFile, size_t *pcbFile)
{
    FILE *pFile = openfile(pszFile, false);
    if (pFile)
    {
        /*
         * Figure the size.
         */
        if (fseek(pFile, 0, SEEK_END) == 0)
        {
            long cbFile = ftell(pFile);
            if (cbFile > 0)
            {
                if (fseek(pFile, SEEK_SET, 0) == 0)
                {
                    /*
                     * Allocate and read content.
                     */
                    void *pvFile = malloc((size_t)cbFile);
                    if (pvFile)
                    {
                        if (fread(pvFile, cbFile, 1, pFile) == 1)
                        {
                            *ppvFile = pvFile;
                            *pcbFile = (size_t)cbFile;
                            fclose(pFile);
                            return true;
                        }
                        free(pvFile);
                        fprintf(stderr, "error: fread failed in '%s': %s (%d)\n", pszFile, strerror(errno), errno);
                    }
                    else
                        fprintf(stderr, "error: failed to allocate %ld bytes of memory for '%s'\n", cbFile, pszFile);
                }
                else
                    fprintf(stderr, "error: fseek #2 failed in '%s': %s (%d)\n", pszFile, strerror(errno), errno);
            }
            else
                fprintf(stderr, "error: ftell failed in '%s': %s (%d)\n", pszFile, strerror(errno), errno);
        }
        else
            fprintf(stderr, "error: fseek #1 failed in '%s': %s (%d)\n", pszFile, strerror(errno), errno);
        fclose(pFile);
    }
    return false;
}


/**
 * Write the given file into memory.
 *
 * @returns true on success, false on failure.
 * @param   pszFile     The file to write.
 * @param   pvFile      Where to return the memory.
 * @param   cbFile      Where to return the size.
 */
static bool writefile(const char *pszFile, void const *pvFile, size_t cbFile)
{
    remove(pszFile);

    FILE *pFile = openfile(pszFile, true);
    if (pFile)
    {
        if (fwrite(pvFile, cbFile, 1, pFile) == 1)
        {
            fclose(pFile);
            return true;
        }
        fprintf(stderr, "error: fwrite failed in '%s': %s (%d)\n", pszFile, strerror(errno), errno);
        fclose(pFile);
    }
    return false;
}


/**
 * Reports an error and returns false.
 *
 * @returns false
 * @param   pszFile             The filename.
 * @param   pszFormat           The message format string.
 * @param   ...                 Format arguments.
 */
static bool error(const char *pszFile, const char *pszFormat, ...)
{
    fflush(stdout);
    fprintf(stderr, "error: %s: ", pszFile);
    va_list va;
    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
    return false;
}



/*********************************************************************************************************************************
*   Common OMF Writer                                                                                                            *
*********************************************************************************************************************************/

/** Entry for each segment/section in the source format for mapping it to a
 *  segment defintion. */
typedef struct OMFTOSEGDEF
{
    /** The segment defintion index of the section, UINT16_MAX if not translated. */
    uint16_t    iSegDef;
    /** The group index for this segment, UINT16_MAX if not applicable. */
    uint16_t    iGrpDef;
    /** The class name table entry, UINT16_MAX if not applicable. */
    uint16_t    iClassNm;
    /** The group name for this segment, UINT16_MAX if not applicable. */
    uint16_t    iGrpNm;
    /** The group name for this segment, UINT16_MAX if not applicable. */
    uint16_t    iSegNm;
    /** The number of public definitions for this segment. */
    uint32_t    cPubDefs;
    /** The segment name (OMF). */
    char       *pszName;
} OMFTOSEGDEF;
/** Pointer to a segment/section to segdef mapping. */
typedef OMFTOSEGDEF *POMFTOSEGDEF;

/** Symbol table translation type. */
typedef enum OMFSYMTYPE
{
    /** Invalid symbol table entry (aux sym). */
    OMFSYMTYPE_INVALID = 0,
    /** Ignored. */
    OMFSYMTYPE_IGNORED,
    /** A public defintion.  */
    OMFSYMTYPE_PUBDEF,
    /** An external definition. */
    OMFSYMTYPE_EXTDEF,
    /** A segment reference for fixups. */
    OMFSYMTYPE_SEGDEF,
    /** Internal symbol that may be used for fixups. */
    OMFSYMTYPE_INTERNAL
} OMFSYMTYPE;

/** Symbol table translation. */
typedef struct OMFSYMBOL
{
    /** What this source symbol table entry should be translated into. */
    OMFSYMTYPE      enmType;
    /** The OMF table index. UINT16_MAX if not applicable. */
    uint16_t        idx;
    /** The OMF segment definition index. */
    uint16_t        idxSegDef;
    /** The OMF group definition index. */
    uint16_t        idxGrpDef;
} OMFSYMBOL;
/** Pointer to an source symbol table translation entry. */
typedef OMFSYMBOL *POMFSYMBOL;

/** OMF Writer LNAME lookup record. */
typedef struct OMFWRLNAME
{
    /** Pointer to the next entry with the name hash. */
    struct OMFWRLNAME      *pNext;
    /** The LNAMES index number.   */
    uint16_t                idxName;
    /** The name length.   */
    uint8_t                 cchName;
    /** The name (variable size).   */
    char                    szName[1];
} OMFWRLNAME;
/** Pointer to the a OMF writer LNAME lookup record. */
typedef OMFWRLNAME *POMFWRLNAME;

/**
 * OMF converter & writer instance.
 */
typedef struct OMFWRITER
{
    /** The source file name (for bitching). */
    const char     *pszSrc;
    /** The destination output file. */
    FILE           *pDst;

    /** Pointer to the table mapping from source segments/section to segdefs. */
    POMFTOSEGDEF    paSegments;
    /** Number of source segments/sections. */
    uint32_t        cSegments;

    /** Number of entries in the source symbol table. */
    uint32_t        cSymbols;
    /** Pointer to the table mapping from source symbols to OMF stuff. */
    POMFSYMBOL      paSymbols;

    /** LEDATA segment offset. */
    uint32_t        offSeg;
    /** Start of the current LEDATA record.   */
    uint32_t        offSegRec;
    /** The LEDATA end segment offset. */
    uint32_t        offSegEnd;
    /** The current LEDATA segment. */
    uint16_t        idx;

    /** The index of the next list of names entry. */
    uint16_t        idxNextName;

    /** The current record size.  */
    uint16_t        cbRec;
    /** The current record type */
    uint8_t         bType;
    /** The record data buffer (too large, but whatever).  */
    uint8_t         abData[_1K + 64];

    /** Current FIXUPP entry. */
    uint8_t         iFixupp;
    /** FIXUPP records being prepared for LEDATA currently stashed in abData.
     * We may have to adjust addend values in the LEDATA when converting to OMF
     * fixups. */
    struct
    {
        uint16_t    cbRec;
        uint8_t     abData[_1K + 64];
        uint8_t     abAlign[2]; /**< Alignment padding. */
    } aFixupps[3];

    /** The index of the FLAT group. */
    uint16_t        idxGrpFlat;
    /** The EXTDEF index of the __ImageBase symbol. */
    uint16_t        idxExtImageBase;

    /** LNAME lookup hash table. To avoid too many duplicates. */
    POMFWRLNAME     apNameLookup[63];
} OMFWRITE;
/** Pointer to an OMF writer. */
typedef OMFWRITE *POMFWRITER;


/**
 * Creates an OMF writer instance.
 */
static POMFWRITER omfWriter_Create(const char *pszSrc, uint32_t cSegments, uint32_t cSymbols, FILE *pDst)
{
    POMFWRITER pThis = (POMFWRITER)calloc(1, sizeof(OMFWRITER));
    if (pThis)
    {
        pThis->pszSrc        = pszSrc;
        pThis->idxNextName   = 1;       /* We start counting at 1. */
        pThis->cSegments     = cSegments;
        pThis->paSegments    = (POMFTOSEGDEF)calloc(cSegments, sizeof(OMFTOSEGDEF));
        if (pThis->paSegments)
        {
            pThis->cSymbols  = cSymbols;
            pThis->paSymbols = (POMFSYMBOL)calloc(cSymbols, sizeof(OMFSYMBOL));
            if (pThis->paSymbols)
            {
                pThis->pDst  = pDst;
                return pThis;
            }
            free(pThis->paSegments);
        }
        free(pThis);
    }
    error(pszSrc, "Out of memory!\n");
    return NULL;
}

/**
 * Destroys the given OMF writer instance.
 * @param   pThis           OMF writer instance.
 */
static void omfWriter_Destroy(POMFWRITER pThis)
{
    free(pThis->paSymbols);

    for (uint32_t i = 0; i < pThis->cSegments; i++)
        if (pThis->paSegments[i].pszName)
            free(pThis->paSegments[i].pszName);

    free(pThis->paSegments);

    uint32_t i = RT_ELEMENTS(pThis->apNameLookup);
    while (i-- > 0)
    {
        POMFWRLNAME pNext = pThis->apNameLookup[i];
        pThis->apNameLookup[i] = NULL;
        while (pNext)
        {
            POMFWRLNAME pFree = pNext;
            pNext = pNext->pNext;
            free(pFree);
        }
    }

    free(pThis);
}

static bool omfWriter_RecBegin(POMFWRITER pThis, uint8_t bType)
{
    pThis->bType = bType;
    pThis->cbRec = 0;
    return true;
}

static bool omfWriter_RecAddU8(POMFWRITER pThis, uint8_t b)
{
    if (pThis->cbRec < OMF_MAX_RECORD_PAYLOAD)
    {
        pThis->abData[pThis->cbRec++] = b;
        return true;
    }
    return error(pThis->pszSrc, "Exceeded max OMF record length (bType=%#x)!\n", pThis->bType);
}

static bool omfWriter_RecAddU16(POMFWRITER pThis, uint16_t u16)
{
    if (pThis->cbRec + 2U <= OMF_MAX_RECORD_PAYLOAD)
    {
        pThis->abData[pThis->cbRec++] = (uint8_t)u16;
        pThis->abData[pThis->cbRec++] = (uint8_t)(u16 >> 8);
        return true;
    }
    return error(pThis->pszSrc, "Exceeded max OMF record length (bType=%#x)!\n", pThis->bType);
}

static bool omfWriter_RecAddU32(POMFWRITER pThis, uint32_t u32)
{
    if (pThis->cbRec + 4U <= OMF_MAX_RECORD_PAYLOAD)
    {
        pThis->abData[pThis->cbRec++] = (uint8_t)u32;
        pThis->abData[pThis->cbRec++] = (uint8_t)(u32 >> 8);
        pThis->abData[pThis->cbRec++] = (uint8_t)(u32 >> 16);
        pThis->abData[pThis->cbRec++] = (uint8_t)(u32 >> 24);
        return true;
    }
    return error(pThis->pszSrc, "Exceeded max OMF record length (bType=%#x)!\n", pThis->bType);
}

static bool omfWriter_RecAddIdx(POMFWRITER pThis, uint16_t idx)
{
    if (idx < 128)
        return omfWriter_RecAddU8(pThis, (uint8_t)idx);
    if (idx < _32K)
        return omfWriter_RecAddU8(pThis, (uint8_t)(idx >> 8) | 0x80)
            && omfWriter_RecAddU8(pThis, (uint8_t)idx);
    return error(pThis->pszSrc, "Index out of range %#x\n", idx);
}

static bool omfWriter_RecAddBytes(POMFWRITER pThis, const void *pvData, size_t cbData)
{
    const uint16_t cbNasmHack = OMF_MAX_RECORD_PAYLOAD + 1;
    if (cbData + pThis->cbRec <= cbNasmHack)
    {
        memcpy(&pThis->abData[pThis->cbRec], pvData, cbData);
        pThis->cbRec += (uint16_t)cbData;
        return true;
    }
    return error(pThis->pszSrc, "Exceeded max OMF record length (bType=%#x, cbData=%#x, cbRec=%#x, max=%#x)!\n",
                 pThis->bType, (unsigned)cbData, pThis->cbRec, OMF_MAX_RECORD_PAYLOAD);
}

static bool omfWriter_RecAddStringNEx(POMFWRITER pThis, const char *pchString, size_t cchString, bool fPrependUnderscore)
{
    if (cchString < 256)
    {
        return omfWriter_RecAddU8(pThis, (uint8_t)cchString + fPrependUnderscore)
            && (!fPrependUnderscore || omfWriter_RecAddU8(pThis, '_'))
            && omfWriter_RecAddBytes(pThis, pchString, cchString);
    }
    return error(pThis->pszSrc, "String too long (%u bytes): '%*.*s'\n",
                 (unsigned)cchString, (int)cchString, (int)cchString, pchString);
}

static bool omfWriter_RecAddStringN(POMFWRITER pThis, const char *pchString, size_t cchString)
{
    return omfWriter_RecAddStringNEx(pThis, pchString, cchString, false /*fPrependUnderscore*/);
}

static bool omfWriter_RecAddString(POMFWRITER pThis, const char *pszString)
{
    return omfWriter_RecAddStringNEx(pThis, pszString, strlen(pszString), false /*fPrependUnderscore*/);
}

static bool omfWriter_RecEnd(POMFWRITER pThis, bool fAddCrc)
{
    if (   !fAddCrc
        || omfWriter_RecAddU8(pThis, 0))
    {
        OMFRECHDR RecHdr = { pThis->bType, RT_H2LE_U16(pThis->cbRec) };
        if (   fwrite(&RecHdr, sizeof(RecHdr), 1, pThis->pDst) == 1
            && fwrite(pThis->abData, pThis->cbRec, 1, pThis->pDst) == 1)
        {
            pThis->bType = 0;
            pThis->cbRec = 0;
            return true;
        }
        return error(pThis->pszSrc, "Write error\n");
    }
    return false;
}

static bool omfWriter_RecEndWithCrc(POMFWRITER pThis)
{
    return omfWriter_RecEnd(pThis, true /*fAddCrc*/);
}


static bool omfWriter_BeginModule(POMFWRITER pThis, const char *pszFile)
{
    return omfWriter_RecBegin(pThis, OMF_THEADR)
        && omfWriter_RecAddString(pThis, pszFile)
        && omfWriter_RecEndWithCrc(pThis);
}


/**
 * Simple stupid string hashing function (for LNAMES)
 * @returns 8-bit hash.
 * @param   pchName             The string.
 * @param   cchName             The string length.
 */
DECLINLINE(uint8_t) omfWriter_HashStrU8(const char *pchName, size_t cchName)
{
    if (cchName)
        return (uint8_t)(cchName + pchName[cchName >> 1]);
    return 0;
}

/**
 * Looks up a LNAME.
 *
 * @returns Index (0..32K) if found, UINT16_MAX if not found.
 * @param   pThis               The OMF writer.
 * @param   pchName             The name to look up.
 * @param   cchName             The length of the name.
 */
static uint16_t omfWriter_LNamesLookupN(POMFWRITER pThis, const char *pchName, size_t cchName)
{
    uint8_t uHash = omfWriter_HashStrU8(pchName, cchName);
    uHash %= RT_ELEMENTS(pThis->apNameLookup);

    POMFWRLNAME pCur = pThis->apNameLookup[uHash];
    while (pCur)
    {
        if (   pCur->cchName == cchName
            && memcmp(pCur->szName, pchName, cchName) == 0)
            return pCur->idxName;
        pCur = pCur->pNext;
    }

    return UINT16_MAX;
}

/**
 * Add a LNAME lookup record.
 *
 * @returns success indicator.
 * @param   pThis               The OMF writer.
 * @param   pchName             The name to look up.
 * @param   cchName             The length of the name.
 * @param   idxName             The name index.
 */
static bool omfWriter_LNamesAddLookup(POMFWRITER pThis, const char *pchName, size_t cchName, uint16_t idxName)
{
    POMFWRLNAME pCur = (POMFWRLNAME)malloc(sizeof(*pCur) + cchName);
    if (!pCur)
        return error("???", "Out of memory!\n");

    pCur->idxName = idxName;
    pCur->cchName = (uint8_t)cchName;
    memcpy(pCur->szName, pchName, cchName);
    pCur->szName[cchName] = '\0';

    uint8_t uHash = omfWriter_HashStrU8(pchName, cchName);
    uHash %= RT_ELEMENTS(pThis->apNameLookup);
    pCur->pNext = pThis->apNameLookup[uHash];
    pThis->apNameLookup[uHash] = pCur;

    return true;
}


static bool omfWriter_LNamesAddN(POMFWRITER pThis, const char *pchName, size_t cchName, uint16_t *pidxName)
{
    /* See if we've already got that name in the list. */
    uint16_t idxName;
    if (pidxName) /* If pidxName is NULL, we assume the caller might just be passing stuff thru. */
    {
        idxName = omfWriter_LNamesLookupN(pThis, pchName, cchName);
        if (idxName != UINT16_MAX)
        {
            *pidxName = idxName;
            return true;
        }
    }

    /* split? */
    if (pThis->cbRec + 1 /*len*/ + cchName + 1 /*crc*/ > OMF_MAX_RECORD_PAYLOAD)
    {
        if (pThis->cbRec == 0)
            return error(pThis->pszSrc, "Too long LNAME '%*.*s'\n", (int)cchName, (int)cchName, pchName);
        if (   !omfWriter_RecEndWithCrc(pThis)
            || !omfWriter_RecBegin(pThis, OMF_LNAMES))
            return false;
    }

    idxName = pThis->idxNextName++;
    if (pidxName)
        *pidxName = idxName;
    return omfWriter_RecAddStringN(pThis, pchName, cchName)
        && omfWriter_LNamesAddLookup(pThis, pchName, cchName, idxName);
}

static bool omfWriter_LNamesAdd(POMFWRITER pThis, const char *pszName, uint16_t *pidxName)
{
    return omfWriter_LNamesAddN(pThis, pszName, strlen(pszName), pidxName);
}

static bool omfWriter_LNamesBegin(POMFWRITER pThis, bool fAddZeroEntry)
{
    /* First entry is an empty string. */
    return omfWriter_RecBegin(pThis, OMF_LNAMES)
        && (   pThis->idxNextName > 1
            || !fAddZeroEntry
            || omfWriter_LNamesAddN(pThis, "", 0, NULL));
}

static bool omfWriter_LNamesEnd(POMFWRITER pThis)
{
    return omfWriter_RecEndWithCrc(pThis);
}


static bool omfWriter_SegDef(POMFWRITER pThis, uint8_t bSegAttr, uint32_t cbSeg, uint16_t idxSegName, uint16_t idxSegClass,
                             uint16_t idxOverlay = 1 /* NULL entry */)
{
    return omfWriter_RecBegin(pThis, OMF_SEGDEF32)
        && omfWriter_RecAddU8(pThis, bSegAttr)
        && omfWriter_RecAddU32(pThis, cbSeg)
        && omfWriter_RecAddIdx(pThis, idxSegName)
        && omfWriter_RecAddIdx(pThis, idxSegClass)
        && omfWriter_RecAddIdx(pThis, idxOverlay)
        && omfWriter_RecEndWithCrc(pThis);
}

static bool omfWriter_SegDef16(POMFWRITER pThis, uint8_t bSegAttr, uint32_t cbSeg, uint16_t idxSegName, uint16_t idxSegClass,
                               uint16_t idxOverlay = 1 /* NULL entry */)
{
    Assert(cbSeg <= UINT16_MAX);
    return omfWriter_RecBegin(pThis, OMF_SEGDEF16)
        && omfWriter_RecAddU8(pThis, bSegAttr)
        && omfWriter_RecAddU16(pThis, cbSeg)
        && omfWriter_RecAddIdx(pThis, idxSegName)
        && omfWriter_RecAddIdx(pThis, idxSegClass)
        && omfWriter_RecAddIdx(pThis, idxOverlay)
        && omfWriter_RecEndWithCrc(pThis);
}

static bool omfWriter_GrpDefBegin(POMFWRITER pThis, uint16_t idxGrpName)
{
    return omfWriter_RecBegin(pThis, OMF_GRPDEF)
        && omfWriter_RecAddIdx(pThis, idxGrpName);
}

static bool omfWriter_GrpDefAddSegDef(POMFWRITER pThis, uint16_t idxSegDef)
{
    return omfWriter_RecAddU8(pThis, 0xff)
        && omfWriter_RecAddIdx(pThis, idxSegDef);
}

static bool omfWriter_GrpDefEnd(POMFWRITER pThis)
{
    return omfWriter_RecEndWithCrc(pThis);
}


static bool omfWriter_PubDefBegin(POMFWRITER pThis, uint16_t idxGrpDef, uint16_t idxSegDef)
{
    return omfWriter_RecBegin(pThis, OMF_PUBDEF32)
        && omfWriter_RecAddIdx(pThis, idxGrpDef)
        && omfWriter_RecAddIdx(pThis, idxSegDef)
        && (   idxSegDef != 0
            || omfWriter_RecAddU16(pThis, 0));

}

static bool omfWriter_PubDefAddN(POMFWRITER pThis, uint32_t uValue, const char *pchString, size_t cchString,
                                 bool fPrependUnderscore)
{
    /* Split? */
    if (pThis->cbRec + 1 + cchString + 4 + 1 + 1 + fPrependUnderscore > OMF_MAX_RECORD_PAYLOAD)
    {
        if (cchString >= 256)
            return error(pThis->pszSrc, "PUBDEF string too long %u ('%s')\n",
                         (unsigned)cchString, (int)cchString, (int)cchString, pchString);
        if (!omfWriter_RecEndWithCrc(pThis))
            return false;

        /* Figure out the initial data length. */
        pThis->cbRec      = 1 + ((pThis->abData[0] & 0x80) != 0);
        if (pThis->abData[pThis->cbRec] != 0)
            pThis->cbRec += 1 + ((pThis->abData[pThis->cbRec] & 0x80) != 0);
        else
            pThis->cbRec += 3;
        pThis->bType = OMF_PUBDEF32;
    }

    return omfWriter_RecAddStringNEx(pThis, pchString, cchString, fPrependUnderscore)
        && omfWriter_RecAddU32(pThis, uValue)
        && omfWriter_RecAddIdx(pThis, 0); /* type */
}

static bool omfWriter_PubDefAdd(POMFWRITER pThis, uint32_t uValue, const char *pszString, bool fPrependUnderscore)
{
    return omfWriter_PubDefAddN(pThis, uValue, pszString, strlen(pszString), fPrependUnderscore);
}

static bool omfWriter_PubDefEnd(POMFWRITER pThis)
{
    return omfWriter_RecEndWithCrc(pThis);
}

/**
 * EXTDEF - Begin record.
 */
static bool omfWriter_ExtDefBegin(POMFWRITER pThis)
{
    return omfWriter_RecBegin(pThis, OMF_EXTDEF);

}

/**
 * EXTDEF - Add an entry, split record if necessary.
 */
static bool omfWriter_ExtDefAddN(POMFWRITER pThis, const char *pchString, size_t cchString, uint16_t idxType,
                                 bool fPrependUnderscore)
{
    /* Split? */
    if (pThis->cbRec + 1 + cchString + 1 + 1 + fPrependUnderscore > OMF_MAX_RECORD_PAYLOAD)
    {
        if (cchString >= 256)
            return error(pThis->pszSrc, "EXTDEF string too long %u ('%s')\n",
                         (unsigned)cchString, (int)cchString, (int)cchString, pchString);
        if (   !omfWriter_RecEndWithCrc(pThis)
            || !omfWriter_RecBegin(pThis, OMF_EXTDEF))
            return false;
    }

    return omfWriter_RecAddStringNEx(pThis, pchString, cchString, fPrependUnderscore)
        && omfWriter_RecAddIdx(pThis, idxType); /* type */
}

/**
 * EXTDEF - Add an entry, split record if necessary.
 */
static bool omfWriter_ExtDefAdd(POMFWRITER pThis, const char *pszString, bool fPrependUnderscore)
{
    return omfWriter_ExtDefAddN(pThis, pszString, strlen(pszString), 0, fPrependUnderscore);
}

/**
 * EXTDEF - End of record.
 */
static bool omfWriter_ExtDefEnd(POMFWRITER pThis)
{
    return omfWriter_RecEndWithCrc(pThis);
}

/**
 * COMENT/LINK_PASS_SEP - Add a link pass separator comment.
 */
static bool omfWriter_LinkPassSeparator(POMFWRITER pThis)
{
    return omfWriter_RecBegin(pThis, OMF_COMENT)
        && omfWriter_RecAddU8(pThis, OMF_CTYP_NO_LIST)
        && omfWriter_RecAddU8(pThis, OMF_CCLS_LINK_PASS_SEP)
        && omfWriter_RecAddU8(pThis, 1)
        && omfWriter_RecEndWithCrc(pThis);
}


/**
 * LEDATA + FIXUPP - Begin records.
 */
static bool omfWriter_LEDataBegin(POMFWRITER pThis, uint16_t idxSeg, uint32_t offSeg)
{
    if (   omfWriter_RecBegin(pThis, OMF_LEDATA32)
        && omfWriter_RecAddIdx(pThis, idxSeg)
        && omfWriter_RecAddU32(pThis, offSeg))
    {
        pThis->idx       = idxSeg;
        pThis->offSeg    = offSeg;
        pThis->offSegRec = offSeg;
        pThis->offSegEnd = offSeg + OMF_MAX_RECORD_PAYLOAD - 1 /*CRC*/ - pThis->cbRec;
        pThis->offSegEnd &= ~(uint32_t)7; /* qword align. */

        /* Reset the associated FIXUPP records. */
        pThis->iFixupp = 0;
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->aFixupps); i++)
            pThis->aFixupps[i].cbRec = 0;
        return true;
    }
    return false;
}

/**
 * LEDATA + FIXUPP - Begin records.
 */
static bool omfWriter_LEDataBeginEx(POMFWRITER pThis, uint16_t idxSeg, uint32_t offSeg,
                                    uint32_t cbData, uint32_t cbRawData, void const *pbRawData, uint8_t **ppbData)
{
    if (   omfWriter_RecBegin(pThis, OMF_LEDATA32)
        && omfWriter_RecAddIdx(pThis, idxSeg)
        && omfWriter_RecAddU32(pThis, offSeg))
    {
        if (   cbData <= _1K
            && pThis->cbRec + cbData + 1 <= OMF_MAX_RECORD_PAYLOAD)
        {
            uint8_t *pbDst = &pThis->abData[pThis->cbRec];
            if (ppbData)
                *ppbData = pbDst;

            if (cbRawData)
                memcpy(pbDst, pbRawData, RT_MIN(cbData, cbRawData));
            if (cbData > cbRawData)
                memset(&pbDst[cbRawData], 0, cbData - cbRawData);

            pThis->cbRec    += cbData;
            pThis->idx       = idxSeg;
            pThis->offSegRec = offSeg;
            pThis->offSeg    = offSeg + cbData;
            pThis->offSegEnd = offSeg + cbData;

            /* Reset the associated FIXUPP records. */
            pThis->iFixupp = 0;
            for (unsigned i = 0; i < RT_ELEMENTS(pThis->aFixupps); i++)
                pThis->aFixupps[i].cbRec = 0;
            return true;
        }
        error(pThis->pszSrc, "Too much data for LEDATA record! (%#x)\n", (unsigned)cbData);
    }
    return false;
}

/**
 * LEDATA + FIXUPP - Add FIXUPP subrecord bytes, split if necessary.
 */
static bool omfWriter_LEDataAddFixuppBytes(POMFWRITER pThis, void *pvSubRec, size_t cbSubRec)
{
    /* Split? */
    unsigned iFixupp = pThis->iFixupp;
    if (pThis->aFixupps[iFixupp].cbRec + cbSubRec >= OMF_MAX_RECORD_PAYLOAD)
    {
        if (g_cVerbose >= 2)
            printf("debug: FIXUPP split\n");
        iFixupp++;
        if (iFixupp >= RT_ELEMENTS(pThis->aFixupps))
            return error(pThis->pszSrc, "Out of FIXUPP records\n");
        pThis->iFixupp = iFixupp;
        pThis->aFixupps[iFixupp].cbRec = 0; /* paranoia */
    }

    /* Append the sub-record data. */
    memcpy(&pThis->aFixupps[iFixupp].abData[pThis->aFixupps[iFixupp].cbRec], pvSubRec, cbSubRec);
    pThis->aFixupps[iFixupp].cbRec += (uint16_t)cbSubRec;
    return true;
}

/**
 * LEDATA + FIXUPP - Add fixup, split if necessary.
 */
static bool omfWriter_LEDataAddFixup(POMFWRITER pThis, uint16_t offDataRec, bool fSelfRel, uint8_t bLocation,
                                     uint8_t bFrame, uint16_t idxFrame,
                                     uint8_t bTarget, uint16_t idxTarget, bool fTargetDisp, uint32_t offTargetDisp)
{
    if (g_cVerbose >= 2)
        printf("debug: FIXUP[%#x]: off=%#x frame=%u:%#x target=%u:%#x disp=%d:%#x\n", pThis->aFixupps[pThis->iFixupp].cbRec,
               offDataRec, bFrame, idxFrame, bTarget, idxTarget, fTargetDisp, offTargetDisp);

    if (   offDataRec >= _1K
        || bFrame >= 6
        || bTarget > 6
        || idxFrame >= _32K
        || idxTarget >= _32K
        || fTargetDisp != (bTarget <= OMF_FIX_T_FRAME_NO) )
        return error(pThis->pszSrc,
                     "Internal error: offDataRec=%#x bFrame=%u idxFrame=%#x bTarget=%u idxTarget=%#x fTargetDisp=%d offTargetDisp=%#x\n",
                     offDataRec, bFrame, idxFrame, bTarget, idxTarget, fTargetDisp, offTargetDisp);


    /*
     * Encode the FIXUP subrecord.
     */
    uint8_t abFixup[16];
    uint8_t off = 0;
    /* Location */
    abFixup[off++] = (offDataRec >> 8) | (bLocation << 2) | ((uint8_t)!fSelfRel << 6) | 0x80;
    abFixup[off++] = (uint8_t)offDataRec;
    /* Fix Data */
    abFixup[off++] = 0x00 /*F=0*/ | (bFrame << 4) | 0x00 /*T=0*/ | bTarget;
    /* Frame Datum */
    if (bFrame <= OMF_FIX_F_FRAME_NO)
    {
        if (idxFrame >= 128)
            abFixup[off++] = (uint8_t)(idxFrame >> 8) | 0x80;
        abFixup[off++] = (uint8_t)idxFrame;
    }
    /* Target Datum */
    if (idxTarget >= 128)
        abFixup[off++] = (uint8_t)(idxTarget >> 8) | 0x80;
    abFixup[off++] = (uint8_t)idxTarget;
    /* Target Displacement */
    if (fTargetDisp)
    {
        abFixup[off++] = RT_BYTE1(offTargetDisp);
        abFixup[off++] = RT_BYTE2(offTargetDisp);
        abFixup[off++] = RT_BYTE3(offTargetDisp);
        abFixup[off++] = RT_BYTE4(offTargetDisp);
    }

    return omfWriter_LEDataAddFixuppBytes(pThis, abFixup, off);
}

/**
 * LEDATA + FIXUPP - Add simple fixup, split if necessary.
 */
static bool omfWriter_LEDataAddFixupNoDisp(POMFWRITER pThis, uint16_t offDataRec, uint8_t bLocation,
                                           uint8_t bFrame, uint16_t idxFrame, uint8_t bTarget, uint16_t idxTarget)
{
    return omfWriter_LEDataAddFixup(pThis, offDataRec, false /*fSelfRel*/, bLocation, bFrame, idxFrame, bTarget, idxTarget,
                                    false /*fTargetDisp*/, 0 /*offTargetDisp*/);
}


/**
 * LEDATA + FIXUPP - End of records.
 */
static bool omfWriter_LEDataEnd(POMFWRITER pThis)
{
    if (omfWriter_RecEndWithCrc(pThis))
    {
        for (unsigned iFixupp = 0; iFixupp <= pThis->iFixupp; iFixupp++)
        {
            uint16_t const cbRec = pThis->aFixupps[iFixupp].cbRec;
            if (!cbRec)
                break;
            if (g_cVerbose >= 3)
                printf("debug: FIXUPP32 #%u cbRec=%#x\n", iFixupp, cbRec);
            if (   !omfWriter_RecBegin(pThis, OMF_FIXUPP32)
                || !omfWriter_RecAddBytes(pThis, pThis->aFixupps[iFixupp].abData, cbRec)
                || !omfWriter_RecEndWithCrc(pThis))
                return false;
        }
        pThis->iFixupp = 0;
        return true;
    }
    return false;
}

/**
 * LEDATA + FIXUPP - Splits the LEDATA record.
 */
static bool omfWriter_LEDataSplit(POMFWRITER pThis)
{
    return omfWriter_LEDataEnd(pThis)
        && omfWriter_LEDataBegin(pThis, pThis->idx, pThis->offSeg);
}

/**
 * LEDATA + FIXUPP - Returns available space in current LEDATA record.
 */
static uint32_t omfWriter_LEDataAvailable(POMFWRITER pThis)
{
    if (pThis->offSeg < pThis->offSegEnd)
        return pThis->offSegEnd - pThis->offSeg;
    return 0;
}

/**
 * LEDATA + FIXUPP - Splits LEDATA record if less than @a cb bytes available.
 */
static bool omfWriter_LEDataEnsureSpace(POMFWRITER pThis, uint32_t cb)
{
    if (   omfWriter_LEDataAvailable(pThis) >= cb
        || omfWriter_LEDataSplit(pThis))
        return true;
    return false;
}

/**
 * LEDATA + FIXUPP - Adds data to the LEDATA record, splitting it if needed.
 */
static bool omfWriter_LEDataAddBytes(POMFWRITER pThis, void const *pvData, size_t cbData)
{
    while (cbData > 0)
    {
        uint32_t cbAvail = omfWriter_LEDataAvailable(pThis);
        if (cbAvail >= cbData)
        {
            if (omfWriter_RecAddBytes(pThis, pvData, cbData))
            {
                pThis->offSeg += (uint32_t)cbData;
                break;
            }
            return false;
        }
        if (!omfWriter_RecAddBytes(pThis, pvData, cbAvail))
            return false;
        pThis->offSeg += cbAvail;
        pvData         = (uint8_t const *)pvData + cbAvail;
        cbData        -= cbAvail;
        if (!omfWriter_LEDataSplit(pThis))
            return false;
    }
    return true;
}

/**
 * LEDATA + FIXUPP - Adds a U32 to the LEDATA record, splitting if needed.
 */
static bool omfWriter_LEDataAddU32(POMFWRITER pThis, uint32_t u32)
{
    if (   omfWriter_LEDataEnsureSpace(pThis, 4)
        && omfWriter_RecAddU32(pThis, u32))
    {
        pThis->offSeg += 4;
        return true;
    }
    return false;
}

/**
 * LEDATA + FIXUPP - Adds a U16 to the LEDATA record, splitting if needed.
 */
static bool omfWriter_LEDataAddU16(POMFWRITER pThis, uint16_t u16)
{
    if (   omfWriter_LEDataEnsureSpace(pThis, 2)
        && omfWriter_RecAddU16(pThis, u16))
    {
        pThis->offSeg += 2;
        return true;
    }
    return false;
}

#if 0 /* unused */
/**
 * LEDATA + FIXUPP - Adds a byte to the LEDATA record, splitting if needed.
 */
static bool omfWriter_LEDataAddU8(POMFWRITER pThis, uint8_t b)
{
    if (   omfWriter_LEDataEnsureSpace(pThis, 1)
        && omfWriter_RecAddU8(pThis, b))
    {
        pThis->offSeg += 1;
        return true;
    }
    return false;
}
#endif

/**
 * MODEND - End of module, simple variant.
 */
static bool omfWriter_EndModule(POMFWRITER pThis)
{
    return omfWriter_RecBegin(pThis, OMF_MODEND32)
        && omfWriter_RecAddU8(pThis, 0)
        && omfWriter_RecEndWithCrc(pThis);
}




/*********************************************************************************************************************************
*   ELF64/AMD64 -> ELF64/i386 Converter                                                                                          *
*********************************************************************************************************************************/

/** AMD64 relocation type names for ELF. */
static const char * const g_apszElfAmd64RelTypes[] =
{
    "R_X86_64_NONE",
    "R_X86_64_64",
    "R_X86_64_PC32",
    "R_X86_64_GOT32",
    "R_X86_64_PLT32",
    "R_X86_64_COPY",
    "R_X86_64_GLOB_DAT",
    "R_X86_64_JMP_SLOT",
    "R_X86_64_RELATIVE",
    "R_X86_64_GOTPCREL",
    "R_X86_64_32",
    "R_X86_64_32S",
    "R_X86_64_16",
    "R_X86_64_PC16",
    "R_X86_64_8",
    "R_X86_64_PC8",
    "R_X86_64_DTPMOD64",
    "R_X86_64_DTPOFF64",
    "R_X86_64_TPOFF64",
    "R_X86_64_TLSGD",
    "R_X86_64_TLSLD",
    "R_X86_64_DTPOFF32",
    "R_X86_64_GOTTPOFF",
    "R_X86_64_TPOFF32",
};

/** AMD64 relocation type sizes for ELF. */
static uint8_t const g_acbElfAmd64RelTypes[] =
{
    0, /* R_X86_64_NONE */
    8, /* R_X86_64_64 */
    4, /* R_X86_64_PC32 */
    4, /* R_X86_64_GOT32 */
    4, /* R_X86_64_PLT32 */
    0, /* R_X86_64_COPY */
    0, /* R_X86_64_GLOB_DAT */
    0, /* R_X86_64_JMP_SLOT */
    0, /* R_X86_64_RELATIVE */
    0, /* R_X86_64_GOTPCREL */
    4, /* R_X86_64_32 */
    4, /* R_X86_64_32S */
    2, /* R_X86_64_16 */
    2, /* R_X86_64_PC16 */
    1, /* R_X86_64_8 */
    1, /* R_X86_64_PC8 */
    0, /* R_X86_64_DTPMOD64 */
    0, /* R_X86_64_DTPOFF64 */
    0, /* R_X86_64_TPOFF64 */
    0, /* R_X86_64_TLSGD */
    0, /* R_X86_64_TLSLD */
    0, /* R_X86_64_DTPOFF32 */
    0, /* R_X86_64_GOTTPOFF */
    0, /* R_X86_64_TPOFF32 */
};

/** Macro for getting the size of a AMD64 ELF relocation. */
#define ELF_AMD64_RELOC_SIZE(a_Type) ( (a_Type) < RT_ELEMENTS(g_acbElfAmd64RelTypes) ? g_acbElfAmd64RelTypes[(a_Type)] : 1)


typedef struct ELFDETAILS
{
    /** The ELF header. */
    Elf64_Ehdr const   *pEhdr;
    /** The section header table.   */
    Elf64_Shdr const   *paShdrs;
    /** The string table for the section names. */
    const char         *pchShStrTab;

    /** The symbol table section number. UINT16_MAX if not found.   */
    uint16_t            iSymSh;
    /** The string table section number. UINT16_MAX if not found. */
    uint16_t            iStrSh;

    /** The symbol table.   */
    Elf64_Sym const    *paSymbols;
    /** The number of symbols in the symbol table. */
    uint32_t            cSymbols;

    /** Pointer to the (symbol) string table if found. */
    const char         *pchStrTab;
    /** The string table size. */
    size_t              cbStrTab;

} ELFDETAILS;
typedef ELFDETAILS *PELFDETAILS;
typedef ELFDETAILS const *PCELFDETAILS;


static bool validateElf(const char *pszFile, uint8_t const *pbFile, size_t cbFile, PELFDETAILS pElfStuff)
{
    /*
     * Initialize the ELF details structure.
     */
    memset(pElfStuff, 0,  sizeof(*pElfStuff));
    pElfStuff->iSymSh = UINT16_MAX;
    pElfStuff->iStrSh = UINT16_MAX;

    /*
     * Validate the header and our other expectations.
     */
    Elf64_Ehdr const *pEhdr = (Elf64_Ehdr const *)pbFile;
    pElfStuff->pEhdr = pEhdr;
    if (   pEhdr->e_ident[EI_CLASS] != ELFCLASS64
        || pEhdr->e_ident[EI_DATA]  != ELFDATA2LSB
        || pEhdr->e_ehsize          != sizeof(Elf64_Ehdr)
        || pEhdr->e_shentsize       != sizeof(Elf64_Shdr)
        || pEhdr->e_version         != EV_CURRENT )
        return error(pszFile, "Unsupported ELF config\n");
    if (pEhdr->e_type != ET_REL)
        return error(pszFile, "Expected relocatable ELF file (e_type=%d)\n", pEhdr->e_type);
    if (pEhdr->e_machine != EM_X86_64)
        return error(pszFile, "Expected relocatable ELF file (e_type=%d)\n", pEhdr->e_machine);
    if (pEhdr->e_phnum != 0)
        return error(pszFile, "Expected e_phnum to be zero not %u\n", pEhdr->e_phnum);
    if (pEhdr->e_shnum < 2)
        return error(pszFile, "Expected e_shnum to be two or higher\n");
    if (pEhdr->e_shstrndx >= pEhdr->e_shnum || pEhdr->e_shstrndx == 0)
        return error(pszFile, "Bad e_shstrndx=%u (e_shnum=%u)\n", pEhdr->e_shstrndx, pEhdr->e_shnum);
    if (   pEhdr->e_shoff >= cbFile
        || pEhdr->e_shoff + pEhdr->e_shnum * sizeof(Elf64_Shdr) > cbFile)
        return error(pszFile, "Section table is outside the file (e_shoff=%#llx, e_shnum=%u, cbFile=%#llx)\n",
                     pEhdr->e_shstrndx, pEhdr->e_shnum, (uint64_t)cbFile);

    /*
     * Locate the section name string table.
     * We assume it's okay as we only reference it in verbose mode.
     */
    Elf64_Shdr const *paShdrs = (Elf64_Shdr const *)&pbFile[pEhdr->e_shoff];
    pElfStuff->paShdrs = paShdrs;

    Elf64_Xword const cbShStrTab = paShdrs[pEhdr->e_shstrndx].sh_size;
    if (   paShdrs[pEhdr->e_shstrndx].sh_offset > cbFile
        || cbShStrTab > cbFile
        || paShdrs[pEhdr->e_shstrndx].sh_offset + cbShStrTab > cbFile)
        return error(pszFile,
                     "Section string table is outside the file (sh_offset=%#" ELF_FMT_X64 " sh_size=%#" ELF_FMT_X64 " cbFile=%#" ELF_FMT_X64 ")\n",
                     paShdrs[pEhdr->e_shstrndx].sh_offset, paShdrs[pEhdr->e_shstrndx].sh_size, (Elf64_Xword)cbFile);
    const char *pchShStrTab = (const char *)&pbFile[paShdrs[pEhdr->e_shstrndx].sh_offset];
    pElfStuff->pchShStrTab = pchShStrTab;

    /*
     * Work the section table.
     */
    bool fRet = true;
    for (uint32_t i = 1; i < pEhdr->e_shnum; i++)
    {
        if (paShdrs[i].sh_name >= cbShStrTab)
            return error(pszFile, "Invalid sh_name value (%#x) for section #%u\n", paShdrs[i].sh_name, i);
        const char *pszShNm = &pchShStrTab[paShdrs[i].sh_name];

        if (   paShdrs[i].sh_offset > cbFile
            || paShdrs[i].sh_size > cbFile
            || paShdrs[i].sh_offset + paShdrs[i].sh_size > cbFile)
            return error(pszFile, "Section #%u '%s' has data outside the file: %#" ELF_FMT_X64 " LB %#" ELF_FMT_X64 " (cbFile=%#" ELF_FMT_X64 ")\n",
                         i, pszShNm, paShdrs[i].sh_offset, paShdrs[i].sh_size, (Elf64_Xword)cbFile);
        if (g_cVerbose)
            printf("shdr[%u]: name=%#x '%s' type=%#x flags=%#" ELF_FMT_X64 " addr=%#" ELF_FMT_X64 " off=%#" ELF_FMT_X64 " size=%#" ELF_FMT_X64 "\n"
                   "          link=%u info=%#x align=%#" ELF_FMT_X64 " entsize=%#" ELF_FMT_X64 "\n",
                   i, paShdrs[i].sh_name, pszShNm, paShdrs[i].sh_type, paShdrs[i].sh_flags,
                   paShdrs[i].sh_addr, paShdrs[i].sh_offset, paShdrs[i].sh_size,
                   paShdrs[i].sh_link, paShdrs[i].sh_info, paShdrs[i].sh_addralign, paShdrs[i].sh_entsize);

        if (paShdrs[i].sh_link >= pEhdr->e_shnum)
            return error(pszFile, "Section #%u '%s' links to a section outside the section table: %#x, max %#x\n",
                         i, pszShNm, paShdrs[i].sh_link, pEhdr->e_shnum);
        if (!RT_IS_POWER_OF_TWO(paShdrs[i].sh_addralign))
            return error(pszFile, "Section #%u '%s' alignment value is not a power of two: %#" ELF_FMT_X64 "\n",
                         i, pszShNm, paShdrs[i].sh_addralign);
        if (!RT_IS_POWER_OF_TWO(paShdrs[i].sh_addralign))
            return error(pszFile, "Section #%u '%s' alignment value is not a power of two: %#" ELF_FMT_X64 "\n",
                         i, pszShNm, paShdrs[i].sh_addralign);
        if (paShdrs[i].sh_addr != 0)
            return error(pszFile, "Section #%u '%s' has non-zero address: %#" ELF_FMT_X64 "\n", i, pszShNm, paShdrs[i].sh_addr);

        if (paShdrs[i].sh_type == SHT_RELA)
        {
            if (paShdrs[i].sh_entsize != sizeof(Elf64_Rela))
                return error(pszFile, "Expected sh_entsize to be %u not %u for section #%u (%s)\n", (unsigned)sizeof(Elf64_Rela),
                             paShdrs[i].sh_entsize, i, pszShNm);
            uint32_t const cRelocs = paShdrs[i].sh_size / sizeof(Elf64_Rela);
            if (cRelocs * sizeof(Elf64_Rela) != paShdrs[i].sh_size)
                return error(pszFile, "Uneven relocation entry count in #%u (%s): sh_size=%#" ELF_FMT_X64 "\n",
                             i, pszShNm, paShdrs[i].sh_size);
            if (   paShdrs[i].sh_offset > cbFile
                || paShdrs[i].sh_size  >= cbFile
                || paShdrs[i].sh_offset + paShdrs[i].sh_size > cbFile)
                return error(pszFile, "The content of section #%u '%s' is outside the file (%#" ELF_FMT_X64 " LB %#" ELF_FMT_X64 ", cbFile=%#lx)\n",
                             i, pszShNm, paShdrs[i].sh_offset, paShdrs[i].sh_size, (unsigned long)cbFile);
            if (paShdrs[i].sh_info != i - 1)
                return error(pszFile, "Expected relocation section #%u (%s) to link to previous section: sh_info=%#u\n",
                             i, pszShNm, (unsigned)paShdrs[i].sh_link);
            if (paShdrs[paShdrs[i].sh_link].sh_type != SHT_SYMTAB)
                return error(pszFile, "Expected relocation section #%u (%s) to link to symbol table: sh_link=%#u -> sh_type=%#x\n",
                             i, pszShNm, (unsigned)paShdrs[i].sh_link, (unsigned)paShdrs[paShdrs[i].sh_link].sh_type);
            uint32_t cSymbols = paShdrs[paShdrs[i].sh_link].sh_size / paShdrs[paShdrs[i].sh_link].sh_entsize;

            Elf64_Rela const  *paRelocs = (Elf64_Rela *)&pbFile[paShdrs[i].sh_offset];
            for (uint32_t j = 0; j < cRelocs; j++)
            {
                uint8_t const bType = ELF64_R_TYPE(paRelocs[j].r_info);
                if (RT_UNLIKELY(bType >= R_X86_64_COUNT))
                    fRet = error(pszFile,
                                 "%#018" ELF_FMT_X64 "  %#018" ELF_FMT_X64 ": unknown fix up %#x  (%+" ELF_FMT_D64 ")\n",
                                 paRelocs[j].r_offset, paRelocs[j].r_info, bType, paRelocs[j].r_addend);
                if (RT_UNLIKELY(   paRelocs[j].r_offset > paShdrs[i - 1].sh_size
                                ||   paRelocs[j].r_offset + ELF_AMD64_RELOC_SIZE(ELF64_R_TYPE(paRelocs[j].r_info))
                                   > paShdrs[i - 1].sh_size))
                    fRet = error(pszFile,
                                 "%#018" ELF_FMT_X64 "  %#018" ELF_FMT_X64 ": out of bounds (sh_size %" ELF_FMT_X64 ")\n",
                                 paRelocs[j].r_offset, paRelocs[j].r_info, paShdrs[i - 1].sh_size);

                uint32_t const iSymbol = ELF64_R_SYM(paRelocs[j].r_info);
                if (RT_UNLIKELY(iSymbol >= cSymbols))
                    fRet = error(pszFile,
                                 "%#018" ELF_FMT_X64 "  %#018" ELF_FMT_X64 ": symbol index (%#x) out of bounds (%#x)\n",
                                 paRelocs[j].r_offset, paRelocs[j].r_info, iSymbol, cSymbols);
            }
        }
        else if (paShdrs[i].sh_type == SHT_REL)
            fRet = error(pszFile, "Section #%u '%s': Unexpected SHT_REL section\n", i, pszShNm);
        else if (paShdrs[i].sh_type == SHT_SYMTAB)
        {
            if (paShdrs[i].sh_entsize != sizeof(Elf64_Sym))
                fRet = error(pszFile, "Section #%u '%s': Unsupported symbol table entry size in : #%u (expected #%u)\n",
                             i, pszShNm, paShdrs[i].sh_entsize, sizeof(Elf64_Sym));
            Elf64_Xword const cSymbols = paShdrs[i].sh_size / paShdrs[i].sh_entsize;
            if (cSymbols * paShdrs[i].sh_entsize != paShdrs[i].sh_size)
                fRet = error(pszFile, "Section #%u '%s': Size not a multiple of entry size: %#" ELF_FMT_X64 " %% %#" ELF_FMT_X64 " = %#" ELF_FMT_X64 "\n",
                             i, pszShNm, paShdrs[i].sh_size, paShdrs[i].sh_entsize, paShdrs[i].sh_size % paShdrs[i].sh_entsize);
            if (cSymbols > UINT32_MAX)
                fRet = error(pszFile, "Section #%u '%s': too many symbols: %" ELF_FMT_X64 "\n",
                             i, pszShNm, paShdrs[i].sh_size, cSymbols);

            if (pElfStuff->iSymSh == UINT16_MAX)
            {
                pElfStuff->iSymSh    = (uint16_t)i;
                pElfStuff->paSymbols = (Elf64_Sym const *)&pbFile[paShdrs[i].sh_offset];
                pElfStuff->cSymbols  = cSymbols;

                if (paShdrs[i].sh_link != 0)
                {
                    /* Note! The symbol string table section header may not have been validated yet! */
                    Elf64_Shdr const *pStrTabShdr = &paShdrs[paShdrs[i].sh_link];
                    pElfStuff->iStrSh    = paShdrs[i].sh_link;
                    pElfStuff->pchStrTab = (const char *)&pbFile[pStrTabShdr->sh_offset];
                    pElfStuff->cbStrTab  = (size_t)pStrTabShdr->sh_size;
                }
                else
                    fRet = error(pszFile, "Section #%u '%s': String table link is out of bounds (%#x)\n",
                                 i, pszShNm, paShdrs[i].sh_link);
            }
            else
                fRet = error(pszFile, "Section #%u '%s': Found additonal symbol table, previous in #%u\n",
                             i, pszShNm, pElfStuff->iSymSh);
        }
    }
    return fRet;
}


static bool convertElfSectionsToSegDefsAndGrpDefs(POMFWRITER pThis, PCELFDETAILS pElfStuff)
{
    /*
     * Do the list of names pass.
     */
    uint16_t idxGrpFlat, idxGrpData;
    uint16_t idxClassCode, idxClassData, idxClassDwarf;
    if (   !omfWriter_LNamesBegin(pThis, true /*fAddZeroEntry*/)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("FLAT"), &idxGrpFlat)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("BS3DATA64_GROUP"), &idxGrpData)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("BS3CLASS64CODE"), &idxClassCode)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("FAR_DATA"), &idxClassData)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("DWARF"), &idxClassDwarf)
       )
        return false;

    bool              fHaveData = false;
    Elf64_Shdr const *pShdr     = &pElfStuff->paShdrs[1];
    Elf64_Half const  cSections = pElfStuff->pEhdr->e_shnum;
    for (Elf64_Half i = 1; i < cSections; i++, pShdr++)
    {
        const char *pszName = &pElfStuff->pchShStrTab[pShdr->sh_name];
        if (*pszName == '\0')
            return error(pThis->pszSrc, "Section #%u has an empty name!\n", i);

        switch (pShdr->sh_type)
        {
            case SHT_PROGBITS:
            case SHT_NOBITS:
                /* We drop a few sections we don't want:. */
                if (   strcmp(pszName, ".comment") != 0         /* compiler info  */
                    && strcmp(pszName, ".note.GNU-stack") != 0  /* some empty section for hinting the linker/whatever */
                    && strcmp(pszName, ".eh_frame") != 0        /* unwind / exception info */
                    )
                {
                    pThis->paSegments[i].iSegDef  = UINT16_MAX;
                    pThis->paSegments[i].iGrpDef  = UINT16_MAX;

                    /* Translate the name and determine group and class.
                       Note! We currently strip sub-sections. */
                    if (   strcmp(pszName, ".text") == 0
                        || strncmp(pszName, RT_STR_TUPLE(".text.")) == 0)
                    {
                        pszName = "BS3TEXT64";
                        pThis->paSegments[i].iGrpNm   = idxGrpFlat;
                        pThis->paSegments[i].iClassNm = idxClassCode;
                    }
                    else if (   strcmp(pszName, ".data") == 0
                             || strncmp(pszName, RT_STR_TUPLE(".data.")) == 0)
                    {
                        pszName = "BS3DATA64";
                        pThis->paSegments[i].iGrpNm   = idxGrpData;
                        pThis->paSegments[i].iClassNm = idxClassData;
                    }
                    else if (strcmp(pszName, ".bss") == 0)
                    {
                        pszName = "BS3BSS64";
                        pThis->paSegments[i].iGrpNm   = idxGrpData;
                        pThis->paSegments[i].iClassNm = idxClassData;
                    }
                    else if (   strcmp(pszName, ".rodata") == 0
                             || strncmp(pszName, RT_STR_TUPLE(".rodata.")) == 0)
                    {
                        pszName = "BS3DATA64CONST";
                        pThis->paSegments[i].iGrpNm   = idxGrpData;
                        pThis->paSegments[i].iClassNm = idxClassData;
                    }
                    else if (strncmp(pszName, RT_STR_TUPLE(".debug_")) == 0)
                    {
                        pThis->paSegments[i].iGrpNm   = UINT16_MAX;
                        pThis->paSegments[i].iClassNm = idxClassDwarf;
                    }
                    else
                    {
                        pThis->paSegments[i].iGrpNm   = idxGrpData;
                        pThis->paSegments[i].iClassNm = idxClassData;
                        error(pThis->pszSrc, "Unknown data (?) segment: '%s'\n", pszName);
                    }

                    /* Save the name. */
                    pThis->paSegments[i].pszName  = strdup(pszName);
                    if (!pThis->paSegments[i].pszName)
                        return error(pThis->pszSrc, "Out of memory!\n");

                    /* Add the section name. */
                    if (!omfWriter_LNamesAdd(pThis, pThis->paSegments[i].pszName, &pThis->paSegments[i].iSegNm))
                        return false;

                    fHaveData |= pThis->paSegments[i].iGrpNm == idxGrpData;
                    break;
                }
                RT_FALL_THRU();

            default:
                pThis->paSegments[i].iSegDef  = UINT16_MAX;
                pThis->paSegments[i].iGrpDef  = UINT16_MAX;
                pThis->paSegments[i].iSegNm   = UINT16_MAX;
                pThis->paSegments[i].iGrpNm   = UINT16_MAX;
                pThis->paSegments[i].iClassNm = UINT16_MAX;
                pThis->paSegments[i].pszName  = NULL;
                break;
        }
    }

    if (!omfWriter_LNamesEnd(pThis))
        return false;

    /*
     * Emit segment definitions.
     */
    uint16_t iSegDef = 1; /* Start counting at 1. */
    pShdr = &pElfStuff->paShdrs[1];
    for (Elf64_Half i = 1; i < cSections; i++, pShdr++)
    {
        if (pThis->paSegments[i].iSegNm == UINT16_MAX)
            continue;

        uint8_t bSegAttr = 0;

        /* The A field. */
        switch (pShdr->sh_addralign)
        {
            case 0:
            case 1:
                bSegAttr |= 1 << 5;
                break;
            case 2:
                bSegAttr |= 2 << 5;
                break;
            case 4:
                bSegAttr |= 5 << 5;
                break;
            case 8:
            case 16:
                bSegAttr |= 3 << 5;
                break;
            case 32:
            case 64:
            case 128:
            case 256:
                bSegAttr |= 4 << 5;
                break;
            default:
                bSegAttr |= 6 << 5; /* page aligned, pharlabs extension. */
                break;
        }

        /* The C field. */
        bSegAttr |= 2 << 2; /* public */

        /* The B field. We don't have 4GB segments, so leave it as zero. */

        /* The D field shall be set as we're doing USE32.  */
        bSegAttr |= 1;


        /* Done. */
        if (!omfWriter_SegDef(pThis, bSegAttr, (uint32_t)pShdr->sh_size,
                              pThis->paSegments[i].iSegNm,
                              pThis->paSegments[i].iClassNm))
            return false;
        pThis->paSegments[i].iSegDef = iSegDef++;
    }

    /*
     * Flat group definition (#1) - special, no members.
     */
    uint16_t iGrpDef = 1;
    if (   !omfWriter_GrpDefBegin(pThis, idxGrpFlat)
        || !omfWriter_GrpDefEnd(pThis))
        return false;
    for (uint16_t i = 0; i < cSections; i++)
        if (pThis->paSegments[i].iGrpNm == idxGrpFlat)
            pThis->paSegments[i].iGrpDef = iGrpDef;
    pThis->idxGrpFlat = iGrpDef++;

    /*
     * Data group definition (#2).
     */
    /** @todo do we need to consider missing segments and ordering? */
    uint16_t cGrpNms = 0;
    uint16_t aiGrpNms[2] = { 0, 0 }; /* Shut up, GCC. */
    if (fHaveData)
        aiGrpNms[cGrpNms++] = idxGrpData;
    for (uint32_t iGrpNm = 0; iGrpNm < cGrpNms; iGrpNm++)
    {
        if (!omfWriter_GrpDefBegin(pThis, aiGrpNms[iGrpNm]))
            return false;
        for (uint16_t i = 0; i < cSections; i++)
            if (pThis->paSegments[i].iGrpNm == aiGrpNms[iGrpNm])
            {
                pThis->paSegments[i].iGrpDef = iGrpDef;
                if (!omfWriter_GrpDefAddSegDef(pThis, pThis->paSegments[i].iSegDef))
                    return false;
            }
        if (!omfWriter_GrpDefEnd(pThis))
            return false;
        iGrpDef++;
    }

    return true;
}

static bool convertElfSymbolsToPubDefsAndExtDefs(POMFWRITER pThis, PCELFDETAILS pElfStuff)
{
    if (!pElfStuff->cSymbols)
        return true;

    /*
     * Process the symbols the first.
     */
    uint32_t cAbsSyms = 0;
    uint32_t cExtSyms = 0;
    uint32_t cPubSyms = 0;
    for (uint32_t iSeg = 0; iSeg < pThis->cSegments; iSeg++)
        pThis->paSegments[iSeg].cPubDefs = 0;

    uint32_t const          cSections = pElfStuff->pEhdr->e_shnum;
    uint32_t const          cSymbols  = pElfStuff->cSymbols;
    Elf64_Sym const * const paSymbols = pElfStuff->paSymbols;
    for (uint32_t iSym = 0; iSym < cSymbols; iSym++)
    {
        const uint8_t bBind      = ELF64_ST_BIND(paSymbols[iSym].st_info);
        const uint8_t bType      = ELF64_ST_TYPE(paSymbols[iSym].st_info);
        const char   *pszSymName = &pElfStuff->pchStrTab[paSymbols[iSym].st_name];
        if (   *pszSymName == '\0'
            && bType == STT_SECTION
            && paSymbols[iSym].st_shndx < cSections)
            pszSymName = &pElfStuff->pchShStrTab[pElfStuff->paShdrs[paSymbols[iSym].st_shndx].sh_name];

        pThis->paSymbols[iSym].enmType   = OMFSYMTYPE_IGNORED;
        pThis->paSymbols[iSym].idx       = UINT16_MAX;
        pThis->paSymbols[iSym].idxSegDef = UINT16_MAX;
        pThis->paSymbols[iSym].idxGrpDef = UINT16_MAX;

        uint32_t const idxSection = paSymbols[iSym].st_shndx;
        if (idxSection == SHN_UNDEF)
        {
            if (bBind == STB_GLOBAL)
            {
                pThis->paSymbols[iSym].enmType = OMFSYMTYPE_EXTDEF;
                cExtSyms++;
                if (*pszSymName == '\0')
                    return error(pThis->pszSrc, "External symbol #%u (%s) has an empty name.\n", iSym, pszSymName);
            }
            else if (bBind != STB_LOCAL || iSym != 0) /* Entry zero is usually a dummy. */
                return error(pThis->pszSrc, "Unsupported or invalid bind type %#x for undefined symbol #%u (%s)\n",
                             bBind, iSym, pszSymName);
        }
        else if (idxSection < cSections)
        {
            pThis->paSymbols[iSym].idxSegDef = pThis->paSegments[idxSection].iSegDef;
            pThis->paSymbols[iSym].idxGrpDef = pThis->paSegments[idxSection].iGrpDef;
            if (bBind == STB_GLOBAL)
            {
                pThis->paSymbols[iSym].enmType = OMFSYMTYPE_PUBDEF;
                pThis->paSegments[idxSection].cPubDefs++;
                cPubSyms++;
                if (bType == STT_SECTION)
                    return error(pThis->pszSrc, "Don't know how to export STT_SECTION symbol #%u (%s)\n", iSym, pszSymName);
                if (*pszSymName == '\0')
                    return error(pThis->pszSrc, "Public symbol #%u (%s) has an empty name.\n", iSym, pszSymName);
            }
            else if (bType == STT_SECTION)
                pThis->paSymbols[iSym].enmType = OMFSYMTYPE_SEGDEF;
            else
                pThis->paSymbols[iSym].enmType = OMFSYMTYPE_INTERNAL;
        }
        else if (idxSection == SHN_ABS)
        {
            if (bType != STT_FILE)
            {
                if (bBind == STB_GLOBAL)
                {
                    pThis->paSymbols[iSym].enmType   = OMFSYMTYPE_PUBDEF;
                    pThis->paSymbols[iSym].idxSegDef = 0;
                    pThis->paSymbols[iSym].idxGrpDef = 0;
                    cAbsSyms++;
                    if (*pszSymName == '\0')
                        return error(pThis->pszSrc, "Public absolute symbol #%u (%s) has an empty name.\n", iSym, pszSymName);
                }
                else
                    return error(pThis->pszSrc, "Unsupported or invalid bind type %#x for absolute symbol #%u (%s)\n",
                                 bBind, iSym, pszSymName);
            }
        }
        else if (idxSection == SHN_COMMON)
            return error(pThis->pszSrc, "Symbol #%u (%s) is in the unsupported 'common' section.\n", iSym, pszSymName);
        else
            return error(pThis->pszSrc, "Unsupported or invalid section number %#x for symbol #%u (%s)\n",
                         idxSection, iSym, pszSymName);
    }

    /*
     * Emit the PUBDEFs the first time around (see order of records in TIS spec).
     */
    uint16_t idxPubDef = 1;
    if (cPubSyms)
    {
        for (uint32_t iSeg = 0; iSeg < pThis->cSegments; iSeg++)
            if (pThis->paSegments[iSeg].cPubDefs > 0)
            {
                uint16_t const idxSegDef = pThis->paSegments[iSeg].iSegDef;
                if (!omfWriter_PubDefBegin(pThis, pThis->paSegments[iSeg].iGrpDef, idxSegDef))
                    return false;
                for (uint16_t iSym = 0; iSym < cSymbols; iSym++)
                    if (   pThis->paSymbols[iSym].idxSegDef == idxSegDef
                        && pThis->paSymbols[iSym].enmType   == OMFSYMTYPE_PUBDEF)
                    {
                        /* Underscore prefix all names not already underscored/mangled. */
                        const char *pszName = &pElfStuff->pchStrTab[paSymbols[iSym].st_name];
                        if (!omfWriter_PubDefAdd(pThis, paSymbols[iSym].st_value, pszName, pszName[0] != '_'))
                            return false;
                        pThis->paSymbols[iSym].idx = idxPubDef++;
                    }
                if (!omfWriter_PubDefEnd(pThis))
                    return false;
            }
    }

    if (cAbsSyms > 0)
    {
        if (!omfWriter_PubDefBegin(pThis, 0, 0))
            return false;
        for (uint16_t iSym = 0; iSym < cSymbols; iSym++)
            if (   pThis->paSymbols[iSym].idxSegDef == 0
                && pThis->paSymbols[iSym].enmType   == OMFSYMTYPE_PUBDEF)
            {
                /* Underscore prefix all names not already underscored/mangled. */
                const char *pszName = &pElfStuff->pchStrTab[paSymbols[iSym].st_name];
                if (!omfWriter_PubDefAdd(pThis, paSymbols[iSym].st_value, pszName, pszName[0] != '_'))
                    return false;
                pThis->paSymbols[iSym].idx = idxPubDef++;
            }
        if (!omfWriter_PubDefEnd(pThis))
            return false;
    }

    /*
     * Go over the symbol table and emit external definition records.
     */
    if (!omfWriter_ExtDefBegin(pThis))
        return false;
    uint16_t idxExtDef = 1;
    for (uint16_t iSym = 0; iSym < cSymbols; iSym++)
        if (pThis->paSymbols[iSym].enmType == OMFSYMTYPE_EXTDEF)
        {
            /* Underscore prefix all names not already underscored/mangled. */
            const char *pszName = &pElfStuff->pchStrTab[paSymbols[iSym].st_name];
            if (!omfWriter_ExtDefAdd(pThis, pszName, *pszName != '_'))
                return false;
            pThis->paSymbols[iSym].idx = idxExtDef++;
        }

    if (!omfWriter_ExtDefEnd(pThis))
        return false;

    return true;
}

/**
 * @callback_method_impl{FNRTSORTCMP, For Elf64_Rela tables.}
 */
static DECLCALLBACK(int) convertElfCompareRelA(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    Elf64_Rela const *pReloc1 = (Elf64_Rela const *)pvElement1;
    Elf64_Rela const *pReloc2 = (Elf64_Rela const *)pvElement2;
    if (pReloc1->r_offset < pReloc2->r_offset)
        return -1;
    if (pReloc1->r_offset > pReloc2->r_offset)
        return 1;
    RT_NOREF_PV(pvUser);
    return 0;
}

static bool convertElfSectionsToLeDataAndFixupps(POMFWRITER pThis, PCELFDETAILS pElfStuff, uint8_t const *pbFile, size_t cbFile)
{
    Elf64_Sym const    *paSymbols = pElfStuff->paSymbols;
    Elf64_Shdr const   *paShdrs   = pElfStuff->paShdrs;
    bool                fRet      = true;
    RT_NOREF_PV(cbFile);

    for (uint32_t i = 1; i < pThis->cSegments; i++)
    {
        if (pThis->paSegments[i].iSegDef == UINT16_MAX)
            continue;

        const char         *pszSegNm   = &pElfStuff->pchShStrTab[paShdrs[i].sh_name];
        bool const          fRelocs    = i + 1 < pThis->cSegments && paShdrs[i + 1].sh_type == SHT_RELA;
        uint32_t            cRelocs    = fRelocs ? paShdrs[i + 1].sh_size / sizeof(Elf64_Rela) : 0;
        Elf64_Rela const   *paRelocs   = fRelocs ? (Elf64_Rela *)&pbFile[paShdrs[i + 1].sh_offset] : NULL;
        Elf64_Xword         cbVirtData = paShdrs[i].sh_size;
        Elf64_Xword         cbData     = paShdrs[i].sh_type == SHT_NOBITS ? 0 : cbVirtData;
        uint8_t const      *pbData     = &pbFile[paShdrs[i].sh_offset];
        uint32_t            off        = 0;

        /* We sort fixups by r_offset in order to more easily split them into chunks. */
        RTSortShell((void *)paRelocs, cRelocs, sizeof(paRelocs[0]), convertElfCompareRelA, NULL);

        /* The OMF record size requires us to split larger sections up.  To make
           life simple, we fill zeros for unitialized (BSS) stuff. */
        const uint32_t cbMaxData = RT_MIN(OMF_MAX_RECORD_PAYLOAD - 1 - (pThis->paSegments[i].iSegDef >= 128) - 4 - 1, _1K);
        while (cbVirtData > 0)
        {
            /* Figure out how many bytes to put out in this chunk.  Must make sure
               fixups doesn't cross chunk boundraries.  ASSUMES sorted relocs. */
            uint32_t       cChunkRelocs = cRelocs;
            uint32_t       cbChunk      = cbVirtData;
            uint32_t       offEnd       = off + cbChunk;
            if (cbChunk > cbMaxData)
            {
                cbChunk      = cbMaxData;
                offEnd       = off + cbChunk;
                cChunkRelocs = 0;

                /* Quickly determin the reloc range. */
                while (   cChunkRelocs < cRelocs
                       && paRelocs[cChunkRelocs].r_offset < offEnd)
                    cChunkRelocs++;

                /* Ensure final reloc doesn't go beyond chunk. */
                while (   cChunkRelocs > 0
                       &&     paRelocs[cChunkRelocs - 1].r_offset
                            + ELF_AMD64_RELOC_SIZE(ELF64_R_TYPE(paRelocs[cChunkRelocs - 1].r_info))
                          > offEnd)
                {
                    uint32_t cbDrop = offEnd - paRelocs[cChunkRelocs - 1].r_offset;
                    cbChunk -= cbDrop;
                    offEnd  -= cbDrop;
                    cChunkRelocs--;
                }

                if (!cbVirtData)
                    return error(pThis->pszSrc, "Wtf? cbVirtData is zero!\n");
            }
            if (g_cVerbose >= 2)
                printf("debug: LEDATA off=%#x cb=%#x cRelocs=%#x sect=#%u segdef=%#x grpdef=%#x '%s'\n",
                       off, cbChunk, cRelocs, i, pThis->paSegments[i].iSegDef, pThis->paSegments[i].iGrpDef, pszSegNm);

            /*
             * We stash the bytes into the OMF writer record buffer, receiving a
             * pointer to the start of it so we can make adjustments if necessary.
             */
            uint8_t *pbCopy;
            if (!omfWriter_LEDataBeginEx(pThis, pThis->paSegments[i].iSegDef, off, cbChunk, cbData, pbData, &pbCopy))
                return false;

            /*
             * Convert fiuxps.
             */
            for (uint32_t iReloc = 0; iReloc < cChunkRelocs; iReloc++)
            {
                /* Get the OMF and ELF data for the symbol the reloc references. */
                uint32_t const          uType      = ELF64_R_TYPE(paRelocs[iReloc].r_info);
                uint32_t const          iSymbol    = ELF64_R_SYM(paRelocs[iReloc].r_info);
                Elf64_Sym const * const pElfSym    =        &paSymbols[iSymbol];
                POMFSYMBOL const        pOmfSym    = &pThis->paSymbols[iSymbol];
                const char * const      pszSymName = &pElfStuff->pchStrTab[pElfSym->st_name];

                /* Calc fixup location in the pending chunk and setup a flexible pointer to it. */
                uint16_t  offDataRec = (uint16_t)(paRelocs[iReloc].r_offset - off);
                RTPTRUNION uLoc;
                uLoc.pu8 = &pbCopy[offDataRec];

                /* OMF fixup data initialized with typical defaults. */
                bool        fSelfRel  = true;
                uint8_t     bLocation = OMF_FIX_LOC_32BIT_OFFSET;
                uint8_t     bFrame    = OMF_FIX_F_GRPDEF;
                uint16_t    idxFrame  = pThis->idxGrpFlat;
                uint8_t     bTarget;
                uint16_t    idxTarget;
                bool        fTargetDisp;
                uint32_t    offTargetDisp;
                switch (pOmfSym->enmType)
                {
                    case OMFSYMTYPE_INTERNAL:
                    case OMFSYMTYPE_PUBDEF:
                        bTarget       = OMF_FIX_T_SEGDEF;
                        idxTarget     = pOmfSym->idxSegDef;
                        fTargetDisp   = true;
                        offTargetDisp = pElfSym->st_value;
                        break;

                    case OMFSYMTYPE_SEGDEF:
                        bTarget       = OMF_FIX_T_SEGDEF_NO_DISP;
                        idxTarget     = pOmfSym->idxSegDef;
                        fTargetDisp   = false;
                        offTargetDisp = 0;
                        break;

                    case OMFSYMTYPE_EXTDEF:
                        bTarget       = OMF_FIX_T_EXTDEF_NO_DISP;
                        idxTarget     = pOmfSym->idx;
                        fTargetDisp   = false;
                        offTargetDisp = 0;
                        break;

                    default:
                        return error(pThis->pszSrc, "Relocation in segment #%u '%s' references ignored or invalid symbol (%s)\n",
                                     i, pszSegNm, pszSymName);
                }

                /* Do COFF relocation type conversion. */
                switch (uType)
                {
                    case R_X86_64_64:
                    {
                        int64_t iAddend = paRelocs[iReloc].r_addend;
                        if (iAddend > _1G || iAddend < -_1G)
                            fRet = error(pThis->pszSrc, "R_X86_64_64 with large addend (%" ELF_FMT_D64 ") at %#x in segment #%u '%s'\n",
                                         iAddend, paRelocs[iReloc].r_offset, i, pszSegNm);
                        *uLoc.pu64 = iAddend;
                        fSelfRel = false;
                        break;
                    }

                    case R_X86_64_32:
                    case R_X86_64_32S:  /* signed, unsigned, whatever. */
                        fSelfRel = false;
                        RT_FALL_THRU();
                    case R_X86_64_PC32:
                    case R_X86_64_PLT32: /* binutils commit 451875b4f976a527395e9303224c7881b65e12ed feature/regression. */
                    {
                        /* defaults are ok, just handle the addend. */
                        int32_t iAddend = paRelocs[iReloc].r_addend;
                        if (iAddend != paRelocs[iReloc].r_addend)
                            fRet = error(pThis->pszSrc, "R_X86_64_PC32 with large addend (%d) at %#x in segment #%u '%s'\n",
                                         iAddend, paRelocs[iReloc].r_offset, i, pszSegNm);
                        if (fSelfRel)
                            *uLoc.pu32 = iAddend + 4;
                        else
                            *uLoc.pu32 = iAddend;
                        break;
                    }

                    case R_X86_64_NONE:
                        continue; /* Ignore this one */

                    case R_X86_64_GOT32:
                    case R_X86_64_COPY:
                    case R_X86_64_GLOB_DAT:
                    case R_X86_64_JMP_SLOT:
                    case R_X86_64_RELATIVE:
                    case R_X86_64_GOTPCREL:
                    case R_X86_64_16:
                    case R_X86_64_PC16:
                    case R_X86_64_8:
                    case R_X86_64_PC8:
                    case R_X86_64_DTPMOD64:
                    case R_X86_64_DTPOFF64:
                    case R_X86_64_TPOFF64:
                    case R_X86_64_TLSGD:
                    case R_X86_64_TLSLD:
                    case R_X86_64_DTPOFF32:
                    case R_X86_64_GOTTPOFF:
                    case R_X86_64_TPOFF32:
                    default:
                        return error(pThis->pszSrc, "Unsupported fixup type %#x (%s) at rva=%#x in section #%u '%s' against '%s'\n",
                                     uType, g_apszElfAmd64RelTypes[uType], paRelocs[iReloc].r_offset, i, pszSegNm, pszSymName);
                }

                /* Add the fixup. */
                if (idxFrame == UINT16_MAX)
                    error(pThis->pszSrc, "idxFrame=UINT16_MAX for %s type=%s\n", pszSymName, g_apszElfAmd64RelTypes[uType]);
                fRet = omfWriter_LEDataAddFixup(pThis, offDataRec, fSelfRel, bLocation, bFrame, idxFrame,
                                                bTarget, idxTarget, fTargetDisp, offTargetDisp) && fRet;
            }

            /*
             * Write the LEDATA and associated FIXUPPs.
             */
            if (!omfWriter_LEDataEnd(pThis))
                return false;

            /*
             * Advance.
             */
            paRelocs   += cChunkRelocs;
            cRelocs    -= cChunkRelocs;
            if (cbData > cbChunk)
            {
                cbData -= cbChunk;
                pbData += cbChunk;
            }
            else
                cbData  = 0;
            off        += cbChunk;
            cbVirtData -= cbChunk;
        }
    }

    return fRet;
}


static bool convertElfToOmf(const char *pszFile, uint8_t const *pbFile, size_t cbFile, FILE *pDst)
{
    /*
     * Validate the source file a little.
     */
    ELFDETAILS ElfStuff;
    if (!validateElf(pszFile, pbFile, cbFile, &ElfStuff))
        return false;

    /*
     * Instantiate the OMF writer.
     */
    POMFWRITER pThis = omfWriter_Create(pszFile, ElfStuff.pEhdr->e_shnum, ElfStuff.cSymbols, pDst);
    if (!pThis)
        return false;

    /*
     * Write the OMF object file.
     */
    if (omfWriter_BeginModule(pThis, pszFile))
    {
        if (   convertElfSectionsToSegDefsAndGrpDefs(pThis, &ElfStuff)
            && convertElfSymbolsToPubDefsAndExtDefs(pThis, &ElfStuff)
            && omfWriter_LinkPassSeparator(pThis)
            && convertElfSectionsToLeDataAndFixupps(pThis, &ElfStuff, pbFile, cbFile)
            && omfWriter_EndModule(pThis) )
        {

            omfWriter_Destroy(pThis);
            return true;
        }
    }

    omfWriter_Destroy(pThis);
    return false;
}



/*********************************************************************************************************************************
*   COFF -> OMF Converter                                                                                                        *
*********************************************************************************************************************************/

/** AMD64 relocation type names for (Microsoft) COFF. */
static const char * const g_apszCoffAmd64RelTypes[] =
{
    "ABSOLUTE",
    "ADDR64",
    "ADDR32",
    "ADDR32NB",
    "REL32",
    "REL32_1",
    "REL32_2",
    "REL32_3",
    "REL32_4",
    "REL32_5",
    "SECTION",
    "SECREL",
    "SECREL7",
    "TOKEN",
    "SREL32",
    "PAIR",
    "SSPAN32"
};

/** AMD64 relocation type sizes for (Microsoft) COFF. */
static uint8_t const g_acbCoffAmd64RelTypes[] =
{
    8, /* ABSOLUTE */
    8, /* ADDR64   */
    4, /* ADDR32   */
    4, /* ADDR32NB */
    4, /* REL32    */
    4, /* REL32_1  */
    4, /* REL32_2  */
    4, /* REL32_3  */
    4, /* REL32_4  */
    4, /* REL32_5  */
    2, /* SECTION  */
    4, /* SECREL   */
    1, /* SECREL7  */
    0, /* TOKEN    */
    4, /* SREL32   */
    0, /* PAIR     */
    4, /* SSPAN32  */
};

/** Macro for getting the size of a AMD64 COFF relocation. */
#define COFF_AMD64_RELOC_SIZE(a_Type) ( (a_Type) < RT_ELEMENTS(g_acbCoffAmd64RelTypes) ? g_acbCoffAmd64RelTypes[(a_Type)] : 1)


static const char *coffGetSymbolName(PCIMAGE_SYMBOL pSym, const char *pchStrTab, uint32_t cbStrTab, char pszShortName[16])
{
    if (pSym->N.Name.Short != 0)
    {
        memcpy(pszShortName, pSym->N.ShortName, 8);
        pszShortName[8] = '\0';
        return pszShortName;
    }
    if (pSym->N.Name.Long < cbStrTab)
    {
        uint32_t const cbLeft = cbStrTab - pSym->N.Name.Long;
        const char    *pszRet = pchStrTab + pSym->N.Name.Long;
        if (memchr(pszRet, '\0', cbLeft) != NULL)
            return pszRet;
    }
    error("<null>",  "Invalid string table index %#x!\n", pSym->N.Name.Long);
    return "Invalid Symbol Table Entry";
}

static bool validateCoff(const char *pszFile, uint8_t const *pbFile, size_t cbFile)
{
    /*
     * Validate the header and our other expectations.
     */
    PIMAGE_FILE_HEADER pHdr = (PIMAGE_FILE_HEADER)pbFile;
    if (pHdr->Machine != IMAGE_FILE_MACHINE_AMD64)
        return error(pszFile, "Expected IMAGE_FILE_MACHINE_AMD64 not %#x\n", pHdr->Machine);
    if (pHdr->SizeOfOptionalHeader != 0)
        return error(pszFile, "Expected SizeOfOptionalHeader to be zero, not %#x\n", pHdr->SizeOfOptionalHeader);
    if (pHdr->NumberOfSections == 0)
        return error(pszFile, "Expected NumberOfSections to be non-zero\n");
    uint32_t const cbHeaders = pHdr->NumberOfSections * sizeof(IMAGE_SECTION_HEADER) + sizeof(*pHdr);
    if (cbHeaders > cbFile)
        return error(pszFile, "Section table goes beyond the end of the of the file (cSections=%#x)\n", pHdr->NumberOfSections);
    if (pHdr->NumberOfSymbols)
    {
        if (   pHdr->PointerToSymbolTable >= cbFile
            || pHdr->NumberOfSymbols * (uint64_t)IMAGE_SIZE_OF_SYMBOL > cbFile)
            return error(pszFile, "Symbol table goes beyond the end of the of the file (cSyms=%#x, offFile=%#x)\n",
                         pHdr->NumberOfSymbols, pHdr->PointerToSymbolTable);
    }

    return true;
}


static bool convertCoffSectionsToSegDefsAndGrpDefs(POMFWRITER pThis, PCIMAGE_SECTION_HEADER paShdrs, uint16_t cSections)
{
    /*
     * Do the list of names pass.
     */
    uint16_t idxGrpFlat, idxGrpData;
    uint16_t idxClassCode, idxClassData, idxClassDebugSymbols, idxClassDebugTypes;
    if (   !omfWriter_LNamesBegin(pThis, true /*fAddZeroEntry*/)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("FLAT"), &idxGrpFlat)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("BS3DATA64_GROUP"), &idxGrpData)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("BS3CLASS64CODE"), &idxClassCode)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("FAR_DATA"), &idxClassData)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("DEBSYM"), &idxClassDebugSymbols)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("DEBTYP"), &idxClassDebugTypes)
       )
        return false;

    bool fHaveData = false;
    for (uint16_t i = 0; i < cSections; i++)
    {
        /* Copy the name and terminate it. */
        char szName[32];
        memcpy(szName, paShdrs[i].Name, sizeof(paShdrs[i].Name));
        unsigned cchName = sizeof(paShdrs[i].Name);
        while (cchName > 0 && RT_C_IS_SPACE(szName[cchName - 1]))
            cchName--;
        if (cchName == 0)
            return error(pThis->pszSrc, "Section #%u has an empty name!\n", i);
        szName[cchName] = '\0';

        if (   (paShdrs[i].Characteristics & (IMAGE_SCN_LNK_REMOVE | IMAGE_SCN_LNK_INFO))
            || strcmp(szName, ".pdata") == 0 /* Exception stuff, I think, so discard it. */
            || strcmp(szName, ".xdata") == 0 /* Ditto. */ )
        {
            pThis->paSegments[i].iSegDef  = UINT16_MAX;
            pThis->paSegments[i].iGrpDef  = UINT16_MAX;
            pThis->paSegments[i].iSegNm   = UINT16_MAX;
            pThis->paSegments[i].iGrpNm   = UINT16_MAX;
            pThis->paSegments[i].iClassNm = UINT16_MAX;
            pThis->paSegments[i].pszName  = NULL;
        }
        else
        {
            /* Translate the name, group and class. */
            if (   strcmp(szName, ".text") == 0
                || strcmp(szName, ".text$mn") == 0 /* Seen first in VC++ 14.1 (could be older). */)
            {
                strcpy(szName, "BS3TEXT64");
                pThis->paSegments[i].iGrpNm   = idxGrpFlat;
                pThis->paSegments[i].iClassNm = idxClassCode;
            }
            else if (strcmp(szName, ".data") == 0)
            {
                strcpy(szName, "BS3DATA64");
                pThis->paSegments[i].iGrpNm   = idxGrpData;
                pThis->paSegments[i].iClassNm = idxClassData;
            }
            else if (strcmp(szName, ".bss") == 0)
            {
                strcpy(szName, "BS3BSS64");
                pThis->paSegments[i].iGrpNm   = idxGrpData;
                pThis->paSegments[i].iClassNm = idxClassData;
            }
            else if (strcmp(szName, ".rdata") == 0)
            {
                strcpy(szName, "BS3DATA64CONST");
                pThis->paSegments[i].iGrpNm   = idxGrpData;
                pThis->paSegments[i].iClassNm = idxClassData;
            }
            else if (strcmp(szName, ".debug$S") == 0)
            {
                strcpy(szName, "$$SYMBOLS");
                pThis->paSegments[i].iGrpNm   = UINT16_MAX;
                pThis->paSegments[i].iClassNm = idxClassDebugSymbols;
            }
            else if (strcmp(szName, ".debug$T") == 0)
            {
                strcpy(szName, "$$TYPES");
                pThis->paSegments[i].iGrpNm   = UINT16_MAX;
                pThis->paSegments[i].iClassNm = idxClassDebugTypes;
            }
            else if (paShdrs[i].Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE))
            {
                pThis->paSegments[i].iGrpNm   = idxGrpFlat;
                pThis->paSegments[i].iClassNm = idxClassCode;
                error(pThis->pszSrc, "Unknown code segment: '%s'\n", szName);
            }
            else
            {
                pThis->paSegments[i].iGrpNm = idxGrpData;
                pThis->paSegments[i].iClassNm = idxClassData;
                error(pThis->pszSrc, "Unknown data (?) segment: '%s'\n", szName);
            }

            /* Save the name. */
            pThis->paSegments[i].pszName = strdup(szName);
            if (!pThis->paSegments[i].pszName)
                return error(pThis->pszSrc, "Out of memory!\n");

            /* Add the section name. */
            if (!omfWriter_LNamesAdd(pThis, pThis->paSegments[i].pszName, &pThis->paSegments[i].iSegNm))
                return false;

            fHaveData |= pThis->paSegments[i].iGrpNm == idxGrpData;
        }
    }

    if (!omfWriter_LNamesEnd(pThis))
        return false;

    /*
     * Emit segment definitions.
     */
    uint16_t iSegDef = 1; /* Start counting at 1. */
    for (uint16_t i = 0; i < cSections; i++)
    {
        if (pThis->paSegments[i].iSegDef == UINT16_MAX)
            continue;

        uint8_t bSegAttr = 0;

        /* The A field. */
        switch (paShdrs[i].Characteristics & IMAGE_SCN_ALIGN_MASK)
        {
            default:
            case IMAGE_SCN_ALIGN_1BYTES:
                bSegAttr |= 1 << 5;
                break;
            case IMAGE_SCN_ALIGN_2BYTES:
                bSegAttr |= 2 << 5;
                break;
            case IMAGE_SCN_ALIGN_4BYTES:
                bSegAttr |= 5 << 5;
                break;
            case IMAGE_SCN_ALIGN_8BYTES:
            case IMAGE_SCN_ALIGN_16BYTES:
                bSegAttr |= 3 << 5;
                break;
            case IMAGE_SCN_ALIGN_32BYTES:
            case IMAGE_SCN_ALIGN_64BYTES:
            case IMAGE_SCN_ALIGN_128BYTES:
            case IMAGE_SCN_ALIGN_256BYTES:
                bSegAttr |= 4 << 5;
                break;
            case IMAGE_SCN_ALIGN_512BYTES:
            case IMAGE_SCN_ALIGN_1024BYTES:
            case IMAGE_SCN_ALIGN_2048BYTES:
            case IMAGE_SCN_ALIGN_4096BYTES:
            case IMAGE_SCN_ALIGN_8192BYTES:
                bSegAttr |= 6 << 5; /* page aligned, pharlabs extension. */
                break;
        }

        /* The C field. */
        bSegAttr |= 2 << 2; /* public */

        /* The B field. We don't have 4GB segments, so leave it as zero. */

        /* The D field shall be set as we're doing USE32.  */
        bSegAttr |= 1;


        /* Done. */
        if (!omfWriter_SegDef(pThis, bSegAttr, paShdrs[i].SizeOfRawData,
                              pThis->paSegments[i].iSegNm,
                              pThis->paSegments[i].iClassNm))
            return false;
        pThis->paSegments[i].iSegDef = iSegDef++;
    }

    /*
     * Flat group definition (#1) - special, no members.
     */
    uint16_t iGrpDef = 1;
    if (   !omfWriter_GrpDefBegin(pThis, idxGrpFlat)
        || !omfWriter_GrpDefEnd(pThis))
        return false;
    for (uint16_t i = 0; i < cSections; i++)
        if (pThis->paSegments[i].iGrpNm == idxGrpFlat)
            pThis->paSegments[i].iGrpDef = iGrpDef;
    pThis->idxGrpFlat = iGrpDef++;

    /*
     * Data group definition (#2).
     */
    /** @todo do we need to consider missing segments and ordering? */
    uint16_t cGrpNms = 0;
    uint16_t aiGrpNms[2] = { 0, 0 }; /* Shut up, GCC. */
    if (fHaveData)
        aiGrpNms[cGrpNms++] = idxGrpData;
    for (uint32_t iGrpNm = 0; iGrpNm < cGrpNms; iGrpNm++)
    {
        if (!omfWriter_GrpDefBegin(pThis, aiGrpNms[iGrpNm]))
            return false;
        for (uint16_t i = 0; i < cSections; i++)
            if (pThis->paSegments[i].iGrpNm == aiGrpNms[iGrpNm])
            {
                pThis->paSegments[i].iGrpDef = iGrpDef;
                if (!omfWriter_GrpDefAddSegDef(pThis, pThis->paSegments[i].iSegDef))
                    return false;
            }
        if (!omfWriter_GrpDefEnd(pThis))
            return false;
        iGrpDef++;
    }

    return true;
}

/**
 * This is for matching STATIC symbols with value 0 against the section name,
 * to see if it's a section reference or symbol at offset 0 reference.
 *
 * @returns true / false.
 * @param   pszSymbol       The symbol name.
 * @param   pachSectName8   The section name (8-bytes).
 */
static bool isCoffSymbolMatchingSectionName(const char *pszSymbol, uint8_t const pachSectName8[8])
{
    uint32_t off = 0;
    char ch;
    while (off < 8 && (ch = pszSymbol[off]) != '\0')
    {
        if (ch != pachSectName8[off])
            return false;
        off++;
    }
    while (off < 8)
    {
        if (!RT_C_IS_SPACE((ch = pachSectName8[off])))
            return ch == '\0';
        off++;
    }
    return true;
}

static bool convertCoffSymbolsToPubDefsAndExtDefs(POMFWRITER pThis, PCIMAGE_SYMBOL paSymbols, uint16_t cSymbols,
                                                  const char *pchStrTab, PCIMAGE_SECTION_HEADER paShdrs)
{

    if (!cSymbols)
        return true;
    uint32_t const  cbStrTab = *(uint32_t const *)pchStrTab;
    char            szShort[16];

    /*
     * Process the symbols the first.
     */
    uint32_t iSymImageBase = UINT32_MAX;
    uint32_t cAbsSyms = 0;
    uint32_t cExtSyms = 0;
    uint32_t cPubSyms = 0;
    for (uint32_t iSeg = 0; iSeg < pThis->cSegments; iSeg++)
        pThis->paSegments[iSeg].cPubDefs = 0;

    for (uint16_t iSym = 0; iSym < cSymbols; iSym++)
    {
        const char *pszSymName = coffGetSymbolName(&paSymbols[iSym], pchStrTab, cbStrTab, szShort);

        pThis->paSymbols[iSym].enmType   = OMFSYMTYPE_IGNORED;
        pThis->paSymbols[iSym].idx       = UINT16_MAX;
        pThis->paSymbols[iSym].idxSegDef = UINT16_MAX;
        pThis->paSymbols[iSym].idxGrpDef = UINT16_MAX;

        int16_t const idxSection = paSymbols[iSym].SectionNumber;
        if (   (idxSection >= 1 && idxSection <= (int32_t)pThis->cSegments)
            || idxSection == IMAGE_SYM_ABSOLUTE)
        {
            switch (paSymbols[iSym].StorageClass)
            {
                case IMAGE_SYM_CLASS_EXTERNAL:
                    if (idxSection != IMAGE_SYM_ABSOLUTE)
                    {
                        if (pThis->paSegments[idxSection - 1].iSegDef != UINT16_MAX)
                        {
                            pThis->paSymbols[iSym].enmType   = OMFSYMTYPE_PUBDEF;
                            pThis->paSymbols[iSym].idxSegDef = pThis->paSegments[idxSection - 1].iSegDef;
                            pThis->paSymbols[iSym].idxGrpDef = pThis->paSegments[idxSection - 1].iGrpDef;
                            pThis->paSegments[idxSection - 1].cPubDefs++;
                            cPubSyms++;
                        }
                    }
                    else
                    {
                        pThis->paSymbols[iSym].enmType   = OMFSYMTYPE_PUBDEF;
                        pThis->paSymbols[iSym].idxSegDef = 0;
                        pThis->paSymbols[iSym].idxGrpDef = 0;
                        cAbsSyms++;
                    }
                    break;

                case IMAGE_SYM_CLASS_STATIC:
                    if (   paSymbols[iSym].Value == 0
                        && idxSection != IMAGE_SYM_ABSOLUTE
                        && isCoffSymbolMatchingSectionName(pszSymName, paShdrs[idxSection - 1].Name) )
                    {
                        pThis->paSymbols[iSym].enmType   = OMFSYMTYPE_SEGDEF;
                        pThis->paSymbols[iSym].idxSegDef = pThis->paSegments[idxSection - 1].iSegDef;
                        pThis->paSymbols[iSym].idxGrpDef = pThis->paSegments[idxSection - 1].iGrpDef;
                        break;
                    }
                    RT_FALL_THRU();

                case IMAGE_SYM_CLASS_END_OF_FUNCTION:
                case IMAGE_SYM_CLASS_AUTOMATIC:
                case IMAGE_SYM_CLASS_REGISTER:
                case IMAGE_SYM_CLASS_LABEL:
                case IMAGE_SYM_CLASS_MEMBER_OF_STRUCT:
                case IMAGE_SYM_CLASS_ARGUMENT:
                case IMAGE_SYM_CLASS_STRUCT_TAG:
                case IMAGE_SYM_CLASS_MEMBER_OF_UNION:
                case IMAGE_SYM_CLASS_UNION_TAG:
                case IMAGE_SYM_CLASS_TYPE_DEFINITION:
                case IMAGE_SYM_CLASS_ENUM_TAG:
                case IMAGE_SYM_CLASS_MEMBER_OF_ENUM:
                case IMAGE_SYM_CLASS_REGISTER_PARAM:
                case IMAGE_SYM_CLASS_BIT_FIELD:
                case IMAGE_SYM_CLASS_BLOCK:
                case IMAGE_SYM_CLASS_FUNCTION:
                case IMAGE_SYM_CLASS_END_OF_STRUCT:
                case IMAGE_SYM_CLASS_FILE:
                    pThis->paSymbols[iSym].enmType = OMFSYMTYPE_INTERNAL;
                    if (idxSection != IMAGE_SYM_ABSOLUTE)
                    {
                        pThis->paSymbols[iSym].idxSegDef = pThis->paSegments[idxSection - 1].iSegDef;
                        pThis->paSymbols[iSym].idxGrpDef = pThis->paSegments[idxSection - 1].iGrpDef;
                    }
                    else
                    {
                        pThis->paSymbols[iSym].idxSegDef = 0;
                        pThis->paSymbols[iSym].idxGrpDef = 0;
                    }
                    break;

                case IMAGE_SYM_CLASS_SECTION:
                case IMAGE_SYM_CLASS_EXTERNAL_DEF:
                case IMAGE_SYM_CLASS_NULL:
                case IMAGE_SYM_CLASS_UNDEFINED_LABEL:
                case IMAGE_SYM_CLASS_UNDEFINED_STATIC:
                case IMAGE_SYM_CLASS_CLR_TOKEN:
                case IMAGE_SYM_CLASS_FAR_EXTERNAL:
                case IMAGE_SYM_CLASS_WEAK_EXTERNAL:
                    return error(pThis->pszSrc, "Unsupported storage class value %#x for symbol #%u (%s)\n",
                                 paSymbols[iSym].StorageClass, iSym, pszSymName);

                default:
                    return error(pThis->pszSrc, "Unknown storage class value %#x for symbol #%u (%s)\n",
                                 paSymbols[iSym].StorageClass, iSym, pszSymName);
            }
        }
        else if (idxSection == IMAGE_SYM_UNDEFINED)
        {
            if (   paSymbols[iSym].StorageClass == IMAGE_SYM_CLASS_EXTERNAL
                || paSymbols[iSym].StorageClass == IMAGE_SYM_CLASS_EXTERNAL_DEF)
            {
                pThis->paSymbols[iSym].enmType = OMFSYMTYPE_EXTDEF;
                cExtSyms++;
                if (iSymImageBase == UINT32_MAX && strcmp(pszSymName, "__ImageBase") == 0)
                    iSymImageBase = iSym;
            }
            else
                return error(pThis->pszSrc, "Unknown/unknown storage class value %#x for undefined symbol #%u (%s)\n",
                             paSymbols[iSym].StorageClass, iSym, pszSymName);
        }
        else if (idxSection != IMAGE_SYM_DEBUG)
            return error(pThis->pszSrc, "Invalid section number %#x for symbol #%u (%s)\n", idxSection, iSym, pszSymName);

        /* Skip AUX symbols. */
        uint8_t cAuxSyms = paSymbols[iSym].NumberOfAuxSymbols;
        while (cAuxSyms-- > 0)
        {
            iSym++;
            pThis->paSymbols[iSym].enmType = OMFSYMTYPE_INVALID;
            pThis->paSymbols[iSym].idx     = UINT16_MAX;
        }
    }

    /*
     * Emit the PUBDEFs the first time around (see order of records in TIS spec).
     */
    uint16_t idxPubDef = 1;
    if (cPubSyms)
    {
        for (uint32_t iSeg = 0; iSeg < pThis->cSegments; iSeg++)
            if (pThis->paSegments[iSeg].cPubDefs > 0)
            {
                uint16_t const idxSegDef = pThis->paSegments[iSeg].iSegDef;
                if (!omfWriter_PubDefBegin(pThis, pThis->paSegments[iSeg].iGrpDef, idxSegDef))
                    return false;
                for (uint16_t iSym = 0; iSym < cSymbols; iSym++)
                    if (   pThis->paSymbols[iSym].idxSegDef == idxSegDef
                        && pThis->paSymbols[iSym].enmType   == OMFSYMTYPE_PUBDEF)
                    {
                        /* Underscore prefix all symbols not already underscored or mangled. */
                        const char *pszName = coffGetSymbolName(&paSymbols[iSym], pchStrTab, cbStrTab, szShort);
                        if (!omfWriter_PubDefAdd(pThis, paSymbols[iSym].Value, pszName, pszName[0] != '_' && pszName[0] != '?'))
                            return false;
                        pThis->paSymbols[iSym].idx = idxPubDef++;
                    }
                if (!omfWriter_PubDefEnd(pThis))
                    return false;
            }
    }

    if (cAbsSyms > 0)
    {
        if (!omfWriter_PubDefBegin(pThis, 0, 0))
            return false;
        for (uint16_t iSym = 0; iSym < cSymbols; iSym++)
            if (   pThis->paSymbols[iSym].idxSegDef == 0
                && pThis->paSymbols[iSym].enmType   == OMFSYMTYPE_PUBDEF)
            {
                /* Underscore prefix all symbols not already underscored or mangled. */
                const char *pszName = coffGetSymbolName(&paSymbols[iSym], pchStrTab, cbStrTab, szShort);
                if (!omfWriter_PubDefAdd(pThis, paSymbols[iSym].Value, pszName, pszName[0] != '_' && pszName[0] != '?') )
                    return false;
                pThis->paSymbols[iSym].idx = idxPubDef++;
            }
        if (!omfWriter_PubDefEnd(pThis))
            return false;
    }

    /*
     * Go over the symbol table and emit external definition records.
     */
    if (!omfWriter_ExtDefBegin(pThis))
        return false;
    uint16_t idxExtDef = 1;
    for (uint16_t iSym = 0; iSym < cSymbols; iSym++)
        if (pThis->paSymbols[iSym].enmType == OMFSYMTYPE_EXTDEF)
        {
            /* Underscore prefix all symbols not already underscored or mangled. */
            const char *pszName = coffGetSymbolName(&paSymbols[iSym], pchStrTab, cbStrTab, szShort);
            if (!omfWriter_ExtDefAdd(pThis, pszName, pszName[0] != '_' && pszName[0] != '?'))
                return false;
            pThis->paSymbols[iSym].idx = idxExtDef++;
        }

    /* Always add an __ImageBase reference, in case we need it to deal with ADDR32NB fixups. */
    /** @todo maybe we don't actually need this and could use FLAT instead? */
    if (iSymImageBase != UINT32_MAX)
        pThis->idxExtImageBase = pThis->paSymbols[iSymImageBase].idx;
    else if (omfWriter_ExtDefAdd(pThis, "__ImageBase", false /*fPrependUnderscore*/))
        pThis->idxExtImageBase = idxExtDef;
    else
        return false;

    if (!omfWriter_ExtDefEnd(pThis))
        return false;

    return true;
}


static bool convertCoffSectionsToLeDataAndFixupps(POMFWRITER pThis, uint8_t const *pbFile, size_t cbFile,
                                                  PCIMAGE_SECTION_HEADER paShdrs, uint16_t cSections,
                                                  PCIMAGE_SYMBOL paSymbols, uint16_t cSymbols, const char *pchStrTab)
{
    RT_NOREF_PV(cbFile);
    RT_NOREF_PV(cSections);
    RT_NOREF_PV(cSymbols);

    uint32_t const  cbStrTab = *(uint32_t const *)pchStrTab;
    bool            fRet     = true;
    for (uint32_t i = 0; i < pThis->cSegments; i++)
    {
        if (pThis->paSegments[i].iSegDef == UINT16_MAX)
            continue;

        char                szShortName[16];
        const char         *pszSegNm   = pThis->paSegments[i].pszName;
        uint16_t            cRelocs    = paShdrs[i].NumberOfRelocations;
        PCIMAGE_RELOCATION  paRelocs   = (PCIMAGE_RELOCATION)&pbFile[paShdrs[i].PointerToRelocations];
        uint32_t            cbVirtData = paShdrs[i].SizeOfRawData;
        uint32_t            cbData     = paShdrs[i].Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA ? 0 : cbVirtData;
        uint8_t const      *pbData     = &pbFile[paShdrs[i].PointerToRawData];
        uint32_t            off        = 0;

        /* Check that the relocations are sorted and within the section. */
        for (uint32_t iReloc = 1; iReloc < cRelocs; iReloc++)
            if (paRelocs[iReloc - 1].u.VirtualAddress >= paRelocs[iReloc].u.VirtualAddress)
                return error(pThis->pszSrc, "Section #%u (%s) relocations aren't sorted\n", i, pszSegNm);
        if (   cRelocs > 0
            &&    paRelocs[cRelocs - 1].u.VirtualAddress - paShdrs[i].VirtualAddress
               +  COFF_AMD64_RELOC_SIZE(paRelocs[cRelocs - 1].Type) > cbVirtData)
            return error(pThis->pszSrc,
                         "Section #%u (%s) relocations beyond section data! cbVirtData=%#x RvaFix=%#x RVASeg=%#x type=%#x\n",
                         i, pszSegNm, cbVirtData, paRelocs[cRelocs - 1].u.VirtualAddress, paShdrs[i].VirtualAddress,
                         paRelocs[cRelocs - 1].Type);

        /* The OMF record size requires us to split larger sections up.  To make
           life simple, we fill zeros for unitialized (BSS) stuff. */
        const uint32_t cbMaxData = RT_MIN(OMF_MAX_RECORD_PAYLOAD - 1 - (pThis->paSegments[i].iSegDef >= 128) - 4 - 1, _1K);
        while (cbVirtData > 0)
        {
            /* Figure out how many bytes to put out in this chunk.  Must make sure
               fixups doesn't cross chunk boundraries.  ASSUMES sorted relocs. */
            uint32_t       cChunkRelocs = cRelocs;
            uint32_t       cbChunk      = cbVirtData;
            uint32_t       uRvaEnd      = paShdrs[i].VirtualAddress + off + cbChunk;
            if (cbChunk > cbMaxData)
            {
                cbChunk      = cbMaxData;
                uRvaEnd      = paShdrs[i].VirtualAddress + off + cbChunk;
                cChunkRelocs = 0;

                /* Quickly determin the reloc range. */
                while (   cChunkRelocs < cRelocs
                       && paRelocs[cChunkRelocs].u.VirtualAddress < uRvaEnd)
                    cChunkRelocs++;

                /* Ensure final reloc doesn't go beyond chunk. */
                while (   cChunkRelocs > 0
                       &&   paRelocs[cChunkRelocs - 1].u.VirtualAddress + COFF_AMD64_RELOC_SIZE(paRelocs[cChunkRelocs - 1].Type)
                          > uRvaEnd)
                {
                    uint32_t cbDrop = uRvaEnd - paRelocs[cChunkRelocs - 1].u.VirtualAddress;
                    cbChunk -= cbDrop;
                    uRvaEnd -= cbDrop;
                    cChunkRelocs--;
                }

                if (!cbVirtData)
                    return error(pThis->pszSrc, "Wtf? cbVirtData is zero!\n");
            }

            /*
             * We stash the bytes into the OMF writer record buffer, receiving a
             * pointer to the start of it so we can make adjustments if necessary.
             */
            uint8_t *pbCopy;
            if (!omfWriter_LEDataBeginEx(pThis, pThis->paSegments[i].iSegDef, off, cbChunk, cbData, pbData, &pbCopy))
                return false;

            /*
             * Convert fiuxps.
             */
            uint32_t const uRvaChunk = paShdrs[i].VirtualAddress + off;
            for (uint32_t iReloc = 0; iReloc < cChunkRelocs; iReloc++)
            {
                /* Get the OMF and COFF data for the symbol the reloc references. */
                if (paRelocs[iReloc].SymbolTableIndex >= pThis->cSymbols)
                    return error(pThis->pszSrc, "Relocation symtab index (%#x) is out of range in segment #%u '%s'\n",
                                 paRelocs[iReloc].SymbolTableIndex, i, pszSegNm);
                PCIMAGE_SYMBOL pCoffSym =        &paSymbols[paRelocs[iReloc].SymbolTableIndex];
                POMFSYMBOL     pOmfSym  = &pThis->paSymbols[paRelocs[iReloc].SymbolTableIndex];

                /* Calc fixup location in the pending chunk and setup a flexible pointer to it. */
                uint16_t  offDataRec = (uint16_t)(paRelocs[iReloc].u.VirtualAddress - uRvaChunk);
                RTPTRUNION uLoc;
                uLoc.pu8 = &pbCopy[offDataRec];

                /* OMF fixup data initialized with typical defaults. */
                bool        fSelfRel  = true;
                uint8_t     bLocation = OMF_FIX_LOC_32BIT_OFFSET;
                uint8_t     bFrame    = OMF_FIX_F_GRPDEF;
                uint16_t    idxFrame  = pThis->idxGrpFlat;
                uint8_t     bTarget;
                uint16_t    idxTarget;
                bool        fTargetDisp;
                uint32_t    offTargetDisp;
                switch (pOmfSym->enmType)
                {
                    case OMFSYMTYPE_INTERNAL:
                    case OMFSYMTYPE_PUBDEF:
                        bTarget       = OMF_FIX_T_SEGDEF;
                        idxTarget     = pOmfSym->idxSegDef;
                        fTargetDisp   = true;
                        offTargetDisp = pCoffSym->Value;
                        break;

                    case OMFSYMTYPE_SEGDEF:
                        bTarget       = OMF_FIX_T_SEGDEF_NO_DISP;
                        idxTarget     = pOmfSym->idxSegDef;
                        fTargetDisp   = false;
                        offTargetDisp = 0;
                        break;

                    case OMFSYMTYPE_EXTDEF:
                        bTarget       = OMF_FIX_T_EXTDEF_NO_DISP;
                        idxTarget     = pOmfSym->idx;
                        fTargetDisp   = false;
                        offTargetDisp = 0;
                        break;

                    default:
                        return error(pThis->pszSrc, "Relocation in segment #%u '%s' references ignored or invalid symbol (%s)\n",
                                     i, pszSegNm, coffGetSymbolName(pCoffSym, pchStrTab, cbStrTab, szShortName));
                }

                /* Do COFF relocation type conversion. */
                switch (paRelocs[iReloc].Type)
                {
                    case IMAGE_REL_AMD64_ADDR64:
                    {
                        uint64_t uAddend = *uLoc.pu64;
                        if (uAddend > _1G)
                            fRet = error(pThis->pszSrc, "ADDR64 with large addend (%#llx) at %#x in segment #%u '%s'\n",
                                         uAddend, paRelocs[iReloc].u.VirtualAddress, i, pszSegNm);
                        fSelfRel = false;
                        break;
                    }

                    case IMAGE_REL_AMD64_REL32_1:
                    case IMAGE_REL_AMD64_REL32_2:
                    case IMAGE_REL_AMD64_REL32_3:
                    case IMAGE_REL_AMD64_REL32_4:
                    case IMAGE_REL_AMD64_REL32_5:
                        /** @todo Check whether OMF read addends from the data or relies on the
                         *        displacement. Also, check what it's relative to. */
                        *uLoc.pu32 -= paRelocs[iReloc].Type - IMAGE_REL_AMD64_REL32;
                        break;

                    case IMAGE_REL_AMD64_ADDR32:
                        fSelfRel = false;
                        break;

                    case IMAGE_REL_AMD64_ADDR32NB:
                        fSelfRel = false;
                        bFrame   = OMF_FIX_F_EXTDEF;
                        idxFrame = pThis->idxExtImageBase;
                        break;

                    case IMAGE_REL_AMD64_REL32:
                        /* defaults are ok. */
                        break;

                    case IMAGE_REL_AMD64_SECTION:
                        bLocation = OMF_FIX_LOC_16BIT_SEGMENT;
                        RT_FALL_THRU();

                    case IMAGE_REL_AMD64_SECREL:
                        fSelfRel = false;
                        if (pOmfSym->enmType == OMFSYMTYPE_EXTDEF)
                        {
                            bFrame   = OMF_FIX_F_EXTDEF;
                            idxFrame = pOmfSym->idx;
                        }
                        else
                        {
                            bFrame   = OMF_FIX_F_SEGDEF;
                            idxFrame = pOmfSym->idxSegDef;
                        }
                        break;

                    case IMAGE_REL_AMD64_ABSOLUTE:
                        continue; /* Ignore it like the PECOFF.DOC says we should. */

                    case IMAGE_REL_AMD64_SECREL7:
                    default:
                        return error(pThis->pszSrc, "Unsupported fixup type %#x (%s) at rva=%#x in section #%u '%-8.8s'\n",
                                     paRelocs[iReloc].Type,
                                     paRelocs[iReloc].Type < RT_ELEMENTS(g_apszCoffAmd64RelTypes)
                                     ? g_apszCoffAmd64RelTypes[paRelocs[iReloc].Type] : "unknown",
                                     paRelocs[iReloc].u.VirtualAddress, i, paShdrs[i].Name);
                }

                /* Add the fixup. */
                if (idxFrame == UINT16_MAX)
                    error(pThis->pszSrc, "idxFrame=UINT16_MAX for %s type=%s\n",
                          coffGetSymbolName(pCoffSym, pchStrTab, cbStrTab, szShortName),
                          g_apszCoffAmd64RelTypes[paRelocs[iReloc].Type]);
                fRet = omfWriter_LEDataAddFixup(pThis, offDataRec, fSelfRel, bLocation, bFrame, idxFrame,
                                                bTarget, idxTarget, fTargetDisp, offTargetDisp) && fRet;
            }

            /*
             * Write the LEDATA and associated FIXUPPs.
             */
            if (!omfWriter_LEDataEnd(pThis))
                return false;

            /*
             * Advance.
             */
            paRelocs   += cChunkRelocs;
            cRelocs    -= cChunkRelocs;
            if (cbData > cbChunk)
            {
                cbData -= cbChunk;
                pbData += cbChunk;
            }
            else
                cbData  = 0;
            off        += cbChunk;
            cbVirtData -= cbChunk;
        }
    }

    return fRet;
}


static bool convertCoffToOmf(const char *pszFile, uint8_t const *pbFile, size_t cbFile, FILE *pDst)
{
    /*
     * Validate the source file a little.
     */
    if (!validateCoff(pszFile, pbFile, cbFile))
        return false;

    /*
     * Instantiate the OMF writer.
     */
    PIMAGE_FILE_HEADER pHdr = (PIMAGE_FILE_HEADER)pbFile;
    POMFWRITER pThis = omfWriter_Create(pszFile, pHdr->NumberOfSections, pHdr->NumberOfSymbols, pDst);
    if (!pThis)
        return false;

    /*
     * Write the OMF object file.
     */
    if (omfWriter_BeginModule(pThis, pszFile))
    {
        PCIMAGE_SECTION_HEADER paShdrs   = (PCIMAGE_SECTION_HEADER)(pHdr + 1);
        PCIMAGE_SYMBOL         paSymTab  = (PCIMAGE_SYMBOL)&pbFile[pHdr->PointerToSymbolTable];
        const char            *pchStrTab = (const char *)&paSymTab[pHdr->NumberOfSymbols];
        if (   convertCoffSectionsToSegDefsAndGrpDefs(pThis, paShdrs, pHdr->NumberOfSections)
            && convertCoffSymbolsToPubDefsAndExtDefs(pThis, paSymTab, pHdr->NumberOfSymbols, pchStrTab, paShdrs)
            && omfWriter_LinkPassSeparator(pThis)
            && convertCoffSectionsToLeDataAndFixupps(pThis, pbFile, cbFile, paShdrs, pHdr->NumberOfSections,
                                                     paSymTab, pHdr->NumberOfSymbols, pchStrTab)
            && omfWriter_EndModule(pThis) )
        {

            omfWriter_Destroy(pThis);
            return true;
        }
    }

    omfWriter_Destroy(pThis);
    return false;
}


/*********************************************************************************************************************************
*   Mach-O/AMD64 -> OMF/i386 Converter                                                                                           *
*********************************************************************************************************************************/

//#define MACHO_TO_OMF_CONVERSION
#ifdef MACHO_TO_OMF_CONVERSION

/** AMD64 relocation type names for Mach-O. */
static const char * const g_apszMachOAmd64RelTypes[] =
{
    "X86_64_RELOC_UNSIGNED",
    "X86_64_RELOC_SIGNED",
    "X86_64_RELOC_BRANCH",
    "X86_64_RELOC_GOT_LOAD",
    "X86_64_RELOC_GOT",
    "X86_64_RELOC_SUBTRACTOR",
    "X86_64_RELOC_SIGNED_1",
    "X86_64_RELOC_SIGNED_2",
    "X86_64_RELOC_SIGNED_4"
};

/** AMD64 relocation type sizes for Mach-O. */
static uint8_t const g_acbMachOAmd64RelTypes[] =
{
    8, /* X86_64_RELOC_UNSIGNED */
    4, /* X86_64_RELOC_SIGNED */
    4, /* X86_64_RELOC_BRANCH */
    4, /* X86_64_RELOC_GOT_LOAD */
    4, /* X86_64_RELOC_GOT */
    8, /* X86_64_RELOC_SUBTRACTOR */
    4, /* X86_64_RELOC_SIGNED_1 */
    4, /* X86_64_RELOC_SIGNED_2 */
    4, /* X86_64_RELOC_SIGNED_4 */
};

/** Macro for getting the size of a AMD64 Mach-O relocation. */
#define MACHO_AMD64_RELOC_SIZE(a_Type) ( (a_Type) < RT_ELEMENTS(g_acbMachOAmd64RelTypes) ? g_acbMachOAmd64RelTypes[(a_Type)] : 1)


typedef struct MACHODETAILS
{
    /** The ELF header. */
    Elf64_Ehdr const   *pEhdr;
    /** The section header table.   */
    Elf64_Shdr const   *paShdrs;
    /** The string table for the section names. */
    const char         *pchShStrTab;

    /** The symbol table section number. UINT16_MAX if not found.   */
    uint16_t            iSymSh;
    /** The string table section number. UINT16_MAX if not found. */
    uint16_t            iStrSh;

    /** The symbol table.   */
    Elf64_Sym const    *paSymbols;
    /** The number of symbols in the symbol table. */
    uint32_t            cSymbols;

    /** Pointer to the (symbol) string table if found. */
    const char         *pchStrTab;
    /** The string table size. */
    size_t              cbStrTab;

} MACHODETAILS;
typedef MACHODETAILS *PMACHODETAILS;
typedef MACHODETAILS const *PCMACHODETAILS;


static bool validateMacho(const char *pszFile, uint8_t const *pbFile, size_t cbFile, PMACHODETAILS pMachOStuff)
{
    /*
     * Initialize the Mach-O details structure.
     */
    memset(pMachOStuff, 0,  sizeof(*pMachOStuff));
    pMachOStuff->iSymSh = UINT16_MAX;
    pMachOStuff->iStrSh = UINT16_MAX;

    /*
     * Validate the header and our other expectations.
     */
    Elf64_Ehdr const *pEhdr = (Elf64_Ehdr const *)pbFile;
    pMachOStuff->pEhdr = pEhdr;
    if (   pEhdr->e_ident[EI_CLASS] != ELFCLASS64
        || pEhdr->e_ident[EI_DATA]  != ELFDATA2LSB
        || pEhdr->e_ehsize          != sizeof(Elf64_Ehdr)
        || pEhdr->e_shentsize       != sizeof(Elf64_Shdr)
        || pEhdr->e_version         != EV_CURRENT )
        return error(pszFile, "Unsupported ELF config\n");
    if (pEhdr->e_type != ET_REL)
        return error(pszFile, "Expected relocatable ELF file (e_type=%d)\n", pEhdr->e_type);
    if (pEhdr->e_machine != EM_X86_64)
        return error(pszFile, "Expected relocatable ELF file (e_type=%d)\n", pEhdr->e_machine);
    if (pEhdr->e_phnum != 0)
        return error(pszFile, "Expected e_phnum to be zero not %u\n", pEhdr->e_phnum);
    if (pEhdr->e_shnum < 2)
        return error(pszFile, "Expected e_shnum to be two or higher\n");
    if (pEhdr->e_shstrndx >= pEhdr->e_shnum || pEhdr->e_shstrndx == 0)
        return error(pszFile, "Bad e_shstrndx=%u (e_shnum=%u)\n", pEhdr->e_shstrndx, pEhdr->e_shnum);
    if (   pEhdr->e_shoff >= cbFile
        || pEhdr->e_shoff + pEhdr->e_shnum * sizeof(Elf64_Shdr) > cbFile)
        return error(pszFile, "Section table is outside the file (e_shoff=%#llx, e_shnum=%u, cbFile=%#llx)\n",
                     pEhdr->e_shstrndx, pEhdr->e_shnum, (uint64_t)cbFile);

    /*
     * Locate the section name string table.
     * We assume it's okay as we only reference it in verbose mode.
     */
    Elf64_Shdr const *paShdrs = (Elf64_Shdr const *)&pbFile[pEhdr->e_shoff];
    pMachOStuff->paShdrs = paShdrs;

    Elf64_Xword const cbShStrTab = paShdrs[pEhdr->e_shstrndx].sh_size;
    if (   paShdrs[pEhdr->e_shstrndx].sh_offset > cbFile
        || cbShStrTab > cbFile
        || paShdrs[pEhdr->e_shstrndx].sh_offset + cbShStrTab > cbFile)
        return error(pszFile,
                     "Section string table is outside the file (sh_offset=%#" ELF_FMT_X64 " sh_size=%#" ELF_FMT_X64 " cbFile=%#" ELF_FMT_X64 ")\n",
                     paShdrs[pEhdr->e_shstrndx].sh_offset, paShdrs[pEhdr->e_shstrndx].sh_size, (Elf64_Xword)cbFile);
    const char *pchShStrTab = (const char *)&pbFile[paShdrs[pEhdr->e_shstrndx].sh_offset];
    pMachOStuff->pchShStrTab = pchShStrTab;

    /*
     * Work the section table.
     */
    bool fRet = true;
    for (uint32_t i = 1; i < pEhdr->e_shnum; i++)
    {
        if (paShdrs[i].sh_name >= cbShStrTab)
            return error(pszFile, "Invalid sh_name value (%#x) for section #%u\n", paShdrs[i].sh_name, i);
        const char *pszShNm = &pchShStrTab[paShdrs[i].sh_name];

        if (   paShdrs[i].sh_offset > cbFile
            || paShdrs[i].sh_size > cbFile
            || paShdrs[i].sh_offset + paShdrs[i].sh_size > cbFile)
            return error(pszFile, "Section #%u '%s' has data outside the file: %#" ELF_FMT_X64 " LB %#" ELF_FMT_X64 " (cbFile=%#" ELF_FMT_X64 ")\n",
                         i, pszShNm, paShdrs[i].sh_offset, paShdrs[i].sh_size, (Elf64_Xword)cbFile);
        if (g_cVerbose)
            printf("shdr[%u]: name=%#x '%s' type=%#x flags=%#" ELF_FMT_X64 " addr=%#" ELF_FMT_X64 " off=%#" ELF_FMT_X64 " size=%#" ELF_FMT_X64 "\n"
                   "          link=%u info=%#x align=%#" ELF_FMT_X64 " entsize=%#" ELF_FMT_X64 "\n",
                   i, paShdrs[i].sh_name, pszShNm, paShdrs[i].sh_type, paShdrs[i].sh_flags,
                   paShdrs[i].sh_addr, paShdrs[i].sh_offset, paShdrs[i].sh_size,
                   paShdrs[i].sh_link, paShdrs[i].sh_info, paShdrs[i].sh_addralign, paShdrs[i].sh_entsize);

        if (paShdrs[i].sh_link >= pEhdr->e_shnum)
            return error(pszFile, "Section #%u '%s' links to a section outside the section table: %#x, max %#x\n",
                         i, pszShNm, paShdrs[i].sh_link, pEhdr->e_shnum);
        if (!RT_IS_POWER_OF_TWO(paShdrs[i].sh_addralign))
            return error(pszFile, "Section #%u '%s' alignment value is not a power of two: %#" ELF_FMT_X64 "\n",
                         i, pszShNm, paShdrs[i].sh_addralign);
        if (!RT_IS_POWER_OF_TWO(paShdrs[i].sh_addralign))
            return error(pszFile, "Section #%u '%s' alignment value is not a power of two: %#" ELF_FMT_X64 "\n",
                         i, pszShNm, paShdrs[i].sh_addralign);
        if (paShdrs[i].sh_addr != 0)
            return error(pszFile, "Section #%u '%s' has non-zero address: %#" ELF_FMT_X64 "\n", i, pszShNm, paShdrs[i].sh_addr);

        if (paShdrs[i].sh_type == SHT_RELA)
        {
            if (paShdrs[i].sh_entsize != sizeof(Elf64_Rela))
                return error(pszFile, "Expected sh_entsize to be %u not %u for section #%u (%s)\n", (unsigned)sizeof(Elf64_Rela),
                             paShdrs[i].sh_entsize, i, pszShNm);
            uint32_t const cRelocs = paShdrs[i].sh_size / sizeof(Elf64_Rela);
            if (cRelocs * sizeof(Elf64_Rela) != paShdrs[i].sh_size)
                return error(pszFile, "Uneven relocation entry count in #%u (%s): sh_size=%#" ELF_FMT_X64 "\n",
                             i, pszShNm, paShdrs[i].sh_size);
            if (   paShdrs[i].sh_offset > cbFile
                || paShdrs[i].sh_size  >= cbFile
                || paShdrs[i].sh_offset + paShdrs[i].sh_size > cbFile)
                return error(pszFile, "The content of section #%u '%s' is outside the file (%#" ELF_FMT_X64 " LB %#" ELF_FMT_X64 ", cbFile=%#lx)\n",
                             i, pszShNm, paShdrs[i].sh_offset, paShdrs[i].sh_size, (unsigned long)cbFile);
            if (paShdrs[i].sh_info != i - 1)
                return error(pszFile, "Expected relocation section #%u (%s) to link to previous section: sh_info=%#u\n",
                             i, pszShNm, (unsigned)paShdrs[i].sh_link);
            if (paShdrs[paShdrs[i].sh_link].sh_type != SHT_SYMTAB)
                return error(pszFile, "Expected relocation section #%u (%s) to link to symbol table: sh_link=%#u -> sh_type=%#x\n",
                             i, pszShNm, (unsigned)paShdrs[i].sh_link, (unsigned)paShdrs[paShdrs[i].sh_link].sh_type);
            uint32_t cSymbols = paShdrs[paShdrs[i].sh_link].sh_size / paShdrs[paShdrs[i].sh_link].sh_entsize;

            Elf64_Rela const  *paRelocs = (Elf64_Rela *)&pbFile[paShdrs[i].sh_offset];
            for (uint32_t j = 0; j < cRelocs; j++)
            {
                uint8_t const bType = ELF64_R_TYPE(paRelocs[j].r_info);
                if (RT_UNLIKELY(bType >= R_X86_64_COUNT))
                    fRet = error(pszFile,
                                 "%#018" ELF_FMT_X64 "  %#018" ELF_FMT_X64 ": unknown fix up %#x  (%+" ELF_FMT_D64 ")\n",
                                 paRelocs[j].r_offset, paRelocs[j].r_info, bType, paRelocs[j].r_addend);
                if (RT_UNLIKELY(   j > 1
                                && paRelocs[j].r_offset <= paRelocs[j - 1].r_offset
                                &&   paRelocs[j].r_offset + ELF_AMD64_RELOC_SIZE(ELF64_R_TYPE(paRelocs[j].r_info))
                                   < paRelocs[j - 1].r_offset ))
                    fRet = error(pszFile,
                                 "%#018" ELF_FMT_X64 "  %#018" ELF_FMT_X64 ": out of offset order (prev %" ELF_FMT_X64 ")\n",
                                 paRelocs[j].r_offset, paRelocs[j].r_info, paRelocs[j - 1].r_offset);
                uint32_t const iSymbol = ELF64_R_SYM(paRelocs[j].r_info);
                if (RT_UNLIKELY(iSymbol >= cSymbols))
                    fRet = error(pszFile,
                                 "%#018" ELF_FMT_X64 "  %#018" ELF_FMT_X64 ": symbol index (%#x) out of bounds (%#x)\n",
                                 paRelocs[j].r_offset, paRelocs[j].r_info, iSymbol, cSymbols);
            }
            if (RT_UNLIKELY(   cRelocs > 0
                            && fRet
                            && (   paRelocs[cRelocs - 1].r_offset > paShdrs[i - 1].sh_size
                                || paRelocs[cRelocs - 1].r_offset + ELF_AMD64_RELOC_SIZE(ELF64_R_TYPE(paRelocs[cRelocs-1].r_info))
                                   > paShdrs[i - 1].sh_size )))
                fRet = error(pszFile,
                             "%#018" ELF_FMT_X64 "  %#018" ELF_FMT_X64 ": out of bounds (sh_size %" ELF_FMT_X64 ")\n",
                             paRelocs[cRelocs - 1].r_offset, paRelocs[cRelocs - 1].r_info, paShdrs[i - 1].sh_size);

        }
        else if (paShdrs[i].sh_type == SHT_REL)
            fRet = error(pszFile, "Section #%u '%s': Unexpected SHT_REL section\n", i, pszShNm);
        else if (paShdrs[i].sh_type == SHT_SYMTAB)
        {
            if (paShdrs[i].sh_entsize != sizeof(Elf64_Sym))
                fRet = error(pszFile, "Section #%u '%s': Unsupported symbol table entry size in : #%u (expected #%u)\n",
                             i, pszShNm, paShdrs[i].sh_entsize, sizeof(Elf64_Sym));
            Elf64_Xword const cSymbols = paShdrs[i].sh_size / paShdrs[i].sh_entsize;
            if (cSymbols * paShdrs[i].sh_entsize != paShdrs[i].sh_size)
                fRet = error(pszFile, "Section #%u '%s': Size not a multiple of entry size: %#" ELF_FMT_X64 " %% %#" ELF_FMT_X64 " = %#" ELF_FMT_X64 "\n",
                             i, pszShNm, paShdrs[i].sh_size, paShdrs[i].sh_entsize, paShdrs[i].sh_size % paShdrs[i].sh_entsize);
            if (cSymbols > UINT32_MAX)
                fRet = error(pszFile, "Section #%u '%s': too many symbols: %" ELF_FMT_X64 "\n",
                             i, pszShNm, paShdrs[i].sh_size, cSymbols);

            if (pMachOStuff->iSymSh == UINT16_MAX)
            {
                pMachOStuff->iSymSh    = (uint16_t)i;
                pMachOStuff->paSymbols = (Elf64_Sym const *)&pbFile[paShdrs[i].sh_offset];
                pMachOStuff->cSymbols  = cSymbols;

                if (paShdrs[i].sh_link != 0)
                {
                    /* Note! The symbol string table section header may not have been validated yet! */
                    Elf64_Shdr const *pStrTabShdr = &paShdrs[paShdrs[i].sh_link];
                    pMachOStuff->iStrSh    = paShdrs[i].sh_link;
                    pMachOStuff->pchStrTab = (const char *)&pbFile[pStrTabShdr->sh_offset];
                    pMachOStuff->cbStrTab  = (size_t)pStrTabShdr->sh_size;
                }
                else
                    fRet = error(pszFile, "Section #%u '%s': String table link is out of bounds (%#x)\n",
                                 i, pszShNm, paShdrs[i].sh_link);
            }
            else
                fRet = error(pszFile, "Section #%u '%s': Found additonal symbol table, previous in #%u\n",
                             i, pszShNm, pMachOStuff->iSymSh);
        }
    }
    return fRet;
}

static bool convertMachoSectionsToSegDefsAndGrpDefs(POMFWRITER pThis, PCMACHODETAILS pMachOStuff)
{
    /*
     * Do the list of names pass.
     */
    uint16_t idxGrpFlat, idxGrpData;
    uint16_t idxClassCode, idxClassData, idxClassDwarf;
    if (   !omfWriter_LNamesBegin(pThis, true /*fAddZeroEntry*/)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("FLAT"), &idxGrpFlat)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("BS3DATA64_GROUP"), &idxGrpData)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("BS3CLASS64CODE"), &idxClassCode)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("FAR_DATA"), &idxClassData)
        || !omfWriter_LNamesAddN(pThis, RT_STR_TUPLE("DWARF"), &idxClassDwarf)
       )
        return false;

    bool              fHaveData = false;
    Elf64_Shdr const *pShdr     = &pMachOStuff->paShdrs[1];
    Elf64_Half const  cSections = pMachOStuff->pEhdr->e_shnum;
    for (Elf64_Half i = 1; i < cSections; i++, pShdr++)
    {
        const char *pszName = &pMachOStuff->pchShStrTab[pShdr->sh_name];
        if (*pszName == '\0')
            return error(pThis->pszSrc, "Section #%u has an empty name!\n", i);

        switch (pShdr->sh_type)
        {
            case SHT_PROGBITS:
            case SHT_NOBITS:
                /* We drop a few sections we don't want:. */
                if (   strcmp(pszName, ".comment") != 0         /* compiler info  */
                    && strcmp(pszName, ".note.GNU-stack") != 0  /* some empty section for hinting the linker/whatever */
                    && strcmp(pszName, ".eh_frame") != 0        /* unwind / exception info */
                    )
                {
                    pThis->paSegments[i].iSegDef  = UINT16_MAX;
                    pThis->paSegments[i].iGrpDef  = UINT16_MAX;

                    /* Translate the name and determine group and class.
                       Note! We currently strip sub-sections. */
                    if (   strcmp(pszName, ".text") == 0
                        || strncmp(pszName, RT_STR_TUPLE(".text.")) == 0)
                    {
                        pszName = "BS3TEXT64";
                        pThis->paSegments[i].iGrpNm   = idxGrpFlat;
                        pThis->paSegments[i].iClassNm = idxClassCode;
                    }
                    else if (   strcmp(pszName, ".data") == 0
                             || strncmp(pszName, RT_STR_TUPLE(".data.")) == 0)
                    {
                        pszName = "BS3DATA64";
                        pThis->paSegments[i].iGrpNm   = idxGrpData;
                        pThis->paSegments[i].iClassNm = idxClassData;
                    }
                    else if (strcmp(pszName, ".bss") == 0)
                    {
                        pszName = "BS3BSS64";
                        pThis->paSegments[i].iGrpNm   = idxGrpData;
                        pThis->paSegments[i].iClassNm = idxClassData;
                    }
                    else if (   strcmp(pszName, ".rodata") == 0
                             || strncmp(pszName, RT_STR_TUPLE(".rodata.")) == 0)
                    {
                        pszName = "BS3DATA64CONST";
                        pThis->paSegments[i].iGrpNm   = idxGrpData;
                        pThis->paSegments[i].iClassNm = idxClassData;
                    }
                    else if (strncmp(pszName, RT_STR_TUPLE(".debug_")) == 0)
                    {
                        pThis->paSegments[i].iGrpNm   = UINT16_MAX;
                        pThis->paSegments[i].iClassNm = idxClassDwarf;
                    }
                    else
                    {
                        pThis->paSegments[i].iGrpNm   = idxGrpData;
                        pThis->paSegments[i].iClassNm = idxClassData;
                        error(pThis->pszSrc, "Unknown data (?) segment: '%s'\n", pszName);
                    }

                    /* Save the name. */
                    pThis->paSegments[i].pszName  = strdup(pszName);
                    if (!pThis->paSegments[i].pszName)
                        return error(pThis->pszSrc, "Out of memory!\n");

                    /* Add the section name. */
                    if (!omfWriter_LNamesAdd(pThis, pThis->paSegments[i].pszName, &pThis->paSegments[i].iSegNm))
                        return false;

                    fHaveData |= pThis->paSegments[i].iGrpDef == idxGrpData;
                    break;
                }
                RT_FALL_THRU();

            default:
                pThis->paSegments[i].iSegDef  = UINT16_MAX;
                pThis->paSegments[i].iGrpDef  = UINT16_MAX;
                pThis->paSegments[i].iSegNm   = UINT16_MAX;
                pThis->paSegments[i].iGrpNm   = UINT16_MAX;
                pThis->paSegments[i].iClassNm = UINT16_MAX;
                pThis->paSegments[i].pszName  = NULL;
                break;
        }
    }

    if (!omfWriter_LNamesEnd(pThis))
        return false;

    /*
     * Emit segment definitions.
     */
    uint16_t iSegDef = 1; /* Start counting at 1. */
    pShdr = &pMachOStuff->paShdrs[1];
    for (Elf64_Half i = 1; i < cSections; i++, pShdr++)
    {
        if (pThis->paSegments[i].iSegNm == UINT16_MAX)
            continue;

        uint8_t bSegAttr = 0;

        /* The A field. */
        switch (pShdr->sh_addralign)
        {
            case 0:
            case 1:
                bSegAttr |= 1 << 5;
                break;
            case 2:
                bSegAttr |= 2 << 5;
                break;
            case 4:
                bSegAttr |= 5 << 5;
                break;
            case 8:
            case 16:
                bSegAttr |= 3 << 5;
                break;
            case 32:
            case 64:
            case 128:
            case 256:
                bSegAttr |= 4 << 5;
                break;
            default:
                bSegAttr |= 6 << 5; /* page aligned, pharlabs extension. */
                break;
        }

        /* The C field. */
        bSegAttr |= 2 << 2; /* public */

        /* The B field. We don't have 4GB segments, so leave it as zero. */

        /* The D field shall be set as we're doing USE32.  */
        bSegAttr |= 1;


        /* Done. */
        if (!omfWriter_SegDef(pThis, bSegAttr, (uint32_t)pShdr->sh_size,
                              pThis->paSegments[i].iSegNm,
                              pThis->paSegments[i].iClassNm))
            return false;
        pThis->paSegments[i].iSegDef = iSegDef++;
    }

    /*
     * Flat group definition (#1) - special, no members.
     */
    uint16_t iGrpDef = 1;
    if (   !omfWriter_GrpDefBegin(pThis, idxGrpFlat)
        || !omfWriter_GrpDefEnd(pThis))
        return false;
    for (uint16_t i = 0; i < cSections; i++)
        if (pThis->paSegments[i].iGrpNm == idxGrpFlat)
            pThis->paSegments[i].iGrpDef = iGrpDef;
    pThis->idxGrpFlat = iGrpDef++;

    /*
     * Data group definition (#2).
     */
    /** @todo do we need to consider missing segments and ordering? */
    uint16_t cGrpNms = 0;
    uint16_t aiGrpNms[2] = { 0, 0 }; /* Shut up, GCC. */
    if (fHaveData)
        aiGrpNms[cGrpNms++] = idxGrpData;
    for (uint32_t iGrpNm = 0; iGrpNm < cGrpNms; iGrpNm++)
    {
        if (!omfWriter_GrpDefBegin(pThis, aiGrpNms[iGrpNm]))
            return false;
        for (uint16_t i = 0; i < cSections; i++)
            if (pThis->paSegments[i].iGrpNm == aiGrpNms[iGrpNm])
            {
                pThis->paSegments[i].iGrpDef = iGrpDef;
                if (!omfWriter_GrpDefAddSegDef(pThis, pThis->paSegments[i].iSegDef))
                    return false;
            }
        if (!omfWriter_GrpDefEnd(pThis))
            return false;
        iGrpDef++;
    }

    return true;
}

static bool convertMachOSymbolsToPubDefsAndExtDefs(POMFWRITER pThis, PCMACHODETAILS pMachOStuff)
{
    if (!pMachOStuff->cSymbols)
        return true;

    /*
     * Process the symbols the first.
     */
    uint32_t cAbsSyms = 0;
    uint32_t cExtSyms = 0;
    uint32_t cPubSyms = 0;
    for (uint32_t iSeg = 0; iSeg < pThis->cSegments; iSeg++)
        pThis->paSegments[iSeg].cPubDefs = 0;

    uint32_t const          cSections = pMachOStuff->pEhdr->e_shnum;
    uint32_t const          cSymbols  = pMachOStuff->cSymbols;
    Elf64_Sym const * const paSymbols = pMachOStuff->paSymbols;
    for (uint32_t iSym = 0; iSym < cSymbols; iSym++)
    {
        const uint8_t bBind      = ELF64_ST_BIND(paSymbols[iSym].st_info);
        const uint8_t bType      = ELF64_ST_TYPE(paSymbols[iSym].st_info);
        const char   *pszSymName = &pMachOStuff->pchStrTab[paSymbols[iSym].st_name];
        if (   *pszSymName == '\0'
            && bType == STT_SECTION
            && paSymbols[iSym].st_shndx < cSections)
            pszSymName = &pMachOStuff->pchShStrTab[pMachOStuff->paShdrs[paSymbols[iSym].st_shndx].sh_name];

        pThis->paSymbols[iSym].enmType   = OMFSYMTYPE_IGNORED;
        pThis->paSymbols[iSym].idx       = UINT16_MAX;
        pThis->paSymbols[iSym].idxSegDef = UINT16_MAX;
        pThis->paSymbols[iSym].idxGrpDef = UINT16_MAX;

        uint32_t const idxSection = paSymbols[iSym].st_shndx;
        if (idxSection == SHN_UNDEF)
        {
            if (bBind == STB_GLOBAL)
            {
                pThis->paSymbols[iSym].enmType = OMFSYMTYPE_EXTDEF;
                cExtSyms++;
                if (*pszSymName == '\0')
                    return error(pThis->pszSrc, "External symbol #%u (%s) has an empty name.\n", iSym, pszSymName);
            }
            else if (bBind != STB_LOCAL || iSym != 0) /* Entry zero is usually a dummy. */
                return error(pThis->pszSrc, "Unsupported or invalid bind type %#x for undefined symbol #%u (%s)\n",
                             bBind, iSym, pszSymName);
        }
        else if (idxSection < cSections)
        {
            pThis->paSymbols[iSym].idxSegDef = pThis->paSegments[idxSection].iSegDef;
            pThis->paSymbols[iSym].idxGrpDef = pThis->paSegments[idxSection].iGrpDef;
            if (bBind == STB_GLOBAL)
            {
                pThis->paSymbols[iSym].enmType = OMFSYMTYPE_PUBDEF;
                pThis->paSegments[idxSection].cPubDefs++;
                cPubSyms++;
                if (bType == STT_SECTION)
                    return error(pThis->pszSrc, "Don't know how to export STT_SECTION symbol #%u (%s)\n", iSym, pszSymName);
                if (*pszSymName == '\0')
                    return error(pThis->pszSrc, "Public symbol #%u (%s) has an empty name.\n", iSym, pszSymName);
            }
            else if (bType == STT_SECTION)
                pThis->paSymbols[iSym].enmType = OMFSYMTYPE_SEGDEF;
            else
                pThis->paSymbols[iSym].enmType = OMFSYMTYPE_INTERNAL;
        }
        else if (idxSection == SHN_ABS)
        {
            if (bType != STT_FILE)
            {
                if (bBind == STB_GLOBAL)
                {
                    pThis->paSymbols[iSym].enmType   = OMFSYMTYPE_PUBDEF;
                    pThis->paSymbols[iSym].idxSegDef = 0;
                    pThis->paSymbols[iSym].idxGrpDef = 0;
                    cAbsSyms++;
                    if (*pszSymName == '\0')
                        return error(pThis->pszSrc, "Public absolute symbol #%u (%s) has an empty name.\n", iSym, pszSymName);
                }
                else
                    return error(pThis->pszSrc, "Unsupported or invalid bind type %#x for absolute symbol #%u (%s)\n",
                                 bBind, iSym, pszSymName);
            }
        }
        else
            return error(pThis->pszSrc, "Unsupported or invalid section number %#x for symbol #%u (%s)\n",
                         idxSection, iSym, pszSymName);
    }

    /*
     * Emit the PUBDEFs the first time around (see order of records in TIS spec).
     * Note! We expect the os x compiler to always underscore symbols, so unlike the
     *       other 64-bit converters we don't need to check for underscores and add them.
     */
    uint16_t idxPubDef = 1;
    if (cPubSyms)
    {
        for (uint32_t iSeg = 0; iSeg < pThis->cSegments; iSeg++)
            if (pThis->paSegments[iSeg].cPubDefs > 0)
            {
                uint16_t const idxSegDef = pThis->paSegments[iSeg].iSegDef;
                if (!omfWriter_PubDefBegin(pThis, pThis->paSegments[iSeg].iGrpDef, idxSegDef))
                    return false;
                for (uint16_t iSym = 0; iSym < cSymbols; iSym++)
                    if (   pThis->paSymbols[iSym].idxSegDef == idxSegDef
                        && pThis->paSymbols[iSym].enmType   == OMFSYMTYPE_PUBDEF)
                    {
                        const char *pszName = &pMachOStuff->pchStrTab[paSymbols[iSym].st_name];
                        if (!omfWriter_PubDefAdd(pThis, paSymbols[iSym].st_value, pszName, false /*fPrependUnderscore*/))
                            return false;
                        pThis->paSymbols[iSym].idx = idxPubDef++;
                    }
                if (!omfWriter_PubDefEnd(pThis))
                    return false;
            }
    }

    if (cAbsSyms > 0)
    {
        if (!omfWriter_PubDefBegin(pThis, 0, 0))
            return false;
        for (uint16_t iSym = 0; iSym < cSymbols; iSym++)
            if (   pThis->paSymbols[iSym].idxSegDef == 0
                && pThis->paSymbols[iSym].enmType   == OMFSYMTYPE_PUBDEF)
            {
                const char *pszName = &pMachOStuff->pchStrTab[paSymbols[iSym].st_name];
                if (!omfWriter_PubDefAdd(pThis, paSymbols[iSym].st_value, pszName, false /*fPrependUnderscore*/))
                    return false;
                pThis->paSymbols[iSym].idx = idxPubDef++;
            }
        if (!omfWriter_PubDefEnd(pThis))
            return false;
    }

    /*
     * Go over the symbol table and emit external definition records.
     */
    if (!omfWriter_ExtDefBegin(pThis))
        return false;
    uint16_t idxExtDef = 1;
    for (uint16_t iSym = 0; iSym < cSymbols; iSym++)
        if (pThis->paSymbols[iSym].enmType == OMFSYMTYPE_EXTDEF)
        {
            const char *pszName = &pMachOStuff->pchStrTab[paSymbols[iSym].st_name];
            if (!omfWriter_ExtDefAdd(pThis, pszName, false /*fPrependUnderscore*/))
                return false;
            pThis->paSymbols[iSym].idx = idxExtDef++;
        }

    if (!omfWriter_ExtDefEnd(pThis))
        return false;

    return true;
}

static bool convertMachOSectionsToLeDataAndFixupps(POMFWRITER pThis, PCMACHODETAILS pMachOStuff,
                                                   uint8_t const *pbFile, size_t cbFile)
{
    Elf64_Sym const    *paSymbols = pMachOStuff->paSymbols;
    Elf64_Shdr const   *paShdrs   = pMachOStuff->paShdrs;
    bool                fRet      = true;
    for (uint32_t i = 1; i < pThis->cSegments; i++)
    {
        if (pThis->paSegments[i].iSegDef == UINT16_MAX)
            continue;

        const char         *pszSegNm   = &pMachOStuff->pchShStrTab[paShdrs[i].sh_name];
        bool const          fRelocs    = i + 1 < pThis->cSegments && paShdrs[i + 1].sh_type == SHT_RELA;
        uint32_t            cRelocs    = fRelocs ? paShdrs[i + 1].sh_size / sizeof(Elf64_Rela) : 0;
        Elf64_Rela const   *paRelocs   = fRelocs ? (Elf64_Rela *)&pbFile[paShdrs[i + 1].sh_offset] : NULL;
        Elf64_Xword         cbVirtData = paShdrs[i].sh_size;
        Elf64_Xword         cbData     = paShdrs[i].sh_type == SHT_NOBITS ? 0 : cbVirtData;
        uint8_t const      *pbData     = &pbFile[paShdrs[i].sh_offset];
        uint32_t            off        = 0;

        /* The OMF record size requires us to split larger sections up.  To make
           life simple, we fill zeros for unitialized (BSS) stuff. */
        const uint32_t cbMaxData = RT_MIN(OMF_MAX_RECORD_PAYLOAD - 1 - (pThis->paSegments[i].iSegDef >= 128) - 4 - 1, _1K);
        while (cbVirtData > 0)
        {
            /* Figure out how many bytes to put out in this chunk.  Must make sure
               fixups doesn't cross chunk boundraries.  ASSUMES sorted relocs. */
            uint32_t       cChunkRelocs = cRelocs;
            uint32_t       cbChunk      = cbVirtData;
            uint32_t       offEnd       = off + cbChunk;
            if (cbChunk > cbMaxData)
            {
                cbChunk      = cbMaxData;
                offEnd       = off + cbChunk;
                cChunkRelocs = 0;

                /* Quickly determin the reloc range. */
                while (   cChunkRelocs < cRelocs
                       && paRelocs[cChunkRelocs].r_offset < offEnd)
                    cChunkRelocs++;

                /* Ensure final reloc doesn't go beyond chunk. */
                while (   cChunkRelocs > 0
                       &&     paRelocs[cChunkRelocs - 1].r_offset
                            + ELF_AMD64_RELOC_SIZE(ELF64_R_TYPE(paRelocs[cChunkRelocs - 1].r_info))
                          > offEnd)
                {
                    uint32_t cbDrop = offEnd - paRelocs[cChunkRelocs - 1].r_offset;
                    cbChunk -= cbDrop;
                    offEnd  -= cbDrop;
                    cChunkRelocs--;
                }

                if (!cbVirtData)
                    return error(pThis->pszSrc, "Wtf? cbVirtData is zero!\n");
            }

            /*
             * We stash the bytes into the OMF writer record buffer, receiving a
             * pointer to the start of it so we can make adjustments if necessary.
             */
            uint8_t *pbCopy;
            if (!omfWriter_LEDataBeginEx(pThis, pThis->paSegments[i].iSegDef, off, cbChunk, cbData, pbData, &pbCopy))
                return false;

            /*
             * Convert fiuxps.
             */
            for (uint32_t iReloc = 0; iReloc < cChunkRelocs; iReloc++)
            {
                /* Get the OMF and ELF data for the symbol the reloc references. */
                uint32_t const          uType      = ELF64_R_TYPE(paRelocs[iReloc].r_info);
                uint32_t const          iSymbol    = ELF64_R_SYM(paRelocs[iReloc].r_info);
                Elf64_Sym const * const pElfSym    =        &paSymbols[iSymbol];
                POMFSYMBOL const        pOmfSym    = &pThis->paSymbols[iSymbol];
                const char * const      pszSymName = &pMachOStuff->pchStrTab[pElfSym->st_name];

                /* Calc fixup location in the pending chunk and setup a flexible pointer to it. */
                uint16_t  offDataRec = (uint16_t)(paRelocs[iReloc].r_offset - off);
                RTPTRUNION uLoc;
                uLoc.pu8 = &pbCopy[offDataRec];

                /* OMF fixup data initialized with typical defaults. */
                bool        fSelfRel  = true;
                uint8_t     bLocation = OMF_FIX_LOC_32BIT_OFFSET;
                uint8_t     bFrame    = OMF_FIX_F_GRPDEF;
                uint16_t    idxFrame  = pThis->idxGrpFlat;
                uint8_t     bTarget;
                uint16_t    idxTarget;
                bool        fTargetDisp;
                uint32_t    offTargetDisp;
                switch (pOmfSym->enmType)
                {
                    case OMFSYMTYPE_INTERNAL:
                    case OMFSYMTYPE_PUBDEF:
                        bTarget       = OMF_FIX_T_SEGDEF;
                        idxTarget     = pOmfSym->idxSegDef;
                        fTargetDisp   = true;
                        offTargetDisp = pElfSym->st_value;
                        break;

                    case OMFSYMTYPE_SEGDEF:
                        bTarget       = OMF_FIX_T_SEGDEF_NO_DISP;
                        idxTarget     = pOmfSym->idxSegDef;
                        fTargetDisp   = false;
                        offTargetDisp = 0;
                        break;

                    case OMFSYMTYPE_EXTDEF:
                        bTarget       = OMF_FIX_T_EXTDEF_NO_DISP;
                        idxTarget     = pOmfSym->idx;
                        fTargetDisp   = false;
                        offTargetDisp = 0;
                        break;

                    default:
                        return error(pThis->pszSrc, "Relocation in segment #%u '%s' references ignored or invalid symbol (%s)\n",
                                     i, pszSegNm, pszSymName);
                }

                /* Do COFF relocation type conversion. */
                switch (uType)
                {
                    case R_X86_64_64:
                    {
                        int64_t iAddend = paRelocs[iReloc].r_addend;
                        if (iAddend > _1G || iAddend < -_1G)
                            fRet = error(pThis->pszSrc, "R_X86_64_64 with large addend (%" ELF_FMT_D64 ") at %#x in segment #%u '%s'\n",
                                         iAddend, paRelocs[iReloc].r_offset, i, pszSegNm);
                        *uLoc.pu64 = iAddend;
                        fSelfRel = false;
                        break;
                    }

                    case R_X86_64_32:
                    case R_X86_64_32S:  /* signed, unsigned, whatever. */
                        fSelfRel = false;
                        RT_FALL_THRU();
                    case R_X86_64_PC32:
                    {
                        /* defaults are ok, just handle the addend. */
                        int32_t iAddend = paRelocs[iReloc].r_addend;
                        if (iAddend != paRelocs[iReloc].r_addend)
                            fRet = error(pThis->pszSrc, "R_X86_64_PC32 with large addend (%d) at %#x in segment #%u '%s'\n",
                                         iAddend, paRelocs[iReloc].r_offset, i, pszSegNm);
                        *uLoc.pu32 = iAddend;
                        break;
                    }

                    case R_X86_64_NONE:
                        continue; /* Ignore this one */

                    case R_X86_64_GOT32:
                    case R_X86_64_PLT32:
                    case R_X86_64_COPY:
                    case R_X86_64_GLOB_DAT:
                    case R_X86_64_JMP_SLOT:
                    case R_X86_64_RELATIVE:
                    case R_X86_64_GOTPCREL:
                    case R_X86_64_16:
                    case R_X86_64_PC16:
                    case R_X86_64_8:
                    case R_X86_64_PC8:
                    case R_X86_64_DTPMOD64:
                    case R_X86_64_DTPOFF64:
                    case R_X86_64_TPOFF64:
                    case R_X86_64_TLSGD:
                    case R_X86_64_TLSLD:
                    case R_X86_64_DTPOFF32:
                    case R_X86_64_GOTTPOFF:
                    case R_X86_64_TPOFF32:
                    default:
                        return error(pThis->pszSrc, "Unsupported fixup type %#x (%s) at rva=%#x in section #%u '%s' against '%s'\n",
                                     uType, g_apszElfAmd64RelTypes[uType], paRelocs[iReloc].r_offset, i, pszSegNm, pszSymName);
                }

                /* Add the fixup. */
                if (idxFrame == UINT16_MAX)
                    error(pThis->pszSrc, "idxFrame=UINT16_MAX for %s type=%s\n", pszSymName, g_apszElfAmd64RelTypes[uType]);
                fRet = omfWriter_LEDataAddFixup(pThis, offDataRec, fSelfRel, bLocation, bFrame, idxFrame,
                                                bTarget, idxTarget, fTargetDisp, offTargetDisp) && fRet;
            }

            /*
             * Write the LEDATA and associated FIXUPPs.
             */
            if (!omfWriter_LEDataEnd(pThis))
                return false;

            /*
             * Advance.
             */
            paRelocs   += cChunkRelocs;
            cRelocs    -= cChunkRelocs;
            if (cbData > cbChunk)
            {
                cbData -= cbChunk;
                pbData += cbChunk;
            }
            else
                cbData  = 0;
            off        += cbChunk;
            cbVirtData -= cbChunk;
        }
    }

    return fRet;
}


static bool convertMachoToOmf(const char *pszFile, uint8_t const *pbFile, size_t cbFile, FILE *pDst)
{
    /*
     * Validate the source file a little.
     */
    MACHODETAILS MachOStuff;
    if (!validateMachO(pszFile, pbFile, cbFile, &MachOStuff))
        return false;

    /*
     * Instantiate the OMF writer.
     */
    POMFWRITER pThis = omfWriter_Create(pszFile, MachOStuff.pEhdr->e_shnum, MachOStuff.cSymbols, pDst);
    if (!pThis)
        return false;

    /*
     * Write the OMF object file.
     */
    if (omfWriter_BeginModule(pThis, pszFile))
    {
        Elf64_Ehdr const *pEhdr     = (Elf64_Ehdr const *)pbFile;

        if (   convertMachOSectionsToSegDefsAndGrpDefs(pThis, &MachOStuff)
            && convertMachOSymbolsToPubDefsAndExtDefs(pThis, &MachOStuff)
            && omfWriter_LinkPassSeparator(pThis)
            && convertMachOSectionsToLeDataAndFixupps(pThis, &MachOStuff, pbFile, cbFile)
            && omfWriter_EndModule(pThis) )
        {

            omfWriter_Destroy(pThis);
            return true;
        }
    }

    omfWriter_Destroy(pThis);
    return false;
}

#endif /* !MACHO_TO_OMF_CONVERSION */


/*********************************************************************************************************************************
*   OMF Converter/Tweaker                                                                                                        *
*********************************************************************************************************************************/

/** Watcom intrinsics we need to modify so we can mix 32-bit and 16-bit
 * code, since the 16 and 32 bit compilers share several names.
 * The names are length prefixed.
 */
static const char * const g_apszExtDefRenames[] =
{
    "\x05" "__I4D",
    "\x05" "__I4M",
    "\x05" "__I8D",
    "\x06" "__I8DQ",
    "\x07" "__I8DQE",
    "\x06" "__I8DR",
    "\x07" "__I8DRE",
    "\x06" "__I8LS",
    "\x05" "__I8M",
    "\x06" "__I8ME",
    "\x06" "__I8RS",
    "\x05" "__PIA",
    "\x05" "__PIS",
    "\x05" "__PTC",
    "\x05" "__PTS",
    "\x05" "__U4D",
    "\x05" "__U4M",
    "\x05" "__U8D",
    "\x06" "__U8DQ",
    "\x07" "__U8DQE",
    "\x06" "__U8DR",
    "\x07" "__U8DRE",
    "\x06" "__U8LS",
    "\x05" "__U8M",
    "\x06" "__U8ME",
    "\x06" "__U8RS",
};

/**
 * Segment definition.
 */
typedef struct OMFSEGDEF
{
    uint32_t    cbSeg;
    uint8_t     bSegAttr;
    uint16_t    idxName;
    uint16_t    idxClass;
    uint16_t    idxOverlay;
    uint8_t     cchName;
    uint8_t     cchClass;
    uint8_t     cchOverlay;
    const char *pchName;
    const char *pchClass;
    const char *pchOverlay;
    bool        fUse32;
    bool        f32bitRec;
} OMFSEGDEF;
typedef OMFSEGDEF *POMFSEGDEF;

/**
 * Group definition.
 */
typedef struct OMFGRPDEF
{
    const char *pchName;
    uint16_t    idxName;
    uint8_t     cchName;
    uint16_t    cSegDefs;
    uint16_t   *paidxSegDefs;
} OMFGRPDEF;
typedef OMFGRPDEF *POMFGRPDEF;

/**
 * Records line number information for a file in a segment (for CV8 debug info).
 */
typedef struct OMFFILELINES
{
    /** The source info offset. */
    uint32_t        offSrcInfo;
    /** Number of line/offset pairs. */
    uint32_t        cPairs;
    /** Number of pairs allocated. */
    uint32_t        cPairsAlloc;
    /** Table with line number and offset pairs, ordered by offset. */
    PRTCV8LINEPAIR  paPairs;
} OMFFILEINES;
typedef OMFFILEINES *POMFFILEINES;

/**
 * Records line number information for a segment (for CV8 debug info).
 */
typedef struct OMFSEGLINES
{
    /** Number of files.   */
    uint32_t        cFiles;
    /** Number of bytes we need. */
    uint32_t        cb;
    /** The segment index. */
    uint16_t        idxSeg;
    /** The group index for this segment.  Initially OMF_REPLACE_GRP_XXX values,
     * later convertOmfWriteDebugGrpDefs replaces them with actual values. */
    uint16_t        idxGrp;
    /** File table. */
    POMFFILEINES    paFiles;
} OMFSEGLINES;
typedef OMFSEGLINES *POMFSEGLINES;

/** @name OMF_REPLACE_GRP_XXX - Special OMFSEGLINES::idxGrp values.
 * @{ */
#define OMF_REPLACE_GRP_CGROUP16    UINT16_C(0xffe0)
#define OMF_REPLACE_GRP_RMCODE      UINT16_C(0xffe1)
#define OMF_REPLACE_GRP_X0CODE      UINT16_C(0xffe2)
#define OMF_REPLACE_GRP_X1CODE      UINT16_C(0xffe3)
/** @} */


/**
 * OMF details allocation that needs to be freed when done.
 */
typedef struct OMFDETAILSALLOC
{
    /** Pointer to the next allocation. */
    struct OMFDETAILSALLOC *pNext;
    /** The allocated bytes. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t                 abData[RT_FLEXIBLE_ARRAY];
} OMFDETAILSALLOC;
typedef OMFDETAILSALLOC *POMFDETAILSALLOC;

/**
 * OMF conversion details.
 *
 * Keeps information relevant to the conversion and CV8 debug info.
 */
typedef struct OMFDETAILS
{
    /** The input file name. */
    const char     *pszFile;

    /** Set if it has line numbers. */
    bool            fLineNumbers;
    /** Set if we think this may be a 32-bit OMF file. */
    bool            fProbably32bit;
    /** Set if this module may need mangling. */
    bool            fMayNeedMangling;
    /** The LNAME index of '$$SYMBOLS' or UINT16_MAX it not found. */
    uint16_t        iSymbolsNm;
    /** The LNAME index of 'DEBSYM' or UINT16_MAX it not found. */
    uint16_t        iDebSymNm;
    /** The '$$SYMBOLS' segment index. */
    uint16_t        iSymbolsSeg;

    /** Number of SEGDEFs records. */
    uint16_t        cSegDefs;
    /** Number of GRPDEFs records. */
    uint16_t        cGrpDefs;
    /** Number of listed names. */
    uint16_t        cLNames;

    /** Segment defintions. */
    POMFSEGDEF      paSegDefs;
    /** Group defintions. */
    POMFGRPDEF      paGrpDefs;
    /** Name list.  Points to the size repfix. */
    char          **papchLNames;

    /** Code groups we need to keep an eye on for line number fixup purposes. */
    struct OMFLINEGROUPS
    {
        /** The name. */
        const char *pszName;
        /** The primary class name. */
        const char *pszClass1;
        /** The secondary class name. */
        const char *pszClass2;
        /** The main segment name, NULL if not applicable (CGROUP16). */
        const char *pszSeg;
        /** The name length. */
        uint8_t     cchName;
        /** The primary class name length. */
        uint8_t     cchClass1;
        /** The secondary class name length. */
        uint8_t     cchClass2;
        /** Whether this group is needed. */
        bool        fNeeded;
        /** The group index (UINT16_MAX if not found). */
        uint16_t    idxGroup;
        /** The group name. */
        uint16_t    idxName;
        /** The OMF_REPLACE_GRP_XXX value. */
        uint16_t    idxReplaceGrp;
    }               aGroups[4];

    /** CV8: Filename string table size. */
    uint32_t        cbStrTab;
    /** CV8: Filename string table allocation size (always multiple of dword,
     *  zero initialized). */
    uint32_t        cbStrTabAlloc;
    /** CV8: Filename String table. */
    char           *pchStrTab;
    /** CV8: Elements in the source info table. */
    uint16_t        cSrcInfo;
    /** CV8: Source info table. */
    PRTCV8SRCINFO   paSrcInfo;

    /** Number of entries in the paSegLines table. */
    uint32_t        cSegLines;
    /** Segment line numbers, indexed by segment number. */
    POMFSEGLINES    paSegLines;

    /** List of allocations that needs freeing. */
    POMFDETAILSALLOC    pAllocHead;
} OMFDETAILS;
typedef OMFDETAILS *POMFDETAILS;
typedef OMFDETAILS const *PCOMFDETAILS;


/** Grows a table to a given size (a_cNewEntries). */
#define OMF_GROW_TABLE_EX_RET_ERR(a_EntryType, a_paTable, a_cEntries, a_cNewEntries) \
    do\
    { \
        size_t cbOld = (a_cEntries) * sizeof(a_EntryType); \
        size_t cbNew = (a_cNewEntries) * sizeof(a_EntryType); \
        void  *pvNew = realloc(a_paTable, cbNew); \
        if (pvNew) \
        { \
            memset((uint8_t *)pvNew + cbOld, 0, cbNew - cbOld); \
            (a_paTable) = (a_EntryType *)pvNew; \
        } \
        else return error("???", "Out of memory!\n"); \
    } while (0)

/** Grows a table. */
#define OMF_GROW_TABLE_RET_ERR(a_EntryType, a_paTable, a_cEntries, a_cEvery) \
    if ((a_cEntries) % (a_cEvery) != 0) { /* likely */ } \
    else do\
    { \
        size_t cbOld = (a_cEntries) * sizeof(a_EntryType); \
        size_t cbNew = cbOld + (a_cEvery) * sizeof(a_EntryType); \
        void  *pvNew = realloc(a_paTable, cbNew); \
        if (pvNew) \
        { \
            memset((uint8_t *)pvNew + cbOld, 0, (a_cEvery) * sizeof(a_EntryType)); \
            (a_paTable) = (a_EntryType *)pvNew; \
        } \
        else return error("???", "Out of memory!\n"); \
    } while (0)

#define OMF_EXPLODE_LNAME(a_pOmfStuff, a_idxName, a_pchName, a_cchName, a_Name) \
            do { \
                if ((a_idxName) < (a_pOmfStuff)->cLNames) \
                { \
                    a_cchName = (uint8_t)*(a_pOmfStuff)->papchLNames[(a_idxName)]; \
                    a_pchName = (a_pOmfStuff)->papchLNames[(a_idxName)] + 1; \
                } \
                else return error((a_pOmfStuff)->pszFile, "Invalid LNAME reference %#x in " #a_Name "!\n", a_idxName); \
            } while (0)


/**
 * Allocates memory that will be freed when we're done converting.
 *
 * @returns Pointer tot he memory.
 * @param   pOmfStuff   The OMF details data.
 * @param   cbNeeded    The amount of memory required.
 */
static void *omfDetails_Alloc(POMFDETAILS pOmfStuff, size_t cbNeeded)
{
    POMFDETAILSALLOC pAlloc = (POMFDETAILSALLOC)malloc(RT_UOFFSETOF_DYN(OMFDETAILSALLOC, abData[cbNeeded]));
    if (pAlloc)
    {
        pAlloc->pNext = pOmfStuff->pAllocHead;
        pOmfStuff->pAllocHead = pAlloc;
        return &pAlloc->abData[0];
    }
    return NULL;
}

/**
 * Adds a line number to the CV8 debug info.
 *
 * @returns success indicator.
 * @param   pOmfStuff   Where to collect CV8 debug info.
 * @param   cchSrcFile  The length of the source file name.
 * @param   pchSrcFile  The source file name, not terminated.
 * @param   poffFile    Where to return the source file information table
 *                      offset (for use in the line number tables).
 */
static bool collectOmfAddFile(POMFDETAILS pOmfStuff, uint8_t cchSrcFile, const char *pchSrcFile, uint32_t *poffFile)
{
    /*
     * Do lookup first.
     */
    uint32_t i = pOmfStuff->cSrcInfo;
    while (i-- > 0)
    {
        const char *pszCur = &pOmfStuff->pchStrTab[pOmfStuff->paSrcInfo[i].offSourceName];
        if (   strncmp(pszCur, pchSrcFile, cchSrcFile) == 0
            && pszCur[cchSrcFile] == '\0')
        {
            *poffFile = i * sizeof(pOmfStuff->paSrcInfo[0]);
            return true;
        }
    }

    /*
     * Add it to the string table (dword aligned and zero padded).
     */
    uint32_t offSrcTab = pOmfStuff->cbStrTab;
    if (offSrcTab + cchSrcFile + 1 > pOmfStuff->cbStrTabAlloc)
    {
        uint32_t cbNew = (offSrcTab == 0) + offSrcTab + cchSrcFile + 1;
        cbNew = RT_ALIGN(cbNew, 256);
        void *pvNew = realloc(pOmfStuff->pchStrTab, cbNew);
        if (!pvNew)
            return error("???", "out of memory");
        pOmfStuff->pchStrTab     = (char *)pvNew;
        pOmfStuff->cbStrTabAlloc = cbNew;
        memset(&pOmfStuff->pchStrTab[offSrcTab], 0, cbNew - offSrcTab);

        if (!offSrcTab)
            offSrcTab++;
    }

    memcpy(&pOmfStuff->pchStrTab[offSrcTab], pchSrcFile, cchSrcFile);
    pOmfStuff->pchStrTab[offSrcTab + cchSrcFile] = '\0';
    pOmfStuff->cbStrTab = offSrcTab + cchSrcFile + 1;

    /*
     * Add it to the filename info table.
     */
    if ((pOmfStuff->cSrcInfo % 8) == 0)
    {
        void *pvNew = realloc(pOmfStuff->paSrcInfo, sizeof(pOmfStuff->paSrcInfo[0]) * (pOmfStuff->cSrcInfo + 8));
        if (!pvNew)
            return error("???", "out of memory");
        pOmfStuff->paSrcInfo = (PRTCV8SRCINFO)pvNew;
    }

    PRTCV8SRCINFO  pSrcInfo = &pOmfStuff->paSrcInfo[pOmfStuff->cSrcInfo++];
    pSrcInfo->offSourceName = offSrcTab;
    pSrcInfo->uDigestType   = RTCV8SRCINFO_DIGEST_TYPE_MD5;
    memset(&pSrcInfo->Digest, 0, sizeof(pSrcInfo->Digest));

    *poffFile = (uint32_t)((uintptr_t)pSrcInfo - (uintptr_t)pOmfStuff->paSrcInfo);
    return true;
}


/**
 * Adds a line number to the CV8 debug info.
 *
 * @returns success indicator.
 * @param   pOmfStuff   Where to collect CV8 debug info.
 * @param   idxSeg      The segment index.
 * @param   off         The segment offset.
 * @param   uLine       The line number.
 * @param   offSrcInfo  The source file info table offset.
 */
static bool collectOmfAddLine(POMFDETAILS pOmfStuff, uint16_t idxSeg, uint32_t off, uint16_t uLine, uint32_t offSrcInfo)
{
    /*
     * Get/add the segment line structure.
     */
    if (idxSeg >= pOmfStuff->cSegLines)
    {
        OMF_GROW_TABLE_EX_RET_ERR(OMFSEGLINES, pOmfStuff->paSegLines, pOmfStuff->cSegLines, idxSeg + 1);
        for (uint32_t i = pOmfStuff->cSegLines; i <= idxSeg; i++)
        {
            pOmfStuff->paSegLines[i].idxSeg = i;
            pOmfStuff->paSegLines[i].idxGrp = UINT16_MAX;
            pOmfStuff->paSegLines[i].cb = sizeof(RTCV8LINESHDR);
        }
        pOmfStuff->cSegLines = idxSeg + 1;
    }
    POMFSEGLINES pSegLines = &pOmfStuff->paSegLines[idxSeg];

    /*
     * Get/add the file structure with the segment.
     */
    POMFFILEINES pFileLines = NULL;
    uint32_t i = pSegLines->cFiles;
    while (i-- > 0)
        if (pSegLines->paFiles[i].offSrcInfo == offSrcInfo)
        {
            pFileLines = &pSegLines->paFiles[i];
            break;
        }
    if (!pFileLines)
    {
        i = pSegLines->cFiles;
        OMF_GROW_TABLE_RET_ERR(OMFFILEINES, pSegLines->paFiles, pSegLines->cFiles, 4);
        pSegLines->cFiles = i + 1;
        pSegLines->cb    += sizeof(RTCV8LINESSRCMAP);

        pFileLines = &pSegLines->paFiles[i];
        pFileLines->offSrcInfo  = offSrcInfo;
        pFileLines->cPairs      = 0;
        pFileLines->cPairsAlloc = 0;
        pFileLines->paPairs     = NULL;

        /*
         * Check for segment group requirements the first time a segment is used.
         */
        if (i == 0)
        {
            if (idxSeg >= pOmfStuff->cSegDefs)
                return error("???", "collectOmfAddLine: idxSeg=%#x is out of bounds (%#x)!\n", idxSeg, pOmfStuff->cSegDefs);
            POMFSEGDEF pSegDef = &pOmfStuff->paSegDefs[idxSeg];
            unsigned j = RT_ELEMENTS(pOmfStuff->aGroups);
            while (j-- > 0)
                if (   (   pSegDef->cchClass == pOmfStuff->aGroups[j].cchClass1
                        && memcmp(pSegDef->pchClass, pOmfStuff->aGroups[j].pszClass1, pSegDef->cchClass) == 0)
                    || (   pSegDef->cchClass == pOmfStuff->aGroups[j].cchClass2
                        && memcmp(pSegDef->pchClass, pOmfStuff->aGroups[j].pszClass2, pSegDef->cchClass) == 0))
                {
                    pOmfStuff->aGroups[j].fNeeded = true;
                    pSegLines->idxGrp = pOmfStuff->aGroups[j].idxReplaceGrp;
                    break;
                }
        }
    }

    /*
     * Add the line number (sorted, duplicates removed).
     */
    if (pFileLines->cPairs + 1 > pFileLines->cPairsAlloc)
    {
        void *pvNew = realloc(pFileLines->paPairs, (pFileLines->cPairsAlloc + 16) * sizeof(pFileLines->paPairs[0]));
        if (!pvNew)
            return error("???", "out of memory");
        pFileLines->paPairs      = (PRTCV8LINEPAIR)pvNew;
        pFileLines->cPairsAlloc += 16;
    }

    i = pFileLines->cPairs;
    while (i > 0 && (   off < pFileLines->paPairs[i - 1].offSection
                     || (   off == pFileLines->paPairs[i - 1].offSection
                         && uLine < pFileLines->paPairs[i - 1].uLineNumber)) )
        i--;
    if (   i     == pFileLines->cPairs
        || off   != pFileLines->paPairs[i].offSection
        || uLine != pFileLines->paPairs[i].uLineNumber)
    {
        if (i < pFileLines->cPairs)
            memmove(&pFileLines->paPairs[i + 1], &pFileLines->paPairs[i],
                    (pFileLines->cPairs - i) * sizeof(pFileLines->paPairs));
        pFileLines->paPairs[i].offSection      = off;
        pFileLines->paPairs[i].uLineNumber     = uLine;
        pFileLines->paPairs[i].fEndOfStatement = true;
        pFileLines->cPairs++;
        pSegLines->cb += sizeof(pFileLines->paPairs[0]);
    }

    return true;
}


/**
 * Parses OMF file gathering line numbers (for CV8 debug info) and checking out
 * external defintions for mangling work (compiler instrinsics).
 *
 * @returns success indicator.
 * @param   pszFile     The name of the OMF file.
 * @param   pbFile      The file content.
 * @param   cbFile      The size of the file content.
 * @param   pOmfStuff   Where to collect CV8 debug info and anything else we
 *                      find out about the OMF file.
 */
static bool collectOmfDetails(const char *pszFile, uint8_t const *pbFile, size_t cbFile, POMFDETAILS pOmfStuff)
{
    uint32_t        cExtDefs = 0;
    uint32_t        cPubDefs = 0;
    uint32_t        off = 0;
    uint8_t         cchSrcFile = 0;
    const char     *pchSrcFile = NULL;
    uint32_t        offSrcInfo = UINT32_MAX;

    memset(pOmfStuff, 0, sizeof(*pOmfStuff));
    pOmfStuff->pszFile      = pszFile;
    pOmfStuff->iDebSymNm    = UINT16_MAX;
    pOmfStuff->iSymbolsNm   = UINT16_MAX;
    pOmfStuff->iSymbolsSeg  = UINT16_MAX;

    /* Dummy entries. */
    OMF_GROW_TABLE_RET_ERR(char *, pOmfStuff->papchLNames, pOmfStuff->cLNames, 16);
    pOmfStuff->papchLNames[0] = (char *)"";
    pOmfStuff->cLNames = 1;

    OMF_GROW_TABLE_RET_ERR(OMFSEGDEF, pOmfStuff->paSegDefs, pOmfStuff->cSegDefs, 16);
    pOmfStuff->cSegDefs = 1;

    OMF_GROW_TABLE_RET_ERR(OMFGRPDEF, pOmfStuff->paGrpDefs, pOmfStuff->cGrpDefs, 16);
    pOmfStuff->cGrpDefs = 1;

    /* Groups we seek. */
#define OMF_INIT_WANTED_GROUP(a_idx, a_szName, a_szClass1, a_szClass2, a_pszSeg, a_idxReplace) \
        pOmfStuff->aGroups[a_idx].pszName   = a_szName; \
        pOmfStuff->aGroups[a_idx].cchName   = sizeof(a_szName) - 1; \
        pOmfStuff->aGroups[a_idx].pszClass1 = a_szClass1; \
        pOmfStuff->aGroups[a_idx].cchClass1 = sizeof(a_szClass1) - 1; \
        pOmfStuff->aGroups[a_idx].pszClass2 = a_szClass2; \
        pOmfStuff->aGroups[a_idx].cchClass2 = sizeof(a_szClass2) - 1; \
        pOmfStuff->aGroups[a_idx].pszSeg    = a_pszSeg; \
        pOmfStuff->aGroups[a_idx].fNeeded   = false; \
        pOmfStuff->aGroups[a_idx].idxGroup  = UINT16_MAX; \
        pOmfStuff->aGroups[a_idx].idxName   = UINT16_MAX; \
        pOmfStuff->aGroups[a_idx].idxReplaceGrp = a_idxReplace
    OMF_INIT_WANTED_GROUP(0, "CGROUP16",         "BS3CLASS16CODE",   "CODE", NULL,          OMF_REPLACE_GRP_CGROUP16);
    OMF_INIT_WANTED_GROUP(1, "BS3GROUPRMTEXT16", "BS3CLASS16RMCODE", "",     "BS3RMTEXT16", OMF_REPLACE_GRP_RMCODE);
    OMF_INIT_WANTED_GROUP(2, "BS3GROUPX0TEXT16", "BS3CLASS16X0CODE", "",     "BS3X0TEXT16", OMF_REPLACE_GRP_X0CODE);
    OMF_INIT_WANTED_GROUP(3, "BS3GROUPX1TEXT16", "BS3CLASS16X1CODE", "",     "BS3X1TEXT16", OMF_REPLACE_GRP_X1CODE);

    /*
     * Process the OMF records.
     */
    while (off + 3 < cbFile)
    {
        uint8_t     bRecType = pbFile[off];
        uint16_t    cbRec    = RT_MAKE_U16(pbFile[off + 1], pbFile[off + 2]);
        if (g_cVerbose > 2)
            printf( "%#07x: type=%#04x len=%#06x\n", off, bRecType, cbRec);
        if (off + cbRec > cbFile)
            return error(pszFile, "Invalid record length at %#x: %#x (cbFile=%#lx)\n", off, cbRec, (unsigned long)cbFile);

        uint32_t        offRec = 0;
        uint8_t const  *pbRec  = &pbFile[off + 3];
#define OMF_CHECK_RET(a_cbReq, a_Name) /* Not taking the checksum into account, so we're good with 1 or 2 byte fields. */ \
            if (offRec + (a_cbReq) <= cbRec) {/*likely*/} \
            else return error(pszFile, "Malformed " #a_Name "! off=%#x offRec=%#x cbRec=%#x cbNeeded=%#x line=%d\n", \
                              off, offRec, cbRec, (a_cbReq), __LINE__)
#define OMF_READ_IDX(a_idx, a_Name) \
            do { \
                OMF_CHECK_RET(2, a_Name); \
                a_idx = pbRec[offRec++]; \
                if ((a_idx) & 0x80) \
                    a_idx = (((a_idx) & 0x7f) << 8) | pbRec[offRec++]; \
            } while (0)

#define OMF_READ_U16(a_u16, a_Name) \
            do { \
                OMF_CHECK_RET(4, a_Name); \
                a_u16 = RT_MAKE_U16(pbRec[offRec], pbRec[offRec + 1]); \
                offRec += 2; \
            } while (0)
#define OMF_READ_U32(a_u32, a_Name) \
            do { \
                OMF_CHECK_RET(4, a_Name); \
                a_u32 = RT_MAKE_U32_FROM_U8(pbRec[offRec], pbRec[offRec + 1], pbRec[offRec + 2], pbRec[offRec + 3]); \
                offRec += 4; \
            } while (0)

        switch (bRecType)
        {
            /*
             * Record LNAME records, scanning for FLAT.
             */
            case OMF_LNAMES:
                while (offRec + 1 < cbRec)
                {
                    uint8_t cch = pbRec[offRec];
                    if (offRec + 1 + cch >= cbRec)
                        return error(pszFile, "Invalid LNAME string length at %#x+3+%#x: %#x (cbFile=%#lx)\n",
                                     off, offRec, cch, (unsigned long)cbFile);

                    if (g_cVerbose > 2)
                        printf("  LNAME[%u]: %-*.*s\n", pOmfStuff->cLNames, cch, cch, &pbRec[offRec + 1]);

                    OMF_GROW_TABLE_RET_ERR(char *, pOmfStuff->papchLNames, pOmfStuff->cLNames, 16);
                    pOmfStuff->papchLNames[pOmfStuff->cLNames] = (char *)&pbRec[offRec];

                    if (IS_OMF_STR_EQUAL_EX(cch, &pbRec[offRec + 1], "FLAT"))
                        pOmfStuff->fProbably32bit = true;

                    if (IS_OMF_STR_EQUAL_EX(cch, &pbRec[offRec + 1], "DEBSYM"))
                        pOmfStuff->iDebSymNm = pOmfStuff->cLNames;
                    if (IS_OMF_STR_EQUAL_EX(cch, &pbRec[offRec + 1], "$$SYMBOLS"))
                        pOmfStuff->iSymbolsNm = pOmfStuff->cLNames;

                    unsigned j = RT_ELEMENTS(pOmfStuff->aGroups);
                    while (j-- > 0)
                        if (   cch == pOmfStuff->aGroups[j].cchName
                            && memcmp(&pbRec[offRec + 1], pOmfStuff->aGroups[j].pszName, pOmfStuff->aGroups[j].cchName) == 0)
                        {
                            pOmfStuff->aGroups[j].idxName = pOmfStuff->cLNames;
                            break;
                        }

                    pOmfStuff->cLNames++;
                    offRec += cch + 1;
                }
                break;

            /*
             * Display external definitions if -v is specified, also check if anything needs mangling.
             */
            case OMF_EXTDEF:
                while (offRec + 1 < cbRec)
                {
                    uint8_t cch = pbRec[offRec++];
                    OMF_CHECK_RET(cch, EXTDEF);
                    char *pchName = (char *)&pbRec[offRec];
                    offRec += cch;

                    uint16_t idxType;
                    OMF_READ_IDX(idxType, EXTDEF);

                    if (g_cVerbose > 2)
                        printf("  EXTDEF [%u]: %-*.*s type=%#x\n", cExtDefs, cch, cch, pchName, idxType);
                    else if (g_cVerbose > 0)
                        printf("              U %-*.*s\n", cch, cch, pchName);

                    /* Look for g_apszExtDefRenames entries that requires changing. */
                    if (   !pOmfStuff->fMayNeedMangling
                        && cch >= 5
                        && cch <= 7
                        && pchName[0] == '_'
                        && pchName[1] == '_'
                        && (   pchName[2] == 'U'
                            || pchName[2] == 'I'
                            || pchName[2] == 'P')
                        && (   pchName[3] == '4'
                            || pchName[3] == '8'
                            || pchName[3] == 'I'
                            || pchName[3] == 'T') )
                    {
                        pOmfStuff->fMayNeedMangling = true;
                    }
                }
                break;

            /*
             * Display public names if -v is specified.
             */
            case OMF_PUBDEF32:
            case OMF_LPUBDEF32:
                pOmfStuff->fProbably32bit = true;
                RT_FALL_THRU();
            case OMF_PUBDEF16:
            case OMF_LPUBDEF16:
                if (g_cVerbose > 0)
                {
                    char const  chType  = bRecType == OMF_PUBDEF16 || bRecType == OMF_PUBDEF32 ? 'T' : 't';
                    const char *pszRec = "LPUBDEF";
                    if (chType == 'T')
                        pszRec++;

                    uint16_t idxGrp;
                    OMF_READ_IDX(idxGrp, [L]PUBDEF);

                    uint16_t idxSeg;
                    OMF_READ_IDX(idxSeg, [L]PUBDEF);

                    uint16_t uFrameBase = 0;
                    if (idxSeg == 0)
                    {
                        OMF_CHECK_RET(2, [L]PUBDEF);
                        uFrameBase = RT_MAKE_U16(pbRec[offRec], pbRec[offRec + 1]);
                        offRec += 2;
                    }
                    if (g_cVerbose > 2)
                        printf("  %s: idxGrp=%#x idxSeg=%#x uFrameBase=%#x\n", pszRec, idxGrp, idxSeg, uFrameBase);
                    uint16_t const uSeg = idxSeg ? idxSeg : uFrameBase;

                    while (offRec + 1 < cbRec)
                    {
                        uint8_t cch = pbRec[offRec++];
                        OMF_CHECK_RET(cch, [L]PUBDEF);
                        const char *pchName = (const char *)&pbRec[offRec];
                        offRec += cch;

                        uint32_t offSeg;
                        if (bRecType & OMF_REC32)
                        {
                            OMF_CHECK_RET(4, [L]PUBDEF);
                            offSeg = RT_MAKE_U32_FROM_U8(pbRec[offRec], pbRec[offRec + 1], pbRec[offRec + 2], pbRec[offRec + 3]);
                            offRec += 4;
                        }
                        else
                        {
                            OMF_CHECK_RET(2, [L]PUBDEF);
                            offSeg = RT_MAKE_U16(pbRec[offRec], pbRec[offRec + 1]);
                            offRec += 2;
                        }

                        uint16_t idxType;
                        OMF_READ_IDX(idxType, [L]PUBDEF);

                        if (g_cVerbose > 2)
                            printf("  %s[%u]: off=%#010x type=%#x %-*.*s\n", pszRec, cPubDefs, offSeg, idxType, cch, cch, pchName);
                        else if (g_cVerbose > 0)
                            printf("%04x:%08x %c %-*.*s\n", uSeg, offSeg, chType, cch, cch, pchName);
                    }
                }
                break;

            /*
             * Must count segment definitions to figure the index of our segment.
             */
            case OMF_SEGDEF16:
            case OMF_SEGDEF32:
            {
                OMF_GROW_TABLE_RET_ERR(OMFSEGDEF, pOmfStuff->paSegDefs, pOmfStuff->cSegDefs, 16);
                POMFSEGDEF pSegDef = &pOmfStuff->paSegDefs[pOmfStuff->cSegDefs++];

                OMF_CHECK_RET(1 + (bRecType == OMF_SEGDEF16 ? 2 : 4) + 1 + 1 + 1, SEGDEF);
                pSegDef->f32bitRec  = bRecType == OMF_SEGDEF32;
                pSegDef->bSegAttr   = pbRec[offRec++];
                pSegDef->fUse32     = pSegDef->bSegAttr & 1;
                if ((pSegDef->bSegAttr >> 5) == 0)
                {
                    /* A=0: skip frame number of offset. */
                    OMF_CHECK_RET(3, SEGDEF);
                    offRec += 3;
                }
                if (bRecType == OMF_SEGDEF16)
                    OMF_READ_U16(pSegDef->cbSeg, SEGDEF16);
                else
                    OMF_READ_U32(pSegDef->cbSeg, SEGDEF32);
                OMF_READ_IDX(pSegDef->idxName, SEGDEF);
                OMF_READ_IDX(pSegDef->idxClass, SEGDEF);
                OMF_READ_IDX(pSegDef->idxOverlay, SEGDEF);
                OMF_EXPLODE_LNAME(pOmfStuff, pSegDef->idxName, pSegDef->pchName, pSegDef->cchName, SEGDEF);
                OMF_EXPLODE_LNAME(pOmfStuff, pSegDef->idxClass, pSegDef->pchClass, pSegDef->cchClass, SEGDEF);
                OMF_EXPLODE_LNAME(pOmfStuff, pSegDef->idxOverlay, pSegDef->pchOverlay, pSegDef->cchOverlay, SEGDEF);
                break;
            }

            /*
             * Must count segment definitions to figure the index of our group.
             */
            case OMF_GRPDEF:
            {
                OMF_GROW_TABLE_RET_ERR(OMFGRPDEF, pOmfStuff->paGrpDefs, pOmfStuff->cGrpDefs, 8);
                POMFGRPDEF pGrpDef = &pOmfStuff->paGrpDefs[pOmfStuff->cGrpDefs];

                OMF_READ_IDX(pGrpDef->idxName, GRPDEF);
                OMF_EXPLODE_LNAME(pOmfStuff, pGrpDef->idxName, pGrpDef->pchName, pGrpDef->cchName, GRPDEF);

                unsigned j = RT_ELEMENTS(pOmfStuff->aGroups);
                while (j-- > 0)
                    if (pGrpDef->idxName == pOmfStuff->aGroups[j].idxName)
                    {
                        pOmfStuff->aGroups[j].idxGroup = pOmfStuff->cGrpDefs;
                        break;
                    }

                pGrpDef->cSegDefs    = 0;
                pGrpDef->paidxSegDefs = NULL;
                while (offRec + 2 + 1 <= cbRec)
                {
                    if (pbRec[offRec] != 0xff)
                        return error(pszFile, "Unsupported GRPDEF member type: %#x\n", pbRec[offRec]);
                    offRec++;
                    OMF_GROW_TABLE_RET_ERR(uint16_t, pGrpDef->paidxSegDefs, pGrpDef->cSegDefs, 16);
                    OMF_READ_IDX(pGrpDef->paidxSegDefs[pGrpDef->cSegDefs], GRPDEF);
                    pGrpDef->cSegDefs++;
                }
                pOmfStuff->cGrpDefs++;
                break;
            }

            /*
             * Gather file names.
             */
            case OMF_THEADR: /* watcom */
                cchSrcFile = pbRec[offRec++];
                OMF_CHECK_RET(cchSrcFile, OMF_THEADR);
                pchSrcFile = (const char *)&pbRec[offRec];
                if (!collectOmfAddFile(pOmfStuff, cchSrcFile, pchSrcFile, &offSrcInfo))
                    return false;
                break;

            case OMF_COMENT:
            {
                OMF_CHECK_RET(2, COMENT);
                offRec++; /* skip the type (flags) */
                uint8_t bClass = pbRec[offRec++];
                if (bClass == OMF_CCLS_BORLAND_SRC_FILE) /* nasm */
                {
                    OMF_CHECK_RET(1+1+4, BORLAND_SRC_FILE);
                    offRec++; /* skip unknown byte */
                    cchSrcFile = pbRec[offRec++];
                    OMF_CHECK_RET(cchSrcFile + 4, BORLAND_SRC_FILE);
                    pchSrcFile = (const char *)&pbRec[offRec];
                    offRec += cchSrcFile;
                    if (offRec + 4 + 1 != cbRec)
                        return error(pszFile, "BAD BORLAND_SRC_FILE record at %#x: %d bytes left\n",
                                     off, cbRec - offRec - 4 - 1);
                    if (!collectOmfAddFile(pOmfStuff, cchSrcFile, pchSrcFile, &offSrcInfo))
                        return false;
                    break;
                }
                break;
            }

            /*
             * Line number conversion.
             */
            case OMF_LINNUM16:
            case OMF_LINNUM32:
            {
                uint16_t idxGrp;
                OMF_READ_IDX(idxGrp, LINNUM);
                uint16_t idxSeg;
                OMF_READ_IDX(idxSeg, LINNUM);

                uint16_t iLine;
                uint32_t offSeg;
                if (bRecType == OMF_LINNUM16)
                    while (offRec + 4 < cbRec)
                    {
                        iLine  = RT_MAKE_U16(pbRec[offRec + 0], pbRec[offRec + 1]);
                        offSeg = RT_MAKE_U16(pbRec[offRec + 2], pbRec[offRec + 3]);
                        if (!collectOmfAddLine(pOmfStuff, idxSeg, offSeg, iLine, offSrcInfo))
                            return false;
                        offRec += 4;
                    }
                else
                    while (offRec + 6 < cbRec)
                    {
                        iLine  = RT_MAKE_U16(pbRec[offRec + 0], pbRec[offRec + 1]);
                        offSeg = RT_MAKE_U32_FROM_U8(pbRec[offRec + 2], pbRec[offRec + 3], pbRec[offRec + 4], pbRec[offRec + 5]);
                        if (!collectOmfAddLine(pOmfStuff, idxSeg, offSeg, iLine, offSrcInfo))
                            return false;
                        offRec += 6;
                    }
                if (offRec + 1 != cbRec)
                    return error(pszFile, "BAD LINNUM record at %#x: %d bytes left\n", off, cbRec - offRec - 1);
                break;
            }
        }

        /* advance */
        off += cbRec + 3;
    }

    return true;
#undef OMF_READ_IDX
#undef OMF_CHECK_RET
}


/**
 * Adds a LNAMES entry (returns existing).
 *
 * @returns success indicator.
 * @param   pOmfStuff       The OMF stuff.
 * @param   pszName         The name to add.
 * @param   pidxName        Where to return the name index.
 */
static bool omfDetails_AddLName(POMFDETAILS pOmfStuff, const char *pszName, uint16_t *pidxName)
{
    size_t const cchName = strlen(pszName);

    /*
     * Check if we've already got the name.
     */
    for (unsigned iName = 1; iName < pOmfStuff->cLNames; iName++)
        if (   (unsigned char)pOmfStuff->papchLNames[iName][0] == cchName
            && memcmp(pOmfStuff->papchLNames[iName] + 1, pszName, cchName) == 0)
        {
            *pidxName = iName;
            return true;
        }

    /*
     * Not found, append it.
     */
    char *pszCopy = (char *)omfDetails_Alloc(pOmfStuff, cchName + 2);
    if (!pszCopy)
        return false;
    *(unsigned char *)&pszCopy[0] = (unsigned char)cchName;
    memcpy(pszCopy + 1, pszName, cchName + 1);

    OMF_GROW_TABLE_RET_ERR(char *, pOmfStuff->papchLNames, pOmfStuff->cLNames, 16);
    pOmfStuff->papchLNames[pOmfStuff->cLNames] = (char *)pszCopy;
    *pidxName = pOmfStuff->cLNames;
    pOmfStuff->cLNames++;
    return true;
}


/**
 * Adds a SEGDEF (always adds a new one).
 *
 * @returns success indicator.
 * @param   pOmfStuff   The OMF stuff.
 * @param   bSegAttr    The OMF segment attributes.
 * @param   cbSeg       The segment size.
 * @param   idxSegName  The LNAMES index of the segment name.
 * @param   idxSegClas  The LNAMES index of the segment class.
 * @param   idxOverlay  The LNAMES index of the overlay name; pass 1.
 * @param   fRec32      Set if SEGDEF32 should be emitted, clear for SEGDEF16.
 * @param   pidxSeg     Where to return the segment index.
 */
static bool omfDetails_AddSegDef(POMFDETAILS pOmfStuff, uint8_t bSegAttr, uint32_t cbSeg, uint16_t idxSegName,
                                 uint16_t idxSegClass, uint16_t idxOverlay, bool fRec32, uint16_t *pidxSeg)
{
    Assert(cbSeg <= UINT16_MAX || fRec32);
    Assert(idxSegName < pOmfStuff->cLNames);
    Assert(idxSegClass < pOmfStuff->cLNames);

    OMF_GROW_TABLE_RET_ERR(OMFSEGDEF, pOmfStuff->paSegDefs, pOmfStuff->cSegDefs, 16);
    POMFSEGDEF pSegDef = &pOmfStuff->paSegDefs[pOmfStuff->cSegDefs];

    pSegDef->bSegAttr   = bSegAttr;
    pSegDef->fUse32     = bSegAttr & 1;
    pSegDef->f32bitRec  = fRec32;
    pSegDef->cbSeg      = cbSeg;
    pSegDef->idxName    = idxSegName;
    pSegDef->idxClass   = idxSegClass;
    pSegDef->idxOverlay = idxOverlay;

    OMF_EXPLODE_LNAME(pOmfStuff, pSegDef->idxName, pSegDef->pchName, pSegDef->cchName, SEGDEF);
    OMF_EXPLODE_LNAME(pOmfStuff, pSegDef->idxClass, pSegDef->pchClass, pSegDef->cchClass, SEGDEF);
    OMF_EXPLODE_LNAME(pOmfStuff, pSegDef->idxOverlay, pSegDef->pchOverlay, pSegDef->cchOverlay, SEGDEF);

    *pidxSeg = pOmfStuff->cSegDefs;
    pOmfStuff->cSegDefs++;
    return true;
}


/**
 * Adds a SEGDEF if not found.
 *
 * @returns success indicator.
 * @param   pOmfStuff   The OMF stuff.
 * @param   bSegAttr    The OMF segment attributes.
 * @param   cbSeg       The segment size.
 * @param   idxSegName  The LNAMES index of the segment name.
 * @param   idxSegClas  The LNAMES index of the segment class.
 * @param   idxOverlay  The LNAMES index of the overlay name; pass 1.
 * @param   fRec32      Set if SEGDEF32 should be emitted, clear for SEGDEF16.
 * @param   pidxSeg     Where to return the segment index.
 */
static bool omfDetails_AddSegDefIfNeeded(POMFDETAILS pOmfStuff, uint8_t bSegAttr, uint32_t cbSeg, uint16_t idxSegName,
                                         uint16_t idxSegClass, uint16_t idxOverlay, bool fRec32, uint16_t *pidxSeg)
{
    /* Search for name */
    for (unsigned iSegDef = 1; iSegDef < pOmfStuff->cSegDefs; iSegDef++)
    {
        POMFSEGDEF pSegDef = &pOmfStuff->paSegDefs[iSegDef];
        if (pSegDef->idxName == idxSegName)
        {
            if (   pSegDef->bSegAttr   != bSegAttr
                || pSegDef->f32bitRec  != fRec32
                || pSegDef->idxName    != idxSegName
                || pSegDef->idxClass   != idxSegClass
                || pSegDef->idxOverlay != idxOverlay)
                return error(pOmfStuff->pszFile,
                             "Existing SEGDEF differs: bSegAttr=%#x vs %#x, f32bitRec=%d vs %d, idxName=%#x vs %#x, idxClass=%#x vs %#x, idxOverlay=%#x vs %#x\n",
                             pSegDef->bSegAttr,   bSegAttr,
                             pSegDef->f32bitRec,  fRec32,
                             pSegDef->idxName,    idxSegName,
                             pSegDef->idxClass,   idxSegClass,
                             pSegDef->idxOverlay, idxOverlay);
            *pidxSeg = iSegDef;
            return true;
        }
    }
    return omfDetails_AddSegDef(pOmfStuff, bSegAttr, cbSeg, idxSegName, idxSegClass, idxOverlay, fRec32, pidxSeg);
}


#if 0 /* unused */
/**
 * Looks up a GRPDEF in the .
 *
 * @returns Index (0..32K) if found, UINT16_MAX if not found.
 * @param   pOmfStuff   The OMF stuff.
 * @param   pchName     The name to look up.
 * @param   cchName     The length of the name.
 */
static uint16_t omfDetails_GrpDefLookupN(POMFDETAILS pOmfStuff, const char *pchName, size_t cchName)
{
    unsigned iGrpDef = pOmfStuff->cGrpDefs;
    while (iGrpDef-- > 0)
    {
        if (   pOmfStuff->paGrpDefs[iGrpDef].cchName == cchName
            && memcmp(pOmfStuff->paGrpDefs[iGrpDef].pchName, pchName, cchName) == 0)
            return iGrpDef;
    }
    return UINT16_MAX;
}
#endif


/**
 * Adds an empty GRPDEF (always adds a new one).
 *
 * @returns success indicator.
 * @param   pOmfStuff   The OMF stuff.
 * @param   idxGrpName  The LNAMES index of the group name.
 * @param   pidxGrp     Where to return the group index.
 */
static bool omfDetails_AddGrpDef(POMFDETAILS pOmfStuff, uint16_t idxGrpName, uint16_t *pidxGrp)
{
    Assert(idxGrpName < pOmfStuff->cLNames);

    OMF_GROW_TABLE_RET_ERR(OMFGRPDEF, pOmfStuff->paGrpDefs, pOmfStuff->cGrpDefs, 8);
    POMFGRPDEF pGrpDef = &pOmfStuff->paGrpDefs[pOmfStuff->cGrpDefs];

    pGrpDef->idxName      = idxGrpName;
    pGrpDef->cSegDefs     = 0;
    pGrpDef->paidxSegDefs = NULL;

    *pidxGrp = pOmfStuff->cGrpDefs;
    pOmfStuff->cGrpDefs++;
    return true;
}


/**
 * Adds a segment to an existing GRPDEF.
 *
 * @returns success indicator.
 * @param   pOmfStuff   The OMF stuff.
 * @param   idxGrp      The GRPDEF index of the group to append a member to.
 * @param   idxSeg      The SEGDEF index of the segment name.
 */
static bool omfDetails_AddSegToGrpDef(POMFDETAILS pOmfStuff, uint16_t idxGrp, uint16_t idxSeg)
{
    Assert(idxGrp < pOmfStuff->cGrpDefs && idxGrp > 0);
    Assert(idxSeg < pOmfStuff->cSegDefs && idxSeg > 0);

    POMFGRPDEF pGrpDef = &pOmfStuff->paGrpDefs[idxGrp];
    OMF_GROW_TABLE_RET_ERR(uint16_t, pGrpDef->paidxSegDefs, pGrpDef->cSegDefs, 16);
    pGrpDef->paidxSegDefs[pGrpDef->cSegDefs] = idxSeg;
    pGrpDef->cSegDefs++;

    return true;
}


/**
 * Marks 16-bit code segment groups that is used in the object file as needed.
 *
 * @param   pOmfStuff   The OMF stuff.
 */
static void convertOmfLookForNeededGroups(POMFDETAILS pOmfStuff)
{
    /*
     * Consult the groups in question.  We mark the groups which segments are
     * included in the segment definitions as needed.
     */
    unsigned i = RT_ELEMENTS(pOmfStuff->aGroups);
    while (i-- > 0)
        if (pOmfStuff->aGroups[i].pszSeg)
        {
            const char * const  pszSegNm = pOmfStuff->aGroups[i].pszSeg;
            size_t const        cchSegNm = strlen(pszSegNm);
            for (unsigned iSegDef = 0; iSegDef < pOmfStuff->cSegDefs; iSegDef++)
                if (   pOmfStuff->paSegDefs[iSegDef].cchName == cchSegNm
                    && memcmp(pOmfStuff->paSegDefs[iSegDef].pchName, pszSegNm, cchSegNm) == 0)
                {
                    pOmfStuff->aGroups[i].fNeeded = true;
                    break;
                }
        }
}


/**
 * Adds necessary group and segment definitions.
 *
 * @returns success indicator.
 * @param   pOmfStuff   The OMF stuff.
 */
static bool convertOmfAddNeededGrpDefs(POMFDETAILS pOmfStuff)
{
    /*
     * Process the groups.
     */
    unsigned j = RT_ELEMENTS(pOmfStuff->aGroups);
    while (j-- > 0)
        if (pOmfStuff->aGroups[j].fNeeded)
        {
            if (pOmfStuff->aGroups[j].idxName == UINT16_MAX)
            {
                Assert(pOmfStuff->aGroups[j].idxGroup == UINT16_MAX);
                if (!omfDetails_AddLName(pOmfStuff, pOmfStuff->aGroups[j].pszName, &pOmfStuff->aGroups[j].idxName))
                    return false;
            }
            if (pOmfStuff->aGroups[j].idxGroup == UINT16_MAX)
            {
                if (!omfDetails_AddGrpDef(pOmfStuff, pOmfStuff->aGroups[j].idxName, &pOmfStuff->aGroups[j].idxGroup))
                    return false;

                if (pOmfStuff->aGroups[j].pszSeg)
                {
                    /* We need the segment class name. */
                    uint16_t idxSegClass;
                    if (!omfDetails_AddLName(pOmfStuff, pOmfStuff->aGroups[j].pszClass1, &idxSegClass))
                        return false;

                    /* Prep segment name buffer. */
                    size_t   cchSegNm = strlen(pOmfStuff->aGroups[j].pszSeg);
                    char     szSegNm[256+16];
                    Assert(cchSegNm < 256);
                    memcpy(szSegNm, pOmfStuff->aGroups[j].pszSeg, cchSegNm);

                    /* Add the three segments. */
                    static RTSTRTUPLE const s_aSuffixes[3] = { {RT_STR_TUPLE("_START")}, {RT_STR_TUPLE("")}, {RT_STR_TUPLE("_END")}, };
                    for (unsigned iSuffix = 0; iSuffix < RT_ELEMENTS(s_aSuffixes); iSuffix++)
                    {
                        uint16_t idxSegNm;
                        memcpy(&szSegNm[cchSegNm], s_aSuffixes[iSuffix].psz, s_aSuffixes[iSuffix].cch + 1);
                        if (!omfDetails_AddLName(pOmfStuff, szSegNm, &idxSegNm))
                            return false;
                        uint8_t  const  fAlign = iSuffix == 1 ? OMF_SEG_ATTR_ALIGN_BYTE : OMF_SEG_ATTR_ALIGN_PARA;
                        uint16_t        idxSeg;
                        if (!omfDetails_AddSegDefIfNeeded(pOmfStuff, fAlign | OMF_SEG_ATTR_COMB_PUBLIC | OMF_SEG_ATTR_USE16,
                                                          0, idxSegNm, idxSegClass, 1, false /*fRec*/, &idxSeg))
                            return false;
                        if (!omfDetails_AddSegToGrpDef(pOmfStuff, pOmfStuff->aGroups[j].idxGroup, idxSeg))
                            return false;
                    }
                }
            }
        }

    /*
     * Replace group references in the segment lines table.
     */
    j = RT_ELEMENTS(pOmfStuff->aGroups);
    while (j-- > 0)
        if (pOmfStuff->aGroups[j].fNeeded)
            for (unsigned i = 0; i < pOmfStuff->cSegLines; i++)
                if (pOmfStuff->paSegLines[i].idxGrp == pOmfStuff->aGroups[j].idxReplaceGrp)
                    pOmfStuff->paSegLines[i].idxGrp = pOmfStuff->aGroups[j].idxGroup;
    return true;
}


/**
 * Adds the debug segment definitions (names too) to the OMF state.
 *
 * @returns success indicator.
 * @param   pOmfStuff   The OMF stuff with CV8 line number info.
 */
static bool convertOmfAddDebugSegDefs(POMFDETAILS pOmfStuff)
{
    if (   pOmfStuff->cSegLines == 0
        || pOmfStuff->iSymbolsSeg != UINT16_MAX)
        return true;

    /*
     * Add the names we need.
     */
    if (   pOmfStuff->iSymbolsNm == UINT16_MAX
        && !omfDetails_AddLName(pOmfStuff, "$$SYMBOLS", &pOmfStuff->iSymbolsNm))
        return false;
    if (   pOmfStuff->iDebSymNm == UINT16_MAX
        && !omfDetails_AddLName(pOmfStuff, "DEBSYM", &pOmfStuff->iDebSymNm))
        return false;

    /*
     * Add the segment definition.
     */
    uint8_t   bSegAttr = 0;
    bSegAttr |= 5 << 5; /* A: dword alignment */
    bSegAttr |= 0 << 2; /* C: private */
    bSegAttr |= 0 << 1; /* B: not big */
    bSegAttr |= 1;      /* D: use32 */

    /* calc the segment size. */
    uint32_t  cbSeg = 4; /* dword 4 */
    cbSeg += 4 + 4 + RT_ALIGN_32(pOmfStuff->cbStrTab, 4);
    cbSeg += 4 + 4 + pOmfStuff->cSrcInfo * sizeof(pOmfStuff->paSrcInfo[0]);
    uint32_t i = pOmfStuff->cSegLines;
    while (i-- > 0)
        if (pOmfStuff->paSegLines[i].cFiles > 0)
            cbSeg += 4 + 4 + pOmfStuff->paSegLines[i].cb;
    return omfDetails_AddSegDef(pOmfStuff, bSegAttr, cbSeg, pOmfStuff->iSymbolsNm, pOmfStuff->iDebSymNm, 1 /*idxOverlay*/,
                                true /*fRec32*/, &pOmfStuff->iSymbolsSeg);
}


/**
 * Writes the debug segment data.
 *
 * @returns success indicator.
 * @param   pThis       The OMF writer.
 * @param   pOmfStuff   The OMF stuff with CV8 line number info.
 */
static bool convertOmfWriteDebugData(POMFWRITER pThis, POMFDETAILS pOmfStuff)
{
    if (pOmfStuff->cSegLines == 0)
        return true;
    Assert(pOmfStuff->iSymbolsSeg != UINT16_MAX);

    /* Begin and write the CV version signature. */
    if (   !omfWriter_LEDataBegin(pThis, pOmfStuff->iSymbolsSeg, 0)
        || !omfWriter_LEDataAddU32(pThis, RTCVSYMBOLS_SIGNATURE_CV8))
        return false;

    /*
     * Emit the string table (no fixups).
     */
    uint32_t cbLeft = pOmfStuff->cbStrTab;
    if (   !omfWriter_LEDataAddU32(pThis, RTCV8SYMBLOCK_TYPE_SRC_STR)
        || !omfWriter_LEDataAddU32(pThis, cbLeft)
        || !omfWriter_LEDataAddBytes(pThis, pOmfStuff->pchStrTab, RT_ALIGN_32(cbLeft, 4)) ) /* table is zero padded to nearest dword */
        return false;

    /*
     * Emit the source file info table (no fixups).
     */
    cbLeft = pOmfStuff->cSrcInfo * sizeof(pOmfStuff->paSrcInfo[0]);
    if (   !omfWriter_LEDataAddU32(pThis, RTCV8SYMBLOCK_TYPE_SRC_INFO)
        || !omfWriter_LEDataAddU32(pThis, cbLeft)
        || !omfWriter_LEDataAddBytes(pThis, pOmfStuff->paSrcInfo, cbLeft) )
        return false;

    /*
     * Emit the segment line numbers. There are two fixups here at the start
     * of each chunk.
     */
    POMFSEGLINES pSegLines = pOmfStuff->paSegLines;
    uint32_t     i         = pOmfStuff->cSegLines;
    while (i-- > 0)
    {
        if (pSegLines->cFiles)
        {
            /* Calc covered area. */
            uint32_t cbSectionCovered = 0;
            uint32_t j = pSegLines->cFiles;
            while (j-- > 0)
            {
                uint32_t offLast = pSegLines->paFiles[j].paPairs[pSegLines->paFiles[j].cPairs - 1].offSection;
                if (offLast > cbSectionCovered)
                    offLast = cbSectionCovered;
            }

            /* For simplicity and debuggability, just split the LEDATA here. */
            if (   !omfWriter_LEDataSplit(pThis)
                || !omfWriter_LEDataAddU32(pThis, RTCV8SYMBLOCK_TYPE_SECT_LINES)
                || !omfWriter_LEDataAddU32(pThis, pSegLines->cb)
                || !omfWriter_LEDataAddU32(pThis, 0)                /*RTCV8LINESHDR::offSection*/
                || !omfWriter_LEDataAddU16(pThis, 0)                /*RTCV8LINESHDR::iSection*/
                || !omfWriter_LEDataAddU16(pThis, 0)                /*RTCV8LINESHDR::u16Padding*/
                || !omfWriter_LEDataAddU32(pThis, cbSectionCovered) /*RTCV8LINESHDR::cbSectionCovered*/ )
                return false;

            /* Default to the segment (BS3TEXT32, BS3TEXT64) or the group (CGROUP16,
               RMGROUP16, etc).  The important thing is that we're framing the fixups
               using a segment or group which ends up in the codeview segment map. */
            uint16_t idxFrame = pSegLines->idxSeg;
            uint8_t  bFrame   = OMF_FIX_F_SEGDEF;
            if (pSegLines->idxGrp != UINT16_MAX)
            {
                idxFrame = pSegLines->idxGrp;
                bFrame   = OMF_FIX_F_GRPDEF;
            }

            /* Fixup #1: segment offset - IMAGE_REL_AMD64_SECREL. */
            if (!omfWriter_LEDataAddFixupNoDisp(pThis, 4 + 4 + RT_UOFFSETOF(RTCV8LINESHDR, offSection), OMF_FIX_LOC_32BIT_OFFSET,
                                                bFrame, idxFrame, OMF_FIX_T_SEGDEF_NO_DISP, pSegLines->idxSeg))
                return false;


            /* Fixup #2: segment number - IMAGE_REL_AMD64_SECTION. */
            if (!omfWriter_LEDataAddFixupNoDisp(pThis, 4 + 4 + RT_UOFFSETOF(RTCV8LINESHDR, iSection), OMF_FIX_LOC_16BIT_SEGMENT,
                                                bFrame, idxFrame, OMF_FIX_T_SEGDEF_NO_DISP, pSegLines->idxSeg))
                return false;

            /* Emit data for each source file. */
            for (j = 0; j < pSegLines->cFiles; j++)
            {
                uint32_t const cbPairs = pSegLines->paFiles[j].cPairs * sizeof(RTCV8LINEPAIR);
                if (   !omfWriter_LEDataAddU32(pThis, pSegLines->paFiles[j].offSrcInfo)   /*RTCV8LINESSRCMAP::offSourceInfo*/
                    || !omfWriter_LEDataAddU32(pThis, pSegLines->paFiles[j].cPairs)       /*RTCV8LINESSRCMAP::cLines*/
                    || !omfWriter_LEDataAddU32(pThis, cbPairs + sizeof(RTCV8LINESSRCMAP)) /*RTCV8LINESSRCMAP::cb*/
                    || !omfWriter_LEDataAddBytes(pThis, pSegLines->paFiles[j].paPairs, cbPairs))
                    return false;
            }
        }
        pSegLines++;
    }

    return omfWriter_LEDataEnd(pThis);
}


/**
 * Writes out all the segment group definitions.
 *
 * @returns success indicator.
 * @param   pThis           The OMF writer.
 * @param   pOmfStuff       The OMF stuff containing the segment defs.
 * @param   pfFlushState    Pointer to the flush state variable.
 */
static bool convertOmfWriteAllSegDefs(POMFWRITER pThis, POMFDETAILS pOmfStuff, int *pfFlushState)
{
    if (*pfFlushState > 0)
    {
        for (unsigned iSegDef = 1; iSegDef < pOmfStuff->cSegDefs; iSegDef++)
        {
            if (!(pOmfStuff->paSegDefs[iSegDef].f32bitRec
                  ? omfWriter_SegDef : omfWriter_SegDef16)(pThis, pOmfStuff->paSegDefs[iSegDef].bSegAttr,
                                                           pOmfStuff->paSegDefs[iSegDef].cbSeg,
                                                           pOmfStuff->paSegDefs[iSegDef].idxName,
                                                           pOmfStuff->paSegDefs[iSegDef].idxClass,
                                                           pOmfStuff->paSegDefs[iSegDef].idxOverlay))
                    return false;
        }
        *pfFlushState = -1;
    }
    return true;
}


/**
 * Writes out all the segment group definitions.
 *
 * @returns success indicator.
 * @param   pThis       The OMF writer.
 * @param   pOmfStuff   The OMF stuff containing the group defs.
 * @param   pfFlushState    Pointer to the flush state variable.
 */
static bool convertOmfWriteAllGrpDefs(POMFWRITER pThis, POMFDETAILS pOmfStuff, int *pfFlushState)
{
    if (*pfFlushState > 0)
    {
        for (unsigned iGrpDef = 1; iGrpDef < pOmfStuff->cGrpDefs; iGrpDef++)
        {
            if (!omfWriter_GrpDefBegin(pThis, pOmfStuff->paGrpDefs[iGrpDef].idxName))
                return false;
            for (unsigned iSegDef = 0; iSegDef < pOmfStuff->paGrpDefs[iGrpDef].cSegDefs; iSegDef++)
                if (!omfWriter_GrpDefAddSegDef(pThis, pOmfStuff->paGrpDefs[iGrpDef].paidxSegDefs[iSegDef]))
                    return false;
            if (!omfWriter_GrpDefEnd(pThis))
                return false;
        }
        *pfFlushState = -1;
    }
    return true;
}


/**
 * This does the actual converting, passthru style.
 *
 * It only modifies, removes and inserts stuff it care about, the rest is passed
 * thru as-is.
 *
 * @returns success indicator.
 * @param   pThis       The OMF writer.
 * @param   pbFile      The original file content.
 * @param   cbFile      The size of the original file.
 * @param   pOmfStuff   The OMF stuff we've gathered during the first pass,
 *                      contains CV8 line number info if we converted anything.
 * @param   fConvertLineNumbers     Whether we're converting line numbers and stuff.
 */
static bool convertOmfPassthru(POMFWRITER pThis, uint8_t const *pbFile, size_t cbFile, POMFDETAILS pOmfStuff,
                               bool fConvertLineNumbers)
{
    int         fFlushLNames   = 1;
    int         fFlushSegDefs  = 1;
    int         fFlushGrpDefs  = 1;
    bool        fSeenTheAdr    = false;
    bool        fConvertFixupp = false;

    uint32_t    off = 0;
    while (off + 3 < cbFile)
    {
        uint8_t         bRecType = pbFile[off];
        uint16_t        cbRec    = RT_MAKE_U16(pbFile[off + 1], pbFile[off + 2]);
        uint32_t        offRec   = 0;
        uint8_t const  *pbRec    = &pbFile[off + 3];

#define OMF_READ_IDX(a_idx, a_Name) \
            do { \
                a_idx = pbRec[offRec++]; \
                if ((a_idx) & 0x80) \
                    a_idx = (((a_idx) & 0x7f) << 8) | pbRec[offRec++]; \
            } while (0)

#define OMF_PEEK_IDX(a_idx, a_offRec) \
            do { \
                a_idx = pbRec[a_offRec]; \
                if ((a_idx) & 0x80) \
                    a_idx = (((a_idx) & 0x7f) << 8) | pbRec[(a_offRec) + 1]; \
            } while (0)

        /*
         * Remove/insert switch.  will
         */
        bool fSkip = false;
        switch (bRecType)
        {
            /*
             * Mangle watcom intrinsics if necessary.
             */
            case OMF_EXTDEF:
                if (pOmfStuff->fMayNeedMangling)
                {
                    if (!omfWriter_ExtDefBegin(pThis))
                        return false;
                    while (offRec + 1 < cbRec)
                    {
                        uint8_t cchName = pbRec[offRec++];
                        char   *pchName = (char *)&pbRec[offRec];
                        offRec += cchName;

                        uint16_t idxType;
                        OMF_READ_IDX(idxType, EXTDEF);

                        /* Look for g_apszExtDefRenames entries that requires changing. */
                        if (   cchName >= 5
                            && cchName <= 7
                            && pchName[0] == '_'
                            && pchName[1] == '_'
                            && (   pchName[2] == 'U'
                                || pchName[2] == 'I'
                                || pchName[2] == 'P')
                            && (   pchName[3] == '4'
                                || pchName[3] == '8'
                                || pchName[3] == 'I'
                                || pchName[3] == 'T') )
                        {
                            char szName[12];
                            memcpy(szName, pchName, cchName);
                            szName[cchName] = '\0';

                            uint32_t i = RT_ELEMENTS(g_apszExtDefRenames);
                            while (i-- > 0)
                                if (   cchName == (uint8_t)g_apszExtDefRenames[i][0]
                                    && memcmp(&g_apszExtDefRenames[i][1], szName, cchName) == 0)
                                {
                                    szName[0] = pOmfStuff->fProbably32bit ? '?' : '_';
                                    szName[1] = '?';
                                    break;
                                }

                            if (!omfWriter_ExtDefAddN(pThis, szName, cchName, idxType, false /*fPrependUnderscore*/))
                                return false;
                        }
                        else if (!omfWriter_ExtDefAddN(pThis, pchName, cchName, idxType, false /*fPrependUnderscore*/))
                            return false;
                    }
                    if (!omfWriter_ExtDefEnd(pThis))
                        return false;
                    fSkip = true;
                }
                break;

            /*
             * Remove line number records.
             */
            case OMF_LINNUM16:
            case OMF_LINNUM32:
                fSkip = fConvertLineNumbers;
                break;

            /*
             * Remove all but the first OMF_THEADR.
             */
            case OMF_THEADR:
                fSkip = fSeenTheAdr && fConvertLineNumbers;
                fSeenTheAdr = true;
                break;

            /*
             * Remove borland source file changes. Also, make sure the group
             * definitions are written out.
             */
            case OMF_COMENT:
                if (pbRec[1] == OMF_CCLS_LINK_PASS_SEP)
                {
                    Assert(fFlushSegDefs <= 0);
                    if (   fFlushGrpDefs > 0
                        && !convertOmfWriteAllGrpDefs(pThis, pOmfStuff, &fFlushGrpDefs))
                        return false;
                }
                if (fConvertLineNumbers)
                    fSkip = pbRec[1] == OMF_CCLS_BORLAND_SRC_FILE;
                break;

            /*
             * Redo these so the OMF writer is on top of the index thing.
             */
            case OMF_LNAMES:
                if (fFlushLNames >= 0)
                {
                    if (!omfWriter_LNamesBegin(pThis, false /*fAddZeroEntry*/))
                        return false;
                    if (!fFlushLNames)
                    {
                        while (offRec + 1 < cbRec)
                        {
                            uint8_t     cch = pbRec[offRec];
                            const char *pch = (const char *)&pbRec[offRec + 1];
                            if (!omfWriter_LNamesAddN(pThis, pch, cch, NULL))
                                return false;
                            offRec += cch + 1;
                        }
                    }
                    else
                    {
                        /* Flush all LNAMES in one go. */
                        for (unsigned i = 1; i < pOmfStuff->cLNames; i++)
                            if (!omfWriter_LNamesAddN(pThis, pOmfStuff->papchLNames[i] + 1, *pOmfStuff->papchLNames[i], NULL))
                                return false;
                        fFlushLNames = -1;
                    }
                    if (!omfWriter_LNamesEnd(pThis))
                        return false;
                }
                fSkip = true;
                break;

            /*
             * We may want to flush all the segments when we see the first one.
             */
            case OMF_SEGDEF16:
            case OMF_SEGDEF32:
                fSkip = fFlushSegDefs != 0;
                if (!convertOmfWriteAllSegDefs(pThis, pOmfStuff, &fFlushSegDefs))
                    return false;
                break;

            /*
             * We may want to flush all the groups when we see the first one.
             */
            case OMF_GRPDEF:
                fSkip = fFlushGrpDefs != 0;
                if (!convertOmfWriteAllGrpDefs(pThis, pOmfStuff, &fFlushGrpDefs))
                    return false;
                break;

            /*
             * Hook LEDATA to flush groups and figure out when to convert FIXUPP records.
             */
            case OMF_LEDATA16:
            case OMF_LEDATA32:
                if (   fFlushGrpDefs > 0
                    && !convertOmfWriteAllGrpDefs(pThis, pOmfStuff, &fFlushGrpDefs))
                    return false;
                fConvertFixupp = false;
#if 0
                if (   g_f16BitWatcomC
                    && bRecType == OMF_LEDATA16)
                {
                    /* Check if this is a code segment. */
                    uint16_t idxSeg;
                    OMF_PEEK_IDX(idxSeg, offRec);

                }
#endif
                break;


            /*
             * Convert fixups for 16-bit code segments to groups.
             * Deals with switch table trouble.
             */
            case OMF_FIXUPP16:
                if (fConvertFixupp)
                {
                    /* Gave up on this for now, easier to drop the eyecatcher in the _START segments. */
                }
                break;

            /*
             * Upon seeing MODEND we write out the debug info.
             */
            case OMF_MODEND16:
            case OMF_MODEND32:
                if (fConvertLineNumbers)
                    if (!convertOmfWriteDebugData(pThis, pOmfStuff))
                        return false;
                break;
        }

        /*
         * Pass the record thru, if so was decided.
         */
        if (!fSkip)
        {
            if (   omfWriter_RecBegin(pThis, bRecType)
                && omfWriter_RecAddBytes(pThis, pbRec, cbRec)
                && omfWriter_RecEnd(pThis, false))
            { /* likely */ }
            else return false;
        }

        /* advance */
        off += cbRec + 3;
    }

    return true;
}


/**
 * Converts LINNUMs and compiler intrinsics in an OMF object file.
 *
 * Wlink does a cheesy (to use their own term) job of generating the
 * sstSrcModule subsection.  It is limited to one file and cannot deal with line
 * numbers in different segment.  The latter is very annoying in assembly files
 * that jumps between segments, these a frequent on crash stacks.
 *
 * The solution is to convert to the same line number tables that cl.exe /Z7
 * generates for our 64-bit C code, we named that format codeview v8, or CV8.
 * Our code codeview debug info reader can deal with this already because of the
 * 64-bit code, so Bob's your uncle.
 *
 * @returns success indicator.
 * @param   pszFile     The name of the file being converted.
 * @param   pbFile      The file content.
 * @param   cbFile      The size of the file content.
 * @param   pDst        The destiation (output) file.
 */
static bool convertOmfToOmf(const char *pszFile, uint8_t const *pbFile, size_t cbFile, FILE *pDst)
{
    bool const fConvertLineNumbers = true;

    /*
     * Collect line number information, names, segment defintions, groups definitions and such.
     */
    OMFDETAILS OmfStuff;
    if (!collectOmfDetails(pszFile, pbFile, cbFile, &OmfStuff))
        return false;

    /* Mark groups for 16-bit code segments used by this object file as needed
       so we can reframe fixups to these segments correctly. */
    convertOmfLookForNeededGroups(&OmfStuff);

    /* Add debug segments definitions. */
    bool fRc = true;
    if (fConvertLineNumbers)
        fRc = convertOmfAddDebugSegDefs(&OmfStuff);

    /* Add any additional group defintions we may need (for 16-bit code segs). */
    if (fRc)
        fRc = convertOmfAddNeededGrpDefs(&OmfStuff);
    if (fRc)
    {
        /*
         * Instantiate the OMF writer and do pass-thru modifications.
         */
        POMFWRITER pThis = omfWriter_Create(pszFile, 0, 0, pDst);
        if (pThis)
        {
            fRc = convertOmfPassthru(pThis, pbFile, cbFile, &OmfStuff, fConvertLineNumbers);
            omfWriter_Destroy(pThis);
        }
        else
            fRc = false;
    }

    /*
     * Cleanup OmfStuff.
     */
    uint32_t i = OmfStuff.cSegLines;
    while (i-- >0)
    {
        uint32_t j = OmfStuff.paSegLines[i].cFiles;
        while (j-- > 0)
            free(OmfStuff.paSegLines[i].paFiles[j].paPairs);
        free(OmfStuff.paSegLines[i].paFiles);
    }
    free(OmfStuff.paSegLines);
    free(OmfStuff.paSrcInfo);
    free(OmfStuff.pchStrTab);

    while (OmfStuff.pAllocHead)
    {
        POMFDETAILSALLOC pFreeMe = OmfStuff.pAllocHead;
        OmfStuff.pAllocHead = OmfStuff.pAllocHead->pNext;
        free(pFreeMe);
    }

    return fRc;
}


/**
 * Does the convertion using convertelf and convertcoff.
 *
 * @returns exit code (0 on success, non-zero on failure)
 * @param   pszFile     The file to convert.
 */
static int convertit(const char *pszFile)
{
    /* Construct the filename for saving the unmodified file. */
    char szOrgFile[_4K];
    size_t cchFile = strlen(pszFile);
    if (cchFile + sizeof(".original") > sizeof(szOrgFile))
    {
        error(pszFile, "Filename too long!\n");
        return RTEXITCODE_FAILURE;
    }
    memcpy(szOrgFile, pszFile, cchFile);
    memcpy(&szOrgFile[cchFile], ".original", sizeof(".original"));

    /* Read the whole file. */
    void  *pvFile;
    size_t cbFile;
    if (readfile(pszFile, &pvFile, &cbFile))
    {
        /*
         * Do format conversions / adjustments.
         */
        bool fRc = false;
        uint8_t *pbFile = (uint8_t *)pvFile;
        if (   cbFile > sizeof(Elf64_Ehdr)
            && pbFile[0] == ELFMAG0
            && pbFile[1] == ELFMAG1
            && pbFile[2] == ELFMAG2
            && pbFile[3] == ELFMAG3)
        {
            if (writefile(szOrgFile, pvFile, cbFile))
            {
                FILE *pDst = openfile(pszFile, true /*fWrite*/);
                if (pDst)
                {
                    fRc = convertElfToOmf(pszFile, pbFile, cbFile, pDst);
                    fRc = fclose(pDst) == 0 && fRc;
                }
            }
        }
        else if (   cbFile > sizeof(IMAGE_FILE_HEADER)
                 && RT_MAKE_U16(pbFile[0], pbFile[1]) == IMAGE_FILE_MACHINE_AMD64
                 &&   RT_MAKE_U16(pbFile[2], pbFile[3]) * sizeof(IMAGE_SECTION_HEADER) + sizeof(IMAGE_FILE_HEADER)
                    < cbFile
                 && RT_MAKE_U16(pbFile[2], pbFile[3]) > 0)
        {
            if (writefile(szOrgFile, pvFile, cbFile))
            {
                FILE *pDst = openfile(pszFile, true /*fWrite*/);
                if (pDst)
                {
                    fRc = convertCoffToOmf(pszFile, pbFile, cbFile, pDst);
                    fRc = fclose(pDst) == 0 && fRc;
                }
            }
        }
        else if (   cbFile >= 8
                 && pbFile[0] == OMF_THEADR
                 && RT_MAKE_U16(pbFile[1], pbFile[2]) < cbFile)
        {
            if (writefile(szOrgFile, pvFile, cbFile))
            {
                FILE *pDst = openfile(pszFile, true /*fWrite*/);
                if (pDst)
                {
                    fRc = convertOmfToOmf(pszFile, pbFile, cbFile, pDst);
                    fRc = fclose(pDst) == 0 && fRc;
                }
            }
        }
        else
            fprintf(stderr, "error: Don't recognize format of '%s' (%#x %#x %#x %#x, cbFile=%lu)\n",
                    pszFile, pbFile[0], pbFile[1], pbFile[2], pbFile[3], (unsigned long)cbFile);
        free(pvFile);
        if (fRc)
            return 0;
    }
    return 1;
}


int main(int argc, char **argv)
{
    int rcExit = 0;

    /*
     * Scan the arguments.
     */
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            const char *pszOpt = &argv[i][1];
            if (*pszOpt == '-')
            {
                /* Convert long options to short ones. */
                pszOpt--;
                if (!strcmp(pszOpt, "--wcc"))
                    pszOpt = "w";
                else if (!strcmp(pszOpt, "--verbose"))
                    pszOpt = "v";
                else if (!strcmp(pszOpt, "--version"))
                    pszOpt = "V";
                else if (!strcmp(pszOpt, "--help"))
                    pszOpt = "h";
                else
                {
                    fprintf(stderr, "syntax errro: Unknown options '%s'\n", pszOpt);
                    return 2;
                }
            }

            /* Process the list of short options. */
            while (*pszOpt)
            {
                switch (*pszOpt++)
                {
                    case 'w':
                        g_f16BitWatcomC = true;
                        break;

                    case 'v':
                        g_cVerbose++;
                        break;

                    case 'V':
                        printf("%s\n", "$Revision: 106320 $");
                        return 0;

                    case '?':
                    case 'h':
                        printf("usage: %s [options] -o <output> <input1> [input2 ... [inputN]]\n",
                               argv[0]);
                        return 0;
                }
            }
        }
        else
        {
            /*
             * File to convert.  Do the job right away.
             */
            rcExit = convertit(argv[i]);
            if (rcExit != 0)
                break;
        }
    }

    return rcExit;
}

