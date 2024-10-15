/* $Id: DevIoApic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IO APIC - Input/Output Advanced Programmable Interrupt Controller.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_IOAPIC
#include <VBox/log.h>
#include <VBox/vmm/hm.h>
#include <VBox/msi.h>
#include <VBox/pci.h>
#include <VBox/vmm/pdmdev.h>

#include "VBoxDD.h"
#include <iprt/x86.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The current IO APIC saved state version. */
#define IOAPIC_SAVED_STATE_VERSION                  3
/** The current IO APIC saved state version. */
#define IOAPIC_SAVED_STATE_VERSION_NO_FLIPFLOP_MAP  2
/** The saved state version used by VirtualBox 5.0 and
 *  earlier.  */
#define IOAPIC_SAVED_STATE_VERSION_VBOX_50      1

/** Implementation specified by the "Intel I/O Controller Hub 9
 *  (ICH9) Family" */
#define IOAPIC_VERSION_ICH9                     0x20
/** Implementation specified by the "82093AA I/O Advanced Programmable Interrupt
Controller" */
#define IOAPIC_VERSION_82093AA                  0x11

/** The default MMIO base physical address. */
#define IOAPIC_MMIO_BASE_PHYSADDR               UINT64_C(0xfec00000)
/** The size of the MMIO range. */
#define IOAPIC_MMIO_SIZE                        X86_PAGE_4K_SIZE
/** The mask for getting direct registers from physical address. */
#define IOAPIC_MMIO_REG_MASK                    0xff

/** The number of interrupt input pins. */
#define IOAPIC_NUM_INTR_PINS                    24
/** Maximum redirection entires. */
#define IOAPIC_MAX_RTE_INDEX                    (IOAPIC_NUM_INTR_PINS - 1)
/** Reduced RTEs used by SIO.A (82379AB). */
#define IOAPIC_REDUCED_MAX_RTE_INDEX            (16 - 1)

/** Version register - Gets the version. */
#define IOAPIC_VER_GET_VER(a_Reg)               ((a_Reg) & 0xff)
/** Version register - Gets the maximum redirection entry. */
#define IOAPIC_VER_GET_MRE(a_Reg)               (((a_Reg) >> 16) & 0xff)
/** Version register - Gets whether Pin Assertion Register (PRQ) is
 *  supported. */
#define IOAPIC_VER_HAS_PRQ(a_Reg)               RT_BOOL((a_Reg) & RT_BIT_32(15))

/** Index register - Valid write mask. */
#define IOAPIC_INDEX_VALID_WRITE_MASK           UINT32_C(0xff)

/** Arbitration register - Gets the ID. */
#define IOAPIC_ARB_GET_ID(a_Reg)                ((a_Reg) >> 24 & 0xf)

/** ID register - Gets the ID. */
#define IOAPIC_ID_GET_ID(a_Reg)                 ((a_Reg) >> 24 & 0xff)

/** Redirection table entry - Vector. */
#define IOAPIC_RTE_VECTOR                       UINT64_C(0xff)
/** Redirection table entry - Delivery mode. */
#define IOAPIC_RTE_DELIVERY_MODE                (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10))
/** Redirection table entry - Destination mode. */
#define IOAPIC_RTE_DEST_MODE                    RT_BIT_64(11)
/** Redirection table entry - Delivery status. */
#define IOAPIC_RTE_DELIVERY_STATUS              RT_BIT_64(12)
/** Redirection table entry - Interrupt input pin polarity. */
#define IOAPIC_RTE_POLARITY                     RT_BIT_64(13)
/** Redirection table entry - Remote IRR. */
#define IOAPIC_RTE_REMOTE_IRR                   RT_BIT_64(14)
/** Redirection table entry - Trigger Mode. */
#define IOAPIC_RTE_TRIGGER_MODE                 RT_BIT_64(15)
/** Redirection table entry - Number of bits to shift to get the Mask. */
#define IOAPIC_RTE_MASK_BIT                     16
/** Redirection table entry - The Mask. */
#define IOAPIC_RTE_MASK                         RT_BIT_64(IOAPIC_RTE_MASK_BIT)
/** Redirection table entry - Extended Destination ID. */
#define IOAPIC_RTE_EXT_DEST_ID                  UINT64_C(0x00ff000000000000)
/** Redirection table entry - Destination. */
#define IOAPIC_RTE_DEST                         UINT64_C(0xff00000000000000)

/** Redirection table entry - Gets the destination. */
#define IOAPIC_RTE_GET_DEST(a_Reg)              ((a_Reg) >> 56 & 0xff)
/** Redirection table entry - Gets the mask flag. */
#define IOAPIC_RTE_GET_MASK(a_Reg)              (((a_Reg) >> IOAPIC_RTE_MASK_BIT) & 0x1)
/** Redirection table entry - Checks whether it's masked. */
#define IOAPIC_RTE_IS_MASKED(a_Reg)             ((a_Reg) & IOAPIC_RTE_MASK)
/** Redirection table entry - Gets the trigger mode. */
#define IOAPIC_RTE_GET_TRIGGER_MODE(a_Reg)      (((a_Reg) >> 15) & 0x1)
/** Redirection table entry - Gets the remote IRR flag. */
#define IOAPIC_RTE_GET_REMOTE_IRR(a_Reg)        (((a_Reg) >> 14) & 0x1)
/** Redirection table entry - Gets the interrupt pin polarity. */
#define IOAPIC_RTE_GET_POLARITY(a_Reg)          (((a_Reg) >> 13) & 0x1)
/** Redirection table entry - Gets the delivery status. */
#define IOAPIC_RTE_GET_DELIVERY_STATUS(a_Reg)   (((a_Reg) >> 12) & 0x1)
/** Redirection table entry - Gets the destination mode. */
#define IOAPIC_RTE_GET_DEST_MODE(a_Reg)         (((a_Reg) >> 11) & 0x1)
/** Redirection table entry - Gets the delivery mode. */
#define IOAPIC_RTE_GET_DELIVERY_MODE(a_Reg)     (((a_Reg) >> 8)  & 0x7)
/** Redirection table entry - Gets the vector. */
#define IOAPIC_RTE_GET_VECTOR(a_Reg)            ((a_Reg) & IOAPIC_RTE_VECTOR)

/** @name DMAR variant interpretation of RTE fields.
 * @{ */
/** Redirection table entry - Number of bits to shift to get Interrupt
 *  Index[14:0]. */
#define IOAPIC_RTE_INTR_INDEX_LO_BIT            49
/** Redirection table entry - Interrupt Index[14:0]. */
#define IOAPIC_RTE_INTR_INDEX_LO                UINT64_C(0xfffe000000000000)
/** Redirection table entry - Number of bits to shift to get interrupt format. */
#define IOAPIC_RTE_INTR_FORMAT_BIT              48
/** Redirection table entry - Interrupt format. */
#define IOAPIC_RTE_INTR_FORMAT                  RT_BIT_64(IOAPIC_RTE_INTR_FORMAT_BIT)
/** Redirection table entry - Number of bits to shift to get Interrupt Index[15]. */
#define IOAPIC_RTE_INTR_INDEX_HI_BIT            11
/** Redirection table entry - Interrupt Index[15]. */
#define IOAPIC_RTE_INTR_INDEX_HI                RT_BIT_64(11)

/** Redirection table entry - Gets the Interrupt Index[14:0]. */
#define IOAPIC_RTE_GET_INTR_INDEX_LO(a_Reg)     ((a_Reg) >> IOAPIC_RTE_INTR_INDEX_LO_BIT)
/** Redirection table entry - Gets the Interrupt format. */
#define IOAPIC_RTE_GET_INTR_FORMAT(a_Reg)       (((a_Reg) >> IOAPIC_RTE_INTR_FORMAT_BIT) & 0x1)
/** Redirection table entry - Gets the Interrupt Index[15]. */
#define IOAPIC_RTE_GET_INTR_INDEX_HI(a_Reg)     (((a_Reg) >> IOAPIC_RTE_INTR_INDEX_HI_BIT) & 0x1)
/** @} */

/** Redirection table entry - Valid write mask for 82093AA. */
#define IOAPIC_RTE_VALID_WRITE_MASK_82093AA     (  IOAPIC_RTE_DEST     | IOAPIC_RTE_MASK      | IOAPIC_RTE_TRIGGER_MODE \
                                                 | IOAPIC_RTE_POLARITY | IOAPIC_RTE_DEST_MODE | IOAPIC_RTE_DELIVERY_MODE \
                                                 | IOAPIC_RTE_VECTOR)
/** Redirection table entry - Valid read mask for 82093AA. */
#define IOAPIC_RTE_VALID_READ_MASK_82093AA      (  IOAPIC_RTE_DEST       | IOAPIC_RTE_MASK          | IOAPIC_RTE_TRIGGER_MODE \
                                                 | IOAPIC_RTE_REMOTE_IRR | IOAPIC_RTE_POLARITY      | IOAPIC_RTE_DELIVERY_STATUS \
                                                 | IOAPIC_RTE_DEST_MODE  | IOAPIC_RTE_DELIVERY_MODE | IOAPIC_RTE_VECTOR)

/** Redirection table entry - Valid write mask for ICH9. */
/** @note The remote IRR bit has been reverted to read-only as it turns out the
 *        ICH9 spec. is wrong, see @bugref{8386#c46}. */
#define IOAPIC_RTE_VALID_WRITE_MASK_ICH9        (  IOAPIC_RTE_DEST           | IOAPIC_RTE_MASK      | IOAPIC_RTE_TRIGGER_MODE \
                                                 /*| IOAPIC_RTE_REMOTE_IRR */| IOAPIC_RTE_POLARITY  | IOAPIC_RTE_DEST_MODE \
                                                 | IOAPIC_RTE_DELIVERY_MODE  | IOAPIC_RTE_VECTOR)
/** Redirection table entry - Valid read mask (incl. ExtDestID) for ICH9. */
#define IOAPIC_RTE_VALID_READ_MASK_ICH9         (  IOAPIC_RTE_DEST            | IOAPIC_RTE_EXT_DEST_ID | IOAPIC_RTE_MASK \
                                                 | IOAPIC_RTE_TRIGGER_MODE    | IOAPIC_RTE_REMOTE_IRR  | IOAPIC_RTE_POLARITY \
                                                 | IOAPIC_RTE_DELIVERY_STATUS | IOAPIC_RTE_DEST_MODE   | IOAPIC_RTE_DELIVERY_MODE \
                                                 | IOAPIC_RTE_VECTOR)

/** Redirection table entry - Valid write mask for DMAR variant. */
#define IOAPIC_RTE_VALID_WRITE_MASK_DMAR        (  IOAPIC_RTE_INTR_INDEX_LO  | IOAPIC_RTE_INTR_FORMAT |  IOAPIC_RTE_MASK \
                                                 | IOAPIC_RTE_TRIGGER_MODE   | IOAPIC_RTE_POLARITY    | IOAPIC_RTE_INTR_INDEX_HI \
                                                 | IOAPIC_RTE_DELIVERY_MODE  | IOAPIC_RTE_VECTOR)
/** Redirection table entry - Valid read mask for DMAR variant. */
#define IOAPIC_RTE_VALID_READ_MASK_DMAR         (  IOAPIC_RTE_INTR_INDEX_LO   | IOAPIC_RTE_INTR_FORMAT   | IOAPIC_RTE_MASK \
                                                 | IOAPIC_RTE_TRIGGER_MODE    | IOAPIC_RTE_REMOTE_IRR    | IOAPIC_RTE_POLARITY \
                                                 | IOAPIC_RTE_DELIVERY_STATUS | IOAPIC_RTE_INTR_INDEX_HI | IOAPIC_RTE_DELIVERY_MODE \
                                                 | IOAPIC_RTE_VECTOR)

/** Redirection table entry - Trigger mode edge. */
#define IOAPIC_RTE_TRIGGER_MODE_EDGE            0
/** Redirection table entry - Trigger mode level. */
#define IOAPIC_RTE_TRIGGER_MODE_LEVEL           1
/** Redirection table entry - Destination mode physical. */
#define IOAPIC_RTE_DEST_MODE_PHYSICAL           0
/** Redirection table entry - Destination mode logical. */
#define IOAPIC_RTE_DEST_MODE_LOGICAL            1


/** Index of indirect registers in the I/O APIC register table. */
#define IOAPIC_INDIRECT_INDEX_ID                0x0
#define IOAPIC_INDIRECT_INDEX_VERSION           0x1
#define IOAPIC_INDIRECT_INDEX_ARB               0x2     /* Older I/O APIC only. */
#define IOAPIC_INDIRECT_INDEX_REDIR_TBL_START   0x10    /* First valid RTE register index. */
#define IOAPIC_INDIRECT_INDEX_RTE_END           0x3F    /* Last valid RTE register index (24 RTEs). */
#define IOAPIC_REDUCED_INDIRECT_INDEX_RTE_END   0x2F    /* Last valid RTE register index (16 RTEs). */

/** Offset of direct registers in the I/O APIC MMIO space. */
#define IOAPIC_DIRECT_OFF_INDEX                 0x00
#define IOAPIC_DIRECT_OFF_DATA                  0x10
#define IOAPIC_DIRECT_OFF_EOI                   0x40    /* Newer I/O APIC only. */

/* Use PDM critsect for now for I/O APIC locking, see @bugref{8245#c121}. */
#define IOAPIC_WITH_PDM_CRITSECT
#ifdef IOAPIC_WITH_PDM_CRITSECT
# define IOAPIC_LOCK(a_pDevIns, a_pThis, a_pThisCC, rcBusy)  (a_pThisCC)->pIoApicHlp->pfnLock((a_pDevIns), (rcBusy))
# define IOAPIC_UNLOCK(a_pDevIns, a_pThis, a_pThisCC)        (a_pThisCC)->pIoApicHlp->pfnUnlock((a_pDevIns))
# define IOAPIC_LOCK_IS_OWNER(a_pDevIns, a_pThis, a_pThisCC) (a_pThisCC)->pIoApicHlp->pfnLockIsOwner((a_pDevIns))
#else
# define IOAPIC_LOCK(a_pDevIns, a_pThis, a_pThisCC, rcBusy)  PDMDevHlpCritSectEnter((a_pDevIns), &(a_pThis)->CritSect, (rcBusy))
# define IOAPIC_UNLOCK(a_pDevIns, a_pThis, a_pThisCC)        PDMDevHlpCritSectLeave((a_pDevIns), &(a_pThis)->CritSect)
# define IOAPIC_LOCK_IS_OWNER(a_pDevIns, a_pThis, a_pThisCC) PDMDevHlpCritSectIsOwner((a_pDevIns), &(a_pThis)->CritSect)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * I/O APIC chipset (and variants) we support.
 */
typedef enum IOAPICTYPE
{
    IOAPICTYPE_ICH9 = 1,
    IOAPICTYPE_DMAR,
    IOAPICTYPE_82093AA,
    IOAPICTYPE_82379AB,
    IOAPICTYPE_32BIT_HACK = 0x7fffffff
} IOAPICTYPE;
AssertCompileSize(IOAPICTYPE, 4);

/**
 * The shared I/O APIC device state.
 */
typedef struct IOAPIC
{
    /** The ID register. */
    uint8_t volatile        u8Id;
    /** The index register. */
    uint8_t volatile        u8Index;
    /** Number of CPUs. */
    uint8_t                 cCpus;
    /** I/O APIC version. */
    uint8_t                 u8ApicVer;
    /** I/O APIC ID mask. */
    uint8_t                 u8IdMask;
    /** Maximum Redirection Table Entry (RTE) Entry. */
    uint8_t                 u8MaxRte;
    /** Last valid RTE indirect register index. */
    uint8_t                 u8LastRteRegIdx;
    /* Alignment padding. */
    uint8_t                 u8Padding0[1];
    /** Redirection table entry - Valid write mask. */
    uint64_t                u64RteWriteMask;
    /** Redirection table entry - Valid read mask. */
    uint64_t                u64RteReadMask;

    /** The redirection table registers. */
    uint64_t                au64RedirTable[IOAPIC_NUM_INTR_PINS];
    /** The IRQ tags and source IDs for each pin (tracing purposes). */
    uint32_t                au32TagSrc[IOAPIC_NUM_INTR_PINS];
    /** Bitmap keeping the flip-flop-ness of pending interrupts.
     * The information held here is only relevan between SetIrq and the
     * delivery, thus no real need to initialize or reset this. */
    uint64_t                bmFlipFlop[(IOAPIC_NUM_INTR_PINS + 63) / 64];

    /** The internal IRR reflecting state of the interrupt lines. */
    uint32_t                uIrr;
    /** The I/O APIC chipset type. */
    IOAPICTYPE              enmType;
    /** The I/O APIC PCI address. */
    PCIBDF                  uPciAddress;
    /** Padding. */
    uint32_t                uPadding0;

#ifndef IOAPIC_WITH_PDM_CRITSECT
    /** The critsect for updating to the RTEs. */
    PDMCRITSECT             CritSect;
#endif

    /** The MMIO region. */
    IOMMMIOHANDLE           hMmio;

#ifdef VBOX_WITH_STATISTICS
    /** Number of MMIO reads in RZ. */
    STAMCOUNTER             StatMmioReadRZ;
    /** Number of MMIO reads in R3. */
    STAMCOUNTER             StatMmioReadR3;

    /** Number of MMIO writes in RZ. */
    STAMCOUNTER             StatMmioWriteRZ;
    /** Number of MMIO writes in R3. */
    STAMCOUNTER             StatMmioWriteR3;

    /** Number of SetIrq calls in RZ. */
    STAMCOUNTER             StatSetIrqRZ;
    /** Number of SetIrq calls in R3. */
    STAMCOUNTER             StatSetIrqR3;

    /** Number of SetEoi calls in RZ. */
    STAMCOUNTER             StatSetEoiRZ;
    /** Number of SetEoi calls in R3. */
    STAMCOUNTER             StatSetEoiR3;

    /** Number of redundant edge-triggered interrupts. */
    STAMCOUNTER             StatRedundantEdgeIntr;
    /** Number of redundant level-triggered interrupts. */
    STAMCOUNTER             StatRedundantLevelIntr;
    /** Number of suppressed level-triggered interrupts (by remote IRR). */
    STAMCOUNTER             StatSuppressedLevelIntr;
    /** Number of IOMMU remapped interrupts (signaled by RTE). */
    STAMCOUNTER             StatIommuRemappedIntr;
    /** Number of IOMMU discarded interrupts (signaled by RTE). */
    STAMCOUNTER             StatIommuDiscardedIntr;
    /** Number of IOMMU remapped MSIs. */
    STAMCOUNTER             StatIommuRemappedMsi;
    /** Number of IOMMU denied or failed MSIs. */
    STAMCOUNTER             StatIommuDiscardedMsi;
    /** Number of returns to ring-3 due to Set RTE lock contention. */
    STAMCOUNTER             StatSetRteContention;
    /** Number of level-triggered interrupts dispatched to the local APIC(s). */
    STAMCOUNTER             StatLevelIrqSent;
    /** Number of EOIs received for level-triggered interrupts from the local
     *  APIC(s). */
    STAMCOUNTER             StatEoiReceived;
    /** The time an interrupt level spent in the pending state. */
    STAMPROFILEADV          aStatLevelAct[IOAPIC_NUM_INTR_PINS];
#endif
    /** Per-vector stats. */
    STAMCOUNTER             aStatVectors[256];
} IOAPIC;
AssertCompileMemberAlignment(IOAPIC, au64RedirTable, 8);
/** Pointer to shared IOAPIC data. */
typedef IOAPIC *PIOAPIC;
/** Pointer to const shared IOAPIC data. */
typedef IOAPIC const *PCIOAPIC;


/**
 * The I/O APIC device state for ring-3.
 */
typedef struct IOAPICR3
{
    /** The IOAPIC helpers. */
    R3PTRTYPE(PCPDMIOAPICHLP)   pIoApicHlp;
} IOAPICR3;
/** Pointer to the I/O APIC device state for ring-3. */
typedef IOAPICR3 *PIOAPICR3;


/**
 * The I/O APIC device state for ring-0.
 */
typedef struct IOAPICR0
{
    /** The IOAPIC helpers. */
    R0PTRTYPE(PCPDMIOAPICHLP)   pIoApicHlp;
} IOAPICR0;
/** Pointer to the I/O APIC device state for ring-0. */
typedef IOAPICR0 *PIOAPICR0;


/**
 * The I/O APIC device state for raw-mode.
 */
typedef struct IOAPICRC
{
    /** The IOAPIC helpers. */
    RCPTRTYPE(PCPDMIOAPICHLP)   pIoApicHlp;
} IOAPICRC;
/** Pointer to the I/O APIC device state for raw-mode. */
typedef IOAPICRC *PIOAPICRC;


/** The I/O APIC device state for the current context. */
typedef CTX_SUFF(IOAPIC) IOAPICCC;
/** Pointer to the I/O APIC device state for the current context. */
typedef CTX_SUFF(PIOAPIC) PIOAPICCC;


/**
 * xAPIC interrupt.
 */
typedef struct XAPICINTR
{
    /** The interrupt vector. */
    uint8_t         u8Vector;
    /** The destination (mask or ID). */
    uint8_t         u8Dest;
    /** The destination mode. */
    uint8_t         u8DestMode;
    /** Delivery mode. */
    uint8_t         u8DeliveryMode;
    /** Trigger mode. */
    uint8_t         u8TriggerMode;
    /** Redirection hint. */
    uint8_t         u8RedirHint;
    /** Polarity. */
    uint8_t         u8Polarity;
    /** Padding. */
    uint8_t         abPadding0;
} XAPICINTR;
/** Pointer to an I/O xAPIC interrupt struct. */
typedef XAPICINTR *PXAPICINTR;
/** Pointer to a const xAPIC interrupt struct. */
typedef XAPICINTR const *PCXAPICINTR;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE

/**
 * Gets the arbitration register.
 *
 * @returns The arbitration.
 */
DECLINLINE(uint32_t) ioapicGetArb(void)
{
    Log2(("IOAPIC: ioapicGetArb: returns 0\n"));
    return 0;
}


/**
 * Gets the version register.
 *
 * @returns The version.
 */
DECLINLINE(uint32_t) ioapicGetVersion(PCIOAPIC pThis)
{
    uint32_t uValue = RT_MAKE_U32(pThis->u8ApicVer, pThis->u8MaxRte);
    Log2(("IOAPIC: ioapicGetVersion: returns %#RX32\n", uValue));
    return uValue;
}


/**
 * Sets the ID register.
 *
 * @param   pThis       The shared I/O APIC device state.
 * @param   uValue      The value to set.
 */
DECLINLINE(void) ioapicSetId(PIOAPIC pThis, uint32_t uValue)
{
    Log2(("IOAPIC: ioapicSetId: uValue=%#RX32\n", uValue));
    ASMAtomicWriteU8(&pThis->u8Id, (uValue >> 24) & pThis->u8IdMask);
}


/**
 * Gets the ID register.
 *
 * @returns The ID.
 * @param   pThis       The shared I/O APIC device state.
 */
DECLINLINE(uint32_t) ioapicGetId(PCIOAPIC pThis)
{
    uint32_t uValue = (uint32_t)pThis->u8Id << 24;
    Log2(("IOAPIC: ioapicGetId: returns %#RX32\n", uValue));
    return uValue;
}


/**
 * Sets the index register.
 *
 * @param pThis     The shared I/O APIC device state.
 * @param uValue    The value to set.
 */
DECLINLINE(void) ioapicSetIndex(PIOAPIC pThis, uint32_t uValue)
{
    LogFlow(("IOAPIC: ioapicSetIndex: uValue=%#RX32\n", uValue));
    ASMAtomicWriteU8(&pThis->u8Index, uValue & IOAPIC_INDEX_VALID_WRITE_MASK);
}


/**
 * Gets the index register.
 *
 * @returns The index value.
 */
DECLINLINE(uint32_t) ioapicGetIndex(PCIOAPIC pThis)
{
    uint32_t const uValue = pThis->u8Index;
    LogFlow(("IOAPIC: ioapicGetIndex: returns %#x\n", uValue));
    return uValue;
}


/**
 * Converts an MSI message to an APIC interrupt.
 *
 * @param   pMsi    The MSI message to convert.
 * @param   pIntr   Where to store the APIC interrupt.
 */
DECLINLINE(void) ioapicGetApicIntrFromMsi(PCMSIMSG pMsi, PXAPICINTR pIntr)
{
    /*
     * Parse the message from the physical address and data.
     * Do -not- zero out other fields in the APIC interrupt.
     *
     * See Intel spec. 10.11.1 "Message Address Register Format".
     * See Intel spec. 10.11.2 "Message Data Register Format".
     */
    pIntr->u8Dest         = pMsi->Addr.n.u8DestId;
    pIntr->u8DestMode     = pMsi->Addr.n.u1DestMode;
    pIntr->u8RedirHint    = pMsi->Addr.n.u1RedirHint;

    pIntr->u8Vector       = pMsi->Data.n.u8Vector;
    pIntr->u8TriggerMode  = pMsi->Data.n.u1TriggerMode;
    pIntr->u8DeliveryMode = pMsi->Data.n.u3DeliveryMode;
}


#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
/**
 * Convert an RTE into an MSI message.
 *
 * @param   u64Rte      The RTE to convert.
 * @param   enmType     The I/O APIC chipset type.
 * @param   pMsi        Where to store the MSI message.
 */
DECLINLINE(void) ioapicGetMsiFromRte(uint64_t u64Rte, IOAPICTYPE enmType, PMSIMSG pMsi)
{
    bool const fRemappable = IOAPIC_RTE_GET_INTR_FORMAT(u64Rte);
    if (!fRemappable)
    {
        pMsi->Addr.n.u12Addr        = VBOX_MSI_ADDR_BASE >> VBOX_MSI_ADDR_SHIFT;
        pMsi->Addr.n.u8DestId       = IOAPIC_RTE_GET_DEST(u64Rte);
        pMsi->Addr.n.u1RedirHint    = 0;
        pMsi->Addr.n.u1DestMode     = IOAPIC_RTE_GET_DEST_MODE(u64Rte);

        pMsi->Data.n.u8Vector       = IOAPIC_RTE_GET_VECTOR(u64Rte);
        pMsi->Data.n.u3DeliveryMode = IOAPIC_RTE_GET_DELIVERY_MODE(u64Rte);
        pMsi->Data.n.u1TriggerMode  = IOAPIC_RTE_GET_TRIGGER_MODE(u64Rte);
        /* pMsi->Data.n.u1Level     = ??? */
        /** @todo r=ramshankar: Level triggered MSIs don't make much sense though
         *        possible in theory? Maybe document this more explicitly... */
    }
    else
    {
        Assert(enmType == IOAPICTYPE_DMAR);
        NOREF(enmType);

        /*
         * The spec. mentions that SHV will be 0 when delivery mode is 0 (fixed), but
         * not what SHV will be if delivery mode is not 0. I ASSUME copying delivery
         * mode into SHV here is what hardware actually does.
         *
         * See Intel VT-d spec. 5.1.5.1 "I/OxAPIC Programming".
         */
        pMsi->Addr.dmar_remap.u12Addr        = VBOX_MSI_ADDR_BASE >> VBOX_MSI_ADDR_SHIFT;
        pMsi->Addr.dmar_remap.u14IntrIndexLo = IOAPIC_RTE_GET_INTR_INDEX_LO(u64Rte);
        pMsi->Addr.dmar_remap.fIntrFormat    = 1;
        pMsi->Addr.dmar_remap.fShv           = IOAPIC_RTE_GET_DELIVERY_MODE(u64Rte);
        pMsi->Addr.dmar_remap.u1IntrIndexHi  = IOAPIC_RTE_GET_INTR_INDEX_HI(u64Rte);

        pMsi->Data.dmar_remap.u16SubHandle   = 0;
    }
}
#endif


/**
 * Signals the next pending interrupt for the specified Redirection Table Entry
 * (RTE).
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared I/O APIC device state.
 * @param   pThisCC     The I/O APIC device state for the current context.
 * @param   idxRte      The index of the RTE (validated).
 *
 * @remarks It is the responsibility of the caller to verify that an interrupt is
 *          pending for the pin corresponding to the RTE before calling this
 *          function.
 */
static void ioapicSignalIntrForRte(PPDMDEVINS pDevIns, PIOAPIC pThis, PIOAPICCC pThisCC, uint8_t idxRte)
{
    Assert(IOAPIC_LOCK_IS_OWNER(pDevIns, pThis, pThisCC));

    /*
     * Ensure the interrupt isn't masked.
     */
    uint64_t const u64Rte = pThis->au64RedirTable[idxRte];
    if (!IOAPIC_RTE_IS_MASKED(u64Rte))
    { /* likely */ }
    else
        return;

    /* We cannot accept another level-triggered interrupt until remote IRR has been cleared. */
    uint8_t const u8TriggerMode = IOAPIC_RTE_GET_TRIGGER_MODE(u64Rte);
    if (u8TriggerMode == IOAPIC_RTE_TRIGGER_MODE_LEVEL)
    {
        uint8_t const u8RemoteIrr = IOAPIC_RTE_GET_REMOTE_IRR(u64Rte);
        if (u8RemoteIrr)
        {
            STAM_COUNTER_INC(&pThis->StatSuppressedLevelIntr);
            return;
        }
    }

    XAPICINTR ApicIntr;
    RT_ZERO(ApicIntr);
    ApicIntr.u8Vector       = IOAPIC_RTE_GET_VECTOR(u64Rte);
    ApicIntr.u8Dest         = IOAPIC_RTE_GET_DEST(u64Rte);
    ApicIntr.u8DestMode     = IOAPIC_RTE_GET_DEST_MODE(u64Rte);
    ApicIntr.u8DeliveryMode = IOAPIC_RTE_GET_DELIVERY_MODE(u64Rte);
    ApicIntr.u8Polarity     = IOAPIC_RTE_GET_POLARITY(u64Rte);
    ApicIntr.u8TriggerMode  = u8TriggerMode;
    //ApicIntr.u8RedirHint    = 0;

    /** @todo We might be able to release the IOAPIC(PDM) lock here and re-acquire it
     *        before setting the remote IRR bit below. The APIC and IOMMU should not
     *        require the caller to hold the PDM lock. */

#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    /*
     * The interrupt may need to be remapped (or discarded) if an IOMMU is present.
     * For line-based interrupts we must use the southbridge I/O APIC's BDF as
     * the origin of the interrupt, see @bugref{9654#c74}.
     */
    MSIMSG MsiIn;
    RT_ZERO(MsiIn);
    ioapicGetMsiFromRte(u64Rte, pThis->enmType, &MsiIn);

    MSIMSG MsiOut;
    int const rcRemap = pThisCC->pIoApicHlp->pfnIommuMsiRemap(pDevIns, pThis->uPciAddress, &MsiIn, &MsiOut);
    if (   rcRemap == VERR_IOMMU_NOT_PRESENT
        || rcRemap == VERR_IOMMU_CANNOT_CALL_SELF)
    { /* likely - assuming majority of VMs don't have IOMMU configured. */ }
    else if (RT_SUCCESS(rcRemap))
    {
        /* Update the APIC interrupt with the remapped data. */
        ioapicGetApicIntrFromMsi(&MsiOut, &ApicIntr);

        /* Ensure polarity hasn't changed (trigger mode might change with Intel IOMMUs). */
        Assert(ApicIntr.u8Polarity == IOAPIC_RTE_GET_POLARITY(u64Rte));
        STAM_COUNTER_INC(&pThis->StatIommuRemappedIntr);
    }
    else
    {
        STAM_COUNTER_INC(&pThis->StatIommuDiscardedIntr);
        return;
    }
#endif

    uint32_t const u32TagSrc = pThis->au32TagSrc[idxRte];
    Log2(("IOAPIC: Signaling %s-triggered interrupt. Dest=%#x DestMode=%s Vector=%#x (%u)\n",
          ApicIntr.u8TriggerMode == IOAPIC_RTE_TRIGGER_MODE_EDGE ? "edge" : "level", ApicIntr.u8Dest,
          ApicIntr.u8DestMode == IOAPIC_RTE_DEST_MODE_PHYSICAL ? "physical" : "logical",
          ApicIntr.u8Vector, ApicIntr.u8Vector));

    /*
     * Deliver to the local APIC via the system/3-wire-APIC bus.
     */
    int rc = pThisCC->pIoApicHlp->pfnApicBusDeliver(pDevIns,
                                                    ApicIntr.u8Dest,
                                                    ApicIntr.u8DestMode,
                                                    ApicIntr.u8DeliveryMode,
                                                    ApicIntr.u8Vector,
                                                    ApicIntr.u8Polarity,
                                                    ApicIntr.u8TriggerMode,
                                                    u32TagSrc);
    /* Can't reschedule to R3. */
    Assert(rc == VINF_SUCCESS || rc == VERR_APIC_INTR_DISCARDED);
#ifdef DEBUG_ramshankar
    if (rc == VERR_APIC_INTR_DISCARDED)
        AssertMsgFailed(("APIC: Interrupt discarded u8Vector=%#x (%u) u64Rte=%#RX64\n", u8Vector, u8Vector, u64Rte));
#endif

    if (rc == VINF_SUCCESS)
    {
        /*
         * For level-triggered interrupts, we set the remote IRR bit to indicate
         * the local APIC has accepted the interrupt.
         *
         * For edge-triggered interrupts, we should not clear the IRR bit as it
         * should remain intact to reflect the state of the interrupt line.
         * The device will explicitly transition to inactive state via the
         * ioapicSetIrq() callback.
         */
        if (u8TriggerMode == IOAPIC_RTE_TRIGGER_MODE_LEVEL)
        {
            Assert(u8TriggerMode == IOAPIC_RTE_TRIGGER_MODE_LEVEL);
            pThis->au64RedirTable[idxRte] |= IOAPIC_RTE_REMOTE_IRR;
            STAM_COUNTER_INC(&pThis->StatLevelIrqSent);
            STAM_PROFILE_ADV_START(&pThis->aStatLevelAct[idxRte], a);
        }
        /*
         * Edge-triggered flip-flops gets cleaned up here as the device code will
         * not do any explicit ioapicSetIrq and we won't receive any EOI either.
         */
        else if (ASMBitTest(pThis->bmFlipFlop, idxRte))
        {
            Log2(("IOAPIC: Clearing IRR for edge flip-flop %#x uTagSrc=%#x\n", idxRte, pThis->au32TagSrc[idxRte]));
            pThis->au32TagSrc[idxRte] = 0;
            pThis->uIrr &= ~RT_BIT_32(idxRte);
        }
    }
}


/**
 * Gets the redirection table entry.
 *
 * @returns The redirection table entry.
 * @param   pThis       The shared I/O APIC device state.
 * @param   uIndex      The index value.
 */
DECLINLINE(uint32_t) ioapicGetRedirTableEntry(PCIOAPIC pThis, uint32_t uIndex)
{
    uint8_t const idxRte = (uIndex - IOAPIC_INDIRECT_INDEX_REDIR_TBL_START) >> 1;
    AssertMsgReturn(idxRte < RT_ELEMENTS(pThis->au64RedirTable),
                    ("Invalid index %u, expected < %u\n", idxRte, RT_ELEMENTS(pThis->au64RedirTable)),
                    UINT32_MAX);
    uint32_t uValue;
    if (!(uIndex & 1))
        uValue = RT_LO_U32(pThis->au64RedirTable[idxRte]) & RT_LO_U32(pThis->u64RteReadMask);
    else
        uValue = RT_HI_U32(pThis->au64RedirTable[idxRte]) & RT_HI_U32(pThis->u64RteReadMask);

    LogFlow(("IOAPIC: ioapicGetRedirTableEntry: uIndex=%#RX32 idxRte=%u returns %#RX32\n", uIndex, idxRte, uValue));
    return uValue;
}


/**
 * Sets the redirection table entry.
 *
 * @returns Strict VBox status code (VINF_IOM_R3_MMIO_WRITE / VINF_SUCCESS).
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared I/O APIC device state.
 * @param   pThisCC     The I/O APIC device state for the current context.
 * @param   uIndex      The index value.
 * @param   uValue      The value to set.
 */
static VBOXSTRICTRC ioapicSetRedirTableEntry(PPDMDEVINS pDevIns, PIOAPIC pThis, PIOAPICCC pThisCC,
                                             uint32_t uIndex, uint32_t uValue)
{
    uint8_t const idxRte = (uIndex - IOAPIC_INDIRECT_INDEX_REDIR_TBL_START) >> 1;
    AssertMsgReturn(idxRte < RT_ELEMENTS(pThis->au64RedirTable),
                    ("Invalid index %u, expected < %u\n", idxRte, RT_ELEMENTS(pThis->au64RedirTable)),
                    VINF_SUCCESS);

    VBOXSTRICTRC rc = IOAPIC_LOCK(pDevIns, pThis, pThisCC, VINF_IOM_R3_MMIO_WRITE);
    if (rc == VINF_SUCCESS)
    {
        /*
         * Write the low or high 32-bit value into the specified 64-bit RTE register,
         * update only the valid, writable bits.
         *
         * We need to preserve the read-only bits as it can have dire consequences
         * otherwise, see @bugref{8386#c24}.
         */
        uint64_t const u64Rte = pThis->au64RedirTable[idxRte];
        if (!(uIndex & 1))
        {
            uint32_t const u32RtePreserveLo = RT_LO_U32(u64Rte) & ~RT_LO_U32(pThis->u64RteWriteMask);
            uint32_t const u32RteNewLo      = (uValue & RT_LO_U32(pThis->u64RteWriteMask)) | u32RtePreserveLo;
            uint64_t const u64RteHi         = u64Rte & UINT64_C(0xffffffff00000000);
            pThis->au64RedirTable[idxRte]   = u64RteHi | u32RteNewLo;
        }
        else
        {
            uint32_t const u32RtePreserveHi = RT_HI_U32(u64Rte) & ~RT_HI_U32(pThis->u64RteWriteMask);
            uint32_t const u32RteLo         = RT_LO_U32(u64Rte);
            uint64_t const u64RteNewHi      = ((uint64_t)((uValue & RT_HI_U32(pThis->u64RteWriteMask)) | u32RtePreserveHi) << 32);
            pThis->au64RedirTable[idxRte]   = u64RteNewHi | u32RteLo;
        }

        LogFlow(("IOAPIC: ioapicSetRedirTableEntry: uIndex=%#RX32 idxRte=%u uValue=%#RX32\n", uIndex, idxRte, uValue));

        /*
         * Signal the next pending interrupt for this RTE.
         */
        uint32_t const uPinMask = UINT32_C(1) << idxRte;
        if (pThis->uIrr & uPinMask)
        {
            LogFlow(("IOAPIC: ioapicSetRedirTableEntry: Signalling pending interrupt. idxRte=%u\n", idxRte));
            ioapicSignalIntrForRte(pDevIns, pThis, pThisCC, idxRte);
        }

        IOAPIC_UNLOCK(pDevIns, pThis, pThisCC);
    }
    else
        STAM_COUNTER_INC(&pThis->StatSetRteContention);

    return rc;
}


/**
 * Gets the data register.
 *
 * @returns The data value.
 * @param pThis     The shared I/O APIC device state.
 */
static uint32_t ioapicGetData(PCIOAPIC pThis)
{
    uint8_t const uIndex = pThis->u8Index;
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
    if (   uIndex >= IOAPIC_INDIRECT_INDEX_REDIR_TBL_START
        && uIndex <= pThis->u8LastRteRegIdx)
        return ioapicGetRedirTableEntry(pThis, uIndex);

    uint32_t uValue;
    switch (uIndex)
    {
        case IOAPIC_INDIRECT_INDEX_ID:
            uValue = ioapicGetId(pThis);
            break;

        case IOAPIC_INDIRECT_INDEX_VERSION:
            uValue = ioapicGetVersion(pThis);
            break;

        case IOAPIC_INDIRECT_INDEX_ARB:
            if (pThis->u8ApicVer == IOAPIC_VERSION_82093AA)
            {
                uValue = ioapicGetArb();
                break;
            }
            RT_FALL_THRU();

        default:
            uValue = UINT32_C(0xffffffff);
            Log2(("IOAPIC: Attempt to read register at invalid index %#x\n", uIndex));
            break;
    }
    return uValue;
}


/**
 * Sets the data register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared I/O APIC device state.
 * @param   pThisCC     The I/O APIC device state for the current context.
 * @param   uValue      The value to set.
 */
static VBOXSTRICTRC ioapicSetData(PPDMDEVINS pDevIns, PIOAPIC pThis, PIOAPICCC pThisCC, uint32_t uValue)
{
    uint8_t const uIndex = pThis->u8Index;
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
    LogFlow(("IOAPIC: ioapicSetData: uIndex=%#x uValue=%#RX32\n", uIndex, uValue));

    if (   uIndex >= IOAPIC_INDIRECT_INDEX_REDIR_TBL_START
        && uIndex <= pThis->u8LastRteRegIdx)
        return ioapicSetRedirTableEntry(pDevIns, pThis, pThisCC, uIndex, uValue);

    if (uIndex == IOAPIC_INDIRECT_INDEX_ID)
        ioapicSetId(pThis, uValue);
    else
        Log2(("IOAPIC: ioapicSetData: Invalid index %#RX32, ignoring write request with uValue=%#RX32\n", uIndex, uValue));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIOAPICREG,pfnSetEoi}
 */
static DECLCALLBACK(void) ioapicSetEoi(PPDMDEVINS pDevIns, uint8_t u8Vector)
{
    PIOAPIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    PIOAPICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOAPICCC);

    LogFlow(("IOAPIC: ioapicSetEoi: u8Vector=%#x (%u)\n", u8Vector, u8Vector));
    STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatSetEoi));

    bool fRemoteIrrCleared = false;
    int rc = IOAPIC_LOCK(pDevIns, pThis, pThisCC, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, NULL, rc);

    for (uint8_t idxRte = 0; idxRte < RT_ELEMENTS(pThis->au64RedirTable); idxRte++)
    {
        uint64_t const u64Rte = pThis->au64RedirTable[idxRte];
/** @todo r=bird: bugref:10073: I've changed it to ignore edge triggered
 * entries here since the APIC will only call us for those?  Not doing so
 * confuses ended up with spurious HPET/RTC IRQs in SMP linux because of it
 * sharing the vector with a level-triggered IRQ (like vboxguest) delivered on a
 * different CPU.
 *
 * Maybe we should also/instead filter on the source APIC number? */
        if (   IOAPIC_RTE_GET_VECTOR(u64Rte) == u8Vector
            && IOAPIC_RTE_GET_TRIGGER_MODE(u64Rte) != IOAPIC_RTE_TRIGGER_MODE_EDGE)
        {
#ifdef DEBUG_ramshankar
            /* This assertion may trigger when restoring saved-states created using the old, incorrect I/O APIC code. */
            Assert(IOAPIC_RTE_GET_REMOTE_IRR(u64Rte));
#endif
            pThis->au64RedirTable[idxRte] &= ~IOAPIC_RTE_REMOTE_IRR;
            fRemoteIrrCleared = true;
            STAM_PROFILE_ADV_STOP(&pThis->aStatLevelAct[idxRte], a);
            STAM_COUNTER_INC(&pThis->StatEoiReceived);
            Log2(("IOAPIC: ioapicSetEoi: Cleared remote IRR, idxRte=%u vector=%#x (%u)\n", idxRte, u8Vector, u8Vector));

            /*
             * Signal the next pending interrupt for this RTE.
             */
            uint32_t const uPinMask = UINT32_C(1) << idxRte;
            if (pThis->uIrr & uPinMask)
                ioapicSignalIntrForRte(pDevIns, pThis, pThisCC, idxRte);
        }
    }

    IOAPIC_UNLOCK(pDevIns, pThis, pThisCC);

#ifndef VBOX_WITH_IOMMU_AMD
    AssertMsg(fRemoteIrrCleared, ("Failed to clear remote IRR for vector %#x (%u)\n", u8Vector, u8Vector));
#endif
}


/**
 * @interface_method_impl{PDMIOAPICREG,pfnSetIrq}
 */
static DECLCALLBACK(void) ioapicSetIrq(PPDMDEVINS pDevIns, PCIBDF uBusDevFn, int iIrq, int iLevel, uint32_t uTagSrc)
{
    RT_NOREF(uBusDevFn);    /** @todo r=ramshankar: Remove this argument if it's also unnecessary with Intel IOMMU. */
#define IOAPIC_ASSERT_IRQ(a_uBusDevFn, a_idxRte, a_PinMask, a_fForceTag) do { \
        pThis->au32TagSrc[(a_idxRte)] = (a_fForceTag) || !pThis->au32TagSrc[(a_idxRte)] ? uTagSrc : RT_BIT_32(31); \
        pThis->uIrr |= a_PinMask; \
        ioapicSignalIntrForRte(pDevIns, pThis, pThisCC, (a_idxRte)); \
    } while (0)

    PIOAPIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    PIOAPICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOAPICCC);
    LogFlow(("IOAPIC: ioapicSetIrq: iIrq=%d iLevel=%d uTagSrc=%#x\n", iIrq, iLevel, uTagSrc));

    STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatSetIrq));

    if (RT_LIKELY((unsigned)iIrq < RT_ELEMENTS(pThis->au64RedirTable)))
    {
        int rc = IOAPIC_LOCK(pDevIns, pThis, pThisCC, VINF_SUCCESS);
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, NULL, rc);

        uint8_t  const idxRte        = iIrq;
        uint32_t const uPinMask      = UINT32_C(1) << idxRte;
        uint32_t const u32RteLo      = RT_LO_U32(pThis->au64RedirTable[idxRte]);
        uint8_t  const u8TriggerMode = IOAPIC_RTE_GET_TRIGGER_MODE(u32RteLo);

        bool fActive = RT_BOOL(iLevel & 1);
        /** @todo Polarity is busted elsewhere, we need to fix that
         *        first. See @bugref{8386#c7}. */
#if 0
        uint8_t const u8Polarity = IOAPIC_RTE_GET_POLARITY(u32RteLo);
        fActive ^= u8Polarity; */
#endif
        if (!fActive)
        {
            pThis->uIrr &= ~uPinMask;
            pThis->au32TagSrc[idxRte] = 0;
            IOAPIC_UNLOCK(pDevIns, pThis, pThisCC);
            return;
        }

        bool const fFlipFlop = ((iLevel & PDM_IRQ_LEVEL_FLIP_FLOP) == PDM_IRQ_LEVEL_FLIP_FLOP);
        if (!fFlipFlop)
        {
            ASMBitClear(pThis->bmFlipFlop, idxRte);

            uint32_t const uPrevIrr = pThis->uIrr & uPinMask;
            if (u8TriggerMode == IOAPIC_RTE_TRIGGER_MODE_EDGE)
            {
                /*
                 * For edge-triggered interrupts, we need to act only on a low to high edge transition.
                 * See ICH9 spec. 13.5.7 "REDIR_TBL: Redirection Table (LPC I/F-D31:F0)".
                 */
                if (!uPrevIrr)
                    IOAPIC_ASSERT_IRQ(uBusDevFn, idxRte, uPinMask, false);
                else
                {
                    STAM_COUNTER_INC(&pThis->StatRedundantEdgeIntr);
                    Log2(("IOAPIC: Redundant edge-triggered interrupt %#x (%u)\n", idxRte, idxRte));
                }
            }
            else
            {
                Assert(u8TriggerMode == IOAPIC_RTE_TRIGGER_MODE_LEVEL);

                /*
                 * For level-triggered interrupts, redundant interrupts are not a problem
                 * and will eventually be delivered anyway after an EOI, but our PDM devices
                 * should not typically call us with no change to the level.
                 */
                if (!uPrevIrr)
                { /* likely */ }
                else
                {
                    STAM_COUNTER_INC(&pThis->StatRedundantLevelIntr);
                    Log2(("IOAPIC: Redundant level-triggered interrupt %#x (%u)\n", idxRte, idxRte));
                }

                IOAPIC_ASSERT_IRQ(uBusDevFn, idxRte, uPinMask, false);
            }
        }
        else
        {
            /*
             * The device is flip-flopping the interrupt line, which implies we should de-assert
             * and assert the interrupt line. The interrupt line is left in the asserted state
             * after a flip-flop request. The de-assert is a NOP wrts to signaling an interrupt
             * hence just the assert is done.
             *
             * Update @bugref{10073}: We now de-assert the interrupt line once it has been
             * delivered to the APIC to prevent it from getting re-delivered by accident (e.g.
             * on RTE write or by buggy EOI code).  The XT-PIC works differently because of the
             * INTA, so it's set IRQ function will do what's described above: first lower the
             * interrupt line and then immediately raising it again, leaving the IRR flag set
             * most of the time.  (How a real HPET/IOAPIC does this is a really good question
             * and would be observable if we could get at the IRR register of the IOAPIC...
             * Maybe by modifying the RTE? Our code will retrigger the interrupt that way.) */
            ASMBitSet(pThis->bmFlipFlop, idxRte);
            IOAPIC_ASSERT_IRQ(uBusDevFn, idxRte, uPinMask, true);
        }

        IOAPIC_UNLOCK(pDevIns, pThis, pThisCC);
    }
#undef IOAPIC_ASSERT_IRQ
}


/**
 * @interface_method_impl{PDMIOAPICREG,pfnSendMsi}
 */
static DECLCALLBACK(void) ioapicSendMsi(PPDMDEVINS pDevIns, PCIBDF uBusDevFn, PCMSIMSG pMsi, uint32_t uTagSrc)
{
    PIOAPICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOAPICCC);
    PIOAPIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    LogFlow(("IOAPIC: ioapicSendMsi: uBusDevFn=%#x Addr=%#RX64 Data=%#RX32 uTagSrc=%#x\n",
             uBusDevFn, pMsi->Addr.u64, pMsi->Data.u32, uTagSrc));

    XAPICINTR ApicIntr;
    RT_ZERO(ApicIntr);

#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    /*
     * The MSI may need to be remapped (or discarded) if an IOMMU is present.
     *
     * If the Bus:Dev:Fn isn't valid, it is ASSUMED the device generating the
     * MSI is the IOMMU itself and hence isn't subjected to remapping. This
     * is the case with Intel IOMMUs.
     *
     * AMD IOMMUs are full fledged PCI devices, hence the BDF will be a
     * valid PCI slot, but interrupts generated by the IOMMU will be handled
     * by VERR_IOMMU_CANNOT_CALL_SELF case.
     */
    MSIMSG MsiOut;
    if (PCIBDF_IS_VALID(uBusDevFn))
    {
        int const rcRemap = pThisCC->pIoApicHlp->pfnIommuMsiRemap(pDevIns, uBusDevFn, pMsi, &MsiOut);
        if (   rcRemap == VERR_IOMMU_NOT_PRESENT
            || rcRemap == VERR_IOMMU_CANNOT_CALL_SELF)
        { /* likely - assuming majority of VMs don't have IOMMU configured. */ }
        else if (RT_SUCCESS(rcRemap))
        {
            STAM_COUNTER_INC(&pThis->StatIommuRemappedMsi);
            pMsi = &MsiOut;
        }
        else
        {
            STAM_COUNTER_INC(&pThis->StatIommuDiscardedMsi);
            return;
        }
    }
#else
    NOREF(uBusDevFn);
#endif

    ioapicGetApicIntrFromMsi(pMsi, &ApicIntr);

    /*
     * Deliver to the local APIC via the system/3-wire-APIC bus.
     */
    STAM_REL_COUNTER_INC(&pThis->aStatVectors[ApicIntr.u8Vector]);

    int rc = pThisCC->pIoApicHlp->pfnApicBusDeliver(pDevIns,
                                                    ApicIntr.u8Dest,
                                                    ApicIntr.u8DestMode,
                                                    ApicIntr.u8DeliveryMode,
                                                    ApicIntr.u8Vector,
                                                    0 /* u8Polarity - N/A */,
                                                    ApicIntr.u8TriggerMode,
                                                    uTagSrc);
    /* Can't reschedule to R3. */
    Assert(rc == VINF_SUCCESS || rc == VERR_APIC_INTR_DISCARDED); NOREF(rc);
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) ioapicMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PIOAPIC pThis = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatMmioRead));
    Assert(cb == 4); RT_NOREF_PV(cb); /* registered for dwords only */
    RT_NOREF_PV(pvUser);

    VBOXSTRICTRC rc      = VINF_SUCCESS;
    uint32_t    *puValue = (uint32_t *)pv;
    uint32_t     offReg  = off & IOAPIC_MMIO_REG_MASK;
    switch (offReg)
    {
        case IOAPIC_DIRECT_OFF_INDEX:
            *puValue = ioapicGetIndex(pThis);
            break;

        case IOAPIC_DIRECT_OFF_DATA:
            *puValue = ioapicGetData(pThis);
            break;

        default:
            Log2(("IOAPIC: ioapicMmioRead: Invalid offset. off=%#RGp offReg=%#x\n", off, offReg));
            rc = VINF_IOM_MMIO_UNUSED_FF;
            break;
    }

    LogFlow(("IOAPIC: ioapicMmioRead: offReg=%#x, returns %#RX32\n", offReg, *puValue));
    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) ioapicMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PIOAPIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    PIOAPICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOAPICCC);
    RT_NOREF_PV(pvUser);

    STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatMmioWrite));

    Assert(!(off & 3));
    Assert(cb == 4); RT_NOREF_PV(cb); /* registered for dwords only */

    VBOXSTRICTRC   rc     = VINF_SUCCESS;
    uint32_t const uValue = *(uint32_t const *)pv;
    uint32_t const offReg = off & IOAPIC_MMIO_REG_MASK;

    LogFlow(("IOAPIC: ioapicMmioWrite: pThis=%p off=%#RGp cb=%u uValue=%#RX32\n", pThis, off, cb, uValue));
    switch (offReg)
    {
        case IOAPIC_DIRECT_OFF_INDEX:
            ioapicSetIndex(pThis, uValue);
            break;

        case IOAPIC_DIRECT_OFF_DATA:
            rc = ioapicSetData(pDevIns, pThis, pThisCC, uValue);
            break;

        case IOAPIC_DIRECT_OFF_EOI:
            if (pThis->u8ApicVer == IOAPIC_VERSION_ICH9)
                ioapicSetEoi(pDevIns, uValue);
            else
                Log(("IOAPIC: ioapicMmioWrite: Write to EOI register ignored!\n"));
            break;

        default:
            Log2(("IOAPIC: ioapicMmioWrite: Invalid offset. off=%#RGp offReg=%#x\n", off, offReg));
            break;
    }

    return rc;
}


#ifdef IN_RING3

/** @interface_method_impl{DBGFREGDESC,pfnGet} */
static DECLCALLBACK(int) ioapicR3DbgReg_GetIndex(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    RT_NOREF(pDesc);
    pValue->u32 = ioapicGetIndex(PDMDEVINS_2_DATA((PPDMDEVINS)pvUser, PCIOAPIC));
    return VINF_SUCCESS;
}


/** @interface_method_impl{DBGFREGDESC,pfnSet} */
static DECLCALLBACK(int) ioapicR3DbgReg_SetIndex(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    RT_NOREF(pDesc, pfMask);
    ioapicSetIndex(PDMDEVINS_2_DATA((PPDMDEVINS)pvUser, PIOAPIC), pValue->u8);
    return VINF_SUCCESS;
}


/** @interface_method_impl{DBGFREGDESC,pfnGet} */
static DECLCALLBACK(int) ioapicR3DbgReg_GetData(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    RT_NOREF(pDesc);
    pValue->u32 = ioapicGetData((PDMDEVINS_2_DATA((PPDMDEVINS)pvUser, PCIOAPIC)));
    return VINF_SUCCESS;
}


/** @interface_method_impl{DBGFREGDESC,pfnSet} */
static DECLCALLBACK(int) ioapicR3DbgReg_SetData(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    PPDMDEVINS pDevIns = (PPDMDEVINS)pvUser;
    PIOAPIC    pThis   = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    PIOAPICCC  pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOAPICCC);
    RT_NOREF(pDesc, pfMask);
    return VBOXSTRICTRC_VAL(ioapicSetData(pDevIns, pThis, pThisCC, pValue->u32));
}


/** @interface_method_impl{DBGFREGDESC,pfnGet} */
static DECLCALLBACK(int) ioapicR3DbgReg_GetVersion(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PCIOAPIC pThis = PDMDEVINS_2_DATA((PPDMDEVINS)pvUser, PCIOAPIC);
    RT_NOREF(pDesc);
    pValue->u32 = ioapicGetVersion(pThis);
    return VINF_SUCCESS;
}


/** @interface_method_impl{DBGFREGDESC,pfnGet} */
static DECLCALLBACK(int) ioapicR3DbgReg_GetArb(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    RT_NOREF(pvUser, pDesc);
    pValue->u32 = ioapicGetArb();
    return VINF_SUCCESS;
}


/** @interface_method_impl{DBGFREGDESC,pfnGet} */
static DECLCALLBACK(int) ioapicR3DbgReg_GetRte(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PCIOAPIC pThis = PDMDEVINS_2_DATA((PPDMDEVINS)pvUser, PCIOAPIC);
    Assert(pDesc->offRegister < RT_ELEMENTS(pThis->au64RedirTable));
    pValue->u64 = pThis->au64RedirTable[pDesc->offRegister];
    return VINF_SUCCESS;
}


/** @interface_method_impl{DBGFREGDESC,pfnSet} */
static DECLCALLBACK(int) ioapicR3DbgReg_SetRte(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    RT_NOREF(pfMask);
    PIOAPIC pThis = PDMDEVINS_2_DATA((PPDMDEVINS)pvUser, PIOAPIC);
    /* No locks, no checks, just do it. */
    Assert(pDesc->offRegister < RT_ELEMENTS(pThis->au64RedirTable));
    pThis->au64RedirTable[pDesc->offRegister] = pValue->u64;
    return VINF_SUCCESS;
}


/** IOREDTBLn sub fields. */
static DBGFREGSUBFIELD const g_aRteSubs[] =
{
    { "vector",       0,   8,  0,  0, NULL, NULL },
    { "dlvr_mode",    8,   3,  0,  0, NULL, NULL },
    { "dest_mode",    11,  1,  0,  0, NULL, NULL },
    { "dlvr_status",  12,  1,  0,  DBGFREGSUBFIELD_FLAGS_READ_ONLY, NULL, NULL },
    { "polarity",     13,  1,  0,  0, NULL, NULL },
    { "remote_irr",   14,  1,  0,  DBGFREGSUBFIELD_FLAGS_READ_ONLY, NULL, NULL },
    { "trigger_mode", 15,  1,  0,  0, NULL, NULL },
    { "mask",         16,  1,  0,  0, NULL, NULL },
    { "ext_dest_id",  48,  8,  0,  DBGFREGSUBFIELD_FLAGS_READ_ONLY, NULL, NULL },
    { "dest",         56,  8,  0,  0, NULL, NULL },
    DBGFREGSUBFIELD_TERMINATOR()
};


/** Register descriptors for DBGF. */
static DBGFREGDESC const g_aRegDesc[] =
{
    { "index",      DBGFREG_END, DBGFREGVALTYPE_U8,  0,  0, ioapicR3DbgReg_GetIndex, ioapicR3DbgReg_SetIndex,    NULL, NULL },
    { "data",       DBGFREG_END, DBGFREGVALTYPE_U32, 0,  0, ioapicR3DbgReg_GetData,  ioapicR3DbgReg_SetData,     NULL, NULL },
    { "version",    DBGFREG_END, DBGFREGVALTYPE_U32, DBGFREG_FLAGS_READ_ONLY, 0, ioapicR3DbgReg_GetVersion, NULL, NULL, NULL },
    { "arb",        DBGFREG_END, DBGFREGVALTYPE_U32, DBGFREG_FLAGS_READ_ONLY, 0, ioapicR3DbgReg_GetArb,     NULL, NULL, NULL },
    { "rte0",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  0, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte1",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  1, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte2",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  2, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte3",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  3, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte4",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  4, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte5",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  5, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte6",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  6, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte7",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  7, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte8",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  8, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte9",       DBGFREG_END, DBGFREGVALTYPE_U64, 0,  9, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte10",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 10, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte11",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 11, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte12",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 12, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte13",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 13, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte14",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 14, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte15",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 15, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte16",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 16, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte17",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 17, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte18",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 18, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte19",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 19, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte20",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 20, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte21",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 21, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte22",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 22, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    { "rte23",      DBGFREG_END, DBGFREGVALTYPE_U64, 0, 23, ioapicR3DbgReg_GetRte, ioapicR3DbgReg_SetRte, NULL, &g_aRteSubs[0] },
    DBGFREGDESC_TERMINATOR()
};


/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) ioapicR3DbgInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PCIOAPIC pThis = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    LogFlow(("IOAPIC: ioapicR3DbgInfo: pThis=%p pszArgs=%s\n", pThis, pszArgs));

    bool const fLegacy = RTStrCmp(pszArgs, "legacy") == 0;

    static const char * const s_apszDeliveryModes[] =
    {
        " fixed",
        "lowpri",
        "   smi",
        "  rsvd",
        "   nmi",
        "  init",
        "  rsvd",
        "extint"
    };
    static const char * const s_apszDestMode[]       = { "phys", "log " };
    static const char * const s_apszTrigMode[]       = { " edge", "level" };
    static const char * const s_apszPolarity[]       = { "acthi", "actlo" };
    static const char * const s_apszDeliveryStatus[] = { "idle", "pend" };

    pHlp->pfnPrintf(pHlp, "I/O APIC at %#010x:\n", IOAPIC_MMIO_BASE_PHYSADDR);

    uint32_t const uId = ioapicGetId(pThis);
    pHlp->pfnPrintf(pHlp, "  ID                      = %#RX32\n", uId);
    pHlp->pfnPrintf(pHlp, "    ID                      = %#x\n",     IOAPIC_ID_GET_ID(uId));

    uint32_t const uVer = ioapicGetVersion(pThis);
    pHlp->pfnPrintf(pHlp, "  Version                 = %#RX32\n",  uVer);
    pHlp->pfnPrintf(pHlp, "    Version                 = %#x\n",     IOAPIC_VER_GET_VER(uVer));
    pHlp->pfnPrintf(pHlp, "    Pin Assert Reg. Support = %RTbool\n", IOAPIC_VER_HAS_PRQ(uVer));
    pHlp->pfnPrintf(pHlp, "    Max. Redirection Entry  = %u\n",      IOAPIC_VER_GET_MRE(uVer));

    if (pThis->u8ApicVer == IOAPIC_VERSION_82093AA)
    {
        uint32_t const uArb = ioapicGetArb();
        pHlp->pfnPrintf(pHlp, "  Arbitration             = %#RX32\n", uArb);
        pHlp->pfnPrintf(pHlp, "    Arbitration ID          = %#x\n",     IOAPIC_ARB_GET_ID(uArb));
    }

    pHlp->pfnPrintf(pHlp, "  Current index           = %#x\n",     ioapicGetIndex(pThis));

    pHlp->pfnPrintf(pHlp, "  I/O Redirection Table and IRR:\n");
    if (   pThis->enmType != IOAPICTYPE_DMAR
        || fLegacy)
    {
        pHlp->pfnPrintf(pHlp, "  idx dst_mode dst_addr mask irr trigger rirr polar dlvr_st dlvr_mode vector rte\n");
        pHlp->pfnPrintf(pHlp, "  ---------------------------------------------------------------------------------------------\n");

        uint8_t const idxMaxRte = RT_MIN(pThis->u8MaxRte, RT_ELEMENTS(pThis->au64RedirTable) - 1);
        for (uint8_t idxRte = 0; idxRte <= idxMaxRte; idxRte++)
        {
            const uint64_t u64Rte = pThis->au64RedirTable[idxRte];
            const char    *pszDestMode       = s_apszDestMode[IOAPIC_RTE_GET_DEST_MODE(u64Rte)];
            const uint8_t  uDest             = IOAPIC_RTE_GET_DEST(u64Rte);
            const uint8_t  uMask             = IOAPIC_RTE_GET_MASK(u64Rte);
            const char    *pszTriggerMode    = s_apszTrigMode[IOAPIC_RTE_GET_TRIGGER_MODE(u64Rte)];
            const uint8_t  uRemoteIrr        = IOAPIC_RTE_GET_REMOTE_IRR(u64Rte);
            const char    *pszPolarity       = s_apszPolarity[IOAPIC_RTE_GET_POLARITY(u64Rte)];
            const char    *pszDeliveryStatus = s_apszDeliveryStatus[IOAPIC_RTE_GET_DELIVERY_STATUS(u64Rte)];
            const uint8_t  uDeliveryMode     = IOAPIC_RTE_GET_DELIVERY_MODE(u64Rte);
            Assert(uDeliveryMode < RT_ELEMENTS(s_apszDeliveryModes));
            const char    *pszDeliveryMode   = s_apszDeliveryModes[uDeliveryMode];
            const uint8_t  uVector           = IOAPIC_RTE_GET_VECTOR(u64Rte);

            pHlp->pfnPrintf(pHlp, "   %02d     %s       %02x    %u   %u   %s    %u %s    %s    %s    %3u (%016llx)\n",
                            idxRte,
                            pszDestMode,
                            uDest,
                            uMask,
                            (pThis->uIrr >> idxRte) & 1,
                            pszTriggerMode,
                            uRemoteIrr,
                            pszPolarity,
                            pszDeliveryStatus,
                            pszDeliveryMode,
                            uVector,
                            u64Rte);
        }
    }
    else
    {
        pHlp->pfnPrintf(pHlp, "  idx intr_idx fmt mask irr trigger rirr polar dlvr_st dlvr_mode vector rte\n");
        pHlp->pfnPrintf(pHlp, "  ----------------------------------------------------------------------------------------\n");

        uint8_t const idxMaxRte = RT_MIN(pThis->u8MaxRte, RT_ELEMENTS(pThis->au64RedirTable) - 1);
        for (uint8_t idxRte = 0; idxRte <= idxMaxRte; idxRte++)
        {
            const uint64_t u64Rte = pThis->au64RedirTable[idxRte];
            const uint16_t idxIntrLo         = IOAPIC_RTE_GET_INTR_INDEX_LO(u64Rte);
            const uint8_t  fIntrFormat       = IOAPIC_RTE_GET_INTR_FORMAT(u64Rte);
            const uint8_t  uMask             = IOAPIC_RTE_GET_MASK(u64Rte);
            const char    *pszTriggerMode    = s_apszTrigMode[IOAPIC_RTE_GET_TRIGGER_MODE(u64Rte)];
            const uint8_t  uRemoteIrr        = IOAPIC_RTE_GET_REMOTE_IRR(u64Rte);
            const char    *pszPolarity       = s_apszPolarity[IOAPIC_RTE_GET_POLARITY(u64Rte)];
            const char    *pszDeliveryStatus = s_apszDeliveryStatus[IOAPIC_RTE_GET_DELIVERY_STATUS(u64Rte)];
            const uint8_t  uDeliveryMode     = IOAPIC_RTE_GET_DELIVERY_MODE(u64Rte);
            Assert(uDeliveryMode < RT_ELEMENTS(s_apszDeliveryModes));
            const char    *pszDeliveryMode   = s_apszDeliveryModes[uDeliveryMode];
            const uint16_t idxIntrHi         = IOAPIC_RTE_GET_INTR_INDEX_HI(u64Rte);
            const uint8_t  uVector           = IOAPIC_RTE_GET_VECTOR(u64Rte);
            const uint16_t idxIntr           = idxIntrLo | (idxIntrHi << 15);
            pHlp->pfnPrintf(pHlp, "   %02d     %4u   %u    %u   %u   %s    %u %s    %s    %s    %3u (%016llx)\n",
                            idxRte,
                            idxIntr,
                            fIntrFormat,
                            uMask,
                            (pThis->uIrr >> idxRte) & 1,
                            pszTriggerMode,
                            uRemoteIrr,
                            pszPolarity,
                            pszDeliveryStatus,
                            pszDeliveryMode,
                            uVector,
                            u64Rte);
        }
    }
}


/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
static DECLCALLBACK(int) ioapicR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PCIOAPIC        pThis = PDMDEVINS_2_DATA(pDevIns, PCIOAPIC);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    LogFlow(("IOAPIC: ioapicR3SaveExec\n"));

    pHlp->pfnSSMPutU32(pSSM, pThis->uIrr);
    pHlp->pfnSSMPutU8(pSSM,  pThis->u8Id);
    pHlp->pfnSSMPutU8(pSSM,  pThis->u8Index);
    for (uint8_t idxRte = 0; idxRte < RT_ELEMENTS(pThis->au64RedirTable); idxRte++)
        pHlp->pfnSSMPutU64(pSSM, pThis->au64RedirTable[idxRte]);

    for (uint8_t idx = 0; idx < RT_ELEMENTS(pThis->bmFlipFlop); idx++)
        pHlp->pfnSSMPutU64(pSSM, pThis->bmFlipFlop[idx]);

    return VINF_SUCCESS;
}


/**
 * @copydoc FNSSMDEVLOADEXEC
 */
static DECLCALLBACK(int) ioapicR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PIOAPIC         pThis = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    LogFlow(("APIC: apicR3LoadExec: uVersion=%u uPass=%#x\n", uVersion, uPass));

    Assert(uPass == SSM_PASS_FINAL);
    NOREF(uPass);

    /* Weed out invalid versions. */
    if (   uVersion != IOAPIC_SAVED_STATE_VERSION
        && uVersion != IOAPIC_SAVED_STATE_VERSION_NO_FLIPFLOP_MAP
        && uVersion != IOAPIC_SAVED_STATE_VERSION_VBOX_50)
    {
        LogRel(("IOAPIC: ioapicR3LoadExec: Invalid/unrecognized saved-state version %u (%#x)\n", uVersion, uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    if (uVersion >= IOAPIC_SAVED_STATE_VERSION_NO_FLIPFLOP_MAP)
        pHlp->pfnSSMGetU32(pSSM, &pThis->uIrr);

    pHlp->pfnSSMGetU8V(pSSM, &pThis->u8Id);
    pHlp->pfnSSMGetU8V(pSSM, &pThis->u8Index);
    for (uint8_t idxRte = 0; idxRte < RT_ELEMENTS(pThis->au64RedirTable); idxRte++)
        pHlp->pfnSSMGetU64(pSSM, &pThis->au64RedirTable[idxRte]);

    if (uVersion > IOAPIC_SAVED_STATE_VERSION_NO_FLIPFLOP_MAP)
        for (uint8_t idx = 0; idx < RT_ELEMENTS(pThis->bmFlipFlop); idx++)
            pHlp->pfnSSMGetU64(pSSM, &pThis->bmFlipFlop[idx]);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) ioapicR3Reset(PPDMDEVINS pDevIns)
{
    PIOAPIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    PIOAPICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOAPICCC);
    LogFlow(("IOAPIC: ioapicR3Reset: pThis=%p\n", pThis));

    /* There might be devices threads calling ioapicSetIrq() in parallel, hence the lock. */
    IOAPIC_LOCK(pDevIns, pThis, pThisCC, VERR_IGNORED);

    pThis->uIrr    = 0;
    pThis->u8Index = 0;
    pThis->u8Id    = 0;

    for (uint8_t idxRte = 0; idxRte < RT_ELEMENTS(pThis->au64RedirTable); idxRte++)
    {
        pThis->au64RedirTable[idxRte] = IOAPIC_RTE_MASK;
        pThis->au32TagSrc[idxRte] = 0;
    }

    IOAPIC_UNLOCK(pDevIns, pThis, pThisCC);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnRelocate}
 */
static DECLCALLBACK(void) ioapicR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    PIOAPICRC pThisRC = PDMINS_2_DATA_RC(pDevIns, PIOAPICRC);
    LogFlow(("IOAPIC: ioapicR3Relocate: pThis=%p offDelta=%RGi\n", PDMDEVINS_2_DATA(pDevIns, PIOAPIC), offDelta));

    pThisRC->pIoApicHlp += offDelta;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) ioapicR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PIOAPIC pThis = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    LogFlow(("IOAPIC: ioapicR3Destruct: pThis=%p\n", pThis));

# ifndef IOAPIC_WITH_PDM_CRITSECT
    /*
     * Destroy the RTE critical section.
     */
    if (PDMDevHlpCritSectIsInitialized(pDevIns, &pThis->CritSect))
        PDMDevHlpCritSectDelete(pDevIns, &pThis->CritSect);
# else
    RT_NOREF_PV(pThis);
# endif

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) ioapicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PIOAPIC         pThis   = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    PIOAPICCC       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOAPICCC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    LogFlow(("IOAPIC: ioapicR3Construct: pThis=%p iInstance=%d\n", pThis, iInstance));
    Assert(iInstance == 0); RT_NOREF(iInstance);

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "NumCPUs|ChipType|PCIAddress", "");

    /* The number of CPUs is currently unused, but left in CFGM and saved-state in case an ID of 0
       upsets some guest which we haven't yet been tested. */
    uint32_t cCpus;
    int rc = pHlp->pfnCFGMQueryU32Def(pCfg, "NumCPUs", &cCpus, 1);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query integer value \"NumCPUs\""));
    pThis->cCpus = (uint8_t)cCpus;

    char szChipType[16];
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "ChipType", &szChipType[0], sizeof(szChipType), "ICH9");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query string value \"ChipType\""));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "PCIAddress", &pThis->uPciAddress, NIL_PCIBDF);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query 32-bit integer \"PCIAddress\""));

    if (!strcmp(szChipType, "ICH9"))
    {
        /* Newer 2007-ish I/O APIC integrated into ICH southbridges. */
        pThis->enmType         = IOAPICTYPE_ICH9;
        pThis->u8ApicVer       = IOAPIC_VERSION_ICH9;
        pThis->u8IdMask        = 0xff;
        pThis->u8MaxRte        = IOAPIC_MAX_RTE_INDEX;
        pThis->u8LastRteRegIdx = IOAPIC_INDIRECT_INDEX_RTE_END;
        pThis->u64RteWriteMask = IOAPIC_RTE_VALID_WRITE_MASK_ICH9;
        pThis->u64RteReadMask  = IOAPIC_RTE_VALID_READ_MASK_ICH9;
    }
    else if (!strcmp(szChipType, "DMAR"))
    {
        /* Intel DMAR compatible I/O APIC integrated into ICH southbridges. */
        /* Identical to ICH9, but interprets RTEs and MSI address and data fields differently. */
        pThis->enmType         = IOAPICTYPE_DMAR;
        pThis->u8ApicVer       = IOAPIC_VERSION_ICH9;
        pThis->u8IdMask        = 0xff;
        pThis->u8MaxRte        = IOAPIC_MAX_RTE_INDEX;
        pThis->u8LastRteRegIdx = IOAPIC_INDIRECT_INDEX_RTE_END;
        pThis->u64RteWriteMask = IOAPIC_RTE_VALID_WRITE_MASK_DMAR;
        pThis->u64RteReadMask  = IOAPIC_RTE_VALID_READ_MASK_DMAR;
    }
    else if (!strcmp(szChipType, "82093AA"))
    {
        /* Older 1995-ish discrete I/O APIC, used in P6 class systems. */
        pThis->enmType         = IOAPICTYPE_82093AA;
        pThis->u8ApicVer       = IOAPIC_VERSION_82093AA;
        pThis->u8IdMask        = 0x0f;
        pThis->u8MaxRte        = IOAPIC_MAX_RTE_INDEX;
        pThis->u8LastRteRegIdx = IOAPIC_INDIRECT_INDEX_RTE_END;
        pThis->u64RteWriteMask = IOAPIC_RTE_VALID_WRITE_MASK_82093AA;
        pThis->u64RteReadMask  = IOAPIC_RTE_VALID_READ_MASK_82093AA;
    }
    else if (!strcmp(szChipType, "82379AB"))
    {
        /* Even older 1993-ish I/O APIC built into SIO.A, used in EISA and early PCI systems. */
        /* Exact same version and behavior as 82093AA, only the number of RTEs is different. */
        pThis->enmType         = IOAPICTYPE_82379AB;
        pThis->u8ApicVer       = IOAPIC_VERSION_82093AA;
        pThis->u8IdMask        = 0x0f;
        pThis->u8MaxRte        = IOAPIC_REDUCED_MAX_RTE_INDEX;
        pThis->u8LastRteRegIdx = IOAPIC_REDUCED_INDIRECT_INDEX_RTE_END;
        pThis->u64RteWriteMask = IOAPIC_RTE_VALID_WRITE_MASK_82093AA;
        pThis->u64RteReadMask  = IOAPIC_RTE_VALID_READ_MASK_82093AA;
    }
    else
        return PDMDevHlpVMSetError(pDevIns, VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES, RT_SRC_POS,
                                   N_("I/O APIC configuration error: The \"ChipType\" value \"%s\" is unsupported"), szChipType);
    Log2(("IOAPIC: cCpus=%u fRZEnabled=%RTbool szChipType=%s\n", cCpus, pDevIns->fR0Enabled | pDevIns->fRCEnabled, szChipType));

    /*
     * We will use our own critical section for the IOAPIC device.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

# ifndef IOAPIC_WITH_PDM_CRITSECT
    /*
     * Setup the critical section to protect concurrent writes to the RTEs.
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "IOAPIC");
    AssertRCReturn(rc, rc);
# endif

    /*
     * Register the IOAPIC.
     */
    PDMIOAPICREG IoApicReg;
    IoApicReg.u32Version   = PDM_IOAPICREG_VERSION;
    IoApicReg.pfnSetIrq    = ioapicSetIrq;
    IoApicReg.pfnSendMsi   = ioapicSendMsi;
    IoApicReg.pfnSetEoi    = ioapicSetEoi;
    IoApicReg.u32TheEnd    = PDM_IOAPICREG_VERSION;
    rc = PDMDevHlpIoApicRegister(pDevIns, &IoApicReg, &pThisCC->pIoApicHlp);
    AssertRCReturn(rc, rc);
    AssertPtr(pThisCC->pIoApicHlp->pfnApicBusDeliver);
    AssertPtr(pThisCC->pIoApicHlp->pfnLock);
    AssertPtr(pThisCC->pIoApicHlp->pfnUnlock);
    AssertPtr(pThisCC->pIoApicHlp->pfnLockIsOwner);
    AssertPtr(pThisCC->pIoApicHlp->pfnIommuMsiRemap);

    /*
     * Register MMIO region.
     */
    rc = PDMDevHlpMmioCreateAndMap(pDevIns, IOAPIC_MMIO_BASE_PHYSADDR, IOAPIC_MMIO_SIZE, ioapicMmioWrite, ioapicMmioRead,
                                   IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED, "I/O APIC", &pThis->hMmio);
    AssertRCReturn(rc, rc);

    /*
     * Register the saved state.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, IOAPIC_SAVED_STATE_VERSION, sizeof(*pThis), ioapicR3SaveExec, ioapicR3LoadExec);
    AssertRCReturn(rc, rc);

    /*
     * Register debugger info item.
     */
    rc = PDMDevHlpDBGFInfoRegister(pDevIns, "ioapic", "Display IO APIC state.", ioapicR3DbgInfo);
    AssertRCReturn(rc, rc);

    /*
     * Register debugger register access.
     */
    rc = PDMDevHlpDBGFRegRegister(pDevIns, g_aRegDesc);
    AssertRCReturn(rc, rc);

# ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioReadRZ,  STAMTYPE_COUNTER, "RZ/MmioRead",  STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO reads in RZ.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioWriteRZ, STAMTYPE_COUNTER, "RZ/MmioWrite", STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO writes in RZ.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetIrqRZ,    STAMTYPE_COUNTER, "RZ/SetIrq",    STAMUNIT_OCCURENCES, "Number of IOAPIC SetIrq calls in RZ.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetEoiRZ,    STAMTYPE_COUNTER, "RZ/SetEoi",    STAMUNIT_OCCURENCES, "Number of IOAPIC SetEoi calls in RZ.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioReadR3,  STAMTYPE_COUNTER, "R3/MmioRead",  STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO reads in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioWriteR3, STAMTYPE_COUNTER, "R3/MmioWrite", STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO writes in R3.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetIrqR3,    STAMTYPE_COUNTER, "R3/SetIrq",    STAMUNIT_OCCURENCES, "Number of IOAPIC SetIrq calls in R3.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetEoiR3,    STAMTYPE_COUNTER, "R3/SetEoi",    STAMUNIT_OCCURENCES, "Number of IOAPIC SetEoi calls in R3.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRedundantEdgeIntr,   STAMTYPE_COUNTER, "RedundantEdgeIntr",   STAMUNIT_OCCURENCES, "Number of redundant edge-triggered interrupts (no IRR change).");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRedundantLevelIntr,  STAMTYPE_COUNTER, "RedundantLevelIntr",  STAMUNIT_OCCURENCES, "Number of redundant level-triggered interrupts (no IRR change).");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSuppressedLevelIntr, STAMTYPE_COUNTER, "SuppressedLevelIntr", STAMUNIT_OCCURENCES, "Number of suppressed level-triggered interrupts by remote IRR.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIommuRemappedIntr,  STAMTYPE_COUNTER, "Iommu/RemappedIntr",  STAMUNIT_OCCURENCES, "Number of interrupts remapped by the IOMMU.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIommuRemappedMsi,   STAMTYPE_COUNTER, "Iommu/RemappedMsi",   STAMUNIT_OCCURENCES, "Number of MSIs remapped by the IOMMU.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIommuDiscardedIntr, STAMTYPE_COUNTER, "Iommu/DiscardedIntr", STAMUNIT_OCCURENCES, "Number of interrupts discarded by the IOMMU.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIommuDiscardedMsi,  STAMTYPE_COUNTER, "Iommu/DiscardedMsi",  STAMUNIT_OCCURENCES, "Number of MSIs discarded by the IOMMU.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetRteContention, STAMTYPE_COUNTER, "CritSect/ContentionSetRte", STAMUNIT_OCCURENCES, "Number of times the critsect is busy during RTE writes causing trips to R3.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatLevelIrqSent, STAMTYPE_COUNTER, "LevelIntr/Sent", STAMUNIT_OCCURENCES, "Number of level-triggered interrupts sent to the local APIC(s).");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatEoiReceived,  STAMTYPE_COUNTER, "LevelIntr/Recv", STAMUNIT_OCCURENCES, "Number of EOIs received for level-triggered interrupts from the local APIC(s).");

    for (size_t i = 0; i < RT_ELEMENTS(pThis->aStatLevelAct); ++i)
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatLevelAct[i], STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Time spent in the level active state", "IntPending/%02x", i);
# endif
    for (size_t i = 0; i < RT_ELEMENTS(pThis->aStatVectors); i++)
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStatVectors[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "Number of ioapicSendMsi/pfnApicBusDeliver calls for the vector.", "Vectors/%02x", i);

    /*
     * Init. the device state.
     */
    LogRel(("IOAPIC: Version=%d.%d ChipType=%s\n", pThis->u8ApicVer >> 4, pThis->u8ApicVer & 0x0f, szChipType));
    ioapicR3Reset(pDevIns);

    return VINF_SUCCESS;
}

#else /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) ioapicRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PIOAPIC     pThis   = PDMDEVINS_2_DATA(pDevIns, PIOAPIC);
    PIOAPICCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOAPICCC);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    PDMIOAPICREG IoApicReg;
    IoApicReg.u32Version   = PDM_IOAPICREG_VERSION;
    IoApicReg.pfnSetIrq    = ioapicSetIrq;
    IoApicReg.pfnSendMsi   = ioapicSendMsi;
    IoApicReg.pfnSetEoi    = ioapicSetEoi;
    IoApicReg.u32TheEnd    = PDM_IOAPICREG_VERSION;
    rc = PDMDevHlpIoApicSetUpContext(pDevIns, &IoApicReg, &pThisCC->pIoApicHlp);
    AssertRCReturn(rc, rc);
    AssertPtr(pThisCC->pIoApicHlp->pfnApicBusDeliver);
    AssertPtr(pThisCC->pIoApicHlp->pfnLock);
    AssertPtr(pThisCC->pIoApicHlp->pfnUnlock);
    AssertPtr(pThisCC->pIoApicHlp->pfnLockIsOwner);
    AssertPtr(pThisCC->pIoApicHlp->pfnIommuMsiRemap);

    rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, ioapicMmioWrite, ioapicMmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * IO APIC device registration structure.
 */
const PDMDEVREG g_DeviceIOAPIC =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "ioapic",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE
                                    | PDM_DEVREG_FLAGS_REQUIRE_R0 | PDM_DEVREG_FLAGS_REQUIRE_RC,
    /* .fClass = */                 PDM_DEVREG_CLASS_PIC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(IOAPIC),
    /* .cbInstanceCC = */           sizeof(IOAPICCC),
    /* .cbInstanceRC = */           sizeof(IOAPICRC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "I/O Advanced Programmable Interrupt Controller (IO-APIC) Device",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           ioapicR3Construct,
    /* .pfnDestruct = */            ioapicR3Destruct,
    /* .pfnRelocate = */            ioapicR3Relocate,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               ioapicR3Reset,
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
    /* .pfnConstruct = */           ioapicRZConstruct,
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
    /* .pfnConstruct = */           ioapicRZConstruct,
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


#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

