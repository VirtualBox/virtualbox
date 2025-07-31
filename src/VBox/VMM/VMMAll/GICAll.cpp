/* $Id: GICAll.cpp 110399 2025-07-24 09:25:19Z ramshankar.venkataraman@oracle.com $ */
/** @file
 * GIC - Generic Interrupt Controller Architecture (GIC) - All Contexts.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

/** @page pg_gic    GIC - Generic Interrupt Controller
 *
 * The GIC is an interrupt controller device that lives in VMM but also registers
 * itself with PDM similar to the APIC. The reason for this is needs to access
 * per-VCPU data and is an integral part of any ARMv8 VM.
 *
 * The GIC is made up of 3 main components:
 *      - Distributor
 *      - Redistributor
 *      - Interrupt Translation Service (ITS)
 *
 * The distributor is per-VM while the redistributors are per-VCPU. PEs (Processing
 * Elements) and CIs (CPU Interfaces) correspond to VCPUs. The distributor and
 * redistributor each have their memory mapped I/O regions. The redistributor is
 * accessible via CPU system registers as well. The distributor and redistributor
 * code lives in GICAll.cpp and GICR3.cpp.
 *
 * The ITS is the interrupt translation service component of the GIC and its
 * presence is optional. It provides MSI support along with routing interrupt
 * sources to specific PEs. The ITS is only accessible via its memory mapped I/O
 * region. When the MMIO handle for the its region is NIL_IOMMMIOHANDLE it's
 * considered to be disabled for the VM. Most of the ITS code lives in GITSAll.cpp.
 *
 * This implementation only targets GICv3. This implementation does not support
 * dual security states, nor does it support exception levels (EL2, EL3). Earlier
 * versions are considered legacy and not important enough to be emulated.
 * GICv4 primarily adds support for virtualizing the GIC and its necessity will be
 * evaluated in the future if/when there is support for nested virtualization on
 * ARMv8 hosts.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_GIC
#include "GICInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/vmcpuset.h>
#include <VBox/vmm/pdmapi.h>        /* PDMR3HasLoadedState */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * GIC interrupt.
 */
typedef struct GICINTR
{
    /** The interrupt group mask (GIC_INTR_GROUP_XXX). */
    uint32_t        fIntrGroupMask;
    /** The interrupt ID. */
    uint16_t        uIntId;
    /** The index of the interrupt in the pending bitmap (UINT16_MAX if none). */
    uint16_t        idxIntr;
    /** The interrupt priority. */
    uint8_t         bPriority;
} GICINTR;
/** Pointer to a GIC interrupt. */
typedef GICINTR *PGICINTR;
/** Pointer to a const GIC interrupt. */
typedef GICINTR const *PCGICINTR;


#ifdef LOG_ENABLED
/**
 * Gets the description of a CPU interface register.
 *
 * @returns The description.
 * @param   u32Reg  The CPU interface register offset.
 */
static const char *gicIccGetRegDescription(uint32_t u32Reg)
{
    switch (u32Reg)
    {
#define GIC_ICC_REG_CASE(a_Reg) case ARMV8_AARCH64_SYSREG_ ## a_Reg: return #a_Reg
        GIC_ICC_REG_CASE(ICC_PMR_EL1);
        GIC_ICC_REG_CASE(ICC_IAR0_EL1);
        GIC_ICC_REG_CASE(ICC_EOIR0_EL1);
        GIC_ICC_REG_CASE(ICC_HPPIR0_EL1);
        GIC_ICC_REG_CASE(ICC_BPR0_EL1);
        GIC_ICC_REG_CASE(ICC_AP0R0_EL1);
        GIC_ICC_REG_CASE(ICC_AP0R1_EL1);
        GIC_ICC_REG_CASE(ICC_AP0R2_EL1);
        GIC_ICC_REG_CASE(ICC_AP0R3_EL1);
        GIC_ICC_REG_CASE(ICC_AP1R0_EL1);
        GIC_ICC_REG_CASE(ICC_AP1R1_EL1);
        GIC_ICC_REG_CASE(ICC_AP1R2_EL1);
        GIC_ICC_REG_CASE(ICC_AP1R3_EL1);
        GIC_ICC_REG_CASE(ICC_DIR_EL1);
        GIC_ICC_REG_CASE(ICC_RPR_EL1);
        GIC_ICC_REG_CASE(ICC_SGI1R_EL1);
        GIC_ICC_REG_CASE(ICC_ASGI1R_EL1);
        GIC_ICC_REG_CASE(ICC_SGI0R_EL1);
        GIC_ICC_REG_CASE(ICC_IAR1_EL1);
        GIC_ICC_REG_CASE(ICC_EOIR1_EL1);
        GIC_ICC_REG_CASE(ICC_HPPIR1_EL1);
        GIC_ICC_REG_CASE(ICC_BPR1_EL1);
        GIC_ICC_REG_CASE(ICC_CTLR_EL1);
        GIC_ICC_REG_CASE(ICC_SRE_EL1);
        GIC_ICC_REG_CASE(ICC_IGRPEN0_EL1);
        GIC_ICC_REG_CASE(ICC_IGRPEN1_EL1);
#undef GIC_ICC_REG_CASE
        default:
            return "<UNKNOWN>";
    }
}


/**
 * Gets the description of a distributor register given it's register offset.
 *
 * @returns The register description.
 * @param   offReg  The distributor register offset.
 */
static const char *gicDistGetRegDescription(uint16_t offReg)
{
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IGROUPRn_OFF_START,     GIC_DIST_REG_IGROUPRn_RANGE_SIZE))     return "GICD_IGROUPRn";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IGROUPRnE_OFF_START,    GIC_DIST_REG_IGROUPRnE_RANGE_SIZE))    return "GICD_IGROUPRnE";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IROUTERn_OFF_START,     GIC_DIST_REG_IROUTERn_RANGE_SIZE))     return "GICD_IROUTERn";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IROUTERnE_OFF_START,    GIC_DIST_REG_IROUTERnE_RANGE_SIZE))    return "GICD_IROUTERnE";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISENABLERn_OFF_START,   GIC_DIST_REG_ISENABLERn_RANGE_SIZE))   return "GICD_ISENABLERn";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISENABLERnE_OFF_START,  GIC_DIST_REG_ISENABLERnE_RANGE_SIZE))  return "GICD_ISENABLERnE";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICENABLERn_OFF_START,   GIC_DIST_REG_ICENABLERn_RANGE_SIZE))   return "GICD_ICENABLERn";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICENABLERnE_OFF_START,  GIC_DIST_REG_ICENABLERnE_RANGE_SIZE))  return "GICD_ICENABLERnE";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISACTIVERn_OFF_START,   GIC_DIST_REG_ISACTIVERn_RANGE_SIZE))   return "GICD_ISACTIVERn";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISACTIVERnE_OFF_START,  GIC_DIST_REG_ISACTIVERnE_RANGE_SIZE))  return "GICD_ISACTIVERnE";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICACTIVERn_OFF_START,   GIC_DIST_REG_ICACTIVERn_RANGE_SIZE))   return "GICD_ICACTIVERn";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICACTIVERnE_OFF_START,  GIC_DIST_REG_ICACTIVERnE_RANGE_SIZE))  return "GICD_ICACTIVERnE";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IPRIORITYRn_OFF_START,  GIC_DIST_REG_IPRIORITYRn_RANGE_SIZE))  return "GICD_IPRIORITYRn";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IPRIORITYRnE_OFF_START, GIC_DIST_REG_IPRIORITYRnE_RANGE_SIZE)) return "GICD_IPRIORITYRnE";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISPENDRn_OFF_START,     GIC_DIST_REG_ISPENDRn_RANGE_SIZE))     return "GICD_ISPENDRn";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISPENDRnE_OFF_START,    GIC_DIST_REG_ISPENDRnE_RANGE_SIZE))    return "GICD_ISPENDRnE";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICPENDRn_OFF_START,     GIC_DIST_REG_ICPENDRn_RANGE_SIZE))     return "GICD_ICPENDRn";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICPENDRnE_OFF_START,    GIC_DIST_REG_ICPENDRnE_RANGE_SIZE))    return "GICD_ICPENDRnE";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICFGRn_OFF_START,       GIC_DIST_REG_ICFGRn_RANGE_SIZE))       return "GICD_ICFGRn";
   if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICFGRnE_OFF_START,      GIC_DIST_REG_ICFGRnE_RANGE_SIZE))      return "GICD_ICFGRnE";
   switch (offReg)
   {
       case GIC_DIST_REG_CTLR_OFF:             return "GICD_CTLR";
       case GIC_DIST_REG_TYPER_OFF:            return "GICD_TYPER";
       case GIC_DIST_REG_STATUSR_OFF:          return "GICD_STATUSR";
       case GIC_DIST_REG_ITARGETSRn_OFF_START: return "GICD_ITARGETSRn";
       case GIC_DIST_REG_IGRPMODRn_OFF_START:  return "GICD_IGRPMODRn";
       case GIC_DIST_REG_NSACRn_OFF_START:     return "GICD_NSACRn";
       case GIC_DIST_REG_SGIR_OFF:             return "GICD_SGIR";
       case GIC_DIST_REG_CPENDSGIRn_OFF_START: return "GICD_CSPENDSGIRn";
       case GIC_DIST_REG_SPENDSGIRn_OFF_START: return "GICD_SPENDSGIRn";
       case GIC_DIST_REG_INMIn_OFF_START:      return "GICD_INMIn";
       case GIC_DIST_REG_PIDR2_OFF:            return "GICD_PIDR2";
       case GIC_DIST_REG_IIDR_OFF:             return "GICD_IIDR";
       case GIC_DIST_REG_TYPER2_OFF:           return "GICD_TYPER2";
       default:
           return "<UNKNOWN>";
   }
}
#endif /* LOG_ENABLED */


/**
 * Packs alternate bits (starting at and including bit 1 to bit 31) from a
 * 32-bit source into a 32-bit value.
 *
 * @returns The 32-bit result with alternate bits from the 32-bit source.
 * @param uSrc      The 32-bit input.
 * @remarks It is intentional that this function doesn't return a 16-bit value
 *          since all callers would explicitly need to typecast it to 32-bit
 *          anyway.
 */
DECL_FORCE_INLINE(uint32_t) gicPackAltBits(uint32_t uSrc)
{
    /** @todo There are faster ways of doing this, but that is for later. */
    uint32_t       uOut = 0;
    unsigned const cBits = sizeof(uSrc) << 3;
    for (unsigned i = 1; i < cBits; i += 2)
        uOut |= ((uSrc >> i) & 1) << (i / 2);
    return uOut;
}


/**
 * Unpacks every bit from a 32-bit source into alternate bit positions (starting at
 * and including 1 to bit 63) into a 64-bit result.
 *
 * @returns The 64-bit result with alternate bits from the 32-bit source.
 * @param uSrc      The 32-bit input.
 */
DECL_FORCE_INLINE(uint64_t) gicUnpackAltBits(uint32_t uSrc)
{
    /** @todo There are faster ways of doing this, but that is for later. */
    uint64_t       uOut  = 0;
    unsigned const cBits = sizeof(uSrc) << 3;
    for (unsigned i = 0; i < cBits; i++)
        uOut |= (uint64_t)((uSrc >> i) & 1) << ((i << 1) + 1);
    return uOut;
}


/**
 * Gets a bit in an (interrupt) bitmap.
 *
 * @returns @c true if the bit was set, @c false if the bit was clear.
 * @param   pu32Bitmap  The bitmap.
 * @param   idxBit      The index of the bit to get (from bit 0).
 *
 * @remarks The bitmap is 32-bit aligned because it's ASSUMED 32-bit would yield
 *          marginally better performance than byte accesses and because all
 *          bitmaps in the GIC thus far are multiples of 32 bits.
 */
DECL_FORCE_INLINE(bool) gicBitmapGetBit(uint32_t const *pu32Bitmap, uint32_t idxBit)
{
    uint32_t const cBitsPerElement = sizeof(uint32_t) << 3;
    uint32_t const idxElement      = idxBit / cBitsPerElement;
    uint8_t const  idxBitInElement = idxBit % cBitsPerElement;
    uint32_t const bmElement       = pu32Bitmap[idxElement];
    AssertCompile(sizeof(bmElement) * 8 == cBitsPerElement);
    return RT_BOOL(bmElement & RT_BIT_32(idxBitInElement));
}


/**
 * Sets or clears a bit in an (interrupt) bitmap if needed.
 *
 * @returns @c true if the bit was updated, @c false otherwise.
 * @param   pu32Bitmap  The bitmap.
 * @param   idxBit      Index of the bit to set (from bit 0).
 * @param   fSet        Whether to set the bit (@c true) or to clear the bit (@c
 *                      false).
 */
DECL_FORCE_INLINE(bool) gicBitmapPutBit(uint32_t *pu32Bitmap, uint32_t idxBit, bool fSet)
{
    uint32_t const cBitsPerElement = sizeof(uint32_t) << 3;
    uint32_t const idxElement      = idxBit / cBitsPerElement;
    uint8_t const  idxBitInElement = idxBit % cBitsPerElement;
    uint32_t       bmElement       = pu32Bitmap[idxElement];
    bool     const fAlreadySet     = RT_BOOL(bmElement & RT_BIT_32(idxBitInElement));
    AssertCompile(sizeof(bmElement) * 8 == cBitsPerElement);
    if (fAlreadySet != fSet)
    {
        bmElement &= ~RT_BIT_32(idxBitInElement);
        bmElement |= ((uint32_t)fSet << idxBitInElement);
        pu32Bitmap[idxElement] = bmElement;
        return true;
    }
    return false;
}


/**
 * Gets the description of a redistributor register given it's register offset.
 *
 * @returns The register description.
 * @param   offReg  The redistributor register offset.
 */
static const char *gicReDistGetRegDescription(uint16_t offReg)
{
    switch (offReg)
    {
        case GIC_REDIST_REG_CTLR_OFF:           return "GICR_CTLR";
        case GIC_REDIST_REG_IIDR_OFF:           return "GICR_IIDR";
        case GIC_REDIST_REG_TYPER_OFF:          return "GICR_TYPER";
        case GIC_REDIST_REG_TYPER_AFFINITY_OFF: return "GICR_TYPER_AFF";
        case GIC_REDIST_REG_STATUSR_OFF:        return "GICR_STATUSR";
        case GIC_REDIST_REG_WAKER_OFF:          return "GICR_WAKER";
        case GIC_REDIST_REG_MPAMIDR_OFF:        return "GICR_MPAMIDR";
        case GIC_REDIST_REG_PARTIDR_OFF:        return "GICR_PARTIDR";
        case GIC_REDIST_REG_SETLPIR_OFF:        return "GICR_SETLPIR";
        case GIC_REDIST_REG_CLRLPIR_OFF:        return "GICR_CLRLPIR";
        case GIC_REDIST_REG_PROPBASER_OFF:      return "GICR_PROPBASER";
        case GIC_REDIST_REG_PENDBASER_OFF:      return "GICR_PENDBASER";
        case GIC_REDIST_REG_INVLPIR_OFF:        return "GICR_INVLPIR";
        case GIC_REDIST_REG_INVALLR_OFF:        return "GICR_INVALLR";
        case GIC_REDIST_REG_SYNCR_OFF:          return "GICR_SYNCR";
        case GIC_REDIST_REG_PIDR2_OFF:          return "GICR_PIDR2";
        default:
            return "<UNKNOWN>";
    }
}


/**
 * Gets the description of an SGI/PPI redistributor register given it's register
 * offset.
 *
 * @returns The register description.
 * @param   offReg  The redistributor register offset.
 */
static const char *gicReDistGetSgiPpiRegDescription(uint16_t offReg)
{
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_IGROUPR0_OFF,          GIC_REDIST_SGI_PPI_REG_IGROUPRnE_RANGE_SIZE))    return "GICR_IGROUPn";
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISENABLER0_OFF,        GIC_REDIST_SGI_PPI_REG_ISENABLERnE_RANGE_SIZE))  return "GICR_ISENABLERn";
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICENABLER0_OFF,        GIC_REDIST_SGI_PPI_REG_ICENABLERnE_RANGE_SIZE))  return "GICR_ICENABLERn";
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISACTIVER0_OFF,        GIC_REDIST_SGI_PPI_REG_ISACTIVERnE_RANGE_SIZE))  return "GICR_ISACTIVERn";
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICACTIVER0_OFF,        GIC_REDIST_SGI_PPI_REG_ICACTIVERnE_RANGE_SIZE))  return "GICR_ICACTIVERn";
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISPENDR0_OFF,          GIC_REDIST_SGI_PPI_REG_ISPENDRnE_RANGE_SIZE))    return "GICR_ISPENDRn";
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICPENDR0_OFF,          GIC_REDIST_SGI_PPI_REG_ICPENDRnE_RANGE_SIZE))    return "GICR_ICPENDRn";
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_IPRIORITYRn_OFF_START, GIC_REDIST_SGI_PPI_REG_IPRIORITYRnE_RANGE_SIZE)) return "GICR_IPREIORITYn";
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICFGR0_OFF,            GIC_REDIST_SGI_PPI_REG_ICFGRnE_RANGE_SIZE))      return "GICR_ICFGRn";
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_INMIR0_OFF,            GIC_REDIST_SGI_PPI_REG_INMIRnE_RANGE_SIZE))      return "GICR_INMIRn";
    switch (offReg)
    {
        case GIC_REDIST_SGI_PPI_REG_NSACR_OFF:      return "GICR_NSACR";
        case GIC_REDIST_SGI_PPI_REG_IGRPMODR0_OFF:  return "GICR_IGRPMODR0";
        case GIC_REDIST_SGI_PPI_REG_IGRPMODR1E_OFF: return "GICR_IGRPMODR1E";
        case GIC_REDIST_SGI_PPI_REG_IGRPMODR2E_OFF: return "GICR_IGRPMODR2E";
        default:
            return "<UNKNOWN>";
    }
}


/**
 * Gets the interrupt ID given a distributor interrupt index.
 *
 * @returns The interrupt ID.
 * @param   idxIntr     The distributor interrupt index.
 * @remarks A distributor interrupt is an interrupt type that belong in the
 *          distributor (e.g. SPIs, extended SPIs).
 */
DECLHIDDEN(uint16_t) gicDistGetIntIdFromIndex(uint16_t idxIntr)
{
    /*
     * Distributor interrupts bits to interrupt ID mapping:
     * +--------------------------------------------------------+
     * | Range (incl) | SGI    | PPI    | SPI      | Ext SPI    |
     * |--------------+--------+--------+----------+------------|
     * | Bit          | 0..15  | 16..31 | 32..1023 | 1024..2047 |
     * | Int Id       | 0..15  | 16..31 | 32..1023 | 4096..5119 |
     * +--------------------------------------------------------+
     */
    uint16_t uIntId;
    /* SGIs, PPIs, SPIs and specials. */
    if (idxIntr < 1024)
        uIntId = idxIntr;
    /* Extended SPIs. */
    else if (idxIntr < 2048)
        uIntId = GIC_INTID_RANGE_EXT_SPI_START + idxIntr - 1024;
    else
    {
        uIntId = 0;
        AssertReleaseMsgFailed(("idxIntr=%u\n", idxIntr));
    }
    Assert(   GIC_IS_INTR_SGI_OR_PPI(uIntId)
           || GIC_IS_INTR_SPI(uIntId)
           || GIC_IS_INTR_SPECIAL(uIntId)
           || GIC_IS_INTR_EXT_SPI(uIntId));
    return uIntId;
}


/**
 * Gets the distributor interrupt index given an interrupt ID.
 *
 * @returns The distributor interrupt index.
 * @param   uIntId  The interrupt ID.
 * @remarks A distributor interrupt is an interrupt type that belong in the
 *          distributor (e.g. SPIs, extended SPIs).
 */
static uint16_t gicDistGetIndexFromIntId(uint16_t uIntId)
{
    uint16_t idxIntr;
    /* SGIs, PPIs, SPIs and specials. */
    if (uIntId <= GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT)
        idxIntr = uIntId;
    /* Extended SPIs. */
    else if (GIC_IS_INTR_EXT_SPI(uIntId))
        idxIntr = 1024 + uIntId - GIC_INTID_RANGE_EXT_SPI_START;
    else
    {
        idxIntr = 0;
        AssertReleaseMsgFailed(("uIntId=%u\n", uIntId));
    }
    Assert(idxIntr < sizeof(GICDEV::IntrPending) * 8);
    return idxIntr;
}


/**
 * Gets the interrupt ID given a redistributor interrupt index.
 *
 * @returns The interrupt ID.
 * @param   idxIntr     The redistributor interrupt index.
 * @remarks A redistributor interrupt is an interrupt type that belong in the
 *          redistributor (e.g. SGIs, PPIs, extended PPIs).
 */
DECLHIDDEN(uint16_t) gicReDistGetIntIdFromIndex(uint16_t idxIntr)
{
    /*
     * Redistributor interrupts bits to interrupt ID mapping:
     * +---------------------------------------------+
     * | Range (incl) | SGI    | PPI    | Ext PPI    |
     * +---------------------------------------------+
     * | Bit          | 0..15  | 16..31 |   32..95   |
     * | Int Id       | 0..15  | 16..31 | 1056..1119 |
     * +---------------------------------------------+
     */
    uint16_t uIntId;
    /* SGIs and PPIs. */
    if (idxIntr < 32)
        uIntId = idxIntr;
    /* Extended PPIs. */
    else if (idxIntr < 96)
        uIntId = GIC_INTID_RANGE_EXT_PPI_START + idxIntr - 32;
    else
    {
        uIntId = 0;
        AssertReleaseMsgFailed(("idxIntr=%u\n", idxIntr));
    }
    Assert(GIC_IS_INTR_SGI_OR_PPI(uIntId) || GIC_IS_INTR_EXT_PPI(uIntId));
    return uIntId;
}


/**
 * Gets the redistributor interrupt index given an interrupt ID.
 *
 * @returns The interrupt ID.
 * @param   uIntId      The interrupt ID.
 * @remarks A redistributor interrupt is an interrupt type that belong in the
 *          redistributor (e.g. SGIs, PPIs, extended PPIs).
 */
static uint16_t gicReDistGetIndexFromIntId(uint16_t uIntId)
{
    /* SGIs and PPIs. */
    uint16_t idxIntr;
    if (uIntId <= GIC_INTID_RANGE_PPI_LAST)
        idxIntr = uIntId;
    /* Extended PPIs. */
    else if (GIC_IS_INTR_EXT_PPI(uIntId))
        idxIntr = 32 + uIntId - GIC_INTID_RANGE_EXT_PPI_START;
    else
    {
        idxIntr = 0;
        AssertReleaseMsgFailed(("uIntId=%u\n", uIntId));
    }
    Assert(idxIntr < sizeof(GICCPU::bmIntrPending) * 8);
    return idxIntr;
}


/**
 * Updates the interrupt force-flag.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fIrq    Flag whether to set the IRQ flag.
 * @param   fFiq    Flag whether to set the FIQ flag.
 */
DECLINLINE(void) gicUpdateInterruptFF(PVMCPUCC pVCpu, bool fIrq, bool fFiq)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} fIrq=%RTbool fFiq=%RTbool\n", pVCpu, pVCpu->idCpu, fIrq, fFiq));
    /** @todo Could be faster if the caller directly supplied set and clear masks. */
    if (fIrq)
    {
        if (!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ))
            VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ);
        else
            fIrq = false;
    }
    else
    {
        if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ))
            VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_IRQ);
    }

    if (fFiq)
    {
        if (!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_FIQ))
            VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_FIQ);
        else
            fFiq = false;
    }
    else
    {
        if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_FIQ))
            VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_FIQ);
    }

    /*
     * If IRQ or FIQ was currently set, wake up the target CPU if we're not on EMT.
     */
    if (fIrq || fFiq)
    {
#if defined(IN_RING0)
# error "Implement me!"
#elif defined(IN_RING3)
        /* IRQ state should be loaded as-is by "LoadExec". Changes can be made from LoadDone. */
        Assert(pVCpu->pVMR3->enmVMState != VMSTATE_LOADING || PDMR3HasLoadedState(pVCpu->pVMR3));
        PVMCC pVM = pVCpu->CTX_SUFF(pVM);
        VMCPUID const idCpu = pVCpu->idCpu;
        if (VMMGetCpuId(pVM) != idCpu)
        {
            Log7Func(("idCpu=%u enmState=%d\n", idCpu, pVCpu->enmState));
            VMR3NotifyCpuFFU(pVCpu->pUVCpu, VMNOTIFYFF_FLAGS_POKE);
        }
#endif
    }
}


/**
 * Gets whether the priority of an enabled, pending interrupt is sufficient to
 * signal the interrupt to the PE.
 *
 * @return @c true if the interrupt can be signalled, @c false otherwise.
 * @param   pGicCpu         The GIC redistributor and CPU interface state.
 * @param   bIntrPriority   The priority of the pending interrupt.
 * @param   fGroup0         Whether the interrupt belongs to interrupt group 0.
 */
DECLINLINE(bool) gicReDistIsSufficientPriority(PCGICCPU pGicCpu, uint8_t bIntrPriority, bool fGroup0)
{
    Assert(bIntrPriority < GIC_IDLE_PRIORITY);

    /*
     * Only allow enabled, pending interrupts with:
     *    - A higher priority than the priority mask.
     *    - A higher priority than the current running priority.
     *    - The preemption settings for the CPU interface.
     * All three of the above conditions must be met to signal the interrupt.
     *
     * When comparing the interrupt priority with the priority mask (threshold) of the
     * CPU interface we must NOT use priority grouping.
     *
     * See ARM GIC spec. 4.8.6 "Priority masking".
     * See ARM GIC spec. 1.2.4 "Additional terms".
     */
    if (bIntrPriority < pGicCpu->bIntrPriorityMask)
    {
        uint8_t const bRunningPriority = pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority];
        if (bRunningPriority == GIC_IDLE_PRIORITY)
            return true;

        /*
         * The group priority of the pending interrupt must be higher than that of the running priority.
         * The number of bits for the group priority depends on the binary point registers.
         * We mask the sub-priority bits and only compare the group priority.
         *
         * When the binary point registers indicates no preemption, we must allow interrupts that have
         * a higher priority than idle. Hence, the use of two different masks below.
         *
         * See ARM GIC spec. 4.8.3 "Priority grouping".
         * See ARM GIC spec. 4.8.5 "Preemption".
         */
        Assert(pGicCpu->bBinaryPtGroup1 > 0);   /* Since we don't support dual security states. */
        static uint8_t const s_afGroup0PriorityMasks[8]  = { 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0x00 };
        static uint8_t const s_afGroup1PriorityMasks[8]  = { 0x00, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80 };
        uint8_t const fGroupMask = (fGroup0 || (pGicCpu->uIccCtlr & ARMV8_ICC_CTLR_EL1_AARCH64_CBPR))
                                 ? s_afGroup0PriorityMasks[pGicCpu->bBinaryPtGroup0 & 7]
                                 : s_afGroup1PriorityMasks[pGicCpu->bBinaryPtGroup1 & 7];
        uint8_t const bRunningGroupPriority = bRunningPriority & fGroupMask;
        uint8_t const bIntrGroupPriority    = bIntrPriority    & fGroupMask;
        if (bIntrGroupPriority < bRunningGroupPriority)
            return true;
    }
    return false;
}


/**
 * Gets the distributor's pending-interrupt bitmap for a given 32-bit index.
 * This is typically used for guest register reads.
 *
 * @returns The pending interrupt bitmap.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The 32-bit index of the pending-interrupt bitmap.
 */
DECL_FORCE_INLINE(uint32_t) gicDistGetPendingIntrAt32(PCGICDEV pGicDev, uint16_t idxReg)
{
    Assert(idxReg < RT_ELEMENTS(pGicDev->IntrPending.au32));
    AssertCompile(RT_ELEMENTS(pGicDev->IntrConfig.au32) == RT_ELEMENTS(pGicDev->IntrPending.au32));
    AssertCompile(sizeof(pGicDev->IntrConfig) == sizeof(pGicDev->IntrPending));
    uint32_t const bmLevelPending = pGicDev->IntrLevel.au32[idxReg]   & ~pGicDev->IntrConfig.au32[idxReg];
    uint32_t const bmIntrPending  = pGicDev->IntrPending.au32[idxReg] | bmLevelPending;
    return bmIntrPending;
}


/**
 * Gets the distributor's pending-interrupt bitmap for a given 64-bit index.
 * This is typically used while collating interrupts.
 *
 * @returns The pending interrupt bitmap.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The 64-bit index of the pending-interrupt bitmap.
 */
DECL_FORCE_INLINE(uint64_t) gicDistGetPendingIntrAt64(PCGICDEV pGicDev, uint16_t idxReg)
{
    Assert(idxReg < RT_ELEMENTS(pGicDev->IntrPending.au64));
    AssertCompile(RT_ELEMENTS(pGicDev->IntrConfig.au64) == RT_ELEMENTS(pGicDev->IntrPending.au64));
    AssertCompile(sizeof(pGicDev->IntrConfig) == sizeof(pGicDev->IntrPending));
    uint64_t const bmLevelPending = pGicDev->IntrLevel.au64[idxReg]   & ~pGicDev->IntrConfig.au64[idxReg];
    uint64_t const bmIntrPending  = pGicDev->IntrPending.au64[idxReg] | bmLevelPending;
    return bmIntrPending;
}


/**
 * Gets the redistributor's pending-interrupt bitmap for a given index.
 *
 * @return  The pending interrupt bitmap.
 * @param   pGicCpu     The GIC redistributor and CPU interface state.
 * @param   idxReg      The index of the pending-interrupt bitmap.
 */
DECL_FORCE_INLINE(uint32_t) gicReDistGetPendingIntrAt(PCGICCPU pGicCpu, uint16_t idxReg)
{
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrPending));
    AssertCompile(RT_ELEMENTS(pGicCpu->bmIntrConfig) == RT_ELEMENTS(pGicCpu->bmIntrPending));
    AssertCompile(sizeof(pGicCpu->bmIntrConfig) == sizeof(pGicCpu->bmIntrPending));
    uint32_t const bmLevelPending = pGicCpu->bmIntrLevel[idxReg]   & ~pGicCpu->bmIntrConfig[idxReg];
    uint32_t const bmIntrPending  = pGicCpu->bmIntrPending[idxReg] | bmLevelPending;
    return bmIntrPending;
}


/**
 * Get the highest priority pending interrupt from the redistributor.
 *
 * @returns The interrupt ID or GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT if no
 *          interrupts are pending or not in a state to be signalled.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fIntrGroupMask  The interrupt groups to consider.
 * @param   pIntr           Where to store the pending interrupt. Optional, can be NULL.
 */
static uint16_t gicReDistGetHighestPriorityPendingIntr(PCVMCPUCC pVCpu, uint32_t fIntrGroupMask, PGICINTR pIntr)
{
    PCGICCPU pGicCpu   = VMCPU_TO_GICCPU(pVCpu);
    uint16_t idxIntr   = UINT16_MAX;
    uint16_t uIntId    = GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT;
    uint8_t  bPriority = GIC_IDLE_PRIORITY;
    uint32_t fIntrGrp  = 0;

    /* SGIs, PPIs and extended PPIs. */
    {
        uint16_t idxHighest = UINT16_MAX;
        for (uint16_t i = 0; i < RT_ELEMENTS(pGicCpu->bmIntrPending); i++)
        {
            /* Collect interrupts that are pending, enabled and inactive. */
            uint32_t const bmIntrPending = gicReDistGetPendingIntrAt(pGicCpu, i);
            uint32_t       bmIntr        = (bmIntrPending & pGicCpu->bmIntrEnabled[i]) & ~pGicCpu->bmIntrActive[i];

            /* Discard interrupts if the group they belong to is not requested. */
            if (!(fIntrGroupMask & GIC_INTR_GROUP_0))
                bmIntr &= pGicCpu->bmIntrGroup[i];
            if (!(fIntrGroupMask & (GIC_INTR_GROUP_1NS | GIC_INTR_GROUP_1S)))
                bmIntr &= ~pGicCpu->bmIntrGroup[i];

            /* Among the collected interrupts, pick the one with the highest, non-idle priority. */
            uint16_t idxIntrInElement = 0;
            while (bmIntr)
            {
                if (bmIntr & RT_BIT_32(0))
                {
                    uint16_t const cIntrsPerElement = sizeof(bmIntr) * 8;
                    uint16_t const idxPending       = i * cIntrsPerElement + idxIntrInElement;
                    Assert(idxPending < RT_ELEMENTS(pGicCpu->abIntrPriority));
                    if (pGicCpu->abIntrPriority[idxPending] < bPriority)
                    {
                        idxHighest = idxPending;
                        bPriority  = pGicCpu->abIntrPriority[idxPending];
                    }
                }
                bmIntr >>= 1;
                ++idxIntrInElement;
            }
        }
        if (idxHighest != UINT16_MAX)
        {
            uint16_t const cIntrsPerElement = sizeof(pGicCpu->bmIntrGroup[0]) * 8;
            uIntId   = gicReDistGetIntIdFromIndex(idxHighest);
            idxIntr  = idxHighest;
            fIntrGrp = RT_BOOL(  pGicCpu->bmIntrGroup[idxHighest / cIntrsPerElement]
                               & RT_BIT_32(idxHighest % cIntrsPerElement))
                     ? (GIC_INTR_GROUP_1NS | GIC_INTR_GROUP_1S)
                     : GIC_INTR_GROUP_0;
            Assert(   GIC_IS_INTR_SGI_OR_PPI(uIntId)
                   || GIC_IS_INTR_EXT_PPI(uIntId));
        }
    }

    /* LPIs. */
    {
        PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
        PCGICDEV   pGicDev = PDMDEVINS_2_DATA(pDevIns, PCGICDEV);
        if (pGicDev->fEnableLpis)
        {
            uint16_t idxHighest = UINT16_MAX;
            for (uint16_t i = 0; i < RT_ELEMENTS(pGicCpu->LpiPending.au64); i++)
            {
                uint64_t bmLpiPending    = pGicCpu->LpiPending.au64[i];
                uint16_t idxLpiInElement = 0;
                while (bmLpiPending)
                {
                    if (bmLpiPending & RT_BIT_64(0))
                    {
                        /* We don't support dual security states, hence priority is -not- shifted */
                        uint16_t const cIntrsPerElement = sizeof(bmLpiPending) * 8;
                        uint16_t const idxLpi           = i * cIntrsPerElement + idxLpiInElement;
                        Assert(idxLpi < RT_ELEMENTS(pGicDev->abLpiConfig));
                        uint8_t const  bLpiPriority    = pGicDev->abLpiConfig[idxLpi] & GIC_BF_LPI_CTE_PRIORITY_MASK;
                        bool const     fLpiEnabled     = pGicDev->abLpiConfig[idxLpi] & GIC_BF_LPI_CTE_ENABLE_MASK;
                        if (   fLpiEnabled
                            && bLpiPriority < bPriority)
                        {
                            bPriority  = bLpiPriority;
                            idxHighest = idxLpi;
                        }
                    }
                    bmLpiPending >>= 1;
                    ++idxLpiInElement;
                }
            }
            if (idxHighest != UINT16_MAX)
            {
                uIntId   = idxHighest + GIC_INTID_RANGE_LPI_START;
                idxIntr  = idxHighest;
                fIntrGrp = GIC_INTR_GROUP_1NS;
                Assert(GIC_IS_INTR_LPI(uIntId));
            }
        }
    }

    /* Ensure that if no interrupt is pending, the idle priority is returned. */
    Assert(uIntId != GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT || bPriority == GIC_IDLE_PRIORITY);

    /* Ensure that if no interrupt is pending, the index is invalid. */
    Assert(uIntId != GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT || idxIntr == UINT16_MAX);

    /* Populate the pending interrupt data and we're done. */
    if (pIntr)
    {
        pIntr->fIntrGroupMask = fIntrGrp;
        pIntr->uIntId         = uIntId;
        pIntr->idxIntr        = idxIntr;
        pIntr->bPriority      = bPriority;
    }

    LogFlowFunc(("uIntId=%u [idxIntr=%u bPriority=%u fIntrGroupMask=%#RX32]\n", uIntId, idxIntr, bPriority, fIntrGroupMask));
    return uIntId;
}


/**
 * Get the highest priority pending interrupt from the distributor that could be
 * routed to the given VCPU.
 *
 * @returns The interrupt ID or GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT if no
 *          interrupts are pending or not in a state to be signalled.
 * @param   pGicDev         The GIC distributor state.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fIntrGroupMask  The interrupt groups to consider.
 * @param   pIntr           Where to store the pending interrupt. Optional, can be NULL.
 */
static uint16_t gicDistGetHighestPriorityPendingIntr(PCGICDEV pGicDev, PCVMCPUCC pVCpu, uint32_t fIntrGroupMask, PGICINTR pIntr)
{
    Assert(fIntrGroupMask);

    uint16_t idxIntr    = UINT16_MAX;
    uint16_t uIntId     = GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT;
    uint8_t  bPriority  = GIC_IDLE_PRIORITY;
    uint32_t fIntrGrp   = 0;

    /* SPIs and extended SPIs. */
    for (uint16_t i = 0; i < RT_ELEMENTS(pGicDev->IntrPending.au64); i++)
    {
        /* Collect interrupts that are pending, enabled and inactive. */
        uint64_t const bmIntrPending = gicDistGetPendingIntrAt64(pGicDev, i);
        uint64_t       bmIntr        = (bmIntrPending & pGicDev->IntrEnabled.au64[i]) & ~pGicDev->IntrActive.au64[i];

        /* Discard interrupts if the group they belong to is not requested. */
        if (!(fIntrGroupMask & GIC_INTR_GROUP_0))
            bmIntr &= pGicDev->IntrGroup.au64[i];
        if (!(fIntrGroupMask & (GIC_INTR_GROUP_1NS | GIC_INTR_GROUP_1S)))
            bmIntr &= ~pGicDev->IntrGroup.au64[i];

        /* Among the collected interrupts, pick the highest priority pending interrupt that can be routed to the target VCPU. */
        uint16_t idxIntrInElement = 0;
        while (bmIntr)
        {
            if (bmIntr & RT_BIT_64(0))
            {
                uint16_t const cIntrsPerElement = sizeof(bmIntr) * 8;
                uint16_t const idxPending       = i * cIntrsPerElement + idxIntrInElement;
                Assert(idxPending < RT_ELEMENTS(pGicDev->abIntrPriority));
                if (   pGicDev->abIntrPriority[idxPending] < bPriority
                    && pGicDev->au32IntrRouting[idxPending] == pVCpu->idCpu)
                {
                    idxIntr   = idxPending;
                    bPriority = pGicDev->abIntrPriority[idxPending];
                }
            }
            bmIntr >>= 1;
            ++idxIntrInElement;
        }
    }
    if (idxIntr != UINT16_MAX)
    {
        uint16_t const cIntrsPerElement = sizeof(pGicDev->IntrGroup.au64[0]) * 8;
        uIntId   = gicDistGetIntIdFromIndex(idxIntr);
        fIntrGrp = RT_BOOL(  pGicDev->IntrGroup.au64[idxIntr / cIntrsPerElement]
                           & RT_BIT_64(idxIntr % cIntrsPerElement))
                 ? (GIC_INTR_GROUP_1NS | GIC_INTR_GROUP_1S)
                 : GIC_INTR_GROUP_0;
        Assert(   GIC_IS_INTR_SPI(uIntId)
               || GIC_IS_INTR_EXT_SPI(uIntId));
    }

    /* Ensure that if no interrupt is pending, the idle priority is returned. */
    Assert(uIntId != GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT || bPriority == GIC_IDLE_PRIORITY);

    /* Ensure that if no interrupt is pending, the index is invalid. */
    Assert(uIntId != GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT || idxIntr == UINT16_MAX);

    /* Populate the pending interrupt data and we're done. */
    if (pIntr)
    {
        pIntr->fIntrGroupMask = fIntrGrp;
        pIntr->uIntId         = uIntId;
        pIntr->idxIntr        = idxIntr;
        pIntr->bPriority      = bPriority;
    }

    LogFlowFunc(("uIntId=%u [idxIntr=%u bPriority=%u fIntrGroupMask=%#RX32]\n", uIntId, idxIntr, bPriority, fIntrGroupMask));
    return uIntId;
}


/**
 * Updates the internal IRQ state of the redistributor and sets or clears the
 * appropriate force action flags.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static VBOXSTRICTRC gicReDistUpdateIrqState(PVMCPUCC pVCpu)
{
    LogFlowFunc(("\n"));
    PCGICCPU   pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PCGICDEV   pGicDev = PDMDEVINS_2_DATA(pDevIns, PCGICDEV);

    /* Redistributor. */
    bool fIrq = false;
    bool fFiq = false;
    {
        uint32_t const fIntrGroupMask = pGicCpu->fIntrGroupMask;
        if (   fIntrGroupMask
            && pGicCpu->bIntrPriorityMask
            && pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority])
        {
            GICINTR Intr;
            gicReDistGetHighestPriorityPendingIntr(pVCpu, fIntrGroupMask, &Intr);
            if (Intr.uIntId != GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT)
            {
                bool const fGroup0 = RT_BOOL(fIntrGroupMask & GIC_INTR_GROUP_0);
                bool const fSufficientPriority = gicReDistIsSufficientPriority(pGicCpu, Intr.bPriority, fGroup0);
                if (fSufficientPriority)
                {
                    fFiq |= RT_BOOL(fIntrGroupMask & Intr.fIntrGroupMask & GIC_INTR_GROUP_0);
                    fIrq |= RT_BOOL(fIntrGroupMask & Intr.fIntrGroupMask & (GIC_INTR_GROUP_1S | GIC_INTR_GROUP_1NS));
                }
            }
        }
    }

    /* Distributor. */
    {
        uint32_t const fIntrGroupMask = pGicDev->fIntrGroupMask;
        if (fIntrGroupMask)
        {
            GICINTR Intr;
            gicDistGetHighestPriorityPendingIntr(pGicDev, pVCpu, fIntrGroupMask, &Intr);
            if (Intr.uIntId != GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT)
            {
                bool const fGroup0 = RT_BOOL(fIntrGroupMask & GIC_INTR_GROUP_0);
                bool const fSufficientPriority = gicReDistIsSufficientPriority(pGicCpu, Intr.bPriority, fGroup0);
                if (fSufficientPriority)
                {
                    fFiq |= RT_BOOL(fIntrGroupMask & Intr.fIntrGroupMask & GIC_INTR_GROUP_0);
                    fIrq |= RT_BOOL(fIntrGroupMask & Intr.fIntrGroupMask & (GIC_INTR_GROUP_1S | GIC_INTR_GROUP_1NS));
                }
            }
        }
    }

    gicUpdateInterruptFF(pVCpu, fIrq, fFiq);
    return VINF_SUCCESS;
}


/**
 * Updates the internal IRQ state of the distributor and sets or clears the
 * appropriate force action flags.
 *
 * @returns Strict VBox status code.
 * @param   pVM     The cross context VM state.
 */
static VBOXSTRICTRC gicDistUpdateIrqState(PCVMCC pVM)
{
    LogFlowFunc(("\n"));
    VMCC_FOR_EACH_VMCPU(pVM)
        gicReDistUpdateIrqState(pVCpu);
    VMCC_FOR_EACH_VMCPU_END(pVM);
    return VINF_SUCCESS;
}


/**
 * Reads the LPI config table from guest memory.
 *
 * @param   pDevIns     The device instance.
 */
DECLHIDDEN(void) gicDistReadLpiConfigTableFromMem(PPDMDEVINS pDevIns)
{
    Assert(GIC_CRIT_SECT_IS_OWNER(pDevIns));

    PGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fEnableLpis);
    LogFlowFunc(("\n"));

    /* Check if the guest is disabling LPIs by setting the number of LPI INTID bits below the minimum required bits. */
    uint8_t const cIdBits = RT_BF_GET(pGicDev->uLpiConfigBaseReg.u, GIC_BF_REDIST_REG_PROPBASER_ID_BITS) + 1;
    if (cIdBits < GIC_LPI_ID_BITS_MIN)
    {
        RT_ZERO(pGicDev->abLpiConfig);
        return;
    }

    /* Copy the LPI config table from guest memory to our cache. */
    Assert(UINT32_C(2) << pGicDev->uMaxLpi == RT_ELEMENTS(pGicDev->abLpiConfig));
    RTGCPHYS const GCPhysLpiConfigTable = pGicDev->uLpiConfigBaseReg.u & GIC_BF_REDIST_REG_PROPBASER_PHYS_ADDR_MASK;
    uint32_t const cbLpiConfigTable     = sizeof(pGicDev->abLpiConfig);

    int const rc = PDMDevHlpPhysReadMeta(pDevIns, GCPhysLpiConfigTable, (void *)&pGicDev->abLpiConfig[0], cbLpiConfigTable);
    if (RT_SUCCESS(rc))
        return;

    AssertMsgFailed(("Failed to read the LPI config table at %#RGp rc=%Rrc\n", GCPhysLpiConfigTable, rc));

    /* If we failed to read the table for whatever reason, clear our LPI config table cache. */
    RT_ZERO(pGicDev->abLpiConfig);
}


/**
 * Reads the LPI pending table from guest memory if necessary.
 *
 * @param   pDevIns     The device instance.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static void gicReDistReadLpiPendingTableFromMem(PPDMDEVINS pDevIns, PVMCPU pVCpu)
{
    Assert(GIC_CRIT_SECT_IS_OWNER(pDevIns));

    PGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fEnableLpis);
    LogFlowFunc(("\n"));

    PGICCPU    pGicCpu   = VMCPU_TO_GICCPU(pVCpu);
    bool const fIsZeroed = RT_BF_GET(pGicDev->uLpiPendingBaseReg.u, GIC_BF_REDIST_REG_PENDBASER_PTZ);
    if (!fIsZeroed)
    {
        /* Read the LPI pending bitmap from guest memory to our cache. */
        RTGCPHYS const GCPhysLpiPendingTable = (pGicDev->uLpiPendingBaseReg.u & GIC_BF_REDIST_REG_PENDBASER_PHYS_ADDR_MASK)
                                             + GIC_INTID_RANGE_LPI_START;  /* Skip first 1KB (since LPI INTIDs start at 8192). */
        uint32_t const cbLpiPendingTable     = sizeof(pGicCpu->LpiPending);
        int const rc = PDMDevHlpPhysReadMeta(pDevIns, GCPhysLpiPendingTable, (void *)&pGicCpu->LpiPending.au64[0],
                                             cbLpiPendingTable);
        if (RT_SUCCESS(rc))
            return;
        AssertMsgFailed(("Failed to read the LPI pending table at %#RGp rc=%Rrc\n", GCPhysLpiPendingTable, rc));
    }

    /* If the guest indicates the table is zero'ed OR if we failed to read the table for whatever reason,
       clear our LPI pending cache. */
    RT_ZERO(pGicCpu->LpiPending);
}


/**
 * Reads the distributor's interrupt routing register (GICD_IROUTER).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_IROUTER range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicDistReadIntrRoutingReg(PCGICDEV pGicDev, uint16_t idxReg, uint32_t *puValue)
{
    /* When affinity routing is disabled, reads return 0. */
    Assert(pGicDev->fAffRouting);

    /* Hardware does not map the first 32 registers (corresponding to SGIs and PPIs). */
    idxReg += GIC_INTID_RANGE_SPI_START;
    AssertReturn(idxReg < RT_ELEMENTS(pGicDev->au32IntrRouting), VERR_BUFFER_OVERFLOW);
    Assert(idxReg < sizeof(pGicDev->IntrRoutingMode.au32) * 8);
    if (!(idxReg % 2))
    {
        /* Lower 32-bits. */
        uint8_t const fIrm = gicBitmapGetBit(&pGicDev->IntrRoutingMode.au32[0], idxReg);
        *puValue = GIC_DIST_REG_IROUTERn_SET(fIrm, pGicDev->au32IntrRouting[idxReg]);
    }
    else
    {
        /* Upper 32-bits. */
        *puValue = pGicDev->au32IntrRouting[idxReg] >> 24;
    }

    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, *puValue));
    return VINF_SUCCESS;
}


/**
 * Writes the distributor's interrupt routing register (GICD_IROUTER).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_IROUTER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicDistWriteIntrRoutingReg(PGICDEV pGicDev, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is disabled, writes are ignored. */
    Assert(pGicDev->fAffRouting);

    AssertMsgReturn(idxReg < RT_ELEMENTS(pGicDev->au32IntrRouting), ("idxReg=%u\n", idxReg), VERR_BUFFER_OVERFLOW);
    Assert(idxReg < sizeof(pGicDev->IntrRoutingMode) * 8);
    if (!(idxReg % 2))
    {
        /* Lower 32-bits. */
        bool const fIrm = GIC_DIST_REG_IROUTERn_IRM_GET(uValue);
        if (fIrm)
            gicBitmapPutBit(&pGicDev->IntrRoutingMode.au32[0], idxReg, true /* fSet */);
        else
            gicBitmapPutBit(&pGicDev->IntrRoutingMode.au32[0], idxReg, false /* fSet */);
        uint32_t const fAff3 = pGicDev->au32IntrRouting[idxReg] & 0xff000000;
        pGicDev->au32IntrRouting[idxReg] = fAff3 | (uValue & 0x00ffffff);
    }
    else
    {
        /* Upper 32-bits. */
        uint32_t const fAffOthers = pGicDev->au32IntrRouting[idxReg] & 0x00ffffff;
        pGicDev->au32IntrRouting[idxReg] = (uValue << 24) | fAffOthers;
    }

    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->au32IntrRouting[idxReg]));
    return VINF_SUCCESS;
}


/**
 * Reads the distributor's interrupt (set/clear) enable register (GICD_ISENABLER and
 * GICD_ICENABLER).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ISENABLER and
 *                      GICD_ICENABLER range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicDistReadIntrEnableReg(PGICDEV pGicDev, uint16_t idxReg, uint32_t *puValue)
{
    Assert(idxReg < RT_ELEMENTS(pGicDev->IntrEnabled.au32));
    *puValue = pGicDev->IntrEnabled.au32[idxReg];
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicDev->IntrEnabled.au32[idxReg]));
    return VINF_SUCCESS;
}


/**
 * Writes the distributor's interrupt set-enable register (GICD_ISENABLER).
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ISENABLER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicDistWriteIntrSetEnableReg(PVM pVM, PGICDEV pGicDev, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is enabled, writes to SGIs and PPIs are ignored. */
    Assert(pGicDev->fAffRouting);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->IntrEnabled.au32));
        pGicDev->IntrEnabled.au32[idxReg] |= uValue;
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->IntrEnabled.au32[idxReg]));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    return gicDistUpdateIrqState(pVM);
}


/**
 * Writes the distributor's interrupt clear-enable register (GICD_ICENABLER).
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ICENABLER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicDistWriteIntrClearEnableReg(PVM pVM, PGICDEV pGicDev, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is enabled, writes to SGIs and PPIs are ignored. */
    Assert(pGicDev->fAffRouting);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->IntrEnabled.au32));
        pGicDev->IntrEnabled.au32[idxReg] &= ~uValue;
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->IntrEnabled.au32[idxReg]));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    return gicDistUpdateIrqState(pVM);
}


/**
 * Reads the distributor's interrupt active register (GICD_ISACTIVER and
 * GICD_ICACTIVER).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ISACTIVER and
 *                      GICD_ICACTIVER range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicDistReadIntrActiveReg(PGICDEV pGicDev, uint16_t idxReg, uint32_t *puValue)
{
    Assert(idxReg < RT_ELEMENTS(pGicDev->IntrActive.au32));
    *puValue = pGicDev->IntrActive.au32[idxReg];
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicDev->IntrActive.au32[idxReg]));
    return VINF_SUCCESS;
}


/**
 * Writes the distributor's interrupt set-active register (GICD_ISACTIVER).
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ISACTIVER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicDistWriteIntrSetActiveReg(PVM pVM, PGICDEV pGicDev, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is enabled, writes to SGIs and PPIs are ignored. */
    Assert(pGicDev->fAffRouting);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->IntrActive.au32));
        pGicDev->IntrActive.au32[idxReg] |= uValue;
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->IntrActive.au32[idxReg]));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    return gicDistUpdateIrqState(pVM);
}


/**
 * Writes the distributor's interrupt clear-active register (GICD_ICACTIVER).
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ICACTIVER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicDistWriteIntrClearActiveReg(PVM pVM, PGICDEV pGicDev, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is enabled, writes to SGIs and PPIs are ignored. */
    Assert(pGicDev->fAffRouting);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->IntrActive.au32));
        pGicDev->IntrActive.au32[idxReg] &= ~uValue;
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->IntrActive.au32[idxReg]));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    return gicDistUpdateIrqState(pVM);
}


/**
 * Reads the distributor's interrupt priority register (GICD_IPRIORITYR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_IPRIORITY range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicDistReadIntrPriorityReg(PGICDEV pGicDev, uint16_t idxReg, uint32_t *puValue)
{
    /* When affinity routing is enabled, reads to registers 0..7 (pertaining to SGIs and PPIs) return 0. */
    Assert(pGicDev->fAffRouting);
    Assert(idxReg < RT_ELEMENTS(pGicDev->abIntrPriority) / sizeof(uint32_t));
    Assert(idxReg != 255);
    if (idxReg > 7)
    {
        uint16_t const idxPriority = idxReg * sizeof(uint32_t);
        AssertReturn(idxPriority <= RT_ELEMENTS(pGicDev->abIntrPriority) - sizeof(uint32_t), VERR_BUFFER_OVERFLOW);
        AssertCompile(sizeof(*puValue) == sizeof(uint32_t));
        *puValue = *(uint32_t *)&pGicDev->abIntrPriority[idxPriority];
    }
    else
    {
        AssertReleaseMsgFailed(("Unexpected (but not illegal) read to SGI/PPI register in distributor\n"));
        *puValue = 0;
    }
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, *puValue));
    return VINF_SUCCESS;
}


/**
 * Writes the distributor's interrupt priority register (GICD_IPRIORITYR).
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_IPRIORITY range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicDistWriteIntrPriorityReg(PVM pVM, PGICDEV pGicDev, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is enabled, writes to registers 0..7 are ignored. */
    Assert(pGicDev->fAffRouting);
    Assert(idxReg < RT_ELEMENTS(pGicDev->abIntrPriority) / sizeof(uint32_t));
    Assert(idxReg != 255);
    if (idxReg > 7)
    {
        uint16_t const idxPriority = idxReg * sizeof(uint32_t);
        AssertReturn(idxPriority <= RT_ELEMENTS(pGicDev->abIntrPriority) - sizeof(uint32_t), VERR_BUFFER_OVERFLOW);
        AssertCompile(sizeof(uValue) == sizeof(uint32_t));

        /*
         * We don't support dual-security states and thus this is a non-secure write to
         * the interrupt priority.
         *
         * For non-secure writes:
         *   - For group 0 interrupts and secure group 1 interrupts, are permitted to be modified.
         *   - For non-secure group 1 interrupts, priority shifted left by 1.
         *
         * See ARM GIC spec. 4.8.7 "Software accesses of interrupt priority".
         * See ARM GIC spec. 12.9.4 "GICD_CTLR, Distributor Control Register".
         */
        for (uint8_t i = 0; i < sizeof(uint32_t); i++)
        {
            uint8_t const cShift    = i << 3;
            uint8_t const uPriority = uValue >> cShift;
            bool const    fGroup1   = gicBitmapGetBit(&pGicDev->IntrGroup.au32[0], idxPriority + i);
            if (fGroup1)
            {
                /*
                 * Sigh... this horrible hack is required to fix the Windows 11 (24H2) guest boot up hang
                 * as Windows sets the priority of PCI interrupts to 0 which would sometimes incorrectly
                 * preempt the VCPU timer interrupt (which uses a higher priority of 32). While, setting
                 * bit 7 (which seems to  be the right thing to do) fixes Windows 11 it breaks Fedora
                 * guests, see @bugref{10877#c45}.
                 */
                /** @todo Try to fix this properly later... */
                if (uPriority != 0)
                    pGicDev->abIntrPriority[idxPriority + i] = uPriority << 1;
                else
                    pGicDev->abIntrPriority[idxPriority + i] = RT_BIT(7) | (uPriority >> 1);
            }
            else
            {
                /*
                 * Since we don't support dual-security states, non-secure accesses are permitted to
                 * modify registers that group 0 interrupts.
                 */
                pGicDev->abIntrPriority[idxPriority + i] = uPriority;
            }
        }
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, *(uint32_t *)&pGicDev->abIntrPriority[idxPriority]));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    return gicDistUpdateIrqState(pVM);
}


/**
 * Reads the distributor's interrupt pending register (GICD_ISPENDR and
 * GICD_ICPENDR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ISPENDR and
 *                      GICD_ICPENDR range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicDistReadIntrPendingReg(PGICDEV pGicDev, uint16_t idxReg, uint32_t *puValue)
{
    /* When affinity routing is enabled, reads for SGIs and PPIs return 0. */
    Assert(pGicDev->fAffRouting);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->IntrPending.au32));
        *puValue = gicDistGetPendingIntrAt32(pGicDev, idxReg);
    }
    else
    {
        AssertReleaseMsgFailed(("Unexpected (but not illegal) read to SGI/PPI register in distributor\n"));
        *puValue = 0;
    }
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicDev->IntrPending.au32[idxReg]));
    return VINF_SUCCESS;
}


/**
 * Write's the distributor's interrupt set-pending register (GICD_ISPENDR).
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ISPENDR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicDistWriteIntrSetPendingReg(PVMCC pVM, PGICDEV pGicDev, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is enabled, writes to SGIs and PPIs are ignored. */
    Assert(pGicDev->fAffRouting);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->IntrPending.au32));
        pGicDev->IntrPending.au32[idxReg] |= uValue;
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->IntrPending.au32[idxReg]));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    return gicDistUpdateIrqState(pVM);
}


/**
 * Write's the distributor's interrupt clear-pending register (GICD_ICPENDR).
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ICPENDR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicDistWriteIntrClearPendingReg(PVMCC pVM, PGICDEV pGicDev, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is enabled, writes to SGIs and PPIs are ignored. */
    Assert(pGicDev->fAffRouting);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->IntrPending.au32));
        pGicDev->IntrPending.au32[idxReg] &= ~uValue;
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->IntrPending.au32[idxReg]));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    return gicDistUpdateIrqState(pVM);
}


/**
 * Reads the distributor's interrupt config register (GICD_ICFGR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ICFGR range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicDistReadIntrConfigReg(PCGICDEV pGicDev, uint16_t idxReg, uint32_t *puValue)
{
    /* When affinity routing is enabled SGIs and PPIs, reads to SGIs and PPIs return 0. */
    Assert(pGicDev->fAffRouting);
    if (idxReg >= 2)
    {
        Assert(idxReg / 2 < RT_ELEMENTS(pGicDev->IntrConfig.au32));
        uint64_t const bmIntrConfig = gicUnpackAltBits(pGicDev->IntrConfig.au32[idxReg / 2]);
        *puValue = bmIntrConfig >> (32 * (idxReg % 2));
        LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, *puValue));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) read to SGI/PPI register in distributor\n"));
    return VINF_SUCCESS;
}


/**
 * Writes the distributor's interrupt config register (GICD_ICFGR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ICFGR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicDistWriteIntrConfigReg(PGICDEV pGicDev, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is enabled SGIs and PPIs, writes to SGIs and PPIs are ignored. */
    Assert(pGicDev->fAffRouting);
    if (idxReg >= 2)
    {
        /* Only the lower or higher 16-bits of the 32-bits should be updated, retaining other bits. */
        Assert(idxReg / 2 < RT_ELEMENTS(pGicDev->IntrConfig.au32));
        static uint32_t const s_afRetainMasks[] = { 0xffff0000, 0x0000ffff };
        uint8_t const  idxUpdate = idxReg % 2;
        uint32_t const fUpdate   = gicPackAltBits(uValue & 0xaaaaaaaa) << (16 * idxUpdate);
        uint32_t const fRetain   = pGicDev->IntrConfig.au32[idxReg / 2] & s_afRetainMasks[idxUpdate];
        pGicDev->IntrConfig.au32[idxReg / 2] = fUpdate | fRetain;
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->IntrConfig.au32[idxReg / 2]));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    return VINF_SUCCESS;
}


/**
 * Reads the distributor's interrupt config register (GICD_IGROUPR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_IGROUPR range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicDistReadIntrGroupReg(PGICDEV pGicDev, uint16_t idxReg, uint32_t *puValue)
{
    /* When affinity routing is enabled, reads to SGIs and PPIs return 0. */
    Assert(pGicDev->fAffRouting);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->IntrGroup.au32));
        *puValue = pGicDev->IntrGroup.au32[idxReg];
        LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, *puValue));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) read to SGI/PPI register in distributor\n"));
    return VINF_SUCCESS;
}


/**
 * Writes the distributor's interrupt config register (GICD_ICFGR).
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_ICFGR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicDistWriteIntrGroupReg(PCVM pVM, PGICDEV pGicDev, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is enabled, writes to SGIs and PPIs are ignored. */
    Assert(pGicDev->fAffRouting);
    if (idxReg > 0)
    {
        pGicDev->IntrGroup.au32[idxReg] = uValue;
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->IntrGroup.au32[idxReg]));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    return gicDistUpdateIrqState(pVM);
}


/**
 * Reads the redistributor's interrupt priority register (GICR_IPRIORITYR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pGicCpu     The GIC redistributor and CPU interface state.
 * @param   idxReg      The index of the register in the GICR_IPRIORITY range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicReDistReadIntrPriorityReg(PCGICDEV pGicDev, PGICCPU pGicCpu, uint16_t idxReg, uint32_t *puValue)
{
    /* When affinity routing is disabled, reads return 0. */
    Assert(pGicDev->fAffRouting); RT_NOREF(pGicDev);

    uint16_t const idxPriority = idxReg * sizeof(uint32_t);
    AssertReturn(idxPriority <= RT_ELEMENTS(pGicCpu->abIntrPriority) - sizeof(uint32_t), VERR_BUFFER_OVERFLOW);
    AssertCompile(sizeof(*puValue) == sizeof(uint32_t));
    *puValue = *(uint32_t *)&pGicCpu->abIntrPriority[idxPriority];
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, *puValue));
    return VINF_SUCCESS;
}


/**
 * Writes the redistributor's interrupt priority register (GICR_IPRIORITYR).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_IPRIORITY range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrPriorityReg(PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is disabled, writes are ignored. */
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PCGICDEV   pGicDev = PDMDEVINS_2_DATA(pDevIns, PCGICDEV);
    Assert(pGicDev->fAffRouting);

    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    uint16_t const idxPriority = idxReg * sizeof(uint32_t);
    AssertReturn(idxPriority <= RT_ELEMENTS(pGicCpu->abIntrPriority) - sizeof(uint32_t), VERR_BUFFER_OVERFLOW);
    AssertCompile(sizeof(uValue) == sizeof(uint32_t));

    for (uint8_t i = 0; i < sizeof(uint32_t); i++)
    {
        uint8_t const cShift    = i << 3;
        uint8_t const uPriority = uValue >> cShift;
        bool const    fGroup1   = gicBitmapGetBit(&pGicDev->IntrGroup.au32[0], idxPriority + i);
        if (fGroup1)
            pGicCpu->abIntrPriority[idxPriority + i] = uPriority << 1;
        else
            pGicCpu->abIntrPriority[idxPriority + i] = uPriority;
    }

    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, *(uint32_t *)&pGicCpu->abIntrPriority[idxPriority]));
    return gicReDistUpdateIrqState(pVCpu);
}


/**
 * Reads the redistributor's interrupt pending register (GICR_ISPENDR and
 * GICR_ICPENDR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pGicCpu     The GIC redistributor and CPU interface state.
 * @param   idxReg      The index of the register in the GICR_ISPENDR and
 *                      GICR_ICPENDR range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicReDistReadIntrPendingReg(PCGICDEV pGicDev, PGICCPU pGicCpu, uint16_t idxReg, uint32_t *puValue)
{
    /* When affinity routing is disabled, reads return 0. */
    Assert(pGicDev->fAffRouting); RT_NOREF(pGicDev);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrPending));
    *puValue = gicReDistGetPendingIntrAt(pGicCpu, idxReg);
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicCpu->bmIntrPending[idxReg]));
    return VINF_SUCCESS;
}


/**
 * Writes the redistributor's interrupt set-pending register (GICR_ISPENDR).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ISPENDR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrSetPendingReg(PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
#ifdef RT_STRICT
    /* When affinity routing is disabled, writes are ignored. */
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fAffRouting);
#endif
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrPending));
    pGicCpu->bmIntrPending[idxReg] |= uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrPending[idxReg]));
    return gicReDistUpdateIrqState(pVCpu);
}


/**
 * Writes the redistributor's interrupt clear-pending register (GICR_ICPENDR).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ICPENDR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrClearPendingReg(PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
#ifdef RT_STRICT
    /* When affinity routing is disabled, writes are ignored. */
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fAffRouting);
#endif
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrPending));
    pGicCpu->bmIntrPending[idxReg] &= ~uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrPending[idxReg]));
    return gicReDistUpdateIrqState(pVCpu);
}


/**
 * Reads the redistributor's interrupt enable register (GICR_ISENABLER and
 * GICR_ICENABLER).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pGicCpu     The GIC redistributor and CPU interface state.
 * @param   idxReg      The index of the register in the GICR_ISENABLER and
 *                      GICR_ICENABLER range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicReDistReadIntrEnableReg(PCGICDEV pGicDev, PGICCPU pGicCpu, uint16_t idxReg, uint32_t *puValue)
{
    Assert(pGicDev->fAffRouting); RT_NOREF(pGicDev);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrEnabled));
    *puValue = pGicCpu->bmIntrEnabled[idxReg];
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicCpu->bmIntrEnabled[idxReg]));
    return VINF_SUCCESS;
}


/**
 * Writes the redistributor's interrupt set-enable register (GICR_ISENABLER).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ISENABLER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrSetEnableReg(PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
#ifdef RT_STRICT
    /* When affinity routing is disabled, writes are ignored. */
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fAffRouting);
#endif
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrEnabled));
    pGicCpu->bmIntrEnabled[idxReg] |= uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrEnabled[idxReg]));
    return gicReDistUpdateIrqState(pVCpu);
}


/**
 * Writes the redistributor's interrupt clear-enable register (GICR_ICENABLER).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ICENABLER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrClearEnableReg(PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
#ifdef RT_STRICT
    /* When affinity routing is disabled, writes are ignored. */
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fAffRouting);
#endif
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrEnabled));
    pGicCpu->bmIntrEnabled[idxReg] &= ~uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrEnabled[idxReg]));
    return gicReDistUpdateIrqState(pVCpu);
}


/**
 * Reads the redistributor's interrupt active register (GICR_ISACTIVER and
 * GICR_ICACTIVER).
 *
 * @returns Strict VBox status code.
 * @param   pGicCpu     The GIC redistributor and CPU interface state.
 * @param   idxReg      The index of the register in the GICR_ISACTIVER and
 *                      GICR_ICACTIVER range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicReDistReadIntrActiveReg(PGICCPU pGicCpu, uint16_t idxReg, uint32_t *puValue)
{
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrActive));
    *puValue = pGicCpu->bmIntrActive[idxReg];
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicCpu->bmIntrActive[idxReg]));
    return VINF_SUCCESS;
}


/**
 * Writes the redistributor's interrupt set-active register (GICR_ISACTIVER).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ISACTIVER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrSetActiveReg(PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
#ifdef RT_STRICT
    /* When affinity routing is disabled, writes are ignored. */
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fAffRouting);
#endif
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrActive));
    pGicCpu->bmIntrActive[idxReg] |= uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrActive[idxReg]));
    return gicReDistUpdateIrqState(pVCpu);
}


/**
 * Writes the redistributor's interrupt clear-active register (GICR_ICACTIVER).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ICACTIVER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrClearActiveReg(PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
#ifdef RT_STRICT
    /* When affinity routing is disabled, writes are ignored. */
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fAffRouting);
#endif
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrActive));
    pGicCpu->bmIntrActive[idxReg] &= ~uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrActive[idxReg]));
    return gicReDistUpdateIrqState(pVCpu);
}


/**
 * Reads the redistributor's interrupt config register (GICR_ICFGR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pGicCpu     The GIC redistributor and CPU interface state.
 * @param   idxReg      The index of the register in the GICR_ICFGR range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicReDistReadIntrConfigReg(PCGICDEV pGicDev, PGICCPU pGicCpu, uint16_t idxReg, uint32_t *puValue)
{
    /* When affinity routing is disabled, reads return 0. */
    Assert(pGicDev->fAffRouting); RT_NOREF(pGicDev);
    Assert(idxReg / 2 < RT_ELEMENTS(pGicCpu->bmIntrConfig));
    uint64_t const bmIntrConfig = gicUnpackAltBits(pGicCpu->bmIntrConfig[idxReg / 2]);
    *puValue = bmIntrConfig >> (32 * (idxReg % 2));

    /* Ensure SGIs are read-only and remain configured as edge-triggered. */
    AssertMsg(idxReg > 0 || *puValue == 0xaaaaaaaa, ("uValue=%#RX32 Src=%#RX32\\n", *puValue, pGicCpu->bmIntrConfig[idxReg / 2]));
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, *puValue));
    return VINF_SUCCESS;
}


/**
 * Writes the redistributor's interrupt config register (GICR_ICFGR).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ICFGR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrConfigReg(PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
#ifdef RT_STRICT
    /* When affinity routing is disabled, writes are ignored. */
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fAffRouting);
#endif
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    if (idxReg > 0)
    {
        /* Only the lower or higher 16-bits of the 32-bits should be updated, retaining other bits. */
        Assert(idxReg / 2 < RT_ELEMENTS(pGicCpu->bmIntrConfig));
        static uint32_t const s_afRetainMasks[] = { 0xffff0000, 0x0000ffff };
        uint8_t const  idxUpdate          = idxReg % 2;
        uint32_t const fUpdate            = gicPackAltBits(uValue & 0xaaaaaaaa) << (16 * idxUpdate);
        uint32_t const fRetain            = pGicCpu->bmIntrConfig[idxReg / 2] & s_afRetainMasks[idxUpdate];
        pGicCpu->bmIntrConfig[idxReg / 2] = fUpdate | fRetain;
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrConfig[idxReg / 2]));
    }
    else
    {
        /* SGIs are always edge-triggered ignore writes. Windows 11 (24H2) arm64 guests writes these. */
        Assert(uValue == 0xaaaaaaaa);
        Assert((pGicCpu->bmIntrConfig[0] & UINT32_C(0xffff)) == UINT32_C(0xffff));
    }
    return VINF_SUCCESS;
}


/**
 * Reads the redistributor's interrupt group register (GICD_IGROUPR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pGicCpu     The GIC redistributor and CPU interface state.
 * @param   idxReg      The index of the register in the GICR_IGROUPR range.
 * @param   puValue     Where to store the register's value.
 */
static VBOXSTRICTRC gicReDistReadIntrGroupReg(PCGICDEV pGicDev, PGICCPU pGicCpu, uint16_t idxReg, uint32_t *puValue)
{
    /* When affinity routing is disabled, reads return 0. */
    Assert(pGicDev->fAffRouting); RT_NOREF(pGicDev);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrGroup));
    *puValue = pGicCpu->bmIntrGroup[idxReg];
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicCpu->bmIntrGroup[idxReg]));
    return VINF_SUCCESS;
}


/**
 * Writes the redistributor's interrupt group register (GICR_IGROUPR).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_IGROUPR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrGroupReg(PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
#ifdef RT_STRICT
    /* When affinity routing is disabled, writes are ignored. */
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fAffRouting);
#endif
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrGroup));
    pGicCpu->bmIntrGroup[idxReg] = uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrGroup[idxReg]));
    return gicReDistUpdateIrqState(pVCpu);
}


/**
 * Gets the virtual CPUID given the affinity values.
 *
 * @returns The virtual CPUID.
 * @param   idCpuInterface  The virtual CPUID within the PE cluster (0..15).
 * @param   uAff1           The affinity 1 value.
 * @param   uAff2           The affinity 2 value.
 * @param   uAff3           The affinity 3 value.
 */
DECL_FORCE_INLINE(VMCPUID) gicGetCpuIdFromAffinity(uint8_t idCpuInterface, uint8_t uAff1,  uint8_t uAff2,  uint8_t uAff3)
{
    AssertReturn(idCpuInterface < 16, 0);
    return (uAff3 * 1048576) + (uAff2 * 4096) + (uAff1 * 16) + idCpuInterface;
}


/**
 * Updates the pending state of the specified LPI in the redistributor and updates
 * the LPI pending table in guest memory if needed.
 *
 * @returns @c true if the LPI pending state was updated, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uIntId      The interrupt ID.
 * @param   fAsserted   Flag whether to mark the interrupt as asserted/de-asserted.
 */
static bool gicReDistUpdateLpiPending(PVMCPUCC pVCpu, uint16_t uIntId, bool fAsserted)
{
    Assert(GIC_IS_INTR_LPI(uIntId));
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);

    /* Get the particular bit that represents the pending state of the LPI. */
    uint16_t const idxIntr  = uIntId - GIC_INTID_RANGE_LPI_START;
    bool const     fUpdated = gicBitmapPutBit(&pGicCpu->LpiPending.au32[0], idxIntr, fAsserted);
    if (fUpdated)
    {
        /*
         * There is no guarantee that GICR_CTLR.EnableLPIs=0 causes the LPI pending table
         * to be updated in (guest) memory. It's my understanding that software cannot rely
         * on memory being coherent with the hardware cache of the LPI pending table,
         * see @bugref{10877#c57}. If for some reason, this turns out to be false, uncomment
         * the code block below (would hurt performance).
         *
         * See ARM GIC spec. 5.1.2 "LPI Pending tables".
         */
#if 0
        /* Update the pending state of the LPI in the LPI pending table in guest memory. */
        PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
        PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
        RTGCPHYS const GCPhysLpiPt = pGicDev->uLpiPendingBaseReg.u & GIC_BF_REDIST_REG_PENDBASER_PHYS_ADDR_MASK;
        uint16_t const offPending  = (uIntId / cIntrsPerElement);
        PDMDevHlpPhysWriteMeta(pDevIns, GCPhysLpiPt + offPending, (const void *)&bmLpiPending, sizeof(bmLpiPending));
#endif
    }
    return fUpdated;
}


/**
 * Gets the highest priority pending interrupt that can be signalled to the PE.
 *
 * @returns The interrupt ID or GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT if no interrupt
 *          is pending or not in a state to be signalled to the PE.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fIntrGroupMask  The interrupt groups to consider.
 * @param   pIntr           Where to store the pending interrupt. Optional, can be NULL.
 */
static uint16_t gicGetHighestPriorityPendingIntr(PCVMCPUCC pVCpu, uint32_t fIntrGroupMask, PGICINTR pIntr)
{
    /* Only one group must be specified here since this is called from registers that specify a single group! */
    Assert(   fIntrGroupMask == GIC_INTR_GROUP_0
           || fIntrGroupMask == GIC_INTR_GROUP_1S
           || fIntrGroupMask == GIC_INTR_GROUP_1NS);

    PCGICCPU   pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);

    uint16_t uIntId;
    if (   pGicCpu->bIntrPriorityMask
        && pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority])
    {
        GICINTR IntrRedist;
        gicReDistGetHighestPriorityPendingIntr(pVCpu, fIntrGroupMask, &IntrRedist);

        GICINTR IntrDist;
        gicDistGetHighestPriorityPendingIntr(pGicDev, pVCpu, fIntrGroupMask, &IntrDist);

        /* Get the interrupt ID of the highest priority pending interrupt if any. */
        PGICINTR pHighestIntr = IntrRedist.bPriority < IntrDist.bPriority ? &IntrRedist : &IntrDist;
        uIntId = pHighestIntr->uIntId;

        /* Check if it has sufficient priority to be signalled to the PE. */
        if (uIntId != GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT)
        {
            bool const fGroup0 = RT_BOOL(fIntrGroupMask & GIC_INTR_GROUP_0);
            bool const fSufficientPriority = gicReDistIsSufficientPriority(pGicCpu, pHighestIntr->bPriority, fGroup0);
            if (!fSufficientPriority)
            {
                uIntId = GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT;
                pHighestIntr->fIntrGroupMask = 0;
                pHighestIntr->uIntId         = uIntId;
                pHighestIntr->idxIntr        = UINT16_MAX;
                pHighestIntr->bPriority      = GIC_IDLE_PRIORITY;
            }
        }

        /* Populate the pending interrupt data and we're done. */
        if (pIntr)
            *pIntr = *pHighestIntr;
    }
    else
    {
        uIntId = GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT;
        if (pIntr)
        {
            RT_BZERO(pIntr, sizeof(*pIntr));
            pIntr->fIntrGroupMask = fIntrGroupMask;
            pIntr->uIntId         = GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT;
            pIntr->idxIntr        = UINT16_MAX;
            pIntr->bPriority      = GIC_IDLE_PRIORITY;
        }
    }

    /* Ensure that if no interrupt is pending, the idle priority is returned. */
    Assert(uIntId != GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT || pIntr->bPriority == GIC_IDLE_PRIORITY);

    /* Ensure that if no interrupt is pending, the index is invalid. */
    Assert(uIntId != GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT || pIntr->idxIntr == UINT16_MAX);

    LogFlowFunc(("uIntId=%u fIntrGroupMask=%#RX32\n", uIntId, fIntrGroupMask));
    return uIntId;
}


/**
 * Sets/updates the priority of the CPU interface's active interrupt.
 *
 * @param   pGicCpu         The GIC redistributor and CPU interface state.
 * @param   uIntId          The interrupt ID (used for updating diagnostic info).
 * @param   bIntrPriority   The priority of the active interrupt.
 * @param   fIntrGroupMask  The interrupt group of the active interrupt.
 */
static void gicReDistSetActiveIntrPriority(PGICCPU pGicCpu, uint16_t uIntId, uint8_t bIntrPriority, uint32_t fIntrGroupMask)
{
    /* Only one group must be specified here since this is called from registers that specify a single group! */
    Assert(   fIntrGroupMask == GIC_INTR_GROUP_0
           || fIntrGroupMask == GIC_INTR_GROUP_1S
           || fIntrGroupMask == GIC_INTR_GROUP_1NS);

    /* Updating active priority bitmap. */
    AssertCompile(sizeof(pGicCpu->bmActivePriorityGroup0) * 8 >= 128);
    AssertCompile(sizeof(pGicCpu->bmActivePriorityGroup1) * 8 >= 128);
    uint8_t const idxPreemptionLevel = bIntrPriority >> 1;
    if (fIntrGroupMask & GIC_INTR_GROUP_0)
        gicBitmapPutBit(&pGicCpu->bmActivePriorityGroup0[0], idxPreemptionLevel, true /* fSet */);
    else
        gicBitmapPutBit(&pGicCpu->bmActivePriorityGroup1[0], idxPreemptionLevel, true /* fSet */);

    /* Set running priority of the active interrupt. */
    if (RT_LIKELY(pGicCpu->idxRunningPriority < RT_ELEMENTS(pGicCpu->abRunningPriorities) - 1))
    {
        Assert(pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority] > bIntrPriority);
        LogFlowFunc(("Dropping interrupt priority from %u -> %u (idxRunningPriority: %u -> %u)\n",
                     pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority],
                     bIntrPriority,
                     pGicCpu->idxRunningPriority, pGicCpu->idxRunningPriority + 1));
        ++pGicCpu->idxRunningPriority;
        pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority] = bIntrPriority;
        pGicCpu->abRunningIntId[pGicCpu->idxRunningPriority] = uIntId;
    }
    else
        AssertReleaseMsgFailed(("Index of running-interrupt priority out-of-bounds %u\n", pGicCpu->idxRunningPriority));
}


/**
 * Get and acknowledge the interrupt ID of a signalled interrupt.
 *
 * @returns The interrupt ID or GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT no interrupts
 *          are pending or not in a state to be signalled.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fIntrGroupMask  The interrupt group to consider. Only one group must be
 *                          specified!
 */
static uint16_t gicAckHighestPriorityPendingIntr(PVMCPUCC pVCpu, uint32_t fIntrGroupMask)
{
    LogFlowFunc(("[%u]: fIntrGroupMask=%#RX32\n", pVCpu->idCpu, fIntrGroupMask));

    /* Only one group must be specified here since this is called from registers that specify a single group! */
    Assert(   fIntrGroupMask == GIC_INTR_GROUP_0
           || fIntrGroupMask == GIC_INTR_GROUP_1S
           || fIntrGroupMask == GIC_INTR_GROUP_1NS);

    STAM_PROFILE_START(&pGicCpu->StatProfIntrAck, x);

    PGICCPU    pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);

    /*
     * Get the pending interrupt with the highest priority for the given group.
     */
    GICINTR Intr;
    gicGetHighestPriorityPendingIntr(pVCpu, fIntrGroupMask, &Intr);
    if (Intr.uIntId == GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT)
    {
        STAM_PROFILE_STOP(&pGicCpu->StatProfIntrAck, x);
        return GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT;
    }

    uint8_t const  bIntrPriority = Intr.bPriority;
    uint16_t const uIntId        = Intr.uIntId;
    uint16_t const idxIntr       = Intr.idxIntr;
    Assert(idxIntr != UINT16_MAX);

    /*
     * Acknowledge the interrupt.
     */
    if (   GIC_IS_INTR_SGI_OR_PPI(uIntId)
        || GIC_IS_INTR_EXT_PPI(uIntId))
    {
        AssertMsg(idxIntr < sizeof(pGicCpu->bmIntrActive) * 8, ("idxIntr=%u\n", idxIntr));
        gicBitmapPutBit(&pGicCpu->bmIntrActive[0], idxIntr, true /* fSet */);

        AssertMsg(idxIntr < sizeof(pGicCpu->bmIntrConfig) * 8, ("idxIntr=%u\n", idxIntr));
        bool const fEdgeTriggered = gicBitmapGetBit(&pGicCpu->bmIntrConfig[0], idxIntr);
        if (fEdgeTriggered)
            gicBitmapPutBit(&pGicCpu->bmIntrPending[0], idxIntr, false /* fSet */);

        /* SGIs are always edge-triggered. */
        Assert(!GIC_IS_INTR_SGI(uIntId) || fEdgeTriggered);

        gicReDistSetActiveIntrPriority(pGicCpu, uIntId, bIntrPriority, fIntrGroupMask);
        gicReDistUpdateIrqState(pVCpu);
        STAM_COUNTER_INC(&pVCpu->gic.s.StatIntrAck);
    }
    else if (uIntId < GIC_INTID_RANGE_LPI_START)
    {
        /* Sanity check if the interrupt ID belongs to the distributor. */
        Assert(GIC_IS_INTR_SPI(uIntId) || GIC_IS_INTR_EXT_SPI(uIntId));

        Assert(idxIntr < sizeof(pGicDev->IntrActive) * 8);
        gicBitmapPutBit(&pGicDev->IntrActive.au32[0], idxIntr, true /* fSet */);

        AssertMsg(idxIntr < sizeof(pGicDev->IntrConfig) * 8, ("idxIntr=%u\n", idxIntr));
        bool const fEdgeTriggered = gicBitmapGetBit(&pGicDev->IntrConfig.au32[0], idxIntr);
        if (fEdgeTriggered)
            gicBitmapPutBit(&pGicDev->IntrPending.au32[0], idxIntr, false /* fSet */);

        gicReDistSetActiveIntrPriority(pGicCpu, uIntId, bIntrPriority, fIntrGroupMask);
        gicDistUpdateIrqState(pVCpu->CTX_SUFF(pVM));
        STAM_COUNTER_INC(&pVCpu->gic.s.StatIntrAck);
    }
    else if (GIC_IS_INTR_LPI(uIntId))
    {
        /* LPIs are always edge-triggered, mark the interrupt as no longer pending. */
        gicReDistUpdateLpiPending(pVCpu, uIntId, false /* fAsserted */);
        gicReDistSetActiveIntrPriority(pGicCpu, uIntId, bIntrPriority, fIntrGroupMask);
        gicReDistUpdateIrqState(pVCpu);
        STAM_COUNTER_INC(&pVCpu->gic.s.StatIntrAck);
    }
    else
        AssertReleaseMsgFailed(("Invalid interrupt-ID %u\n", uIntId));

    LogFlowFunc(("uIntId=%u\n", uIntId));
    STAM_PROFILE_STOP(&pGicCpu->StatProfIntrAck, x);
    return uIntId;
}


/**
 * Reads a distributor register.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being read.
 * @param   puValue         Where to store the register value.
 */
DECLINLINE(VBOXSTRICTRC) gicDistReadRegister(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t *puValue)
{
    VMCPU_ASSERT_EMT(pVCpu); RT_NOREF(pVCpu);
    PGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);

    /*
     * 64-bit registers.
     */
    {
        /*
         * GICD_IROUTER<n> and GICD_IROUTER<n>E.
         */
        uint16_t const cbReg = sizeof(uint64_t);
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IROUTERn_OFF_START, GIC_DIST_REG_IROUTERn_RANGE_SIZE))
        {
            /* Hardware does not map the first 32 registers (corresponding to SGIs and PPIs). */
            uint16_t const idxExt = GIC_INTID_RANGE_SPI_START;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_IROUTERn_OFF_START) / cbReg;
            return gicDistReadIntrRoutingReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IROUTERnE_OFF_START, GIC_DIST_REG_IROUTERnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->au32IntrRouting) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_IROUTERnE_OFF_START) / cbReg;
            return gicDistReadIntrRoutingReg(pGicDev, idxReg, puValue);
        }
    }

    /*
     * 32-bit registers.
     */
    {
        /*
         * GICD_IGROUPR<n> and GICD_IGROUPR<n>E.
         */
        uint16_t const cbReg = sizeof(uint32_t);
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IGROUPRn_OFF_START, GIC_DIST_REG_IGROUPRn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_IGROUPRn_OFF_START) / cbReg;
            return gicDistReadIntrGroupReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IGROUPRnE_OFF_START, GIC_DIST_REG_IGROUPRnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrGroup.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_IGROUPRnE_OFF_START) / cbReg;
            return gicDistReadIntrGroupReg(pGicDev, idxReg, puValue);
        }

        /*
         * GICD_ISENABLER<n> and GICD_ISENABLER<n>E.
         * GICD_ICENABLER<n> and GICD_ICENABLER<n>E.
         */
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISENABLERn_OFF_START, GIC_DIST_REG_ISENABLERn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ISENABLERn_OFF_START) / cbReg;
            return gicDistReadIntrEnableReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISENABLERnE_OFF_START, GIC_DIST_REG_ISENABLERnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrEnabled.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ISENABLERnE_OFF_START) / cbReg;
            return gicDistReadIntrEnableReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICENABLERn_OFF_START, GIC_DIST_REG_ICENABLERn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ICENABLERn_OFF_START) / cbReg;
            return gicDistReadIntrEnableReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICENABLERnE_OFF_START, GIC_DIST_REG_ICENABLERnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrEnabled.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ICENABLERnE_OFF_START) / cbReg;
            return gicDistReadIntrEnableReg(pGicDev, idxReg, puValue);
        }

        /*
         * GICD_ISACTIVER<n> and GICD_ISACTIVER<n>E.
         * GICD_ICACTIVER<n> and GICD_ICACTIVER<n>E.
         */
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISACTIVERn_OFF_START, GIC_DIST_REG_ISACTIVERn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ISACTIVERn_OFF_START) / cbReg;
            return gicDistReadIntrActiveReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISACTIVERnE_OFF_START, GIC_DIST_REG_ISACTIVERnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrActive.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ISACTIVERnE_OFF_START) / cbReg;
            return gicDistReadIntrActiveReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICACTIVERn_OFF_START, GIC_DIST_REG_ICACTIVERn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ICENABLERn_OFF_START) / cbReg;
            return gicDistReadIntrActiveReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICACTIVERnE_OFF_START, GIC_DIST_REG_ICACTIVERnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrActive.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ICACTIVERnE_OFF_START) / cbReg;
            return gicDistReadIntrActiveReg(pGicDev, idxReg, puValue);
        }

        /*
         * GICD_IPRIORITYR<n> and GICD_IPRIORITYR<n>E.
         */
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IPRIORITYRn_OFF_START, GIC_DIST_REG_IPRIORITYRn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_IPRIORITYRn_OFF_START) / cbReg;
            return gicDistReadIntrPriorityReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IPRIORITYRnE_OFF_START, GIC_DIST_REG_IPRIORITYRnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->abIntrPriority) / (2 * sizeof(uint32_t));
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_IPRIORITYRnE_OFF_START) / cbReg;
            return gicDistReadIntrPriorityReg(pGicDev, idxReg, puValue);
        }

        /*
         * GICD_ISPENDR<n> and GICD_ISPENDR<n>E.
         * GICD_ICPENDR<n> and GICD_ICPENDR<n>E.
         */
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISPENDRn_OFF_START, GIC_DIST_REG_ISPENDRn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ISPENDRn_OFF_START) / cbReg;
            return gicDistReadIntrPendingReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISPENDRnE_OFF_START, GIC_DIST_REG_ISPENDRnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrPending.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ISPENDRnE_OFF_START) / cbReg;
            return gicDistReadIntrPendingReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICPENDRn_OFF_START, GIC_DIST_REG_ICPENDRn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ICPENDRn_OFF_START) / cbReg;
            return gicDistReadIntrPendingReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICPENDRnE_OFF_START, GIC_DIST_REG_ICPENDRnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrPending.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ICPENDRnE_OFF_START) / cbReg;
            return gicDistReadIntrPendingReg(pGicDev, idxReg, puValue);
        }

        /*
         * GICD_ICFGR<n> and GICD_ICFGR<n>E.
         */
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICFGRn_OFF_START, GIC_DIST_REG_ICFGRn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ICFGRn_OFF_START) / cbReg;
            return gicDistReadIntrConfigReg(pGicDev, idxReg, puValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICFGRnE_OFF_START, GIC_DIST_REG_ICFGRnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrConfig.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ICFGRnE_OFF_START) / cbReg;
            return gicDistReadIntrConfigReg(pGicDev, idxReg, puValue);
        }
    }

    switch (offReg)
    {
        case GIC_DIST_REG_CTLR_OFF:
            Assert(pGicDev->fAffRouting);
            *puValue = ((pGicDev->fIntrGroupMask & GIC_INTR_GROUP_0)   ? GIC_DIST_REG_CTRL_ENABLE_GRP0    : 0)
                     | ((pGicDev->fIntrGroupMask & GIC_INTR_GROUP_1NS) ? GIC_DIST_REG_CTRL_ENABLE_GRP1_NS : 0)
                     | GIC_DIST_REG_CTRL_DS         /* We don't support dual security states. */
                     | GIC_DIST_REG_CTRL_ARE_S;     /* We don't support GICv2 backwards compatibility, ARE is always enabled. */
            break;

        case GIC_DIST_REG_TYPER_OFF:
        {
            Assert(pGicDev->uMaxSpi > 0 && pGicDev->uMaxSpi <= GIC_DIST_REG_TYPER_NUM_ITLINES);
            Assert(pGicDev->fAffRouting);
            *puValue = GIC_DIST_REG_TYPER_NUM_ITLINES_SET(pGicDev->uMaxSpi)
                     | GIC_DIST_REG_TYPER_NUM_PES_SET(0)             /* Affinity routing is always enabled, hence this MBZ. */
                     /*| GIC_DIST_REG_TYPER_NMI*/                    /** @todo Support non-maskable interrupts */
                     /*| GIC_DIST_REG_TYPER_SECURITY_EXTN*/          /** @todo Support dual security states. */
                     | (pGicDev->fMbi ? GIC_DIST_REG_TYPER_MBIS : 0)
                     | (pGicDev->fRangeSel ? GIC_DIST_REG_TYPER_RSS : 0)
                     | GIC_DIST_REG_TYPER_IDBITS_SET(15)             /* We only support 16-bit interrupt IDs. */
                     | (pGicDev->fAff3Levels ? GIC_DIST_REG_TYPER_A3V : 0);
            if (pGicDev->fExtSpi)
                *puValue |= GIC_DIST_REG_TYPER_ESPI
                         |  GIC_DIST_REG_TYPER_ESPI_RANGE_SET(pGicDev->uMaxExtSpi);
            if (pGicDev->fLpi)
            {
                Assert(pGicDev->uMaxLpi - 2 < 13);
                Assert(GIC_INTID_RANGE_LPI_START + (UINT32_C(2) << pGicDev->uMaxLpi) <= UINT16_MAX);
                *puValue |= GIC_DIST_REG_TYPER_LPIS
                         |  GIC_DIST_REG_TYPER_NUM_LPIS_SET(pGicDev->uMaxLpi);
            }
            AssertMsg(   RT_BOOL(*puValue & GIC_DIST_REG_TYPER_MBIS)
                      == RT_BOOL(*puValue & GIC_DIST_REG_TYPER_LPIS), ("%#RX32\n", *puValue));
            break;
        }

        case GIC_DIST_REG_PIDR2_OFF:
            Assert(pGicDev->uArchRev <= GIC_DIST_REG_PIDR2_ARCHREV_GICV4);
            *puValue = GIC_DIST_REG_PIDR2_ARCHREV_SET(pGicDev->uArchRev);
            break;

        case GIC_DIST_REG_IIDR_OFF:
            *puValue = GIC_DIST_REG_IIDR_IMPL_SET(GIC_JEDEC_JEP106_IDENTIFICATION_CODE, GIC_JEDEC_JEP106_CONTINUATION_CODE);
            break;

        case GIC_DIST_REG_TYPER2_OFF:
            *puValue = 0;
            break;

        default:
            AssertReleaseMsgFailed(("offReg=%#x\n", offReg));
            *puValue = 0;
            break;
    }
    return VINF_SUCCESS;
}


/**
 * Writes a distributor register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being written.
 * @param   uValue          The register value.
 */
DECLINLINE(VBOXSTRICTRC) gicDistWriteRegister(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t uValue)
{
    VMCPU_ASSERT_EMT(pVCpu); RT_NOREF(pVCpu);
    PGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    PVMCC   pVM     = PDMDevHlpGetVM(pDevIns);

    /*
     * 64-bit registers.
     */
    {
        /*
         * GICD_IROUTER<n> and GICD_IROUTER<n>E.
         */
        uint16_t const cbReg = sizeof(uint64_t);
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IROUTERn_OFF_START, GIC_DIST_REG_IROUTERn_RANGE_SIZE))
        {
            /* Hardware does not map the first 32 registers (corresponding to SGIs and PPIs). */
            uint16_t const idxExt = GIC_INTID_RANGE_SPI_START;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_IROUTERn_OFF_START) / cbReg;
            return gicDistWriteIntrRoutingReg(pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IROUTERnE_OFF_START, GIC_DIST_REG_IROUTERnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->au32IntrRouting) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_IROUTERnE_OFF_START) / cbReg;
            return gicDistWriteIntrRoutingReg(pGicDev, idxReg, uValue);
        }

    }

    /*
     * 32-bit registers.
     */
    {
        /*
         * GICD_IGROUPR<n> and GICD_IGROUPR<n>E.
         */
        uint16_t const cbReg = sizeof(uint32_t);
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IGROUPRn_OFF_START, GIC_DIST_REG_IGROUPRn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_IGROUPRn_OFF_START) / cbReg;
            return gicDistWriteIntrGroupReg(pVM, pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IGROUPRnE_OFF_START, GIC_DIST_REG_IGROUPRnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrGroup.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_IGROUPRnE_OFF_START) / cbReg;
            return gicDistWriteIntrGroupReg(pVM, pGicDev, idxReg, uValue);
        }

        /*
         * GICD_ISENABLER<n> and GICD_ISENABLER<n>E.
         * GICD_ICENABLER<n> and GICD_ICENABLER<n>E.
         */
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISENABLERn_OFF_START, GIC_DIST_REG_ISENABLERn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ISENABLERn_OFF_START) / cbReg;
            return gicDistWriteIntrSetEnableReg(pVM, pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISENABLERnE_OFF_START, GIC_DIST_REG_ISENABLERnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrEnabled.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ISENABLERnE_OFF_START) / cbReg;
            return gicDistWriteIntrSetEnableReg(pVM, pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICENABLERn_OFF_START, GIC_DIST_REG_ICENABLERn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ICENABLERn_OFF_START) / cbReg;
            return gicDistWriteIntrClearEnableReg(pVM, pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICENABLERnE_OFF_START, GIC_DIST_REG_ICENABLERnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrEnabled.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ICENABLERnE_OFF_START) / cbReg;
            return gicDistWriteIntrClearEnableReg(pVM, pGicDev, idxReg, uValue);
        }

        /*
         * GICD_ISACTIVER<n> and GICD_ISACTIVER<n>E.
         * GICD_ICACTIVER<n> and GICD_ICACTIVER<n>E.
         */
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISACTIVERn_OFF_START, GIC_DIST_REG_ISACTIVERn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ISACTIVERn_OFF_START) / cbReg;
            return gicDistWriteIntrSetActiveReg(pVM, pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISACTIVERnE_OFF_START, GIC_DIST_REG_ISACTIVERnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrActive.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ISACTIVERnE_OFF_START) / cbReg;
            return gicDistWriteIntrSetActiveReg(pVM, pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICACTIVERn_OFF_START, GIC_DIST_REG_ICACTIVERn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ICACTIVERn_OFF_START) / cbReg;
            return gicDistWriteIntrClearActiveReg(pVM, pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICACTIVERnE_OFF_START, GIC_DIST_REG_ICACTIVERnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrActive.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ICACTIVERnE_OFF_START) / cbReg;
            return gicDistWriteIntrClearActiveReg(pVM, pGicDev, idxReg, uValue);
        }

        /*
         * GICD_IPRIORITYR<n> and GICD_IPRIORITYR<n>E.
         */
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IPRIORITYRn_OFF_START, GIC_DIST_REG_IPRIORITYRn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_IPRIORITYRn_OFF_START) / cbReg;
            return gicDistWriteIntrPriorityReg(pVM, pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IPRIORITYRnE_OFF_START, GIC_DIST_REG_IPRIORITYRnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->abIntrPriority) / (2 * sizeof(uint32_t));
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_IPRIORITYRnE_OFF_START) / cbReg;
            return gicDistWriteIntrPriorityReg(pVM, pGicDev, idxReg, uValue);
        }

        /*
         * GICD_ISPENDR<n> and GICD_ISPENDR<n>E.
         * GICD_ICPENDR<n> and GICD_ICPENDR<n>E.
         */
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISPENDRn_OFF_START, GIC_DIST_REG_ISPENDRn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ISPENDRn_OFF_START) / cbReg;
            return gicDistWriteIntrSetPendingReg(pVM, pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ISPENDRnE_OFF_START, GIC_DIST_REG_ISPENDRnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrPending.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ISPENDRnE_OFF_START) / cbReg;
            return gicDistWriteIntrSetPendingReg(pVM, pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICPENDRn_OFF_START, GIC_DIST_REG_ICPENDRn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ICPENDRn_OFF_START) / cbReg;
            return gicDistWriteIntrClearPendingReg(pVM, pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICPENDRnE_OFF_START, GIC_DIST_REG_ICPENDRnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrPending.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ICPENDRnE_OFF_START) / cbReg;
            return gicDistWriteIntrClearPendingReg(pVM, pGicDev, idxReg, uValue);
        }

        /*
         * GICD_ICFGR<n> and GICD_ICFGR<n>E.
         */
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICFGRn_OFF_START, GIC_DIST_REG_ICFGRn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_ICFGRn_OFF_START) / cbReg;
            return gicDistWriteIntrConfigReg(pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_ICFGRnE_OFF_START, GIC_DIST_REG_ICFGRnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->IntrConfig.au32) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ICFGRnE_OFF_START) / cbReg;
            return gicDistWriteIntrConfigReg(pGicDev, idxReg, uValue);
        }
    }

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    switch (offReg)
    {
        case GIC_DIST_REG_CTLR_OFF:
        {
            Assert(!(uValue & GIC_DIST_REG_CTRL_ARE_NS));
            uint32_t const fIntrGroupMask = ((uValue & GIC_DIST_REG_CTRL_ENABLE_GRP0)    ? GIC_INTR_GROUP_0   : 0)
                                          | ((uValue & GIC_DIST_REG_CTRL_ENABLE_GRP1_NS) ? GIC_INTR_GROUP_1NS : 0);
            if (pGicDev->fIntrGroupMask != fIntrGroupMask)
            {
                pGicDev->fIntrGroupMask = fIntrGroupMask;
                rcStrict = gicDistUpdateIrqState(pVM);
            }
            break;
        }

        default:
        {
            /* Windows 11 arm64 (24H2) writes zeroes into these reserved registers. We ignore them. */
            if (offReg >= 0x7fe0 && offReg <= 0x7ffc)
                LogFlowFunc(("Bad guest writing to reserved GIC distributor register space [0x7fe0..0x7ffc] -- ignoring!"));
            else
                AssertReleaseMsgFailed(("offReg=%#x uValue=%#RX32\n", offReg, uValue));
            break;
        }
    }

    return rcStrict;
}


/**
 * Reads a GIC redistributor register.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   idRedist        The redistributor ID.
 * @param   offReg          The offset of the register being read.
 * @param   puValue         Where to store the register value.
 */
DECLINLINE(VBOXSTRICTRC) gicReDistReadRegister(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint32_t idRedist, uint16_t offReg, uint32_t *puValue)
{
    PCVMCC   pVM     = pVCpu->CTX_SUFF(pVM);
    PCGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(idRedist == pVCpu->idCpu);

    switch (offReg)
    {
        case GIC_REDIST_REG_TYPER_OFF:
            *puValue = (pVCpu->idCpu == pVM->cCpus - 1 ? GIC_REDIST_REG_TYPER_LAST : 0)
                     | GIC_REDIST_REG_TYPER_CPU_NUMBER_SET(idRedist)
                     | GIC_REDIST_REG_TYPER_CMN_LPI_AFF_SET(GIC_REDIST_REG_TYPER_CMN_LPI_AFF_ALL)
                     | (pGicDev->fExtPpi ? GIC_REDIST_REG_TYPER_PPI_NUM_SET(pGicDev->uMaxExtPpi) : 0)
                     | (pGicDev->fLpi    ? GIC_REDIST_REG_TYPER_PLPIS : 0);
            Assert(!pGicDev->fExtPpi || pGicDev->uMaxExtPpi > 0);
            break;

        case GIC_REDIST_REG_WAKER_OFF:
            *puValue = 0;
            break;

        case GIC_REDIST_REG_IIDR_OFF:
            *puValue = GIC_REDIST_REG_IIDR_IMPL_SET(GIC_JEDEC_JEP106_IDENTIFICATION_CODE, GIC_JEDEC_JEP106_CONTINUATION_CODE);
            break;

        case GIC_REDIST_REG_TYPER_AFFINITY_OFF:
            *puValue = idRedist;
            break;

        case GIC_REDIST_REG_PIDR2_OFF:
            Assert(pGicDev->uArchRev <= GIC_DIST_REG_PIDR2_ARCHREV_GICV4);
            *puValue = GIC_REDIST_REG_PIDR2_ARCHREV_SET(pGicDev->uArchRev);
            break;

        case GIC_REDIST_REG_CTLR_OFF:
            *puValue = (pGicDev->fEnableLpis ? GIC_REDIST_REG_CTLR_ENABLE_LPI : 0)
                     | GIC_REDIST_REG_CTLR_CES_SET(1);
            break;

        case GIC_REDIST_REG_PROPBASER_OFF:
            *puValue = pGicDev->uLpiConfigBaseReg.s.Lo;
            break;
        case GIC_REDIST_REG_PROPBASER_OFF + 4:
            *puValue = pGicDev->uLpiConfigBaseReg.s.Hi;
            break;

        case GIC_REDIST_REG_PENDBASER_OFF:
            *puValue = pGicDev->uLpiPendingBaseReg.s.Lo;
            break;
        case GIC_REDIST_REG_PENDBASER_OFF + 4:
            *puValue = pGicDev->uLpiPendingBaseReg.s.Hi;
            break;

        default:
            AssertReleaseMsgFailed(("offReg=%#x\n",  offReg));
            *puValue = 0;
            break;
    }
    return VINF_SUCCESS;
}


/**
 * Reads a GIC redistributor SGI/PPI frame register.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being read.
 * @param   puValue         Where to store the register value.
 */
DECLINLINE(VBOXSTRICTRC) gicReDistReadSgiPpiRegister(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t *puValue)
{
    VMCPU_ASSERT_EMT(pVCpu);
    RT_NOREF(pDevIns);

    PGICCPU  pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    PCGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    uint16_t const cbReg = sizeof(uint32_t);

    /*
     * GICR_IGROUPR0 and GICR_IGROUPR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_IGROUPR0_OFF, GIC_REDIST_SGI_PPI_REG_IGROUPRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_IGROUPR0_OFF) / cbReg;
        return gicReDistReadIntrGroupReg(pGicDev, pGicCpu, idxReg, puValue);
    }

    /*
     * GICR_ISENABLER0 and GICR_ISENABLER<n>E.
     * GICR_ICENABLER0 and GICR_ICENABLER<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISENABLER0_OFF, GIC_REDIST_SGI_PPI_REG_ISENABLERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ISENABLER0_OFF) / cbReg;
        return gicReDistReadIntrEnableReg(pGicDev, pGicCpu, idxReg, puValue);
    }
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICENABLER0_OFF, GIC_REDIST_SGI_PPI_REG_ICENABLERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICENABLERnE_OFF_START) / cbReg;
        return gicReDistReadIntrEnableReg(pGicDev, pGicCpu, idxReg, puValue);
    }

    /*
     * GICR_ISACTIVER0 and GICR_ISACTIVER<n>E.
     * GICR_ICACTIVER0 and GICR_ICACTIVER<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISACTIVER0_OFF, GIC_REDIST_SGI_PPI_REG_ISACTIVERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ISACTIVER0_OFF) / cbReg;
        return gicReDistReadIntrActiveReg(pGicCpu, idxReg, puValue);
    }
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICACTIVER0_OFF, GIC_REDIST_SGI_PPI_REG_ICACTIVERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICACTIVER0_OFF) / cbReg;
        return gicReDistReadIntrActiveReg(pGicCpu, idxReg, puValue);
    }

    /*
     * GICR_ISPENDR0 and GICR_ISPENDR<n>E.
     * GICR_ICPENDR0 and GICR_ICPENDR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISPENDR0_OFF, GIC_REDIST_SGI_PPI_REG_ISPENDRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ISPENDR0_OFF) / cbReg;
        return gicReDistReadIntrPendingReg(pGicDev, pGicCpu, idxReg, puValue);
    }
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICPENDR0_OFF, GIC_REDIST_SGI_PPI_REG_ICPENDRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICPENDR0_OFF) / cbReg;
        return gicReDistReadIntrPendingReg(pGicDev, pGicCpu, idxReg, puValue);
    }

    /*
     * GICR_IPRIORITYR<n> and GICR_IPRIORITYR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_IPRIORITYRn_OFF_START, GIC_REDIST_SGI_PPI_REG_IPRIORITYRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_IPRIORITYRn_OFF_START) / cbReg;
        return gicReDistReadIntrPriorityReg(pGicDev, pGicCpu, idxReg, puValue);
    }

    /*
     * GICR_ICFGR0, GICR_ICFGR1 and GICR_ICFGR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICFGR0_OFF, GIC_REDIST_SGI_PPI_REG_ICFGRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICFGR0_OFF) / cbReg;
        return gicReDistReadIntrConfigReg(pGicDev, pGicCpu, idxReg, puValue);
    }

    AssertReleaseMsgFailed(("offReg=%#x (%s)\n", offReg, gicReDistGetSgiPpiRegDescription(offReg)));
    *puValue = 0;
    return VINF_SUCCESS;
}


/**
 * Writes a GIC redistributor frame register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being written.
 * @param   uValue          The register value.
 */
DECLINLINE(VBOXSTRICTRC) gicReDistWriteRegister(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t uValue)
{
    VMCPU_ASSERT_EMT(pVCpu);

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    PGICDEV      pGicDev  = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    switch (offReg)
    {
        case GIC_REDIST_REG_WAKER_OFF:
            Assert(uValue == 0);
            break;

        case GIC_REDIST_REG_CTLR_OFF:
        {
            /* Check if LPIs are supported and whether the enable LPI bit changed. */
            uint32_t const uOldCtlr = pGicDev->fEnableLpis ? GIC_REDIST_REG_CTLR_ENABLE_LPI : 0;
            uint32_t const uNewCtlr = uValue & GIC_REDIST_REG_CTLR_ENABLE_LPI;
            if (   pGicDev->fLpi
                && ((uNewCtlr ^ uOldCtlr) & GIC_REDIST_REG_CTLR_ENABLE_LPI))
            {
                pGicDev->fEnableLpis = RT_BOOL(uNewCtlr & GIC_REDIST_REG_CTLR_ENABLE_LPI);
                if (pGicDev->fEnableLpis)
                {
                    gicDistReadLpiConfigTableFromMem(pDevIns);
                    gicReDistReadLpiPendingTableFromMem(pDevIns, pVCpu);
                }
                else
                {
                    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
                    RT_ZERO(pGicCpu->LpiPending);
                }
                gitsLpiCacheInvalidateAll(&pGicDev->Gits);
            }
            break;
        }

        case GIC_REDIST_REG_PROPBASER_OFF:
            pGicDev->uLpiConfigBaseReg.s.Lo = uValue & RT_LO_U32(GIC_REDIST_REG_PROPBASER_RW_MASK);
            break;
        case GIC_REDIST_REG_PROPBASER_OFF + 4:
            pGicDev->uLpiConfigBaseReg.s.Hi = uValue & RT_HI_U32(GIC_REDIST_REG_PROPBASER_RW_MASK);
            break;

        case GIC_REDIST_REG_PENDBASER_OFF:
            pGicDev->uLpiPendingBaseReg.s.Lo = uValue & RT_LO_U32(GIC_REDIST_REG_PENDBASER_RW_MASK);
            break;
        case GIC_REDIST_REG_PENDBASER_OFF + 4:
            pGicDev->uLpiPendingBaseReg.s.Hi = uValue & RT_HI_U32(GIC_REDIST_REG_PENDBASER_RW_MASK);
            break;

        default:
            AssertReleaseMsgFailed(("offReg=%#x (%s) uValue=%#RX32\n", offReg, gicReDistGetRegDescription(offReg), uValue));
            break;
    }

    return rcStrict;
}


/**
 * Writes a GIC redistributor SGI/PPI frame register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the register being written.
 * @param   uValue          The register value.
 */
DECLINLINE(VBOXSTRICTRC) gicReDistWriteSgiPpiRegister(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t offReg, uint32_t uValue)
{
    VMCPU_ASSERT_EMT(pVCpu); RT_NOREF(pDevIns);
    uint16_t const cbReg = sizeof(uint32_t);

    /*
     * GICR_IGROUPR0 and GICR_IGROUPR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_IGROUPR0_OFF, GIC_REDIST_SGI_PPI_REG_IGROUPRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_IGROUPR0_OFF) / cbReg;
        return gicReDistWriteIntrGroupReg(pVCpu, idxReg, uValue);
    }

    /*
     * GICR_ISENABLER0 and GICR_ISENABLER<n>E.
     * GICR_ICENABLER0 and GICR_ICENABLER<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISENABLER0_OFF, GIC_REDIST_SGI_PPI_REG_ISENABLERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ISENABLER0_OFF) / cbReg;
        return gicReDistWriteIntrSetEnableReg(pVCpu, idxReg, uValue);
    }
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICENABLER0_OFF, GIC_REDIST_SGI_PPI_REG_ICENABLERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICENABLER0_OFF) / cbReg;
        return gicReDistWriteIntrClearEnableReg(pVCpu, idxReg, uValue);
    }

    /*
     * GICR_ISACTIVER0 and GICR_ISACTIVER<n>E.
     * GICR_ICACTIVER0 and GICR_ICACTIVER<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISACTIVER0_OFF, GIC_REDIST_SGI_PPI_REG_ISACTIVERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ISACTIVER0_OFF) / cbReg;
        return gicReDistWriteIntrSetActiveReg(pVCpu, idxReg, uValue);
    }
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICACTIVER0_OFF, GIC_REDIST_SGI_PPI_REG_ICACTIVERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICACTIVER0_OFF) / cbReg;
        return gicReDistWriteIntrClearActiveReg(pVCpu, idxReg, uValue);
    }

    /*
     * GICR_ISPENDR0 and GICR_ISPENDR<n>E.
     * GICR_ICPENDR0 and GICR_ICPENDR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISPENDR0_OFF, GIC_REDIST_SGI_PPI_REG_ISPENDRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ISPENDR0_OFF) / cbReg;
        return gicReDistWriteIntrSetPendingReg(pVCpu, idxReg, uValue);
    }
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICPENDR0_OFF, GIC_REDIST_SGI_PPI_REG_ICPENDRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICPENDR0_OFF) / cbReg;
        return gicReDistWriteIntrClearPendingReg(pVCpu, idxReg, uValue);
    }

    /*
     * GICR_IPRIORITYR<n> and GICR_IPRIORITYR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_IPRIORITYRn_OFF_START, GIC_REDIST_SGI_PPI_REG_IPRIORITYRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_IPRIORITYRn_OFF_START) / cbReg;
        return gicReDistWriteIntrPriorityReg(pVCpu, idxReg, uValue);
    }

    /*
     * GICR_ICFGR0, GIC_ICFGR1 and GICR_ICFGR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICFGR0_OFF, GIC_REDIST_SGI_PPI_REG_ICFGRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICFGR0_OFF) / cbReg;
        return gicReDistWriteIntrConfigReg(pVCpu, idxReg, uValue);
    }

    AssertReleaseMsgFailed(("offReg=%#RX16 (%s)\n", offReg, gicReDistGetSgiPpiRegDescription(offReg)));
    return VERR_INTERNAL_ERROR_2;
}


/**
 * Sets the specified Locality-specific Peripheral Interrupt (LPI).
 *
 * @param   pDevIns     The device instance.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uIntId      The interrupt ID.
 * @param   fAsserted   Flag whether to mark the interrupt as asserted/de-asserted.
 */
DECLHIDDEN(void) gicReDistSetLpi(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t uIntId, bool fAsserted)
{
    Assert(GIC_CRIT_SECT_IS_OWNER(pDevIns)); RT_NOREF(pDevIns);
    Assert(GIC_IS_INTR_LPI(uIntId));
    Log4Func(("[%u] uIntId=%RU32 fAsserted=%RTbool\n", pVCpu->idCpu, uIntId, fAsserted));

    bool const fUpdated = gicReDistUpdateLpiPending(pVCpu, uIntId, fAsserted);
    if (fUpdated)
        gicReDistUpdateIrqState(pVCpu);
}


/**
 * @interface_method_impl{PDMGICBACKEND,pfnSetSpi}
 */
static DECLCALLBACK(int) gicSetSpi(PVMCC pVM, uint32_t uSpiIntId, bool fAsserted)
{
    LogFlowFunc(("pVM=%p uSpiIntId=%u fAsserted=%RTbool\n", pVM, uSpiIntId, fAsserted));

    PGIC       pGic    = VM_TO_GIC(pVM);
    PPDMDEVINS pDevIns = pGic->CTX_SUFF(pDevIns);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);

    STAM_PROFILE_START(&pGicDev->StatProfSetSpi, a);
    STAM_COUNTER_INC(&pGicDev->StatSetSpi);

    uint16_t const uIntId  = GIC_INTID_RANGE_SPI_START + uSpiIntId;
    uint16_t const idxIntr = gicDistGetIndexFromIntId(uIntId);

    Assert(idxIntr >= GIC_INTID_RANGE_SPI_START);
    AssertMsgReturn(idxIntr < sizeof(pGicDev->IntrLevel) * 8,
                    ("out-of-range SPI interrupt ID %RU32 (%RU32)\n", uIntId, uSpiIntId),
                    VERR_INVALID_PARAMETER);
    Assert(GIC_IS_INTR_SPI(uIntId) || GIC_IS_INTR_EXT_SPI(uIntId));

    GIC_CRIT_SECT_ENTER(pDevIns);

#if 0
    /* For edge-triggered we should probably only update on 0 to 1 transition. */
    bool const fEdgeTriggered = gicBitmapGetBit(&pGicDev->IntrConfig.au32[0], idxIntr);
    Assert(!fEdgeTriggered); NOREF(fEdgeTriggered);
#endif
    bool const fUpdated = gicBitmapPutBit(&pGicDev->IntrLevel.au32[0], idxIntr, fAsserted);
    int const  rc = fUpdated
                  ? VBOXSTRICTRC_VAL(gicDistUpdateIrqState(pVM))
                  : VINF_SUCCESS;

    GIC_CRIT_SECT_LEAVE(pDevIns);

    STAM_PROFILE_STOP(&pGicDev->StatProfSetSpi, a);
    return rc;
}


/**
 * @interface_method_impl{PDMGICBACKEND,pfnSetPpi}
 */
static DECLCALLBACK(int) gicSetPpi(PVMCPUCC pVCpu, uint32_t uPpiIntId, bool fAsserted)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} uPpiIntId=%u fAsserted=%RTbool\n", pVCpu, pVCpu->idCpu, uPpiIntId, fAsserted));

    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICCPU    pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    AssertCompile(RT_ELEMENTS(pGicCpu->bmIntrConfig) == RT_ELEMENTS(pGicCpu->bmIntrPending));
    AssertCompile(sizeof(pGicCpu->bmIntrConfig) == sizeof(pGicCpu->bmIntrPending));

    STAM_COUNTER_INC(&pVCpu->gic.s.StatSetPpi);
    STAM_PROFILE_START(&pGicCpu->StatProfSetPpi, b);

    uint32_t const uIntId  = GIC_INTID_RANGE_PPI_START + uPpiIntId;
    uint16_t const idxIntr = gicReDistGetIndexFromIntId(uIntId);

    Assert(idxIntr >= GIC_INTID_RANGE_PPI_START);
    AssertMsgReturn(idxIntr < sizeof(pGicCpu->bmIntrPending) * 8,
                    ("out-of-range PPI interrupt ID %RU32 (%RU32)\n", uIntId, uPpiIntId),
                    VERR_INVALID_PARAMETER);
    AssertCompile(RT_ELEMENTS(pGicCpu->bmIntrConfig) == RT_ELEMENTS(pGicCpu->bmIntrPending));
    AssertCompile(sizeof(pGicCpu->bmIntrConfig) == sizeof(pGicCpu->bmIntrPending));
    Assert(GIC_IS_INTR_PPI(uIntId) || GIC_IS_INTR_EXT_PPI(uIntId));

    GIC_CRIT_SECT_ENTER(pDevIns);

#if 0
    /* For edge-triggered we should probably only update on 0 to 1 transition. */
    bool const fEdgeTriggered = gicBitmapGetBit(&pGicCpu->bmIntrConfig[0], idxIntr);
    Assert(!fEdgeTriggered); NOREF(fEdgeTriggered);
#endif

    bool const fUpdated = gicBitmapPutBit(&pGicCpu->bmIntrLevel[0], idxIntr, fAsserted);
    int const rc = fUpdated
                 ? VBOXSTRICTRC_VAL(gicReDistUpdateIrqState(pVCpu))
                 : VINF_SUCCESS;

    GIC_CRIT_SECT_LEAVE(pDevIns);
    STAM_PROFILE_STOP(&pGicCpu->StatProfSetPpi, b);
    return rc;
}


/**
 * @interface_method_impl{PDMGICBACKEND,pfnSendMsi}
 */
DECL_HIDDEN_CALLBACK(int) gicSendMsi(PVMCC pVM, PCIBDF uBusDevFn, PCMSIMSG pMsi, uint32_t uTagSrc)
{
    NOREF(uTagSrc); /** @todo Consider setting (on assert) and clearing (on de-assert) if possible later. */
    AssertPtrReturn(pMsi, VERR_INVALID_PARAMETER);
    Log4Func(("uBusDevFn=%#RX32 Msi.Addr=%#RX64 Msi.Data=%#RX32\n", uBusDevFn, pMsi->Addr.u64, pMsi->Data.u32));

    /*
     * ARM expects that the 16-bit requester ID from a PCIe Root Complex is
     * presented to an ITS as the device ID. The guest seems to use device ID
     * of 0 instead of the legacy endpoint BDF (bus:dev:fn).
     *
     * See ARM GIC spec. 5.2.3 "The Device Table".
     */
    /** @todo Figure out why device ID 0 is being used? Is it because the device
     *        maybe behind the PCI-to-PCI bridge? */
    uBusDevFn = 0;

    PGIC           pGic     = VM_TO_GIC(pVM);
    PPDMDEVINS     pDevIns  = pGic->CTX_SUFF(pDevIns);
    PGICDEV        pGicDev  = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    uint16_t const uEventId = pMsi->Data.u32;
    uint16_t const uDevId   = uBusDevFn;
    AssertMsg((pMsi->Addr.u64 & ~(RTGCPHYS)GITS_REG_OFFSET_MASK) == pGicDev->GCPhysGits + GITS_REG_FRAME_SIZE,
              ("Addr=%#RX64 MMIO frame=%#RX64\n", pMsi->Addr.u64, pGicDev->GCPhysGits));
    AssertMsg((pMsi->Addr.u64 & GITS_REG_OFFSET_MASK) == GITS_TRANSLATION_REG_TRANSLATER,
              ("Addr=%#RX64 offset=%#RX32\n", pMsi->Addr.u64, GITS_TRANSLATION_REG_TRANSLATER));
    STAM_COUNTER_INC(&pGicDev->StatSetLpi);

    /*
     * When LPIs are disabled (GICR_CTLR.EnableLpis bit), they cannot be made pending and are lost.[1]
     * When LPIs are enabled, we update the LPI pending state and if the LPI is newly asserted, we
     * update redistributor IRQ state as we well as we might have to signal the LPI to the PE.
     *
     * [1] -- ARM GIC spec. 5.1 "LPIs".
     */
    GIC_CRIT_SECT_ENTER(pDevIns);
    if (pGicDev->fEnableLpis)
        gitsLpiTrigger(pVM, pDevIns, &pGicDev->Gits, uDevId, uEventId, true /* fAsserted */);
    GIC_CRIT_SECT_LEAVE(pDevIns);

    return VINF_SUCCESS;
}


/**
 * Sets the specified software generated interrupt (SGI).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pDestCpuSet     Which CPUs to deliver the SGI to.
 * @param   uIntId          The SGI interrupt ID.
 */
static VBOXSTRICTRC gicSetSgi(PVMCPUCC pVCpu, PCVMCPUSET pDestCpuSet, uint8_t uIntId)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} uIntId=%u\n", pVCpu, pVCpu->idCpu, uIntId));

#ifdef RT_STRICT
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    Assert(GIC_CRIT_SECT_IS_OWNER(pDevIns));
    AssertReturn(uIntId <= GIC_INTID_RANGE_SGI_LAST, VERR_INVALID_PARAMETER);
#endif

    PCVMCC         pVM   = pVCpu->CTX_SUFF(pVM);
    uint32_t const cCpus = pVM->cCpus;
    for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
    {
        if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
        {
            PVMCPUCC pVCpuTarget = pVM->CTX_SUFF(apCpus)[idCpu];
            PGICCPU  pGicCpu     = VMCPU_TO_GICCPU(pVCpuTarget);
            pGicCpu->bmIntrPending[0] |= RT_BIT_32(uIntId);
            gicReDistUpdateIrqState(pVCpuTarget);
        }
    }
    return gicDistUpdateIrqState(pVM);
}


/**
 * Writes to the redistributor's SGI group 1 register (ICC_SGI1R_EL1).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uValue      The value being written to the ICC_SGI1R_EL1 register.
 */
static VBOXSTRICTRC gicReDistWriteSgiReg(PVMCPUCC pVCpu, uint64_t uValue)
{
#ifdef VBOX_WITH_STATISTICS
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    STAM_COUNTER_INC(&pVCpu->gic.s.StatSetSgi);
    STAM_PROFILE_START(&pGicCpu->StatProfSetSgi, c);
#else
    PCGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
#endif

    VMCPUSET DestCpuSet;
    if (uValue & ARMV8_ICC_SGI1R_EL1_AARCH64_IRM)
    {
        /*
         * Deliver to all VCPUs but this one.
         */
        VMCPUSET_FILL(&DestCpuSet);
        VMCPUSET_DEL(&DestCpuSet, pVCpu->idCpu);
    }
    else
    {
        /*
         * Target specific VCPUs.
         * See ARM GICv3 and GICv4 Software Overview spec 3.3 "Affinity routing".
         */
        VMCPUSET_EMPTY(&DestCpuSet);
        bool const     fRangeSelSupport = RT_BOOL(pGicCpu->uIccCtlr & ARMV8_ICC_CTLR_EL1_AARCH64_RSS);
        uint8_t const  idRangeStart     = ARMV8_ICC_SGI1R_EL1_AARCH64_RS_GET(uValue) * 16;
        uint16_t const bmCpuInterfaces  = ARMV8_ICC_SGI1R_EL1_AARCH64_TARGET_LIST_GET(uValue);
        uint8_t const  uAff1            = ARMV8_ICC_SGI1R_EL1_AARCH64_AFF1_GET(uValue);
        uint8_t const  uAff2            = ARMV8_ICC_SGI1R_EL1_AARCH64_AFF2_GET(uValue);
        uint8_t const  uAff3            = (pGicCpu->uIccCtlr & ARMV8_ICC_CTLR_EL1_AARCH64_A3V)
                                        ? ARMV8_ICC_SGI1R_EL1_AARCH64_AFF3_GET(uValue)
                                        : 0;
        uint32_t const cCpus            = pVCpu->CTX_SUFF(pVM)->cCpus;
        for (uint8_t idCpuInterface = 0; idCpuInterface < 16; idCpuInterface++)
        {
            if (bmCpuInterfaces & RT_BIT(idCpuInterface))
            {
                VMCPUID idCpuTarget;
                if (fRangeSelSupport)
                    idCpuTarget = RT_MAKE_U32_FROM_U8(idRangeStart + idCpuInterface, uAff1, uAff2, uAff3);
                else
                    idCpuTarget = gicGetCpuIdFromAffinity(idCpuInterface, uAff1, uAff2, uAff3);
                if (RT_LIKELY(idCpuTarget < cCpus))
                    VMCPUSET_ADD(&DestCpuSet, idCpuTarget);
                else
                    AssertReleaseMsgFailed(("VCPU ID out-of-bounds %RU32, must be < %u\n", idCpuTarget, cCpus));
            }
        }
    }

    if (!VMCPUSET_IS_EMPTY(&DestCpuSet))
    {
        uint8_t const uSgiIntId = ARMV8_ICC_SGI1R_EL1_AARCH64_INTID_GET(uValue);
        Assert(GIC_IS_INTR_SGI(uSgiIntId));
        VBOXSTRICTRC const rcStrict = gicSetSgi(pVCpu, &DestCpuSet, uSgiIntId);
        Assert(RT_SUCCESS(rcStrict)); RT_NOREF_PV(rcStrict);
    }

    STAM_PROFILE_STOP(&pGicCpu->StatProfSetSgi, c);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMGICBACKEND,pfnReadSysReg}
 */
static DECLCALLBACK(VBOXSTRICTRC) gicReadSysReg(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t *pu64Value)
{
    /*
     * Validate.
     */
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pu64Value);

    STAM_COUNTER_INC(&pVCpu->gic.s.StatSysRegRead);

    *pu64Value = 0;
    PGICCPU    pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);

    GIC_CRIT_SECT_ENTER(pDevIns);

    switch (u32Reg)
    {
        case ARMV8_AARCH64_SYSREG_ICC_PMR_EL1:
            *pu64Value = pGicCpu->bIntrPriorityMask;
            break;

        case ARMV8_AARCH64_SYSREG_ICC_BPR0_EL1:
            *pu64Value = ARMV8_ICC_BPR0_EL1_AARCH64_BINARYPOINT_SET(pGicCpu->bBinaryPtGroup0);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_AP0R0_EL1:
            *pu64Value = pGicCpu->bmActivePriorityGroup0[0];
            break;

        case ARMV8_AARCH64_SYSREG_ICC_AP0R1_EL1:
            AssertReleaseFailed();
            *pu64Value = pGicCpu->bmActivePriorityGroup0[1];
            break;

        case ARMV8_AARCH64_SYSREG_ICC_AP0R2_EL1:
            AssertReleaseFailed();
            *pu64Value = pGicCpu->bmActivePriorityGroup0[2];
            break;

        case ARMV8_AARCH64_SYSREG_ICC_AP0R3_EL1:
            AssertReleaseFailed();
            *pu64Value = pGicCpu->bmActivePriorityGroup0[3];
            break;

        case ARMV8_AARCH64_SYSREG_ICC_AP1R0_EL1:
            AssertReleaseFailed();
            *pu64Value = pGicCpu->bmActivePriorityGroup1[0];
            break;

        case ARMV8_AARCH64_SYSREG_ICC_AP1R1_EL1:
            AssertReleaseFailed();
            *pu64Value = pGicCpu->bmActivePriorityGroup1[1];
            break;

        case ARMV8_AARCH64_SYSREG_ICC_AP1R2_EL1:
            AssertReleaseFailed();
            *pu64Value = pGicCpu->bmActivePriorityGroup1[2];
            break;

        case ARMV8_AARCH64_SYSREG_ICC_AP1R3_EL1:
            AssertReleaseFailed();
            *pu64Value = pGicCpu->bmActivePriorityGroup1[3];
            break;

        case ARMV8_AARCH64_SYSREG_ICC_RPR_EL1:
            Assert(    pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority] == GIC_IDLE_PRIORITY
                   || !(pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority] & RT_BIT(0)));
            *pu64Value = pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority];
            break;

        case ARMV8_AARCH64_SYSREG_ICC_IAR1_EL1:
            *pu64Value = gicAckHighestPriorityPendingIntr(pVCpu, GIC_INTR_GROUP_1NS);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_HPPIR1_EL1:
            *pu64Value = gicGetHighestPriorityPendingIntr(pVCpu, GIC_INTR_GROUP_1NS, NULL /*pIntr*/);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_BPR1_EL1:
            *pu64Value = ARMV8_ICC_BPR1_EL1_AARCH64_BINARYPOINT_SET(pGicCpu->bBinaryPtGroup1);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_CTLR_EL1:
            *pu64Value = pGicCpu->uIccCtlr;
            break;

        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN0_EL1:
            *pu64Value = (pGicCpu->fIntrGroupMask & GIC_INTR_GROUP_0) ? ARMV8_ICC_IGRPEN0_EL1_AARCH64_ENABLE : 0;
            break;

        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN1_EL1:
            *pu64Value = (pGicCpu->fIntrGroupMask & (GIC_INTR_GROUP_1NS | GIC_INTR_GROUP_1S))
                       ? ARMV8_ICC_IGRPEN1_EL1_AARCH64_ENABLE : 0;
            break;

        default:
            AssertReleaseMsgFailed(("u32Reg=%#RX32\n", u32Reg));
            break;
    }

    GIC_CRIT_SECT_LEAVE(pDevIns);

    LogFlowFunc(("pVCpu=%p u32Reg=%#x{%s} pu64Value=%RX64\n", pVCpu, u32Reg, gicIccGetRegDescription(u32Reg), *pu64Value));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMGICBACKEND,pfnWriteSysReg}
 */
static DECLCALLBACK(VBOXSTRICTRC) gicWriteSysReg(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t u64Value)
{
    /*
     * Validate.
     */
    VMCPU_ASSERT_EMT(pVCpu);
    LogFlowFunc(("pVCpu=%p u32Reg=%#x{%s} u64Value=%RX64\n", pVCpu, u32Reg, gicIccGetRegDescription(u32Reg), u64Value));

    STAM_COUNTER_INC(&pVCpu->gic.s.StatSysRegWrite);

    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    PGICCPU    pGicCpu = VMCPU_TO_GICCPU(pVCpu);

    GIC_CRIT_SECT_ENTER(pDevIns);

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    switch (u32Reg)
    {
        case ARMV8_AARCH64_SYSREG_ICC_PMR_EL1:
            pGicCpu->bIntrPriorityMask = (uint8_t)u64Value;
            rcStrict = gicReDistUpdateIrqState(pVCpu);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_BPR0_EL1:
            pGicCpu->bBinaryPtGroup0 = (uint8_t)ARMV8_ICC_BPR0_EL1_AARCH64_BINARYPOINT_GET(u64Value);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_AP0R0_EL1:
        case ARMV8_AARCH64_SYSREG_ICC_AP0R1_EL1:
        case ARMV8_AARCH64_SYSREG_ICC_AP0R2_EL1:
        case ARMV8_AARCH64_SYSREG_ICC_AP0R3_EL1:
        case ARMV8_AARCH64_SYSREG_ICC_AP1R0_EL1:
        case ARMV8_AARCH64_SYSREG_ICC_AP1R1_EL1:
        case ARMV8_AARCH64_SYSREG_ICC_AP1R2_EL1:
        case ARMV8_AARCH64_SYSREG_ICC_AP1R3_EL1:
            /* Writes ignored, well behaving guest would write all 0s or the last read value of the register. */
            break;

        case ARMV8_AARCH64_SYSREG_ICC_SGI1R_EL1:
            gicReDistWriteSgiReg(pVCpu, u64Value);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_EOIR1_EL1:
        {
            /*
             * We only support priority drop + interrupt deactivation with writes to this register.
             * This avoids an extra access which would be required by software for deactivation.
             */
            Assert(!(pGicCpu->uIccCtlr & ARMV8_ICC_CTLR_EL1_AARCH64_EOIMODE));

            /*
             * Mark the interrupt as inactive, though it might still be pending.
             * It is up to the guest to ensure the interrupt ID belongs to the right group as
             * failure to do so results in unpredictable behavior.
             *
             * See ARM GIC spec. 12.2.10 "ICC_EOIR1_EL1, Interrupt Controller End Of Interrupt Register 1".
             * NOTE! The order of the 'if' checks below are crucial.
             */
            bool fIsRedistIntId = true;
            uint16_t const uIntId = (uint16_t)u64Value;
            if (uIntId <= GIC_INTID_RANGE_PPI_LAST)
            {
                /* SGIs and PPIs. */
                AssertCompile(GIC_INTID_RANGE_PPI_LAST < 8 * sizeof(pGicCpu->bmIntrActive[0]));
                Assert(pGicDev->fAffRouting);
                pGicCpu->bmIntrActive[0] &= ~RT_BIT_32(uIntId);
            }
            else if (uIntId <= GIC_INTID_RANGE_SPI_LAST)
            {
                /* SPIs. */
                uint16_t const idxIntr = /*gicDistGetIndexFromIntId*/(uIntId);
                AssertReturn(idxIntr < sizeof(pGicDev->IntrActive) * 8, VERR_BUFFER_OVERFLOW);
                gicBitmapPutBit(&pGicDev->IntrActive.au32[0], idxIntr, false /* fSet */);
                fIsRedistIntId = false;
            }
            else if (uIntId <= GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT)
            {
                /* Special interrupt IDs, ignored. */
                Log(("Ignoring write to EOI with special interrupt ID.\n"));
                break;
            }
            else if (uIntId <= GIC_INTID_RANGE_EXT_PPI_LAST)
            {
                /* Extended PPIs. */
                uint16_t const idxIntr = gicReDistGetIndexFromIntId(uIntId);
                AssertReturn(idxIntr < sizeof(pGicCpu->bmIntrActive) * 8, VERR_BUFFER_OVERFLOW);
                gicBitmapPutBit(&pGicCpu->bmIntrActive[0], idxIntr, false /* fSet */);
            }
            else if (uIntId <= GIC_INTID_RANGE_EXT_SPI_LAST)
            {
                /* Extended SPIs. */
                uint16_t const idxIntr = gicDistGetIndexFromIntId(uIntId);
                AssertReturn(idxIntr < sizeof(pGicDev->IntrActive) * 8, VERR_BUFFER_OVERFLOW);
                gicBitmapPutBit(&pGicDev->IntrActive.au32[0], idxIntr, false /* fSet */);
                fIsRedistIntId = false;
            }
            else if (uIntId <= GIC_INTID_RANGE_LPI_START + RT_ELEMENTS(pGicDev->abLpiConfig) - 1)
            {
                /* LPIs. LPIs don't have an active state so there's nothing to do here. */
            }
            else
            {
                AssertMsgFailed(("Invalid INTID %u\n", uIntId));
                break;
            }

            /*
             * Drop priority by restoring previous interrupt.
             */
            if (RT_LIKELY(pGicCpu->idxRunningPriority > 0))
            {
                LogFlowFunc(("Restoring interrupt priority from %u -> %u (idxRunningPriority: %u -> %u)\n",
                             pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority],
                             pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority - 1],
                             pGicCpu->idxRunningPriority, pGicCpu->idxRunningPriority - 1));

                /*
                 * Clear the interrupt priority from the active priorities bitmap.
                 * It is up to the guest to ensure that writes to EOI registers are done in the exact
                 * reverse order of the reads from the IAR registers.
                 *
                 * See ARM GIC spec 4.1.1 "Physical CPU interface".
                 */
                uint8_t const idxPreemptionLevel = pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority] >> 1;
                AssertCompile(sizeof(pGicCpu->bmActivePriorityGroup1) * 8 >= 128);
                gicBitmapPutBit(&pGicCpu->bmActivePriorityGroup1[0], idxPreemptionLevel, false /* fSet */);

                pGicCpu->idxRunningPriority--;
                Assert(pGicCpu->abRunningPriorities[0] == GIC_IDLE_PRIORITY);
                STAM_COUNTER_INC(&pVCpu->gic.s.StatIntrEoi);
            }
            else
                AssertReleaseMsgFailed(("Index of running-priority interrupt out-of-bounds %u\n", pGicCpu->idxRunningPriority));

            /*
             * Always update the full state here as there might not have been anything
             * pending in the distributor but now might be. Just because a redistributor
             * interrupt was EOI'd doesn't mean we might not be in a situation that
             * requires flagging of a distributor interrupt.
             */
            rcStrict = gicDistUpdateIrqState(pVCpu->CTX_SUFF(pVM));
            break;
        }

        case ARMV8_AARCH64_SYSREG_ICC_BPR1_EL1:
            pGicCpu->bBinaryPtGroup1 = (uint8_t)ARMV8_ICC_BPR1_EL1_AARCH64_BINARYPOINT_GET(u64Value);
            /* We don't support dual security states, hence the minimum value is 1. */
            if (!pGicCpu->bBinaryPtGroup1)
                pGicCpu->bBinaryPtGroup1 = 1;
            break;

        case ARMV8_AARCH64_SYSREG_ICC_CTLR_EL1:
            /* We don't support dual security states, so we use the "DS_RW" read/write mask. */
            GIC_SET_REG_U64_FULL(pGicCpu->uIccCtlr, u64Value, ARMV8_ICC_CTLR_EL1_DS_RW);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN0_EL1:
        {
            uint32_t const fIntrGroupMask = (pGicCpu->fIntrGroupMask & ~GIC_INTR_GROUP_0)
                                          | (u64Value & ARMV8_ICC_IGRPEN0_EL1_AARCH64_ENABLE) ? GIC_INTR_GROUP_0 : 0;
            if (pGicCpu->fIntrGroupMask != fIntrGroupMask)
            {
                pGicCpu->fIntrGroupMask = fIntrGroupMask;
                rcStrict = gicReDistUpdateIrqState(pVCpu);
            }
            break;
        }

        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN1_EL1:
        {
            uint32_t const fIntrGroupMask = (pGicCpu->fIntrGroupMask & ~GIC_INTR_GROUP_1NS)
                                          | (u64Value & ARMV8_ICC_IGRPEN0_EL1_AARCH64_ENABLE) ? GIC_INTR_GROUP_1NS : 0;
            if (pGicCpu->fIntrGroupMask != fIntrGroupMask)
            {
                pGicCpu->fIntrGroupMask = fIntrGroupMask;
                rcStrict = gicReDistUpdateIrqState(pVCpu);
            }
            break;
        }

        default:
            AssertReleaseMsgFailed(("u32Reg=%#RX32 uValue=%#RX64\n", u32Reg, u64Value));
            break;
    }

    GIC_CRIT_SECT_LEAVE(pDevIns);
    return rcStrict;
}


/**
 * Initializes the GIC distributor state.
 *
 * @param   pDevIns     The device instance.
 * @remarks This is also called during VM reset, so do NOT remove values that are
 *          cleared to zero!
 */
static void gicInit(PPDMDEVINS pDevIns)
{
    LogFlowFunc(("\n"));
    PGICDEV  pGicDev  = PDMDEVINS_2_DATA(pDevIns, PGICDEV);

    /* Distributor. */
    RT_ZERO(pGicDev->IntrGroup);
    RT_ZERO(pGicDev->IntrConfig);
    RT_ZERO(pGicDev->IntrEnabled);
    RT_ZERO(pGicDev->IntrPending);
    RT_ZERO(pGicDev->IntrActive);
    RT_ZERO(pGicDev->IntrLevel);
    RT_ZERO(pGicDev->IntrRoutingMode);
    RT_ZERO(pGicDev->au32IntrRouting);
    RT_ZERO(pGicDev->abIntrPriority);
    pGicDev->fIntrGroupMask = 0;
    pGicDev->fAffRouting = true; /* GICv2 backwards compatibility is not implemented, so this is RA1/WI. */

    /* LPIs. */
    pGicDev->fEnableLpis = false;
    pGicDev->uLpiConfigBaseReg.u = 0;
    pGicDev->uLpiPendingBaseReg.u = 0;
    RT_ZERO(pGicDev->abLpiConfig);

    /* GITS. */
    PGITSDEV pGitsDev = &pGicDev->Gits;
    gitsInit(pGitsDev);
}


/**
 * Initializes the GIC redistributor and CPU interface state.
 *
 * @param   pDevIns     The device instance.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @remarks This is also called during VM reset, so do NOT remove values that are
 *          cleared to zero!
 */
static void gicInitCpu(PPDMDEVINS pDevIns, PVMCPUCC pVCpu)
{
    LogFlowFunc(("[%u]\n", pVCpu->idCpu));
    PGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    PGICCPU pGicCpu = &pVCpu->gic.s;

    RT_ZERO(pGicCpu->bmIntrGroup);
    RT_ZERO(pGicCpu->bmIntrConfig);
    /* SGIs are always edge-triggered, writes to GICR_ICFGR0 are to be ignored. */
    pGicCpu->bmIntrConfig[0] = 0xffff;
    RT_ZERO(pGicCpu->bmIntrEnabled);
    RT_ZERO(pGicCpu->bmIntrPending);
    RT_ZERO(pGicCpu->bmIntrActive);
    RT_ZERO(pGicCpu->bmIntrLevel);
    RT_ZERO(pGicCpu->abIntrPriority);

    pGicCpu->uIccCtlr = ARMV8_ICC_CTLR_EL1_AARCH64_PMHE
                      | ARMV8_ICC_CTLR_EL1_AARCH64_PRIBITS_SET(4)
                      | ARMV8_ICC_CTLR_EL1_AARCH64_IDBITS_SET(ARMV8_ICC_CTLR_EL1_AARCH64_IDBITS_16BITS)
                      | (pGicDev->fRangeSel   ? ARMV8_ICC_CTLR_EL1_AARCH64_RSS : 0)
                      | (pGicDev->fAff3Levels ? ARMV8_ICC_CTLR_EL1_AARCH64_A3V : 0)
                      | (pGicDev->fExtPpi || pGicDev->fExtSpi ? ARMV8_ICC_CTLR_EL1_AARCH64_EXTRANGE : 0);

    pGicCpu->bIntrPriorityMask  = 0; /* Means no interrupt gets through to the PE. */
    pGicCpu->idxRunningPriority = 0;
    memset((void *)&pGicCpu->abRunningPriorities[0], 0xff, sizeof(pGicCpu->abRunningPriorities));
    memset((void *)&pGicCpu->abRunningIntId[0], 0xff, sizeof(pGicCpu->abRunningIntId));
    RT_ZERO(pGicCpu->bmActivePriorityGroup0);
    RT_ZERO(pGicCpu->bmActivePriorityGroup1);
    pGicCpu->bBinaryPtGroup0    = 0;
    pGicCpu->bBinaryPtGroup1    = 1; /* We don't support dual security state, minimum value is 1. */
    pGicCpu->fIntrGroupMask     = 0;
    RT_ZERO(pGicCpu->LpiPending);
}


/**
 * Initializes per-VM GIC to the state following a power-up or hardware
 * reset.
 *
 * @param   pDevIns     The device instance.
 */
DECLHIDDEN(void) gicReset(PPDMDEVINS pDevIns)
{
    LogFlowFunc(("\n"));
    gicInit(pDevIns);
}


/**
 * Initializes per-VCPU GIC to the state following a power-up or hardware
 * reset.
 *
 * @param   pDevIns     The device instance.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLHIDDEN(void) gicResetCpu(PPDMDEVINS pDevIns, PVMCPUCC pVCpu)
{
    LogFlowFunc(("[%u]\n", pVCpu->idCpu));
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    gicInitCpu(pDevIns, pVCpu);
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicDistMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 4); RT_NOREF_PV(cb);

    PVMCPUCC pVCpu  = PDMDevHlpGetVMCPU(pDevIns);
    uint16_t offReg = off & 0xfffc;
    uint32_t uValue = 0;

    STAM_COUNTER_INC(&pVCpu->gic.s.StatMmioRead);

    VBOXSTRICTRC rc = VBOXSTRICTRC_VAL(gicDistReadRegister(pDevIns, pVCpu, offReg, &uValue));
    *(uint32_t *)pv = uValue;

    LogFlowFunc(("[%u]: offReg=%#RX16 (%s) uValue=%#RX32\n", pVCpu->idCpu, offReg, gicDistGetRegDescription(offReg), uValue));
    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicDistMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 4); RT_NOREF_PV(cb);

    PVMCPUCC pVCpu  = PDMDevHlpGetVMCPU(pDevIns);
    uint16_t offReg = off & 0xfffc;
    uint32_t uValue = *(uint32_t *)pv;

    STAM_COUNTER_INC(&pVCpu->gic.s.StatMmioWrite);
    LogFlowFunc(("[%u]: offReg=%#RX16 (%s) uValue=%#RX32\n", pVCpu->idCpu, offReg, gicDistGetRegDescription(offReg), uValue));

    return gicDistWriteRegister(pDevIns, pVCpu, offReg, uValue);
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicReDistMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 4); RT_NOREF_PV(cb);

    /*
     * Determine the redistributor being targeted. Each redistributor takes
     * GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE bytes
     * and the redistributors are adjacent.
     */
    uint32_t const idReDist = off / (GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE);
    off %= (GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE);

    PVMCC pVM = PDMDevHlpGetVM(pDevIns);
    Assert(idReDist < pVM->cCpus);
    PVMCPUCC pVCpu = pVM->CTX_SUFF(apCpus)[idReDist];

    STAM_COUNTER_INC(&pVCpu->gic.s.StatMmioRead);

    /* Redistributor or SGI/PPI frame? */
    uint16_t const offReg = off & 0xfffc;
    uint32_t       uValue = 0;
    VBOXSTRICTRC rcStrict;
    if (off < GIC_REDIST_REG_FRAME_SIZE)
        rcStrict = gicReDistReadRegister(pDevIns, pVCpu, idReDist, offReg, &uValue);
    else
        rcStrict = gicReDistReadSgiPpiRegister(pDevIns, pVCpu, offReg, &uValue);

    *(uint32_t *)pv = uValue;
    LogFlowFunc(("[%u]: off=%RGp idReDist=%u offReg=%#RX16 (%s) uValue=%#RX32 -> %Rrc\n", pVCpu->idCpu, off, idReDist, offReg,
                 gicReDistGetRegDescription(offReg), uValue, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicReDistMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 4); RT_NOREF_PV(cb);

    uint32_t uValue = *(uint32_t *)pv;

    /*
     * Determine the redistributor being targeted. Each redistributor takes
     * GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE bytes
     * and the redistributors are adjacent.
     */
    uint32_t const idReDist = off / (GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE);
    off %= (GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE);

    PCVMCC pVM = PDMDevHlpGetVM(pDevIns);
    Assert(idReDist < pVM->cCpus);
    PVMCPUCC pVCpu = pVM->CTX_SUFF(apCpus)[idReDist];

    STAM_COUNTER_INC(&pVCpu->gic.s.StatMmioWrite);

    /* Redistributor or SGI/PPI frame? */
    uint16_t const offReg = off & 0xfffc;
    VBOXSTRICTRC rcStrict;
    if (off < GIC_REDIST_REG_FRAME_SIZE)
        rcStrict = gicReDistWriteRegister(pDevIns, pVCpu, offReg, uValue);
    else
        rcStrict = gicReDistWriteSgiPpiRegister(pDevIns, pVCpu, offReg, uValue);

    LogFlowFunc(("[%u]: off=%RGp idReDist=%u offReg=%#RX16 (%s) uValue=%#RX32 -> %Rrc\n", pVCpu->idCpu, off, idReDist, offReg,
                 gicReDistGetRegDescription(offReg), uValue, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicItsMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    RT_NOREF_PV(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 8 || cb == 4);

    PCGICDEV  pGicDev  = PDMDEVINS_2_DATA(pDevIns, PCGICDEV);
    PCGITSDEV pGitsDev = &pGicDev->Gits;
    uint64_t  uReg;
    if (off < GITS_REG_FRAME_SIZE)
    {
        /* Control registers space. */
        uint16_t const offReg = off & 0xfffc;
        uReg = gitsMmioReadCtrl(pGitsDev, offReg, cb);
        LogFlowFunc(("offReg=%#RX16 (%s) read %#RX64\n", offReg, gitsGetCtrlRegDescription(offReg), uReg));
    }
    else
    {
        /* Translation registers space. */
        uint16_t const offReg = (off - GITS_REG_FRAME_SIZE) & 0xfffc;
        uReg = gitsMmioReadTranslate(pGitsDev, offReg, cb);
        LogFlowFunc(("offReg=%#RX16 (%s) read %#RX64\n", offReg, gitsGetTranslationRegDescription(offReg), uReg));
    }

    if (cb == 8)
        *(uint64_t *)pv = uReg;
    else
        *(uint32_t *)pv = uReg;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicItsMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    RT_NOREF_PV(pvUser);
    Assert(!(off & 0x3));
    Assert(cb == 8 || cb == 4);

    PGICDEV  pGicDev  = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    PGITSDEV pGitsDev = &pGicDev->Gits;

    uint64_t const uValue = cb == 8 ? *(uint64_t *)pv : *(uint32_t *)pv;
    if (off < GITS_REG_FRAME_SIZE)
    {
        /* Control registers space. */
        uint16_t const offReg = off & 0xfffc;
        gitsMmioWriteCtrl(pDevIns, pGitsDev, offReg, uValue, cb);
        LogFlowFunc(("offReg=%#RX16 (%s) written %#RX64\n", offReg, gitsGetCtrlRegDescription(offReg), uValue));
    }
    else
    {
        /* Translation registers space. */
        uint16_t const offReg = (off - GITS_REG_FRAME_SIZE) & 0xfffc;
        gitsMmioWriteTranslate(pGitsDev, offReg, uValue, cb);
        LogFlowFunc(("offReg=%#RX16 (%s) written %#RX64\n", offReg, gitsGetTranslationRegDescription(offReg), uValue));
    }
    return VINF_SUCCESS;
}


/**
 * GIC device registration structure.
 */
const PDMDEVREG g_DeviceGIC =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "gic",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_PIC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(GICDEV),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Generic Interrupt Controller",
#if defined(IN_RING3)
    /* .szRCMod = */                "VMMRC.rc",
    /* .szR0Mod = */                "VMMR0.r0",
    /* .pfnConstruct = */           gicR3Construct,
    /* .pfnDestruct = */            gicR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               gicR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           NULL,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};


/**
 * The VirtualBox GIC backend.
 */
const PDMGICBACKEND g_GicBackend =
{
    /* .pfnReadSysReg = */  gicReadSysReg,
    /* .pfnWriteSysReg = */ gicWriteSysReg,
    /* .pfnSetSpi = */      gicSetSpi,
    /* .pfnSetPpi = */      gicSetPpi,
    /* .pfnSendMsi = */     gicSendMsi,
};

