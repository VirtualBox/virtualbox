/* $Id: nocrt-startup-common-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - Common Windows startup code.
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
#include "internal/nocrt.h"
#include "internal/process.h"

#include <iprt/nt/nt-and-windows.h>
#ifndef IPRT_NOCRT_WITHOUT_FATAL_WRITE
# include <iprt/assert.h>
#endif
#include <iprt/getopt.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include "internal/compiler-vcc.h"
#include "internal/process.h"


#ifdef RT_ARCH_X86
/**
 * NT 3.1 does not know about the IMAGE_SECTION_HEADER::Misc.VirtualSize field
 * and will therefore not handle merging initialized and uninitialized data into
 * the same section.
 *
 * We work around this by manually zeroing the uninitialized data before any
 * other code has been executed.
 */
void rtVccWinInitBssOnNt3(void *pvImageBase)
{
    /* We are called really early on, so we must figure out the NT version
       on our own.  It doesn't have to be all that accurate, though, as only
       NT 3.10 is affected (3.50 isn't). */
    DWORD const dwRawVer  = GetVersion();
    DWORD const uMajorVer = RT_BYTE1(dwRawVer);
    DWORD const uMinorVer = RT_BYTE2(dwRawVer);
    if (uMajorVer != 3 || uMinorVer >= 50)
        return;

    /*
     * Locate the NT headers.
     */
    PIMAGE_NT_HEADERS   pNtHdrs;
    PIMAGE_DOS_HEADER   pDosHdr = (PIMAGE_DOS_HEADER)pvImageBase;
    if (pDosHdr->e_magic == IMAGE_DOS_SIGNATURE)
        pNtHdrs = (PIMAGE_NT_HEADERS)((uintptr_t)pvImageBase + pDosHdr->e_lfanew);
    else
        pNtHdrs = (PIMAGE_NT_HEADERS)pvImageBase;
    if (pNtHdrs->Signature == IMAGE_NT_SIGNATURE)
    {
        /*
         * Locate the section table and walk thru it, memsetting anything that
         * wasn't loaded from the file.
         */
        PIMAGE_SECTION_HEADER paSHdrs = (PIMAGE_SECTION_HEADER)(  (uintptr_t)&pNtHdrs->OptionalHeader
                                                                + pNtHdrs->FileHeader.SizeOfOptionalHeader);
        uint32_t const        cSections = pNtHdrs->FileHeader.NumberOfSections;
        for (uint32_t i = 0; i < cSections; i++)
        {
            if (paSHdrs[i].Misc.VirtualSize > paSHdrs[i].SizeOfRawData)
            {
                /* ASSUMES VirtualAddress is still an RVAs */
                uint8_t       *pbToZero = (uint8_t *)pvImageBase + paSHdrs[i].VirtualAddress + paSHdrs[i].SizeOfRawData;
                uint32_t const cbToZero = paSHdrs[i].Misc.VirtualSize - paSHdrs[i].SizeOfRawData;

# if 0 /* very very crude debugging */
                const char *pszHex = "0123456789abcdef";
                char szMsg[128];
                char *psz = szMsg;
                *psz++ = 'd'; *psz++ = 'b'; *psz++ = 'g'; *psz++ = ':'; *psz++ = ' ';
                for (uint32_t q = 0, u = i; q < 8; q++, u >>= 4)
                    psz[7 - q] = pszHex[u & 0xf];
                psz += 8;
                *psz++ = ':';
                *psz++ = ' ';
                for (uint32_t q = 0, u = (uint32_t)pbToZero; q < 8; q++, u >>= 4)
                    psz[7 - q] = pszHex[u & 0xf];
                psz += 8;
                *psz++ = ' ';
                *psz++ = 'L';
                *psz++ = 'B';
                *psz++ = ' ';
                for (uint32_t q = 0, u = cbToZero; q < 8; q++, u >>= 4)
                    psz[7 - q] = pszHex[u & 0xf];
                psz += 8;
                *psz++ = ' ';
                for (uint32_t q = 0; q < 8; q++)
                    *psz++ = paSHdrs[i].Name[q] ? paSHdrs[i].Name[q] : ' ';
                *psz++ = ' '; *psz++ = '/'; *psz++ = ' '; *psz++ = 'v'; *psz++ = 'e'; *psz++ = 'r'; *psz++ = ' ';
                *psz++ = pszHex[(uMajorVer >> 4) & 0xf];
                *psz++ = pszHex[uMajorVer & 0xf];
                *psz++ = '.';
                *psz++ = pszHex[(uMinorVer >> 4) & 0xf];
                *psz++ = pszHex[uMinorVer & 0xf];
                *psz++ = '\r'; *psz++ = '\n';
                *psz   = '\0';
                DWORD cbIgn;
                HANDLE hOut = RTNtCurrentPeb()->ProcessParameters->StandardOutput;
                if (hOut == NULL || hOut == INVALID_HANDLE_VALUE)
                    hOut = RTNtCurrentPeb()->ProcessParameters->StandardError;
                if (hOut == NULL || hOut == INVALID_HANDLE_VALUE)
                    hOut = RTNtCurrentPeb()->ProcessParameters->ConsoleHandle;
                if (hOut == NULL || hOut == INVALID_HANDLE_VALUE)
                    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                WriteFile(hOut, szMsg, psz - szMsg, &cbIgn, NULL);
# endif

                if (paSHdrs[i].Characteristics & IMAGE_SCN_MEM_WRITE)
                    memset(pbToZero, 0, cbToZero);
                else
                {
# if 1              /* This dynamic stupidity is because of stupid AV heuristics. */
                    static decltype(NtProtectVirtualMemory) *s_pfnNtProtectVirtualMemory = NULL;
                    if (!s_pfnNtProtectVirtualMemory)
                        s_pfnNtProtectVirtualMemory
                            = (decltype(NtProtectVirtualMemory) *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                                                 "NtProtectVirtualMemory");
                    if (!s_pfnNtProtectVirtualMemory)
                    {
                        RT_BREAKPOINT();
                        continue;
                    }
# else
#  define s_pfnNtProtectVirtualMemory NtProtectVirtualMemory
# endif

                    /* The section is not writable, so temporarily make it writable. */
                    PVOID pvAligned = pbToZero - ((uintptr_t)pbToZero & PAGE_OFFSET_MASK);
                    ULONG cbAligned = RT_ALIGN_32(cbToZero + ((uintptr_t)pbToZero & PAGE_OFFSET_MASK), PAGE_SIZE);
                    ULONG fNewProt  = paSHdrs[i].Characteristics & IMAGE_SCN_MEM_EXECUTE
                                    ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
                    ULONG fOldProt  = fNewProt;
                    NTSTATUS rcNt = s_pfnNtProtectVirtualMemory(NtCurrentProcess(), &pvAligned, &cbAligned, fNewProt, &fOldProt);
                    if (NT_SUCCESS(rcNt))
                    {
                        memset(pbToZero, 0, cbToZero);

                        rcNt = s_pfnNtProtectVirtualMemory(NtCurrentProcess(), &pvAligned, &cbAligned, fOldProt, &fNewProt);
                    }
                    else
                        RT_BREAKPOINT();
                }
            }
        }
    }
    else
        RT_BREAKPOINT();
}
#endif


void rtVccWinInitProcExecPath(void)
{
    WCHAR wszPath[RTPATH_MAX];
    UINT cwcPath = GetModuleFileNameW(NULL, wszPath, RT_ELEMENTS(wszPath));
    if (cwcPath)
    {
        char *pszDst = g_szrtProcExePath;
        int rc = RTUtf16ToUtf8Ex(wszPath, cwcPath, &pszDst, sizeof(g_szrtProcExePath), &g_cchrtProcExePath);
        if (RT_SUCCESS(rc))
        {
            g_cchrtProcExeDir = g_offrtProcName = RTPathFilename(pszDst) - g_szrtProcExePath;
            while (   g_cchrtProcExeDir >= 2
                   && RTPATH_IS_SLASH(g_szrtProcExePath[g_cchrtProcExeDir - 1])
                   && g_szrtProcExePath[g_cchrtProcExeDir - 2] != ':')
                g_cchrtProcExeDir--;
        }
        else
        {
#ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
            RTMsgError("initProcExecPath: RTUtf16ToUtf8Ex failed: %Rrc\n", rc);
#else
            rtNoCrtFatalMsgWithRc(RT_STR_TUPLE("initProcExecPath: RTUtf16ToUtf8Ex failed: "), rc);
#endif
        }
    }
    else
    {
#ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
        RTMsgError("initProcExecPath: GetModuleFileNameW failed: %Rhrc\n", GetLastError());
#else
        rtNoCrtFatalWriteBegin(RT_STR_TUPLE("initProcExecPath: GetModuleFileNameW failed: "));
        rtNoCrtFatalWriteWinRc(GetLastError());
        rtNoCrtFatalWrite(RT_STR_TUPLE("\r\n"));
#endif
    }
}

