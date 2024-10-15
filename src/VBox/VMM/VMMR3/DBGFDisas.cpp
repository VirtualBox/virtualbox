/* $Id: DBGFDisas.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Disassembler.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/cpum.h>
#include "DBGFInternal.h"
#include <VBox/dis.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/alloca.h>
#include <iprt/ctype.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Structure used when disassembling and instructions in DBGF.
 * This is used so the reader function can get the stuff it needs.
 */
typedef struct
{
    /** The core structure. */
    DISSTATE        Dis;
    /** The cross context VM structure. */
    PVM             pVM;
    /** The cross context virtual CPU structure. */
    PVMCPU          pVCpu;
    /** The address space for resolving symbol. */
    RTDBGAS         hDbgAs;
#if !defined(VBOX_VMM_TARGET_ARMV8)
    /** Pointer to the first byte in the segment. */
    RTGCUINTPTR     GCPtrSegBase;
    /** Pointer to the byte after the end of the segment. (might have wrapped!) */
    RTGCUINTPTR     GCPtrSegEnd;
    /** The size of the segment minus 1. */
    RTGCUINTPTR     cbSegLimit;
#endif
    /** The guest paging mode. */
    PGMMODE         enmMode;
    /** Pointer to the current page - R3 Ptr. */
    void const     *pvPageR3;
    /** Pointer to the current page - GC Ptr. */
    RTGCPTR         GCPtrPage;
    /** Pointer to the next instruction (relative to GCPtrSegBase). */
    RTGCUINTPTR     GCPtrNext;
    /** The lock information that PGMPhysReleasePageMappingLock needs. */
    PGMPAGEMAPLOCK  PageMapLock;
    /** Whether the PageMapLock is valid or not. */
    bool            fLocked;
    /** 64 bits mode or not. */
    bool            f64Bits;
} DBGFDISASSTATE, *PDBGFDISASSTATE;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNDISREADBYTES dbgfR3DisasInstrRead;



/**
 * Calls the disassembler with the proper reader functions and such for disa
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pSelInfo    The selector info.
 * @param   enmMode     The guest paging mode.
 * @param   fFlags      DBGF_DISAS_FLAGS_XXX.
 * @param   GCPtr       The GC pointer (selector offset).
 * @param   pState      The disas CPU state.
 */
static int dbgfR3DisasInstrFirst(PVM pVM, PVMCPU pVCpu, PDBGFSELINFO pSelInfo, PGMMODE enmMode,
                                 RTGCPTR GCPtr, uint32_t fFlags, PDBGFDISASSTATE pState)
{
#if !defined(VBOX_VMM_TARGET_ARMV8)
    pState->GCPtrSegBase    = pSelInfo->GCPtrBase;
    pState->GCPtrSegEnd     = pSelInfo->cbLimit + 1 + (RTGCUINTPTR)pSelInfo->GCPtrBase;
    pState->cbSegLimit      = pSelInfo->cbLimit;
    pState->f64Bits         = enmMode >= PGMMODE_AMD64 && pSelInfo->u.Raw.Gen.u1Long;
#else
    RT_NOREF(pSelInfo);

    pState->f64Bits         = CPUMGetGuestCodeBits(pVCpu) == 64;
#endif
    pState->enmMode         = enmMode;
    pState->GCPtrPage       = 0;
    pState->pvPageR3        = NULL;
    pState->hDbgAs          = DBGF_AS_GLOBAL;
    pState->pVM             = pVM;
    pState->pVCpu           = pVCpu;
    pState->fLocked         = false;

    DISCPUMODE enmCpuMode;
    switch (fFlags & DBGF_DISAS_FLAGS_MODE_MASK)
    {
        default:
            AssertFailed();
            RT_FALL_THRU();
        case DBGF_DISAS_FLAGS_DEFAULT_MODE:
            enmCpuMode = CPUMGetGuestDisMode(pVCpu);
            break;
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case DBGF_DISAS_FLAGS_16BIT_MODE:
        case DBGF_DISAS_FLAGS_16BIT_REAL_MODE:
            enmCpuMode = DISCPUMODE_16BIT;
            break;
        case DBGF_DISAS_FLAGS_32BIT_MODE:
            enmCpuMode = DISCPUMODE_32BIT;
            break;
        case DBGF_DISAS_FLAGS_64BIT_MODE:
            enmCpuMode = DISCPUMODE_64BIT;
            break;
#else
        case DBGF_DISAS_FLAGS_16BIT_MODE: /** @todo r=aeichner This is a bit abusive... */
        case DBGF_DISAS_FLAGS_16BIT_REAL_MODE:
            enmCpuMode = DISCPUMODE_ARMV8_T32;
            break;
        case DBGF_DISAS_FLAGS_32BIT_MODE:
            enmCpuMode = DISCPUMODE_ARMV8_A32;
            break;
        case DBGF_DISAS_FLAGS_64BIT_MODE:
            enmCpuMode = DISCPUMODE_ARMV8_A64;
            break;
#endif
    }

    uint32_t cbInstr;
    int rc = DISInstrWithReader(GCPtr,
                                enmCpuMode,
                                dbgfR3DisasInstrRead,
                                &pState->Dis,
                                &pState->Dis,
                                &cbInstr);
    if (RT_SUCCESS(rc))
    {
        pState->GCPtrNext = GCPtr + cbInstr;
        return VINF_SUCCESS;
    }

    /* cleanup */
    if (pState->fLocked)
    {
        PGMPhysReleasePageMappingLock(pVM, &pState->PageMapLock);
        pState->fLocked = false;
    }
    return rc;
}


#if 0
/**
 * Calls the disassembler for disassembling the next instruction.
 *
 * @returns VBox status code.
 * @param   pState      The disas CPU state.
 */
static int dbgfR3DisasInstrNext(PDBGFDISASSTATE pState)
{
    uint32_t cbInstr;
    int rc = DISInstr(&pState->Cpu, (void *)pState->GCPtrNext, 0, &cbInstr, NULL);
    if (RT_SUCCESS(rc))
    {
        pState->GCPtrNext = GCPtr + cbInstr;
        return VINF_SUCCESS;
    }
    return rc;
}
#endif


/**
 * Done with the disassembler state, free associated resources.
 *
 * @param   pState      The disas CPU state ++.
 */
static void dbgfR3DisasInstrDone(PDBGFDISASSTATE pState)
{
    if (pState->fLocked)
    {
        PGMPhysReleasePageMappingLock(pState->pVM, &pState->PageMapLock);
        pState->fLocked = false;
    }
}


/**
 * @callback_method_impl{FNDISREADBYTES}
 *
 * @remarks The source is relative to the base address indicated by
 *          DBGFDISASSTATE::GCPtrSegBase.
 */
static DECLCALLBACK(int) dbgfR3DisasInstrRead(PDISSTATE pDis, uint8_t offInstr, uint8_t cbMinRead, uint8_t cbMaxRead)
{
    PDBGFDISASSTATE pState = (PDBGFDISASSTATE)pDis;
    for (;;)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        RTGCUINTPTR GCPtr = pDis->uInstrAddr + offInstr + pState->GCPtrSegBase;
#else
        RTGCUINTPTR GCPtr = pDis->uInstrAddr + offInstr;
#endif

        /*
         * Need to update the page translation?
         */
        if (    !pState->pvPageR3
            ||  (GCPtr >> GUEST_PAGE_SHIFT) != (pState->GCPtrPage >> GUEST_PAGE_SHIFT))
        {
            int rc = VINF_SUCCESS;

            /* translate the address */
            pState->GCPtrPage = GCPtr & ~(RTGCPTR)GUEST_PAGE_OFFSET_MASK;
            if (pState->fLocked)
                PGMPhysReleasePageMappingLock(pState->pVM, &pState->PageMapLock);
            if (PGMMODE_WITH_PAGING(pState->enmMode))
                rc = PGMPhysGCPtr2CCPtrReadOnly(pState->pVCpu, pState->GCPtrPage, &pState->pvPageR3, &pState->PageMapLock);
            else
                rc = PGMPhysGCPhys2CCPtrReadOnly(pState->pVM, pState->GCPtrPage, &pState->pvPageR3, &pState->PageMapLock);
            if (RT_SUCCESS(rc))
                pState->fLocked = true;
            else
            {
                pState->fLocked  = false;
                pState->pvPageR3 = NULL;
                return rc;
            }
        }

        uint32_t cb = GUEST_PAGE_SIZE - (GCPtr & GUEST_PAGE_OFFSET_MASK);
#if !defined(VBOX_VMM_TARGET_ARMV8)
        /*
         * Check the segment limit.
         */
        if (!pState->f64Bits && pDis->uInstrAddr + offInstr > pState->cbSegLimit)
            return VERR_OUT_OF_SELECTOR_BOUNDS;

        /*
         * Calc how much we can read, maxing out the read.
         */
        if (!pState->f64Bits)
        {
            RTGCUINTPTR cbSeg = pState->GCPtrSegEnd - GCPtr;
            if (cb > cbSeg && cbSeg)
                cb = cbSeg;
        }
#endif
        if (cb > cbMaxRead)
            cb = cbMaxRead;

        /*
         * Read and advance,
         */
        memcpy(&pDis->Instr.ab[offInstr], (char *)pState->pvPageR3 + (GCPtr & GUEST_PAGE_OFFSET_MASK), cb);
        offInstr  += (uint8_t)cb;
        if (cb >= cbMinRead)
        {
            pDis->cbCachedInstr = offInstr;
            return VINF_SUCCESS;
        }
        cbMaxRead -= (uint8_t)cb;
        cbMinRead -= (uint8_t)cb;
    }
}


/**
 * @callback_method_impl{FNDISGETSYMBOL}
 */
static DECLCALLBACK(int) dbgfR3DisasGetSymbol(PCDISSTATE pDis, uint32_t u32Sel, RTUINTPTR uAddress,
                                              char *pszBuf, size_t cchBuf, RTINTPTR *poff, void *pvUser)
{
    PDBGFDISASSTATE pState   = (PDBGFDISASSTATE)pDis;
    PCDBGFSELINFO   pSelInfo = (PCDBGFSELINFO)pvUser;

    /*
     * Address conversion
     */
    DBGFADDRESS     Addr;
    int             rc;
#if !defined(VBOX_VMM_TARGET_ARMV8)
    /* Start with CS. */
    if (   DIS_FMT_SEL_IS_REG(u32Sel)
        ?  DIS_FMT_SEL_GET_REG(u32Sel) == DISSELREG_CS
        :  pSelInfo->Sel == DIS_FMT_SEL_GET_VALUE(u32Sel))
        rc = DBGFR3AddrFromSelInfoOff(pState->pVM->pUVM, &Addr, pSelInfo, uAddress);
    /* In long mode everything but FS and GS is easy. */
    else if (   pState->Dis.uCpuMode == DISCPUMODE_64BIT
             && DIS_FMT_SEL_IS_REG(u32Sel)
             && DIS_FMT_SEL_GET_REG(u32Sel) != DISSELREG_GS
             && DIS_FMT_SEL_GET_REG(u32Sel) != DISSELREG_FS)
    {
        DBGFR3AddrFromFlat(pState->pVM->pUVM, &Addr, uAddress);
        rc = VINF_SUCCESS;
    }
    /* Here's a quick hack to catch patch manager SS relative access. */
    else if (   DIS_FMT_SEL_IS_REG(u32Sel)
             && DIS_FMT_SEL_GET_REG(u32Sel) == DISSELREG_SS
             && pSelInfo->GCPtrBase == 0
             && pSelInfo->cbLimit   >= UINT32_MAX)
    {
        DBGFR3AddrFromFlat(pState->pVM->pUVM, &Addr, uAddress);
        rc = VINF_SUCCESS;
    }
    else
    {
        /** @todo implement a generic solution here. */
        rc = VERR_SYMBOL_NOT_FOUND;
    }
#else
    RT_NOREF(pSelInfo, u32Sel);

    DBGFR3AddrFromFlat(pState->pVM->pUVM, &Addr, uAddress);
    rc = VINF_SUCCESS;
#endif

    /*
     * If we got an address, try resolve it into a symbol.
     */
    if (RT_SUCCESS(rc))
    {
        RTDBGSYMBOL     Sym;
        RTGCINTPTR      off;
        rc = DBGFR3AsSymbolByAddr(pState->pVM->pUVM, pState->hDbgAs, &Addr,
                                  RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                  &off, &Sym, NULL /*phMod*/);
        if (RT_SUCCESS(rc))
        {
            /*
             * Return the symbol and offset.
             */
            size_t cchName = strlen(Sym.szName);
            if (cchName >= cchBuf)
                cchName = cchBuf - 1;
            memcpy(pszBuf, Sym.szName, cchName);
            pszBuf[cchName] = '\0';

            *poff = off;
        }
    }
    return rc;
}


/**
 * Disassembles the one instruction according to the specified flags and
 * address, internal worker executing on the EMT of the specified virtual CPU.
 *
 * @returns VBox status code.
 * @param       pVM             The cross context VM structure.
 * @param       pVCpu           The cross context virtual CPU structure.
 * @param       Sel             The code selector. This used to determine the 32/16 bit ness and
 *                              calculation of the actual instruction address.
 * @param       pGCPtr          Pointer to the variable holding the code address
 *                              relative to the base of Sel.
 * @param       fFlags          Flags controlling where to start and how to format.
 *                              A combination of the DBGF_DISAS_FLAGS_* \#defines.
 * @param       pszOutput       Output buffer.
 * @param       cbOutput        Size of the output buffer.
 * @param       pcbInstr        Where to return the size of the instruction.
 * @param       pDisState       Where to store the disassembler state into.
 */
static DECLCALLBACK(int)
dbgfR3DisasInstrExOnVCpu(PVM pVM, PVMCPU pVCpu, RTSEL Sel, PRTGCPTR pGCPtr, uint32_t fFlags,
                         char *pszOutput, uint32_t cbOutput, uint32_t *pcbInstr, PDBGFDISSTATE pDisState)
{
    VMCPU_ASSERT_EMT(pVCpu);
    RTGCPTR GCPtr = *pGCPtr;
    int     rc;

#if defined(VBOX_VMM_TARGET_ARMV8)
    DBGFSELINFO     SelInfo; RT_ZERO(SelInfo);
    const PGMMODE   enmMode = PGMGetGuestMode(pVCpu);
    const bool      fRealModeAddress = false;
    if (fFlags & DBGF_DISAS_FLAGS_CURRENT_GUEST)
        GCPtr = CPUMGetGuestFlatPC(pVCpu);
#else
    /*
     * Get the Sel and GCPtr if fFlags requests that.
     */
    PCCPUMCTX      pCtx    = CPUMQueryGuestCtxPtr(pVCpu);
    PCCPUMSELREG   pSRegCS = NULL;
    if (fFlags & DBGF_DISAS_FLAGS_CURRENT_GUEST)
    {
        Sel        = pCtx->cs.Sel;
        pSRegCS    = &pCtx->cs;
        GCPtr      = pCtx->rip;
    }
    /*
     * Check if the selector matches the guest CS, use the hidden
     * registers from that if they are valid. Saves time and effort.
     */
    else
    {
        if (pCtx->cs.Sel == Sel && Sel != DBGF_SEL_FLAT)
            pSRegCS = &pCtx->cs;
        else
            pCtx = NULL;
    }

    /*
     * Read the selector info - assume no stale selectors and nasty stuff like that.
     *
     * Note! We CANNOT load invalid hidden selector registers since that would
     *       mean that log/debug statements or the debug will influence the
     *       guest state and make things behave differently.
     */
    DBGFSELINFO     SelInfo;
    const PGMMODE   enmMode          = PGMGetGuestMode(pVCpu);
    bool            fRealModeAddress = false;

    if (   pSRegCS
        && CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, pSRegCS))
    {
        SelInfo.Sel                     = Sel;
        SelInfo.SelGate                 = 0;
        SelInfo.GCPtrBase               = pSRegCS->u64Base;
        SelInfo.cbLimit                 = pSRegCS->u32Limit;
        SelInfo.fFlags                  = PGMMODE_IS_64BIT_MODE(enmMode)
                                        ? DBGFSELINFO_FLAGS_LONG_MODE
                                        : enmMode != PGMMODE_REAL && !pCtx->eflags.Bits.u1VM
                                        ? DBGFSELINFO_FLAGS_PROT_MODE
                                        : DBGFSELINFO_FLAGS_REAL_MODE;

        SelInfo.u.Raw.au32[0]           = 0;
        SelInfo.u.Raw.au32[1]           = 0;
        SelInfo.u.Raw.Gen.u16LimitLow   = 0xffff;
        SelInfo.u.Raw.Gen.u4LimitHigh   = 0xf;
        SelInfo.u.Raw.Gen.u1Present     = pSRegCS->Attr.n.u1Present;
        SelInfo.u.Raw.Gen.u1Granularity = pSRegCS->Attr.n.u1Granularity;;
        SelInfo.u.Raw.Gen.u1DefBig      = pSRegCS->Attr.n.u1DefBig;
        SelInfo.u.Raw.Gen.u1Long        = pSRegCS->Attr.n.u1Long;
        SelInfo.u.Raw.Gen.u1DescType    = pSRegCS->Attr.n.u1DescType;
        SelInfo.u.Raw.Gen.u4Type        = pSRegCS->Attr.n.u4Type;
        fRealModeAddress                = !!(SelInfo.fFlags & DBGFSELINFO_FLAGS_REAL_MODE);
    }
    else if (Sel == DBGF_SEL_FLAT)
    {
        SelInfo.Sel                     = Sel;
        SelInfo.SelGate                 = 0;
        SelInfo.GCPtrBase               = 0;
        SelInfo.cbLimit                 = ~(RTGCUINTPTR)0;
        SelInfo.fFlags                  = PGMMODE_IS_64BIT_MODE(enmMode)
                                        ? DBGFSELINFO_FLAGS_LONG_MODE
                                        : enmMode != PGMMODE_REAL
                                        ? DBGFSELINFO_FLAGS_PROT_MODE
                                        : DBGFSELINFO_FLAGS_REAL_MODE;
        SelInfo.u.Raw.au32[0]           = 0;
        SelInfo.u.Raw.au32[1]           = 0;
        SelInfo.u.Raw.Gen.u16LimitLow   = 0xffff;
        SelInfo.u.Raw.Gen.u4LimitHigh   = 0xf;

        pSRegCS = &CPUMQueryGuestCtxPtr(pVCpu)->cs;
        if (CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, pSRegCS))
        {
            /* Assume the current CS defines the execution mode. */
            SelInfo.u.Raw.Gen.u1Present     = pSRegCS->Attr.n.u1Present;
            SelInfo.u.Raw.Gen.u1Granularity = pSRegCS->Attr.n.u1Granularity;;
            SelInfo.u.Raw.Gen.u1DefBig      = pSRegCS->Attr.n.u1DefBig;
            SelInfo.u.Raw.Gen.u1Long        = pSRegCS->Attr.n.u1Long;
            SelInfo.u.Raw.Gen.u1DescType    = pSRegCS->Attr.n.u1DescType;
            SelInfo.u.Raw.Gen.u4Type        = pSRegCS->Attr.n.u4Type;
        }
        else
        {
            pSRegCS  = NULL;
            SelInfo.u.Raw.Gen.u1Present     = 1;
            SelInfo.u.Raw.Gen.u1Granularity = 1;
            SelInfo.u.Raw.Gen.u1DefBig      = 1;
            SelInfo.u.Raw.Gen.u1DescType    = 1;
            SelInfo.u.Raw.Gen.u4Type        = X86_SEL_TYPE_EO;
        }
    }
    else if (   (pCtx && pCtx->eflags.Bits.u1VM)
             || enmMode == PGMMODE_REAL
             || (fFlags & DBGF_DISAS_FLAGS_MODE_MASK) == DBGF_DISAS_FLAGS_16BIT_REAL_MODE)
    {   /* V86 mode or real mode - real mode addressing */
        SelInfo.Sel                     = Sel;
        SelInfo.SelGate                 = 0;
        SelInfo.GCPtrBase               = Sel * 16;
        SelInfo.cbLimit                 = ~(RTGCUINTPTR)0;
        SelInfo.fFlags                  = DBGFSELINFO_FLAGS_REAL_MODE;
        SelInfo.u.Raw.au32[0]           = 0;
        SelInfo.u.Raw.au32[1]           = 0;
        SelInfo.u.Raw.Gen.u16LimitLow   = 0xffff;
        SelInfo.u.Raw.Gen.u4LimitHigh   = 0xf;
        SelInfo.u.Raw.Gen.u1Present     = 1;
        SelInfo.u.Raw.Gen.u1Granularity = 1;
        SelInfo.u.Raw.Gen.u1DefBig      = 0; /* 16 bits */
        SelInfo.u.Raw.Gen.u1DescType    = 1;
        SelInfo.u.Raw.Gen.u4Type        = X86_SEL_TYPE_EO;
        fRealModeAddress                = true;
    }
    else
    {
        rc = SELMR3GetSelectorInfo(pVCpu, Sel, &SelInfo);
        if (RT_FAILURE(rc))
        {
            RTStrPrintf(pszOutput, cbOutput, "Sel=%04x -> %Rrc\n", Sel, rc);
            return rc;
        }
    }
#endif

    /*
     * Disassemble it.
     */
    DBGFDISASSTATE State;
    rc = dbgfR3DisasInstrFirst(pVM, pVCpu, &SelInfo, enmMode, GCPtr, fFlags, &State);
    if (RT_FAILURE(rc))
    {
        if (State.Dis.cbCachedInstr)
            RTStrPrintf(pszOutput, cbOutput, "Disas -> %Rrc; %.*Rhxs\n", rc, (size_t)State.Dis.cbCachedInstr, State.Dis.Instr.ab);
        else
            RTStrPrintf(pszOutput, cbOutput, "Disas -> %Rrc\n", rc);
        return rc;
    }

    /*
     * Format it.
     */
    char szBuf[512];
#if defined(VBOX_VMM_TARGET_ARMV8)
    DISFormatArmV8Ex(&State.Dis, szBuf, sizeof(szBuf),
                     DIS_FMT_FLAGS_RELATIVE_BRANCH,
                     fFlags & DBGF_DISAS_FLAGS_NO_SYMBOLS ? NULL : dbgfR3DisasGetSymbol,
                     NULL);
#else
    DISFormatYasmEx(&State.Dis, szBuf, sizeof(szBuf),
                    DIS_FMT_FLAGS_RELATIVE_BRANCH,
                    fFlags & DBGF_DISAS_FLAGS_NO_SYMBOLS ? NULL : dbgfR3DisasGetSymbol,
                    &SelInfo);
#endif

    /*
     * Print it to the user specified buffer.
     */
    size_t cch;
    if (fFlags & DBGF_DISAS_FLAGS_NO_BYTES)
    {
        if (fFlags & DBGF_DISAS_FLAGS_NO_ADDRESS)
            cch = RTStrPrintf(pszOutput, cbOutput, "%s", szBuf);
        else if (fRealModeAddress)
            cch = RTStrPrintf(pszOutput, cbOutput, "%04x:%04x  %s", Sel, (unsigned)GCPtr, szBuf);
        else if (Sel == DBGF_SEL_FLAT)
        {
            if (PGMMODE_IS_64BIT_MODE(enmMode))
                cch = RTStrPrintf(pszOutput, cbOutput, "%RGv  %s", GCPtr, szBuf);
            else
                cch = RTStrPrintf(pszOutput, cbOutput, "%08RX32  %s", (uint32_t)GCPtr, szBuf);
        }
        else
        {
            if (PGMMODE_IS_64BIT_MODE(enmMode))
                cch = RTStrPrintf(pszOutput, cbOutput, "%04x:%RGv  %s", Sel, GCPtr, szBuf);
            else
                cch = RTStrPrintf(pszOutput, cbOutput, "%04x:%08RX32  %s", Sel, (uint32_t)GCPtr, szBuf);
        }
    }
    else
    {
        uint32_t        cbInstr  = State.Dis.cbInstr;
        uint8_t const  *pabInstr = State.Dis.Instr.ab;
        if (fFlags & DBGF_DISAS_FLAGS_NO_ADDRESS)
            cch = RTStrPrintf(pszOutput, cbOutput, "%.*Rhxs%*s %s",
                              cbInstr, pabInstr, cbInstr < 8 ? (8 - cbInstr) * 3 : 0, "",
                              szBuf);
        else if (fRealModeAddress)
            cch = RTStrPrintf(pszOutput, cbOutput, "%04x:%04x %.*Rhxs%*s %s",
                              Sel, (unsigned)GCPtr,
                              cbInstr, pabInstr, cbInstr < 8 ? (8 - cbInstr) * 3 : 0, "",
                              szBuf);
        else if (Sel == DBGF_SEL_FLAT)
        {
            if (PGMMODE_IS_64BIT_MODE(enmMode))
                cch = RTStrPrintf(pszOutput, cbOutput, "%RGv %.*Rhxs%*s %s",
                                  GCPtr,
                                  cbInstr, pabInstr, cbInstr < 8 ? (8 - cbInstr) * 3 : 0, "",
                                  szBuf);
            else
                cch = RTStrPrintf(pszOutput, cbOutput, "%08RX32 %.*Rhxs%*s %s",
                                  (uint32_t)GCPtr,
                                  cbInstr, pabInstr, cbInstr < 8 ? (8 - cbInstr) * 3 : 0, "",
                                  szBuf);
        }
        else
        {
            if (PGMMODE_IS_64BIT_MODE(enmMode))
                cch = RTStrPrintf(pszOutput, cbOutput, "%04x:%RGv %.*Rhxs%*s %s",
                                  Sel, GCPtr,
                                  cbInstr, pabInstr, cbInstr < 8 ? (8 - cbInstr) * 3 : 0, "",
                                  szBuf);
            else
                cch = RTStrPrintf(pszOutput, cbOutput, "%04x:%08RX32 %.*Rhxs%*s %s",
                                  Sel, (uint32_t)GCPtr,
                                  cbInstr, pabInstr, cbInstr < 8 ? (8 - cbInstr) * 3 : 0, "",
                                  szBuf);
        }
    }

    if (pcbInstr)
        *pcbInstr = State.Dis.cbInstr;

    if (pDisState)
    {
        pDisState->pCurInstr = State.Dis.pCurInstr;
        pDisState->cbInstr   = State.Dis.cbInstr;
        pDisState->Param1    = State.Dis.aParams[0];
        pDisState->Param2    = State.Dis.aParams[1];
        pDisState->Param3    = State.Dis.aParams[2];
        pDisState->Param4    = State.Dis.aParams[3];
    }

    dbgfR3DisasInstrDone(&State);
    return VINF_SUCCESS;
}


/**
 * Disassembles the one instruction according to the specified flags and address
 * returning part of the disassembler state.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   idCpu           The ID of virtual CPU.
 * @param   pAddr           The code address.
 * @param   fFlags          Flags controlling where to start and how to format.
 *                          A combination of the DBGF_DISAS_FLAGS_* \#defines.
 * @param   pszOutput       Output buffer.  This will always be properly
 *                          terminated if @a cbOutput is greater than zero.
 * @param   cbOutput        Size of the output buffer.
 * @param   pDisState       The disassembler state to fill in.
 *
 * @remarks May have to switch to the EMT of the virtual CPU in order to do
 *          address conversion.
 */
DECLHIDDEN(int) dbgfR3DisasInstrStateEx(PUVM pUVM, VMCPUID idCpu, PDBGFADDRESS pAddr, uint32_t fFlags,
                                        char *pszOutput, uint32_t cbOutput, PDBGFDISSTATE pDisState)
{
    AssertReturn(cbOutput > 0, VERR_INVALID_PARAMETER);
    *pszOutput = '\0';
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);
    AssertReturn(!(fFlags & ~DBGF_DISAS_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn((fFlags & DBGF_DISAS_FLAGS_MODE_MASK) <= DBGF_DISAS_FLAGS_64BIT_MODE, VERR_INVALID_PARAMETER);

    /*
     * Optimize the common case where we're called on the EMT of idCpu since
     * we're using this all the time when logging.
     */
    int     rc;
    PVMCPU  pVCpu = VMMGetCpu(pVM);
    if (    pVCpu
        &&  pVCpu->idCpu == idCpu)
        rc = dbgfR3DisasInstrExOnVCpu(pVM, pVCpu, pAddr->Sel, &pAddr->off, fFlags, pszOutput, cbOutput, NULL, pDisState);
    else
        rc = VMR3ReqPriorityCallWait(pVM, idCpu, (PFNRT)dbgfR3DisasInstrExOnVCpu, 9,
                                     pVM, VMMGetCpuById(pVM, idCpu), pAddr->Sel, &pAddr->off, fFlags,
                                     pszOutput, cbOutput, NULL, pDisState);
    return rc;
}

/**
 * Disassembles the one instruction according to the specified flags and address.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   idCpu           The ID of virtual CPU.
 * @param   Sel             The code selector. This used to determine the 32/16 bit ness and
 *                          calculation of the actual instruction address.
 * @param   GCPtr           The code address relative to the base of Sel.
 * @param   fFlags          Flags controlling where to start and how to format.
 *                          A combination of the DBGF_DISAS_FLAGS_* \#defines.
 * @param   pszOutput       Output buffer.  This will always be properly
 *                          terminated if @a cbOutput is greater than zero.
 * @param   cbOutput        Size of the output buffer.
 * @param   pcbInstr        Where to return the size of the instruction.
 *
 * @remarks May have to switch to the EMT of the virtual CPU in order to do
 *          address conversion.
 */
VMMR3DECL(int) DBGFR3DisasInstrEx(PUVM pUVM, VMCPUID idCpu, RTSEL Sel, RTGCPTR GCPtr, uint32_t fFlags,
                                  char *pszOutput, uint32_t cbOutput, uint32_t *pcbInstr)
{
    AssertReturn(cbOutput > 0, VERR_INVALID_PARAMETER);
    *pszOutput = '\0';
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);
    AssertReturn(!(fFlags & ~DBGF_DISAS_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn((fFlags & DBGF_DISAS_FLAGS_MODE_MASK) <= DBGF_DISAS_FLAGS_64BIT_MODE, VERR_INVALID_PARAMETER);

    /*
     * Optimize the common case where we're called on the EMT of idCpu since
     * we're using this all the time when logging.
     */
    int     rc;
    PVMCPU  pVCpu = VMMGetCpu(pVM);
    if (    pVCpu
        &&  pVCpu->idCpu == idCpu)
        rc = dbgfR3DisasInstrExOnVCpu(pVM, pVCpu, Sel, &GCPtr, fFlags, pszOutput, cbOutput, pcbInstr, NULL);
    else
        rc = VMR3ReqPriorityCallWait(pVM, idCpu, (PFNRT)dbgfR3DisasInstrExOnVCpu, 9,
                                     pVM, VMMGetCpuById(pVM, idCpu), Sel, &GCPtr, fFlags, pszOutput, cbOutput, pcbInstr, NULL);
    return rc;
}


/**
 * Disassembles the current guest context instruction.
 * All registers and data will be displayed. Addresses will be attempted resolved to symbols.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pszOutput       Output buffer.  This will always be properly
 *                          terminated if @a cbOutput is greater than zero.
 * @param   cbOutput        Size of the output buffer.
 * @thread  EMT(pVCpu)
 */
VMMR3_INT_DECL(int) DBGFR3DisasInstrCurrent(PVMCPU pVCpu, char *pszOutput, uint32_t cbOutput)
{
    AssertReturn(cbOutput > 0, VERR_INVALID_PARAMETER);
    *pszOutput = '\0';
    Assert(VMCPU_IS_EMT(pVCpu));

    RTGCPTR GCPtr = 0;
    return dbgfR3DisasInstrExOnVCpu(pVCpu->pVMR3, pVCpu, 0, &GCPtr,
                                    DBGF_DISAS_FLAGS_CURRENT_GUEST | DBGF_DISAS_FLAGS_DEFAULT_MODE
                                    | DBGF_DISAS_FLAGS_ANNOTATE_PATCHED,
                                    pszOutput, cbOutput, NULL, NULL);
}


/**
 * Disassembles the current guest context instruction and writes it to the log.
 * All registers and data will be displayed. Addresses will be attempted resolved to symbols.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pszPrefix       Short prefix string to the disassembly string. (optional)
 * @thread  EMT(pVCpu)
 */
VMMR3DECL(int) DBGFR3DisasInstrCurrentLogInternal(PVMCPU pVCpu, const char *pszPrefix)
{
    char szBuf[256];
    szBuf[0] = '\0';
    int rc = DBGFR3DisasInstrCurrent(pVCpu, &szBuf[0], sizeof(szBuf));
    if (RT_FAILURE(rc))
        RTStrPrintf(szBuf, sizeof(szBuf), "DBGFR3DisasInstrCurrentLog failed with rc=%Rrc\n", rc);
    if (pszPrefix && *pszPrefix)
    {
        if (pVCpu->CTX_SUFF(pVM)->cCpus > 1)
            RTLogPrintf("%s-CPU%u: %s\n", pszPrefix, pVCpu->idCpu, szBuf);
        else
            RTLogPrintf("%s: %s\n", pszPrefix, szBuf);
    }
    else
        RTLogPrintf("%s\n", szBuf);
    return rc;
}



/**
 * Disassembles the specified guest context instruction and writes it to the log.
 * Addresses will be attempted resolved to symbols.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      EMT.
 * @param   Sel         The code selector. This used to determine the 32/16
 *                      bit-ness and calculation of the actual instruction
 *                      address.
 * @param   GCPtr       The code address relative to the base of Sel.
 * @param   pszPrefix   Short prefix string to the disassembly string.
 *                      (optional)
 * @thread  EMT(pVCpu)
 */
VMMR3DECL(int) DBGFR3DisasInstrLogInternal(PVMCPU pVCpu, RTSEL Sel, RTGCPTR GCPtr, const char *pszPrefix)
{
    Assert(VMCPU_IS_EMT(pVCpu));

    char szBuf[256];
    RTGCPTR GCPtrTmp = GCPtr;
    int rc = dbgfR3DisasInstrExOnVCpu(pVCpu->pVMR3, pVCpu, Sel, &GCPtrTmp, DBGF_DISAS_FLAGS_DEFAULT_MODE,
                                      &szBuf[0], sizeof(szBuf), NULL, NULL);
    if (RT_FAILURE(rc))
        RTStrPrintf(szBuf, sizeof(szBuf), "DBGFR3DisasInstrLog(, %RTsel, %RGv) failed with rc=%Rrc\n", Sel, GCPtr, rc);
    if (pszPrefix && *pszPrefix)
    {
        if (pVCpu->CTX_SUFF(pVM)->cCpus > 1)
            RTLogPrintf("%s-CPU%u: %s\n", pszPrefix, pVCpu->idCpu, szBuf);
        else
            RTLogPrintf("%s: %s\n", pszPrefix, szBuf);
    }
    else
        RTLogPrintf("%s\n", szBuf);
    return rc;
}

