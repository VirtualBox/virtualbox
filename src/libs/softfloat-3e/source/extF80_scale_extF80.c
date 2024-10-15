/* $Id: extF80_scale_extF80.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SoftFloat - 387-style fscale.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "platform.h"
#include "internals.h"
#include "specialize.h"
#include "softfloat.h"
#include <iprt/assert.h>
#include <iprt/asm.h>


/**
 * Wrapper around softfloat_propagateNaNExtF80UI that returns extFloat80_t
 * and takes fully unpacked input.
 */
DECLINLINE(extFloat80_t)
softfloat_extF80_propagateNaN(bool fSignA, int_fast32_t iExpA, uint_fast64_t uSigA,
                              bool fSignB, int_fast32_t iExpB, uint_fast64_t uSigB SOFTFLOAT_STATE_DECL_COMMA)
{
    struct uint128 uiZ = softfloat_propagateNaNExtF80UI(packToExtF80UI64(fSignA, iExpA), uSigA,
                                                        packToExtF80UI64(fSignB, iExpB), uSigB SOFTFLOAT_STATE_ARG_COMMA);
    union extF80M_extF80 Ret;
    Ret.fM.signExp = uiZ.v64;
    Ret.fM.signif  = uiZ.v0;
    return Ret.f;
}


/**
 * This performs a function similar to extF80_to_i32_r_minMag, but returns
 * proper MIN/MAX values and no NaNs.
 *
 * ASSUMES the input is normalized.
 *
 * @returns Values in the range -2^24...+2^24.
 */
static int_fast32_t convertToInt(bool fSign, uint_fast64_t uSig, int_fast32_t iExp)
{
    iExp -= RTFLOAT80U_EXP_BIAS;
    if (iExp < 0)
        return 0;

    /* Restrict the range to -2^24...+2^24 to prevent overflows during scaling. */
    if (iExp >= 24)
        return fSign ? INT32_MIN / 128 : INT32_MAX / 128;

    int_fast32_t iRet = (int_fast32_t)(uSig >> (63 - iExp));
    if (fSign)
        return -iRet;
    return iRet;
}


/**
 * VBox: scale @a a by 2^truncateToInt(@a b)
 *
 * This function accepts and deals correctly with denormals (pseudo and
 * otherwise).
 */
extFloat80_t extF80_scale_extF80(extFloat80_t a, extFloat80_t b, softfloat_state_t *pState)
{
    static union extF80M_extF80 const s_extF80Indefinite   = EXTF80M_EXTF80_INIT(defaultNaNExtF80UI64, defaultNaNExtF80UI0);
    static union extF80M_extF80 const s_aExtF80Zero[2]     =
    {
        EXTF80M_EXTF80_INIT3_C(0, 0, 0), EXTF80M_EXTF80_INIT3_C(1, 0, 0),
    };
    static union extF80M_extF80 const s_aExtF80Infinity[2] =
    {
        EXTF80M_EXTF80_INIT3(0, RT_BIT_64(63), RTFLOAT80U_EXP_MAX),
        EXTF80M_EXTF80_INIT3(1, RT_BIT_64(63), RTFLOAT80U_EXP_MAX),
    };

    /*
     * Unpack the input.
     */
    bool const    fSignA = signExtF80UI64(a.signExp);
    int_fast32_t  iExpA  = expExtF80UI64(a.signExp);
    uint_fast64_t uSigA  = a.signif;

    bool const    fSignB = signExtF80UI64(b.signExp);
    int_fast32_t  iExpB  = expExtF80UI64(b.signExp);
    uint_fast64_t uSigB  = b.signif;

    /*
     * Deal with funny input.
     */
    /* Invalid first. We ASSUME subnormals are rejected here. */
    if (   RTFLOAT80U_IS_387_INVALID_EX(uSigA, iExpA)
        || RTFLOAT80U_IS_387_INVALID_EX(uSigB, iExpB))
    {
        softfloat_raiseFlags(softfloat_flag_invalid, pState);
        return s_extF80Indefinite.f;
    }

    /* Then NaNs and indefinites (special NaNs): */
    if (   RTFLOAT80U_IS_INDEFINITE_OR_QUIET_OR_SIGNALLING_NAN_EX(uSigA, iExpA)
        || RTFLOAT80U_IS_INDEFINITE_OR_QUIET_OR_SIGNALLING_NAN_EX(uSigB, iExpB))
        return softfloat_extF80_propagateNaN(fSignA, iExpA, uSigA, fSignB, iExpB, uSigB, pState);

    /* Normalize denormal inputs: */
    if (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL_EX(uSigA, iExpA))
    {
        softfloat_raiseFlags(softfloat_flag_denormal, pState);
        if (uSigA & RT_BIT_64(63))
            iExpA = 1; /* -16382 */
        else
        {
            /* We must return the denormal a value unchanged when b=zero, intel 10980XE
               does this at least. Where-as pseudo-denormals are normalized. Go figure. */
            if (RTFLOAT80U_IS_ZERO_EX(uSigB, iExpB))
                return a;
            iExpA = 64 - ASMBitLastSetU64(uSigA);
            uSigA <<= iExpA;
            iExpA = 1 - iExpA; /* -16382 - shift */
        }
    }

    if (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL_EX(uSigB, iExpB))
    {
        softfloat_raiseFlags(softfloat_flag_denormal, pState);
        if (uSigB & RT_BIT_64(63))
            iExpB = 1; /* -16382 */
        else
        {
            iExpB = 64 - ASMBitLastSetU64(uSigB);
            uSigB <<= iExpB;
            iExpB = 1 - iExpB; /* -16382 - shift */
        }
    }

    /* Infinities and zeros: If a is Zero or Infinity, return it as-is unless
       b=-Infinity & a=+/-Infinity or b=+Infinity & a=+/-zero when we have to
       raise #I and return indefinite instead.

       Note! If b is zero, don't, because pseudo-denormals should be returned
             normalized (intel does that at least). Excpetion is b=zero and
             a=denormal, which is handled above. */
    if (   RTFLOAT80U_IS_INF_EX(uSigA, iExpA)
        || RTFLOAT80U_IS_ZERO_EX(uSigA, iExpA))
    {
        if (RTFLOAT80U_IS_INF_EX(uSigB, iExpB) && fSignB == RTFLOAT80U_IS_INF_EX(uSigA, iExpA))
        {
            softfloat_raiseFlags(softfloat_flag_invalid, pState);
            return s_extF80Indefinite.f;
        }
        return a;
    }

    if (RTFLOAT80U_IS_INF_EX(uSigB, iExpB))
    {
        if (fSignB)
            return s_aExtF80Zero[fSignA].f;
        return s_aExtF80Infinity[fSignA].f;
    }

    /*
     * Convert b to an integer and do the scaling.
     */
    int_fast32_t iScaleFactor = convertToInt(fSignB, uSigB, iExpB);
    int_fast32_t iScaledExp   = iExpA + iScaleFactor;
    return softfloat_normRoundPackToExtF80(fSignA, iScaledExp, uSigA, 0 /*sigExtra*/, 80 /*precision*/, pState);
}

