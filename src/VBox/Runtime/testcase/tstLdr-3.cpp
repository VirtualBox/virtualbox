/* $Id: tstLdr-3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <iprt/err.h>
#include <iprt/string.h>
#include <VBox/dis.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTUINTPTR g_uLoadAddr;
static RTLDRMOD  g_hLdrMod;
static void     *g_pvBits;
static uint8_t   g_cBits;
static uint8_t   g_fNearImports;

/**
 * Current nearest symbol.
 */
typedef struct TESTNEARSYM
{
    RTUINTPTR   Addr;
    struct TESTSYM
    {
        RTUINTPTR   Value;
        unsigned    uSymbol;
        char        szName[512];
    } aSyms[2];
} TESTNEARSYM, *PTESTNEARSYM;

/**
 * Enumeration callback function used by RTLdrEnumSymbols().
 *
 * @returns iprt status code. Failure will stop the enumeration.
 * @param   hLdrMod         The loader module handle.
 * @param   pszSymbol       Symbol name. NULL if ordinal only.
 * @param   uSymbol         Symbol ordinal, ~0 if not used.
 * @param   Value           Symbol value.
 * @param   pvUser          The user argument specified to RTLdrEnumSymbols().
 */
static DECLCALLBACK(int) testEnumSymbol2(RTLDRMOD hLdrMod, const char *pszSymbol, unsigned uSymbol, RTUINTPTR Value, void *pvUser)
{
    RT_NOREF1(hLdrMod);
    PTESTNEARSYM pSym = (PTESTNEARSYM)pvUser;

    /* less or equal */
    if (    Value <= pSym->Addr
        &&  (   Value > pSym->aSyms[0].Value
             || (   Value == pSym->aSyms[0].Value
                 && !pSym->aSyms[0].szName[0]
                 && pszSymbol
                 && *pszSymbol
                )
            )
       )
    {
        pSym->aSyms[0].Value     = Value;
        pSym->aSyms[0].uSymbol   = uSymbol;
        pSym->aSyms[0].szName[0] = '\0';
        if (pszSymbol)
            strncat(pSym->aSyms[0].szName, pszSymbol, sizeof(pSym->aSyms[0].szName)-1);
    }

    /* above */
    if (    Value > pSym->Addr
        &&  (   Value < pSym->aSyms[1].Value
             || (   Value == pSym->aSyms[1].Value
                 && !pSym->aSyms[1].szName[1]
                 && pszSymbol
                 && *pszSymbol
                )
            )
       )
    {
        pSym->aSyms[1].Value     = Value;
        pSym->aSyms[1].uSymbol   = uSymbol;
        pSym->aSyms[1].szName[0] = '\0';
        if (pszSymbol)
            strncat(pSym->aSyms[1].szName, pszSymbol, sizeof(pSym->aSyms[1].szName)-1);
    }

    return VINF_SUCCESS;
}

static int FindNearSymbol(RTUINTPTR uAddr, PTESTNEARSYM pNearSym)
{
    RT_ZERO(*pNearSym);
    pNearSym->Addr = (RTUINTPTR)uAddr;
    pNearSym->aSyms[1].Value = ~(RTUINTPTR)0;
    int rc = RTLdrEnumSymbols(g_hLdrMod, RTLDR_ENUM_SYMBOL_FLAGS_ALL, g_pvBits, g_uLoadAddr, testEnumSymbol2, pNearSym);
    if (RT_FAILURE(rc))
        RTPrintf("tstLdr-3: Failed to enumerate symbols: %Rra\n", rc);
    return rc;
}

static DECLCALLBACK(int) MyGetSymbol(PCDISSTATE pDis, uint32_t u32Sel, RTUINTPTR uAddress,
                                     char *pszBuf, size_t cchBuf, RTINTPTR *poff,
                                     void *pvUser)
{
    RT_NOREF3(pDis, u32Sel, pvUser);

    if (   uAddress > RTLdrSize(g_hLdrMod) + g_uLoadAddr
        || uAddress < g_uLoadAddr)
        return VERR_SYMBOL_NOT_FOUND;

    TESTNEARSYM NearSym;
    int rc = FindNearSymbol(uAddress, &NearSym);
    if (RT_FAILURE(rc))
        return rc;

    RTStrCopy(pszBuf, cchBuf, NearSym.aSyms[0].szName);
    *poff = uAddress - NearSym.aSyms[0].Value;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDISREADBYTES}
 */
static DECLCALLBACK(int) MyReadBytes(PDISSTATE pDis, uint8_t offInstr, uint8_t cbMinRead, uint8_t cbMaxRead)
{
    RT_NOREF1(cbMaxRead);
    uint8_t const *pbSrc = (uint8_t const *)((uintptr_t)pDis->uInstrAddr + (uintptr_t)pDis->pvUser + offInstr);
    memcpy(&pDis->Instr.ab[offInstr], pbSrc, cbMinRead);
    pDis->cbCachedInstr = offInstr + cbMinRead;
    return VINF_SUCCESS;
}


static bool MyDisBlock(DISCPUMODE enmCpuMode, RTHCUINTPTR pvCodeBlock, int32_t cbMax, RTUINTPTR off,
                       RTUINTPTR uNearAddr, RTUINTPTR uSearchAddr)
{
    DISSTATE Dis;
    int32_t  i = 0;
    while (i < cbMax)
    {
        bool        fQuiet    = RTAssertSetQuiet(true);
        bool        fMayPanic = RTAssertSetMayPanic(false);
        char        szOutput[256];
        unsigned    cbInstr;
        int rc = DISInstrWithReader(uNearAddr + i, enmCpuMode,
                                    MyReadBytes, (uint8_t *)pvCodeBlock - (uintptr_t)uNearAddr,
                                    &Dis, &cbInstr);
        RTAssertSetMayPanic(fMayPanic);
        RTAssertSetQuiet(fQuiet);
        if (RT_FAILURE(rc))
            return false;

        TESTNEARSYM NearSym;
        rc = FindNearSymbol(uNearAddr + i, &NearSym);
        if (RT_SUCCESS(rc) && NearSym.aSyms[0].Value == NearSym.Addr)
            RTPrintf("%s:\n", NearSym.aSyms[0].szName);

        DISFormatYasmEx(&Dis, szOutput, sizeof(szOutput),
                        DIS_FMT_FLAGS_RELATIVE_BRANCH | DIS_FMT_FLAGS_BYTES_RIGHT | DIS_FMT_FLAGS_ADDR_LEFT  | DIS_FMT_FLAGS_BYTES_SPACED,
                        MyGetSymbol, NULL);

        RTPrintf("%s\n", szOutput);
        if (pvCodeBlock + i + off == uSearchAddr)
            RTPrintf("^^^^^^^^\n");

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
static DECLCALLBACK(int) testGetImport(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol,
                                       unsigned uSymbol, RTUINTPTR *pValue, void *pvUser)
{
    RT_NOREF5(hLdrMod, pszModule, pszSymbol, uSymbol, pvUser);
    RTUINTPTR BaseAddr = *(PCRTUINTPTR)pvUser;
    if (g_fNearImports)
        *pValue = BaseAddr + UINT32_C(0x604020f0);
    else if (   BaseAddr < UINT64_C(0xffffff7f820df000) - _4G
             || BaseAddr > UINT64_C(0xffffff7f820df000) + _4G)
        *pValue = UINT64_C(0xffffff7f820df000);
    else
        *pValue = UINT64_C(0xffffff7c820df000);
    if (g_cBits == 32)
        *pValue &= UINT32_MAX;
    return VINF_SUCCESS;
}

static uint32_t g_iSegNo = 0;
static DECLCALLBACK(int) testEnumSegment1(RTLDRMOD hLdrMod, PCRTLDRSEG pSeg, void *pvUser)
{
    if (hLdrMod != g_hLdrMod || pvUser != NULL)
        return VERR_INTERNAL_ERROR_3;
    RTPrintf("Seg#%02u: %RTptr LB %RTptr %s\n"
             "   link=%RTptr LB %RTptr align=%RTptr fProt=%#x offFile=%RTfoff\n"
             , g_iSegNo++, pSeg->RVA, pSeg->cbMapped, pSeg->pszName,
             pSeg->LinkAddress, pSeg->cb, pSeg->Alignment, pSeg->fProt, pSeg->offFile);

    return VINF_SUCCESS;
}


/**
 * Enumeration callback function used by RTLdrEnumSymbols().
 *
 * @returns iprt status code. Failure will stop the enumeration.
 * @param   hLdrMod         The loader module handle.
 * @param   pszSymbol       Symbol name. NULL if ordinal only.
 * @param   uSymbol         Symbol ordinal, ~0 if not used.
 * @param   Value           Symbol value.
 * @param   pvUser          The user argument specified to RTLdrEnumSymbols().
 */
static DECLCALLBACK(int) testEnumSymbol1(RTLDRMOD hLdrMod, const char *pszSymbol, unsigned uSymbol, RTUINTPTR Value, void *pvUser)
{
    if (hLdrMod != g_hLdrMod || pvUser != NULL)
        return VERR_INTERNAL_ERROR_3;
    RTPrintf("  %RTptr %s (%d)\n", Value, pszSymbol, uSymbol);
    return VINF_SUCCESS;
}


static int testDisasNear(uint64_t uAddr)
{
    TESTNEARSYM NearSym;
    int rc = FindNearSymbol(uAddr, &NearSym);
    if (RT_FAILURE(rc))
        return rc;

    RTPrintf("tstLdr-3: Addr=%RTptr\n"
             "%RTptr %s (%d) - %RTptr %s (%d)\n",
             NearSym.Addr,
             NearSym.aSyms[0].Value, NearSym.aSyms[0].szName, NearSym.aSyms[0].uSymbol,
             NearSym.aSyms[1].Value, NearSym.aSyms[1].szName, NearSym.aSyms[1].uSymbol);
    if (NearSym.Addr - NearSym.aSyms[0].Value < 0x10000)
    {
        DISCPUMODE enmDisCpuMode = g_cBits == 32 ? DISCPUMODE_32BIT : DISCPUMODE_64BIT;
        uint8_t *pbCode = (uint8_t *)g_pvBits + (NearSym.aSyms[0].Value - g_uLoadAddr);
        MyDisBlock(enmDisCpuMode, (uintptr_t)pbCode,
                   RT_MAX(NearSym.aSyms[1].Value - NearSym.aSyms[0].Value, 0x20000),
                   NearSym.aSyms[0].Value - (uintptr_t)pbCode,
                   NearSym.aSyms[0].Value,
                   NearSym.Addr);
    }

    return VINF_SUCCESS;
}

int main(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, 0);

    /*
     * Module & code bitness (optional).
     */
    g_cBits = ARCH_BITS;
#if !defined(RT_OS_WINDOWS) || defined(RT_OS_DARWIN)
    g_fNearImports = false;
#else
    g_fNearImports = true;
#endif
    while (argc > 1)
    {
        if (!strcmp(argv[1], "--32"))
            g_cBits = 32;
        else if (!strcmp(argv[1], "--64"))
            g_cBits = 64;
        else if (!strcmp(argv[1], "--near-imports"))
            g_fNearImports = true;
        else if (!strcmp(argv[1], "--wide-imports"))
            g_fNearImports = false;
        else
            break;
        argc--;
        argv++;
    }

    int rcRet = 0;
    if (argc <= 2)
    {
        RTPrintf("usage: %s [--32|--64] [--<near|wide>-imports] <load-addr> <module> [addr1 []]\n", argv[0]);
        return 1;
    }

    /*
     * Load the module.
     */
    RTERRINFOSTATIC ErrInfo;
    g_uLoadAddr = (RTUINTPTR)RTStrToUInt64(argv[1]);
    int rc = RTLdrOpenEx(argv[2], 0, RTLDRARCH_WHATEVER, &g_hLdrMod, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstLdr-3: Failed to open '%s': %Rra\n", argv[2], rc);
        if (ErrInfo.szMsg[0])
            RTPrintf("tstLdr-3: %s\n", ErrInfo.szMsg);
        return 1;
    }

    g_pvBits = RTMemAlloc(RTLdrSize(g_hLdrMod));
    rc = RTLdrGetBits(g_hLdrMod, g_pvBits, g_uLoadAddr, testGetImport, &g_uLoadAddr);
    if (RT_SUCCESS(rc))
    {
        if (   argc == 4
            && argv[3][0] == '*')
        {
            /*
             * Wildcard address mode.
             */
            uint64_t uWild       = RTStrToUInt64(&argv[3][1]);
            uint64_t uIncrements = strchr(argv[3], '/') ? RTStrToUInt64(strchr(argv[3], '/') + 1) : 0x1000;
            if (!uIncrements)
                uIncrements = 0x1000;
            uint64_t uMax        = RTLdrSize(g_hLdrMod) + g_uLoadAddr;
            for (uint64_t uCur = g_uLoadAddr + uWild; uCur < uMax; uCur += uIncrements)
                testDisasNear(uCur);
        }
        else if (argc > 3)
        {
            /*
             * User specified addresses within the module.
             */
            for (int i = 3; i < argc; i++)
            {
                rc = testDisasNear(RTStrToUInt64(argv[i]));
                if (RT_FAILURE(rc))
                    rcRet++;
            }
        }
        else
        {
            /*
             * Enumerate symbols.
             */
            rc = RTLdrEnumSymbols(g_hLdrMod, RTLDR_ENUM_SYMBOL_FLAGS_ALL, g_pvBits, g_uLoadAddr, testEnumSymbol1, NULL);
            if (RT_FAILURE(rc))
            {
                RTPrintf("tstLdr-3: Failed to enumerate symbols: %Rra\n", rc);
                rcRet++;
            }

            /*
             * Query various properties.
             */
            union
            {
                char        szName[256];
                uint32_t    iImpModule;
                RTUUID      Uuid;
            } uBuf;
            rc = RTLdrQueryProp(g_hLdrMod, RTLDRPROP_INTERNAL_NAME, &uBuf, sizeof(uBuf));
            if (RT_SUCCESS(rc))
                RTPrintf("tstLdr-3: Internal name: %s\n", uBuf.szName);
            else if (rc != VERR_NOT_FOUND && rc != VERR_NOT_SUPPORTED)
            {
                RTPrintf("tstLdr-3: Internal name: failed - %Rrc\n", rc);
                rcRet++;
            }

            uint32_t cImports = 0;
            rc = RTLdrQueryProp(g_hLdrMod, RTLDRPROP_IMPORT_COUNT, &cImports, sizeof(cImports));
            if (RT_SUCCESS(rc))
            {
                RTPrintf("tstLdr-3: Import count: %u\n", cImports);
                for (uint32_t i = 0; i < cImports; i++)
                {
                    uBuf.iImpModule = i;
                    rc = RTLdrQueryProp(g_hLdrMod, RTLDRPROP_IMPORT_MODULE, &uBuf, sizeof(uBuf));
                    if (RT_SUCCESS(rc))
                        RTPrintf("tstLdr-3: Import module #%u: %s\n", i, uBuf.szName);
                    else
                    {
                        RTPrintf("tstLdr-3: Import module #%u: failed - %Rrc\n", i, rc);
                        rcRet++;
                    }
                }
            }
            else if (rc != VERR_NOT_FOUND && rc != VERR_NOT_SUPPORTED)
            {
                RTPrintf("tstLdr-3: Import count: failed - %Rrc\n", rc);
                rcRet++;
            }

            rc = RTLdrQueryProp(g_hLdrMod, RTLDRPROP_UUID, &uBuf.Uuid, sizeof(uBuf.Uuid));
            if (RT_SUCCESS(rc))
                RTPrintf("tstLdr-3: UUID: %RTuuid\n", uBuf.Uuid);
            else if (rc != VERR_NOT_FOUND && rc != VERR_NOT_SUPPORTED)
            {
                RTPrintf("tstLdr-3: UUID: failed - %Rrc\n", rc);
                rcRet++;
            }

            /*
             * Enumerate segments.
             */
            RTPrintf("tstLdr-3: Segments:\n");
            rc = RTLdrEnumSegments(g_hLdrMod, testEnumSegment1, NULL);
            if (RT_FAILURE(rc))
            {
                RTPrintf("tstLdr-3: Failed to enumerate symbols: %Rra\n", rc);
                rcRet++;
            }
        }
    }
    else
    {
        RTPrintf("tstLdr-3: Failed to get bits for '%s' at %RTptr: %Rra\n", argv[2], g_uLoadAddr, rc);
        rcRet++;
    }
    RTMemFree(g_pvBits);
    RTLdrClose(g_hLdrMod);

    /*
     * Test result summary.
     */
    if (!rcRet)
        RTPrintf("tstLdr-3: SUCCESS\n");
    else
        RTPrintf("tstLdr-3: FAILURE - %d errors\n", rcRet);
    return !!rcRet;
}
