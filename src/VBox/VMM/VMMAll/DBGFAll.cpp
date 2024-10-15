/* $Id: DBGFAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, All Context Code.
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
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/dbgf.h>
#include "DBGFInternal.h"
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/stdarg.h>


/*
 * Check the read-only VM members.
 */
AssertCompileMembersSameSizeAndOffset(VM, dbgf.s.bmSoftIntBreakpoints,  VM, dbgf.ro.bmSoftIntBreakpoints);
AssertCompileMembersSameSizeAndOffset(VM, dbgf.s.bmHardIntBreakpoints,  VM, dbgf.ro.bmHardIntBreakpoints);
AssertCompileMembersSameSizeAndOffset(VM, dbgf.s.bmSelectedEvents,      VM, dbgf.ro.bmSelectedEvents);
AssertCompileMembersSameSizeAndOffset(VM, dbgf.s.cHardIntBreakpoints,   VM, dbgf.ro.cHardIntBreakpoints);
AssertCompileMembersSameSizeAndOffset(VM, dbgf.s.cSoftIntBreakpoints,   VM, dbgf.ro.cSoftIntBreakpoints);
AssertCompileMembersSameSizeAndOffset(VM, dbgf.s.cSelectedEvents,       VM, dbgf.ro.cSelectedEvents);

#if !defined(VBOX_VMM_TARGET_ARMV8)


/**
 * Gets the hardware breakpoint configuration as DR7.
 *
 * @returns DR7 from the DBGF point of view.
 * @param   pVM         The cross context VM structure.
 */
VMM_INT_DECL(RTGCUINTREG) DBGFBpGetDR7(PVM pVM)
{
    RTGCUINTREG uDr7 = X86_DR7_GD | X86_DR7_GE | X86_DR7_LE | X86_DR7_RA1_MASK;
    for (uint32_t i = 0; i < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints); i++)
    {
        if (   pVM->dbgf.s.aHwBreakpoints[i].fEnabled
            && pVM->dbgf.s.aHwBreakpoints[i].hBp != NIL_DBGFBP)
        {
            static const uint8_t s_au8Sizes[8] =
            {
                X86_DR7_LEN_BYTE, X86_DR7_LEN_BYTE, X86_DR7_LEN_WORD, X86_DR7_LEN_BYTE,
                X86_DR7_LEN_DWORD,X86_DR7_LEN_BYTE, X86_DR7_LEN_BYTE, X86_DR7_LEN_QWORD
            };
            uDr7 |= X86_DR7_G(i)
                 |  X86_DR7_RW(i, pVM->dbgf.s.aHwBreakpoints[i].fType)
                 |  X86_DR7_LEN(i, s_au8Sizes[pVM->dbgf.s.aHwBreakpoints[i].cb]);
        }
    }
    return uDr7;
}


/**
 * Gets the address of the hardware breakpoint number 0.
 *
 * @returns DR0 from the DBGF point of view.
 * @param   pVM         The cross context VM structure.
 */
VMM_INT_DECL(RTGCUINTREG) DBGFBpGetDR0(PVM pVM)
{
    return pVM->dbgf.s.aHwBreakpoints[0].GCPtr;
}


/**
 * Gets the address of the hardware breakpoint number 1.
 *
 * @returns DR1 from the DBGF point of view.
 * @param   pVM         The cross context VM structure.
 */
VMM_INT_DECL(RTGCUINTREG) DBGFBpGetDR1(PVM pVM)
{
    return pVM->dbgf.s.aHwBreakpoints[1].GCPtr;
}


/**
 * Gets the address of the hardware breakpoint number 2.
 *
 * @returns DR2 from the DBGF point of view.
 * @param   pVM         The cross context VM structure.
 */
VMM_INT_DECL(RTGCUINTREG) DBGFBpGetDR2(PVM pVM)
{
    return pVM->dbgf.s.aHwBreakpoints[2].GCPtr;
}


/**
 * Gets the address of the hardware breakpoint number 3.
 *
 * @returns DR3 from the DBGF point of view.
 * @param   pVM         The cross context VM structure.
 */
VMM_INT_DECL(RTGCUINTREG) DBGFBpGetDR3(PVM pVM)
{
    return pVM->dbgf.s.aHwBreakpoints[3].GCPtr;
}


/**
 * Checks if any of the hardware breakpoints are armed.
 *
 * @returns true if armed, false if not.
 * @param   pVM        The cross context VM structure.
 * @remarks Don't call this from CPUMRecalcHyperDRx!
 */
VMM_INT_DECL(bool) DBGFBpIsHwArmed(PVM pVM)
{
    return pVM->dbgf.s.cEnabledHwBreakpoints > 0;
}


/**
 * Checks if any of the hardware I/O breakpoints are armed.
 *
 * @returns true if armed, false if not.
 * @param   pVM        The cross context VM structure.
 * @remarks Don't call this from CPUMRecalcHyperDRx!
 */
VMM_INT_DECL(bool) DBGFBpIsHwIoArmed(PVM pVM)
{
    return pVM->dbgf.s.cEnabledHwIoBreakpoints > 0;
}


/**
 * Checks if any INT3 breakpoints are armed.
 *
 * @returns true if armed, false if not.
 * @param   pVM        The cross context VM structure.
 * @remarks Don't call this from CPUMRecalcHyperDRx!
 */
VMM_INT_DECL(bool) DBGFBpIsInt3Armed(PVM pVM)
{
    /** @todo There was a todo here and returning false when I (bird) removed
     *        VBOX_WITH_LOTS_OF_DBGF_BPS, so this might not be correct. */
    return pVM->dbgf.s.cEnabledInt3Breakpoints > 0;
}


/**
 * Checks instruction boundrary for guest or hypervisor hardware breakpoints.
 *
 * @returns Strict VBox status code.  May return DRx register import errors in
 *          addition to the ones detailed.
 * @retval  VINF_SUCCESS no breakpoint.
 * @retval  VINF_EM_DBG_BREAKPOINT hypervisor breakpoint triggered.
 * @retval  VINF_EM_RAW_GUEST_TRAP caller must trigger \#DB trap, DR6 and DR7
 *          have been updated appropriately.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtrPC     The unsegmented PC address.
 * @param   fCheckGuest Whether to include guest breakpoints or not.
 */
VMM_INT_DECL(VBOXSTRICTRC)  DBGFBpCheckInstruction(PVMCC pVM, PVMCPUCC pVCpu, RTGCPTR GCPtrPC, bool fCheckGuest)
{
    CPUM_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_DR7);

    /*
     * Check hyper breakpoints first as the VMM debugger has priority over
     * the guest.
     */
    /** @todo we need some kind of resume flag for these. */
    if (pVM->dbgf.s.cEnabledHwBreakpoints > 0)
        for (unsigned iBp = 0; iBp < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints); iBp++)
        {
            if (   pVM->dbgf.s.aHwBreakpoints[iBp].GCPtr != GCPtrPC
                || pVM->dbgf.s.aHwBreakpoints[iBp].fType != X86_DR7_RW_EO
                || pVM->dbgf.s.aHwBreakpoints[iBp].cb    != 1
                || !pVM->dbgf.s.aHwBreakpoints[iBp].fEnabled
                || pVM->dbgf.s.aHwBreakpoints[iBp].hBp   == NIL_DBGFBP)
            { /*likely*/ }
            else
            {
                /* (See also DBGFRZTrap01Handler.) */
                pVCpu->dbgf.s.hBpActive = pVM->dbgf.s.aHwBreakpoints[iBp].hBp;
                pVCpu->dbgf.s.fSingleSteppingRaw = false;

                LogFlow(("DBGFBpCheckInstruction: hit hw breakpoint %u at %04x:%RGv (%RGv)\n",
                         iBp, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, GCPtrPC));
                return VINF_EM_DBG_BREAKPOINT;
            }
        }

    /*
     * Check the guest.
     */
    if (fCheckGuest)
    {
        uint32_t const fDr7 = (uint32_t)pVCpu->cpum.GstCtx.dr[7];
        if (X86_DR7_ANY_EO_ENABLED(fDr7) && !pVCpu->cpum.GstCtx.eflags.Bits.u1RF)
        {
            /*
             * The CPU (10980XE & 6700K at least) will set the DR6.BPx bits for any
             * DRx that matches the current PC and is configured as an execution
             * breakpoint (RWx=EO, LENx=1byte).  They don't have to be enabled,
             * however one that is enabled must match for the #DB to be raised and
             * DR6 to be modified, of course.
             */
            CPUM_IMPORT_EXTRN_RET(pVCpu, CPUMCTX_EXTRN_DR0_DR3);
            uint32_t fMatched = 0;
            uint32_t fEnabled = 0;
            for (unsigned iBp = 0, uBpMask = 1; iBp < 4; iBp++, uBpMask <<= 1)
                if (X86_DR7_IS_EO_CFG(fDr7, iBp))
                {
                    if (fDr7 & X86_DR7_L_G(iBp))
                        fEnabled |= uBpMask;
                    if (pVCpu->cpum.GstCtx.dr[iBp] == GCPtrPC)
                        fMatched |= uBpMask;
                }
            if (!(fEnabled & fMatched))
            { /*likely*/ }
            else
            {
                /*
                 * Update DR6 and DR7.
                 *
                 * See "AMD64 Architecture Programmer's Manual Volume 2", chapter
                 * 13.1.1.3 for details on DR6 bits.  The basics is that the B0..B3
                 * bits are always cleared while the others must be cleared by software.
                 *
                 * The following sub chapters says the GD bit is always cleared when
                 * generating a #DB so the handler can safely access the debug registers.
                 */
                CPUM_IMPORT_EXTRN_RET(pVCpu, CPUMCTX_EXTRN_DR6);
                pVCpu->cpum.GstCtx.dr[6] &= ~X86_DR6_B_MASK;
                if (pVM->cpum.ro.GuestFeatures.enmCpuVendor != CPUMCPUVENDOR_INTEL)
                    pVCpu->cpum.GstCtx.dr[6] |= fMatched & fEnabled;
                else
                    pVCpu->cpum.GstCtx.dr[6] |= fMatched;    /* Intel: All matched, regardless of whether they're enabled or not  */
                pVCpu->cpum.GstCtx.dr[7] &= ~X86_DR7_GD;
                LogFlow(("DBGFBpCheckInstruction: hit hw breakpoints %#x at %04x:%RGv (%RGv)\n",
                         fMatched, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, GCPtrPC));
                return VINF_EM_RAW_GUEST_TRAP;
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * Common worker for DBGFBpCheckDataRead and DBGFBpCheckDataWrite.
 */
template<bool const a_fRead>
DECL_FORCE_INLINE(uint32_t) dbgfBpCheckData(PVMCC pVM, PVMCPUCC pVCpu, RTGCPTR GCPtrAccess, uint32_t cbAccess, bool fSysAccess)
{
    AssertCompile((X86_DR7_RW_RW & 1) && (X86_DR7_RW_WO & 1));
    CPUM_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_DR7);

    uint32_t      fRet           = 0;
    RTGCPTR const GCPtrAccessPfn = GCPtrAccess >> GUEST_PAGE_SHIFT;
    Assert(((GCPtrAccess + cbAccess - 1) >> GUEST_PAGE_SHIFT) == GCPtrAccessPfn); /* No page crossing expected here! */

    /*
     * Check hyper breakpoints first as the VMM debugger has priority over
     * the guest.
     */
    if (pVM->dbgf.s.cEnabledHwBreakpoints > 0)
        for (unsigned iBp = 0; iBp < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints); iBp++)
        {
            if (   (pVM->dbgf.s.aHwBreakpoints[iBp].GCPtr >> GUEST_PAGE_SHIFT) != GCPtrAccessPfn
                || (  a_fRead
                    ? pVM->dbgf.s.aHwBreakpoints[iBp].fType != X86_DR7_RW_RW
                    : !(pVM->dbgf.s.aHwBreakpoints[iBp].fType & 1))
                || pVM->dbgf.s.aHwBreakpoints[iBp].cb    != 0
                || !pVM->dbgf.s.aHwBreakpoints[iBp].fEnabled
                || pVM->dbgf.s.aHwBreakpoints[iBp].hBp   == NIL_DBGFBP)
            { /*likely*/ }
            else
            {
                /* The page is of interest. */
                AssertCompile(!((CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_DBG_DBGF_MASK) & UINT32_C(1)));
                fRet |= UINT32_C(1);

                /* If the access overlapping the breakpoint area, we have a hit. */
                if (   GCPtrAccess            < pVM->dbgf.s.aHwBreakpoints[iBp].GCPtr + pVM->dbgf.s.aHwBreakpoints[iBp].cb
                    && GCPtrAccess + cbAccess > pVM->dbgf.s.aHwBreakpoints[iBp].GCPtr)
                {
                    pVCpu->dbgf.s.hBpActive          = pVM->dbgf.s.aHwBreakpoints[iBp].hBp; /* ? */
                    pVCpu->dbgf.s.fSingleSteppingRaw = false;
                    LogFlow(("DBGFBpCheckData%s: hit hw breakpoint %u when accessing %RGv LB %#x\n",
                             a_fRead ? "Read" : "Write", iBp, GCPtrAccess, cbAccess));
                    fRet |= CPUMCTX_DBG_DBGF_BP;
                }
            }
        }

    /*
     * Check the guest.
     */
    uint32_t const fDr7 = (uint32_t)pVCpu->cpum.GstCtx.dr[7];
    if (    (a_fRead ? X86_DR7_ANY_RW_ENABLED(fDr7) : X86_DR7_ANY_W_ENABLED(fDr7))
        && !pVCpu->cpum.GstCtx.eflags.Bits.u1RF)
    {
        /* This is a bit suboptimal... Need a NORET variant. */
        int rcIgn = VINF_SUCCESS;
        CPUM_IMPORT_EXTRN_RCSTRICT(pVCpu, CPUMCTX_EXTRN_DR0_DR3, rcIgn);
        RT_NOREF(rcIgn);

        /** @todo Not sure what exactly intel and amd CPUs does here wrt disabled
         *        breakpoint configurations.  We need a testcase for this.  Following
         *        the guidelines of the execution breakpoints for now and making
         *        intel CPUs set status flags regardless of enabled or not. */
        uint32_t fMatched = 0;
        uint32_t fEnabled = 0;
        for (uint32_t iBp = 0, fBpMask = CPUMCTX_DBG_HIT_DR0, fDr7Cfg = fDr7 >> 16, fDr7En = fDr7;
             iBp < 4;
             iBp++, fBpMask <<= 1, fDr7Cfg >>= 4, fDr7En >>= 2)
            if (   (a_fRead ? (fDr7Cfg & 3) == X86_DR7_RW_RW : (fDr7Cfg & 1) != 0)
                && (pVCpu->cpum.GstCtx.dr[iBp] >> GUEST_PAGE_SHIFT) == GCPtrAccessPfn)
            {
                if (fDr7En & 3)
                {
                    fEnabled |= fBpMask;
                    fRet     |= UINT32_C(1);
                }
                static uint8_t const s_acbBp[] = { 1, 2, 8, 4 };
                uint8_t const        cbBp      = s_acbBp[(fDr7Cfg >> 2) & 3];
                if (   GCPtrAccess            < pVCpu->cpum.GstCtx.dr[iBp] + cbBp
                    && GCPtrAccess + cbAccess > pVCpu->cpum.GstCtx.dr[iBp])
                    fMatched |= fBpMask;
            }
        if (!(fEnabled & fMatched))
        { /*likely*/ }
        else
        {
            if (pVM->cpum.ro.GuestFeatures.enmCpuVendor != CPUMCPUVENDOR_INTEL)
                fRet |= fMatched & fEnabled;
            else if (!fSysAccess)
                fRet |= fMatched;
            else
                fRet |= CPUMCTX_DBG_HIT_DRX_SILENT; /* see bs3-cpu-weird-1 for special intel behviour  */
            LogFlow(("DBGFBpCheckData%s: hit hw breakpoints %#x (fRet=%#x) when accessing %RGv LB %#x\n",
                     a_fRead ? "Read" : "Write", fMatched, fRet, GCPtrAccess, cbAccess));
        }
    }

    return fRet;
}


/**
 * Checks read data access for guest or hypervisor hardware breakpoints.
 *
 * @returns Anything in CPUMCTX_DBG_HIT_DRX_MASK and CPUMCTX_DBG_DBGF_MASK if
 *          there is a hit, zero or one if no hit.  Bit 0 is set if the page
 *          being accessed has a data breakpoint associated with it and needs
 *          special handling.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtrAccess The address being accessed.
 * @param   cbAccess    The size of the access.  Must not cross a page
 *                      boundrary.
 * @param   fSysAccess  Set if a system access, like GDT, LDT or IDT.
 */
VMM_INT_DECL(uint32_t) DBGFBpCheckDataRead(PVMCC pVM, PVMCPUCC pVCpu, RTGCPTR GCPtrAccess, uint32_t cbAccess, bool fSysAccess)
{
    return dbgfBpCheckData<true /*a_fRead*/>(pVM, pVCpu, GCPtrAccess, cbAccess, fSysAccess);
}


/**
 * Checks read data access for guest or hypervisor hardware breakpoints.
 *
 * @returns Anything in CPUMCTX_DBG_DBGF_MASK if there is a hit, zero or one if
 *          no hit.  Bit 0 is set if the page being accessed has a data
 *          breakpoint associated with it and needs special handling.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtrAccess The address being accessed.
 * @param   cbAccess    The size of the access.  Must not cross a page
 *                      boundrary.
 * @param   fSysAccess  Set if a system access, like GDT, LDT or IDT.
 */
VMM_INT_DECL(uint32_t) DBGFBpCheckDataWrite(PVMCC pVM, PVMCPUCC pVCpu, RTGCPTR GCPtrAccess, uint32_t cbAccess, bool fSysAccess)
{
    return dbgfBpCheckData<false /*a_fRead*/>(pVM, pVCpu, GCPtrAccess, cbAccess, fSysAccess);
}


/**
 * Checks I/O access for guest or hypervisor hardware breakpoints.
 *
 * @returns Strict VBox status code
 * @retval  VINF_SUCCESS no breakpoint.
 * @retval  VINF_EM_DBG_BREAKPOINT hypervisor breakpoint triggered.
 * @retval  VINF_EM_RAW_GUEST_TRAP guest breakpoint triggered, DR6 and DR7 have
 *          been updated appropriately.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx        The CPU context for the calling EMT.
 * @param   uIoPort     The I/O port being accessed.
 * @param   cbValue     The size/width of the access, in bytes.
 */
VMM_INT_DECL(VBOXSTRICTRC)  DBGFBpCheckIo(PVM pVM, PVMCPU pVCpu, PCPUMCTX pCtx, RTIOPORT uIoPort, uint8_t cbValue)
{
    uint32_t const uIoPortFirst = uIoPort;
    uint32_t const uIoPortLast  = uIoPortFirst + cbValue - 1;

    /*
     * Check hyper breakpoints first as the VMM debugger has priority over
     * the guest.
     */
    if (pVM->dbgf.s.cEnabledHwIoBreakpoints > 0)
    {
        for (unsigned iBp = 0; iBp < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints); iBp++)
        {
            if (   pVM->dbgf.s.aHwBreakpoints[iBp].fType == X86_DR7_RW_IO
                && pVM->dbgf.s.aHwBreakpoints[iBp].fEnabled
                && pVM->dbgf.s.aHwBreakpoints[iBp].hBp   != NIL_DBGFBP)
            {
                uint8_t  cbReg      = pVM->dbgf.s.aHwBreakpoints[iBp].cb; Assert(RT_IS_POWER_OF_TWO(cbReg));
                uint64_t uDrXFirst  = pVM->dbgf.s.aHwBreakpoints[iBp].GCPtr & ~(uint64_t)(cbReg - 1);
                uint64_t uDrXLast   = uDrXFirst + cbReg - 1;
                if (uDrXFirst <= uIoPortLast && uDrXLast >= uIoPortFirst)
                {
                    /* (See also DBGFRZTrap01Handler.) */
                    pVCpu->dbgf.s.hBpActive = pVM->dbgf.s.aHwBreakpoints[iBp].hBp;
                    pVCpu->dbgf.s.fSingleSteppingRaw = false;

                    LogFlow(("DBGFBpCheckIo: hit hw breakpoint %d at %04x:%RGv (iop %#x)\n",
                             iBp, pCtx->cs.Sel, pCtx->rip, uIoPort));
                    return VINF_EM_DBG_BREAKPOINT;
                }
            }
        }
    }

    /*
     * Check the guest.
     */
    uint32_t const uDr7 = pCtx->dr[7];
    if (   (uDr7 & X86_DR7_ENABLED_MASK)
        && X86_DR7_ANY_RW_IO(uDr7)
        && (pCtx->cr4 & X86_CR4_DE) )
    {
        for (unsigned iBp = 0; iBp < 4; iBp++)
        {
            if (   (uDr7 & X86_DR7_L_G(iBp))
                && X86_DR7_GET_RW(uDr7, iBp) == X86_DR7_RW_IO)
            {
                /* ASSUME the breakpoint and the I/O width qualifier uses the same encoding (1 2 x 4). */
                static uint8_t const s_abInvAlign[4] = { 0, 1, 7, 3 };
                uint8_t  cbInvAlign = s_abInvAlign[X86_DR7_GET_LEN(uDr7, iBp)];
                uint64_t uDrXFirst  = pCtx->dr[iBp] & ~(uint64_t)cbInvAlign;
                uint64_t uDrXLast   = uDrXFirst + cbInvAlign;

                if (uDrXFirst <= uIoPortLast && uDrXLast >= uIoPortFirst)
                {
                    /*
                     * Update DR6 and DR7.
                     *
                     * See "AMD64 Architecture Programmer's Manual Volume 2",
                     * chapter 13.1.1.3 for details on DR6 bits.  The basics is
                     * that the B0..B3 bits are always cleared while the others
                     * must be cleared by software.
                     *
                     * The following sub chapters says the GD bit is always
                     * cleared when generating a #DB so the handler can safely
                     * access the debug registers.
                     */
                    pCtx->dr[6] &= ~X86_DR6_B_MASK;
                    pCtx->dr[6] |= X86_DR6_B(iBp);
                    pCtx->dr[7] &= ~X86_DR7_GD;
                    LogFlow(("DBGFBpCheckIo: hit hw breakpoint %d at %04x:%RGv (iop %#x)\n",
                             iBp, pCtx->cs.Sel, pCtx->rip, uIoPort));
                    return VINF_EM_RAW_GUEST_TRAP;
                }
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * Checks I/O access for guest or hypervisor hardware breakpoints.
 *
 * Caller must make sure DR0-3 and DR7 are present in the CPU context before
 * calling this function.
 *
 * @returns CPUMCTX_DBG_DBGF_BP, CPUMCTX_DBG_HIT_DRX_MASK, or 0 (no match).
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uIoPort     The I/O port being accessed.
 * @param   cbValue     The size/width of the access, in bytes.
 */
VMM_INT_DECL(uint32_t) DBGFBpCheckIo2(PVMCC pVM, PVMCPUCC pVCpu, RTIOPORT uIoPort, uint8_t cbValue)
{
    uint32_t const uIoPortFirst = uIoPort;
    uint32_t const uIoPortLast  = uIoPortFirst + cbValue - 1;

    /*
     * Check hyper breakpoints first as the VMM debugger has priority over
     * the guest.
     */
    if (pVM->dbgf.s.cEnabledHwIoBreakpoints > 0)
        for (unsigned iBp = 0; iBp < RT_ELEMENTS(pVM->dbgf.s.aHwBreakpoints); iBp++)
        {
            if (   pVM->dbgf.s.aHwBreakpoints[iBp].fType == X86_DR7_RW_IO
                && pVM->dbgf.s.aHwBreakpoints[iBp].fEnabled
                && pVM->dbgf.s.aHwBreakpoints[iBp].hBp   != NIL_DBGFBP)
            {
                uint8_t  cbReg      = pVM->dbgf.s.aHwBreakpoints[iBp].cb; Assert(RT_IS_POWER_OF_TWO(cbReg));
                uint64_t uDrXFirst  = pVM->dbgf.s.aHwBreakpoints[iBp].GCPtr & ~(uint64_t)(cbReg - 1);
                uint64_t uDrXLast   = uDrXFirst + cbReg - 1;
                if (uDrXFirst <= uIoPortLast && uDrXLast >= uIoPortFirst)
                {
                    /* (See also DBGFRZTrap01Handler.) */
                    pVCpu->dbgf.s.hBpActive = pVM->dbgf.s.aHwBreakpoints[iBp].hBp;
                    pVCpu->dbgf.s.fSingleSteppingRaw = false;

                    LogFlow(("DBGFBpCheckIo2: hit hw breakpoint %d at %04x:%RGv (iop %#x L %u)\n",
                             iBp, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, uIoPort, cbValue));
                    return CPUMCTX_DBG_DBGF_BP;
                }
            }
        }

    /*
     * Check the guest.
     */
    uint32_t const fDr7 = pVCpu->cpum.GstCtx.dr[7];
    if (   (fDr7 & X86_DR7_ENABLED_MASK)
        && X86_DR7_ANY_RW_IO(fDr7)
        && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_DE) )
    {
        uint32_t fEnabled = 0;
        uint32_t fMatched = 0;
        for (unsigned iBp = 0, uBpMask = 1; iBp < 4; iBp++, uBpMask <<= 1)
        {
            if (fDr7 & X86_DR7_L_G(iBp))
                fEnabled |= uBpMask;
            if (X86_DR7_GET_RW(fDr7, iBp) == X86_DR7_RW_IO)
            {
                /* ASSUME the breakpoint and the I/O width qualifier uses the same encoding (1 2 x 4). */
                static uint8_t const s_abInvAlign[4] = { 0, 1, 7, 3 };
                uint8_t  const cbInvAlign = s_abInvAlign[X86_DR7_GET_LEN(fDr7, iBp)];
                uint64_t const uDrXFirst  = pVCpu->cpum.GstCtx.dr[iBp] & ~(uint64_t)cbInvAlign;
                uint64_t const uDrXLast   = uDrXFirst + cbInvAlign;
                if (uDrXFirst <= uIoPortLast && uDrXLast >= uIoPortFirst)
                    fMatched |= uBpMask;
            }
        }
        if (fEnabled & fMatched)
        {
            LogFlow(("DBGFBpCheckIo2: hit hw breakpoint %#x at %04x:%RGv (iop %#x L %u)\n",
                     fMatched, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, uIoPort, cbValue));
            return fMatched << CPUMCTX_DBG_HIT_DRX_SHIFT;
        }
    }

    return 0;
}

#endif /* !VBOX_VMM_TARGET_ARMV8 */

/**
 * Returns the single stepping state for a virtual CPU.
 *
 * @returns stepping (true) or not (false).
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMM_INT_DECL(bool) DBGFIsStepping(PVMCPU pVCpu)
{
    return pVCpu->dbgf.s.fSingleSteppingRaw;
}


/**
 * Checks if the specified generic event is enabled or not.
 *
 * @returns true / false.
 * @param   pVM                 The cross context VM structure.
 * @param   enmEvent            The generic event being raised.
 * @param   uEventArg           The argument of that event.
 */
DECLINLINE(bool) dbgfEventIsGenericWithArgEnabled(PVM pVM, DBGFEVENTTYPE enmEvent, uint64_t uEventArg)
{
    if (DBGF_IS_EVENT_ENABLED(pVM, enmEvent))
    {
        switch (enmEvent)
        {
            case DBGFEVENT_INTERRUPT_HARDWARE:
                AssertReturn(uEventArg < 256, false);
                return ASMBitTest(pVM->dbgf.s.bmHardIntBreakpoints, (uint32_t)uEventArg);

            case DBGFEVENT_INTERRUPT_SOFTWARE:
                AssertReturn(uEventArg < 256, false);
                return ASMBitTest(pVM->dbgf.s.bmSoftIntBreakpoints, (uint32_t)uEventArg);

            default:
                return true;

        }
    }
    return false;
}


/**
 * Raises a generic debug event if enabled and not being ignored.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_EM_DBG_EVENT if the event was raised and the caller should
 *          return ASAP to the debugger (via EM).  We set VMCPU_FF_DBGF so, it
 *          is okay not to pass this along in some situations.
 * @retval  VINF_SUCCESS if the event was disabled or ignored.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   enmEvent            The generic event being raised.
 * @param   enmCtx              The context in which this event is being raised.
 * @param   cArgs               Number of arguments (0 - 6).
 * @param   ...                 Event arguments.
 *
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) DBGFEventGenericWithArgs(PVM pVM, PVMCPU pVCpu, DBGFEVENTTYPE enmEvent, DBGFEVENTCTX enmCtx,
                                                    unsigned cArgs, ...)
{
    Assert(cArgs < RT_ELEMENTS(pVCpu->dbgf.s.aEvents[0].Event.u.Generic.auArgs));

    /*
     * Is it enabled.
     */
    va_list va;
    va_start(va, cArgs);
    uint64_t uEventArg0 = cArgs ? va_arg(va, uint64_t) : 0;
    if (dbgfEventIsGenericWithArgEnabled(pVM, enmEvent, uEventArg0))
    {
        /*
         * Any events on the stack. Should the incoming event be ignored?
         */
#if defined(VBOX_VMM_TARGET_ARMV8)
        uint64_t const rip = CPUMGetGuestFlatPC(pVCpu); /* rip is a misnomer but saves us #ifdef's later on. */
#else
        uint64_t const rip = CPUMGetGuestRIP(pVCpu);
#endif
        uint32_t i = pVCpu->dbgf.s.cEvents;
        if (i > 0)
        {
            while (i-- > 0)
            {
                if (   pVCpu->dbgf.s.aEvents[i].Event.enmType   == enmEvent
                    && pVCpu->dbgf.s.aEvents[i].enmState        == DBGFEVENTSTATE_IGNORE
                    && pVCpu->dbgf.s.aEvents[i].rip             == rip)
                {
                    pVCpu->dbgf.s.aEvents[i].enmState = DBGFEVENTSTATE_RESTORABLE;
                    va_end(va);
                    return VINF_SUCCESS;
                }
                Assert(pVCpu->dbgf.s.aEvents[i].enmState != DBGFEVENTSTATE_CURRENT);
            }

            /*
             * Trim the event stack.
             */
            i = pVCpu->dbgf.s.cEvents;
            while (i-- > 0)
            {
                if (   pVCpu->dbgf.s.aEvents[i].rip == rip
                    && (   pVCpu->dbgf.s.aEvents[i].enmState == DBGFEVENTSTATE_RESTORABLE
                        || pVCpu->dbgf.s.aEvents[i].enmState == DBGFEVENTSTATE_IGNORE) )
                    pVCpu->dbgf.s.aEvents[i].enmState = DBGFEVENTSTATE_IGNORE;
                else
                {
                    if (i + 1 != pVCpu->dbgf.s.cEvents)
                        memmove(&pVCpu->dbgf.s.aEvents[i], &pVCpu->dbgf.s.aEvents[i + 1],
                                (pVCpu->dbgf.s.cEvents - i) * sizeof(pVCpu->dbgf.s.aEvents));
                    pVCpu->dbgf.s.cEvents--;
                }
            }

            i = pVCpu->dbgf.s.cEvents;
            AssertStmt(i < RT_ELEMENTS(pVCpu->dbgf.s.aEvents), i = RT_ELEMENTS(pVCpu->dbgf.s.aEvents) - 1);
        }

        /*
         * Push the event.
         */
        pVCpu->dbgf.s.aEvents[i].enmState               = DBGFEVENTSTATE_CURRENT;
        pVCpu->dbgf.s.aEvents[i].rip                    = rip;
        pVCpu->dbgf.s.aEvents[i].Event.enmType          = enmEvent;
        pVCpu->dbgf.s.aEvents[i].Event.enmCtx           = enmCtx;
        pVCpu->dbgf.s.aEvents[i].Event.u.Generic.cArgs  = cArgs;
        pVCpu->dbgf.s.aEvents[i].Event.u.Generic.auArgs[0] = uEventArg0;
        if (cArgs > 1)
        {
            AssertStmt(cArgs < RT_ELEMENTS(pVCpu->dbgf.s.aEvents[i].Event.u.Generic.auArgs),
                       cArgs = RT_ELEMENTS(pVCpu->dbgf.s.aEvents[i].Event.u.Generic.auArgs));
            for (unsigned iArg = 1; iArg < cArgs; iArg++)
                pVCpu->dbgf.s.aEvents[i].Event.u.Generic.auArgs[iArg] = va_arg(va, uint64_t);
        }
        pVCpu->dbgf.s.cEvents = i + 1;

        VMCPU_FF_SET(pVCpu, VMCPU_FF_DBGF);
        va_end(va);
        return VINF_EM_DBG_EVENT;
    }

    va_end(va);
    return VINF_SUCCESS;
}

