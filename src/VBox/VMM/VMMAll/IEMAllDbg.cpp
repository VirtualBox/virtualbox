/* $Id: IEMAllDbg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Debug and Logging.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_IEM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pgm.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <iprt/errcore.h>
#include <iprt/ctype.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Syscalls                                                                                                                     *
*********************************************************************************************************************************/

#ifdef LOG_ENABLED

# undef  LOG_GROUP
# define LOG_GROUP LOG_GROUP_IEM_SYSCALL

DECLINLINE(char *) iemLogSyscallFormatChr(char pszBuf[4], char ch)
{
    if (RT_C_IS_PRINT(ch))
    {
        pszBuf[0] = '\'';
        pszBuf[1] = ch;
        pszBuf[2] = '\'';
    }
    else if (ch == '\n' || ch == '\r')
    {
        pszBuf[0] = '\'';
        pszBuf[1] = ch == '\n' ? 'n' : 'r';
        pszBuf[2] = ' ';
    }
    else
    {
        pszBuf[0] = ' ';
        pszBuf[1] = '?';
        pszBuf[2] = ' ';
    }
    pszBuf[3] = '\0';
    return pszBuf;
}


/**
 * The output buffer must be 4x + 8 the size of the input.
 */
static char *iemLogSyscallFormatStr(char *pszDst, const char *pszSrc, size_t cchSrc)
{
    char * const pszRet = pszDst;
    *pszDst++ = '\"';
    for (unsigned off = 0; off < cchSrc; off++)
    {
        char const ch = pszSrc[off];
        if (RT_C_IS_PRINT(ch))
            *pszDst++ = ch;
        else
        {
            *pszDst++ = '\\';
            if (ch == '\n')
                *pszDst++ = 'n';
            else if (ch == '\r')
                *pszDst++ = 'r';
            else if (ch == '\t')
                *pszDst++ = 't';
            else if (ch == '\0')
                *pszDst++ = '0';
            else
            {
                static char const s_szHexChars[17] = "0123456789abcdef";
                *pszDst++ = 'x';
                *pszDst++ = s_szHexChars[(uint8_t)ch >> 4];
                *pszDst++ = s_szHexChars[(uint8_t)ch & 0xf];
            }
        }
    }
    *pszDst++ = '\"';
    *pszDst++ = '\0';
    return pszRet;
}


/**
 * VIDEO.
 */
static void iemLogSyscallVgaBiosInt10h(PVMCPUCC pVCpu)
{
    const char *pszSimple;
    switch (pVCpu->cpum.GstCtx.ah)
    {
        case 0x00:
            Log(("VGABIOS INT 10h: AH=00h: set video mode: AL=%#x (BX=%#x)\n", pVCpu->cpum.GstCtx.al, pVCpu->cpum.GstCtx.bx));
            return;
        case 0x01: pszSimple = "set text-mode cursor shape"; break;
        case 0x02: pszSimple = "set cursor position"; break;
        case 0x03: pszSimple = "get cursor position"; break;
        case 0x04: pszSimple = "get light pen position"; break;
        case 0x05: pszSimple = "select active display page"; break;
        case 0x06: pszSimple = "scroll up window"; break;
        case 0x07: pszSimple = "scroll down window"; break;
        case 0x08: pszSimple = "read char & attr at cursor"; break;
        case 0x09: pszSimple = "write char & attr at cursor"; break;
        case 0x0a: pszSimple = "write char only at cursor"; break;
        case 0x0b:
            switch (pVCpu->cpum.GstCtx.bh)
            {
                case 0: pszSimple = "set background/border color"; break;
                case 1: pszSimple = "set palette"; break;
                case 2: pszSimple = "set palette entry"; break;
                default:
                    return;
            }
            break;
        case 0x0c: pszSimple = "write graphics pixel"; break;
        case 0x0d: pszSimple = "read graphics pixel"; break;
        case 0x0e:
        {
            char szChar[4];
            Log(("VGABIOS INT 10h: AH=0eh: teletype output: AL=%#04x %s BH=%#x (pg) BL=%#x\n",
                 pVCpu->cpum.GstCtx.al, iemLogSyscallFormatChr(szChar, pVCpu->cpum.GstCtx.al), pVCpu->cpum.GstCtx.bh,
                 pVCpu->cpum.GstCtx.bl));
            return;
        }
        case 0x13:
        {
            char            szRaw[256] = {0};
            unsigned const  cbToRead   = RT_MIN(RT_ELEMENTS(szRaw), pVCpu->cpum.GstCtx.cx);
            PGMPhysSimpleReadGCPtr(pVCpu, szRaw, pVCpu->cpum.GstCtx.es.u64Base + pVCpu->cpum.GstCtx.bp, cbToRead);
            char            szChars[256+1];
            if (pVCpu->cpum.GstCtx.al & RT_BIT_32(1))
            {
                for (unsigned i = 0; i < cbToRead; i += 2)
                    szChars[i / 2] = RT_C_IS_PRINT(szRaw[i]) ? szRaw[i] : '.';
                szChars[cbToRead / 2] = '\0';
            }
            else
            {
                for (unsigned i = 0; i < cbToRead; i += 2)
                    szChars[i] = RT_C_IS_PRINT(szRaw[i]) ? szRaw[i] : '.';
                szChars[cbToRead] = '\0';
            }
            Log(("VGABIOS INT 10h: AH=13h: write string: AL=%#x BH=%#x (pg) BL=%#x DH=%#x (row) DL=%#x (col) CX=%#x (len) ES:BP=%04x:%04x: '%s' (%.*Rhxs)\n",
                 pVCpu->cpum.GstCtx.al, pVCpu->cpum.GstCtx.bh, pVCpu->cpum.GstCtx.bl, pVCpu->cpum.GstCtx.dh, pVCpu->cpum.GstCtx.dl,
                 pVCpu->cpum.GstCtx.cx, pVCpu->cpum.GstCtx.es.Sel, pVCpu->cpum.GstCtx.bp, szChars, cbToRead, szRaw));
            return;
        }
        default:
            return;
    }
    Log(("VGABIOS INT 10h: AH=%02xh: %s - AL=%#x BX=%#x CX=%#x DX=%#x\n",
         pVCpu->cpum.GstCtx.ah, pszSimple, pVCpu->cpum.GstCtx.al,
         pVCpu->cpum.GstCtx.bx, pVCpu->cpum.GstCtx.cx, pVCpu->cpum.GstCtx.dx));
}


/**
 * BIOS INT 16h.
 */
static void iemLogSyscallBiosInt16h(PVMCPUCC pVCpu)
{
    const char *pszSimple;
    switch (pVCpu->cpum.GstCtx.ah)
    {
        case 0x00: pszSimple = "get keystroke"; break;
        case 0x01: pszSimple = "check for keystroke"; break;
        case 0x02: pszSimple = "get shift flags"; break;
        case 0x03: pszSimple = "set typematic rate and delay"; break;
        case 0x09: pszSimple = "get keyboard functionality"; break;
        case 0x0a: pszSimple = "get keyboard id"; break;
        case 0x10: pszSimple = "get enhanced keystroke"; break;
        case 0x11: pszSimple = "check for enhanced keystroke"; break;
        case 0x12: pszSimple = "get enhanced shift flags"; break;
        default:
            return;
    }
    Log(("BIOS INT 16h: AH=%02xh: %s - AL=%#x BX=%#x CX=%#x DX=%#x\n",
         pVCpu->cpum.GstCtx.ah, pszSimple, pVCpu->cpum.GstCtx.al,
         pVCpu->cpum.GstCtx.bx, pVCpu->cpum.GstCtx.cx, pVCpu->cpum.GstCtx.dx));
}


/**
 * DOS INT 21h.
 */
static void iemLogSyscallDosInt21h(PVMCPUCC pVCpu)
{
    const char *pszSimple;
    switch (pVCpu->cpum.GstCtx.ah)
    {
        case 0x00: pszSimple = "terminate program"; break;
        case 0x01: pszSimple = "read stdin char w/ echo"; break;
        case 0x02:
        {
            char szChar[4];
            Log(("DOS INT 21h: AH=02h: write char to stdout - DL=%#04x %s (AL=%#x BX=%#x CX=%#x DX=%#x BP=%#x SI=%#x DI=%#x)\n",
                 pVCpu->cpum.GstCtx.dl, iemLogSyscallFormatChr(szChar, pVCpu->cpum.GstCtx.dl), pVCpu->cpum.GstCtx.al,
                 pVCpu->cpum.GstCtx.bx, pVCpu->cpum.GstCtx.cx, pVCpu->cpum.GstCtx.dx, pVCpu->cpum.GstCtx.bp,
                 pVCpu->cpum.GstCtx.si, pVCpu->cpum.GstCtx.di));
            return;
        }
        case 0x03: pszSimple = "read char from stdaux"; break;
        case 0x04: pszSimple = "write char to stdaux"; break;
        case 0x05: pszSimple = "write char to printer"; break;
        case 0x06: pszSimple = pVCpu->cpum.GstCtx.dl == 0xff ? "direct console input" : "direct console output"; break;
        case 0x07: pszSimple = "direct character input w/o echo"; break;
        case 0x08: pszSimple = "character input w/o echo"; break;
        case 0x09:
        {
            char            achRaw[128] = {0};
            unsigned const  cbToRead    = RT_MIN(RT_ELEMENTS(achRaw), 0x10000U - pVCpu->cpum.GstCtx.dx);
            PGMPhysSimpleReadGCPtr(pVCpu, achRaw, pVCpu->cpum.GstCtx.ds.u64Base + pVCpu->cpum.GstCtx.dx, cbToRead);
            unsigned        cchRaw = 0;
            while (cchRaw < cbToRead)
                if (achRaw[cchRaw++] == '$')
                    break;

            char szFmt[sizeof(achRaw) * 4 + 16];
            Log(("DOS INT 21h: AH=09h: write string to stdout - DS:DX=%04x:%04x %s\n",
                 pVCpu->cpum.GstCtx.ds.Sel, pVCpu->cpum.GstCtx.dx, iemLogSyscallFormatStr(szFmt, achRaw, cchRaw)));
            return;
        }
        case 0x0a: pszSimple = "buffered input"; break;
        case 0x0b: pszSimple = "get stdin status"; break;
        case 0x0c: pszSimple = "flush buf & read stdin"; break;
        case 0x0d: pszSimple = "disk reset"; break;
        case 0x0e: pszSimple = "select default drive"; break;
        case 0x0f: pszSimple = "open file (fcb)"; break;
        case 0x10: pszSimple = "close file (fcb)"; break;
        case 0x11: pszSimple = "find first (fcb)"; break;
        case 0x12: pszSimple = "find next (fcb)"; break;
        case 0x13: pszSimple = "delete file (fcb)"; break;
        case 0x14: pszSimple = "seq read (fcb)"; break;
        case 0x15: pszSimple = "seq write (fcb)"; break;
        case 0x16: pszSimple = "create/truncate file (fcb)"; break;
        case 0x17: pszSimple = "rename (fcb)"; break;
        default:
            return;
    }
    Log(("DOS INT 21h: AH=%02xh: %s - AL=%#x BX=%#x CX=%#x DX=%#x BP=%#x SI=%#x DI=%#x\n",
         pVCpu->cpum.GstCtx.ah, pszSimple, pVCpu->cpum.GstCtx.al, pVCpu->cpum.GstCtx.bx, pVCpu->cpum.GstCtx.cx,
         pVCpu->cpum.GstCtx.dx, pVCpu->cpum.GstCtx.bp, pVCpu->cpum.GstCtx.si, pVCpu->cpum.GstCtx.di));
}


static void iemLogSyscallWinVxDCall(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    /*
     * Two double words follow the instruction:
     *      1. Service number.
     *      2. VxD identifier.
     */
    uint16_t       auParams[2] = {0, 0};
    RTGCPTR  const GCPtrParams = pVCpu->cpum.GstCtx.cs.u64Base + pVCpu->cpum.GstCtx.rip + cbInstr;
    int rc = PGMPhysSimpleReadGCPtr(pVCpu, auParams, GCPtrParams, sizeof(auParams));
    if (RT_SUCCESS(rc))
    {
        const char    *pszVxD     = NULL;
        const char    *pszService = NULL;
        uint16_t const idVxD      = auParams[1];
        uint16_t const idService  = auParams[0];
        switch (idVxD)
        {
            case 0x0001:
                switch (idService)
                {
                    case 0x0000: pszService = "get version"; break;
                    case 0x0001: pszService = "get current VM handle"; break;
                    case 0x0002: pszService = "test current VM handle"; break;
                    case 0x0003: pszService = "get system VM handle"; break;
                    case 0x0004: pszService = "test system VM handle"; break;
                    case 0x0005: pszService = "validate VM handle"; break;
                    case 0x0006: pszService = "get VMM reenter count"; break;
                    case 0x0007: pszService = "begin reentrant execution"; break;
                    case 0x0008: pszService = "end reentrant execution"; break;
                    case 0x0009: pszService = "install V86 breakpoint"; break;
                    case 0x000a: pszService = "remove V86 breakpoint"; break;
                    case 0x000b: pszService = "allocate V86 callback"; break;
                    case 0x000c: pszService = "allocation PM callback"; break;
                    case 0x000d: pszService = "call when VM returns"; break;
                    case 0x000e: pszService = "schedule global event"; break;
                    case 0x000f: pszService = "schedule VM event"; break;
                    case 0x0010: pszService = "call global event"; break;
                    case 0x0011: pszService = "call VM event"; break;
                    case 0x0012: pszService = "cancel global event"; break;
                    case 0x0013: pszService = "cancel VM event"; break;
                    case 0x0014: pszService = "call priority VM event"; break;
                    case 0x0015: pszService = "cancel priority VM event"; break;
                    case 0x0016: pszService = "get NMI handler address"; break;
                    case 0x0017: pszService = "set NMI handler address"; break;
                    case 0x0018: pszService = "hook NMI event"; break;
                    case 0x0019: pszService = "call when VM interrupts enabled"; break;
                    case 0x001a: pszService = "enable VM interrupts"; break;
                    case 0x001b: pszService = "disable VM interrupts"; break;
                    case 0x001c: pszService = "map flat"; break;
                    case 0x001d: pszService = "map linear to VM address"; break;
                    case 0x001e: pszService = "adjust execution priority"; break;
                    case 0x001f: pszService = "begin critical section"; break;
                    case 0x0020: pszService = "end critical section"; break;
                    case 0x0021: pszService = "end critical section and suspend"; break;
                    case 0x0022: pszService = "claim critical section"; break;
                    case 0x0023: pszService = "release critical section"; break;
                    case 0x0024: pszService = "call when not critical"; break;
                    case 0x0025: pszService = "create semaphore"; break;
                    case 0x0026: pszService = "destroy semaphore"; break;
                    case 0x0027: pszService = "wait on semaphore"; break;
                    case 0x0028: pszService = "signal semaphore"; break;
                    case 0x0029: pszService = "get critical section status"; break;
                    case 0x002a: pszService = "call when task switched"; break;
                    case 0x002b: pszService = "suspend VM"; break;
                    case 0x002c: pszService = "resume VM"; break;
                    case 0x002d: pszService = "no-fail resume VM"; break;
                    case 0x002e: pszService = "nuke VM"; break;
                    case 0x002f: pszService = "crash current VM"; break;
                    case 0x0030: pszService = "get execution focus"; break;
                    case 0x0031: pszService = "set execution focus"; break;
                    case 0x0032: pszService = "get time slice priority"; break;
                    case 0x0033: pszService = "set time slice priority"; break;
                    case 0x0034: pszService = "get time slice granularity"; break;
                    case 0x0035: pszService = "set time slice granularity"; break;
                    case 0x0036: pszService = "get time slice information"; break;
                    case 0x0037: pszService = "adjust execution time"; break;
                    case 0x0038: pszService = "release time slice"; break;
                    case 0x0039: pszService = "wake up VM"; break;
                    case 0x003a: pszService = "call when idle"; break;
                    case 0x003b: pszService = "get next VM handle"; break;
                    case 0x003c: pszService = "set global timeout"; break;
                    case 0x003d: pszService = "set VM timeout"; break;
                    case 0x003e: pszService = "cancel timeout"; break;
                    case 0x003f: pszService = "get system time"; break;
                    case 0x0040: pszService = "get VM execution time"; break;
                    case 0x0041: pszService = "hook V86 interrupt chain"; break;
                    case 0x0042: pszService = "get V86 interrupt vector"; break;
                    case 0x0043: pszService = "set V86 interrupt vector"; break;
                    case 0x0044: pszService = "get PM interrupt vector"; break;
                    case 0x0045: pszService = "set PM interrupt vector"; break;
                    case 0x0046: pszService = "simulate interrupt"; break;
                    case 0x0047: pszService = "simulate IRET"; break;
                    case 0x0048: pszService = "simulate far call"; break;
                    case 0x0049: pszService = "simulate far jump"; break;
                    case 0x004a: pszService = "simulate far RET"; break;
                    case 0x004b: pszService = "simulate far RET N"; break;
                    case 0x004c: pszService = "build interrupt stack frame"; break;
                    case 0x004d: pszService = "simulate push"; break;
                    case 0x004e: pszService = "simulate pop"; break;
                    case 0x004f: pszService = "HeapAllocate"; break;
                    case 0x0050: pszService = "HeapReAllocate"; break;
                    case 0x0051: pszService = "HeapFree"; break;
                    case 0x0052: pszService = "HeapGetSize"; break;
                    case 0x0053: pszService = "PageAllocate"; break;
                    case 0x0054: pszService = "PageReAllocate"; break;
                    case 0x0055: pszService = "PageFree"; break;
                    case 0x0056: pszService = "PageLock"; break;
                    case 0x0057: pszService = "PageUnLock"; break;
                    case 0x0058: pszService = "PageGetSizeAddr"; break;
                    case 0x0059: pszService = "PageGetAllocInfo"; break;
                    case 0x005a: pszService = "GetFreePageCount"; break;
                    case 0x005b: pszService = "GetSysPageCount"; break;
                    case 0x005c: pszService = "GetVMPgCount"; break;
                    case 0x005d: pszService = "MapIntoV86"; break;
                    case 0x005e: pszService = "PhysIntoV86"; break;
                    case 0x005f: pszService = "TestGlobalV86Mem"; break;
                    case 0x0060: pszService = "ModifyPageBits"; break;
                    case 0x0061: pszService = "copy page table"; break;
                    case 0x0062: pszService = "map linear into V86"; break;
                    case 0x0063: pszService = "linear page lock"; break;
                    case 0x0064: pszService = "linear page unlock"; break;
                    case 0x0065: pszService = "SetResetV86Pageabl"; break;
                    case 0x0066: pszService = "GetV86PageableArray"; break;
                    case 0x0067: pszService = "PageCheckLinRange"; break;
                    case 0x0068: pszService = "page out dirty pages"; break;
                    case 0x0069: pszService = "discard pages"; break;
                    case 0x006a: pszService = "GetNulPageHandle"; break;
                    case 0x006b: pszService = "get first V86 page"; break;
                    case 0x006c: pszService = "map physical address to linear address"; break;
                    case 0x006d: pszService = "GetAppFlatDSAlias"; break;
                    case 0x006e: pszService = "SelectorMapFlat"; break;
                    case 0x006f: pszService = "GetDemandPageInfo"; break;
                    case 0x0070: pszService = "GetSetPageOutCount"; break;
                    case 0x0071: pszService = "hook V86 page"; break;
                    case 0x0072: pszService = "assign device V86 pages"; break;
                    case 0x0073: pszService = "deassign device V86 pages"; break;
                    case 0x0074: pszService = "get array of V86 pages for device"; break;
                    case 0x0075: pszService = "SetNULPageAddr"; break;
                    case 0x0076: pszService = "allocate GDT selector"; break;
                    case 0x0077: pszService = "free GDT selector"; break;
                    case 0x0078: pszService = "allocate LDT selector"; break;
                    case 0x0079: pszService = "free LDT selector"; break;
                    case 0x007a: pszService = "BuildDescriptorDWORDs"; break;
                    case 0x007b: pszService = "get descriptor"; break;
                    case 0x007c: pszService = "set descriptor"; break;
                    case 0x007d: pszService = "toggle HMA"; break;
                    case 0x007e: pszService = "get fault hook addresses"; break;
                    case 0x007f: pszService = "hook V86 fault"; break;
                    case 0x0080: pszService = "hook PM fault"; break;
                    case 0x0081: pszService = "hook VMM fault"; break;
                    case 0x0082: pszService = "begin nested V86 execution"; break;
                    case 0x0083: pszService = "begin nested execution"; break;
                    case 0x0084: pszService = "execute V86-mode interrupt"; break;
                    case 0x0085: pszService = "resume execution"; break;
                    case 0x0086: pszService = "end nested execution"; break;
                    case 0x0087: pszService = "allocate PM application callback area"; break;
                    case 0x0088: pszService = "get current PM application callback area"; break;
                    case 0x0089: pszService = "set V86 execution mode"; break;
                    case 0x008a: pszService = "set PM execution mode"; break;
                    case 0x008b: pszService = "begin using locked PM stack"; break;
                    case 0x008c: pszService = "end using locked PM stack"; break;
                    case 0x008d: pszService = "save client state"; break;
                    case 0x008e: pszService = "restore client state"; break;
                    case 0x008f: pszService = "execute VxD interrupt"; break;
                    case 0x0090: pszService = "hook device service"; break;
                    case 0x0091: pszService = "hook device V86 API"; break;
                    case 0x0092: pszService = "hook device PM API"; break;
                    case 0x0093: pszService = "system control (see also #02657)"; break;
                    case 0x0094: pszService = "simulate I/O"; break;
                    case 0x0095: pszService = "install multiple I/O handlers"; break;
                    case 0x0096: pszService = "install I/O handler"; break;
                    case 0x0097: pszService = "enable global trapping"; break;
                    case 0x0098: pszService = "enable local trapping"; break;
                    case 0x0099: pszService = "disable global trapping"; break;
                    case 0x009a: pszService = "disable local trapping"; break;
                    case 0x009b: pszService = "create list"; break;
                    case 0x009c: pszService = "destroy list"; break;
                    case 0x009d: pszService = "allocate list"; break;
                    case 0x009e: pszService = "attach list"; break;
                    case 0x009f: pszService = "attach list tail"; break;
                    case 0x00a0: pszService = "insert into list"; break;
                    case 0x00a1: pszService = "remove from list"; break;
                    case 0x00a2: pszService = "deallocate list"; break;
                    case 0x00a3: pszService = "get first item in list"; break;
                    case 0x00a4: pszService = "get next item in list"; break;
                    case 0x00a5: pszService = "remove first item in list"; break;
                    case 0x00a6: pszService = "add instance item"; break;
                    case 0x00a7: pszService = "allocate device callback area"; break;
                    case 0x00a8: pszService = "allocate global V86 data area"; break;
                    case 0x00a9: pszService = "allocate temporary V86 data area"; break;
                    case 0x00aa: pszService = "free temporary V86 data area"; break;
                    case 0x00ab: pszService = "get decimal integer from profile"; break;
                    case 0x00ac: pszService = "convert decimal string to integer"; break;
                    case 0x00ad: pszService = "get fixed-point number from profile"; break;
                    case 0x00ae: pszService = "convert fixed-point string"; break;
                    case 0x00af: pszService = "get hex integer from profile"; break;
                    case 0x00b0: pszService = "convert hex string to integer"; break;
                    case 0x00b1: pszService = "get boolean value from profile"; break;
                    case 0x00b2: pszService = "convert boolean string"; break;
                    case 0x00b3: pszService = "get string from profile"; break;
                    case 0x00b4: pszService = "get next string from profile"; break;
                    case 0x00b5: pszService = "get environment string"; break;
                    case 0x00b6: pszService = "get exec path"; break;
                    case 0x00b7: pszService = "get configuration directory"; break;
                    case 0x00b8: pszService = "open file"; break;
                    case 0x00b9: pszService = "get PSP segment"; break;
                    case 0x00ba: pszService = "get DOS vectors"; break;
                    case 0x00bb: pszService = "get machine information"; break;
                    case 0x00bc: pszService = "get/set HMA information"; break;
                    case 0x00bd: pszService = "set system exit code"; break;
                    case 0x00be: pszService = "fatal error handler"; break;
                    case 0x00bf: pszService = "fatal memory error"; break;
                    case 0x00c0: pszService = "update system clock"; break;
                    case 0x00c1: pszService = "test if debugger installed"; break;
                    case 0x00c2: pszService = "output debugger string"; break;
                    case 0x00c3: pszService = "output debugger character"; break;
                    case 0x00c4: pszService = "input debugger character"; break;
                    case 0x00c5: pszService = "debugger convert hex to binary"; break;
                    case 0x00c6: pszService = "debugger convert hex to decimal"; break;
                    case 0x00c7: pszService = "debugger test if valid handle"; break;
                    case 0x00c8: pszService = "validate client pointer"; break;
                    case 0x00c9: pszService = "test reentry"; break;
                    case 0x00ca: pszService = "queue debugger string"; break;
                    case 0x00cb: pszService = "log procedure call"; break;
                    case 0x00cc: pszService = "debugger test current VM"; break;
                    case 0x00cd: pszService = "get PM interrupt type"; break;
                    case 0x00ce: pszService = "set PM interrupt type"; break;
                    case 0x00cf: pszService = "get last updated system time"; break;
                    case 0x00d0: pszService = "get last updated VM execution time"; break;
                    case 0x00d1: pszService = "test if double-byte character-set lead byte"; break;
                    case 0x00d2: pszService = "AddFreePhysPage"; break;
                    case 0x00d3: pszService = "PageResetHandlePAddr"; break;
                    case 0x00d4: pszService = "SetLastV86Page"; break;
                    case 0x00d5: pszService = "GetLastV86Page"; break;
                    case 0x00d6: pszService = "MapFreePhysReg"; break;
                    case 0x00d7: pszService = "UnmapFreePhysReg"; break;
                    case 0x00d8: pszService = "XchgFreePhysReg"; break;
                    case 0x00d9: pszService = "SetFreePhysRegCalBk"; break;
                    case 0x00da: pszService = "get next arena (MCB)"; break;
                    case 0x00db: pszService = "get name of ugly TSR"; break;
                    case 0x00dc: pszService = "get debug options"; break;
                    case 0x00dd: pszService = "set physical HMA alias"; break;
                    case 0x00de: pszService = "GetGlblRng0V86IntBase"; break;
                    case 0x00df: pszService = "add global V86 data area"; break;
                    case 0x00e0: pszService = "get/set detailed VM error"; break;
                    case 0x00e1: pszService = "Is_Debug_Chr"; break;
                    case 0x00e2: pszService = "clear monochrome screen"; break;
                    case 0x00e3: pszService = "output character to mono screen"; break;
                    case 0x00e4: pszService = "output string to mono screen"; break;
                    case 0x00e5: pszService = "set current position on mono screen"; break;
                    case 0x00e6: pszService = "get current position on mono screen"; break;
                    case 0x00e7: pszService = "get character from mono screen"; break;
                    case 0x00e8: pszService = "locate byte in ROM"; break;
                    case 0x00e9: pszService = "hook invalid page fault"; break;
                    case 0x00ea: pszService = "unhook invalid page fault"; break;
                    case 0x00eb: pszService = "set delete on exit file"; break;
                    case 0x00ec: pszService = "close VM"; break;
                    case 0x00ed: pszService = "Enable_Touch_1st_Meg"; break;
                    case 0x00ee: pszService = "Disable_Touch_1st_Meg"; break;
                    case 0x00ef: pszService = "install exception handler"; break;
                    case 0x00f0: pszService = "remove exception handler"; break;
                    case 0x00f1: pszService = "Get_Crit_Status_No_Block"; break;
                    case 0x00f2: pszService = "Schedule_VM_RTI_Event"; break;
                    case 0x00f3: pszService = "Trace_Out_Service"; break;
                    case 0x00f4: pszService = "Debug_Out_Service"; break;
                    case 0x00f5: pszService = "Debug_Flags_Service"; break;
                    case 0x00f6: pszService = "VMM add import module name"; break;
                    case 0x00f7: pszService = "VMM Add DDB"; break;
                    case 0x00f8: pszService = "VMM Remove DDB"; break;
                    case 0x00f9: pszService = "get thread time slice priority"; break;
                    case 0x00fa: pszService = "set thread time slice priority"; break;
                    case 0x00fb: pszService = "schedule thread event"; break;
                    case 0x00fc: pszService = "cancel thread event"; break;
                    case 0x00fd: pszService = "set thread timeout"; break;
                    case 0x00fe: pszService = "set asynchronous timeout"; break;
                    case 0x00ff: pszService = "AllocatreThreadDataSlot"; break;
                    case 0x0100: pszService = "FreeThreadDataSlot"; break;
                    case 0x0101: pszService = "create Mutex"; break;
                    case 0x0102: pszService = "destroy Mutex"; break;
                    case 0x0103: pszService = "get Mutex owner"; break;
                    case 0x0104: pszService = "call when thread switched"; break;
                    case 0x0105: pszService = "create thread"; break;
                    case 0x0106: pszService = "start thread"; break;
                    case 0x0107: pszService = "terminate thread"; break;
                    case 0x0108: pszService = "get current thread handle"; break;
                    case 0x0109: pszService = "test current thread handle"; break;
                    case 0x010a: pszService = "Get_Sys_Thread_Handle"; break;
                    case 0x010b: pszService = "Test_Sys_Thread_Handle"; break;
                    case 0x010c: pszService = "Validate_Thread_Handle"; break;
                    case 0x010d: pszService = "Get_Initial_Thread_Handle"; break;
                    case 0x010e: pszService = "Test_Initial_Thread_Handle"; break;
                    case 0x010f: pszService = "Debug_Test_Valid_Thread_Handle"; break;
                    case 0x0110: pszService = "Debug_Test_Cur_Thread"; break;
                    case 0x0111: pszService = "VMM_GetSystemInitState"; break;
                    case 0x0112: pszService = "Cancel_Call_When_Thread_Switched"; break;
                    case 0x0113: pszService = "Get_Next_Thread_Handle"; break;
                    case 0x0114: pszService = "Adjust_Thread_Exec_Priority"; break;
                    case 0x0115: pszService = "Deallocate_Device_CB_Area"; break;
                    case 0x0116: pszService = "Remove_IO_Handler"; break;
                    case 0x0117: pszService = "Remove_Mult_IO_Handlers"; break;
                    case 0x0118: pszService = "unhook V86 interrupt chain"; break;
                    case 0x0119: pszService = "unhook V86 fault handler"; break;
                    case 0x011a: pszService = "unhook PM fault handler"; break;
                    case 0x011b: pszService = "unhook VMM fault handler"; break;
                    case 0x011c: pszService = "unhook device service"; break;
                    case 0x011d: pszService = "PageReserve"; break;
                    case 0x011e: pszService = "PageCommit"; break;
                    case 0x011f: pszService = "PageDecommit"; break;
                    case 0x0120: pszService = "PagerRegister"; break;
                    case 0x0121: pszService = "PagerQuery"; break;
                    case 0x0122: pszService = "PagerDeregister"; break;
                    case 0x0123: pszService = "ContextCreate"; break;
                    case 0x0124: pszService = "ContextDestroy"; break;
                    case 0x0125: pszService = "PageAttach"; break;
                    case 0x0126: pszService = "PageFlush"; break;
                    case 0x0127: pszService = "SignalID"; break;
                    case 0x0128: pszService = "PageCommitPhys"; break;
                    case 0x0129: pszService = "Register_Win32_Services"; break;
                    case 0x012a: pszService = "Cancel_Call_When_Not_Critical"; break;
                    case 0x012b: pszService = "Cancel_Call_When_Idle"; break;
                    case 0x012c: pszService = "Cancel_Call_When_Task_Switched"; break;
                    case 0x012d: pszService = "Debug_Printf_Service"; break;
                    case 0x012e: pszService = "enter Mutex"; break;
                    case 0x012f: pszService = "leave Mutex"; break;
                    case 0x0130: pszService = "simulate VM I/O"; break;
                    case 0x0131: pszService = "Signal_Semaphore_No_Switch"; break;
                    case 0x0132: pszService = "MMSwitchContext"; break;
                    case 0x0133: pszService = "MMModifyPermissions"; break;
                    case 0x0134: pszService = "MMQuery"; break;
                    case 0x0135: pszService = "EnterMustComplete"; break;
                    case 0x0136: pszService = "LeaveMustComplete"; break;
                    case 0x0137: pszService = "ResumeExecMustComplete"; break;
                    case 0x0138: pszService = "get thread termination status"; break;
                    case 0x0139: pszService = "GetInstanceInfo"; break;
                    case 0x013a: pszService = "ExecIntMustComplete"; break;
                    case 0x013b: pszService = "ExecVxDIntMustComplete"; break;
                    case 0x013c: pszService = "begin V86 serialization"; break;
                    case 0x013d: pszService = "unhook V86 page"; break;
                    case 0x013e: pszService = "VMM_GetVxDLocationList"; break;
                    case 0x013f: pszService = "VMM_GetDDBList get start of VxD chain"; break;
                    case 0x0140: pszService = "unhook NMI event"; break;
                    case 0x0141: pszService = "Get_Instanced_V86_Int_Vector"; break;
                    case 0x0142: pszService = "get or set real DOS PSP"; break;
                    case 0x0143: pszService = "call priority thread event"; break;
                    case 0x0144: pszService = "Get_System_Time_Address"; break;
                    case 0x0145: pszService = "Get_Crit_Status_Thread"; break;
                    case 0x0146: pszService = "Get_DDB"; break;
                    case 0x0147: pszService = "Directed_Sys_Control"; break;
                    case 0x0148: pszService = "RegOpenKey"; break;
                    case 0x0149: pszService = "RegCloseKey"; break;
                    case 0x014a: pszService = "RegCreateKey"; break;
                    case 0x014b: pszService = "RegDeleteKey"; break;
                    case 0x014c: pszService = "RegEnumKey"; break;
                    case 0x014d: pszService = "RegQueryValue"; break;
                    case 0x014e: pszService = "RegSetValue"; break;
                    case 0x014f: pszService = "RegDeleteValue"; break;
                    case 0x0150: pszService = "RegEnumValue"; break;
                    case 0x0151: pszService = "RegQueryValueEx"; break;
                    case 0x0152: pszService = "RegSetValueEx"; break;
                    case 0x0153: pszService = "CallRing3"; break;
                    case 0x0154: pszService = "Exec_PM_Int"; break;
                    case 0x0155: pszService = "RegFlushKey"; break;
                    case 0x0156: pszService = "PageCommitContig"; break;
                    case 0x0157: pszService = "GetCurrentContext"; break;
                    case 0x0158: pszService = "LocalizeSprintf"; break;
                    case 0x0159: pszService = "LocalizeStackSprintf"; break;
                    case 0x015a: pszService = "Call_Restricted_Event"; break;
                    case 0x015b: pszService = "Cancel_Restricted_Event"; break;
                    case 0x015c: pszService = "Register_PEF_Provider"; break;
                    case 0x015d: pszService = "GetPhysPageInfo"; break;
                    case 0x015e: pszService = "RegQueryInfoKey"; break;
                    case 0x015f: pszService = "MemArb_Reserve_Pages"; break;
                    case 0x0160: pszService = "Time_Slice_Sys_VM_Idle"; break;
                    case 0x0161: pszService = "Time_Slice_Sleep"; break;
                    case 0x0162: pszService = "Boost_With_Decay"; break;
                    case 0x0163: pszService = "Set_Inversion_Pri"; break;
                    case 0x0164: pszService = "Reset_Inversion_Pri"; break;
                    case 0x0165: pszService = "Release_Inversion_Pri"; break;
                    case 0x0166: pszService = "Get_Thread_Win32_Pri"; break;
                    case 0x0167: pszService = "Set_Thread_Win32_Pri"; break;
                    case 0x0168: pszService = "Set_Thread_Static_Boost"; break;
                    case 0x0169: pszService = "Set_VM_Static_Boost"; break;
                    case 0x016a: pszService = "Release_Inversion_Pri_ID"; break;
                    case 0x016b: pszService = "Attach_Thread_To_Group"; break;
                    case 0x016c: pszService = "Detach_Thread_From_Group"; break;
                    case 0x016d: pszService = "Set_Group_Static_Boost"; break;
                    case 0x016e: pszService = "GetRegistryPath"; break;
                    case 0x016f: pszService = "GetRegistryKey"; break;
                    case 0x0170: pszService = "CleanupNestedExec"; break;
                    case 0x0171: pszService = "RegRemapPreDefKey"; break;
                    case 0x0172: pszService = "End_V86_Serialization"; break;
                    case 0x0173: pszService = "Assert_Range"; break;
                    case 0x0174: pszService = "Sprintf"; break;
                    case 0x0175: pszService = "PageChangePager"; break;
                    case 0x0176: pszService = "RegCreateDynKey"; break;
                    case 0x0177: pszService = "RegQMulti"; break;
                    case 0x0178: pszService = "Boost_Thread_With_VM"; break;
                    case 0x0179: pszService = "Get_Boot_Flags"; break;
                    case 0x017a: pszService = "Set_Boot_Flags"; break;
                    case 0x017b: pszService = "lstrcpyn"; break;
                    case 0x017c: pszService = "lstrlen"; break;
                    case 0x017d: pszService = "lmemcpy"; break;
                    case 0x017e: pszService = "GetVxDName"; break;
                    case 0x017f: pszService = "Force_Mutexes_Free"; break;
                    case 0x0180: pszService = "Restore_Forced_Mutexes"; break;
                    case 0x0181: pszService = "AddReclaimableItem"; break;
                    case 0x0182: pszService = "SetReclaimableItem"; break;
                    case 0x0183: pszService = "EnumReclaimableItem"; break;
                    case 0x0184: pszService = "Time_Slice_Wake_Sys_VM"; break;
                    case 0x0185: pszService = "VMM_Replace_Global_Environment"; break;
                    case 0x0186: pszService = "Begin_Non_Serial_Nest_V86_Exec"; break;
                    case 0x0187: pszService = "Get_Nest_Exec_Status"; break;
                    case 0x0188: pszService = "Open_Boot_Log"; break;
                    case 0x0189: pszService = "Write_Boot_Log"; break;
                    case 0x018a: pszService = "Close_Boot_Log"; break;
                    case 0x018b: pszService = "EnableDisable_Boot_Log"; break;
                    case 0x018c: pszService = "Call_On_My_Stack"; break;
                    case 0x018d: pszService = "Get_Inst_V86_Int_Vec_Base"; break;
                    case 0x018e: pszService = "lstrcmpi"; break;
                    case 0x018f: pszService = "strupr"; break;
                    case 0x0190: pszService = "Log_Fault_Call_Out"; break;
                    case 0x0191: pszService = "AtEventTime"; break;
                }
                pszVxD = "VMM";
                break;
            case 0x0002: pszVxD = "DEBUG"; break;
            case 0x0003: pszVxD = "VPICD"; break;
            case 0x0004: pszVxD = "VDMAD"; break;
            case 0x0005: pszVxD = "VTD"; break;
            case 0x0006: pszVxD = "V86MMGR"; break;
            case 0x0007: pszVxD = "PageSwap"; break;
            case 0x0009: pszVxD = "REBOOT"; break;
            case 0x000A: pszVxD = "VDD"; break;
            case 0x000B: pszVxD = "VSD"; break;
            case 0x000C: pszVxD = "VMD / VMOUSE"; break;
            case 0x000D: pszVxD = "VKD"; break;
            case 0x000E: pszVxD = "VCD"; break;
            case 0x0010: pszVxD = "BlockDev / IOS"; break;
            case 0x0011: pszVxD = "VMCPD"; break;
            case 0x0012: pszVxD = "EBIOS"; break;
            case 0x0014: pszVxD = "VNETBIOS"; break;
            case 0x0015: pszVxD = "DOSMGR"; break;
            case 0x0017: pszVxD = "SHELL"; break;
            case 0x0018: pszVxD = "VMPoll"; break;
            case 0x001A: pszVxD = "DOSNET"; break;
            case 0x001B: pszVxD = "VFD"; break;
            case 0x001C: pszVxD = "LoadHi"; break;
            case 0x0020: pszVxD = "Int13"; break;
            case 0x0021: pszVxD = "PAGEFILE"; break;
            case 0x0026: pszVxD = "VPOWERD"; break;
            case 0x0027: pszVxD = "VXDLDR"; break;
            case 0x0028: pszVxD = "NDIS"; break;
            case 0x002A: pszVxD = "VWIN32"; break;
            case 0x002B: pszVxD = "VCOMM"; break;
            case 0x002C: pszVxD = "SPOOLER"; break;
            case 0x0032: pszVxD = "VSERVER"; break;
            case 0x0033: pszVxD = "CONFIGMG"; break;
            case 0x0034: pszVxD = "DWCFGMG.SYS"; break;
            case 0x0036: pszVxD = "VFBACKUP"; break;
            case 0x0037: pszVxD = "VMINI / ENABLE"; break;
            case 0x0038: pszVxD = "VCOND"; break;
            case 0x003D: pszVxD = "BIOS"; break;
            case 0x003E: pszVxD = "WSOCK"; break;
            case 0x0040: pszVxD = "IFSMgr"; break;
            case 0x0041: pszVxD = "VCDFSD"; break;
            case 0x0048: pszVxD = "PERF"; break;
            case 0x004A: pszVxD = "MTRR"; break;
            case 0x004B: pszVxD = "NTKERN"; break;
            case 0x011F: pszVxD = "VFLATD"; break;
            case 0x0449: pszVxD = "vjoyd"; break;
            case 0x044A: pszVxD = "mmdevldr"; break;
            case 0x0480: pszVxD = "VNetSup"; break;
            case 0x0481: pszVxD = "VREDIR"; break;
            case 0x0483: pszVxD = "VSHARE"; break;
            case 0x0487: pszVxD = "NWLINK"; break;
            case 0x0488: pszVxD = "VTDI"; break;
            case 0x0489: pszVxD = "VIP"; break;
            case 0x048A: pszVxD = "MSTCP"; break;
            case 0x048B: pszVxD = "VCACHE"; break;
            case 0x048E: pszVxD = "NWREDIR"; break;
            case 0x0491: pszVxD = "FILESEC"; break;
            case 0x0492: pszVxD = "NWSERVER"; break;
            case 0x0493: pszVxD = "MSSP / NWSP"; break;
            case 0x0494: pszVxD = "NSCL"; break;
            case 0x0495: pszVxD = "AFVXD"; break;
            case 0x0496: pszVxD = "NDIS2SUP"; break;
            case 0x0498: pszVxD = "Splitter"; break;
            case 0x0499: pszVxD = "PPPMAC"; break;
            case 0x049A: pszVxD = "VDHCP"; break;
            case 0x049B: pszVxD = "VNBT"; break;
            case 0x049D: pszVxD = "LOGGER"; break;
            case 0x097C: pszVxD = "PCCARD"; break;
            case 0x3098: pszVxD = "VstlthD"; break; /* QEMM */
            case 0x30F6: pszVxD = "WSVV"; break;
            case 0x33FC: pszVxD = "ASPIENUM"; break;
            case 0x357E: pszVxD = "DSOUND"; break;
            case 0x39E6: pszVxD = "A3D"; break;
            case 0x3BFD: pszVxD = "CWCPROXY"; break;
            case 0x3C78: pszVxD = "VGARTD"; break;

            default:
            {
                uint8_t abOpcodes[16] = {0};
                cbInstr = RT_MIN(cbInstr, 16);
                rc = PGMPhysSimpleReadGCPtr(pVCpu, abOpcodes, GCPtrParams - cbInstr, cbInstr);
                Log2(("VxD syscall: VxD=%#x Service=%#x - Unknown at %04x:%08RX64: %.*Rhxs - %.*Rhxs (%Rrc)\n",
                      idVxD, idService, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
                      cbInstr, abOpcodes, sizeof(auParams), auParams, rc));
                return;
            }
        }
        if (pszService)
            Log2(("VxD syscall: VxD=%#04x Service=%#04x - %s: %s\n", idVxD, idService, pszVxD, pszService));
        else
            Log2(("VxD syscall: VxD=%#04x Service=%#04x - %s\n", idVxD, idService, pszVxD));
    }
    else
        Log2(("VxD syscall: unable to read parameters at %RGv: %Rrc\n", GCPtrParams, rc));
}


static void iemLogSyscallLinuxX86Int80(PVMCPUCC pVCpu)
{
    uint32_t       fStrArgs = 0;
    const char    *pszName;
    int            cArgs;
    uint32_t const uSysCall = pVCpu->cpum.GstCtx.eax;
    switch (uSysCall)
    {
        case   0: cArgs = -1; pszName = "restart_syscall"; break;
        case   1: cArgs =  1; pszName = "exit"; break;
        case   2: cArgs = -1; pszName = "fork"; break;
        case   3: cArgs =  3; pszName = "read"; break;
        case   4: cArgs =  3; pszName = "write"; break;
        case   5: cArgs =  3; pszName = "open"; fStrArgs = 1; break;
        case   6: cArgs =  1; pszName = "close"; break;
        case   7: cArgs =  3; pszName = "waitpid"; break;
        case   8: cArgs =  2; pszName = "creat"; break;
        case   9: cArgs =  2; pszName = "link"; fStrArgs = 1|2; break;
        case  10: cArgs =  1; pszName = "unlink"; fStrArgs = 1; break;
        case  11: cArgs =  3; pszName = "execve"; fStrArgs = 1; break;
        case  12: cArgs =  1; pszName = "chdir"; fStrArgs = 1; break;
        case  13: cArgs =  1; pszName = "time"; break;
        case  14: cArgs =  3; pszName = "mknod"; fStrArgs = 1; break;
        case  15: cArgs =  2; pszName = "chmod"; fStrArgs = 1; break;
        case  16: cArgs =  3; pszName = "lchown"; fStrArgs = 1; break;
        case  17: cArgs = -1; pszName = "break;"; break;
        case  18: cArgs =  2; pszName = "oldstat"; fStrArgs = 1; break;
        case  19: cArgs =  3; pszName = "lseek"; break;
        case  20: cArgs = -1; pszName = "getpid"; break;
        case  21: cArgs =  5; pszName = "mount"; fStrArgs = 1|2|4; break;
        case  22: cArgs =  1; pszName = "umount"; fStrArgs = 1; break;
        case  23: cArgs =  1; pszName = "setuid"; break;
        case  24: cArgs = -1; pszName = "getuid"; break;
        case  25: cArgs =  1; pszName = "stime"; break;
        case  26: cArgs =  4; pszName = "ptrace"; break;
        case  27: cArgs =  1; pszName = "alarm"; break;
        case  28: cArgs =  2; pszName = "oldfstat"; break;
        case  29: cArgs = -1; pszName = "pause"; break;
        case  30: cArgs =  2; pszName = "utime"; fStrArgs = 1; break;
        case  31: cArgs = -1; pszName = "stty"; break;
        case  32: cArgs = -1; pszName = "gtty"; break;
        case  33: cArgs =  2; pszName = "access"; fStrArgs = 1; break;
        case  34: cArgs =  1; pszName = "nice"; break;
        case  35: cArgs = -1; pszName = "ftime"; break;
        case  36: cArgs = -1; pszName = "sync"; break;
        case  37: cArgs =  2; pszName = "kill"; break;
        case  38: cArgs =  2; pszName = "rename"; fStrArgs = 1|2; break;
        case  39: cArgs =  2; pszName = "mkdir"; fStrArgs = 1; break;
        case  40: cArgs =  1; pszName = "rmdir"; fStrArgs = 1; break;
        case  41: cArgs =  1; pszName = "dup"; break;
        case  42: cArgs =  1; pszName = "pipe"; break;
        case  43: cArgs =  1; pszName = "times"; fStrArgs = 1; break;
        case  44: cArgs = -1; pszName = "prof"; break;
        case  45: cArgs =  1; pszName = "brk"; break;
        case  46: cArgs =  1; pszName = "setgid"; break;
        case  47: cArgs = -1; pszName = "getgid"; break;
        case  48: cArgs =  2; pszName = "signal"; break;
        case  49: cArgs = -1; pszName = "geteuid"; break;
        case  50: cArgs = -1; pszName = "getegid"; break;
        case  51: cArgs =  1; pszName = "acct"; break;
        case  52: cArgs =  2; pszName = "umount2"; fStrArgs = 1; break;
        case  53: cArgs = -1; pszName = "lock"; break;
        case  54: cArgs =  3; pszName = "ioctl"; break;
        case  55: cArgs =  3; pszName = "fcntl"; break;
        case  56: cArgs = -1; pszName = "mpx"; break;
        case  57: cArgs =  2; pszName = "setpgid"; break;
        case  58: cArgs = -1; pszName = "ulimit"; break;
        case  59: cArgs =  1; pszName = "oldolduname"; break;
        case  60: cArgs =  1; pszName = "umask"; break;
        case  61: cArgs =  1; pszName = "chroot"; fStrArgs = 1; break;
        case  62: cArgs =  2; pszName = "ustat"; break;
        case  63: cArgs =  2; pszName = "dup2"; break;
        case  64: cArgs = -1; pszName = "getppid"; break;
        case  65: cArgs = -1; pszName = "getpgrp"; break;
        case  66: cArgs = -1; pszName = "setsid"; break;
        case  67: cArgs =  3; pszName = "sigaction"; break;
        case  68: cArgs = -1; pszName = "sgetmask"; break;
        case  69: cArgs =  1; pszName = "ssetmask"; break;
        case  70: cArgs =  2; pszName = "setreuid"; break;
        case  71: cArgs =  2; pszName = "setregid"; break;
        case  72: cArgs =  1; pszName = "sigsuspend"; break;
        case  73: cArgs =  1; pszName = "sigpending"; break;
        case  74: cArgs =  2; pszName = "sethostname"; fStrArgs = 1; break;
        case  75: cArgs =  2; pszName = "setrlimit"; break;
        case  76: cArgs =  2; pszName = "getrlimit"; break;
        case  77: cArgs =  2; pszName = "getrusage"; break;
        case  78: cArgs =  2; pszName = "gettimeofday"; break;
        case  79: cArgs =  2; pszName = "settimeofday"; break;
        case  80: cArgs =  2; pszName = "getgroups"; break;
        case  81: cArgs =  2; pszName = "setgroups"; break;
        case  82: cArgs =  1; pszName = "select"; break;
        case  83: cArgs =  2; pszName = "symlink"; fStrArgs = 1|2; break;
        case  84: cArgs =  2; pszName = "oldlstat"; fStrArgs = 1; break;
        case  85: cArgs =  3; pszName = "readlink"; fStrArgs = 1; break;
        case  86: cArgs =  1; pszName = "uselib"; break;
        case  87: cArgs =  2; pszName = "swapon"; fStrArgs = 1; break;
        case  88: cArgs =  4; pszName = "reboot"; break;
        case  89: cArgs =  3; pszName = "readdir"; break;
        case  90: cArgs =  1; pszName = "mmap"; break;
        case  91: cArgs =  2; pszName = "munmap"; break;
        case  92: cArgs =  2; pszName = "truncate"; fStrArgs = 1; break;
        case  93: cArgs =  2; pszName = "ftruncate"; break;
        case  94: cArgs =  2; pszName = "fchmod"; break;
        case  95: cArgs =  3; pszName = "fchown"; break;
        case  96: cArgs =  2; pszName = "getpriority"; break;
        case  97: cArgs =  3; pszName = "setpriority"; break;
        case  98: cArgs = -1; pszName = "profil"; break;
        case  99: cArgs =  2; pszName = "statfs"; fStrArgs = 1; break;
        case 100: cArgs =  2; pszName = "fstatfs"; break;
        case 101: cArgs =  3; pszName = "ioperm"; break;
        case 102: cArgs =  2; pszName = "socketcall"; break;
        case 103: cArgs =  3; pszName = "syslog"; break;
        case 104: cArgs =  3; pszName = "setitimer"; break;
        case 105: cArgs =  2; pszName = "getitimer"; break;
        case 106: cArgs =  2; pszName = "stat"; fStrArgs = 1; break;
        case 107: cArgs =  2; pszName = "lstat"; fStrArgs = 1; break;
        case 108: cArgs =  2; pszName = "fstat"; break;
        case 109: cArgs =  1; pszName = "olduname"; break;
        case 110: cArgs =  1; pszName = "iopl"; break;
        case 111: cArgs = -1; pszName = "vhangup"; break;
        case 112: cArgs = -1; pszName = "idle"; break;
        case 113: cArgs =  1; pszName = "vm86old"; break;
        case 114: cArgs =  4; pszName = "wait4"; break;
        case 115: cArgs =  1; pszName = "swapoff"; fStrArgs = 1; break;
        case 116: cArgs =  1; pszName = "sysinfo"; break;
        case 117: cArgs =  6; pszName = "ipc"; break;
        case 118: cArgs =  1; pszName = "fsync"; break;
        case 119: cArgs = -1; pszName = "sigreturn"; break;
        case 120: cArgs =  5; pszName = "clone"; break;
        case 121: cArgs =  2; pszName = "setdomainname"; fStrArgs = 1; break;
        case 122: cArgs =  1; pszName = "uname"; break;
        case 123: cArgs =  3; pszName = "modify_ldt"; break;
        case 124: cArgs =  1; pszName = "adjtimex"; break;
        case 125: cArgs =  3; pszName = "mprotect"; break;
        case 126: cArgs =  3; pszName = "sigprocmask"; break;
        case 127: cArgs = -1; pszName = "create_module"; fStrArgs = 1; break;
        case 128: cArgs =  3; pszName = "init_module"; break;
        case 129: cArgs =  2; pszName = "delete_module"; fStrArgs = 1; break;
        case 130: cArgs = -1; pszName = "get_kernel_syms"; break;
        case 131: cArgs =  4; pszName = "quotactl"; break;
        case 132: cArgs =  1; pszName = "getpgid"; break;
        case 133: cArgs =  1; pszName = "fchdir"; break;
        case 134: cArgs = -1; pszName = "bdflush"; break;
        case 135: cArgs =  3; pszName = "sysfs"; break;
        case 136: cArgs =  1; pszName = "personality"; break;
        case 137: cArgs = -1; pszName = "afs_syscall"; break;
        case 138: cArgs =  1; pszName = "setfsuid"; break;
        case 139: cArgs =  1; pszName = "setfsgid"; break;
        case 140: cArgs =  5; pszName = "_llseek"; break;
        case 141: cArgs =  3; pszName = "getdents"; break;
        case 142: cArgs =  5; pszName = "_newselect"; break;
        case 143: cArgs =  2; pszName = "flock"; break;
        case 144: cArgs =  3; pszName = "msync"; break;
        case 145: cArgs =  3; pszName = "readv"; break;
        case 146: cArgs =  3; pszName = "writev"; break;
        case 147: cArgs =  1; pszName = "getsid"; break;
        case 148: cArgs =  1; pszName = "fdatasync"; break;
        case 149: cArgs = -1; pszName = "_sysctl"; break;
        case 150: cArgs =  2; pszName = "mlock"; break;
        case 151: cArgs =  2; pszName = "munlock"; break;
        case 152: cArgs =  1; pszName = "mlockall"; break;
        case 153: cArgs = -1; pszName = "munlockall"; break;
        case 154: cArgs =  2; pszName = "sched_setparam"; break;
        case 155: cArgs =  2; pszName = "sched_getparam"; break;
        case 156: cArgs =  3; pszName = "sched_setscheduler"; break;
        case 157: cArgs =  1; pszName = "sched_getscheduler"; break;
        case 158: cArgs = -1; pszName = "sched_yield"; break;
        case 159: cArgs =  1; pszName = "sched_get_priority_max"; break;
        case 160: cArgs =  1; pszName = "sched_get_priority_min"; break;
        case 161: cArgs =  2; pszName = "sched_rr_get_interval"; break;
        case 162: cArgs =  2; pszName = "nanosleep"; break;
        case 163: cArgs =  5; pszName = "mremap"; break;
        case 164: cArgs =  3; pszName = "setresuid"; break;
        case 165: cArgs =  3; pszName = "getresuid"; break;
        case 166: cArgs =  2; pszName = "vm86"; break;
        case 167: cArgs = -1; pszName = "query_module"; break;
        case 168: cArgs =  3; pszName = "poll"; break;
        case 169: cArgs = -1; pszName = "nfsservctl"; break;
        case 170: cArgs =  3; pszName = "setresgid"; break;
        case 171: cArgs =  3; pszName = "getresgid"; break;
        case 172: cArgs =  5; pszName = "prctl"; break;
        case 173: cArgs = -1; pszName = "rt_sigreturn"; break;
        case 174: cArgs =  4; pszName = "rt_sigaction"; break;
        case 175: cArgs =  4; pszName = "rt_sigprocmask"; break;
        case 176: cArgs =  2; pszName = "rt_sigpending"; break;
        case 177: cArgs =  4; pszName = "rt_sigtimedwait"; break;
        case 178: cArgs =  3; pszName = "rt_sigqueueinfo"; break;
        case 179: cArgs =  2; pszName = "rt_sigsuspend"; break;
        case 180: cArgs =  5; pszName = "pread64"; break;
        case 181: cArgs =  5; pszName = "pwrite64"; break;
        case 182: cArgs =  3; pszName = "chown"; break;
        case 183: cArgs =  2; pszName = "getcwd"; break;
        case 184: cArgs =  2; pszName = "capget"; break;
        case 185: cArgs =  2; pszName = "capset"; break;
        case 186: cArgs =  2; pszName = "sigaltstack"; break;
        case 187: cArgs =  4; pszName = "sendfile"; break;
        case 188: cArgs = -1; pszName = "getpmsg"; break;
        case 189: cArgs = -1; pszName = "putpmsg"; break;
        case 190: cArgs = -1; pszName = "vfork"; break;
        case 191: cArgs =  2; pszName = "ugetrlimit"; break;
        case 192: cArgs =  6; pszName = "mmap2"; break;
        case 193: cArgs =  3; pszName = "truncate64"; break;
        case 194: cArgs =  3; pszName = "ftruncate64"; break;
        case 195: cArgs =  2; pszName = "stat64"; break;
        case 196: cArgs =  2; pszName = "lstat64"; break;
        case 197: cArgs =  2; pszName = "fstat64"; break;
        case 198: cArgs =  3; pszName = "lchown32"; break;
        case 199: cArgs = -1; pszName = "getuid32"; break;
        case 200: cArgs = -1; pszName = "getgid32"; break;
        case 201: cArgs = -1; pszName = "geteuid32"; break;
        case 202: cArgs = -1; pszName = "getegid32"; break;
        case 203: cArgs =  2; pszName = "setreuid32"; break;
        case 204: cArgs =  2; pszName = "setregid32"; break;
        case 205: cArgs =  2; pszName = "getgroups32"; break;
        case 206: cArgs =  2; pszName = "setgroups32"; break;
        case 207: cArgs =  3; pszName = "fchown32"; break;
        case 208: cArgs =  3; pszName = "setresuid32"; break;
        case 209: cArgs =  3; pszName = "getresuid32"; break;
        case 210: cArgs =  3; pszName = "setresgid32"; break;
        case 211: cArgs =  3; pszName = "getresgid32"; break;
        case 212: cArgs =  3; pszName = "chown32"; break;
        case 213: cArgs =  1; pszName = "setuid32"; break;
        case 214: cArgs =  1; pszName = "setgid32"; break;
        case 215: cArgs =  1; pszName = "setfsuid32"; break;
        case 216: cArgs =  1; pszName = "setfsgid32"; break;
        case 217: cArgs =  2; pszName = "pivot_root"; break;
        case 218: cArgs =  3; pszName = "mincore"; break;
        case 219: cArgs =  3; pszName = "madvise"; break;
        case 220: cArgs =  3; pszName = "getdents64"; break;
        case 221: cArgs =  3; pszName = "fcntl64"; break;
        case 224: cArgs = -1; pszName = "gettid"; break;
        case 225: cArgs =  4; pszName = "readahead"; break;
        case 226: cArgs =  5; pszName = "setxattr"; break;
        case 227: cArgs =  5; pszName = "lsetxattr"; break;
        case 228: cArgs =  5; pszName = "fsetxattr"; break;
        case 229: cArgs =  4; pszName = "getxattr"; break;
        case 230: cArgs =  4; pszName = "lgetxattr"; break;
        case 231: cArgs =  4; pszName = "fgetxattr"; break;
        case 232: cArgs =  3; pszName = "listxattr"; break;
        case 233: cArgs =  3; pszName = "llistxattr"; break;
        case 234: cArgs =  3; pszName = "flistxattr"; break;
        case 235: cArgs =  2; pszName = "removexattr"; break;
        case 236: cArgs =  2; pszName = "lremovexattr"; break;
        case 237: cArgs =  2; pszName = "fremovexattr"; break;
        case 238: cArgs =  2; pszName = "tkill"; break;
        case 239: cArgs =  4; pszName = "sendfile64"; break;
        case 240: cArgs =  6; pszName = "futex"; break;
        case 241: cArgs =  3; pszName = "sched_setaffinity"; break;
        case 242: cArgs =  3; pszName = "sched_getaffinity"; break;
        case 243: cArgs =  1; pszName = "set_thread_area"; break;
        case 244: cArgs =  1; pszName = "get_thread_area"; break;
        case 245: cArgs =  2; pszName = "io_setup"; break;
        case 246: cArgs =  1; pszName = "io_destroy"; break;
        case 247: cArgs =  5; pszName = "io_getevents"; break;
        case 248: cArgs =  3; pszName = "io_submit"; break;
        case 249: cArgs =  3; pszName = "io_cancel"; break;
        case 250: cArgs =  5; pszName = "fadvise64"; break;
        case 252: cArgs =  1; pszName = "exit_group"; break;
        case 253: cArgs = -1; pszName = "lookup_dcookie"; break;
        case 254: cArgs =  1; pszName = "epoll_create"; break;
        case 255: cArgs =  4; pszName = "epoll_ctl"; break;
        case 256: cArgs =  4; pszName = "epoll_wait"; break;
        case 257: cArgs =  5; pszName = "remap_file_pages"; break;
        case 258: cArgs =  1; pszName = "set_tid_address"; break;
        case 259: cArgs =  3; pszName = "timer_create"; break;
        case 260: cArgs =  4; pszName = "timer_settime"; break;
        case 261: cArgs =  2; pszName = "timer_gettime"; break;
        case 262: cArgs =  1; pszName = "timer_getoverrun"; break;
        case 263: cArgs =  1; pszName = "timer_delete"; break;
        case 264: cArgs =  2; pszName = "clock_settime"; break;
        case 265: cArgs =  2; pszName = "clock_gettime"; break;
        case 266: cArgs =  2; pszName = "clock_getres"; break;
        case 267: cArgs =  4; pszName = "clock_nanosleep"; break;
        case 268: cArgs =  3; pszName = "statfs64"; break;
        case 269: cArgs =  3; pszName = "fstatfs64"; break;
        case 270: cArgs =  3; pszName = "tgkill"; break;
        case 271: cArgs =  2; pszName = "utimes"; break;
        case 272: cArgs =  6; pszName = "fadvise64_64"; break;
        case 273: cArgs = -1; pszName = "vserver"; break;
        case 274: cArgs =  6; pszName = "mbind"; break;
        case 275: cArgs =  5; pszName = "get_mempolicy"; break;
        case 276: cArgs =  3; pszName = "set_mempolicy"; break;
        case 277: cArgs =  4; pszName = "mq_open"; break;
        case 278: cArgs =  1; pszName = "mq_unlink"; break;
        case 279: cArgs =  5; pszName = "mq_timedsend"; break;
        case 280: cArgs =  5; pszName = "mq_timedreceive"; break;
        case 281: cArgs =  2; pszName = "mq_notify"; break;
        case 282: cArgs =  3; pszName = "mq_getsetattr"; break;
        case 283: cArgs =  4; pszName = "kexec_load"; break;
        case 284: cArgs =  5; pszName = "waitid"; break;
        case 286: cArgs =  5; pszName = "add_key"; break;
        case 287: cArgs =  4; pszName = "request_key"; break;
        case 288: cArgs =  5; pszName = "keyctl"; break;
        case 289: cArgs =  3; pszName = "ioprio_set"; break;
        case 290: cArgs =  2; pszName = "ioprio_get"; break;
        case 291: cArgs = -1; pszName = "inotify_init"; break;
        case 292: cArgs =  3; pszName = "inotify_add_watch"; break;
        case 293: cArgs =  2; pszName = "inotify_rm_watch"; break;
        case 294: cArgs =  4; pszName = "migrate_pages"; break;
        case 295: cArgs =  4; pszName = "openat"; break;
        case 296: cArgs =  3; pszName = "mkdirat"; break;
        case 297: cArgs =  4; pszName = "mknodat"; break;
        case 298: cArgs =  5; pszName = "fchownat"; break;
        case 299: cArgs =  3; pszName = "futimesat"; break;
        case 300: cArgs =  4; pszName = "fstatat64"; break;
        case 301: cArgs =  3; pszName = "unlinkat"; break;
        case 302: cArgs =  4; pszName = "renameat"; break;
        case 303: cArgs =  5; pszName = "linkat"; break;
        case 304: cArgs =  3; pszName = "symlinkat"; break;
        case 305: cArgs =  4; pszName = "readlinkat"; break;
        case 306: cArgs =  3; pszName = "fchmodat"; break;
        case 307: cArgs =  3; pszName = "faccessat"; break;
        case 308: cArgs =  6; pszName = "pselect6"; break;
        case 309: cArgs =  5; pszName = "ppoll"; break;
        case 310: cArgs =  1; pszName = "unshare"; break;
        case 311: cArgs =  2; pszName = "set_robust_list"; break;
        case 312: cArgs =  3; pszName = "get_robust_list"; break;
        case 313: cArgs =  6; pszName = "splice"; break;
        case 314: cArgs =  6; pszName = "sync_file_range"; break;
        case 315: cArgs =  4; pszName = "tee"; break;
        case 316: cArgs =  4; pszName = "vmsplice"; break;
        case 317: cArgs =  6; pszName = "move_pages"; break;
        case 318: cArgs =  3; pszName = "getcpu"; break;
        case 319: cArgs =  6; pszName = "epoll_pwait"; break;
        case 320: cArgs =  4; pszName = "utimensat"; break;
        case 321: cArgs =  3; pszName = "signalfd"; break;
        case 322: cArgs =  2; pszName = "timerfd_create"; break;
        case 323: cArgs =  1; pszName = "eventfd"; break;
        case 324: cArgs =  6; pszName = "fallocate"; break;
        case 325: cArgs =  4; pszName = "timerfd_settime"; break;
        case 326: cArgs =  2; pszName = "timerfd_gettime"; break;
        case 327: cArgs =  4; pszName = "signalfd4"; break;
        case 328: cArgs =  2; pszName = "eventfd2"; break;
        case 329: cArgs =  1; pszName = "epoll_create1"; break;
        case 330: cArgs =  3; pszName = "dup3"; break;
        case 331: cArgs =  2; pszName = "pipe2"; break;
        case 332: cArgs =  1; pszName = "inotify_init1"; break;
        case 333: cArgs =  5; pszName = "preadv"; break;
        case 334: cArgs =  5; pszName = "pwritev"; break;
        case 335: cArgs =  4; pszName = "rt_tgsigqueueinfo"; break;
        case 336: cArgs =  5; pszName = "perf_event_open"; break;
        case 337: cArgs =  5; pszName = "recvmmsg"; break;
        case 338: cArgs =  2; pszName = "fanotify_init"; break;
        case 339: cArgs =  5; pszName = "fanotify_mark"; break;
        case 340: cArgs =  4; pszName = "prlimit64"; break;
        case 341: cArgs =  5; pszName = "name_to_handle_at"; break;
        case 342: cArgs =  3; pszName = "open_by_handle_at"; break;
        case 343: cArgs =  2; pszName = "clock_adjtime"; break;
        case 344: cArgs =  1; pszName = "syncfs"; break;
        case 345: cArgs =  4; pszName = "sendmmsg"; break;
        case 346: cArgs =  2; pszName = "setns"; break;
        case 347: cArgs =  6; pszName = "process_vm_readv"; break;
        case 348: cArgs =  6; pszName = "process_vm_writev"; break;
        case 349: cArgs =  5; pszName = "kcmp"; break;
        case 350: cArgs =  3; pszName = "finit_module"; break;
        case 351: cArgs =  3; pszName = "sched_setattr"; break;
        case 352: cArgs =  4; pszName = "sched_getattr"; break;
        case 353: cArgs =  5; pszName = "renameat2"; break;
        case 354: cArgs =  3; pszName = "seccomp"; break;
        case 355: cArgs =  3; pszName = "getrandom"; break;
        case 356: cArgs =  2; pszName = "memfd_create"; break;
        case 357: cArgs =  3; pszName = "bpf"; break;
        case 358: cArgs =  5; pszName = "execveat"; break;
        case 359: cArgs =  3; pszName = "socket"; break;
        case 360: cArgs =  4; pszName = "socketpair"; break;
        case 361: cArgs =  3; pszName = "bind"; break;
        case 362: cArgs =  3; pszName = "connect"; break;
        case 363: cArgs =  2; pszName = "listen"; break;
        case 364: cArgs =  4; pszName = "accept4"; break;
        case 365: cArgs =  5; pszName = "getsockopt"; break;
        case 366: cArgs =  5; pszName = "setsockopt"; break;
        case 367: cArgs =  3; pszName = "getsockname"; break;
        case 368: cArgs =  3; pszName = "getpeername"; break;
        case 369: cArgs =  6; pszName = "sendto"; break;
        case 370: cArgs =  3; pszName = "sendmsg"; break;
        case 371: cArgs =  6; pszName = "recvfrom"; break;
        case 372: cArgs =  3; pszName = "recvmsg"; break;
        case 373: cArgs =  2; pszName = "shutdown"; break;
        case 374: cArgs =  1; pszName = "userfaultfd"; break;
        case 375: cArgs =  3; pszName = "membarrier"; break;
        case 376: cArgs =  3; pszName = "mlock2"; break;
        case 377: cArgs =  6; pszName = "copy_file_range"; break;
        case 378: cArgs =  6; pszName = "preadv2"; break;
        case 379: cArgs =  6; pszName = "pwritev2"; break;
        case 380: cArgs =  4; pszName = "pkey_mprotect"; break;
        case 381: cArgs =  2; pszName = "pkey_alloc"; break;
        case 382: cArgs =  1; pszName = "pkey_free"; break;
        case 383: cArgs =  5; pszName = "statx"; break;
        case 384: cArgs =  2; pszName = "arch_prctl"; break;
        case 385: cArgs =  6; pszName = "io_pgetevents"; break;
        case 386: cArgs =  4; pszName = "rseq"; break;
        case 393: cArgs =  3; pszName = "semget"; break;
        case 394: cArgs =  4; pszName = "semctl"; break;
        case 395: cArgs =  3; pszName = "shmget"; break;
        case 396: cArgs =  3; pszName = "shmctl"; break;
        case 397: cArgs =  3; pszName = "shmat"; break;
        case 398: cArgs =  1; pszName = "shmdt"; break;
        case 399: cArgs =  2; pszName = "msgget"; break;
        case 400: cArgs =  4; pszName = "msgsnd"; break;
        case 401: cArgs =  5; pszName = "msgrcv"; break;
        case 402: cArgs =  3; pszName = "msgctl"; break;
        case 403: cArgs =  2; pszName = "clock_gettime64"; break;
        case 404: cArgs =  2; pszName = "clock_settime64"; break;
        case 405: cArgs =  2; pszName = "clock_adjtime64"; break;
        case 406: cArgs =  2; pszName = "clock_getres_time64"; break;
        case 407: cArgs =  4; pszName = "clock_nanosleep_time64"; break;
        case 408: cArgs =  2; pszName = "timer_gettime64"; break;
        case 409: cArgs =  4; pszName = "timer_settime64"; break;
        case 410: cArgs =  2; pszName = "timerfd_gettime64"; break;
        case 411: cArgs =  4; pszName = "timerfd_settime64"; break;
        case 412: cArgs =  4; pszName = "utimensat_time64"; break;
        case 413: cArgs =  6; pszName = "pselect6_time64"; break;
        case 414: cArgs =  5; pszName = "ppoll_time64"; break;
        case 416: cArgs =  6; pszName = "io_pgetevents_time64"; break;
        case 417: cArgs =  5; pszName = "recvmmsg_time64"; break;
        case 418: cArgs =  5; pszName = "mq_timedsend_time64"; break;
        case 419: cArgs =  5; pszName = "mq_timedreceive_time64"; break;
        case 420: cArgs =  4; pszName = "semtimedop_time64"; break;
        case 421: cArgs =  4; pszName = "rt_sigtimedwait_time64"; break;
        case 422: cArgs =  6; pszName = "futex_time64"; break;
        case 423: cArgs =  2; pszName = "sched_rr_get_interval_time64"; break;
        case 424: cArgs =  4; pszName = "pidfd_send_signal"; break;
        case 425: cArgs =  2; pszName = "io_uring_setup"; break;
        case 426: cArgs =  6; pszName = "io_uring_enter"; break;
        case 427: cArgs =  4; pszName = "io_uring_register"; break;
        case 428: cArgs =  3; pszName = "open_tree"; break;
        case 429: cArgs =  5; pszName = "move_mount"; break;
        case 430: cArgs =  2; pszName = "fsopen"; break;
        case 431: cArgs =  5; pszName = "fsconfig"; break;
        case 432: cArgs =  3; pszName = "fsmount"; break;
        case 433: cArgs =  3; pszName = "fspick"; break;
        case 434: cArgs =  2; pszName = "pidfd_open"; break;
        case 435: cArgs =  2; pszName = "clone3"; break;
        case 436: cArgs =  3; pszName = "close_range"; break;
        case 437: cArgs =  4; pszName = "openat2"; break;
        case 438: cArgs =  3; pszName = "pidfd_getfd"; break;
        case 439: cArgs =  4; pszName = "faccessat2"; break;
        case 440: cArgs =  5; pszName = "process_madvise"; break;
        case 441: cArgs =  6; pszName = "epoll_pwait2"; break;
        case 442: cArgs =  5; pszName = "mount_setattr"; break;
        case 443: cArgs =  4; pszName = "quotactl_fd"; break;
        case 444: cArgs =  3; pszName = "landlock_create_ruleset"; break;
        case 445: cArgs =  4; pszName = "landlock_add_rule"; break;
        case 446: cArgs =  2; pszName = "landlock_restrict_self"; break;
        case 447: cArgs =  1; pszName = "memfd_secret"; break;
        case 448: cArgs =  2; pszName = "process_mrelease"; break;
        case 449: cArgs =  5; pszName = "futex_waitv"; break;
        case 450: cArgs =  4; pszName = "set_mempolicy_home_node"; break;
        case 451: cArgs =  4; pszName = "cachestat"; break;
        case 452: cArgs =  4; pszName = "fchmodat2"; break;

        default:
            pszName = "unknown!";
            cArgs   = -1;
            break;;
    }
    Log3(("Linux syscall: %s (%#x) at %04x:%08x - cArgs=%d: ebx=%#x ecx=%#x edx=%#x esi=%#x edi=%#x ebp=%#x (esp=%#x eax=%#x efl=%#x)\n",
          pszName, uSysCall, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip, cArgs, pVCpu->cpum.GstCtx.ebx,
          pVCpu->cpum.GstCtx.ecx, pVCpu->cpum.GstCtx.edx, pVCpu->cpum.GstCtx.esi, pVCpu->cpum.GstCtx.edi, pVCpu->cpum.GstCtx.ebp,
          pVCpu->cpum.GstCtx.esp, pVCpu->cpum.GstCtx.eax, pVCpu->cpum.GstCtx.eflags.uBoth));

#ifdef IN_RING3
    /*
     * Log string arguments.
     */
    static const uint8_t s_aidxArgToGReg[] =
    { X86_GREG_xBX, X86_GREG_xCX, X86_GREG_xDX, X86_GREG_xSI, X86_GREG_xDI, X86_GREG_xBP };
    if (fStrArgs)
    {
        PUVM pUVM = pVCpu->pVMR3->pUVM;
        do
        {
            unsigned const iStrArg = ASMBitFirstSetU32(fStrArgs) - 1;
            fStrArgs &= ~RT_BIT_32(iStrArg);
            if (iStrArg < RT_ELEMENTS(s_aidxArgToGReg))
            {
                char           szStr[1024];
                uint32_t const uAddr = pVCpu->cpum.GstCtx.aGRegs[s_aidxArgToGReg[iStrArg]].u32;
                DBGFADDRESS    DbgAddr;
                int rc = DBGFR3MemReadString(pUVM, pVCpu->idCpu, DBGFR3AddrFromFlat(pUVM, &DbgAddr, uAddr), szStr, sizeof(szStr));
                if (RT_SUCCESS(rc))
                {
                    rc = RTStrValidateEncoding(szStr);
                    if (RT_SUCCESS(rc))
                        Log3(("Linux syscall %x/arg #%u: %#x '%s'\n", uSysCall, iStrArg, uAddr, szStr));
                    else
                        Log3(("Linux syscall %x/arg #%u: %#x %.*Rhxs\n", uSysCall, iStrArg, uAddr, strlen(szStr), szStr));
                }
            }


        } while (fStrArgs);
    }
#else
    RT_NOREF(fStrArgs);
#endif
}


void iemLogSyscallRealModeInt(PVMCPUCC pVCpu, uint8_t u8Vector, uint8_t cbInstr)
{
    /* DOS & BIOS (V86 mode) */
    if (LogIsEnabled())
    {
        switch (u8Vector)
        {
            case 0x10:
                iemLogSyscallVgaBiosInt10h(pVCpu);
                break;
            case 0x16:
                iemLogSyscallBiosInt16h(pVCpu);
                break;
            case 0x21:
                iemLogSyscallDosInt21h(pVCpu);
                break;
        }
    }
    RT_NOREF(cbInstr);
}


void iemLogSyscallProtModeInt(PVMCPUCC pVCpu, uint8_t u8Vector, uint8_t cbInstr)
{
    /* DOS & BIOS (V86 mode) */
    if (   LogIsEnabled()
        && pVCpu->cpum.GstCtx.eflags.Bits.u1VM /* v8086 mode */)
    {
        switch (u8Vector)
        {
            case 0x10:
                iemLogSyscallVgaBiosInt10h(pVCpu);
                break;
            case 0x16:
                iemLogSyscallBiosInt16h(pVCpu);
                break;
            case 0x21:
                iemLogSyscallDosInt21h(pVCpu);
                break;
        }
    }

    /* Windows 3.x */
    if (LogIs2Enabled())
        switch (u8Vector)
        {
            case 0x20: /* VxD call. */
                iemLogSyscallWinVxDCall(pVCpu, cbInstr);
                break;
        }

    /* Linux */
    if (LogIs3Enabled() && u8Vector == 0x80)
        iemLogSyscallLinuxX86Int80(pVCpu);
}

#endif /* LOG_ENABLED */
