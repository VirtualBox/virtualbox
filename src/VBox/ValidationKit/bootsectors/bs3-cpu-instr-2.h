/* $Id: bs3-cpu-instr-2.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - bs3-cpu-instr-2, common header file.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_bootsectors_bs3_cpu_instr_2_h
#define VBOX_INCLUDED_SRC_bootsectors_bs3_cpu_instr_2_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#pragma pack(1)

/* binary: */

typedef struct BS3CPUINSTR2BIN8
{
    uint8_t uSrc1, uSrc2, uResult;
    uint16_t fEflOut;
} BS3CPUINSTR2BIN8;
typedef BS3CPUINSTR2BIN8 const RT_FAR *PCBS3CPUINSTR2BIN8;

typedef struct BS3CPUINSTR2BIN16
{
    uint16_t uSrc1, uSrc2, uResult;
    uint16_t fEflOut;
} BS3CPUINSTR2BIN16;
typedef BS3CPUINSTR2BIN16 const RT_FAR *PCBS3CPUINSTR2BIN16;

typedef struct BS3CPUINSTR2BIN32
{
    uint32_t uSrc1, uSrc2, uResult;
    uint16_t fEflOut;
} BS3CPUINSTR2BIN32;
typedef BS3CPUINSTR2BIN32 const RT_FAR *PCBS3CPUINSTR2BIN32;

typedef struct BS3CPUINSTR2BIN64
{
    uint64_t uSrc1, uSrc2, uResult;
    uint16_t fEflOut;
} BS3CPUINSTR2BIN64;
typedef BS3CPUINSTR2BIN64 const RT_FAR *PCBS3CPUINSTR2BIN64;

/** Using unused EFLAGS bit 3 for CF input value for ADC, SBB and such. */
#define BS3CPUINSTR2BIN_EFL_CARRY_IN_BIT    3


/* shifting: */

typedef struct BS3CPUINSTR2SHIFT8
{
    uint8_t  uSrc1;
    uint8_t  uSrc2;
    uint16_t fEflIn;
    uint8_t  uResult;
    uint16_t fEflOut;
} BS3CPUINSTR2SHIFT8;
typedef BS3CPUINSTR2SHIFT8 const RT_FAR *PCBS3CPUINSTR2SHIFT8;

typedef struct BS3CPUINSTR2SHIFT16
{
    uint16_t uSrc1;
    uint8_t  uSrc2;
    uint16_t fEflIn;
    uint16_t uResult;
    uint16_t fEflOut;
} BS3CPUINSTR2SHIFT16;
typedef BS3CPUINSTR2SHIFT16 const RT_FAR *PCBS3CPUINSTR2SHIFT16;

typedef struct BS3CPUINSTR2SHIFT32
{
    uint32_t uSrc1;
    uint8_t  uSrc2;
    uint16_t fEflIn;
    uint32_t uResult;
    uint16_t fEflOut;
} BS3CPUINSTR2SHIFT32;
typedef BS3CPUINSTR2SHIFT32 const RT_FAR *PCBS3CPUINSTR2SHIFT32;

typedef struct BS3CPUINSTR2SHIFT64
{
    uint64_t uSrc1;
    uint8_t  uSrc2;
    uint16_t fEflIn;
    uint64_t uResult;
    uint16_t fEflOut;
} BS3CPUINSTR2SHIFT64;
typedef BS3CPUINSTR2SHIFT64 const RT_FAR *PCBS3CPUINSTR2SHIFT64;

#pragma pack()

/** Using unused EFLAGS bit 3 for alternative OF value for reg,Ib.  */
#define BS3CPUINSTR2SHIFT_EFL_IB_OVERFLOW_OUT_BIT 3

#endif /* !VBOX_INCLUDED_SRC_bootsectors_bs3_cpu_instr_2_h */

