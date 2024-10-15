/* $Id: extF80_sincos.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SoftFloat - VBox Extension - extF80_sin, extF80_cos, extF80_sincos, extF80_atan2.
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
#include <stdbool.h>
#include <stdint.h>
#include "platform.h"
#include "internals.h"
#include "specialize.h"
#include "softfloat.h"
#include <iprt/types.h>
#include <iprt/x86.h>

#include "extF80_sincos.h"


static void cordic_sincos( float128_t z, float128_t *pv1, float128_t *pv2 SOFTFLOAT_STATE_DECL_COMMA )
{
    float128_t v1 = { { 0, 0 } }; /* MSC thinks it can be used uninitialized */
    float128_t v2 = { { 0, 0 } }; /* MSC thinks it can be used uninitialized */
    /** @todo TBD: CORDIC kernel should be easily implemented in assembly *   */

    float128_t x1 = ui32_to_f128(1, pState);
    float128_t x2 = ui32_to_f128(0, pState);
    float128_t zz = ui32_to_f128(0, pState);

    float128_t p2m = ui32_to_f128(1, pState);
    float128_t two = ui32_to_f128(2, pState);

    for (unsigned k = 0; k < RT_ELEMENTS(g_ar128FsincosCORDICConsts); k++)
    {
        float128_t atg   = *(float128_t *)&g_ar128FsincosCORDICConsts[k];
        float128_t scale = *(float128_t *)&g_ar128FsincosCORDICConsts2[k];

        float128_t px1 = f128_mul(x1, p2m, pState);
        float128_t px2 = f128_mul(x2, p2m, pState);

        if (f128_le(zz, z, pState))
        {
            x1 = f128_sub(x1, px2, pState);
            x2 = f128_add(x2, px1, pState);
            zz = f128_add(zz, atg, pState);
        }
        else
        {
            x1 = f128_add(x1, px2, pState);
            x2 = f128_sub(x2, px1, pState);
            zz = f128_sub(zz, atg, pState);
        }

        p2m = f128_div(p2m, two, pState);

        v1 = f128_mul(x1, scale, pState);
        v2 = f128_mul(x2, scale, pState);
    }

    *pv1 = v1;
    *pv2 = v2;
}

static float128_t cordic_atan2( float128_t y, float128_t x SOFTFLOAT_STATE_DECL_COMMA )
{
    float128_t v1 = { { 0, 0 } }; /* MSC thinks it can be used uninitialized */
    float128_t v2 = { { 0, 0 } }; /* MSC thinks it can be used uninitialized */
    /** @todo TBD: CORDIC kernel should be easily implemented in assembly *   */

    float128_t x1 = x, x2 = y;
    float128_t z = ui32_to_f128(0, pState);
    float128_t zero = ui32_to_f128(0, pState);
    float128_t p2m = ui32_to_f128(1, pState);
    float128_t two = ui32_to_f128(2, pState);

    for (unsigned k = 0; k < RT_ELEMENTS(g_ar128FsincosCORDICConsts); k++)
    {
        float128_t atg   = *(float128_t *)&g_ar128FsincosCORDICConsts[k];
        float128_t scale = *(float128_t *)&g_ar128FsincosCORDICConsts2[k];

        float128_t px1 = f128_mul(x1, p2m, pState);
        float128_t px2 = f128_mul(x2, p2m, pState);

        if (f128_le(x2, zero, pState))
        {
            x1 = f128_sub(x1, px2, pState);
            x2 = f128_add(x2, px1, pState);
            z = f128_sub(z, atg, pState);
        }
        else
        {
            x1 = f128_add(x1, px2, pState);
            x2 = f128_sub(x2, px1, pState);
            z = f128_add(z, atg, pState);
        }

        p2m = f128_div(p2m, two, pState);

        v1 = f128_mul(x1, scale, pState);
        v2 = f128_mul(x2, scale, pState);
    }

    return z;
}

extFloat80_t extF80_sin( extFloat80_t x SOFTFLOAT_STATE_DECL_COMMA )
{
    int32_t fSign = 0;
    extFloat80_t f80zero = ui32_to_extF80(0, pState);
    if (extF80_le(x, f80zero, pState))
    {
        x = extF80_sub(f80zero, x, pState);
        fSign = 1;
    }

    extFloat80_t f80pi2 = f128_to_extF80(*(float128_t *)&g_r128pi2, pState);

    /** @todo TBD: Partial remainder should be calculated using float128 value of pi2 to increase precision **/
    uint16_t fCxFlags = 0;
    extFloat80_t rem = extF80_partialRem(x, f80pi2, pState->roundingMode, &fCxFlags, pState);
    int32_t const quo = X86_FSW_CX_TO_QUOTIENT(fCxFlags);

    float128_t z = extF80_to_f128(rem, pState);
    float128_t f128zero = ui32_to_f128(0, pState);

    float128_t v1, v2;
    cordic_sincos(z, &v1, &v2, pState);

    float128_t v;
    switch(quo % 4)
    {
#ifdef _MSC_VER /* stupid MSC thinks v might be used uninitialized otherwise: */
        default:
#endif
        case 0:
            v = v2;
            break;

        case 1:
            v = v1;
            break;

        case 2:
            v = f128_sub(f128zero, v2, pState);
            break;

        case 3:
            v = f128_sub(f128zero, v1, pState);
            break;
    }

    if (fSign)
        v = f128_sub(f128zero, v, pState);

    return f128_to_extF80(v, pState);
}

extFloat80_t extF80_cos( extFloat80_t x SOFTFLOAT_STATE_DECL_COMMA )
{
    extFloat80_t f80zero = ui32_to_extF80(0, pState);
    if (extF80_le(x, f80zero, pState))
        x = extF80_sub(f80zero, x, pState);

    extFloat80_t f80pi2 = f128_to_extF80(*(float128_t *)&g_r128pi2, pState);

    /** TBD: Partial remainder should be calculated using float128 value of pi2 to increase precision **/
    uint16_t fCxFlags = 0;
    extFloat80_t rem = extF80_partialRem(x, f80pi2, pState->roundingMode, &fCxFlags, pState);
    int32_t const quo = X86_FSW_CX_TO_QUOTIENT(fCxFlags);

    float128_t z = extF80_to_f128(rem, pState);
    float128_t f128zero = ui32_to_f128(0, pState);

    float128_t v1, v2;
    cordic_sincos(z, &v1, &v2, pState);

    float128_t v;
    switch(quo % 4)
    {
#ifdef _MSC_VER /* stupid MSC thinks v might be used uninitialized otherwise: */
        default:
#endif
        case 0:
            v = v1;
            break;

        case 1:
            v = f128_sub(f128zero, v2, pState);;
            break;

        case 2:
            v = f128_sub(f128zero, v1, pState);
            break;

        case 3:
            v = v2;
            break;
    }

    return f128_to_extF80(v, pState);
}

void extF80_sincos( extFloat80_t x, extFloat80_t* pSin, extFloat80_t* pCos SOFTFLOAT_STATE_DECL_COMMA )
{
    uint16_t fCxFlags = 0;
    int32_t quo;
    extFloat80_t rem, f80pi2, f80zero;
    int32_t fSign = 0;

    f80zero = ui32_to_extF80(0, pState);
    if (extF80_le(x, f80zero, pState))
    {
        x = extF80_sub(f80zero, x, pState);
        fSign = 1;
    }

    f80pi2 = f128_to_extF80(*(float128_t const *)&g_r128pi2, pState);

    /** @todo TBD: Partial remainder should be calculated using float128 value of pi2 to increase precision **/
    rem = extF80_partialRem(x, f80pi2, pState->roundingMode, &fCxFlags, pState);
    quo = X86_FSW_CX_TO_QUOTIENT(fCxFlags);

    float128_t z = extF80_to_f128(rem, pState);
    float128_t f128zero = ui32_to_f128(0, pState);

    float128_t v1, v2;
    cordic_sincos(z, &v1, &v2, pState);

    float128_t vCos, vSin;
    switch(quo % 4)
    {
#ifdef _MSC_VER /* stupid MSC thinks vCos & cSin might be used uninitialized otherwise: */
        default:
#endif
        case 0:
            vCos = v1;
            vSin = v2;
            break;

        case 1:
            vCos = f128_sub(f128zero, v2, pState);
            vSin = v1;
            break;

        case 2:
            vCos = f128_sub(f128zero, v1, pState);
            vSin = f128_sub(f128zero, v2, pState);
            break;

        case 3:
            vCos = v2;
            vSin = f128_sub(f128zero, v1, pState);
            break;
    }

    if (fSign)
        vSin = f128_sub(f128zero, vSin, pState);

    *pCos = f128_to_extF80(vCos, pState);
    *pSin = f128_to_extF80(vSin, pState);
}

extFloat80_t extF80_atan2( extFloat80_t f80y, extFloat80_t f80x SOFTFLOAT_STATE_DECL_COMMA )
{
    float128_t v;
    int32_t fSignX = 0, fSignY = 0;
    float128_t f128zero = ui32_to_f128(0, pState);
    float128_t y = extF80_to_f128(f80y, pState);
    float128_t x = extF80_to_f128(f80x, pState);

    if (f128_le(x, f128zero, pState))
    {
        x = f128_sub(f128zero, x, pState);
        fSignX = 1;
    }

    if (f128_le(y, f128zero, pState))
    {
        y = f128_sub(f128zero, y, pState);
        fSignY = 1;
    }

    v = cordic_atan2(y, x, pState);

    if (fSignX)
    {
        if (fSignY)
            v = f128_sub(v, *(float128_t const *)&g_r128pi, pState);
        else
            v = f128_sub(*(float128_t const *)&g_r128pi, v, pState);
    }
    else
    {
        if (fSignY)
            v = f128_sub(f128zero, v, pState);
    }

    return f128_to_extF80(v, pState);
}
