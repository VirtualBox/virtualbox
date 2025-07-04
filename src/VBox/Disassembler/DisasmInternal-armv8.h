/* $Id: DisasmInternal-armv8.h 106830 2024-11-01 10:06:53Z alexander.eichner@oracle.com $ */
/** @file
 * VBox disassembler - Internal header.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_DisasmInternal_armv8_h
#define VBOX_INCLUDED_SRC_DisasmInternal_armv8_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/dis.h>
#include <VBox/log.h>

#include <iprt/param.h>
#include "DisasmInternal.h"


/** @addtogroup grp_dis_int Internals.
 * @ingroup grp_dis
 * @{
 */

/** @name Index into g_apfnFullDisasm.
 * @{ */
typedef enum DISPARMPARSEIDX
{
    kDisParmParseNop = 0,
    kDisParmParseSize,
    kDisParmParseImm,
    kDisParmParseImmRel,
    kDisParmParseImmAdr,
    kDisParmParseImmZero,
    kDisParmParseGprZr,
    kDisParmParseGprZr32,
    kDisParmParseGprZr64,
    kDisParmParseGprSp,
    kDisParmParseGprOff,
    kDisParmParseVecReg,
    kDisParmParseAddrGprSp,
    kDisParmParseRegFixed31,
    kDisParmParseGprCount,
    kDisParmParseImmsImmrN,
    kDisParmParseHw,
    kDisParmParseCond,
    kDisParmParsePState,
    kDisParmParseCRnCRm,
    kDisParmParseSysReg,
    kDisParmParseSh12,
    kDisParmParseImmTbz,
    kDisParmParseShift,
    kDisParmParseShiftAmount,
    kDisParmParseImmMemOff,
    kDisParmParseSImmMemOff,
    kDisParmParseSImmMemOffUnscaled,
    kDisParmParseOption,
    kDisParmParseS,
    kDisParmParseSetPreIndexed,
    kDisParmParseSetPostIndexed,
    kDisParmParseFpType,
    kDisParmParseFpReg,
    kDisParmParseFpScale,
    kDisParmParseFpFixupFCvt,
    kDisParmParseSimdRegSize,
    kDisParmParseSimdRegSize32,
    kDisParmParseSimdRegSize64,
    kDisParmParseSimdRegSize128,
    kDisParmParseSimdRegScalar,
    kDisParmParseImmHImmB,
    kDisParmParseSf,
    kDisParmParseImmX16,
    kDisParmParseSImmTags,
    kDisParmParseLdrPacImm,
    kDisParmParseLdrPacW,
    kDisParmParseVecRegElemSize,
    kDisParmParseVecQ,
    kDisParmParseVecGrp,
    kDisParmParseSimdLdStPostIndexImm,
    kDisParmParseMax
} DISPARMPARSEIDX;
/** @}  */


/**
 * Decoder step.
 */
typedef struct DISARMV8INSNPARAM
{
    /** The parser to use for the decode step. */
    DISPARMPARSEIDX     idxParse;
    /** Bit index at which the field starts. */
    uint8_t             idxBitStart;
    /** Size of the bit field. */
    uint8_t             cBits;
    /** The parameter this decoder param contributes to. */
    uint8_t             idxParam;
} DISARMV8INSNPARAM;
typedef DISARMV8INSNPARAM *PDISARMV8INSNPARAM;
typedef const DISARMV8INSNPARAM *PCDISARMV8INSNPARAM;

#define DIS_ARMV8_INSN_DECODE_TERM { kDisParmParseNop, 0, 0, DIS_ARMV8_INSN_PARAM_UNSET }
#define DIS_ARMV8_INSN_DECODE(a_idxParse, a_idxBitStart, a_cBits, a_idxParam) \
    { a_idxParse, a_idxBitStart, a_cBits, a_idxParam }

#define DIS_ARMV8_INSN_PARAM_UNSET        UINT8_MAX


/**
 * Opcode structure.
 */
typedef struct DISARMV8OPCODE
{
    /** The value of the fixed bits of the instruction. */
    uint32_t            fValue;
    /** Special flags for the opcode. */
    uint32_t            fFlags;
    /** Pointer to an alternative decoder overriding the default one for the instruction class. */
    PCDISARMV8INSNPARAM paDecode;
    /** The generic opcode structure. */
    DISOPCODE           Opc;
} DISARMV8OPCODE;
/** Pointer to a const opcode. */
typedef const DISARMV8OPCODE *PCDISARMV8OPCODE;


/**
 * Opcode decode index.
 */
typedef enum DISARMV8OPCDECODE
{
    kDisArmV8OpcDecodeNop = 0,
    kDisArmV8OpcDecodeLookup,
    kDisArmV8OpcDecodeCollate,
    kDisArmV8OpcDecodeMax
} DISARMV8OPCDECODE;


/**
 * Decoder stage type.
 */
typedef enum kDisArmV8DecodeType
{
    kDisArmV8DecodeType_Invalid = 0,
    kDisArmV8DecodeType_Map,
    kDisArmV8DecodeType_Table,
    kDisArmV8DecodeType_InsnClass,
    kDisArmV8DecodeType_32Bit_Hack = 0x7fffffff
} kDisArmV8DecodeType;


/**
 * Decode header.
 */
typedef struct DISARMV8DECODEHDR
{
    /** Next stage decoding type. */
    kDisArmV8DecodeType         enmDecodeType;
    /** Number of entries in the next decoder stage or
     * opcodes in the instruction class. */
    uint32_t                    cDecode;
} DISARMV8DECODEHDR;
/** Pointer to a decode header. */
typedef DISARMV8DECODEHDR *PDISARMV8DECODEHDR;
/** Pointer to a const decode header. */
typedef const DISARMV8DECODEHDR *PCDISARMV8DECODEHDR;
typedef const PCDISARMV8DECODEHDR *PPCDISARMV8DECODEHDR;


/**
 * Instruction class descriptor.
 */
typedef struct DISARMV8INSNCLASS
{
    /** Decoder header. */
    DISARMV8DECODEHDR       Hdr;
    /** Pointer to the arry of opcodes. */
    PCDISARMV8OPCODE        paOpcodes;
    /** The mask of fixed instruction bits. */
    uint32_t                fFixedInsn;
    /** Opcode decoder function. */
    DISARMV8OPCDECODE       enmOpcDecode;
    /** The mask of the bits relevant for decoding. */
    uint32_t                fMask;
    /** Number of bits to shift to get an index. */
    uint32_t                cShift;
    /** The array of decoding steps. */
    PCDISARMV8INSNPARAM     paParms;
} DISARMV8INSNCLASS;
/** Pointer to a constant instruction class descriptor. */
typedef const DISARMV8INSNCLASS *PCDISARMV8INSNCLASS;

/** The N bit in an N:ImmR:ImmS bit vector must be 1 for 64-bit instruction variants. */
#define DISARMV8INSNCLASS_F_N_FORCED_1_ON_64BIT         RT_BIT_32(1)
/** The instruction class is using the 64-bit register encoding only. */
#define DISARMV8INSNCLASS_F_FORCED_64BIT                RT_BIT_32(2)
/** The instruction class is using the 32-bit register encoding only. */
#define DISARMV8INSNCLASS_F_FORCED_32BIT                RT_BIT_32(3)


#define DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(a_Name) \
    static const DISARMV8INSNPARAM g_aArmV8A64Insn ## a_Name ## Decode[] = {
#define DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER_ALTERNATIVE(a_Name) \
        DIS_ARMV8_INSN_DECODE_TERM \
    }; \
    static const DISARMV8INSNPARAM g_aArmV8A64Insn ## a_Name ## Decode[] = {
#define DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(a_Name) \
        DIS_ARMV8_INSN_DECODE_TERM \
    }; \
    static const DISARMV8OPCODE g_aArmV8A64Insn ## a_Name ## Opcodes[] = {
#define DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END(a_Name, a_fFixedInsn, a_enmOpcDecode, a_fMask, a_cShift) \
    }; \
    static const DISARMV8INSNCLASS g_aArmV8A64Insn ## a_Name = { { kDisArmV8DecodeType_InsnClass, \
                                                                   RT_ELEMENTS(g_aArmV8A64Insn ## a_Name ## Opcodes) }, \
                                                                 & g_aArmV8A64Insn ## a_Name ## Opcodes[0], \
                                                                 a_fFixedInsn, a_enmOpcDecode, a_fMask, a_cShift, \
                                                                 & g_aArmV8A64Insn ## a_Name ## Decode[0] }

/**
 * Decoder lookup table entry.
 */
typedef struct DISARMV8DECODETBLENTRY
{
    /** The mask to apply to the instruction. */
    uint32_t                    fMask;
    /** The value the masked instruction must match for the entry to match. */
    uint32_t                    fValue;
    /** The next stage followed when there is a match. */
    PCDISARMV8DECODEHDR         pHdrNext;
} DISARMV8DECODETBLENTRY;
typedef struct DISARMV8DECODETBLENTRY *PDISARMV8DECODETBLENTRY;
typedef const DISARMV8DECODETBLENTRY *PCDISARMV8DECODETBLENTRY;


#define DIS_ARMV8_DECODE_TBL_ENTRY_INIT(a_fMask, a_fValue, a_pNext) \
    { a_fMask, a_fValue, & g_aArmV8A64Insn ## a_pNext.Hdr }


/**
 * Decoder lookup table using masks and values.
 */
typedef struct DISARMV8DECODETBL
{
    /** The header for the decoder lookup table. */
    DISARMV8DECODEHDR           Hdr;
    /** Pointer to the individual entries. */
    PCDISARMV8DECODETBLENTRY    paEntries;
} DISARMV8DECODETBL;
/** Pointer to a const decode table. */
typedef const struct DISARMV8DECODETBL *PCDISARMV8DECODETBL;


#define DIS_ARMV8_DECODE_TBL_DEFINE_BEGIN(a_Name) \
    static const DISARMV8DECODETBLENTRY g_aArmV8A64Insn ## a_Name ## TblEnt[] = {

#define DIS_ARMV8_DECODE_TBL_DEFINE_END(a_Name) \
    }; \
    static const DISARMV8DECODETBL g_aArmV8A64Insn ## a_Name = { { kDisArmV8DecodeType_Table, RT_ELEMENTS(g_aArmV8A64Insn ## a_Name ## TblEnt) }, \
                                                                 & g_aArmV8A64Insn ## a_Name ## TblEnt[0] }


/**
 * Decoder map when direct indexing is possible.
 */
typedef struct DISARMV8DECODEMAP
{
    /** The header for the decoder map. */
    DISARMV8DECODEHDR           Hdr;
    /** The bitmask used to decide where to go next. */
    uint32_t                    fMask;
    /** Amount to shift to get at the index. */
    uint32_t                    cShift;
    /** Pointer to the array of pointers to the next stage to index into. */
    PPCDISARMV8DECODEHDR        papNext;
} DISARMV8DECODEMAP;
/** Pointer to a const decode map. */
typedef const struct DISARMV8DECODEMAP *PCDISARMV8DECODEMAP;

#define DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(a_Name) \
    static const PCDISARMV8DECODEHDR g_aArmV8A64Insn ## a_Name ## MapHdrs[] = {

#define DIS_ARMV8_DECODE_MAP_DEFINE_END(a_Name, a_fMask, a_cShift) \
    }; \
    static const DISARMV8DECODEMAP g_aArmV8A64Insn ## a_Name = { { kDisArmV8DecodeType_Map, RT_ELEMENTS(g_aArmV8A64Insn ## a_Name ## MapHdrs) }, \
                                                                 a_fMask, a_cShift, & g_aArmV8A64Insn ## a_Name ## MapHdrs[0] }

#define DIS_ARMV8_DECODE_MAP_DEFINE_END_SINGLE_BIT(a_Name, a_idxBit) \
    }; \
    static const DISARMV8DECODEMAP g_aArmV8A64Insn ## a_Name = { { kDisArmV8DecodeType_Map, RT_ELEMENTS(g_aArmV8A64Insn ## a_Name ## MapHdrs) }, \
                                                                 RT_BIT_32(a_idxBit), a_idxBit, & g_aArmV8A64Insn ## a_Name ## MapHdrs[0] }


#define DIS_ARMV8_DECODE_MAP_DEFINE_END_NON_STATIC(a_Name, a_fMask, a_cShift) \
    }; \
    DECL_HIDDEN_CONST(DISARMV8DECODEMAP) g_aArmV8A64Insn ## a_Name = { { kDisArmV8DecodeType_Map, RT_ELEMENTS(g_aArmV8A64Insn ## a_Name ## MapHdrs) }, \
                                                                       a_fMask, a_cShift, & g_aArmV8A64Insn ## a_Name ## MapHdrs[0] }

#define DIS_ARMV8_DECODE_MAP_INVALID_ENTRY NULL
#define DIS_ARMV8_DECODE_MAP_ENTRY(a_Next) & g_aArmV8A64Insn ## a_Next.Hdr


/** @name Decoder maps.
 * @{ */
extern DECL_HIDDEN_DATA(DISOPCODE) g_ArmV8A64InvalidOpcode[1];

extern DECL_HIDDEN_DATA(DISARMV8DECODEMAP) g_aArmV8A64InsnDecodeL0;
/** @} */


/** @} */
#endif /* !VBOX_INCLUDED_SRC_DisasmInternal_armv8_h */

