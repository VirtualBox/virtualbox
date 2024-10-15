/* $Id: tstWinAdditionsInstallHelper.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstWinAdditionsInstallHelper - Testcases for the Windows Guest Additions Installer Helper DLL.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#include <iprt/win/windows.h>
#include "../exdll.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/ldr.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Symbol names to test. */
#define TST_FILEGETARCHITECTURE_NAME   "FileGetArchitecture"
#define TST_FILEGETVENDOR_NAME         "FileGetVendor"
#define TST_VBOXTRAYSHOWBALLONMSG_NAME "VBoxTrayShowBallonMsg"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** A generic NSIS plugin function. */
typedef void __cdecl NSIS_PLUGIN_FUNC(HWND hwndParent, int string_size, WCHAR *variables, stack_t **stacktop, extra_parameters *extra);
/** Pointer to a generic NSIS plugin function. */
typedef NSIS_PLUGIN_FUNC *PNSIS_PLUGIN_FUNC;



/**
 * Destroys a stack.
 *
 * @param   pStackTop           Stack to destroy.
 */
static void tstStackDestroy(stack_t *pStackTop)
{
    while (pStackTop)
    {
        stack_t *pStackNext = pStackTop->next;
        GlobalFree(pStackTop);
        pStackTop = pStackNext;
    }
}

/**
 * Pushes a string to a stack
 *
 * @returns VBox status code.
 * @param   ppStackTop          Stack to push string to.
 * @param   pwszString          String to push to the stack.
 */
static int tstStackPushString(stack_t **ppStackTop, const WCHAR *pwszString)
{
    size_t cwcString = RTUtf16Len(pwszString);
    stack_t *pStack = (stack_t *)GlobalAlloc(GPTR, RT_UOFFSETOF_DYN(stack_t, text[cwcString + 1]));
    if (pStack)
    {
        AssertCompile(sizeof(pStack->text[0]) == sizeof(*pwszString));
        memcpy(pStack->text, pwszString, (cwcString + 1) * sizeof(pStack->text[0]));
        pStack->next = ppStackTop ? *ppStackTop : NULL;

        *ppStackTop = pStack;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}

/**
 * Pops a string off a stack.
 *
 * @returns IPRT status code.
 * @param   ppStackTop      Stack to pop off string from.
 * @param   pwszDst         Where to return the string.
 * @param   cwcDst          The size of the destination buffer.
 */
static int tstStackPopString(stack_t **ppStackTop, WCHAR *pwszDst, size_t cwcDst)
{
    stack_t *pStack = *ppStackTop;
    if (pStack)
    {
        int rc = RTUtf16Copy(pwszDst, cwcDst, pStack->text);

        *ppStackTop = pStack->next;
        GlobalFree((HGLOBAL)pStack);

        return rc;
    }
    return VERR_NOT_FOUND;
}

int main(int argc, char **argv)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitExAndCreate(argc, &argv, 0, "tstWinAdditionsInstallHelper", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    char szGuestInstallHelperDll[RTPATH_MAX];
    RTPathExecDir(szGuestInstallHelperDll, sizeof(szGuestInstallHelperDll));

    /** @todo This ASSUMES that this testcase always is located in the separate "bin/additions" sub directory
     *        and that we currently always repack the Guest Additions stuff in a separate directory.
     *        Might need some more tweaking ... */
    int rc = RTPathAppend(szGuestInstallHelperDll, sizeof(szGuestInstallHelperDll),
                          "..\\..\\repackadd\\resources\\VBoxGuestInstallHelper\\VBoxGuestInstallHelper.dll");
    if (RT_SUCCESS(rc))
    {
        RTTestIPrintf(RTTESTLVL_ALWAYS, "Using DLL: %s\n", szGuestInstallHelperDll);

        RTLDRMOD hLdrMod;
        rc = RTLdrLoad(szGuestInstallHelperDll, &hLdrMod);
        if (RT_SUCCESS(rc))
        {
            WCHAR wszVars[NSIS_MAX_STRLEN * sizeof(WCHAR)] = { 0 };
            WCHAR wszResult[NSIS_MAX_STRLEN];

            /*
             * Tests FileGetArchitecture
             */
            PNSIS_PLUGIN_FUNC pfnFileGetArchitecture = NULL;
            rc = RTLdrGetSymbol(hLdrMod, TST_FILEGETARCHITECTURE_NAME, (void**)&pfnFileGetArchitecture);
            if (RT_SUCCESS(rc))
            {
                stack_t *pStack = NULL;
                tstStackPushString(&pStack, L"c:\\windows\\system32\\kernel32.dll");

                pfnFileGetArchitecture(NULL /* hWnd */, NSIS_MAX_STRLEN, wszVars, &pStack, NULL /* extra */);

                rc = tstStackPopString(&pStack, wszResult, sizeof(wszResult));
                if (   RT_SUCCESS(rc)
                    && (   RTUtf16CmpAscii(wszResult, "x86") == 0
                        || RTUtf16CmpAscii(wszResult, "amd64") == 0))
                    RTTestIPrintf(RTTESTLVL_ALWAYS, "Arch: %ls\n", wszResult);
                else
                    RTTestIFailed("Getting file arch on kernel32 failed: %Rrc - '%ls', expected 'x86' or 'amd64'", rc, wszResult);

                if (pStack)
                    RTTestIFailed("Too many items on the stack!");
                tstStackDestroy(pStack);
            }
            else
                RTTestIFailed("Loading pfnFileGetArchitecture failed: %Rrc", rc);

            /*
             * Tests FileGetVendor
             */
            PNSIS_PLUGIN_FUNC pfnFileGetVendor;
            rc = RTLdrGetSymbol(hLdrMod, TST_FILEGETVENDOR_NAME, (void **)&pfnFileGetVendor);
            if (RT_SUCCESS(rc))
            {
                stack_t *pStack = NULL;
                tstStackPushString(&pStack, L"c:\\windows\\system32\\kernel32.dll");

                pfnFileGetVendor(NULL /* hWnd */, NSIS_MAX_STRLEN, wszVars, &pStack, NULL /* extra */);

                rc = tstStackPopString(&pStack, wszResult, RT_ELEMENTS(wszResult));
                if (   RT_SUCCESS(rc)
                    && RTUtf16CmpAscii(wszResult, "Microsoft Corporation") == 0)
                    RTTestIPrintf(RTTESTLVL_ALWAYS, "Vendor: %ls\n", wszResult);
                else
                    RTTestIFailed("Getting file vendor failed: %Rrc - '%ls', expected 'Microsoft Corporation'\n", rc, wszResult);

                if (pStack)
                    RTTestIFailed("Too many items on the stack!");
                tstStackDestroy(pStack);
            }
            else
                RTTestIFailed("Loading pfnFileGetVendor failed: %Rrc", rc);

            /*
             * Tests VBoxTrayShowBallonMsg
             */
            PNSIS_PLUGIN_FUNC pfnVBoxTrayShowBallonMsg;
            rc = RTLdrGetSymbol(hLdrMod, TST_VBOXTRAYSHOWBALLONMSG_NAME, (void **)&pfnVBoxTrayShowBallonMsg);
            if (RT_SUCCESS(rc))
            {
                stack_t *pStack = NULL;
                /* Push stuff in reverse order to the stack. */
                tstStackPushString(&pStack, L"5000" /* Time to show in ms */);
                tstStackPushString(&pStack, L"1" /* Type - info */);
                tstStackPushString(&pStack, L"This is a message from tstWinAdditionsInstallHelper!");
                tstStackPushString(&pStack, L"This is a title from tstWinAdditionsInstallHelper!");

                pfnVBoxTrayShowBallonMsg(NULL /* hWnd */, NSIS_MAX_STRLEN, wszVars, &pStack, NULL /* extra */);

                rc = tstStackPopString(&pStack, wszResult, RT_ELEMENTS(wszResult));
                if (RT_SUCCESS(rc))
                    RTTestIPrintf(RTTESTLVL_ALWAYS, "Reply: '%ls'\n", wszResult);
                else
                    RTTestIFailed("Sending message to VBoxTray failed: stack pop error - %Rrc", rc);

                if (pStack)
                    RTTestIFailed("Too many items on the stack!");
                tstStackDestroy(pStack);
            }
            else
                RTTestIFailed("Loading pfnVBoxTrayShowBallonMsg failed: %Rrc", rc);

            RTLdrClose(hLdrMod);
        }
        else
            RTTestIFailed("Loading DLL failed: %Rrc", rc);
    }
    else
        RTTestIFailed("Getting absolute path of DLL failed: %Rrc", rc);

    return RTTestSummaryAndDestroy(hTest);
}

