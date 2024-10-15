/* $Id: IEMGenFpuConstants.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEMGenFpuConstants - Generates FPU constants for IEMAllAImplC.cpp.
 *
 * Compile on linux: gcc -I../../../../include -DIN_RING3 IEMGenFpuConstants.c -lmpfr -g -o IEMGenFpuConstants
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
#include <iprt/types.h>
#include <iprt/assertcompile.h>
#include <stdio.h>
#define MPFR_WANT_FLOAT128
#include <gmp.h>
#include <mpfr.h>


void PrintComment(const char *pszComment, va_list va, mpfr_srcptr pVal, bool fList)
{
    const char * const pszIndent = fList ? "    " : "";
    printf(fList ? "    /* " : "/** ");
    vprintf(pszComment, va);
    printf("\n%s * base-10: ", pszIndent);
    mpfr_out_str(stdout, 10, 0, pVal, MPFR_RNDD);
    printf("\n%s * base-16: ", pszIndent);
    mpfr_out_str(stdout, 16, 0, pVal, MPFR_RNDD);
    printf("\n%s * base-2 : ", pszIndent);
    mpfr_out_str(stdout, 2, 0, pVal, MPFR_RNDD);
    printf(" */\n");
}


uint64_t BinStrToU64(const char *psz, size_t cch)
{
    uint64_t u = 0;
    while (cch-- > 0)
    {
        u <<= 1;
        u |= *psz++ == '1';
    }
    return u;
}


void PrintU128(mpfr_srcptr pVal, const char *pszVariable, const char *pszComment, ...)
{
    va_list va;
    va_start(va, pszComment);
    PrintComment(pszComment, va, pVal, !pszVariable);
    va_end(va);
    if (pszVariable)
        printf("const RTUINT128U %s = ", pszVariable);
    else
        printf("    ");
    mpfr_exp_t iExpBinary;
    char *pszBinary = mpfr_get_str(NULL, &iExpBinary, 2, 0, pVal, MPFR_RNDD);
    printf("RTUINT128_INIT_C(%#llx, %#llx)%s\n",
           BinStrToU64(pszBinary, 64), BinStrToU64(&pszBinary[64], 64), pszVariable ? ";" : ",");
    mpfr_free_str(pszBinary);
}


void PrintF128(mpfr_srcptr pVal, const char *pszVariable, const char *pszComment, ...)
{
    RTFLOAT128U r128;
    *(_Float128 *)&r128 = mpfr_get_float128(pVal, MPFR_RNDD);

    va_list va;
    va_start(va, pszComment);
    PrintComment(pszComment, va, pVal, !pszVariable);
    va_end(va);
    if (pszVariable)
        printf("const RTFLOAT128U %s = ", pszVariable);
    else
        printf("    ");
    printf("RTFLOAT128U_INIT_C(%d, 0x%012llx, 0x%016llx, 0x%04x)%s\n",
           r128.s.fSign, r128.s64.uFractionHi, r128.s64.uFractionLo, r128.s64.uExponent, pszVariable ? ";" : ",");
}


int main(void)
{
    mpfr_t Val;

    mpfr_init2(Val, 112 + 1);
    mpfr_const_log2(Val, MPFR_RNDN);
    PrintF128(Val, "g_r128Ln2", "The ln2 constant as 128-bit floating point value.");

    mpfr_init2(Val, 128);
    mpfr_const_log2(Val, MPFR_RNDN);
    PrintU128(Val, "g_u128Ln2Mantissa", "High precision ln2 value.");

    mpfr_t Val2;
    mpfr_init2(Val2, 67);
    mpfr_const_log2(Val2, MPFR_RNDN);
    mpfr_set(Val, Val2, MPFR_RNDN);
    PrintU128(Val, "g_u128Ln2MantissaIntel", "High precision ln2 value, compatible with f2xm1 results on intel 10980XE.");

    /** @todo emit constants with 68-bit precision (1+67 bits), as that's what we
     *        use for intel now. */
    printf("\n"
           "/** Horner constants for f2xm1 */\n"
           "const RTFLOAT128U g_ar128F2xm1HornerConsts[] =\n"
           "{\n");
    mpfr_t One;
    mpfr_init2(One, 112 + 1);
    mpfr_set_ui(One, 1, MPFR_RNDD);
    PrintF128(One, NULL, "a0");

    mpfr_init2(Val, 112 + 1);
    mpfr_set_ui(Val, 1, MPFR_RNDD);
    for (unsigned a = 1; a < 22; a++)
    {
        mpfr_div_ui(Val, Val, a + 1, MPFR_RNDD);
        PrintF128(Val, NULL, "a%u", a);
    }

    printf("};\n");

    mpfr_init2(Val, 112 + 1);
    mpfr_const_pi(Val, MPFR_RNDN);
    PrintF128(Val, "g_r128pi", "The pi constant as 128-bit floating point value.");
    mpfr_div_ui(Val, Val, 2, MPFR_RNDD);
    PrintF128(Val, "g_r128pi2", "The pi/2 constant as 128-bit floating point value.");

    printf("\n"
           "/** CORDIC constants for fsin and fcos, defined by c(i)=atan(2^(-i)) */\n"
           "const RTFLOAT128U g_ar128FsincosCORDICConsts[] =\n"
           "{\n");
    mpfr_init2(Val, 112 + 1);
    signed kmax = 68;

    for (signed k = 0; k < kmax; k++)
    {
        // mpfr_mul_2si ?
        mpfr_set_si_2exp(Val, 1, -k, MPFR_RNDD);
        mpfr_atan(Val, Val, MPFR_RNDD);
        PrintF128(Val, NULL, "c%u", k);
    }

    printf("};\n");

    printf("\n"
           "/** CORDIC multipliers for fsin and fcos, defined by K(i)=1/sqrt(1+2^(-2i)) */\n"
           "const RTFLOAT128U g_ar128FsincosCORDICConsts2[] =\n"
           "{\n");

    mpfr_init2(Val, 112 + 1);
    mpfr_init2(Val2, 112 + 1);

    mpfr_set_ui(Val, 2, MPFR_RNDD);
    mpfr_sqrt(Val, Val, MPFR_RNDD);
    mpfr_ui_div(Val2, 1, Val, MPFR_RNDD);
    PrintF128(Val2, NULL, "K_%u", 0);

    for (signed k = 1; k < kmax; k++)
    {
        mpfr_set_si_2exp(Val, 1, -2 * k, MPFR_RNDD);
        mpfr_add_ui(Val, Val, 1, MPFR_RNDD);
        mpfr_sqrt(Val, Val, MPFR_RNDD);
        mpfr_div(Val2, Val2, Val, MPFR_RNDD);
        PrintF128(Val2, NULL, "K_%u", k);
    }

    printf("};\n");

    printf("\n"
           "/** Chebyshev coeffs for log2 function in [1, 2] interval */\n"
           "const RTFLOAT128U g_ar128ChebLog2Consts[] =\n"
           "{\n");
    signed j, d, dmax = 22;
    mpfr_t ValX, ValXX, ValA, ValB, ValBmA, ValCos, ValSum;
    mpfr_init2(Val, 112 + 1);
    mpfr_init2(Val2, 112 + 1);
    mpfr_init2(ValX, 112 + 1);
    mpfr_init2(ValXX, 112 + 1);
    mpfr_init2(ValA, 112 + 1);
    mpfr_init2(ValB, 112 + 1);
    mpfr_init2(ValBmA, 112 + 1);
    mpfr_init2(ValCos, 112 + 1);
    mpfr_init2(ValSum, 112 + 1);

    /* Setting the desired interpolation range [1.0, 2.0] */
    mpfr_set_d(ValA, 1.0, MPFR_RNDD);
    mpfr_set_d(ValB, 2.0, MPFR_RNDD);
    mpfr_sub(ValBmA, ValB, ValA, MPFR_RNDD);

    for (signed d = 0; d < dmax; d++)
    {
        mpfr_set_si(ValSum, 0, MPFR_RNDD);

        for(j = 0; j < dmax; j++)
        {
            mpfr_set_si_2exp(Val, 1, -1, MPFR_RNDD);
            mpfr_add_ui(Val, Val, j, MPFR_RNDD);
            mpfr_const_pi(Val2, MPFR_RNDN);
            mpfr_mul(Val, Val2, Val, MPFR_RNDN);
            mpfr_div_si(Val, Val, dmax, MPFR_RNDN);
            /* Val = M_PIq * (j + 0.5Q) / N */

            mpfr_cos(ValX, Val, MPFR_RNDN);
            /* ValX = cos(M_PIq * (j + 0.5Q) / N) */

            mpfr_mul_si(Val, Val, d, MPFR_RNDN);
            mpfr_cos(ValCos, Val, MPFR_RNDN);
            /* ValCos = cos(M_PIq * d * (j + 0.5Q) / N) */

            mpfr_add_si(Val, ValX, 1, MPFR_RNDN);
            mpfr_div_si(Val, Val, 2, MPFR_RNDN);
            mpfr_mul(Val, ValBmA, Val, MPFR_RNDN);
            mpfr_add(ValXX, ValA, Val, MPFR_RNDN);
            /* ValXX = a + (b - a) * (x + 1.0Q) / 2.0Q */

            mpfr_sub_si(Val, ValXX, 1, MPFR_RNDN);
            mpfr_log2(Val2, ValXX, MPFR_RNDN);
            mpfr_div(Val, Val2, Val, MPFR_RNDN);
            mpfr_mul(Val, Val, ValCos, MPFR_RNDN);
            mpfr_add(ValSum, ValSum, Val, MPFR_RNDN);
        }

        mpfr_div_si(ValSum, ValSum, dmax, MPFR_RNDN);

        if (d != 0)
            mpfr_mul_si(ValSum, ValSum, 2, MPFR_RNDN);

        PrintF128(ValSum, NULL, "c%u", d);
    }

    printf("};\n");

    mpfr_init2(One, 112 + 1);
    mpfr_set_ui(One, 1, MPFR_RNDD);

    mpfr_init2(Val, 112 + 1);
    mpfr_exp(Val, One, MPFR_RNDD);
    mpfr_log2(Val, Val, MPFR_RNDD);

    PrintF128(Val, "g_r128Log2e", "The log2e constant as 128-bit floating point value.");

    mpfr_clear(ValXX);
    mpfr_clear(ValX);
    mpfr_clear(Val);
    mpfr_clear(Val2);
    mpfr_clear(One);
    mpfr_free_cache();
    return 0;
}

