/** @file
 * ARMv8 GIC Interrupt Translation Service (ITS) definitions.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_gic_its_h
#define VBOX_INCLUDED_gic_its_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>

/** Size of the ITS register frame. */
#define GITS_REG_FRAME_SIZE                                     _64K
/** The offset mask for an ITS register within a frame. */
#define GITS_REG_OFFSET_MASK                                    (_64K - 1)

/** The GITS command queue page size. */
#define GITS_CMD_QUEUE_PAGE_SIZE                                0x1000
/** The GITS command queue page offset mask. */
#define GITS_CMD_QUEUE_PAGE_OFFSET_MASK                         0xfff
/** The guest page shift (x86). */
#define GITS_CMD_QUEUE_PAGE_SHIFT                               12

/** The GITS command size in bytes.  */
#define GITS_CMD_SIZE                                           32

/** GITS_CTLR: Control register - RW. */
#define GITS_CTRL_REG_CTLR_OFF                                  0x0000
/** GITS_CTLR: Enabled. */
#define GITS_BF_CTRL_REG_CTLR_ENABLED_SHIFT                     0
#define GITS_BF_CTRL_REG_CTLR_ENABLED_MASK                      UINT32_C(0x00000001)
/** GITS_CTLR: ImDe - Implementation Defined. */
#define GITS_BF_CTRL_REG_CTLR_IM_DE_SHIFT                       1
#define GITS_BF_CTRL_REG_CTLR_IM_DE_MASK                        UINT32_C(0x00000002)
/** GITS_CTLR: Reserved (bits 3:2). */
#define GITS_BF_CTRL_REG_CTLR_RSVD_3_2_SHIFT                    2
#define GITS_BF_CTRL_REG_CTLR_RSVD_3_2_MASK                     UINT32_C(0x0000000c)
/** GITS_CTLR: ITS_Number (0 for GICv3). */
#define GITS_BF_CTRL_REG_CTLR_ITS_NUMBER_SHIFT                  4
#define GITS_BF_CTRL_REG_CTLR_ITS_NUMBER_MASK                   UINT32_C(0x000000f0)
/** GITS_CTLR: UMSIirq - Unmapped MSI reporting interrupt enable. */
#define GITS_BF_CTRL_REG_CTLR_UMSI_IRQ_SHIFT                    8
#define GITS_BF_CTRL_REG_CTLR_UMSI_IRQ_MASK                     UINT32_C(0x00000100)
/** GITS_CTLR: Reserved (bits 30:9). */
#define GITS_BF_CTRL_REG_CTLR_RSVD_30_9_SHIFT                   9
#define GITS_BF_CTRL_REG_CTLR_RSVD_30_9_MASK                    UINT32_C(0x7ffffe00)
/** GITS_CTLR: Quiescent. */
#define GITS_BF_CTRL_REG_CTLR_QUIESCENT_SHIFT                   31
#define GITS_BF_CTRL_REG_CTLR_QUIESCENT_MASK                    UINT32_C(0x80000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CTRL_REG_CTLR_, UINT32_C(0), UINT32_MAX,
                            (ENABLED, IM_DE, RSVD_3_2, ITS_NUMBER, UMSI_IRQ, RSVD_30_9, QUIESCENT));
/** GITS_CTLR: Mask of valid read-write bits. */
#define GITS_BF_CTRL_REG_CTLR_RW_MASK                           (  GITS_BF_CTRL_REG_CTLR_ENABLED_MASK \
                                                                 | GITS_BF_CTRL_REG_CTLR_IM_DE_MASK)

/** GITS_IIDR: Implementer and revision register - RO. */
#define GITS_CTRL_REG_IIDR_OFF                                  0x0004
/** GITS_IIDR: Implementer - JEP106 identification code. */
#define GITS_BF_CTRL_REG_IIDR_IMPL_ID_CODE_SHIFT                0
#define GITS_BF_CTRL_REG_IIDR_IMPL_ID_CODE_MASK                 UINT32_C(0x0000007f)
/** GITS_IIDR: Implementer - Reserved (bit 7). */
#define GITS_BF_CTRL_REG_IIDR_IMPL_ZERO_7_SHIFT                 7
#define GITS_BF_CTRL_REG_IIDR_IMPL_ZERO_7_MASK                  UINT32_C(0x00000080)
/** GITS_IIDR: Implementer - JEP106 continuation code. */
#define GITS_BF_CTRL_REG_IIDR_IMPL_CONT_CODE_SHIFT              8
#define GITS_BF_CTRL_REG_IIDR_IMPL_CONT_CODE_MASK               UINT32_C(0x00000f00)
/** GITS_IIDR: Revision. */
#define GITS_BF_CTRL_REG_IIDR_REVISION_SHIFT                    12
#define GITS_BF_CTRL_REG_IIDR_REVISION_MASK                     UINT32_C(0x0000f000)
/** GITS_IIDR: Variant. */
#define GITS_BF_CTRL_REG_IIDR_VARIANT_SHIFT                     16
#define GITS_BF_CTRL_REG_IIDR_VARIANT_MASK                      UINT32_C(0x000f0000)
/** GITS_IIDR: Reserved (bits 23:20). */
#define GITS_BF_CTRL_REG_IIDR_RSVD_23_20_SHIFT                  20
#define GITS_BF_CTRL_REG_IIDR_RSVD_23_20_MASK                   UINT32_C(0x00f00000)
/** GITS_IIDR: Product ID. */
#define GITS_BF_CTRL_REG_IIDR_PRODUCT_ID_SHIFT                  24
#define GITS_BF_CTRL_REG_IIDR_PRODUCT_ID_MASK                   UINT32_C(0xff000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CTRL_REG_IIDR_, UINT32_C(0), UINT32_MAX,
                            (IMPL_ID_CODE, IMPL_ZERO_7, IMPL_CONT_CODE, REVISION, VARIANT, RSVD_23_20, PRODUCT_ID));

/** GITS_TYPER: Feature register - RO. */
#define GITS_CTRL_REG_TYPER_OFF                                 0x0008
/** GITS_TYPER: Physical - Physical LPI support. */
#define GITS_BF_CTRL_REG_TYPER_PHYSICAL_SHIFT                   0
#define GITS_BF_CTRL_REG_TYPER_PHYSICAL_MASK                    UINT64_C(0x0000000000000001)
/** GITS_TYPER: Virtual - Virtual LPI support. */
#define GITS_BF_CTRL_REG_TYPER_VIRTUAL_SHIFT                    1
#define GITS_BF_CTRL_REG_TYPER_VIRTUAL_MASK                     UINT64_C(0x0000000000000002)
/** GITS_TYPER: CCT - Cumulative Collections Table. */
#define GITS_BF_CTRL_REG_TYPER_CCT_SHIFT                        2
#define GITS_BF_CTRL_REG_TYPER_CCT_MASK                         UINT64_C(0x0000000000000004)
/** GITS_TYPER: Implementation Defined. */
#define GITS_BF_CTRL_REG_TYPER_IM_DE_SHIFT                      3
#define GITS_BF_CTRL_REG_TYPER_IM_DE_MASK                       UINT64_C(0x0000000000000008)
/** GITS_TYPER: ITT_entry_size - Size of translation table entry. */
#define GITS_BF_CTRL_REG_TYPER_ITT_ENTRY_SIZE_SHIFT             4
#define GITS_BF_CTRL_REG_TYPER_ITT_ENTRY_SIZE_MASK              UINT64_C(0x00000000000000f0)
/** GITS_TYPER: ID_bits - Number of event ID bits implemented (minus one). */
#define GITS_BF_CTRL_REG_TYPER_ID_BITS_SHIFT                    8
#define GITS_BF_CTRL_REG_TYPER_ID_BITS_MASK                     UINT64_C(0x0000000000001f00)
/** GITS_TYPER: Devbits - Number of device ID bits implemented (minus one). */
#define GITS_BF_CTRL_REG_TYPER_DEV_BITS_SHIFT                   13
#define GITS_BF_CTRL_REG_TYPER_DEV_BITS_MASK                    UINT64_C(0x000000000003e000)
/** GITS_TYPER: SEIS - SEI support for virtual CPUs. */
#define GITS_BF_CTRL_REG_TYPER_SEIS_SHIFT                       18
#define GITS_BF_CTRL_REG_TYPER_SEIS_MASK                        UINT64_C(0x0000000000040000)
/** GITS_TYPER: PTA - Physical target address format. */
#define GITS_BF_CTRL_REG_TYPER_PTA_SHIFT                        19
#define GITS_BF_CTRL_REG_TYPER_PTA_MASK                         UINT64_C(0x0000000000080000)
/** GITS_TYPER: Reserved (bits 23:20). */
#define GITS_BF_CTRL_REG_TYPER_RSVD_23_20_SHIFT                 20
#define GITS_BF_CTRL_REG_TYPER_RSVD_23_20_MASK                  UINT64_C(0x0000000000f00000)
/** GITS_TYPER: HCC - Hardware collection count. */
#define GITS_BF_CTRL_REG_TYPER_HCC_SHIFT                        24
#define GITS_BF_CTRL_REG_TYPER_HCC_MASK                         UINT64_C(0x00000000ff000000)
/** GITS_TYPER: CIDbits - Number of collection ID bits (minus one). */
#define GITS_BF_CTRL_REG_TYPER_CID_BITS_SHIFT                   32
#define GITS_BF_CTRL_REG_TYPER_CID_BITS_MASK                    UINT64_C(0x0000000f00000000)
/** GITS_TYPER: CIL - Collection ID limit. */
#define GITS_BF_CTRL_REG_TYPER_CIL_SHIFT                        36
#define GITS_BF_CTRL_REG_TYPER_CIL_MASK                         UINT64_C(0x0000001000000000)
/** GITS_TYPER: VMOVP - Form of VMOVP command. */
#define GITS_BF_CTRL_REG_TYPER_VMOVP_SHIFT                      37
#define GITS_BF_CTRL_REG_TYPER_VMOVP_MASK                       UINT64_C(0x0000002000000000)
/** GITS_TYPER: MPAM - Memory partitioning and monitoring support. */
#define GITS_BF_CTRL_REG_TYPER_MPAM_SHIFT                       38
#define GITS_BF_CTRL_REG_TYPER_MPAM_MASK                        UINT64_C(0x0000004000000000)
/** GITS_TYPER: VSGI - Direct injection of virtual SGI support. */
#define GITS_BF_CTRL_REG_TYPER_VSGI_SHIFT                       39
#define GITS_BF_CTRL_REG_TYPER_VSGI_MASK                        UINT64_C(0x0000008000000000)
/** GITS_TYPER: VMAPP - VMAPP command support. */
#define GITS_BF_CTRL_REG_TYPER_VMAPP_SHIFT                      40
#define GITS_BF_CTRL_REG_TYPER_VMAPP_MASK                       UINT64_C(0x0000010000000000)
/** GITS_TYPER: SVPET - Shared VPE table configuration. */
#define GITS_BF_CTRL_REG_TYPER_SVPET_SHIFT                      41
#define GITS_BF_CTRL_REG_TYPER_SVPET_MASK                       UINT64_C(0x0000060000000000)
/** GITS_TYPER: nID - Individual doorbell interrupt support. */
#define GITS_BF_CTRL_REG_TYPER_NID_SHIFT                        43
#define GITS_BF_CTRL_REG_TYPER_NID_MASK                         UINT64_C(0x0000080000000000)
/** GITS_TYPER: UMSI - Support for reporting receipts of unmapped MSI. */
#define GITS_BF_CTRL_REG_TYPER_UMSI_SHIFT                       44
#define GITS_BF_CTRL_REG_TYPER_UMSI_MASK                        UINT64_C(0x0000100000000000)
/** GITS_TYPER: UMSIirq - Support for generating interrupt on receiving unmapped MSI. */
#define GITS_BF_CTRL_REG_TYPER_UMSI_IRQ_SHIFT                   45
#define GITS_BF_CTRL_REG_TYPER_UMSI_IRQ_MASK                    UINT64_C(0x0000200000000000)
/** GITS_TYPER: INV - Invalidate ITS cache on disable. */
#define GITS_BF_CTRL_REG_TYPER_INV_SHIFT                        46
#define GITS_BF_CTRL_REG_TYPER_INV_MASK                         UINT64_C(0x0000400000000000)
/** GITS_TYPER: Reserved (bits 63:47). */
#define GITS_BF_CTRL_REG_TYPER_RSVD_63_47_SHIFT                 47
#define GITS_BF_CTRL_REG_TYPER_RSVD_63_47_MASK                  UINT64_C(0xffff800000000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CTRL_REG_TYPER_, UINT64_C(0), UINT64_MAX,
                            (PHYSICAL, VIRTUAL, CCT, IM_DE, ITT_ENTRY_SIZE, ID_BITS, DEV_BITS, SEIS, PTA, RSVD_23_20, HCC,
                             CID_BITS, CIL, VMOVP, MPAM, VSGI, VMAPP, SVPET, NID, UMSI, UMSI_IRQ, INV, RSVD_63_47));

/** GITS_MPAMIDR: Memory partitioning ID sizes. */
#define GITS_CTRL_REG_MPAMIDR_OFF                               0x0010
/** GITS_MPAMIDR: PARTIDmax - Maximum PARTID value supported. */
#define GITS_BF_CTRL_REG_MPAMIDR_PARTID_MAX_SHIFT               0
#define GITS_BF_CTRL_REG_MPAMIDR_PARTID_MAX_MASK                UINT32_C(0x0000ffff)
/** GITS_MPAMIDR: PMGmax - Maximum PMG value supported. */
#define GITS_BF_CTRL_REG_MPAMIDR_PMG_MAX_SHIFT                  16
#define GITS_BF_CTRL_REG_MPAMIDR_PMG_MAX_MASK                   UINT32_C(0x00ff0000)
/** GITS_MPAMIDR: Reserved (bits 24:31). */
#define GITS_BF_CTRL_REG_MPAMIDR_RSVD_31_24_SHIFT               24
#define GITS_BF_CTRL_REG_MPAMIDR_RSVD_31_24_MASK                UINT32_C(0xff000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CTRL_REG_MPAMIDR_, UINT32_C(0), UINT32_MAX, (PARTID_MAX, PMG_MAX, RSVD_31_24));

/** GITS_PARTID: PARTID and PMG values register. */
#define GITS_CTRL_REG_PARTIDR_OFF                               0x0014
/** GITS_PARTID: PARTID - PARTID when ITS accesses memory. */
#define GITS_BF_CTRL_REG_PARTIDR_PARTID_SHIFT                   0
#define GITS_BF_CTRL_REG_PARTIDR_PARTID_MASK                    UINT32_C(0x0000ffff)
/** GITS_PARTID: PMG - PMG value when ITS accesses memory. */
#define GITS_BF_CTRL_REG_PARTIDR_PMG_SHIFT                      16
#define GITS_BF_CTRL_REG_PARTIDR_PMG_MASK                       UINT32_C(0x00ff0000)
/** GITS_PARTID: Reserved (bits 24:31). */
#define GITS_BF_CTRL_REG_PARTIDR_RSVD_31_24_SHIFT               24
#define GITS_BF_CTRL_REG_PARTIDR_RSVD_31_24_MASK                UINT32_C(0xff000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CTRL_REG_PARTIDR_, UINT32_C(0), UINT32_MAX, (PARTID, PMG, RSVD_31_24));

#define GITS_CTRL_REG_MPIDR_OFF                                 0x0018
#define GITS_CTRL_REG_STATUSR_OFF                               0x0040
#define GITS_CTRL_REG_UMSIR_OFF                                 0x0048

/** GITS_CBASER: ITS command queue base register - RW. */
#define GITS_CTRL_REG_CBASER_OFF                                0x0080
#define GITS_BF_CTRL_REG_CBASER_SIZE_SHIFT                      0
#define GITS_BF_CTRL_REG_CBASER_SIZE_MASK                       UINT64_C(0x00000000000000ff)
#define GITS_BF_CTRL_REG_CBASER_RSVD_9_8_SHIFT                  8
#define GITS_BF_CTRL_REG_CBASER_RSVD_9_8_MASK                   UINT64_C(0x0000000000000300)
#define GITS_BF_CTRL_REG_CBASER_SHAREABILITY_SHIFT              10
#define GITS_BF_CTRL_REG_CBASER_SHAREABILITY_MASK               UINT64_C(0x0000000000000c00)
#define GITS_BF_CTRL_REG_CBASER_PHYS_ADDR_SHIFT                 12
#define GITS_BF_CTRL_REG_CBASER_PHYS_ADDR_MASK                  UINT64_C(0x000ffffffffff000)
#define GITS_BF_CTRL_REG_CBASER_RSVD_52_SHIFT                   52
#define GITS_BF_CTRL_REG_CBASER_RSVD_52_MASK                    UINT64_C(0x0010000000000000)
#define GITS_BF_CTRL_REG_CBASER_OUTER_CACHE_SHIFT               53
#define GITS_BF_CTRL_REG_CBASER_OUTER_CACHE_MASK                UINT64_C(0x00e0000000000000)
#define GITS_BF_CTRL_REG_CBASER_RSVD_58_56_SHIFT                56
#define GITS_BF_CTRL_REG_CBASER_RSVD_58_56_MASK                 UINT64_C(0x0700000000000000)
#define GITS_BF_CTRL_REG_CBASER_INNER_CACHE_SHIFT               59
#define GITS_BF_CTRL_REG_CBASER_INNER_CACHE_MASK                UINT64_C(0x3800000000000000)
#define GITS_BF_CTRL_REG_CBASER_RSVD_62_SHIFT                   62
#define GITS_BF_CTRL_REG_CBASER_RSVD_62_MASK                    UINT64_C(0x4000000000000000)
#define GITS_BF_CTRL_REG_CBASER_VALID_SHIFT                     63
#define GITS_BF_CTRL_REG_CBASER_VALID_MASK                      UINT64_C(0x8000000000000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CTRL_REG_CBASER_, UINT64_C(0), UINT64_MAX,
                            (SIZE, RSVD_9_8, SHAREABILITY, PHYS_ADDR, RSVD_52, OUTER_CACHE, RSVD_58_56, INNER_CACHE, RSVD_62,
                             VALID));
/** GITS_CBASER: Physical address bits [15:12] are reserved MBZ. */
#define GITS_CTRL_REG_CBASER_PHYS_ADDR_RSVD_15_12_MASK          UINT64_C(0x000000000000f000)
/** GITS_CBASER: Mask of valid read-write bits. */
#define GITS_CTRL_REG_CBASER_RW_MASK                            (UINT64_MAX & ~(GITS_BF_CTRL_REG_CBASER_RSVD_9_8_MASK   | \
                                                                                GITS_BF_CTRL_REG_CBASER_RSVD_52_MASK    | \
                                                                                GITS_BF_CTRL_REG_CBASER_RSVD_58_56_MASK | \
                                                                                GITS_BF_CTRL_REG_CBASER_RSVD_62_MASK    | \
                                                                                GITS_CTRL_REG_CBASER_PHYS_ADDR_RSVD_15_12_MASK))

/** GITS_CWRITER: ITS command queue write register - RW. */
#define GITS_CTRL_REG_CWRITER_OFF                               0x0088
#define GITS_BF_CTRL_REG_CWRITER_RETRY_SHIFT                    0
#define GITS_BF_CTRL_REG_CWRITER_RETRY_MASK                     UINT64_C(0x0000000000000001)
#define GITS_BF_CTRL_REG_CWRITER_RSVD_4_1_SHIFT                 1
#define GITS_BF_CTRL_REG_CWRITER_RSVD_4_1_MASK                  UINT64_C(0x000000000000001e)
#define GITS_BF_CTRL_REG_CWRITER_OFFSET_SHIFT                   5
#define GITS_BF_CTRL_REG_CWRITER_OFFSET_MASK                    UINT64_C(0x00000000000fffe0)
#define GITS_BF_CTRL_REG_CWRITER_RSVD_63_20_SHIFT               20
#define GITS_BF_CTRL_REG_CWRITER_RSVD_63_20_MASK                UINT64_C(0xfffffffffff00000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CTRL_REG_CWRITER_, UINT64_C(0), UINT64_MAX,
                            (RETRY, RSVD_4_1, OFFSET, RSVD_63_20));
#define GITS_CTRL_REG_CWRITER_RW_MASK                           (UINT64_MAX & ~(GITS_BF_CTRL_REG_CWRITER_RSVD_4_1_MASK | \
                                                                                GITS_BF_CTRL_REG_CWRITER_RSVD_63_20_MASK))

/** GITS_CREADR: Command read register - RO. */
#define GITS_CTRL_REG_CREADR_OFF                                0x0090
#define GITS_BF_CTRL_REG_CREADR_STALLED_SHIFT                   0
#define GITS_BF_CTRL_REG_CREADR_STALLED_MASK                    UINT64_C(0x0000000000000001)
#define GITS_BF_CTRL_REG_CREADR_RSVD_4_1_SHIFT                  1
#define GITS_BF_CTRL_REG_CREADR_RSVD_4_1_MASK                   UINT64_C(0x000000000000001e)
#define GITS_BF_CTRL_REG_CREADR_OFFSET_SHIFT                    5
#define GITS_BF_CTRL_REG_CREADR_OFFSET_MASK                     UINT64_C(0x00000000000fffe0)
#define GITS_BF_CTRL_REG_CREADR_RSVD_63_20_SHIFT                20
#define GITS_BF_CTRL_REG_CREADR_RSVD_63_20_MASK                 UINT64_C(0xfffffffffff00000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CTRL_REG_CREADR_, UINT64_C(0), UINT64_MAX,
                            (STALLED, RSVD_4_1, OFFSET, RSVD_63_20));

/** GITS_BASER: ITS Table Descriptors - RW. */
#define GITS_CTRL_REG_BASER_OFF_FIRST                           0x0100
#define GITS_CTRL_REG_BASER_OFF_LAST                            0x0138
#define GITS_CTRL_REG_BASER_RANGE_SIZE                          (GITS_CTRL_REG_BASER_OFF_LAST + sizeof(uint64_t) - GITS_CTRL_REG_BASER_OFF_FIRST)
/** GITS_BASER: Size - Number of pages allocated to the table minus one. */
#define GITS_BF_CTRL_REG_BASER_SIZE_SHIFT                       0
#define GITS_BF_CTRL_REG_BASER_SIZE_MASK                        UINT64_C(0x00000000000000ff)
/** GITS_BASER: Page_Size - Size of the page that the table uses. */
#define GITS_BF_CTRL_REG_BASER_PAGESIZE_SHIFT                   8
#define GITS_BF_CTRL_REG_BASER_PAGESIZE_MASK                    UINT64_C(0x0000000000000300)
/** GITS_BASER: Shareability attributes of the table. */
#define GITS_BF_CTRL_REG_BASER_SHAREABILITY_SHIFT               10
#define GITS_BF_CTRL_REG_BASER_SHAREABILITY_MASK                UINT64_C(0x0000000000000c00)
/** GITS_BASER: Physical_Address - Physical address of the table. */
#define GITS_BF_CTRL_REG_BASER_PHYS_ADDR_SHIFT                  12
#define GITS_BF_CTRL_REG_BASER_PHYS_ADDR_MASK                   UINT64_C(0x0000fffffffff000)
/** GITS_BASER: Entry_Size - Size of each table entry minus one in bytes.   */
#define GITS_BF_CTRL_REG_BASER_ENTRY_SIZE_SHIFT                 48
#define GITS_BF_CTRL_REG_BASER_ENTRY_SIZE_MASK                  UINT64_C(0x001f000000000000)
/** GITS_BASER: OuterCache - Outer cacheability attributes of the table. */
#define GITS_BF_CTRL_REG_BASER_OUTER_CACHE_SHIFT                53
#define GITS_BF_CTRL_REG_BASER_OUTER_CACHE_MASK                 UINT64_C(0x00e0000000000000)
/** GITS_BASER: Type - The type of entity. */
#define GITS_BF_CTRL_REG_BASER_TYPE_SHIFT                       56
#define GITS_BF_CTRL_REG_BASER_TYPE_MASK                        UINT64_C(0x0700000000000000)
/** GITS_BASER: InnerCache - Inner cacheability attribtues of the table. */
#define GITS_BF_CTRL_REG_BASER_INNER_CACHE_SHIFT                59
#define GITS_BF_CTRL_REG_BASER_INNER_CACHE_MASK                 UINT64_C(0x3800000000000000)
/** GITS_BASER: Indirect - Whether this is a single or two-level table. */
#define GITS_BF_CTRL_REG_BASER_INDIRECT_SHIFT                   62
#define GITS_BF_CTRL_REG_BASER_INDIRECT_MASK                    UINT64_C(0x4000000000000000)
/** GITS_BASER: Valid - Whether memory has been allocated for the table. */
#define GITS_BF_CTRL_REG_BASER_VALID_SHIFT                      63
#define GITS_BF_CTRL_REG_BASER_VALID_MASK                       UINT64_C(0x8000000000000000)
/*  Sigh C macros... "PAGE_SIZE" is already defined here, just use "PAGESIZE" instead of temporarily undef, redef. */
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CTRL_REG_BASER_, UINT64_C(0), UINT64_MAX,
                            (SIZE, PAGESIZE, SHAREABILITY, PHYS_ADDR, ENTRY_SIZE, OUTER_CACHE, TYPE, INNER_CACHE, INDIRECT,
                             VALID));
/** GITS_BASER: Mask of valid read-write bits. */
#define GITS_CTRL_REG_BASER_RW_MASK                             (UINT64_MAX & ~(  GITS_BF_CTRL_REG_BASER_ENTRY_SIZE_MASK \
                                                                                | GITS_BF_CTRL_REG_BASER_TYPE_MASK))

/** GITS_BASER: Table type - Unimplemented (not a table). */
#define GITS_BASER_TYPE_UNIMPL                                  0
/** GITS_BASER: Table type - Devices. */
#define GITS_BASER_TYPE_DEVICES                                 1
/** GITS_BASER: Table type - vPE. */
#define GITS_BASER_TYPE_VPES                                    2
/** GITS_BASER: Table type - Interrupt Collections. */
#define GITS_BASER_TYPE_INTR_COLLECTION                         3

/** GITS_BASER: Page_Size: 4K. */
#define GITS_BASER_PAGE_SIZE_4K                                 0
/** GITS_BASER: Page_Size: 16K. */
#define GITS_BASER_PAGE_SIZE_16K                                1
/** GITS_BASER: Page_Size: 64K. */
#define GITS_BASER_PAGE_SIZE_64K                                2
/** GITS_BASER: Page_Size: Reserved (treated as 64K). */
#define GITS_BASER_PAGE_SIZE_RSVD                               3

/** GITS_PIDR2: ITS Peripheral ID2 register - RO. */
#define GITS_CTRL_REG_PIDR2_OFF                                 0xffe8
/** GITS_PIDR2: JEDEC - JEP code. */
#define GITS_BF_CTRL_REG_PIDR2_JEDEC_SHIFT                      0
#define GITS_BF_CTRL_REG_PIDR2_JEDEC_MASK                       UINT32_C(0x00000007)
/** GITS_PIDR2: DES_1 - JEP106 identification code (bits 6:4).  */
#define GITS_BF_CTRL_REG_PIDR2_DES_1_SHIFT                      3
#define GITS_BF_CTRL_REG_PIDR2_DES_1_MASK                       UINT32_C(0x00000008)
/** GITS_PIDR2: Architecture revision . */
#define GITS_BF_CTRL_REG_PIDR2_ARCHREV_SHIFT                    4
#define GITS_BF_CTRL_REG_PIDR2_ARCHREV_MASK                     UINT32_C(0x000000f0)
/** GITS_PIDR2: Reserved (bits 31:8). */
#define GITS_BF_CTRL_REG_PIDR2_RSVD_31_8_SHIFT                  8
#define GITS_BF_CTRL_REG_PIDR2_RSVD_31_8_MASK                   UINT32_C(0xffffff00)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CTRL_REG_PIDR2_, UINT32_C(0), UINT32_MAX,
                            (JEDEC, DES_1, ARCHREV, RSVD_31_8));

/** GITS_PIDR2: GICv1 architecture revision. */
#define GITS_CTRL_REG_PIDR2_ARCHREV_GICV1                       0x1
/** GITS_PIDR2: GICv2 architecture revision. */
#define GITS_CTRL_REG_PIDR2_ARCHREV_GICV2                       0x2
/** GITS_PIDR2: GICv3 architecture revision. */
#define GITS_CTRL_REG_PIDR2_ARCHREV_GICV3                       0x3
/** GITS_PIDR2: GICv4 architecture revision. */
#define GITS_CTRL_REG_PIDR2_ARCHREV_GICV4                       0x4

/** GITS_TRANSLATER register. */
#define GITS_TRANSLATION_REG_TRANSLATER                         0x0040

/** GITS indirect table level-1 entry (4K page size). */
#define GITS_BF_ITE_INDIRECT_LVL1_4K_RSVD_11_0_SHIFT            0
#define GITS_BF_ITE_INDIRECT_LVL1_4K_RSVD_11_0_MASK             UINT64_C(0x0000000000000fff)
#define GITS_BF_ITE_INDIRECT_LVL1_4K_PHYS_ADDR_SHIFT            12
#define GITS_BF_ITE_INDIRECT_LVL1_4K_PHYS_ADDR_MASK             UINT64_C(0x000ffffffffff000)
#define GITS_BF_ITE_INDIRECT_LVL1_4K_RSVD_62_52_SHIFT           52
#define GITS_BF_ITE_INDIRECT_LVL1_4K_RSVD_62_52_MASK            UINT64_C(0x7ff0000000000000)
#define GITS_BF_ITE_INDIRECT_LVL1_4K_VALID_SHIFT                63
#define GITS_BF_ITE_INDIRECT_LVL1_4K_VALID_MASK                 UINT64_C(0x8000000000000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_ITE_INDIRECT_LVL1_4K_, UINT64_C(0), UINT64_MAX,
                            (RSVD_11_0, PHYS_ADDR, RSVD_62_52, VALID));

/** GITS indirect table level-1 entry (16K page size). */
#define GITS_BF_ITE_INDIRECT_LVL1_16K_RSVD_14_0_SHIFT           0
#define GITS_BF_ITE_INDIRECT_LVL1_16K_RSVD_14_0_MASK            UINT64_C(0x0000000000003fff)
#define GITS_BF_ITE_INDIRECT_LVL1_16K_PHYS_ADDR_SHIFT           14
#define GITS_BF_ITE_INDIRECT_LVL1_16K_PHYS_ADDR_MASK            UINT64_C(0x000fffffffffc000)
#define GITS_BF_ITE_INDIRECT_LVL1_16K_RSVD_62_52_SHIFT          52
#define GITS_BF_ITE_INDIRECT_LVL1_16K_RSVD_62_52_MASK           UINT64_C(0x7ff0000000000000)
#define GITS_BF_ITE_INDIRECT_LVL1_16K_VALID_SHIFT               63
#define GITS_BF_ITE_INDIRECT_LVL1_16K_VALID_MASK                UINT64_C(0x8000000000000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_ITE_INDIRECT_LVL1_16K_, UINT64_C(0), UINT64_MAX,
                            (RSVD_14_0, PHYS_ADDR, RSVD_62_52, VALID));

/** GITS indirect table level-1 entry (64K page size). */
#define GITS_BF_ITE_INDIRECT_LVL1_64K_RSVD_15_0_SHIFT           0
#define GITS_BF_ITE_INDIRECT_LVL1_64K_RSVD_15_0_MASK            UINT64_C(0x000000000000ffff)
#define GITS_BF_ITE_INDIRECT_LVL1_64K_PHYS_ADDR_SHIFT           16
#define GITS_BF_ITE_INDIRECT_LVL1_64K_PHYS_ADDR_MASK            UINT64_C(0x000fffffffff0000)
#define GITS_BF_ITE_INDIRECT_LVL1_64K_RSVD_62_52_SHIFT          52
#define GITS_BF_ITE_INDIRECT_LVL1_64K_RSVD_62_52_MASK           UINT64_C(0x7ff0000000000000)
#define GITS_BF_ITE_INDIRECT_LVL1_64K_VALID_SHIFT               63
#define GITS_BF_ITE_INDIRECT_LVL1_64K_VALID_MASK                UINT64_C(0x8000000000000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_ITE_INDIRECT_LVL1_64K_, UINT64_C(0), UINT64_MAX,
                            (RSVD_15_0, PHYS_ADDR, RSVD_62_52, VALID));

/** GITS indirect table level-1 entry size in bytes. */
#define GITS_ITE_INDIRECT_LVL1_SIZE                             8

/**
 * Memory shareability attributes.
 * In accordance to the ARM GIC spec.
 */
typedef enum GITSATTRSHARE
{
    GITSATTRSHARE_NON_SHAREABLE = 0,
    GITSATTRSHARE_INNER_SHAREABLE,
    GITSATTRSHARE_OUTER_SHAREABLE,
    GITSATTRSHARE_RSVD
} GITSATTRSHARE;

/**
 * Memory cacheability attribute.
 * In accordance to the ARM GIC spec.
 */
typedef enum GITSATTRMEM
{
    GITSATTRMEM_DEFAULT = 0,
    GITSATTRMEM_NOCACHE,
    GITSATTRMEM_CACHE_RD_ALLOC_WT,
    GITSATTRMEM_CACHE_RD_ALLOC_WB,
    GITSATTRMEM_CACHE_WR_ALLOC_WT,
    GITSATTRMEM_CACHE_WR_ALLOC_WB,
    GITSATTRMEM_CACHE_RW_ALLOC_WT,
    GITSATTRMEM_CACHE_RW_ALLOC_WB
} GITSMEMATTR;

/**
 * GITS entry type.
 * In accordance to the ARM GIC spec.
 */
typedef enum GITSITSTYPE
{
    GITSITSTYPE_UNIMPLEMENTED = 0,
    GITSITSTYPE_DEVICES,
    GITSITSTYPE_VPES,
    GITSITSTYPE_INTR_COLLECTIONS
} GITSITSTYPE;

/**
 * ITS command.
 * In accordance to the ARM GIC spec.
 */
typedef union GITSCMD
{
    RTUINT64U au64[4];
    struct
    {
        uint8_t         uCmdId;
        uint8_t         auData[31];
    } common;
} GITSCMD;
/** Pointer to an ITS command. */
typedef GITSCMD *PGITSCMD;
/** Pointer to a const ITS command. */
typedef GITSCMD const *PCGITSCMD;
AssertCompileSize(GITSCMD, GITS_CMD_SIZE);

/** @name GITS command IDs.
 * @{ */
#define GITS_CMD_ID_CLEAR                                       0x04
#define GITS_CMD_ID_DISCARD                                     0x0f
#define GITS_CMD_ID_INT                                         0x03
#define GITS_CMD_ID_INV                                         0x0c
#define GITS_CMD_ID_INVALL                                      0x0d
#define GITS_CMD_ID_INVDB                                       0x2e
#define GITS_CMD_ID_MAPC                                        0x09
#define GITS_CMD_ID_MAPD                                        0x08
#define GITS_CMD_ID_MAPI                                        0x0b
#define GITS_CMD_ID_MAPTI                                       0x0a
#define GITS_CMD_ID_MOVALL                                      0x0e
#define GITS_CMD_ID_MOVI                                        0x01
#define GITS_CMD_ID_SYNC                                        0x05
#define GITS_CMD_ID_VINVALL                                     0x2d
#define GITS_CMD_ID_VMAPI                                       0x2b
#define GITS_CMD_ID_VMAPP                                       0x29
#define GITS_CMD_ID_VMAPTI                                      0x2a
#define GITS_CMD_ID_VMOVI                                       0x21
#define GITS_CMD_ID_VMOVP                                       0x22
#define GITS_CMD_ID_VSGI                                        0x23
#define GITS_CMD_ID_VSYNC                                       0x25
/** @} */

/** @name GITS command: MAPC.
 * @{ */
/** MAPC DW0: Command ID. */
#define GITS_BF_CMD_MAPC_DW0_CMD_ID_SHIFT                       0
#define GITS_BF_CMD_MAPC_DW0_CMD_ID_MASK                        UINT64_C(0x00000000000000ff)
/** MAPC DW0: Reserved (bits 63:8). */
#define GITS_BF_CMD_MAPC_DW0_RSVD_63_8_SHIFT                    8
#define GITS_BF_CMD_MAPC_DW0_RSVD_63_8_MASK                     UINT64_C(0xffffffffffffff00)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_MAPC_DW0_, UINT64_C(0), UINT64_MAX,
                            (CMD_ID, RSVD_63_8));

/** MAPC DW1: Reserved (bits 63:0). */
#define GITS_BF_CMD_MAPC_DW1_RSVD_63_0_MASK                     UINT64_MAX

/** MAPC DW2: IC ID - The interrupt collection ID. */
#define GITS_BF_CMD_MAPC_DW2_IC_ID_SHIFT                        0
#define GITS_BF_CMD_MAPC_DW2_IC_ID_MASK                         UINT64_C(0x000000000000ffff)
/** MAPC DW2: RDBase - The target redistributor base address or PE number. */
#define GITS_BF_CMD_MAPC_DW2_RDBASE_SHIFT                       16
#define GITS_BF_CMD_MAPC_DW2_RDBASE_MASK                        UINT64_C(0x0007ffffffff0000)
/** MAPC DW2: Reserved (bits 62:51). */
#define GITS_BF_CMD_MAPC_DW2_RSVD_62_51_SHIFT                   51
#define GITS_BF_CMD_MAPC_DW2_RSVD_62_51_MASK                    UINT64_C(0x7ff8000000000000)
/** MAPC DW2: Valid bit. */
#define GITS_BF_CMD_MAPC_DW2_VALID_SHIFT                        63
#define GITS_BF_CMD_MAPC_DW2_VALID_MASK                         UINT64_C(0x8000000000000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_MAPC_DW2_, UINT64_C(0), UINT64_MAX,
                            (IC_ID, RDBASE, RSVD_62_51, VALID));

/** MAPC DW3: Reserved (bits 63:0). */
#define GITS_BF_CMD_MAPC_DW3_RSVD_63_0_MASK                     UINT64_MAX
/** @} */

/** @name GITS command: MAPD.
 * @{ */
/** MAPD DW0: Command ID. */
#define GITS_BF_CMD_MAPD_DW0_CMD_ID_SHIFT                       0
#define GITS_BF_CMD_MAPD_DW0_CMD_ID_MASK                        UINT64_C(0x00000000000000ff)
/** MAPD DW0: Reserved (bits 31:8). */
#define GITS_BF_CMD_MAPD_DW0_RSVD_31_8_SHIFT                    8
#define GITS_BF_CMD_MAPD_DW0_RSVD_31_8_MASK                     UINT64_C(0x00000000ffffff00)
/** MAPD DW0: Device ID. */
#define GITS_BF_CMD_MAPD_DW0_DEV_ID_SHIFT                       32
#define GITS_BF_CMD_MAPD_DW0_DEV_ID_MASK                        UINT64_C(0xffffffff00000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_MAPD_DW0_, UINT64_C(0), UINT64_MAX,
                            (CMD_ID, RSVD_31_8, DEV_ID));

/** MAPD DW1: Size. */
#define GITS_BF_CMD_MAPD_DW1_SIZE_SHIFT                         0
#define GITS_BF_CMD_MAPD_DW1_SIZE_MASK                          UINT64_C(0x000000000000001f)
/** MAPD DW1: Reserved (bits 63:5). */
#define GITS_BF_CMD_MAPD_DW1_RSVD_63_5_SHIFT                    5
#define GITS_BF_CMD_MAPD_DW1_RSVD_63_5_MASK                     UINT64_C(0xffffffffffffffe0)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_MAPD_DW1_, UINT64_C(0), UINT64_MAX,
                            (SIZE, RSVD_63_5));

/** MAPD: DW2: Reserved (bits 7:0). */
#define GITS_BF_CMD_MAPD_DW2_RSVD_7_0_SHIFT                     0
#define GITS_BF_CMD_MAPD_DW2_RSVD_7_0_MASK                      UINT64_C(0x00000000000000ff)
/** MAPD: DW2: ITT address. */
#define GITS_BF_CMD_MAPD_DW2_ITT_ADDR_SHIFT                     8
#define GITS_BF_CMD_MAPD_DW2_ITT_ADDR_MASK                      UINT64_C(0x000fffffffffff00)
/** MAPD: DW2: Reserved (bits 62:52). */
#define GITS_BF_CMD_MAPD_DW2_RSVD_62_52_SHIFT                   52
#define GITS_BF_CMD_MAPD_DW2_RSVD_62_52_MASK                    UINT64_C(0x7ff0000000000000)
/** MAPD: DW2: Valid. */
#define GITS_BF_CMD_MAPD_DW2_VALID_SHIFT                        63
#define GITS_BF_CMD_MAPD_DW2_VALID_MASK                         UINT64_C(0x8000000000000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_MAPD_DW2_, UINT64_C(0), UINT64_MAX,
                            (RSVD_7_0, ITT_ADDR, RSVD_62_52, VALID));
/** @} */

/** @name GITS command: MAPTI.
 * @{ */
/** MAPTI DW0: Command ID. */
#define GITS_BF_CMD_MAPTI_DW0_CMD_ID_SHIFT                      0
#define GITS_BF_CMD_MAPTI_DW0_CMD_ID_MASK                       UINT64_C(0x00000000000000ff)
/** MAPTI DW0: Reserved (bits 31:8). */
#define GITS_BF_CMD_MAPTI_DW0_RSVD_31_8_SHIFT                   8
#define GITS_BF_CMD_MAPTI_DW0_RSVD_31_8_MASK                    UINT64_C(0x00000000ffffff00)
/** MAPTI DW0: Device ID. */
#define GITS_BF_CMD_MAPTI_DW0_DEV_ID_SHIFT                      32
#define GITS_BF_CMD_MAPTI_DW0_DEV_ID_MASK                       UINT64_C(0xffffffff00000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_MAPTI_DW0_, UINT64_C(0), UINT64_MAX,
                            (CMD_ID, RSVD_31_8, DEV_ID));

/** MAPTI DW1: Event ID. */
#define GITS_BF_CMD_MAPTI_DW1_EVENT_ID_SHIFT                    0
#define GITS_BF_CMD_MAPTI_DW1_EVENT_ID_MASK                     UINT64_C(0x00000000ffffffff)
/** MAPTI DW1: Physical INTID. */
#define GITS_BF_CMD_MAPTI_DW1_PHYS_INTID_SHIFT                  32
#define GITS_BF_CMD_MAPTI_DW1_PHYS_INTID_MASK                   UINT64_C(0xffffffff00000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_MAPTI_DW1_, UINT64_C(0), UINT64_MAX,
                            (EVENT_ID, PHYS_INTID));

/** MAPTI DW2: ICID. */
#define GITS_BF_CMD_MAPTI_DW2_IC_ID_SHIFT                       0
#define GITS_BF_CMD_MAPTI_DW2_IC_ID_MASK                        UINT64_C(0x000000000000ffff)
/** MAPTI DW2: Reserved (bits 63:16). */
#define GITS_BF_CMD_MAPTI_DW2_RSVD_63_16_SHIFT                  16
#define GITS_BF_CMD_MAPTI_DW2_RSVD_63_16_MASK                   UINT64_C(0xffffffffffff0000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_MAPTI_DW2_, UINT64_C(0), UINT64_MAX,
                            (IC_ID, RSVD_63_16));
/** @} */

/** @name GITS command: INV.
 * @{ */
/** INV DW0: Command ID. */
#define GITS_BF_CMD_INV_DW0_CMD_ID_SHIFT                        0
#define GITS_BF_CMD_INV_DW0_CMD_ID_MASK                         UINT64_C(0x00000000000000ff)
/** INV DW0: Reserved (bits 31:8). */
#define GITS_BF_CMD_INV_DW0_RSVD_31_8_SHIFT                     8
#define GITS_BF_CMD_INV_DW0_RSVD_31_8_MASK                      UINT64_C(0x00000000ffffff00)
/** INV DW0: Device ID. */
#define GITS_BF_CMD_INV_DW0_DEV_ID_SHIFT                        32
#define GITS_BF_CMD_INV_DW0_DEV_ID_MASK                         UINT64_C(0xffffffff00000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_INV_DW0_, UINT64_C(0), UINT64_MAX,
                            (CMD_ID, RSVD_31_8, DEV_ID));

/** INV DW1: Event ID. */
#define GITS_BF_CMD_INV_DW1_EVENT_ID_SHIFT                      0
#define GITS_BF_CMD_INV_DW1_EVENT_ID_MASK                       UINT64_C(0x00000000ffffffff)
/** INV DW1: Reserved (bits 63:32). */
#define GITS_BF_CMD_INV_DW1_RSVD_63_32_SHIFT                    32
#define GITS_BF_CMD_INV_DW1_RSVD_63_32_MASK                     UINT64_C(0xffffffff00000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_INV_DW1_, UINT64_C(0), UINT64_MAX,
                            (EVENT_ID, RSVD_63_32));
/** @} */

/** @name GITS command: INVALL.
 * @{ */
/** INVALL DW0: Command ID. */
#define GITS_BF_CMD_INVALL_DW0_CMD_ID_SHIFT                     0
#define GITS_BF_CMD_INVALL_DW0_CMD_ID_MASK                      UINT64_C(0x00000000000000ff)
/** INVALL DW0: Reserved (bits 63:8). */
#define GITS_BF_CMD_INVALL_DW0_RSVD_63_8_SHIFT                  8
#define GITS_BF_CMD_INVALL_DW0_RSVD_63_8_MASK                   UINT64_C(0xffffffffffffff00)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_INVALL_DW0_, UINT64_C(0), UINT64_MAX,
                            (CMD_ID, RSVD_63_8));

/** INVALL DW1: Reserved (bits 63:0). */
#define GITS_BF_CMD_INVALL_DW1_RSVD_63_0_MASK                   UINT64_MAX

/** INVALL DW2: IC ID - The interrupt collection ID. */
#define GITS_BF_CMD_INVALL_DW2_IC_ID_SHIFT                      0
#define GITS_BF_CMD_INVALL_DW2_IC_ID_MASK                       UINT64_C(0x000000000000ffff)
/** INVALL DW2: Reserved (bits 63:16). */
#define GITS_BF_CMD_INVALL_DW2_RSVD_63_16_SHIFT                 16
#define GITS_BF_CMD_INVALL_DW2_RSVD_63_16_MASK                  UINT64_C(0xffffffffffff0000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_INVALL_DW2_, UINT64_C(0), UINT64_MAX,
                            (IC_ID, RSVD_63_16));
/** @} */


/** @name GITS command: DISCARD.
 * @{ */
/** DISCARD DW0: Command ID. */
#define GITS_BF_CMD_DISCARD_DW0_CMD_ID_SHIFT                    0
#define GITS_BF_CMD_DISCARD_DW0_CMD_ID_MASK                     UINT64_C(0x00000000000000ff)
/** DISCARD DW0: Reserved (bits 31:8). */
#define GITS_BF_CMD_DISCARD_DW0_RSVD_31_8_SHIFT                 8
#define GITS_BF_CMD_DISCARD_DW0_RSVD_31_8_MASK                  UINT64_C(0x00000000ffffff00)
/** DISCARD DW0: Device ID. */
#define GITS_BF_CMD_DISCARD_DW0_DEV_ID_SHIFT                    32
#define GITS_BF_CMD_DISCARD_DW0_DEV_ID_MASK                     UINT64_C(0xffffffff00000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_DISCARD_DW0_, UINT64_C(0), UINT64_MAX,
                            (CMD_ID, RSVD_31_8, DEV_ID));

/** DISCARD DW1: Event ID. */
#define GITS_BF_CMD_DISCARD_DW1_EVENT_ID_SHIFT                  0
#define GITS_BF_CMD_DISCARD_DW1_EVENT_ID_MASK                   UINT64_C(0x00000000ffffffff)
/** DISCARD DW1: Reserved (bits 63:32). */
#define GITS_BF_CMD_DISCARD_DW1_RSVD_63_32_SHIFT                32
#define GITS_BF_CMD_DISCARD_DW1_RSVD_63_32_MASK                 UINT64_C(0xffffffff00000000)
RT_BF_ASSERT_COMPILE_CHECKS(GITS_BF_CMD_DISCARD_DW1_, UINT64_C(0), UINT64_MAX,
                            (EVENT_ID, RSVD_63_32));
/** @} */

#endif /* !VBOX_INCLUDED_gic_its_h */

