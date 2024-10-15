/* $Id: VBoxMpLogger.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox WDDM Display logger implementation
 *
 * We're unable to use standard r3 vbgl-based backdoor logging API because
 * win8 Metro apps can not do CreateFile/Read/Write by default.  This is why
 * we use miniport escape functionality to issue backdoor log string to the
 * miniport and submit it to host via standard r0 backdoor logging api
 * accordingly
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

#define IPRT_NO_CRT_FOR_3RD_PARTY /* To get malloc and free wrappers in IPRT_NO_CRT mode. Doesn't link with IPRT in non-no-CRT mode. */
#include "UmHlpInternal.h"

#include <../../../common/wddm/VBoxMPIf.h>
#include <stdlib.h>
#ifdef IPRT_NO_CRT
# include <iprt/process.h>
# include <iprt/string.h>
#else
# include <stdio.h>
#endif
#include <VBox/VBoxGuestLib.h>


static void VBoxDispMpLoggerLogN(const char *pchString, size_t cchString)
{
    D3DKMTFUNCTIONS const *d3dkmt = D3DKMTFunctions();
    if (d3dkmt->pfnD3DKMTEscape == NULL)
        return;

    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status = vboxDispKmtOpenAdapter(&hAdapter);
    Assert(Status == STATUS_SUCCESS);
    if (Status == 0)
    {
        uint32_t cchString2 = (uint32_t)RT_MIN(cchString, _64K - 1U);
        uint32_t cbCmd = RT_UOFFSETOF_DYN(VBOXDISPIFESCAPE_DBGPRINT, aStringBuf[cchString2 + 1]);
        PVBOXDISPIFESCAPE_DBGPRINT pCmd = (PVBOXDISPIFESCAPE_DBGPRINT)malloc(cbCmd);
        Assert(pCmd);
        if (pCmd)
        {
            pCmd->EscapeHdr.escapeCode = VBOXESC_DBGPRINT;
            pCmd->EscapeHdr.u32CmdSpecific = 0;
            memcpy(pCmd->aStringBuf, pchString, cchString2);
            pCmd->aStringBuf[cchString2] = '\0';

            D3DKMT_ESCAPE EscapeData;
            memset(&EscapeData, 0, sizeof(EscapeData));
            EscapeData.hAdapter = hAdapter;
            // EscapeData.hDevice = NULL;
            EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
            // EscapeData.Flags.HardwareAccess = 0;
            EscapeData.pPrivateDriverData = pCmd;
            EscapeData.PrivateDriverDataSize = cbCmd;
            // EscapeData.hContext = NULL;

            Status = d3dkmt->pfnD3DKMTEscape(&EscapeData);
            Assert(Status == STATUS_SUCCESS);

            free(pCmd);
        }

        Status = vboxDispKmtCloseAdapter(hAdapter);
        Assert(Status == STATUS_SUCCESS);
    }
}


DECLCALLBACK(void) VBoxDispMpLoggerLog(const char *pszString)
{
    VBoxDispMpLoggerLogN(pszString, strlen(pszString));
}


DECLCALLBACK(void) VBoxDispMpLoggerLogF(const char *pszFormat, ...)
{
    /** @todo would make a whole lot more sense to just allocate
     *        VBOXDISPIFESCAPE_DBGPRINT here and printf into it's buffer than
     *        double buffering it like this */
    char szBuffer[4096];
    va_list va;
    va_start(va, pszFormat);
#ifdef IPRT_NO_CRT
    RTStrPrintfV(szBuffer, sizeof(szBuffer), pszFormat, va);
#else
    _vsnprintf(szBuffer, sizeof(szBuffer), pszFormat, va);
    szBuffer[sizeof(szBuffer) - 1] = '\0'; /* Don't trust the _vsnprintf function terminate the string! */
#endif
    va_end(va);

    VBoxDispMpLoggerLog(szBuffer);
}


/* Interface used for backdoor logging.  In no-CRT mode we will drag in IPRT
   logging and it will be used on assertion in the no-CRT and IPRT code. */
VBGLR3DECL(int) VbglR3WriteLog(const char *pch, size_t cch)
{
    VBoxDispMpLoggerLogN(pch, cch);
    return VINF_SUCCESS;
}


/**
 * Prefix the output string with exe name and pid/tid.
 */
#ifndef IPRT_NO_CRT
static const char *vboxUmLogGetExeName(void)
{
    static int s_fModuleNameInited = 0;
    static char s_szModuleName[MAX_PATH];

    if (!s_fModuleNameInited)
    {
        const DWORD cchName = GetModuleFileNameA(NULL, s_szModuleName, RT_ELEMENTS(s_szModuleName));
        if (cchName == 0)
            return "<no module>";
        s_fModuleNameInited = 1;
    }
    return &s_szModuleName[0];
}
#endif

DECLCALLBACK(void) VBoxWddmUmLog(const char *pszString)
{
    /** @todo Allocate VBOXDISPIFESCAPE_DBGPRINT here and format right into it
     *        instead? That would be a lot more flexible and a little faster. */
    char szBuffer[4096];
#ifdef IPRT_NO_CRT
    /** @todo use RTProcShortName instead of RTProcExecutablePath? Will avoid
     *        chopping off log text if the executable path is too long. */
    RTStrPrintf(szBuffer, sizeof(szBuffer), "['%s' 0x%lx.0x%lx]: %s",
                RTProcExecutablePath() /* should've been initialized by nocrt-startup-dll-win.cpp already */,
                GetCurrentProcessId(), GetCurrentThreadId(), pszString);
#else
    int cch = _snprintf(szBuffer, sizeof(szBuffer), "['%s' 0x%lx.0x%lx]: %s",
                        vboxUmLogGetExeName(), GetCurrentProcessId(), GetCurrentThreadId(), pszString);
    AssertReturnVoid(cch > 0);             /* unlikely that we'll have string encoding problems, but just in case. */
    szBuffer[sizeof(szBuffer) - 1] = '\0'; /* the function doesn't necessarily terminate the buffer on overflow. */
#endif

    VBoxDispMpLoggerLog(szBuffer);
}

