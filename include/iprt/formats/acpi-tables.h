/* $Id: acpi-tables.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, ACPI (Advanced Configuration and Power Interface) table format.
 *
 * Spec taken from: https://uefi.org/sites/default/files/resources/ACPI_Spec_6_5_Aug29.pdf (2024-07-25)
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_acpi_tables_h
#define IPRT_INCLUDED_formats_acpi_tables_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/cdefs.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_formats_acpi_tbl    Advanced Configuration and Power Interface (ACPI) table structures and definitions
 * @ingroup grp_rt_formats
 * @{
 */

/**
 * Generic Address Structure (GAS)
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,generic-address-structure-gas}
 */
#pragma pack(1)
typedef struct ACPIGAS
{
    uint8_t             bAddrSpaceId;                   /**< 0x00: The address space ID. */
    uint8_t             cRegBits;                       /**< 0x01: The Register bit width. */
    uint8_t             offReg;                         /**< 0x02: The register bit offset at the given address. */
    uint8_t             bAccSize;                       /**< 0x03: The access size. */
    uint64_t            u64Address;                     /**< 0x04: The 64-bit address of the data structure or register. */
} ACPIGAS;
#pragma pack()
AssertCompileSize(ACPIGAS, 12);
/** Pointer to an ACPI generic address structure. */
typedef ACPIGAS *PACPIGAS;
/** Pointer to a const ACPI generic address structure. */
typedef const ACPIGAS *PCACPIGAS;


/** @name ACPI_GAS_ADDRESS_SPACE_ID_XXX - ACPIGAS::bAddrSpaceId
 * @{ */
/** System Memory space. */
#define ACPI_GAS_ADDRESS_SPACE_ID_SYS_MEM       UINT8_C(0x00)
/** System I/O space. */
#define ACPI_GAS_ADDRESS_SPACE_ID_SYS_IO        UINT8_C(0x01)
/** PCI Configuration space. */
#define ACPI_GAS_ADDRESS_SPACE_ID_PCI_CFG       UINT8_C(0x02)
/** Embedded Controller. */
#define ACPI_GAS_ADDRESS_SPACE_ID_EMC           UINT8_C(0x03)
/** SMBus. */
#define ACPI_GAS_ADDRESS_SPACE_ID_SMBUS         UINT8_C(0x04)
/** System CMOS. */
#define ACPI_GAS_ADDRESS_SPACE_ID_SYS_CMOS      UINT8_C(0x05)
/** PCI BAR target. */
#define ACPI_GAS_ADDRESS_SPACE_ID_PCI_BAR_TGT   UINT8_C(0x06)
/** IPMI. */
#define ACPI_GAS_ADDRESS_SPACE_ID_IPMI          UINT8_C(0x07)
/** General Purpose I/O. */
#define ACPI_GAS_ADDRESS_SPACE_ID_GPIO          UINT8_C(0x08)
/** General Serial Bus. */
#define ACPI_GAS_ADDRESS_SPACE_ID_GEN_SER_BUS   UINT8_C(0x09)
/** Platform Communications Channel. */
#define ACPI_GAS_ADDRESS_SPACE_ID_PCC           UINT8_C(0x0a)
/** Platform Runtime Mechanism. */
#define ACPI_GAS_ADDRESS_SPACE_ID_PRM           UINT8_C(0x0b)
/** Functional Fixed Hardware. */
#define ACPI_GAS_ADDRESS_SPACE_ID_FFH           UINT8_C(0x7f)
/** @} */


/** @name ACPI_GAS_ACCESS_SIZE_XXX - ACPIGAS::bAccSize
 * @{ */
/** Undefined (legacy reasons). */
#define ACPI_GAS_ACCESS_SIZE_UNDEFINED          UINT8_C(0)
/** Byte access. */
#define ACPI_GAS_ACCESS_SIZE_BYTE               UINT8_C(1)
/** Word access. */
#define ACPI_GAS_ACCESS_SIZE_WORD               UINT8_C(2)
/** Dword access. */
#define ACPI_GAS_ACCESS_SIZE_DWORD              UINT8_C(3)
/** Qword access. */
#define ACPI_GAS_ACCESS_SIZE_QWORD              UINT8_C(4)
/** @} */


/**
 * Root System Description Pointer (RSDP)
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,root-system-description-pointer-rsdp}
 *
 * @note This is the format for revision 2 and later.
 */
#pragma pack(1)
typedef struct ACPIRSDP
{
    uint8_t             abSignature[8];                 /**< 0x000: The signature for identifcation ("RSD PTR "). */
    uint8_t             bChkSum;                        /**< 0x008: The checksum of the fields defined in the ACPI 1.0 specification. */
    uint8_t             abOemId[6];                     /**< 0x009: An OEM supplied string that identifies the OEM. */
    uint8_t             bRevision;                      /**< 0x00f: The revision of this structure. */
    uint32_t            u32AddrRsdt;                    /**< 0x010: The 32-bit physical address of the RSDT. */
    uint32_t            cbRsdp;                         /**< 0x014: The length of the table including the header, starting from offset 0. */
    uint64_t            u64AddrXsdt;                    /**< 0x018: The 64-bit physical address of the XSDT. */
    uint8_t             bExtChkSum;                     /**< 0x020: Checksum of the entire table, including both checksum fields. */
    uint8_t             abRsvd[3];                      /**< 0x021: Reserved fields. */
} ACPIRSDP;
#pragma pack()
AssertCompileSize(ACPIRSDP, 36);
/** Pointer to an ACPI Root System Descriptor Pointer. */
typedef ACPIRSDP *PACPIRSDP;
/** Pointer to a const ACPI Root System Descriptor Pointer. */
typedef const ACPIRSDP *PCACPIRSDP;

/** The RSDP signature. */
#define ACPI_RSDP_SIGNATURE                     "RSD PTR "


/**
 * System Description Table Header
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,system-description-table-header}
 */
typedef struct ACPITBLHDR
{
    uint32_t            u32Signature;                   /**< 0x000: The signature for identifcation. */
    uint32_t            cbTbl;                          /**< 0x004: Length of the table in bytes starting from the beginning of the header. */
    uint8_t             bRevision;                      /**< 0x008: Revision of the structure corresponding to the signature field for this table. */
    uint8_t             bChkSum;                        /**< 0x009: Checksum of the entire table including this field, must sum up to zero to be valid. */
    uint8_t             abOemId[6];                     /**< 0x00a: An OEM supplied string that identifies the OEM. */
    uint8_t             abOemTblId[8];                  /**< 0x010: An OEM supplied string to identify the particular data table. */
    uint32_t            u32OemRevision;                 /**< 0x018: An OEM supplied revision number. */
    uint8_t             abCreatorId[4];                 /**< 0x01c: Vendor ID of the utility that created the table. */
    uint32_t            u32CreatorRevision;             /**< 0x020: Revision of the utility that created the table. */
} ACPITBLHDR;
AssertCompileSize(ACPITBLHDR, 36);
/** Pointer to an ACPI System Description Tablhe Header. */
typedef ACPITBLHDR *PACPITBLHDR;
/** Pointer to a const ACPI System Description Tablhe Header. */
typedef const ACPITBLHDR *PCACPITBLHDR;


/** @def ACPI_SIGNATURE_MAKE_FROM_U8
 * Create a table signature from the supplied 4 characters.
 */
#ifdef RT_BIG_ENDIAN
# define ACPI_SIGNATURE_MAKE_FROM_U8(b0, b1, b2, b3) RT_MAKE_U32_FROM_MSB_U8(b0, b1, b2, b3)
#elif defined(RT_LITTLE_ENDIAN)
# define ACPI_SIGNATURE_MAKE_FROM_U8(b0, b1, b2, b3) RT_MAKE_U32_FROM_U8(b0, b1, b2, b3)
#else
# error "Whut?"
#endif


/** @name ACPI_TABLE_HDR_SIGNATURE_XXX - ACPITBLHDR::abSignature
 * @see  @acpi65{05_ACPI_Software_Programming_Model,description-header-signatures-for-tables-defined-by-acpi}
 * @{ */
/** Multiple APIC Description Table. */
#define ACPI_TABLE_HDR_SIGNATURE_APIC           ACPI_SIGNATURE_MAKE_FROM_U8('A', 'P', 'I', 'C')
/** Boot Error Record Table. */
#define ACPI_TABLE_HDR_SIGNATURE_BERT           ACPI_SIGNATURE_MAKE_FROM_U8('B', 'E', 'R', 'T')
/** Boot Graphics Resource Table. */
#define ACPI_TABLE_HDR_SIGNATURE_BGRT           ACPI_SIGNATURE_MAKE_FROM_U8('B', 'G', 'R', 'T')
/** Virtual Firmware Confidential Computing Event Log Table. */
#define ACPI_TABLE_HDR_SIGNATURE_CCEL           ACPI_SIGNATURE_MAKE_FROM_U8('C', 'C', 'E', 'L')
/** Corrected Platform Error Polling Table. */
#define ACPI_TABLE_HDR_SIGNATURE_CPEP           ACPI_SIGNATURE_MAKE_FROM_U8('C', 'P', 'E', 'P')
/** Differentiated System Description Table. */
#define ACPI_TABLE_HDR_SIGNATURE_DSDT           ACPI_SIGNATURE_MAKE_FROM_U8('D', 'S', 'D', 'T')
/** Embedded Controller Boot Resource Table. */
#define ACPI_TABLE_HDR_SIGNATURE_ECDT           ACPI_SIGNATURE_MAKE_FROM_U8('E', 'C', 'D', 'T')
/** Error Injection Table. */
#define ACPI_TABLE_HDR_SIGNATURE_EINJ           ACPI_SIGNATURE_MAKE_FROM_U8('E', 'I', 'N', 'J')
/** Error Record Serialization Table. */
#define ACPI_TABLE_HDR_SIGNATURE_ERST           ACPI_SIGNATURE_MAKE_FROM_U8('E', 'R', 'S', 'T')
/** Fixed ACPI Description Table. */
#define ACPI_TABLE_HDR_SIGNATURE_FACP           ACPI_SIGNATURE_MAKE_FROM_U8('F', 'A', 'C', 'P')
/** Firmware ACPI Control Structure. */
#define ACPI_TABLE_HDR_SIGNATURE_FACS           ACPI_SIGNATURE_MAKE_FROM_U8('F', 'A', 'C', 'S')
/** Firmware Performance Data Table. */
#define ACPI_TABLE_HDR_SIGNATURE_FPDT           ACPI_SIGNATURE_MAKE_FROM_U8('F', 'P', 'D', 'T')
/** Generic Timer Description Table. */
#define ACPI_TABLE_HDR_SIGNATURE_GTDT           ACPI_SIGNATURE_MAKE_FROM_U8('G', 'T', 'D', 'T')
/** Hardware Error Source Table. */
#define ACPI_TABLE_HDR_SIGNATURE_HEST           ACPI_SIGNATURE_MAKE_FROM_U8('H', 'E', 'S', 'T')
/** Miscellaneous GUIDed Table Entries. */
#define ACPI_TABLE_HDR_SIGNATURE_MISC           ACPI_SIGNATURE_MAKE_FROM_U8('M', 'I', 'S', 'C')
/** Maximum System Characteristics Table. */
#define ACPI_TABLE_HDR_SIGNATURE_MSCT           ACPI_SIGNATURE_MAKE_FROM_U8('M', 'S', 'C', 'T')
/** Memory Power State Table. */
#define ACPI_TABLE_HDR_SIGNATURE_MPST           ACPI_SIGNATURE_MAKE_FROM_U8('M', 'P', 'S', 'T')
/** NVDIMM Firmware Interface Table. */
#define ACPI_TABLE_HDR_SIGNATURE_NFIT           ACPI_SIGNATURE_MAKE_FROM_U8('N', 'F', 'I', 'T')
/** OEM Specific Information Table. */
#define ACPI_TABLE_HDR_SIGNATURE_OEM(a_b3)      ACPI_SIGNATURE_MAKE_FROM_U8('O', 'E', 'M', a_b3)
/** Platform Communications Channel Table. */
#define ACPI_TABLE_HDR_SIGNATURE_PCCT           ACPI_SIGNATURE_MAKE_FROM_U8('P', 'C', 'C', 'T')
/** Platform Health Assessment Table. */
#define ACPI_TABLE_HDR_SIGNATURE_PHAT           ACPI_SIGNATURE_MAKE_FROM_U8('P', 'H', 'A', 'T')
/** Platform Memory Topology Table. */
#define ACPI_TABLE_HDR_SIGNATURE_PMTT           ACPI_SIGNATURE_MAKE_FROM_U8('P', 'M', 'T', 'T')
/** Processor Properties Topology Table. */
#define ACPI_TABLE_HDR_SIGNATURE_PPTT           ACPI_SIGNATURE_MAKE_FROM_U8('P', 'P', 'T', 'T')
/** Persistent System Description Table. */
#define ACPI_TABLE_HDR_SIGNATURE_PSDT           ACPI_SIGNATURE_MAKE_FROM_U8('P', 'S', 'D', 'T')
/** ACPI RAS Feature Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RASF           ACPI_SIGNATURE_MAKE_FROM_U8('R', 'A', 'S', 'F')
/** ACPI RAS2 Feature Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RAS2           ACPI_SIGNATURE_MAKE_FROM_U8('R', 'A', 'S', '2')
/** Root System Description Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSDT           ACPI_SIGNATURE_MAKE_FROM_U8('R', 'S', 'D', 'T')
/** Smart Battery Specification Table. */
#define ACPI_TABLE_HDR_SIGNATURE_SBST           ACPI_SIGNATURE_MAKE_FROM_U8('S', 'B', 'S', 'T')
/** Secure DEVices Table. */
#define ACPI_TABLE_HDR_SIGNATURE_SDEV           ACPI_SIGNATURE_MAKE_FROM_U8('S', 'D', 'E', 'V')
/** System Locality Distance Information Table. */
#define ACPI_TABLE_HDR_SIGNATURE_SLIT           ACPI_SIGNATURE_MAKE_FROM_U8('S', 'L', 'I', 'T')
/** System Resource Affinity Table. */
#define ACPI_TABLE_HDR_SIGNATURE_SRAT           ACPI_SIGNATURE_MAKE_FROM_U8('S', 'R', 'A', 'T')
/** Secondary System Description Table. */
#define ACPI_TABLE_HDR_SIGNATURE_SSDT           ACPI_SIGNATURE_MAKE_FROM_U8('S', 'S', 'D', 'T')
/** Storage Volume Key Data Table. */
#define ACPI_TABLE_HDR_SIGNATURE_SVKL           ACPI_SIGNATURE_MAKE_FROM_U8('S', 'V', 'K', 'L')
/** Extended System Description Table. */
#define ACPI_TABLE_HDR_SIGNATURE_XSDT           ACPI_SIGNATURE_MAKE_FROM_U8('X', 'S', 'D', 'T')
/** @} */


/** @name ACPI_TABLE_HDR_SIGNATURE_RSVD_XXX - ACPITBLHDR::abSignature
 * @see  @acpi65{05_ACPI_Software_Programming_Model,description-header-signatures-for-tables-reserved-by-acpi}
 * @{ */
/** ARM Error Source Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_AEST      ACPI_SIGNATURE_MAKE_FROM_U8('A', 'E', 'S', 'T')
/** ARM Generic Diagnostic Dump and Reset Interface. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_AGDI      ACPI_SIGNATURE_MAKE_FROM_U8('A', 'G', 'D', 'I')
/** ARM Performance Monitoring Unit Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_APMT      ACPI_SIGNATURE_MAKE_FROM_U8('A', 'P', 'M', 'T')
/** BIOS Data ACPI Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_BDAT      ACPI_SIGNATURE_MAKE_FROM_U8('B', 'D', 'A', 'T')
/** Reserved. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_BOOT      ACPI_SIGNATURE_MAKE_FROM_U8('B', 'O', 'O', 'T')
/** CXL Early Discovery Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_CEDT      ACPI_SIGNATURE_MAKE_FROM_U8('C', 'E', 'D', 'T')
/** Core System Resource Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_CSRT      ACPI_SIGNATURE_MAKE_FROM_U8('C', 'S', 'R', 'T')
/** Debug Port Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_DBGP      ACPI_SIGNATURE_MAKE_FROM_U8('D', 'B', 'G', 'P')
/** Debug Port Table 2. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_DBG2      ACPI_SIGNATURE_MAKE_FROM_U8('D', 'B', 'G', '2')
/** DMA Remapping Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_DMAR      ACPI_SIGNATURE_MAKE_FROM_U8('D', 'M', 'A', 'R')
/** Dynamic Root of Trust for Measurement Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_DRTM      ACPI_SIGNATURE_MAKE_FROM_U8('D', 'R', 'T', 'M')
/** DMA TXT Protected Range. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_DTPR      ACPI_SIGNATURE_MAKE_FROM_U8('D', 'T', 'P', 'R')
/** Event Timer Description Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_ETDT      ACPI_SIGNATURE_MAKE_FROM_U8('E', 'T', 'D', 'T')
/** IA-PC High Precision Event Timer Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_HPET      ACPI_SIGNATURE_MAKE_FROM_U8('H', 'P', 'E', 'T')
/** iSCSI Boot Firmware Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_IBFT      ACPI_SIGNATURE_MAKE_FROM_U8('I', 'B', 'F', 'T')
/** Inline Encryption Reporting Structure. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_IERS      ACPI_SIGNATURE_MAKE_FROM_U8('I', 'E', 'R', 'S')
/** I/O Remapping Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_IORT      ACPI_SIGNATURE_MAKE_FROM_U8('I', 'O', 'R', 'T')
/** I/O Virtualization Reporting Structure. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_IVRS      ACPI_SIGNATURE_MAKE_FROM_U8('I', 'V', 'R', 'S')
/** Key Programming Interface for Root Complex Integrity and Data Encryption (IDE). */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_KEYP      ACPI_SIGNATURE_MAKE_FROM_U8('K', 'E', 'Y', 'P')
/** Low Power Idle Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_LPIT      ACPI_SIGNATURE_MAKE_FROM_U8('L', 'P', 'I', 'T')
/** PCI Express Memory Mapped Configuration. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_MCFG      ACPI_SIGNATURE_MAKE_FROM_U8('M', 'C', 'F', 'G')
/** Management Controller Host Interface Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_MCHI      ACPI_SIGNATURE_MAKE_FROM_U8('M', 'C', 'H', 'I')
/** Microsoft Pluton Security Processor Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_MHSP      ACPI_SIGNATURE_MAKE_FROM_U8('M', 'H', 'S', 'P')
/** ARM Memory Partitioning and Monitoring. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_MPAM      ACPI_SIGNATURE_MAKE_FROM_U8('M', 'P', 'A', 'M')
/** Microsoft Data Management Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_MSDM      ACPI_SIGNATURE_MAKE_FROM_U8('M', 'S', 'D', 'M')
/** NVMe-over-Fabric (NVMe-oF) Boot Firmware Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_NBFT      ACPI_SIGNATURE_MAKE_FROM_U8('N', 'B', 'F', 'T')
/** Platform Runtime Mechanism Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_PRMT      ACPI_SIGNATURE_MAKE_FROM_U8('P', 'R', 'M', 'T')
/** Regulatory Graphics Resource Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_RGRT      ACPI_SIGNATURE_MAKE_FROM_U8('R', 'G', 'R', 'T')
/** Software Delegated Exceptions Interface. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_SDEI      ACPI_SIGNATURE_MAKE_FROM_U8('S', 'D', 'E', 'I')
/** Microsoft Software Licensing Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_SLIC      ACPI_SIGNATURE_MAKE_FROM_U8('S', 'L', 'I', 'C')
/** Microsoft Serial Port Console Redirection Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_SPCR      ACPI_SIGNATURE_MAKE_FROM_U8('S', 'P', 'C', 'R')
/** Server Platform Management Interface Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_SPMI      ACPI_SIGNATURE_MAKE_FROM_U8('S', 'P', 'M', 'I')
/** _STA OVerride Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_STAO      ACPI_SIGNATURE_MAKE_FROM_U8('S', 'T', 'A', 'O')
/** Sound Wire File Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_SWFT      ACPI_SIGNATURE_MAKE_FROM_U8('S', 'W', 'F', 'T')
/** Trusted Computing Platform Alliance Capabilities Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_TCPA      ACPI_SIGNATURE_MAKE_FROM_U8('T', 'C', 'P', 'A')
/** Trusted Platform Module 2 Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_TPM2      ACPI_SIGNATURE_MAKE_FROM_U8('T', 'P', 'M', '2')
/** Unified Extensible Firmware Interface. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_UEFI      ACPI_SIGNATURE_MAKE_FROM_U8('U', 'E', 'F', 'I')
/** Windows ACPI Emulated Devics Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_WAET      ACPI_SIGNATURE_MAKE_FROM_U8('W', 'A', 'E', 'T')
/** Watchdog Action Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_WDAT      ACPI_SIGNATURE_MAKE_FROM_U8('W', 'D', 'A', 'T')
/** Watchdog Descriptor Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_WDDT      ACPI_SIGNATURE_MAKE_FROM_U8('W', 'D', 'D', 'T')
/** Watchdog Resource Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_WDRT      ACPI_SIGNATURE_MAKE_FROM_U8('W', 'D', 'R', 'T')
/** Windows Platform Binary Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_WPBT      ACPI_SIGNATURE_MAKE_FROM_U8('W', 'P', 'B', 'T')
/** Windows Security Mitigations Table. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_WSMT      ACPI_SIGNATURE_MAKE_FROM_U8('W', 'S', 'M', 'T')
/** Xen Project. */
#define ACPI_TABLE_HDR_SIGNATURE_RSVD_XENV      ACPI_SIGNATURE_MAKE_FROM_U8('X', 'E', 'N', 'V')
/** @} */


/**
 * Root System Description Table (RSDT)
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,root-system-description-table-rsdt}
 */
typedef struct ACPIRSDT
{
    ACPITBLHDR          Hdr;                            /**< 0x000: The table header. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint32_t            au32AddrTbl[RT_FLEXIBLE_ARRAY]; /**< 0x024: Array of 32-bit physical addresses pointing to other tables, variable in size. */
} ACPIRSDT;
//AssertCompileSize(ACPIRSDT, 40);
/** Pointer to an ACPI Root System Description Table. */
typedef ACPIRSDT *PACPIRSDT;
/** Pointer to a const ACPI Root System Description Table. */
typedef const ACPIRSDT *PCACPIRSDT;


/** The ACPI RSDT signature. */
#define ACPI_RSDT_REVISION                      1


/**
 * Extended System Description Table (XSDT)
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,extended-system-description-table-xsdt}
 */
#pragma pack(1)
typedef struct ACPIXSDT
{
    ACPITBLHDR          Hdr;                            /**< 0x000: The table header. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint64_t            au64AddrTbl[RT_FLEXIBLE_ARRAY]; /**< 0x024: Array of 64-bit physical addresses pointing to other tables, variable in size. */
} ACPIXSDT;
#pragma pack()
//AssertCompileSize(ACPIXSDT, 44);
/** Pointer to an ACPI Extended System Description Table. */
typedef ACPIXSDT *PACPIXSDT;
/** Pointer to a const ACPI Extended System Description Table. */
typedef const ACPIXSDT *PCACPIXSDT;


/** The ACPI XSDT signature. */
#define ACPI_XSDT_REVISION                      1


/**
 * Fixed ACPI Description Table (ACPIFADT)
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,fixed-acpi-description-table-fadt}
 */
#pragma pack(1)
typedef struct ACPIFADT
{
    ACPITBLHDR          Hdr;                            /**< 0x000: The table header. */
    uint32_t            u32AddrFwCtrl;                  /**< 0x024: Physical memory address of the FACS, where OSPM and Firmware exchange control information. */
    uint32_t            u32AddrDsdt;                    /**< 0x028: Physical memory address of the DSDT. */
    uint8_t             bRsvd0;                         /**< 0x02c: Reserved from ACPI 2.0 onwards, ACPI 1.0 has a field named INT_MODEL. */
    uint8_t             bPreferredPmProfile;            /**< 0x02d: Preferred power management profile set by OEM. */
    uint16_t            u16SciInt;                      /**< 0x02e: System vector the SCI interrupt is wired to in 8259 mode, if no 8259 is present this is the GSI. */
    uint32_t            u32PortSmiCmd;                  /**< 0x030: System port address of the SMI command port. */
    uint8_t             bAcpiEnable;                    /**< 0x034: Value to write to the SMI command port to disable SMI ownership of the ACPI hardware registers. */
    uint8_t             bAcpiDisable;                   /**< 0x035: Value to write to the SMI command port to re-enable SMI ownership of the ACPI hardware registers. */
    uint8_t             bS4BiosReq;                     /**< 0x036: Value to write to the SMI command port to enter the S4 BIOS state. */
    uint8_t             bPStateCnt;                     /**< 0x037: Value to write to the SMI command port to assume processor performance state control responsibility. */
    uint32_t            u32PortPm1aEvtBlk;              /**< 0x038: System port address of the PM1a Event Register Block. */
    uint32_t            u32PortPm1bEvtBlk;              /**< 0x03c: System port address of the PM1b Event Register Block. */
    uint32_t            u32PortPm1aCntBlk;              /**< 0x040: System port address of the PM1a Control Register Block. */
    uint32_t            u32PortPm1bCntBlk;              /**< 0x044: System port address of the PM1b Control Register Block. */
    uint32_t            u32PortPm2CntBlk;               /**< 0x048: System port address of the PM2 Control Register Block. */
    uint32_t            u32PortPmTmrBlk;                /**< 0x04c: System port address of the Power Management Timer Control Register Block. */
    uint32_t            u32PortGpe0Blk;                 /**< 0x050: System port address of the General Purpose Event 0 Register Block. */
    uint32_t            u32PortGpe1Blk;                 /**< 0x054: System port address of the General Purpose Event 1 Register Block. */
    uint8_t             cbPm1EvtDecoded;                /**< 0x058: Number of bytes decoded by PM1a_EVT_BLK and, if supported, PM1b_EVT_BLK. */
    uint8_t             cbPm1CntDecoded;                /**< 0x059: Number of bytes decoded by PM1a_CNT_BLK and, if supported, PM1b_CNT_BLK. */
    uint8_t             cbPm2CntDecoded;                /**< 0x05a: Number of bytes decoded by PM2_CNT_BL. */
    uint8_t             cbPmTmrDecoded;                 /**< 0x05b: Number of bytes decoded by PM_TMR_BLK. */
    uint8_t             cbGpe0BlkDecoded;               /**< 0x05c: Length of the register GPE0_BLK or X_GPE0_BLK. */
    uint8_t             cbGpe1BlkDecoded;               /**< 0x05d: Length of the register GPE1_BLK or X_GPE1_BLK. */
    uint8_t             bGpe1BaseOff;                   /**< 0x05e: Offset within the ACPI general-purpose event model where GPE1 based events start. */
    uint8_t             bCstCnt;                        /**< 0x05f: If non-zero, value OSPM writes to the SMI command port to indicate OS support for the _CST object and C States Changed notification. */
    uint16_t            cLvl2LatUs;                     /**< 0x060: Worst-case latency in microseconds to enter and exit C2 state. */
    uint16_t            cLvl3LatUs;                     /**< 0x062: Worst-case latency in microseconds to enter and exit C3 state. */
    uint16_t            cFlushStridesSize;              /**< 0x064: Number of flush strides needed to be read to flush dirty lines from processor's memory caches. */
    uint16_t            cbFlushStride;                  /**< 0x066: Cache line width in bytes. */
    uint8_t             bDutyOffset;                    /**< 0x068: Zero based index of where the processor's duty cycle setting is within the P_CNT register. */
    uint8_t             cBitsDutyWidth;                 /**< 0x069: The bit width of the processor's duty cacle setting value. */
    uint8_t             bCmosOffRtcDayAlarm;            /**< 0x06a: RTC CMOS RAM index to the day-of-month alarm value. */
    uint8_t             bCmosOffRtcMonAlarm;            /**< 0x06b: RTC CMOS RAM index to the month of year alarm value. */
    uint8_t             bCmosOffRtcCenturyAlarm;        /**< 0x06c: RTC CMOS RAM index to the century data value. */
    uint16_t            fIaPcBootArch;                  /**< 0x06d: IA-PC Boot Architecture Flags. */
    uint8_t             bRsvd1;                         /**< 0x06f: Reserved, must be 0. */
    uint32_t            fFeatures;                      /**< 0x070: Fixed feature flags. */
    ACPIGAS             AddrResetReg;                   /**< 0x074: Address of the reset register. */
    uint8_t             bResetVal;                      /**< 0x080: Value to write to the reset register. */
    uint16_t            fArmBootArch;                   /**< 0x081: ARM Boot Architecture Flags. */
    uint8_t             bFadtVersionMinor;              /**< 0x083: FADT minor version. */
    uint64_t            u64AddrXFwCtrl;                 /**< 0x084: 64-bit physical address of the FACS, where OSPM and Firmware exchange control information. */
    uint64_t            u64AddrXDsdt;                   /**< 0x08c: 64-bit physical memory address of the DSDT. */
    ACPIGAS             AddrXPm1aEvtBlk;                /**< 0x094: Extended address of the PM1a Event Register Block. */
    ACPIGAS             AddrXPm1bEvtBlk;                /**< 0x0a0: Extended address of the PM1b Event Register Block. */
    ACPIGAS             AddrXPm1aCntBlk;                /**< 0x0ac: Extended address of the PM1a Control Register Block. */
    ACPIGAS             AddrXPm1bCntBlk;                /**< 0x0b8: Extended address of the PM1b Control Register Block. */
    ACPIGAS             AddrXPm2bCntBlk;                /**< 0x0c4: Extended address of the PM2 Control Register Block. */
    ACPIGAS             AddrXPmTmrBlk;                  /**< 0x0d0: Extended address of the Power Management Timer Control Register Block. */
    ACPIGAS             AddrXGpe0Blk;                   /**< 0x0dc: Extended address of the General Purpose Event 0 Register Block. */
    ACPIGAS             AddrXGpe1Blk;                   /**< 0x0e8: Extended address of the General Purpose Event 1 Register Block. */
    ACPIGAS             AddrSleepCtrlReg;               /**< 0x0f4: Extended address of the Sleep Control register. */
    ACPIGAS             AddrSleepStsReg;                /**< 0x100: Extended address of the Sleep Status register. */
    uint64_t            u64HypervisorId;                /**< 0x10c: 64-bit hypervisor vendor. */
} ACPIFADT;
#pragma pack()
AssertCompileSize(ACPIFADT, 276);
/** Pointer to an ACPI Fixed ACPI Description Table. */
typedef ACPIFADT *PACPIFADT;
/** Pointer to a const ACPI Fixed ACPI Description Table. */
typedef const ACPIFADT *PCACPIFADT;


/** The ACPI FADT revision. */
#define ACPI_FADT_REVISION                      6


/** @name ACPI_FADT_PM_PROFILE_XXX - ACPIFADT::bPreferredPmProfile
 * @{ */
/** Unspecified. */
#define ACPI_FADT_PM_PROFILE_UNSPECIFIED        0
/** Desktop. */
#define ACPI_FADT_PM_PROFILE_DESKTOP            1
/** Mobile. */
#define ACPI_FADT_PM_PROFILE_MOBILE             2
/** Workstation. */
#define ACPI_FADT_PM_PROFILE_WORKSTATION        3
/** Enterprise Server. */
#define ACPI_FADT_PM_PROFILE_ENTERPRISE_SERVER  4
/** SOHO Server. */
#define ACPI_FADT_PM_PROFILE_SOHO_SERVER        5
/** Appliance PC. */
#define ACPI_FADT_PM_PROFILE_APPLIANCE_PC       6
/** Performance Server. */
#define ACPI_FADT_PM_PROFILE_PERFORMANCE_SERVER 7
/** Tablet. */
#define ACPI_FADT_PM_PROFILE_TABLET             8
/** @} */


/** @name ACPI_FADT_IAPC_BOOT_ARCH_F_XXX - ACPIFADT::fIaPcBootArch
 * @{ */
/** Bit 0 - Indicates that the motherboard supports user-visible devices on the LPC or ISA bus if set. */
#define ACPI_FADT_IAPC_BOOT_ARCH_F_LEGACY_DEVS  RT_BIT(0)
/** Bit 1 - Indicates that the motherboard contains support for a port 60 and 64 based keyboard controller. */
#define ACPI_FADT_IAPC_BOOT_ARCH_F_8042         RT_BIT(1)
/** Bit 2 - Indicates that the OSPM must not blindly probe the VGA hardware. */
#define ACPI_FADT_IAPC_BOOT_ARCH_F_NO_VGA       RT_BIT(2)
/** Bit 3 - Indicates that the OSPM must not enable Message Signaled Interrupts on this platform. */
#define ACPI_FADT_IAPC_BOOT_ARCH_F_NO_MSI       RT_BIT(3)
/** Bit 3 - Indicates that the OSPM must not enable OSPM ASPM on this platform. */
#define ACPI_FADT_IAPC_BOOT_ARCH_F_NO_PCIE_ASPM RT_BIT(4)
/** Bit 3 - Indicates that the CMOS RTC is either not implemented, or does not exist at legacy addresses. */
#define ACPI_FADT_IAPC_BOOT_ARCH_F_NO_RTC_CMOS  RT_BIT(5)
/** @} */


/** @name ACPI_FADT_F_XXX - ACPIFADT::fFeatures
 * @{ */
/** Bit 0 - Processor properly implements a function equivalent to the WBINVD IA-32 instruction. */
#define ACPI_FADT_F_WBINVD                      RT_BIT_32(0)
/** Bit 1 - If set, indicates that the hardware flushes all caches on the WBINVD instruction and maintains memory coherence. */
#define ACPI_FADT_F_WBINVD_FLUSH                RT_BIT_32(1)
/** Bit 2 - Indicates that the C1 power state is supported on all processors. */
#define ACPI_FADT_F_C1                          RT_BIT_32(2)
/** Bit 3 - Indicates that the C2 power state is configured to work on a UP or MP system. */
#define ACPI_FADT_F_P_LVL2_UP                   RT_BIT_32(3)
/** Bit 4 - Indicates that the power buttong is handled as a control method device if set. */
#define ACPI_FADT_F_POWER_BUTTON                RT_BIT_32(4)
/** Bit 5 - Indicates that the sleep buttong is handled as a control method device if set. */
#define ACPI_FADT_F_SLEEP_BUTTON                RT_BIT_32(5)
/** Bit 6 - Indicates that the RTC wake status is not supported in fixed register space if set. */
#define ACPI_FADT_F_FIX_RTC                     RT_BIT_32(6)
/** Bit 7 - Indicates whether the RTC alarm function can wake the system from the S4 state. */
#define ACPI_FADT_F_RTC_S4                      RT_BIT_32(7)
/** Bit 8 - Indicates that the TMR_VAL is implemented as a 32-bit value if one, 24-bit if 0. */
#define ACPI_FADT_F_TMR_VAL_EXT                 RT_BIT_32(8)
/** Bit 9 - Indicates that the system can support docking if set. */
#define ACPI_FADT_F_DCK_CAP                     RT_BIT_32(9)
/** Bit 10 - Indicates that the system supports system reset via ACPIFADT::AddrResetReg if set. */
#define ACPI_FADT_F_RESET_REG_SUP               RT_BIT_32(10)
/** Bit 11 - Indicates that the system has no internal expansion capabilities and the case is sealed. */
#define ACPI_FADT_F_SEALED_CASE                 RT_BIT_32(11)
/** Bit 12 - Indicates that the system cannot detect the monitor or keyboard/mouse devices. */
#define ACPI_FADT_F_HEADLESS                    RT_BIT_32(12)
/** Bit 13 - Indicates that the OSPM needs to execute a processor native instruction after writing the SLP_TYPx register. */
#define ACPI_FADT_F_CPU_SW_SLEEP                RT_BIT_32(13)
/** Bit 14 - Indicates that the platform supports PCIEXP_WAKE_STS bit in the PM1 status register and PCIEXP_WAKE_EN bit in the PM1 enable register. */
#define ACPI_FADT_F_PCI_EXP_WAK                 RT_BIT_32(14)
/** Bit 15 - Indicates that the OSPM should use a platform provided timer to drive any monotonically non-decreasing counters. */
#define ACPI_FADT_F_USE_PLATFORM_CLOCK          RT_BIT_32(15)
/** Bit 16 - Indicates that the contents of the RTC_STS flag is valid when waking the system from S4. */
#define ACPI_FADT_F_S4_RTC_STS_VALID            RT_BIT_32(16)
/** Bit 17 - Indicates that the platform is compatible with remote power-on. */
#define ACPI_FADT_F_REMOTE_POWER_ON_CAPABLE     RT_BIT_32(17)
/** Bit 18 - Indicates that all local APICs must be configured for the cluster destination model when delivering interrupts in logical mode. */
#define ACPI_FADT_F_FORCE_APIC_CLUSTER_MODEL    RT_BIT_32(18)
/** Bit 19 - Indicates that all local APICs must be configured for physical destination mode. */
#define ACPI_FADT_F_FORCE_APIC_PHYS_DEST_MDOE   RT_BIT_32(19)
/** Bit 20 - Indicates that the Hardware-Reduced ACPI is implemented. */
#define ACPI_FADT_F_HW_REDUCED_ACPI             RT_BIT_32(20)
/** Bit 21 - Indicates that the platform is able to achieve power savings in S0 similar to or better than those typically achieved in S3. */
#define ACPI_FADT_F_LOW_POWER_S0_IDLE_CAPABLE   RT_BIT_32(21)
/** Bit 22 - 23 - Describes whether the CPU caches and any other caches coherent with them, are considered by the platform to be persistent. */
#define ACPI_FADT_F_PERSISTENT_CPU_CACHES_MASK  (RT_BIT_32(22) | RT_BIT_32(23))
#define ACPI_FADT_F_PERSISTENT_CPU_CACHES_SHIFT 22
/** Not reported by the platform. */
# define ACPI_FADT_F_PERSISTENT_CPU_CACHES_NOT_REPORTED     0
/** CPU caches and any other caches coherent with them are not persistent. */
# define ACPI_FADT_F_PERSISTENT_CPU_CACHES_NOT_PERSISTENT   1
/** CPU caches and any other caches coherent with them are persistent. */
# define ACPI_FADT_F_PERSISTENT_CPU_CACHES_PERSISTENT       2
/** Reserved. */
# define ACPI_FADT_F_PERSISTENT_CPU_CACHES_RESERVED         3
/** @} */


/** @name ACPI_FADT_ARM_BOOT_ARCH_F_XXX - ACPIFADT::fArmBootArch
 * @{ */
/** Bit 0 - Indicates that PSCI is implemented if set. */
#define ACPI_FADT_ARM_BOOT_ARCH_F_PSCI_COMP    RT_BIT(0)
/** Bit 1 - Indicates that the HVC must be used as the PSCI conduit instead of SMC. */
#define ACPI_FADT_ARM_BOOT_ARCH_F_PSCI_USE_HVC RT_BIT(1)
/** @} */


/**
 * Generic Timer Description Table (ACPIGTDT)
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,generic-timer-description-table-gtdt}
 */
#pragma pack(1)
typedef struct ACPIGTDT
{
    ACPITBLHDR          Hdr;                            /**< 0x000: The table header. */
    uint64_t            u64PhysAddrCntControlBase;      /**< 0x024: The 64-bit physical address at which the Counter Control block is located, 0xffffffffffffffff if not provied. */
    uint32_t            u32Rsvd;                        /**< 0x02c: Reserved. */
    uint32_t            u32El1SecTimerGsiv;             /**< 0x030: GSIV of the secure EL1 timer, optional. */
    uint32_t            fEl1SecTimer;                   /**< 0x034: Secure EL1 timer flags, optional. Combination of ACPI_GTDT_TIMER_F_XXX. */
    uint32_t            u32El1NonSecTimerGsiv;          /**< 0x038: GSIV of the non-secure EL1 timer, optional. */
    uint32_t            fEl1NonSecTimer;                /**< 0x03c: Non-Secure EL1 timer flags, optional. Combination of ACPI_GTDT_TIMER_F_XXX. */
    uint32_t            u32El1VirtTimerGsiv;            /**< 0x040: GSIV of the virtual EL1 timer, optional. */
    uint32_t            fEl1VirtTimer;                  /**< 0x044: Virtual EL1 timer flags, optional. Combination of ACPI_GTDT_TIMER_F_XXX. */
    uint32_t            u32El2TimerGsiv;                /**< 0x048: GSIV of the EL2 timer, optional. */
    uint32_t            fEl2Timer;                      /**< 0x04c: EL2 timer flags, optional. Combination of ACPI_GTDT_TIMER_F_XXX. */
    uint64_t            u64PhysAddrCndReadBase;         /**< 0x050: The 64-bit physical address at which the Counter Read block is located, 0xffffffffffffffff if not provied. */
    uint32_t            cPlatformTimers;                /**< 0x058: Number of entries in the plaform timer structure array. */
    uint32_t            offPlatformTimers;              /**< 0x05c: Offset to the platform timer structure array from the start of this table. */
    uint32_t            u32El2VirtTimerGsiv;            /**< 0x060: GSIV of the virtual EL2 timer, optional. */
    uint32_t            fEl2VirtTimer;                  /**< 0x064: Virtual EL2 timer flags, optional. Combination of ACPI_GTDT_TIMER_F_XXX. */
} ACPIGTDT;
#pragma pack()
AssertCompileSize(ACPIGTDT, 104);
/** Pointer to an ACPI Fixed ACPI Description Table. */
typedef ACPIGTDT *PACPIGTDT;
/** Pointer to a const ACPI Fixed ACPI Description Table. */
typedef const ACPIGTDT *PCACPIGTDT;


/** @name ACPI_GTDT_TIMER_F_XXX - ACPIGTDT::fEl1SecTimer, ACPIGTDT::fEl1NonSecTimer, ACPIGTDT::fEl1VirtTimer,
 * ACPIGTDT::fEl2Timer, ACPIGTDT::fEl2VirtTimer
 * @{ */
/** Bit 0 - Interrupt mode, level triggered. */
#define ACPI_GTDT_TIMER_F_INTR_MODE_LEVEL                   0
/** Bit 0 - Interrupt mode, edge triggered. */
#define ACPI_GTDT_TIMER_F_INTR_MODE_EDGE                    RT_BIT_32(0)
/** Bit 1 - Interrupt polarity, active high. */
#define ACPI_GTDT_TIMER_F_INTR_POLARITY_ACTIVE_HIGH         0
/** Bit 1 - Interrupt polarity, active low. */
#define ACPI_GTDT_TIMER_F_INTR_POLARITY_ACTIVE_LOW          RT_BIT_32(1)
/** Bit 2 - Timer is always active independent of the processor's power state,
 * if clear the timer may lose context or not assert interrupts if the processor is in a low power state. */
#define ACPI_GTDT_TIMER_F_ALWAYS_ON_CAP                     RT_BIT_32(0)
/** @} */

/** @todo GT Block structure and Watchdog timer structure. */


/**
 * Multiple APIC Description Table (ACPIMADT)
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,multiple-apic-description-table-madt}
 */
#pragma pack(1)
typedef struct ACPIMADT
{
    ACPITBLHDR          Hdr;                            /**< 0x000: The table header. */
    uint32_t            u32PhysAddrLocalIntrCtrl;       /**< 0x024: The 32-bit physical address at which each processor can access its local interrupt controller. */
    uint32_t            fApic;                          /**< 0x028: Multiple APIC flags. Combination of ACPI_MADT_APIC_F_XXX. */
    /* Variable number of interrupt controller structures follows. */
} ACPIMADT;
#pragma pack()
AssertCompileSize(ACPIMADT, 44);
/** Pointer to an ACPI Multiple APIC Description Table. */
typedef ACPIMADT *PACPIMADT;
/** Pointer to a const ACPI Multiple APIC Description Table. */
typedef const ACPIMADT *PCACPIMADT;


/** @name ACPI_MADT_APIC_F_XXX - ACPIMADT::fApic
 * @{ */
/** Bit 0 - Indicates that the system has a PC-AT compatible dual 8259 setup. */
#define ACPI_MADT_APIC_F_PCAT_COMPAT                        RT_BIT_32(0)
/** @} */


/** @name ACPI_MADT_INTR_CTRL_TYPE_XXX - Interrupt Controller Structure Types.
 * @{ */
/** Processor Local APIC. */
#define ACPI_MADT_INTR_CTRL_TYPE_PROCESSOR_LOCAL_APIC       0
/** I/O APIC. */
#define ACPI_MADT_INTR_CTRL_TYPE_IO_APIC                    1
/** Interrupt source override. */
#define ACPI_MADT_INTR_CTRL_TYPE_INTR_SRC_OVERRIDE          2
/** Non-maskable Interrupt (NMI) Source. */
#define ACPI_MADT_INTR_CTRL_TYPE_NMI                        3
/** Local APIC NMI. */
#define ACPI_MADT_INTR_CTRL_TYPE_LOCAL_APIC_NMI             4
/** Local APIC address override. */
#define ACPI_MADT_INTR_CTRL_TYPE_LOCAL_APIC_ADDR_OVERRIDE   5
/** I/O SAPIC. */
#define ACPI_MADT_INTR_CTRL_TYPE_IO_SAPIC                   6
/** Local SAPIC. */
#define ACPI_MADT_INTR_CTRL_TYPE_LOCAL_SAPIC                7
/** Platform interrupt sources. */
#define ACPI_MADT_INTR_CTRL_TYPE_PLATFORM_INTR_SRCS         8
/** Processor Local x2APIC. */
#define ACPI_MADT_INTR_CTRL_TYPE_PROCESSOR_LOCAL_X2APIC     9
/** Local x2APIC NMI. */
#define ACPI_MADT_INTR_CTRL_TYPE_LOCAL_X2APIC_NMI          10
/** GIC CPU Interface (GICC). */
#define ACPI_MADT_INTR_CTRL_TYPE_GICC                      11
/** GIC Distributor (GICD). */
#define ACPI_MADT_INTR_CTRL_TYPE_GICD                      12
/** GIC MSI Frame. */
#define ACPI_MADT_INTR_CTRL_TYPE_GIC_MSI_FRAME             13
/** GIC Redistributor (GICR). */
#define ACPI_MADT_INTR_CTRL_TYPE_GICR                      14
/** GIC Interrupt Translation Service (ITS). */
#define ACPI_MADT_INTR_CTRL_TYPE_GIC_ITS                   15
/** @} */


/**
 * GIC CPU Interface (GICC) Structure.
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,gic-cpu-interface-gicc-structure}
 */
#pragma pack(1)
typedef struct ACPIMADTGICC
{
    uint8_t             bType;                          /**< 0x000: The GICC structure type, ACPI_MADT_INTR_CTRL_TYPE_GICC. */
    uint8_t             cbThis;                         /**< 0x001: Length of this structure, 82. */
    uint16_t            u16Rsvd0;                       /**< 0x002: Reserved, MBZ. */
    uint32_t            u32CpuId;                       /**< 0x004: GIC's CPU Interface Number. */
    uint32_t            u32AcpiCpuUid;                  /**< 0x008: The matching processor object's _UID return value for this structure. */
    uint32_t            fGicc;                          /**< 0x00c: GICC CPU interface flags, see ACPI_MADT_GICC_F_XXX. */
    uint32_t            u32ParkingProtocolVersion;      /**< 0x010: Version of the ARM-Processor Parking Protocol implemented. */
    uint32_t            u32PerformanceGsiv;             /**< 0x014: The GSIV used for performance monitoring interrupts. */
    uint64_t            u64PhysAddrParked;              /**< 0x018: The 64-bit physical address of the processor's parking protocol mailbox. */
    uint64_t            u64PhysAddrBase;                /**< 0x020: Physical address at which the CPU can access the GIC CPU interface. */
    uint64_t            u64PhysAddrGicv;                /**< 0x028: Address of the GIC virtual CPU interface registers. 0 if not present. */
    uint64_t            u64PhysAddrGich;                /**< 0x030: Address of the GIC virtual interface control block registers. 0 if not present. */
    uint32_t            u32VGicMaintenanceGsiv;         /**< 0x038: GSIV for the Virtual GIC maintenance interrupt. */
    uint64_t            u64PhysAddrGicrBase;            /**< 0x03c: On GICv3+ holds the 64-bit physical address of the associated redistributor. */
    uint64_t            u64Mpidr;                       /**< 0x044: Matches the MPIDR register of the CPU associated with this structure. */
    uint8_t             bCpuEfficiencyClass;            /**< 0x04c: Describes the relative power efficiency of the associated processor. */
    uint8_t             bRsvd1;                         /**< 0x04d: Reserved, MBZ. */
    uint16_t            u16SpeOverflowGsiv;             /**< 0x04e: Statistical Profiling Extension buffer overflow GSIV, level triggered PPI. */
    uint16_t            u16TrbeGsiv;                    /**< 0x050: Trace Buffer Extension interrupt GSIV, level triggered PPI. */
} ACPIMADTGICC;
#pragma pack()
AssertCompileSize(ACPIMADTGICC, 82);
/** Pointer to an GIC CPU Interface (GICC) Structure. */
typedef ACPIMADTGICC *PACPIMADTGICC;
/** Pointer to a const GIC CPU Interface (GICC) Structure. */
typedef const ACPIMADTGICC *PCACPIMADTGICC;


/** @name ACPI_MADT_GICC_F_XXX - ACPIMADTGICC::fGicc
 * @{ */
/** Bit 0 - If set the processor is ready for use. */
#define ACPI_MADT_GICC_F_ENABLED                            RT_BIT_32(0)
/** Bit 1 - The performance interrupt is edge triggered, if 0 level triggered. */
#define ACPI_MADT_GICC_F_PERF_INTR_MODE_EDGE                RT_BIT_32(1)
/** Bit 2 - The VGIC maintenance interrupt is edge triggered, if 0 level triggered. */
#define ACPI_MADT_GICC_F_VGIC_MAINTENANCE_INTR_MODE_EDGE    RT_BIT_32(2)
/** Bit 3 - System supports enabling this processor later during OS runtime. */
#define ACPI_MADT_GICC_F_ONLINE_CAPABLE                     RT_BIT_32(3)
/** @} */


/**
 * GIC Distributor Interface (GICD) Structure.
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,gic-distributor-gicd-structure}
 */
#pragma pack(1)
typedef struct ACPIMADTGICD
{
    uint8_t             bType;                          /**< 0x000: The GICD structure type, ACPI_MADT_INTR_CTRL_TYPE_GICD. */
    uint8_t             cbThis;                         /**< 0x001: Length of this structure, 24. */
    uint16_t            u16Rsvd0;                       /**< 0x002: Reserved, MBZ. */
    uint32_t            u32GicdId;                      /**< 0x004: This GIC distributor's hardware ID. */
    uint64_t            u64PhysAddrBase;                /**< 0x008: The 64-bit physical address for this distributor. */
    uint32_t            u32SystemVectorBase;            /**< 0x010: Reserved, MBZ. */
    uint8_t             bGicVersion;                    /**< 0x014: GIC version, ACPI_MADT_GICD_VERSION_XXX. */
    uint8_t             abRsvd0[3];                     /**< 0x015: Reserved, MBZ. */
} ACPIMADTGICD;
#pragma pack()
AssertCompileSize(ACPIMADTGICD, 24);
/** Pointer to an GIC Distributor Interface (GICD) Structure. */
typedef ACPIMADTGICD *PACPIMADTGICD;
/** Pointer to a const GIC Distributor Interface (GICD) Structure. */
typedef const ACPIMADTGICD *PCACPIMADTGICD;


/** @name ACPI_MADT_GICD_VERSION_XXX - ACPIMADTGICD::bGicVersion
 * @{ */
/** No GIC version is specified, fall back to hardware discovery for GIC version. */
#define ACPI_MADT_GICD_VERSION_UNSPECIFIED                  0
/** GICv1. */
#define ACPI_MADT_GICD_VERSION_GICv1                        1
/** GICv2. */
#define ACPI_MADT_GICD_VERSION_GICv2                        2
/** GICv3. */
#define ACPI_MADT_GICD_VERSION_GICv3                        3
/** GICv4. */
#define ACPI_MADT_GICD_VERSION_GICv4                        4
/** @} */


/** @todo GIC MSI Frame Structure. */


/**
 * GIC Redistributor (GICR) Structure.
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,gic-redistributor-gicr-structure}
 */
#pragma pack(1)
typedef struct ACPIMADTGICR
{
    uint8_t             bType;                          /**< 0x000: The GICR structure type, ACPI_MADT_INTR_CTRL_TYPE_GICR. */
    uint8_t             cbThis;                         /**< 0x001: Length of this structure, 16. */
    uint16_t            u16Rsvd0;                       /**< 0x002: Reserved, MBZ. */
    uint64_t            u64PhysAddrGicrRangeBase;       /**< 0x004: The 64-bit physical address of a page range containing all GIC Redistributors. */
    uint32_t            cbGicrRange;                    /**< 0x00c: The length of the GIC Redistributor discovery page range. */
} ACPIMADTGICR;
#pragma pack()
AssertCompileSize(ACPIMADTGICR, 16);
/** Pointer to an GIC Redistributor (GICR) Structure. */
typedef ACPIMADTGICR *PACPIMADTGICR;
/** Pointer to a const GIC Redistributor (GICR) Structure. */
typedef const ACPIMADTGICR *PCACPIMADTGICR;


/**
 * GIC Interrupt Translation Service (ITS) Structure.
 *
 * @see  @acpi65{05_ACPI_Software_Programming_Model,gic-interrupt-translation-service-its-structure}
 */
#pragma pack(1)
typedef struct ACPIMADTGICITS
{
    uint8_t             bType;                          /**< 0x000: The GICR structure type, ACPI_MADT_INTR_CTRL_TYPE_GICR. */
    uint8_t             cbThis;                         /**< 0x001: Length of this structure, 16. */
    uint16_t            u16Rsvd0;                       /**< 0x002: Reserved, MBZ. */
    uint32_t            u32GicItsId;                    /**< 0x004: This GIC ITS ID. */
    uint64_t            u64PhysAddrBase;                /**< 0x008: The 64-bit physical address for the Interrupt Translation Service. */
    uint32_t            u32Rsvd1;                       /**< 0x010: Rserved, MBZ. */
} ACPIMADTGICITS;
#pragma pack()
AssertCompileSize(ACPIMADTGICITS, 20);
/** Pointer to an GIC Interrupt Translation Service (ITS) Structure. */
typedef ACPIMADTGICITS *PACPIMADTGICITS;
/** Pointer to a const GIC Interrupt Translation Service (ITS) Structure. */
typedef const ACPIMADTGICITS *PCACPIMADTGICITS;


/**
 * Memory Mapped Configuration Space base address description table (MCFG). (part of the PCI Express spec).
 */
#pragma pack(1)
typedef struct ACPIMCFG
{
    ACPITBLHDR          Hdr;                            /**< 0x000: The table header. */
    uint64_t            u64Rsvd0;                       /**< 0x024: Reserved, MBZ. */
    /* Variable number of base address allocation structures follows. */
} ACPIMCFG;
#pragma pack()
AssertCompileSize(ACPIMCFG, 44);
/** Pointer to an ACPI MCFG Table. */
typedef ACPIMCFG *PACPIMCFG;
/** Pointer to a const ACPI MCFG Table. */
typedef const ACPIMCFG *PCACPIMCFG;


/**
 * MCFG allocation structure.
 */
#pragma pack(1)
typedef struct ACPIMCFGALLOC
{
    uint64_t            u64PhysAddrBase;                /**< 0x000: Base address of the enhanced configuration mechanism. */
    uint16_t            u16PciSegGrpNr;                 /**< 0x008: PCI segment group number. */
    uint8_t             bPciBusFirst;                   /**< 0x00a: First PCI bus number decoded by this PCI host bridge. */
    uint8_t             bPciBusLast;                    /**< 0x00b: Last PCI bus number decoded by this PCI host bridge. */
    uint32_t            u32Rsvd0;                       /**< 0x00c: Reserved, MBZ. */
} ACPIMCFGALLOC;
#pragma pack()
AssertCompileSize(ACPIMCFGALLOC, 16);
/** Pointer to an ACPI MCFG Table. */
typedef ACPIMCFGALLOC *PACPIMCFGALLOC;
/** Pointer to a const ACPI MCFG Table. */
typedef const ACPIMCFGALLOC *PCACPIMCFGALLOC;

/** @} */

#endif /* !IPRT_INCLUDED_formats_acpi_tables_h */

