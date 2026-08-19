/* $Id: APICInternal.h 115073 2026-08-19 10:00:47Z alexander.eichner@oracle.com $ */
/** @file
 * APIC - Advanced Programmable Interrupt Controller, Internal header.
 */

/*
 * Copyright (C) 2016-2026 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_APICInternal_h
#define VMM_INCLUDED_SRC_include_APICInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/apic.h>
#include <VBox/sup.h>
#include <VBox/vmm/pdmapic.h>
#include <VBox/vmm/stam.h>

/** @defgroup grp_apic_int       Internal
 * @ingroup grp_apic
 * @internal
 * @{
 */

#ifdef VBOX_INCLUDED_vmm_pdmapic_h
/** The VirtualBox APIC backend table. */
extern const PDMAPICBACKEND g_ApicBackend;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_LINUX)
/** The Hyper-V/KVM APIC backend. */
extern const PDMAPICBACKEND g_ApicNemBackend;
# endif
#endif

/** The APIC hardware version we are emulating. */
#define XAPIC_HARDWARE_VERSION               XAPIC_HARDWARE_VERSION_P4

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
#define XAPIC_SVR_VALID                      XAPIC_SVR_VALID_P4
#define XAPIC_ID_BROADCAST_MASK              XAPIC_ID_BROADCAST_MASK_P4
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif

#define VMCPU_TO_XAPICPAGE(a_pVCpu)          ((PXAPICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))
#define VMCPU_TO_CXAPICPAGE(a_pVCpu)         ((PCXAPICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))

#define VMCPU_TO_X2APICPAGE(a_pVCpu)         ((PX2APICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))
#define VMCPU_TO_CX2APICPAGE(a_pVCpu)        ((PCX2APICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))

#define VMCPU_TO_APICCPU(a_pVCpu)            (&(a_pVCpu)->apic.s)
#define VM_TO_APIC(a_pVM)                    (&(a_pVM)->apic.s)
#define VM_TO_APICDEV(a_pVM)                 CTX_SUFF(VM_TO_APIC(a_pVM)->pApicDev)
#ifdef IN_RING3
# define VMCPU_TO_DEVINS(a_pVCpu)           ((a_pVCpu)->pVMR3->apic.s.pDevInsR3)
#elif defined(IN_RING0)
# define VMCPU_TO_DEVINS(a_pVCpu)           ((a_pVCpu)->pGVM->apicr0.s.pDevInsR0)
#endif

#define APICCPU_TO_XAPICPAGE(a_ApicCpu)      ((PXAPICPAGE)(CTX_SUFF((a_ApicCpu)->pvApicPage)))
#define APICCPU_TO_CXAPICPAGE(a_ApicCpu)     ((PCXAPICPAGE)(CTX_SUFF((a_ApicCpu)->pvApicPage)))

/**
 * APIC MSR access error.
 * @note The values must match the array indices in apicMsrAccessError().
 */
typedef enum APICMSRACCESS
{
    /** MSR read while not in x2APIC. */
    APICMSRACCESS_INVALID_READ_MODE = 0,
    /** MSR write while not in x2APIC. */
    APICMSRACCESS_INVALID_WRITE_MODE,
    /** MSR read for a reserved/unknown/invalid MSR. */
    APICMSRACCESS_READ_RSVD_OR_UNKNOWN,
    /** MSR write for a reserved/unknown/invalid MSR. */
    APICMSRACCESS_WRITE_RSVD_OR_UNKNOWN,
    /** MSR read for a write-only MSR. */
    APICMSRACCESS_READ_WRITE_ONLY,
    /** MSR write for a read-only MSR. */
    APICMSRACCESS_WRITE_READ_ONLY,
    /** MSR read to reserved bits. */
    APICMSRACCESS_READ_RSVD_BITS,
    /** MSR write to reserved bits. */
    APICMSRACCESS_WRITE_RSVD_BITS,
    /** MSR write with invalid value. */
    APICMSRACCESS_WRITE_INVALID,
    /** MSR write disallowed due to incompatible config. */
    APICMSRACCESS_WRITE_DISALLOWED_CONFIG,
    /** MSR read disallowed due to incompatible config. */
    APICMSRACCESS_READ_DISALLOWED_CONFIG,
    /** Count of enum members (don't use). */
    APICMSRACCESS_COUNT
} APICMSRACCESS;


/** @def APIC_CACHE_LINE_SIZE
 * Padding (in bytes) for aligning data in different cache lines. Present
 * generation x86 CPUs use 64-byte cache lines[1]. However, Intel NetBurst
 * architecture supposedly uses 128-byte cache lines[2]. Since 128 is a
 * multiple of 64, we use the larger one here.
 *
 * [1] - Intel spec "Table 11-1. Characteristics of the Caches, TLBs, Store
 *       Buffer, and Write Combining Buffer in Intel 64 and IA-32 Processors"
 * [2] - Intel spec. 8.10.6.7 "Place Locks and Semaphores in Aligned, 128-Byte
 *       Blocks of Memory".
 */
#define APIC_CACHE_LINE_SIZE              128

/**
 * APIC Pending-Interrupt Bitmap (PIB).
 */
typedef struct APICPIB
{
    uint64_t volatile au64VectorBitmap[4];
    uint32_t volatile fOutstandingNotification;
    uint8_t           au8Reserved[APIC_CACHE_LINE_SIZE - sizeof(uint32_t) - (sizeof(uint64_t) * 4)];
} APICPIB;
AssertCompileMemberOffset(APICPIB, fOutstandingNotification, 256 / 8);
AssertCompileSize(APICPIB, APIC_CACHE_LINE_SIZE);
/** Pointer to a pending-interrupt bitmap. */
typedef APICPIB *PAPICPIB;
/** Pointer to a const pending-interrupt bitmap. */
typedef const APICPIB *PCAPICPIB;

/**
 * APIC PDM instance data (per-VM).
 */
typedef struct APICDEV
{
    /** The MMIO handle. */
    IOMMMIOHANDLE               hMmio;
} APICDEV;
/** Pointer to an APIC device. */
typedef APICDEV *PAPICDEV;
/** Pointer to a const APIC device. */
typedef APICDEV const *PCAPICDEV;


/**
 * The APIC GVM instance data.
 */
typedef struct APICR0PERVM
{
    /** The ring-0 device instance. */
    PPDMDEVINSR0                pDevInsR0;
} APICR0PERVM;


/**
 * APIC VM Instance data.
 */
typedef struct APIC
{
    /** The ring-3 device instance. */
    PPDMDEVINSR3                pDevInsR3;

    /** @name The APIC pending-interrupt bitmap (PIB).
     * @{ */
    /** The host-context physical address of the PIB. */
    RTHCPHYS                    HCPhysApicPib;
    /** The ring-0 memory object of the PIB. */
    RTR0MEMOBJ                  hMemObjApicPibR0;
    /** The ring-3 mapping of the memory object of the PIB. */
    RTR0MEMOBJ                  hMapObjApicPibR0;
    /** The APIC PIB virtual address - R0 ptr. */
    R0PTRTYPE(void *)           pvApicPibR0;
    /** The APIC PIB virtual address - R3 ptr. */
    R3PTRTYPE(void *)           pvApicPibR3;
    /** The size of the page in bytes. */
    uint32_t                    cbApicPib;
    /** @} */

    /** @name Other miscellaneous data.
     * @{ */
    /** Whether full APIC register virtualization is enabled. */
    bool                        fVirtApicRegsEnabled;
    /** Whether posted-interrupt processing is enabled. */
    bool                        fPostedIntrsEnabled;
    /** Whether the SVM AVIC feature is enabled. */
    bool                        fAvicEnabled;
    /** Whether TSC-deadline timer mode is supported for the guest. */
    bool                        fSupportsTscDeadline;
    /** Whether this VM has an IO-APIC. */
    bool                        fIoApicPresent;
    /** Whether R0 is enabled or not (applies to MSR handling as well). */
    bool                        fR0Enabled;
    /** Whether RC is enabled or not (applies to MSR handling as well). */
    bool                        fRCEnabled;
    /** Whether Hyper-V x2APIC compatibility mode is enabled. */
    bool                        fHyperVCompatMode;
    /** Enable horrible macOS workaround where the ID register has the value
     * shifted up 24 bits to be compatible with buggy code in
     * i386_init.c/vstart().  Only applied if we're in typical macOS 64-bit
     * kernel load area and macOS kernel selector value (8), as we must not ever
     * apply this to the EFI code. */
    bool                        fMacOSWorkaround;
    /** Alignment padding. */
    bool                        afPadding[3];
    /** The max supported APIC mode from CFGM.  */
    PDMAPICMODE                 enmMaxMode;
    /** @} */
} APIC;
/** Pointer to APIC VM instance data. */
typedef APIC *PAPIC;
/** Pointer to const APIC VM instance data. */
typedef APIC const *PCAPIC;
AssertCompileMemberAlignment(APIC, cbApicPib, 8);
AssertCompileSizeAlignment(APIC, 8);

/**
 * APIC VMCPU Instance data.
 */
typedef struct APICCPU
{
    /** @name The APIC page.
     * @{ */
    /** The host-context physical address of the page. */
    RTHCPHYS                    HCPhysApicPage;
    /** The ring-0 memory object of the page. */
    RTR0MEMOBJ                  hMemObjApicPageR0;
    /** The ring-3 mapping of the memory object of the page. */
    RTR0MEMOBJ                  hMapObjApicPageR0;
    /** The APIC page virtual address - R0 ptr. */
    R0PTRTYPE(void *)           pvApicPageR0;
    /** The APIC page virtual address - R3 ptr. */
    R3PTRTYPE(void *)           pvApicPageR3;
    /** The size of the page in bytes. */
    uint32_t                    cbApicPage;
    /** @} */

    /** @name Auxiliary state.
     * @{ */
    /** The error status register's internal state. */
    uint32_t                    uEsrInternal;
    /** The APIC base MSR.*/
    uint64_t volatile           uApicBaseMsr;
    /** @} */

    /** @name The pending-interrupt bitmaps (PIB).
     * @{ */
    /** The host-context physical address of the page. */
    RTHCPHYS                    HCPhysApicPib;
    /** The APIC PIB virtual address - R0 ptr. */
    R0PTRTYPE(void *)           pvApicPibR0;
    /** The APIC PIB virtual address - R3 ptr. */
    R3PTRTYPE(void *)           pvApicPibR3;
    /** The APIC PIB for level-sensitive interrupts. */
    APICPIB                     ApicPibLevel;
    /** @} */

    /** @name Other miscellaneous data.
     * @{ */
    /** Whether the LINT0 interrupt line is active. */
    bool volatile               fActiveLint0;
    /** Whether the LINT1 interrupt line is active. */
    bool volatile               fActiveLint1;
    /** Alignment padding. */
    uint8_t                     auAlignment2[6];
    /** The source tags corresponding to each interrupt vector (debugging). */
    uint32_t                    auSrcTags[256];
    /** @} */

    /** @name The APIC timer.
     * @{ */
    /** The timer. */
    TMTIMERHANDLE               hTimer;
    /** The time stamp when the timer was initialized.
     * @note Access protected by the timer critsect.  */
    uint64_t                    u64TimerInitial;
    /** Cache of timer initial count of the frequency hint to TM. */
    uint32_t                    uHintedTimerInitialCount;
    /** Cache of timer shift of the frequency hint to TM. */
    uint32_t                    uHintedTimerShift;
    /** The timer description. */
    char                        szTimerDesc[16];
    /** @} */

    /** @name Log Max counters
     * @{ */
    uint32_t                    cLogMaxAccessError;
    uint32_t                    cLogMaxSetApicBaseAddr;
    uint32_t                    cLogMaxGetApicBaseAddr;
    uint32_t                    uAlignment4;
    /** @} */

    /** @name APIC statistics.
     * @{ */
#ifdef VBOX_WITH_STATISTICS
    /** Number of MMIO reads in RZ. */
    STAMCOUNTER                 StatMmioReadRZ;
    /** Number of MMIO reads in R3. */
    STAMCOUNTER                 StatMmioReadR3;

    /** Number of MMIO writes in RZ. */
    STAMCOUNTER                 StatMmioWriteRZ;
    /** Number of MMIO writes in R3. */
    STAMCOUNTER                 StatMmioWriteR3;

    /** Number of MSR reads in RZ. */
    STAMCOUNTER                 StatMsrReadRZ;
    /** Number of MSR reads in R3. */
    STAMCOUNTER                 StatMsrReadR3;

    /** Number of MSR writes in RZ. */
    STAMCOUNTER                 StatMsrWriteRZ;
    /** Number of MSR writes in R3. */
    STAMCOUNTER                 StatMsrWriteR3;

    /** Profiling of APICUpdatePendingInterrupts().  */
    STAMPROFILE                 StatUpdatePendingIntrs;
    /** Profiling of apicPostInterrupt().  */
    STAMPROFILE                 StatPostIntr;
    /** Number of times an interrupt is already pending in
     *  apicPostInterrupts().*/
    STAMCOUNTER                 StatPostIntrAlreadyPending;
    /** Number of times the timer callback is invoked. */
    STAMCOUNTER                 StatTimerCallback;
    /** Number of times the TPR is written. */
    STAMCOUNTER                 StatTprWrite;
    /** Number of times the TPR is read. */
    STAMCOUNTER                 StatTprRead;
    /** Number of times the EOI is written. */
    STAMCOUNTER                 StatEoiWrite;
    /** Number of times the EOI is written in the fast path. */
    STAMCOUNTER                 StatEoiWriteFast;
    /** Number of times TPR masks an interrupt in apicGetInterrupt(). */
    STAMCOUNTER                 StatMaskedByTpr;
    /** Number of times PPR masks an interrupt in apicGetInterrupt(). */
    STAMCOUNTER                 StatMaskedByPpr;
    /** Number of times the timer ICR is written. */
    STAMCOUNTER                 StatTimerIcrWrite;
    /** Number of times the ICR Lo (send IPI) is written. */
    STAMCOUNTER                 StatIcrLoWrite;
    /** Number of times the ICR Hi is written. */
    STAMCOUNTER                 StatIcrHiWrite;
    /** Number of times the full ICR (x2APIC send IPI) is written. */
    STAMCOUNTER                 StatIcrFullWrite;
    /** Number of times the DCR is written. */
    STAMCOUNTER                 StatDcrWrite;
    /** Number of times the DFR is written. */
    STAMCOUNTER                 StatDfrWrite;
    /** Number of times the LDR is written. */
    STAMCOUNTER                 StatLdrWrite;
    /** Number of times the APIC-ID MSR is read. */
    STAMCOUNTER                 StatIdMsrRead;
    /** Number of times the LVT timer is written. */
    STAMCOUNTER                 StatLvtTimerWrite;
#endif
    /** Number of apicPostInterrupt() calls. */
    STAMCOUNTER                 StatPostIntrCnt;
    /** Number of interrupts broken down by vector. */
    STAMCOUNTER                 aStatVectors[256];
    /** @} */
} APICCPU;
/** Pointer to APIC VMCPU instance data. */
typedef APICCPU *PAPICCPU;
/** Pointer to a const APIC VMCPU instance data. */
typedef APICCPU const *PCAPICCPU;
AssertCompileMemberAlignment(APICCPU, uApicBaseMsr, 8);

void                          apicHintTimerFreq(PPDMDEVINS pDevIns, PAPICCPU pApicCpu, uint32_t uInitialCount, uint8_t uTimerShift);

DECLCALLBACK(VBOXSTRICTRC)    apicReadMmio(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb);
DECLCALLBACK(VBOXSTRICTRC)    apicWriteMmio(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb);
DECLCALLBACK(bool)            apicPostInterrupt(PVMCPUCC pVCpu, uint8_t uVector, XAPICTRIGGERMODE enmTriggerMode, bool fAutoEoi,
                                                uint32_t uSrcTag);
#ifdef IN_RING3
DECLCALLBACK(int)             apicR3SetHvCompatMode(PVM pVM, bool fHyperVCompatMode);
#endif
void                          apicStartTimer(PVMCPUCC pVCpu, uint32_t uInitialCount);
void                          apicClearInterruptFF(PVMCPUCC pVCpu, PDMAPICIRQ enmType);
void                          apicResetCpu(PVMCPUCC pVCpu, bool fResetApicBaseMsr);

DECLCALLBACK(int)             apicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg);
DECLCALLBACK(int)             apicR3Destruct(PPDMDEVINS pDevIns);
DECLCALLBACK(void)            apicR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta);
DECLCALLBACK(void)            apicR3Reset(PPDMDEVINS pDevIns);
DECLCALLBACK(int)             apicR3InitComplete(PPDMDEVINS pDevIns);

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_APICInternal_h */

