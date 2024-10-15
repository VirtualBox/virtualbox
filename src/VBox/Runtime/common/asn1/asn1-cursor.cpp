/* $Id: asn1-cursor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, Basic Operations.
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

#include <iprt/asm.h>
#include <iprt/alloca.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/ctype.h>

#include <iprt/formats/asn1.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def RTASN1_MAX_NESTING
 * The maximum nesting depth we allow.  This limit is enforced to avoid running
 * out of stack due to malformed ASN.1 input.
 *
 * For reference, 'RTSignTool verify-exe RTSignTool.exe', requires a value of 15
 * to work without hitting the limit for signatures with simple timestamps, and
 * 23 (amd64/rel = ~3KB) for the new microsoft timestamp counter signatures.
 */
#ifdef IN_RING3
# define RTASN1_MAX_NESTING     64
#else
# define RTASN1_MAX_NESTING     32
#endif



RTDECL(PRTASN1CURSOR) RTAsn1CursorInitPrimary(PRTASN1CURSORPRIMARY pPrimaryCursor, void const *pvFirst, uint32_t cb,
                                              PRTERRINFO pErrInfo, PCRTASN1ALLOCATORVTABLE pAllocator, uint32_t fFlags,
                                              const char *pszErrorTag)
{
    pPrimaryCursor->Cursor.pbCur            = (uint8_t const *)pvFirst;
    pPrimaryCursor->Cursor.cbLeft           = cb;
    pPrimaryCursor->Cursor.fFlags           = (uint8_t)fFlags; Assert(fFlags <= UINT8_MAX);
    pPrimaryCursor->Cursor.cDepth           = 0;
    pPrimaryCursor->Cursor.abReserved[0]    = 0;
    pPrimaryCursor->Cursor.abReserved[1]    = 0;
    pPrimaryCursor->Cursor.pPrimary         = pPrimaryCursor;
    pPrimaryCursor->Cursor.pUp              = NULL;
    pPrimaryCursor->Cursor.pszErrorTag      = pszErrorTag;
    pPrimaryCursor->pErrInfo                = pErrInfo;
    pPrimaryCursor->pAllocator              = pAllocator;
    pPrimaryCursor->pbFirst                 = (uint8_t const *)pvFirst;
    return &pPrimaryCursor->Cursor;
}


RTDECL(int) RTAsn1CursorInitSub(PRTASN1CURSOR pParent, uint32_t cb, PRTASN1CURSOR pChild, const char *pszErrorTag)
{
    AssertReturn(pParent->pPrimary, VERR_ASN1_INTERNAL_ERROR_1);
    AssertReturn(pParent->pbCur, VERR_ASN1_INTERNAL_ERROR_2);

    pChild->pbCur           = pParent->pbCur;
    pChild->cbLeft          = cb;
    pChild->fFlags          = pParent->fFlags & ~RTASN1CURSOR_FLAGS_INDEFINITE_LENGTH;
    pChild->cDepth          = pParent->cDepth + 1;
    AssertReturn(pChild->cDepth < RTASN1_MAX_NESTING, VERR_ASN1_TOO_DEEPLY_NESTED);
    pChild->abReserved[0]   = 0;
    pChild->abReserved[1]   = 0;
    pChild->pPrimary        = pParent->pPrimary;
    pChild->pUp             = pParent;
    pChild->pszErrorTag     = pszErrorTag;

    AssertReturn(pParent->cbLeft >= cb, VERR_ASN1_INTERNAL_ERROR_3);
    pParent->pbCur  += cb;
    pParent->cbLeft -= cb;

    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1CursorInitSubFromCore(PRTASN1CURSOR pParent, PRTASN1CORE pAsn1Core,
                                        PRTASN1CURSOR pChild, const char *pszErrorTag)
{
    AssertReturn(pParent->pPrimary, VERR_ASN1_INTERNAL_ERROR_1);
    AssertReturn(pParent->pbCur, VERR_ASN1_INTERNAL_ERROR_2);

    pChild->pbCur           = pAsn1Core->uData.pu8;
    pChild->cbLeft          = pAsn1Core->cb;
    pChild->fFlags          = pParent->fFlags & ~RTASN1CURSOR_FLAGS_INDEFINITE_LENGTH;
    pChild->cDepth          = pParent->cDepth + 1;
    AssertReturn(pChild->cDepth < RTASN1_MAX_NESTING, VERR_ASN1_TOO_DEEPLY_NESTED);
    pChild->abReserved[0]   = 0;
    pChild->abReserved[1]   = 0;
    pChild->pPrimary        = pParent->pPrimary;
    pChild->pUp             = pParent;
    pChild->pszErrorTag     = pszErrorTag;

    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1CursorSetInfoV(PRTASN1CURSOR pCursor, int rc, const char *pszMsg, va_list va)
{
    PRTERRINFO pErrInfo = pCursor->pPrimary->pErrInfo;
    if (pErrInfo)
    {
        /* Format the message. */
        RTErrInfoSetV(pErrInfo, rc, pszMsg, va);

        /* Add the prefixes.  This isn't the fastest way, but it's the one
           which eats the least stack. */
        char   *pszBuf = pErrInfo->pszMsg;
        size_t  cbBuf  = pErrInfo->cbMsg;
        if (pszBuf && cbBuf > 32)
        {
            size_t cbMove = strlen(pszBuf) + 1;

            /* Make sure there is a ': '. */
            bool fFirst = false;
            if (pszMsg[0] != '%' || pszMsg[1] != 's' || pszMsg[2] != ':')
            {
                if (cbMove + 2 < cbBuf)
                {
                    memmove(pszBuf + 2, pszBuf, cbMove);
                    pszBuf[0] = ':';
                    pszBuf[1] = ' ';
                    cbMove += 2;
                    fFirst = true;
                }
            }

            /* Add the prefixes from the cursor chain. */
            while (pCursor)
            {
                if (pCursor->pszErrorTag)
                {
                    size_t cchErrorTag = strlen(pCursor->pszErrorTag);
                    if (cchErrorTag + !fFirst + cbMove > cbBuf)
                        break;
                    memmove(pszBuf + cchErrorTag + !fFirst, pszBuf, cbMove);
                    memcpy(pszBuf, pCursor->pszErrorTag, cchErrorTag);
                    if (!fFirst)
                        pszBuf[cchErrorTag] = '.';
                    cbMove += cchErrorTag + !fFirst;
                    fFirst  = false;
                }
                pCursor = pCursor->pUp;
            }
        }
    }

    return rc;
}


RTDECL(int) RTAsn1CursorSetInfo(PRTASN1CURSOR pCursor, int rc, const char *pszMsg, ...)
{
    va_list va;
    va_start(va, pszMsg);
    rc = RTAsn1CursorSetInfoV(pCursor, rc, pszMsg, va);
    va_end(va);
    return rc;
}


RTDECL(bool) RTAsn1CursorIsEnd(PRTASN1CURSOR pCursor)
{
    if (pCursor->cbLeft == 0)
        return true;
    if (!(pCursor->fFlags & RTASN1CURSOR_FLAGS_INDEFINITE_LENGTH))
        return false;
    return pCursor->cbLeft >= 2
        && pCursor->pbCur[0] == 0
        && pCursor->pbCur[1] == 0;
}


RTDECL(int) RTAsn1CursorCheckEnd(PRTASN1CURSOR pCursor)
{
    if (!(pCursor->fFlags & RTASN1CURSOR_FLAGS_INDEFINITE_LENGTH))
    {
        if (pCursor->cbLeft == 0)
            return VINF_SUCCESS;
        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_NOT_AT_END,
                                   "%u (%#x) bytes left over", pCursor->cbLeft, pCursor->cbLeft);
    }

    /*
     * There must be exactly two zero bytes here.
     */
    if (pCursor->cbLeft == 2)
    {
        if (   pCursor->pbCur[0] == 0
            && pCursor->pbCur[1] == 0)
            return VINF_SUCCESS;
        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_NOT_AT_END,
                                   "%u (%#x) bytes left over [indef: %.*Rhxs]",
                                   pCursor->cbLeft, pCursor->cbLeft, RT_MIN(pCursor->cbLeft, 16), pCursor->pbCur);
    }
    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_NOT_AT_END,
                               "%u (%#x) byte(s) left over, exepcted exactly two zero bytes [indef len]",
                               pCursor->cbLeft, pCursor->cbLeft);
}


/**
 * Worker for RTAsn1CursorCheckSeqEnd and RTAsn1CursorCheckSetEnd.
 */
static int rtAsn1CursorCheckSeqOrSetEnd(PRTASN1CURSOR pCursor, PRTASN1CORE pAsn1Core)
{
    if (!(pAsn1Core->fFlags & RTASN1CORE_F_INDEFINITE_LENGTH))
    {
        if (pCursor->cbLeft == 0)
            return VINF_SUCCESS;
        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_NOT_AT_END,
                                   "%u (%#x) bytes left over", pCursor->cbLeft, pCursor->cbLeft);
    }

    if (pCursor->cbLeft >= 2)
    {
        if (   pCursor->pbCur[0] == 0
            && pCursor->pbCur[1] == 0)
        {
            pAsn1Core->cb = (uint32_t)(pCursor->pbCur - pAsn1Core->uData.pu8);
            pCursor->cbLeft -= 2;
            pCursor->pbCur  += 2;

            PRTASN1CURSOR pParentCursor = pCursor->pUp;
            if (   pParentCursor
                && (pParentCursor->fFlags & RTASN1CURSOR_FLAGS_INDEFINITE_LENGTH))
            {
                pParentCursor->pbCur  -= pCursor->cbLeft;
                pParentCursor->cbLeft += pCursor->cbLeft;
                return VINF_SUCCESS;
            }

            if (pCursor->cbLeft == 0)
                return VINF_SUCCESS;

            return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_NOT_AT_END,
                                       "%u (%#x) bytes left over (parent not indefinite length)", pCursor->cbLeft, pCursor->cbLeft);
        }
        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_NOT_AT_END, "%u (%#x) bytes left over [indef: %.*Rhxs]",
                                   pCursor->cbLeft, pCursor->cbLeft, RT_MIN(pCursor->cbLeft, 16), pCursor->pbCur);
    }
    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_NOT_AT_END,
                               "1 byte left over, expected two for indefinite length end-of-content sequence");
}


RTDECL(int) RTAsn1CursorCheckSeqEnd(PRTASN1CURSOR pCursor, PRTASN1SEQUENCECORE pSeqCore)
{
    return rtAsn1CursorCheckSeqOrSetEnd(pCursor, &pSeqCore->Asn1Core);
}


RTDECL(int) RTAsn1CursorCheckSetEnd(PRTASN1CURSOR pCursor, PRTASN1SETCORE pSetCore)
{
    return rtAsn1CursorCheckSeqOrSetEnd(pCursor, &pSetCore->Asn1Core);
}


RTDECL(int) RTAsn1CursorCheckOctStrEnd(PRTASN1CURSOR pCursor, PRTASN1OCTETSTRING pOctetString)
{
    return rtAsn1CursorCheckSeqOrSetEnd(pCursor, &pOctetString->Asn1Core);
}


RTDECL(PRTASN1ALLOCATION) RTAsn1CursorInitAllocation(PRTASN1CURSOR pCursor, PRTASN1ALLOCATION pAllocation)
{
    pAllocation->cbAllocated = 0;
    pAllocation->cReallocs   = 0;
    pAllocation->uReserved0  = 0;
    pAllocation->pAllocator  = pCursor->pPrimary->pAllocator;
    return pAllocation;
}


RTDECL(PRTASN1ARRAYALLOCATION) RTAsn1CursorInitArrayAllocation(PRTASN1CURSOR pCursor, PRTASN1ARRAYALLOCATION pAllocation,
                                                               size_t cbEntry)
{
    Assert(cbEntry >= sizeof(RTASN1CORE));
    Assert(cbEntry < _1M);
    Assert(RT_ALIGN_Z(cbEntry, sizeof(void *)) == cbEntry);
    pAllocation->cbEntry            = (uint32_t)cbEntry;
    pAllocation->cPointersAllocated = 0;
    pAllocation->cEntriesAllocated  = 0;
    pAllocation->cResizeCalls       = 0;
    pAllocation->uReserved0         = 0;
    pAllocation->pAllocator         = pCursor->pPrimary->pAllocator;
    return pAllocation;
}


RTDECL(int) RTAsn1CursorReadHdr(PRTASN1CURSOR pCursor, PRTASN1CORE pAsn1Core, const char *pszErrorTag)
{
    /*
     * Initialize the return structure in case of failure.
     */
    pAsn1Core->uTag         = 0;
    pAsn1Core->fClass       = 0;
    pAsn1Core->uRealTag     = 0;
    pAsn1Core->fRealClass   = 0;
    pAsn1Core->cbHdr        = 0;
    pAsn1Core->cb           = 0;
    pAsn1Core->fFlags       = 0;
    pAsn1Core->uData.pv     = NULL;
    pAsn1Core->pOps         = NULL;

    /*
     * The header has at least two bytes: Type & length.
     */
    if (pCursor->cbLeft >= 2)
    {
        uint32_t uTag = pCursor->pbCur[0];
        uint32_t cb   = pCursor->pbCur[1];
        pCursor->cbLeft -= 2;
        pCursor->pbCur  += 2;

        pAsn1Core->uRealTag   = pAsn1Core->uTag   = uTag & ASN1_TAG_MASK;
        pAsn1Core->fRealClass = pAsn1Core->fClass = uTag & ~ASN1_TAG_MASK;
        pAsn1Core->cbHdr  = 2;
        if ((uTag & ASN1_TAG_MASK) == ASN1_TAG_USE_LONG_FORM)
            return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_LONG_TAG,
                                       "%s: Implement parsing of tags > 30: %#x (length=%#x)", pszErrorTag, uTag, cb);

        /* Extended length field? */
        if (cb & RT_BIT(7))
        {
            if (cb != RT_BIT(7))
            {
                /* Definite form. */
                uint8_t cbEnc = cb & 0x7f;
                if (cbEnc > pCursor->cbLeft)
                    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_BAD_LENGTH_ENCODING,
                                               "%s: Extended BER length field longer than available data: %#x vs %#x (uTag=%#x)",
                                               pszErrorTag, cbEnc, pCursor->cbLeft, uTag);
                switch (cbEnc)
                {
                    case 1:
                        cb = pCursor->pbCur[0];
                        break;
                    case 2:
                        cb = RT_MAKE_U16(pCursor->pbCur[1], pCursor->pbCur[0]);
                        break;
                    case 3:
                        cb = RT_MAKE_U32_FROM_U8(pCursor->pbCur[2], pCursor->pbCur[1], pCursor->pbCur[0], 0);
                        break;
                    case 4:
                        cb = RT_MAKE_U32_FROM_U8(pCursor->pbCur[3], pCursor->pbCur[2], pCursor->pbCur[1], pCursor->pbCur[0]);
                        break;
                    default:
                        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_BAD_LENGTH_ENCODING,
                                                   "%s: Too long/short extended BER length field: %#x (uTag=%#x)",
                                                   pszErrorTag, cbEnc, uTag);
                }
                pCursor->cbLeft  -= cbEnc;
                pCursor->pbCur   += cbEnc;
                pAsn1Core->cbHdr += cbEnc;

                /* Check the length encoding efficiency (T-REC-X.690-200811 10.1, 9.1). */
                if (pCursor->fFlags & (RTASN1CURSOR_FLAGS_DER | RTASN1CURSOR_FLAGS_CER))
                {
                    if (cb <= 0x7f)
                        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_BAD_LENGTH_ENCODING,
                                                   "%s: Invalid DER/CER length encoding: cbEnc=%u cb=%#x uTag=%#x",
                                                   pszErrorTag, cbEnc, cb, uTag);
                    uint8_t cbNeeded;
                    if (cb <= 0x000000ff)       cbNeeded = 1;
                    else if (cb <= 0x0000ffff)  cbNeeded = 2;
                    else if (cb <= 0x00ffffff)  cbNeeded = 3;
                    else                        cbNeeded = 4;
                    if (cbNeeded != cbEnc)
                        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_BAD_LENGTH_ENCODING,
                                                   "%s: Invalid DER/CER length encoding: cb=%#x uTag=%#x cbEnc=%u cbNeeded=%u",
                                                   pszErrorTag, cb, uTag, cbEnc, cbNeeded);
                }
            }
            /* Indefinite form. */
            else if (pCursor->fFlags & RTASN1CURSOR_FLAGS_DER)
                return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_ILLEGAL_INDEFINITE_LENGTH,
                                           "%s: Indefinite length form not allowed in DER mode (uTag=%#x).", pszErrorTag, uTag);
            else if (!(uTag & ASN1_TAGFLAG_CONSTRUCTED))
                return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_BAD_INDEFINITE_LENGTH,
                                           "%s: Indefinite BER/CER encoding is for non-constructed tag (uTag=%#x)", pszErrorTag, uTag);
            else if (   uTag != (ASN1_TAG_SEQUENCE | ASN1_TAGFLAG_CONSTRUCTED)
                     && uTag != (ASN1_TAG_SET      | ASN1_TAGFLAG_CONSTRUCTED)
                     &&    (uTag & (ASN1_TAGFLAG_CONSTRUCTED | ASN1_TAGCLASS_CONTEXT))
                        !=         (ASN1_TAGFLAG_CONSTRUCTED | ASN1_TAGCLASS_CONTEXT) )
                return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_BAD_INDEFINITE_LENGTH,
                                           "%s: Indefinite BER/CER encoding not supported for this tag (uTag=%#x)", pszErrorTag, uTag);
            else if (pCursor->fFlags & RTASN1CURSOR_FLAGS_INDEFINITE_LENGTH)
                return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_BAD_INDEFINITE_LENGTH,
                                           "%s: Nested indefinite BER/CER encoding. (uTag=%#x)", pszErrorTag, uTag);
            else if (pCursor->cbLeft < 2)
                return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_BAD_INDEFINITE_LENGTH,
                                           "%s: Too little data left for indefinite BER/CER encoding (uTag=%#x)", pszErrorTag, uTag);
            else
            {
                pCursor->fFlags   |= RTASN1CURSOR_FLAGS_INDEFINITE_LENGTH;
                pAsn1Core->fFlags |= RTASN1CORE_F_INDEFINITE_LENGTH;
                cb = pCursor->cbLeft; /* Start out with the whole sequence, adjusted later upon reach the end. */
            }
        }
        /* else if (cb == 0 && uTag == 0) { end of content } - callers handle this */

        /* Check if the length makes sense. */
        if (cb > pCursor->cbLeft)
            return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_BAD_LENGTH,
                                       "%s: BER value length out of bounds: %#x (max=%#x uTag=%#x)",
                                       pszErrorTag, cb, pCursor->cbLeft, uTag);

        pAsn1Core->fFlags  |= RTASN1CORE_F_PRESENT | RTASN1CORE_F_DECODED_CONTENT;
        pAsn1Core->cb       = cb;
        pAsn1Core->uData.pv = (void *)pCursor->pbCur;
        return VINF_SUCCESS;
    }

    if (pCursor->cbLeft)
        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_TOO_LITTLE_DATA_LEFT,
                                   "%s: Too little data left to form a valid BER header", pszErrorTag);
    return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_NO_MORE_DATA,
                               "%s: No more data reading BER header", pszErrorTag);
}


RTDECL(int) RTAsn1CursorMatchTagClassFlagsEx(PRTASN1CURSOR pCursor, PRTASN1CORE pAsn1Core, uint32_t uTag, uint32_t fClass,
                                             bool fString, uint32_t fFlags, const char *pszErrorTag, const char *pszWhat)
{
    if (pAsn1Core->uTag == uTag)
    {
        if (pAsn1Core->fClass == fClass)
            return VINF_SUCCESS;
        if (   fString
            && pAsn1Core->fClass == (fClass | ASN1_TAGFLAG_CONSTRUCTED))
        {
            if (!(pCursor->fFlags & (RTASN1CURSOR_FLAGS_DER | RTASN1CURSOR_FLAGS_CER)))
                return VINF_SUCCESS;
            if (pCursor->fFlags & RTASN1CURSOR_FLAGS_CER)
            {
                if (pAsn1Core->cb > 1000)
                    return VINF_SUCCESS;
                return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_ILLEGAL_CONSTRUCTED_STRING,
                                           "%s: Constructed %s only allowed for >1000 byte in CER encoding: cb=%#x uTag=%#x fClass=%#x",
                                           pszErrorTag, pszWhat, pAsn1Core->cb, pAsn1Core->uTag, pAsn1Core->fClass);
            }
            return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_ILLEGAL_CONSTRUCTED_STRING,
                                       "%s: DER encoding does not allow constructed %s (cb=%#x uTag=%#x fClass=%#x)",
                                       pszErrorTag, pszWhat, pAsn1Core->cb, pAsn1Core->uTag, pAsn1Core->fClass);
        }
    }

    if (fFlags & RTASN1CURSOR_GET_F_IMPLICIT)
    {
        pAsn1Core->fFlags |= RTASN1CORE_F_TAG_IMPLICIT;
        pAsn1Core->uRealTag   = uTag;
        pAsn1Core->fRealClass = fClass;
        return VINF_SUCCESS;
    }

    return RTAsn1CursorSetInfo(pCursor, pAsn1Core->uTag != uTag ? VERR_ASN1_CURSOR_TAG_MISMATCH : VERR_ASN1_CURSOR_TAG_FLAG_CLASS_MISMATCH,
                               "%s: Unexpected %s type/flags: %#x/%#x (expected %#x/%#x)",
                               pszErrorTag, pszWhat, pAsn1Core->uTag, pAsn1Core->fClass, uTag, fClass);
}



static int rtAsn1CursorGetXxxxCursor(PRTASN1CURSOR pCursor, uint32_t fFlags, uint32_t uTag, uint8_t fClass,
                                     PRTASN1CORE pAsn1Core, PRTASN1CURSOR pRetCursor,
                                     const char *pszErrorTag, const char *pszWhat)
{
    int rc = RTAsn1CursorReadHdr(pCursor, pAsn1Core, pszErrorTag);
    if (RT_SUCCESS(rc))
    {
        if (   pAsn1Core->uTag == uTag
            && pAsn1Core->fClass == fClass)
            rc = VINF_SUCCESS;
        else if (fFlags & RTASN1CURSOR_GET_F_IMPLICIT)
        {
            pAsn1Core->fFlags |= RTASN1CORE_F_TAG_IMPLICIT;
            pAsn1Core->uRealTag   = uTag;
            pAsn1Core->fRealClass = fClass;
            rc = VINF_SUCCESS;
        }
        else
            return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_ILLEGAL_CONSTRUCTED_STRING,
                                       "%s: Unexpected %s type/flags: %#x/%#x (expected %#x/%#x)",
                                       pszErrorTag, pszWhat, pAsn1Core->uTag, pAsn1Core->fClass, uTag, fClass);
        rc = RTAsn1CursorInitSub(pCursor, pAsn1Core->cb, pRetCursor, pszErrorTag);
        if (RT_SUCCESS(rc))
        {
            pAsn1Core->fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
            return VINF_SUCCESS;
        }
    }
    return rc;
}


RTDECL(int) RTAsn1CursorGetSequenceCursor(PRTASN1CURSOR pCursor, uint32_t fFlags,
                                          PRTASN1SEQUENCECORE pSeqCore, PRTASN1CURSOR pSeqCursor, const char *pszErrorTag)
{
    return rtAsn1CursorGetXxxxCursor(pCursor, fFlags, ASN1_TAG_SEQUENCE, ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_CONSTRUCTED,
                                     &pSeqCore->Asn1Core, pSeqCursor, pszErrorTag, "sequence");
}


RTDECL(int) RTAsn1CursorGetSetCursor(PRTASN1CURSOR pCursor, uint32_t fFlags,
                                     PRTASN1SETCORE pSetCore, PRTASN1CURSOR pSetCursor, const char *pszErrorTag)
{
    return rtAsn1CursorGetXxxxCursor(pCursor, fFlags, ASN1_TAG_SET, ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_CONSTRUCTED,
                                     &pSetCore->Asn1Core, pSetCursor, pszErrorTag, "set");
}


RTDECL(int) RTAsn1CursorGetContextTagNCursor(PRTASN1CURSOR pCursor, uint32_t fFlags, uint32_t uExpectedTag,
                                             PCRTASN1COREVTABLE pVtable, PRTASN1CONTEXTTAG pCtxTag, PRTASN1CURSOR pCtxTagCursor,
                                             const char *pszErrorTag)
{
    int rc = rtAsn1CursorGetXxxxCursor(pCursor, fFlags, uExpectedTag, ASN1_TAGCLASS_CONTEXT | ASN1_TAGFLAG_CONSTRUCTED,
                                       &pCtxTag->Asn1Core, pCtxTagCursor, pszErrorTag, "ctx tag");
    pCtxTag->Asn1Core.pOps = pVtable;
    return rc;
}


RTDECL(int) RTAsn1CursorPeek(PRTASN1CURSOR pCursor, PRTASN1CORE pAsn1Core)
{
    uint32_t            cbSavedLeft         = pCursor->cbLeft;
    uint8_t const      *pbSavedCur          = pCursor->pbCur;
    uint8_t const       fSavedFlags         = pCursor->fFlags;
    PRTERRINFO const    pErrInfo            = pCursor->pPrimary->pErrInfo;
    pCursor->pPrimary->pErrInfo = NULL;

    int rc = RTAsn1CursorReadHdr(pCursor, pAsn1Core, "peek");

    pCursor->pPrimary->pErrInfo = pErrInfo;
    pCursor->pbCur              = pbSavedCur;
    pCursor->cbLeft             = cbSavedLeft;
    pCursor->fFlags             = fSavedFlags;
    return rc;
}


RTDECL(bool) RTAsn1CursorIsNextEx(PRTASN1CURSOR pCursor, uint32_t uTag, uint8_t fClass)
{
    RTASN1CORE Asn1Core;
    int rc = RTAsn1CursorPeek(pCursor, &Asn1Core);
    if (RT_SUCCESS(rc))
        return uTag   == Asn1Core.uTag
            && fClass == Asn1Core.fClass;
    return false;
}


/** @name Legacy Interfaces.
 * @{ */
RTDECL(int) RTAsn1CursorGetCore(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1CORE pAsn1Core, const char *pszErrorTag)
{
    return RTAsn1Core_DecodeAsn1(pCursor, fFlags, pAsn1Core, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetNull(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1NULL pNull, const char *pszErrorTag)
{
    return RTAsn1Null_DecodeAsn1(pCursor, fFlags, pNull, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetInteger(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1INTEGER pInteger, const char *pszErrorTag)
{
    return RTAsn1Integer_DecodeAsn1(pCursor, fFlags, pInteger, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetBoolean(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1BOOLEAN pBoolean, const char *pszErrorTag)
{
    return RTAsn1Boolean_DecodeAsn1(pCursor, fFlags, pBoolean, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetObjId(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1OBJID pObjId, const char *pszErrorTag)
{
    return RTAsn1ObjId_DecodeAsn1(pCursor, fFlags, pObjId, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetTime(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1TIME pTime, const char *pszErrorTag)
{
    return RTAsn1Time_DecodeAsn1(pCursor, fFlags, pTime, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetBitStringEx(PRTASN1CURSOR pCursor, uint32_t fFlags, uint32_t cMaxBits, PRTASN1BITSTRING pBitString,
                                       const char *pszErrorTag)
{
    return RTAsn1BitString_DecodeAsn1Ex(pCursor, fFlags, cMaxBits, pBitString, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetBitString(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1BITSTRING pBitString, const char *pszErrorTag)
{
    return RTAsn1BitString_DecodeAsn1(pCursor, fFlags, pBitString, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetOctetString(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1OCTETSTRING pOctetString,
                                       const char *pszErrorTag)
{
    return RTAsn1OctetString_DecodeAsn1(pCursor, fFlags, pOctetString, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetString(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1STRING pString, const char *pszErrorTag)
{
    return RTAsn1String_DecodeAsn1(pCursor, fFlags, pString, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetIa5String(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1STRING pString, const char *pszErrorTag)
{
    return RTAsn1Ia5String_DecodeAsn1(pCursor, fFlags, pString, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetUtf8String(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1STRING pString, const char *pszErrorTag)
{
    return RTAsn1Utf8String_DecodeAsn1(pCursor, fFlags, pString, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetBmpString(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1STRING pString, const char *pszErrorTag)
{
    return RTAsn1BmpString_DecodeAsn1(pCursor, fFlags, pString, pszErrorTag);
}


RTDECL(int) RTAsn1CursorGetDynType(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1DYNTYPE pDynType, const char *pszErrorTag)
{
    return RTAsn1DynType_DecodeAsn1(pCursor, fFlags, pDynType, pszErrorTag);
}
/** @} */

