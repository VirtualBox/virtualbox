/* $Id: VBoxPrintHexDump.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxPrintHex.c - Implementation of the VBoxPrintHex() debug logging routine.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include "VBoxDebugLib.h"
#include "DevEFI.h"
#include "iprt/asm.h"
#include "iprt/ctype.h"


/**
 * Prints a char.
 * @returns 1.
 * @param   ch              The char to print.
 */
DECLINLINE(int) vboxPrintHexDumpChar(int ch)
{
    ASMOutU8(EFI_DEBUG_PORT, (uint8_t)ch);
    return 1;
}


/**
 * Prints a hex dump the specified memory block.
 *
 * @returns Number of bytes printed.
 *
 * @param   pv      The memory to dump.
 * @param   cb      Number of bytes to dump.
 */
size_t VBoxPrintHexDump(const void *pv, size_t cb)
{
    size_t          cchPrinted = 0;
    uint8_t const  *pb         = (uint8_t const *)pv;
    while (cb > 0)
    {
        unsigned i;

        /* the offset */
        cchPrinted += VBoxPrintHex((uintptr_t)pb, sizeof(pb));
        cchPrinted += VBoxPrintString("  ");

        /* the hex bytes value. */
        for (i = 0; i < 16; i++)
        {
            cchPrinted += vboxPrintHexDumpChar(i == 7 ? '-' : ' ');
            if (i < cb)
                cchPrinted += VBoxPrintHex(pb[i], 1);
            else
                cchPrinted += VBoxPrintString("  ");
        }

        /* the printable chars */
        cchPrinted += VBoxPrintString("  ");
        for (i = 0; i < 16 && i < cb; i++)
            cchPrinted += vboxPrintHexDumpChar(RT_C_IS_PRINT(pb[i])
                                               ? pb[i]
                                               : '.');

        /* finally, the new line. */
        cchPrinted += vboxPrintHexDumpChar('\n');

        /*
         * Advance.
         */
        if (cb <= 16)
            break;
        cb -= 16;
        pb += 16;
    }

    return cchPrinted;
}

