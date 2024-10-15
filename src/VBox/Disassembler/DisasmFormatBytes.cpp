/* $Id: DisasmFormatBytes.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Disassembler - Helper for formatting the opcode bytes.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include "DisasmInternal.h"
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>


/**
 * Helper function for formatting the opcode bytes.
 *
 * @returns The number of output bytes.
 *
 * @param   pDis    Pointer to the disassembler state.
 * @param   pszDst  The output buffer.
 * @param   cchDst  The size of the output buffer.
 * @param   fFlags  The flags passed to the formatter.
 */
size_t disFormatBytes(PCDISSTATE pDis, char *pszDst, size_t cchDst, uint32_t fFlags)
{
    size_t      cchOutput = 0;
    uint32_t    cb        = pDis->cbInstr;
    AssertStmt(cb <= 16, cb = 16);

#define PUT_C(ch) \
            do { \
                cchOutput++; \
                if (cchDst > 1) \
                { \
                    cchDst--; \
                    *pszDst++ = (ch); \
                } \
            } while (0)
#define PUT_NUM(cch, fmt, num) \
            do { \
                 cchOutput += (cch); \
                 if (cchDst > 1) \
                 { \
                    const size_t cchTmp = RTStrPrintf(pszDst, cchDst, fmt, (num)); \
                    pszDst += cchTmp; \
                    cchDst -= cchTmp; \
                 } \
            } while (0)


    if (fFlags & DIS_FMT_FLAGS_BYTES_BRACKETS)
        PUT_C('[');

    for (uint32_t i = 0; i < cb; i++)
    {
        if (i != 0 && (fFlags & DIS_FMT_FLAGS_BYTES_SPACED))
            PUT_NUM(3, " %02x", pDis->Instr.ab[i]);
        else
            PUT_NUM(2, "%02x", pDis->Instr.ab[i]);
    }

    if (fFlags & DIS_FMT_FLAGS_BYTES_BRACKETS)
        PUT_C(']');

    /* Terminate it just in case. */
    if (cchDst >= 1)
        *pszDst = '\0';

#undef PUT_C
#undef PUT_NUM
    return cchOutput;
}

