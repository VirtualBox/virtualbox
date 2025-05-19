/* $Id: GICAll.cpp 168928 2025-05-19 13:23:23Z ramshankar.venkataraman@oracle.com $ */
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
#include <VBox/vmm/pdmgic.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vmcpuset.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define GIC_IDLE_PRIORITY                       0xff
#define GIC_IS_INTR_SGI(a_uIntId)               ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_SGI_START < (uint32_t)GIC_INTID_SGI_RANGE_SIZE)
#define GIC_IS_INTR_PPI(a_uIntId)               ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_PPI_START < (uint32_t)GIC_INTID_PPI_RANGE_SIZE)
#define GIC_IS_INTR_SGI_OR_PPI(a_uIntId)        ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_SGI_START < (uint32_t)(GIC_INTID_SGI_RANGE_SIZE + GIC_INTID_PPI_RANGE_SIZE))
#define GIC_IS_INTR_SPI(a_uIntId)               ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_SPI_START < (uint32_t)GIC_INTID_SPI_RANGE_SIZE)
#define GIC_IS_INTR_SPECIAL(a_uIntId)           ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_SPECIAL_START < (uint32_t)GIC_INTID_SPECIAL_RANGE_SIZE)
#define GIC_IS_INTR_EXT_PPI(a_uIntId)           ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_EXT_PPI_START < (uint32_t)GIC_INTID_EXT_PPI_RANGE_SIZE)
#define GIC_IS_INTR_EXT_SPI(a_uIntId)           ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_EXT_SPI_START < (uint32_t)GIC_INTID_EXT_SPI_RANGE_SIZE)
#define GIC_IS_INTR_LPI(a_pGicDev, a_uIntId)    ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_LPI_START < (uint32_t)RT_ELEMENTS((a_pGicDev)->abLpiConfig))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * GIC interrupt groups.
 */
typedef enum GICINTRGROUP
{
    GICINTRGROUP_0 = 0,
    GICINTRGROUP_1S,
    GICINTRGROUP_1NS
} GICINTRGROUP;

/**
 * GIC interrupt.
 */
typedef struct GICINTR
{
    /** The interrupt group. */
    GICINTRGROUP    enmGroup;
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
 * Gets the description of an interrupt group.
 *
 * @returns The group description.
 * @param enmGroup      The interrupt group.
 */
static const char *gicIntrGroupDescription(GICINTRGROUP enmGroup)
{
    switch (enmGroup)
    {
        case GICINTRGROUP_0:    return "Group 0";
        case GICINTRGROUP_1S:   return "Group 1 (Secure)";
        case GICINTRGROUP_1NS:  return "Group 1 (Non-Secure)";
        default:                return "<UNKNOWN>";
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
    Assert(idxIntr < sizeof(GICDEV::bmIntrPending) * 8);
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
 * Sets the interrupt pending force-flag and pokes the EMT if required.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fIrq            Flag whether to assert the IRQ line or leave it alone.
 * @param   fFiq            Flag whether to assert the FIQ line or leave it alone.
 */
static void gicSetInterruptFF(PVMCPUCC pVCpu, bool fIrq, bool fFiq)
{
    Assert(fIrq || fFiq);
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} fIrq=%RTbool fFiq=%RTbool\n", pVCpu, pVCpu->idCpu, fIrq, fFiq));

#ifdef IN_RING3
    /* IRQ state should be loaded as-is by "LoadExec". Changes can be made from LoadDone. */
    Assert(pVCpu->pVMR3->enmVMState != VMSTATE_LOADING || PDMR3HasLoadedState(pVCpu->pVMR3));
#endif

    if (fIrq)
        VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ);
    if (fFiq)
        VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_FIQ);

    /*
     * We need to wake up the target CPU if we're not on EMT.
     */
    /** @todo We could just use RTThreadNativeSelf() here, couldn't we? */
#if defined(IN_RING0)
# error "Implement me!"
#elif defined(IN_RING3)
    PVMCC   pVM   = pVCpu->CTX_SUFF(pVM);
    VMCPUID idCpu = pVCpu->idCpu;
    if (VMMGetCpuId(pVM) != idCpu)
    {
        Log7Func(("idCpu=%u enmState=%d\n", idCpu, pVCpu->enmState));
        VMR3NotifyCpuFFU(pVCpu->pUVCpu, VMNOTIFYFF_FLAGS_POKE);
    }
#endif
}


/**
 * Clears the interrupt pending force-flag.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fIrq    Flag whether to clear the IRQ flag.
 * @param   fFiq    Flag whether to clear the FIQ flag.
 */
DECLINLINE(void) gicClearInterruptFF(PVMCPUCC pVCpu, bool fIrq, bool fFiq)
{
    Assert(fIrq || fFiq);
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} fIrq=%RTbool fFiq=%RTbool\n", pVCpu, pVCpu->idCpu, fIrq, fFiq));

#ifdef IN_RING3
    /* IRQ state should be loaded as-is by "LoadExec". Changes can be made from LoadDone. */
    Assert(pVCpu->pVMR3->enmVMState != VMSTATE_LOADING || PDMR3HasLoadedState(pVCpu->pVMR3));
#endif

    if (fIrq)
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_IRQ);
    if (fFiq)
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_FIQ);
}


/**
 * Updates the interrupt force-flag.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fIrq    Flag whether to clear the IRQ flag.
 * @param   fFiq    Flag whether to clear the FIQ flag.
 */
DECLINLINE(void) gicUpdateInterruptFF(PVMCPUCC pVCpu, bool fIrq, bool fFiq)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} fIrq=%RTbool fFiq=%RTbool\n", pVCpu, pVCpu->idCpu, fIrq, fFiq));

    if (fIrq || fFiq)
        gicSetInterruptFF(pVCpu, fIrq, fFiq);

    if (!fIrq || !fFiq)
        gicClearInterruptFF(pVCpu, !fIrq, !fFiq);
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
static bool gicReDistIsSufficientPriority(PCGICCPU pGicCpu, uint8_t bIntrPriority, bool fGroup0)
{
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
    uint8_t const bPriorityMask    = pGicCpu->bIntrPriorityMask;
    uint8_t const bRunningPriority = pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority];
    if (   bIntrPriority < bPriorityMask
        && bIntrPriority < bRunningPriority)
    {
        /*
         * The group priority of the pending interrupt must be higher than that of the running priority.
         * The number of bits for the group priority depends on the the binary point registers.
         * We mask the sub-priority bits and only compare the group priority.
         *
         * When the binary point registers indicates no preemption, we must allow interrupts that have
         * a higher priority than idle. Hence, the use of two different masks below.
         *
         * See ARM GIC spec. 4.8.3 "Priority grouping".
         * See ARM GIC spec. 4.8.5 "Preemption".
         */
        static uint8_t const s_afGroupPriorityMasks[8]   = { 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0xff };
        static uint8_t const s_afRunningPriorityMasks[8] = { 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0xff };
        uint8_t const idxPriorityMask       = (fGroup0 || (pGicCpu->uIccCtlr & ARMV8_ICC_CTLR_EL1_AARCH64_CBPR))
                                            ? pGicCpu->bBinaryPtGroup0 & 7
                                            : pGicCpu->bBinaryPtGroup1 & 7;
        uint8_t const bRunningGroupPriority = bRunningPriority & s_afRunningPriorityMasks[idxPriorityMask];
        uint8_t const bIntrGroupPriority    = bIntrPriority    & s_afGroupPriorityMasks[idxPriorityMask];
        if (bIntrGroupPriority < bRunningGroupPriority)
            return true;
    }
    return false;
}


/**
 * Gets whether the redistributor has pending interrupts with sufficient priority to
 * be signalled to the PE.
 *
 * @param   pGicCpu     The GIC redistributor and CPU interface state.
 * @param   pfIrq       Where to store whether IRQs can be signalled.
 * @param   pfFiq       Where to store whether FIQs can be signalled.
 */
static void gicReDistHasIrqPending(PCGICCPU pGicCpu, bool *pfIrq, bool *pfFiq)
{
    bool const fIsGroup1Enabled = pGicCpu->fIntrGroup1Enabled;
    bool const fIsGroup0Enabled = pGicCpu->fIntrGroup0Enabled;
    LogFlowFunc(("fIsGroup0Enabled=%RTbool fIsGroup1Enabled=%RTbool\n", fIsGroup0Enabled, fIsGroup1Enabled));

# if 1
    uint32_t bmIntrs[3];
    for (uint8_t i = 0; i < RT_ELEMENTS(bmIntrs); i++)
    {
        /* Collect interrupts that are pending, enabled and inactive. */
        bmIntrs[i] = (pGicCpu->bmIntrPending[i] & pGicCpu->bmIntrEnabled[i]) & ~pGicCpu->bmIntrActive[i];

        /* Discard interrupts if the group they belong to is disabled. */
        if (!fIsGroup1Enabled)
            bmIntrs[i] &= ~pGicCpu->bmIntrGroup[i];
        if (!fIsGroup0Enabled)
            bmIntrs[i] &= pGicCpu->bmIntrGroup[i];
    }

    uint32_t const cIntrs  = sizeof(bmIntrs) * 8;
    int32_t        idxIntr = ASMBitFirstSet(&bmIntrs[0], cIntrs);
    AssertCompile(!(cIntrs % 32));
    if (idxIntr >= 0)
    {
        /* Only allow interrupts with higher priority than the current configured and running one. */
        do
        {
            Assert((uint32_t)idxIntr < RT_ELEMENTS(pGicCpu->abIntrPriority));
            Assert((uint32_t)idxIntr < sizeof(pGicCpu->bmIntrGroup) * 8);
            uint8_t const bIntrPriority = pGicCpu->abIntrPriority[idxIntr];
            bool const    fInGroup1     = ASMBitTest(&pGicCpu->bmIntrGroup[0], idxIntr);
            bool const    fInGroup0     = !fInGroup1;
            bool const    fSuffPriority = gicReDistIsSufficientPriority(pGicCpu, bIntrPriority, fInGroup0);
            if (fSuffPriority)
            {
                *pfIrq = fInGroup1 && fIsGroup1Enabled;
                *pfFiq = fInGroup0 && fIsGroup0Enabled;
                return;
            }
            idxIntr = ASMBitNextSet(&bmIntrs[0], cIntrs, idxIntr);
        } while (idxIntr != -1);
    }
#else   /** @todo Measure and pick the faster version. */
    /* Only allow interrupts with higher priority than the current configured and running one. */
    uint8_t const bPriority = RT_MIN(pGicCpu->bIntrPriorityMask, pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority]);

    for (uint8_t i = 0; i < RT_ELEMENTS(pGicCpu->bmIntrPending); i++)
    {
        /* Collect interrupts that are pending, enabled and inactive. */
        uint32_t bmIntr = (pGicCpu->bmIntrPending[i] & pGicCpu->bmIntrEnabled[i]) & ~pGicCpu->bmIntrActive[i];

        /* Discard interrupts if the group they belong to is disabled. */
        if (!fIsGroup1Enabled)
            bmIntr &= ~pGicCpu->bmIntrGroup[i];
        if (!fIsGroup0Enabled)
            bmIntr &= pGicCpu->bmIntrGroup[i];

        /* If the interrupt is higher priority than the running interrupt, return whether to signal an IRQ, FIQ or neither. */
        uint16_t const idxPending = ASMBitFirstSetU32(bmIntr);
        if (idxPending > 0)
        {
            uint16_t const idxIntr = 32 * i + idxPending - 1;
            AssertRelease(idxIntr < RT_ELEMENTS(pGicCpu->abIntrPriority));
            if (pGicCpu->abIntrPriority[idxIntr] < bPriority)
            {
                AssertRelease(idxIntr < sizeof(pGicCpu->bmIntrGroup) * 8);
                bool const fInGroup1 = ASMBitTest(&pGicCpu->bmIntrGroup[0], idxIntr);
                bool const fInGroup0 = !fInGroup1;
                *pfIrq = fInGroup1 && fIsGroup1Enabled;
                *pfFiq = fInGroup0 && fIsGroup0Enabled;
                return;
            }
        }
    }
#endif
    *pfIrq = false;
    *pfFiq = false;
}


/**
 * Gets whether the distributor has pending interrupts with sufficient priority to
 * be signalled to the PE.
 *
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idCpu       The ID of the virtual CPU.
 * @param   pfIrq       Where to store whether there are IRQs can be signalled.
 * @param   pfFiq       Where to store whether there are FIQs can be signalled.
 */
static void gicDistHasIrqPendingForVCpu(PCGICDEV pGicDev, PCVMCPUCC pVCpu, VMCPUID idCpu, bool *pfIrq, bool *pfFiq)
{
    bool const fIsGroup1Enabled = pGicDev->fIntrGroup1Enabled;
    bool const fIsGroup0Enabled = pGicDev->fIntrGroup0Enabled;
    LogFlowFunc(("fIsGroup1Enabled=%RTbool fIsGroup0Enabled=%RTbool\n", fIsGroup1Enabled, fIsGroup0Enabled));

#if 1
    uint32_t bmIntrs[64];
    for (uint8_t i = 0; i < RT_ELEMENTS(bmIntrs); i++)
    {
        /* Collect interrupts that are pending, enabled and inactive. */
        bmIntrs[i] = (pGicDev->bmIntrPending[i] & pGicDev->bmIntrEnabled[i]) & ~pGicDev->bmIntrActive[i];

        /* Discard interrupts if the group they belong to is disabled. */
        if (!fIsGroup1Enabled)
            bmIntrs[i] &= ~pGicDev->bmIntrGroup[i];
        if (!fIsGroup0Enabled)
            bmIntrs[i] &= pGicDev->bmIntrGroup[i];
    }

    /*
     * The distributor's interrupt pending/enabled/active bitmaps have 2048 bits which map
     * SGIs (16), PPIs (16), SPIs (988), reserved SPIs (4) and extended SPIs (1024).
     * Of these, the first 32 bits corresponding to SGIs and PPIs are RAZ/WI when affinity
     * routing is enabled (which it currently is always enabled in our implementation).
     */
    Assert(pGicDev->fAffRoutingEnabled);
    Assert(bmIntrs[0] == 0);
    uint32_t const cIntrs  = sizeof(bmIntrs) * 8;
    int32_t        idxIntr = ASMBitFirstSet(&bmIntrs[0], cIntrs);
    AssertCompile(!(cIntrs % 32));
    if (idxIntr >= 0)
    {
        /* Only allow interrupts with higher priority than the current configured and running one. */
        PCGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
        do
        {
            AssertCompile(RT_ELEMENTS(pGicDev->abIntrPriority) == RT_ELEMENTS(pGicDev->au32IntrRouting));
            Assert((uint32_t)idxIntr < RT_ELEMENTS(pGicDev->abIntrPriority));
            Assert(idxIntr > GIC_INTID_RANGE_PPI_LAST);
            Assert(idxIntr < GIC_INTID_RANGE_SPECIAL_START || idxIntr > GIC_INTID_RANGE_SPECIAL_LAST);
            uint8_t const bIntrPriority = pGicDev->abIntrPriority[idxIntr];
            bool const    fInGroup1     = ASMBitTest(&pGicDev->bmIntrGroup[0], idxIntr);
            bool const    fInGroup0     = !fInGroup1;
            bool const    fSuffPriority = gicReDistIsSufficientPriority(pGicCpu, bIntrPriority, fInGroup0);
            if (   fSuffPriority
                && (   pGicDev->au32IntrRouting[idxIntr] == idCpu
                    || ASMBitTest(&pGicDev->bmIntrRoutingMode[0], idxIntr) == 1))
            {
                *pfFiq = fInGroup0 && fIsGroup0Enabled;
                *pfIrq = fInGroup1 && fIsGroup1Enabled;
                return;
            }
            idxIntr = ASMBitNextSet(&bmIntrs[0], cIntrs, idxIntr);
        } while (idxIntr != -1);
    }
#else   /** @todo Measure and pick the faster version. */
    /* Only allow interrupts with higher priority than the running one. */
    PCGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    uint8_t const bPriority = RT_MIN(pGicCpu->bIntrPriorityMask, pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority]);

    for (uint8_t i = 0; i < RT_ELEMENTS(pGicDev->bmIntrPending); i += 2)
    {
        /* Collect interrupts that are pending, enabled and inactive. */
        uint32_t uLo = (pGicDev->bmIntrPending[i]     & pGicDev->bmIntrEnabled[i])     & ~pGicDev->bmIntrActive[i];
        uint32_t uHi = (pGicDev->bmIntrPending[i + 1] & pGicDev->bmIntrEnabled[i + 1]) & ~pGicDev->bmIntrActive[i + 1];

        /* Discard interrupts if the group they belong to is disabled. */
        if (!fIsGroup1Enabled)
        {
            uLo &= ~pGicDev->bmIntrGroup[i];
            uHi &= ~pGicDev->bmIntrGroup[i + 1];
        }
        if (!fIsGroup0Enabled)
        {
            uLo &= pGicDev->bmIntrGroup[i];
            uHi &= pGicDev->bmIntrGroup[i + 1];
        }

        /* If the interrupt is higher priority than the running interrupt, return whether to signal an IRQ, FIQ or neither. */
        Assert(pGicDev->fAffRoutingEnabled);
        uint64_t const bmIntrPending = RT_MAKE_U64(uLo, uHi);
        uint16_t const idxPending    = ASMBitFirstSetU64(bmIntrPending);
        if (idxPending > 0)
        {
            /*
             * The distributor's interrupt pending/enabled/active bitmaps have 2048 bits which map
             * SGIs (16), PPIs (16), SPIs (988), reserved SPIs (4) and extended SPIs (1024).
             * Of these, the first 32 bits corresponding to SGIs and PPIs are RAZ/WI when affinity
             * routing is enabled (which it always is in our implementation).
             */
            uint32_t const idxIntr = 64 * i + idxPending - 1;
            AssertRelease(idxIntr < RT_ELEMENTS(pGicDev->abIntrPriority));
            if (   pGicDev->abIntrPriority[idxIntr] < bPriority
                && pGicDev->au32IntrRouting[idxIntr] == idCpu)
            {
                Assert(idxIntr > GIC_INTID_RANGE_PPI_LAST);
                AssertRelease(idxIntr < sizeof(pGicDev->bmIntrGroup) * 8);
                bool const fInGroup1 = ASMBitTest(&pGicDev->bmIntrGroup[0], idxIntr);
                bool const fInGroup0 = !fInGroup1;
                *pfFiq = fInGroup0 && fIsGroup0Enabled;
                *pfIrq = fInGroup1 && fIsGroup1Enabled;
                return;
            }
        }
    }
#endif
    *pfIrq = false;
    *pfFiq = false;
}


DECLHIDDEN(bool) gicDistIsLpiValid(PPDMDEVINS pDevIns, uint16_t uIntId)
{
    PGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    return GIC_IS_INTR_LPI(pGicDev, uIntId);
}


DECLHIDDEN(void) gicDistReadLpiConfigTableFromMem(PPDMDEVINS pDevIns)
{
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

    /* Copy the LPI config table from guest memory to our internal cache. */
    Assert(UINT32_C(2) << pGicDev->uMaxLpi == RT_ELEMENTS(pGicDev->abLpiConfig));
    RTGCPHYS const GCPhysLpiConfigTable = pGicDev->uLpiConfigBaseReg.u & GIC_BF_REDIST_REG_PROPBASER_PHYS_ADDR_MASK;
    uint32_t const cbLpiConfigTable     = sizeof(pGicDev->abLpiConfig);

    /** @todo Try releasing and re-acquiring the device critical section here.
     *        Probably safe, but haven't verified this... */
    int const rc = PDMDevHlpPhysReadMeta(pDevIns, GCPhysLpiConfigTable, (void *)&pGicDev->abLpiConfig[0], cbLpiConfigTable);
    AssertRC(rc);
}


static void gicReDistReadLpiPendingBitmapFromMem(PPDMDEVINS pDevIns, PVMCPU pVCpu)
{
    PGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fEnableLpis);
    LogFlowFunc(("\n"));

    PGICCPU    pGicCpu   = VMCPU_TO_GICCPU(pVCpu);
    bool const fIsZeroed = RT_BF_GET(pGicDev->uLpiPendingBaseReg.u, GIC_BF_REDIST_REG_PENDBASER_PTZ);
    if (!fIsZeroed)
    {
        /* Copy the LPI pending bitmap from guest memory to our internal cache. */
        RTGCPHYS const GCPhysLpiPendingBitmap = (pGicDev->uLpiPendingBaseReg.u & GIC_BF_REDIST_REG_PENDBASER_PHYS_ADDR_MASK)
                                              + GIC_INTID_RANGE_LPI_START;  /* Skip first 1KB (since LPI INTIDs start at 8192). */
        uint32_t const cbLpiPendingBitmap     = sizeof(pGicCpu->bmLpiPending);

        /** @todo Try releasing and re-acquiring the device critical section here.
         *        Probably safe, but haven't verified this... */
        int const rc = PDMDevHlpPhysReadMeta(pDevIns, GCPhysLpiPendingBitmap, (void *)&pGicCpu->bmLpiPending[0],
                                             cbLpiPendingBitmap);
        AssertRC(rc);
    }
    else
        RT_ZERO(pGicCpu->bmLpiPending); /* Paranoia. */
}


DECLHIDDEN(void) gicReDistSetLpi(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t uIntId, bool fAsserted)
{
    Assert(GIC_CRIT_SECT_IS_OWNER(pDevIns));
    RT_NOREF4(pDevIns, pVCpu, uIntId, fAsserted);
    AssertMsgFailed(("[%u] uIntId=%RU32 fAsserted=%RTbool\n", pVCpu->idCpu, uIntId, fAsserted));
}



/**
 * Updates the internal IRQ state and sets or clears the appropriate force action
 * flags.
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static VBOXSTRICTRC gicReDistUpdateIrqState(PCGICDEV pGicDev, PVMCPUCC pVCpu)
{
    LogFlowFunc(("\n"));
    bool fIrq;
    bool fFiq;
    gicReDistHasIrqPending(VMCPU_TO_GICCPU(pVCpu), &fIrq, &fFiq);

    bool fIrqDist;
    bool fFiqDist;
    gicDistHasIrqPendingForVCpu(pGicDev, pVCpu, pVCpu->idCpu, &fIrqDist, &fFiqDist);
    LogFlowFunc(("fIrq=%RTbool fFiq=%RTbool fIrqDist=%RTbool fFiqDist=%RTbool\n", fIrq, fFiq, fIrqDist, fFiqDist));

    fIrq |= fIrqDist;
    fFiq |= fFiqDist;
    gicUpdateInterruptFF(pVCpu, fIrq, fFiq);
    return VINF_SUCCESS;
}


/**
 * Updates the internal IRQ state of the distributor and sets or clears the appropirate force action flags.
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM state.
 * @param   pGicDev     The GIC distributor state.
 */
static VBOXSTRICTRC gicDistUpdateIrqState(PCVMCC pVM, PCGICDEV pGicDev)
{
    LogFlowFunc(("\n"));
    for (uint32_t i = 0; i < pVM->cCpus; i++)
    {
        PVMCPUCC pVCpu   = pVM->CTX_SUFF(apCpus)[i];
        PCGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);

        bool fIrq, fFiq;
        gicReDistHasIrqPending(pGicCpu, &fIrq, &fFiq);

        bool fIrqDist, fFiqDist;
        gicDistHasIrqPendingForVCpu(pGicDev, pVCpu, i, &fIrqDist, &fFiqDist);
        fIrq |= fIrqDist;
        fFiq |= fFiqDist;

        gicUpdateInterruptFF(pVCpu, fIrq, fFiq);
    }
    return VINF_SUCCESS;
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
    Assert(pGicDev->fAffRoutingEnabled);

    /* Hardware does not map the first 32 registers (corresponding to SGIs and PPIs). */
    idxReg += GIC_INTID_RANGE_SPI_START;
    AssertReturn(idxReg < RT_ELEMENTS(pGicDev->au32IntrRouting), VERR_BUFFER_OVERFLOW);
    Assert(idxReg < sizeof(pGicDev->bmIntrRoutingMode) * 8);
    if (!(idxReg % 2))
    {
        /* Lower 32-bits. */
        uint8_t const fIrm = ASMBitTest(&pGicDev->bmIntrRoutingMode[0], idxReg);
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
    Assert(pGicDev->fAffRoutingEnabled);

    AssertMsgReturn(idxReg < RT_ELEMENTS(pGicDev->au32IntrRouting), ("idxReg=%u\n", idxReg), VERR_BUFFER_OVERFLOW);
    Assert(idxReg < sizeof(pGicDev->bmIntrRoutingMode) * 8);
    if (!(idxReg % 2))
    {
        /* Lower 32-bits. */
        bool const fIrm = GIC_DIST_REG_IROUTERn_IRM_GET(uValue);
        if (fIrm)
            ASMBitSet(&pGicDev->bmIntrRoutingMode[0], idxReg);
        else
            ASMBitClear(&pGicDev->bmIntrRoutingMode[0], idxReg);
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
    Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrEnabled));
    *puValue = pGicDev->bmIntrEnabled[idxReg];
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicDev->bmIntrEnabled[idxReg]));
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
    Assert(pGicDev->fAffRoutingEnabled);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrEnabled));
        pGicDev->bmIntrEnabled[idxReg] |= uValue;
        return gicDistUpdateIrqState(pVM, pGicDev);
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->bmIntrEnabled[idxReg]));
    return VINF_SUCCESS;
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
    Assert(pGicDev->fAffRoutingEnabled);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrEnabled));
        pGicDev->bmIntrEnabled[idxReg] &= ~uValue;
        return gicDistUpdateIrqState(pVM, pGicDev);
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->bmIntrEnabled[idxReg]));
    return VINF_SUCCESS;
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
    Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrActive));
    *puValue = pGicDev->bmIntrActive[idxReg];
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicDev->bmIntrActive[idxReg]));
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
    Assert(pGicDev->fAffRoutingEnabled);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrActive));
        pGicDev->bmIntrActive[idxReg] |= uValue;
        return gicDistUpdateIrqState(pVM, pGicDev);
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->bmIntrActive[idxReg]));
    return VINF_SUCCESS;
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
    Assert(pGicDev->fAffRoutingEnabled);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrActive));
        pGicDev->bmIntrActive[idxReg] &= ~uValue;
        return gicDistUpdateIrqState(pVM, pGicDev);
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->bmIntrActive[idxReg]));
    return VINF_SUCCESS;
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
    Assert(pGicDev->fAffRoutingEnabled);
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
 * @param   pGicDev     The GIC distributor state.
 * @param   idxReg      The index of the register in the GICD_IPRIORITY range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicDistWriteIntrPriorityReg(PGICDEV pGicDev, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is enabled, writes to registers 0..7 are ignored. */
    Assert(pGicDev->fAffRoutingEnabled);
    Assert(idxReg < RT_ELEMENTS(pGicDev->abIntrPriority) / sizeof(uint32_t));
    Assert(idxReg != 255);
    if (idxReg > 7)
    {
        uint16_t const idxPriority = idxReg * sizeof(uint32_t);
        AssertReturn(idxPriority <= RT_ELEMENTS(pGicDev->abIntrPriority) - sizeof(uint32_t), VERR_BUFFER_OVERFLOW);
        AssertCompile(sizeof(uValue) == sizeof(uint32_t));
        *(uint32_t *)&pGicDev->abIntrPriority[idxPriority] = uValue;
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, *(uint32_t *)&pGicDev->abIntrPriority[idxPriority]));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    return VINF_SUCCESS;
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
    Assert(pGicDev->fAffRoutingEnabled);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrPending));
        *puValue = pGicDev->bmIntrPending[idxReg];
    }
    else
    {
        AssertReleaseMsgFailed(("Unexpected (but not illegal) read to SGI/PPI register in distributor\n"));
        *puValue = 0;
    }
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicDev->bmIntrPending[idxReg]));
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
    Assert(pGicDev->fAffRoutingEnabled);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrPending));
        pGicDev->bmIntrPending[idxReg] |= uValue;
        return gicDistUpdateIrqState(pVM, pGicDev);
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->bmIntrPending[idxReg]));
    return VINF_SUCCESS;
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
    Assert(pGicDev->fAffRoutingEnabled);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrPending));
        pGicDev->bmIntrPending[idxReg] &= ~uValue;
        return gicDistUpdateIrqState(pVM, pGicDev);
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->bmIntrPending[idxReg]));
    return VINF_SUCCESS;
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
    Assert(pGicDev->fAffRoutingEnabled);
    if (idxReg >= 2)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrConfig));
        *puValue = pGicDev->bmIntrConfig[idxReg];
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) read to SGI/PPI register in distributor\n"));
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicDev->bmIntrConfig[idxReg]));
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
    Assert(pGicDev->fAffRoutingEnabled);
    if (idxReg >= 2)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrConfig));
        pGicDev->bmIntrConfig[idxReg] = uValue & 0xaaaaaaaa;
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->bmIntrConfig[idxReg]));
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
    Assert(pGicDev->fAffRoutingEnabled);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicDev->bmIntrGroup));
        *puValue = pGicDev->bmIntrGroup[idxReg];
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) read to SGI/PPI register in distributor\n"));
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, *puValue));
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
    Assert(pGicDev->fAffRoutingEnabled);
    if (idxReg > 0)
    {
        pGicDev->bmIntrGroup[idxReg] = uValue;
        LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicDev->bmIntrGroup[idxReg]));
    }
    else
        AssertReleaseMsgFailed(("Unexpected (but not illegal) write to SGI/PPI register in distributor\n"));
    return gicDistUpdateIrqState(pVM, pGicDev);
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
    Assert(pGicDev->fAffRoutingEnabled); RT_NOREF(pGicDev);
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
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_IPRIORITY range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrPriorityReg(PCGICDEV pGicDev, PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is disabled, writes are ignored. */
    Assert(pGicDev->fAffRoutingEnabled); RT_NOREF(pGicDev);
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    uint16_t const idxPriority = idxReg * sizeof(uint32_t);
    AssertReturn(idxPriority <= RT_ELEMENTS(pGicCpu->abIntrPriority) - sizeof(uint32_t), VERR_BUFFER_OVERFLOW);
    AssertCompile(sizeof(uValue) == sizeof(uint32_t));
    *(uint32_t *)&pGicCpu->abIntrPriority[idxPriority] = uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, *(uint32_t *)&pGicCpu->abIntrPriority[idxPriority]));
    return VINF_SUCCESS;
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
    Assert(pGicDev->fAffRoutingEnabled); RT_NOREF(pGicDev);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrPending));
    *puValue = pGicCpu->bmIntrPending[idxReg];
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicCpu->bmIntrPending[idxReg]));
    return VINF_SUCCESS;
}


/**
 * Writes the redistributor's interrupt set-pending register (GICR_ISPENDR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ISPENDR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrSetPendingReg(PCGICDEV pGicDev, PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is disabled, writes are ignored. */
    Assert(pGicDev->fAffRoutingEnabled);
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrPending));
    pGicCpu->bmIntrPending[idxReg] |= uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrPending[idxReg]));
    return gicReDistUpdateIrqState(pGicDev, pVCpu);
}


/**
 * Writes the redistributor's interrupt clear-pending register (GICR_ICPENDR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ICPENDR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrClearPendingReg(PCGICDEV pGicDev, PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is disabled, writes are ignored. */
    Assert(pGicDev->fAffRoutingEnabled);
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrPending));
    pGicCpu->bmIntrPending[idxReg] &= ~uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrPending[idxReg]));
    return gicReDistUpdateIrqState(pGicDev, pVCpu);
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
    Assert(pGicDev->fAffRoutingEnabled); RT_NOREF(pGicDev);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrEnabled));
    *puValue = pGicCpu->bmIntrEnabled[idxReg];
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicCpu->bmIntrEnabled[idxReg]));
    return VINF_SUCCESS;
}


/**
 * Writes the redistributor's interrupt set-enable register (GICR_ISENABLER).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ISENABLER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrSetEnableReg(PCGICDEV pGicDev, PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
    Assert(pGicDev->fAffRoutingEnabled);
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrEnabled));
    pGicCpu->bmIntrEnabled[idxReg] |= uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrEnabled[idxReg]));
    return gicReDistUpdateIrqState(pGicDev, pVCpu);
}


/**
 * Writes the redistributor's interrupt clear-enable register (GICR_ICENABLER).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ICENABLER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrClearEnableReg(PCGICDEV pGicDev, PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrEnabled));
    pGicCpu->bmIntrEnabled[idxReg] &= ~uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrEnabled[idxReg]));
    return gicReDistUpdateIrqState(pGicDev, pVCpu);
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
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ISACTIVER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrSetActiveReg(PCGICDEV pGicDev, PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrActive));
    pGicCpu->bmIntrActive[idxReg] |= uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrActive[idxReg]));
    return gicReDistUpdateIrqState(pGicDev, pVCpu);
}


/**
 * Writes the redistributor's interrupt clear-active register (GICR_ICACTIVER).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ICACTIVER range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrClearActiveReg(PCGICDEV pGicDev, PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrActive));
    pGicCpu->bmIntrActive[idxReg] &= ~uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrActive[idxReg]));
    return gicReDistUpdateIrqState(pGicDev, pVCpu);
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
    Assert(pGicDev->fAffRoutingEnabled); RT_NOREF(pGicDev);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrConfig));
    *puValue = pGicCpu->bmIntrConfig[idxReg];
    /* Ensure SGIs are read-only and remain configured as edge-triggered. */
    Assert(idxReg > 0 || *puValue == 0xaaaaaaaa);
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, *puValue));
    return VINF_SUCCESS;
}


/**
 * Writes the redistributor's interrupt config register (GICR_ICFGR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_ICFGR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrConfigReg(PCGICDEV pGicDev, PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is disabled, writes are ignored. */
    Assert(pGicDev->fAffRoutingEnabled); RT_NOREF(pGicDev);
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    if (idxReg > 0)
    {
        Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrConfig));
        pGicCpu->bmIntrConfig[idxReg] = uValue & 0xaaaaaaaa;
    }
    else
    {
        /* SGIs are always edge-triggered ignore writes. Windows 11 (24H2) arm64 guests writes these. */
        Assert(uValue == 0xaaaaaaaa);
        Assert(pGicCpu->bmIntrConfig[0] == uValue);
    }
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrConfig[idxReg]));
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
    Assert(pGicDev->fAffRoutingEnabled); RT_NOREF(pGicDev);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrGroup));
    *puValue = pGicCpu->bmIntrGroup[idxReg];
    LogFlowFunc(("idxReg=%#x read %#x\n", idxReg, pGicCpu->bmIntrGroup[idxReg]));
    return VINF_SUCCESS;
}


/**
 * Writes the redistributor's interrupt group register (GICR_IGROUPR).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idxReg      The index of the register in the GICR_IGROUPR range.
 * @param   uValue      The value to write to the register.
 */
static VBOXSTRICTRC gicReDistWriteIntrGroupReg(PCGICDEV pGicDev, PVMCPUCC pVCpu, uint16_t idxReg, uint32_t uValue)
{
    /* When affinity routing is disabled, writes are ignored. */
    Assert(pGicDev->fAffRoutingEnabled);
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    Assert(idxReg < RT_ELEMENTS(pGicCpu->bmIntrGroup));
    pGicCpu->bmIntrGroup[idxReg] = uValue;
    LogFlowFunc(("idxReg=%#x written %#x\n", idxReg, pGicCpu->bmIntrGroup[idxReg]));
    return gicReDistUpdateIrqState(pGicDev, pVCpu);
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
 * Gets the highest priority pending interrupt that can be signalled to the PE.
 *
 * @returns The interrupt ID or GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT if no interrupt
 *          is pending or not in a state to be signalled to the PE.
 * @param   pGicDev     The GIC distributor state.
 * @param   pGicCpu     The GIC redistributor and CPU interface state.
 * @param   enmGroup    The interrupt group to consider.
 * @param   pIntr       Where to store the pending interrupt. Optional, can be NULL.
 */
static uint16_t gicGetHighestPriorityPendingIntr(PCGICDEV pGicDev, PCGICCPU pGicCpu, GICINTRGROUP enmGroup, PGICINTR pIntr)
{
#if 1
    uint16_t idxIntr   = UINT16_MAX;
    uint16_t uIntId    = GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT;
    uint8_t  bPriority = GIC_IDLE_PRIORITY;

    /* Redistributor. */
    {
        uint32_t bmReDistIntrs[RT_ELEMENTS(pGicCpu->bmIntrPending)];
        AssertCompile(sizeof(pGicCpu->bmIntrPending) == sizeof(bmReDistIntrs));
        for (uint16_t i = 0; i < RT_ELEMENTS(bmReDistIntrs); i++)
        {
            /* Collect interrupts are pending, enabled and inactive. */
            bmReDistIntrs[i] = (pGicCpu->bmIntrPending[i] & pGicCpu->bmIntrEnabled[i]) & ~pGicCpu->bmIntrActive[i];

            /* Discard interrupts if the group they belong to is disabled. */
            if (enmGroup == GICINTRGROUP_0)
                bmReDistIntrs[i] &= ~pGicCpu->bmIntrGroup[i];
            else
                bmReDistIntrs[i] &= pGicCpu->bmIntrGroup[i];
        }
        /* Among the collected interrupts, pick the one with the highest, non-idle priority. */
        {
            uint16_t       idxHighest = UINT16_MAX;
            const void    *pvIntrs    = &bmReDistIntrs[0];
            uint32_t const cIntrs     = sizeof(bmReDistIntrs) * 8; AssertCompile(!(cIntrs % 32));
            int16_t        idxPending = ASMBitFirstSet(pvIntrs, cIntrs);
            if (idxPending >= 0)
            {
                do
                {
                    Assert(idxPending < 32);        /** @todo REMOVE before commit, local verification ... */
                    if (pGicCpu->abIntrPriority[idxPending] < bPriority)
                    {
                        idxHighest = (uint16_t)idxPending;
                        bPriority  = pGicCpu->abIntrPriority[idxPending];
                    }
                    idxPending = ASMBitNextSet(pvIntrs, cIntrs, idxPending);
                } while (idxPending != -1);
                if (idxHighest != UINT16_MAX)
                {
                    uIntId  = gicReDistGetIntIdFromIndex(idxHighest);
                    idxIntr = idxHighest;
                    Assert(   GIC_IS_INTR_SGI_OR_PPI(uIntId)
                           || GIC_IS_INTR_EXT_PPI(uIntId));
                }
            }
        }
    }

    /* Distributor. */
    {
        uint32_t bmDistIntrs[RT_ELEMENTS(pGicDev->bmIntrPending)];
        AssertCompile(sizeof(pGicDev->bmIntrPending) == sizeof(bmDistIntrs));
        for (uint16_t i = 0; i < RT_ELEMENTS(bmDistIntrs); i++)
        {
            /* Collect interrupts are pending, enabled and inactive. */
            bmDistIntrs[i] = (pGicDev->bmIntrPending[i] & pGicDev->bmIntrEnabled[i]) & ~pGicDev->bmIntrActive[i];

            /* Discard interrupts if the group they belong to is disabled. */
            if (enmGroup == GICINTRGROUP_0)
                bmDistIntrs[i] &= ~pGicDev->bmIntrGroup[i];
            else
                bmDistIntrs[i] &= pGicDev->bmIntrGroup[i];
        }
        /* Among the collected interrupts, pick one with priority higher than what we picked from the redistributor. */
        {
            uint16_t       idxHighest = UINT16_MAX;
            const void    *pvIntrs    = &bmDistIntrs[0];
            uint32_t const cIntrs     = sizeof(bmDistIntrs) * 8; AssertCompile(!(cIntrs % 32));
            int16_t        idxPending = ASMBitFirstSet(pvIntrs, cIntrs);
            if (idxPending >= 0)
            {
                do
                {
                    if (pGicDev->abIntrPriority[idxPending] < bPriority)
                    {
                        idxHighest = (uint16_t)idxPending;
                        bPriority  = pGicDev->abIntrPriority[idxPending];
                    }
                    idxPending = ASMBitNextSet(pvIntrs, cIntrs, idxPending);
                } while (idxPending != -1);
                if (idxHighest != UINT16_MAX)
                {
                    uIntId  = gicDistGetIntIdFromIndex(idxHighest);
                    idxIntr = idxHighest;
                    Assert(   GIC_IS_INTR_SPI(uIntId)
                           || GIC_IS_INTR_EXT_SPI(uIntId));
                }
            }
        }
    }
#else   /** @todo Measure and pick the faster version. */
    /*
     * Collect interrupts that are pending, enabled and inactive.
     * Discard interrupts if the group they belong to is disabled.
     * While collecting the interrupts, pick the one with the highest, non-idle priority.
     */
    uint16_t uIntId     = GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT;
    uint16_t idxIntr    = UINT16_MAX;
    uint8_t  bPriority  = GIC_IDLE_PRIORITY;

    /* Redistributor. */
    {
        uint16_t idxHighest = UINT16_MAX;
        for (uint16_t i = 0; i < RT_ELEMENTS(pGicCpu->bmIntrPending); i++)
        {
            uint32_t uIntrPending = (pGicCpu->bmIntrPending[i] & pGicCpu->bmIntrEnabled[i]) & ~pGicCpu->bmIntrActive[i];
            if (enmGroup == GICINTRGROUP_0)
                uIntrPending &= ~pGicCpu->bmIntrGroup[i];
            else
                uIntrPending &= pGicCpu->bmIntrGroup[i];

            uint16_t const idxPending = ASMBitFirstSetU32(uIntrPending);
            if (idxPending > 0)
            {
                uint32_t const idxPriority = 32 * i + idxPending - 1;
                Assert(idxPriority < RT_ELEMENTS(pGicCpu->abIntrPriority));
                if (pGicCpu->abIntrPriority[idxPriority] < bPriority)
                {
                    idxHighest = idxPriority;
                    bPriority  = pGicCpu->abIntrPriority[idxPriority];
                }
            }
        }
        if (idxHighest != UINT16_MAX)
        {
            idxIntr = idxHighest;
            uIntId  = gicReDistGetIntIdFromIndex(idxHighest);
            Assert(GIC_IS_INTR_SGI_OR_PPI(uIntId)
                   || GIC_IS_INTR_EXT_PPI(uIntId));
            Assert(bPriority != GIC_IDLE_PRIORITY);
        }
    }

    /* Distributor. */
    {
        uint16_t idxHighest = UINT16_MAX;
        for (uint16_t i = 0; i < RT_ELEMENTS(pGicDev->bmIntrPending); i += 2)
        {
            uint32_t uLo = (pGicDev->bmIntrPending[i] & pGicDev->bmIntrEnabled[i]) & ~pGicDev->bmIntrActive[i];
            uint32_t uHi = (pGicDev->bmIntrPending[i + 1] & pGicDev->bmIntrEnabled[i + 1]) & ~pGicDev->bmIntrActive[i + 1];
            if (enmGroup == GICINTRGROUP_0)
            {
                uLo &= ~pGicDev->bmIntrGroup[i];
                uHi &= ~pGicDev->bmIntrGroup[i + 1];
            }
            else
            {
                uLo &= pGicDev->bmIntrGroup[i];
                uHi &= pGicDev->bmIntrGroup[i + 1];
            }

            uint64_t const uIntrPending = RT_MAKE_U64(uLo, uHi);
            uint16_t const idxPending   = ASMBitFirstSetU64(uIntrPending);
            if (idxPending > 0)
            {
                uint32_t const idxPriority = 64 * i + idxPending - 1;
                if (pGicDev->abIntrPriority[idxPriority] < bPriority)
                {
                    idxHighest = idxPriority;
                    bPriority  = pGicDev->abIntrPriority[idxPriority];
                }
            }
        }
        if (idxHighest != UINT16_MAX)
        {
            idxIntr = idxHighest;
            uIntId  = gicDistGetIntIdFromIndex(idxHighest);
            Assert(   GIC_IS_INTR_SPI(uIntId)
                   || GIC_IS_INTR_EXT_SPI(uIntId));
            Assert(bPriority != GIC_IDLE_PRIORITY);
        }
    }
#endif

    /* Ensure that if no interrupt is pending, the idle priority is returned. */
    Assert(uIntId != GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT || bPriority == GIC_IDLE_PRIORITY);

    /* Ensure that if no interrupt is pending, the index is invalid. */
    Assert(uIntId != GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT || idxIntr == UINT16_MAX);

    /* Populate the pending interrupt data and we're done. */
    if (pIntr)
    {
        RT_BZERO(pIntr, sizeof(*pIntr));
        pIntr->enmGroup  = enmGroup;
        pIntr->uIntId    = uIntId;
        pIntr->idxIntr   = idxIntr;
        pIntr->bPriority = bPriority;
    }

    LogFlowFunc(("uIntId=%u [idxIntr=%u bPriority=%u enmGroup=%d (%s)]\n", uIntId, idxIntr, bPriority, enmGroup,
                 gicIntrGroupDescription(enmGroup)));
    return uIntId;
}


/**
 * Get and acknowledge the interrupt ID of a signalled interrupt.
 *
 * @returns The interrupt ID or GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT no interrupts
 *          are pending or not in a state to be signalled.
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   enmGroup    The interrupt group to consider.
 */
static uint16_t gicAckHighestPriorityPendingIntr(PGICDEV pGicDev, PVMCPUCC pVCpu, GICINTRGROUP enmGroup)
{
    LogFlowFunc(("enmGroup=%d (%s)\n", enmGroup, gicIntrGroupDescription(enmGroup)));

    STAM_PROFILE_START(&pGicCpu->StatProfIntrAck, x);

    /*
     * Get the pending interrupt with the highest priority for the given group.
     */
    GICINTR Intr;
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
    gicGetHighestPriorityPendingIntr(pGicDev, pGicCpu, enmGroup, &Intr);
    if (Intr.uIntId == GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT)
    {
        STAM_PROFILE_STOP(&pGicCpu->StatProfIntrAck, x);
        return Intr.uIntId;
    }

    uint8_t const  bIntrPriority = Intr.bPriority;
    uint16_t const uIntId        = Intr.uIntId;
    uint16_t const idxIntr       = Intr.idxIntr;
    Assert(idxIntr != UINT16_MAX);

    /*
     * Check if it has sufficient priority to be signalled to the PE.
     */
    bool const fGroup0 = RT_BOOL(enmGroup == GICINTRGROUP_0);
    bool const fSufficientPriority = gicReDistIsSufficientPriority(pGicCpu, bIntrPriority, fGroup0);
    if (!fSufficientPriority)
    {
        STAM_PROFILE_STOP(&pGicCpu->StatProfIntrAck, x);
        return GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT;
    }

    /*
     * Acknowledge the interrupt.
     */
    bool const fIsRedistIntId = GIC_IS_INTR_SGI_OR_PPI(uIntId)|| GIC_IS_INTR_EXT_PPI(uIntId);
    if (fIsRedistIntId)
    {
        /* Mark the interrupt as active. */
        AssertMsg(idxIntr < sizeof(pGicCpu->bmIntrActive) * 8, ("idxIntr=%u\n", idxIntr));
        AssertMsg(idxIntr < 32, ("idxIntr=%u uIntId=%u\n", idxIntr, uIntId));
        ASMBitSet(&pGicCpu->bmIntrActive[0], idxIntr);

        /* If it is an edge-triggered interrupt, mark it as no longer pending. */
        AssertRelease(UINT32_C(2) * idxIntr + 1 < sizeof(pGicCpu->bmIntrConfig) * 8);
        bool const fEdgeTriggered = ASMBitTest(&pGicCpu->bmIntrConfig[0], 2 * idxIntr + 1);
        if (fEdgeTriggered)
            ASMBitClear(&pGicCpu->bmIntrPending[0], idxIntr);

        /** @todo Duplicate block Id=E5ED12D2-088D-4525-9609-8325C02846C3 (start). */
        /* Update the active priorities bitmap. */
        AssertCompile(sizeof(pGicCpu->bmActivePriorityGroup0) * 8 >= 128);
        AssertCompile(sizeof(pGicCpu->bmActivePriorityGroup1) * 8 >= 128);
        uint8_t const idxPreemptionLevel = bIntrPriority >> 1;
        if (enmGroup == GICINTRGROUP_0)
            ASMBitSet(&pGicCpu->bmActivePriorityGroup0[0], idxPreemptionLevel);
        else
            ASMBitSet(&pGicCpu->bmActivePriorityGroup1[0], idxPreemptionLevel);

        /* Set priority of the running interrupt. */
        if (RT_LIKELY(pGicCpu->idxRunningPriority < RT_ELEMENTS(pGicCpu->abRunningPriorities) - 1))
        {
            Assert(pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority] > bIntrPriority);
            LogFlowFunc(("Setting interrupt priority from %u -> %u (idxRunningPriority: %u -> %u)\n",
                         pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority],
                         bIntrPriority,
                         pGicCpu->idxRunningPriority, pGicCpu->idxRunningPriority + 1));
            ++pGicCpu->idxRunningPriority;
            pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority] = bIntrPriority;
        }
        else
            AssertReleaseMsgFailed(("Index of running-interrupt priority out-of-bounds %u\n", pGicCpu->idxRunningPriority));

        /** @todo Duplicate block Id=E5ED12D2-088D-4525-9609-8325C02846C3 (end). */

        /* Update the redistributor IRQ state to reflect change to the active interrupt. */
        gicReDistUpdateIrqState(pGicDev, pVCpu);
        STAM_COUNTER_INC(&pVCpu->gic.s.StatIntrAck);
    }
    else if (uIntId < GIC_INTID_RANGE_LPI_START)
    {
        /* Sanity check if the interrupt ID belongs to the distributor. */
        Assert(GIC_IS_INTR_SPI(uIntId) || GIC_IS_INTR_EXT_SPI(uIntId));

        /* Mark the interrupt as active. */
        Assert(idxIntr < sizeof(pGicDev->bmIntrActive) * 8);
        ASMBitSet(&pGicDev->bmIntrActive[0], idxIntr);

        /* If it is an edge-triggered interrupt, mark it as no longer pending. */
        AssertRelease(UINT32_C(2) * idxIntr + 1 < sizeof(pGicDev->bmIntrConfig) * 8);
        bool const fEdgeTriggered = ASMBitTest(&pGicDev->bmIntrConfig[0], 2 * idxIntr + 1);
        if (fEdgeTriggered)
            ASMBitClear(&pGicDev->bmIntrPending[0], idxIntr);

        /** @todo Duplicate block Id=E5ED12D2-088D-4525-9609-8325C02846C3 (start). */
        /* Update the active priorities bitmap. */
        AssertCompile(sizeof(pGicCpu->bmActivePriorityGroup0) * 8 >= 128);
        AssertCompile(sizeof(pGicCpu->bmActivePriorityGroup1) * 8 >= 128);
        uint8_t const idxPreemptionLevel = bIntrPriority >> 1;
        if (enmGroup == GICINTRGROUP_0)
            ASMBitSet(&pGicCpu->bmActivePriorityGroup0[0], idxPreemptionLevel);
        else
            ASMBitSet(&pGicCpu->bmActivePriorityGroup1[0], idxPreemptionLevel);

        /* Set priority of the running priority. */
        if (RT_LIKELY(pGicCpu->idxRunningPriority < RT_ELEMENTS(pGicCpu->abRunningPriorities) - 1))
        {
            LogFlowFunc(("Dropping interrupt priority from %u -> %u (idxRunningPriority: %u -> %u)\n",
                         pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority],
                         bIntrPriority,
                         pGicCpu->idxRunningPriority, pGicCpu->idxRunningPriority + 1));
            ++pGicCpu->idxRunningPriority;
            pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority] = bIntrPriority;
        }
        else
            AssertReleaseMsgFailed(("Index of running-interrupt priority out-of-bounds %u\n", pGicCpu->idxRunningPriority));

        /** @todo Duplicate block Id=E5ED12D2-088D-4525-9609-8325C02846C3 (end). */

        /* Update the distributor IRQ state to reflect change to the active interrupt. */
        gicDistUpdateIrqState(pVCpu->CTX_SUFF(pVM), pGicDev);
        STAM_COUNTER_INC(&pVCpu->gic.s.StatIntrAck);
    }
    else
    {
        /** @todo LPIs. */
        /* Sanity check if the interrupt ID is an LPIs. */
        AssertMsgFailed(("todo\n"));
    }

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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrGroup) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrEnabled) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrEnabled) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrActive) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrActive) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrPending) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrPending) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrConfig) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ICFGRnE_OFF_START) / cbReg;
            return gicDistReadIntrConfigReg(pGicDev, idxReg, puValue);
        }
    }

    switch (offReg)
    {
        case GIC_DIST_REG_CTLR_OFF:
            Assert(pGicDev->fAffRoutingEnabled);
            *puValue = (pGicDev->fIntrGroup0Enabled ? GIC_DIST_REG_CTRL_ENABLE_GRP0    : 0)
                     | (pGicDev->fIntrGroup1Enabled ? GIC_DIST_REG_CTRL_ENABLE_GRP1_NS : 0)
                     | GIC_DIST_REG_CTRL_DS         /* We don't support multiple security states. */
                     | GIC_DIST_REG_CTRL_ARE_S;     /* We don't support GICv2 backwards compatibility, ARE is always enabled. */
            break;

        case GIC_DIST_REG_TYPER_OFF:
        {
            Assert(pGicDev->uMaxSpi > 0 && pGicDev->uMaxSpi <= GIC_DIST_REG_TYPER_NUM_ITLINES);
            Assert(pGicDev->fAffRoutingEnabled);
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrGroup) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrEnabled) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrEnabled) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrActive) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrActive) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ICACTIVERnE_OFF_START) / cbReg;
            return gicDistWriteIntrClearActiveReg(pVM, pGicDev, idxReg, uValue);
        }

        /*
         * GICD_IPRIORITYR<n> and GICD_IPRIORITYR<n>E.
         */
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IPRIORITYRn_OFF_START, GIC_DIST_REG_IPRIORITYRn_RANGE_SIZE))
        {
            uint16_t const idxReg = (offReg - GIC_DIST_REG_IPRIORITYRn_OFF_START) / cbReg;
            return gicDistWriteIntrPriorityReg(pGicDev, idxReg, uValue);
        }
        if (GIC_IS_REG_IN_RANGE(offReg, GIC_DIST_REG_IPRIORITYRnE_OFF_START, GIC_DIST_REG_IPRIORITYRnE_RANGE_SIZE))
        {
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->abIntrPriority) / (2 * sizeof(uint32_t));
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_IPRIORITYRnE_OFF_START) / cbReg;
            return gicDistWriteIntrPriorityReg(pGicDev, idxReg, uValue);
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrPending) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrPending) / 2;
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
            uint16_t const idxExt = RT_ELEMENTS(pGicDev->bmIntrConfig) / 2;
            uint16_t const idxReg = idxExt + (offReg - GIC_DIST_REG_ICFGRnE_OFF_START) / cbReg;
            return gicDistWriteIntrConfigReg(pGicDev, idxReg, uValue);
        }
    }

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    switch (offReg)
    {
        case GIC_DIST_REG_CTLR_OFF:
            Assert(!(uValue & GIC_DIST_REG_CTRL_ARE_NS));
            pGicDev->fIntrGroup0Enabled = RT_BOOL(uValue & GIC_DIST_REG_CTRL_ENABLE_GRP0);
            pGicDev->fIntrGroup1Enabled = RT_BOOL(uValue & GIC_DIST_REG_CTRL_ENABLE_GRP1_NS);
            rcStrict = gicDistUpdateIrqState(pVM, pGicDev);
            break;

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
    RT_NOREF(pVCpu, uValue);

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
                    gicReDistReadLpiPendingBitmapFromMem(pDevIns, pVCpu);
                }
                else
                {
                    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
                    RT_ZERO(pGicCpu->bmLpiPending);
                }
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
    VMCPU_ASSERT_EMT(pVCpu);
    PCGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PCGICDEV);
    uint16_t const cbReg = sizeof(uint32_t);

    /*
     * GICR_IGROUPR0 and GICR_IGROUPR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_IGROUPR0_OFF, GIC_REDIST_SGI_PPI_REG_IGROUPRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_IGROUPR0_OFF) / cbReg;
        return gicReDistWriteIntrGroupReg(pGicDev, pVCpu, idxReg, uValue);
    }

    /*
     * GICR_ISENABLER0 and GICR_ISENABLER<n>E.
     * GICR_ICENABLER0 and GICR_ICENABLER<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISENABLER0_OFF, GIC_REDIST_SGI_PPI_REG_ISENABLERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ISENABLER0_OFF) / cbReg;
        return gicReDistWriteIntrSetEnableReg(pGicDev, pVCpu, idxReg, uValue);
    }
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICENABLER0_OFF, GIC_REDIST_SGI_PPI_REG_ICENABLERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICENABLER0_OFF) / cbReg;
        return gicReDistWriteIntrClearEnableReg(pGicDev, pVCpu, idxReg, uValue);
    }

    /*
     * GICR_ISACTIVER0 and GICR_ISACTIVER<n>E.
     * GICR_ICACTIVER0 and GICR_ICACTIVER<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISACTIVER0_OFF, GIC_REDIST_SGI_PPI_REG_ISACTIVERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ISACTIVER0_OFF) / cbReg;
        return gicReDistWriteIntrSetActiveReg(pGicDev, pVCpu, idxReg, uValue);
    }
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICACTIVER0_OFF, GIC_REDIST_SGI_PPI_REG_ICACTIVERnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICACTIVER0_OFF) / cbReg;
        return gicReDistWriteIntrClearActiveReg(pGicDev, pVCpu, idxReg, uValue);
    }

    /*
     * GICR_ISPENDR0 and GICR_ISPENDR<n>E.
     * GICR_ICPENDR0 and GICR_ICPENDR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ISPENDR0_OFF, GIC_REDIST_SGI_PPI_REG_ISPENDRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ISPENDR0_OFF) / cbReg;
        return gicReDistWriteIntrSetPendingReg(pGicDev, pVCpu, idxReg, uValue);
    }
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICPENDR0_OFF, GIC_REDIST_SGI_PPI_REG_ICPENDRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICPENDR0_OFF) / cbReg;
        return gicReDistWriteIntrClearPendingReg(pGicDev, pVCpu, idxReg, uValue);
    }

    /*
     * GICR_IPRIORITYR<n> and GICR_IPRIORITYR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_IPRIORITYRn_OFF_START, GIC_REDIST_SGI_PPI_REG_IPRIORITYRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_IPRIORITYRn_OFF_START) / cbReg;
        return gicReDistWriteIntrPriorityReg(pGicDev, pVCpu, idxReg, uValue);
    }

    /*
     * GICR_ICFGR0, GIC_ICFGR1 and GICR_ICFGR<n>E.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GIC_REDIST_SGI_PPI_REG_ICFGR0_OFF, GIC_REDIST_SGI_PPI_REG_ICFGRnE_RANGE_SIZE))
    {
        uint16_t const idxReg = (offReg - GIC_REDIST_SGI_PPI_REG_ICFGR0_OFF) / cbReg;
        return gicReDistWriteIntrConfigReg(pGicDev, pVCpu, idxReg, uValue);
    }

    AssertReleaseMsgFailed(("offReg=%#RX16 (%s)\n", offReg, gicReDistGetSgiPpiRegDescription(offReg)));
    return VERR_INTERNAL_ERROR_2;
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

#ifdef VBOX_WITH_STATISTICS
    PVMCPU pVCpu = VMMGetCpuById(pVM, 0);
    STAM_COUNTER_INC(&pVCpu->gic.s.StatSetSpi);
    PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);
#endif
    STAM_PROFILE_START(&pGicCpu->StatProfSetSpi, a);

    uint16_t const uIntId  = GIC_INTID_RANGE_SPI_START + uSpiIntId;
    uint16_t const idxIntr = gicDistGetIndexFromIntId(uIntId);

    Assert(idxIntr >= GIC_INTID_RANGE_SPI_START);
    AssertMsgReturn(idxIntr < sizeof(pGicDev->bmIntrPending) * 8,
                    ("out-of-range SPI interrupt ID %RU32 (%RU32)\n", uIntId, uSpiIntId),
                    VERR_INVALID_PARAMETER);

    GIC_CRIT_SECT_ENTER(pDevIns);

    /* Update the interrupt pending state. */
    if (fAsserted)
        ASMBitSet(&pGicDev->bmIntrPending[0], idxIntr);
    else
        ASMBitClear(&pGicDev->bmIntrPending[0], idxIntr);

    int const rc = VBOXSTRICTRC_VAL(gicDistUpdateIrqState(pVM, pGicDev));
    GIC_CRIT_SECT_LEAVE(pDevIns);
    STAM_PROFILE_STOP(&pGicCpu->StatProfSetSpi, a);
    return rc;
}


/**
 * @interface_method_impl{PDMGICBACKEND,pfnSetPpi}
 */
static DECLCALLBACK(int) gicSetPpi(PVMCPUCC pVCpu, uint32_t uPpiIntId, bool fAsserted)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} uPpiIntId=%u fAsserted=%RTbool\n", pVCpu, pVCpu->idCpu, uPpiIntId, fAsserted));

    PPDMDEVINS pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PCGICDEV   pGicDev = PDMDEVINS_2_DATA(pDevIns, PCGICDEV);
    PGICCPU    pGicCpu = VMCPU_TO_GICCPU(pVCpu);

    STAM_COUNTER_INC(&pVCpu->gic.s.StatSetPpi);
    STAM_PROFILE_START(&pGicCpu->StatProfSetPpi, b);

    uint32_t const uIntId  = GIC_INTID_RANGE_PPI_START + uPpiIntId;
    uint16_t const idxIntr = gicReDistGetIndexFromIntId(uIntId);

    Assert(idxIntr >= GIC_INTID_RANGE_PPI_START);
    AssertMsgReturn(idxIntr < sizeof(pGicCpu->bmIntrPending) * 8,
                    ("out-of-range PPI interrupt ID %RU32 (%RU32)\n", uIntId, uPpiIntId),
                    VERR_INVALID_PARAMETER);

    GIC_CRIT_SECT_ENTER(pDevIns);

    /* Update the interrupt pending state. */
    if (fAsserted)
        ASMBitSet(&pGicCpu->bmIntrPending[0], idxIntr);
    else
        ASMBitClear(&pGicCpu->bmIntrPending[0], idxIntr);

    int const rc = VBOXSTRICTRC_VAL(gicReDistUpdateIrqState(pGicDev, pVCpu));
    STAM_PROFILE_STOP(&pGicCpu->StatProfSetPpi, b);

    GIC_CRIT_SECT_LEAVE(pDevIns);
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

    PGIC       pGic    = VM_TO_GIC(pVM);
    PPDMDEVINS pDevIns = pGic->CTX_SUFF(pDevIns);
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    Assert(pGicDev->fEnableLpis);

    uint32_t const uEventId = pMsi->Data.u32;
    uint32_t const uDevId   = uBusDevFn;
    AssertMsg((pMsi->Addr.u64 & ~(RTGCPHYS)GITS_REG_OFFSET_MASK) == pGicDev->GCPhysGits + GITS_REG_FRAME_SIZE,
              ("Addr=%#RX64 MMIO frame=%#RX64\n", pMsi->Addr.u64, pGicDev->GCPhysGits));
    AssertMsg((pMsi->Addr.u64 & GITS_REG_OFFSET_MASK) == GITS_TRANSLATION_REG_TRANSLATER,
              ("Addr=%#RX64 offset=%#RX32\n", pMsi->Addr.u64, GITS_TRANSLATION_REG_TRANSLATER));

    gitsSetLpi(pDevIns, &pGicDev->Gits, uDevId, uEventId, true /* fAsserted */);

    AssertMsgFailed(("uBusDevFn=%#RX32 (%RTbool) Msi.Addr=%#RX64 Msi.Data=%#RX32\n", uBusDevFn, PCIBDF_IS_VALID(uBusDevFn),
                     pMsi->Addr.u64, pMsi->Data.u32));
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Sets the specified software generated interrupt (SGI).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pDestCpuSet     Which CPUs to deliver the SGI to.
 * @param   uIntId          The SGI interrupt ID.
 */
static VBOXSTRICTRC gicSetSgi(PCGICDEV pGicDev, PVMCPUCC pVCpu, PCVMCPUSET pDestCpuSet, uint8_t uIntId)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} uIntId=%u\n", pVCpu, pVCpu->idCpu, uIntId));

    PPDMDEVINS     pDevIns = VMCPU_TO_DEVINS(pVCpu);
    PCVMCC         pVM     = pVCpu->CTX_SUFF(pVM);
    uint32_t const cCpus   = pVM->cCpus;
    AssertReturn(uIntId <= GIC_INTID_RANGE_SGI_LAST, VERR_INVALID_PARAMETER);
    Assert(GIC_CRIT_SECT_IS_OWNER(pDevIns)); NOREF(pDevIns);

    for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
        if (VMCPUSET_IS_PRESENT(pDestCpuSet, idCpu))
        {
            PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVM->CTX_SUFF(apCpus)[idCpu]);
            pGicCpu->bmIntrPending[0] |= RT_BIT_32(uIntId);
        }

    return gicDistUpdateIrqState(pVM, pGicDev);
}


/**
 * Writes to the redistributor's SGI group 1 register (ICC_SGI1R_EL1).
 *
 * @returns Strict VBox status code.
 * @param   pGicDev     The GIC distributor state.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uValue      The value being written to the ICC_SGI1R_EL1 register.
 */
static VBOXSTRICTRC gicReDistWriteSgiReg(PCGICDEV pGicDev, PVMCPUCC pVCpu, uint64_t uValue)
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
        VBOXSTRICTRC const rcStrict = gicSetSgi(pGicDev, pVCpu, &DestCpuSet, uSgiIntId);
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
    PGICDEV    pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);

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
            *pu64Value = pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority] & 0xfe;
            break;

        case ARMV8_AARCH64_SYSREG_ICC_IAR1_EL1:
            *pu64Value = gicAckHighestPriorityPendingIntr(pGicDev, pVCpu, GICINTRGROUP_1NS);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_HPPIR1_EL1:
            *pu64Value = gicGetHighestPriorityPendingIntr(pGicDev, pGicCpu, GICINTRGROUP_1NS, NULL /*pIntr*/);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_BPR1_EL1:
            *pu64Value = ARMV8_ICC_BPR1_EL1_AARCH64_BINARYPOINT_SET(pGicCpu->bBinaryPtGroup1);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_CTLR_EL1:
            *pu64Value = pGicCpu->uIccCtlr;
            break;

        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN0_EL1:
            *pu64Value = pGicCpu->fIntrGroup0Enabled ? ARMV8_ICC_IGRPEN0_EL1_AARCH64_ENABLE : 0;
            break;

        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN1_EL1:
            *pu64Value = pGicCpu->fIntrGroup1Enabled ? ARMV8_ICC_IGRPEN1_EL1_AARCH64_ENABLE : 0;
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
            LogFlowFunc(("ICC_PMR_EL1: Interrupt priority now %u\n", (uint8_t)u64Value));
            pGicCpu->bIntrPriorityMask = (uint8_t)u64Value;
            rcStrict = gicReDistUpdateIrqState(pGicDev, pVCpu);
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
            gicReDistWriteSgiReg(pGicDev, pVCpu, u64Value);
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
                AssertCompile(GIC_INTID_RANGE_PPI_LAST < 8 * sizeof(pGicDev->bmIntrActive[0]));
                Assert(pGicDev->fAffRoutingEnabled);
                pGicCpu->bmIntrActive[0] &= ~RT_BIT_32(uIntId);
            }
            else if (uIntId <= GIC_INTID_RANGE_SPI_LAST)
            {
                /* SPIs. */
                uint16_t const idxIntr = /*gicDistGetIndexFromIntId*/(uIntId);
                AssertReturn(idxIntr < sizeof(pGicDev->bmIntrActive) * 8, VERR_BUFFER_OVERFLOW);
                ASMBitClear(&pGicDev->bmIntrActive[0], idxIntr);
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
                ASMBitClear(&pGicCpu->bmIntrActive[0], idxIntr);
            }
            else if (uIntId <= GIC_INTID_RANGE_EXT_SPI_LAST)
            {
                /* Extended SPIs. */
                uint16_t const idxIntr = gicDistGetIndexFromIntId(uIntId);
                AssertReturn(idxIntr < sizeof(pGicDev->bmIntrActive) * 8, VERR_BUFFER_OVERFLOW);
                ASMBitClear(&pGicDev->bmIntrActive[0], idxIntr);
                fIsRedistIntId = false;
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
                ASMBitClear(&pGicCpu->bmActivePriorityGroup1[0], idxPreemptionLevel);

                pGicCpu->idxRunningPriority--;
                Assert(pGicCpu->abRunningPriorities[0] == GIC_IDLE_PRIORITY);
                STAM_COUNTER_INC(&pVCpu->gic.s.StatIntrEoi);
            }
            else
                AssertReleaseMsgFailed(("Index of running-priority interrupt out-of-bounds %u\n", pGicCpu->idxRunningPriority));
            if (fIsRedistIntId)
                rcStrict = gicReDistUpdateIrqState(pGicDev, pVCpu);
            else
                rcStrict = gicDistUpdateIrqState(pVCpu->CTX_SUFF(pVM), pGicDev);
            break;
        }

        case ARMV8_AARCH64_SYSREG_ICC_BPR1_EL1:
            pGicCpu->bBinaryPtGroup1 = (uint8_t)ARMV8_ICC_BPR1_EL1_AARCH64_BINARYPOINT_GET(u64Value);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_CTLR_EL1:
            /* We don't support dual-security states, so we use the "DS_RW" read/write mask. */
            GIC_SET_REG_U64_FULL(pGicCpu->uIccCtlr, u64Value, ARMV8_ICC_CTLR_EL1_DS_RW);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN0_EL1:
            pGicCpu->fIntrGroup0Enabled = RT_BOOL(u64Value & ARMV8_ICC_IGRPEN0_EL1_AARCH64_ENABLE);
            break;

        case ARMV8_AARCH64_SYSREG_ICC_IGRPEN1_EL1:
            pGicCpu->fIntrGroup1Enabled = RT_BOOL(u64Value & ARMV8_ICC_IGRPEN1_EL1_AARCH64_ENABLE);
            break;

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
    RT_ZERO(pGicDev->bmIntrGroup);
    RT_ZERO(pGicDev->bmIntrConfig);
    RT_ZERO(pGicDev->bmIntrEnabled);
    RT_ZERO(pGicDev->bmIntrPending);
    RT_ZERO(pGicDev->bmIntrActive);
    RT_ZERO(pGicDev->abIntrPriority);
    RT_ZERO(pGicDev->au32IntrRouting);
    RT_ZERO(pGicDev->bmIntrRoutingMode);
    pGicDev->fIntrGroup0Enabled = false;
    pGicDev->fIntrGroup1Enabled = false;
    pGicDev->fAffRoutingEnabled = true; /* GICv2 backwards compatibility is not implemented, so this is RA1/WI. */

    /* GITS. */
    PGITSDEV pGitsDev = &pGicDev->Gits;
    gitsInit(pGitsDev);

    /* LPIs. */
    RT_ZERO(pGicDev->abLpiConfig);
    pGicDev->uLpiConfigBaseReg.u = 0;
    pGicDev->uLpiPendingBaseReg.u = 0;
    pGicDev->fEnableLpis = false;
}


/**
 * Initialies the GIC redistributor and CPU interface state.
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
    pGicCpu->bmIntrConfig[0] = 0xaaaaaaaa;
    RT_ZERO(pGicCpu->bmIntrEnabled);
    RT_ZERO(pGicCpu->bmIntrPending);
    RT_ZERO(pGicCpu->bmIntrActive);
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
    RT_ZERO(pGicCpu->bmActivePriorityGroup0);
    RT_ZERO(pGicCpu->bmActivePriorityGroup1);
    pGicCpu->bBinaryPtGroup0    = 0;
    pGicCpu->bBinaryPtGroup1    = 0;
    pGicCpu->fIntrGroup0Enabled = false;
    pGicCpu->fIntrGroup1Enabled = false;
    RT_ZERO(pGicCpu->bmLpiPending);
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

