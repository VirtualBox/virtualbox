/* $Id: TRPMAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TRPM - Trap Monitor - Any Context.
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
#define LOG_GROUP LOG_GROUP_TRPM
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/dbgf.h>
#include "TRPMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>
#include <VBox/vmm/em.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/param.h>
#include <iprt/x86.h>



/**
 * Query info about the current active trap/interrupt.
 * If no trap is active active an error code is returned.
 *
 * @returns VBox status code.
 * @param   pVCpu                   The cross context virtual CPU structure.
 * @param   pu8TrapNo               Where to store the trap number.
 * @param   penmType                Where to store the trap type
 */
VMMDECL(int) TRPMQueryTrap(PVMCPU pVCpu, uint8_t *pu8TrapNo, TRPMEVENT *penmType)
{
    /*
     * Check if we have a trap at present.
     */
    if (pVCpu->trpm.s.uActiveVector != ~0U)
    {
        if (pu8TrapNo)
            *pu8TrapNo = (uint8_t)pVCpu->trpm.s.uActiveVector;
        if (penmType)
            *penmType = pVCpu->trpm.s.enmActiveType;
        return VINF_SUCCESS;
    }

    return VERR_TRPM_NO_ACTIVE_TRAP;
}


/**
 * Gets the trap number for the current trap.
 *
 * The caller is responsible for making sure there is an active trap which
 * takes an error code when making this request.
 *
 * @returns The current trap number.
 * @param   pVCpu                   The cross context virtual CPU structure.
 */
VMMDECL(uint8_t) TRPMGetTrapNo(PVMCPU pVCpu)
{
    AssertMsg(pVCpu->trpm.s.uActiveVector != ~0U, ("No active trap!\n"));
    return (uint8_t)pVCpu->trpm.s.uActiveVector;
}


/**
 * Gets the error code for the current trap.
 *
 * The caller is responsible for making sure there is an active trap which
 * takes an error code when making this request.
 *
 * @returns Error code.
 * @param   pVCpu                   The cross context virtual CPU structure.
 */
VMMDECL(uint32_t) TRPMGetErrorCode(PVMCPU pVCpu)
{
    AssertMsg(pVCpu->trpm.s.uActiveVector != ~0U, ("No active trap!\n"));
#ifdef VBOX_STRICT
    switch (pVCpu->trpm.s.uActiveVector)
    {
        case X86_XCPT_TS:
        case X86_XCPT_NP:
        case X86_XCPT_SS:
        case X86_XCPT_GP:
        case X86_XCPT_PF:
        case X86_XCPT_AC:
        case X86_XCPT_DF:
            break;
        default:
            AssertMsgFailed(("This trap (%#x) doesn't have any error code\n", pVCpu->trpm.s.uActiveVector));
            break;
    }
#endif
    return pVCpu->trpm.s.uActiveErrorCode;
}


/**
 * Gets the fault address for the current trap.
 *
 * The caller is responsible for making sure there is an active trap 0x0e when
 * making this request.
 *
 * @returns Fault address associated with the trap.
 * @param   pVCpu                   The cross context virtual CPU structure.
 */
VMMDECL(RTGCUINTPTR) TRPMGetFaultAddress(PVMCPU pVCpu)
{
    AssertMsg(pVCpu->trpm.s.uActiveVector != ~0U, ("No active trap!\n"));
    AssertMsg(pVCpu->trpm.s.uActiveVector == X86_XCPT_PF, ("Not page-fault trap!\n"));
    return pVCpu->trpm.s.uActiveCR2;
}


/**
 * Gets the instruction-length for the current trap (only relevant for software
 * interrupts and software exceptions \#BP and \#OF).
 *
 * The caller is responsible for making sure there is an active trap 0x0e when
 * making this request.
 *
 * @returns Fault address associated with the trap.
 * @param   pVCpu                   The cross context virtual CPU structure.
 */
VMMDECL(uint8_t) TRPMGetInstrLength(PVMCPU pVCpu)
{
    AssertMsg(pVCpu->trpm.s.uActiveVector != ~0U, ("No active trap!\n"));
    return pVCpu->trpm.s.cbInstr;
}


/**
 * Checks if the current \#DB exception is due to an INT1/ICEBP instruction.
 *
 * The caller is responsible for making sure there is an active trap.
 *
 * @returns @c true if it's due to INT1/ICEBP, @c false if not.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 */
VMMDECL(bool) TRPMIsTrapDueToIcebp(PVMCPU pVCpu)
{
    AssertMsg(pVCpu->trpm.s.uActiveVector != ~0U, ("No active trap!\n"));
    return pVCpu->trpm.s.fIcebp;
}


/**
 * Clears the current active trap/exception/interrupt.
 *
 * The caller is responsible for making sure there is an active trap
 * when making this request.
 *
 * @returns VBox status code.
 * @param   pVCpu                   The cross context virtual CPU structure.
 */
VMMDECL(int) TRPMResetTrap(PVMCPU pVCpu)
{
    /*
     * Cannot reset non-existing trap!
     */
    if (pVCpu->trpm.s.uActiveVector == ~0U)
    {
        AssertMsgFailed(("No active trap!\n"));
        return VERR_TRPM_NO_ACTIVE_TRAP;
    }

    /*
     * Reset it.
     */
    pVCpu->trpm.s.uActiveVector = ~0U;
    return VINF_SUCCESS;
}


/**
 * Assert trap/exception/interrupt.
 *
 * The caller is responsible for making sure there is no active trap
 * when making this request.
 *
 * @returns VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   u8TrapNo            The trap vector to assert.
 * @param   enmType             Trap type.
 */
VMMDECL(int) TRPMAssertTrap(PVMCPUCC pVCpu, uint8_t u8TrapNo, TRPMEVENT enmType)
{
    Log2(("TRPMAssertTrap: u8TrapNo=%02x type=%d\n", u8TrapNo, enmType));

    /*
     * Cannot assert a trap when one is already active.
     */
    if (pVCpu->trpm.s.uActiveVector != ~0U)
    {
        AssertMsgFailed(("CPU%d: Active trap %#x\n", pVCpu->idCpu, pVCpu->trpm.s.uActiveVector));
        return VERR_TRPM_ACTIVE_TRAP;
    }

    /* NMI TRPM type must specify the vector as 2 (NMI). */
    Assert(enmType != TRPM_NMI || u8TrapNo == X86_XCPT_NMI);

    pVCpu->trpm.s.uActiveVector               = u8TrapNo;
    pVCpu->trpm.s.enmActiveType               = enmType;
    pVCpu->trpm.s.uActiveErrorCode            = ~0U;
    pVCpu->trpm.s.uActiveCR2                  = 0xdeadface;
    pVCpu->trpm.s.cbInstr                     = UINT8_MAX;
    pVCpu->trpm.s.fIcebp                      = false;
    return VINF_SUCCESS;
}


/**
 * Assert a page-fault exception.
 *
 * The caller is responsible for making sure there is no active trap
 * when making this request.
 *
 * @returns VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   uCR2                The new fault address.
 * @param   uErrorCode          The error code for the page-fault.
 */
VMMDECL(int) TRPMAssertXcptPF(PVMCPUCC pVCpu, RTGCUINTPTR uCR2, uint32_t uErrorCode)
{
    Log2(("TRPMAssertXcptPF: uCR2=%RGv uErrorCode=%#RX32\n", uCR2, uErrorCode));

    /*
     * Cannot assert a trap when one is already active.
     */
    if (pVCpu->trpm.s.uActiveVector != ~0U)
    {
        AssertMsgFailed(("CPU%d: Active trap %#x\n", pVCpu->idCpu, pVCpu->trpm.s.uActiveVector));
        return VERR_TRPM_ACTIVE_TRAP;
    }

    pVCpu->trpm.s.uActiveVector               = X86_XCPT_PF;
    pVCpu->trpm.s.enmActiveType               = TRPM_TRAP;
    pVCpu->trpm.s.uActiveErrorCode            = uErrorCode;
    pVCpu->trpm.s.uActiveCR2                  = uCR2;
    pVCpu->trpm.s.cbInstr                     = UINT8_MAX;
    return VINF_SUCCESS;
}


/**
 * Sets the error code of the current trap.
 * (This function is for use in trap handlers and such.)
 *
 * The caller is responsible for making sure there is an active trap
 * which takes an errorcode when making this request.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   uErrorCode          The new error code.
 */
VMMDECL(void) TRPMSetErrorCode(PVMCPU pVCpu, uint32_t uErrorCode)
{
    Log2(("TRPMSetErrorCode: uErrorCode=%#RX32\n", uErrorCode));
    AssertMsg(pVCpu->trpm.s.uActiveVector != ~0U, ("No active trap!\n"));
    AssertMsg(   pVCpu->trpm.s.enmActiveType == TRPM_TRAP
              || (   pVCpu->trpm.s.enmActiveType == TRPM_SOFTWARE_INT && pVCpu->trpm.s.uActiveVector == X86_XCPT_DB),
              ("Not hardware exception or privileged software exception (INT1/ICEBP)!\n"));
    pVCpu->trpm.s.uActiveErrorCode = uErrorCode;
#ifdef VBOX_STRICT
    if (pVCpu->trpm.s.enmActiveType == TRPM_TRAP)
    {
        switch (pVCpu->trpm.s.uActiveVector)
        {
            case X86_XCPT_TS: case X86_XCPT_NP: case X86_XCPT_SS: case X86_XCPT_GP: case X86_XCPT_PF:
                AssertMsg(uErrorCode != ~0U, ("Invalid uErrorCode=%#x u8TrapNo=%u\n", uErrorCode, pVCpu->trpm.s.uActiveVector));
                break;
            case X86_XCPT_AC: case X86_XCPT_DF:
                AssertMsg(uErrorCode == 0,   ("Invalid uErrorCode=%#x u8TrapNo=%u\n", uErrorCode, pVCpu->trpm.s.uActiveVector));
                break;
            default:
                AssertMsg(uErrorCode == ~0U, ("Invalid uErrorCode=%#x u8TrapNo=%u\n", uErrorCode, pVCpu->trpm.s.uActiveVector));
                break;
        }
    }
#endif
}


/**
 * Sets the fault address of the current \#PF trap. (This function is for use in
 * trap handlers and such.)
 *
 * The caller is responsible for making sure there is an active trap 0e
 * when making this request.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   uCR2                The new fault address (cr2 register).
 */
VMMDECL(void) TRPMSetFaultAddress(PVMCPU pVCpu, RTGCUINTPTR uCR2)
{
    Log2(("TRPMSetFaultAddress: uCR2=%RGv\n", uCR2));
    AssertMsg(pVCpu->trpm.s.uActiveVector != ~0U, ("No active trap!\n"));
    AssertMsg(pVCpu->trpm.s.enmActiveType == TRPM_TRAP, ("Not hardware exception!\n"));
    AssertMsg(pVCpu->trpm.s.uActiveVector == X86_XCPT_PF, ("Not trap 0e!\n"));
    pVCpu->trpm.s.uActiveCR2 = uCR2;
}


/**
 * Sets the instruction-length of the current trap (relevant for software
 * interrupts and software exceptions like \#BP, \#OF).
 *
 * The caller is responsible for making sure there is an active trap when making
 * this request.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length.
 */
VMMDECL(void) TRPMSetInstrLength(PVMCPU pVCpu, uint8_t cbInstr)
{
    Log2(("TRPMSetInstrLength: cbInstr=%u\n", cbInstr));
    AssertMsg(pVCpu->trpm.s.uActiveVector != ~0U, ("No active trap!\n"));
    AssertMsg(   pVCpu->trpm.s.enmActiveType == TRPM_SOFTWARE_INT
              || (   pVCpu->trpm.s.enmActiveType == TRPM_TRAP
                  && (   pVCpu->trpm.s.uActiveVector == X86_XCPT_BP
                      || pVCpu->trpm.s.uActiveVector == X86_XCPT_OF)),
              ("Invalid trap type %#x\n", pVCpu->trpm.s.enmActiveType));
    pVCpu->trpm.s.cbInstr = cbInstr;
}


/**
 * Sets if the current \#DB exception is due to an INT1/ICEBP instruction.
 *
 * The caller is responsible for making sure there is an active trap and it's a
 * \#DB.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 */
VMMDECL(void) TRPMSetTrapDueToIcebp(PVMCPU pVCpu)
{
    AssertMsg(pVCpu->trpm.s.enmActiveType == TRPM_SOFTWARE_INT, ("Trap type for INT1/ICEBP invalid!"));
    AssertMsg(pVCpu->trpm.s.uActiveVector == X86_XCPT_DB, ("INT1/ICEBP must be indicated by a #DB!\n"));
    pVCpu->trpm.s.fIcebp = true;
}


/**
 * Checks if the current active trap/interrupt/exception/fault/whatever is a software
 * interrupt or not.
 *
 * The caller is responsible for making sure there is an active trap
 * when making this request.
 *
 * @returns true if software interrupt, false if not.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 */
VMMDECL(bool) TRPMIsSoftwareInterrupt(PVMCPU pVCpu)
{
    AssertMsg(pVCpu->trpm.s.uActiveVector != ~0U, ("No active trap!\n"));
    return (pVCpu->trpm.s.enmActiveType == TRPM_SOFTWARE_INT);
}


/**
 * Check if there is an active trap.
 *
 * @returns true if trap active, false if not.
 * @param   pVCpu               The cross context virtual CPU structure.
 */
VMMDECL(bool) TRPMHasTrap(PVMCPU pVCpu)
{
    return pVCpu->trpm.s.uActiveVector != ~0U;
}


/**
 * Query all info about the current active trap/interrupt.
 * If no trap is active active an error code is returned.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pu8TrapNo       Where to store the trap number.
 * @param   pEnmType        Where to store the trap type.
 * @param   puErrorCode     Where to store the error code associated with some
 *                          traps. ~0U is stored if the trap has no error code.
 * @param   puCR2           Where to store the CR2 associated with a trap 0E.
 * @param   pcbInstr        Where to store the instruction-length associated with
 *                          some traps.
 * @param   pfIcebp         Where to store whether the trap is a \#DB caused by an
 *                          INT1/ICEBP instruction.
 */
VMMDECL(int) TRPMQueryTrapAll(PVMCPU pVCpu, uint8_t *pu8TrapNo, TRPMEVENT *pEnmType, uint32_t *puErrorCode, PRTGCUINTPTR puCR2,
                              uint8_t *pcbInstr, bool *pfIcebp)
{
    /*
     * Check if we have an active trap.
     */
    if (pVCpu->trpm.s.uActiveVector == ~0U)
        return VERR_TRPM_NO_ACTIVE_TRAP;

    if (pu8TrapNo)
        *pu8TrapNo      = (uint8_t)pVCpu->trpm.s.uActiveVector;
    if (pEnmType)
        *pEnmType       = pVCpu->trpm.s.enmActiveType;
    if (puErrorCode)
        *puErrorCode    = pVCpu->trpm.s.uActiveErrorCode;
    if (puCR2)
        *puCR2          = pVCpu->trpm.s.uActiveCR2;
    if (pcbInstr)
        *pcbInstr       = pVCpu->trpm.s.cbInstr;
    if (pfIcebp)
        *pfIcebp        = pVCpu->trpm.s.fIcebp;
    return VINF_SUCCESS;
}

