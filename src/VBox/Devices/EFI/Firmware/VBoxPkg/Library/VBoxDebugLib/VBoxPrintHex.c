/* $Id: VBoxPrintHex.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <Library/BaseLib.h>

#include "VBoxDebugLib.h"
#include "DevEFI.h"
#include "iprt/asm.h"


/**
 * Prints a char.
 * @param   ch              The char to print.
 */
DECLINLINE(void) vboxPrintHexChar(int ch)
{
    ASMOutU8(EFI_DEBUG_PORT, (uint8_t)ch);
}


/**
 * Print a hex number, up to 64-bit long.
 *
 * @returns Number of chars printed.
 *
 * @param   uValue          The value.
 * @param   cbType          The size of the value type.
 */
size_t VBoxPrintHex(UINT64 uValue, size_t cbType)
{
    static const char s_szHex[17] = "0123456789abcdef";
    switch (cbType)
    {
/*
 * We have to cast the result to UINTN before indexing into the array
 * or cl.exe insists on generating a call to __allmul for unoptimized 32bit builds,
 * see: https://patchew.org/EDK2/1486606121-226912-1-git-send-email-dandan.bi@intel.com/
 */
#define VAL_NIBBLE_EXTRACT(a_uValue, a_iNibbleStart) (s_szHex[(UINTN)(RShiftU64((a_uValue), (a_iNibbleStart)) & 0xf)])
        case 8:
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 60));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 56));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 52));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 48));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 44));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 40));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 36));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 32));
        case 4:
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 28));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 24));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 20));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 16));
        case 2:
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue, 12));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue,  8));
        case 1:
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue,  4));
            vboxPrintHexChar(VAL_NIBBLE_EXTRACT(uValue,  0));
            break;
#undef VAL_NIBBLE_EXTRACT
    }

#if 0 /* There is no MultU32x32 for 32bit and cl insists on emitting __allmul otherwise so we just hardcode everything here... */
    return cbType * 2;
#else
    static size_t s_acbPrinted[9] = { 0, 2, 4, 0, 8, 0, 0, 0, 16};
    if (cbType < RT_ELEMENTS(s_acbPrinted))
        return s_acbPrinted[cbType];
    return 0;
#endif
}
