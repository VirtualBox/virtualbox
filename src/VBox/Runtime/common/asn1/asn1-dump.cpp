/* $Id: asn1-dump.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, Structure Dumper.
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
#include <iprt/asn1.h>

#include <iprt/errcore.h>
#include <iprt/log.h>
#ifdef IN_RING3
# include <iprt/stream.h>
#endif
#include <iprt/string.h>

#include <iprt/formats/asn1.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Dump data structure.
 */
typedef struct RTASN1DUMPDATA
{
    /** RTASN1DUMP_F_XXX. */
    uint32_t                fFlags;
    /** The printfv like output function. */
    PFNRTDUMPPRINTFV        pfnPrintfV;
    /** PrintfV user argument. */
    void                   *pvUser;
} RTASN1DUMPDATA;
/** Pointer to a dump data structure. */
typedef RTASN1DUMPDATA *PRTASN1DUMPDATA;


#ifndef IN_SUP_HARDENED_R3

/*
 * Since we're the only user of OIDs, this stuff lives here.
 * Should that ever change, this code needs to move elsewhere and get it's own public API.
 */
# include "oiddb.h"


/**
 * Searches a range in the big table for a key.
 *
 * @returns Pointer to the matching entry. NULL if not found.
 * @param   iEntry              The start of the range.
 * @param   cEntries            The number of entries in the range.
 * @param   uKey                The key to find.
 */
DECLINLINE(PCRTOIDENTRYBIG) rtOidDbLookupBig(uint32_t iEntry, uint32_t cEntries, uint32_t uKey)
{
    /* Not worth doing binary search here, too few entries. */
    while (cEntries-- > 0)
    {
        uint32_t const uThisKey = g_aBigOidTable[iEntry].uKey;
        if (uThisKey >= uKey)
        {
            if (uThisKey == uKey)
                return &g_aBigOidTable[iEntry];
            break;
        }
        iEntry++;
    }
    return NULL;
}


/**
 * Searches a range in the small table for a key.
 *
 * @returns Pointer to the matching entry. NULL if not found.
 * @param   iEntry              The start of the range.
 * @param   cEntries            The number of entries in the range.
 * @param   uKey                The key to find.
 */
DECLINLINE(PCRTOIDENTRYSMALL) rtOidDbLookupSmall(uint32_t iEntry, uint32_t cEntries, uint32_t uKey)
{
    if (cEntries < 6)
    {
        /* Linear search for small ranges. */
        while (cEntries-- > 0)
        {
            uint32_t const uThisKey = g_aSmallOidTable[iEntry].uKey;
            if (uThisKey >= uKey)
            {
                if (uThisKey == uKey)
                    return &g_aSmallOidTable[iEntry];
                break;
            }
            iEntry++;
        }
    }
    else
    {
        /* Binary search. */
        uint32_t iEnd = iEntry + cEntries;
        for (;;)
        {
            uint32_t const i        = iEntry + (iEnd - iEntry) / 2;
            uint32_t const uThisKey = g_aSmallOidTable[i].uKey;
            if (uThisKey < uKey)
            {
                iEntry = i + 1;
                if (iEntry >= iEnd)
                    break;
            }
            else if (uThisKey > uKey)
            {
                iEnd = i;
                if (iEnd <= iEntry)
                    break;
            }
            else
                return &g_aSmallOidTable[i];
        }
    }
    return NULL;
}


/**
 * Queries the name for an object identifier.
 *
 * @returns IPRT status code (VINF_SUCCESS, VERR_NOT_FOUND,
 *          VERR_BUFFER_OVERFLOW)
 * @param   pauComponents   The components making up the object ID.
 * @param   cComponents     The number of components.
 * @param   pszDst          Where to store the name if found.
 * @param   cbDst           The size of the destination buffer.
 */
static int rtOidDbQueryObjIdName(uint32_t const *pauComponents, uint8_t cComponents, char *pszDst, size_t cbDst)
{
    int rc = VERR_NOT_FOUND;
    if (cComponents > 0)
    {
        /*
         * The top level is always in the small table as the range is restricted to 0,1,2.
         */
        bool     fBigTable = false;
        uint32_t cEntries  = RT_MIN(RT_ELEMENTS(g_aSmallOidTable), 3);
        uint32_t iEntry    = 0;
        for (;;)
        {
            uint32_t const uKey = *pauComponents++;
            if (!fBigTable)
            {
                PCRTOIDENTRYSMALL pSmallHit = rtOidDbLookupSmall(iEntry, cEntries, uKey);
                if (pSmallHit)
                {
                    if (--cComponents == 0)
                    {
                        if (RTBldProgStrTabQueryString(&g_OidDbStrTab, pSmallHit->offString,
                                                       pSmallHit->cchString, pszDst, cbDst) >= 0)
                            return VINF_SUCCESS;
                        rc = VERR_BUFFER_OVERFLOW;
                        break;
                    }
                    cEntries = pSmallHit->cChildren;
                    if (cEntries)
                    {
                        iEntry = pSmallHit->idxChildren;
                        fBigTable = pSmallHit->fBigTable;
                        continue;
                    }
                }
            }
            else
            {
                PCRTOIDENTRYBIG pBigHit = rtOidDbLookupBig(iEntry, cEntries, uKey);
                if (pBigHit)
                {
                    if (--cComponents == 0)
                    {
                        if (RTBldProgStrTabQueryString(&g_OidDbStrTab, pBigHit->offString,
                                                       pBigHit->cchString, pszDst, cbDst) >= 0)
                            return VINF_SUCCESS;
                        rc = VERR_BUFFER_OVERFLOW;
                        break;
                    }
                    cEntries = pBigHit->cChildren;
                    if (cEntries)
                    {
                        iEntry = pBigHit->idxChildren;
                        fBigTable = pBigHit->fBigTable;
                        continue;
                    }
                }
            }
            break;
        }
    }

    return rc;
}


/**
 * Queries the name for an object identifier.
 *
 * This API is simple and more or less requires a
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_FOUND if not found.
 * @retval  VERR_BUFFER_OVERFLOW if more buffer space is required.
 *
 * @param   pauComponents   The components making up the object ID.
 * @param   cComponents     The number of components.
 * @param   pszDst          Where to store the name if found.
 * @param   cbDst           The size of the destination buffer.
 */
RTDECL(int) RTAsn1QueryObjIdName(PCRTASN1OBJID pObjId, char *pszDst, size_t cbDst)
{
    return rtOidDbQueryObjIdName(pObjId->pauComponents, pObjId->cComponents, pszDst, cbDst);
}

#endif /* !IN_SUP_HARDENED_R3 */



/**
 * Wrapper around FNRTASN1DUMPPRINTFV.
 *
 * @param   pData               The dump data structure.
 * @param   pszFormat           Format string.
 * @param   ...                 Format arguments.
 */
static void rtAsn1DumpPrintf(PRTASN1DUMPDATA pData, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    pData->pfnPrintfV(pData->pvUser, pszFormat, va);
    va_end(va);
}


/**
 * Prints indentation.
 *
 * @param   pData               The dump data structure.
 * @param   uDepth              The indentation depth.
 */
static void rtAsn1DumpPrintIdent(PRTASN1DUMPDATA pData, uint32_t uDepth)
{
    uint32_t cchLeft = uDepth * 2;
    while (cchLeft > 0)
    {
        static char const s_szSpaces[] = "                                        ";
        uint32_t cch = RT_MIN(cchLeft, sizeof(s_szSpaces) - 1);
        rtAsn1DumpPrintf(pData, &s_szSpaces[sizeof(s_szSpaces) - 1 - cch]);
        cchLeft -= cch;
    }
}


/**
 * Dumps UTC TIME and GENERALIZED TIME
 *
 * @param   pData               The dump data structure.
 * @param   pAsn1Core           The ASN.1 core object representation.
 * @param   pszType             The time type name.
 */
static void rtAsn1DumpTime(PRTASN1DUMPDATA pData, PCRTASN1CORE pAsn1Core, const char *pszType)
{
    if ((pAsn1Core->fFlags & RTASN1CORE_F_PRIMITE_TAG_STRUCT))
    {
        PCRTASN1TIME pTime = (PCRTASN1TIME)pAsn1Core;
        rtAsn1DumpPrintf(pData, "%s -- %04u-%02u-%02u %02u:%02u:%02.%09Z\n",
                         pszType,
                         pTime->Time.i32Year, pTime->Time.u8Month,  pTime->Time.u8MonthDay,
                         pTime->Time.u8Hour, pTime->Time.u8Minute, pTime->Time.u8Second,
                         pTime->Time.u32Nanosecond);
    }
    else if (pAsn1Core->cb > 0 && pAsn1Core->cb < 32 && pAsn1Core->uData.pch)
        rtAsn1DumpPrintf(pData, "%s '%.*s'\n", pszType, (size_t)pAsn1Core->cb, pAsn1Core->uData.pch);
    else
        rtAsn1DumpPrintf(pData, "%s -- cb=%u\n", pszType, pAsn1Core->cb);
}


/**
 * Dumps strings sharing the RTASN1STRING structure.
 *
 * @param   pData               The dump data structure.
 * @param   pAsn1Core           The ASN.1 core object representation.
 * @param   pszType             The string type name.
 * @param   uDepth              The current identation level.
 */
static void rtAsn1DumpString(PRTASN1DUMPDATA pData, PCRTASN1CORE pAsn1Core, const char *pszType, uint32_t uDepth)
{
    rtAsn1DumpPrintf(pData, "%s", pszType);

    const char     *pszPostfix  = "'\n";
    bool            fUtf8       = false;
    const char     *pch         = pAsn1Core->uData.pch;
    uint32_t        cch         = pAsn1Core->cb;
    PCRTASN1STRING  pString     = (PCRTASN1STRING)pAsn1Core;
    if (   (pAsn1Core->fFlags & RTASN1CORE_F_PRIMITE_TAG_STRUCT)
        && pString->pszUtf8
        && pString->cchUtf8)
    {
        fUtf8 = true;
        pszPostfix = "' -- utf-8\n";
    }

    if (cch == 0 || !pch)
        rtAsn1DumpPrintf(pData, "-- cb=%u\n", pszType, pAsn1Core->cb);
    else
    {
        if (cch >= 48)
        {
            rtAsn1DumpPrintf(pData, "\n");
            rtAsn1DumpPrintIdent(pData, uDepth + 1);
        }
        rtAsn1DumpPrintf(pData, " '");

        /** @todo Handle BMP and UNIVERSIAL strings specially. */
        do
        {
            const char *pchStart = pch;
            while (   cch > 0
                   && (uint8_t)*pch >= 0x20
                   && (!fUtf8 ? (uint8_t)*pch < 0x7f : (uint8_t)*pch != 0x7f)
                   && *pch != '\'')
                cch--, pch++;
            if (pchStart != pch)
                rtAsn1DumpPrintf(pData, "%.*s", pch - pchStart, pchStart);

            while (   cch > 0
                   && (   (uint8_t)*pch < 0x20
                       || (!fUtf8 ? (uint8_t)*pch >= 0x7f : (uint8_t)*pch == 0x7f)
                       || (uint8_t)*pch == '\'') )
            {
                rtAsn1DumpPrintf(pData, "\\x%02x", *pch);
                cch--;
                pch++;
            }
        } while (cch > 0);

        rtAsn1DumpPrintf(pData, pszPostfix);
    }
}


/**
 * Dumps the type and value of an universal ASN.1 type.
 *
 * @returns True if it opens a child, false if not.
 * @param   pData               The dumper data.
 * @param   pAsn1Core           The ASN.1 object to dump.
 * @param   uDepth              The current depth (for indentation).
 */
static bool rtAsn1DumpUniversalTypeAndValue(PRTASN1DUMPDATA pData, PCRTASN1CORE pAsn1Core, uint32_t uDepth)
{
    const char *pszValuePrefix = "-- value:";
    const char *pszDefault = "";
    if (pAsn1Core->fFlags & RTASN1CORE_F_DEFAULT)
    {
        pszValuePrefix = "DEFAULT";
        pszDefault = "DEFAULT ";
    }

    bool fOpen = false;
    switch (pAsn1Core->uRealTag)
    {
        case ASN1_TAG_BOOLEAN:
            if (pAsn1Core->fFlags & RTASN1CORE_F_PRIMITE_TAG_STRUCT)
                rtAsn1DumpPrintf(pData, "BOOLEAN %s %RTbool\n", pszValuePrefix, ((PCRTASN1BOOLEAN)pAsn1Core)->fValue);
            else if (pAsn1Core->cb == 1 && pAsn1Core->uData.pu8)
                rtAsn1DumpPrintf(pData, "BOOLEAN %s %u\n", pszValuePrefix, *pAsn1Core->uData.pu8);
            else
                rtAsn1DumpPrintf(pData, "BOOLEAN -- cb=%u\n", pAsn1Core->cb);
            break;

        case ASN1_TAG_INTEGER:
            if ((pAsn1Core->fFlags & RTASN1CORE_F_PRIMITE_TAG_STRUCT) && pAsn1Core->cb <= 8)
                rtAsn1DumpPrintf(pData, "INTEGER %s %llu / %#llx\n", pszValuePrefix,
                                 ((PCRTASN1INTEGER)pAsn1Core)->uValue, ((PCRTASN1INTEGER)pAsn1Core)->uValue);
            else if (pAsn1Core->cb == 0 || pAsn1Core->cb >= 512 || !pAsn1Core->uData.pu8)
                rtAsn1DumpPrintf(pData, "INTEGER -- cb=%u\n", pAsn1Core->cb);
            else if (pAsn1Core->cb <= 32)
                rtAsn1DumpPrintf(pData, "INTEGER %s %.*Rhxs\n", pszValuePrefix, (size_t)pAsn1Core->cb, pAsn1Core->uData.pu8);
            else
                rtAsn1DumpPrintf(pData, "INTEGER %s\n%.*Rhxd\n", pszValuePrefix, (size_t)pAsn1Core->cb, pAsn1Core->uData.pu8);
            break;

        case ASN1_TAG_BIT_STRING:
            if ((pAsn1Core->fFlags & RTASN1CORE_F_PRIMITE_TAG_STRUCT))
            {
                PCRTASN1BITSTRING pBitString = (PCRTASN1BITSTRING)pAsn1Core;
                rtAsn1DumpPrintf(pData, "BIT STRING %s-- cb=%u cBits=%#x cMaxBits=%#x",
                                 pszDefault, pBitString->Asn1Core.cb, pBitString->cBits, pBitString->cMaxBits);
                if (pBitString->cBits <= 64)
                    rtAsn1DumpPrintf(pData, " value=%#llx\n", RTAsn1BitString_GetAsUInt64(pBitString));
                else
                    rtAsn1DumpPrintf(pData, "\n");
            }
            else
                rtAsn1DumpPrintf(pData, "BIT STRING %s-- cb=%u\n", pszDefault, pAsn1Core->cb);
            fOpen = pAsn1Core->pOps != NULL;
            break;

        case ASN1_TAG_OCTET_STRING:
            rtAsn1DumpPrintf(pData, "OCTET STRING %s-- cb=%u\n", pszDefault, pAsn1Core->cb);
            fOpen = pAsn1Core->pOps != NULL;
            break;

        case ASN1_TAG_NULL:
            rtAsn1DumpPrintf(pData, "NULL\n");
            break;

        case ASN1_TAG_OID:
            if ((pAsn1Core->fFlags & RTASN1CORE_F_PRIMITE_TAG_STRUCT))
            {
#ifndef IN_SUP_HARDENED_R3
                PCRTASN1OBJID pObjId = (PCRTASN1OBJID)pAsn1Core;
                char szName[64];
                if (rtOidDbQueryObjIdName(pObjId->pauComponents, pObjId->cComponents, szName, sizeof(szName)) == VINF_SUCCESS)
                    rtAsn1DumpPrintf(pData, "OBJECT IDENTIFIER %s%s ('%s')\n",
                                     pszDefault, szName, ((PCRTASN1OBJID)pAsn1Core)->szObjId);
                else
#endif
                    rtAsn1DumpPrintf(pData, "OBJECT IDENTIFIER %s'%s'\n", pszDefault, ((PCRTASN1OBJID)pAsn1Core)->szObjId);
            }
            else
                rtAsn1DumpPrintf(pData, "OBJECT IDENTIFIER %s -- cb=%u\n", pszDefault, pAsn1Core->cb);
            break;

        case ASN1_TAG_OBJECT_DESCRIPTOR:
            rtAsn1DumpPrintf(pData, "OBJECT DESCRIPTOR -- cb=%u TODO\n", pAsn1Core->cb);
            break;

        case ASN1_TAG_EXTERNAL:
            rtAsn1DumpPrintf(pData, "EXTERNAL -- cb=%u TODO\n", pAsn1Core->cb);
            break;

        case ASN1_TAG_REAL:
            rtAsn1DumpPrintf(pData, "REAL -- cb=%u TODO\n", pAsn1Core->cb);
            break;

        case ASN1_TAG_ENUMERATED:
            rtAsn1DumpPrintf(pData, "ENUMERATED -- cb=%u TODO\n", pAsn1Core->cb);
            break;

        case ASN1_TAG_EMBEDDED_PDV:
            rtAsn1DumpPrintf(pData, "EMBEDDED PDV -- cb=%u TODO\n", pAsn1Core->cb);
            break;

        case ASN1_TAG_UTF8_STRING:
            rtAsn1DumpString(pData, pAsn1Core, "UTF8 STRING", uDepth);
            break;

        case ASN1_TAG_RELATIVE_OID:
            rtAsn1DumpPrintf(pData, "RELATIVE OBJECT IDENTIFIER -- cb=%u TODO\n", pAsn1Core->cb);
            break;

        case ASN1_TAG_SEQUENCE:
            rtAsn1DumpPrintf(pData, "SEQUENCE -- cb=%u\n", pAsn1Core->cb);
            fOpen = true;
            break;
        case ASN1_TAG_SET:
            rtAsn1DumpPrintf(pData, "SET -- cb=%u\n", pAsn1Core->cb);
            fOpen = true;
            break;

        case ASN1_TAG_NUMERIC_STRING:
            rtAsn1DumpString(pData, pAsn1Core, "NUMERIC STRING", uDepth);
            break;

        case ASN1_TAG_PRINTABLE_STRING:
            rtAsn1DumpString(pData, pAsn1Core, "PRINTABLE STRING", uDepth);
            break;

        case ASN1_TAG_T61_STRING:
            rtAsn1DumpString(pData, pAsn1Core, "T61 STRING", uDepth);
            break;

        case ASN1_TAG_VIDEOTEX_STRING:
            rtAsn1DumpString(pData, pAsn1Core, "VIDEOTEX STRING", uDepth);
            break;

        case ASN1_TAG_IA5_STRING:
            rtAsn1DumpString(pData, pAsn1Core, "IA5 STRING", uDepth);
            break;

        case ASN1_TAG_GRAPHIC_STRING:
            rtAsn1DumpString(pData, pAsn1Core, "GRAPHIC STRING", uDepth);
            break;

        case ASN1_TAG_VISIBLE_STRING:
            rtAsn1DumpString(pData, pAsn1Core, "VISIBLE STRING", uDepth);
            break;

        case ASN1_TAG_GENERAL_STRING:
            rtAsn1DumpString(pData, pAsn1Core, "GENERAL STRING", uDepth);
            break;

        case ASN1_TAG_UNIVERSAL_STRING:
            rtAsn1DumpString(pData, pAsn1Core, "UNIVERSAL STRING", uDepth);
            break;

        case ASN1_TAG_BMP_STRING:
            rtAsn1DumpString(pData, pAsn1Core, "BMP STRING", uDepth);
            break;

        case ASN1_TAG_UTC_TIME:
            rtAsn1DumpTime(pData, pAsn1Core, "UTC TIME");
            break;

        case ASN1_TAG_GENERALIZED_TIME:
            rtAsn1DumpTime(pData, pAsn1Core, "GENERALIZED TIME");
            break;

        case ASN1_TAG_CHARACTER_STRING:
            rtAsn1DumpPrintf(pData, "CHARACTER STRING -- cb=%u TODO\n", pAsn1Core->cb);
            break;

        default:
            rtAsn1DumpPrintf(pData, "[UNIVERSAL %u]\n", pAsn1Core->uTag);
            break;
    }
    return fOpen;
}


/** @callback_method_impl{FNRTASN1ENUMCALLBACK}  */
static DECLCALLBACK(int) rtAsn1DumpEnumCallback(PRTASN1CORE pAsn1Core, const char *pszName, uint32_t uDepth, void *pvUser)
{
    PRTASN1DUMPDATA pData = (PRTASN1DUMPDATA)pvUser;
    if (!pAsn1Core->fFlags)
        return VINF_SUCCESS;

    bool fOpen = false;
    rtAsn1DumpPrintIdent(pData, uDepth);
    switch (pAsn1Core->fClass & ASN1_TAGCLASS_MASK)
    {
        case ASN1_TAGCLASS_UNIVERSAL:
            rtAsn1DumpPrintf(pData, "%-16s ", pszName);
            fOpen = rtAsn1DumpUniversalTypeAndValue(pData, pAsn1Core, uDepth);
            break;

        case ASN1_TAGCLASS_CONTEXT:
            if ((pAsn1Core->fRealClass & ASN1_TAGCLASS_MASK) == ASN1_TAGCLASS_UNIVERSAL)
            {
                rtAsn1DumpPrintf(pData, "%-16s [%u] ", pszName, pAsn1Core->uTag);
                fOpen = rtAsn1DumpUniversalTypeAndValue(pData, pAsn1Core, uDepth);
            }
            else
            {
                rtAsn1DumpPrintf(pData, "%-16s [%u]\n", pszName, pAsn1Core->uTag);
                fOpen = true;
            }
            break;

        case ASN1_TAGCLASS_APPLICATION:
            if ((pAsn1Core->fRealClass & ASN1_TAGCLASS_MASK) == ASN1_TAGCLASS_UNIVERSAL)
            {
                rtAsn1DumpPrintf(pData, "%-16s [APPLICATION %u] ", pszName, pAsn1Core->uTag);
                fOpen = rtAsn1DumpUniversalTypeAndValue(pData, pAsn1Core, uDepth);
            }
            else
            {
                rtAsn1DumpPrintf(pData, "%-16s [APPLICATION %u]\n", pszName, pAsn1Core->uTag);
                fOpen = true;
            }
            break;

        case ASN1_TAGCLASS_PRIVATE:
            if (RTASN1CORE_IS_DUMMY(pAsn1Core))
                rtAsn1DumpPrintf(pData, "%-16s DUMMY\n", pszName);
            else
            {
                rtAsn1DumpPrintf(pData, "%-16s [PRIVATE %u]\n", pszName, pAsn1Core->uTag);
                fOpen = true;
            }
            break;
    }
    /** @todo {} */

    /*
     * Recurse.
     */
    if (   pAsn1Core->pOps
        && pAsn1Core->pOps->pfnEnum)
        pAsn1Core->pOps->pfnEnum(pAsn1Core, rtAsn1DumpEnumCallback, uDepth, pData);
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Dump(PCRTASN1CORE pAsn1Core, uint32_t fFlags, uint32_t uLevel, PFNRTDUMPPRINTFV pfnPrintfV, void *pvUser)
{
    if (   pAsn1Core->pOps
        && pAsn1Core->pOps->pfnEnum)
    {
        RTASN1DUMPDATA Data;
        Data.fFlags     = fFlags;
        Data.pfnPrintfV = pfnPrintfV;
        Data.pvUser     = pvUser;

        return pAsn1Core->pOps->pfnEnum((PRTASN1CORE)pAsn1Core, rtAsn1DumpEnumCallback, uLevel, &Data);
    }
    return VINF_SUCCESS;
}

