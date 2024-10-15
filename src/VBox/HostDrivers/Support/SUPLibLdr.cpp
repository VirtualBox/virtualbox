/* $Id: SUPLibLdr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Library - Loader related bits.
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
#define LOG_GROUP LOG_GROUP_SUP
#include <VBox/sup.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/log.h>
#include <VBox/VBoxTpG.h>

#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/alloca.h>
#include <iprt/ldr.h>
#include <iprt/asm.h>
#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/thread.h>
#include <iprt/process.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/env.h>
#include <iprt/rand.h>
#include <iprt/x86.h>

#include "SUPDrvIOC.h"
#include "SUPLibInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** R0 VMM module name. */
#define VMMR0_NAME      "VMMR0"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef DECLCALLBACKTYPE(int, FNCALLVMMR0,(PVMR0 pVMR0, unsigned uOperation, void *pvArg));
typedef FNCALLVMMR0 *PFNCALLVMMR0;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** VMMR0 Load Address. */
static RTR0PTR                  g_pvVMMR0 = NIL_RTR0PTR;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int               supLoadModule(const char *pszFilename, const char *pszModule, const char *pszSrvReqHandler,
                                       PRTERRINFO pErrInfo, void **ppvImageBase);
static DECLCALLBACK(int) supLoadModuleResolveImport(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol,
                                                    unsigned uSymbol, RTUINTPTR *pValue, void *pvUser);


SUPR3DECL(int) SUPR3LoadModule(const char *pszFilename, const char *pszModule, void **ppvImageBase, PRTERRINFO pErrInfo)
{
    /*
     * Check that the module can be trusted.
     */
    int rc = SUPR3HardenedVerifyPlugIn(pszFilename, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        rc = supLoadModule(pszFilename, pszModule, NULL, pErrInfo, ppvImageBase);
        if (RT_FAILURE(rc) && !RTErrInfoIsSet(pErrInfo))
            RTErrInfoSetF(pErrInfo, rc, "SUPR3LoadModule: supLoadModule returned %Rrc", rc);
    }
    return rc;
}


SUPR3DECL(int) SUPR3LoadServiceModule(const char *pszFilename, const char *pszModule,
                                      const char *pszSrvReqHandler, void **ppvImageBase)
{
    AssertPtrReturn(pszSrvReqHandler, VERR_INVALID_PARAMETER);

    /*
     * Check that the module can be trusted.
     */
    int rc = SUPR3HardenedVerifyPlugIn(pszFilename, NULL /*pErrInfo*/);
    if (RT_SUCCESS(rc))
        rc = supLoadModule(pszFilename, pszModule, pszSrvReqHandler, NULL /*pErrInfo*/, ppvImageBase);
    else
        LogRel(("SUPR3LoadServiceModule: Verification of \"%s\" failed, rc=%Rrc\n", pszFilename, rc));
    return rc;
}


/**
 * Argument package for supLoadModuleResolveImport.
 */
typedef struct SUPLDRRESIMPARGS
{
    const char *pszModule;
    PRTERRINFO  pErrInfo;
    uint32_t    fLoadReq;               /**< SUPLDRLOAD_F_XXX */
} SUPLDRRESIMPARGS, *PSUPLDRRESIMPARGS;

/**
 * Resolve an external symbol during RTLdrGetBits().
 *
 * @returns VBox status code.
 * @param   hLdrMod         The loader module handle.
 * @param   pszModule       Module name.
 * @param   pszSymbol       Symbol name, NULL if uSymbol should be used.
 * @param   uSymbol         Symbol ordinal, ~0 if pszSymbol should be used.
 * @param   pValue          Where to store the symbol value (address).
 * @param   pvUser          User argument.
 */
static DECLCALLBACK(int) supLoadModuleResolveImport(RTLDRMOD hLdrMod, const char *pszModule,
                                                    const char *pszSymbol, unsigned uSymbol, RTUINTPTR *pValue, void *pvUser)
{
    NOREF(hLdrMod); NOREF(uSymbol);
    AssertPtr(pValue);
    AssertPtr(pvUser);
    PSUPLDRRESIMPARGS pArgs = (PSUPLDRRESIMPARGS)pvUser;

    /*
     * Only SUPR0 and VMMR0.r0
     */
    if (   pszModule
        && *pszModule
        && strcmp(pszModule, "VBoxSup.sys")
        && strcmp(pszModule, "VBoxDrv.sys") /* old name */
        && strcmp(pszModule, "VMMR0.r0"))
    {
#if defined(RT_OS_WINDOWS) && 0 /* Useful for VMMR0 hacking, not for production use.  See also SUPDrv-win.cpp */
        if (strcmp(pszModule, "ntoskrnl.exe") == 0)
        {
            *pValue = 42; /* Non-zero so ring-0 can find the end of the IAT and exclude it when comparing. */
            return VINF_SUCCESS;
        }
#endif
        AssertMsgFailed(("%s is importing from %s! (expected 'SUPR0.dll' or 'VMMR0.r0', case-sensitive)\n", pArgs->pszModule, pszModule));
        return RTErrInfoSetF(pArgs->pErrInfo, VERR_SYMBOL_NOT_FOUND,
                             "Unexpected import module '%s' in '%s'", pszModule, pArgs->pszModule);
    }

    /*
     * No ordinals.
     */
    if (uSymbol != ~0U)
    {
        AssertMsgFailed(("%s is importing by ordinal (ord=%d)\n", pArgs->pszModule, uSymbol));
        return RTErrInfoSetF(pArgs->pErrInfo, VERR_SYMBOL_NOT_FOUND,
                             "Unexpected ordinal import (%#x) in '%s'", uSymbol, pArgs->pszModule);
    }

    /*
     * Lookup symbol.
     */
    /* Skip the 64-bit ELF import prefix first. */
    /** @todo is this actually used??? */
    if (!strncmp(pszSymbol, RT_STR_TUPLE("SUPR0$")))
        pszSymbol += sizeof("SUPR0$") - 1;

    /*
     * Check the VMMR0.r0 module if loaded.
     */
    if (g_pvVMMR0 != NIL_RTR0PTR)
    {
        void *pvValue;
        if (!SUPR3GetSymbolR0((void *)g_pvVMMR0, pszSymbol, &pvValue))
        {
            *pValue = (uintptr_t)pvValue;
            pArgs->fLoadReq |= SUPLDRLOAD_F_DEP_VMMR0;
            return VINF_SUCCESS;
        }
    }

    /* iterate the function table. */
    int c = g_pSupFunctions->u.Out.cFunctions;
    PSUPFUNC pFunc = &g_pSupFunctions->u.Out.aFunctions[0];
    while (c-- > 0)
    {
        if (!strcmp(pFunc->szName, pszSymbol))
        {
            *pValue = (uintptr_t)pFunc->pfn;
            return VINF_SUCCESS;
        }
        pFunc++;
    }

    /*
     * The GIP.
     */
    if (    pszSymbol
        &&  g_pSUPGlobalInfoPage
        &&  g_pSUPGlobalInfoPageR0
        &&  !strcmp(pszSymbol, "g_SUPGlobalInfoPage")
       )
    {
        *pValue = (uintptr_t)g_pSUPGlobalInfoPageR0;
        return VINF_SUCCESS;
    }

    /*
     * Symbols that are undefined by convention.
     */
#ifdef RT_OS_SOLARIS
    static const char * const s_apszConvSyms[] =
    {
        "", "mod_getctl",
        "", "mod_install",
        "", "mod_remove",
        "", "mod_info",
        "", "mod_miscops",
    };
    for (unsigned i = 0; i < RT_ELEMENTS(s_apszConvSyms); i += 2)
    {
        if (   !RTStrCmp(s_apszConvSyms[i],     pszModule)
            && !RTStrCmp(s_apszConvSyms[i + 1], pszSymbol))
        {
            *pValue = ~(uintptr_t)0;
            return VINF_SUCCESS;
        }
    }
#endif

    /*
     * Despair.
     */
    c = g_pSupFunctions->u.Out.cFunctions;
    pFunc = &g_pSupFunctions->u.Out.aFunctions[0];
    while (c-- > 0)
    {
        RTAssertMsg2Weak("%d: %s\n", g_pSupFunctions->u.Out.cFunctions - c, pFunc->szName);
        pFunc++;
    }
    RTAssertMsg2Weak("%s is importing %s which we couldn't find\n", pArgs->pszModule, pszSymbol);

    AssertLogRelMsgFailed(("%s is importing %s which we couldn't find\n", pArgs->pszModule, pszSymbol));
    if (g_uSupFakeMode)
    {
        *pValue = 0xdeadbeef;
        return VINF_SUCCESS;
    }
    return RTErrInfoSetF(pArgs->pErrInfo, VERR_SYMBOL_NOT_FOUND,
                         "Unable to locate imported symbol '%s%s%s' for module '%s'",
                         pszModule ? pszModule : "",
                         pszModule && *pszModule ? "." : "",
                         pszSymbol,
                         pArgs->pszModule);
}


/** Argument package for supLoadModuleCalcSizeCB. */
typedef struct SUPLDRCALCSIZEARGS
{
    size_t          cbStrings;
    uint32_t        cSymbols;
    size_t          cbImage;
} SUPLDRCALCSIZEARGS, *PSUPLDRCALCSIZEARGS;

/**
 * Callback used to calculate the image size.
 * @return VINF_SUCCESS
 */
static DECLCALLBACK(int) supLoadModuleCalcSizeCB(RTLDRMOD hLdrMod, const char *pszSymbol, unsigned uSymbol, RTUINTPTR Value, void *pvUser)
{
    PSUPLDRCALCSIZEARGS pArgs = (PSUPLDRCALCSIZEARGS)pvUser;
    if (    pszSymbol != NULL
        &&  *pszSymbol
        &&  Value <= pArgs->cbImage)
    {
        pArgs->cSymbols++;
        pArgs->cbStrings += strlen(pszSymbol) + 1;
    }
    NOREF(hLdrMod); NOREF(uSymbol);
    return VINF_SUCCESS;
}


/** Argument package for supLoadModuleCreateTabsCB. */
typedef struct SUPLDRCREATETABSARGS
{
    size_t          cbImage;
    PSUPLDRSYM      pSym;
    char           *pszBase;
    char           *psz;
} SUPLDRCREATETABSARGS, *PSUPLDRCREATETABSARGS;

/**
 * Callback used to calculate the image size.
 * @return VINF_SUCCESS
 */
static DECLCALLBACK(int) supLoadModuleCreateTabsCB(RTLDRMOD hLdrMod, const char *pszSymbol, unsigned uSymbol, RTUINTPTR Value, void *pvUser)
{
    PSUPLDRCREATETABSARGS pArgs = (PSUPLDRCREATETABSARGS)pvUser;
    if (    pszSymbol != NULL
        &&  *pszSymbol
        &&  Value <= pArgs->cbImage)
    {
        pArgs->pSym->offSymbol = (uint32_t)Value;
        pArgs->pSym->offName = pArgs->psz - pArgs->pszBase;
        pArgs->pSym++;

        size_t cbCopy = strlen(pszSymbol) + 1;
        memcpy(pArgs->psz, pszSymbol, cbCopy);
        pArgs->psz += cbCopy;
    }
    NOREF(hLdrMod); NOREF(uSymbol);
    return VINF_SUCCESS;
}


/** Argument package for supLoadModuleCompileSegmentsCB. */
typedef struct SUPLDRCOMPSEGTABARGS
{
    uint32_t        uStartRva;
    uint32_t        uEndRva;
    uint32_t        fProt;
    uint32_t        iSegs;
    uint32_t        cSegsAlloc;
    PSUPLDRSEG      paSegs;
    PRTERRINFO      pErrInfo;
} SUPLDRCOMPSEGTABARGS, *PSUPLDRCOMPSEGTABARGS;

/**
 * @callback_method_impl{FNRTLDRENUMSEGS,
 *  Compile list of segments with the same memory protection.}
 */
static DECLCALLBACK(int) supLoadModuleCompileSegmentsCB(RTLDRMOD hLdrMod, PCRTLDRSEG pSeg, void *pvUser)
{
    PSUPLDRCOMPSEGTABARGS pArgs = (PSUPLDRCOMPSEGTABARGS)pvUser;
    AssertCompile(RTMEM_PROT_READ  == SUPLDR_PROT_READ);
    AssertCompile(RTMEM_PROT_WRITE == SUPLDR_PROT_WRITE);
    AssertCompile(RTMEM_PROT_EXEC  == SUPLDR_PROT_EXEC);
    RT_NOREF(hLdrMod);

    Log2(("supLoadModuleCompileSegmentsCB: %RTptr/%RTptr LB %RTptr/%RTptr prot %#x %s\n",
          pSeg->LinkAddress, pSeg->RVA, pSeg->cbMapped, pSeg->cb, pSeg->fProt, pSeg->pszName));

    /* Ignore segments not part of the loaded image. */
    if (pSeg->RVA == NIL_RTLDRADDR || pSeg->cbMapped == 0)
    {
        Log2(("supLoadModuleCompileSegmentsCB: -> skipped\n"));
        return VINF_SUCCESS;
    }

    /* We currently ASSUME that all relevant segments are in ascending RVA order. */
    AssertReturn(pSeg->RVA >= pArgs->uEndRva,
                 RTERRINFO_LOG_REL_SET_F(pArgs->pErrInfo, VERR_BAD_EXE_FORMAT, "Out of order segment: %p LB %#zx #%.*s",
                                         pSeg->RVA, pSeg->cb, pSeg->cchName, pSeg->pszName));

    /* We ASSUME the cbMapped field is implemented. */
    AssertReturn(pSeg->cbMapped != NIL_RTLDRADDR, VERR_INTERNAL_ERROR_2);
    AssertReturn(pSeg->cbMapped < _1G, VERR_INTERNAL_ERROR_4);
    uint32_t cbMapped = (uint32_t)pSeg->cbMapped;
    AssertReturn(pSeg->RVA      < _1G, VERR_INTERNAL_ERROR_3);
    uint32_t uRvaSeg  = (uint32_t)pSeg->RVA;

    /*
     * If the protection is the same as the previous segment,
     * just update uEndRva and continue.
     */
    uint32_t fProt = pSeg->fProt;
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    if (fProt & RTMEM_PROT_EXEC)
        fProt |= fProt & RTMEM_PROT_READ;
#endif
    if (pSeg->fProt == pArgs->fProt)
    {
        pArgs->uEndRva = uRvaSeg + cbMapped;
        Log2(("supLoadModuleCompileSegmentsCB: -> merged, end %#x\n", pArgs->uEndRva));
        return VINF_SUCCESS;
    }

    /*
     * The protection differs, so commit current segment and start a new one.
     * However, if the new segment and old segment share a page, this becomes
     * a little more complicated...
     */
    if (pArgs->uStartRva < pArgs->uEndRva)
    {
        if (((pArgs->uEndRva - 1) >> PAGE_SHIFT) != (uRvaSeg >> PAGE_SHIFT))
        {
            /* No common page, so make the new segment start on a page boundrary. */
            cbMapped += uRvaSeg & PAGE_OFFSET_MASK;
            uRvaSeg &= ~(uint32_t)PAGE_OFFSET_MASK;
            Assert(pArgs->uEndRva <= uRvaSeg);
            Log2(("supLoadModuleCompileSegmentsCB: -> new, no common\n"));
        }
        else if ((fProt & pArgs->fProt) == fProt)
        {
            /* The current segment includes the memory protections of the
               previous, so include the common page in it: */
            uint32_t const cbCommon = PAGE_SIZE - (uRvaSeg & PAGE_OFFSET_MASK);
            if (cbCommon >= cbMapped)
            {
                pArgs->uEndRva = uRvaSeg + cbMapped;
                Log2(("supLoadModuleCompileSegmentsCB: -> merge, %#x common, upgrading prot to %#x, end %#x\n",
                      cbCommon, pArgs->fProt, pArgs->uEndRva));
                return VINF_SUCCESS; /* New segment was smaller than a page. */
            }
            cbMapped -= cbCommon;
            uRvaSeg  += cbCommon;
            Assert(pArgs->uEndRva <= uRvaSeg);
            Log2(("supLoadModuleCompileSegmentsCB: -> new, %#x common into previous\n", cbCommon));
        }
        else if ((fProt & pArgs->fProt) == pArgs->fProt)
        {
            /* The new segment includes the memory protections of the
               previous, so include the common page in it: */
            cbMapped += uRvaSeg & PAGE_OFFSET_MASK;
            uRvaSeg &= ~(uint32_t)PAGE_OFFSET_MASK;
            if (uRvaSeg == pArgs->uStartRva)
            {
                pArgs->fProt   = fProt;
                pArgs->uEndRva = uRvaSeg + cbMapped;
                Log2(("supLoadModuleCompileSegmentsCB: -> upgrade current protection, end %#x\n", pArgs->uEndRva));
                return VINF_SUCCESS; /* Current segment was smaller than a page. */
            }
            Log2(("supLoadModuleCompileSegmentsCB: -> new, %#x common into new\n", (uint32_t)(pSeg->RVA & PAGE_OFFSET_MASK)));
        }
        else
        {
            /* Create a new segment for the common page with the combined protection. */
            Log2(("supLoadModuleCompileSegmentsCB: -> it's complicated...\n"));
            pArgs->uEndRva &= ~(uint32_t)PAGE_OFFSET_MASK;
            if (pArgs->uEndRva > pArgs->uStartRva)
            {
                Log2(("supLoadModuleCompileSegmentsCB: SUP Seg #%u: %#x LB %#x prot %#x\n",
                      pArgs->iSegs, pArgs->uStartRva, pArgs->uEndRva - pArgs->uStartRva, pArgs->fProt));
                if (pArgs->paSegs)
                {
                    AssertReturn(pArgs->iSegs < pArgs->cSegsAlloc, VERR_INTERNAL_ERROR_5);
                    pArgs->paSegs[pArgs->iSegs].off     = pArgs->uStartRva;
                    pArgs->paSegs[pArgs->iSegs].cb      = pArgs->uEndRva - pArgs->uStartRva;
                    pArgs->paSegs[pArgs->iSegs].fProt   = pArgs->fProt;
                    pArgs->paSegs[pArgs->iSegs].fUnused = 0;
                }
                pArgs->iSegs++;
                pArgs->uStartRva = pArgs->uEndRva;
            }
            pArgs->fProt |= fProt;

            uint32_t const cbCommon = PAGE_SIZE - (uRvaSeg & PAGE_OFFSET_MASK);
            if (cbCommon >= cbMapped)
            {
                fProt |= pArgs->fProt;
                pArgs->uEndRva = uRvaSeg + cbMapped;
                return VINF_SUCCESS; /* New segment was smaller than a page. */
            }
            cbMapped -= cbCommon;
            uRvaSeg  += cbCommon;
            Assert(uRvaSeg - pArgs->uStartRva == PAGE_SIZE);
        }

        /* The current segment should end where the new one starts, no gaps. */
        pArgs->uEndRva = uRvaSeg;

        /* Emit the current segment */
        Log2(("supLoadModuleCompileSegmentsCB: SUP Seg #%u: %#x LB %#x prot %#x\n",
              pArgs->iSegs, pArgs->uStartRva, pArgs->uEndRva - pArgs->uStartRva, pArgs->fProt));
        if (pArgs->paSegs)
        {
            AssertReturn(pArgs->iSegs < pArgs->cSegsAlloc, VERR_INTERNAL_ERROR_5);
            pArgs->paSegs[pArgs->iSegs].off     = pArgs->uStartRva;
            pArgs->paSegs[pArgs->iSegs].cb      = pArgs->uEndRva - pArgs->uStartRva;
            pArgs->paSegs[pArgs->iSegs].fProt   = pArgs->fProt;
            pArgs->paSegs[pArgs->iSegs].fUnused = 0;
        }
        pArgs->iSegs++;
    }
    /* else: current segment is empty */

    /* Start the new segment. */
    Assert(!(uRvaSeg & PAGE_OFFSET_MASK));
    pArgs->fProt     = fProt;
    pArgs->uStartRva = uRvaSeg;
    pArgs->uEndRva   = uRvaSeg + cbMapped;
    return VINF_SUCCESS;
}


/**
 * Worker for supLoadModule().
 */
static int supLoadModuleInner(RTLDRMOD hLdrMod, PSUPLDRLOAD pLoadReq, uint32_t cbImageWithEverything,
                              RTR0PTR uImageBase, size_t cbImage, const char *pszModule, const char *pszFilename,
                              bool fNativeLoader, bool fIsVMMR0, const char *pszSrvReqHandler,
                              uint32_t offSymTab, uint32_t cSymbols,
                              uint32_t offStrTab, size_t cbStrTab,
                              uint32_t offSegTab, uint32_t cSegments,
                              PRTERRINFO pErrInfo)
{
    /*
     * Get the image bits.
     */
    SUPLDRRESIMPARGS Args = { pszModule, pErrInfo, 0 };
    int rc = RTLdrGetBits(hLdrMod, &pLoadReq->u.In.abImage[0], uImageBase, supLoadModuleResolveImport, &Args);
    if (RT_FAILURE(rc))
    {
        LogRel(("SUP: RTLdrGetBits failed for %s (%s). rc=%Rrc\n", pszModule, pszFilename, rc));
        if (!RTErrInfoIsSet(pErrInfo))
            RTErrInfoSetF(pErrInfo, rc, "RTLdrGetBits failed");
        return rc;
    }

    /*
     * Get the entry points.
     */
    RTUINTPTR VMMR0EntryFast = 0;
    RTUINTPTR VMMR0EntryEx = 0;
    RTUINTPTR SrvReqHandler = 0;
    RTUINTPTR ModuleInit = 0;
    RTUINTPTR ModuleTerm = 0;
    const char *pszEp = NULL;
    if (fIsVMMR0)
    {
        rc = RTLdrGetSymbolEx(hLdrMod, &pLoadReq->u.In.abImage[0], uImageBase,
                              UINT32_MAX, pszEp = "VMMR0EntryFast", &VMMR0EntryFast);
        if (RT_SUCCESS(rc))
            rc = RTLdrGetSymbolEx(hLdrMod, &pLoadReq->u.In.abImage[0], uImageBase,
                                  UINT32_MAX, pszEp = "VMMR0EntryEx", &VMMR0EntryEx);
    }
    else if (pszSrvReqHandler)
        rc = RTLdrGetSymbolEx(hLdrMod, &pLoadReq->u.In.abImage[0], uImageBase,
                              UINT32_MAX, pszEp = pszSrvReqHandler, &SrvReqHandler);
    if (RT_SUCCESS(rc))
    {
        int rc2 = RTLdrGetSymbolEx(hLdrMod, &pLoadReq->u.In.abImage[0], uImageBase,
                                   UINT32_MAX, pszEp = "ModuleInit", &ModuleInit);
        if (RT_FAILURE(rc2))
            ModuleInit = 0;

        rc2 = RTLdrGetSymbolEx(hLdrMod, &pLoadReq->u.In.abImage[0], uImageBase,
                               UINT32_MAX, pszEp = "ModuleTerm", &ModuleTerm);
        if (RT_FAILURE(rc2))
            ModuleTerm = 0;
    }
    if (RT_FAILURE(rc))
    {
        LogRel(("SUP: Failed to get entry point '%s' for %s (%s) rc=%Rrc\n", pszEp, pszModule, pszFilename, rc));
        return RTErrInfoSetF(pErrInfo, rc, "Failed to resolve entry point '%s'", pszEp);
    }

    /*
     * Create the symbol and string tables.
     */
    SUPLDRCREATETABSARGS CreateArgs;
    CreateArgs.cbImage = cbImage;
    CreateArgs.pSym    = (PSUPLDRSYM)&pLoadReq->u.In.abImage[offSymTab];
    CreateArgs.pszBase =     (char *)&pLoadReq->u.In.abImage[offStrTab];
    CreateArgs.psz     = CreateArgs.pszBase;
    rc = RTLdrEnumSymbols(hLdrMod, 0, NULL, 0, supLoadModuleCreateTabsCB, &CreateArgs);
    if (RT_FAILURE(rc))
    {
        LogRel(("SUP: RTLdrEnumSymbols failed for %s (%s) rc=%Rrc\n", pszModule, pszFilename, rc));
        return RTErrInfoSetF(pErrInfo, rc, "RTLdrEnumSymbols #2 failed");
    }
    AssertRelease((size_t)(CreateArgs.psz  - CreateArgs.pszBase) <= cbStrTab);
    AssertRelease((size_t)(CreateArgs.pSym - (PSUPLDRSYM)&pLoadReq->u.In.abImage[offSymTab]) <= cSymbols);

    /*
     * Create the segment table.
     */
    SUPLDRCOMPSEGTABARGS SegArgs;
    SegArgs.uStartRva   = 0;
    SegArgs.uEndRva     = 0;
    SegArgs.fProt       = RTMEM_PROT_READ;
    SegArgs.iSegs       = 0;
    SegArgs.cSegsAlloc  = cSegments;
    SegArgs.paSegs      = (PSUPLDRSEG)&pLoadReq->u.In.abImage[offSegTab];
    SegArgs.pErrInfo    = pErrInfo;
    rc = RTLdrEnumSegments(hLdrMod, supLoadModuleCompileSegmentsCB, &SegArgs);
    if (RT_FAILURE(rc))
    {
        LogRel(("SUP: RTLdrEnumSegments failed for %s (%s) rc=%Rrc\n", pszModule, pszFilename, rc));
        return RTErrInfoSetF(pErrInfo, rc, "RTLdrEnumSegments #2 failed");
    }
    SegArgs.uEndRva = (uint32_t)cbImage;
    AssertReturn(SegArgs.uEndRva == cbImage, VERR_OUT_OF_RANGE);
    if (SegArgs.uEndRva > SegArgs.uStartRva)
    {
        SegArgs.paSegs[SegArgs.iSegs].off     = SegArgs.uStartRva;
        SegArgs.paSegs[SegArgs.iSegs].cb      = SegArgs.uEndRva - SegArgs.uStartRva;
        SegArgs.paSegs[SegArgs.iSegs].fProt   = SegArgs.fProt;
        SegArgs.paSegs[SegArgs.iSegs].fUnused = 0;
        SegArgs.iSegs++;
    }
    for (uint32_t i = 0; i < SegArgs.iSegs; i++)
        LogRel(("SUP: seg #%u: %c%c%c %#010RX32 LB %#010RX32\n", i, /** @todo LogRel2 */
                SegArgs.paSegs[i].fProt & SUPLDR_PROT_READ  ? 'R' : ' ',
                SegArgs.paSegs[i].fProt & SUPLDR_PROT_WRITE ? 'W' : ' ',
                SegArgs.paSegs[i].fProt & SUPLDR_PROT_EXEC  ? 'X' : ' ',
                SegArgs.paSegs[i].off, SegArgs.paSegs[i].cb));
    AssertRelease(SegArgs.iSegs == cSegments);
    AssertRelease(SegArgs.cSegsAlloc == cSegments);

    /*
     * Upload the image.
     */
    pLoadReq->Hdr.u32Cookie = g_u32Cookie;
    pLoadReq->Hdr.u32SessionCookie = g_u32SessionCookie;
    pLoadReq->Hdr.cbIn = SUP_IOCTL_LDR_LOAD_SIZE_IN(cbImageWithEverything);
    pLoadReq->Hdr.cbOut = SUP_IOCTL_LDR_LOAD_SIZE_OUT;
    pLoadReq->Hdr.fFlags = SUPREQHDR_FLAGS_MAGIC | SUPREQHDR_FLAGS_EXTRA_IN;
    pLoadReq->Hdr.rc = VERR_INTERNAL_ERROR;

    pLoadReq->u.In.pfnModuleInit              = (RTR0PTR)ModuleInit;
    pLoadReq->u.In.pfnModuleTerm              = (RTR0PTR)ModuleTerm;
    if (fIsVMMR0)
    {
        pLoadReq->u.In.eEPType                = SUPLDRLOADEP_VMMR0;
        pLoadReq->u.In.EP.VMMR0.pvVMMR0EntryFast = (RTR0PTR)VMMR0EntryFast;
        pLoadReq->u.In.EP.VMMR0.pvVMMR0EntryEx   = (RTR0PTR)VMMR0EntryEx;
    }
    else if (pszSrvReqHandler)
    {
        pLoadReq->u.In.eEPType                = SUPLDRLOADEP_SERVICE;
        pLoadReq->u.In.EP.Service.pfnServiceReq  = (RTR0PTR)SrvReqHandler;
        pLoadReq->u.In.EP.Service.apvReserved[0] = NIL_RTR0PTR;
        pLoadReq->u.In.EP.Service.apvReserved[1] = NIL_RTR0PTR;
        pLoadReq->u.In.EP.Service.apvReserved[2] = NIL_RTR0PTR;
    }
    else
        pLoadReq->u.In.eEPType                = SUPLDRLOADEP_NOTHING;
    pLoadReq->u.In.offStrTab                  = offStrTab;
    pLoadReq->u.In.cbStrTab                   = (uint32_t)cbStrTab;
    AssertRelease(pLoadReq->u.In.cbStrTab == cbStrTab);
    pLoadReq->u.In.cbImageBits                = (uint32_t)cbImage;
    pLoadReq->u.In.offSymbols                 = offSymTab;
    pLoadReq->u.In.cSymbols                   = cSymbols;
    pLoadReq->u.In.offSegments                = offSegTab;
    pLoadReq->u.In.cSegments                  = cSegments;
    pLoadReq->u.In.cbImageWithEverything      = cbImageWithEverything;
    pLoadReq->u.In.pvImageBase                = uImageBase;
    pLoadReq->u.In.fFlags                     = Args.fLoadReq;
    if (!g_uSupFakeMode)
    {
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_LDR_LOAD, pLoadReq, SUP_IOCTL_LDR_LOAD_SIZE(cbImageWithEverything));
        if (RT_SUCCESS(rc))
            rc = pLoadReq->Hdr.rc;
        else
            LogRel(("SUP: SUP_IOCTL_LDR_LOAD ioctl for %s (%s) failed rc=%Rrc\n", pszModule, pszFilename, rc));
    }
    else
        rc = VINF_SUCCESS;
    if (    RT_SUCCESS(rc)
        ||  rc == VERR_ALREADY_LOADED /* A competing process. */
       )
    {
        LogRel(("SUP: Loaded %s (%s) at %#RKv - ModuleInit at %RKv and ModuleTerm at %RKv%s\n",
                pszModule, pszFilename, uImageBase, (RTR0PTR)ModuleInit, (RTR0PTR)ModuleTerm,
                fNativeLoader ? " using the native ring-0 loader" : ""));
        if (fIsVMMR0)
        {
            g_pvVMMR0 = uImageBase;
            LogRel(("SUP: VMMR0EntryEx located at %RKv and VMMR0EntryFast at %RKv\n", (RTR0PTR)VMMR0EntryEx, (RTR0PTR)VMMR0EntryFast));
        }
#ifdef RT_OS_WINDOWS
        LogRel(("SUP: windbg> .reload /f %s=%#RKv\n", pszFilename, uImageBase));
#endif
        return VINF_SUCCESS;
    }

    /*
     * Failed, bail out.
     */
    LogRel(("SUP: Loading failed for %s (%s) rc=%Rrc\n", pszModule, pszFilename, rc));
    if (   pLoadReq->u.Out.uErrorMagic == SUPLDRLOAD_ERROR_MAGIC
        && pLoadReq->u.Out.szError[0] != '\0')
    {
        LogRel(("SUP: %s\n", pLoadReq->u.Out.szError));
        return RTErrInfoSet(pErrInfo, rc, pLoadReq->u.Out.szError);
    }
    return RTErrInfoSet(pErrInfo, rc, "SUP_IOCTL_LDR_LOAD failed");
}


/**
 * Worker for SUPR3LoadModule().
 *
 * @returns VBox status code.
 * @param   pszFilename         Name of the VMMR0 image file
 * @param   pszModule           The modulen name.
 * @param   pszSrvReqHandler    The service request handler symbol name,
 *                              optional.
 * @param   pErrInfo            Where to store detailed error info. Optional.
 * @param   ppvImageBase        Where to return the load address.
 */
static int supLoadModule(const char *pszFilename, const char *pszModule, const char *pszSrvReqHandler,
                         PRTERRINFO pErrInfo, void **ppvImageBase)
{
    SUPLDROPEN OpenReq;

    /*
     * Validate input.
     */
    AssertPtrReturn(pszFilename, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszModule, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppvImageBase, VERR_INVALID_PARAMETER);
    AssertReturn(strlen(pszModule) < sizeof(OpenReq.u.In.szName), VERR_FILENAME_TOO_LONG);

    const bool fIsVMMR0 = !strcmp(pszModule, "VMMR0.r0");
    AssertReturn(!pszSrvReqHandler || !fIsVMMR0, VERR_INTERNAL_ERROR);
    *ppvImageBase = NULL;

    /*
     * First try open it w/o preparing a binary for loading.
     *
     * This will be a lot faster if it's already loaded, and it will
     * avoid fixup issues when using wrapped binaries.  With wrapped
     * ring-0 binaries not all binaries need to be wrapped, so trying
     * to load it ourselves is not a bug, but intentional behaviour
     * (even it it asserts in the loader code).
     */
    OpenReq.Hdr.u32Cookie              = g_u32Cookie;
    OpenReq.Hdr.u32SessionCookie       = g_u32SessionCookie;
    OpenReq.Hdr.cbIn                   = SUP_IOCTL_LDR_OPEN_SIZE_IN;
    OpenReq.Hdr.cbOut                  = SUP_IOCTL_LDR_OPEN_SIZE_OUT;
    OpenReq.Hdr.fFlags                 = SUPREQHDR_FLAGS_DEFAULT;
    OpenReq.Hdr.rc                     = VERR_INTERNAL_ERROR;
    OpenReq.u.In.cbImageWithEverything = 0;
    OpenReq.u.In.cbImageBits           = 0;
    strcpy(OpenReq.u.In.szName, pszModule);
    int rc = RTPathAbs(pszFilename, OpenReq.u.In.szFilename, sizeof(OpenReq.u.In.szFilename));
    if (RT_FAILURE(rc))
        return rc;
    if (   (SUPDRV_IOC_VERSION & 0xffff0000) != 0x00300000
        || g_uSupSessionVersion >= 0x00300001)
    {
        if (!g_uSupFakeMode)
        {
            rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_LDR_OPEN, &OpenReq, SUP_IOCTL_LDR_OPEN_SIZE);
            if (RT_SUCCESS(rc))
                rc = OpenReq.Hdr.rc;
        }
        else
        {
            OpenReq.u.Out.fNeedsLoading = true;
            OpenReq.u.Out.pvImageBase = 0xef423420;
        }
        *ppvImageBase = (void *)OpenReq.u.Out.pvImageBase;
        if (rc != VERR_MODULE_NOT_FOUND)
        {
            if (fIsVMMR0)
                g_pvVMMR0 = OpenReq.u.Out.pvImageBase;
            LogRel(("SUP: Opened %s (%s) at %#RKv%s.\n", pszModule, pszFilename, OpenReq.u.Out.pvImageBase,
                    OpenReq.u.Out.fNativeLoader ? " loaded by the native ring-0 loader" : ""));
#ifdef RT_OS_WINDOWS
            LogRel(("SUP: windbg> .reload /f %s=%#RKv\n", pszFilename, OpenReq.u.Out.pvImageBase));
#endif
            return rc;
        }
    }

    /*
     * Open image file and figure its size.
     */
    RTLDRMOD hLdrMod;
    rc = RTLdrOpenEx(OpenReq.u.In.szFilename, 0 /*fFlags*/, RTLDRARCH_HOST, &hLdrMod, pErrInfo);
    if (RT_FAILURE(rc))
    {
        LogRel(("SUP: RTLdrOpen failed for %s (%s) %Rrc\n", pszModule, OpenReq.u.In.szFilename, rc));
        return rc;
    }

    SUPLDRCALCSIZEARGS CalcArgs;
    CalcArgs.cbStrings = 0;
    CalcArgs.cSymbols = 0;
    CalcArgs.cbImage = RTLdrSize(hLdrMod);
    rc = RTLdrEnumSymbols(hLdrMod, 0, NULL, 0, supLoadModuleCalcSizeCB, &CalcArgs);
    if (RT_SUCCESS(rc))
    {
        /*
         * Figure out the number of segments needed first.
         */
        SUPLDRCOMPSEGTABARGS SegArgs;
        SegArgs.uStartRva   = 0;
        SegArgs.uEndRva     = 0;
        SegArgs.fProt       = RTMEM_PROT_READ;
        SegArgs.iSegs       = 0;
        SegArgs.cSegsAlloc  = 0;
        SegArgs.paSegs      = NULL;
        SegArgs.pErrInfo    = pErrInfo;
        rc = RTLdrEnumSegments(hLdrMod, supLoadModuleCompileSegmentsCB, &SegArgs);
        if (RT_SUCCESS(rc))
        {
            Assert(SegArgs.uEndRva <= RTLdrSize(hLdrMod));
            SegArgs.uEndRva = (uint32_t)CalcArgs.cbImage; /* overflow is checked later */
            if (SegArgs.uEndRva > SegArgs.uStartRva)
            {
                Log2(("supLoadModule:                  SUP Seg #%u: %#x LB %#x prot %#x\n",
                      SegArgs.iSegs, SegArgs.uStartRva, SegArgs.uEndRva - SegArgs.uStartRva, SegArgs.fProt));
                SegArgs.iSegs++;
            }

            const uint32_t offSymTab = RT_ALIGN_32(CalcArgs.cbImage, 8);
            const uint32_t offStrTab = offSymTab + CalcArgs.cSymbols * sizeof(SUPLDRSYM);
            const uint32_t offSegTab = RT_ALIGN_32(offStrTab + CalcArgs.cbStrings, 8);
            const uint32_t cbImageWithEverything = RT_ALIGN_32(offSegTab + sizeof(SUPLDRSEG) * SegArgs.iSegs, 8);

            /*
             * Open the R0 image.
             */
            OpenReq.Hdr.u32Cookie              = g_u32Cookie;
            OpenReq.Hdr.u32SessionCookie       = g_u32SessionCookie;
            OpenReq.Hdr.cbIn                   = SUP_IOCTL_LDR_OPEN_SIZE_IN;
            OpenReq.Hdr.cbOut                  = SUP_IOCTL_LDR_OPEN_SIZE_OUT;
            OpenReq.Hdr.fFlags                 = SUPREQHDR_FLAGS_DEFAULT;
            OpenReq.Hdr.rc                     = VERR_INTERNAL_ERROR;
            OpenReq.u.In.cbImageWithEverything = cbImageWithEverything;
            OpenReq.u.In.cbImageBits           = (uint32_t)CalcArgs.cbImage;
            strcpy(OpenReq.u.In.szName, pszModule);
            rc = RTPathAbs(pszFilename, OpenReq.u.In.szFilename, sizeof(OpenReq.u.In.szFilename));
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                if (!g_uSupFakeMode)
                {
                    rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_LDR_OPEN, &OpenReq, SUP_IOCTL_LDR_OPEN_SIZE);
                    if (RT_SUCCESS(rc))
                        rc = OpenReq.Hdr.rc;
                }
                else
                {
                    OpenReq.u.Out.fNeedsLoading = true;
                    OpenReq.u.Out.pvImageBase = 0xef423420;
                }
            }
            *ppvImageBase = (void *)OpenReq.u.Out.pvImageBase;
            if (   RT_SUCCESS(rc)
                && OpenReq.u.Out.fNeedsLoading)
            {
                /*
                 * We need to load it.
                 *
                 * Allocate the request and pass it to an inner work function
                 * that populates it and sends it off to the driver.
                 */
                const uint32_t cbLoadReq = SUP_IOCTL_LDR_LOAD_SIZE(cbImageWithEverything);
                PSUPLDRLOAD    pLoadReq  = (PSUPLDRLOAD)RTMemTmpAlloc(cbLoadReq);
                if (pLoadReq)
                {
                    rc = supLoadModuleInner(hLdrMod, pLoadReq, cbImageWithEverything, OpenReq.u.Out.pvImageBase, CalcArgs.cbImage,
                                            pszModule, pszFilename, OpenReq.u.Out.fNativeLoader, fIsVMMR0, pszSrvReqHandler,
                                            offSymTab, CalcArgs.cSymbols,
                                            offStrTab, CalcArgs.cbStrings,
                                            offSegTab, SegArgs.iSegs,
                                            pErrInfo);
                    RTMemTmpFree(pLoadReq);
                }
                else
                {
                    AssertMsgFailed(("failed to allocated %u bytes for SUPLDRLOAD_IN structure!\n", SUP_IOCTL_LDR_LOAD_SIZE(cbImageWithEverything)));
                    rc = RTErrInfoSetF(pErrInfo, VERR_NO_TMP_MEMORY, "Failed to allocate %u bytes for the load request",
                                       SUP_IOCTL_LDR_LOAD_SIZE(cbImageWithEverything));
                }
            }
            /*
             * Already loaded?
             */
            else if (RT_SUCCESS(rc))
            {
                if (fIsVMMR0)
                    g_pvVMMR0 = OpenReq.u.Out.pvImageBase;
                LogRel(("SUP: Opened %s (%s) at %#RKv%s.\n", pszModule, pszFilename, OpenReq.u.Out.pvImageBase,
                        OpenReq.u.Out.fNativeLoader ? " loaded by the native ring-0 loader" : ""));
#ifdef RT_OS_WINDOWS
                LogRel(("SUP: windbg> .reload /f %s=%#RKv\n", pszFilename, OpenReq.u.Out.pvImageBase));
#endif
            }
            /*
             * No, failed.
             */
            else
                RTErrInfoSet(pErrInfo, rc, "SUP_IOCTL_LDR_OPEN failed");
        }
        else if (!RTErrInfoIsSet(pErrInfo) && pErrInfo)
            RTErrInfoSetF(pErrInfo, rc, "RTLdrEnumSegments #1 failed");
    }
    else
        RTErrInfoSetF(pErrInfo, rc, "RTLdrEnumSymbols #1 failed");
    RTLdrClose(hLdrMod);
    return rc;
}


SUPR3DECL(int) SUPR3FreeModule(void *pvImageBase)
{
    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
    {
        g_pvVMMR0 = NIL_RTR0PTR;
        return VINF_SUCCESS;
    }

    /*
     * Free the requested module.
     */
    SUPLDRFREE Req;
    Req.Hdr.u32Cookie = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn = SUP_IOCTL_LDR_FREE_SIZE_IN;
    Req.Hdr.cbOut = SUP_IOCTL_LDR_FREE_SIZE_OUT;
    Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc = VERR_INTERNAL_ERROR;
    Req.u.In.pvImageBase = (RTR0PTR)pvImageBase;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_LDR_FREE, &Req, SUP_IOCTL_LDR_FREE_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    if (    RT_SUCCESS(rc)
        &&  (RTR0PTR)pvImageBase == g_pvVMMR0)
        g_pvVMMR0 = NIL_RTR0PTR;
    return rc;
}


SUPR3DECL(int) SUPR3GetSymbolR0(void *pvImageBase, const char *pszSymbol, void **ppvValue)
{
    *ppvValue = NULL;

    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
    {
        *ppvValue = (void *)(uintptr_t)0xdeadf00d;
        return VINF_SUCCESS;
    }

    /*
     * Do ioctl.
     */
    SUPLDRGETSYMBOL Req;
    Req.Hdr.u32Cookie = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn = SUP_IOCTL_LDR_GET_SYMBOL_SIZE_IN;
    Req.Hdr.cbOut = SUP_IOCTL_LDR_GET_SYMBOL_SIZE_OUT;
    Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc = VERR_INTERNAL_ERROR;
    Req.u.In.pvImageBase = (RTR0PTR)pvImageBase;
    size_t cchSymbol = strlen(pszSymbol);
    if (cchSymbol >= sizeof(Req.u.In.szSymbol))
        return VERR_SYMBOL_NOT_FOUND;
    memcpy(Req.u.In.szSymbol, pszSymbol, cchSymbol + 1);
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_LDR_GET_SYMBOL, &Req, SUP_IOCTL_LDR_GET_SYMBOL_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    if (RT_SUCCESS(rc))
        *ppvValue = (void *)Req.u.Out.pvSymbol;
    return rc;
}


SUPR3DECL(int) SUPR3LoadVMM(const char *pszFilename, PRTERRINFO pErrInfo)
{
    void *pvImageBase;
    return SUPR3LoadModule(pszFilename, "VMMR0.r0", &pvImageBase, pErrInfo);
}


SUPR3DECL(int) SUPR3UnloadVMM(void)
{
    return SUPR3FreeModule((void*)g_pvVMMR0);
}


/**
 * Worker for SUPR3HardenedLdrLoad and SUPR3HardenedLdrLoadAppPriv.
 *
 * @returns iprt status code.
 * @param   pszFilename     The full file name.
 * @param   phLdrMod        Where to store the handle to the loaded module.
 * @param   fFlags          See RTLDFLAGS_.
 * @param   pErrInfo        Where to return extended error information.
 *                          Optional.
 *
 */
static int supR3HardenedLdrLoadIt(const char *pszFilename, PRTLDRMOD phLdrMod, uint32_t fFlags, PRTERRINFO pErrInfo)
{
#ifdef VBOX_WITH_HARDENING
    /*
     * Verify the image file.
     */
    int rc = SUPR3HardenedVerifyInit();
    if (RT_FAILURE(rc))
        rc = supR3HardenedVerifyFixedFile(pszFilename, false /* fFatal */);
    if (RT_FAILURE(rc))
    {
        LogRel(("supR3HardenedLdrLoadIt: Verification of \"%s\" failed, rc=%Rrc\n", pszFilename, rc));
        return RTErrInfoSet(pErrInfo, rc, "supR3HardenedVerifyFixedFile failed");
    }
#endif

    /*
     * Try load it.
     */
    return RTLdrLoadEx(pszFilename, phLdrMod, fFlags, pErrInfo);
}


SUPR3DECL(int) SUPR3HardenedLdrLoad(const char *pszFilename, PRTLDRMOD phLdrMod, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    /*
     * Validate input.
     */
    RTErrInfoClear(pErrInfo);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertPtrReturn(phLdrMod, VERR_INVALID_POINTER);
    *phLdrMod = NIL_RTLDRMOD;
    AssertReturn(RTPathHavePath(pszFilename), VERR_INVALID_PARAMETER);

    /*
     * Add the default extension if it's missing.
     */
    if (!RTPathHasSuffix(pszFilename))
    {
        const char *pszSuff = RTLdrGetSuff();
        size_t      cchSuff = strlen(pszSuff);
        size_t      cchFilename = strlen(pszFilename);
        char       *psz = (char *)alloca(cchFilename + cchSuff + 1);
        AssertReturn(psz, VERR_NO_TMP_MEMORY);
        memcpy(psz, pszFilename, cchFilename);
        memcpy(psz + cchFilename, pszSuff, cchSuff + 1);
        pszFilename = psz;
    }

    /*
     * Pass it on to the common library loader.
     */
    return supR3HardenedLdrLoadIt(pszFilename, phLdrMod, fFlags, pErrInfo);
}


SUPR3DECL(int) SUPR3HardenedLdrLoadAppPriv(const char *pszFilename, PRTLDRMOD phLdrMod, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    LogFlow(("SUPR3HardenedLdrLoadAppPriv: pszFilename=%p:{%s} phLdrMod=%p fFlags=%08x pErrInfo=%p\n", pszFilename, pszFilename, phLdrMod, fFlags, pErrInfo));

    /*
     * Validate input.
     */
    RTErrInfoClear(pErrInfo);
    AssertPtrReturn(phLdrMod, VERR_INVALID_PARAMETER);
    *phLdrMod = NIL_RTLDRMOD;
    AssertPtrReturn(pszFilename, VERR_INVALID_PARAMETER);
    AssertMsgReturn(!RTPathHavePath(pszFilename), ("%s\n", pszFilename), VERR_INVALID_PARAMETER);

    /*
     * Check the filename.
     */
    size_t cchFilename = strlen(pszFilename);
    AssertMsgReturn(cchFilename < (RTPATH_MAX / 4) * 3, ("%zu\n", cchFilename), VERR_INVALID_PARAMETER);

    const char *pszExt = "";
    size_t cchExt = 0;
    if (!RTPathHasSuffix(pszFilename))
    {
        pszExt = RTLdrGetSuff();
        cchExt = strlen(pszExt);
    }

    /*
     * Construct the private arch path and check if the file exists.
     */
    char szPath[RTPATH_MAX];
    int rc = RTPathAppPrivateArch(szPath, sizeof(szPath) - 1 - cchExt - cchFilename);
    AssertRCReturn(rc, rc);

    char *psz = strchr(szPath, '\0');
    *psz++ = RTPATH_SLASH;
    memcpy(psz, pszFilename, cchFilename);
    psz += cchFilename;
    memcpy(psz, pszExt, cchExt + 1);

    if (!RTPathExists(szPath))
    {
        LogRel(("SUPR3HardenedLdrLoadAppPriv: \"%s\" not found\n", szPath));
        return VERR_FILE_NOT_FOUND;
    }

    /*
     * Pass it on to SUPR3HardenedLdrLoad.
     */
    rc = SUPR3HardenedLdrLoad(szPath, phLdrMod, fFlags, pErrInfo);

    LogFlow(("SUPR3HardenedLdrLoadAppPriv: returns %Rrc\n", rc));
    return rc;
}


SUPR3DECL(int) SUPR3HardenedLdrLoadPlugIn(const char *pszFilename, PRTLDRMOD phLdrMod, PRTERRINFO pErrInfo)
{
    /*
     * Validate input.
     */
    RTErrInfoClear(pErrInfo);
    AssertPtrReturn(phLdrMod, VERR_INVALID_PARAMETER);
    *phLdrMod = NIL_RTLDRMOD;
    AssertPtrReturn(pszFilename, VERR_INVALID_PARAMETER);
    AssertReturn(RTPathStartsWithRoot(pszFilename), VERR_INVALID_PARAMETER);

#ifdef VBOX_WITH_HARDENING
    /*
     * Verify the image file.
     */
    int rc = supR3HardenedVerifyFile(pszFilename, RTHCUINTPTR_MAX, true /*fMaybe3rdParty*/, pErrInfo);
    if (RT_FAILURE(rc))
    {
        if (!RTErrInfoIsSet(pErrInfo))
            LogRel(("supR3HardenedVerifyFile: Verification of \"%s\" failed, rc=%Rrc\n", pszFilename, rc));
        return rc;
    }
#endif

    /*
     * Try load it.
     */
    return RTLdrLoadEx(pszFilename, phLdrMod, RTLDRLOAD_FLAGS_LOCAL, pErrInfo);
}

