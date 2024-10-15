/* $Id: nt-structures.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Header for NT structures.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_nt_nt_structures_h
#define IPRT_INCLUDED_nt_nt_structures_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#include <iprt/types.h>


/** @name NT Kernel Structures
 * @{ */
typedef struct KTRAP_FRAME_AMD64
{
    uint64_t            P1Home;             /**< 0x00 */
    uint64_t            P2Home;             /**< 0x08 */
    uint64_t            P3Home;             /**< 0x10 */
    uint64_t            P4Home;             /**< 0x18 */
    uint64_t            P5;                 /**< 0x20 */
    uint8_t             PreviousMode;       /**< 0x28: KPROCESSOR_MODE / MODE - unused? */
    uint8_t             PreviousIrql;       /**< 0x29: KIRQL - Interrupts? */
    uint8_t             FaultIndicator;     /**< 0x2a: Holds (ErrCd >> 1) & 9) for \#PF. */
    uint8_t             ExceptionActive;    /**< 0x2b: 0 if interrupt, 1 if exception, 2 if service call,  */
    uint32_t            MxCsr;              /**< 0x2c */
    /** @name Volatile general register state. Only saved on interrupts and exceptions.
     * @{ */
    uint64_t            Rax;                /**< 0x30 */
    uint64_t            Rcx;                /**< 0x38 */
    uint64_t            Rdx;                /**< 0x40 */
    uint64_t            R8;                 /**< 0x48 */
    uint64_t            R9;                 /**< 0x50 */
    uint64_t            R10;                /**< 0x58 */
    uint64_t            R11;                /**< 0x60 */
    /** @} */
    uint64_t            GsBaseOrSwap;       /**< 0x68: GsBase if previous mode is kernel, GsSwap if pervious mode was user. */
    /** @name Volatile SSE state. Only saved on interrupts and exceptions.
     * @{ */
    RTUINT128U          Xmm0;               /**< 0x70 */
    RTUINT128U          Xmm1;               /**< 0x80: RBP points here.  */
    RTUINT128U          Xmm2;               /**< 0x90 */
    RTUINT128U          Xmm3;               /**< 0xa0 */
    RTUINT128U          Xmm4;               /**< 0xb0 */
    RTUINT128U          Xmm5;               /**< 0xc0 */
    /** @} */
    uint64_t            FaultAddrOrCtxRecOrTS; /**< 0xd0: Used to save CR2 in \#PF and NMI handlers. */
    /** @name Usermode debug state.
     * @{ */
    uint64_t            Dr0;                /**< 0xd8: Only if DR7 indicates active. */
    uint64_t            Dr1;                /**< 0xe0: Only if DR7 indicates active. */
    uint64_t            Dr2;                /**< 0xe8: Only if DR7 indicates active. */
    uint64_t            Dr3;                /**< 0xf0: Only if DR7 indicates active. */
    uint64_t            Dr6;                /**< 0xf8: Only if DR7 indicates active. */
    uint64_t            Dr7;                /**< 0x100: Considered active any of these bits are set:
                                                        X86_DR7_LE_ALL | X86_DR7_LE | X86_DR7_GE. */
    union
    {
        struct
        {
            uint64_t    LastBranchControl;  /**< 0x108 */
            uint32_t    LastBranchMSR;      /**< 0x110 */
        } amd;
        struct
        {
            uint64_t    DebugControl;       /**< 0x108 */
            uint64_t    LastBranchToRip;    /**< 0x110 */
            uint64_t    LastBranchFromRip;  /**< 0x118 */
            uint64_t    LastExceptionToRip; /**< 0x120 */
            uint64_t    LastExceptionFromRip; /**< 0x128 */
        } intel;
    } u;
    /** @} */
    /** @name Segment registers. Not sure when these would actually be used.
     * @{ */
    uint16_t            SegDs;              /**< 0x130 */
    uint16_t            SegEs;              /**< 0x132 */
    uint16_t            SegFs;              /**< 0x134 */
    uint16_t            SegGs;              /**< 0x136 */
    /** @} */
    uint64_t            TrapFrame;          /**< 0x138 */
    /** @name Some non-volatile registers only saved in service calls.
     * @{ */
    uint64_t            Rbx;                /**< 0x140 */
    uint64_t            Rdi;                /**< 0x148 */
    uint64_t            Rsi;                /**< 0x150 */
    /** @} */
    uint64_t            Rbp;                /**< 0x158: Typically restored by: MOV RBP, [RBP + 0xd8] */
    uint64_t            ErrCdOrXcptFrameOrS; /**< 0x160 */
    uint64_t            Rip;                /**< 0x168 - IRET RIP */
    uint16_t            SegCs;              /**< 0x170 - IRET CS */
    uint8_t             Fill0;              /**< 0x172 */
    uint8_t             Logging;            /**< 0x173 */
    uint16_t            Fill1[2];           /**< 0x174 */
    uint32_t            EFlags;             /**< 0x178 - IRET EFLAGS - Uninitialized for stack switching/growth code path. */
    uint32_t            Fill2;              /**< 0x17c */
    uint64_t            Rsp;                /**< 0x180 - IRET RSP */
    uint16_t            SegSs;              /**< 0x188 - IRET SS */
    uint16_t            Fill3;              /**< 0x18a */
    uint32_t            Fill4;              /**< 0x18c */
} KTRAP_FRAME_AMD64;
AssertCompileSize(KTRAP_FRAME_AMD64, 0x190);
/** Pointer to an AMD64 NT trap frame. */
typedef KTRAP_FRAME_AMD64 *PKTRAP_FRAME_AMD64;
/** Pointer to a const AMD64 NT trap frame. */
typedef KTRAP_FRAME_AMD64 const *PCKTRAP_FRAME_AMD64;

/** @} */


#endif /* !IPRT_INCLUDED_nt_nt_structures_h */

