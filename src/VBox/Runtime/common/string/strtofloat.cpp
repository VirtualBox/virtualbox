/* $Id: strtofloat.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - String To Floating Point Conversion.
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
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h> /* needed for RT_C_IS_DIGIT */
#include <iprt/err.h>

#include <float.h>
#include <math.h>
#if !defined(_MSC_VER) || !defined(IPRT_NO_CRT) /** @todo fix*/
# include <fenv.h>
#endif
#ifndef INFINITY  /* Not defined on older Solaris (like the one in the add build VM). */
# define INFINITY HUGE_VAL
#endif

#if defined(SOFTFLOAT_FAST_INT64) && !defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE) /** @todo better softfloat indicator? */
# define USE_SOFTFLOAT /* for scaling by power of 10 */
#endif
#ifdef USE_SOFTFLOAT
# include <softfloat.h>
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef union FLOATUNION
{
#ifdef RT_COMPILER_WITH_128BIT_LONG_DOUBLE
    RTFLOAT128U lrd;
#elif defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE)
    RTFLOAT80U2 lrd;
#else
    RTFLOAT64U  lrd;
#endif
    RTFLOAT64U  rd;
    RTFLOAT32U  r;
} FLOATUNION;

#define RET_TYPE_FLOAT        0
#define RET_TYPE_DOUBLE       1
#define RET_TYPE_LONG_DOUBLE  2

#ifdef RT_COMPILER_WITH_128BIT_LONG_DOUBLE
typedef RTFLOAT128U LONG_DOUBLE_U_T;
typedef __uint128_t UINT_MANTISSA_T;
# define UINT_MANTISSA_T_BITS   128
#elif defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE)
typedef RTFLOAT80U2 LONG_DOUBLE_U_T;
typedef uint64_t    UINT_MANTISSA_T;
# define UINT_MANTISSA_T_BITS   64
#else
typedef RTFLOAT64U  LONG_DOUBLE_U_T;
typedef uint64_t    UINT_MANTISSA_T;
# define UINT_MANTISSA_T_BITS   64
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/* in strtonum.cpp */
extern const unsigned char g_auchDigits[256];

#define DIGITS_ZERO_TERM 254
#define DIGITS_COLON     253
#define DIGITS_SPACE     252
#define DIGITS_DOT       251

/** Pair of default float quiet NaN values (indexed by fPositive). */
static RTFLOAT32U const     g_ar32QNan[2]   = { RTFLOAT32U_INIT_QNAN(1), RTFLOAT32U_INIT_QNAN(0) };

/** Pair of default double quiet NaN values (indexed by fPositive). */
static RTFLOAT64U const     g_ardQNan[2]    = { RTFLOAT64U_INIT_QNAN(1), RTFLOAT64U_INIT_QNAN(0) };

/** Pair of default double quiet NaN values (indexed by fPositive). */
#if defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
static RTFLOAT128U const    g_alrdQNan[2]   = { RTFLOAT128U_INIT_QNAN(1), RTFLOAT128U_INIT_QNAN(0) };
#elif defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE)
static RTFLOAT80U2 const    g_alrdQNan[2]   = { RTFLOAT80U_INIT_QNAN(1), RTFLOAT80U_INIT_QNAN(0) };
#else
static RTFLOAT64U const     g_alrdQNan[2]   = { RTFLOAT64U_INIT_QNAN(1), RTFLOAT64U_INIT_QNAN(0) };
#endif

/** NaN fraction value masks. */
static uint64_t const       g_fNanMasks[3] =
{
    RT_BIT_64(RTFLOAT32U_FRACTION_BITS - 1) - 1,        /* 22=quiet(1) / silent(0) */
    RT_BIT_64(RTFLOAT64U_FRACTION_BITS - 1) - 1,        /* 51=quiet(1) / silent(0) */
#if defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
    RT_BIT_64(RTFLOAT128U_FRACTION_BITS - 1 - 64) - 1,  /* 111=quiet(1) / silent(0) */
#elif defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE)
    RT_BIT_64(RTFLOAT80U_FRACTION_BITS - 1) - 1,        /* bit 63=NaN; bit 62=quiet(1) / silent(0) */
#else
    RT_BIT_64(RTFLOAT64U_FRACTION_BITS - 1) - 1,
#endif
};

#if 0
/** Maximum exponent value in the binary representation for a RET_TYPE_XXX. */
static const int32_t g_iMaxExp[3] =
{
    RTFLOAT32U_EXP_MAX - 1 - RTFLOAT32U_EXP_BIAS,
    RTFLOAT64U_EXP_MAX - 1 - RTFLOAT64U_EXP_BIAS,
#if defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
    RTFLOAT128U_EXP_MAX - 1 - RTFLOAT128U_EXP_BIAS,
#elif defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE)
    RTFLOAT80U_EXP_MAX - 1 - RTFLOAT80U_EXP_BIAS,
#else
    RTFLOAT64U_EXP_MAX - 1 - RTFLOAT64U_EXP_BIAS,
#endif
};

/** Minimum exponent value in the binary representation for a RET_TYPE_XXX. */
static const int32_t g_iMinExp[3] =
{
    1 - RTFLOAT32U_EXP_BIAS,
    1 - RTFLOAT64U_EXP_BIAS,
#if defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
    1 - RTFLOAT128U_EXP_BIAS,
#elif defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE)
    1 - RTFLOAT80U_EXP_BIAS,
#else
    1 - RTFLOAT64U_EXP_BIAS,
#endif
};
#endif

#if 0 /* unused */
# if defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE) || defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
static const long double g_lrdPowerMin10 = 1e4931L;
static const long double g_lrdPowerMax10 = 1e4932L;
# else
static const long double g_lrdPowerMin10 = 1e307L;
static const long double g_lrdPowerMax10 = 1e308L;
# endif
#endif

#ifdef USE_SOFTFLOAT
/** SoftFloat: Power of 10 table using 128-bit floating point.
 *
 * @code
    softfloat_state_t SoftState = SOFTFLOAT_STATE_INIT_DEFAULTS();
    float128_t Power10;
    ui32_to_f128M(10, &Power10, &SoftState);
    for (unsigned iBit = 0; iBit < 13; iBit++)
    {
        RTAssertMsg2("    { { UINT64_C(%#018RX64), UINT64_C(%#018RX64) } }, %c* 1e%u (%RU64) *%c\n", Power10.v[0], Power10.v[1],
                     '/', RT_BIT_32(iBit), f128M_to_ui64(&Power10, softfloat_round_near_even, false, &SoftState), '/');
        f128M_mul(&Power10, &Power10, &Power10, &SoftState);
    }
  @endcode */
static const float128_t g_ar128Power10[] =
{
    { { UINT64_C(0x0000000000000000), UINT64_C(0x4002400000000000) } }, /* 1e1 (10) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x4005900000000000) } }, /* 1e2 (100) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x400c388000000000) } }, /* 1e4 (10000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x40197d7840000000) } }, /* 1e8 (100000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x40341c37937e0800) } }, /* 1e16 (10000000000000000) */
    { { UINT64_C(0x6b3be04000000000), UINT64_C(0x40693b8b5b5056e1) } }, /* 1e32 (18446744073709551615) */
    { { UINT64_C(0x4daa797ed6e38ed6), UINT64_C(0x40d384f03e93ff9f) } }, /* 1e64 (18446744073709551615) */
    { { UINT64_C(0x19bf8cde66d86d61), UINT64_C(0x41a827748f9301d3) } }, /* 1e128 (18446744073709551615) */
    { { UINT64_C(0xbd1bbb77203731fb), UINT64_C(0x435154fdd7f73bf3) } }, /* 1e256 (18446744073709551615) */
    { { UINT64_C(0x238d98cab8a97899), UINT64_C(0x46a3c633415d4c1d) } }, /* 1e512 (18446744073709551615) */
    { { UINT64_C(0x182eca1a7a51e308), UINT64_C(0x4d4892eceb0d02ea) } }, /* 1e1024 (18446744073709551615) */
    { { UINT64_C(0xbbc94e9a519c651e), UINT64_C(0x5a923d1676bb8a7a) } }, /* 1e2048 (18446744073709551615) */
    { { UINT64_C(0x2f3592982a7f005a), UINT64_C(0x752588c0a4051441) } }, /* 1e4096 (18446744073709551615) */
    /* INF */
};

/** SoftFloat: Initial value for power of 10 scaling.
 * This deals with the first 32 powers of 10, covering the a full 64-bit
 * mantissa and a small exponent w/o needing to make use of g_ar128Power10.
 *
 * @code
    softfloat_state_t SoftState = SOFTFLOAT_STATE_INIT_DEFAULTS();
    float128_t Num10;
    ui32_to_f128M(10, &Num10, &SoftState);
    float128_t Power10;
    ui32_to_f128M(1, &Power10, &SoftState);
    for (unsigned cTimes = 0; cTimes < 32; cTimes++)
    {
        RTAssertMsg2("        { { UINT64_C(%#018RX64), UINT64_C(%#018RX64) } }, %c* 1e%u (%RU64) *%c\n", Power10.v[0], Power10.v[1],
                     '/', cTimes, f128M_to_ui64(&Power10, softfloat_round_near_even, false, &SoftState), '/');
        f128M_mul(&Power10, &Num10, &Power10, &SoftState);
    }
   @endcode */
static const float128_t g_ar128Power10Initial[] =
{
    { { UINT64_C(0x0000000000000000), UINT64_C(0x3fff000000000000) }  }, /* 1e0 (1) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x4002400000000000) }  }, /* 1e1 (10) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x4005900000000000) }  }, /* 1e2 (100) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x4008f40000000000) }  }, /* 1e3 (1000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x400c388000000000) }  }, /* 1e4 (10000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x400f86a000000000) }  }, /* 1e5 (100000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x4012e84800000000) }  }, /* 1e6 (1000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x4016312d00000000) }  }, /* 1e7 (10000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x40197d7840000000) }  }, /* 1e8 (100000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x401cdcd650000000) }  }, /* 1e9 (1000000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x40202a05f2000000) }  }, /* 1e10 (10000000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x402374876e800000) }  }, /* 1e11 (100000000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x4026d1a94a200000) }  }, /* 1e12 (1000000000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x402a2309ce540000) }  }, /* 1e13 (10000000000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x402d6bcc41e90000) }  }, /* 1e14 (100000000000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x4030c6bf52634000) }  }, /* 1e15 (1000000000000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x40341c37937e0800) }  }, /* 1e16 (10000000000000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x40376345785d8a00) }  }, /* 1e17 (100000000000000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x403abc16d674ec80) }  }, /* 1e18 (1000000000000000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x403e158e460913d0) }  }, /* 1e19 (10000000000000000000) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x40415af1d78b58c4) }  }, /* 1e20 (18446744073709551615) */
    { { UINT64_C(0x0000000000000000), UINT64_C(0x4044b1ae4d6e2ef5) }  }, /* 1e21 (18446744073709551615) */
    { { UINT64_C(0x2000000000000000), UINT64_C(0x40480f0cf064dd59) }  }, /* 1e22 (18446744073709551615) */
    { { UINT64_C(0x6800000000000000), UINT64_C(0x404b52d02c7e14af) }  }, /* 1e23 (18446744073709551615) */
    { { UINT64_C(0x4200000000000000), UINT64_C(0x404ea784379d99db) }  }, /* 1e24 (18446744073709551615) */
    { { UINT64_C(0x0940000000000000), UINT64_C(0x405208b2a2c28029) }  }, /* 1e25 (18446744073709551615) */
    { { UINT64_C(0x4b90000000000000), UINT64_C(0x40554adf4b732033) }  }, /* 1e26 (18446744073709551615) */
    { { UINT64_C(0x1e74000000000000), UINT64_C(0x40589d971e4fe840) }  }, /* 1e27 (18446744073709551615) */
    { { UINT64_C(0x1308800000000000), UINT64_C(0x405c027e72f1f128) }  }, /* 1e28 (18446744073709551615) */
    { { UINT64_C(0x17caa00000000000), UINT64_C(0x405f431e0fae6d72) }  }, /* 1e29 (18446744073709551615) */
    { { UINT64_C(0x9dbd480000000000), UINT64_C(0x406293e5939a08ce) }  }, /* 1e30 (18446744073709551615) */
    { { UINT64_C(0x452c9a0000000000), UINT64_C(0x4065f8def8808b02) }  }, /* 1e31 (18446744073709551615) */
};

#else /* !USE_SOFTFLOAT */
/** Long Double: Power of 10 table scaling table.
 * @note LDBL_MAX_10_EXP is 4932 for 80-bit and 308 for 64-bit type. */
static const long double a_lrdPower10[] =
{
    1e1L,
    1e2L,
    1e4L,
    1e8L,
    1e16L,
    1e32L,
    1e64L,
    1e128L,
    1e256L,
# if defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE) || defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
    1e512L,
    1e1024L,
    1e2048L,
    1e4096L,
# endif
};

/** Long double: Initial value for power of 10 scaling.
 * This deals with the first 32 powers of 10, covering the a full 64-bit
 * mantissa and a small exponent w/o needing to make use of g_ar128Power10. */
static const long double g_alrdPower10Initial[] =
{
    1e0L,
    1e1L,
    1e2L,
    1e3L,
    1e4L,
    1e5L,
    1e6L,
    1e7L,
    1e8L,
    1e9L,
    1e10L,
    1e11L,
    1e12L,
    1e13L,
    1e14L,
    1e15L,
    1e16L,
    1e17L,
    1e18L,
    1e19L,
    1e20L,
    1e21L,
    1e22L,
    1e23L,
    1e24L,
    1e25L,
    1e26L,
    1e27L,
    1e28L,
    1e29L,
    1e30L,
    1e31L,
};

/* Tell the compiler that we'll mess with the FPU environment. */
# ifdef _MSC_VER
#  pragma fenv_access(on)
# endif
#endif /*!USE_SOFTFLOAT */


/**
 * Multiply @a pVal by 10 to the power of @a iExponent10.
 *
 * This is currently a weak point where we might end up with rounding issues.
 */
static int rtStrToLongDoubleExp10(LONG_DOUBLE_U_T *pVal, int iExponent10)
{
    AssertReturn(iExponent10 != 0, VINF_SUCCESS);
#ifdef USE_SOFTFLOAT
    /* Use 128-bit precision floating point from softfloat to improve accuracy. */

    softfloat_state_t   SoftState = SOFTFLOAT_STATE_INIT_DEFAULTS();
    float128_t          Val;
# ifdef RT_COMPILER_WITH_80BIT_LONG_DOUBLE
    extFloat80M         Tmp = EXTFLOAT80M_INIT(pVal->s2.uSignAndExponent, pVal->s2.uMantissa);
    extF80M_to_f128M(&Tmp, &Val, &SoftState);
# else
    float64_t           Tmp = { pVal->u };
    f64_to_f128M(Tmp, &Val, &SoftState);
# endif

    /*
     * Calculate the scaling factor.  If we need to make use of the last table
     * entry, we will do part of the scaling here to avoid overflowing Factor.
     */
    unsigned            uAbsExp   = (unsigned)RT_ABS(iExponent10);
    AssertCompile(RT_ELEMENTS(g_ar128Power10Initial) == 32);
    unsigned            iBit      = 5;
    float128_t          Factor    = g_ar128Power10Initial[uAbsExp & 31];
    uAbsExp >>= iBit;
    while (uAbsExp != 0)
    {
        if (iBit < RT_ELEMENTS(g_ar128Power10))
        {
            if (uAbsExp & 1)
            {
                if (iBit < RT_ELEMENTS(g_ar128Power10) - 1)
                    f128M_mul(&Factor, &g_ar128Power10[iBit], &Factor, &SoftState);
                else
                {
                    /* Must do it in two steps to avoid prematurely overflowing the factor value. */
                    if (iExponent10 > 0)
                        f128M_mul(&Val, &Factor, &Val, &SoftState);
                    else
                        f128M_div(&Val, &Factor, &Val, &SoftState);
                    Factor = g_ar128Power10[iBit];
                }
            }
        }
        else if (iExponent10 < 0)
        {
            pVal->r = pVal->r < 0.0L ? -0.0L : +0.0L;
            return VERR_FLOAT_UNDERFLOW;
        }
        else
        {
            pVal->r = pVal->r < 0.0L ? -INFINITY : +INFINITY;
            return VERR_FLOAT_OVERFLOW;
        }
        iBit++;
        uAbsExp >>= 1;
    }

    /*
     * Do the scaling (or what remains).
     */
    if (iExponent10 > 0)
        f128M_mul(&Val, &Factor, &Val, &SoftState);
    else
        f128M_div(&Val, &Factor, &Val, &SoftState);

# ifdef RT_COMPILER_WITH_80BIT_LONG_DOUBLE
    f128M_to_extF80M(&Val, &Tmp, &SoftState);
    pVal->s2.uSignAndExponent = Tmp.signExp;
    pVal->s2.uMantissa        = Tmp.signif;
# else
    Tmp = f128M_to_f64(&Val, &SoftState);
    pVal->u = Tmp.v;
# endif

    /*
     * Check for under/overflow and return.
     */
    int rc;
    if (!(SoftState.exceptionFlags & (softfloat_flag_underflow | softfloat_flag_overflow)))
        rc = VINF_SUCCESS;
    else if (SoftState.exceptionFlags & softfloat_flag_underflow)
        rc = VERR_FLOAT_UNDERFLOW;
    else
        rc = VERR_FLOAT_OVERFLOW;

#else  /* !USE_SOFTFLOAT */
# if 0
    /*
     * Use RTBigNum, falling back on the simple approach if we don't need the
     * precision or run out of memory?
     */
   /** @todo implement RTBigNum approach */
# endif

    /*
     * Simple approach.
     */
# if !defined(_MSC_VER) || !defined(IPRT_NO_CRT) /** @todo fix*/
    fenv_t SavedFpuEnv;
    feholdexcept(&SavedFpuEnv);
# endif

    /*
     * Calculate the scaling factor.  If we need to make use of the last table
     * entry, we will do part of the scaling here to avoid overflowing lrdFactor.
     */
    AssertCompile(RT_ELEMENTS(g_alrdPower10Initial) == 32);
    int         rc        = VINF_SUCCESS;
    unsigned    uAbsExp   = (unsigned)RT_ABS(iExponent10);
    long double lrdFactor = g_alrdPower10Initial[uAbsExp & 31];
    unsigned    iBit      = 5;
    uAbsExp >>= iBit;

    while (uAbsExp != 0)
    {
        if (iBit < RT_ELEMENTS(a_lrdPower10))
        {
            if (uAbsExp & 1)
            {
                if (iBit < RT_ELEMENTS(a_lrdPower10) - 1)
                    lrdFactor *= a_lrdPower10[iBit];
                else
                {
                    /* Must do it in two steps to avoid prematurely overflowing the factor value. */
                    if (iExponent10 < 0)
                        pVal->r /= lrdFactor;
                    else
                        pVal->r *= lrdFactor;
                    lrdFactor = a_lrdPower10[iBit];
                }
            }
        }
        else if (iExponent10 < 0)
        {
            pVal->r = pVal->r < 0.0L ? -0.0L : +0.0L;
            rc = VERR_FLOAT_UNDERFLOW;
            break;
        }
        else
        {
            pVal->r = pVal->r < 0.0L ? -INFINITY : +INFINITY;
            rc = VERR_FLOAT_OVERFLOW;
            break;
        }
        iBit++;
        uAbsExp >>= 1;
    }

    /*
     * Do the scaling (or what remains).
     */
    if (iExponent10 < 0)
        pVal->r /= lrdFactor;
    else
        pVal->r *= lrdFactor;

# if !defined(_MSC_VER) || !defined(IPRT_NO_CRT) /** @todo fix*/
    fesetenv(&SavedFpuEnv);
# endif

#endif /* !USE_SOFTFLOAT */
    return rc;
}



/**
 * Set @a ppszNext and check for trailing spaces & chars if @a rc is
 * VINF_SUCCESS.
 *
 * @returns IPRT status code.
 * @param   psz         The current input position.
 * @param   ppszNext    Where to return the pointer to the end of the value.
 *                      Optional.
 * @param   cchMax      Number of bytes left in the string starting at @a psz.
 * @param   rc          The status code to return.
 */
static int rtStrToLongDoubleReturnChecks(const char *psz, char **ppszNext, size_t cchMax, int rc)
{
    if (ppszNext)
        *ppszNext = (char *)psz;

    /* Trailing spaces/chars warning: */
    if (rc == VINF_SUCCESS && cchMax > 0 && *psz)
    {
        do
        {
            char ch = *psz++;
            if (ch == ' ' || ch == '\t')
                cchMax--;
            else
                return ch == '\0' ? VWRN_TRAILING_SPACES : VWRN_TRAILING_CHARS;
        } while (cchMax > 0);
        rc = VWRN_TRAILING_SPACES;
    }
    return rc;
}


/**
 * Set @a pRet to infinity, set @a ppszNext, and check for trailing spaces &
 * chars if @a rc is VINF_SUCCESS.
 *
 * @returns IPRT status code.
 * @param   psz         The current input position.
 * @param   ppszNext    Where to return the pointer to the end of the value.
 *                      Optional.
 * @param   cchMax      Number of bytes left in the string starting at @a psz.
 * @param   fPositive   Whether the infinity should be positive or negative.
 * @param   rc          The status code to return.
 * @param   iRetType    The target type.
 * @param   pRet        Where to store the result.
 */
static int rtStrToLongDoubleReturnInf(const char *psz, char **ppszNext, size_t cchMax, bool fPositive,
                                      int rc, unsigned iRetType, FLOATUNION *pRet)
{
    /*
     * Skip to the end of long form?
     */
    char ch;
    if (   cchMax >= 5
        && ((ch = psz[0]) == 'i' || ch == 'I')
        && ((ch = psz[1]) == 'n' || ch == 'N')
        && ((ch = psz[2]) == 'i' || ch == 'I')
        && ((ch = psz[3]) == 't' || ch == 'T')
        && ((ch = psz[4]) == 'y' || ch == 'Y'))
    {
        psz    += 5;
        cchMax -= 5;
    }

    /*
     * Set the return value:
     */
    switch (iRetType)
    {
        case RET_TYPE_FLOAT:
        {
            RTFLOAT32U const uRet = RTFLOAT32U_INIT_INF(!fPositive);
            AssertCompile(sizeof(uRet) == sizeof(pRet->r.r));
            pRet->r.r = uRet.r;
            break;
        }

        case RET_TYPE_LONG_DOUBLE:
#if defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE) || defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
        {
# if defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE)
            RTFLOAT80U2 const uRet = RTFLOAT80U_INIT_INF(!fPositive);
# else
            RTFLOAT128U const uRet = RTFLOAT128U_INIT_INF(!fPositive);
# endif
            pRet->lrd.lrd = uRet.lrd;
            break;
        }
#else
            AssertCompile(sizeof(long double) == sizeof(pRet->rd.rd));
            RT_FALL_THRU();
#endif
        case RET_TYPE_DOUBLE:
        {
            RTFLOAT64U const uRet = RTFLOAT64U_INIT_INF(!fPositive);
            AssertCompile(sizeof(uRet) == sizeof(pRet->rd.rd));
            pRet->rd.rd = uRet.rd;
            break;
        }

        default: AssertFailedBreak();
    }

    /*
     * Deal with whatever follows and return:
     */
    return rtStrToLongDoubleReturnChecks(psz, ppszNext, cchMax, rc);
}


/**
 * Parses the tag of a "NaN(tag)" value.
 *
 * We take the tag to be a number to be put in the mantissa of the NaN, possibly
 * suffixed by '[_]quiet' or '[_]signaling' (all or part) to indicate the type
 * of NaN.
 *
 * @param   pchTag      The tag string to parse.  Not zero terminated.
 * @param   cchTag      The length of the tag string value.
 * @param   fPositive   Whether the NaN should be positive or negative.
 * @param   iRetType    The target type.
 * @param   pRet        Where to store the result.
 */
static void rtStrParseNanTag(const char *pchTag, size_t cchTag, bool fPositive, unsigned iRetType, FLOATUNION *pRet)
{
    /*
     * Skip 0x - content is hexadecimal, so this is not necessary.
     */
    if (cchTag > 2 && pchTag[0] == '0' && (pchTag[1] == 'x' || pchTag[1] == 'X'))
    {
        pchTag += 2;
        cchTag -= 2;
    }

    /*
     * Parse the number, ignoring overflows and stopping on non-xdigit.
     */
    uint64_t uHiNum  = 0;
    uint64_t uLoNum    = 0;
    unsigned iXDigit = 0;
    while (cchTag > 0)
    {
        unsigned char uch      = (unsigned char)*pchTag;
        unsigned char uchDigit = g_auchDigits[uch];
        if (uchDigit >= 16)
            break;
        iXDigit++;
        if (iXDigit >= 16)
            uHiNum = (uHiNum << 4) | (uLoNum >> 60);
        uLoNum <<= 4;
        uLoNum  += uchDigit;
        pchTag++;
        cchTag--;
    }

    /*
     * Check for special "non-standard" quiet / signalling indicator.
     */
    while (cchTag > 0 && *pchTag == '_')
        pchTag++, cchTag--;
    bool fQuiet = true;
    if (cchTag > 0)
    {
        //const char *pszSkip = NULL;
        char       ch       = pchTag[0];
        if (ch == 'q' || ch == 'Q')
        {
            fQuiet  = true;
            //pszSkip = "qQuUiIeEtT\0"; /* cchTag stop before '\0', so we put two at the end to break out of the loop below. */
        }
        else if (ch == 's' || ch == 'S')
        {
            fQuiet  = false;
            //pszSkip = "sSiIgGnNaAlLiInNgG\0";
        }
        //if (pszSkip)
        //    do
        //    {
        //        pchTag++;
        //        cchTag--;
        //        pszSkip += 2;
        //    } while (cchTag > 0 && ((ch = *pchTag) == pszSkip[0] || ch == pszSkip[1]));
    }

    /*
     * Adjust the number according to the type.
     */
    Assert(iRetType < RT_ELEMENTS(g_fNanMasks));
#if defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
    if (iRetType == RET_TYPE_LONG_DOUBLE)
        uHiNum &= g_fNanMasks[RET_TYPE_LONG_DOUBLE];
    else
#endif
    {
        uHiNum  = 0;
        uLoNum &= g_fNanMasks[iRetType];
    }
    if (!uLoNum && !uHiNum && !fQuiet)
        uLoNum = 1; /* must not be zero, or it'll turn into an infinity */

    /*
     * Set the return value.
     */
    switch (iRetType)
    {
        case RET_TYPE_FLOAT:
        {
            RTFLOAT32U const uRet = RTFLOAT32U_INIT_NAN_EX(fQuiet, !fPositive, (uint32_t)uLoNum);
            pRet->r = uRet;
            break;
        }

        case RET_TYPE_LONG_DOUBLE:
#if defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE) || defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
        {
# if defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE)
            RTFLOAT80U2 const uRet = RTFLOAT80U_INIT_NAN_EX(fQuiet, !fPositive, uLoNum);
# else
            RTFLOAT128U const uRet = RTFLOAT128U_INIT_NAN_EX(fQuiet, !fPositive, uHiNum, uLoNum);
# endif
            pRet->lrd = uRet;
            break;
        }
#else
            AssertCompile(sizeof(long double) == sizeof(pRet->rd.rd));
            RT_FALL_THRU();
#endif
        case RET_TYPE_DOUBLE:
        {
            RTFLOAT64U const uRet = RTFLOAT64U_INIT_NAN_EX(fQuiet, !fPositive, uLoNum);
            pRet->rd = uRet;
            break;
        }

        default: AssertFailedBreak();
    }

    //return cchTag == 0;
}


/**
 * Finish parsing NaN, set @a pRet to NaN, set @a ppszNext, and check for
 * trailing spaces & chars if @a rc is VINF_SUCCESS.
 *
 * @returns IPRT status code.
 * @param   psz         The current input position.
 * @param   ppszNext    Where to return the pointer to the end of the value.
 *                      Optional.
 * @param   cchMax      Number of bytes left in the string starting at @a psz.
 * @param   fPositive   Whether the NaN should be positive or negative.
 * @param   iRetType    The target type.
 * @param   pRet        Where to store the result.
 */
static int rtStrToLongDoubleReturnNan(const char *psz, char **ppszNext, size_t cchMax, bool fPositive,
                                      unsigned iRetType, FLOATUNION *pRet)
{
    /*
     * Any NaN sub-number? E.g. NaN(1) or Nan(0x42).  We'll require a closing
     * parenthesis or we'll just ignore it.
     */
    if (cchMax >= 2 && *psz == '(')
    {
        unsigned cchTag = 1;
        char     ch     = '\0';
        while (cchTag < cchMax && (RT_C_IS_ALNUM((ch = psz[cchTag])) || ch == '_'))
            cchTag++;
        if (ch == ')')
        {
            rtStrParseNanTag(psz + 1, cchTag - 1, fPositive, iRetType, pRet);
            psz    += cchTag + 1;
            cchMax -= cchTag + 1;
            return rtStrToLongDoubleReturnChecks(psz, ppszNext, cchMax, VINF_SUCCESS);
        }
    }

    /*
     * Set the return value to the default NaN value.
     */
    switch (iRetType)
    {
        case RET_TYPE_FLOAT:
            pRet->r = g_ar32QNan[fPositive];
            break;

        case RET_TYPE_DOUBLE:
            pRet->rd = g_ardQNan[fPositive];
            break;

        case RET_TYPE_LONG_DOUBLE:
            pRet->lrd = g_alrdQNan[fPositive];
            break;

        default: AssertFailedBreak();
    }

    return rtStrToLongDoubleReturnChecks(psz, ppszNext, cchMax, VINF_SUCCESS);
}


RTDECL(long double) RTStrNanLongDouble(const char *pszTag, bool fPositive)
{
    if (pszTag)
    {
        size_t cchTag = strlen(pszTag);
        if (cchTag > 0)
        {
            FLOATUNION u;
            rtStrParseNanTag(pszTag, cchTag, fPositive, RET_TYPE_LONG_DOUBLE, &u);
            return u.lrd.r;
        }
    }
    return g_alrdQNan[fPositive].r;
}


RTDECL(double) RTStrNanDouble(const char *pszTag, bool fPositive)
{
    if (pszTag)
    {
        size_t cchTag = strlen(pszTag);
        if (cchTag > 0)
        {
            FLOATUNION u;
            rtStrParseNanTag(pszTag, cchTag, fPositive, RET_TYPE_DOUBLE, &u);
            return u.rd.r;
        }
    }
    return g_ardQNan[fPositive].r;
}


RTDECL(float) RTStrNanFloat(const char *pszTag, bool fPositive)
{
    if (pszTag)
    {
        size_t cchTag = strlen(pszTag);
        if (cchTag > 0)
        {
            FLOATUNION u;
            rtStrParseNanTag(pszTag, cchTag, fPositive, RET_TYPE_FLOAT, &u);
            return u.r.r;
        }
    }
    return g_ar32QNan[fPositive].r;
}


/**
 * Set @a pRet to zero, set @a ppszNext, and check for trailing spaces &
 * chars if @a rc is VINF_SUCCESS.
 *
 * @returns IPRT status code.
 * @param   psz         The current input position.
 * @param   ppszNext    Where to return the pointer to the end of the value.
 *                      Optional.
 * @param   cchMax      Number of bytes left in the string starting at @a psz.
 * @param   fPositive   Whether the value should be positive or negative.
 * @param   rc          The status code to return.
 * @param   iRetType    The target type.
 * @param   pRet        Where to store the result.
 */
static int rtStrToLongDoubleReturnZero(const char *psz, char **ppszNext, size_t cchMax, bool fPositive,
                                       int rc, unsigned iRetType, FLOATUNION *pRet)
{
    switch (iRetType)
    {
        case RET_TYPE_FLOAT:
            pRet->r.r     = fPositive ? +0.0F : -0.0F;
            break;

        case RET_TYPE_LONG_DOUBLE:
#if defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE) || defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
            pRet->lrd.lrd = fPositive ? +0.0L : -0.0L;
            break;
#else
            AssertCompile(sizeof(long double) == sizeof(pRet->rd.rd));
            RT_FALL_THRU();
#endif
        case RET_TYPE_DOUBLE:
            pRet->rd.rd   = fPositive ? +0.0  : -0.0;
            break;

        default: AssertFailedBreak();
    }

    return rtStrToLongDoubleReturnChecks(psz, ppszNext, cchMax, rc);
}


/**
 * Return overflow or underflow - setting @a pRet and @a ppszNext accordingly.
 *
 * @returns IPRT status code.
 * @param   psz         The current input position.
 * @param   ppszNext    Where to return the pointer to the end of the value.
 *                      Optional.
 * @param   cchMax      Number of bytes left in the string starting at @a psz.
 * @param   fPositive   Whether the value should be positive or negative.
 * @param   iExponent   Overflow/underflow indicator.
 * @param   iRetType    The target type.
 * @param   pRet        Where to store the result.
 */
static int rtStrToLongDoubleReturnOverflow(const char *psz, char **ppszNext, size_t cchMax, bool fPositive,
                                           int32_t iExponent, unsigned iRetType, FLOATUNION *pRet)
{
    if (iExponent > 0)
        return rtStrToLongDoubleReturnInf(psz, ppszNext, cchMax, fPositive, VERR_FLOAT_OVERFLOW, iRetType, pRet);
    return rtStrToLongDoubleReturnZero(psz, ppszNext, cchMax, fPositive, VERR_FLOAT_UNDERFLOW, iRetType, pRet);
}


/**
 * Returns a denormal/subnormal value.
 *
 * This implies that iRetType is long double, or double if they are the same,
 * and that we should warn about underflowing.
 */
static int rtStrToLongDoubleReturnSubnormal(const char *psz, char **ppszNext, size_t cchMax, LONG_DOUBLE_U_T const *pVal,
                                            unsigned iRetType, FLOATUNION *pRet)
{
#if defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE) || defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
    Assert(iRetType == RET_TYPE_LONG_DOUBLE);
    pRet->lrd = *pVal;
#else
    Assert(iRetType == RET_TYPE_LONG_DOUBLE || iRetType == RET_TYPE_DOUBLE);
    pRet->rd = *pVal;
#endif
    RT_NOREF(iRetType);
    return rtStrToLongDoubleReturnChecks(psz, ppszNext, cchMax, VWRN_FLOAT_UNDERFLOW);
}


/**
 * Packs the given sign, mantissa, and (power of 2) exponent into the
 * return value.
 */
static int rtStrToLongDoubleReturnValue(const char *psz, char **ppszNext, size_t cchMax,
                                        bool fPositive, UINT_MANTISSA_T uMantissa, int32_t iExponent,
                                        unsigned iRetType, FLOATUNION *pRet)
{
    int rc = VINF_SUCCESS;
    switch (iRetType)
    {
        case RET_TYPE_FLOAT:
            iExponent += RTFLOAT32U_EXP_BIAS;
            if (iExponent <= 0)
            {
                /* Produce a subnormal value if it's within range, otherwise return zero. */
                if (iExponent < -RTFLOAT32U_FRACTION_BITS)
                    return rtStrToLongDoubleReturnZero(psz, ppszNext, cchMax, fPositive, VERR_FLOAT_UNDERFLOW, iRetType, pRet);
                rc = VWRN_FLOAT_UNDERFLOW;
                uMantissa >>= -iExponent + 1;
                iExponent   = 0;
            }
            else if (iExponent >= RTFLOAT32U_EXP_MAX)
                return rtStrToLongDoubleReturnInf(psz, ppszNext, cchMax, fPositive, VERR_FLOAT_OVERFLOW, iRetType, pRet);

            pRet->r.s.uFraction = (uMantissa >> (UINT_MANTISSA_T_BITS - 1 - RTFLOAT32U_FRACTION_BITS))
                                & (RT_BIT_32(RTFLOAT32U_FRACTION_BITS) - 1);
            pRet->r.s.uExponent = iExponent;
            pRet->r.s.fSign     = !fPositive;
            break;

        case RET_TYPE_LONG_DOUBLE:
#ifdef RT_COMPILER_WITH_80BIT_LONG_DOUBLE
# if UINT_MANTISSA_T_BITS != 64
#  error Unsupported UINT_MANTISSA_T_BITS count.
# endif
            iExponent += RTFLOAT80U_EXP_BIAS;
            if (iExponent <= 0)
            {
                /* Produce a subnormal value if it's within range, otherwise return zero. */
                if (iExponent < -RTFLOAT80U_FRACTION_BITS)
                    return rtStrToLongDoubleReturnZero(psz, ppszNext, cchMax, fPositive, VERR_FLOAT_UNDERFLOW, iRetType, pRet);
                rc = VWRN_FLOAT_UNDERFLOW;
                uMantissa >>= -iExponent + 1;
                iExponent   = 0;
            }
            else if (iExponent >= RTFLOAT80U_EXP_MAX)
                return rtStrToLongDoubleReturnInf(psz, ppszNext, cchMax, fPositive, VERR_FLOAT_OVERFLOW, iRetType, pRet);

            pRet->lrd.s.uMantissa = uMantissa;
            pRet->lrd.s.uExponent = iExponent;
            pRet->lrd.s.fSign     = !fPositive;
            break;
#elif defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
            iExponent += RTFLOAT128U_EXP_BIAS;
            uMantissa >>= 128 - RTFLOAT128U_FRACTION_BITS;
            if (iExponent <= 0)
            {
                /* Produce a subnormal value if it's within range, otherwise return zero. */
                if (iExponent < -RTFLOAT128U_FRACTION_BITS)
                    return rtStrToLongDoubleReturnZero(psz, ppszNext, cchMax, fPositive, VERR_FLOAT_UNDERFLOW, iRetType, pRet);
                rc = VWRN_FLOAT_UNDERFLOW;
                uMantissa >>= -iExponent + 1;
                iExponent   = 0;
            }
            else if (iExponent >= RTFLOAT80U_EXP_MAX)
                return rtStrToLongDoubleReturnInf(psz, ppszNext, cchMax, fPositive, VERR_FLOAT_OVERFLOW, iRetType, pRet);
            pRet->lrd.s64.uFractionHi = (uint64_t)(uMantissa >> 64) & (RT_BIT_64(RTFLOAT128U_FRACTION_BITS - 64) - 1);
            pRet->lrd.s64.uFractionLo = (uint64_t)uMantissa;
            pRet->lrd.s64.uExponent   = iExponent;
            pRet->lrd.s64.fSign       = !fPositive;
            break;
#else
            AssertCompile(sizeof(long double) == sizeof(pRet->rd.rd));
            RT_FALL_THRU();
#endif
        case RET_TYPE_DOUBLE:
            iExponent += RTFLOAT64U_EXP_BIAS;
            if (iExponent <= 0)
            {
                /* Produce a subnormal value if it's within range, otherwise return zero. */
                if (iExponent < -RTFLOAT64U_FRACTION_BITS)
                    return rtStrToLongDoubleReturnZero(psz, ppszNext, cchMax, fPositive, VERR_FLOAT_UNDERFLOW, iRetType, pRet);
                rc = VWRN_FLOAT_UNDERFLOW;
                uMantissa >>= -iExponent + 1;
                iExponent   = 0;
            }
            else if (iExponent >= RTFLOAT64U_EXP_MAX)
                return rtStrToLongDoubleReturnInf(psz, ppszNext, cchMax, fPositive, VERR_FLOAT_OVERFLOW, iRetType, pRet);

            pRet->rd.s64.uFraction = (uMantissa >> (UINT_MANTISSA_T_BITS - 1 - RTFLOAT64U_FRACTION_BITS))
                                   & (RT_BIT_64(RTFLOAT64U_FRACTION_BITS) - 1);
            pRet->rd.s64.uExponent = iExponent;
            pRet->rd.s64.fSign     = !fPositive;
            break;

        default:
            AssertFailedReturn(VERR_INTERNAL_ERROR_3);
    }
    return rtStrToLongDoubleReturnChecks(psz, ppszNext, cchMax, rc);
}


/**
 * Worker for RTStrToLongDoubleEx, RTStrToDoubleEx and RTStrToFloatEx.
 *
 * @returns IPRT status code
 * @param   pszValue    The string value to convert.
 * @param   ppszNext    Where to return the pointer to the end of the value.
 *                      Optional.
 * @param   cchMax      Number of bytes left in the string starting at @a psz.
 * @param   iRetType    The return type: float, double or long double.
 * @param   pRet        The return value union.
 */
static int rtStrToLongDoubleWorker(const char *pszValue, char **ppszNext, size_t cchMax, unsigned iRetType, FLOATUNION *pRet)
{
    const char *psz = pszValue;
    if (!cchMax)
        cchMax = ~(size_t)cchMax;

    /*
     * Sign.
     */
    bool fPositive = true;
    while (cchMax > 0)
    {
        if (*psz == '+')
            fPositive = true;
        else if (*psz == '-')
            fPositive = !fPositive;
        else
            break;
        psz++;
        cchMax--;
    }

    /*
     * Constant like "Inf", "Infinity", "NaN" or "NaN(hexstr)"?
     */
    /* "Inf" or "Infinity"? */
    if (cchMax == 0)
        return rtStrToLongDoubleReturnZero(pszValue, ppszNext, cchMax, fPositive, VERR_NO_DIGITS, iRetType, pRet);
    if (cchMax >= 3)
    {
        char ch = *psz;
        /* Inf: */
        if (ch == 'i' || ch == 'I')
        {
            if (   ((ch = psz[1]) == 'n' || ch == 'N')
                && ((ch = psz[2]) == 'f' || ch == 'F'))
                return rtStrToLongDoubleReturnInf(psz + 3, ppszNext, cchMax - 3, fPositive, VINF_SUCCESS, iRetType, pRet);
        }
        /* Nan: */
        else if (ch == 'n' || ch == 'N')
        {
            if (   ((ch = psz[1]) == 'a' || ch == 'A')
                && ((ch = psz[2]) == 'n' || ch == 'N'))
                return rtStrToLongDoubleReturnNan(psz + 3, ppszNext, cchMax - 3, fPositive, iRetType, pRet);
        }
    }

    /*
     * Check for hex prefix.
     */
#ifdef RT_COMPILER_WITH_128BIT_LONG_DOUBLE
    unsigned cMaxDigits      = 33;
#elif defined(RT_COMPILER_WITH_80BIT_LONG_DOUBLE)
    unsigned cMaxDigits      = 19;
#else
    unsigned cMaxDigits      = 18;
#endif
    unsigned uBase           = 10;
    unsigned uExpDigitFactor = 1;
    if (cchMax >= 2 && psz[0] == '0' && (psz[1] == 'x' || psz[1] == 'X'))
    {
        cMaxDigits      = 16;
        uBase           = 16;
        uExpDigitFactor = 4;
        cchMax         -= 2;
        psz            += 2;
    }

    /*
     * Now, parse the mantissa.
     */
#ifdef RT_COMPILER_WITH_128BIT_LONG_DOUBLE
    uint8_t     abDigits[36];
#else
    uint8_t     abDigits[20];
#endif
    unsigned    cDigits           = 0;
    unsigned    cFractionDigits   = 0;
    uint8_t     fSeenNonZeroDigit = 0;
    bool        fInFraction       = false;
    bool        fSeenDigits       = false;
    while (cchMax > 0)
    {
        uint8_t b = g_auchDigits[(unsigned char)*psz];
        if (b < uBase)
        {
            fSeenDigits        = true;
            fSeenNonZeroDigit |= b;
            if (fSeenNonZeroDigit)
            {
                if (cDigits < RT_ELEMENTS(abDigits))
                    abDigits[cDigits] = b;
                cDigits++;
                cFractionDigits += fInFraction;
            }
        }
        else if (b == DIGITS_DOT && !fInFraction)
            fInFraction = true;
        else
            break;
        psz++;
        cchMax--;
    }

    /* If we've seen no digits, or just a dot, return zero already. */
    if (!fSeenDigits)
    {
        if (fInFraction) /* '+.' => 0.0 ? */
            return rtStrToLongDoubleReturnZero(psz, ppszNext, cchMax, fPositive,  VINF_SUCCESS, iRetType, pRet);
        if (uBase == 16) /* '+0x' => 0.0 & *=pszNext="x..." */
            return rtStrToLongDoubleReturnZero(psz - 1, ppszNext, cchMax, fPositive,  VINF_SUCCESS, iRetType, pRet);
        /* '' and '+' -> no digits + 0.0. */
        return rtStrToLongDoubleReturnZero(pszValue, ppszNext, cchMax, fPositive, VERR_NO_DIGITS, iRetType, pRet);
    }

    /*
     * Parse the exponent.
     * This is optional and we ignore incomplete ones like "e+".
     */
    int32_t iExponent = 0;
    if (cchMax >= 2)  /* min "e0" */
    {
        char ch = *psz;
        if (uBase == 10 ? ch == 'e' || ch == 'E' : ch == 'p' || ch == 'P')
        {
            bool    fExpOverflow = false;
            bool    fPositiveExp = true;
            size_t  off          = 1;
            ch = psz[off];
            if (ch == '+' || ch == '-')
            {
                fPositiveExp = ch == '+';
                off++;
            }
            uint8_t b;
            if (   off < cchMax
                && (b = g_auchDigits[(unsigned char)psz[off]]) < 10)
            {
                do
                {
                    int32_t const iPreviousExponent = iExponent;
                    iExponent *= 10;
                    iExponent += b;
                    if (iExponent < iPreviousExponent)
                        fExpOverflow = true;
                    off++;
                } while (off < cchMax && (b = g_auchDigits[(unsigned char)psz[off]]) < 10);
                if (!fPositiveExp)
                    iExponent = -iExponent;
                cchMax -= off;
                psz    += off;
            }
            if (fExpOverflow || iExponent <= -65536 || iExponent >= 65536)
                return rtStrToLongDoubleReturnOverflow(pszValue, ppszNext, cchMax, fPositive, iExponent, iRetType, pRet);
        }
    }

    /* If the mantissa was all zeros, we can return zero now that we're past the exponent. */
    if (!fSeenNonZeroDigit)
        return rtStrToLongDoubleReturnZero(psz, ppszNext, cchMax, fPositive, VINF_SUCCESS, iRetType, pRet);

    /*
     * Adjust the expontent so we've got all digits to the left of the decimal point.
     */
    iExponent -= cFractionDigits * uExpDigitFactor;

    /*
     * Drop digits we won't translate.
     */
    if (cDigits > cMaxDigits)
    {
        iExponent += (cDigits - cMaxDigits) * uExpDigitFactor;
        cDigits    = cMaxDigits;
    }

    /*
     * Strip least significant zero digits.
     */
    while (cDigits > 0 && abDigits[cDigits - 1] == 0)
    {
        cDigits--;
        iExponent += uExpDigitFactor;
    }

    /*
     * The hexadecimal is relatively straight forward.
     */
    if (uBase == 16)
    {
        UINT_MANTISSA_T uMantissa = 0;
        for (unsigned iDigit = 0; iDigit < cDigits; iDigit++)
        {
            uMantissa |= (UINT_MANTISSA_T)abDigits[iDigit] << (UINT_MANTISSA_T_BITS - 4 - iDigit * 4);
            iExponent += 4;
        }
        Assert(uMantissa != 0);

        /* Shift to the left till the most significant bit is 1. */
        if (!((uMantissa >> (UINT_MANTISSA_T_BITS - 1)) & 1))
        {
#if UINT_MANTISSA_T_BITS == 64
            unsigned cShift = 64 - ASMBitLastSetU64(uMantissa);
            uMantissa <<= cShift;
            iExponent  -= cShift;
            Assert(uMantissa & RT_BIT_64(63));
#else
            do
            {
                uMantissa <<= 1;
                iExponent  -= 1;
            } while (!((uMantissa >> (UINT_MANTISSA_T_BITS - 1)) & 1));
#endif
        }

        /* Account for the 1 left of the decimal point. */
        iExponent--;

        /*
         * Produce the return value.
         */
        return rtStrToLongDoubleReturnValue(psz, ppszNext, cchMax, fPositive, uMantissa, iExponent, iRetType, pRet);
    }

    /*
     * For the decimal format, we'll rely on the floating point conversion of
     * the compiler/CPU for the mantissa.
     */
    UINT_MANTISSA_T uMantissa = 0;
    for (unsigned iDigit = 0; iDigit < cDigits; iDigit++)
    {
        uMantissa *= 10;
        uMantissa += abDigits[iDigit];
    }
    Assert(uMantissa != 0);

    LONG_DOUBLE_U_T uTmp;
    uTmp.r = fPositive ? (long double)uMantissa : -(long double)uMantissa;

    /*
     * Here comes the fun part, scaling it according to the power of 10 exponent.
     * We only need to consider overflows and underflows when scaling, when
     * iExponent is zero we can be sure the target type can handle the result.
     */
    if (iExponent != 0)
    {
        rtStrToLongDoubleExp10(&uTmp, iExponent);
#ifdef RT_COMPILER_WITH_80BIT_LONG_DOUBLE
        if (!RTFLOAT80U_IS_NORMAL(&uTmp))
#elif defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
        if (!RTFLOAT128U_IS_NORMAL(&uTmp))
#else
        if (!RTFLOAT64U_IS_NORMAL(&uTmp))
#endif
        {
#ifdef RT_COMPILER_WITH_80BIT_LONG_DOUBLE
            if (RTFLOAT80U_IS_DENORMAL(&uTmp) && iRetType == RET_TYPE_LONG_DOUBLE)
#elif defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
            if (RTFLOAT128U_IS_SUBNORMAL(&uTmp) && iRetType == RET_TYPE_LONG_DOUBLE)
#else
            if (RTFLOAT64U_IS_SUBNORMAL(&uTmp) && iRetType != RET_TYPE_FLOAT)
#endif
                return rtStrToLongDoubleReturnSubnormal(psz, ppszNext, cchMax, &uTmp, iRetType, pRet);
            return rtStrToLongDoubleReturnOverflow(psz, ppszNext, cchMax, fPositive, iExponent, iRetType, pRet);
        }
    }

    /*
     * We've got a normal value in uTmp when we get here, just repack it in the
     * target format and return.
     */
#ifdef RT_COMPILER_WITH_80BIT_LONG_DOUBLE
    Assert(RTFLOAT80U_IS_NORMAL(&uTmp));
    if (iRetType == RET_TYPE_LONG_DOUBLE)
    {
        pRet->lrd = uTmp;
        return rtStrToLongDoubleReturnChecks(psz, ppszNext, cchMax, VINF_SUCCESS);
    }
    fPositive = uTmp.s.fSign;
    iExponent = uTmp.s.uExponent - RTFLOAT80U_EXP_BIAS;
    uMantissa = uTmp.s.uMantissa;
# if UINT_MANTISSA_T_BITS > 64
    uMantissa <<= UINT_MANTISSA_T_BITS - 64;
# endif
#elif defined(RT_COMPILER_WITH_128BIT_LONG_DOUBLE)
    Assert(RTFLOAT128U_IS_NORMAL(&uTmp));
    if (iRetType == RET_TYPE_LONG_DOUBLE)
    {
        pRet->lrd = uTmp;
        return rtStrToLongDoubleReturnChecks(psz, ppszNext, cchMax, VINF_SUCCESS);
    }
    fPositive  = uTmp.s64.fSign;
    iExponent  = uTmp.s64.uExponent - RTFLOAT128U_EXP_BIAS;
    uMantissa  = (UINT_MANTISSA_T)uTmp.s64.uFractionHi << (UINT_MANTISSA_T_BITS - RTFLOAT128U_FRACTION_BITS - 1 + 64);
    uMantissa |= (UINT_MANTISSA_T)uTmp.s64.uFractionLo << (UINT_MANTISSA_T_BITS - RTFLOAT128U_FRACTION_BITS - 1);
    uMantissa |= (UINT_MANTISSA_T)1 << (UINT_MANTISSA_T_BITS - 1);
#else
    Assert(RTFLOAT64U_IS_NORMAL(&uTmp));
    if (   iRetType == RET_TYPE_DOUBLE
        || iRetType == RET_TYPE_LONG_DOUBLE)
    {
        pRet->rd = uTmp;
        return rtStrToLongDoubleReturnChecks(psz, ppszNext, cchMax, VINF_SUCCESS);
    }
    fPositive = uTmp.s64.fSign;
    iExponent = uTmp.s64.uExponent - RTFLOAT64U_EXP_BIAS;
    uMantissa = uTmp.s64.uFraction | RT_BIT_64(RTFLOAT64U_FRACTION_BITS);
# if UINT_MANTISSA_T_BITS > 64
    uMantissa <<= UINT_MANTISSA_T_BITS - 64;
# endif
#endif
    return rtStrToLongDoubleReturnValue(psz, ppszNext, cchMax, fPositive, uMantissa, iExponent, iRetType, pRet);
}


RTDECL(int) RTStrToLongDoubleEx(const char *pszValue, char **ppszNext, size_t cchMax, long double *plrd)
{
    FLOATUNION u;
    int rc = rtStrToLongDoubleWorker(pszValue, ppszNext, cchMax, RET_TYPE_LONG_DOUBLE, &u);
    if (plrd)
#ifdef RT_COMPILER_WITH_80BIT_LONG_DOUBLE
        *plrd = u.lrd.lrd;
#else
        *plrd = u.rd.rd;
#endif
    return rc;
}


RTDECL(int) RTStrToDoubleEx(const char *pszValue, char **ppszNext, size_t cchMax, double *prd)
{
    FLOATUNION u;
    int rc = rtStrToLongDoubleWorker(pszValue, ppszNext, cchMax, RET_TYPE_DOUBLE, &u);
    if (prd)
        *prd = u.rd.rd;
    return rc;
}


RTDECL(int) RTStrToFloatEx(const char *pszValue, char **ppszNext, size_t cchMax, float *pr)
{
    FLOATUNION u;
    int rc = rtStrToLongDoubleWorker(pszValue, ppszNext, cchMax, RET_TYPE_FLOAT, &u);
    if (pr)
        *pr = u.r.r;
    return rc;
}

