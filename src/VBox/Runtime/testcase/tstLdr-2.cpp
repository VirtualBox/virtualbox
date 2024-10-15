/* $Id: tstLdr-2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Testcase for parts of RTLdr*, manual inspection.
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
#include <iprt/ldr.h>
#include <iprt/alloc.h>
#include <iprt/stream.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <VBox/dis.h>
#include <iprt/errcore.h>
#include <iprt/string.h>


static bool MyDisBlock(uint8_t const *pbCodeBlock, int32_t cbMax)
{
    DISSTATE Dis;
    int32_t i = 0;
    while (i < cbMax)
    {
        char        szOutput[256];
        uint32_t    cbInstr;
        if (RT_FAILURE(DISInstrToStr(pbCodeBlock + i, DISCPUMODE_32BIT, &Dis, &cbInstr, szOutput, sizeof(szOutput))))
            return false;

        RTPrintf("%s", szOutput);

        /* next */
        i += cbInstr;
    }
    return true;
}



/**
 * Resolve an external symbol during RTLdrGetBits().
 *
 * @returns iprt status code.
 * @param   hLdrMod         The loader module handle.
 * @param   pszModule       Module name.
 * @param   pszSymbol       Symbol name, NULL if uSymbol should be used.
 * @param   uSymbol         Symbol ordinal, ~0 if pszSymbol should be used.
 * @param   pValue          Where to store the symbol value (address).
 * @param   pvUser          User argument.
 */
static DECLCALLBACK(int) testGetImport(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol, unsigned uSymbol,
                                       RTUINTPTR *pValue, void *pvUser)
{
    RT_NOREF5(hLdrMod, pszModule, pszSymbol, uSymbol, pvUser);
    /* check the name format and only permit certain names */
    *pValue = 0xf0f0f0f0;
    return VINF_SUCCESS;
}


/**
 * One test iteration with one file.
 *
 * The test is very simple, we load the file three times
 * into two different regions. The first two into each of the
 * regions the for compare usage. The third is loaded into one
 * and then relocated between the two and other locations a few times.
 *
 * @returns number of errors.
 * @param   pszFilename     The file to load the mess with.
 */
static int testLdrOne(const char *pszFilename)
{
    RTERRINFOSTATIC ErrInfo;
    RTLDRMOD hLdrMod;
    int rc = RTLdrOpenEx(pszFilename, 0, RTLDRARCH_WHATEVER, &hLdrMod, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstLdr: Failed to open '%s', rc=%Rrc. aborting test.\n", pszFilename, rc);
        if (ErrInfo.szMsg[0])
            RTPrintf("tstLdr: %s\n", ErrInfo.szMsg);
        Assert(hLdrMod == NIL_RTLDRMOD);
        return 1;
    }

    int rcRet = 1;
    size_t cb = RTLdrSize(hLdrMod);
    if (cb > 100)
    {
        void *pvBits = RTMemAlloc(cb);
        if (pvBits)
        {
            RTUINTPTR Addr = 0xc0000000;
            rc = RTLdrGetBits(hLdrMod, pvBits, Addr, testGetImport, NULL);
            if (RT_SUCCESS(rc))
            {
                RTUINTPTR Value;
                rc = RTLdrGetSymbolEx(hLdrMod, pvBits, Addr, UINT32_MAX, "Entrypoint", &Value);
                if (RT_SUCCESS(rc))
                {
                    unsigned off = Value - Addr;
                    if (off < cb)
                    {
                        if (MyDisBlock((uint8_t *)pvBits + off, Addr - (uintptr_t)pvBits))
                        {
                            RTUINTPTR Addr2 = 0xd0000000;
                            rc = RTLdrRelocate(hLdrMod, pvBits, Addr2, Addr, testGetImport, NULL);
                            if (RT_SUCCESS(rc))
                            {
                                if (MyDisBlock((uint8_t *)pvBits + off, Addr2 - (uintptr_t)pvBits))
                                    rcRet = 0;
                                else
                                    RTPrintf("tstLdr: Disassembly failed!\n");
                            }
                            else
                                RTPrintf("tstLdr: Relocate of '%s' from %#x to %#x failed, rc=%Rrc. Aborting test.\n",
                                         pszFilename, Addr2, Addr, rc);
                        }
                        else
                            RTPrintf("tstLdr: Disassembly failed!\n");
                    }
                    else
                        RTPrintf("tstLdr: Invalid value for symbol '%s' in '%s'. off=%#x Value=%#x\n",
                                 "Entrypoint", pszFilename, off, Value);
                }
                else
                    RTPrintf("tstLdr: Failed to resolve symbol '%s' in '%s', rc=%Rrc.\n", "Entrypoint", pszFilename, rc);
            }
            else
                RTPrintf("tstLdr: Failed to get bits for '%s', rc=%Rrc. aborting test\n", pszFilename, rc);
            RTMemFree(pvBits);
        }
        else
            RTPrintf("tstLdr: Out of memory '%s' cb=%d. aborting test.\n", pszFilename, cb);
    }
    else
        RTPrintf("tstLdr: Size is odd, '%s'. aborting test.\n", pszFilename);


    /* cleanup */
    rc = RTLdrClose(hLdrMod);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstLdr: Failed to close '%s', rc=%Rrc.\n", pszFilename, rc);
        rcRet++;
    }

    return rcRet;
}



int main(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, 0);

    int rcRet = 0;
    if (argc <= 1)
    {
        RTPrintf("usage: %s <module> [more modules]\n", argv[0]);
        return 1;
    }

    /*
     * Iterate the files.
     */
    for (int argi = 1; argi < argc; argi++)
    {
        RTPrintf("tstLdr: TESTING '%s'...\n", argv[argi]);
        rcRet += testLdrOne(argv[argi]);
    }

    /*
     * Test result summary.
     */
    if (!rcRet)
        RTPrintf("tstLdr: SUCCESS\n");
    else
        RTPrintf("tstLdr: FAILURE - %d errors\n", rcRet);
    return !!rcRet;
}
