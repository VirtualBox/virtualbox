/* $Id: VBoxDebugLib.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDebugLib.c - Debug logging and assertions support routines using DevEFI.
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
#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Library/DebugLib.h>

#include "VBoxDebugLib.h"
#include <Protocol/DevicePath.h>
#include <Protocol/DevicePathToText.h>
#include <Uefi/UefiSpec.h>
#include <Library/UefiBootServicesTableLib.h>
#include "DevEFI.h"
#include "iprt/asm.h"


VOID EFIAPI
DebugPrint(IN UINTN ErrorLevel, IN CONST CHAR8 *Format, ...)
{
    CHAR8       szBuf[256];
    VA_LIST     va;
    UINTN       cch;
    BOOLEAN     InterruptState;

    /* No pool noise, please. */
    if (ErrorLevel == DEBUG_POOL)
        return;

    VA_START(va, Format);
    cch = AsciiVSPrint(szBuf, sizeof(szBuf), Format, va);
    VA_END(va);

    /* make sure it's terminated and doesn't end with a newline */
    if (cch >= sizeof(szBuf))
        cch = sizeof(szBuf) - 1;
    while (cch > 0 && (szBuf[cch - 1] == '\n' || szBuf[cch - 1] == '\r'))
        cch--;
    szBuf[cch] = '\0';

    InterruptState = SaveAndDisableInterrupts();

    /* Output the log string. */
    VBoxPrintString("dbg/");
    VBoxPrintHex(ErrorLevel, sizeof(ErrorLevel));
    VBoxPrintChar(' ');
    VBoxPrintString(szBuf);
    VBoxPrintChar('\n');

    SetInterruptState(InterruptState);
}

/**
 * Our own log worker function, avoid the dbg/00000xxx prefix and makes it clear
 * which log statements we added..
 *
 * @param   pszFormat           Format string. EFI style!
 * @param   ...                 Argument referneced in the format string.
 */
VOID EFIAPI
VBoxLogWorker(const char *pszFormat, ...)
{
    CHAR8       szBuf[384];
    VA_LIST     va;
    BOOLEAN     InterruptState;

    /* Format it. */
    VA_START(va, pszFormat);
    AsciiVSPrint(szBuf, sizeof(szBuf), pszFormat, va);
    VA_END(va);
    szBuf[sizeof(szBuf) - 1] = '\0';

    InterruptState = SaveAndDisableInterrupts();

    /* Output the log string. */
    VBoxPrintString(szBuf);
    VBoxPrintChar('\n');

    SetInterruptState(InterruptState);
}

/**
 * Adds a character to the panic message.
 *
 * @param   ch          The ASCII char to add.
 */
static void
VBoxPanicMsgChar(int ch)
{
    ASMOutU16(EFI_PANIC_PORT, EFI_PANIC_CMD_MSG_FROM_CHAR(ch));
}

/**
 * Adds a string to the panic message.
 *
 * @param   pszString   The string to add.
 */
static void
VBoxPanicMsgString(const char *pszString)
{
    char ch;
    while ((ch = *pszString++) != '\0')
        VBoxPanicMsgChar(ch);
}

/**
 * Adds a unsigned decimal number to the panic message.
 *
 * @param   uValue      The value.
 */
static void
VBoxPanicMsgDecimalU32(uint32_t uValue)
{
    char     szTmp[32];
    unsigned off = sizeof(szTmp) - 1;

    szTmp[off] = '\0';
    do
    {
        char chDigit = uValue % 10;
        uValue /= 10;
        szTmp[--off] = chDigit + '0';
    } while (uValue != 0 && off > 0);

    VBoxPanicMsgString(&szTmp[off]);
}

VOID EFIAPI
DebugAssert(IN CONST CHAR8 *FileName, IN UINTN LineNumber, IN CONST CHAR8 *Description)
{
    BOOLEAN InterruptState = SaveAndDisableInterrupts();

    ASMOutU8(EFI_PANIC_PORT, EFI_PANIC_CMD_START_MSG);
    VBoxPanicMsgString("EFI Assertion failed!"
                       "\nFile:  ");
    VBoxPanicMsgString(FileName ? FileName : "<NULL>");
    VBoxPanicMsgString("\nLine:  ");
    VBoxPanicMsgDecimalU32((uint32_t)LineNumber);
    VBoxPanicMsgString("\nDescription: ");
    VBoxPanicMsgString(Description ? Description : "<NULL>");
    ASMOutU8(EFI_PANIC_PORT, EFI_PANIC_CMD_END_MSG);

    SetInterruptState(InterruptState);
}


VOID * EFIAPI
DebugClearMemory(OUT VOID *Buffer, IN UINTN Length)
{
    return Buffer;
}


BOOLEAN EFIAPI
DebugAssertEnabled(VOID)
{
    return TRUE;
}


BOOLEAN EFIAPI
DebugPrintEnabled(VOID)
{
    /** @todo some PCD for this so we can disable it in release builds. */
    return TRUE;
}


BOOLEAN EFIAPI
DebugPrintLevelEnabled(IN CONST UINTN ErrorLevel)
{
    /** @todo some PCD for this so we can disable it in release builds. */
    return TRUE;
}


BOOLEAN EFIAPI
DebugCodeEnabled(VOID)
{
    /** @todo ditto */
    return TRUE;
}


BOOLEAN EFIAPI
DebugClearMemoryEnabled(VOID)
{
    return FALSE;
}

