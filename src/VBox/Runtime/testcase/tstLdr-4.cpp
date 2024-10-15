/* $Id: tstLdr-4.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Testcase for RTLdrOpen using ldrLdrObjR0.r0.
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
#include <iprt/log.h>
#include <iprt/stream.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/test.h>

#include <VBox/sup.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST               g_hTest;
static SUPGLOBALINFOPAGE    g_MyGip = { SUPGLOBALINFOPAGE_MAGIC, SUPGLOBALINFOPAGE_VERSION, SUPGIPMODE_INVARIANT_TSC, 42 };
static PSUPGLOBALINFOPAGE   g_pMyGip = &g_MyGip;

extern "C" DECLEXPORT(int) DisasmTest1(void);


static DECLCALLBACK(int) testEnumSegment(RTLDRMOD hLdrMod, PCRTLDRSEG pSeg, void *pvUser)
{
    uint32_t *piSeg = (uint32_t *)pvUser;
    RTPrintf("  Seg#%02u: %RTptr LB %RTptr %s\n"
             "     link=%RTptr LB %RTptr align=%RTptr fProt=%#x offFile=%RTfoff\n"
             , *piSeg, pSeg->RVA, pSeg->cbMapped, pSeg->pszName,
             pSeg->LinkAddress, pSeg->cb, pSeg->Alignment, pSeg->fProt, pSeg->offFile);

    if (pSeg->RVA != NIL_RTLDRADDR)
    {
        RTTESTI_CHECK(pSeg->cbMapped != NIL_RTLDRADDR);
        RTTESTI_CHECK(pSeg->cbMapped >= pSeg->cb);
    }
    else
    {
        RTTESTI_CHECK(pSeg->cbMapped == NIL_RTLDRADDR);
    }

    /*
     * Do some address conversion tests:
     */
    if (pSeg->cbMapped != NIL_RTLDRADDR)
    {
        /* RTLdrRvaToSegOffset: */
        uint32_t    iSegConv   = ~(uint32_t)42;
        RTLDRADDR   offSegConv = ~(RTLDRADDR)22;
        int rc = RTLdrRvaToSegOffset(hLdrMod, pSeg->RVA, &iSegConv, &offSegConv);
        if (RT_FAILURE(rc))
            RTTestIFailed("RTLdrRvaToSegOffset failed on Seg #%u / RVA %#RTptr: %Rrc", *piSeg, pSeg->RVA, rc);
        else if (iSegConv != *piSeg || offSegConv != 0)
                RTTestIFailed("RTLdrRvaToSegOffset on Seg #%u / RVA %#RTptr returned: iSegConv=%#x offSegConv=%RTptr, expected %#x and 0",
                              *piSeg, pSeg->RVA, iSegConv, offSegConv, *piSeg);

        /* RTLdrSegOffsetToRva: */
        RTLDRADDR uRvaConv = ~(RTLDRADDR)22;
        rc = RTLdrSegOffsetToRva(hLdrMod, *piSeg, 0, &uRvaConv);
        if (RT_FAILURE(rc))
            RTTestIFailed("RTLdrSegOffsetToRva failed on Seg #%u / off 0: %Rrc", *piSeg, rc);
        else if (uRvaConv != pSeg->RVA)
            RTTestIFailed("RTLdrSegOffsetToRva on Seg #%u / off 0 returned: %RTptr, expected %RTptr", *piSeg, uRvaConv, pSeg->RVA);

        /* RTLdrLinkAddressToRva: */
        uRvaConv = ~(RTLDRADDR)22;
        rc = RTLdrLinkAddressToRva(hLdrMod, pSeg->LinkAddress, &uRvaConv);
        if (RT_FAILURE(rc))
            RTTestIFailed("RTLdrLinkAddressToRva failed on Seg #%u / %RTptr: %Rrc", *piSeg, pSeg->LinkAddress, rc);
        else if (uRvaConv != pSeg->RVA)
            RTTestIFailed("RTLdrLinkAddressToRva on Seg #%u / %RTptr returned: %RTptr, expected %RTptr",
                          *piSeg, pSeg->LinkAddress, uRvaConv, pSeg->RVA);

        /* RTLdrLinkAddressToSegOffset: */
        iSegConv   = ~(uint32_t)42;
        offSegConv = ~(RTLDRADDR)22;
        rc = RTLdrLinkAddressToSegOffset(hLdrMod, pSeg->LinkAddress, &iSegConv, &offSegConv);
        if (RT_FAILURE(rc))
            RTTestIFailed("RTLdrLinkAddressToSegOffset failed on Seg #%u / %#RTptr: %Rrc", *piSeg, pSeg->LinkAddress, rc);
        else if (iSegConv != *piSeg || offSegConv != 0)
                RTTestIFailed("RTLdrLinkAddressToSegOffset on Seg #%u / %#RTptr returned: iSegConv=%#x offSegConv=%RTptr, expected %#x and 0",
                              *piSeg, pSeg->LinkAddress, iSegConv, offSegConv, *piSeg);
    }

    *piSeg += 1;
    RT_NOREF(hLdrMod);
    return VINF_SUCCESS;
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
static DECLCALLBACK(int) testGetImport(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol, unsigned uSymbol, RTUINTPTR *pValue, void *pvUser)
{
    RT_NOREF4(hLdrMod, pszModule, uSymbol, pvUser);
    if (     !strcmp(pszSymbol, "RTAssertMsg1Weak")     || !strcmp(pszSymbol, "_RTAssertMsg1Weak"))
        *pValue = (uintptr_t)RTAssertMsg1Weak;
    else if (!strcmp(pszSymbol, "RTAssertMsg2Weak")     || !strcmp(pszSymbol, "_RTAssertMsg2Weak"))
        *pValue = (uintptr_t)RTAssertMsg1Weak;
    else if (!strcmp(pszSymbol, "RTAssertMsg1")         || !strcmp(pszSymbol, "_RTAssertMsg1"))
        *pValue = (uintptr_t)RTAssertMsg1;
    else if (!strcmp(pszSymbol, "RTAssertMsg2")         || !strcmp(pszSymbol, "_RTAssertMsg2"))
        *pValue = (uintptr_t)RTAssertMsg2;
    else if (!strcmp(pszSymbol, "RTAssertMsg2V")        || !strcmp(pszSymbol, "_RTAssertMsg2V"))
        *pValue = (uintptr_t)RTAssertMsg2V;
    else if (!strcmp(pszSymbol, "RTAssertMayPanic")     || !strcmp(pszSymbol, "_RTAssertMayPanic"))
        *pValue = (uintptr_t)RTAssertMayPanic;
    else if (!strcmp(pszSymbol, "RTLogDefaultInstanceEx") || !strcmp(pszSymbol, "RTLogDefaultInstanceEx"))
        *pValue = (uintptr_t)RTLogDefaultInstanceEx;
    else if (!strcmp(pszSymbol, "RTLogLoggerExV")       || !strcmp(pszSymbol, "_RTLogLoggerExV"))
        *pValue = (uintptr_t)RTLogLoggerExV;
    else if (!strcmp(pszSymbol, "RTLogPrintfV")         || !strcmp(pszSymbol, "_RTLogPrintfV"))
        *pValue = (uintptr_t)RTLogPrintfV;
    else if (!strcmp(pszSymbol, "RTR0AssertPanicSystem")|| !strcmp(pszSymbol, "_RTR0AssertPanicSystem"))
        *pValue = (uintptr_t)0;
    else if (!strcmp(pszSymbol, "MyPrintf")             || !strcmp(pszSymbol, "_MyPrintf"))
        *pValue = (uintptr_t)RTPrintf;
    else if (!strcmp(pszSymbol, "SUPR0Printf")          || !strcmp(pszSymbol, "_SUPR0Printf"))
        *pValue = (uintptr_t)RTPrintf;
    else if (!strcmp(pszSymbol, "SUPR0PrintfV")         || !strcmp(pszSymbol, "_SUPR0PrintfV"))
        *pValue = (uintptr_t)RTPrintfV;
    else if (!strcmp(pszSymbol, "SomeImportFunction")   || !strcmp(pszSymbol, "_SomeImportFunction"))
        *pValue = (uintptr_t)0;
    else if (!strcmp(pszSymbol, "g_pSUPGlobalInfoPage") || !strcmp(pszSymbol, "_g_pSUPGlobalInfoPage"))
        *pValue = (uintptr_t)&g_pMyGip;
    else if (!strcmp(pszSymbol, "g_SUPGlobalInfoPage")  || !strcmp(pszSymbol, "_g_SUPGlobalInfoPage"))
        *pValue = (uintptr_t)&g_MyGip;
    else
    {
        RTPrintf("tstLdr-4: Unexpected import '%s'!\n", pszSymbol);
        return VERR_SYMBOL_NOT_FOUND;
    }
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
 * @param   pszFilename     The file to load the mess with.
 */
static void testLdrOne(const char *pszFilename)
{
    RTTestSub(g_hTest, RTPathFilename(pszFilename));

    size_t          cbImage = 0;
    struct Load
    {
        RTLDRMOD    hLdrMod;
        void       *pvBits;
        size_t      cbBits;
        const char *pszName;
    }   aLoads[6] =
    {
        { NULL, NULL, 0, "foo" },
        { NULL, NULL, 0, "bar" },
        { NULL, NULL, 0, "foobar" },
    };
    unsigned i;
    int rc;

    /*
     * Load them.
     */
    for (i = 0; i < RT_ELEMENTS(aLoads); i++)
    {
        rc = RTLdrOpen(pszFilename, 0, RTLDRARCH_WHATEVER, &aLoads[i].hLdrMod);
        if (RT_FAILURE(rc))
        {
            RTTestIFailed("tstLdr-4: Failed to open '%s'/%d, rc=%Rrc. aborting test.", pszFilename, i, rc);
            Assert(aLoads[i].hLdrMod == NIL_RTLDRMOD);
            break;
        }

        /* size it */
        size_t cb = RTLdrSize(aLoads[i].hLdrMod);
        if (cbImage && cb != cbImage)
        {
            RTTestIFailed("tstLdr-4: Size mismatch '%s'/%d. aborting test.", pszFilename, i);
            break;
        }
        aLoads[i].cbBits = cbImage = cb;

        /* Allocate bits. */
        aLoads[i].pvBits = RTMemPageAlloc(cb);
        if (!aLoads[i].pvBits)
        {
            RTTestIFailed("Out of memory '%s'/%d cbImage=%d. aborting test.", pszFilename, i, cbImage);
            break;
        }
        rc = RTMemProtect(aLoads[i].pvBits, cb, RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC);
        if (RT_FAILURE(rc))
        {
            RTTestIFailed("RTMemProtect/RWX '%s'/%d cbImage=%d, %Rrc. aborting test.", pszFilename, i, cbImage, rc);
            break;
        }

        /* Get the bits. */
        rc = RTLdrGetBits(aLoads[i].hLdrMod, aLoads[i].pvBits, (uintptr_t)aLoads[i].pvBits, testGetImport, NULL);
        if (RT_FAILURE(rc))
        {
            RTTestIFailed("Failed to get bits for '%s'/%d, rc=%Rrc. aborting test", pszFilename, i, rc);
            break;
        }
    }

    /*
     * Execute the code.
     */
    if (!RTTestSubErrorCount(g_hTest))
    {
        for (i = 0; i < RT_ELEMENTS(aLoads); i += 1)
        {
            /* VERR_ELF_EXE_NOT_SUPPORTED in the previous loop? */
            if (!aLoads[i].hLdrMod)
                continue;
            /* get the pointer. */
            RTUINTPTR Value;
            rc = RTLdrGetSymbolEx(aLoads[i].hLdrMod, aLoads[i].pvBits, (uintptr_t)aLoads[i].pvBits,
                                  UINT32_MAX, "DisasmTest1", &Value);
            if (rc == VERR_SYMBOL_NOT_FOUND)
                rc = RTLdrGetSymbolEx(aLoads[i].hLdrMod, aLoads[i].pvBits, (uintptr_t)aLoads[i].pvBits,
                                      UINT32_MAX, "_DisasmTest1", &Value);
            if (RT_FAILURE(rc))
            {
                RTTestIFailed("Failed to get symbol \"DisasmTest1\" from load #%d: %Rrc", i, rc);
                break;
            }
            typedef DECLCALLBACKPTR(int, PFNDISASMTEST1,(void));
            PFNDISASMTEST1 pfnDisasmTest1 = (PFNDISASMTEST1)(uintptr_t)Value;
            RTPrintf("tstLdr-4: pfnDisasmTest1=%p / add-symbol-file %s %#p\n", pfnDisasmTest1, pszFilename, aLoads[i].pvBits);
            uint32_t iSeg = 0;
            RTLdrEnumSegments(aLoads[i].hLdrMod, testEnumSegment, &iSeg);

            /* call the test function. */
            rc = pfnDisasmTest1();
            if (rc)
                RTTestIFailed("load #%d Test1 -> %#x", i, rc);

            /* While we're here, check a couple of RTLdrQueryProp calls too */
            void *pvBits = aLoads[i].pvBits;
            for (unsigned iBits = 0; iBits < 2; iBits++, pvBits = NULL)
            {
                union
                {
                    char szName[127];
                } uBuf;
                rc = RTLdrQueryPropEx(aLoads[i].hLdrMod, RTLDRPROP_INTERNAL_NAME, aLoads[i].pvBits,
                                      uBuf.szName, sizeof(uBuf.szName), NULL);
                if (RT_SUCCESS(rc))
                    RTPrintf("tstLdr-4: internal name #%d: '%s'\n", i, uBuf.szName);
                else if (rc != VERR_NOT_FOUND && rc != VERR_NOT_SUPPORTED)
                    RTPrintf("tstLdr-4: internal name #%d failed: %Rrc\n", i, rc);
            }
        }
    }

    /*
     * Clean up.
     */
    for (i = 0; i < RT_ELEMENTS(aLoads); i++)
    {
        if (aLoads[i].pvBits)
        {
            RTMemProtect(aLoads[i].pvBits, aLoads[i].cbBits, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
            RTMemPageFree(aLoads[i].pvBits, aLoads[i].cbBits);
        }
        if (aLoads[i].hLdrMod)
        {
            rc = RTLdrClose(aLoads[i].hLdrMod);
            if (RT_FAILURE(rc))
                RTTestIFailed("Failed to close '%s' i=%d, rc=%Rrc.", pszFilename, i, rc);
        }
    }

}



int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstLdr-4", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Sanity check.
     */
    int rc = DisasmTest1();
    if (rc == 0)
    {
        /*
         * Execute the test.
         */
        char szPath[RTPATH_MAX];
        rc = RTPathExecDir(szPath, sizeof(szPath) - sizeof("/tstLdrObjR0.r0"));
        if (RT_SUCCESS(rc))
        {
            strcat(szPath, "/tstLdrObjR0.r0");

            testLdrOne(szPath);
        }
        else
            RTTestIFailed("RTPathExecDir -> %Rrc", rc);
    }
    else
        RTTestIFailed("FATAL ERROR - DisasmTest1 is buggy: rc=%#x", rc);

    return RTTestSummaryAndDestroy(g_hTest);
}
