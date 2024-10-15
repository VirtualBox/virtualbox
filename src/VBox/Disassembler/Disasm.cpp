/* $Id: Disasm.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox disassembler - Disassemble and optionally format.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DIS
#include <VBox/dis.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include "DisasmInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{FNDISREADBYTES, The default byte reader callber.}
 */
static DECLCALLBACK(int) disReadBytesDefault(PDISSTATE pDis, uint8_t offInstr, uint8_t cbMinRead, uint8_t cbMaxRead)
{
#if 0 /*def IN_RING0 - why? */
    RT_NOREF_PV(cbMinRead);
    AssertMsgFailed(("disReadWord with no read callback in ring 0!!\n"));
    RT_BZERO(&pDis->Instr.ab[offInstr], cbMaxRead);
    pDis->cbCachedInstr = offInstr + cbMaxRead;
    return VERR_DIS_NO_READ_CALLBACK;
#else
    uint8_t const  *pbSrc        = (uint8_t const *)(uintptr_t)pDis->uInstrAddr + offInstr;
    size_t          cbLeftOnPage = (uintptr_t)pbSrc & PAGE_OFFSET_MASK;
    uint8_t         cbToRead     = cbLeftOnPage >= cbMaxRead
                                 ? cbMaxRead
                                 : cbLeftOnPage <= cbMinRead
                                 ? cbMinRead
                                 : (uint8_t)cbLeftOnPage;
    memcpy(&pDis->Instr.ab[offInstr], pbSrc, cbToRead);
    pDis->cbCachedInstr = offInstr + cbToRead;
    return VINF_SUCCESS;
#endif
}


/**
 * Read more bytes into the DISSTATE::Instr.ab buffer, advance
 * DISSTATE::cbCachedInstr.
 *
 * Will set DISSTATE::rc on failure, but still advance cbCachedInstr.
 *
 * The caller shall fend off reads beyond the DISSTATE::Instr.ab buffer.
 *
 * @param   pDis                The disassembler state.
 * @param   offInstr            The offset of the read request.
 * @param   cbMin               The size of the read request that needs to be
 *                              satisfied.
 */
DECLHIDDEN(void) disReadMore(PDISSTATE pDis, uint8_t offInstr, uint8_t cbMin)
{
    Assert(cbMin + offInstr <= sizeof(pDis->Instr.ab));

    /*
     * Adjust the incoming request to not overlap with bytes that has already
     * been read and to make sure we don't leave unread gaps.
     */
    if (offInstr < pDis->cbCachedInstr)
    {
        Assert(offInstr + cbMin > pDis->cbCachedInstr);
        cbMin -= pDis->cbCachedInstr - offInstr;
        offInstr = pDis->cbCachedInstr;
    }
    else if (offInstr > pDis->cbCachedInstr)
    {
        cbMin += offInstr - pDis->cbCachedInstr;
        offInstr = pDis->cbCachedInstr;
    }

    /*
     * Do the read.
     * (No need to zero anything on failure as Instr.ab is already zeroed by the
     * DISInstrEx API.)
     */
    int rc = pDis->pfnReadBytes(pDis, offInstr, cbMin, sizeof(pDis->Instr.ab) - offInstr);
    if (RT_SUCCESS(rc))
    {
        Assert(pDis->cbCachedInstr >= offInstr + cbMin);
        Assert(pDis->cbCachedInstr <= sizeof(pDis->Instr.ab));
    }
    else
    {
        Log(("disReadMore failed with rc=%Rrc!!\n", rc));
        pDis->rc = rc;
    }
}


/**
 * Function for handling a 8-bit cache miss.
 *
 * @returns The requested byte.
 * @param   pDis                The disassembler state.
 * @param   offInstr            The offset of the byte relative to the
 *                              instruction.
 */
DECLHIDDEN(uint8_t) disReadByteSlow(PDISSTATE pDis, size_t offInstr)
{
    if (RT_LIKELY(offInstr < DIS_MAX_INSTR_LENGTH))
    {
        disReadMore(pDis, (uint8_t)offInstr, 1);
        return pDis->Instr.ab[offInstr];
    }

    Log(("disReadByte: too long instruction...\n"));
    pDis->rc = VERR_DIS_TOO_LONG_INSTR;
    ssize_t cbLeft = (ssize_t)(sizeof(pDis->Instr.ab) - offInstr);
    if (cbLeft > 0)
        return pDis->Instr.ab[offInstr];
    return 0;
}


/**
 * Function for handling a 16-bit cache miss.
 *
 * @returns The requested word.
 * @param   pDis                The disassembler state.
 * @param   offInstr            The offset of the word relative to the
 *                              instruction.
 */
DECLHIDDEN(uint16_t) disReadWordSlow(PDISSTATE pDis, size_t offInstr)
{
    if (RT_LIKELY(offInstr + 2 <= DIS_MAX_INSTR_LENGTH))
    {
        disReadMore(pDis, (uint8_t)offInstr, 2);
#ifdef DIS_HOST_UNALIGNED_ACCESS_OK
        return *(uint16_t const *)&pDis->Instr.ab[offInstr];
#else
        return RT_MAKE_U16(pDis->Instr.ab[offInstr], pDis->Instr.ab[offInstr + 1]);
#endif
    }

    Log(("disReadWord: too long instruction...\n"));
    pDis->rc = VERR_DIS_TOO_LONG_INSTR;
    ssize_t cbLeft = (ssize_t)(sizeof(pDis->Instr.ab) - offInstr);
    switch (cbLeft)
    {
        case 1:
            return pDis->Instr.ab[offInstr];
        default:
            if (cbLeft >= 2)
                return RT_MAKE_U16(pDis->Instr.ab[offInstr], pDis->Instr.ab[offInstr + 1]);
            return 0;
    }
}


/**
 * Function for handling a 32-bit cache miss.
 *
 * @returns The requested dword.
 * @param   pDis                The disassembler state.
 * @param   offInstr            The offset of the dword relative to the
 *                              instruction.
 */
DECLHIDDEN(uint32_t) disReadDWordSlow(PDISSTATE pDis, size_t offInstr)
{
    if (RT_LIKELY(offInstr + 4 <= DIS_MAX_INSTR_LENGTH))
    {
        disReadMore(pDis, (uint8_t)offInstr, 4);
#ifdef DIS_HOST_UNALIGNED_ACCESS_OK
        return *(uint32_t const *)&pDis->Instr.ab[offInstr];
#else
        return RT_MAKE_U32_FROM_U8(pDis->Instr.ab[offInstr    ], pDis->Instr.ab[offInstr + 1],
                                   pDis->Instr.ab[offInstr + 2], pDis->Instr.ab[offInstr + 3]);
#endif
    }

    Log(("disReadDWord: too long instruction...\n"));
    pDis->rc = VERR_DIS_TOO_LONG_INSTR;
    ssize_t cbLeft = (ssize_t)(sizeof(pDis->Instr.ab) - offInstr);
    switch (cbLeft)
    {
        case 1:
            return RT_MAKE_U32_FROM_U8(pDis->Instr.ab[offInstr], 0, 0, 0);
        case 2:
            return RT_MAKE_U32_FROM_U8(pDis->Instr.ab[offInstr], pDis->Instr.ab[offInstr + 1], 0, 0);
        case 3:
            return RT_MAKE_U32_FROM_U8(pDis->Instr.ab[offInstr], pDis->Instr.ab[offInstr + 1], pDis->Instr.ab[offInstr + 2], 0);
        default:
            if (cbLeft >= 4)
                return RT_MAKE_U32_FROM_U8(pDis->Instr.ab[offInstr    ], pDis->Instr.ab[offInstr + 1],
                                           pDis->Instr.ab[offInstr + 2], pDis->Instr.ab[offInstr + 3]);
            return 0;
    }
}


/**
 * Function for handling a 64-bit cache miss.
 *
 * @returns The requested qword.
 * @param   pDis                The disassembler state.
 * @param   offInstr            The offset of the qword relative to the
 *                              instruction.
 */
DECLHIDDEN(uint64_t) disReadQWordSlow(PDISSTATE pDis, size_t offInstr)
{
    if (RT_LIKELY(offInstr + 8 <= DIS_MAX_INSTR_LENGTH))
    {
        disReadMore(pDis, (uint8_t)offInstr, 8);
#ifdef DIS_HOST_UNALIGNED_ACCESS_OK
        return *(uint64_t const *)&pDis->Instr.ab[offInstr];
#else
        return RT_MAKE_U64_FROM_U8(pDis->Instr.ab[offInstr    ], pDis->Instr.ab[offInstr + 1],
                                   pDis->Instr.ab[offInstr + 2], pDis->Instr.ab[offInstr + 3],
                                   pDis->Instr.ab[offInstr + 4], pDis->Instr.ab[offInstr + 5],
                                   pDis->Instr.ab[offInstr + 6], pDis->Instr.ab[offInstr + 7]);
#endif
    }

    Log(("disReadQWord: too long instruction...\n"));
    pDis->rc = VERR_DIS_TOO_LONG_INSTR;
    ssize_t cbLeft = (ssize_t)(sizeof(pDis->Instr.ab) - offInstr);
    switch (cbLeft)
    {
        case 1:
            return RT_MAKE_U64_FROM_U8(pDis->Instr.ab[offInstr], 0, 0, 0,   0, 0, 0, 0);
        case 2:
            return RT_MAKE_U64_FROM_U8(pDis->Instr.ab[offInstr], pDis->Instr.ab[offInstr + 1], 0, 0,   0, 0, 0, 0);
        case 3:
            return RT_MAKE_U64_FROM_U8(pDis->Instr.ab[offInstr    ], pDis->Instr.ab[offInstr + 1],
                                       pDis->Instr.ab[offInstr + 2], 0,   0, 0, 0, 0);
        case 4:
            return RT_MAKE_U64_FROM_U8(pDis->Instr.ab[offInstr    ], pDis->Instr.ab[offInstr + 1],
                                       pDis->Instr.ab[offInstr + 2], pDis->Instr.ab[offInstr + 3],
                                       0, 0, 0, 0);
        case 5:
            return RT_MAKE_U64_FROM_U8(pDis->Instr.ab[offInstr    ], pDis->Instr.ab[offInstr + 1],
                                       pDis->Instr.ab[offInstr + 2], pDis->Instr.ab[offInstr + 3],
                                       pDis->Instr.ab[offInstr + 4], 0, 0, 0);
        case 6:
            return RT_MAKE_U64_FROM_U8(pDis->Instr.ab[offInstr    ], pDis->Instr.ab[offInstr + 1],
                                       pDis->Instr.ab[offInstr + 2], pDis->Instr.ab[offInstr + 3],
                                       pDis->Instr.ab[offInstr + 4], pDis->Instr.ab[offInstr + 5],
                                       0, 0);
        case 7:
            return RT_MAKE_U64_FROM_U8(pDis->Instr.ab[offInstr    ], pDis->Instr.ab[offInstr + 1],
                                       pDis->Instr.ab[offInstr + 2], pDis->Instr.ab[offInstr + 3],
                                       pDis->Instr.ab[offInstr + 4], pDis->Instr.ab[offInstr + 5],
                                       pDis->Instr.ab[offInstr + 6], 0);
        default:
            if (cbLeft >= 8)
                return RT_MAKE_U64_FROM_U8(pDis->Instr.ab[offInstr    ], pDis->Instr.ab[offInstr + 1],
                                           pDis->Instr.ab[offInstr + 2], pDis->Instr.ab[offInstr + 3],
                                           pDis->Instr.ab[offInstr + 4], pDis->Instr.ab[offInstr + 5],
                                           pDis->Instr.ab[offInstr + 6], pDis->Instr.ab[offInstr + 7]);
            return 0;
    }
}


/**
 * Inlined worker that initializes the disassembler state.
 *
 * @returns The primary opcode map to use.
 * @param   pDis            The disassembler state.
 * @param   uInstrAddr      The instruction address.
 * @param   enmCpuMode      The CPU mode.
 * @param   fFilter         The instruction filter settings.
 * @param   pfnReadBytes    The byte reader, can be NULL.
 * @param   pvUser          The user data for the reader.
 */
DECL_FORCE_INLINE(PCDISOPCODE)
disInitializeState(PDISSTATE pDis, RTUINTPTR uInstrAddr, DISCPUMODE enmCpuMode, uint32_t fFilter,
                   PFNDISREADBYTES pfnReadBytes, void *pvUser)
{
    RT_ZERO(*pDis);

#ifdef VBOX_STRICT
    pDis->aParams[0].uValue = UINT64_C(0xb1b1b1b1b1b1b1b1);
    pDis->aParams[1].uValue = UINT64_C(0xb2b2b2b2b2b2b2b2);
    pDis->aParams[2].uValue = UINT64_C(0xb3b3b3b3b3b3b3b3);
    pDis->aParams[3].uValue = UINT64_C(0xb4b4b4b4b4b4b4b4);
#endif

    pDis->rc                = VINF_SUCCESS;
    pDis->uInstrAddr        = uInstrAddr;
    pDis->pfnReadBytes      = pfnReadBytes ? pfnReadBytes : disReadBytesDefault;
    pDis->pvUser            = pvUser;
    pDis->uCpuMode          = (uint8_t)enmCpuMode;

    switch (enmCpuMode)
    {
        case DISCPUMODE_16BIT:
        case DISCPUMODE_32BIT:
        case DISCPUMODE_64BIT:
#if defined(VBOX_DIS_WITH_X86_AMD64)
            return disInitializeStateX86(pDis, enmCpuMode, fFilter);
#else
            return NULL;
#endif
        case DISCPUMODE_ARMV8_A64:
        case DISCPUMODE_ARMV8_A32:
        case DISCPUMODE_ARMV8_T32:
#if defined(VBOX_DIS_WITH_ARMV8)
            return disInitializeStateArmV8(pDis, enmCpuMode, fFilter);
#else
            return NULL;
#endif
        default:
            break;
    }

    AssertReleaseFailed(); /* Should never get here. */
    return NULL;
}


/**
 * Disassembles on instruction, details in @a pDis and length in @a pcbInstr.
 *
 * @returns VBox status code.
 * @param   uInstrAddr      Address of the instruction to decode. What this means
 *                          is left to the pfnReadBytes function.
 * @param   enmCpuMode      The CPU mode. DISCPUMODE_32BIT, DISCPUMODE_16BIT, or DISCPUMODE_64BIT.
 * @param   pfnReadBytes    Callback for reading instruction bytes.
 * @param   fFilter         Instruction type filter.
 * @param   pvUser          User argument for the instruction reader. (Ends up in pvUser.)
 * @param   pDis            Pointer to disassembler state (output).
 * @param   pcbInstr        Where to store the size of the instruction.  (This
 *                          is also stored in PDISSTATE::cbInstr.)  Optional.
 */
DISDECL(int) DISInstrEx(RTUINTPTR uInstrAddr, DISCPUMODE enmCpuMode, uint32_t fFilter,
                        PFNDISREADBYTES pfnReadBytes, void *pvUser,
                        PDISSTATE pDis, uint32_t *pcbInstr)
{

    PCDISOPCODE paOneByteMap = disInitializeState(pDis, uInstrAddr, enmCpuMode, fFilter, pfnReadBytes, pvUser);
    disPrefetchBytes(pDis);

    switch (enmCpuMode)
    {
        case DISCPUMODE_16BIT:
        case DISCPUMODE_32BIT:
        case DISCPUMODE_64BIT:
#if defined(VBOX_DIS_WITH_X86_AMD64)
            return disInstrWorkerX86(pDis, paOneByteMap, pcbInstr);
#else
            return VERR_NOT_SUPPORTED;
#endif
        case DISCPUMODE_ARMV8_A64:
        case DISCPUMODE_ARMV8_A32:
        case DISCPUMODE_ARMV8_T32:
#if defined(VBOX_DIS_WITH_ARMV8)
            return disInstrWorkerArmV8(pDis, paOneByteMap, pcbInstr);
#else
            return VERR_NOT_SUPPORTED;
#endif
        default:
            break;
    }

    AssertReleaseFailed(); /* Should never get here. */
    return VERR_INTERNAL_ERROR;
}


/**
 * Disassembles on instruction partially or fully from prefetched bytes, details
 * in @a pDis and length in @a pcbInstr.
 *
 * @returns VBox status code.
 * @param   uInstrAddr      Address of the instruction to decode. What this means
 *                          is left to the pfnReadBytes function.
 * @param   enmCpuMode      The CPU mode. DISCPUMODE_32BIT, DISCPUMODE_16BIT, or DISCPUMODE_64BIT.
 * @param   pvPrefetched    Pointer to the prefetched bytes.
 * @param   cbPrefetched    The number of valid bytes pointed to by @a
 *                          pbPrefetched.
 * @param   pfnReadBytes    Callback for reading instruction bytes.
 * @param   fFilter         Instruction type filter.
 * @param   pvUser          User argument for the instruction reader. (Ends up in pvUser.)
 * @param   pDis            Pointer to disassembler state (output).
 * @param   pcbInstr        Where to store the size of the instruction.  (This
 *                          is also stored in PDISSTATE::cbInstr.)  Optional.
 */
DISDECL(int) DISInstrWithPrefetchedBytes(RTUINTPTR uInstrAddr, DISCPUMODE enmCpuMode, uint32_t fFilter,
                                         void const *pvPrefetched, size_t cbPretched,
                                         PFNDISREADBYTES pfnReadBytes, void *pvUser,
                                         PDISSTATE pDis, uint32_t *pcbInstr)
{
    PCDISOPCODE paOneByteMap = disInitializeState(pDis, uInstrAddr, enmCpuMode, fFilter, pfnReadBytes, pvUser);

    if (!cbPretched)
        disPrefetchBytes(pDis);
    else
    {
        if (cbPretched >= sizeof(pDis->Instr.ab))
        {
            memcpy(pDis->Instr.ab, pvPrefetched, sizeof(pDis->Instr.ab));
            pDis->cbCachedInstr = (uint8_t)sizeof(pDis->Instr.ab);
        }
        else
        {
            memcpy(pDis->Instr.ab, pvPrefetched, cbPretched);
            pDis->cbCachedInstr = (uint8_t)cbPretched;
        }
    }

    switch (enmCpuMode)
    {
        case DISCPUMODE_16BIT:
        case DISCPUMODE_32BIT:
        case DISCPUMODE_64BIT:
#if defined(VBOX_DIS_WITH_X86_AMD64)
            return disInstrWorkerX86(pDis, paOneByteMap, pcbInstr);
#else
            return VERR_NOT_SUPPORTED;
#endif
        case DISCPUMODE_ARMV8_A64:
        case DISCPUMODE_ARMV8_A32:
        case DISCPUMODE_ARMV8_T32:
#if defined(VBOX_DIS_WITH_ARMV8)
            return disInstrWorkerArmV8(pDis, paOneByteMap, pcbInstr);
#else
            return VERR_NOT_SUPPORTED;
#endif
        default:
            break;
    }

    AssertReleaseFailed(); /* Should never get here. */
    return VERR_INTERNAL_ERROR;
}


/**
 * Parses one guest instruction.
 *
 * The result is found in pDis and pcbInstr.
 *
 * @returns VBox status code.
 * @param   uInstrAddr      Address of the instruction to decode. What this means
 *                          is left to the pfnReadBytes function.
 * @param   enmCpuMode      The CPU mode. DISCPUMODE_32BIT, DISCPUMODE_16BIT, or DISCPUMODE_64BIT.
 * @param   pfnReadBytes    Callback for reading instruction bytes.
 * @param   pvUser          User argument for the instruction reader. (Ends up in pvUser.)
 * @param   pDis            Pointer to disassembler state (output).
 * @param   pcbInstr        Where to store the size of the instruction.
 *                          NULL is allowed.  This is also stored in
 *                          PDISSTATE::cbInstr.
 */
DISDECL(int) DISInstrWithReader(RTUINTPTR uInstrAddr, DISCPUMODE enmCpuMode, PFNDISREADBYTES pfnReadBytes, void *pvUser,
                                PDISSTATE pDis, uint32_t *pcbInstr)
{
    return DISInstrEx(uInstrAddr, enmCpuMode, DISOPTYPE_ALL, pfnReadBytes, pvUser, pDis, pcbInstr);
}


/**
 * Parses one guest instruction.
 *
 * The result is found in pDis and pcbInstr.
 *
 * @returns VBox status code.
 * @param   pvInstr         Address of the instruction to decode.  This is a
 *                          real address in the current context that can be
 *                          accessed without faulting.  (Consider
 *                          DISInstrWithReader if this isn't the case.)
 * @param   enmCpuMode      The CPU mode. DISCPUMODE_32BIT, DISCPUMODE_16BIT, or DISCPUMODE_64BIT.
 * @param   pfnReadBytes    Callback for reading instruction bytes.
 * @param   pvUser          User argument for the instruction reader. (Ends up in pvUser.)
 * @param   pDis            Pointer to disassembler state (output).
 * @param   pcbInstr        Where to store the size of the instruction.
 *                          NULL is allowed.  This is also stored in
 *                          PDISSTATE::cbInstr.
 */
DISDECL(int) DISInstr(const void *pvInstr, DISCPUMODE enmCpuMode, PDISSTATE pDis, uint32_t *pcbInstr)
{
    return DISInstrEx((uintptr_t)pvInstr, enmCpuMode, DISOPTYPE_ALL, NULL /*pfnReadBytes*/, NULL /*pvUser*/, pDis, pcbInstr);
}


#ifndef DIS_CORE_ONLY
/**
 * Disassembles one instruction
 *
 * @returns VBox error code
 * @param   pvInstr         Pointer to the instruction to disassemble.
 * @param   enmCpuMode      The CPU state.
 * @param   pDis            The disassembler state (output).
 * @param   pcbInstr        Where to store the size of the instruction. NULL is
 *                          allowed.
 * @param   pszOutput       Storage for disassembled instruction
 * @param   cbOutput        Size of the output buffer.
 *
 * @todo    Define output callback.
 */
DISDECL(int) DISInstrToStr(void const *pvInstr, DISCPUMODE enmCpuMode, PDISSTATE pDis, uint32_t *pcbInstr,
                           char *pszOutput, size_t cbOutput)
{
    return DISInstrToStrEx((uintptr_t)pvInstr, enmCpuMode, NULL, NULL, DISOPTYPE_ALL,
                           pDis, pcbInstr, pszOutput, cbOutput);
}


/**
 * Disassembles one instruction with a byte fetcher caller.
 *
 * @returns VBox error code
 * @param   uInstrAddr      Pointer to the structure to disassemble.
 * @param   enmCpuMode      The CPU mode.
 * @param   pfnCallback     The byte fetcher callback.
 * @param   pvUser          The user argument (found in
 *                          DISSTATE::pvUser).
 * @param   pDis            The disassembler state (output).
 * @param   pcbInstr        Where to store the size of the instruction. NULL is
 *                          allowed.
 * @param   pszOutput       Storage for disassembled instruction.
 * @param   cbOutput        Size of the output buffer.
 *
 * @todo    Define output callback.
 */
DISDECL(int) DISInstrToStrWithReader(RTUINTPTR uInstrAddr, DISCPUMODE enmCpuMode, PFNDISREADBYTES pfnReadBytes, void *pvUser,
                                     PDISSTATE pDis, uint32_t *pcbInstr, char *pszOutput, size_t cbOutput)

{
    return DISInstrToStrEx(uInstrAddr, enmCpuMode, pfnReadBytes, pvUser, DISOPTYPE_ALL,
                           pDis, pcbInstr, pszOutput, cbOutput);
}


/**
 * Disassembles one instruction; only fully disassembly an instruction if it matches the filter criteria
 *
 * @returns VBox error code
 * @param   uInstrAddr      Pointer to the structure to disassemble.
 * @param   enmCpuMode      The CPU mode.
 * @param   pfnCallback     The byte fetcher callback.
 * @param   uFilter         Instruction filter.
 * @param   pDis            Where to return the disassembled instruction info.
 * @param   pcbInstr        Where to store the size of the instruction. NULL is
 *                          allowed.
 * @param   pszOutput       Storage for disassembled instruction.
 * @param   cbOutput        Size of the output buffer.
 *
 * @todo    Define output callback.
 */
DISDECL(int) DISInstrToStrEx(RTUINTPTR uInstrAddr, DISCPUMODE enmCpuMode,
                             PFNDISREADBYTES pfnReadBytes, void *pvUser, uint32_t uFilter,
                             PDISSTATE pDis, uint32_t *pcbInstr, char *pszOutput, size_t cbOutput)
{
    /* Don't filter if formatting is desired. */
    if (uFilter !=  DISOPTYPE_ALL && pszOutput && cbOutput)
        uFilter = DISOPTYPE_ALL;

    int rc = DISInstrEx(uInstrAddr, enmCpuMode, uFilter, pfnReadBytes, pvUser, pDis, pcbInstr);
    if (RT_SUCCESS(rc) && pszOutput && cbOutput)
    {
        size_t cch = 0;

        switch (enmCpuMode)
        {
            case DISCPUMODE_16BIT:
            case DISCPUMODE_32BIT:
            case DISCPUMODE_64BIT:
#if defined(VBOX_DIS_WITH_X86_AMD64)
                cch = DISFormatYasmEx(pDis, pszOutput, cbOutput,
                                        DIS_FMT_FLAGS_BYTES_LEFT | DIS_FMT_FLAGS_BYTES_BRACKETS | DIS_FMT_FLAGS_BYTES_SPACED
                                      | DIS_FMT_FLAGS_RELATIVE_BRANCH | DIS_FMT_FLAGS_ADDR_LEFT,
                                      NULL /*pfnGetSymbol*/, NULL /*pvUser*/);
#else
                AssertReleaseFailed(); /* Shouldn't ever get here (DISInstrEx() returning VERR_NOT_SUPPORTED). */
#endif
                break;
            case DISCPUMODE_ARMV8_A64:
            case DISCPUMODE_ARMV8_A32:
            case DISCPUMODE_ARMV8_T32:
#if defined(VBOX_DIS_WITH_ARMV8)
                cch = DISFormatArmV8Ex(pDis, pszOutput, cbOutput,
                                         DIS_FMT_FLAGS_BYTES_LEFT | DIS_FMT_FLAGS_BYTES_BRACKETS | DIS_FMT_FLAGS_BYTES_SPACED
                                       | DIS_FMT_FLAGS_RELATIVE_BRANCH | DIS_FMT_FLAGS_ADDR_LEFT,
                                       NULL /*pfnGetSymbol*/, NULL /*pvUser*/);
#else
               AssertReleaseFailed(); /* Shouldn't ever get here (DISInstrEx() returning VERR_NOT_SUPPORTED). */
#endif
                break;
            default:
                break;
        }

        if (cch + 2 <= cbOutput)
        {
            pszOutput[cch++] = '\n';
            pszOutput[cch] = '\0';
        }
    }
    return rc;
}
#endif /* DIS_CORE_ONLY */

