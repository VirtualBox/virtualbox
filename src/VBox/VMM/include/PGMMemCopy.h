/* $Id: PGMMemCopy.h 114109 2026-05-08 22:10:30Z knut.osmundsen@oracle.com $ */
/** @file
 * PGM - Internal header file, safe memcpy replacement.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_PGMMemCopy_h
#define VMM_INCLUDED_SRC_include_PGMMemCopy_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/asm.h>
#include <iprt/string.h>


/**
 * Safe memcpy - large buffer case (128 bytes and larger).
 */
template <const bool a_fDstAligned, typename a_DstPtrUnionType, typename a_SrcPtrUnionType>
DECLINLINE(void) pgmPhysLargeMemCopy(a_DstPtrUnionType uDst, a_SrcPtrUnionType uSrc, size_t cb)
{
    Assert(cb >= 128);

    /*
     * 64-byte align the important buffer.
     */
    if ((a_fDstAligned ? uDst.u : uSrc.u) & 63)
    {
        if ((a_fDstAligned ? uDst.u : uSrc.u) & 1)
        {
            *uDst.pu8++  = *uSrc.pu8++;
            cb          -= 1;
        }
        if ((a_fDstAligned ? uDst.u : uSrc.u) & 2)
        {
            *uDst.pu16++ = *uSrc.pu16++;
            cb          -= 2;
        }
        if ((a_fDstAligned ? uDst.u : uSrc.u) & 4)
        {
            *uDst.pu32++ = *uSrc.pu32++;
            cb          -= 4;
        }
        Assert(!((a_fDstAligned ? uDst.u : uSrc.u) & 7));
        if ((a_fDstAligned ? uDst.u : uSrc.u) & 8)
        {
            *uDst.pu64++ = *uSrc.pu64++;
            cb          -= 8;
        }
        if ((a_fDstAligned ? uDst.u : uSrc.u) & 16)
        {
            uint64_t const uTmp0 = uSrc.pu64[0];
            uint64_t const uTmp1 = uSrc.pu64[1];
            ASMCompilerBarrier();
            uDst.pu64[0] = uTmp0;
            uDst.pu64[1] = uTmp1;
            uSrc.pu64 += 2;
            uDst.pu64 += 2;
            cb        -= 2*8;
        }
        if ((a_fDstAligned ? uDst.u : uSrc.u) & 32)
        {
            uint64_t const uTmp0 = uSrc.pu64[0];
            uint64_t const uTmp1 = uSrc.pu64[1];
            uint64_t const uTmp2 = uSrc.pu64[2];
            uint64_t const uTmp3 = uSrc.pu64[3];
            ASMCompilerBarrier();
            uDst.pu64[0] = uTmp0;
            uDst.pu64[1] = uTmp1;
            uDst.pu64[2] = uTmp2;
            uDst.pu64[3] = uTmp3;
            uSrc.pu64 += 4;
            uDst.pu64 += 4;
            cb        -= 4*8;
        }
    }
    Assert(!((a_fDstAligned ? uDst.u : uSrc.u) & 63));

    /*
     * Copy in chunks of 64 bytes.
     */
    while (cb >= 64)
    {
        uint64_t const uTmp0 = uSrc.pu64[0];
        uint64_t const uTmp1 = uSrc.pu64[1];
        uint64_t const uTmp2 = uSrc.pu64[2];
        uint64_t const uTmp3 = uSrc.pu64[3];
        uint64_t const uTmp4 = uSrc.pu64[4];
        uint64_t const uTmp5 = uSrc.pu64[5];
        uint64_t const uTmp6 = uSrc.pu64[6];
        uint64_t const uTmp7 = uSrc.pu64[7];
        ASMCompilerBarrier();
        uDst.pu64[0] = uTmp0;
        uDst.pu64[1] = uTmp1;
        uDst.pu64[2] = uTmp2;
        uDst.pu64[3] = uTmp3;
        uDst.pu64[4] = uTmp4;
        uDst.pu64[5] = uTmp5;
        uDst.pu64[6] = uTmp6;
        uDst.pu64[7] = uTmp7;
        uSrc.pu64 += 8;
        uDst.pu64 += 8;
        cb        -= 8*8;
    }

    /*
     * Deal with whatever is remaining.
     */
    if (cb > 0)
    {
        if (cb >= 32)
        {
            uint64_t const uTmp0 = uSrc.pu64[0];
            uint64_t const uTmp1 = uSrc.pu64[1];
            uint64_t const uTmp2 = uSrc.pu64[2];
            uint64_t const uTmp3 = uSrc.pu64[3];
            ASMCompilerBarrier();
            uDst.pu64[0] = uTmp0;
            uDst.pu64[1] = uTmp1;
            uDst.pu64[2] = uTmp2;
            uDst.pu64[3] = uTmp3;
            uSrc.pu64 += 4;
            uDst.pu64 += 4;
            cb        -= 4*8;
        }
        if (cb >= 16)
        {
            uint64_t const uTmp0 = uSrc.pu64[0];
            uint64_t const uTmp1 = uSrc.pu64[1];
            ASMCompilerBarrier();
            uDst.pu64[0] = uTmp0;
            uDst.pu64[1] = uTmp1;
            uSrc.pu64 += 2;
            uDst.pu64 += 2;
            cb        -= 2*8;
        }
        if (cb >= 8)
        {
            *uDst.pu64++ = *uSrc.pu64++;
            cb          -= 8;
        }
        if (cb >= 4)
        {
            *uDst.pu32++ = *uSrc.pu32++;
            cb          -= 4;
        }
        if (cb >= 2)
        {
            *uDst.pu16++ = *uSrc.pu16++;
            cb          -= 2;
        }
        if (cb >= 1)
        {
            *uDst.pu8++  = *uSrc.pu8++;
            cb          -= 1;
        }
        Assert(cb == 0);
    }
}


/**
 * Safe memcpy - small buffers (< 128 bytes) fallback for odd sizes.
 */
template <const bool a_fDstAligned, typename a_DstPtrUnionType, typename a_SrcPtrUnionType>
static void pgmPhysSmallMemCopyFallback(a_DstPtrUnionType uDst, a_SrcPtrUnionType uSrc, size_t cb)
{
    while (cb > 0)
    {
        if (cb >= 8 && ((a_fDstAligned ? uDst.u : uSrc.u) & 7) == 0)
        {
            *uDst.pu64++ = *uSrc.pu64++;
            cb          -= 8;
        }
        else if (cb >= 4 && ((a_fDstAligned ? uDst.u : uSrc.u) & 3) == 0)
        {
            *uDst.pu32++ = *uSrc.pu32++;
            cb          -= 4;
        }
        else if (cb >= 2 && ((a_fDstAligned ? uDst.u : uSrc.u) & 1) == 0)
        {
            *uDst.pu16++ = *uSrc.pu16++;
            cb          -= 2;
        }
        else
        {
            *uDst.pu8++ = *uSrc.pu8++;
            cb          -= 1;
        }
    }
}


/**
 * Safe memcpy - small buffers (< 128 bytes).
 */
template <const bool a_fDstAligned, typename a_DstPtrUnionType, typename a_SrcPtrUnionType>
DECLINLINE(void) pgmPhysSmallMemCopy(a_DstPtrUnionType uDst, a_SrcPtrUnionType uSrc, size_t cb)
{
    switch (cb)
    {
        case 1:
            *uDst.pu8 = *uSrc.pu8;
            return;
        case 2:
            *uDst.pu16 = *uSrc.pu16;
            return;
        case 4:
            *uDst.pu32 = *uSrc.pu32;
            return;
        case 8:
            *uDst.pu64 = *uSrc.pu64;
            return;
        case 16:
        {
            uint64_t const uTmp0 = uSrc.pu64[0];
            uint64_t const uTmp1 = uSrc.pu64[1];
            ASMCompilerBarrier();
            uDst.pu64[0] = uTmp0;
            uDst.pu64[1] = uTmp1;
            return;
        }
#if 1
        case 24:
        {
             uint64_t const uTmp0 = uSrc.pu64[0];
             uint64_t const uTmp1 = uSrc.pu64[1];
             uint64_t const uTmp2 = uSrc.pu64[2];
             ASMCompilerBarrier();
             uDst.pu64[0] = uTmp0;
             uDst.pu64[1] = uTmp1;
             uDst.pu64[2] = uTmp2;
             return;
        }
        case 32:
        {
            uint64_t const uTmp0 = uSrc.pu64[0];
            uint64_t const uTmp1 = uSrc.pu64[1];
            uint64_t const uTmp2 = uSrc.pu64[2];
            uint64_t const uTmp3 = uSrc.pu64[3];
            ASMCompilerBarrier();
            uDst.pu64[0] = uTmp0;
            uDst.pu64[1] = uTmp1;
            uDst.pu64[2] = uTmp2;
            uDst.pu64[3] = uTmp3;
            return;
        }
        case 48:
        {
            uint64_t const uTmp0 = uSrc.pu64[0];
            uint64_t const uTmp1 = uSrc.pu64[1];
            uint64_t const uTmp2 = uSrc.pu64[2];
            uint64_t const uTmp3 = uSrc.pu64[3];
            uint64_t const uTmp4 = uSrc.pu64[4];
            uint64_t const uTmp5 = uSrc.pu64[5];
            ASMCompilerBarrier();
            uDst.pu64[0] = uTmp0;
            uDst.pu64[1] = uTmp1;
            uDst.pu64[2] = uTmp2;
            uDst.pu64[3] = uTmp3;
            uDst.pu64[4] = uTmp4;
            uDst.pu64[5] = uTmp5;
            return;
        }
        case 64:
        {
            uint64_t const uTmp0 = uSrc.pu64[0];
            uint64_t const uTmp1 = uSrc.pu64[1];
            uint64_t const uTmp2 = uSrc.pu64[2];
            uint64_t const uTmp3 = uSrc.pu64[3];
            uint64_t const uTmp4 = uSrc.pu64[4];
            uint64_t const uTmp5 = uSrc.pu64[5];
            uint64_t const uTmp6 = uSrc.pu64[6];
            uint64_t const uTmp7 = uSrc.pu64[7];
            ASMCompilerBarrier();
            uDst.pu64[0] = uTmp0;
            uDst.pu64[1] = uTmp1;
            uDst.pu64[2] = uTmp2;
            uDst.pu64[3] = uTmp3;
            uDst.pu64[4] = uTmp4;
            uDst.pu64[5] = uTmp5;
            uDst.pu64[6] = uTmp6;
            uDst.pu64[7] = uTmp7;
            return;
        }

#endif
        default:
            pgmPhysSmallMemCopyFallback<a_fDstAligned, a_DstPtrUnionType, a_SrcPtrUnionType>(uDst, uSrc, cb);
            return;
        /* Odd stuff: */
#if 1
        case 3:
            if ((a_fDstAligned ? uDst.u : uSrc.u) & 1)
            {
                uint8_t const  uTmp0 = *uSrc.pu8++;
                uint16_t const uTmp1 = *uSrc.pu16;
                ASMCompilerBarrier();
                *uDst.pu8++  = uTmp0;
                *uDst.pu16   = uTmp1;
            }
            else
            {
                uint16_t const uTmp0 = *uSrc.pu16++;
                uint8_t const  uTmp1 = *uSrc.pu8;
                ASMCompilerBarrier();
                *uDst.pu16++ = uTmp0;
                *uDst.pu8    = uTmp1;
            }
            return;

        case 5:
            if ((a_fDstAligned ? uDst.u : uSrc.u) & 1)
            {
                uint8_t const  uTmp0 = *uSrc.pu8++;
                uint32_t const uTmp1 = *uSrc.pu32;
                ASMCompilerBarrier();
                *uDst.pu8++  = uTmp0;
                *uDst.pu32   = uTmp1;
            }
            else
            {
                uint32_t const uTmp0 = *uSrc.pu32++;
                uint8_t const  uTmp1 = *uSrc.pu8;
                ASMCompilerBarrier();
                *uDst.pu32++ = uTmp0;
                *uDst.pu8    = uTmp1;
            }
            return;

        case 6:
            if ((a_fDstAligned ? uDst.u : uSrc.u) & 3)
            {
                uint16_t const uTmp0 = *uSrc.pu16++;
                uint32_t const uTmp1 = *uSrc.pu32;
                ASMCompilerBarrier();
                *uDst.pu16++ = uTmp0;
                *uDst.pu32   = uTmp1;
            }
            else
            {
                uint32_t const uTmp0 = *uSrc.pu32++;
                uint16_t const uTmp1 = *uSrc.pu16;
                ASMCompilerBarrier();
                *uDst.pu32++ = uTmp0;
                *uDst.pu16   = uTmp1;
            }
            return;

        case 7:
            if ((a_fDstAligned ? uDst.u : uSrc.u) & 3)
            {
                uint8_t const  uTmp0 = *uSrc.pu8++;
                uint16_t const uTmp1 = *uSrc.pu16++;
                uint32_t const uTmp2 = *uSrc.pu32;
                ASMCompilerBarrier();
                *uDst.pu8++  = uTmp0;
                *uDst.pu16++ = uTmp1;
                *uDst.pu32   = uTmp2;
            }
            else
            {
                uint32_t const uTmp0 = *uSrc.pu32++;
                uint16_t const uTmp1 = *uSrc.pu16++;
                uint8_t  const uTmp2 = *uSrc.pu8;
                ASMCompilerBarrier();
                *uDst.pu32++ = uTmp0;
                *uDst.pu16++ = uTmp1;
                *uDst.pu8    = uTmp2;
            }
            return;

#endif
        case 0:
            AssertFailedReturnVoid();
    }
}


/**
 * Wraps memcpy to make the behaviour more predictable and uniform.
 *
 * Background:
 * memcpy(x, y, 2) could easily be split up into two interations of REP MOVSB.
 * This is bad if the memory is concurrently being used by someone else (i.e.
 * guest updates it while we read it byte-by-byte).
 *
 * The memcpy in recent glibc (like 2.42) will deliberatly do overlapping reads
 * and writes on amd64 when the size isn't an exact multiple of the size of the
 * registers involved.  For instance, for a two byte copy the equivalent of this
 * will be executed (rcx=cb=2, rsi=pvSrc, rdi=pvDst):
 * @code{.asm}
 *     mov     bl, [rsi]
 *     mov     dx, [rsi+rcx-2] ; rsi+2-2 => rsi
 *     mov     [rdi+rcx-2], dx ; rdi+2-2 => rdi
 *     mov     [rdi], bl
 * @endcode
 * Our problem is that we could be rescheduled between the two reads and run a
 * thread making updates to the values, so that when resuming on the 2nd
 * instruction it will see a different value.  The result will be that we'll
 * write one byte from the new value (byte 1) and one from the old (byte 0).
 *
 * Similarly, for any writes smaller than two SSE, AVX or AVX512 register-sizes
 * (AVX/AVX512 only if CPU supports), the values are read twice in an
 * overlapping fashion.  For instance, for a 24 byte copy the equivalent of this
 * will be executed (rcx=cb=24, rsi=pvSrc, rdi=pvDst):
 * @code{.asm}
 *     movups  xmm0, [rsi]
 *     movups  xmm1, [rsi+rcx-16] ; rsi+24-16 = rsi+8; overlapping bytes 15..8.
 *     movups  [rdi+rcx-16], xmm1 ; rdi+24-16 = rdi+8
 *     movups  [rdi], xmm0
 * @endcode
 * Bytes 15..8 are both in xmm0 and xmm1 and are read/written twice, creating a
 * potential consistency race should someone update or read the values while they
 * are being copied (imagine IRQs/preemption between each instruction).
 *
 * @note For performance reasons, this generic implementation will fall back on
 *       memcpy for operations 1 KiB or larger that is aligned on 1 KiB.
 *       This is to get good thruput when transfering pure data buffers (USB,
 *       disk, network, etc).  This is ASSUMING that memcpy won't do any double
 *       reading & writing for buffers with so well aligned sizes...
 */
template <bool const a_fIsRead>
static void pgmPhysMemCopyWrapper(void *pvDst, void const *pvSrc, size_t cb)
{
    if RT_CONSTEXPR_IF(a_fIsRead)
    {
        RTPTRUNION   uDst = { pvDst };
        RTCVPTRUNION uSrc = { (void const volatile *)pvSrc };
        if (cb < 128)
            pgmPhysSmallMemCopy<false, RTPTRUNION, RTCVPTRUNION>(uDst, uSrc, cb);
        else if (cb & (_1K - 1))
            pgmPhysLargeMemCopy<false, RTPTRUNION, RTCVPTRUNION>(uDst, uSrc, cb);
        else
            memcpy(uDst.pv, (void const *)uSrc.pv, cb);
    }
    else
    {
        RTVPTRUNION uDst = { (void volatile *)pvDst };
        RTCPTRUNION uSrc = { pvSrc };
        if (cb < 128)
            pgmPhysSmallMemCopy<true, RTVPTRUNION, RTCPTRUNION>(uDst, uSrc, cb);
        else if (cb & (_1K - 1))
            pgmPhysLargeMemCopy<true, RTVPTRUNION, RTCPTRUNION>(uDst, uSrc, cb);
        else
            memcpy((void *)uDst.pv, uSrc.pv, cb);
    }
}


#endif /* !VMM_INCLUDED_SRC_include_PGMMemCopy_h */

