/* $Id: bignum.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Big Integer Numbers.
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
/*#ifdef IN_RING3
# define RTMEM_WRAP_TO_EF_APIS
#endif*/
#include "internal/iprt.h"
#include <iprt/bignum.h>

#include <iprt/asm-mem.h>
#include <iprt/asm.h>
#include <iprt/asm-math.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/memsafer.h>
#include <iprt/string.h>
#if RTBIGNUM_ELEMENT_BITS == 64
# include <iprt/uint128.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Allocation alignment in elements. */
#ifndef RTMEM_WRAP_TO_EF_APIS
# define RTBIGNUM_ALIGNMENT             4U
#else
# define RTBIGNUM_ALIGNMENT             1U
#endif

/** The max size (in bytes) of an elements array. */
#define RTBIGNUM_MAX_SIZE               _4M


/** Assert the validity of a big number structure pointer in strict builds. */
#ifdef RT_STRICT
# define RTBIGNUM_ASSERT_VALID(a_pBigNum) \
    do { \
        AssertPtr(a_pBigNum); \
        Assert(!(a_pBigNum)->fCurScrambled); \
        Assert(   (a_pBigNum)->cUsed == (a_pBigNum)->cAllocated \
               || ASMMemIsZero(&(a_pBigNum)->pauElements[(a_pBigNum)->cUsed], \
                               ((a_pBigNum)->cAllocated - (a_pBigNum)->cUsed) * RTBIGNUM_ELEMENT_SIZE)); \
    } while (0)
#else
# define RTBIGNUM_ASSERT_VALID(a_pBigNum) do {} while (0)
#endif


/** Enable assembly optimizations. */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# define IPRT_BIGINT_WITH_ASM
#endif


/** @def RTBIGNUM_ZERO_ALIGN
 * For calculating the rtBigNumEnsureExtraZeroElements argument from cUsed.
 * This has to do with 64-bit assembly instruction operating as RTBIGNUMELEMENT
 * was 64-bit on some hosts.
 */
#if defined(IPRT_BIGINT_WITH_ASM) && ARCH_BITS == 64 && RTBIGNUM_ELEMENT_SIZE == 4 && defined(RT_LITTLE_ENDIAN)
# define RTBIGNUM_ZERO_ALIGN(a_cUsed)   RT_ALIGN_32(a_cUsed, 2)
#elif defined(IPRT_BIGINT_WITH_ASM)
# define RTBIGNUM_ZERO_ALIGN(a_cUsed)   (a_cUsed)
#else
# define RTBIGNUM_ZERO_ALIGN(a_cUsed)   (a_cUsed)
#endif

#define RTBIGNUMELEMENT_HALF_MASK              ( ((RTBIGNUMELEMENT)1 << (RTBIGNUM_ELEMENT_BITS / 2)) - (RTBIGNUMELEMENT)1)
#define RTBIGNUMELEMENT_LO_HALF(a_uElement)    ( (RTBIGNUMELEMENT_HALF_MASK) & (a_uElement) )
#define RTBIGNUMELEMENT_HI_HALF(a_uElement)    ( (a_uElement) >> (RTBIGNUM_ELEMENT_BITS / 2) )


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Type the size of two elements. */
#if RTBIGNUM_ELEMENT_BITS == 64
typedef RTUINT128U RTBIGNUMELEMENT2X;
#else
typedef RTUINT64U  RTBIGNUMELEMENT2X;
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLINLINE(int) rtBigNumSetUsed(PRTBIGNUM pBigNum, uint32_t cNewUsed);

#ifdef IPRT_BIGINT_WITH_ASM
/* bignum-amd64-x86.asm: */
DECLASM(void) rtBigNumMagnitudeSubAssemblyWorker(RTBIGNUMELEMENT *pauResult, RTBIGNUMELEMENT const *pauMinuend,
                                                 RTBIGNUMELEMENT const *pauSubtrahend, uint32_t cUsed);
DECLASM(void) rtBigNumMagnitudeSubThisAssemblyWorker(RTBIGNUMELEMENT *pauMinuendResult, RTBIGNUMELEMENT const *pauSubtrahend,
                                                     uint32_t cUsed);
DECLASM(RTBIGNUMELEMENT) rtBigNumMagnitudeShiftLeftOneAssemblyWorker(RTBIGNUMELEMENT *pauElements, uint32_t cUsed,
                                                                     RTBIGNUMELEMENT uCarry);
DECLASM(void) rtBigNumElement2xDiv2xBy1x(RTBIGNUMELEMENT2X *puQuotient, RTBIGNUMELEMENT *puRemainder,
                                         RTBIGNUMELEMENT uDividendHi, RTBIGNUMELEMENT uDividendLo, RTBIGNUMELEMENT uDivisor);
DECLASM(void) rtBigNumMagnitudeMultiplyAssemblyWorker(PRTBIGNUMELEMENT pauResult,
                                                      PCRTBIGNUMELEMENT pauMultiplier, uint32_t cMultiplier,
                                                      PCRTBIGNUMELEMENT pauMultiplicand, uint32_t cMultiplicand);
#endif





/** @name Functions working on one element.
 * @{  */

DECLINLINE(uint32_t) rtBigNumElementBitCount(RTBIGNUMELEMENT uElement)
{
#if RTBIGNUM_ELEMENT_SIZE == 8
    if (uElement >> 32)
        return ASMBitLastSetU32((uint32_t)(uElement >> 32)) + 32;
    return ASMBitLastSetU32((uint32_t)uElement);
#elif RTBIGNUM_ELEMENT_SIZE == 4
    return ASMBitLastSetU32(uElement);
#else
# error "Bad RTBIGNUM_ELEMENT_SIZE value"
#endif
}


/**
 * Does addition with carry.
 *
 * This is a candidate for inline assembly on some platforms.
 *
 * @returns The result (the sum)
 * @param   uAugend         What to add to.
 * @param   uAddend         What to add to it.
 * @param   pfCarry         Where to read the input carry and return the output
 *                          carry.
 */
DECLINLINE(RTBIGNUMELEMENT) rtBigNumElementAddWithCarry(RTBIGNUMELEMENT uAugend, RTBIGNUMELEMENT uAddend,
                                                        RTBIGNUMELEMENT *pfCarry)
{
    RTBIGNUMELEMENT uRet = uAugend + uAddend;
    if (!*pfCarry)
        *pfCarry = uRet < uAugend;
    else
    {
        uRet    += 1;
        *pfCarry = uRet <= uAugend;
    }
    return uRet;
}


#if !defined(IPRT_BIGINT_WITH_ASM) || defined(RT_STRICT)
/**
 * Does addition with borrow.
 *
 * This is a candidate for inline assembly on some platforms.
 *
 * @returns The result (the sum)
 * @param   uMinuend        What to subtract from.
 * @param   uSubtrahend     What to subtract.
 * @param   pfBorrow        Where to read the input borrow and return the output
 *                          borrow.
 */
DECLINLINE(RTBIGNUMELEMENT) rtBigNumElementSubWithBorrow(RTBIGNUMELEMENT uMinuend, RTBIGNUMELEMENT uSubtrahend,
                                                         RTBIGNUMELEMENT *pfBorrow)
{
    RTBIGNUMELEMENT uRet = uMinuend - uSubtrahend - *pfBorrow;

    /* Figure out if we borrowed. */
    *pfBorrow = !*pfBorrow ? uMinuend < uSubtrahend : uMinuend <= uSubtrahend;
    return uRet;
}
#endif

/** @} */




/** @name Double element primitives.
 * @{ */

static int rtBigNumElement2xCopyToMagnitude(RTBIGNUMELEMENT2X const *pValue2x, PRTBIGNUM pDst)
{
    int rc;
    if (pValue2x->s.Hi)
    {
        rc = rtBigNumSetUsed(pDst, 2);
        if (RT_SUCCESS(rc))
        {
            pDst->pauElements[0] = pValue2x->s.Lo;
            pDst->pauElements[1] = pValue2x->s.Hi;
        }
    }
    else if (pValue2x->s.Lo)
    {
        rc = rtBigNumSetUsed(pDst, 1);
        if (RT_SUCCESS(rc))
            pDst->pauElements[0] = pValue2x->s.Lo;
    }
    else
        rc = rtBigNumSetUsed(pDst, 0);
    return rc;
}

static void rtBigNumElement2xDiv(RTBIGNUMELEMENT2X *puQuotient, RTBIGNUMELEMENT2X *puRemainder,
                                 RTBIGNUMELEMENT uDividendHi, RTBIGNUMELEMENT uDividendLo,
                                 RTBIGNUMELEMENT uDivisorHi, RTBIGNUMELEMENT uDivisorLo)
{
    RTBIGNUMELEMENT2X uDividend;
    uDividend.s.Lo = uDividendLo;
    uDividend.s.Hi = uDividendHi;

    RTBIGNUMELEMENT2X uDivisor;
    uDivisor.s.Lo = uDivisorLo;
    uDivisor.s.Hi = uDivisorHi;

#if RTBIGNUM_ELEMENT_BITS == 64
    RTUInt128DivRem(puQuotient, puRemainder, &uDividend, &uDivisor);
#else
    puQuotient->u  = uDividend.u / uDivisor.u;
    puRemainder->u = uDividend.u % uDivisor.u;
#endif
}

#ifndef IPRT_BIGINT_WITH_ASM
static void rtBigNumElement2xDiv2xBy1x(RTBIGNUMELEMENT2X *puQuotient, RTBIGNUMELEMENT *puRemainder,
                                       RTBIGNUMELEMENT uDividendHi, RTBIGNUMELEMENT uDividendLo, RTBIGNUMELEMENT uDivisor)
{
    RTBIGNUMELEMENT2X uDividend;
    uDividend.s.Lo = uDividendLo;
    uDividend.s.Hi = uDividendHi;

# if RTBIGNUM_ELEMENT_BITS == 64
    RTBIGNUMELEMENT2X uRemainder2x;
    RTBIGNUMELEMENT2X uDivisor2x;
    uDivisor2x.s.Hi = 0;
    uDivisor2x.s.Lo = uDivisor;
    /** @todo optimize this. */
    RTUInt128DivRem(puQuotient, &uRemainder2x, &uDividend, &uDivisor2x);
    *puRemainder = uRemainder2x.s.Lo;
# else
    puQuotient->u  = uDividend.u / uDivisor;
    puRemainder->u = uDividend.u % uDivisor;
# endif
}
#endif

DECLINLINE(void) rtBigNumElement2xDec(RTBIGNUMELEMENT2X *puValue)
{
#if RTBIGNUM_ELEMENT_BITS == 64
    if (puValue->s.Lo-- == 0)
        puValue->s.Hi--;
#else
    puValue->u -= 1;
#endif
}

#if 0 /* unused */
DECLINLINE(void) rtBigNumElement2xAdd1x(RTBIGNUMELEMENT2X *puValue, RTBIGNUMELEMENT uAdd)
{
#if RTBIGNUM_ELEMENT_BITS == 64
    RTUInt128AssignAddU64(puValue, uAdd);
#else
    puValue->u += uAdd;
#endif
}
#endif /* unused */

/** @} */





/**
 * Scrambles a big number if required.
 *
 * @param   pBigNum     The big number.
 */
DECLINLINE(void) rtBigNumScramble(PRTBIGNUM pBigNum)
{
    if (pBigNum->fSensitive)
    {
        AssertReturnVoid(!pBigNum->fCurScrambled);
        if (pBigNum->pauElements)
        {
            int rc = RTMemSaferScramble(pBigNum->pauElements, pBigNum->cAllocated * RTBIGNUM_ELEMENT_SIZE); AssertRC(rc);
            pBigNum->fCurScrambled = RT_SUCCESS(rc);
        }
        else
            pBigNum->fCurScrambled = true;
    }
}


/**
 * Unscrambles a big number if required.
 *
 * @returns IPRT status code.
 * @param   pBigNum     The big number.
 */
DECLINLINE(int) rtBigNumUnscramble(PRTBIGNUM pBigNum)
{
    if (pBigNum->fSensitive)
    {
        AssertReturn(pBigNum->fCurScrambled, VERR_INTERNAL_ERROR_2);
        if (pBigNum->pauElements)
        {
            int rc = RTMemSaferUnscramble(pBigNum->pauElements, pBigNum->cAllocated * RTBIGNUM_ELEMENT_SIZE); AssertRC(rc);
            pBigNum->fCurScrambled = !RT_SUCCESS(rc);
            return rc;
        }
        else
            pBigNum->fCurScrambled = false;
    }
    return VINF_SUCCESS;
}


/**
 * Getter function for pauElements which extends the array to infinity.
 *
 * @returns The element value.
 * @param   pBigNum         The big number.
 * @param   iElement        The element index.
 */
DECLINLINE(RTBIGNUMELEMENT) rtBigNumGetElement(PCRTBIGNUM pBigNum, uint32_t iElement)
{
    if (iElement < pBigNum->cUsed)
        return pBigNum->pauElements[iElement];
    return 0;
}


/**
 * Grows the pauElements array so it can fit at least @a cNewUsed entries.
 *
 * @returns IPRT status code.
 * @param   pBigNum         The big number.
 * @param   cNewUsed        The new cUsed value.
 * @param   cMinElements    The minimum number of elements.
 */
static int rtBigNumGrow(PRTBIGNUM pBigNum, uint32_t cNewUsed, uint32_t cMinElements)
{
    Assert(cMinElements >= cNewUsed);
    uint32_t const cbOld = pBigNum->cAllocated * RTBIGNUM_ELEMENT_SIZE;
    uint32_t const cNew  = RT_ALIGN_32(cMinElements, RTBIGNUM_ALIGNMENT);
    uint32_t const cbNew = cNew * RTBIGNUM_ELEMENT_SIZE;
    Assert(cbNew > cbOld);
    if (cbNew <= RTBIGNUM_MAX_SIZE && cbNew > cbOld)
    {
        void *pvNew;
        if (pBigNum->fSensitive)
            pvNew = RTMemSaferReallocZ(cbOld, pBigNum->pauElements, cbNew);
        else
            pvNew = RTMemRealloc(pBigNum->pauElements, cbNew);
        if (RT_LIKELY(pvNew))
        {
            if (cbNew > cbOld)
                RT_BZERO((char *)pvNew + cbOld, cbNew - cbOld);
            if (pBigNum->cUsed > cNewUsed)
                RT_BZERO((RTBIGNUMELEMENT *)pvNew + cNewUsed, (pBigNum->cUsed - cNewUsed) * RTBIGNUM_ELEMENT_SIZE);

            pBigNum->pauElements = (RTBIGNUMELEMENT *)pvNew;
            pBigNum->cUsed       = cNewUsed;
            pBigNum->cAllocated  = cNew;
            return VINF_SUCCESS;
        }
        return VERR_NO_MEMORY;
    }
    return VERR_OUT_OF_RANGE;
}


/**
 * Changes the cUsed member, growing the pauElements array if necessary.
 *
 * Any elements added to the array will be initialized to zero.
 *
 * @returns IPRT status code.
 * @param   pBigNum         The big number.
 * @param   cNewUsed        The new cUsed value.
 */
DECLINLINE(int) rtBigNumSetUsed(PRTBIGNUM pBigNum, uint32_t cNewUsed)
{
    if (pBigNum->cAllocated >= cNewUsed)
    {
        if (pBigNum->cUsed > cNewUsed)
            RT_BZERO(&pBigNum->pauElements[cNewUsed], (pBigNum->cUsed - cNewUsed) * RTBIGNUM_ELEMENT_SIZE);
#ifdef RT_STRICT
        else if (pBigNum->cUsed != cNewUsed)
            Assert(ASMMemIsZero(&pBigNum->pauElements[pBigNum->cUsed], (cNewUsed - pBigNum->cUsed) * RTBIGNUM_ELEMENT_SIZE));
#endif
        pBigNum->cUsed = cNewUsed;
        return VINF_SUCCESS;
    }
    return rtBigNumGrow(pBigNum, cNewUsed, cNewUsed);
}


/**
 * Extended version of rtBigNumSetUsed that also allow specifying the number of
 * zero elements required.
 *
 * @returns IPRT status code.
 * @param   pBigNum         The big number.
 * @param   cNewUsed        The new cUsed value.
 * @param   cMinElements    The minimum number of elements allocated. The
 *                          difference between @a cNewUsed and @a cMinElements
 *                          is initialized to zero because all free elements are
 *                          zero.
 */
DECLINLINE(int) rtBigNumSetUsedEx(PRTBIGNUM pBigNum, uint32_t cNewUsed, uint32_t cMinElements)
{
    if (pBigNum->cAllocated >= cMinElements)
    {
        if (pBigNum->cUsed > cNewUsed)
            RT_BZERO(&pBigNum->pauElements[cNewUsed], (pBigNum->cUsed - cNewUsed) * RTBIGNUM_ELEMENT_SIZE);
#ifdef RT_STRICT
        else if (pBigNum->cUsed != cNewUsed)
            Assert(ASMMemIsZero(&pBigNum->pauElements[pBigNum->cUsed], (cNewUsed - pBigNum->cUsed) * RTBIGNUM_ELEMENT_SIZE));
#endif
        pBigNum->cUsed = cNewUsed;
        return VINF_SUCCESS;
    }
    return rtBigNumGrow(pBigNum, cNewUsed, cMinElements);
}


/**
 * For ensuring zero padding of pauElements for sub/add with carry assembly
 * operations.
 *
 * @returns IPRT status code.
 * @param   pBigNum         The big number.
 * @param   cElements       The number of elements that must be in the elements
 *                          array array, where those after pBigNum->cUsed must
 *                          be zero.
 */
DECLINLINE(int) rtBigNumEnsureExtraZeroElements(PRTBIGNUM pBigNum, uint32_t cElements)
{
    if (pBigNum->cAllocated >= cElements)
    {
        Assert(   pBigNum->cAllocated == pBigNum->cUsed
               || ASMMemIsZero(&pBigNum->pauElements[pBigNum->cUsed],
                                  (pBigNum->cAllocated - pBigNum->cUsed) * RTBIGNUM_ELEMENT_SIZE));
        return VINF_SUCCESS;
    }
    return rtBigNumGrow(pBigNum, pBigNum->cUsed, cElements);
}


/**
 * The slow part of rtBigNumEnsureElementPresent where we need to do actual zero
 * extending.
 *
 * @returns IPRT status code.
 * @param   pBigNum             The big number.
 * @param   iElement            The element we wish to access.
 */
static int rtBigNumEnsureElementPresentSlow(PRTBIGNUM pBigNum, uint32_t iElement)
{
    uint32_t const cOldUsed = pBigNum->cUsed;
    int rc = rtBigNumSetUsed(pBigNum, iElement + 1);
    if (RT_SUCCESS(rc))
    {
        RT_BZERO(&pBigNum->pauElements[cOldUsed], (iElement + 1 - cOldUsed) * RTBIGNUM_ELEMENT_SIZE);
        return VINF_SUCCESS;
    }
    return rc;
}


/**
 * Zero extends the element array to make sure a the specified element index is
 * accessible.
 *
 * This is typically used with bit operations and self modifying methods.  Any
 * new elements added will be initialized to zero.  The caller is responsible
 * for there not being any trailing zero elements.
 *
 * The number must be unscrambled.
 *
 * @returns IPRT status code.
 * @param   pBigNum             The big number.
 * @param   iElement            The element we wish to access.
 */
DECLINLINE(int) rtBigNumEnsureElementPresent(PRTBIGNUM pBigNum, uint32_t iElement)
{
    if (iElement < pBigNum->cUsed)
        return VINF_SUCCESS;
    return rtBigNumEnsureElementPresentSlow(pBigNum, iElement);
}


/**
 * Strips zero elements from the magnitude value.
 *
 * @param   pBigNum         The big number to strip.
 */
static void rtBigNumStripTrailingZeros(PRTBIGNUM pBigNum)
{
    uint32_t i = pBigNum->cUsed;
    while (i > 0 && pBigNum->pauElements[i - 1] == 0)
        i--;
    pBigNum->cUsed = i;
}


/**
 * Initialize the big number to zero.
 *
 * @returns @a pBigNum
 * @param   pBigNum         The big number.
 * @param   fFlags          The flags.
 * @internal
 */
DECLINLINE(PRTBIGNUM) rtBigNumInitZeroInternal(PRTBIGNUM pBigNum, uint32_t fFlags)
{
    RT_ZERO(*pBigNum);
    pBigNum->fSensitive = RT_BOOL(fFlags & RTBIGNUMINIT_F_SENSITIVE);
    return pBigNum;
}


/**
 * Initialize the big number to zero from a template variable.
 *
 * @returns @a pBigNum
 * @param   pBigNum         The big number.
 * @param   pTemplate       The template big number.
 * @internal
 */
DECLINLINE(PRTBIGNUM) rtBigNumInitZeroTemplate(PRTBIGNUM pBigNum, PCRTBIGNUM pTemplate)
{
    RT_ZERO(*pBigNum);
    pBigNum->fSensitive = pTemplate->fSensitive;
    return pBigNum;
}


RTDECL(int) RTBigNumInit(PRTBIGNUM pBigNum, uint32_t fFlags, void const *pvRaw, size_t cbRaw)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pBigNum, VERR_INVALID_POINTER);
    AssertReturn(RT_BOOL(fFlags & RTBIGNUMINIT_F_ENDIAN_BIG) ^ RT_BOOL(fFlags & RTBIGNUMINIT_F_ENDIAN_LITTLE),
                 VERR_INVALID_PARAMETER);
    AssertReturn(RT_BOOL(fFlags & RTBIGNUMINIT_F_UNSIGNED) ^ RT_BOOL(fFlags & RTBIGNUMINIT_F_SIGNED), VERR_INVALID_PARAMETER);
    if (cbRaw)
        AssertPtrReturn(pvRaw, VERR_INVALID_POINTER);

    /*
     * Initalize the big number to zero.
     */
    rtBigNumInitZeroInternal(pBigNum, fFlags);

    /*
     * Strip the input and figure the sign flag.
     */
    uint8_t const *pb = (uint8_t const *)pvRaw;
    if (cbRaw)
    {
        if (fFlags & RTBIGNUMINIT_F_ENDIAN_LITTLE)
        {
            if (fFlags & RTBIGNUMINIT_F_UNSIGNED)
            {
                while (cbRaw > 0 && pb[cbRaw - 1] == 0)
                    cbRaw--;
            }
            else
            {
                if (pb[cbRaw - 1] >> 7)
                {
                    pBigNum->fNegative = 1;
                    while (cbRaw > 1 && pb[cbRaw - 1] == 0xff)
                        cbRaw--;
                }
                else
                    while (cbRaw > 0 && pb[cbRaw - 1] == 0)
                        cbRaw--;
            }
        }
        else
        {
            if (fFlags & RTBIGNUMINIT_F_UNSIGNED)
            {
                while (cbRaw > 0 && *pb == 0)
                    pb++, cbRaw--;
            }
            else
            {
                if (*pb >> 7)
                {
                    pBigNum->fNegative = 1;
                    while (cbRaw > 1 && *pb == 0xff)
                        pb++, cbRaw--;
                }
                else
                    while (cbRaw > 0 && *pb == 0)
                        pb++, cbRaw--;
            }
        }
    }

    /*
     * Allocate memory for the elements.
     */
    size_t cbAligned = RT_ALIGN_Z(cbRaw, RTBIGNUM_ELEMENT_SIZE);
    if (RT_UNLIKELY(cbAligned >= RTBIGNUM_MAX_SIZE))
        return VERR_OUT_OF_RANGE;
    pBigNum->cUsed = (uint32_t)cbAligned / RTBIGNUM_ELEMENT_SIZE;
    if (pBigNum->cUsed)
    {
        pBigNum->cAllocated = RT_ALIGN_32(pBigNum->cUsed, RTBIGNUM_ALIGNMENT);
        if (pBigNum->fSensitive)
            pBigNum->pauElements = (RTBIGNUMELEMENT *)RTMemSaferAllocZ(pBigNum->cAllocated * RTBIGNUM_ELEMENT_SIZE);
        else
            pBigNum->pauElements = (RTBIGNUMELEMENT *)RTMemAlloc(pBigNum->cAllocated * RTBIGNUM_ELEMENT_SIZE);
        if (RT_UNLIKELY(!pBigNum->pauElements))
            return VERR_NO_MEMORY;

        /*
         * Initialize the array.
         */
        uint32_t i = 0;
        if (fFlags & RTBIGNUMINIT_F_ENDIAN_LITTLE)
        {
            while (cbRaw >= RTBIGNUM_ELEMENT_SIZE)
            {
#if RTBIGNUM_ELEMENT_SIZE == 8
                pBigNum->pauElements[i] = RT_MAKE_U64_FROM_U8(pb[0], pb[1], pb[2], pb[3], pb[4], pb[5], pb[6], pb[7]);
#elif RTBIGNUM_ELEMENT_SIZE == 4
                pBigNum->pauElements[i] = RT_MAKE_U32_FROM_U8(pb[0], pb[1], pb[2], pb[3]);
#else
# error "Bad RTBIGNUM_ELEMENT_SIZE value"
#endif
                i++;
                pb    += RTBIGNUM_ELEMENT_SIZE;
                cbRaw -= RTBIGNUM_ELEMENT_SIZE;
            }

            if (cbRaw > 0)
            {
                RTBIGNUMELEMENT uLast = pBigNum->fNegative ? ~(RTBIGNUMELEMENT)0 : 0;
                switch (cbRaw)
                {
                    default: AssertFailed();
#if RTBIGNUM_ELEMENT_SIZE == 8
                                                          RT_FALL_THRU();
                    case 7: uLast = (uLast << 8) | pb[6]; RT_FALL_THRU();
                    case 6: uLast = (uLast << 8) | pb[5]; RT_FALL_THRU();
                    case 5: uLast = (uLast << 8) | pb[4]; RT_FALL_THRU();
                    case 4: uLast = (uLast << 8) | pb[3];
#endif
                                                          RT_FALL_THRU();
                    case 3: uLast = (uLast << 8) | pb[2]; RT_FALL_THRU();
                    case 2: uLast = (uLast << 8) | pb[1]; RT_FALL_THRU();
                    case 1: uLast = (uLast << 8) | pb[0];
                }
                pBigNum->pauElements[i] = uLast;
            }
        }
        else
        {
            pb += cbRaw;
            while (cbRaw >= RTBIGNUM_ELEMENT_SIZE)
            {
                pb -= RTBIGNUM_ELEMENT_SIZE;
#if RTBIGNUM_ELEMENT_SIZE == 8
                pBigNum->pauElements[i] = RT_MAKE_U64_FROM_U8(pb[7], pb[6], pb[5], pb[4], pb[3], pb[2], pb[1], pb[0]);
#elif RTBIGNUM_ELEMENT_SIZE == 4
                pBigNum->pauElements[i] = RT_MAKE_U32_FROM_U8(pb[3], pb[2], pb[1], pb[0]);
#else
# error "Bad RTBIGNUM_ELEMENT_SIZE value"
#endif
                i++;
                cbRaw -= RTBIGNUM_ELEMENT_SIZE;
            }

            if (cbRaw > 0)
            {
                RTBIGNUMELEMENT uLast = pBigNum->fNegative ? ~(RTBIGNUMELEMENT)0 : 0;
                pb -= cbRaw;
                switch (cbRaw)
                {
                    default: AssertFailed();
#if RTBIGNUM_ELEMENT_SIZE == 8
                                                          RT_FALL_THRU();
                    case 7: uLast = (uLast << 8) | *pb++; RT_FALL_THRU();
                    case 6: uLast = (uLast << 8) | *pb++; RT_FALL_THRU();
                    case 5: uLast = (uLast << 8) | *pb++; RT_FALL_THRU();
                    case 4: uLast = (uLast << 8) | *pb++;
#endif
                                                          RT_FALL_THRU();
                    case 3: uLast = (uLast << 8) | *pb++; RT_FALL_THRU();
                    case 2: uLast = (uLast << 8) | *pb++; RT_FALL_THRU();
                    case 1: uLast = (uLast << 8) | *pb++;
                }
                pBigNum->pauElements[i] = uLast;
            }
        }

        /*
         * If negative, negate it so we get a positive magnitude value in pauElements.
         */
        if (pBigNum->fNegative)
        {
            pBigNum->pauElements[0] = 0U - pBigNum->pauElements[0];
            for (i = 1; i < pBigNum->cUsed; i++)
                pBigNum->pauElements[i] = 0U - pBigNum->pauElements[i] - 1U;
        }

        /*
         * Clear unused elements.
         */
        if (pBigNum->cUsed != pBigNum->cAllocated)
        {
            RTBIGNUMELEMENT *puUnused = &pBigNum->pauElements[pBigNum->cUsed];
            AssertCompile(RTBIGNUM_ALIGNMENT <= 4);
            switch (pBigNum->cAllocated - pBigNum->cUsed)
            {
                default: AssertFailed(); RT_FALL_THRU();
                case 3: *puUnused++ = 0; RT_FALL_THRU();
                case 2: *puUnused++ = 0; RT_FALL_THRU();
                case 1: *puUnused++ = 0;
            }
        }
        RTBIGNUM_ASSERT_VALID(pBigNum);
    }

    rtBigNumScramble(pBigNum);
    return VINF_SUCCESS;
}


RTDECL(int) RTBigNumInitZero(PRTBIGNUM pBigNum, uint32_t fFlags)
{
    AssertReturn(!(fFlags & ~RTBIGNUMINIT_F_SENSITIVE), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pBigNum, VERR_INVALID_POINTER);

    rtBigNumInitZeroInternal(pBigNum, fFlags);
    rtBigNumScramble(pBigNum);
    return VINF_SUCCESS;
}


/**
 * Internal clone function that assumes the caller takes care of scrambling.
 *
 * @returns IPRT status code.
 * @param   pBigNum         The target number.
 * @param   pSrc            The source number.
 */
static int rtBigNumCloneInternal(PRTBIGNUM pBigNum, PCRTBIGNUM pSrc)
{
    Assert(!pSrc->fCurScrambled);
    int rc = VINF_SUCCESS;

    /*
     * Copy over the data.
     */
    RT_ZERO(*pBigNum);
    pBigNum->fNegative  = pSrc->fNegative;
    pBigNum->fSensitive = pSrc->fSensitive;
    pBigNum->cUsed      = pSrc->cUsed;
    if (pSrc->cUsed)
    {
        /* Duplicate the element array. */
        pBigNum->cAllocated = RT_ALIGN_32(pBigNum->cUsed, RTBIGNUM_ALIGNMENT);
        if (pBigNum->fSensitive)
            pBigNum->pauElements = (RTBIGNUMELEMENT *)RTMemSaferAllocZ(pBigNum->cAllocated * RTBIGNUM_ELEMENT_SIZE);
        else
            pBigNum->pauElements = (RTBIGNUMELEMENT *)RTMemAlloc(pBigNum->cAllocated * RTBIGNUM_ELEMENT_SIZE);
        if (RT_LIKELY(pBigNum->pauElements))
        {
            memcpy(pBigNum->pauElements, pSrc->pauElements, pBigNum->cUsed * RTBIGNUM_ELEMENT_SIZE);
            if (pBigNum->cUsed != pBigNum->cAllocated)
                RT_BZERO(&pBigNum->pauElements[pBigNum->cUsed], (pBigNum->cAllocated - pBigNum->cUsed) * RTBIGNUM_ELEMENT_SIZE);
        }
        else
        {
            RT_ZERO(*pBigNum);
            rc = VERR_NO_MEMORY;
        }
    }
    return rc;
}


RTDECL(int) RTBigNumClone(PRTBIGNUM pBigNum, PCRTBIGNUM pSrc)
{
    int rc = rtBigNumUnscramble((PRTBIGNUM)pSrc);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pSrc);
        rc = rtBigNumCloneInternal(pBigNum, pSrc);
        if (RT_SUCCESS(rc))
            rtBigNumScramble(pBigNum);
        rtBigNumScramble((PRTBIGNUM)pSrc);
    }
    return rc;
}


RTDECL(int) RTBigNumDestroy(PRTBIGNUM pBigNum)
{
    if (pBigNum)
    {
        if (pBigNum->pauElements)
        {
            Assert(pBigNum->cAllocated > 0);
            if (!pBigNum->fSensitive)
                RTMemFree(pBigNum->pauElements);
            else
            {
                RTMemSaferFree(pBigNum->pauElements, pBigNum->cAllocated * RTBIGNUM_ELEMENT_SIZE);
                RT_ZERO(*pBigNum);
            }
            pBigNum->pauElements = NULL;
        }
    }
    return VINF_SUCCESS;
}


RTDECL(int) RTBigNumAssign(PRTBIGNUM pDst, PCRTBIGNUM pSrc)
{
    AssertReturn(pDst->fSensitive >= pSrc->fSensitive, VERR_BIGNUM_SENSITIVE_INPUT);
    int rc = rtBigNumUnscramble(pDst);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pDst);
        rc = rtBigNumUnscramble((PRTBIGNUM)pSrc);
        if (RT_SUCCESS(rc))
        {
            RTBIGNUM_ASSERT_VALID(pSrc);
            if (   pDst->fSensitive == pSrc->fSensitive
                || pDst->fSensitive)
            {
                if (pDst->cAllocated >= pSrc->cUsed)
                {
                    if (pDst->cUsed > pSrc->cUsed)
                        RT_BZERO(&pDst->pauElements[pSrc->cUsed], (pDst->cUsed - pSrc->cUsed) * RTBIGNUM_ELEMENT_SIZE);
                    pDst->cUsed     = pSrc->cUsed;
                    pDst->fNegative = pSrc->fNegative;
                    memcpy(pDst->pauElements, pSrc->pauElements, pSrc->cUsed * RTBIGNUM_ELEMENT_SIZE);
                }
                else
                {
                    rc = rtBigNumGrow(pDst, pSrc->cUsed, pSrc->cUsed);
                    if (RT_SUCCESS(rc))
                    {
                        pDst->fNegative = pSrc->fNegative;
                        memcpy(pDst->pauElements, pSrc->pauElements, pSrc->cUsed * RTBIGNUM_ELEMENT_SIZE);
                    }
                }
            }
            else
                rc = VERR_BIGNUM_SENSITIVE_INPUT;
            rtBigNumScramble((PRTBIGNUM)pSrc);
        }
        rtBigNumScramble(pDst);
    }
    return rc;
}


/**
 * Same as RTBigNumBitWidth, except that it ignore the signed bit.
 *
 * The number must be unscrambled.
 *
 * @returns The effective width of the magnitude, in bits.  Returns 0 if the
 *          value is zero.
 * @param   pBigNum         The bit number.
 */
static uint32_t rtBigNumMagnitudeBitWidth(PCRTBIGNUM pBigNum)
{
    uint32_t idxLast = pBigNum->cUsed;
    if (idxLast)
    {
        idxLast--;
        RTBIGNUMELEMENT uLast = pBigNum->pauElements[idxLast]; Assert(uLast);
        return rtBigNumElementBitCount(uLast) + idxLast * RTBIGNUM_ELEMENT_BITS;
    }
    return 0;
}


RTDECL(uint32_t) RTBigNumBitWidth(PCRTBIGNUM pBigNum)
{
    uint32_t idxLast = pBigNum->cUsed;
    if (idxLast)
    {
        idxLast--;
        rtBigNumUnscramble((PRTBIGNUM)pBigNum);
        RTBIGNUMELEMENT uLast = pBigNum->pauElements[idxLast]; Assert(uLast);
        rtBigNumScramble((PRTBIGNUM)pBigNum);
        return rtBigNumElementBitCount(uLast) + idxLast * RTBIGNUM_ELEMENT_BITS + pBigNum->fNegative;
    }
    return 0;
}


RTDECL(uint32_t) RTBigNumByteWidth(PCRTBIGNUM pBigNum)
{
    uint32_t cBits = RTBigNumBitWidth(pBigNum);
    return (cBits + 7) / 8;
}


RTDECL(int) RTBigNumToBytesBigEndian(PCRTBIGNUM pBigNum, void *pvBuf, size_t cbWanted)
{
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbWanted > 0, VERR_INVALID_PARAMETER);

    int rc = rtBigNumUnscramble((PRTBIGNUM)pBigNum);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pBigNum);
        rc = VINF_SUCCESS;
        if (pBigNum->cUsed != 0)
        {
            uint8_t *pbDst = (uint8_t *)pvBuf;
            pbDst += cbWanted - 1;
            for (uint32_t i = 0; i < pBigNum->cUsed; i++)
            {
                RTBIGNUMELEMENT uElement = pBigNum->pauElements[i];
                if (pBigNum->fNegative)
                    uElement = (RTBIGNUMELEMENT)0 - uElement - (i > 0);
                if (cbWanted >= sizeof(uElement))
                {
                    *pbDst-- = (uint8_t)uElement;
                    uElement >>= 8;
                    *pbDst-- = (uint8_t)uElement;
                    uElement >>= 8;
                    *pbDst-- = (uint8_t)uElement;
                    uElement >>= 8;
                    *pbDst-- = (uint8_t)uElement;
#if RTBIGNUM_ELEMENT_SIZE == 8
                    uElement >>= 8;
                    *pbDst-- = (uint8_t)uElement;
                    uElement >>= 8;
                    *pbDst-- = (uint8_t)uElement;
                    uElement >>= 8;
                    *pbDst-- = (uint8_t)uElement;
                    uElement >>= 8;
                    *pbDst-- = (uint8_t)uElement;
#elif RTBIGNUM_ELEMENT_SIZE != 4
# error "Bad RTBIGNUM_ELEMENT_SIZE value"
#endif
                    cbWanted -= sizeof(uElement);
                }
                else
                {

                    uint32_t cBitsLeft = RTBIGNUM_ELEMENT_BITS;
                    while (cbWanted > 0)
                    {
                        *pbDst-- = (uint8_t)uElement;
                        uElement >>= 8;
                        cBitsLeft -= 8;
                        cbWanted--;
                    }
                    Assert(cBitsLeft > 0); Assert(cBitsLeft < RTBIGNUM_ELEMENT_BITS);
                    if (   i + 1 < pBigNum->cUsed
                        || (  !pBigNum->fNegative
                            ? uElement != 0
                            : uElement != ((RTBIGNUMELEMENT)1 << cBitsLeft) - 1U ) )
                        rc = VERR_BUFFER_OVERFLOW;
                    break;
                }
            }

            /* Sign extend the number to the desired output size. */
            if (cbWanted > 0)
                memset(pbDst - cbWanted, pBigNum->fNegative ? 0 : 0xff, cbWanted);
        }
        else
            RT_BZERO(pvBuf, cbWanted);
        rtBigNumScramble((PRTBIGNUM)pBigNum);
    }
    return rc;
}


RTDECL(int) RTBigNumCompare(PRTBIGNUM pLeft, PRTBIGNUM pRight)
{
    int rc = rtBigNumUnscramble(pLeft);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pLeft);
        rc = rtBigNumUnscramble(pRight);
        if (RT_SUCCESS(rc))
        {
            RTBIGNUM_ASSERT_VALID(pRight);
            if (pLeft->fNegative == pRight->fNegative)
            {
                if (pLeft->cUsed == pRight->cUsed)
                {
                    rc = 0;
                    uint32_t i = pLeft->cUsed;
                    while (i-- > 0)
                        if (pLeft->pauElements[i] != pRight->pauElements[i])
                        {
                            rc = pLeft->pauElements[i] < pRight->pauElements[i] ? -1 : 1;
                            break;
                        }
                    if (pLeft->fNegative)
                        rc = -rc;
                }
                else
                    rc = !pLeft->fNegative
                       ? pLeft->cUsed < pRight->cUsed ? -1 : 1
                       : pLeft->cUsed < pRight->cUsed ? 1 : -1;
            }
            else
                rc = pLeft->fNegative ? -1 : 1;

            rtBigNumScramble(pRight);
        }
        rtBigNumScramble(pLeft);
    }
    return rc;
}


RTDECL(int) RTBigNumCompareWithU64(PRTBIGNUM pLeft, uint64_t uRight)
{
    int rc = rtBigNumUnscramble(pLeft);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pLeft);
        if (!pLeft->fNegative)
        {
            if (pLeft->cUsed * RTBIGNUM_ELEMENT_SIZE <= sizeof(uRight))
            {
                if (pLeft->cUsed == 0)
                    rc = uRight == 0 ? 0 : -1;
                else
                {
#if RTBIGNUM_ELEMENT_SIZE == 8
                    uint64_t uLeft = rtBigNumGetElement(pLeft, 0);
                    if (uLeft < uRight)
                        rc = -1;
                    else
                        rc = uLeft == uRight ? 0 : 1;
#elif RTBIGNUM_ELEMENT_SIZE == 4
                    uint32_t uSubLeft  = rtBigNumGetElement(pLeft, 1);
                    uint32_t uSubRight = uRight >> 32;
                    if (uSubLeft == uSubRight)
                    {
                        uSubLeft  = rtBigNumGetElement(pLeft, 0);
                        uSubRight = (uint32_t)uRight;
                    }
                    if (uSubLeft < uSubRight)
                        rc = -1;
                    else
                        rc = uSubLeft == uSubRight ? 0 : 1;
#else
# error "Bad RTBIGNUM_ELEMENT_SIZE value"
#endif
                }
            }
            else
                rc = 1;
        }
        else
            rc = -1;
        rtBigNumScramble(pLeft);
    }
    return rc;
}


RTDECL(int) RTBigNumCompareWithS64(PRTBIGNUM pLeft, int64_t iRight)
{
    int rc = rtBigNumUnscramble(pLeft);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pLeft);
        if (pLeft->fNegative == (unsigned)(iRight < 0)) /* (unsigned cast is for MSC weirdness) */
        {
            AssertCompile(RTBIGNUM_ELEMENT_SIZE <= sizeof(iRight));
            if (pLeft->cUsed * RTBIGNUM_ELEMENT_SIZE <= sizeof(iRight))
            {
                uint64_t uRightMagn = !pLeft->fNegative ? (uint64_t)iRight : (uint64_t)-iRight;
#if RTBIGNUM_ELEMENT_SIZE == 8
                uint64_t uLeft = rtBigNumGetElement(pLeft, 0);
                if (uLeft < uRightMagn)
                    rc = -1;
                else
                    rc = uLeft == (uint64_t)uRightMagn ? 0 : 1;
#elif RTBIGNUM_ELEMENT_SIZE == 4
                uint32_t uSubLeft  = rtBigNumGetElement(pLeft, 1);
                uint32_t uSubRight = uRightMagn >> 32;
                if (uSubLeft == uSubRight)
                {
                    uSubLeft  = rtBigNumGetElement(pLeft, 0);
                    uSubRight = (uint32_t)uRightMagn;
                }
                if (uSubLeft < uSubRight)
                    rc = -1;
                else
                    rc = uSubLeft == uSubRight ? 0 : 1;
#else
# error "Bad RTBIGNUM_ELEMENT_SIZE value"
#endif
                if (pLeft->fNegative)
                    rc = -rc;
            }
            else
                rc = pLeft->fNegative ? -1 : 1;
        }
        else
            rc = pLeft->fNegative ? -1 : 1;
        rtBigNumScramble(pLeft);
    }
    return rc;
}


/**
 * Compares the magnitude values of two big numbers.
 *
 * @retval  -1 if pLeft is smaller than pRight.
 * @retval  0 if pLeft is equal to pRight.
 * @retval  1 if pLeft is larger than pRight.
 * @param   pLeft           The left side number.
 * @param   pRight          The right side number.
 */
static int rtBigNumMagnitudeCompare(PCRTBIGNUM pLeft, PCRTBIGNUM pRight)
{
    Assert(!pLeft->fCurScrambled); Assert(!pRight->fCurScrambled);
    int rc;
    uint32_t i = pLeft->cUsed;
    if (i == pRight->cUsed)
    {
        rc = 0;
        while (i-- > 0)
            if (pLeft->pauElements[i] != pRight->pauElements[i])
            {
                rc = pLeft->pauElements[i] < pRight->pauElements[i] ? -1 : 1;
                break;
            }
    }
    else
        rc = i < pRight->cUsed ? -1 : 1;
    return rc;
}


/**
 * Copies the magnitude of on number (@a pSrc) to another (@a pBigNum).
 *
 * The variables must be unscrambled.  The sign flag is not considered nor
 * touched.
 *
 * @returns IPRT status code.
 * @param   pDst            The destination number.
 * @param   pSrc            The source number.
 */
DECLINLINE(int) rtBigNumMagnitudeCopy(PRTBIGNUM pDst, PCRTBIGNUM pSrc)
{
    int rc = rtBigNumSetUsed(pDst, pSrc->cUsed);
    if (RT_SUCCESS(rc))
        memcpy(pDst->pauElements, pSrc->pauElements, pSrc->cUsed * RTBIGNUM_ELEMENT_SIZE);
    return rc;
}



/**
 * Adds two magnitudes and stores them into a third.
 *
 * All variables must be unscrambled.  The sign flag is not considered nor
 * touched.
 *
 * @returns IPRT status code.
 * @param   pResult         The resultant.
 * @param   pAugend         To whom it shall be addede.
 * @param   pAddend         The nombre to addede.
 */
static int rtBigNumMagnitudeAdd(PRTBIGNUM pResult, PCRTBIGNUM pAugend, PCRTBIGNUM pAddend)
{
    Assert(!pResult->fCurScrambled); Assert(!pAugend->fCurScrambled); Assert(!pAddend->fCurScrambled);
    Assert(pResult != pAugend); Assert(pResult != pAddend);

    uint32_t cElements = RT_MAX(pAugend->cUsed, pAddend->cUsed);
    int rc = rtBigNumSetUsed(pResult, cElements);
    if (RT_SUCCESS(rc))
    {
        /*
         * The primitive way, requires at least two additions for each entry
         * without machine code help.
         */
        RTBIGNUMELEMENT fCarry = 0;
        for (uint32_t i = 0; i < cElements; i++)
            pResult->pauElements[i] = rtBigNumElementAddWithCarry(rtBigNumGetElement(pAugend, i),
                                                                  rtBigNumGetElement(pAddend, i),
                                                                  &fCarry);
        if (fCarry)
        {
            rc = rtBigNumSetUsed(pResult, cElements + 1);
            if (RT_SUCCESS(rc))
                pResult->pauElements[cElements++] = 1;
        }
        Assert(pResult->cUsed == cElements || RT_FAILURE_NP(rc));
    }

    return rc;
}


/**
 * Substracts a smaller (or equal) magnitude from another one and stores it into
 * a third.
 *
 * All variables must be unscrambled.  The sign flag is not considered nor
 * touched.  For this reason, the @a pMinuend must be larger or equal to @a
 * pSubtrahend.
 *
 * @returns IPRT status code.
 * @param   pResult             There to store the result.
 * @param   pMinuend            What to subtract from.
 * @param   pSubtrahend         What to subtract.
 */
static int rtBigNumMagnitudeSub(PRTBIGNUM pResult, PCRTBIGNUM pMinuend, PCRTBIGNUM pSubtrahend)
{
    Assert(!pResult->fCurScrambled); Assert(!pMinuend->fCurScrambled); Assert(!pSubtrahend->fCurScrambled);
    Assert(pResult != pMinuend); Assert(pResult != pSubtrahend);
    Assert(pMinuend->cUsed >= pSubtrahend->cUsed);

    int rc;
    if (pSubtrahend->cUsed)
    {
        /*
         * Resize the result. In the assembly case, ensure that all three arrays
         * has the same number of used entries, possibly with an extra zero
         * element on 64-bit systems.
         */
        rc = rtBigNumSetUsedEx(pResult, pMinuend->cUsed, RTBIGNUM_ZERO_ALIGN(pMinuend->cUsed));
#ifdef IPRT_BIGINT_WITH_ASM
        if (RT_SUCCESS(rc))
            rc = rtBigNumEnsureExtraZeroElements((PRTBIGNUM)pMinuend, RTBIGNUM_ZERO_ALIGN(pMinuend->cUsed));
        if (RT_SUCCESS(rc))
            rc = rtBigNumEnsureExtraZeroElements((PRTBIGNUM)pSubtrahend, RTBIGNUM_ZERO_ALIGN(pMinuend->cUsed));
#endif
        if (RT_SUCCESS(rc))
        {
#ifdef IPRT_BIGINT_WITH_ASM
            /*
             * Call assembly to do the work.
             */
            rtBigNumMagnitudeSubAssemblyWorker(pResult->pauElements, pMinuend->pauElements,
                                               pSubtrahend->pauElements, pMinuend->cUsed);
# ifdef RT_STRICT
            RTBIGNUMELEMENT fBorrow = 0;
            for (uint32_t i = 0; i < pMinuend->cUsed; i++)
            {
                RTBIGNUMELEMENT uCorrect = rtBigNumElementSubWithBorrow(pMinuend->pauElements[i], rtBigNumGetElement(pSubtrahend, i), &fBorrow);
                AssertMsg(pResult->pauElements[i] == uCorrect, ("[%u]=%#x, expected %#x\n", i, pResult->pauElements[i], uCorrect));
            }
# endif
#else
            /*
             * The primitive C way.
             */
            RTBIGNUMELEMENT fBorrow = 0;
            for (uint32_t i = 0; i < pMinuend->cUsed; i++)
                pResult->pauElements[i] = rtBigNumElementSubWithBorrow(pMinuend->pauElements[i],
                                                                       rtBigNumGetElement(pSubtrahend, i),
                                                                       &fBorrow);
            Assert(fBorrow == 0);
#endif

            /*
             * Trim the result.
             */
            rtBigNumStripTrailingZeros(pResult);
        }
    }
    /*
     * Special case: Subtrahend is zero.
     */
    else
        rc = rtBigNumMagnitudeCopy(pResult, pMinuend);

    return rc;
}


/**
 * Substracts a smaller (or equal) magnitude from another one and stores the
 * result into the first.
 *
 * All variables must be unscrambled.  The sign flag is not considered nor
 * touched.  For this reason, the @a pMinuendResult must be larger or equal to
 * @a pSubtrahend.
 *
 * @returns IPRT status code (memory alloc error).
 * @param   pMinuendResult      What to subtract from and return as result.
 * @param   pSubtrahend         What to subtract.
 */
static int rtBigNumMagnitudeSubThis(PRTBIGNUM pMinuendResult, PCRTBIGNUM pSubtrahend)
{
    Assert(!pMinuendResult->fCurScrambled); Assert(!pSubtrahend->fCurScrambled);
    Assert(pMinuendResult != pSubtrahend);
    Assert(pMinuendResult->cUsed >= pSubtrahend->cUsed);

#ifdef IPRT_BIGINT_WITH_ASM
    /*
     * Use the assembly worker. Requires same sized element arrays, so zero extend them.
     */
    int rc = rtBigNumEnsureExtraZeroElements(pMinuendResult, RTBIGNUM_ZERO_ALIGN(pMinuendResult->cUsed));
    if (RT_SUCCESS(rc))
        rc = rtBigNumEnsureExtraZeroElements((PRTBIGNUM)pSubtrahend, RTBIGNUM_ZERO_ALIGN(pMinuendResult->cUsed));
    if (RT_FAILURE(rc))
        return rc;
    rtBigNumMagnitudeSubThisAssemblyWorker(pMinuendResult->pauElements, pSubtrahend->pauElements, pMinuendResult->cUsed);
#else
    /*
     * The primitive way, as usual.
     */
    RTBIGNUMELEMENT fBorrow = 0;
    for (uint32_t i = 0; i < pMinuendResult->cUsed; i++)
        pMinuendResult->pauElements[i] = rtBigNumElementSubWithBorrow(pMinuendResult->pauElements[i],
                                                                      rtBigNumGetElement(pSubtrahend, i),
                                                                      &fBorrow);
    Assert(fBorrow == 0);
#endif

    /*
     * Trim the result.
     */
    rtBigNumStripTrailingZeros(pMinuendResult);

    return VINF_SUCCESS;
}


RTDECL(int) RTBigNumAdd(PRTBIGNUM pResult, PCRTBIGNUM pAugend, PCRTBIGNUM pAddend)
{
    Assert(pResult != pAugend); Assert(pResult != pAddend);
    AssertReturn(pResult->fSensitive >= (pAugend->fSensitive | pAddend->fSensitive), VERR_BIGNUM_SENSITIVE_INPUT);

    int rc = rtBigNumUnscramble(pResult);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pResult);
        rc = rtBigNumUnscramble((PRTBIGNUM)pAugend);
        if (RT_SUCCESS(rc))
        {
            RTBIGNUM_ASSERT_VALID(pAugend);
            rc = rtBigNumUnscramble((PRTBIGNUM)pAddend);
            if (RT_SUCCESS(rc))
            {
                RTBIGNUM_ASSERT_VALID(pAddend);

                /*
                 * Same sign: Add magnitude, keep sign.
                 *       1  +   1  =  2
                 *     (-1) + (-1) = -2
                 */
                if (pAugend->fNegative == pAddend->fNegative)
                {
                    pResult->fNegative = pAugend->fNegative;
                    rc = rtBigNumMagnitudeAdd(pResult, pAugend, pAddend);
                }
                /*
                 * Different sign: Subtract smaller from larger, keep sign of larger.
                 *     (-5) +   3  = -2
                 *       5  + (-3) =  2
                 *     (-1) +   3  =  2
                 *       1  + (-3) = -2
                 */
                else if (rtBigNumMagnitudeCompare(pAugend, pAddend) >= 0)
                {
                    pResult->fNegative = pAugend->fNegative;
                    rc = rtBigNumMagnitudeSub(pResult, pAugend, pAddend);
                    if (!pResult->cUsed)
                        pResult->fNegative = 0;
                }
                else
                {
                    pResult->fNegative = pAddend->fNegative;
                    rc = rtBigNumMagnitudeSub(pResult, pAddend, pAugend);
                }
                rtBigNumScramble((PRTBIGNUM)pAddend);
            }
            rtBigNumScramble((PRTBIGNUM)pAugend);
        }
        rtBigNumScramble(pResult);
    }
    return rc;
}


RTDECL(int) RTBigNumSubtract(PRTBIGNUM pResult, PCRTBIGNUM pMinuend, PCRTBIGNUM pSubtrahend)
{
    Assert(pResult != pMinuend); Assert(pResult != pSubtrahend);
    AssertReturn(pResult->fSensitive >= (pMinuend->fSensitive | pSubtrahend->fSensitive), VERR_BIGNUM_SENSITIVE_INPUT);

    int rc = rtBigNumUnscramble(pResult);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pResult);
        if (pMinuend != pSubtrahend)
        {
            rc = rtBigNumUnscramble((PRTBIGNUM)pMinuend);
            if (RT_SUCCESS(rc))
            {
                RTBIGNUM_ASSERT_VALID(pMinuend);
                rc = rtBigNumUnscramble((PRTBIGNUM)pSubtrahend);
                if (RT_SUCCESS(rc))
                {
                    RTBIGNUM_ASSERT_VALID(pSubtrahend);

                    /*
                     * Different sign: Add magnitude, keep sign of first.
                     *       1 - (-2) ==  3
                     *      -1 -   2  == -3
                     */
                    if (pMinuend->fNegative != pSubtrahend->fNegative)
                    {
                        pResult->fNegative = pMinuend->fNegative;
                        rc = rtBigNumMagnitudeAdd(pResult, pMinuend, pSubtrahend);
                    }
                    /*
                     * Same sign, minuend has greater or equal absolute value: Subtract, keep sign of first.
                     *      10 - 7 = 3
                     */
                    else if (rtBigNumMagnitudeCompare(pMinuend, pSubtrahend) >= 0)
                    {
                        pResult->fNegative = pMinuend->fNegative;
                        rc = rtBigNumMagnitudeSub(pResult, pMinuend, pSubtrahend);
                    }
                    /*
                     * Same sign, subtrahend is larger: Reverse and subtract, invert sign of first.
                     *      7 -  10  = -3
                     *     -1 - (-3) =  2
                     */
                    else
                    {
                        pResult->fNegative = !pMinuend->fNegative;
                        rc = rtBigNumMagnitudeSub(pResult, pSubtrahend, pMinuend);
                    }
                    rtBigNumScramble((PRTBIGNUM)pSubtrahend);
                }
                rtBigNumScramble((PRTBIGNUM)pMinuend);
            }
        }
        else
        {
            /* zero. */
            pResult->fNegative = 0;
            rtBigNumSetUsed(pResult, 0);
        }
        rtBigNumScramble(pResult);
    }
    return rc;
}


RTDECL(int) RTBigNumNegateThis(PRTBIGNUM pThis)
{
    pThis->fNegative = !pThis->fNegative;
    return VINF_SUCCESS;
}


RTDECL(int) RTBigNumNegate(PRTBIGNUM pResult, PCRTBIGNUM pBigNum)
{
    int rc = RTBigNumAssign(pResult, pBigNum);
    if (RT_SUCCESS(rc))
        rc = RTBigNumNegateThis(pResult);
    return rc;
}


/**
 * Multiplies the magnitudes of two values, letting the caller care about the
 * sign bit.
 *
 * @returns IPRT status code.
 * @param   pResult         Where to store the result.
 * @param   pMultiplicand   The first value.
 * @param   pMultiplier     The second value.
 */
static int rtBigNumMagnitudeMultiply(PRTBIGNUM pResult, PCRTBIGNUM pMultiplicand, PCRTBIGNUM pMultiplier)
{
    Assert(pResult != pMultiplicand); Assert(pResult != pMultiplier);
    Assert(!pResult->fCurScrambled); Assert(!pMultiplicand->fCurScrambled); Assert(!pMultiplier->fCurScrambled);

    /*
     * Multiplication involving zero is zero.
     */
    if (!pMultiplicand->cUsed || !pMultiplier->cUsed)
    {
        pResult->fNegative = 0;
        rtBigNumSetUsed(pResult, 0);
        return VINF_SUCCESS;
    }

    /*
     * Allocate a result array that is the sum of the two factors, initialize
     * it to zero.
     */
    uint32_t cMax = pMultiplicand->cUsed + pMultiplier->cUsed;
    int rc = rtBigNumSetUsed(pResult, cMax);
    if (RT_SUCCESS(rc))
    {
        RT_BZERO(pResult->pauElements, pResult->cUsed * RTBIGNUM_ELEMENT_SIZE);

#ifdef IPRT_BIGINT_WITH_ASM
        rtBigNumMagnitudeMultiplyAssemblyWorker(pResult->pauElements,
                                                pMultiplier->pauElements, pMultiplier->cUsed,
                                                pMultiplicand->pauElements, pMultiplicand->cUsed);
#else
        for (uint32_t i = 0; i < pMultiplier->cUsed; i++)
        {
            RTBIGNUMELEMENT uMultiplier = pMultiplier->pauElements[i];
            for (uint32_t j = 0; j < pMultiplicand->cUsed; j++)
            {
                RTBIGNUMELEMENT uHi;
                RTBIGNUMELEMENT uLo;
#if RTBIGNUM_ELEMENT_SIZE == 4
                uint64_t u64 = ASMMult2xU32RetU64(pMultiplicand->pauElements[j], uMultiplier);
                uLo = (uint32_t)u64;
                uHi = u64 >> 32;
#elif RTBIGNUM_ELEMENT_SIZE == 8
                uLo = ASMMult2xU64Ret2xU64(pMultiplicand->pauElements[j], uMultiplier, &uHi);
#else
# error "Invalid RTBIGNUM_ELEMENT_SIZE value"
#endif
                RTBIGNUMELEMENT fCarry = 0;
                uint64_t k = i + j;
                pResult->pauElements[k] = rtBigNumElementAddWithCarry(pResult->pauElements[k], uLo, &fCarry);
                k++;
                pResult->pauElements[k] = rtBigNumElementAddWithCarry(pResult->pauElements[k], uHi, &fCarry);
                while (fCarry)
                {
                    k++;
                    pResult->pauElements[k] = rtBigNumElementAddWithCarry(pResult->pauElements[k], 0, &fCarry);
                }
                Assert(k < cMax);
            }
        }
#endif

        /* It's possible we overestimated the output size by 1 element. */
        rtBigNumStripTrailingZeros(pResult);
    }
    return rc;
}


RTDECL(int) RTBigNumMultiply(PRTBIGNUM pResult, PCRTBIGNUM pMultiplicand, PCRTBIGNUM pMultiplier)
{
    Assert(pResult != pMultiplicand); Assert(pResult != pMultiplier);
    AssertReturn(pResult->fSensitive >= (pMultiplicand->fSensitive | pMultiplier->fSensitive), VERR_BIGNUM_SENSITIVE_INPUT);

    int rc = rtBigNumUnscramble(pResult);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pResult);
        rc = rtBigNumUnscramble((PRTBIGNUM)pMultiplicand);
        if (RT_SUCCESS(rc))
        {
            RTBIGNUM_ASSERT_VALID(pMultiplicand);
            rc = rtBigNumUnscramble((PRTBIGNUM)pMultiplier);
            if (RT_SUCCESS(rc))
            {
                RTBIGNUM_ASSERT_VALID(pMultiplier);

                /*
                 * The sign values follow XOR rules:
                 *       -1 *  1 = -1;      1 ^ 0 = 1
                 *        1 * -1 = -1;      1 ^ 0 = 1
                 *       -1 * -1 =  1;      1 ^ 1 = 0
                 *        1 *  1 =  1;      0 ^ 0 = 0
                 */
                pResult->fNegative = pMultiplicand->fNegative ^ pMultiplier->fNegative;
                rc = rtBigNumMagnitudeMultiply(pResult, pMultiplicand, pMultiplier);

                rtBigNumScramble((PRTBIGNUM)pMultiplier);
            }
            rtBigNumScramble((PRTBIGNUM)pMultiplicand);
        }
        rtBigNumScramble(pResult);
    }
    return rc;
}


#if 0 /* unused */
/**
 * Clears a bit in the magnitude of @a pBigNum.
 *
 * The variables must be unscrambled.
 *
 * @param   pBigNum         The big number.
 * @param   iBit            The bit to clear (0-based).
 */
DECLINLINE(void) rtBigNumMagnitudeClearBit(PRTBIGNUM pBigNum, uint32_t iBit)
{
    uint32_t iElement = iBit / RTBIGNUM_ELEMENT_BITS;
    if (iElement < pBigNum->cUsed)
    {
        iBit &= RTBIGNUM_ELEMENT_BITS - 1;
        pBigNum->pauElements[iElement] &= ~RTBIGNUM_ELEMENT_BIT(iBit);
        if (iElement + 1 == pBigNum->cUsed && !pBigNum->pauElements[iElement])
            rtBigNumStripTrailingZeros(pBigNum);
    }
}
#endif /* unused */


/**
 * Sets a bit in the magnitude of @a pBigNum.
 *
 * The variables must be unscrambled.
 *
 * @returns IPRT status code.
 * @param   pBigNum         The big number.
 * @param   iBit            The bit to clear (0-based).
 */
DECLINLINE(int) rtBigNumMagnitudeSetBit(PRTBIGNUM pBigNum, uint32_t iBit)
{
    uint32_t iElement = iBit / RTBIGNUM_ELEMENT_BITS;
    int rc = rtBigNumEnsureElementPresent(pBigNum, iElement);
    if (RT_SUCCESS(rc))
    {
        iBit &= RTBIGNUM_ELEMENT_BITS - 1;
        pBigNum->pauElements[iElement] |= RTBIGNUM_ELEMENT_BIT(iBit);
        return VINF_SUCCESS;
    }
    return rc;
}


#if 0 /* unused */
/**
 * Writes a bit in the magnitude of @a pBigNum.
 *
 * The variables must be unscrambled.
 *
 * @returns IPRT status code.
 * @param   pBigNum         The big number.
 * @param   iBit            The bit to write (0-based).
 * @param   fValue          The bit value.
 */
DECLINLINE(int) rtBigNumMagnitudeWriteBit(PRTBIGNUM pBigNum, uint32_t iBit, bool fValue)
{
    if (fValue)
        return rtBigNumMagnitudeSetBit(pBigNum, iBit);
    rtBigNumMagnitudeClearBit(pBigNum, iBit);
    return VINF_SUCCESS;
}
#endif


/**
 * Returns the given magnitude bit.
 *
 * The variables must be unscrambled.
 *
 * @returns The bit value (1 or 0).
 * @param   pBigNum         The big number.
 * @param   iBit            The bit to return (0-based).
 */
DECLINLINE(RTBIGNUMELEMENT) rtBigNumMagnitudeGetBit(PCRTBIGNUM pBigNum, uint32_t iBit)
{
    uint32_t iElement = iBit / RTBIGNUM_ELEMENT_BITS;
    if (iElement < pBigNum->cUsed)
    {
        iBit &= RTBIGNUM_ELEMENT_BITS - 1;
        return (pBigNum->pauElements[iElement] >> iBit) & 1;
    }
    return 0;
}


/**
 * Shifts the magnitude left by one.
 *
 * The variables must be unscrambled.
 *
 * @returns IPRT status code.
 * @param   pBigNum         The big number.
 * @param   uCarry          The value to shift in at the bottom.
 */
DECLINLINE(int) rtBigNumMagnitudeShiftLeftOne(PRTBIGNUM pBigNum, RTBIGNUMELEMENT uCarry)
{
    Assert(uCarry <= 1);

    /* Do the shifting. */
    uint32_t cUsed = pBigNum->cUsed;
#ifdef IPRT_BIGINT_WITH_ASM
    uCarry = rtBigNumMagnitudeShiftLeftOneAssemblyWorker(pBigNum->pauElements, cUsed, uCarry);
#else
    for (uint32_t i = 0; i < cUsed; i++)
    {
        RTBIGNUMELEMENT uTmp = pBigNum->pauElements[i];
        pBigNum->pauElements[i] = (uTmp << 1) | uCarry;
        uCarry = uTmp >> (RTBIGNUM_ELEMENT_BITS - 1);
    }
#endif

    /* If we still carry a bit, we need to increase the size. */
    if (uCarry)
    {
        int rc = rtBigNumSetUsed(pBigNum, cUsed + 1);
        AssertRCReturn(rc, rc);
        pBigNum->pauElements[cUsed] = uCarry;
    }

    return VINF_SUCCESS;
}


/**
 * Shifts the magnitude left by @a cBits.
 *
 * The variables must be unscrambled.
 *
 * @returns IPRT status code.
 * @param   pResult         Where to store the result.
 * @param   pValue          The value to shift.
 * @param   cBits           The shift count.
 */
static int rtBigNumMagnitudeShiftLeft(PRTBIGNUM pResult, PCRTBIGNUM pValue, uint32_t cBits)
{
    int rc;
    if (cBits)
    {
        uint32_t cBitsNew = rtBigNumMagnitudeBitWidth(pValue);
        if (cBitsNew > 0)
        {
            if (cBitsNew + cBits > cBitsNew)
            {
                cBitsNew += cBits;
                rc = rtBigNumSetUsedEx(pResult, 0, RT_ALIGN_32(cBitsNew, RTBIGNUM_ELEMENT_BITS) / RTBIGNUM_ELEMENT_BITS);
                if (RT_SUCCESS(rc))
                    rc = rtBigNumSetUsed(pResult, RT_ALIGN_32(cBitsNew, RTBIGNUM_ELEMENT_BITS) / RTBIGNUM_ELEMENT_BITS);
                if (RT_SUCCESS(rc))
                {
                    uint32_t const      cLeft  = pValue->cUsed;
                    PCRTBIGNUMELEMENT   pauSrc = pValue->pauElements;
                    PRTBIGNUMELEMENT    pauDst = pResult->pauElements;

                    Assert(ASMMemIsZero(pauDst, (cBits / RTBIGNUM_ELEMENT_BITS) * RTBIGNUM_ELEMENT_SIZE));
                    pauDst += cBits / RTBIGNUM_ELEMENT_BITS;

                    cBits &= RTBIGNUM_ELEMENT_BITS - 1;
                    if (cBits)
                    {
                        RTBIGNUMELEMENT uPrev = 0;
                        for (uint32_t i = 0; i < cLeft; i++)
                        {
                            RTBIGNUMELEMENT uCur = pauSrc[i];
                            pauDst[i] = (uCur << cBits) | (uPrev >> (RTBIGNUM_ELEMENT_BITS - cBits));
                            uPrev = uCur;
                        }
                        uPrev >>= RTBIGNUM_ELEMENT_BITS - cBits;
                        if (uPrev)
                            pauDst[pValue->cUsed] = uPrev;
                    }
                    else
                        memcpy(pauDst, pauSrc, cLeft * RTBIGNUM_ELEMENT_SIZE);
                }
            }
            else
                rc = VERR_OUT_OF_RANGE;
        }
        /* Shifting zero always yields a zero result. */
        else
            rc = rtBigNumSetUsed(pResult, 0);
    }
    else
        rc = rtBigNumMagnitudeCopy(pResult, pValue);
    return rc;
}


RTDECL(int) RTBigNumShiftLeft(PRTBIGNUM pResult, PCRTBIGNUM pValue, uint32_t cBits)
{
    Assert(pResult != pValue);
    AssertReturn(pResult->fSensitive >= pValue->fSensitive, VERR_BIGNUM_SENSITIVE_INPUT);

    int rc = rtBigNumUnscramble(pResult);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pResult);
        rc = rtBigNumUnscramble((PRTBIGNUM)pValue);
        if (RT_SUCCESS(rc))
        {
            RTBIGNUM_ASSERT_VALID(pValue);

            pResult->fNegative = pValue->fNegative;
            rc = rtBigNumMagnitudeShiftLeft(pResult, pValue, cBits);

            rtBigNumScramble((PRTBIGNUM)pValue);
        }
        rtBigNumScramble(pResult);
    }
    return rc;
}


/**
 * Shifts the magnitude right by @a cBits.
 *
 * The variables must be unscrambled.
 *
 * @returns IPRT status code.
 * @param   pResult         Where to store the result.
 * @param   pValue          The value to shift.
 * @param   cBits           The shift count.
 */
static int rtBigNumMagnitudeShiftRight(PRTBIGNUM pResult, PCRTBIGNUM pValue, uint32_t cBits)
{
    int rc;
    if (cBits)
    {
        uint32_t cBitsNew = rtBigNumMagnitudeBitWidth(pValue);
        if (cBitsNew > cBits)
        {
            cBitsNew -= cBits;
            uint32_t cElementsNew = RT_ALIGN_32(cBitsNew, RTBIGNUM_ELEMENT_BITS) / RTBIGNUM_ELEMENT_BITS;
            rc = rtBigNumSetUsed(pResult, cElementsNew);
            if (RT_SUCCESS(rc))
            {
                uint32_t            i      = cElementsNew;
                PCRTBIGNUMELEMENT   pauSrc = pValue->pauElements;
                PRTBIGNUMELEMENT    pauDst = pResult->pauElements;

                pauSrc += cBits / RTBIGNUM_ELEMENT_BITS;

                cBits &= RTBIGNUM_ELEMENT_BITS - 1;
                if (cBits)
                {
                    RTBIGNUMELEMENT uPrev = &pauSrc[i] == &pValue->pauElements[pValue->cUsed] ? 0 : pauSrc[i];
                    while (i-- > 0)
                    {
                        RTBIGNUMELEMENT uCur = pauSrc[i];
                        pauDst[i] = (uCur >> cBits) | (uPrev << (RTBIGNUM_ELEMENT_BITS - cBits));
                        uPrev = uCur;
                    }
                }
                else
                    memcpy(pauDst, pauSrc, i * RTBIGNUM_ELEMENT_SIZE);
            }
        }
        else
            rc = rtBigNumSetUsed(pResult, 0);
    }
    else
        rc = rtBigNumMagnitudeCopy(pResult, pValue);
    return rc;
}


RTDECL(int) RTBigNumShiftRight(PRTBIGNUM pResult, PCRTBIGNUM pValue, uint32_t cBits)
{
    Assert(pResult != pValue);
    AssertReturn(pResult->fSensitive >= pValue->fSensitive, VERR_BIGNUM_SENSITIVE_INPUT);

    int rc = rtBigNumUnscramble(pResult);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pResult);
        rc = rtBigNumUnscramble((PRTBIGNUM)pValue);
        if (RT_SUCCESS(rc))
        {
            RTBIGNUM_ASSERT_VALID(pValue);

            pResult->fNegative = pValue->fNegative;
            rc = rtBigNumMagnitudeShiftRight(pResult, pValue, cBits);
            if (!pResult->cUsed)
                pResult->fNegative = 0;

            rtBigNumScramble((PRTBIGNUM)pValue);
        }
        rtBigNumScramble(pResult);
    }
    return rc;
}


/**
 * Implements the D3 test for Qhat decrementation.
 *
 * @returns True if Qhat should be decremented.
 * @param   puQhat              Pointer to Qhat.
 * @param   uRhat               The remainder.
 * @param   uDivisorY           The penultimate divisor element.
 * @param   uDividendJMinus2    The j-2 dividend element.
 */
DECLINLINE(bool) rtBigNumKnuthD3_ShouldDecrementQhat(RTBIGNUMELEMENT2X const *puQhat, RTBIGNUMELEMENT uRhat,
                                                     RTBIGNUMELEMENT uDivisorY, RTBIGNUMELEMENT uDividendJMinus2)
{
    if (puQhat->s.Lo == RTBIGNUM_ELEMENT_MAX && puQhat->s.Hi == 0)
        return true;
#if RTBIGNUM_ELEMENT_BITS == 64
    RTBIGNUMELEMENT2X TmpLeft;
    RTUInt128MulByU64(&TmpLeft, puQhat, uDivisorY);

    RTBIGNUMELEMENT2X TmpRight;
    TmpRight.s.Lo = 0;
    TmpRight.s.Hi = uRhat;
    RTUInt128AssignAddU64(&TmpRight, uDividendJMinus2);

    if (RTUInt128Compare(&TmpLeft, &TmpRight) > 0)
        return true;
#else
    if (puQhat->u * uDivisorY > ((uint64_t)uRhat << 32) + uDividendJMinus2)
        return true;
#endif
    return false;
}


/**
 * C implementation of the D3 step of Knuth's division algorithm.
 *
 * This estimates a value Qhat that will be used as quotient "digit" (element)
 * at the current level of the division (j).
 *
 * @returns The Qhat value we've estimated.
 * @param   pauDividendJN   Pointer to the j+n (normalized) dividend element.
 *                          Will access up to two elements prior to this.
 * @param   uDivZ           The last element in the (normalized) divisor.
 * @param   uDivY           The penultimate element in the (normalized) divisor.
 */
DECLINLINE(RTBIGNUMELEMENT) rtBigNumKnuthD3_EstimateQhat(PCRTBIGNUMELEMENT pauDividendJN,
                                                         RTBIGNUMELEMENT uDivZ, RTBIGNUMELEMENT uDivY)
{
    RTBIGNUMELEMENT2X   uQhat;
    RTBIGNUMELEMENT     uRhat;
    RTBIGNUMELEMENT     uDividendJN = pauDividendJN[0];
    Assert(uDividendJN <= uDivZ);
    if (uDividendJN != uDivZ)
        rtBigNumElement2xDiv2xBy1x(&uQhat, &uRhat, uDividendJN, pauDividendJN[-1], uDivZ);
    else
    {
        /*
         * This is the case where we end up with an initial Qhat that's all Fs.
         */
        /* Calc the remainder for max Qhat value. */
        RTBIGNUMELEMENT2X uTmp1;        /* (v[j+n] << bits) + v[J+N-1]  */
        uTmp1.s.Hi = uDivZ;
        uTmp1.s.Lo = pauDividendJN[-1];

        RTBIGNUMELEMENT2X uTmp2;        /* uQhat * uDividendJN */
        uTmp2.s.Hi = uDivZ - 1;
        uTmp2.s.Lo = 0 - uDivZ;
#if RTBIGNUM_ELEMENT_BITS == 64
        RTUInt128AssignSub(&uTmp1, &uTmp2);
#else
        uTmp1.u -= uTmp2.u;
#endif
        /* If we overflowed the remainder, don't bother trying to adjust. */
        if (uTmp1.s.Hi)
            return RTBIGNUM_ELEMENT_MAX;

        uRhat = uTmp1.s.Lo;
        uQhat.s.Lo = RTBIGNUM_ELEMENT_MAX;
        uQhat.s.Hi = 0;
    }

    /*
     * Adjust Q to eliminate all cases where it's two to large and most cases
     * where it's one too large.
     */
    while (rtBigNumKnuthD3_ShouldDecrementQhat(&uQhat, uRhat, uDivY, pauDividendJN[-2]))
    {
        rtBigNumElement2xDec(&uQhat);
        uRhat += uDivZ;
        if (uRhat < uDivZ /* overflow */ || uRhat == RTBIGNUM_ELEMENT_MAX)
            break;
    }

    return uQhat.s.Lo;
}


#ifdef IPRT_BIGINT_WITH_ASM
DECLASM(bool) rtBigNumKnuthD4_MulSub(PRTBIGNUMELEMENT pauDividendJ, PRTBIGNUMELEMENT pauDivisor,
                                     uint32_t cDivisor, RTBIGNUMELEMENT uQhat);
#else
/**
 * C implementation of the D4 step of Knuth's division algorithm.
 *
 * This subtracts Divisor * Qhat from the dividend at the current J index.
 *
 * @returns true if negative result (unlikely), false if positive.
 * @param   pauDividendJ    Pointer to the j-th (normalized) dividend element.
 *                          Will access up to two elements prior to this.
 * @param   uDivZ           The last element in the (normalized) divisor.
 * @param   uDivY           The penultimate element in the (normalized) divisor.
 */
DECLINLINE(bool) rtBigNumKnuthD4_MulSub(PRTBIGNUMELEMENT pauDividendJ, PRTBIGNUMELEMENT pauDivisor,
                                        uint32_t cDivisor, RTBIGNUMELEMENT uQhat)
{
    uint32_t        i;
    bool            fBorrow   = false;
    RTBIGNUMELEMENT uMulCarry = 0;
    for (i = 0; i < cDivisor; i++)
    {
        RTBIGNUMELEMENT2X uSub;
# if RTBIGNUM_ELEMENT_BITS == 64
        RTUInt128MulU64ByU64(&uSub, uQhat, pauDivisor[i]);
        RTUInt128AssignAddU64(&uSub, uMulCarry);
# else
        uSub.u = (uint64_t)uQhat * pauDivisor[i] + uMulCarry;
# endif
        uMulCarry = uSub.s.Hi;

        RTBIGNUMELEMENT uDividendI = pauDividendJ[i];
        if (!fBorrow)
        {
            fBorrow = uDividendI < uSub.s.Lo;
            uDividendI -= uSub.s.Lo;
        }
        else
        {
            fBorrow = uDividendI <= uSub.s.Lo;
            uDividendI -= uSub.s.Lo + 1;
        }
        pauDividendJ[i] = uDividendI;
    }

    /* Carry and borrow into the final dividend element. */
    RTBIGNUMELEMENT uDividendI = pauDividendJ[i];
    if (!fBorrow)
    {
        fBorrow = uDividendI < uMulCarry;
        pauDividendJ[i] = uDividendI - uMulCarry;
    }
    else
    {
        fBorrow = uDividendI <= uMulCarry;
        pauDividendJ[i] = uDividendI - uMulCarry - 1;
    }

    return fBorrow;
}
#endif /* !IPRT_BIGINT_WITH_ASM */


/**
 * C implementation of the D6 step of Knuth's division algorithm.
 *
 * This adds the divisor to the dividend to undo the negative value step D4
 * produced.  This is not very frequent occurence.
 *
 * @param   pauDividendJ    Pointer to the j-th (normalized) dividend element.
 *                          Will access up to two elements prior to this.
 * @param   pauDivisor      The last element in the (normalized) divisor.
 * @param   cDivisor        The penultimate element in the (normalized) divisor.
 */
DECLINLINE(void) rtBigNumKnuthD6_AddBack(PRTBIGNUMELEMENT pauDividendJ, PRTBIGNUMELEMENT pauDivisor, uint32_t cDivisor)
{
    RTBIGNUMELEMENT2X uTmp;
    uTmp.s.Lo = 0;

    uint32_t i;
    for (i = 0; i < cDivisor; i++)
    {
        uTmp.s.Hi = 0;
#if RTBIGNUM_ELEMENT_BITS == 64
        RTUInt128AssignAddU64(&uTmp, pauDivisor[i]);
        RTUInt128AssignAddU64(&uTmp, pauDividendJ[i]);
#else
        uTmp.u += pauDivisor[i];
        uTmp.u += pauDividendJ[i];
#endif
        pauDividendJ[i] = uTmp.s.Lo;
        uTmp.s.Lo = uTmp.s.Hi;
    }

    /* The final dividend entry. */
    Assert(pauDividendJ[i] + uTmp.s.Lo < uTmp.s.Lo);
    pauDividendJ[i] += uTmp.s.Lo;
}


/**
 * Knuth's division (core).
 *
 * @returns IPRT status code.
 * @param   pQuotient       Where to return the quotient.  Can be NULL.
 * @param   pRemainder      Where to return the remainder.
 * @param   pDividend       What to divide.
 * @param   pDivisor        What to divide by.
 */
static int rtBigNumMagnitudeDivideKnuth(PRTBIGNUM pQuotient, PRTBIGNUM pRemainder, PCRTBIGNUM pDividend, PCRTBIGNUM pDivisor)
{
    Assert(pDivisor->cUsed > 1);
    uint32_t const cDivisor = pDivisor->cUsed;
    Assert(pDividend->cUsed >= cDivisor);

    /*
     * Make sure we've got enough space in the quotient, so we can build it
     * without any trouble come step D5.
     */
    int rc;
    if (pQuotient)
    {
        rc = rtBigNumSetUsedEx(pQuotient, 0, pDividend->cUsed - cDivisor + 1);
        if (RT_SUCCESS(rc))
            rc = rtBigNumSetUsed(pQuotient, pDividend->cUsed - cDivisor + 1);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * D1. Normalize.  The goal here is to make sure the last element in the
     * divisor is greater than RTBIGNUMELEMENTS_MAX/2.  We must also make sure
     * we can access element pDividend->cUsed of the normalized dividend.
     */
    RTBIGNUM    NormDividend;
    RTBIGNUM    NormDivisor;
    PCRTBIGNUM  pNormDivisor = &NormDivisor;
    rtBigNumInitZeroTemplate(&NormDivisor, pDividend);

    uint32_t cNormShift = (RTBIGNUM_ELEMENT_BITS - rtBigNumMagnitudeBitWidth(pDivisor)) & (RTBIGNUM_ELEMENT_BITS - 1);
    if (cNormShift)
    {
        rtBigNumInitZeroTemplate(&NormDividend, pDividend);
        rc = rtBigNumMagnitudeShiftLeft(&NormDividend, pDividend, cNormShift);
        if (RT_SUCCESS(rc))
            rc = rtBigNumMagnitudeShiftLeft(&NormDivisor, pDivisor, cNormShift);
    }
    else
    {
        pNormDivisor = pDivisor;
        rc = rtBigNumCloneInternal(&NormDividend, pDividend);
    }
    if (RT_SUCCESS(rc) && pDividend->cUsed == NormDividend.cUsed)
        rc = rtBigNumEnsureExtraZeroElements(&NormDividend, NormDividend.cUsed + 1);
    if (RT_SUCCESS(rc))
    {
        /*
         * D2. Initialize the j index so we can loop thru the elements in the
         *     dividend that makes it larger than the divisor.
         */
        uint32_t j = pDividend->cUsed - cDivisor;

        RTBIGNUMELEMENT const DivZ = pNormDivisor->pauElements[cDivisor - 1];
        RTBIGNUMELEMENT const DivY = pNormDivisor->pauElements[cDivisor - 2];
        for (;;)
        {
            /*
             * D3. Estimate a Q' by dividing the j and j-1 dividen elements by
             * the last divisor element, then adjust against the next elements.
             */
            RTBIGNUMELEMENT uQhat = rtBigNumKnuthD3_EstimateQhat(&NormDividend.pauElements[j + cDivisor], DivZ, DivY);

            /*
             * D4. Multiply and subtract.
             */
            bool fNegative = rtBigNumKnuthD4_MulSub(&NormDividend.pauElements[j], pNormDivisor->pauElements, cDivisor, uQhat);

            /*
             * D5. Test remainder.
             * D6. Add back.
             */
            if (fNegative)
            {
//__debugbreak();
                rtBigNumKnuthD6_AddBack(&NormDividend.pauElements[j], pNormDivisor->pauElements, cDivisor);
                uQhat--;
            }

            if (pQuotient)
                pQuotient->pauElements[j] = uQhat;

            /*
             * D7. Loop on j.
             */
            if (j == 0)
                break;
            j--;
        }

        /*
         * D8. Unnormalize the remainder.
         */
        rtBigNumStripTrailingZeros(&NormDividend);
        if (cNormShift)
            rc = rtBigNumMagnitudeShiftRight(pRemainder, &NormDividend, cNormShift);
        else
            rc = rtBigNumMagnitudeCopy(pRemainder, &NormDividend);
        if (pQuotient)
            rtBigNumStripTrailingZeros(pQuotient);
    }

    /*
     * Delete temporary variables.
     */
    RTBigNumDestroy(&NormDividend);
    if (pNormDivisor == &NormDivisor)
        RTBigNumDestroy(&NormDivisor);
    return rc;
}


static int rtBigNumMagnitudeDivideSlowLong(PRTBIGNUM pQuotient, PRTBIGNUM pRemainder, PCRTBIGNUM pDividend, PCRTBIGNUM pDivisor)
{
    /*
     * Do very simple long division.  This ain't fast, but it does the trick.
     */
    int rc = VINF_SUCCESS;
    uint32_t iBit = rtBigNumMagnitudeBitWidth(pDividend);
    while (iBit-- > 0)
    {
        rc = rtBigNumMagnitudeShiftLeftOne(pRemainder, rtBigNumMagnitudeGetBit(pDividend, iBit));
        AssertRCBreak(rc);
        int iDiff = rtBigNumMagnitudeCompare(pRemainder, pDivisor);
        if (iDiff >= 0)
        {
            if (iDiff != 0)
            {
                rc = rtBigNumMagnitudeSubThis(pRemainder, pDivisor);
                AssertRCBreak(rc);
            }
            else
                rtBigNumSetUsed(pRemainder, 0);
            rc = rtBigNumMagnitudeSetBit(pQuotient, iBit);
            AssertRCBreak(rc);
        }
    }

    /* This shouldn't be necessary. */
    rtBigNumStripTrailingZeros(pQuotient);
    rtBigNumStripTrailingZeros(pRemainder);

    return rc;
}


/**
 * Divides the magnitudes of two values, letting the caller care about the sign
 * bit.
 *
 * All variables must be unscrambled.  The sign flag is not considered nor
 * touched, this means the caller have to check for zero outputs.
 *
 * @returns IPRT status code.
 * @param   pQuotient       Where to return the quotient.
 * @param   pRemainder      Where to return the remainder.
 * @param   pDividend       What to divide.
 * @param   pDivisor        What to divide by.
 * @param   fForceLong      Force long division.
 */
static int rtBigNumMagnitudeDivide(PRTBIGNUM pQuotient, PRTBIGNUM pRemainder, PCRTBIGNUM pDividend, PCRTBIGNUM pDivisor,
                                   bool fForceLong)
{
    Assert(pQuotient != pDividend); Assert(pQuotient != pDivisor); Assert(pRemainder != pDividend); Assert(pRemainder != pDivisor); Assert(pRemainder != pQuotient);
    Assert(!pQuotient->fCurScrambled); Assert(!pRemainder->fCurScrambled); Assert(!pDividend->fCurScrambled); Assert(!pDivisor->fCurScrambled);

    /*
     * Just set both output values to zero as that's the return for several
     * special case and the initial state of the general case.
     */
    rtBigNumSetUsed(pQuotient, 0);
    rtBigNumSetUsed(pRemainder, 0);

    /*
     * Dividing something by zero is undefined.
     * Diving zero by something is zero, unless the divsor is also zero.
     */
    if (!pDivisor->cUsed || !pDividend->cUsed)
        return pDivisor->cUsed ? VINF_SUCCESS : VERR_BIGNUM_DIV_BY_ZERO;

    /*
     * Dividing by one? Quotient = dividend, no remainder.
     */
    if (pDivisor->cUsed == 1 && pDivisor->pauElements[0] == 1)
        return rtBigNumMagnitudeCopy(pQuotient, pDividend);

    /*
     * Dividend smaller than the divisor. Zero quotient, all divisor.
     */
    int iDiff = rtBigNumMagnitudeCompare(pDividend, pDivisor);
    if (iDiff < 0)
        return rtBigNumMagnitudeCopy(pRemainder, pDividend);

    /*
     * Since we already have done the compare, check if the two values are the
     * same.  The result is 1 and no remainder then.
     */
    if (iDiff == 0)
    {
        int rc = rtBigNumSetUsed(pQuotient, 1);
        if (RT_SUCCESS(rc))
            pQuotient->pauElements[0] = 1;
        return rc;
    }

    /*
     * Sort out special cases before going to the preferred or select algorithm.
     */
    int rc;
    if (pDividend->cUsed <= 2 && !fForceLong)
    {
        if (pDividend->cUsed < 2)
        {
            /*
             * Single element division.
             */
            RTBIGNUMELEMENT uQ = pDividend->pauElements[0] / pDivisor->pauElements[0];
            RTBIGNUMELEMENT uR = pDividend->pauElements[0] % pDivisor->pauElements[0];
            rc = VINF_SUCCESS;
            if (uQ)
            {
                rc = rtBigNumSetUsed(pQuotient, 1);
                if (RT_SUCCESS(rc))
                    pQuotient->pauElements[0] = uQ;
            }
            if (uR && RT_SUCCESS(rc))
            {
                rc = rtBigNumSetUsed(pRemainder, 1);
                if (RT_SUCCESS(rc))
                    pRemainder->pauElements[0] = uR;
            }
        }
        else
        {
            /*
             * Two elements dividend by a one or two element divisor.
             */
            RTBIGNUMELEMENT2X uQ, uR;
            if (pDivisor->cUsed == 1)
            {
                rtBigNumElement2xDiv2xBy1x(&uQ, &uR.s.Lo, pDividend->pauElements[1], pDividend->pauElements[0],
                                           pDivisor->pauElements[0]);
                uR.s.Hi = 0;
            }
            else
                rtBigNumElement2xDiv(&uQ, &uR, pDividend->pauElements[1], pDividend->pauElements[0],
                                     pDivisor->pauElements[1], pDivisor->pauElements[0]);
            rc = rtBigNumElement2xCopyToMagnitude(&uQ, pQuotient);
            if (RT_SUCCESS(rc))
                rc = rtBigNumElement2xCopyToMagnitude(&uR, pRemainder);
        }
    }
    /*
     * Decide upon which algorithm to use.  Knuth requires a divisor that's at
     * least 2 elements big.
     */
    else if (pDivisor->cUsed < 2 || fForceLong)
        rc = rtBigNumMagnitudeDivideSlowLong(pQuotient, pRemainder, pDividend, pDivisor);
    else
        rc = rtBigNumMagnitudeDivideKnuth(pQuotient, pRemainder, pDividend, pDivisor);
    return rc;
}


static int rtBigNumDivideCommon(PRTBIGNUM pQuotient, PRTBIGNUM pRemainder,
                                PCRTBIGNUM pDividend, PCRTBIGNUM pDivisor, bool fForceLong)
{
    Assert(pQuotient != pDividend); Assert(pQuotient != pDivisor); Assert(pRemainder != pDividend); Assert(pRemainder != pDivisor); Assert(pRemainder != pQuotient);
    AssertReturn(pQuotient->fSensitive  >= (pDividend->fSensitive | pDivisor->fSensitive), VERR_BIGNUM_SENSITIVE_INPUT);
    AssertReturn(pRemainder->fSensitive >= (pDividend->fSensitive | pDivisor->fSensitive), VERR_BIGNUM_SENSITIVE_INPUT);

    int rc = rtBigNumUnscramble(pQuotient);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pQuotient);
        rc = rtBigNumUnscramble(pRemainder);
        if (RT_SUCCESS(rc))
        {
            RTBIGNUM_ASSERT_VALID(pRemainder);
            rc = rtBigNumUnscramble((PRTBIGNUM)pDividend);
            if (RT_SUCCESS(rc))
            {
                RTBIGNUM_ASSERT_VALID(pDividend);
                rc = rtBigNumUnscramble((PRTBIGNUM)pDivisor);
                if (RT_SUCCESS(rc))
                {
                    RTBIGNUM_ASSERT_VALID(pDivisor);

                    /*
                     * The sign value of the remainder is the same as the dividend.
                     * The sign values of the quotient follow XOR rules, just like multiplication:
                     *       -3 /  2 = -1; r=-1;    1 ^ 0 = 1
                     *        3 / -2 = -1; r= 1;    1 ^ 0 = 1
                     *       -3 / -2 =  1; r=-1;    1 ^ 1 = 0
                     *        3 /  2 =  1; r= 1;    0 ^ 0 = 0
                     */
                    pQuotient->fNegative  = pDividend->fNegative ^ pDivisor->fNegative;
                    pRemainder->fNegative = pDividend->fNegative;

                    rc = rtBigNumMagnitudeDivide(pQuotient, pRemainder, pDividend, pDivisor, fForceLong);

                    if (pQuotient->cUsed == 0)
                        pQuotient->fNegative = 0;
                    if (pRemainder->cUsed == 0)
                        pRemainder->fNegative = 0;

                    rtBigNumScramble((PRTBIGNUM)pDivisor);
                }
                rtBigNumScramble((PRTBIGNUM)pDividend);
            }
            rtBigNumScramble(pRemainder);
        }
        rtBigNumScramble(pQuotient);
    }
    return rc;
}


RTDECL(int) RTBigNumDivide(PRTBIGNUM pQuotient, PRTBIGNUM pRemainder, PCRTBIGNUM pDividend, PCRTBIGNUM pDivisor)
{
    return rtBigNumDivideCommon(pQuotient, pRemainder, pDividend, pDivisor, false /*fForceLong*/);
}


RTDECL(int) RTBigNumDivideLong(PRTBIGNUM pQuotient, PRTBIGNUM pRemainder, PCRTBIGNUM pDividend, PCRTBIGNUM pDivisor)
{
    return rtBigNumDivideCommon(pQuotient, pRemainder, pDividend, pDivisor, true /*fForceLong*/);
}


/**
 * Calculates the modulus of a magnitude value, leaving the sign bit to the
 * caller.
 *
 * All variables must be unscrambled.  The sign flag is not considered nor
 * touched, this means the caller have to check for zero outputs.
 *
 * @returns IPRT status code.
 * @param   pRemainder      Where to return the remainder.
 * @param   pDividend       What to divide.
 * @param   pDivisor        What to divide by.
 */
static int rtBigNumMagnitudeModulo(PRTBIGNUM pRemainder, PCRTBIGNUM pDividend, PCRTBIGNUM pDivisor)
{
    Assert(pRemainder != pDividend); Assert(pRemainder != pDivisor);
    Assert(!pRemainder->fCurScrambled); Assert(!pDividend->fCurScrambled); Assert(!pDivisor->fCurScrambled);

    /*
     * Just set the output value to zero as that's the return for several
     * special case and the initial state of the general case.
     */
    rtBigNumSetUsed(pRemainder, 0);

    /*
     * Dividing something by zero is undefined.
     * Diving zero by something is zero, unless the divsor is also zero.
     */
    if (!pDivisor->cUsed || !pDividend->cUsed)
        return pDivisor->cUsed ? VINF_SUCCESS : VERR_BIGNUM_DIV_BY_ZERO;

    /*
     * Dividing by one? Quotient = dividend, no remainder.
     */
    if (pDivisor->cUsed == 1 && pDivisor->pauElements[0] == 1)
        return VINF_SUCCESS;

    /*
     * Dividend smaller than the divisor. Zero quotient, all divisor.
     */
    int iDiff = rtBigNumMagnitudeCompare(pDividend, pDivisor);
    if (iDiff < 0)
        return rtBigNumMagnitudeCopy(pRemainder, pDividend);

    /*
     * Since we already have done the compare, check if the two values are the
     * same.  The result is 1 and no remainder then.
     */
    if (iDiff == 0)
        return VINF_SUCCESS;

    /** @todo optimize small numbers. */
    int rc = VINF_SUCCESS;
    if (pDivisor->cUsed < 2)
    {
        /*
         * Do very simple long division.  This ain't fast, but it does the trick.
         */
        uint32_t iBit = rtBigNumMagnitudeBitWidth(pDividend);
        while (iBit-- > 0)
        {
            rc = rtBigNumMagnitudeShiftLeftOne(pRemainder, rtBigNumMagnitudeGetBit(pDividend, iBit));
            AssertRCBreak(rc);
            iDiff = rtBigNumMagnitudeCompare(pRemainder, pDivisor);
            if (iDiff >= 0)
            {
                if (iDiff != 0)
                {
                    rc = rtBigNumMagnitudeSubThis(pRemainder, pDivisor);
                    AssertRCBreak(rc);
                }
                else
                    rtBigNumSetUsed(pRemainder, 0);
            }
        }
    }
    else
    {
        /*
         * Join paths with division.
         */
        rc = rtBigNumMagnitudeDivideKnuth(NULL, pRemainder, pDividend, pDivisor);
    }

    /* This shouldn't be necessary. */
    rtBigNumStripTrailingZeros(pRemainder);
    return rc;
}


RTDECL(int) RTBigNumModulo(PRTBIGNUM pRemainder, PCRTBIGNUM pDividend, PCRTBIGNUM pDivisor)
{
    Assert(pRemainder != pDividend); Assert(pRemainder != pDivisor);
    AssertReturn(pRemainder->fSensitive >= (pDividend->fSensitive | pDivisor->fSensitive), VERR_BIGNUM_SENSITIVE_INPUT);

    int rc = rtBigNumUnscramble(pRemainder);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pRemainder);
        rc = rtBigNumUnscramble((PRTBIGNUM)pDividend);
        if (RT_SUCCESS(rc))
        {
            RTBIGNUM_ASSERT_VALID(pDividend);
            rc = rtBigNumUnscramble((PRTBIGNUM)pDivisor);
            if (RT_SUCCESS(rc))
            {
                RTBIGNUM_ASSERT_VALID(pDivisor);

                /*
                 * The sign value of the remainder is the same as the dividend.
                 */
                pRemainder->fNegative = pDividend->fNegative;

                rc = rtBigNumMagnitudeModulo(pRemainder, pDividend, pDivisor);

                if (pRemainder->cUsed == 0)
                    pRemainder->fNegative = 0;

                rtBigNumScramble((PRTBIGNUM)pDivisor);
            }
            rtBigNumScramble((PRTBIGNUM)pDividend);
        }
        rtBigNumScramble(pRemainder);
    }
    return rc;
}



/**
 * Exponentiate the magnitude.
 *
 * All variables must be unscrambled.  The sign flag is not considered nor
 * touched, this means the caller have to reject negative exponents.
 *
 * @returns IPRT status code.
 * @param   pResult             Where to return power.
 * @param   pBase               The base value.
 * @param   pExponent           The exponent (assumed positive or zero).
 */
static int rtBigNumMagnitudeExponentiate(PRTBIGNUM pResult, PCRTBIGNUM pBase, PCRTBIGNUM pExponent)
{
    Assert(pResult != pBase); Assert(pResult != pExponent);
    Assert(!pResult->fCurScrambled); Assert(!pBase->fCurScrambled); Assert(!pExponent->fCurScrambled);

    /*
     * A couple of special cases.
     */
    int rc;
    /* base ^ 0 => 1. */
    if (pExponent->cUsed == 0)
    {
        rc = rtBigNumSetUsed(pResult, 1);
        if (RT_SUCCESS(rc))
            pResult->pauElements[0] = 1;
        return rc;
    }

    /* base ^ 1 => base. */
    if (pExponent->cUsed == 1 && pExponent->pauElements[0] == 1)
        return rtBigNumMagnitudeCopy(pResult, pBase);

    /*
     * Set up.
     */
    /* Init temporary power-of-two variable to base. */
    RTBIGNUM Pow2;
    rc = rtBigNumCloneInternal(&Pow2, pBase);
    if (RT_SUCCESS(rc))
    {
        /* Init result to 1. */
        rc = rtBigNumSetUsed(pResult, 1);
        if (RT_SUCCESS(rc))
        {
            pResult->pauElements[0] = 1;

            /* Make a temporary variable that we can use for temporary storage of the result. */
            RTBIGNUM TmpMultiplicand;
            rc = rtBigNumCloneInternal(&TmpMultiplicand, pResult);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Exponentiation by squaring.  Reduces the number of
                 * multiplications to: NumBitsSet(Exponent) + BitWidth(Exponent).
                 */
                uint32_t const  cExpBits = rtBigNumMagnitudeBitWidth(pExponent);
                uint32_t        iBit = 0;
                for (;;)
                {
                    if (rtBigNumMagnitudeGetBit(pExponent, iBit) != 0)
                    {
                        rc = rtBigNumMagnitudeCopy(&TmpMultiplicand, pResult);
                        if (RT_SUCCESS(rc))
                            rc = rtBigNumMagnitudeMultiply(pResult, &TmpMultiplicand, &Pow2);
                        if (RT_FAILURE(rc))
                            break;
                    }

                    /* Done? */
                    iBit++;
                    if (iBit >= cExpBits)
                        break;

                    /* Not done yet, square the base again. */
                    rc = rtBigNumMagnitudeCopy(&TmpMultiplicand, &Pow2);
                    if (RT_SUCCESS(rc))
                        rc = rtBigNumMagnitudeMultiply(&Pow2, &TmpMultiplicand, &TmpMultiplicand);
                    if (RT_FAILURE(rc))
                        break;
                }

                RTBigNumDestroy(&TmpMultiplicand);
            }
        }
        RTBigNumDestroy(&Pow2);
    }
    return rc;
}


RTDECL(int) RTBigNumExponentiate(PRTBIGNUM pResult, PCRTBIGNUM pBase, PCRTBIGNUM pExponent)
{
    Assert(pResult != pBase); Assert(pResult != pExponent);
    AssertReturn(pResult->fSensitive >= (pBase->fSensitive | pExponent->fSensitive), VERR_BIGNUM_SENSITIVE_INPUT);

    int rc = rtBigNumUnscramble(pResult);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pResult);
        rc = rtBigNumUnscramble((PRTBIGNUM)pBase);
        if (RT_SUCCESS(rc))
        {
            RTBIGNUM_ASSERT_VALID(pBase);
            rc = rtBigNumUnscramble((PRTBIGNUM)pExponent);
            if (RT_SUCCESS(rc))
            {
                RTBIGNUM_ASSERT_VALID(pExponent);
                if (!pExponent->fNegative)
                {
                    pResult->fNegative = pBase->fNegative; /* sign unchanged. */
                    rc = rtBigNumMagnitudeExponentiate(pResult, pBase, pExponent);
                }
                else
                    rc = VERR_BIGNUM_NEGATIVE_EXPONENT;

                rtBigNumScramble((PRTBIGNUM)pExponent);
            }
            rtBigNumScramble((PRTBIGNUM)pBase);
        }
        rtBigNumScramble(pResult);
    }
    return rc;
}


/**
 * Modular exponentiation, magnitudes only.
 *
 * All variables must be unscrambled.  The sign flag is not considered nor
 * touched, this means the caller have to reject negative exponents and do any
 * other necessary sign bit fiddling.
 *
 * @returns IPRT status code.
 * @param   pResult             Where to return the remainder of the power.
 * @param   pBase               The base value.
 * @param   pExponent           The exponent (assumed positive or zero).
 * @param   pModulus            The modulus value (or divisor if you like).
 */
static int rtBigNumMagnitudeModExp(PRTBIGNUM pResult, PRTBIGNUM pBase, PRTBIGNUM pExponent, PRTBIGNUM pModulus)
{
    Assert(pResult != pBase); Assert(pResult != pBase); Assert(pResult != pExponent); Assert(pResult != pModulus);
    Assert(!pResult->fCurScrambled); Assert(!pBase->fCurScrambled); Assert(!pExponent->fCurScrambled); Assert(!pModulus->fCurScrambled);
    int rc;

    /*
     * Check some special cases to get them out of the way.
     */
    /* Div by 0 => invalid. */
    if (pModulus->cUsed == 0)
        return VERR_BIGNUM_DIV_BY_ZERO;

    /* Div by 1 => no remainder. */
    if (pModulus->cUsed == 1 && pModulus->pauElements[0] == 1)
    {
        rtBigNumSetUsed(pResult, 0);
        return VINF_SUCCESS;
    }

    /* base ^ 0 => 1. */
    if (pExponent->cUsed == 0)
    {
        rc = rtBigNumSetUsed(pResult, 1);
        if (RT_SUCCESS(rc))
            pResult->pauElements[0] = 1;
        return rc;
    }

    /* base ^ 1 => base. */
    if (pExponent->cUsed == 1 && pExponent->pauElements[0] == 1)
        return rtBigNumMagnitudeModulo(pResult, pBase, pModulus);

    /*
     * Set up.
     */
    /* Result = 1; preallocate space for the result while at it. */
    rc = rtBigNumSetUsed(pResult, pModulus->cUsed + 1);
    if (RT_SUCCESS(rc))
        rc = rtBigNumSetUsed(pResult, 1);
    if (RT_SUCCESS(rc))
    {
        pResult->pauElements[0] = 1;

        /* ModBase = pBase or pBase % pModulus depending on the difference in size. */
        RTBIGNUM Pow2;
        if (pBase->cUsed <= pModulus->cUsed + pModulus->cUsed / 2)
            rc = rtBigNumCloneInternal(&Pow2, pBase);
        else
            rc = rtBigNumMagnitudeModulo(rtBigNumInitZeroTemplate(&Pow2, pBase), pBase, pModulus);

        /* Need a couple of temporary variables. */
        RTBIGNUM TmpMultiplicand;
        rtBigNumInitZeroTemplate(&TmpMultiplicand, pResult);

        RTBIGNUM TmpProduct;
        rtBigNumInitZeroTemplate(&TmpProduct, pResult);

        /*
         * We combine the exponentiation by squaring with the fact that:
         *      (a*b) mod n = ( (a mod n) * (b mod n) ) mod n
         *
         * Thus, we can reduce the size of intermediate results by mod'ing them
         * in each step.
         */
        uint32_t const  cExpBits = rtBigNumMagnitudeBitWidth(pExponent);
        uint32_t        iBit = 0;
        for (;;)
        {
            if (rtBigNumMagnitudeGetBit(pExponent, iBit) != 0)
            {
                rc = rtBigNumMagnitudeCopy(&TmpMultiplicand, pResult);
                if (RT_SUCCESS(rc))
                    rc = rtBigNumMagnitudeMultiply(&TmpProduct, &TmpMultiplicand, &Pow2);
                if (RT_SUCCESS(rc))
                    rc = rtBigNumMagnitudeModulo(pResult, &TmpProduct, pModulus);
                if (RT_FAILURE(rc))
                    break;
            }

            /* Done? */
            iBit++;
            if (iBit >= cExpBits)
                break;

            /* Not done yet, square and mod the base again. */
            rc = rtBigNumMagnitudeCopy(&TmpMultiplicand, &Pow2);
            if (RT_SUCCESS(rc))
                rc = rtBigNumMagnitudeMultiply(&TmpProduct, &TmpMultiplicand, &TmpMultiplicand);
            if (RT_SUCCESS(rc))
                rc = rtBigNumMagnitudeModulo(&Pow2, &TmpProduct, pModulus);
            if (RT_FAILURE(rc))
                break;
        }

        RTBigNumDestroy(&TmpMultiplicand);
        RTBigNumDestroy(&TmpProduct);
        RTBigNumDestroy(&Pow2);
    }
    return rc;
}


RTDECL(int) RTBigNumModExp(PRTBIGNUM pResult, PRTBIGNUM pBase, PRTBIGNUM pExponent, PRTBIGNUM pModulus)
{
    Assert(pResult != pBase); Assert(pResult != pBase); Assert(pResult != pExponent); Assert(pResult != pModulus);
    AssertReturn(pResult->fSensitive >= (pBase->fSensitive | pExponent->fSensitive | pModulus->fSensitive),
                 VERR_BIGNUM_SENSITIVE_INPUT);

    int rc = rtBigNumUnscramble(pResult);
    if (RT_SUCCESS(rc))
    {
        RTBIGNUM_ASSERT_VALID(pResult);
        rc = rtBigNumUnscramble((PRTBIGNUM)pBase);
        if (RT_SUCCESS(rc))
        {
            RTBIGNUM_ASSERT_VALID(pBase);
            rc = rtBigNumUnscramble((PRTBIGNUM)pExponent);
            if (RT_SUCCESS(rc))
            {
                RTBIGNUM_ASSERT_VALID(pExponent);
                rc = rtBigNumUnscramble((PRTBIGNUM)pModulus);
                if (RT_SUCCESS(rc))
                {
                    RTBIGNUM_ASSERT_VALID(pModulus);
                    if (!pExponent->fNegative)
                    {
                        pResult->fNegative = pModulus->fNegative; /* pBase ^ pExponent / pModulus; result = remainder. */
                        rc = rtBigNumMagnitudeModExp(pResult, pBase, pExponent, pModulus);
                    }
                    else
                        rc = VERR_BIGNUM_NEGATIVE_EXPONENT;
                    rtBigNumScramble((PRTBIGNUM)pModulus);
                }
                rtBigNumScramble((PRTBIGNUM)pExponent);
            }
            rtBigNumScramble((PRTBIGNUM)pBase);
        }
        rtBigNumScramble(pResult);
    }
    return rc;
}

