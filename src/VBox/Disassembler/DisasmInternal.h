/* $Id: DisasmInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox disassembler - Internal header.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VBOX_INCLUDED_SRC_DisasmInternal_h
#define VBOX_INCLUDED_SRC_DisasmInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/dis.h>
#include <VBox/log.h>

#include <iprt/param.h>


/** @defgroup grp_dis_int Internals.
 * @ingroup grp_dis
 * @{
 */

/** This must be less or equal to DISSTATE::Instr.ab.
 * See Vol3A/Table 6-2 and Vol3B/Section22.25 for instance.  */
#define DIS_MAX_INSTR_LENGTH    15

/** Whether we can do unaligned access. */
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
# define DIS_HOST_UNALIGNED_ACCESS_OK
#endif


/** @def OP
 * Wrapper which initializes an DISOPCODE.
 * We must use this so that we can exclude unused fields in order
 * to save precious bytes in the GC version.
 *
 * @internal
 */
#if DISOPCODE_FORMAT == 0
# define OP(pszOpcode, idxParse1, idxParse2, idxParse3, opcode, param1, param2, param3, optype) \
    { pszOpcode, idxParse1, idxParse2, idxParse3, 0, opcode, param1, param2, param3, 0, 0, optype }
# define OPVEX(pszOpcode, idxParse1, idxParse2, idxParse3, idxParse4, opcode, param1, param2, param3, param4, optype) \
    { pszOpcode, idxParse1, idxParse2, idxParse3, idxParse4, opcode, param1, param2, param3, param4, 0, optype | DISOPTYPE_X86_SSE }

#elif DISOPCODE_FORMAT == 16
# define OP(pszOpcode, idxParse1, idxParse2, idxParse3, opcode, param1, param2, param3, optype) \
    { optype,                 opcode, idxParse1, idxParse2, param1, param2, idxParse3, param3, 0,      0         }
# define OPVEX(pszOpcode, idxParse1, idxParse2, idxParse3, idxParse4, opcode, param1, param2, param3, param4, optype) \
    { optype | DISOPTYPE_X86_SSE, opcode, idxParse1, idxParse2, param1, param2, idxParse3, param3, param4, idxParse4 }

#elif DISOPCODE_FORMAT == 15
# define OP(pszOpcode, idxParse1, idxParse2, idxParse3, opcode, param1, param2, param3, optype) \
    { opcode, idxParse1, idxParse2, idxParse3, param1, param2, param3, optype,                 0,      0         }
# define OPVEX(pszOpcode, idxParse1, idxParse2, idxParse3, idxParse4, opcode, param1, param2, param3, param4, optype) \
    { opcode, idxParse1, idxParse2, idxParse3, param1, param2, param3, optype | DISOPTYPE_X86_SSE, param4, idxParse4 }
#else
# error Unsupported DISOPCODE_FORMAT value
#endif


/* Common */
DECLHIDDEN(void)     disReadMore(PDISSTATE pDis, uint8_t offInstr, uint8_t cbMin);
DECLHIDDEN(uint8_t)  disReadByteSlow(PDISSTATE pDis, size_t offInstr);
DECLHIDDEN(uint16_t) disReadWordSlow(PDISSTATE pDis, size_t offInstr);
DECLHIDDEN(uint32_t) disReadDWordSlow(PDISSTATE pDis, size_t offInstr);
DECLHIDDEN(uint64_t) disReadQWordSlow(PDISSTATE pDis, size_t offInstr);


/**
 * Read a byte (8-bit) instruction.
 *
 * @returns The requested byte.
 * @param   pDis                The disassembler state.
 * @param   uAddress            The address.
 */
DECL_FORCE_INLINE(uint8_t) disReadByte(PDISSTATE pDis, size_t offInstr)
{
    if (offInstr >= pDis->cbCachedInstr)
        return disReadByteSlow(pDis, offInstr);
    return pDis->Instr.ab[offInstr];
}


/**
 * Read a word (16-bit) instruction.
 *
 * @returns The requested word.
 * @param   pDis                The disassembler state.
 * @param   offInstr            The offset of the qword relative to the
 *                              instruction.
 */
DECL_FORCE_INLINE(uint16_t) disReadWord(PDISSTATE pDis, size_t offInstr)
{
    if (offInstr + 2 > pDis->cbCachedInstr)
        return disReadWordSlow(pDis, offInstr);

#ifdef DIS_HOST_UNALIGNED_ACCESS_OK
    return *(uint16_t const *)&pDis->Instr.ab[offInstr];
#else
    return RT_MAKE_U16(pDis->Instr.ab[offInstr], pDis->Instr.ab[offInstr + 1]);
#endif
}


/**
 * Read a dword (32-bit) instruction.
 *
 * @returns The requested dword.
 * @param   pDis                The disassembler state.
 * @param   offInstr            The offset of the qword relative to the
 *                              instruction.
 */
DECL_FORCE_INLINE(uint32_t) disReadDWord(PDISSTATE pDis, size_t offInstr)
{
    if (offInstr + 4 > pDis->cbCachedInstr)
        return disReadDWordSlow(pDis, offInstr);

#ifdef DIS_HOST_UNALIGNED_ACCESS_OK
    return *(uint32_t const *)&pDis->Instr.ab[offInstr];
#else
    return RT_MAKE_U32_FROM_U8(pDis->Instr.ab[offInstr    ], pDis->Instr.ab[offInstr + 1],
                               pDis->Instr.ab[offInstr + 2], pDis->Instr.ab[offInstr + 3]);
#endif
}


/**
 * Read a qword (64-bit) instruction.
 *
 * @returns The requested qword.
 * @param   pDis                The disassembler state.
 * @param   uAddress            The address.
 */
DECL_FORCE_INLINE(uint64_t) disReadQWord(PDISSTATE pDis, size_t offInstr)
{
    if (offInstr + 8 > pDis->cbCachedInstr)
        return disReadQWordSlow(pDis, offInstr);

#ifdef DIS_HOST_UNALIGNED_ACCESS_OK
    return *(uint64_t const *)&pDis->Instr.ab[offInstr];
#else
    return RT_MAKE_U64_FROM_U8(pDis->Instr.ab[offInstr    ], pDis->Instr.ab[offInstr + 1],
                               pDis->Instr.ab[offInstr + 2], pDis->Instr.ab[offInstr + 3],
                               pDis->Instr.ab[offInstr + 4], pDis->Instr.ab[offInstr + 5],
                               pDis->Instr.ab[offInstr + 6], pDis->Instr.ab[offInstr + 7]);
#endif
}


/**
 * Reads some bytes into the cache.
 *
 * While this will set DISSTATE::rc on failure, the caller should disregard
 * this since that is what would happen if we didn't prefetch bytes prior to the
 * instruction parsing.
 *
 * @param   pDis                The disassembler state.
 */
DECL_FORCE_INLINE(void) disPrefetchBytes(PDISSTATE pDis)
{
    /*
     * Read some bytes into the cache.  (If this fail we continue as nothing
     * has gone wrong since this is what would happen if we didn't precharge
     * the cache here.)
     */
    int rc = pDis->pfnReadBytes(pDis, 0, 1, sizeof(pDis->Instr.ab));
    if (RT_SUCCESS(rc))
    {
        Assert(pDis->cbCachedInstr >= 1);
        Assert(pDis->cbCachedInstr <= sizeof(pDis->Instr.ab));
    }
    else
    {
        Log(("Initial read failed with rc=%Rrc!!\n", rc));
        pDis->rc = rc;
    }
}


#if defined(VBOX_DIS_WITH_X86_AMD64)
DECLHIDDEN(PCDISOPCODE) disInitializeStateX86(PDISSTATE pDis, DISCPUMODE enmCpuMode, uint32_t fFilter);
DECLHIDDEN(int)         disInstrWorkerX86(PDISSTATE pDis, PCDISOPCODE paOneByteMap, uint32_t *pcbInstr);
#endif
#if defined(VBOX_DIS_WITH_ARMV8)
DECLHIDDEN(PCDISOPCODE) disInitializeStateArmV8(PDISSTATE pDis, DISCPUMODE enmCpuMode, uint32_t fFilter);
DECLHIDDEN(int)         disInstrWorkerArmV8(PDISSTATE pDis, PCDISOPCODE paOneByteMap, uint32_t *pcbInstr);
#endif

size_t disFormatBytes(PCDISSTATE pDis, char *pszDst, size_t cchDst, uint32_t fFlags);

/** @} */
#endif /* !VBOX_INCLUDED_SRC_DisasmInternal_h */

