/** @file
 * ARMv8 Generic Interrupt Controller Architecture (GIC) definitions.
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

#ifndef VBOX_INCLUDED_gic_h
#define VBOX_INCLUDED_gic_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/armv8.h>

/** @name GIC Interrupt identifiers (INTID) ranges.
 * @{ */
/** Start of the SGI (Software Generated Interrupts) range. */
#define GIC_INTID_RANGE_SGI_START                       0
/** Last valid SGI. */
#define GIC_INTID_RANGE_SGI_LAST                        15
/** Number of SGIs. */
#define GIC_INTID_SGI_RANGE_SIZE                        (GIC_INTID_RANGE_SGI_LAST - GIC_INTID_RANGE_SGI_START + 1)

/** Start of the PPI (Private Peripheral Interrupts) range. */
#define GIC_INTID_RANGE_PPI_START                       16
/** Last valid PPI. */
#define GIC_INTID_RANGE_PPI_LAST                        31
/** Number of PPIs. */
#define GIC_INTID_PPI_RANGE_SIZE                        (GIC_INTID_RANGE_PPI_LAST - GIC_INTID_RANGE_PPI_START + 1)

/** Start of the SPI (Shared Peripheral Interrupts) range. */
#define GIC_INTID_RANGE_SPI_START                       32
/** Last valid SPI. */
#define GIC_INTID_RANGE_SPI_LAST                        1019
/** The size of the SPI range. */
#define GIC_INTID_SPI_RANGE_SIZE                        (GIC_INTID_RANGE_SPI_LAST - GIC_INTID_RANGE_SPI_START + 1)

/** Start of the special interrupt range. */
#define GIC_INTID_RANGE_SPECIAL_START                   1020
/** Last valid special interrupt identifier. */
#define GIC_INTID_RANGE_SPECIAL_LAST                    1023
/** Value for an interrupt acknowledge if no pending interrupt with sufficient
 * priority, security state or interrupt group. */
# define GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT           1023
/** The size of the extended PPI range. */
#define GIC_INTID_SPECIAL_RANGE_SIZE                    (GIC_INTID_RANGE_SPECIAL_NO_INTERRUPT - GIC_INTID_RANGE_SPECIAL_START + 1)

/** Start of the extended PPI (Private Peripheral Interrupts) range. */
#define GIC_INTID_RANGE_EXT_PPI_START                   1056
/** Last valid extended PPI. */
#define GIC_INTID_RANGE_EXT_PPI_LAST                    1119
/** The size of the extended PPI range. */
#define GIC_INTID_EXT_PPI_RANGE_SIZE                    (GIC_INTID_RANGE_EXT_PPI_LAST - GIC_INTID_RANGE_EXT_PPI_START + 1)

/** Start of the extended SPI (Shared Peripheral Interrupts) range. */
#define GIC_INTID_RANGE_EXT_SPI_START                   4096
/** Last valid extended SPI. */
#define GIC_INTID_RANGE_EXT_SPI_LAST                    5119
/** The size of the extended SPI range. */
#define GIC_INTID_EXT_SPI_RANGE_SIZE                    (GIC_INTID_RANGE_EXT_SPI_LAST - GIC_INTID_RANGE_EXT_SPI_START + 1)

/** Start of the LPI (Locality-specific Peripheral Interrupts) range. */
#define GIC_INTID_RANGE_LPI_START                       8192
/** @} */


/** @name GIC Distributor registers.
 * @{ */
/** Size of the distributor register frame. */
#define GIC_DIST_REG_FRAME_SIZE                         _64K

/** Distributor Control Register - RW. */
#define GIC_DIST_REG_CTLR_OFF                           0x0000
/** Bit 0 - Enable Group 0 interrupts. */
# define GIC_DIST_REG_CTRL_ENABLE_GRP0                  RT_BIT_32(0)
# define GIC_DIST_REG_CTRL_ENABLE_GRP0_BIT              0
/** Bit 1 - Enable Non-secure Group 1 interrupts. */
# define GIC_DIST_REG_CTRL_ENABLE_GRP1_NS               RT_BIT_32(1)
# define GIC_DIST_REG_CTRL_ENABLE_GRP1_NS_BIT           1
/** Bit 2 - Enable Secure Group 1 interrupts. */
# define GIC_DIST_REG_CTRL_ENABLE_GRP1_S                RT_BIT_32(2)
# define GIC_DIST_REG_CTRL_ENABLE_GRP1_S_BIT            2
/** Bit 4 - Affinity Routing Enable, Secure state. */
# define GIC_DIST_REG_CTRL_ARE_S                        RT_BIT_32(4)
# define GIC_DIST_REG_CTRL_ARE_S_BIT                    4
/** Bit 5 - Affinity Routing Enable, Non-secure state. */
# define GIC_DIST_REG_CTRL_ARE_NS                       RT_BIT_32(5)
# define GIC_DIST_REG_CTRL_ARE_NS_BIT                   5
/** Bit 6 - Disable Security. */
# define GIC_DIST_REG_CTRL_DS                           RT_BIT_32(6)
# define GIC_DIST_REG_CTRL_DS_BIT                       6
/** Bit 7 - Enable 1 of N Wakeup Functionality. */
# define GIC_DIST_REG_CTRL_E1NWF                        RT_BIT_32(7)
# define GIC_DIST_REG_CTRL_E1NWF_BIT                    7
/** Bit 31 - Register Write Pending. */
# define GIC_DIST_REG_CTRL_RWP                          RT_BIT_32(31)
# define GIC_DIST_REG_CTRL_RWP_BIT                      31

/** Interrupt Controller Type Register - RO. */
#define GIC_DIST_REG_TYPER_OFF                          0x0004
/** Bit 0 - 4 - Maximum number of SPIs supported. */
# define GIC_DIST_REG_TYPER_NUM_ITLINES                 (RT_BIT_32(0) | RT_BIT_32(1) | RT_BIT(2) | RT_BIT_32(3) | RT_BIT_32(4))
# define GIC_DIST_REG_TYPER_NUM_ITLINES_SET(a_NumSpis)  ((a_NumSpis) & GIC_DIST_REG_TYPER_NUM_ITLINES)
/** Bit 5 - 7 - Reports number of PEs that can be used when affinity routing is not enabled, minus 1. */
# define GIC_DIST_REG_TYPER_NUM_PES                     (RT_BIT_32(5) | RT_BIT_32(6) | RT_BIT(7))
# define GIC_DIST_REG_TYPER_NUM_PES_SET(a_Pes)          (((a_Pes) << 5) & GIC_DIST_REG_TYPER_NUM_PES)
/** Bit 8 - Extended SPI range implemented. */
# define GIC_DIST_REG_TYPER_ESPI                        RT_BIT_32(8)
# define GIC_DIST_REG_TYPER_ESPI_BIT                    8
/** Bit 9 - Non-maskable interrupt priority supported. */
# define GIC_DIST_REG_TYPER_NMI                         RT_BIT_32(9)
# define GIC_DIST_REG_TYPER_NMI_BIT                     9
/** Bit 10 - Indicates whether the implementation supports two security states. */
# define GIC_DIST_REG_TYPER_SECURITY_EXTN               RT_BIT_32(10)
# define GIC_DIST_REG_TYPER_SECURITY_EXTN_BIT           10
/** Bit 11 - 15 - The number of supported LPIs. */
# define GIC_DIST_REG_TYPER_NUM_LPIS                    (  RT_BIT_32(11) | RT_BIT_32(12) | RT_BIT(13) \
                                                         | RT_BIT_32(14) | RT_BIT_32(15))
# define GIC_DIST_REG_TYPER_NUM_LPIS_SET(a_Lpis)        (((a_Lpis) << 11) & GIC_DIST_REG_TYPER_NUM_LPIS)
/** Bit 16 - Indicates whether the implementation supports message based interrupts by writing to Distributor registers. */
# define GIC_DIST_REG_TYPER_MBIS                        RT_BIT_32(16)
# define GIC_DIST_REG_TYPER_MBIS_BIT                    16
/** Bit 17 - Indicates whether the implementation supports LPIs. */
# define GIC_DIST_REG_TYPER_LPIS                        RT_BIT_32(17)
# define GIC_DIST_REG_TYPER_LPIS_BIT                    17
/** Bit 18 - Indicates whether the implementation supports Direct Virtual LPI injection (FEAT_GICv4). */
# define GIC_DIST_REG_TYPER_DVIS                        RT_BIT_32(18)
# define GIC_DIST_REG_TYPER_DVIS_BIT                    18
/** Bit 19 - 23 - The number of interrupt identifer bits supported, minus one. */
# define GIC_DIST_REG_TYPER_IDBITS                      (  RT_BIT_32(19) | RT_BIT_32(20) | RT_BIT(21) \
                                                         | RT_BIT_32(22) | RT_BIT_32(23))
# define GIC_DIST_REG_TYPER_IDBITS_SET(a_Bits)          (((a_Bits) << 19) & GIC_DIST_REG_TYPER_IDBITS)
/** Bit 24 - Affinity 3 valid. Indicates whether the Distributor supports nonzero values of Affinity level 3. */
# define GIC_DIST_REG_TYPER_A3V                         RT_BIT_32(24)
# define GIC_DIST_REG_TYPER_A3V_BIT                     24
/** Bit 25 - Indicates whether 1 of N SPI interrupts are supported. */
# define GIC_DIST_REG_TYPER_NO1N                        RT_BIT_32(25)
# define GIC_DIST_REG_TYPER_NO1N_BIT                    25
/** Bit 26 - Range Selector Support. */
# define GIC_DIST_REG_TYPER_RSS                         RT_BIT_32(26)
# define GIC_DIST_REG_TYPER_RSS_BIT                     26
/** Bit 27 - 31 - Indicates maximum INTID in the Extended SPI range. */
# define GIC_DIST_REG_TYPER_ESPI_RANGE                  (  RT_BIT_32(27) | RT_BIT_32(28) | RT_BIT(29) \
                                                         | RT_BIT_32(30) | RT_BIT_32(31))
# define GIC_DIST_REG_TYPER_ESPI_RANGE_BIT              27
# define GIC_DIST_REG_TYPER_ESPI_RANGE_SET(a_Range)     (((a_Range) << 27) & GIC_DIST_REG_TYPER_ESPI_RANGE)

/** Distributor Implementer Identification Register - RO. */
#define GIC_DIST_REG_IIDR_OFF                           0x0008
/** Bits 0 - 6 - Implementer ID code. */
# define GIC_DIST_REG_IIDR_IMPL_ID                      UINT32_C(0x0000007f)
# define GIC_DIST_REG_IIDR_IMPL_ID_BIT                  0
/** Bits 0 - 6 - Implementer continuation code. */
# define GIC_DIST_REG_IIDR_IMPL_CONT                    UINT32_C(0x00000f00)
# define GIC_DIST_REG_IIDR_IMPL_CONT_BIT                8
# define GIC_DIST_REG_IIDR_IMPL_SET(a_Id, a_Cont)       ((a_Id) | \
                                                        (((a_Cont) << GIC_DIST_REG_IIDR_IMPL_CONT_BIT) & GIC_DIST_REG_IIDR_IMPL_CONT))

/** Interrupt Controller Type Register 2 - RO. */
#define GIC_DIST_REG_TYPER2_OFF                         0x000c
/** Error Reporting Status Register (optional) - RW. */
#define GIC_DIST_REG_STATUSR_OFF                        0x0010
/** Set SPI Register - WO. */
#define GIC_DIST_REG_SETSPI_NSR_OFF                     0x0040
/** Clear SPI Register - WO. */
#define GIC_DIST_REG_CLRSPI_NSR_OFF                     0x0048
/** Set SPI, Secure Register - WO. */
#define GIC_DIST_REG_SETSPI_SR_OFF                      0x0050
/** Clear SPI, Secure Register - WO. */
#define GIC_DIST_REG_CLRSPI_SR_OFF                      0x0058

/** Interrupt Group Registers, start offset - RW. */
#define GIC_DIST_REG_IGROUPRn_OFF_START                 0x0080
/** Interrupt Group Registers, last offset - RW. */
#define GIC_DIST_REG_IGROUPRn_OFF_LAST                  0x00fc
/** Interrupt Group Registers, range in bytes. */
#define GIC_DIST_REG_IGROUPRn_RANGE_SIZE                (GIC_DIST_REG_IGROUPRn_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_IGROUPRn_OFF_START)

/** Interrupt Set Enable Registers, start offset - RW. */
#define GIC_DIST_REG_ISENABLERn_OFF_START               0x0100
/** Interrupt Set Enable Registers, last offset - RW. */
#define GIC_DIST_REG_ISENABLERn_OFF_LAST                0x017c
/** Interrupt Set Enable Registers, range in bytes. */
#define GIC_DIST_REG_ISENABLERn_RANGE_SIZE             (GIC_DIST_REG_ISENABLERn_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ISENABLERn_OFF_START)

/** Interrupt Clear Enable Registers, start offset - RW. */
#define GIC_DIST_REG_ICENABLERn_OFF_START               0x0180
/** Interrupt Clear Enable Registers, last offset - RW. */
#define GIC_DIST_REG_ICENABLERn_OFF_LAST                0x01fc
/** Interrupt Clear Enable Registers, range in bytes. */
#define GIC_DIST_REG_ICENABLERn_RANGE_SIZE             (GIC_DIST_REG_ICENABLERn_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ICENABLERn_OFF_START)

/** Interrupt Set Pending Registers, start offset - RW. */
#define GIC_DIST_REG_ISPENDRn_OFF_START                 0x0200
/** Interrupt Set Pending Registers, last offset - RW. */
#define GIC_DIST_REG_ISPENDRn_OFF_LAST                  0x027c
/** Interrupt Set Pending Registers, range in bytes. */
#define GIC_DIST_REG_ISPENDRn_RANGE_SIZE                (GIC_DIST_REG_ISPENDRn_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ISPENDRn_OFF_START)

/** Interrupt Clear Pending Registers, start offset - RW. */
#define GIC_DIST_REG_ICPENDRn_OFF_START                 0x0280
/** Interrupt Clear Pending Registers, last offset - RW. */
#define GIC_DIST_REG_ICPENDRn_OFF_LAST                  0x02fc
/** Interrupt Clear Pending Registers, range in bytes. */
#define GIC_DIST_REG_ICPENDRn_RANGE_SIZE               (GIC_DIST_REG_ICPENDRn_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ICPENDRn_OFF_START)

/** Interrupt Set Active Registers, start offset - RW. */
#define GIC_DIST_REG_ISACTIVERn_OFF_START               0x0300
/** Interrupt Set Active Registers, last offset - RW. */
#define GIC_DIST_REG_ISACTIVERn_OFF_LAST                0x037c
/** Interrupt Set Active Registers, range in bytes. */
#define GIC_DIST_REG_ISACTIVERn_RANGE_SIZE              (GIC_DIST_REG_ISACTIVERn_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ISACTIVERn_OFF_START)

/** Interrupt Clear Active Registers, start offset - RW. */
#define GIC_DIST_REG_ICACTIVERn_OFF_START               0x0380
/** Interrupt Clear Active Registers, last offset - RW. */
#define GIC_DIST_REG_ICACTIVERn_OFF_LAST                0x03fc
/** Interrupt Clear Active Registers, range in bytes. */
#define GIC_DIST_REG_ICACTIVERn_RANGE_SIZE              (GIC_DIST_REG_ICACTIVERn_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ICACTIVERn_OFF_START)

/** Interrupt Priority Registers, start offset - RW. */
#define GIC_DIST_REG_IPRIORITYRn_OFF_START              0x0400
/** Interrupt Priority Registers, last offset - RW. */
#define GIC_DIST_REG_IPRIORITYRn_OFF_LAST               0x07f8
/** Interrupt Priority Registers, range in bytes. */
#define GIC_DIST_REG_IPRIORITYRn_RANGE_SIZE             (GIC_DIST_REG_IPRIORITYRn_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_IPRIORITYRn_OFF_START)

/** Interrupt Processor Targets Registers, start offset - RO/RW. */
#define GIC_DIST_REG_ITARGETSRn_OFF_START               0x0800
/** Interrupt Processor Targets Registers, last offset - RO/RW. */
#define GIC_DIST_REG_ITARGETSRn_OFF_LAST                0x0bf8

/** Interrupt Configuration Registers, start offset - RW. */
#define GIC_DIST_REG_ICFGRn_OFF_START                   0x0c00
/** Interrupt Configuration Registers, last offset - RW. */
#define GIC_DIST_REG_ICFGRn_OFF_LAST                    0x0cfc
/** Interrupt Configuration Registers, range in bytes. */
#define GIC_DIST_REG_ICFGRn_RANGE_SIZE                  (GIC_DIST_REG_ICFGRn_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ICFGRn_OFF_START)

/** Interrupt Group Modifier Registers, start offset - RW. */
#define GIC_DIST_REG_IGRPMODRn_OFF_START                0x0d00
/** Interrupt Group Modifier Registers, last offset - RW. */
#define GIC_DIST_REG_IGRPMODRn_OFF_LAST                 0x0d7c

/** Non-secure Access Control Registers, start offset - RW. */
#define GIC_DIST_REG_NSACRn_OFF_START                   0x0e00
/** Non-secure Access Control Registers, last offset - RW. */
#define GIC_DIST_REG_NSACRn_OFF_LAST                    0x0efc

/** Software Generated Interrupt Register - RW. */
#define GIC_DIST_REG_SGIR_OFF                           0x0f00

/** SGI Clear Pending Registers, start offset - RW. */
#define GIC_DIST_REG_CPENDSGIRn_OFF_START               0x0f10
/** SGI Clear Pending Registers, last offset - RW. */
#define GIC_DIST_REG_CPENDSGIRn_OFF_LAST                0x0f1c
/** SGI Set Pending Registers, start offset - RW. */
#define GIC_DIST_REG_SPENDSGIRn_OFF_START               0x0f20
/** SGI Set Pending Registers, last offset - RW. */
#define GIC_DIST_REG_SPENDSGIRn_OFF_LAST                0x0f2c

/** Non-maskable Interrupt Registers, start offset - RW. */
#define GIC_DIST_REG_INMIn_OFF_START                    0x0f80
/** Non-maskable Interrupt Registers, last offset - RW. */
#define GIC_DIST_REG_INMIn_OFF_LAST                     0x0ffc

/** Interrupt Group Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_IGROUPRnE_OFF_START                0x1000
/** Interrupt Group Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_IGROUPRnE_OFF_LAST                 0x107c
/** Interrupt Group Registers for extended SPI range, range in bytes. */
#define GIC_DIST_REG_IGROUPRnE_RANGE_SIZE               (GIC_DIST_REG_IGROUPRnE_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_IGROUPRnE_OFF_START)

/** Interrupt Set Enable Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_ISENABLERnE_OFF_START              0x1200
/** Interrupt Set Enable Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_ISENABLERnE_OFF_LAST               0x127c
/** Interrupt Set Enable Registers for extended SPI range, range in bytes. */
#define GIC_DIST_REG_ISENABLERnE_RANGE_SIZE             (GIC_DIST_REG_ISENABLERnE_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ISENABLERnE_OFF_START)

/** Interrupt Clear Enable Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_ICENABLERnE_OFF_START              0x1400
/** Interrupt Clear Enable Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_ICENABLERnE_OFF_LAST               0x147c
/** Interrupt Clear Enable Registers for extended SPI range, range in bytes. */
#define GIC_DIST_REG_ICENABLERnE_RANGE_SIZE             (GIC_DIST_REG_ICENABLERnE_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ICENABLERnE_OFF_START)

/** Interrupt Set Pending Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_ISPENDRnE_OFF_START                0x1600
/** Interrupt Set Pending Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_ISPENDRnE_OFF_LAST                 0x167c
/** Interrupt Set Pending Registers for extended SPI range, range in bytes. */
#define GIC_DIST_REG_ISPENDRnE_RANGE_SIZE               (GIC_DIST_REG_ISPENDRnE_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ISPENDRnE_OFF_START)

/** Interrupt Clear Pending Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_ICPENDRnE_OFF_START                0x1800
/** Interrupt Clear Pending Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_ICPENDRnE_OFF_LAST                 0x187c
/** Interrupt Clear Pending Registers for extended SPI range, range in bytes. */
#define GIC_DIST_REG_ICPENDRnE_RANGE_SIZE               (GIC_DIST_REG_ICPENDRnE_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ICPENDRnE_OFF_START)

/** Interrupt Set Active Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_ISACTIVERnE_OFF_START              0x1a00
/** Interrupt Set Active Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_ISACTIVERnE_OFF_LAST               0x1a7c
/** Interrupt Set Active Registers for extended SPI range, range in bytes. */
#define GIC_DIST_REG_ISACTIVERnE_RANGE_SIZE             (GIC_DIST_REG_ISACTIVERnE_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ISACTIVERnE_OFF_START)

/** Interrupt Clear Active Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_ICACTIVERnE_OFF_START              0x1c00
/** Interrupt Clear Active Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_ICACTIVERnE_OFF_LAST               0x1c7c
/** Interrupt Clear Active Registers for extended SPI range, range in bytes. */
#define GIC_DIST_REG_ICACTIVERnE_RANGE_SIZE             (GIC_DIST_REG_ICACTIVERnE_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ICACTIVERnE_OFF_START)

/** Interrupt Priority Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_IPRIORITYRnE_OFF_START             0x2000
/** Interrupt Priority Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_IPRIORITYRnE_OFF_LAST              0x23fc
/** Interrupt Priority Registers for extended SPI range, range in bytes. */
#define GIC_DIST_REG_IPRIORITYRnE_RANGE_SIZE            (GIC_DIST_REG_IPRIORITYRnE_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_IPRIORITYRnE_OFF_START)

/** Interrupt Configuration Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_ICFGRnE_OFF_START                  0x3000
/** Interrupt Configuration Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_ICFGRnE_OFF_LAST                   0x30fc
/** Interrupt Configuration Registers for extended SPI range, range in bytes. */
#define GIC_DIST_REG_ICFGRnE_RANGE_SIZE                (GIC_DIST_REG_ICFGRnE_OFF_LAST + sizeof(uint32_t) - GIC_DIST_REG_ICFGRnE_OFF_START)

/** Interrupt Group Modifier Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_IGRPMODRnE_OFF_START               0x3400
/** Interrupt Group Modifier Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_IGRPMODRnE_OFF_LAST                0x347c

/** Non-secure Access Control Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_NSACRnE_OFF_START                  0x3600
/** Non-secure Access Control Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_NSACRnE_OFF_LAST                   0x367c

/** Non-maskable Interrupt Registers for extended SPIs, start offset - RW. */
#define GIC_DIST_REG_INMInE_OFF_START                   0x3b00
/** Non-maskable Interrupt Registers for extended SPIs, last offset - RW. */
#define GIC_DIST_REG_INMInE_OFF_LAST                    0x3b7c

/** Interrupt Routing Registers, start offset - RW. */
#define GIC_DIST_REG_IROUTERn_OFF_START                 0x6100
/** Interrupt Routing Registers, last offset - RW. */
#define GIC_DIST_REG_IROUTERn_OFF_LAST                  0x7fd8
/** Interrupt Routing Registers range in bytes. */
#define GIC_DIST_REG_IROUTERn_RANGE_SIZE                (GIC_DIST_REG_IROUTERn_OFF_LAST + sizeof(uint64_t) - GIC_DIST_REG_IROUTERn_OFF_START)

/** Interrupt Routing Registers for extended SPI range, start offset - RW. */
#define GIC_DIST_REG_IROUTERnE_OFF_START                0x8000
/** Interrupt Routing Registers for extended SPI range, last offset - RW. */
#define GIC_DIST_REG_IROUTERnE_OFF_LAST                 0x9ffc
/** Interrupt Routing Registers for extended SPI range, range in bytes. */
#define GIC_DIST_REG_IROUTERnE_RANGE_SIZE               (GIC_DIST_REG_IROUTERnE_OFF_LAST + sizeof(uint64_t) - GIC_DIST_REG_IROUTERnE_OFF_START)

#define GIC_DIST_REG_IROUTERn_IRM_BIT                   31
#define GIC_DIST_REG_IROUTERn_MASK                      (RT_BIT_32(GIC_DIST_REG_IROUTERn_IRM_BIT) | 0xffffff)
#define GIC_DIST_REG_IROUTERnE_MASK                     0xff

#define GIC_DIST_REG_IROUTERn_IRM_GET(a_Reg)            (((a_Reg) >> GIC_DIST_REG_IROUTERn_IRM_BIT) & 1)
#define GIC_DIST_REG_IROUTERn_SET(a_fIrm, a_Reg)        ((((a_fIrm) << GIC_DIST_REG_IROUTERn_IRM_BIT) | (a_Reg)) & GIC_DIST_REG_IROUTERn_MASK)

/** Distributor Peripheral ID2 Register - RO. */
#define GIC_DIST_REG_PIDR2_OFF                          0xffe8
/** Bit 4 - 7 - GIC architecture revision */
# define GIC_DIST_REG_PIDR2_ARCHREV                     (RT_BIT_32(4) | RT_BIT_32(5) | RT_BIT_32(6) | RT_BIT_32(7))
# define GIC_DIST_REG_PIDR2_ARCHREV_SET(a_ArchRev)      (((a_ArchRev) << 4) & GIC_DIST_REG_PIDR2_ARCHREV)
/** GICv1 architecture revision. */
#  define GIC_DIST_REG_PIDR2_ARCHREV_GICV1              0x1
/** GICv2 architecture revision. */
#  define GIC_DIST_REG_PIDR2_ARCHREV_GICV2              0x2
/** GICv3 architecture revision. */
#  define GIC_DIST_REG_PIDR2_ARCHREV_GICV3              0x3
/** GICv4 architecture revision. */
#  define GIC_DIST_REG_PIDR2_ARCHREV_GICV4              0x4
/** @} */


/** @name GIC Redistributor registers.
 * @{ */
/** Size of the redistributor register frame. */
#define GIC_REDIST_REG_FRAME_SIZE                       _64K

/** Redistributor Control Register - RW. */
#define GIC_REDIST_REG_CTLR_OFF                         0x0000
/** Bit 0 - Enable LPIs. */
#define GIC_REDIST_REG_CTLR_ENABLE_LPI_BIT              0
#define GIC_REDIST_REG_CTLR_ENABLE_LPI                  RT_BIT_32(0)
/** Bit 1 - Clear Enable Support. */
#define GIC_REDIST_REG_CTLR_CES_BIT                     1
#define GIC_REDIST_REG_CTLR_CES                         RT_BIT_32(1)
#define GIC_REDIST_REG_CTLR_CES_SET(a_Ces)              (((a_Ces) << GIC_REDIST_REG_CTLR_CES_BIT) & GIC_REDIST_REG_CTLR_CES)
/** Bit 2 - LPI invalidate registers supported. */
#define GIC_REDIST_REG_CTLR_IR_BIT                      2
#define GIC_REDIST_REG_CTLR_IR                          RT_BIT_32(2)
/** Bit 3 - Register Write Pending. */
#define GIC_REDIST_REG_CTLR_RWP_BIT                     3
#define GIC_REDIST_REG_CTLR_RWP                         RT_BIT_32(3)
/** Bit 24 - Disable Processor selection for Group 0 interrupt. */
#define GIC_REDIST_REG_CTLR_DPG0_BIT                    24
#define GIC_REDIST_REG_CTLR_DPG0                        RT_BIT_32(24)
/** Bit 25 - Disable Processor selection for Group 1 non-secure interrupt. */
#define GIC_REDIST_REG_CTLR_DPG1NS_BIT                  25
#define GIC_REDIST_REG_CTLR_DPG1NS                      RT_BIT_32(25)
/** Bit 26 - Disable Processor selection for Group 1 secure interrupt. */
#define GIC_REDIST_REG_CTLR_DPG1S_BIT                   26
#define GIC_REDIST_REG_CTLR_DPG1S                       RT_BIT_32(26)
/** Bit 31 - Upstream Write Pending. */
#define GIC_REDIST_REG_CTLR_UWP_BIT                     31
#define GIC_REDIST_REG_CTLR_UWP                         RT_BIT_32(31)
/** GICR_CTLR: Mask of valid read-write bits. */
#define GIC_REDIST_REG_CTLR_RW_MASK                     (  GIC_REDIST_REG_CTLR_ENABLE_LPI \
                                                         | GIC_REDIST_REG_CTLR_DPG0 \
                                                         | GIC_REDIST_REG_CTLR_DPG1NS \
                                                         | GIC_REDIST_REG_CTLR_DPG1S)

/** Implementer Identification Register - RO. */
#define GIC_REDIST_REG_IIDR_OFF                         0x0004
/** Bits 0 - 6 - Implementer ID code. */
# define GIC_REDIST_REG_IIDR_IMPL_ID                    GIC_DIST_REG_IIDR_IMPL_ID
# define GIC_REDIST_REG_IIDR_IMPL_ID_BIT                GIC_DIST_REG_IIDR_IMPL_ID_BIT
/** Bits 0 - 6 - Implementer continuation code. */
# define GIC_REDIST_REG_IIDR_IMPL_CONT                  GIC_DIST_REG_IIDR_IMPL_CONT
# define GIC_REDIST_REG_IIDR_IMPL_CONT_BIT              GIC_DIST_REG_IIDR_IMPL_CONT_BIT
# define GIC_REDIST_REG_IIDR_IMPL_SET(a_Id, a_Cont)     GIC_DIST_REG_IIDR_IMPL_SET(a_Id, a_Cont)

/** Redistributor Type Register - RO. */
#define GIC_REDIST_REG_TYPER_OFF                        0x0008
/** Bit 0 - Indicates whether the GIC implementation supports physical LPIs. */
# define GIC_REDIST_REG_TYPER_PLPIS                     RT_BIT_32(0)
# define GIC_REDIST_REG_TYPER_PLPIS_BIT                 0
/** Bit 1 - Indicates whether the GIC implementation supports virtual LPIs and the direct injection of those. */
# define GIC_REDIST_REG_TYPER_VLPIS                     RT_BIT_32(1)
# define GIC_REDIST_REG_TYPER_VLPIS_BIT                 1
/** Bit 2 - Controls the functionality of GICR_VPENDBASER.Dirty. */
# define GIC_REDIST_REG_TYPER_DIRTY                     RT_BIT_32(2)
# define GIC_REDIST_REG_TYPER_DIRTY_BIT                 2
/** Bit 3 - Indicates whether the redistributor supports direct injection of LPIs. */
# define GIC_REDIST_REG_TYPER_DIRECT_LPI                RT_BIT_32(3)
# define GIC_REDIST_REG_TYPER_DIRECT_LPI_BIT            3
/** Bit 4 - Indicates whether this redistributor is the highest numbered Redistributor in a series. */
# define GIC_REDIST_REG_TYPER_LAST                      RT_BIT_32(4)
# define GIC_REDIST_REG_TYPER_LAST_BIT                  4
/** Bit 5 - Sets support for GICR_CTLR.DPG* bits. */
# define GIC_REDIST_REG_TYPER_DPGS                      RT_BIT_32(5)
# define GIC_REDIST_REG_TYPER_DPGS_BIT                  5
/** Bit 6 - Indicates whether MPAM is supported. */
# define GIC_REDIST_REG_TYPER_MPAM                      RT_BIT_32(6)
# define GIC_REDIST_REG_TYPER_MPAM_BIT                  6
/** Bit 7 - Indicates how the resident vPE is specified. */
# define GIC_REDIST_REG_TYPER_RVPEID                    RT_BIT_32(7)
# define GIC_REDIST_REG_TYPER_RVPEID_BIT                7
/** Bit 8 - 23 - A unique identifier for the PE. */
# define GIC_REDIST_REG_TYPER_CPU_NUMBER                UINT32_C(0x00ffff00)
# define GIC_REDIST_REG_TYPER_CPU_NUMBER_SET(a_CpuNum)  (((a_CpuNum) << 8) & GIC_REDIST_REG_TYPER_CPU_NUMBER)
/** Bit 24 - 25 - The affinity level at Redistributors share an LPI configuration table. */
# define GIC_REDIST_REG_TYPER_CMN_LPI_AFF               (RT_BIT_32(24) | RT_BIT_32(25))
# define GIC_REDIST_REG_TYPER_CMN_LPI_AFF_SET(a_LpiAff) (((a_LpiAff) << 24) & GIC_REDIST_REG_TYPER_CMN_LPI_AFF)
/** All Redistributors must share an LPI Configuration table. */
#  define GIC_REDIST_REG_TYPER_CMN_LPI_AFF_ALL          0
/** All Redistributors with the same affinity 3 value must share an LPI configuration table. */
#  define GIC_REDIST_REG_TYPER_CMN_LPI_AFF_3            1
/** All Redistributors with the same affinity 3.2 value must share an LPI configuration table. */
#  define GIC_REDIST_REG_TYPER_CMN_LPI_AFF_3_2          2
/** All Redistributors with the same affinity 3.2.1 value must share an LPI configuration table. */
#  define GIC_REDIST_REG_TYPER_CMN_LPI_AFF_3_2_1        3
/** Bit 26 - Indicates whether vSGIs are supported. */
# define GIC_REDIST_REG_TYPER_VSGI                      RT_BIT_32(26)
# define GIC_REDIST_REG_TYPER_VSGI_BIT                  26
/** Bit 27 - 31 - Indicates the maximum PPI INTID that a GIC implementation can support. */
# define GIC_REDIST_REG_TYPER_PPI_NUM                   (  RT_BIT_32(27) | RT_BIT_32(28) | RT_BIT_32(29) \
                                                         | RT_BIT_32(30) | RT_BIT_32(31))
# define GIC_REDIST_REG_TYPER_PPI_NUM_SET(a_PpiNum)     (((a_PpiNum) << 27) & GIC_REDIST_REG_TYPER_PPI_NUM)
/** Maximum PPI INTID is 31. */
#  define GIC_REDIST_REG_TYPER_PPI_NUM_MAX_31           0
/** Maximum PPI INTID is 1087. */
#  define GIC_REDIST_REG_TYPER_PPI_NUM_MAX_1087         1
/** Maximum PPI INTID is 1119. */
#  define GIC_REDIST_REG_TYPER_PPI_NUM_MAX_1119         2
# define GIC_REDIST_REG_TYPER_CPU_NUMBER_MASK           (GIC_REDIST_REG_TYPER_CPU_NUMBER >> 8)

/** Redistributor Type Register (the affinity value of the 64-bit register) - RO. */
#define GIC_REDIST_REG_TYPER_AFFINITY_OFF               0x000c
/** Bit 0 - 31 - The identity of the PE associated with this Redistributor. */
# define GIC_REDIST_REG_TYPER_AFFINITY_VALUE            UINT32_C(0xffffffff)
# define GIC_REDIST_REG_TYPER_AFFINITY_VALUE_SET(a_Aff) ((a_Aff) & GIC_REDIST_REG_TYPER_AFFINITY_VALUE)

/** Redistributor Error Reporting Status Register (optional) - RW. */
#define GIC_REDIST_REG_STATUSR_OFF                      0x0010
/** Redistributor Wake Register - RW. */
#define GIC_REDIST_REG_WAKER_OFF                        0x0014
/** Redistributor Report maximum PARTID and PMG Register - RO. */
#define GIC_REDIST_REG_MPAMIDR_OFF                      0x0018
/** Redistributor Set PARTID and PMG Register - RW. */
#define GIC_REDIST_REG_PARTIDR_OFF                      0x001c
/** Redistributor Set LPI Pending Register - WO. */
#define GIC_REDIST_REG_SETLPIR_OFF                      0x0040
/** Redistributor Clear LPI Pending Register - WO. */
#define GIC_REDIST_REG_CLRLPIR_OFF                      0x0048

/** Redistributor Properties Base Address Register - RW. */
#define GIC_REDIST_REG_PROPBASER_OFF                    0x0070
#define GIC_BF_REDIST_REG_PROPBASER_ID_BITS_SHIFT       0
#define GIC_BF_REDIST_REG_PROPBASER_ID_BITS_MASK        UINT64_C(0x000000000000001f)
#define GIC_BF_REDIST_REG_PROPBASER_RSVD_6_5_SHIFT      5
#define GIC_BF_REDIST_REG_PROPBASER_RSVD_6_5_MASK       UINT64_C(0x0000000000000060)
#define GIC_BF_REDIST_REG_PROPBASER_INNER_CACHE_SHIFT   7
#define GIC_BF_REDIST_REG_PROPBASER_INNER_CACHE_MASK    UINT64_C(0x0000000000000380)
#define GIC_BF_REDIST_REG_PROPBASER_SHAREABILITY_SHIFT  10
#define GIC_BF_REDIST_REG_PROPBASER_SHAREABILITY_MASK   UINT64_C(0x0000000000000c00)
#define GIC_BF_REDIST_REG_PROPBASER_PHYS_ADDR_SHIFT     12
#define GIC_BF_REDIST_REG_PROPBASER_PHYS_ADDR_MASK      UINT64_C(0x000ffffffffff000)
#define GIC_BF_REDIST_REG_PROPBASER_RSVD_55_52_SHIFT    52
#define GIC_BF_REDIST_REG_PROPBASER_RSVD_55_52_MASK     UINT64_C(0x00f0000000000000)
#define GIC_BF_REDIST_REG_PROPBASER_OUTER_CACHE_SHIFT   56
#define GIC_BF_REDIST_REG_PROPBASER_OUTER_CACHE_MASK    UINT64_C(0x0700000000000000)
#define GIC_BF_REDIST_REG_PROPBASER_RSVD_63_59_SHIFT    59
#define GIC_BF_REDIST_REG_PROPBASER_RSVD_63_59_MASK     UINT64_C(0xf800000000000000)
RT_BF_ASSERT_COMPILE_CHECKS(GIC_BF_REDIST_REG_PROPBASER_, UINT64_C(0), UINT64_MAX,
                            (ID_BITS, RSVD_6_5, INNER_CACHE, SHAREABILITY, PHYS_ADDR, RSVD_55_52, OUTER_CACHE, RSVD_63_59));
#define GIC_REDIST_REG_PROPBASER_RW_MASK                (UINT64_MAX & ~(  GIC_BF_REDIST_REG_PROPBASER_RSVD_6_5_MASK   \
                                                                        | GIC_BF_REDIST_REG_PROPBASER_RSVD_55_52_MASK \
                                                                        | GIC_BF_REDIST_REG_PROPBASER_RSVD_63_59_MASK))

/** Redistributor LPI Pending Table Base Address Register - RW. */
#define GIC_REDIST_REG_PENDBASER_OFF                    0x0078
#define GIC_BF_REDIST_REG_PENDBASER_RSVD_6_0_SHIFT      0
#define GIC_BF_REDIST_REG_PENDBASER_RSVD_6_0_MASK       UINT64_C(0x000000000000007f)
#define GIC_BF_REDIST_REG_PENDBASER_INNER_CACHE_SHIFT   7
#define GIC_BF_REDIST_REG_PENDBASER_INNER_CACHE_MASK    UINT64_C(0x0000000000000380)
#define GIC_BF_REDIST_REG_PENDBASER_SHAREABILITY_SHIFT  10
#define GIC_BF_REDIST_REG_PENDBASER_SHAREABILITY_MASK   UINT64_C(0x0000000000000c00)
#define GIC_BF_REDIST_REG_PENDBASER_RSVD_15_12_SHIFT    12
#define GIC_BF_REDIST_REG_PENDBASER_RSVD_15_12_MASK     UINT64_C(0x000000000000f000)
#define GIC_BF_REDIST_REG_PENDBASER_PHYS_ADDR_SHIFT     16
#define GIC_BF_REDIST_REG_PENDBASER_PHYS_ADDR_MASK      UINT64_C(0x000fffffffff0000)
#define GIC_BF_REDIST_REG_PENDBASER_RSVD_55_52_SHIFT    52
#define GIC_BF_REDIST_REG_PENDBASER_RSVD_55_52_MASK     UINT64_C(0x00f0000000000000)
#define GIC_BF_REDIST_REG_PENDBASER_OUTER_CACHE_SHIFT   56
#define GIC_BF_REDIST_REG_PENDBASER_OUTER_CACHE_MASK    UINT64_C(0x0700000000000000)
#define GIC_BF_REDIST_REG_PENDBASER_RSVD_61_59_SHIFT    59
#define GIC_BF_REDIST_REG_PENDBASER_RSVD_61_59_MASK     UINT64_C(0x3800000000000000)
#define GIC_BF_REDIST_REG_PENDBASER_PTZ_SHIFT           62
#define GIC_BF_REDIST_REG_PENDBASER_PTZ_MASK            UINT64_C(0x4000000000000000)
#define GIC_BF_REDIST_REG_PENDBASER_RSVD_63_SHIFT       63
#define GIC_BF_REDIST_REG_PENDBASER_RSVD_63_MASK        UINT64_C(0x8000000000000000)
RT_BF_ASSERT_COMPILE_CHECKS(GIC_BF_REDIST_REG_PENDBASER_, UINT64_C(0), UINT64_MAX,
                            (RSVD_6_0, INNER_CACHE, SHAREABILITY, RSVD_15_12, PHYS_ADDR, RSVD_55_52, OUTER_CACHE, RSVD_61_59,
                             PTZ, RSVD_63));
#define GIC_REDIST_REG_PENDBASER_RW_MASK                (UINT64_MAX & ~(  GIC_BF_REDIST_REG_PENDBASER_RSVD_6_0_MASK   \
                                                                        | GIC_BF_REDIST_REG_PENDBASER_RSVD_15_12_MASK \
                                                                        | GIC_BF_REDIST_REG_PENDBASER_RSVD_55_52_MASK \
                                                                        | GIC_BF_REDIST_REG_PENDBASER_RSVD_61_59_MASK \
                                                                        | GIC_BF_REDIST_REG_PENDBASER_RSVD_63_MASK))

/** Redistributor Invalidate LPI Register - WO. */
#define GIC_REDIST_REG_INVLPIR_OFF                      0x00a0
/** Redistributor Invalidate All Register - WO. */
#define GIC_REDIST_REG_INVALLR_OFF                      0x00b0
/** Redistributor Synchronize Register - RO. */
#define GIC_REDIST_REG_SYNCR_OFF                        0x00c0

/** Redistributor Peripheral ID2 Register - RO. */
#define GIC_REDIST_REG_PIDR2_OFF                        0xffe8
/** Bit 4 - 7 - GIC architecture revision */
# define GIC_REDIST_REG_PIDR2_ARCHREV                   (RT_BIT_32(4) | RT_BIT_32(5) | RT_BIT_32(6) | RT_BIT_32(7))
# define GIC_REDIST_REG_PIDR2_ARCHREV_SET(a_ArchRev)    (((a_ArchRev) << 4) & GIC_REDIST_REG_PIDR2_ARCHREV)
/** GICv1 architecture revision. */
#  define GIC_REDIST_REG_PIDR2_ARCHREV_GICV1            0x1
/** GICv2 architecture revision. */
#  define GIC_REDIST_REG_PIDR2_ARCHREV_GICV2            0x2
/** GICv3 architecture revision. */
#  define GIC_REDIST_REG_PIDR2_ARCHREV_GICV3            0x3
/** GICv4 architecture revision. */
#  define GIC_REDIST_REG_PIDR2_ARCHREV_GICV4            0x4
/** @} */


/** @name GIC SGI and PPI Redistributor registers
 *  (Adjacent to the GIC Redistributor register space).
 * @{ */
/** Size of the SGI and PPI redistributor register frame. */
#define GIC_REDIST_SGI_PPI_REG_FRAME_SIZE               _64K

/** Interrupt Group Register 0 - RW. */
#define GIC_REDIST_SGI_PPI_REG_IGROUPR0_OFF             0x0080
/** Interrupt Group Register 2 for extended PPI range - RW, last offset. */
#define GIC_REDIST_SGI_PPI_REG_IGROUPRnE_OFF_LAST       0x0088
/** Interrupt Group Register, range in bytes. */
#define GIC_REDIST_SGI_PPI_REG_IGROUPRnE_RANGE_SIZE     (GIC_REDIST_SGI_PPI_REG_IGROUPRnE_OFF_LAST + sizeof(uint32_t) - GIC_REDIST_SGI_PPI_REG_IGROUPR0_OFF)

/** Interrupt Set Enable Register 0 - RW. */
#define GIC_REDIST_SGI_PPI_REG_ISENABLER0_OFF           0x0100
/** Interrupt Set Enable Register 1 for extended PPI range - RW. */
#define GIC_REDIST_SGI_PPI_REG_ISENABLER1E_OFF          0x0104
/** Interrupt Set Enable Register 2 for extended PPI range - RW. */
#define GIC_REDIST_SGI_PPI_REG_ISENABLER2E_OFF          0x0108
#define GIC_REDIST_SGI_PPI_REG_ISENABLERnE_OFF_LAST     GIC_REDIST_SGI_PPI_REG_ISENABLER2E_OFF
/** Interrupt Set Enable Register, range in bytes. */
#define GIC_REDIST_SGI_PPI_REG_ISENABLERnE_RANGE_SIZE   (GIC_REDIST_SGI_PPI_REG_ISENABLERnE_OFF_LAST + sizeof(uint32_t) - GIC_REDIST_SGI_PPI_REG_ISENABLER0_OFF)

/** Interrupt Clear Enable Register 0 - RW. */
#define GIC_REDIST_SGI_PPI_REG_ICENABLER0_OFF           0x0180
/** Interrupt Clear Enable Register for extended PPI range, start offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_ICENABLERnE_OFF_START    0x0184
/** Interrupt Clear Enable Register for extended PPI range, last offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_ICENABLERnE_OFF_LAST     0x0188
/** Interrupt Clear Enable Register, range in bytes. */
#define GIC_REDIST_SGI_PPI_REG_ICENABLERnE_RANGE_SIZE   (GIC_REDIST_SGI_PPI_REG_ICENABLERnE_OFF_LAST + sizeof(uint32_t) - GIC_REDIST_SGI_PPI_REG_ICENABLER0_OFF)

/** Interrupt Set Pending Register 0 - RW. */
#define GIC_REDIST_SGI_PPI_REG_ISPENDR0_OFF             0x0200
/** Interrupt Set Pending Registers for extended PPI range, last offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_ISPENDRnE_OFF_LAST       0x0208
/** Interrupt Set Pending Registers for extended PPI range, range in bytes. */
#define GIC_REDIST_SGI_PPI_REG_ISPENDRnE_RANGE_SIZE     (GIC_REDIST_SGI_PPI_REG_ISPENDRnE_OFF_LAST + sizeof(uint32_t) - GIC_REDIST_SGI_PPI_REG_ISPENDR0_OFF)

/** Interrupt Clear Pending Register 0 - RW. */
#define GIC_REDIST_SGI_PPI_REG_ICPENDR0_OFF             0x0280
/** Interrupt Clear Pending Registers for extended PPI range, last offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_ICPENDRnE_OFF_LAST       0x0288
/** Interrupt Clear Pending Register for extended PPI range, range in bytes. */
#define GIC_REDIST_SGI_PPI_REG_ICPENDRnE_RANGE_SIZE     (GIC_REDIST_SGI_PPI_REG_ICPENDRnE_OFF_LAST + sizeof(uint32_t) - GIC_REDIST_SGI_PPI_REG_ICPENDR0_OFF)

/** Interrupt Set Active Register 0 - RW. */
#define GIC_REDIST_SGI_PPI_REG_ISACTIVER0_OFF           0x0300
/** Interrupt Set Active Registers for extended PPI range, last offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_ISACTIVERnE_OFF_LAST     0x0308
/** Interrupt Set Active Registers for extended PPI range, range in bytes. */
#define GIC_REDIST_SGI_PPI_REG_ISACTIVERnE_RANGE_SIZE   (GIC_REDIST_SGI_PPI_REG_ISACTIVERnE_OFF_LAST + sizeof(uint32_t) - GIC_REDIST_SGI_PPI_REG_ISACTIVER0_OFF)

/** Interrupt Clear Active Register 0 - RW. */
#define GIC_REDIST_SGI_PPI_REG_ICACTIVER0_OFF           0x0380
/** Interrupt Clear Active Registers for extended PPI range, last offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_ICACTIVERnE_OFF_LAST     0x0388
/** Interrupt Clear Active Register for extended PPI range, range in bytes. */
#define GIC_REDIST_SGI_PPI_REG_ICACTIVERnE_RANGE_SIZE   (GIC_REDIST_SGI_PPI_REG_ICACTIVERnE_OFF_LAST + sizeof(uint32_t) - GIC_REDIST_SGI_PPI_REG_ICACTIVER0_OFF)

/** Interrupt Priority Registers, start offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_IPRIORITYRn_OFF_START    0x0400
/** Interrupt Priority Registers, last offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_IPRIORITYRn_OFF_LAST     0x041c
/** Interrupt Priority Registers for extended PPI range, start offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_IPRIORITYRnE_OFF_START   0x0420
/** Interrupt Priority Registers for extended PPI range, last offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_IPRIORITYRnE_OFF_LAST    0x045c
/** Interrupt Priority Registers for extended PPI range, range in bytes. */
#define GIC_REDIST_SGI_PPI_REG_IPRIORITYRnE_RANGE_SIZE  (GIC_REDIST_SGI_PPI_REG_IPRIORITYRnE_OFF_LAST + sizeof(uint32_t) - GIC_REDIST_SGI_PPI_REG_IPRIORITYRn_OFF_START)

/** SGI Configuration Register - RW. */
#define GIC_REDIST_SGI_PPI_REG_ICFGR0_OFF               0x0c00
/** PPI Configuration Register - RW. */
#define GIC_REDIST_SGI_PPI_REG_ICFGR1_OFF               0x0c04
/** Extended PPI Configuration Register, start offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_ICFGRnE_OFF_START        0x0c08
/** Extended PPI Configuration Register, last offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_ICFGRnE_OFF_LAST         0x0c14
/** SGI Configure Register, range in bytes. */
#define GIC_REDIST_SGI_PPI_REG_ICFGRnE_RANGE_SIZE       (GIC_REDIST_SGI_PPI_REG_ICFGRnE_OFF_LAST + sizeof(uint32_t) - GIC_REDIST_SGI_PPI_REG_ICFGR0_OFF)

/** Interrupt Group Modifier Register 0 - RW. */
#define GIC_REDIST_SGI_PPI_REG_IGRPMODR0_OFF            0x0d00
/** Interrupt Group Modifier Register 1 for extended PPI range - RW. */
#define GIC_REDIST_SGI_PPI_REG_IGRPMODR1E_OFF           0x0d04
/** Interrupt Group Modifier Register 2 for extended PPI range - RW. */
#define GIC_REDIST_SGI_PPI_REG_IGRPMODR2E_OFF           0x0d08

/** Non Secure Access Control Register - RW. */
#define GIC_REDIST_SGI_PPI_REG_NSACR_OFF                0x0e00

/** Non maskable Interrupt Register for PPIs - RW. */
#define GIC_REDIST_SGI_PPI_REG_INMIR0_OFF               0x0f80
/** Non maskable Interrupt Register for Extended PPIs, start offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_INMIRnE_OFF_START        0x0f84
/** Non maskable Interrupt Register for Extended PPIs, last offset - RW. */
#define GIC_REDIST_SGI_PPI_REG_INMIRnE_OFF_LAST         0x0ffc
/** Non maskable Interrupt Register for Extended PPIs, range in bytes. */
#define GIC_REDIST_SGI_PPI_REG_INMIRnE_RANGE_SIZE       (GIC_REDIST_SGI_PPI_REG_INMIRnE_OFF_LAST + sizeof(uint32_t) - GIC_REDIST_SGI_PPI_REG_INMIR0_OFF)
/** @} */


/** @name JEDEC codes for ARM.
 * @{ */
/** JEP106 identification code. */
#define GIC_JEDEC_JEP106_IDENTIFICATION_CODE            0x3b
/** JEP106 continuation code. */
#define GIC_JEDEC_JEP106_CONTINUATION_CODE              0x4

/** DES_0 - JEP106 identification code bits (3:0). */
#define GIC_JEDEC_JEP10_DES_0(a_JepIdCode)              ((a_JepIdCode) & 0xf)
/** DES_1 - JEP106 identification code bits (6:4). */
#define GIC_JEDEC_JEP10_DES_1(a_JepIdCode)              (((a_JepIdCode) >> 4) & 0x70)
/** @} */


/** @name GIC LPIs.
 * @{ */
/** Configuration Table Entry: Enable. */
#define GIC_BF_LPI_CTE_ENABLE_SHIFT                     0
#define GIC_BF_LPI_CTE_ENABLE_MASK                      UINT8_C(0x1)
/** Configuration Table Entry: Reserved (bit 1). */
#define GIC_BF_LPI_CTE_RSVD_1_SHIFT                     1
#define GIC_BF_LPI_CTE_RSVD_1_MASK                      UINT8_C(0x2)
/** Configuration Table Entry: Priority. */
#define GIC_BF_LPI_CTE_PRIORITY_SHIFT                   2
#define GIC_BF_LPI_CTE_PRIORITY_MASK                    UINT8_C(0xfc)
RT_BF_ASSERT_COMPILE_CHECKS(GIC_BF_LPI_CTE_, UINT8_C(0), UINT8_MAX,
                            (ENABLE, RSVD_1, PRIORITY));

/** Minimum number of bits required to enable LPIs (i.e. should accomodate
 *  GIC_INTID_RANGE_LPI_START). */
#define GIC_LPI_ID_BITS_MIN                             14
/** @} */

/** GIC idle priority. */
#define GIC_IDLE_PRIORITY                               0xff

#endif /* !VBOX_INCLUDED_gic_h */

